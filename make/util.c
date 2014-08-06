/*
 * Copyright (c) 2014, 2015 Jonas 'Sortie' Termansen.
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
 * util.c
 * Utility functions.
 */

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool array_add(void*** array_ptr,
               size_t* used_ptr,
               size_t* length_ptr,
               void* value)
{
	void** array;
	memcpy(&array, array_ptr, sizeof(array)); // Strict aliasing.

	if ( *used_ptr == *length_ptr )
	{
		// TODO: Avoid overflow.
		size_t new_length = 2 * *length_ptr;
		if ( !new_length )
			new_length = 16;
		// TODO: Avoid overflow and use reallocarray.
		size_t new_size = new_length * sizeof(void*);
		void** new_array = (void**) realloc(array, new_size);
		if ( !new_array )
			return false;
		array = new_array;
		memcpy(array_ptr, &array, sizeof(array)); // Strict aliasing.
		*length_ptr = new_length;
	}

	memcpy(array + (*used_ptr)++, &value, sizeof(value)); // Strict aliasing.

	return true;
}

char* print_string(const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	char* ret;
	if ( vasprintf(&ret, format, ap) < 0 )
		ret = NULL;
	va_end(ap);
	return ret;
}

char* join_paths(const char* a, const char* b)
{
	size_t a_len = strlen(a);
	bool has_slash = (a_len && a[a_len-1] == '/') || b[0] == '/';
	return has_slash ? print_string("%s%s", a, b) : print_string("%s/%s", a, b);
}

void word_split(const char* string, char*** tokens_ptr, size_t* count_ptr)
{
	*tokens_ptr = NULL;
	*count_ptr = 0;
	size_t tokens_length = 0;

	// TODO: Add proper quoting and escaping.
	while ( *string )
	{
		// TODO: isblank might not be right here. Perhaps only tabs, space and
		//       newline (isblank is only space & tab), or a full isspace.
		while ( *string && isblank((unsigned char) *string) )
			string++;
		if ( !*string )
			break;
		size_t length = 1;
		while ( string[length] && !isblank((unsigned char) string[length]) )
			length++;
		char* word = strndup(string, length);
		assert(word);
		string += length;
		if ( !array_add((void***) tokens_ptr,
		                count_ptr,
		                &tokens_length,
		                word) )
			assert(false);
	}
}
