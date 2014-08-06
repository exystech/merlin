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
 * import.c
 * Import makefiles.
 */

#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "data.h"
#include "import.h"
#include "make.h"
#include "rule.h"
#include "util.h"
#include "variable.h"

extern char** quote_includes;
extern size_t quote_includes_count;
extern char** angle_includes;
extern size_t angle_includes_count;
char** runtime_includes = NULL;
size_t runtime_includes_count = 0;
size_t runtime_includes_max = 0;

static void assignment_type(char* name,
                            bool* is_default,
                            bool* is_add,
                            bool* is_expand,
                            bool* is_shell)
{
	*is_default = false;
	*is_add = false;
	*is_expand = false;
	*is_shell = false;
	size_t namelen = strlen(name);
	while ( namelen )
	{
		switch ( name[namelen-1] )
		{
		case '?': *is_default = true; break;
		case '+': *is_add = true; break;
		case ':': *is_expand = true; break;
		case '!': *is_shell = true; break;
		default: return;
		}
		name[--namelen] = '\0';
	}
}

static bool match_command_word(const char* line,
                               size_t offset,
                               const char* word,
                               size_t* split)
{
	size_t word_length = strlen(word);
	if ( !strncmp(line + offset, word, word_length) )
	{
		if ( !line[offset + word_length] ||
		     isblank((unsigned char) line[offset + word_length]) )
		{
			if ( split )
				*split = offset + word_length;
			return true;
		}
	}
	return false;
}

enum makefile_line_type makefile_get_line_type(const char* line, size_t* split)
{
	if ( line[0] == '\t' )
		return MAKEFILE_LINE_TYPE_COMMAND;

	if ( line[0] == '.' )
	{
		size_t offset = 1;
		while ( line[offset] && isblank((unsigned char) line[offset]) )
			offset++;
		if ( match_command_word(line, offset, "nobuiltin", split) )
			return MAKEFILE_LINE_TYPE_NOBUILTIN;
		if ( match_command_word(line, offset, "include", split) ||
		     match_command_word(line, offset, "tryinclude", split) )
			return MAKEFILE_LINE_TYPE_INCLUDE;
		if ( match_command_word(line, offset, "includepath", split) )
			return MAKEFILE_LINE_TYPE_INCLUDEPATH;
		if ( match_command_word(line, offset, "if", split) ||
		     match_command_word(line, offset, "ifdef", split) ||
		     match_command_word(line, offset, "ifndef", split) ||
		     match_command_word(line, offset, "ifmake", split) ||
		     match_command_word(line, offset, "ifnmake", split) )
			return MAKEFILE_LINE_TYPE_IF;
		if ( match_command_word(line, offset, "for", split) )
			return MAKEFILE_LINE_TYPE_FOR;
		if ( match_command_word(line, offset, "else", split) )
			return MAKEFILE_LINE_TYPE_ELSE;
		if ( match_command_word(line, offset, "elif", split) ||
		     match_command_word(line, offset, "elifdef", split) ||
		     match_command_word(line, offset, "elifndef", split) ||
		     match_command_word(line, offset, "elifmake", split) ||
		     match_command_word(line, offset, "elifnmake", split) )
			return MAKEFILE_LINE_TYPE_ELSEIF;
		if ( match_command_word(line, offset, "endif", split) )
			return MAKEFILE_LINE_TYPE_ENDIF;
		if ( match_command_word(line, offset, "endfor", split) )
			return MAKEFILE_LINE_TYPE_ENDFOR;
	}

	if ( match_command_word(line, 0, "include", split) ||
	     match_command_word(line, 0, "-include", split) )
		return MAKEFILE_LINE_TYPE_INCLUDE;

	bool found_non_blank = false;

	size_t offset = 0;
	while ( line[offset] )
	{
		if ( !isblank((unsigned char) line[offset]) )
			found_non_blank = true;

		if ( line[offset] == '$' )
		{
			offset++;
			if ( !line[offset] )
				break;

			if ( line[offset] == '(' || line[offset] == '{' )
			{
				offset++;
				while ( line[offset] &&
				        line[offset] != ')' &&
				        line[offset] != '}' )
					offset++;
				if ( !line[offset] )
					break;
				offset++;
			}
			else if ( line[offset] != '$' )
			{
				offset++;
			}
			else
			{
				offset++;
			}
		}
		else
		{
			if ( line[offset] == '=' )
			{
				if ( split )
					*split = offset;
				return MAKEFILE_LINE_TYPE_ASSIGNMENT;
			}

			if ( line[offset + 0] == ':' &&
			     !(line[offset + 1] == '=') &&
			     !(line[offset + 1] == ':' &&
			       line[offset + 2] == '=') )
			{
				if ( split )
					*split = offset;
				return MAKEFILE_LINE_TYPE_RULE;
			}

			offset++;
		}
	}

