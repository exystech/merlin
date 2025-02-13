/*
 * Copyright (c) 2012 Jonas 'Sortie' Termansen.
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
 * sortix/kernel/fsfunc.h
 * Filesystem related utility functions.
 */

#ifndef _INCLUDE_SORTIX_KERNEL_FSFUNC_H
#define _INCLUDE_SORTIX_KERNEL_FSFUNC_H

#include <sys/types.h>

namespace Sortix {

static inline bool IsDotOrDotDot(const char* path)
{
	return path[0] == '.' && ((path[1] == '\0') ||
	                          (path[1] == '.' && path[2] == '\0'));
}

unsigned char ModeToDT(mode_t mode);
bool SplitFinalElem(const char* path, char** dir, char** final);

} // namespace Sortix

#endif
