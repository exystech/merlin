/*
 * Copyright (c) 2014 Jonas 'Sortie' Termansen.
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
 * sortix/kernel/sockopt.h
 * getsockopt() and setsockopt() utility functions.
 */

#ifndef _INCLUDE_SORTIX_KERNEL_SOCKOPT_H
#define _INCLUDE_SORTIX_KERNEL_SOCKOPT_H

#include <stddef.h>
#include <stdint.h>

#include <sortix/kernel/ioctx.h>

namespace Sortix {

bool sockopt_fetch_uintmax(uintmax_t* optval_ptr, ioctx_t* ctx,
                           const void* option_value, size_t option_size);
bool sockopt_return_uintmax(uintmax_t optval, ioctx_t* ctx,
                            void* option_value, size_t* option_size_ptr);

} // namespace Sortix

#endif
