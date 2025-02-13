/*
 * Copyright (c) 2011, 2012 Jonas 'Sortie' Termansen.
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
 * sortix/kernel/string.h
 * Useful functions for manipulating strings that don't belong in libc.
 */

#ifndef _INCLUDE_SORTIX_KERNEL_STRING_H
#define _INCLUDE_SORTIX_KERNEL_STRING_H

#include <stddef.h>

namespace Sortix {
namespace String {

char* Clone(const char* Source);
char* Substring(const char* src, size_t offset, size_t length);
bool StartsWith(const char* Haystack, const char* Needle);

} // namespace String
} // namespace Sortix

#endif
