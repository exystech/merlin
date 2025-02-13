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
 * tix-execpatch.c
 * Patches the executable bits of files in the current source directory.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

int fgetc_or_die(FILE* input, const char* input_path, size_t* line,
                 size_t* column)
{
	int result = fgetc(input);
	if ( result == '\n' )
		(*line)++, *column = 0;
	else
		(*column)++;
	if ( result == EOF && ferror(input) )
		err(1, "read: `%s'", input_path);
	return result;
}

int fgetc_or_die_eof(FILE* input, const char* input_path, size_t* line,
                     size_t* column)
{
	int result = fgetc_or_die(input, input_path, line, column);
	if ( result == EOF )
		errx(1, "%s:%zu:%zu: unexpected end of file",
		        input_path, *line, *column);
	return result;
}

bool check_eof(FILE* input, const char* input_path)
{
	size_t line = 0;
	size_t column = 0;
	int c = fgetc_or_die(input, input_path, &line, &column);
	if ( c != EOF )
	{
		ungetc(c, input);
		return false;
	}
	return true;
}

void parse_fixed(const char* text, FILE* input, const char* input_path,
                 size_t* line, size_t* column)
{
	for ( size_t i = 0; text[i]; i++ )
	{
		int ic = fgetc_or_die(input, input_path, line, column);
		if ( ic == EOF )
			errx(1, "%s:%zu:%zu: unexpected end of file, expected `%s'",
			        input_path, *line, *column, text + i);
		if ( ic != (unsigned char) text[i] )
			errx(1, "%s:%zu:%zu: parse error, expected `%s'", input_path,
			        *line, *column, text + i);
	}
}

bool execpatch(FILE* input, const char* input_path, bool check)
{
	char* buffer = NULL;
	size_t buffer_used = 0;
	size_t buffer_length = 0;

	bool result = true;
	size_t line = 1;
	while ( !check_eof(input, input_path) )
	{
		buffer_used = 0;
		size_t column = 0;
		parse_fixed("chmod ", input, input_path, &line, &column);
		bool plus;
		switch ( fgetc_or_die(input, input_path, &line, &column) )
		{
		case '-': plus = false; break;
		case '+': plus = true; break;
		default:
			errx(1, "%s:%zu:%zu: parse error, expected '-' or '+'",
			        input_path, line, column);
		}
		parse_fixed("x -- '", input, input_path, &line, &column);
		while ( true )
		{
			int ic = fgetc_or_die_eof(input, input_path, &line, &column);
			if ( ic == '\'' )
			{
				ic = fgetc_or_die(input, input_path, &line, &column);
				if ( ic == EOF || ic == '\n' )
					break;
				ungetc(ic, input);
				parse_fixed("\\''", input, input_path, &line, &column);
				ic = '\'';
			}
			if ( buffer_used == buffer_length )
			{
				size_t new_length = buffer_length ? 2 * buffer_length : 16;
				buffer = (char*) realloc(buffer, sizeof(char) * (new_length + 1));
				buffer_length = new_length;
			}
			buffer[buffer_used++] = ic;
		}
		if ( !buffer_used )
			errx(1, "%s:%zu: unexpected empty path", input_path, line);
		assert(buffer_length);
		buffer[buffer_used] = '\0';
		if ( buffer[0] == '/' )
			err(1, "%s:%zu: unexpected absolute path", input_path, line);
		if ( does_path_contain_dotdot(buffer) )
			errx(1, "%s:%zu: unexpected path with ..", input_path, line);
		if ( check )
			continue;
		struct stat st;
		if ( fstatat(AT_FDCWD, buffer, &st, AT_SYMLINK_NOFOLLOW) != 0 )
		{
			warn("chmod %cx: `%s'", plus ? '+' : '-', buffer);
			result = false;
			continue;
		}
		mode_t new_mode = st.st_mode;
		if ( plus )
			new_mode |= 0111 & ~get_umask_value();
		else
			new_mode &= ~0111;
		if ( fchmodat(AT_FDCWD, buffer, new_mode, 0) != 0 )
		{
			warn("chmod %cx: `%s'", plus ? '+' : '-', buffer);
			result = false;
			continue;
		}
	}
	return result;
}

static void help(FILE* fp, const char* argv0)
{
	fprintf(fp, "Usage: %s [OPTION]... [PATCH]\n", argv0);
	fprintf(fp, "Patches the executable bits of files in the current source directory.\n");
}

static void version(FILE* fp, const char* argv0)
{
	fprintf(fp, "%s (Sortix) %s\n", argv0, VERSIONSTR);
}

int main(int argc, char* argv[])
{
	bool check = false;
	char* directory = NULL;

	const char* argv0 = argv[0];
	for ( int i = 0; i < argc; i++ )
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
			while ( (c = *++arg) ) switch ( c )
			{
			case 'c': check = true; break;
			default:
				fprintf(stderr, "%s: unknown option -- '%c'\n", argv0, c);
				help(stderr, argv0);
				exit(1);
			}
		}
		else if ( !strcmp(arg, "--help") )
			help(stdout, argv0), exit(0);
		else if ( !strcmp(arg, "--version") )
			version(stdout, argv0), exit(0);
		else if ( !strcmp(arg, "--check") )
			check = true;
		else if ( GET_OPTION_VARIABLE("--directory", &directory) ) { }
		else
		{
			fprintf(stderr, "%s: unknown option: %s\n", argv0, arg);
			help(stderr, argv0);
			exit(1);
		}
	}

	compact_arguments(&argc, &argv);

	if ( 2 < argc )
		errx(1, "extra operand");

	const char* input_path = "<stdin>";
	FILE* input = stdin;

	if ( argc == 2 )
	{
		input_path = argv[1];
		if ( !(input = fopen(input_path, "r")) )
			err(1, "`%s'", input_path);
	}

	if ( directory && chdir(directory) != 0 )
		err(1, "chdir: `%s'", directory);
	free(directory);

	return execpatch(input, input_path, check) ? 0 : 1;
}
