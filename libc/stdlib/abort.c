/*
 * Copyright (c) 2011, 2012, 2013, 2014, 2018 Jonas 'Sortie' Termansen.
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
 * stdlib/abort.c
 * Abnormal process termination.
 */

#include <sys/wait.h>

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __is_sortix_libk
#include <libk.h>
#endif

void abort(void)
{
#ifdef __is_sortix_libk
	libk_abort();
#else
	raise(SIGABRT);

	int exit_code = WCONSTRUCT(WNATURE_SIGNALED, 128 + SIGABRT, SIGABRT);
	int exit_flags = EXIT_THREAD_PROCESS | EXIT_THREAD_DUMP_CORE;
	exit_thread(exit_code, exit_flags, NULL);

	__builtin_unreachable();
#endif
}