	return found_non_blank ? MAKEFILE_LINE_TYPE_UNKNOWN
	                       : MAKEFILE_LINE_TYPE_NONE;
}

struct command* make_command(struct makefile* makefile,
                             struct makefile_parse* parse,
                             const char* string)
{
	struct command* cmd = (struct command*) calloc(1, sizeof(struct command));
	if ( !cmd )
		return NULL;
	if ( !(cmd->cmd = strdup(string)) )
		return command_free(cmd), NULL;
	size_t extra_size = sizeof(char*) * parse->local_vars_used;
	if ( !(cmd->extra_vars = (char**) calloc(1, extra_size)) )
		return command_free(cmd), NULL;
	if ( !(cmd->extra_values = (char**) calloc(1, extra_size)) )
		return command_free(cmd), NULL;
	cmd->extra_count = parse->local_vars_used;
	for ( size_t i = 0; i < cmd->extra_count; i++ )
	{
		const char* var = parse->local_vars[i];
		const char* value = variable_value(makefile_find_variable(makefile, var));
		if ( !(cmd->extra_vars[i] = strdup(var)) )
			return command_free(cmd), NULL;
		if ( !(cmd->extra_values[i] = strdup(value)) )
			return command_free(cmd), NULL;
	}
	return cmd;
}

