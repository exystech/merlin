/*
 * Copyright (c) 2011, 2012, 2013, 2014, 2015, 2016 Jonas 'Sortie' Termansen.
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
 * uname.c
 * Print system information.
 */

#include <sys/utsname.h>

#include <errno.h>
#include <error.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int PRINT_KERNELNAME = 1 << 0;
static const int PRINT_NODENAME = 1 << 1;
static const int PRINT_KERNELREL = 1 << 2;
static const int PRINT_KERNELVER = 1 << 3;
static const int PRINT_TAGLINE = 1 << 4;
static const int PRINT_MACHINE = 1 << 5;
static const int PRINT_PROCESSOR = 1 << 6;

const char* print(const char* prefix, const char* msg)
{
	printf("%s%s", prefix, msg);
	return " ";
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

static void help(FILE* fp, const char* argv0)
{
	fprintf(fp, "Usage: %s [OPTION]...\n", argv0);
	fprintf(fp, "Print certain system information.\n");
}

static void version(FILE* fp, const char* argv0)
{
	fprintf(fp, "%s (Sortix) %s\n", argv0, VERSIONSTR);
}

int main(int argc, char* argv[])
{
	const char* argv0 = argv[0];
	int flags = 0;
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
			case 'a': flags |= INT_MAX; break;
			case 'm': flags |= PRINT_MACHINE; break;
			case 'n': flags |= PRINT_NODENAME; break;
			case 'p': flags |= PRINT_PROCESSOR; break;
			case 'r': flags |= PRINT_KERNELREL; break;
			case 's': flags |= PRINT_KERNELNAME; break;
			case 't': flags |= PRINT_TAGLINE; break;
			case 'v': flags |= PRINT_KERNELVER; break;
			default:
				fprintf(stderr, "%s: unknown option -- '%c'\n", argv0, c);
				help(stderr, argv0);
				exit(1);
			}
		}
		else if ( !strcmp(arg, "--kernel-name") )
			flags |= PRINT_KERNELNAME;
		else if ( !strcmp(arg, "--kernel-release") )
			flags |= PRINT_KERNELREL;
		else if ( !strcmp(arg, "--kernel-version") )
			flags |= PRINT_KERNELVER;
		else if ( !strcmp(arg, "--machine") )
			flags |= PRINT_MACHINE;
		else if ( !strcmp(arg, "--nodename") )
			flags |= PRINT_NODENAME;
		else if ( !strcmp(arg, "--processor") )
			flags |= PRINT_PROCESSOR;
		else if ( !strcmp(arg, "--tagline") )
			flags |= PRINT_TAGLINE;
		else if ( !strcmp(arg, "--help") )
			help(stdout, argv0), exit(0);
		else if ( !strcmp(arg, "--version") )
			version(stdout, argv0), exit(0);
		else
		{
			fprintf(stderr, "%s: unknown option: %s\n", argv0, arg);
			help(stderr, argv0);
			exit(1);
		}
	}

	compact_arguments(&argc, &argv);

	if ( 1 < argc )
		error(1, 0, "extra operand");

	static struct utsname utsname;
	if ( uname(&utsname) < 0 )
		error(1, errno, "uname");

	if ( !flags )
		flags = PRINT_KERNELNAME;

	const char* prefix = "";
	if ( flags & PRINT_KERNELNAME )
		prefix = print(prefix, utsname.sysname);
	if ( flags & PRINT_NODENAME )
		prefix = print(prefix, utsname.nodename);
	if ( flags & PRINT_KERNELREL )
		prefix = print(prefix, utsname.release);
	if ( flags & PRINT_TAGLINE )
		prefix = print(prefix, utsname.tagline);
	if ( flags & PRINT_KERNELVER )
		prefix = print(prefix, utsname.version);
	if ( flags & PRINT_MACHINE )
		prefix = print(prefix, utsname.machine);
	if ( flags & PRINT_PROCESSOR )
		prefix = print(prefix, utsname.processor);
	printf("\n");

	return 0;
}
