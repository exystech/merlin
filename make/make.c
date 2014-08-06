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
 * make.c
 * Maintain, update, and regenerate groups of programs.
 */

#define TIXMAKE_DEBUG_STUFF

#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "data.h"
#include "disambiguate.h"
#include "execute.h"
#include "import.h"
#include "make.h"
#include "rule.h"
#include "signal.h"
#include "variable.h"
#include "util.h"

extern char** environ;

// Missing standard features:
//  * Built-in rules and macros.
//  * Library targets.
//  * $%, $(%D), $(%F)

// Missing upcoming standard features:

// Missing non-standard features:
//  * -V
//  * Supporting quotes for target names.
//  * Exporting variables back to the environment.
//  * Allowing `*', `?', `[]' and `{}' in target and prerequisite names.
//  * FOO != bar - should this run bar if FOO comes from a lower source?
//  * BSD make local variable extensions ($<, $(.ALLSRC), ...)?
//  * Update the PWD variable.
//  * BSD make macro expansion modifiers.
//  * .if statements
//  * .for statements
//  * .foo miscellaneous statements from BSD make.
//  * crt/Scrt1.o crt/rcrt1.o: CFLAGS_ALL += -fPIC
//  * FOO ??= foo # as way to set default (but overridable with ?= and ??=).
//  * vpath % foo bar happens in a few ports. vpath %.c and such also happens.

// .POSIX should not warn about non-POSIX features used in sys.mk.
// Perhaps add -U option to unset variables?
// .reset that undoes sys.mk.
// Macro expansion modifiers that sorts a list, and removes removes duplicates.
// Allow variable expansion modifiers to be chained.

char* progname;
char** quote_includes = NULL;
size_t quote_includes_count = 0;
size_t quote_includes_max = 0;
char** angle_includes = NULL;
size_t angle_includes_count = 0;
size_t angle_includes_max = 0;

static void compact_arguments(int* argc, char*** argv)
{
	for ( int i = 0; i < *argc; i++ )
	{
		while ( i < *argc && !(*argv)[i] )
		{
			for ( int n = i; n < *argc; n++ )
				(*argv)[n] = (*argv)[n+1];
			(*argc)--;
		}
	}
}

static void help(FILE* fp, const char* argv0)
{
	fprintf(fp, "Usage: %s [OPTION]... [TARGET]...\n", argv0);
	fprintf(fp, "Maintain, update, and regenerate groups of programs.\n");
	fprintf(fp, "\n");
	// TODO: List of supported options.
}

static void version(FILE* fp, const char* argv0)
{
	fprintf(fp, "%s (Sortix) %s\n", argv0, VERSIONSTR);
	fprintf(fp, "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n");
	fprintf(fp, "This is free software: you are free to change and redistribute it.\n");
	fprintf(fp, "There is NO WARRANTY, to the extent permitted by law.\n");
}

char* parse_makeflag(char** input_ptr)
{
	char* input = *input_ptr;
	if ( !input )
		return NULL;

	while ( *input && isspace((unsigned char) *input) )
		input++;

	if ( !*input )
		return *input_ptr = NULL, (char*) NULL;

	char* result = input;
	char* output = result;

	bool escaped = false;
	bool singly_quoted = false;
	bool doubly_quoted = false;

	while ( true )
	{
		if ( *input == '\0' )
		{
			if ( escaped || singly_quoted || doubly_quoted )
			{
				// TODO: This is an error, the makeflags are malformed.
			}
			break;
		}
		else if ( !escaped && !singly_quoted && *input == '\\' )
			escaped = true;
		else if ( !escaped && !doubly_quoted && *input == '\'' )
			singly_quoted = !singly_quoted;
		else if ( !escaped && !singly_quoted && *input == '"' )
			doubly_quoted = !doubly_quoted;
		else if ( !(escaped || singly_quoted || doubly_quoted) &&
		          isspace((unsigned char) *input) )
			break;
		else
		{
			*output++ = *input;
			escaped = false;
		}
		input++;
	}

	*input_ptr = *input ? input + 1 : NULL;
	*output = '\0';

	return result;
}

