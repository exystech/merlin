/*
 * Copyright (c) 2013, 2014, 2021, 2022 Jonas 'Sortie' Termansen.
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
 * pthread/pthread_mutex_unlock.c
 * Unlocks a mutex.
 */

#include <sys/futex.h>

#include <limits.h>
#include <pthread.h>
#include <stdbool.h>

static const int UNLOCKED = 0;
static const int CONTENDED = 2;

int pthread_mutex_unlock(pthread_mutex_t* mutex)
{
	if ( mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->recursion )
	{
		mutex->recursion--;
		return 0;
	}
	mutex->owner = 0;
	// TODO: Multiple threads could have caused the contention and this wakes
	//       only one such thread, but then the mutex returns to normal and the
	//       subsequent unlocks doesn't wake the other contended threads. Work
	//       around this bug by waking all of them instead, but it's better to
	//       instead count the number of waiting threads.
	if ( __atomic_exchange_n(&mutex->lock, UNLOCKED,
	                         __ATOMIC_SEQ_CST) == CONTENDED )
		futex(&mutex->lock, FUTEX_WAKE, INT_MAX, NULL);
	return 0;
}