void makefile_consume_line(struct makefile* makefile,
                           struct makefile_parse* parse,
                           const char* line,
                           const char* fp_path)
{
	size_t split = 0;
	enum makefile_line_type line_type = makefile_get_line_type(line, &split);

	if ( parse->parsing_recipe && line_type == MAKEFILE_LINE_TYPE_NONE )
	{
		// TODO: This might not be the right logic? Perhaps the recipe should
		//       end instead.
		return;
	}

	if ( parse->parsing_recipe && line_type != MAKEFILE_LINE_TYPE_COMMAND )
	{
		parse->parsing_recipe = false;
		free(parse->current_rules);
		parse->current_rules = NULL;
		parse->current_rules_used = 0;
		parse->current_rules_length = 0;
	}

	if ( line_type == MAKEFILE_LINE_TYPE_UNKNOWN )
	{
		errx(2, "*** malformed line: %s: `%s'", fp_path, line);
	}
	else if ( line_type == MAKEFILE_LINE_TYPE_NONE )
	{
	}
	else if ( line_type == MAKEFILE_LINE_TYPE_INCLUDE )
	{
		bool bsd;
		bool try;
		if ( (bsd = line[0] == '.') )
		{
			size_t offset = 1;
			while ( line[offset] && isblank((unsigned char) line[offset]) )
				offset++;
			try = line[offset] == 't';
		}
		else
			try = line[0] == '-';

		if ( bsd || try )
			makefile_use_non_posix(makefile);

		char* param_string = strdup(line + split);
		assert(param_string);

		char* param = param_string;
		while ( isblank((unsigned char) *param) )
			param++;
		size_t param_length = strlen(param);
		while ( param_length && isblank((unsigned char) param[param_length-1]) )
			param[--param_length] = '\0';

		bool search_quotes;
		if ( 2 <= param_length &&
		     param[0] == '<' && param[param_length-1] == '>' )
		{
			param++;
			param_length -= 2;
			param[param_length] = '\0';
			search_quotes = false;
		}
		else if ( 2 <= param_length &&
		          param[0] == '"' && param[param_length-1] == '"' )
		{
			param++;
			param_length -= 2;
			param[param_length] = '\0';
			search_quotes = true;
		}
		else
		{
			if ( bsd )
				warnx("*** include path should be quoted (ignored)");
			search_quotes = true;
		}

		char* path = makefile_evaluate_value(makefile, param);
		assert(path);
		free(param_string);

		// TODO: GNU make allows including multiple paths per include statement.
		//       This may be desirable to implement if we want to emulate GNU
		//       make more closely.

		makefile_read_path_search(makefile, path, search_quotes, try);

		free(path);
	}
	else if ( line_type == MAKEFILE_LINE_TYPE_INCLUDEPATH )
	{
		char* param_string = strdup(line + split);
		assert(param_string);

		char* param = param_string;
		while ( isblank((unsigned char) *param) )
			param++;
		size_t param_length = strlen(param);
		while ( param_length && isblank((unsigned char) param[param_length-1]) )
			param[--param_length] = '\0';

		char* path = makefile_evaluate_value(makefile, param);
		assert(path);
		free(param_string);

		if ( !array_add((void***) &runtime_includes,
		                &runtime_includes_count,
		                &runtime_includes_max,
		                path) )
			assert(false);
	}
	else if ( line_type == MAKEFILE_LINE_TYPE_ASSIGNMENT )
	{
		char* raw_name = strndup(line, split);
		assert(raw_name);

		bool is_default, is_add, is_expand, is_shell;
		assignment_type(raw_name, &is_default, &is_add, &is_expand, &is_shell);
		// TODO: Which of these gets added in the upcoming POSIX as of 2015?
		if ( is_default || is_add || is_expand || is_shell )
			makefile_use_non_posix(makefile);

		char* name_string = makefile_evaluate_value(makefile, raw_name);
		assert(name_string);

		free(raw_name);
		char* name = name_string;

		name += strspn(name, " \t");
		size_t name_length = strlen(name);
		while ( name_length && isblank((unsigned char) name[name_length-1]) )
			name_length--;
		name[name_length] = '\0';

		// TODO: Ensure name is a valid name.

		line += split + 1;

		line += strspn(line, " \t");

		size_t value_length = strlen(line);

		char* value = strndup(line, value_length);
		assert(value);

		if ( !strcmp(name, ".DEFAULT_GOAL") )
			makefile_use_non_posix(makefile);

		struct variable* variable = makefile_find_variable(makefile, name);
		bool was_unset = !variable;
		if ( !variable )
		{
			variable = makefile_create_variable(makefile, name);
			assert(variable);
		}

		if ( is_default && !was_unset )
		{
			// Ensure we have no side effects.
		}
		else if ( is_shell )
		{
			// TODO: Go through the list of variables and export all those that
			//       need to be so they are seen by the shell.
			const char* shell = variable_value(makefile_find_variable(makefile, "SHELL"));
			if ( !shell || !shell[0] )
				shell = "sh";
			char* command = makefile_evaluate_value(makefile, value);
			assert(command);
			int fds[2];
			if ( pipe(fds) )
				err(2, "*** pipe()");
			pid_t child_pid = fork();
			if ( child_pid < 0 )
				err(2, "*** fork()");
			if ( child_pid == 0 )
			{
				close(0);
				if ( open("/dev/null", O_RDONLY) != 0 )
					err(2, "*** /dev/null");
				if ( dup2(fds[1], 1) < 0 )
					err(2, "*** dup2()");
				close(fds[0]);
				close(fds[1]);
				const char* argv[] = { shell, "-c", command, NULL };
				execvp(shell, (char**) argv);
				_exit(127);
			}
			close(fds[1]);
			char* new_value;
			size_t new_value_size;
			FILE* fp = open_memstream(&new_value, &new_value_size);
			if ( !fp )
				err(2, "*** open_memstream()");
			char buffer[4096];
			ssize_t count;
			while ( 0 < (count = read(fds[0], buffer, sizeof(buffer))) )
			{
				if ( fwrite(buffer, 1, (size_t) count, fp) != (size_t) count )
					err(2, "*** fwrite(open_memstream())");
			}
			close(fds[0]);
			int exit_code;
			waitpid(child_pid, &exit_code, 0);
			if ( fclose(fp) == EOF )
				err(2, "*** fclose(open_memstream())");
			free(value);
			value = new_value;
			size_t value_length = strlen(value);
			if ( value_length && value[value_length-1] == '\n' )
				value[--value_length] = '\0';
			for ( size_t i = 0; i < value_length; i++ )
				if ( value[i] == '\n' )
					value[i] = ' ';
		}
		else if ( is_expand )
		{
			char* new_value = makefile_evaluate_value(makefile, value);
			assert(new_value);
			free(value);
			value = new_value;
		}
		else
		{
			// Do nothing.
		}

		if ( is_add && variable_value(variable)[0] )
		{
			char* new_value = print_string("%s %s", variable_value(variable), value);
			assert(new_value);
			free(value);
			value = new_value;
		}

		if ( !is_default || was_unset )
			variable_assign(variable, value, VARIABLE_SOURCE_MAKEFILE);

		free(name_string);
		free(value);
	}
	else if ( line_type == MAKEFILE_LINE_TYPE_RULE )
	{
		parse->parsing_recipe = true;
		free(parse->current_rules);
		parse->current_rules = NULL;
		parse->current_rules_used = 0;
		parse->current_rules_length = 0;

		char* raw_rules = strndup(line, split);
		assert(raw_rules);

		char* rules_string = makefile_evaluate_value(makefile, raw_rules);
		assert(rules_string);

		free(raw_rules);
		const char* rules = rules_string;

		while ( true )
		{
			rules += strspn(rules, " \t");
			if ( !rules[0] || rules[0] == ':' )
				break;

			size_t rule_name_length = strcspn(rules, " \t:");
			char* rule_name = strndup(rules, rule_name_length);
			assert(rule_name);
			rules += rule_name_length;

			struct rule* rule = makefile_create_rule(makefile, rule_name, NULL, NULL);
			assert(rule);

			// TODO: Perhaps this should be moved into makefile_create_rule().
			if ( !strcmp(rule_name, ".POSIX") )
				makefile_enter_posix_mode(makefile);
			if ( !strcmp(rule_name, ".PHONY") )
				makefile_use_non_posix(makefile);

			free(rule_name);

			if ( !array_add((void***) &parse->current_rules,
			                &parse->current_rules_used,
			                &parse->current_rules_length,
			                rule) )
				assert(false);
		}

		free(rules_string);

		line += split + 1;

		// TODO: BSD make also has foo ! bar.
		// TODO: Implement this feature. The rule model isn't powerful enough
		//       for this yet though. This should approximate it. Notably the
		//       file is still removed if interrupted (not precious). Each rule
		//       declaration also doesn't have its own independent list of
		//       prerequisites. Or a better way of explaining this.
		bool double_colon = line[0] == ':';
		if ( double_colon )
			line++;

		char* prerequisites_string = makefile_evaluate_value(makefile, line);
		assert(prerequisites_string);

		const char* prerequisites = prerequisites_string;

		bool any_prerequisites = false;

		while ( true )
		{
			prerequisites += strspn(prerequisites, " \t");
			if ( !prerequisites[0] || prerequisites[0] == ';' )
				break;

			size_t prerequisite_name_length = strcspn(prerequisites, " \t;");
			char* prerequisite_name = strndup(prerequisites, prerequisite_name_length);
			assert(prerequisite_name);
			prerequisites += prerequisite_name_length;

			any_prerequisites = true;

			for ( size_t i = 0; i < parse->current_rules_used; i++ )
			{
				struct rule* rule = parse->current_rules[i];
				// TODO: This doesn't handle duplicated prerequisites.
				bool success = rule_add_prerequisite(rule, prerequisite_name);
				assert(success);
			}

			free(prerequisite_name);
		}

		// This is a fun special case from the original make.
		if ( !any_prerequisites )
		{
			for ( size_t i = 0; i < parse->current_rules_used; i++ )
			{
				struct rule* rule = parse->current_rules[i];
				if ( !strcmp(rule->name, ".SUFFIXES") )
				{
					free(rule->prerequisites);
					rule->prerequisites = NULL;
					rule->prerequisites_used = 0;
					rule->prerequisites_length = 0;
				}
			}
		}

		parse->fresh_rule = !double_colon;

		// TODO: Macros get expanded here! That's bad! They should wait until
		//       the command is run. This should be fixed by parsing the rule
		//       line properly first.

		if ( prerequisites[0] == ';' )
		{
			prerequisites++;
			// TODO: libiberty had a line “foo: bar ; @true” that assumes the
			//       whitespace after ; is removed. POSIX doesn't suggest this
			//       happens and might be a bug in libiberty. Work around this
			//       for now.
			while ( isspace((unsigned char) prerequisites[0]) )
				prerequisites++;

			if ( parse->fresh_rule )
			{
				for ( size_t i = 0; i < parse->current_rules_used; i++ )
				{
					struct rule* rule = parse->current_rules[i];
					// TODO: Potentially complain if the rule is overridden.
					rule_reset_commands(rule);
				}
				parse->fresh_rule = false;
			}

			for ( size_t i = 0; i < parse->current_rules_used; i++ )
			{
				struct rule* rule = parse->current_rules[i];
				struct command* cmd = make_command(makefile, parse, prerequisites);
				assert(cmd);
				bool success = rule_add_command(rule, cmd);
				assert(success);
			}
		}

		free(prerequisites_string);
	}
	else if ( line_type == MAKEFILE_LINE_TYPE_COMMAND )
	{
		if ( !parse->parsing_recipe )
			errx(2, "*** command outside of recipe: %s: `%s'", fp_path, line);

		if ( parse->fresh_rule )
		{
			for ( size_t i = 0; i < parse->current_rules_used; i++ )
			{
				struct rule* rule = parse->current_rules[i];
				// TODO: Potentially complain if the rule is overridden.
				rule_reset_commands(rule);
			}
			parse->fresh_rule = false;
		}

		const char* command = line + 1;

		for ( size_t i = 0; i < parse->current_rules_used; i++ )
		{
			struct rule* rule = parse->current_rules[i];
			struct command* cmd = make_command(makefile, parse, command);
			assert(cmd);
			bool success = rule_add_command(rule, cmd);
			assert(success);
		}
	}
}

