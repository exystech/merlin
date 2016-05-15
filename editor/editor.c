/*
 * Copyright (c) 2013, 2014, 2015, 2016 Jonas 'Sortie' Termansen.
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
 * editor.c
 * Editor.
 */

#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <err.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <unistd.h>

#include "command.h"
#include "cursor.h"
#include "display.h"
#include "editor.h"
#include "highlight.h"
#include "input.h"
#include "modal.h"
#include "terminal.h"

void initialize_editor(struct editor* editor)
{
	memset(editor, 0, sizeof(*editor));

	editor->current_file_name = NULL;
	editor->lines = NULL;
	editor->lines_used = 0;
	editor->lines_length = 0;
	editor->cursor_column = 0;
	editor->cursor_row = 0;
	editor->select_column = 0;
	editor->select_row = 0;
	editor->page_x_offset = 0;
	editor->page_y_offset = 0;
	editor->modal = NULL;
	editor->modal_used = 0;
	editor->modal_length = 0;
	editor->modal_cursor = 0;
	editor->clipboard = NULL;
	editor->tabsize = 8;
	editor->margin = SIZE_MAX;
	editor->mode = MODE_EDIT;
	editor->control = false;
	editor->shift = false;
	editor->lshift = false;
	editor->rshift = false;
	editor->dirty = false;
	editor->modal_error = false;
	editor->highlight_source = LANGUAGE_NONE;

	editor->lines_used = 1;
	editor->lines_length = 1;
	editor->lines =
		(struct line*) malloc(sizeof(struct line) * editor->lines_length);
	editor->lines[0].data = NULL;
	editor->lines[0].used = 0;
	editor->lines[0].length = 0;

	editor->color_lines_used = 0;
	editor->color_lines_length = 0;
	editor->color_lines = NULL;
}

void editor_load_config_path(struct editor* editor, const char* path)
{
	FILE* fp = fopen(path, "r");
	if ( !fp )
	{
		if ( errno != ENOENT )
			warn("%s", path);
		return;
	}
	char* line = NULL;
	size_t line_size = 0;
	ssize_t line_length = 0;
	while ( 0 < (line_length = getline(&line, &line_size, fp)) )
	{
		if ( line[line_length - 1] == '\n' )
			line[--line_length] = '\0';
		line_length = strcspn(line, "#");
		line[line_length] = '\0';
		editor_modal_command_config(editor, line);
	}
	if ( ferror(fp) )
		warn("getline: %s", path);
	fclose(fp);
}

void editor_load_config(struct editor* editor)
{
	editor_load_config_path(editor, "/etc/editor");
	const char* home = getenv("HOME");
	if ( !home )
		return;
	char* path;
	if ( asprintf(&path, "%s/.editor", home) < 0 )
	{
		warn("malloc");
		return;
	}
	editor_load_config_path(editor, path);
	free(path);
}

void editor_reset_contents(struct editor* editor)
{
	for ( size_t i = 0; i < editor->lines_used; i++ )
		free(editor->lines[i].data);
	free(editor->lines);

	editor->lines_used = 1;
	editor->lines_length = 1;
	editor->lines =
		(struct line*) malloc(sizeof(struct line) * editor->lines_length);
	editor->lines[0].data = NULL;
	editor->lines[0].used = 0;
	editor->lines[0].length = 0;
	editor->highlight_source = LANGUAGE_NONE;
	editor_cursor_set(editor, 0, 0);
}

bool editor_load_file_contents(struct editor* editor, FILE* fp)
{
	struct stat st;
	if ( fstat(fileno(fp), &st) != 0 || S_ISDIR(st.st_mode) )
		return errno = EISDIR, false;

	free(editor->current_file_name);
	editor->current_file_name = NULL;

	editor_reset_contents(editor);

	mbstate_t ps;
	memset(&ps, 0, sizeof(ps));
	bool last_newline = false;
	int ic;
	while ( (ic = fgetc(fp)) != EOF )
	{
		if ( last_newline )
		{
			editor_type_newline(editor);
			last_newline = false;
		}

		char c = (char) ic;
		wchar_t wc;
		size_t count = mbrtowc(&wc, &c, 1, &ps);
		if ( count == (size_t) 0 )
			continue;
		if ( count == (size_t) -1 )
		{
			memset(&ps, 0, sizeof(ps));
			wc = L'�';
		}
		if ( count == (size_t) -2 )
			continue;
		assert(wc != L'\0');
		if ( !(last_newline = wc == L'\n') )
			editor_type_raw_character(editor, wc);
	}

	if ( !mbsinit(&ps) )
		editor_type_raw_character(editor, L'�');

	editor_cursor_set(editor, 0, 0);

	return true;
}

