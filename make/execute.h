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
 * execute.h
 * Executes the makefile.
 */

#ifndef EXECUTE_H
#define EXECUTE_H

struct bstree;
struct makefile;
struct rule;
struct target;

enum make_result
{
	MAKE_RESULT_WAS_UP_TO_DATE,
	MAKE_RESULT_UPDATED,
	MAKE_RESULT_ERROR,
};

struct target
{
	struct stat st;
	struct rule* rule;
	enum make_result result;
	bool currently_being_made;
	bool warned_circular_dependency;
	bool has_been_made;
	bool stat_cached;
};

struct make
{
	struct bstree* targets;
	bool ignore_errors;
	bool no_execute;
	bool silent;
	bool always_make;
	bool question;
	bool touch;
	bool keep_going;
};

bool make_cached_stat(struct target* target);
struct target* make_find_target(struct makefile* makefile,
                                struct make* make,
                                const char* name);
struct target* make_create_target(struct makefile* makefile,
                                  struct make* make,
                                  const char* name);
enum make_result make_target(struct makefile* makefile,
                             struct make* make,
                             struct target* target);
enum make_result make_target_name(struct makefile* makefile,
                                  struct make* make,
                                  const char* name);
enum make_result make_target_goal(struct makefile* makefile,
                                  struct make* make,
                                  const char* name);

#endif
