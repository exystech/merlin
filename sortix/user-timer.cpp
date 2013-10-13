/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2013.

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

    user-timer.cpp
    Timers that send signals when triggered.

*******************************************************************************/

#include <sys/types.h>

#include <errno.h>
#include <timespec.h>

#include <sortix/clock.h>
#include <sortix/signal.h>
#include <sortix/sigevent.h>
#include <sortix/time.h>

#include <sortix/kernel/platform.h>
#include <sortix/kernel/copy.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/time.h>
#include <sortix/kernel/syscall.h>
#include <sortix/kernel/user-timer.h>

#include "process.h"

// TODO: Memset all user timers in process constructor.

namespace Sortix {

// TODO: We also need to fetch the pthread attr if there is one.
static bool FetchSigevent(struct sigevent* dst, const struct sigevent* src)
{
	if ( src )
		return CopyFromUser(dst, src, sizeof(struct sigevent));
	dst->sigev_notify = SIGEV_SIGNAL;
	dst->sigev_signo = SIGALRM;
	// TODO: "and the sigev_value member having the value of the timer ID."
	//  - Does POSIX want the caller to be psychic and should we write back the
	//    final default sigevent?
	return true;
}

static UserTimer* LookupUserTimer(Process* process, timer_t timerid)
{
	if ( PROCESS_TIMER_NUM_MAX <= timerid )
		return errno = EINVAL, (UserTimer*) NULL;
	UserTimer* user_timer = &process->user_timers[timerid];
	if ( !user_timer->timer.IsAttached() )
		return errno = EINVAL, (UserTimer*) NULL;
	return user_timer;
}

static Timer* LookupTimer(Process* process, timer_t timerid)
{
	UserTimer* user_timer = LookupUserTimer(process, timerid);
	return user_timer ? &user_timer->timer : (Timer*) NULL;
}

static int sys_timer_create(clockid_t clockid, struct sigevent* sigevp,
                            timer_t* timerid_ptr)
{
	Process* process = CurrentProcess();
	ScopedLock lock(&process->user_timers_lock);

	Clock* clock = Time::GetClock(clockid);
	if ( !clock )
		return -1;

	struct sigevent sigev;
	if ( !FetchSigevent(&sigev, sigevp) )
		return -1;

	// Allocate a timer for this request.
	timer_t timerid;
	for ( timerid = 0; timerid < PROCESS_TIMER_NUM_MAX; timerid++ )
	{
		Timer* timer = &process->user_timers[timerid].timer;
		if ( timer->IsAttached() )
			continue;
		timer->Attach(clock);
		break;
	}

	if ( PROCESS_TIMER_NUM_MAX <= timerid )
		return -1;

	if ( !CopyToUser(timerid_ptr, &timerid, sizeof(timerid)) )
	{
		process->user_timers[timerid].timer.Detach();
		return -1;
	}

	process->user_timers[timerid].process = process;
	process->user_timers[timerid].event = sigev;
	process->user_timers[timerid].timerid = timerid;

	return 0;
}

static int sys_timer_delete(timer_t timerid)
{
	Process* process = CurrentProcess();
	ScopedLock lock(&process->user_timers_lock);

	Timer* timer = LookupTimer(process, timerid);
	if ( !timer )
		return -1;

	timer->Cancel();
	timer->Detach();

	return 0;
}

static int sys_timer_getoverrun(timer_t timerid)
{
	Process* process = CurrentProcess();
	ScopedLock lock(&process->user_timers_lock);

	Timer* timer = LookupTimer(process, timerid);
	if ( !timer )
		return -1;

	// TODO: This is not fully kept track of yet.

	return 0;
}

static int sys_timer_gettime(timer_t timerid, struct itimerspec* user_value)
{
	Process* process = CurrentProcess();
	ScopedLock lock(&process->user_timers_lock);

	Timer* timer = LookupTimer(process, timerid);
	if ( !timer )
		return -1;

	struct itimerspec value;
	timer->Get(&value);

	return CopyToUser(user_value, &value, sizeof(value)) ? 0 : -1;
}

static void timer_callback(Clock* /*clock*/, Timer* timer, void* user)
{
	UserTimer* user_timer = (UserTimer*) user;
	Process* process = user_timer->process;
	ScopedLock lock(&process->user_timers_lock);

	size_t current_overrun = timer->num_overrun_events;
	// TODO: This delivery facility is insufficient! sigevent is much more
	//       powerful than sending a simple old-school signal.
	// TODO: If the last signal from last time is still being processed, we need
	//       to handle the sum of overrun. I'm not sure how to handle overrun
	//       properly, so we'll just pretend to user-space it never happens when
	//       it does and we do some of the bookkeeping.
	(void) current_overrun;
	process->DeliverSignal(user_timer->event.sigev_signo);
}

static int sys_timer_settime(timer_t timerid, int flags,
                             const struct itimerspec* user_value,
                             struct itimerspec* user_ovalue)
{
	Process* process = CurrentProcess();
	ScopedLock lock(&process->user_timers_lock);

	UserTimer* user_timer = LookupUserTimer(process, timerid);
	if ( !user_timer )
		return -1;

	Timer* timer = &user_timer->timer;

	struct itimerspec value, ovalue;
	if ( !CopyFromUser(&value, user_value, sizeof(value)) )
		return -1;

	if ( timespec_lt(value.it_value, timespec_nul()) ||
	     timespec_lt(value.it_interval, timespec_nul()) ||
	     (flags & ~(TIMER_ABSTIME)) != 0 )
		return errno = EINVAL, -1;

	int timer_flags = 0;
	if ( flags & TIMER_ABSTIME )
		timer_flags |= TIMER_ABSOLUTE;

	timer->Set(&value, &ovalue, timer_flags, timer_callback, user_timer);

	// Let the caller know how much time was left on the timer.
	if ( user_ovalue && !CopyToUser(user_ovalue, &ovalue, sizeof(ovalue)) )
		return -1;

	return 0;
}

static int sys_uptime(uintmax_t* usecssinceboot)
{
	struct timespec now;
	Clock* clock = Time::GetClock(CLOCK_BOOT);
	clock->Get(&now, NULL);

	uintmax_t seconds = now.tv_sec;
	uintmax_t nano_seconds = now.tv_nsec;
	uintmax_t ret = seconds * 1000000 + nano_seconds / 1000;

	return CopyToUser(usecssinceboot, &ret, sizeof(ret)) ? 0 : -1;
}

void UserTimer::Init()
{
	Syscall::Register(SYSCALL_TIMER_CREATE, (void*) sys_timer_create);
	Syscall::Register(SYSCALL_TIMER_DELETE, (void*) sys_timer_delete);
	Syscall::Register(SYSCALL_TIMER_GETOVERRUN, (void*) sys_timer_getoverrun);
	Syscall::Register(SYSCALL_TIMER_GETTIME, (void*) sys_timer_gettime);
	Syscall::Register(SYSCALL_TIMER_SETTIME, (void*) sys_timer_settime);
	Syscall::Register(SYSCALL_UPTIME, (void*) sys_uptime);
}

} // namespace Sortix
