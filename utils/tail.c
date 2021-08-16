/*
 * Copyright (c) 2021 Juhani 'nortti' Krekelä.
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
 * tail.c
 * Output the trailing part of files.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <unistd.h>

#ifdef HEAD
#define TAIL false
#else
#define TAIL true
#endif

static bool byte_mode = false;
static bool beginning_relative = !TAIL;
static off_t file_offset = 10;
static size_t buffer_size = 10;
static bool verbose = false;
static bool quiet = false;
static bool follow = false;
static bool path_mode = false;

static void xputchar(int c)
{
	if ( putchar(c) < 0 )
		err(1, "stdout");
}

static void xputstr(const char* str)
{
	if ( fputs(str, stdout) < 0 )
		err(1, "stdout");
}

static int from_beginning(FILE* fp, const char* filename)
{
	int failure = 0;

	off_t offset = 0;
	while ( true )
	{
		offset++;

		if ( !TAIL && file_offset < offset )
			break;

		char* line = NULL;
		size_t size = 0;
		int c = 0;
		ssize_t length = 0;
		if ( byte_mode )
			c = fgetc(fp);
		else
			length = getline(&line, &size, fp);
		if ( c < 0 || length < 0 )
		{
			if ( !feof(fp) )
			{
				warn("%s", filename);
				failure = 1;
			}
			if ( !byte_mode )
				free(line);
			break;
		}

		if ( (TAIL && file_offset <= offset) ||
		     (!TAIL && offset <= file_offset) )
		{
			if ( byte_mode )
				xputchar(c);
			else
				xputstr(line);
		}

		if ( !byte_mode )
			free(line);
	}

	return failure;
}

static int from_end(FILE* fp, const char* filename)
{
	int failure = 0;

	void* buffer = calloc(buffer_size, byte_mode ? 1 : sizeof(char*));
	if ( !buffer )
		err(1, "calloc");
	char** lines = !byte_mode ? buffer : NULL;
	unsigned char* bytes = byte_mode ? buffer : NULL;
	size_t buffer_used = 0;

	size_t offset = 0;
	while ( true )
	{
		offset++;

		char* line = NULL;
		size_t size = 0;
		int c = 0;
		ssize_t length = 0;
		if ( byte_mode )
			c = fgetc(fp);
		else
			length = getline(&line, &size, fp);
		if ( c < 0 || length < 0 )
		{
			if ( !feof(fp) )
			{
				warn("%s", filename);
				failure = 1;
			}
			free(line);
			break;
		}

		if ( TAIL )
		{
			if ( buffer_size == 0 )
			{
				free(line);
				break;
			}

			size_t index = offset % buffer_size;
			if ( buffer_used < buffer_size )
				buffer_used++;
			else if ( !byte_mode )
				free(lines[index]);

			if ( byte_mode )
				bytes[index] = c;
			else
				lines[index] = line;
		}
		else
		{
			if ( buffer_size == 0 )
			{
				if ( byte_mode )
					xputchar(c);
				else
					xputstr(line);
				free(line);
				continue;
			}

			size_t index = offset % buffer_size;
			if ( buffer_used < buffer_size )
				buffer_used++;
			else
			{
				if ( byte_mode )
					xputchar(bytes[index]);
				else
				{
					xputstr(lines[index]);
					free(lines[index]);
				}
			}

			if ( byte_mode )
				bytes[index] = c;
			else
				lines[index] = line;
		}
	}

	if ( TAIL )
	{
		for ( size_t i = 0; i < buffer_used; i++ )
		{
			size_t index = (offset - buffer_used + i) % buffer_size;
			if ( byte_mode )
				xputchar(bytes[index]);
			else
				xputstr(lines[index]);
		}
	}

	if ( !byte_mode )
	{
		for ( size_t i = 0; i < buffer_used; i++ )
		{
			size_t index = (offset - buffer_used + i) % buffer_size;
			free(lines[index]);
		}
	}
	free(buffer);

	return failure;
}

static int process_file(FILE* fp, const char* filename)
{
	int failure = beginning_relative ? from_beginning(fp, filename) :
	                                   from_end(fp, filename);

	if ( fflush(stdout) != 0 )
		err(1, "fflush");

	return failure;
}

noreturn static void follow_file(FILE* fp, const char* filename)
{
	off_t previous_size = ftello(fp);
	if ( previous_size == -1 && errno != ESPIPE )
		warn("ftello: %s", filename);
	int reopen_errno = 0;
	while ( true )
	{
		// Currently we have nothing like kqueue or inotify, so keep polling.
		// This is used also by e.g. 2.9BSD tail(1) and GNU tail(1) as fallback.
		sleep(1);

		struct stat st;
		if ( fstat(fileno(fp), &st) < 0 )
			err(1, "fstat");

		if ( st.st_size < previous_size )
		{
			warnx("File truncated: %s", filename);
			if ( fseeko(fp, 0, SEEK_END) < 0 )
				warn("fseeko: %s", filename);
		}
		previous_size = st.st_size;

		bool reopen_file = false;
		struct stat path_st;
		if ( fp != stdin && path_mode && stat(filename, &path_st) == 0 &&
			 (st.st_dev != path_st.st_dev || st.st_ino != path_st.st_ino) )
		{
			reopen_file = true;
			previous_size = path_st.st_size;
		}

		clearerr(fp);
		while ( true )
		{
			int c = fgetc(fp);
			if ( c < 0 && feof(fp) )
				break;
			else if ( c < 0 )
				err(1, "%s", filename);
			xputchar(c);
		}

		if ( fflush(stdout) != 0 )
			err(1, "stdout");

		// Reopen file only after draining the old one, in case there was any
		// change of contents before the file was replaced.
		// Additionally, keep the old file open as long as we can't succesfully
		// open the new one.
		if ( reopen_file )
		{
			FILE* new_fp = fopen(filename, "r");
			if ( new_fp )
			{
				fclose(fp);
				fp = new_fp;
				reopen_errno = 0;
				warnx("reopened: %s", filename);
			}
			else if ( errno != reopen_errno )
			{
				warn("reopening: %s", filename);
				reopen_errno = errno;
			}
		}
	}
}

static void parse_number(const char* num_string)
{
	// Handle explicit marking of whether it's relative to beginning or end.
	const char* str = num_string;

	if ( *str == '+' )
	{
		beginning_relative = true;
		str++;
	}
	else if ( *str == '-' )
	{
		beginning_relative = false;
		str++;
	}

	if ( *str == '+' || *str == '-' )
		errx(1, "Invalid number of %s: %s",
		     byte_mode ? "bytes" : "lines", num_string);

	char* end;
	errno = 0;
	intmax_t parsed = strtoimax(str, &end, 10);
	if ( errno == ERANGE ||
		 (beginning_relative && (off_t) parsed != parsed) ||
		 (!beginning_relative && (size_t) parsed != (uintmax_t) parsed) )
		errx(1, "Number of %s too large",
			 byte_mode ? "bytes" : "lines");
	if ( !*str || *end || errno )
		errx(1, "Invalid number of %s: %s",
			 byte_mode ? "bytes" : "lines", num_string);

	if ( beginning_relative )
		file_offset = parsed;
	else
		buffer_size = parsed;
}

int main(int argc, char* argv[])
{
	const struct option longopts[] =
	{
		{"bytes", required_argument, NULL, 'c'},
		{"lines", required_argument, NULL, 'n'},
		{"follow", no_argument, NULL, 'f'},
		{"quiet", no_argument, NULL, 'q'},
		{"verbose", no_argument, NULL, 'v'},
		{0, 0, 0, 0}
	};
#ifndef HEAD
	const char* opts = "c:fFn:qv";
#else
	const char* opts = "c:n:qv";
#endif
	while ( true )
	{
		// Handle the historical but still widely used -num option format.
		if ( optind < argc && argv[optind][0] == '-' &&
		     isdigit((unsigned char) argv[optind][1]) )
		{
			parse_number(&argv[optind][1]);
			optind++;
			continue;
		}

		int opt = getopt_long(argc, argv, opts, longopts, NULL);
		if ( opt == -1 )
			break;
		switch ( opt )
		{
			case 'c':
			case 'n':
				byte_mode = opt == 'c';
				parse_number(optarg);
				break;
			case 'f': follow = true; path_mode = false; break;
			case 'F': follow = true; path_mode = true; break;
			case 'q': quiet = true; verbose = false; break;
			case 'v': verbose = true; quiet = false; break;
			default: return 1;
		}
	}

	if ( follow && 1 < argc - optind )
		errx(1, "-%c cannot be used with multiple files",
		     path_mode ? 'F' : 'f');

	int failure = 0;

	if ( argc - optind < 1 )
	{
		if ( verbose )
		{
			if ( printf("==> standard input <==\n") < 0 )
				err(1, "stdout");
		}

		failure |= process_file(stdin, "standard input");

		// -f is ignored if stdin is a fifo.
		struct stat st;
		if ( fstat(0, &st) < 0 )
			err(1, "fstat");
		if ( follow && !S_ISFIFO(st.st_mode) )
			follow_file(stdin, "standard input");
	}
	else
	{
		for ( int i = optind; i < argc; i++ )
		{
			bool is_stdin = !strcmp(argv[i], "-");
			const char* filename = is_stdin ? "standard input" : argv[i];
			if ( verbose || (!quiet && 1 < argc - optind) )
			{
				if ( printf("%s==> %s <==\n", i == optind ? "" : "\n",
				            filename) < 0 )
					err(1, "stdout");
			}

			FILE* fp = is_stdin ? stdin : fopen(argv[i], "r");
			if ( !fp )
			{
				warn("%s", argv[i]);
				failure = 1;
				continue;
			}

			failure |= process_file(fp, filename);

			struct stat st;
			if ( fstat(fileno(fp), &st) < 0 )
				err(1, "fstat");
			if ( follow && (!is_stdin || !S_ISFIFO(st.st_mode)) )
				follow_file(fp, filename);

			if ( !is_stdin )
				fclose(fp);
		}
	}

	return failure;
}
