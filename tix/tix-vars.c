/*
 * Copyright (c) 2022 Jonas 'Sortie' Termansen.
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
 * tix-vars.c
 * Evaluate variables in port files.
 */

#include <sys/stat.h>
#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

int main(int argc, char* argv[])
{
	const char* def = NULL;
	bool test = false;

	int opt;
	while ( (opt = getopt(argc, argv, "d:t")) != -1 )
	{
		switch ( opt )
		{
		case 'd': def = optarg; break;
		case 't': test = true; break;
		default: return 1;
		}
	}

	if ( optind == argc )
		errx(2, "expected port file");
	const char* path = argv[optind++];
	string_array_t sa = string_array_make();
	int status = !strcmp(path, "-") ?
	             variables_append_file(&sa, stdin) :
	             variables_append_file_path(&sa, path);
	if ( status == -1 )
		err(2, "%s", path);
	else if ( status == -2 )
		errx(2, "%s: Syntax error", path);

	if ( optind == argc )
	{
		if ( test )
			return 0;
		for ( size_t i = 0; i < sa.length; i++ )
			if ( puts(sa.strings[i]) < 0 )
				err(2, "stdout");
	}
	else for ( int i = optind; i < argc; i++ )
	{
		const char* variable = argv[i];
		const char* value = dictionary_get_def(&sa, variable, def);
		if ( !value && test )
			exit(1);
		else if ( !value )
			errx(1, "%s: Variable is unset: %s", path, variable);
		if ( !test && puts(value) < 0 )
			err(2, "stdout");
	}

	if ( ferror(stdout) || fflush(stdout) == EOF )
		err(2, "stdout");

	return 0;
}
