/*
 * Copyright (c) 2013, 2023 Jonas 'Sortie' Termansen.
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
 * sortix/clock.h
 * Supported logical clock devices.
 */

#ifndef _INCLUDE_SORTIX_CLOCK_H
#define _INCLUDE_SORTIX_CLOCK_H

#include <sys/cdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLOCK_REALTIME 0 /* Current real time. */
#define CLOCK_MONOTONIC 1 /* Always increasing time. */
#define CLOCK_BOOTTIME 2 /* Time since system boot (uptime). */
#define CLOCK_INIT 3 /* Time since 'init' process began. */
#define CLOCK_PROCESS_CPUTIME_ID 4
#define CLOCK_PROCESS_SYSTIME_ID 5
#define CLOCK_CHILD_CPUTIME_ID 6
#define CLOCK_CHILD_SYSTIME_ID 7
#define CLOCK_THREAD_CPUTIME_ID 8
#define CLOCK_THREAD_SYSTIME_ID 9

#define CLOCK_REALTIME_HAS_LEAP_SECONDS 1

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
