/*
 * Copyright (c) 2014, 2018 Jonas 'Sortie' Termansen.
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
 * netinet/tcp.h
 * Definitions for the Internet Transmission Control Protocol.
 */

#ifndef _INCLUDE_NETINET_TCP_H
#define _INCLUDE_NETINET_TCP_H

#include <sys/cdefs.h>

/* Options at the IPPROTO_TCP socket level. */
#define TCP_NODELAY 1
#if __USE_SORTIX
#define TCP_MAXSEG 2
#define TCP_NOPUSH 3
#endif

#endif
