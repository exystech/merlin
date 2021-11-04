/*
 * Copyright (c) 2013, 2014 Jonas 'Sortie' Termansen.
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
 * cursor.c
 * Editor cursor.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "cursor.h"
#include "display.h"
#include "editor.h"

void editor_select_set(struct editor* editor, size_t y, size_t x)
{
	assert(y < editor->lines_used);
	assert(x <= editor->lines[y].used);
	if ( editor->viewport_height )
	{
		if ( y < editor->page_y_offset )
			editor->page_y_offset = y;
		if ( editor->page_y_offset + editor->viewport_height <= y )
			editor->page_y_offset = y + 1 - editor->viewport_height;
	}
	editor->select_row = y;

	if ( editor->viewport_width )
	{
		struct line* line = &editor->lines[editor->select_row];
		size_t rx = displayed_string_length(line->data, x, editor->tabsize);
		if ( rx < editor->page_x_offset )
			editor->page_x_offset = rx;
		if ( editor->page_x_offset + editor->viewport_width <= rx )
			editor->page_x_offset = rx + 1 - editor->viewport_width;
	}
	editor->select_column = x;
}

void editor_cursor_set(struct editor* editor, size_t y, size_t x)
{
	editor_select_set(editor, y, x);
	editor->cursor_column = x;
	editor->cursor_row = y;
}
