/*
 * Copyright (c) 2013, 2014 Jonas 'Sortie' Termansen.
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
 * editor.h
 * Editor.
 */

#ifndef EDITOR_EDITOR_H
#define EDITOR_EDITOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "highlight.h"

struct line
{
	wchar_t* data;
	size_t used;
	size_t length;
};

struct color_line
{
	uint8_t* data;
	size_t length;
};

enum editor_mode
{
	MODE_QUIT,
	MODE_EDIT,
	MODE_LOAD,
	MODE_SAVE,
	MODE_SAVE_LOAD,
	MODE_SAVE_QUIT,
	MODE_ASK_LOAD,
	MODE_ASK_QUIT,
	MODE_GOTO_LINE,
	MODE_COMMAND,
};

struct editor
{
	char* current_file_name;
	struct line* lines;
	size_t lines_used;
	size_t lines_length;
	struct color_line* color_lines;
	size_t color_lines_used;
	size_t color_lines_length;
	size_t cursor_column;
	size_t cursor_row;
	size_t select_column;
	size_t select_row;
	size_t viewport_width;
	size_t viewport_height;
	size_t page_x_offset;
	size_t page_y_offset;
	wchar_t* modal;
	size_t modal_used;
	size_t modal_length;
	size_t modal_cursor;
	wchar_t* clipboard;
	size_t tabsize;
	size_t margin;
	enum editor_mode mode;
	bool control;
	bool shift;
	bool lshift;
	bool rshift;
	bool dirty;
	bool modal_error;
	enum language highlight_source;
	bool line_numbering;
	bool suspend_requested;
};

__attribute__((unused))
static inline bool editor_has_selection(struct editor* editor)
{
	return !(editor->cursor_row == editor->select_row &&
	         editor->cursor_column == editor->select_column);
}

void initialize_editor(struct editor* editor);
void editor_reset_contents(struct editor* editor);
bool editor_load_file_contents(struct editor* editor, FILE* fp);
bool editor_load_file(struct editor* editor, const char* path);
bool editor_load_popen(struct editor* editor, const char* cmd);
bool editor_save_file(struct editor* editor, const char* path);

#endif
