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
 * pwd/fgetpwent_r.c
 * Reads a passwd entry from a FILE.
 */

#include <errno.h>
#include <pwd.h>
#include <stdio.h>

int fgetpwent_r(FILE* restrict fp,
                struct passwd* restrict result,
                char* restrict buf,
                size_t buf_len,
                struct passwd** restrict result_pointer)
{
	if ( !result_pointer )
		return errno = EINVAL;

	if ( !fp || !result || !buf )
		return *result_pointer = NULL, errno = EINVAL;

	int original_errno = errno;

	flockfile(fp);

	off_t original_offset = ftello(fp);
	if ( original_offset < 0 )
	{
		funlockfile(fp);
		return *result_pointer = NULL, errno;
	}

	size_t buf_used = 0;
	int ic;
	while ( (ic = fgetc(fp)) != EOF )
	{
		if ( ic == '\n' )
			break;

		if ( buf_used == buf_len )
		{
			fseeko(fp, original_offset, SEEK_SET);
			funlockfile(fp);
			return *result_pointer = NULL, errno = ERANGE;
		}

		buf[buf_used++] = (char) ic;
	}

	if ( ferror(fp) )
	{
		int original_error = errno;
		fseeko(fp, original_offset, SEEK_SET);
		funlockfile(fp);
		return *result_pointer = NULL, original_error;
	}

	if ( !buf_used && feof(fp) )
	{
		funlockfile(fp);
		return *result_pointer = NULL, errno = original_errno, 0;
	}

	if ( buf_used == buf_len )
	{
		fseeko(fp, original_offset, SEEK_SET);
		funlockfile(fp);
		return *result_pointer = NULL, errno = ERANGE;
	}
	buf[buf_used] = '\0';

	if ( !scanpwent(buf, result) )
		goto parse_failure;

	funlockfile(fp);

	return *result_pointer = result, 0;

parse_failure:
	fseeko(fp, original_offset, SEEK_SET);
	funlockfile(fp);
	return errno = EINVAL;
}
