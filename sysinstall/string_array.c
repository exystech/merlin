/*
 * Copyright (c) 2020 Jonas 'Sortie' Termansen.
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
 * string_array.c
 * String array utility functions.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "string_array.h"

bool string_array_init(char*** array, size_t* count, size_t* length)
{
	*count = 0;
	*length = 0;
	size_t initial_length = 4;
	if ( !(*array = calloc(initial_length, sizeof(char*))) )
		return false;
	*length = initial_length;
	return true;
}

void string_array_free(char*** array, size_t* count, size_t* length)
{
	for ( size_t i = 0; i < *count; i++ )
		free((*array)[i]);
	free((*array));
	*array = NULL;
	*count = 0;
	*length = 0;
}

bool string_array_append_nodup(char*** array,
                               size_t* count,
                               size_t* length,
                               char* str)
{
	if ( *count == *length )
	{
		char** new_array =
			reallocarray(*array, *length, 2 * sizeof(char*));
		if ( !new_array )
			return false;
		*array = new_array;
		*length *= 2;
	}
	(*array)[(*count)++] = str;
	return true;
}

bool string_array_append(char*** array,
                         size_t* count,
                         size_t* length,
                         const char* str)
{
	char* dup = strdup(str);
	if ( !dup )
		return false;
	if ( !string_array_append_nodup(array, count, length, dup) )
	{
		free(dup);
		return false;
	}
	return true;
}

static int strcmp_indirect(const void* a_ptr, const void* b_ptr)
{
	const char* a = *(const char* const*) a_ptr;
	const char* b = *(const char* const*) b_ptr;
	return strcmp(a, b);
}

void string_array_sort_strcmp(char** array, size_t count)
{
	qsort(array, count, sizeof(char*), strcmp_indirect);
}

static int search_strcmp(const void* str_ptr, const void* elem_ptr)
{
	const char* str = (const char*) str_ptr;
	char* elem = *(char**) elem_ptr;
	return strcmp(str, elem);
}

bool string_array_contains_bsearch_strcmp(const char* const* array,
                                          size_t count,
                                          const char* str)
{
	return bsearch(str, array, count, sizeof(char*), search_strcmp);
}

size_t string_array_deduplicate(char** strings, size_t count)
{
	size_t o = 0;
	for ( size_t i = 0; i < count; i++ )
	{
		if ( !o || strcmp(strings[o - 1], strings[i]) != 0 )
			strings[o++] = strings[i];
		else
			free(strings[i]);
	}
	return o;
}
