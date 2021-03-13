/*
 * Copyright (c) 2021 Jonas 'Sortie' Termansen.
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
 * nl.c
 * Number lines.
 */

#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum format
{
	LN,
	RN,
	RZ,
};

struct style
{
	char c;
	regex_t regex;
};

static char delim[] = "\\:\\:\\:";
static off_t initial = 1;
static off_t increment = 1;
static bool restart = false;
static off_t blank_join = 1;
static enum format format = RN;
static int width = 6;
static const char* separator = "\t";
static struct style styles[] =
{
	{.c = 'n'},
	{.c = 't'},
	{.c = 'n'},
};

static off_t line_number;
static size_t state = 1;
static off_t blanks = 0;

static bool nl(FILE* fp, const char* path)
{
	char* line = NULL;
	size_t line_size = 0;
	ssize_t line_length;
	while ( 0 < (line_length = getline(&line, &line_size, fp)) )
	{
		if ( line[line_length - 1] == '\n' )
			line[--line_length] = '\0';
		bool error = false;
		if ( delim[0] && !strcmp(line, delim) )
		{
			state = 0;
			if ( restart )
				line_number = initial;
			error = printf("\n") == EOF;
		}
		else if ( delim[0] && !strcmp(line, delim + 2) )
		{
			state = 1;
			error = printf("\n") == EOF;
		}
		else if ( delim[0] && !strcmp(line, delim + 4) )
		{
			state = 2;
			error = printf("\n") == EOF;
		}
		else
		{
			const struct style* style = &styles[state];
			bool counts =
				style->c == 'a' ||
				(style->c == 't' && strcmp(line, "") != 0) ||
				(style->c == 'p' && !regexec(&style->regex, line, 0, NULL, 0));
			if ( counts )
			{
				if ( !strcmp(line, "") )
				{
					blanks++;
					if ( blank_join <= blanks )
						blanks = 0;
					else
						counts = false;
				}
				else
					blanks = 0;
			}
			if ( counts )
			{
				switch ( format )
				{
				case LN:
					error = printf("%-*ji%s%s\n", width, (intmax_t) line_number,
					               separator, line) == EOF;
					break;
				case RN:
					error = printf("%*ji%s%s\n", width, (intmax_t) line_number,
					               separator, line) == EOF;
					break;
				case RZ:
					error = printf("%0*ji%s%s\n", width, (intmax_t) line_number,
					               separator, line) == EOF;
					break;
				}
				line_number += increment;
			}
			else
				error = printf("%*s %s\n", width, "", line) == EOF;
		}
		if ( error )
		{
			warn("stdout");
			free(line);
			return false;
		}
	}
	free(line);
	if ( ferror(fp) )
	{
		warn("%s", path);
		return false;
	}
	return true;
}

