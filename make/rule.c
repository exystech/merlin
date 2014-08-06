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
 * rule.c
 * Rules.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "rule.h"
#include "util.h"

struct command* command_dup(struct command* cmd)
{
	struct command* new = (struct command*) calloc(1, sizeof(struct command));
	if ( !new )
		return NULL;
	if ( !(new->cmd = strdup(cmd->cmd)) )
		return command_free(new), NULL;
	size_t extra_size = sizeof(char*) * cmd->extra_count;
	if ( !(new->extra_vars = (char**) calloc(1, extra_size)) )
		return command_free(new), NULL;
	if ( !(new->extra_values = (char**) calloc(1, extra_size)) )
		return command_free(new), NULL;
	new->extra_count = cmd->extra_count;
	for ( size_t i = 0; i < cmd->extra_count; i++ )
	{
		if ( !(new->extra_vars[i] = strdup(cmd->extra_vars[i])) )
			return command_free(new), NULL;
		if ( !(new->extra_values[i] = strdup(cmd->extra_values[i])) )
			return command_free(new), NULL;
	}
	return new;
}

void command_free(struct command* cmd)
{
	free(cmd->cmd);
	for ( size_t i = 0; i < cmd->extra_count; i++ )
	{
		free(cmd->extra_vars[i]);
		free(cmd->extra_values[i]);
	}
	free(cmd->extra_vars);
	free(cmd->extra_values);
	free(cmd);
}

bool rule_add_prerequisite(struct rule* rule, const char* prerequisite_name)
{
	// TODO: Check that this name doesn't already have that prerequisite?
	char* prerequisite_name_copy = strdup(prerequisite_name);
	if ( !prerequisite_name_copy )
		return false;
	if ( !array_add((void***) &rule->prerequisites,
	                &rule->prerequisites_used,
	                &rule->prerequisites_length,
	                prerequisite_name_copy) )
		return free(prerequisite_name_copy), false;
	return true;
}

bool rule_add_command(struct rule* rule, struct command* command)
{
	if ( !array_add((void***) &rule->commands,
	                &rule->commands_used,
	                &rule->commands_length,
	                command) )
		return false;
	return true;
}

void rule_reset_commands(struct rule* rule)
{
	for ( size_t i = 0; i < rule->commands_used; i++ )
		command_free(rule->commands[i]);
	free(rule->commands);
	rule->commands = NULL;
	rule->commands_used = 0;
	rule->commands_length = 0;
}
