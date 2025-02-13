/*
 * Copyright (c) 2013, 2017 Jonas 'Sortie' Termansen.
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
 * sys/un.h
 * Unix domain socket definitions.
 */

#ifndef _INCLUDE_SYS_UN_H
#define _INCLUDE_SYS_UN_H

#include <sys/cdefs.h>

#include <sys/__/types.h>

#include <__/stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __sa_family_t_defined
#define __sa_family_t_defined
typedef __uint16_t sa_family_t;
#endif

struct sockaddr_un
{
	sa_family_t sun_family;
	char sun_path[128 - sizeof(sa_family_t)];
};

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
