/*
 * Copyright (c) 2011-2016, 2021, 2022 Jonas 'Sortie' Termansen.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * dtable.cpp
 * Table of file descriptors.
 */

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include <sortix/fcntl.h>

#include <sortix/kernel/descriptor.h>
#include <sortix/kernel/dtable.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/process.h>
#include <sortix/kernel/refcount.h>

namespace Sortix {

struct DescriptorEntry
{
	Ref<Descriptor> desc;
	int flags;
};

DescriptorTable::DescriptorTable()
{
	dtablelock = KTHREAD_MUTEX_INITIALIZER;
	entries = NULL;
	entries_used = 0;
	entries_length = 0;
	reserved_count = 0;
	first_not_taken = 0;
}

DescriptorTable::~DescriptorTable()
{
	for ( int i = 0; i < entries_length; i++ )
		if ( entries[i].desc )
			entries[i].desc.Reset();
	delete[] entries;
}

bool DescriptorTable::IsGoodEntry(int i) // dtablelock locked
{
	return 0 <= i && i < entries_length && entries[i].desc;
}

Ref<DescriptorTable> DescriptorTable::Fork()
{
	ScopedLock lock(&dtablelock);
	Ref<DescriptorTable> ret(new DescriptorTable);
	if ( !ret )
		return Ref<DescriptorTable>(NULL);
	ret->entries = new DescriptorEntry[entries_length];
	if ( !ret->entries )
		return Ref<DescriptorTable>(NULL);
	// Copy all the file descriptors except ones closed on fork.
	ret->entries_length = entries_length;
	for ( int i = 0; i < entries_length; i++ )
	{
		if ( !entries[i].desc || entries[i].flags & FD_CLOFORK )
		{
			//ret->entries[i].desc = NULL; // Constructor did this already.
			ret->entries[i].flags = 0;
			continue;
		}
		ret->entries[i] = entries[i];
		ret->entries_used++;
		if ( ret->first_not_taken == i )
			ret->first_not_taken = i + 1;
	}
	return ret;
}

Ref<Descriptor> DescriptorTable::Get(int index)
{
	ScopedLock lock(&dtablelock);
	if ( !IsGoodEntry(index) )
		return errno = EBADF, Ref<Descriptor>(NULL);
	return entries[index].desc;
}

// Expands the table to have at least need_entries entries and at least
// need_unused unused entries.
bool DescriptorTable::Enlargen(int need_entries,
                               int need_unused) // dtablelock taken
{
	// Figure out how many entries are needed to satisfy the need for unused
	// entries and grow the entries to that size if larger than need_entries.
	if ( __builtin_add_overflow(need_unused, entries_used, &need_unused) ||
	     __builtin_add_overflow(need_unused, reserved_count, &need_unused) )
		return errno = EMFILE, false;
	if ( need_entries < need_unused )
		need_entries = need_unused;
	if ( need_entries <= entries_length )
		return true;
	if ( entries_length == INT_MAX )
		return errno = EMFILE, false;
	// At least double the size of the table but maybe more entries are needed.
	int new_entries_length = 8;
	if ( entries_length &&
	     __builtin_mul_overflow(2, entries_length, &new_entries_length) )
		new_entries_length = INT_MAX;
	if ( new_entries_length < need_entries )
		new_entries_length = need_entries;
	DescriptorEntry* new_entries = new DescriptorEntry[new_entries_length];
	if ( !new_entries )
		return false;
	for ( int i = 0; i < entries_length; i++ )
	{
		new_entries[i] = entries[i];
		entries[i].desc.Reset();
	}
	for ( int i = entries_length; i < new_entries_length; i++ )
	{
		//new_entries[i].desc = NULL; // Constructor did this already.
		new_entries[i].flags = 0;
	}
	delete[] entries;
	entries = new_entries;
	entries_length = new_entries_length;
	return true;
}

bool DescriptorTable::Reserve(int count, int* reservation)
{
	assert(0 <= count);
	if ( !Enlargen(0, count) )
		return false;
	assert(reserved_count <= entries_length - entries_used);
	assert(reserved_count + count <= entries_length - entries_used);
	reserved_count += count;
	*reservation = count;
	return true;
}

void DescriptorTable::Unreserve(int* reservation)
{
	assert(0 <= *reservation);
	assert(*reservation <= reserved_count);
	reserved_count -= *reservation;
	*reservation = 0;
}

int DescriptorTable::AllocateInternal(Ref<Descriptor> desc,
                                      int flags,
                                      int min_index,
                                      int* reservation) // dtablelock locked
{
	assert(!reservation || 1 <= *reservation);
	assert(!reservation || !min_index);
	if ( flags & ~__FD_ALLOWED_FLAGS )
		return errno = EINVAL, -1;
	if ( min_index < 0 || min_index == INT_MAX )
		return errno = EINVAL, -1;
	if ( min_index < first_not_taken )
		min_index = first_not_taken;
	int first_available = min_index;
	for ( int i = min_index; i < entries_length; i++ )
	{
		if ( entries[i].desc )
		{
			if ( first_available == i )
				first_available = i + 1;
			if ( first_not_taken == i )
				first_not_taken = i + 1;
			continue;
		}
		if ( !reservation && entries_length - reserved_count <= i )
			break;
		entries[i].desc = desc;
		entries[i].flags = flags;
		entries_used++;
		if ( reservation )
		{
			assert(reserved_count);
			(*reservation)--;
			reserved_count--;
		}
		if ( first_not_taken == i )
			first_not_taken = i + 1;
		return i;
	}
	assert(!reservation);
	if ( first_available == INT_MAX )
		return errno = EMFILE, -1;
	if ( !Enlargen(first_available + 1, 1) )
		return -1;
	entries[first_available].desc = desc;
	entries[first_available].flags = flags;
	entries_used++;
	if ( first_not_taken == first_available )
		first_not_taken = first_available + 1;
	return first_available;
}

int DescriptorTable::Allocate(Ref<Descriptor> desc, int flags, int min_index,
                              int* reservation)
{
	ScopedLock lock(&dtablelock);
	return AllocateInternal(desc, flags, min_index, reservation);
}

int DescriptorTable::Allocate(int src_index, int flags, int min_index,
                              int* reservation)
{
	ScopedLock lock(&dtablelock);
	if ( !IsGoodEntry(src_index) )
		return errno = EBADF, -1;
	return AllocateInternal(entries[src_index].desc, flags, min_index,
	                        reservation);
}

int DescriptorTable::Copy(int from, int to, int flags)
{
	if ( flags & ~__FD_ALLOWED_FLAGS )
		return errno = EINVAL, -1;
	ScopedLock lock(&dtablelock);
	if ( !IsGoodEntry(from) || to < 0 )
		return errno = EBADF, -1;
	if ( from == to )
		return errno = EINVAL, -1;
	if ( to == INT_MAX )
		return errno = EBADF, -1;
	if ( !IsGoodEntry(to) && !Enlargen(to + 1, 1) )
		return -1;
	if ( entries[to].desc != entries[from].desc )
	{
		if ( !entries[to].desc )
			entries_used++;
		entries[to].desc = entries[from].desc;
	}
	entries[to].flags = flags;
	if ( first_not_taken == to )
		first_not_taken = to + 1;
	return to;
}

Ref<Descriptor> DescriptorTable::FreeKeepInternal(int index)
// dtablelock locked
{
	if ( !IsGoodEntry(index) )
		return errno = EBADF, Ref<Descriptor>(NULL);
	Ref<Descriptor> ret = entries[index].desc;
	entries[index].desc.Reset();
	entries[index].flags = 0;
	entries_used--;
	if ( index < first_not_taken )
		first_not_taken = index;
	return ret;
}

Ref<Descriptor> DescriptorTable::FreeKeep(int index)
{
	ScopedLock lock(&dtablelock);
	return FreeKeepInternal(index);
}

void DescriptorTable::Free(int index)
{
	FreeKeep(index);
}

void DescriptorTable::OnExecute()
{
	ScopedLock lock(&dtablelock);
	for ( int i = 0; i < entries_length; i++ )
	{
		if ( !entries[i].desc )
			continue;
		if ( !(entries[i].flags & FD_CLOEXEC) )
			continue;
		entries[i].desc.Reset();
		if ( i < first_not_taken )
			first_not_taken = i;
		entries_used--;
	}
}

bool DescriptorTable::SetFlags(int index, int flags)
{
	if ( flags & ~__FD_ALLOWED_FLAGS )
		return errno = EINVAL, false;
	ScopedLock lock(&dtablelock);
	if ( !IsGoodEntry(index) )
		return errno = EBADF, false;
	entries[index].flags = flags;
	return true;
}

int DescriptorTable::GetFlags(int index)
{
	ScopedLock lock(&dtablelock);
	if ( !IsGoodEntry(index) )
		return errno = EBADF, -1;
	return entries[index].flags;
}

int DescriptorTable::Previous(int index)
{
	ScopedLock lock(&dtablelock);
	if ( index < 0 )
		index = entries_length;
	do index--;
	while ( 0 <= index && !IsGoodEntry(index) );
	if ( index < 0 )
		return errno = EBADF, -1;
	return index;
}

int DescriptorTable::Next(int index)
{
	ScopedLock lock(&dtablelock);
	if ( index < 0 )
		index = -1;
	if ( entries_length <= index )
		return errno = EBADF, -1;
	do index++;
	while ( index < entries_length && !IsGoodEntry(index) );
	if ( entries_length <= index )
		return errno = EBADF, -1;
	return index;
}

int DescriptorTable::CloseFrom(int index)
{
	if ( index < 0 )
		return errno = EBADF, -1;
	ScopedLock lock(&dtablelock);
	bool any = false;
	for ( ; index < entries_length; index++ )
	{
		if ( !IsGoodEntry(index) )
			continue;
		FreeKeepInternal(index);
		any = true;
	}
	return any ? 0 : (errno = EBADF, -1);
}

} // namespace Sortix
