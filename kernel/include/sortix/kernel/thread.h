/*
 * Copyright (c) 2011, 2012, 2013, 2014, 2015 Jonas 'Sortie' Termansen.
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
 * sortix/kernel/thread.h
 * Describes a thread belonging to a process.
 */

#ifndef INCLUDE_SORTIX_KERNEL_THREAD_H
#define INCLUDE_SORTIX_KERNEL_THREAD_H

#include <stdint.h>

#include <sortix/sigaction.h>
#include <sortix/signal.h>
#include <sortix/sigset.h>
#include <sortix/stack.h>

#include <sortix/kernel/clock.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/registers.h>
#include <sortix/kernel/scheduler.h>
#include <sortix/kernel/signal.h>

namespace Sortix {

class Process;
class Thread;

// These functions create a new kernel process but doesn't start it.
Thread* CreateKernelThread(Process* process, struct thread_registers* regs);
Thread* CreateKernelThread(Process* process, void (*entry)(void*), void* user,
                           size_t stacksize = 0);
Thread* CreateKernelThread(void (*entry)(void*), void* user, size_t stacksize = 0);

// This function can be used to start a thread from the above functions.
void StartKernelThread(Thread* thread);

// Alternatively, these functions both create and start the thread.
Thread* RunKernelThread(Process* process, struct thread_registers* regs);
Thread* RunKernelThread(Process* process, void (*entry)(void*), void* user,
                        size_t stacksize = 0);
Thread* RunKernelThread(void (*entry)(void*), void* user, size_t stacksize = 0);

// Structure to identify each live setjmp buffers. The call_level is the call
// stack depth of the setjmp caller. The setjmp buffer goes out of scope if that
// function returns. rip is the address it will return to. jmpbuf_ptr is the
// pointer to the user-space jmp_buf. Each function can have many jmp_buf
// objects, so they must be kept track of separately.
struct secure_setjmp
{
	size_t call_level;
	uintptr_t rip;
	uintptr_t jmpbuf_ptr;
};

// Limit protected shadow stacks to 4096 pointers. This works well in practice,
// though some programs using a lot of recursion may not work. For simplicity,
// this limit is currently hard-coded, but if nessesary, could be changed to
// allocate a larger array on overflow.
#define CFI_MAX_CALL_DEPTH 4096

// setjmp is rarely used, especially recursively, so 16 should be enough for all
// sensible programs.
#define CFI_MAX_SETJMP 16

class Thread
{
public:
	Thread();
	~Thread();

public:
	uintptr_t system_tid;
	uintptr_t yield_to_tid;
	struct thread_registers registers;
	uint8_t* self_allocation;
	size_t id;
	Process* process;
	Thread* prevsibling;
	Thread* nextsibling;
	Thread* scheduler_list_prev;
	Thread* scheduler_list_next;
	volatile ThreadState state;
	sigset_t signal_pending;
	sigset_t signal_mask;
	stack_t signal_stack;
	addr_t kernelstackpos;
	size_t kernelstacksize;
	size_t signal_count;
	uintptr_t signal_single_frame;
	uintptr_t signal_canary;
	bool kernelstackmalloced;
	bool pledged_destruction;
	bool force_no_signals;
	bool signal_single;
	Clock execute_clock;
	Clock system_clock;
	// Per-thread call stack array. call_count is the current depth, the amount
	// of return pointers stored in calls. This is the protected shadow stack.
	size_t call_count;
	uintptr_t calls[CFI_MAX_CALL_DEPTH];
	// Per-thread live setjmp objects array. setjmp_count is the number of such
	// live entries. It is sorted in increasing call stack depth and elements
	// will be removed when they go out of function scope. This is the kernel
	// jmp_buf registation.
	size_t setjmp_count;
	struct secure_setjmp setjmps[CFI_MAX_SETJMP];

public:
	void HandleSignal(struct interrupt_context* intctx);
	void HandleSigreturn(struct interrupt_context* intctx);
	bool DeliverSignal(int signum);
	bool DeliverSignalUnlocked(int signum);
	void DoUpdatePendingSignal();

};

Thread* AllocateThread();
void FreeThread(Thread* thread);

Thread* CurrentThread();

} // namespace Sortix

#endif