static bool nl_path(const char* path)
{
	if ( !strcmp(path, "-") )
		return nl(stdin, path);
	FILE* fp = fopen(path, "r");
	if ( !fp )
	{
		warn("%s", path);
		return false;
	}
	bool ret = nl(fp, path);
	fclose(fp);
	return ret;
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

static void set_style(struct style* style, const char* string, const char* opt)
{
	if ( style->c == 'p' )
		regfree(&style->regex);
	if ( !strcmp(string, "a") || !strcmp(string, "t") || !strcmp(string, "n") )
		style->c = string[0];
	else if ( string[0] == 'p' || string[0] == 'E' )
	{
		int error = regcomp(&style->regex, string + 1,
		                    string[0] == 'E' ? REG_EXTENDED : 0);
		if ( error )
		{
			size_t size = regerror(error, NULL, NULL, 0);
			char* error_string = malloc(size);
			if ( !error_string )
				errx(1, "%s: Invalid regular expression: %s", opt, string);
			regerror(error, NULL, error_string, size);
			errx(1, "%s: %s: %s", opt, error_string, string);
		}
		style->c = 'p';
	}
	else
		errx(1, "%s: Invalid style: %s", opt, string);
}

int main(int argc, char* argv[])
{
	const char* parameter;

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
			case 'b':
				if ( !*(parameter = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "option requires an argument -- 'b'");
					parameter = argv[i+1];
					argv[++i] = NULL;
				}
				set_style(&styles[1], parameter, "-b");
				arg = "b";
				break;
			case 'd':
				if ( !*(parameter = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "option requires an argument -- 'd'");
					parameter = argv[i+1];
					argv[++i] = NULL;
				}
				if ( !parameter[0] )
					memset(delim, 0, sizeof(delim));
				else if ( !parameter[1] )
				{
					delim[0] = parameter[0];
					delim[1] = ':';
					delim[2] = parameter[0];
					delim[3] = ':';
					delim[4] = parameter[0];
					delim[5] = ':';
				}
				else if ( !parameter[2] )
				{
					delim[0] = parameter[0];
					delim[1] = parameter[1];
					delim[2] = parameter[0];
					delim[3] = parameter[1];
					delim[4] = parameter[0];
					delim[5] = parameter[1];
				}
				else
					errx(1, "-d: Delimiter must be one or two characters");
				arg = "d";
				break;
			case 'f':
				if ( !*(parameter = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "option requires an argument -- 'f'");
					parameter = argv[i+1];
					argv[++i] = NULL;
				}
				set_style(&styles[2], parameter, "-f");
				arg = "f";
				break;
			case 'h':
				if ( !*(parameter = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "option requires an argument -- 'h'");
					parameter = argv[i+1];
					argv[++i] = NULL;
				}
				set_style(&styles[0], parameter, "-h");
				arg = "h";
				break;
			case 'i':
			{
				if ( !*(parameter = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "option requires an argument -- 'i'");
					parameter = argv[i+1];
					argv[++i] = NULL;
				}
				char* end;
				errno = 0;
				intmax_t value = strtoimax(parameter, &end, 10);
				if ( value < 0 || (off_t) value != value || *end )
					errno = ERANGE;
				if ( errno )
					err(1, "-i: %s", parameter);
				increment = (off_t) value;
				arg = "i";
				break;
			}
			case 'l':
			{
				if ( !*(parameter = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "option requires an argument -- 'l'");
					parameter = argv[i+1];
					argv[++i] = NULL;
				}
				char* end;
				errno = 0;
				intmax_t value = strtoimax(parameter, &end, 10);
				if ( value < 0 || (off_t) value != value || *end )
					errno = ERANGE;
				if ( errno )
					err(1, "-l: %s", parameter);
				blank_join = (off_t) value;
				arg = "l";
				break;
			}
			case 'n':
				if ( !*(parameter = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "option requires an argument -- 'n'");
					parameter = argv[i+1];
					argv[++i] = NULL;
				}
				if ( !strcmp(parameter, "ln") )
					format = LN;
				else if ( !strcmp(parameter, "rn") )
					format = RN;
				else if ( !strcmp(parameter, "rz") )
					format = RZ;
				else
					errx(1, "-n: Invalid format: %s", parameter);
				arg = "n";
				break;
			case 'p': restart = false; break;
			case 's':
				if ( !*(separator = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "option requires an argument -- 's'");
					separator = argv[i+1];
					argv[++i] = NULL;
				}
				arg = "s";
				break;
			case 'v':
			{
				if ( !*(parameter = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "option requires an argument -- 'v'");
					parameter = argv[i+1];
					argv[++i] = NULL;
				}
				char* end;
				errno = 0;
				intmax_t value = strtoimax(parameter, &end, 10);
				if ( value < 0 || (off_t) value != value || *end )
					errno = ERANGE;
				if ( errno )
					err(1, "-v: %s", parameter);
				initial = (off_t) value;
				arg = "v";
				break;
			}
			case 'w':
			{
				if ( !*(parameter = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "option requires an argument -- 'w'");
					parameter = argv[i+1];
					argv[++i] = NULL;
				}
				char* end;
				errno = 0;
				long value = strtol(parameter, &end, 10);
				if ( value < 0 || (int) value != value || *end )
					errno = ERANGE;
				if ( errno )
					err(1, "-w: %s", parameter);
				width = (int) value;
				arg = "w";
				break;
			}
			default:
				errx(2, "unknown option -- '%c'", c);
			}
		}
		else
			errx(2, "unknown option: %s", arg);
	}

	compact_arguments(&argc, &argv);

	line_number = initial;

	int ret = 0;

	if ( argc == 1 )
	{
		if ( !nl(stdin, "-") )
			ret = 1;
	}
	else
	{
		for ( int i = 1; i < argc; i++ )
		{
			if ( !nl_path(argv[i]) )
				ret = 1;
		}
	}

	if ( ferror(stdout) || fflush(stdout) == EOF )
		return 1;
	return ret;
}
