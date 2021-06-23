/*
 * Copyright (c) 2011-2017, 2021 Jonas 'Sortie' Termansen.
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
 * interrupt.cpp
 * High level interrupt services.
 */

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <sortix/kernel/interrupt.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/thread.h>

namespace Sortix {
namespace Interrupt {

Thread* interrupt_worker_thread = NULL;
bool interrupt_worker_thread_boost = false;

static struct interrupt_work* first;
static struct interrupt_work* last;
static bool interrupt_worker_idle = false;

void WorkerThread(void* /*user*/)
{
	Thread* thread = CurrentThread();
	assert(Interrupt::IsEnabled());
	while ( true )
	{
		thread->futex_woken = false;
		thread->timer_woken = false;
		struct interrupt_work* work;
		Interrupt::Disable();
		work = first;
		first = NULL;
		last = NULL;
		Interrupt::Enable();
		if ( !work )
		{
			interrupt_worker_idle = true;
			kthread_wait_futex();
			interrupt_worker_idle = false;
			continue;
		}
		while ( work )
		{
			struct interrupt_work* next_work = work->next;
			work->handler(work->context);
			work = next_work;
		}
	}
}

void ScheduleWork(struct interrupt_work* work)
{
	assert(!Interrupt::IsEnabled());
	(last ? last->next : first) = work;
	work->next = NULL;
	last = work;
	interrupt_worker_thread_boost = true;
	if ( interrupt_worker_idle )
	{
		interrupt_worker_thread->futex_woken = true;
		kthread_wake_futex(interrupt_worker_thread);
	}
}

} // namespace Interrupt
} // namespace Sortix
