/*
 * Copyright (c) 2013, 2014 Jonas 'Sortie' Termansen.
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
 * pthread/pthread_create.c
 * Creates a new thread.
 */

#include <sys/mman.h>

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// Utility function that rounds size upwards to a multiple of alignment.
static size_t align_size(size_t size, size_t alignment)
{
	return -(-size & ~(alignment-1));
}

__attribute__((noreturn))
static void pthread_entrance(struct pthread* thread)
{
	pthread_exit(thread->entry_function(thread->entry_cookie));
}

#if defined(__i386__) || defined(__x86_64__)
static const unsigned long FLAGS_CARRY        = 1 << 0; // 0x000001
static const unsigned long FLAGS_RESERVED1    = 1 << 1; // 0x000002, read as one
static const unsigned long FLAGS_PARITY       = 1 << 2; // 0x000004
static const unsigned long FLAGS_RESERVED2    = 1 << 3; // 0x000008
static const unsigned long FLAGS_AUX          = 1 << 4; // 0x000010
static const unsigned long FLAGS_RESERVED3    = 1 << 5; // 0x000020
static const unsigned long FLAGS_ZERO         = 1 << 6; // 0x000040
static const unsigned long FLAGS_SIGN         = 1 << 7; // 0x000080
static const unsigned long FLAGS_TRAP         = 1 << 8; // 0x000100
static const unsigned long FLAGS_INTERRUPT    = 1 << 9; // 0x000200
static const unsigned long FLAGS_DIRECTION    = 1 << 10; // 0x000400
static const unsigned long FLAGS_OVERFLOW     = 1 << 11; // 0x000800
static const unsigned long FLAGS_IOPRIVLEVEL  = 1 << 12 | 1 << 13;
static const unsigned long FLAGS_NESTEDTASK   = 1 << 14; // 0x004000
static const unsigned long FLAGS_RESERVED4    = 1 << 15; // 0x008000
static const unsigned long FLAGS_RESUME       = 1 << 16; // 0x010000
static const unsigned long FLAGS_VIRTUAL8086  = 1 << 17; // 0x020000
static const unsigned long FLAGS_ALIGNCHECK   = 1 << 18; // 0x040000
static const unsigned long FLAGS_VIRTINTR     = 1 << 19; // 0x080000
static const unsigned long FLAGS_VIRTINTRPEND = 1 << 20; // 0x100000
static const unsigned long FLAGS_ID           = 1 << 21; // 0x200000
#endif

#if defined(__i386__)
static const unsigned long MINIMUM_STACK_SIZE = 4 * sizeof(unsigned long);
static void setup_thread_state(struct pthread* thread, struct tfork* regs)
{
	assert(MINIMUM_STACK_SIZE <= thread->uthread.stack_size);

	memset(regs, 0, sizeof(*regs));
	regs->eip = (uintptr_t) pthread_entrance;
	regs->eflags = FLAGS_RESERVED1 | FLAGS_INTERRUPT | FLAGS_ID;
	regs->gsbase = (unsigned long) thread;

	unsigned long* stack =
		(unsigned long*) ((uint8_t*) thread->uthread.stack_mmap +
		                             thread->uthread.stack_size);

	*--stack = 0; // Alignment.
	*--stack = (unsigned long) thread;
	*--stack = 0; // rip=0

	regs->esp = (uintptr_t) stack;
	regs->ebp = 0;
}

#endif

#if defined(__x86_64__)
static const unsigned long MINIMUM_STACK_SIZE = 2 * sizeof(unsigned long);
static void setup_thread_state(struct pthread* thread, struct tfork* regs)
{
	assert(MINIMUM_STACK_SIZE <= thread->uthread.stack_size);

	memset(regs, 0, sizeof(*regs));
	regs->rip = (uintptr_t) pthread_entrance;
	regs->rdi = (uintptr_t) thread;
	regs->rflags = FLAGS_RESERVED1 | FLAGS_INTERRUPT | FLAGS_ID;
	regs->fsbase = (unsigned long) thread;

	unsigned long* stack =
		(unsigned long*) ((uint8_t*) thread->uthread.stack_mmap +
		                             thread->uthread.stack_size);

	*--stack = 0; // rip=0

	regs->rsp = (uintptr_t) stack;
	regs->rbp = 0;
}
#endif

