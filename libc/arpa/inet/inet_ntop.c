/*
 * Copyright (c) 2016 Jonas 'Sortie' Termansen.
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
 * arpa/inet/inet_ntop.c
 * Convert network address to string.
 */

#include <sys/socket.h>

#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>

const char* inet_ntop(int af,
                      const void* restrict src,
                      char* restrict dst,
                      socklen_t size)
{
	if ( af == AF_INET )
	{
		const unsigned char* ip = (const unsigned char*) src;
		int len = snprintf(dst, size, "%u.%u.%u.%u",
		                   ip[0], ip[1], ip[2], ip[3]);
		if ( len < 0 )
			return NULL;
		if ( size <= (size_t) len )
			return errno = ENOSPC, (const char*) NULL;
		return dst;
	}
	else if ( af == AF_INET6 )
	{
		// TODO: Support for :: syntax.
		// TODO: Support for x:x:x:x:x:x:d.d.d.d syntax.
		const unsigned char* ip = (const unsigned char*) src;
		int len = snprintf(dst, size, "%x:%x:%x:%x:%x:%x:%x:%x",
		                   ip[0] << 8 | ip[1], ip[2] << 8 | ip[3],
		                   ip[4] << 8 | ip[5], ip[6] << 8 | ip[7],
		                   ip[8] << 8 | ip[9], ip[10] << 8 | ip[11],
		                   ip[12] << 8 | ip[13], ip[14] << 8 | ip[15]);
		if ( len < 0 )
			return NULL;
		if ( size <= (size_t) len )
			return errno = ENOSPC, (const char*) NULL;
		return dst;
	}
	else
		return errno = EAFNOSUPPORT, NULL;
}
