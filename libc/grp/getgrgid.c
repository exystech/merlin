/*
 * Copyright (c) 2013, 2021 Jonas 'Sortie' Termansen.
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
 * grp/getgrgid.c
 * Searchs the group database for a group with the given numeric group id in a
 * thread-insecure manner.
 */

#include <errno.h>
#include <grp.h>
#include <stdlib.h>

struct group* getgrgid(gid_t gid)
{
	int old_errno = errno;
	static struct group result_object;
	static char* buf = NULL;
	static size_t buflen = 0;
	if ( !buf )
	{
		size_t new_buflen = 64;
		if ( !(buf = (char*) malloc(new_buflen)) )
			return NULL;
		buflen = new_buflen;
	}
	struct group* result;
	int errnum;
retry:
	errnum = getgrgid_r(gid, &result_object, buf, buflen, &result);
	if ( errnum == ERANGE )
	{
		size_t new_buflen = 2 * buflen;
		char* new_buf = (char*) realloc(buf, new_buflen);
		if ( !new_buf )
			return NULL;
		buf = new_buf;
		buflen = new_buflen;
		goto retry;
	}
	if ( errnum < 0 )
		return errno = errnum, (struct group*) NULL;
	errno = old_errno;
	return result;
}