static char* trim(char* string)
{
	while ( *string && isblank((unsigned char) *string) )
		string++;
	size_t length = strlen(string);
	while ( length && isblank((unsigned char) string[length-1]) )
		length--;
	string[length] = '\0';
	return string;
}

static bool makefile_evaluate_condition(struct makefile* makefile,
                                        const char* line)
{
	while ( *line && (*line == '.' || isblank((unsigned char) *line)) )
		line++;
	if ( *line == 'e' )
		line++;
	if ( *line == 'l' )
		line++;
	if ( *line == 'i' )
		line++;
	if ( *line == 'f' )
		line++;
	bool false_value = false;
	if ( *line == 'n' )
	{
		line++;
		false_value = true;
	}
	bool result = false;
	if ( !strncmp(line, "def", 3) )
	{
		line += 3;
		char* value = makefile_evaluate_value(makefile, line);
		assert(value);
		char* name = trim(value);
		result = makefile_find_variable(makefile, name) != NULL;
		free(value);
	}
	else if ( !strncmp(line, "make", 4) )
	{
		line += 4;
		char* value = makefile_evaluate_value(makefile, line);
		assert(value);
		char* name = trim(value);
		(void) name; // TODO: Test if this target is being built or is default.
		free(value);
	}
	else
	{
		// TODO: Implement the general conditional syntax.
	}
	return result != false_value;
}

