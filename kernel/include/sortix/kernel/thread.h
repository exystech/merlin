/*
 * Copyright (c) 2011-2016, 2018, 2021-2022 Jonas 'Sortie' Termansen.
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

#ifndef _INCLUDE_SORTIX_KERNEL_THREAD_H
#define _INCLUDE_SORTIX_KERNEL_THREAD_H

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
Thread* CreateKernelThread(Process* process, struct thread_registers* regs,
                           const char* name);
Thread* CreateKernelThread(Process* process, void (*entry)(void*), void* user,
                           const char* name, size_t stacksize = 0);
Thread* CreateKernelThread(void (*entry)(void*), void* user, const char* name,
                           size_t stacksize = 0);

// This function can be used to start a thread from the above functions.
void StartKernelThread(Thread* thread);

// Alternatively, these functions both create and start the thread.
Thread* RunKernelThread(Process* process, struct thread_registers* regs,
                        const char* name);
Thread* RunKernelThread(Process* process, void (*entry)(void*), void* user,
                        const char* name, size_t stacksize = 0);
Thread* RunKernelThread(void (*entry)(void*), void* user, const char* name,
                        size_t stacksize = 0);

enum yield_operation
{
	YIELD_OPERATION_NONE,
	YIELD_OPERATION_WAIT_FUTEX,
	YIELD_OPERATION_WAIT_FUTEX_SIGNAL,
	YIELD_OPERATION_WAIT_KUTEX,
	YIELD_OPERATION_WAIT_KUTEX_SIGNAL,
};

class Thread
{
public:
	Thread();
	~Thread();

public:
	const char* name;
	uintptr_t system_tid;
	uintptr_t yield_to_tid;
	struct thread_registers registers;
	size_t id;
	Process* process;
	Thread* prevsibling;
	Thread* nextsibling;
	Thread* scheduler_list_prev;
	Thread* scheduler_list_next;
	volatile ThreadState state;
	sigset_t signal_pending;
	sigset_t signal_mask;
	sigset_t saved_signal_mask;
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
	bool has_saved_signal_mask;
	Clock execute_clock;
	Clock system_clock;
	uintptr_t futex_address;
	uintptr_t kutex_address;
	bool futex_woken;
	bool kutex_woken;
	bool timer_woken;
	Thread* futex_prev_waiting;
	Thread* futex_next_waiting;
	Thread* kutex_prev_waiting;
	Thread* kutex_next_waiting;
	enum yield_operation yield_operation;

public:
	void HandleSignal(struct interrupt_context* intctx);
	void HandleSigreturn(struct interrupt_context* intctx);
	bool DeliverSignal(int signum);
	bool DeliverSignalUnlocked(int signum);
	void DoUpdatePendingSignal();

};

Thread* CurrentThread();

} // namespace Sortix

#endif
