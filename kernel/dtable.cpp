/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2011, 2012, 2013, 2014, 2015.

    This file is part of Sortix.

    Sortix is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version.

    Sortix is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
    details.

    You should have received a copy of the GNU General Public License along with
    Sortix. If not, see <http://www.gnu.org/licenses/>.

    dtable.cpp
    Table of file descriptors.

*******************************************************************************/

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

DescriptorTable::DescriptorTable()
{
	dtablelock = KTHREAD_MUTEX_INITIALIZER;
	entries = NULL;
	numentries = 0;
	first_not_taken = 0;
}

DescriptorTable::~DescriptorTable()
{
	Reset();
}

bool DescriptorTable::IsGoodEntry(int i)
{
	bool ret = 0 <= i && i < numentries && entries[i].desc;
	return ret;
}

Ref<DescriptorTable> DescriptorTable::Fork()
{
	ScopedLock lock(&dtablelock);
	Ref<DescriptorTable> ret(new DescriptorTable);
	if ( !ret ) { return Ref<DescriptorTable>(NULL); }
	ret->entries = new dtableent_t[numentries];
	if ( !ret->entries ) { return Ref<DescriptorTable>(NULL); }
	ret->first_not_taken = 0;
	for ( ret->numentries = 0; ret->numentries < numentries; ret->numentries++ )
	{
		int i = ret->numentries;
		if ( entries[i].desc && !(entries[i].flags & FD_CLOFORK) )
		{
			ret->entries[i] = entries[i];
			if ( i == ret->first_not_taken && i != INT_MAX )
				ret->first_not_taken++;
		}
		else
			/* Already set to NULL in dtableent_t::desc constructor. */{}
	}
	return ret;
}

Ref<Descriptor> DescriptorTable::Get(int index)
{
	ScopedLock lock(&dtablelock);
	if ( !IsGoodEntry(index) ) { errno = EBADF; return Ref<Descriptor>(NULL); }
	return entries[index].desc;
}

bool DescriptorTable::Enlargen(int atleast)
{
	if ( numentries == INT_MAX ) { errno = EOVERFLOW; return -1; }
	int newnumentries = INT_MAX - numentries < numentries ?
	                    INT_MAX :
                        numentries ? 2 * numentries : 8;
	if ( newnumentries < atleast )
		newnumentries = atleast;
	dtableent_t* newentries = new dtableent_t[newnumentries];
	if ( !newentries )
		return false;
	for ( int i = 0; i < numentries; i++ )
		newentries[i].desc = entries[i].desc,
		newentries[i].flags = entries[i].flags;
	for ( int i = numentries; i < newnumentries; i++ )
		/* newentries[i].desc is set in dtableent_t::desc constructor */
		newentries[i].flags = 0;
	delete[] entries; entries = newentries;
	numentries = newnumentries;
	return true;
}

int DescriptorTable::AllocateInternal(Ref<Descriptor> desc, int flags,
                                      int min_index)
{
	// dtablelock is held.
	if ( flags & ~__FD_ALLOWED_FLAGS )
		return errno = EINVAL, -1;
	if ( min_index < 0 )
		return errno = EINVAL, -1;
	if ( min_index < first_not_taken )
		min_index = first_not_taken;
	for ( int i = min_index; i < numentries; i++ )
	{
		if ( !entries[i].desc )
		{
			entries[i].desc = desc;
			entries[i].flags = flags;
			return i;
		}
		if ( i == first_not_taken && i != INT_MAX )
			first_not_taken++;
	}
	int oldnumentries = numentries;
	if ( !Enlargen(min_index) )
		return -1;
	int i = oldnumentries++;
	entries[i].desc = desc;
	entries[i].flags = flags;
	return i;
}

int DescriptorTable::Allocate(Ref<Descriptor> desc, int flags, int min_index)
{
	ScopedLock lock(&dtablelock);
	return AllocateInternal(desc, flags, min_index);
}

int DescriptorTable::Allocate(int src_index, int flags, int min_index)
{
	ScopedLock lock(&dtablelock);
	if ( !IsGoodEntry(src_index) )
		return errno = EBADF, -1;
	return AllocateInternal(entries[src_index].desc, flags, min_index);
}

int DescriptorTable::Copy(int from, int to, int flags)
{
	if ( flags & ~__FD_ALLOWED_FLAGS )
		return errno = EINVAL, -1;
	ScopedLock lock(&dtablelock);
	if ( from < 0 || to < 0 )
		return errno = EINVAL, -1;
	if ( !(from < numentries) )
		return errno = EBADF, -1;
	if ( !entries[from].desc )
		return errno = EBADF, -1;
	if ( from == to )
		return errno = EINVAL, -1;
	while ( !(to < numentries) )
		if ( !Enlargen(to+1) )
			return -1;
	if ( entries[to].desc != entries[from].desc )
	{
		if ( entries[to].desc )
			/* TODO: Should this be synced or otherwise properly closed? */{}
		entries[to].desc = entries[from].desc;
		if ( to == first_not_taken && to != INT_MAX )
			first_not_taken++;
	}
	entries[to].flags = flags;
	return to;
}

Ref<Descriptor> DescriptorTable::FreeKeepInternal(int index)
{
	if ( !IsGoodEntry(index) ) { errno = EBADF; return Ref<Descriptor>(NULL); }
	Ref<Descriptor> ret = entries[index].desc;
	entries[index].desc.Reset();
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
	for ( int i = 0; i < numentries; i++ )
	{
		if ( !entries[i].desc )
			continue;
		if ( !(entries[i].flags & FD_CLOEXEC) )
			continue;
		entries[i].desc.Reset();
		if ( i < first_not_taken )
			first_not_taken = i;
	}
}

void DescriptorTable::Reset()
{
	ScopedLock lock(&dtablelock);
	numentries = 0;
	delete[] entries;
	entries = NULL;
	first_not_taken = 0;
}

bool DescriptorTable::SetFlags(int index, int flags)
{
	if ( flags & ~__FD_ALLOWED_FLAGS )
		return errno = EINVAL, -1;
	ScopedLock lock(&dtablelock);
	if ( !IsGoodEntry(index) ) { errno = EBADF; return false; }
	entries[index].flags = flags;
	return true;
}

int DescriptorTable::GetFlags(int index)
{
	ScopedLock lock(&dtablelock);
	if ( !IsGoodEntry(index) ) { errno = EBADF; return -1; }
	return entries[index].flags;
}

int DescriptorTable::Previous(int index)
{
	ScopedLock lock(&dtablelock);

	if ( index < 0 )
		index = numentries;

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

	if ( numentries <= index )
		return errno = EBADF, -1;

	do index++;
	while ( index < numentries && !IsGoodEntry(index) );

	if ( numentries <= index )
		return errno = EBADF, -1;

	return index;
}

int DescriptorTable::CloseFrom(int index)
{
	if ( index < 0 )
		return errno = EBADF, -1;

	ScopedLock lock(&dtablelock);

	bool any = false;

	while ( index < numentries )
	{
		if ( !IsGoodEntry(index) )
			continue;
		FreeKeepInternal(index);
		any = true;
	}

	return any ? 0 : (errno = EBADF, -1);
}

} // namespace Sortix