static void makefile_block_expectation(struct makefile_block* block,
                                       const char* when,
                                       enum makefile_block_type expected_type)
{
	if ( !block )
		errx(2, "***  %s but no block was open", when);
	const char* expected = "another kind of .end";
	switch ( block->type )
	{
	case MAKEFILE_BLOCK_IF: expected = ".if"; break;
	case MAKEFILE_BLOCK_FOR: expected = ".endfor"; break;
	}
	if ( block->type != expected_type )
		errx(2, "*** %s but expected %s", when, expected);
}

void makefile_preprocess_line(struct makefile* makefile,
                              struct makefile_parse* parse,
                              const char* line,
                              const char* fp_path)
{
	if ( !parse->replaying && parse->recording )
	{
		char* clone = strdup(line);
		assert(clone);
		if ( !array_add((void***) &parse->lines,
		                &parse->lines_used,
		                &parse->lines_length,
		                clone) )
			assert(false);
		parse->playback_offset = parse->lines_used - 1;
	}

	size_t split = 0;
	enum makefile_line_type line_type = makefile_get_line_type(line, &split);

	if ( line_type == MAKEFILE_LINE_TYPE_NOBUILTIN )
	{
		// TODO: Warn if builtins have already been read.
		makefile->lazy_builtin = false;
		return;
	}

	if ( makefile->lazy_builtin && line_type != MAKEFILE_LINE_TYPE_NONE )
		makefile_read_builtin(makefile);

	if ( line_type == MAKEFILE_LINE_TYPE_IF )
	{
		struct makefile_block* block =
			(struct makefile_block*) calloc(1, sizeof(struct makefile_block));
		assert(block);
		block->parent_block = parse->current_block;
		block->type = MAKEFILE_BLOCK_IF;
		parse->current_block = block;
		block->parent_active = parse->active;
		if ( parse->active )
			parse->active = makefile_evaluate_condition(makefile, line);
		block->branch_taken = parse->active;
	}
	else if ( line_type == MAKEFILE_LINE_TYPE_ELSEIF )
	{
		struct makefile_block* block = parse->current_block;
		makefile_block_expectation(block, ".elif", MAKEFILE_BLOCK_IF);
		if ( block->else_encountered )
			errx(2, "*** .elif after .else");
		parse->active = !block->branch_taken && block->parent_active;
		if ( parse->active )
			parse->active = makefile_evaluate_condition(makefile, line);
		block->branch_taken |= parse->active;
	}
	else if ( line_type == MAKEFILE_LINE_TYPE_ELSE )
	{
		struct makefile_block* block = parse->current_block;
		makefile_block_expectation(block, ".else", MAKEFILE_BLOCK_IF);
		if ( block->else_encountered )
			errx(2, "*** .else after .else");
		parse->active = !block->branch_taken && block->parent_active;
		if ( parse->active )
			parse->active = true;
		block->branch_taken |= parse->active;
		block->else_encountered = true;
	}
	else if ( line_type == MAKEFILE_LINE_TYPE_ENDIF )
	{
		struct makefile_block* block = parse->current_block;
		makefile_block_expectation(block, ".endif", MAKEFILE_BLOCK_IF);
		parse->current_block = block->parent_block;
		parse->active = block->parent_active;
		free(block);
	}
	else if ( line_type == MAKEFILE_LINE_TYPE_FOR )
	{
		struct makefile_block* block =
			(struct makefile_block*) calloc(1, sizeof(struct makefile_block));
		assert(block);
		block->parent_block = parse->current_block;
		block->type = MAKEFILE_BLOCK_FOR;
		parse->current_block = block;
		block->parent_active = parse->active;
		block->parent_recording = parse->recording;
		if ( parse->recording )
			block->begun_offset = parse->playback_offset + 1;
		else
			block->begun_offset = 0;
		const char* params = line + split;
		bool last_was_blank = true;
		bool found_in = false;
		size_t in_index = 0;
		// TODO: Intelligently skip over variable expansions and such, like is
		//       done elsewhere in this file.
		for ( size_t i = 0; params[i]; i++ )
		{
			if ( last_was_blank &&
			     params[i + 0] == 'i' &&
			     params[i + 1] == 'n' &&
			     (!params[i + 2] || isblank((unsigned char) params[i + 2])) )
			{
				found_in = true;
				in_index = i;
				break;
			}
			last_was_blank = isblank((unsigned char) params[i]);
		}
		if ( !found_in )
			errx(2, "*** .for without 'in' keyword");
		if ( parse->active )
		{
			char* raw_vars_string = strndup(params, in_index);
			assert(raw_vars_string);
			char* raw_values_string = strdup(params + in_index + 2);
			assert(raw_values_string);

			char* vars_string = makefile_evaluate_value(makefile, raw_vars_string);
			assert(vars_string);
			free(raw_vars_string);
			char* values_string = makefile_evaluate_value(makefile, raw_values_string);
			assert(values_string);
			free(raw_values_string);

			word_split(vars_string, &block->vars, &block->vars_count);
			// TODO: Ensure the variable names are reasonable.
			word_split(values_string, &block->values, &block->values_count);
			free(vars_string);
			free(values_string);

			if ( block->vars_count == 0 )
				errx(2, "*** .for loop with no variables");
			if ( block->values_count % block->vars_count != 0 )
				errx(2, "*** .for loop with value count not multiple of variable count");

			parse->recording = true;
			parse->active = false;
		}
	}
	else if ( line_type == MAKEFILE_LINE_TYPE_ENDFOR )
	{
		struct makefile_block* block = parse->current_block;
		makefile_block_expectation(block, ".endfor", MAKEFILE_BLOCK_FOR);
		if ( block->parent_active )
		{
			bool old_replaying = parse->replaying;
			size_t old_playback_offset = parse->playback_offset;
			parse->replaying = true;
			parse->active = true;
			size_t begun = block->begun_offset;
			size_t end = old_playback_offset;
			while ( block->values_index < block->values_count )
			{
				for ( size_t i = 0; i < block->vars_count; i++ )
				{
					const char* var = block->vars[i];
					const char* value = block->values[block->values_index + i];
					makefile_push_variable(makefile, var, value);
					if ( !array_add((void***) &parse->local_vars,
								    &parse->local_vars_used,
								    &parse->local_vars_length,
								    (char*) var) )
						assert(false);
				}
				for ( size_t i = begun; i < end; i++ )
				{
					parse->playback_offset = i;
					makefile_preprocess_line(makefile, parse, parse->lines[i],
					                         fp_path);
				}
				for ( size_t i = 0; i < block->vars_count; i++ )
				{
					const char* var = block->vars[i];
					makefile_pop_variable(makefile, var);
				}
				parse->local_vars_used -= block->vars_count;
				block->values_index += block->vars_count;
			}
			parse->replaying = old_replaying;
			parse->playback_offset = old_playback_offset;
			for ( size_t i = 0; i < block->vars_count; i++ )
				free(block->vars[i]);
			free(block->vars);
			for ( size_t i = 0; i < block->values_count; i++ )
				free(block->values[i]);
			free(block->values);
		}
		parse->current_block = block->parent_block;
		parse->active = block->parent_active;
		parse->recording = block->parent_recording;
		if ( !parse->recording )
		{
			for ( size_t i = 0; i < parse->lines_used; i++ )
				free(parse->lines[i]);
			free(parse->lines);
			parse->lines = NULL;
			parse->lines_used = 0;
			parse->lines_length = 0;
			parse->playback_offset = 0;
		}
		free(block);
	}
	else if ( parse->active )
	{
		makefile_consume_line(makefile, parse, line, fp_path);
	}
}

