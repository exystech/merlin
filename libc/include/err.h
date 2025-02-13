/*
 * Copyright (c) 2015 Jonas 'Sortie' Termansen.
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
 * err.h
 * Error reporting functions.
 */

#ifndef _INCLUDE_ERR_H
#define _INCLUDE_ERR_H

#include <sys/cdefs.h>

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((__noreturn__, __format__(__printf__, 2, 3)))
void err(int, const char*, ...);
__attribute__((__noreturn__, __format__(__printf__, 3, 4)))
void errc(int, int, const char*, ...);
__attribute__((__noreturn__, __format__(__printf__, 2, 3)))
void errx(int, const char*, ...);
__attribute__((__noreturn__, __format__(__printf__, 2, 0)))
void verr(int, const char*, va_list);
__attribute__((__noreturn__, __format__(__printf__, 3, 0)))
void verrc(int, int, const char*, va_list);
__attribute__((__noreturn__, __format__(__printf__, 2, 0)))
void verrx(int, const char*, va_list);
__attribute__((__format__(__printf__, 1, 2)))
void warn(const char*, ...);
__attribute__((__format__(__printf__, 2, 3)))
void warnc(int, const char*, ...);
__attribute__((__format__(__printf__, 1, 2)))
void warnx(const char*, ...);
__attribute__((__format__(__printf__, 1, 0)))
void vwarn(const char*, va_list);
__attribute__((__format__(__printf__, 2, 0)))
void vwarnc(int, const char*, va_list);
__attribute__((__format__(__printf__, 1, 0)))
void vwarnx(const char*, va_list);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