bool editor_load_file(struct editor* editor, const char* path)
{
	FILE* fp;
	if ( (fp = fopen(path, "r")) )
	{
		bool success = editor_load_file_contents(editor, fp);
		fclose(fp);
		if ( !success )
			return false;
		editor->dirty = false;
	}
	else if ( errno == ENOENT )
	{
		editor_reset_contents(editor);
		editor->dirty = true;
	}
	else
		return false;

	editor->current_file_name = strdup(path);
	editor->highlight_source = language_of_path(path);

	return true;
}

bool editor_load_popen(struct editor* editor, const char* cmd)
{
	FILE* fp = popen(cmd, "r");
	if ( !fp )
		return false;
	bool success = editor_load_file_contents(editor, fp);
	pclose(fp);

	if ( !success )
		return false;

	editor->current_file_name = NULL;
	editor->dirty = true;

	return true;
}

bool editor_save_file(struct editor* editor, const char* path)
{
	FILE* fp = fopen(path, "w");
	if ( !fp )
		return false;

	mbstate_t ps;
	memset(&ps, 0, sizeof(ps));
	for ( size_t i = 0; i < editor->lines_used; i++ )
	{
		char mb[MB_CUR_MAX];
		for ( size_t n = 0; n < editor->lines[i].used; n++ )
		{
			mbstate_t saved_ps = ps;
			wchar_t wc = editor->lines[i].data[n];

			size_t count = wcrtomb(mb, wc, &ps);
			if ( count == (size_t) -1 )
			{
				ps = saved_ps;
				count = wcrtomb(mb, L'�', &ps);
				assert(count != (size_t) -1);
			}
			fwrite(mb, sizeof(char), count, fp);
		}
		size_t count = wcrtomb(mb, L'\n', &ps);
		assert(count != (size_t) -1);
		fwrite(mb, sizeof(char), count, fp);
	}

	editor->current_file_name = strdup(path);
	editor->dirty = false;
	editor->highlight_source = language_of_path(path);

	return fclose(fp) != EOF;
}

int main(int argc, char* argv[])
{
	setlocale(LC_ALL, "");

	if ( !isatty(0) )
		err(1, "standard input");
	if ( !isatty(1) )
		err(1, "standard output");

	struct editor editor;
	initialize_editor(&editor);

	editor_load_config(&editor);

	if ( 2 <= argc && !editor_load_file(&editor, argv[1]) )
		err(1, "`%s'", argv[1]);

	struct editor_input editor_input;
	editor_input_begin(&editor_input);

	struct terminal_state stdout_state;
	make_terminal_state(stdout, &stdout_state);
	reset_terminal_state(stdout, &stdout_state);
	fflush(stdout);

	while ( editor.mode != MODE_QUIT )
	{
		if ( editor.suspend_requested )
		{
			editor_input_suspend(&editor_input);
			editor.suspend_requested = false;
			reset_terminal_state(stdout, &stdout_state);
			fflush(stdout);
		}

		struct terminal_state output_state;
		make_terminal_state(stdout, &output_state);
		editor_colorize(&editor);
		render_editor(&editor, &output_state);
		update_terminal(stdout, &output_state, &stdout_state);
		free_terminal_state(&output_state);
		fflush(stdout);

		editor_input_process(&editor_input, &editor);
	}

	reset_terminal_state(stdout, &stdout_state);
	free_terminal_state(&stdout_state);
	fflush(stdout);

	editor_input_end(&editor_input);

	return 0;
}
