/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2012, 2014.

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

    copy.cpp
    The context for io operations: who made it, how should data be copied, etc.

*******************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <sortix/mman.h>

#include <sortix/kernel/copy.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/process.h>
#include <sortix/kernel/segment.h>
#include <sortix/kernel/string.h>

// NOTE: The copy-from-user-space functions are specially made such that they
//       guarantee the relevant memory exist and are unchanged during the copy
//       operation. They do not protect against another thread concurrently
//       modifying the memory, but the functions cope in that case and just
//       transfer whatever memory they see in that case, they don't malfunction.

// TODO: We could check the page tables for extra safely.

namespace Sortix {

static bool IsInProgressAddressSpace(Process* process)
{
	addr_t current_address_space;
#if defined(__i386__)
	asm ( "mov %%cr3, %0" : "=r"(current_address_space) );
#elif defined(__x86_64__)
	asm ( "mov %%cr3, %0" : "=r"(current_address_space) );
#else
	#warning "You should set current_address_space for safety"
	current_address_space = process->addrspace;
#endif
	return current_address_space == process->addrspace;
}

static struct segment* FindSegment(Process* process, uintptr_t addr)
{
	for ( size_t i = 0; i < process->segments_used; i++ )
	{
		struct segment* segment = &process->segments[i];
		if ( addr < segment->addr )
			continue;
		if ( segment->addr + segment->size <= addr )
			continue;
		return segment;
	}
	return NULL;
}

bool CopyToUser(void* userdst_ptr, const void* ksrc_ptr, size_t count)
{
	uintptr_t userdst = (uintptr_t) userdst_ptr;
	uintptr_t ksrc = (uintptr_t) ksrc_ptr;
	bool result = true;
	Process* process = CurrentProcess();
	assert(IsInProgressAddressSpace(process));
	kthread_mutex_lock(&process->segment_lock);
	while ( count )
	{
		struct segment* segment = FindSegment(process, userdst);
		if ( !segment || !(segment->prot & PROT_WRITE) )
		{
			errno = EFAULT;
			result = false;
			break;
		}
		size_t amount = count;
		size_t segment_available = segment->addr + segment->size - userdst;
		if ( segment_available < amount )
			amount = segment_available;
		memcpy((void*) userdst, (const void*) ksrc, amount);
		userdst += amount;
		ksrc += amount;
		count -= amount;
	}
	kthread_mutex_unlock(&process->segment_lock);
	return result;
}

bool CopyFromUser(void* kdst_ptr, const void* usersrc_ptr, size_t count)
{
	uintptr_t kdst = (uintptr_t) kdst_ptr;
	uintptr_t usersrc = (uintptr_t) usersrc_ptr;
	bool result = true;
	Process* process = CurrentProcess();
	assert(IsInProgressAddressSpace(process));
	kthread_mutex_lock(&process->segment_lock);
	while ( count )
	{
		struct segment* segment = FindSegment(process, usersrc);
		if ( !segment || !(segment->prot & PROT_READ) )
		{
			errno = EFAULT;
			result = false;
			break;
		}
		size_t amount = count;
		size_t segment_available = segment->addr + segment->size - usersrc;
		if ( segment_available < amount )
			amount = segment_available;
		memcpy((void*) kdst, (const void*) usersrc, amount);
		kdst += amount;
		usersrc += amount;
		count -= amount;
	}
	kthread_mutex_unlock(&process->segment_lock);
	return result;
}

bool CopyToKernel(void* kdst, const void* ksrc, size_t count)
{
	memcpy(kdst, ksrc, count);
	return true;
}

bool CopyFromKernel(void* kdst, const void* ksrc, size_t count)
{
	memcpy(kdst, ksrc, count);
	return true;
}

bool ZeroKernel(void* kdst, size_t count)
{
	// TODO: We could check the page tables for extra safely.
	memset(kdst, 0, count);
	return true;
}

bool ZeroUser(void* userdst_ptr, size_t count)
{
	uintptr_t userdst = (uintptr_t) userdst_ptr;
	bool result = true;
	Process* process = CurrentProcess();
	assert(IsInProgressAddressSpace(process));
	kthread_mutex_lock(&process->segment_lock);
	while ( count )
	{
		struct segment* segment = FindSegment(process, userdst);
		if ( !segment || !(segment->prot & PROT_WRITE) )
		{
			errno = EFAULT;
			result = false;
			break;
		}
		size_t amount = count;
		size_t segment_available = segment->addr + segment->size - userdst;
		if ( segment_available < amount )
			amount = segment_available;
		memset((void*) userdst, 0, amount);
		userdst += amount;
		count -= amount;
	}
	kthread_mutex_unlock(&process->segment_lock);
	return result;
}

// NOTE: No overflow can happen here because the user can't make an infinitely
//       long string spanning the entire address space because the user can't
//       control the entire address space.
char* GetStringFromUser(const char* usersrc_str)
{
	uintptr_t usersrc = (uintptr_t) usersrc_str;
	size_t result_length = 0;
	Process* process = CurrentProcess();
	assert(IsInProgressAddressSpace(process));

	kthread_mutex_lock(&process->segment_lock);
	while ( true )
	{
		uintptr_t current_at = usersrc + result_length;
		struct segment* segment = FindSegment(process, current_at);
		if ( !segment || !(segment->prot & PROT_READ) )
		{
			kthread_mutex_unlock(&process->segment_lock);
			return errno = EFAULT, (char*) NULL;
		}
		size_t segment_available = segment->addr + segment->size - current_at;
		volatile const char* str = (volatile const char*) current_at;
		size_t length = 0;
		for ( ; length < segment_available; length++ )
		{
			char c = str[length];
			if ( c == '\0' )
				break;
			length++;
		}
		result_length += length;
		if ( length < segment_available )
			break;
	}

	char* result = new char[result_length + 1];
	if ( !result )
	{
		kthread_mutex_unlock(&process->segment_lock);
		return (char*) NULL;
	}

	memcpy(result, (const char*) usersrc, result_length);
	result[result_length] = '\0';

	// We have transferred a bunch of bytes from user-space and appended a zero
	// byte. This is a string. If no concurrent threads were modifying the
	// memory, this is the intended string. If the memory was modified, we got
	// potential garbage followed by a NUL byte. This is a string, but probably
	// not what was intended. If the garbage itself had a premature unexpected
	// NUL byte, that's okay, the garbage string just got truncated.

	kthread_mutex_unlock(&process->segment_lock);
	return result;
}

} // namespace Sortix