char* makefile_read_line(FILE* fp, const char* fp_path)
{
	char* line = NULL;
	size_t line_size = 0;

	errno = 0;
	ssize_t line_length = getline(&line, &line_size, fp);
	if ( line_length < 0 )
	{
		if ( errno != 0 || ferror(fp) )
			err(2, "*** read: `%s'", fp_path);
		return NULL;
	}

	if ( line_length && line[line_length-1] == '\n' )
		line[--line_length] = '\0';

	return line;
}

// TODO: Find out exactly what is supposed to be the right semantics here. This
//       might be a de facto thing to be compatible.
void remove_comment(char* line)
{
	size_t in = 0;
	size_t out = 0;
	while ( line[in] )
	{
		if ( line[in + 0] == '\\' && line[in + 1] == '\\' && line[in + 2] == '#' )
		{
			line[out++] = '\\';
			break;
		}
		else if ( line[in + 0] == '\\' && line[in + 1] == '#' )
		{
			line[out++] = '#';
			in += 2;
		}
		else if ( line[in + 0] == '\\' && line[in + 1] )
		{
			line[out++] = line[in++];
			line[out++] = line[in++];
		}
		else if ( line[in + 0] == '#' )
		{
			break;
		}
		else
		{
			line[out++] = line[in++];
		}
	}
	line[out] = '\0';
}

