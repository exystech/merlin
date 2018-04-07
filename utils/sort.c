/*
 * Copyright (c) 2014, 2015, 2018 Jonas 'Sortie' Termansen.
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
 * sort.c
 * Sort, merge, or sequence check text files.
 */

#include <err.h>
#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// TODO: Implement all the features mandated by POSIX.
// TODO: Implement the useful GNU extensions.

static int flip_comparison(int rel)
{
	return rel < 0 ? 1 : 0 < rel ? -1 : 0;
}

static int indirect_compare(int (*compare)(const char*, const char*),
                            const void* a_ptr, const void* b_ptr)
{
	const char* a = *(const char* const*) a_ptr;
	const char* b = *(const char* const*) b_ptr;
	return compare(a, b);
}

static int compare_line(const char* a, const char* b)
{
	return strcoll(a, b);
}

static int indirect_compare_line(const void* a_ptr, const void* b_ptr)
{
	return indirect_compare(compare_line, a_ptr, b_ptr);
}

static int compare_line_reverse(const char* a, const char* b)
{
	return flip_comparison(compare_line(a, b));
}

static int indirect_compare_line_reverse(const void* a_ptr, const void* b_ptr)
{
	return indirect_compare(compare_line_reverse, a_ptr, b_ptr);
}

static int compare_version(const char* a, const char* b)
{
	return strverscmp(a, b);
}

static int indirect_compare_version(const void* a_ptr, const void* b_ptr)
{
	return indirect_compare(compare_version, a_ptr, b_ptr);
}

static int compare_version_reverse(const char* a, const char* b)
{
	return flip_comparison(compare_version(a, b));
}

static int indirect_compare_version_reverse(const void* a_ptr, const void* b_ptr)
{
	return indirect_compare(compare_version_reverse, a_ptr, b_ptr);
}

struct input_stream
{
	const char* const* files;
	size_t files_current;
	size_t files_length;
	FILE* current_file;
	const char* last_file_path;
	uintmax_t last_line_number;
};

static char* read_line(FILE* fp, const char* fpname, int delim)
{
	char* line = NULL;
	size_t line_size = 0;
	ssize_t amount = getdelim(&line, &line_size, delim, fp);
	if ( amount < 0 )
	{
		free(line);
		if ( ferror(fp) )
			err(2, "read: %s", fpname);
		return NULL;
	}
	if ( (unsigned char) line[amount-1] == (unsigned char) delim )
		line[amount-1] = '\0';
	return line;
}

static char* read_input_stream_line(struct input_stream* is, int delim)
{
	if ( !is->files_length )
	{
		char* result = read_line(stdin, "<stdin>", delim);
		is->last_file_path = "-";
		if ( result )
			is->last_line_number++;
		return result;
	}
	while ( is->files_current < is->files_length )
	{
		const char* path = is->files[is->files_current];
		if ( !is->current_file )
		{
			is->last_line_number = 0;
			if ( !strcmp(path, "-") )
				is->current_file = stdin;
			else if ( !(is->current_file = fopen(path, "r")) )
				err(2, "%s", path);
		}
		char* result = read_line(is->current_file, path, delim);
		if ( !result )
		{
			if ( is->current_file != stdin )
				fclose(is->current_file);
			is->current_file = NULL;
			is->files_current++;
			continue;
		}
		is->last_file_path = path;
		is->last_line_number++;
		return result;
	}
	return NULL;
}

