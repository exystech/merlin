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

    sortix/fork.h
    Declarations related to the fork family of system calls on Sortix.

*******************************************************************************/

#ifndef INCLUDE_SORTIX_FORK_H
#define INCLUDE_SORTIX_FORK_H

#include <sys/cdefs.h>

#include <stdint.h>

#include <sortix/sigset.h>
#include <sortix/stack.h>

__BEGIN_DECLS

/* The sfork system call is much like the rfork system call found in Plan 9 and
   BSD systems, however it works slightly differently and was renamed to avoid
   conflicts with existing programs. In particular, it never forks an item
   unless its bit is set, whereas rfork sometimes forks an item by default. If
   you wish to fork certain items simply set the proper flags. Note that since
   flags may be added from time to time, you should use various compound flags
   defined below such as SFFORK and SFALL. It can be useful do combine these
   compound flags with bit operations, for instance "I want traditional fork,
   except share the working dir pointer" is sfork(SFFORK & ~SFCWD). */
#define SFPROC (1<<0) /* Creates child, otherwise affect current task. */
#define SFPID (1<<1) /* Allocates new PID. */
#define SFFD (1<<2) /* Fork file descriptor table. */
#define SFMEM (1<<3) /* Forks address space. */
#define SFCWD (1<<4) /* Forks current directory pointer. */
#define SFROOT (1<<5) /* Forks root directory pointer. */
#define SFNAME (1<<6) /* Forks namespace. */
#define SFSIG (1<<7) /* Forks signal table. */
#define SFCSIG (1<<8) /* Child will have no pending signals, like fork(2). */

/* Creates a new thread in this process. Beware that it will share the stack of
   the parent thread and that various threading features may not have been set
   up properly. You should use the standard threading API unless you know what
   you are doing; remember that you can always sfork more stuff after the
   standard threading API returns control to you. This is useful combined with
   the tfork System call that lets you control the registers of the new task. */
#define SFTHREAD (SFPROC | SFCSIG)

/* Provides traditional fork(2) behavior; use this instead of the above values
   if you want "as fork(2), but also fork foo", or "as fork(2), except bar". In
   those cases it is better to sfork(SFFORK & ~SFFOO); or sfork(SFFORK | SFBAR);
   as that would allow to add new flags to SFFORK if a new kernel feature is
   added to the system that applications don't know about yet. */
#define SFFORK (SFPROC | SFPID | SFFD | SFMEM | SFCWD | SFROOT | SFCSIG)

/* This allows creating a process that is completely forked from the original
   process, unlike SFFORK which does share a few things (such as the process
   namespace). Note that there is a few unset high bits in this value, these
   are reserved and must not be set. */
#define SFALL ((1<<20)-1)

/* This structure tells tfork the initial values of the registers in the new
   task. It is ignored if no new task is created. sfork works by recording its
   own state into such a structure and calling tfork. Note that this structure
   is highly platform specific, portable code should use the standard threading
   facilities combined with sfork if possible. */
struct tfork
{
#if defined(__i386__)
	uint32_t eip;
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t edi;
	uint32_t esi;
	uint32_t esp;
	uint32_t ebp;
	uint32_t eflags;
	uint32_t fsbase;
	uint32_t gsbase;
#elif defined(__x86_64__)
	uint64_t rip;
	uint64_t rax;
	uint64_t rbx;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t rsp;
	uint64_t rbp;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rflags;
	uint64_t fsbase;
	uint64_t gsbase;
#else
#error "You need to add a struct tfork for your platform"
#endif
	sigset_t sigmask;
	stack_t altstack;
};

__END_DECLS

#endif