void makefile_read(struct makefile* makefile, FILE* fp, const char* fp_path)
{
	struct makefile_parse parse;
	memset(&parse, 0, sizeof(parse));
	parse.active = true;

	char* line;
	while ( (line = makefile_read_line(fp, fp_path)) )
	{
		size_t line_length = strlen(line);
		if ( line[0] == '\t' )
		{
			while ( line_length && line[line_length-1] == '\\' )
			{
				line[--line_length] = '\0';
				char* next_line = makefile_read_line(fp, fp_path);
				if ( !next_line )
					break;
				size_t next_line_length = strlen(next_line);
				size_t next_line_skip = 0;
				if ( next_line[next_line_skip] == '\t' )
					next_line_skip++;
				size_t new_line_length = line_length + 2 + (next_line_length - next_line_skip);
				char* new_line = (char*) realloc(line, new_line_length + 1);
				assert(new_line);
				new_line[line_length + 0] = '\\';
				new_line[line_length + 1] = '\n';
				strcpy(new_line + line_length + 2, next_line + next_line_skip);
				free(next_line);
				line = new_line;
				line_length = new_line_length;
			}
			makefile_preprocess_line(makefile, &parse, line, fp_path);
		}
		else
		{
			while ( line_length && line[line_length-1] == '\\' )
			{
				line[--line_length] = '\0';
				char* next_line = makefile_read_line(fp, fp_path);
				if ( !next_line )
					break;
				size_t next_line_length = strlen(next_line);
				size_t next_line_skip = 0;
				while ( next_line[next_line_skip] &&
				        isblank((unsigned char) next_line[next_line_skip]) )
					next_line_skip++;
				size_t new_line_length = line_length + 1 + (next_line_length - next_line_skip);
				char* new_line = (char*) realloc(line, new_line_length + 1);
				assert(new_line);
				// Only add an extra space if the line didn't already end in a
				// space.
				// TODO: Is this what the standard mandates? It's a little
				//       useful as it makes commands more readable and GNU Make
				//       seems to do this, but I don't think BSD make does.
				if ( line_length &&
				     isblank((unsigned char) new_line[line_length-1]) )
				{
					strcpy(new_line + line_length, next_line + next_line_skip);
					new_line_length--;
				}
				else
				{
					new_line[line_length] = ' ';
					strcpy(new_line + line_length + 1, next_line + next_line_skip);
				}
				free(next_line);
				line = new_line;
				line_length = new_line_length;
			}
			remove_comment(line);
			makefile_preprocess_line(makefile, &parse, line, fp_path);
		}
		free(line);
	}

	if ( parse.current_block )
	{
		const char* expected = "a closing .end matching the current block";
		switch ( parse.current_block->type )
		{
		case MAKEFILE_BLOCK_IF: expected = ".endif"; break;
		case MAKEFILE_BLOCK_FOR: expected = ".endfor"; break;
		}
		errx(2, "*** expected %s before end of file", expected);
	}
}

