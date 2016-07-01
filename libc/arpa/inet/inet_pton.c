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
 * arpa/inet/inet_pton.c
 * Convert string to network address.
 */

#include <sys/socket.h>

#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>

int inet_pton(int af, const char* restrict src, void* restrict dst)
{
	if ( af == AF_INET )
	{
		unsigned char ip[4];
		size_t index = 0;
		for ( int i = 0; i < 4; i++ )
		{
			if ( i && src[index++] != '.' )
				return 0;
			if ( src[index] < '0' || '9' < src[index] )
				return 0;
			int num = src[index++] - '0';
			if ( num )
			{
				if ( '0' <= src[index] && src[index] <= '9' )
				{
					num = 10 * num + src[index++] - '0';
					if ( '0' <= src[index] && src[index] <= '9' )
					{
						num = 10 * num + src[index++] - '0';
						if ( 256 <= num )
							return 0;
					}
				}
			}
			ip[i] = num;
		}
		if ( src[index] )
			return 0;
		memcpy(dst, ip, sizeof(ip));
		return 1;
	}
	else if ( af == AF_INET6 )
	{
		unsigned char ip[16];
		size_t index = 0;
		// TODO: Support for :: syntax.
		// TODO: Support for x:x:x:x:x:x:d.d.d.d syntax.
		for ( int i = 0; i < 8; i++ )
		{
			if ( i && src[index++] != ':' )
				return 0;
			int num = 0;
			for ( int j = 0; j < 4; j++ )
			{
				int dgt;
				if ( '0' <= src[index] && src[index] <= '9' )
					dgt = src[index] - '0';
				else if ( 'a' <= src[index] && src[index] <= 'f' )
					dgt = src[index] - 'a' + 10;
				else if ( 'A' <= src[index] && src[index] <= 'F' )
					dgt = src[index] - 'A' + 10;
				else
				{
					if ( j == 0 )
						return 0;
					break;
				}
				num = num << 4 | dgt;
				index++;
			}
			ip[2 * i + 0] = num >> 8 & 0xFF;
			ip[2 * i + 1] = num >> 0 & 0xFF;
		}
		if ( src[index] )
			return 0;
		memcpy(dst, ip, sizeof(ip));
		return 1;
	}
	else
		return errno = EAFNOSUPPORT, -1;
}
