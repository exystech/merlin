/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2011, 2012, 2013.

    This file is part of the Sortix C Library.

    The Sortix C Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    The Sortix C Library is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
    License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with the Sortix C Library. If not, see <http://www.gnu.org/licenses/>.

    time.h
    Time declarations.

*******************************************************************************/

#ifndef INCLUDE_TIME_H
#define INCLUDE_TIME_H

#include <features.h>

__BEGIN_DECLS

@include(clock_t.h)
@include(size_t.h)
@include(time_t.h)
@include(clockid_t.h)
@include(timer_t.h)
@include(locale_t.h)
@include(pid_t.h)

struct sigevent;

struct tm
{
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};

__END_DECLS
#include <sortix/timespec.h>
__BEGIN_DECLS

/* TODO: This itimer stuff is replaced with another interface IIRC. */
struct itimerspec
{
	struct timespec it_interval;
	struct timespec it_value;
};

@include(NULL.h)

#define CLOCKS_PER_SEC ((clock_t) 1000000)

__END_DECLS
#include <sortix/clock.h>
__BEGIN_DECLS

#define TIMER_ABSTIME (1<<0)

/* getdate_err is omitted, use strptime */

char* asctime(const struct tm*);
char* asctime_r(const struct tm* restrict, char* restrict);
clock_t clock(void);
/* TODO: clock_getcpuclockid */
int clock_getres(clockid_t, struct timespec*);
int clock_gettime(clockid_t, struct timespec*);
int clock_nanosleep(clockid_t, int, const struct timespec*, struct timespec*);
int clock_settime(clockid_t, const struct timespec*);
char* ctime(const time_t* clock);
char* ctime_r(const time_t* clock, char* buf);
/* ctime_r is obsolescent */
double difftime(time_t, time_t);
/* getdate is omitted, use strptime */
struct tm* gmtime(const time_t*);
struct tm* gmtime_r(const time_t* restrict, struct tm* restrict);
struct tm* localtime(const time_t*);
struct tm* localtime_r(const time_t* restrict, struct tm* restrict);
time_t mktime(struct tm*);
int nanosleep(const struct timespec*, struct timespec*);
size_t strftime(char* restrict, size_t, const char* restrict,
                const struct tm* restrict);
size_t strftime_l(char* restrict, size_t, const char* restrict,
                const struct tm* restrict, locale_t);
char* strptime(const char* restrict, const char* restrict,
               struct tm* restrict);
time_t time(time_t*);
int timer_create(clockid_t, struct sigevent* restrict, time_t* restrict);
int timer_delete(timer_t);
int timer_getoverrun(timer_t);
int timer_gettime(timer_t, struct itimerspec*);
int timer_settime(timer_t, int, const struct itimerspec* restrict,
                  struct itimerspec* restrict);
void tzset(void);

extern int daylight;
extern long timezone;
extern char* tzname[];

__END_DECLS

#endif
