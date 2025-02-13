/*
 * Copyright (c) 2016 Nicholas De Nova.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
 * readlink.c
 * Print symbolic link target.
 */

#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void compact_arguments(int* argc, char*** argv)
{
	for ( int i = 0; i < *argc; i++ )
	{
		while ( i < *argc && !(*argv)[i] )
		{
			for ( int n = i; n < *argc; n++ )
				(*argv)[n] = (*argv)[n+1];
			(*argc)--;
		}
	}
}

int main(int argc, char* argv[])
{
	bool append_newline = true;
	bool canonicalize = false;

	for ( int i = 1; i < argc; i++ )
	{
		const char* arg = argv[i];
		if ( arg[0] != '-' || !arg[1] )
			continue;
		argv[i] = NULL;
		if ( !strcmp(arg, "--") )
			break;
		if ( arg[1] != '-' )
		{
			char c;
			while ( (c = *++arg) )
				switch ( c )
				{
				case 'f': canonicalize = true; break;
				case 'n': append_newline = false; break;
				default:
					errx(1, "unknown option -- '%c'", c);
				}
		}
		else if ( !strcmp(arg, "--canonicalize") )
			canonicalize = true;
		else if ( !strcmp(arg, "--no-newline") )
			append_newline = false;
		else
			errx(1, "unknown option: %s", arg);
	}

	compact_arguments(&argc, &argv);

	if ( argc < 2 )
		errx(1, "missing path operand");
	else if ( argc > 2 )
		errx(1, "unexpected extra operand");

	char* buffer;
	if ( canonicalize )
	{
		buffer = realpath(argv[1], NULL);
		if ( !buffer )
			err(1, "%s", argv[1]);
	}
	else
	{
		size_t buffer_size = 4096;
		while ( true )
		{
			buffer = malloc(buffer_size);
			if ( !buffer )
				err(1, "malloc");

			ssize_t path_size = readlink(argv[1], buffer, buffer_size);
			if ( path_size < 0 )
				err(1, "%s", argv[1]);

			if ( (size_t) path_size == buffer_size )
			{
				free(buffer);
				if ( SIZE_MAX / 2 < buffer_size )
				{
					errno = ENOMEM;
					err(1, "malloc");
					return 1;
				}
				buffer_size *= 2;
				continue;
			}

			buffer[path_size] = '\0';
			break;
		}
	}

	fputs(buffer, stdout);
	if ( append_newline )
		putchar('\n');

	if ( ferror(stdout) || fflush(stdout) == EOF )
		err(1, "stdout");

	return 0;
}