int pthread_create(pthread_t* restrict thread_ptr,
                   const pthread_attr_t* restrict attr,
                   void* (*entry_function)(void*),
                   void* restrict entry_cookie)
{
	assert(thread_ptr);

	pthread_attr_t default_attr;
	if ( !attr )
	{
		pthread_attr_init(&default_attr);
		attr = &default_attr;
	}

	struct pthread* self = pthread_self();

	// We can't create a thread local storage copy (and thus a new thread) if
	// the kernel failed to allocate the thread local storage master copy.
	if ( self->uthread.tls_master_size && !self->uthread.tls_master_mmap )
		return errno = ENOMEM;

	size_t raw_tls_size = self->uthread.tls_master_size;
	size_t raw_tls_size_aligned = align_size(raw_tls_size, self->uthread.tls_master_align);
	if ( raw_tls_size && raw_tls_size_aligned == 0 /* overflow */ )
		return errno = EINVAL;

	size_t tls_size = raw_tls_size_aligned + sizeof(struct pthread);
	size_t tls_offset_tls = 0;
	size_t tls_offset_pthread = raw_tls_size_aligned;
	if ( self->uthread.tls_master_align < alignof(struct pthread) )
	{
		size_t more_aligned = align_size(raw_tls_size_aligned, alignof(struct pthread));
		if ( raw_tls_size_aligned && more_aligned == 0 /* overflow */ )
			return errno = EINVAL;
		size_t difference = more_aligned - raw_tls_size_aligned;
		tls_size += difference;
		tls_offset_tls += difference;
		tls_offset_pthread += difference;
	}
	assert((tls_offset_tls & (self->uthread.tls_master_align-1)) == 0);
	assert((tls_offset_pthread & (alignof(struct pthread)-1)) == 0);

	// Allocate a copy of the base thread-local storage area for use by the new
	// thread, followed by the struct pthread for the new thread.
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	uint8_t* mapping = (uint8_t*) mmap(NULL, tls_size, prot, flags, -1, 0);
	if ( (void*) mapping == MAP_FAILED )
		return errno;

	// We'll put the struct pthread nicely aligned in the new mapping.
	struct pthread* thread =
		(struct pthread*) (mapping + tls_offset_pthread);
	assert((((uintptr_t) thread) & (alignof(struct pthread)-1)) == 0);

	// Let's put the thread-local storage just before the struct pthread.
	uint8_t* tls = mapping + tls_offset_tls;

	// We will now initialize the thread-local storage of the new thread.
	memcpy(tls, self->uthread.tls_master_mmap, self->uthread.tls_master_size);

	// We'll clean up the new struct pthread before initializing it.
	memset(thread, 0, sizeof(struct pthread));

	// Initialize the thread object.
	thread->uthread.uthread_pointer = &thread->uthread;
	thread->uthread.uthread_size = sizeof(struct pthread);
	thread->uthread.uthread_flags = 0;
	thread->uthread.tls_master_mmap = self->uthread.tls_master_mmap;
	thread->uthread.tls_master_size = self->uthread.tls_master_size;
	thread->uthread.tls_master_align = self->uthread.tls_master_align;
	thread->uthread.tls_mmap = mapping;
	thread->uthread.tls_size = tls_size;
	thread->uthread.arg_mmap = self->uthread.arg_mmap;
	thread->uthread.arg_size = self->uthread.arg_size;
	thread->join_lock = (pthread_mutex_t) PTHREAD_NORMAL_MUTEX_INITIALIZER_NP;
	thread->join_lock.lock = 1 /* LOCKED_VALUE */;
	thread->join_lock.type = PTHREAD_MUTEX_NORMAL;
	thread->join_lock.owner = (unsigned long) thread;
	thread->detach_lock = (pthread_mutex_t) PTHREAD_NORMAL_MUTEX_INITIALIZER_NP;
	thread->detach_state = attr->detach_state;
	thread->entry_function = entry_function;
	thread->entry_cookie = entry_cookie;

	// Set up a stack for the new thread.
	int stack_prot = PROT_READ | PROT_WRITE;
	int stack_flags = MAP_PRIVATE | MAP_ANONYMOUS;
	thread->uthread.stack_size = attr->stack_size;
	thread->uthread.stack_mmap =
		mmap(NULL, thread->uthread.stack_size, stack_prot, stack_flags, -1, 0);
	if ( thread->uthread.stack_mmap == MAP_FAILED )
	{
		munmap(thread->uthread.tls_mmap, thread->uthread.tls_size);
		return errno;
	}

	// Prepare the registers and initial stack for the new thread.
	struct tfork regs;
	setup_thread_state(thread, &regs);
	memset(&regs.altstack, 0, sizeof(regs.altstack));
	regs.altstack.ss_flags = SS_DISABLE;
	sigprocmask(SIG_SETMASK, NULL, &regs.sigmask);

	// Create a new thread with the requested state.
	if ( tfork(SFTHREAD, &regs) < 0 )
	{
		munmap(thread->uthread.stack_mmap, thread->uthread.stack_size);
		munmap(thread->uthread.tls_mmap, thread->uthread.tls_size);
		return errno;
	}

	*thread_ptr = thread;

	return 0;
}