static char** read_input_stream_lines(size_t* result_num_lines,
                                      struct input_stream* is,
                                      int delim)
{
	char** lines = NULL;
	size_t lines_used = 0;
	size_t lines_length = 0;

	char* line;
	while ( (line = read_input_stream_line(is, delim)) )
	{
		if ( lines_used == lines_length )
		{
			size_t old_lines_length = lines_length ? lines_length : 64;
			char** new_lines = (char**) reallocarray(lines, old_lines_length,
			                                         2 * sizeof(char*));
			if ( !new_lines )
				err(2, "malloc");
			lines = new_lines;
			lines_length = 2 * old_lines_length;
		}
		lines[lines_used++] = line;
	}

	return *result_num_lines = lines_used, lines;
}

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
	setlocale(LC_ALL, "");

	bool check = false;
	bool check_quiet = false;
	bool merge = false;
	const char* output = NULL;
	bool reverse = false;
	bool unique = false;
	bool version_sort = false;
	bool zero_terminated = false;

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
			while ( (c = *++arg) ) switch ( c )
			{
			case 'C': check = true, check_quiet = true; break;
			case 'c': check = true, check_quiet = false; break;
			case 'm': merge = true; break;
			case 'o':
				if ( !*(output = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "option requires an argument -- 'o'");
					output = argv[i+1];
					argv[++i] = NULL;
				}
				arg = "o";
				break;
			case 'r': reverse = true; break;
			case 'u': unique = true; break;
			case 'V': version_sort = true; break;
			case 'z': zero_terminated = true; break;
			default:
				errx(2, "unknown option -- '%c'", c);
			}
		}
		else if ( !strcmp(arg, "--check") ||
		          !strcmp(arg, "--check=diagnose-first") )
			check = true, check_quiet = false;
		else if ( !strcmp(arg, "--check=quiet") ||
		          !strcmp(arg, "--check=silent") )
			check = true, check_quiet = true;
		else if ( !strcmp(arg, "--merge") )
			merge = true;
		else if ( !strncmp(arg, "--output=", strlen("--output=")) )
			output = arg + strlen("--output=");
		else if ( !strcmp(arg, "--output") )
		{
			if ( i + 1 == argc )
				errx(2, "option '--output' requires an argument");
			output = argv[i+1];
			argv[++i] = NULL;
		}
		else if ( !strcmp(arg, "--reverse") )
			reverse = true;
		else if ( !strcmp(arg, "--unique") )
			unique = true;
		else if ( !strcmp(arg, "--version-sort") )
			version_sort = true;
		else if ( !strcmp(arg, "--zero-terminated") )
			zero_terminated = true;
		else
			errx(2, "unknown option: %s", arg);
	}

	compact_arguments(&argc, &argv);

	if ( check_quiet && output )
		errx(1, "the -C and -o options are incompatible");
	if ( check && output )
		errx(1, "the -c and -o options are incompatible");

	int delim = zero_terminated ? '\0' : '\n';

	int (*compare)(const char*, const char*);
	int (*qsort_compare)(const void*, const void*);

	if ( version_sort && reverse )
		compare = compare_version_reverse,
		qsort_compare = indirect_compare_version_reverse;
	else if ( version_sort )
		compare = compare_version,
		qsort_compare = indirect_compare_version;
	else if ( reverse )
		compare = compare_line_reverse,
		qsort_compare = indirect_compare_line_reverse;
	else
		compare = compare_line,
		qsort_compare = indirect_compare_line;

	struct input_stream is;
	memset(&is, 0, sizeof(is));
	is.files = (const char* const*) (argv + 1);
	is.files_current = 0;
	is.files_length = argc - 1;

	if ( check )
	{
		int needed_relation = unique ? 1 : 0;
		char* prev_line = NULL;
		char* line;
		while ( (line = read_input_stream_line(&is, delim)) )
		{
			if ( prev_line && compare(line, prev_line) < needed_relation )
			{
				if ( check_quiet )
					return 1;
				errx(1, "%s:%ju: disorder: %s", is.last_file_path,
				     is.last_line_number, line);
			}
			free(prev_line);
			prev_line = line;
		}
		free(prev_line);
	}
	else
	{
		(void) merge;

		size_t lines_used = 0;
		char** lines = read_input_stream_lines(&lines_used, &is, delim);

		qsort(lines, lines_used, sizeof(*lines), qsort_compare);

		if ( output && !freopen(output, "w", stdout) )
			err(2, "%s", output);

		for ( size_t i = 0; i < lines_used; i++ )
		{
			if ( unique && i && compare(lines[i-1], lines[i]) == 0 )
				continue;
			if ( fputs(lines[i], stdout) == EOF || fputc(delim, stdout) == EOF )
				err(2, "%s", output ? output : "<stdout>");
		}
		if ( fflush(stdout) == EOF )
			err(2, "%s", output ? output : "<stdout>");
	}

	return 0;
}
