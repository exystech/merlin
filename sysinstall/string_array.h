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
 * string_array.h
 * String array utility functions.
 */

#ifndef STRING_ARRAY_H
#define STRING_ARRAY_H

bool string_array_init(char*** array, size_t* count, size_t* length);
void string_array_free(char*** array, size_t* count, size_t* length);
bool string_array_append_nodup(char*** array,
                               size_t* count,
                               size_t* length,
                               char* str);
bool string_array_append(char*** array,
                         size_t* count,
                         size_t* length,
                         const char* str);
void string_array_sort_strcmp(char** array, size_t count);
bool string_array_contains_bsearch_strcmp(const char* const* array,
                                          size_t count,
                                          const char* str);
size_t string_array_deduplicate(char** strings, size_t count);

#endif
