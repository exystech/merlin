/*
 * Copyright (c) 2014, 2015, 2016 Jonas 'Sortie' Termansen.
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
 * import.h
 * Import makefiles.
 */

#ifndef IMPORT_H
#define IMPORT_H

struct makefile;
struct rule;

enum makefile_line_type
{
	MAKEFILE_LINE_TYPE_UNKNOWN,
	MAKEFILE_LINE_TYPE_NONE,
	MAKEFILE_LINE_TYPE_NOBUILTIN,
	MAKEFILE_LINE_TYPE_INCLUDE,
	MAKEFILE_LINE_TYPE_INCLUDEPATH,
	MAKEFILE_LINE_TYPE_ASSIGNMENT,
	MAKEFILE_LINE_TYPE_RULE,
	MAKEFILE_LINE_TYPE_COMMAND,
	MAKEFILE_LINE_TYPE_IF,
	MAKEFILE_LINE_TYPE_FOR,
	MAKEFILE_LINE_TYPE_ELSE,
	MAKEFILE_LINE_TYPE_ELSEIF,
	MAKEFILE_LINE_TYPE_ENDIF,
	MAKEFILE_LINE_TYPE_ENDFOR,
};

enum makefile_block_type
{
	MAKEFILE_BLOCK_IF,
	MAKEFILE_BLOCK_FOR,
};

struct makefile_block
{
	struct makefile_block* parent_block;
	enum makefile_block_type type;
	size_t loop_count;
	bool parent_active;
	bool parent_recording;
	bool branch_taken;
	bool else_encountered;
	char** vars;
	size_t vars_count;
	char** values;
	size_t values_count;
	size_t begun_offset;
	size_t values_index;
};

struct makefile_parse
{
	struct makefile_block* current_block;
	struct rule** current_rules;
	size_t current_rules_used;
	size_t current_rules_length;
	bool parsing_recipe;
	bool fresh_rule;
	bool active;
	bool recording;
	bool replaying;
	char** lines;
	size_t lines_used;
	size_t lines_length;
	size_t playback_offset;
	size_t local_vars_used;
	size_t local_vars_length;
	const char** local_vars;
};

enum makefile_line_type makefile_get_line_type(const char* line, size_t* split);
void makefile_consume_line(struct makefile* makefile,
                           struct makefile_parse* parse,
                           const char* line,
                           const char* fp_path);
char* makefile_read_line(FILE* fp, const char* fp_path);
void makefile_read(struct makefile* makefile, FILE* fp, const char* fp_path);
void makefile_read_path(struct makefile* makefile, const char* path);
void makefile_read_path_search(struct makefile* makefile,
                               const char* path,
                               bool search_quotes,
                               bool try);
void makefile_read_builtin(struct makefile* makefile);

#endif
