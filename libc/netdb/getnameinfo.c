/*
 * Copyright (c) 2013, 2016 Jonas 'Sortie' Termansen.
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
 * netdb/getnameinfo.c
 * Address-to-name translation in protocol-independent manner.
 */

#include <sys/socket.h>

#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int getnameinfo(const struct sockaddr* restrict sa,
                socklen_t salen,
                char* restrict host,
                socklen_t hostlen,
                char* restrict serv,
                socklen_t servlen,
                int flags)
{
	if ( flags & ~(NI_NOFQDN | NI_NUMERICHOST | NI_NAMEREQD | NI_NUMERICSERV |
	               NI_NUMERICSCOPE | NI_DGRAM) )
		return EAI_BADFLAGS;
	int af;
	if ( salen == sizeof(struct sockaddr_in) &&
	     ((struct sockaddr_in*) sa)->sin_family == AF_INET )
		af = AF_INET;
	else if ( salen == sizeof(struct sockaddr_in6) &&
	          ((struct sockaddr_in6*) sa)->sin6_family == AF_INET6 )
		af = AF_INET6;
	else
		return EAI_FAMILY;
	if ( !host && !serv )
		return EAI_NONAME;
	if ( host )
	{
		if ( flags & NI_NAMEREQD )
			return EAI_NONAME;
		const void* addr;
		if ( af == AF_INET )
			addr = &(((struct sockaddr_in*) sa)->sin_addr);
		else if ( af == AF_INET6 )
			addr = &(((struct sockaddr_in6*) sa)->sin6_addr);
		else
			return EAI_FAMILY;
		if ( !inet_ntop(af, addr, host, hostlen) )
			return errno == ENOSPC ? EAI_OVERFLOW :  EAI_NONAME;
	}
	if ( serv )
	{
		in_port_t port;
		if ( af == AF_INET )
			port = be16toh(((struct sockaddr_in*) sa)->sin_port);
		else if ( af == AF_INET6 )
			port = be16toh(((struct sockaddr_in6*) sa)->sin6_port);
		else
			return EAI_FAMILY;
		if ( servlen <= (size_t) snprintf(serv, servlen, "%u", port) )
			return EAI_OVERFLOW;
	}
	return 0;
}
