/*
 * Copyright (c) 2014, 2022 Jonas 'Sortie' Termansen.
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
 * proper-sh.c
 * Forward execution to the best shell.
 */

#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool is_supported(int argc, char* argv[])
{
	int i;
	for ( i = 1; i < argc; i++ )
	{
		const char* arg = argv[i];
		if ( (arg[0] != '-' && arg[0] != '+') || !arg[1] )
			break;  // Intentionally not continue and note '+' support.
		if ( !strcmp(arg, "--") )
			break;
		if ( arg[0] == '+' || arg[1] != '-' )
		{
			char c;
			while ( (c = *++arg) ) switch ( c )
			{
			case 'e': break;
			case 'i': break;
			case 'l': break;
			case 's': break;
			default: return false;
			}
		}
		else if ( !strcmp(arg, "--help") )
			;
		else if ( !strcmp(arg, "--version") )
			;
		else
			return false;
	}
	return i == argc;
}

int main(int argc, char* argv[])
{
	if ( !isatty(0) || !isatty(1) || !is_supported(argc, argv) )
	{
		const char* env = getenv("SORTIX_SH_BACKEND");
		if ( env && env[0] )
		{
			execvp(env, argv);
			if ( errno != ENOENT )
				err(127, "%s", env);
		}

		FILE* fp = fopen("/etc/proper-sh", "r");
		if ( !fp && errno != ENOENT )
			err(127, "/etc/proper-sh");
		if ( fp )
		{
			char* proper_sh = NULL;
			size_t proper_sh_size = 0;
			ssize_t proper_sh_length = getline(&proper_sh, &proper_sh_size, fp);
			if ( proper_sh_length < 0 )
				err(127, "/etc/proper-sh");
			if ( proper_sh_length && proper_sh[proper_sh_length - 1] == '\n' )
				proper_sh[--proper_sh_length] = '\0';
			if ( proper_sh[0] )
			{
				execvp(proper_sh, argv);
				if ( errno != ENOENT )
					err(127, "%s", proper_sh);
			}
			free(proper_sh);
			fclose(fp);
		}

		execvp("dash", argv);
		if ( errno != ENOENT )
			err(127, "dash");
	}

	execvp("sortix-sh", argv);
	err(127, "sortix-sh");
}
