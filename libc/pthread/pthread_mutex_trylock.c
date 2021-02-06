/*
 * Copyright (c) 2013, 2014, 2021 Jonas 'Sortie' Termansen.
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
 * pthread/pthread_mutex_trylock.c
 * Attempts to lock a mutex.
 */

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>

static const int UNLOCKED = 0;
static const int LOCKED = 1;

int pthread_mutex_trylock(pthread_mutex_t* mutex)
{
	int state = UNLOCKED;
	if ( !__atomic_compare_exchange_n(&mutex->lock, &state, LOCKED, false,
	                                  __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) )
	{
		if ( mutex->type == PTHREAD_MUTEX_RECURSIVE &&
		     (pthread_t) mutex->owner == pthread_self() )
		{
			if ( mutex->recursion == ULONG_MAX )
				return errno = EAGAIN;
			mutex->recursion++;
			return 0;
		}
		return errno = EBUSY;
	}
	mutex->owner = (unsigned long) pthread_self();
	mutex->recursion = 0;
	return 0;
}
