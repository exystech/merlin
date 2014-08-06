/*
 * Copyright (c) 2014, 2015 Jonas 'Sortie' Termansen.
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
 * variable.h
 * Variables.
 */

#ifndef VARIABLE_H
#define VARIABLE_H

struct makefile;

enum variable_source
{
	VARIABLE_SOURCE_INTERNAL,
	VARIABLE_SOURCE_COMMAND_LINE,
	VARIABLE_SOURCE_ENVIRONMENT,
	VARIABLE_SOURCE_MAKEFILE,
};

static const int VARIABLE_FLAG_EXPORT = 1 << 0;
static const int VARIABLE_FLAG_FORCE_EXPORT = 1 << 1;
static const int VARIABLE_FLAG_MAKEFLAG = 1 << 2;

struct variable
{
	struct variable* pushed;
	char* name;
	char* value;
	enum variable_source source;
	int flags;
};

void variable_free(struct variable* variable);
const char* variable_value(struct variable* variable);
const char* variable_value_def(struct variable* variable,
                               const char* default_value/* = ""*/);
void variable_assign(struct variable* variable,
                     const char* value,
                     enum variable_source source);

char* makefile_evaluate_value(struct makefile* makefile, const char* value);

char* make_escape_value(const char* value);
char* shell_singly_escape_value(const char* value);

#endif
