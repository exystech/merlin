/*
 * Copyright (c) 2012, 2014 Jonas 'Sortie' Termansen.
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
 * which.c
 * Locate a program in the PATH.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// NOTE: The PATH-searching logic is repeated multiple places. Until this logic
//       can be shared somehow, you need to keep this comment in sync as well
//       as the logic in these files:
//         * kernel/process.cpp
//         * libc/unistd/execvpe.c
//         * utils/which.c
// NOTE: See comments in execvpe() for algorithmic commentary.

bool Which(const char* filename, const char* path, bool all)
{
	bool found = false;

	bool search_path = !strchr(filename, '/') && path;
	bool any_tries = false;
	bool any_eacces = false;

	while ( search_path && *path )
	{
		size_t len = strcspn(path, ":");
		if ( !len )
		{
			path++;
			continue;
		}

		any_tries = true;

		char* dirpath = strndup(path, len);
		if ( !dirpath )
			return -1;
		if ( (path += len)[0] == ':' )
			path++;
		while ( len && dirpath[len - 1] == '/' )
			dirpath[--len] = '\0';

		char* fullpath;
		if ( asprintf(&fullpath, "%s/%s", dirpath, filename) < 0 )
			return free(dirpath), -1;

		if ( access(fullpath, X_OK) == 0 )
		{
			found = true;
			printf("%s\n", fullpath);
			free(fullpath);
			free(dirpath);
			if ( all )
				continue;
			break;
		}

		free(fullpath);
		free(dirpath);

		if ( errno == ENOENT )
			continue;

		if ( errno == ELOOP ||
		     errno == EISDIR ||
		     errno == ENAMETOOLONG ||
		     errno == ENOTDIR )
			continue;

		if ( errno == EACCES )
		{
			any_eacces = true;
			continue;
		}

		if ( all )
			continue;

		break;
	}

	if ( !any_tries )
	{
		if ( access(filename, X_OK) == 0 )
		{
			found = true;
			printf("%s\n", filename);
		}
	}

	(void) any_eacces;

	return found;
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
	fprintf(fp, "Usage: %s [-a] FILENAME...\n", argv0);
	fprintf(fp, "Locate a program in the PATH.\n");
}

static void version(FILE* fp, const char* argv0)
{
	fprintf(fp, "%s (Merlin) %s\n", argv0, VERSIONSTR);
}

int main(int argc, char* argv[])
{
	const char* argv0 = argv[0];
	bool all = false;
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
			case 'a': all = true; break;
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
		else
		{
			fprintf(stderr, "%s: unknown option: %s\n", argv0, arg);
			help(stderr, argv0);
			exit(1);
		}
	}

	compact_arguments(&argc, &argv);

	if ( argc < 2 )
		error(1, 0, "missing operand");

	const char* path = getenv("PATH");

	bool success = true;
	for ( int i = 1; i < argc; i++ )
	{
		const char* arg = argv[i];
		if ( !Which(arg, path, all) )
			success = false;
	}

	return success ? 0 : 1;
}
