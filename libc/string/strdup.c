/*
 * Copyright (c) 2011, 2012, 2014, 2022 Jonas 'Sortie' Termansen.
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
 * string/strdup.c
 * Creates a copy of a string.
 */

#include <stdlib.h>
#include <string.h>

#ifdef __TRACE_ALLOCATION_SITES
char* strdup_trace(struct __allocation_site* allocation_site, const char* input)
#else
char* strdup(const char* input)
#endif
{
	size_t input_length = strlen(input);
#ifdef __TRACE_ALLOCATION_SITES
	char* result = (char*) malloc_trace(allocation_site, input_length + 1);
#else
	char* result = (char*) malloc(input_length + 1);
#endif
	if ( !result )
		return NULL;
	memcpy(result, input, input_length + 1);
	return result;
}
