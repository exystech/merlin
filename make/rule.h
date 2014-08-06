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
 * rule.h
 * Rule.
 */

#ifndef RULE_H
#define RULE_H

struct command
{
	char* cmd;
	size_t extra_count;
	char** extra_vars;
	char** extra_values;
};

struct rule
{
	char* name;
	char* inferred_from;
	char* stem;
	char** prerequisites;
	size_t prerequisites_used;
	size_t prerequisites_length;
	struct command** commands;
	size_t commands_used;
	size_t commands_length;
	bool needs_to_be_inferred;
};

struct command* command_dup(struct command* cmd);
void command_free(struct command* cmd);
bool rule_add_prerequisite(struct rule* rule, const char* prerequisite_name);
bool rule_add_command(struct rule* rule, struct command* command);
void rule_reset_commands(struct rule* rule);

#endif