static bool parse_makeflags(int* mflagsc_ptr,
                            char*** mflagsv_ptr,
                            const char* makeflags_original)
{
	size_t mflagsc = 0;
	size_t mflagsl = 0;
	char** mflagsv = NULL;

	char* makeflags = strdup(makeflags_original);
	assert(makeflags);

	char* input = makeflags;
	char* argument;
	while ( (argument = parse_makeflag(&input)) )
	{
		char* copy = strdup(argument);
		assert(copy);
		if ( !array_add((void***) &mflagsv, &mflagsc, &mflagsl, copy) )
			assert(false);
	}

	if ( !array_add((void***) &mflagsv, &mflagsc, &mflagsl, NULL) )
		assert(false);
	mflagsc--;

	free(makeflags);

	assert(mflagsc <= INT_MAX);
	*mflagsc_ptr = (int) mflagsc;
	*mflagsv_ptr = mflagsv;

	return true;
}

int main(int argc, char* argv[])
{
	progname = argv[0];

	setlocale(LC_ALL, "");

	struct variable* variable;

	if ( argv[0][0] != '/' && strchr(argv[0], '/') )
	{
		char* argv0_real_path = realpath(argv[0], NULL);
		assert(argv0_real_path);
		setenv("MAKE", argv0_real_path ? argv0_real_path : argv[0], 1);
		free(argv0_real_path);
	}
	else
	{
		setenv("MAKE", argv[0], 1);
	}

	// TODO: What do we do about SIGPIPE?

	setup_cleanup_signals();

	bool print_makefile = false;
	bool no_execute = false;
	bool ignore_errors = false;
	bool silent = false;
	bool always_make = false;
	bool question = false;
	bool touch = false;
	bool keep_going = false;
	bool no_builtin = false;
#ifdef TIXMAKE_DEBUG_STUFF
	bool just_print_debug = false;
#endif

	int real_arguments_start_at = 1;
	const char* makeflags = getenv("MAKEFLAGS");
	if ( makeflags )
	{
		int mflagsc;
		char** mflagsv;
		if ( parse_makeflags(&mflagsc, &mflagsv, makeflags) )
		{
			// TODO: We don't support the silly System V style here (part of
			//       POSIX for some reason). Do we even want to do so?
			// TODO: No effort is made to sanitize the mflags array to prevent:
			//         - Options like -C or -f
			//         - Options like --help and --version
			//         - Targets.
			// TODO: Handle overflow.
			int new_argc = argc + mflagsc;
			char** new_argv = (char**) malloc(sizeof(char*) * (new_argc + 1));
			assert(new_argv);
			new_argv[0] = argv[0];
			for ( int i = 0; i < mflagsc; i++ )
				new_argv[1 + i] = mflagsv[i];
			real_arguments_start_at = mflagsc + 1;
			for ( int i = 1; i < argc; i++ )
				new_argv[mflagsc + i] = argv[i];
			new_argv[new_argc] = NULL;
			argc = new_argc;
			argv = new_argv;
			free(mflagsv);
		}
	}

	enum variable_source env_var_source = VARIABLE_SOURCE_MAKEFILE;
	char** makefile_list = NULL;
	size_t makefile_list_count = 0;
	size_t makefile_list_max = 0;
	char** args_system_includes = NULL;
	size_t args_system_includes_count = 0;
	size_t args_system_includes_max = 0;
	char** args_normal_includes = NULL;
	size_t args_normal_includes_count = 0;
	size_t args_normal_includes_max = 0;

	const char* argv0 = argv[0];
	for ( int i = 1; i < argc; i++ )
	{
		const char* arg = argv[i];
		if ( arg[0] != '-' || !arg[1] )
			continue;
		argv[i] = NULL;
		if ( !strcmp(arg, "--") )
		{
			if ( i < real_arguments_start_at )
			{
				i = real_arguments_start_at - 1;
				continue;
			}
			break;
		}
		if ( arg[1] != '-' )
		{
			char c;
			while ( (c = *++arg) ) switch ( c )
			{
			case 'B': always_make = true; break;
			case 'C':
			{
				const char* chdir_value;
				if ( !*(chdir_value = arg + 1) )
				{
					if ( i + 1 == argc )
					{
						warnx("option requires an argument -- 'C'");
						fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
						exit(2);
					}
					chdir_value = argv[i+1];
					argv[++i] = NULL;
				}
				// TODO: Print a `entering directory' message.
				if ( chdir(chdir_value) < 0 )
					err(2, "*** `%s'", chdir_value);
				arg = "C";
				break;
			}
			case 'e': env_var_source = VARIABLE_SOURCE_ENVIRONMENT; break;
			case 'f':
			{
				const char* makefile_path;
				if ( !*(makefile_path = arg + 1) )
				{
					if ( i + 1 == argc )
					{
						warnx("option requires an argument -- 'f'");
						fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
						exit(2);
					}
					makefile_path = argv[i+1];
					argv[++i] = NULL;
				}
				char* path = strdup(makefile_path);
				assert(path);
				if ( !array_add((void***) &makefile_list,
				                &makefile_list_count,
				                &makefile_list_max,
				                path) )
					assert(false);
				arg = "f";
				break;
			}
			case 'i': ignore_errors = true; break;
			case 'I':
			{
				// TODO: If MAKEFLAGS contains -I and this make was passed an
				//       explicit -I option, then the actually passed -I should
				//       be first in the search order.
				const char* include_path;
				if ( !*(include_path = arg + 1) )
				{
					if ( i + 1 == argc )
					{
						warnx("option requires an argument -- 'I'");
						fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
						exit(2);
					}
					include_path = argv[i+1];
					argv[++i] = NULL;
				}
				char* path = strdup(include_path);
				assert(path);
				if ( !array_add((void***) &args_normal_includes,
				                &args_normal_includes_count,
				                &args_normal_includes_max,
				                path) )
					assert(false);
				arg = "m";
				break;
			}
			case 'k': keep_going = true; break;
			case 'm':
			{
				// TODO: If MAKEFLAGS contained -m, but this make was passed
				//       passed -m, then reset all the -m's passed so far and
				//       start with an empty list.
				const char* include_path;
				if ( !*(include_path = arg + 1) )
				{
					if ( i + 1 == argc )
					{
						warnx("option requires an argument -- 'm'");
						fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
						exit(2);
					}
					include_path = argv[i+1];
					argv[++i] = NULL;
				}
				char* path = strdup(include_path);
				assert(path);
				if ( !array_add((void***) &args_system_includes,
				                &args_system_includes_count,
				                &args_system_includes_max,
				                path) )
					assert(false);
				arg = "m";
				break;
			}
			case 'n': no_execute = true; break;
			case 'p': print_makefile = true; break;
#ifdef TIXMAKE_DEBUG_STUFF
			case 'P': just_print_debug = true; break;
#endif
			case 'q': question = true; break;
			case 'r': no_builtin = true; break;
			case 's': silent = true; break;
			case 'S': keep_going = false; break;
			case 't': touch = true; break;
			default:
				fprintf(stderr, "%s: unknown option -- '%c'\n", argv0, c);
				help(stderr, argv0);
				exit(1);
			}
		}
		else if ( !strcmp(arg, "--help") )
			help(stdout, argv0), exit(0);
		else if ( !strcmp(arg, "--version") )
			version(stdout, argv0), exit(0);
		// TODO: Support the alternative long options that GNU make has.
		else
		{
			fprintf(stderr, "%s: unknown option: %s\n", argv0, arg);
			help(stderr, argv0);
			exit(1);
		}
	}

	compact_arguments(&argc, &argv);

	char* dotpath = strdup(".");
	assert(dotpath);
	if ( !array_add((void***) &quote_includes,
	                &quote_includes_count,
	                &quote_includes_max,
	                dotpath) )
		assert(false);

	for ( size_t i = 0; i < args_normal_includes_count; i++ )
	{
		if ( !array_add((void***) &angle_includes,
		                &angle_includes_count,
		                &angle_includes_max,
		                args_normal_includes[i]) )
			assert(false);
	}

	for ( size_t i = 0; i < args_system_includes_count; i++ )
	{
		if ( !array_add((void***) &angle_includes,
		                &angle_includes_count,
		                &angle_includes_max,
		                args_system_includes[i]) )
			assert(false);
	}

	if ( args_system_includes_count == 0 )
	{
		char* share_mk = strdup(SHAREMKDIR);
		assert(share_mk);
		if ( !array_add((void***) &angle_includes,
		                &angle_includes_count,
		                &angle_includes_max,
		                share_mk) )
			assert(false);
	}

	struct makefile makefile;
	memset(&makefile, 0, sizeof(makefile));

	{
		char* new_makeflags;
		size_t new_makeflags_size;
		FILE* makeflags_fp = open_memstream(&new_makeflags, &new_makeflags_size);
		assert(makeflags_fp);

		const char* new_makeflags_prefix = "-";

		if ( always_make )
			fprintf(makeflags_fp, "%s%s", new_makeflags_prefix, "B"),
			new_makeflags_prefix = "";
		// It doesn't make sense to add -C to makeflags.
		if ( env_var_source == VARIABLE_SOURCE_ENVIRONMENT )
			fprintf(makeflags_fp, "%s%s", new_makeflags_prefix, "e"),
			new_makeflags_prefix = "";
		// It doesn't make sense to add -f to makeflags.
		if ( ignore_errors )
			fprintf(makeflags_fp, "%s%s", new_makeflags_prefix, "i"),
			new_makeflags_prefix = "";
		if ( keep_going )
			fprintf(makeflags_fp, "%s%s", new_makeflags_prefix, "k"),
			new_makeflags_prefix = "";
		if ( no_execute )
			fprintf(makeflags_fp, "%s%s", new_makeflags_prefix, "n"),
			new_makeflags_prefix = "";
		// It doesn't make sense to add -p to makeflags.
#ifdef TIXMAKE_DEBUG_STUFF
		// It doesn't make sense to add -P to makeflags.
#endif
		if ( question )
			fprintf(makeflags_fp, "%s%s", new_makeflags_prefix, "q"),
			new_makeflags_prefix = "";
		if ( no_builtin )
			fprintf(makeflags_fp, "%s%s", new_makeflags_prefix, "r"),
			new_makeflags_prefix = "";
		if ( silent )
			fprintf(makeflags_fp, "%s%s", new_makeflags_prefix, "s"),
			new_makeflags_prefix = "";
		// The -S option is added to MAKEFLAGS as the lack of the -k option.
		if ( touch )
			fprintf(makeflags_fp, "%s%s", new_makeflags_prefix, "t"),
			new_makeflags_prefix = "";

		bool anything_so_far = new_makeflags_prefix[0] == '\0';

		for ( size_t i = 0; i < args_system_includes_count; i++ )
		{
			if ( anything_so_far )
				fprintf(makeflags_fp, " ");
			fprintf(makeflags_fp, "-m");
			const char* path = args_system_includes[i];
			// TODO: We should canonicalize the path if it isn't absolute.
			char* entry = shell_singly_escape_value(path);
			fprintf(makeflags_fp, "%s", entry);
			free(entry);
			anything_so_far = true;
		}

		for ( size_t i = 0; i < args_normal_includes_count; i++ )
		{
			if ( anything_so_far )
				fprintf(makeflags_fp, " ");
			fprintf(makeflags_fp, "-I");
			const char* path = args_normal_includes[i];
			// TODO: We should canonicalize the path if it isn't absolute.
			char* entry = shell_singly_escape_value(path);
			fprintf(makeflags_fp, "%s", entry);
			free(entry);
			anything_so_far = true;
		}

		fprintf(makeflags_fp, " --");

		for ( size_t i = 0; i < makefile.variables_used; i++ )
		{
			variable = makefile.variables[i];
			if ( !strcmp(variable->name, "MAKEFLAGS") )
				continue;
			if ( !(variable->flags & VARIABLE_FLAG_MAKEFLAG) )
				continue;
			if ( !variable->value )
			{
				// TODO: Hmm. We can't properly communicate the variable is
				//       unset. This shouldn't be possible though.
				continue;
			}

			// TODO: Is it right to escape the name of the variable?
			char* name_escaped = make_escape_value(variable->name);
			assert(name_escaped);

			char* assignment = print_string("%s=%s", name_escaped, variable->value);
			assert(assignment);
			free(name_escaped);

			char* assignment_escaped = shell_singly_escape_value(assignment);
			assert(assignment_escaped);
			free(assignment);

			// TODO: Huh. Did we forget to write the assignment to makeflags_fp?

			free(assignment_escaped);
		}

		int makeflags_fp_ret = fclose(makeflags_fp);
		assert(makeflags_fp_ret == 0);

		char* escaped_makeflags = make_escape_value(new_makeflags);
		assert(escaped_makeflags);
		free(new_makeflags);

		// TODO: Makeflags needs to be escaped here so it is unchanged when it
		//       is evaluated.

		variable = makefile_create_variable(&makefile, "MAKEFLAGS");
		assert(variable);
		variable_assign(variable, escaped_makeflags, VARIABLE_SOURCE_MAKEFILE);
		variable->flags |= VARIABLE_FLAG_EXPORT;

		free(escaped_makeflags);
	}

	variable = makefile_create_variable(&makefile, "SHELL");
	assert(variable);
	variable_assign(variable, "sh", VARIABLE_SOURCE_MAKEFILE);

	for ( size_t i = 0; environ[i]; i++ )
	{
		const char* envvar = environ[i];
		if ( !strncmp(envvar, "MAKEFLAGS=", strlen("MAKEFLAGS=")) )
			continue;
		if ( !strncmp(envvar, "SHELL=", strlen("SHELL=")) )
			continue;
		size_t equal_position = strcspn(envvar, "=");
		if ( envvar[equal_position] != '=' )
			continue;
		char* name = strndup(envvar, equal_position);
		const char* value = envvar + equal_position + 1;
		variable = makefile_create_variable(&makefile, name);
		assert(variable);
		variable_assign(variable, value, env_var_source);
		free(name);
	}

	// TODO: The results are unspecified if macro and target operands are
	//       intermixed. But we probably want to process all macros before doing
	//       any targets, as that's the traditional and generally useful
	//       behavior.
	for ( int i = 1; i < argc; i++ )
	{
		// TODO: We should probably support ?=, !=, :=, += here too.
		size_t equal_position = strcspn(argv[i], "=");
		if ( argv[i][equal_position] != '=' )
			continue;
		// TODO: POSIX says SHELL should be rejected here. But we do want to
		//       allow that?
		// TODO: Is it the right semantics to prohibit setting MAKEFLAGS here?
		char* name = strndup(argv[i], equal_position);
		assert(name);
		if ( !strcmp(name, "MAKEFLAGS") )
		{
			free(name);
			continue;
		}
		// TODO: We may wish to evaluate name here as that's what GNU make does,
		//       and it makes a bit of sense as it matches the semantics for
		//       regular macro assignments in a makefile.
		const char* value = argv[i] + equal_position + 1;
		variable = makefile_create_variable(&makefile, name);
		assert(variable);
		variable_assign(variable, value, VARIABLE_SOURCE_COMMAND_LINE);
		// We force this export as GNU make doesn't seem to allow unexporting
		// these variables.
		variable->flags |= VARIABLE_FLAG_FORCE_EXPORT;
		variable->flags |= VARIABLE_FLAG_MAKEFLAG;
		free(name);
		argv[i] = NULL;
	}

	compact_arguments(&argc, &argv);

	if ( !no_builtin )
		makefile.lazy_builtin = true;

	bool read_any_makefiles = false;
	for ( size_t i = 0; i < makefile_list_count; i++ )
	{
		makefile_read_path(&makefile, makefile_list[i]);
		free(makefile_list[i]);
		read_any_makefiles = true;
	}
	free(makefile_list);

	if ( !read_any_makefiles )
	{
		const char* candidates[] =
		{
			"TIXmakefile",
			"makefile",
			"Makefile",
			(const char*) NULL
		};

		for ( size_t i = 0; candidates[i]; i++ )
		{
			const char* makefile_path = candidates[i];
			FILE* makefile_fp = fopen(makefile_path, "r");
			if ( makefile_fp )
			{
				makefile_read(&makefile, makefile_fp, makefile_path);
				fclose(makefile_fp);
				read_any_makefiles = true;
				break;
			}
			if ( errno != ENOENT )
				err(2, "*** `%s'", makefile_path);
		}
	}

	if ( !read_any_makefiles && makefile.lazy_builtin )
		makefile_read_builtin(&makefile);

	for ( size_t i = 0; i < makefile.variables_used; i++ )
	{
		variable = makefile.variables[i];
		// TODO: Check if the variable has been marked for export to the
		//       environment (forcibly or normally).
		if ( !(variable->flags & VARIABLE_FLAG_EXPORT ||
		       variable->flags & VARIABLE_FLAG_FORCE_EXPORT) )
			continue;
		if ( !variable->value )
		{
			// TODO: What does this mean? Should we unsetenv it?
			continue;
		}
		char* value = makefile_evaluate_value(&makefile, variable->value);
		assert(value);
		setenv(variable->name, value, 1);
		free(value);
	}

	char* raw_default_target = NULL;
	const char* default_target = NULL;
	struct variable* default_goal_var = makefile_find_variable(&makefile, ".DEFAULT_GOAL");
	const char* default_goal = variable_value_def(default_goal_var, NULL);
	if ( default_goal )
	{
		raw_default_target =
			makefile_evaluate_value(&makefile, default_goal);
		assert(raw_default_target);
		default_target = raw_default_target;
		// TODO: Ensure only one target is listed here.
		while ( default_target[0] &&
		        isblank((unsigned char) default_target[0]) )
			default_target++;
		size_t default_target_length = strlen(default_target);
		while ( default_target_length &&
		        isblank((unsigned char) default_target[default_target_length-1]) )
			((char*) default_target)[--default_target_length] = '\0';
	}

	if ( !default_target )
	{
		for ( size_t i = 0; i < makefile.rules_used; i++ )
		{
			if ( makefile.rules[i]->name[0] == '.' &&
			     !strchr(makefile.rules[i]->name, '/') )
				continue;
			default_target = makefile.rules[i]->name;
			break;
		}
	}

	if ( default_target )
		makefile_infer_rule(&makefile, default_target);

	for ( int i = 1; i < argc; i++ )
	{
		// TODO: I think GNU make evaluates the target name here. Should we do
		//       that too?
		makefile_infer_rule(&makefile, argv[i]);
	}

	struct make_disambiguate make_disambiguate;
	memset(&make_disambiguate, 0, sizeof(make_disambiguate));
	makefile_disambiguate(&makefile, &make_disambiguate);

	struct make make;
	memset(&make, 0, sizeof(make));
	make.no_execute = no_execute;
	make.ignore_errors = ignore_errors;
	make.silent = silent;
	make.always_make = always_make;
	make.question = question;
	make.touch = touch;
	make.keep_going = keep_going;

	enum make_result result = MAKE_RESULT_WAS_UP_TO_DATE;

	if ( !just_print_debug )
	{
		if ( argc <= 1 )
		{
			if ( !default_target && !read_any_makefiles )
				errx(2, "*** No targets specified and no makefile found.  Stop.");
			if ( !default_target )
				errx(2, "*** No targets.  Stop.");
			const char* target =
				makefile_vpath_detour(&make_disambiguate, default_target);
			result = make_target_goal(&makefile, &make, target);
		}

		for ( int i = 1; i < argc; i++ )
		{
			// TODO: We should perhaps evaluate the argument here.
			const char* target = makefile_vpath_detour(&make_disambiguate, argv[i]);
			enum make_result part_result =
				make_target_goal(&makefile, &make, target);
			if ( part_result == MAKE_RESULT_UPDATED &&
				 result == MAKE_RESULT_WAS_UP_TO_DATE )
				result = MAKE_RESULT_UPDATED;
			if ( part_result == MAKE_RESULT_ERROR )
				result = MAKE_RESULT_ERROR;
		}
	}

	if ( print_makefile || just_print_debug )
	{
		for ( size_t i = 0; i < makefile.variables_used; i++ )
		{
			struct variable* variable = makefile.variables[i];
			if ( !variable->value )
				continue;

			printf("%s = %s\n", variable->name, variable->value);
		}

		for ( size_t i = 0; i < makefile.rules_used; i++ )
		{
			struct rule* rule = makefile.rules[i];

			if ( rule->needs_to_be_inferred )
			{
				printf("# %s has no rule\n", rule->name);
				continue;
			}

			printf("%s:", rule->name);
			for ( size_t n = 0; n < rule->prerequisites_used; n++ )
				printf(" %s", rule->prerequisites[n]);
			printf("\n");

			for ( size_t n = 0; n < rule->commands_used; n++ )
			{
				struct command* cmd = rule->commands[n];
				for ( size_t i = 0; i < cmd->extra_count; i++ )
					printf(".push %s %s\n", cmd->extra_vars[i],
					       cmd->extra_values[i]);
				printf("\t%s\n", cmd->cmd);
				for ( size_t i = 0; i < cmd->extra_count; i++ )
					printf(".pop %s\n", cmd->extra_vars[i]);
			}
		}
	}

	free(raw_default_target);

	if ( result == MAKE_RESULT_ERROR )
		return 2;

	if ( make.question && result == MAKE_RESULT_UPDATED )
		return 1;

	return 0;
}
