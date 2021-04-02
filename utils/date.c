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
 * date.c
 * Print or set system date and time.
 */

#include <err.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Note: There's no way to discern whether the format string was too long for
// the output buffer or if the output was simply empty, so don't call this
// function with a format that could produce an empty string. E.g. use a prefix
// like '+' and skip it when using the string.
static char* astrftime(const char* format, const struct tm* tm)
{
	size_t format_size = strlen(format) + 1;
	size_t buffer_size = format_size;
	while ( true )
	{
		char* buffer = calloc(buffer_size, 2);
		if ( !buffer )
			return NULL;
		buffer_size *= 2;
		if ( strftime(buffer, buffer_size, format, tm) )
			return buffer;
		free(buffer);
	}
}

int main(int argc, char* argv[])
{
	const char* format = "+%a %b %e %H:%M:%S %Z %Y";

	if ( 2 <= argc )
	{
		if ( argv[1][0] != '+' )
			errx(1, "setting the system time is not implemented");
		format = argv[1];
	}
	if ( 3 <= argc )
		errx(1, "unexpected extra operand: %s", argv[2]);

	time_t current_time = time(NULL);

	struct tm tm;
	if ( !localtime_r(&current_time, &tm) )
		err(1, "localtime_r(%ji)", (intmax_t) current_time);

	char* string = astrftime(format, &tm);
	if ( !string )
		err(1, "malloc");
	if ( printf("%s\n", string + 1) == EOF )
		err(1, "stdout");
	if ( ferror(stdout) || fflush(stdout) == EOF )
		err(1, "stdout");
	return 0;
}