void makefile_read_path(struct makefile* makefile, const char* path)
{
	if ( !strcmp(path, "-") )
		return makefile_read(makefile, stdin, "<stdin>");
	FILE* fp = fopen(path, "r");
	if ( !fp )
		err(2, "*** `%s'", path);
	makefile_read(makefile, fp, path);
	fclose(fp);
}

static bool makefile_read_path_search_dir(struct makefile* makefile,
                                          const char* dir,
                                          const char* path)
{
	char* fullpath = join_paths(dir, path);
	assert(fullpath);
	FILE* fp = fopen(fullpath, "r");
	// TODO: Continue search for other errors?
	if ( !fp && errno == ENOENT )
		return false;
	if ( !fp )
		err(2, "*** include `%s'", fullpath);
	const char* fppath = path;
	if ( fppath[0] == '.' && fppath[1] == '/' && fppath[1] != '/' )
		fppath += 2;
	makefile_read(makefile, fp, fppath);
	fclose(fp);
	return true;
}

void makefile_read_path_search(struct makefile* makefile,
                               const char* path,
                               bool search_quotes,
                               bool try)
{
	if ( (path[0] == '/') ||
	     (path[0] == '.' && path[1] == '/') ||
	     (path[0] == '.' && path[1] == '.' && path[2] == '/') )
	{
		FILE* fp = fopen(path, "r");
		if ( !fp && try )
			return;
		if ( !fp )
			err(2, "*** `%s'", path);
		makefile_read(makefile, fp, path);
		fclose(fp);
		return;
	}

	for ( size_t n = 0; n < runtime_includes_count; n++ )
	{
		size_t i = runtime_includes_count - (n + 1);
		const char* dir = runtime_includes[i];
		if ( makefile_read_path_search_dir(makefile, dir, path) )
			return;
	}

	for ( size_t i = 0; search_quotes && i < quote_includes_count; i++ )
	{
		const char* dir = quote_includes[i];
		if ( makefile_read_path_search_dir(makefile, dir, path) )
			return;
	}

	for ( size_t i = 0; i < angle_includes_count; i++ )
	{
		const char* dir = angle_includes[i];
		if ( makefile_read_path_search_dir(makefile, dir, path) )
			return;
	}

	if ( !try )
	{
		errno = ENOENT;
		err(2, "*** include `%s'", path);
	}
}

void makefile_read_builtin(struct makefile* makefile)
{
	if ( !makefile->lazy_builtin )
		return;
	makefile->lazy_builtin = false;
	bool used_non_posix = makefile->used_non_posix;
	makefile_read_path_search(makefile, "sys.mk", false, false);
	makefile->used_non_posix = used_non_posix;
}
