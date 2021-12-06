/*
 * Copyright (c) 2021 Juhani 'nortti' Krekelä.
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
 * sortix/ps2mouse.h
 * Defines macros for the PS/2 mouse protocol.
 */

#ifndef INCLUDE_SORTIX_PS2MOUSE_H
#define INCLUDE_SORTIX_PS2MOUSE_H

#define MOUSE_PACKET_SIZE 3

#define MOUSE_BUTTON_LEFT (1 << 0)
#define MOUSE_BUTTON_RIGHT (1 << 1)
#define MOUSE_BUTTON_MIDDLE (1 << 2)

#define MOUSE_ALWAYS_1 (1 << 3)

#define MOUSE_X_SIGN (1 << 4)
#define MOUSE_Y_SIGN (1 << 5)

#define MOUSE_X_OVERFLOW (1 << 6)
#define MOUSE_Y_OVERFLOW (1 << 7)

#define MOUSE_X(bytes) \
( \
	(bytes)[0] & (MOUSE_X_OVERFLOW | MOUSE_Y_OVERFLOW) ? \
		0 : \
		(bytes)[1] - ((bytes)[0] & MOUSE_X_SIGN ? 256 : 0) \
)

#define MOUSE_Y(bytes) \
(\
	(bytes)[0] & (MOUSE_X_OVERFLOW | MOUSE_Y_OVERFLOW) ? \
		0 : \
		-((bytes)[2] - ((bytes)[0] & MOUSE_Y_SIGN ? 256 : 0)) \
)

#endif
