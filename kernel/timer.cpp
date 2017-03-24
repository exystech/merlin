/*
 * Copyright (c) 2013, 2016, 2017 Jonas 'Sortie' Termansen.
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
 * timer.cpp
 * Clock and timer facility.
 */

#include <assert.h>
#include <timespec.h>

#include <sortix/kernel/clock.h>
#include <sortix/kernel/interrupt.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/timer.h>

namespace Sortix {

// The caller should protect the timer using a lock if multiple threads can
// access it.

Timer::Timer()
{
	value = { timespec_nul(), timespec_nul() };
	clock = NULL;
	prev_timer = NULL;
	next_timer = NULL;
	next_interrupt_timer = NULL;
	callback = NULL;
	user = NULL;
	num_firings_scheduled = 0;
	num_overrun_events = 0;
	flags = 0;
}

Timer::~Timer()
{
	// TODO: Is this the right thing?
	// The user of this object should cancel all pending triggers before calling
	// the destructor. We could try to cancel the timer, but if we fail the race
	// the handler function will be called, and that function may access the
	// timer object. We'd then have to wait for the function to finish and then
	// continue the destuction. This is a bit complex and fragile, so we'll just
	// make it the rule that all outstanding requests must be cancelled before
	// our destruction. The caller should know better than we how to cancel.
	assert(!(flags & TIMER_ACTIVE));
}

void Timer::Attach(Clock* the_clock)
{
	assert(!clock);
	assert(the_clock);
	clock = the_clock;
}

void Timer::Detach()
{
	assert(!(flags & TIMER_ACTIVE));
	assert(clock);
	clock = NULL;
}

void Timer::Cancel()
{
	if ( clock )
		clock->Cancel(this);
}

bool Timer::TryCancel()
{
	if ( clock )
		return clock->TryCancel(this);
	return true;
}

void Timer::GetInternal(struct itimerspec* current)
{
	if ( !(this->flags & TIMER_ACTIVE ) )
		current->it_value = timespec_nul(),
		current->it_interval = timespec_nul();
	else if ( flags & TIMER_ABSOLUTE )
		current->it_value = timespec_sub(value.it_value, clock->current_time),
		current->it_interval = value.it_interval;
	else
		*current = value;
}

void Timer::Get(struct itimerspec* current)
{
	assert(clock);

	clock->LockClock();
	GetInternal(current);
	clock->UnlockClock();
}

void Timer::Set(struct itimerspec* new_value, struct itimerspec* old_value,
                int new_flags, void (*new_callback)(Clock*, Timer*, void*),
                void* new_user)
{
	assert(clock);
	assert(!(flags & TIMER_FUNC_MAY_DEALLOCATE_TIMER) ||
	       timespec_le(new_value->it_interval, timespec_nul()));

	clock->LockClock();

	// Dequeue this timer if it is already armed.
	if ( flags & TIMER_ACTIVE )
	{
		// TODO: How does this interplay with concurrently running timer
		//       handlers? Maybe the timer should be cancelled instead?
		clock->Unlink(this);
	}

	// Let the caller know how much time was left on the timer.
	if ( old_value )
		GetInternal(old_value);

	// Arm the timer if a value was specified.
	if ( timespec_lt(timespec_nul(), new_value->it_value) )
	{
		value = *new_value;
		flags = new_flags;
		callback = new_callback;
		user = new_user;
		clock->Register(this);
	}

	clock->UnlockClock();
}

} // namespace Sortix
