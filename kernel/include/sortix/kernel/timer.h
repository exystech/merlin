/*
 * Copyright (c) 2013, 2016 Jonas 'Sortie' Termansen.
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
 * sortix/kernel/timer.h
 * A virtual timer that triggers an action in a worker thread when triggered.
 */

#ifndef INCLUDE_SORTIX_KERNEL_TIMER_H
#define INCLUDE_SORTIX_KERNEL_TIMER_H

#include <sortix/timespec.h>
#include <sortix/itimerspec.h>

#include <sortix/kernel/kthread.h>

namespace Sortix {

class Clock;
class Timer;

static const int TIMER_ABSOLUTE = 1 << 0;
static const int TIMER_ACTIVE = 1 << 1;
static const int TIMER_FIRING = 1 << 2;
static const int TIMER_FUNC_INTERRUPT_HANDLER = 1 << 3;
static const int TIMER_FUNC_ADVANCE_THREAD = 1 << 4;
// The timer callback may deallocate the timer itself. The timer data structure
// will not be touched by the timer clock after running the callback and the
// clock and timer implementation will not have any issues with deallocating it.
// This feature cannot be combined with periodic timers. The Cancel method may
// not be called, as it ensures consistency (the timer is cancelled if pending,
// and if it's firing, then waiting for it to complete). Instead, use TryCancel
// which will return false if the timer wasn't pending. If the timer has been
// armed, and the handler has not yet run, that means the handler is scheduled
// to run and it's not safe to deallocate until the handler runs. It is not
// possible call the Set method on an armed timer, unless the timer has been
// successfully TryCancelled, or the handler has run. It's the user's
// responsibility to ensure deallocation of the timer only happens if no other
// threads will use the timer data structure. I.e. if some code wants to
// TryCancel a timer, it must synchronize with the timer handler, so the timer
// handler doesn't deallocate the timer and then the other thread calls
// TryCancel on a freed pointer.
// The object containing the timer could contain a mutex and a bool of whether
// the timer is armed and the handler has not run. If there's a need to destroy
// the object, attempt to TryCancel and timer and do so if it succeeds,
// otherwise delay the destruction until the timer handler, which also grabs the
// mutex and checks whether object destruction is supposed to happen.
static const int TIMER_FUNC_MAY_DEALLOCATE_TIMER = 1 << 5;

class Timer
{
public:
	Timer();
	~Timer();

public:
	struct itimerspec value;
	Clock* clock;
	Timer* prev_timer;
	Timer* next_timer;
	void (*callback)(Clock* clock, Timer* timer, void* user);
	void* user;
	size_t num_firings_scheduled;
	size_t num_overrun_events;
	int flags;

private:
	void Fire();
	void GetInternal(struct itimerspec* current);

public:
	void Attach(Clock* the_clock);
	void Detach();
	bool IsAttached() const { return clock; }
	void Cancel();
	bool TryCancel();
	Clock* GetClock() const { return clock; }
	void Get(struct itimerspec* current);
	void Set(struct itimerspec* value, struct itimerspec* ovalue, int flags,
	         void (*callback)(Clock*, Timer*, void*), void* user);

};

} // namespace Sortix

#endif
