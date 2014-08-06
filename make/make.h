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
 * make.h
 * Maintain, update, and regenerate groups of programs.
 */

#ifndef MAKE_H
#define MAKE_H

struct bstree;
struct rule;
struct variable;

struct makefile
{
	struct variable** variables;
	size_t variables_used;
	size_t variables_length;
	struct rule** rules;
	size_t rules_used;
	size_t rules_length;
	struct bstree* rules_tree;
	bool used_non_posix;
	bool in_posix_mode;
	bool lazy_builtin;
};

#endif
