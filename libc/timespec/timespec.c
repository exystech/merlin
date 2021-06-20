/*
 * Copyright (c) 2013, 2021 Jonas 'Sortie' Termansen.
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
 * timespec/timespec.c
 * Utility functions for manipulation of struct timespec.
 */

#include <sys/types.h>

#include <assert.h>
#include <timespec.h>

static const long NANOSECONDS_PER_SECOND = 1000000000L;

// The C modulo operator doesn't do exactly what we desire.
static long proper_modulo(long a, long b)
{
	if ( 0 <= a )
		return a % b;
	long tmp = - (unsigned long) a % b;
	return tmp ? tmp : 0;
}

struct timespec timespec_canonalize(struct timespec t)
{
	t.tv_sec += t.tv_nsec / NANOSECONDS_PER_SECOND;
	t.tv_nsec = proper_modulo(t.tv_nsec, NANOSECONDS_PER_SECOND);
	return t;
}

bool timespec_add_overflow(struct timespec a,
                           struct timespec b,
                           struct timespec* res)
{
	assert(timespec_is_canonical(a) && timespec_is_canonical(b));
	struct timespec ret;
	ret.tv_nsec = a.tv_nsec + b.tv_nsec;
	if ( NANOSECONDS_PER_SECOND <= ret.tv_nsec )
	{
		ret.tv_nsec -= NANOSECONDS_PER_SECOND;
		if ( a.tv_sec != TIME_MAX )
			a.tv_sec++;
		else if ( b.tv_sec != TIME_MAX )
			b.tv_sec++;
		else
			goto overflow;
	}
	if ( __builtin_add_overflow(a.tv_sec, b.tv_sec, &ret.tv_sec) )
		goto overflow;
	*res = ret;
	return false;
overflow:
	*res = b.tv_sec < 0 ?
	       timespec_make(TIME_MIN, 0) :
	       timespec_make(TIME_MAX, NANOSECONDS_PER_SECOND - 1);
	return true;
}

struct timespec timespec_add(struct timespec a, struct timespec b)
{
	struct timespec ret;
	timespec_add_overflow(a, b, &ret);
	return ret;
}

bool timespec_sub_overflow(struct timespec a,
                           struct timespec b,
                           struct timespec* res)
{
	assert(timespec_is_canonical(a) && timespec_is_canonical(b));
	struct timespec ret;
	ret.tv_nsec = a.tv_nsec - b.tv_nsec;
	if ( ret.tv_nsec < 0 )
	{
		ret.tv_nsec += NANOSECONDS_PER_SECOND;
		if ( a.tv_sec != TIME_MIN )
			a.tv_sec--;
		else if ( b.tv_sec != TIME_MAX )
			b.tv_sec++;
		else
			goto overflow;
	}
	if ( __builtin_sub_overflow(a.tv_sec, b.tv_sec, &ret.tv_sec) )
		goto overflow;
	*res = ret;
	return false;
overflow:
	*res = b.tv_sec >= 0 ?
	       timespec_make(TIME_MIN, 0) :
	       timespec_make(TIME_MAX, NANOSECONDS_PER_SECOND - 1);
	return true;
}

struct timespec timespec_sub(struct timespec a, struct timespec b)
{
	struct timespec ret;
	timespec_sub_overflow(a, b, &ret);
	return ret;
}
