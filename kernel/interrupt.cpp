/*
 * Copyright (c) 2011, 2012, 2013, 2014, 2016, 2017 Jonas 'Sortie' Termansen.
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

namespace Sortix {
namespace Interrupt {

static struct interrupt_work* first;
static struct interrupt_work* last;

void WorkerThread(void* /*user*/)
{
	assert(Interrupt::IsEnabled());
	while ( true )
	{
		struct interrupt_work* work;
		Interrupt::Disable();
		work = first;
		first = NULL;
		last = NULL;
		Interrupt::Enable();
		if ( !work )
		{
			// TODO: Make this thread not run until work arrives.
			kthread_yield();
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
}

} // namespace Interrupt
} // namespace Sortix
