/*
 * Copyright (c) 2014, 2021 Jonas 'Sortie' Termansen.
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
 * semaphore/sem_post.c
 * Unlock a semaphore.
 */

#include <sys/futex.h>

#include <errno.h>
#include <limits.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>

int sem_post(sem_t* sem)
{
	while ( true )
	{
		int old = __atomic_load_n(&sem->value, __ATOMIC_SEQ_CST);
		if ( old == INT_MAX )
			return errno = EOVERFLOW, -1;
		int new = old == -1 ? 1 : old + 1;
		int waiters = __atomic_load_n(&sem->waiters, __ATOMIC_SEQ_CST);
		if ( !__atomic_compare_exchange_n(&sem->value, &old, new, false,
		                                  __ATOMIC_SEQ_CST, __ATOMIC_RELAXED) )
			continue;
		if ( old == -1 || waiters )
			futex(&sem->value, FUTEX_WAKE, 1, NULL);
		return 0;
	}
}
