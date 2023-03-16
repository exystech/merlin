/*
 * Copyright (c) 2023 Jonas 'Sortie' Termansen.
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
 * pwd/__openent.c
 * Preprocesses include statements in an entry database.
 */

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool __openent_append(FILE* out, const char* path)
{
	FILE* fp = fopen(path, "r");
	if ( !fp )
		return false;
	char* line = NULL;
	size_t size = 0;
	ssize_t amount;
	bool success = true;
	while ( success && 0 < (amount = getline(&line, &size, fp)) )
	{
		size_t length = amount;
		size_t offset = 0;
		while ( offset < length && isspace((unsigned char) line[offset]) )
			offset++;
		if ( !line[offset] || line[offset] == '#' )
			continue;
		size_t include_length = strlen("include");
		if ( !strncmp(line + offset, "include", include_length) &&
		     isspace((unsigned char) line[offset + include_length]) )
		{
			while ( length && isspace((unsigned char) line[length - 1]) )
				line[--length] = '\0';
			offset = offset + include_length;
			while ( offset < length && isspace((unsigned char) line[offset]) )
				offset++;
			const char* pattern = line + offset;
			glob_t gl;
			if ( !glob(pattern, GLOB_NOCHECK, NULL, &gl) )
			{
				for ( size_t i = 0; success && i < gl.gl_pathc; i++ )
					if ( !__openent_append(out, gl.gl_pathv[i]) &&
					     errno != ENOENT )
						success = false;
			}
			else
				success = false;
			globfree(&gl);
			continue;
		}
		if ( fwrite(line, 1, length, out) != length )
			success = false;
	}
	free(line);
	if ( ferror(fp) )
		success = false;
	fclose(fp);
	return success;
}

FILE* __openent(const char* path)
{
	char* data;
	size_t size;
	FILE* memstream = open_memstream(&data, &size);
	if ( !memstream )
		return NULL;
	if ( !__openent_append(memstream, path) ||
	     ferror(memstream) || fflush(memstream) == EOF )
	{
		fclose(memstream);
		free(data);
		return NULL;
	}
	fclose(memstream);
	size = strlen(data);
	FILE* result = fmemopen(NULL, size, "r+");
	if ( !result )
		return free(data), NULL;
	size_t amount = fwrite(data, 1, size, result);
	free(data);
	if ( amount != size || fflush(result) == EOF )
		return fclose(result), NULL;
	rewind(result);
	return result;
}
