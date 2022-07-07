/*
 * Copyright (c) 2021 Jonas 'Sortie' Termansen.
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
 * sortix/futex.h
 * Fast userspace mutexes.
 */

#ifndef _INCLUDE_SORTIX_FUTEX_H
#define _INCLUDE_SORTIX_FUTEX_H

#include <sys/cdefs.h>

#define FUTEX_WAIT 1
#define FUTEX_WAKE 2

#define FUTEX_ABSOLUTE (1 << 8)

#define FUTEX_CLOCK(clock) (clock << 24)

#define FUTEX_GET_OP(op) ((op) & 0xFF)
#define FUTEX_GET_CLOCK(op) ((op) >> 24)

#endif
