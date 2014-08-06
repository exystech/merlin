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
 * execute.c
 * Executes the makefile.
 */

#include <sys/stat.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bstree.h"
#include "data.h"
#include "disambiguate.h"
#include "execute.h"
#include "make.h"
#include "rule.h"
#include "signal.h"
#include "variable.h"
#include "util.h"

extern char* progname;

bool make_cached_stat(struct target* target)
{
	if ( target->stat_cached )
		return true;
	if ( stat(target->rule->name, &target->st) < 0 )
		return false;
	target->stat_cached = true;
	return true;
}

static int compare_target(const void* a_ptr, const void* b_ptr)
{
	const struct target* a = (const struct target*) a_ptr;
	const struct target* b = (const struct target*) b_ptr;
	return strcmp(a->rule->name, b->rule->name);
}

static int search_target(const void* name_ptr, const void* target_ptr)
{
	const char* name = (const char*) name_ptr;
	const struct target* target = (const struct target*) target_ptr;
	return strcmp(name, target->rule->name);
}

struct target* make_find_target(struct makefile* makefile,
                                struct make* make,
                                const char* name)
{
	(void) makefile;
	return bstree_search(make->targets, name, search_target);
}

struct target* make_create_target(struct makefile* makefile,
                                  struct make* make,
                                  const char* name)
{
	struct target* target = make_find_target(makefile, make, name);
	if ( target )
		return target;
	struct rule* rule = makefile_find_rule(makefile, name);
	if ( !rule )
	{
		// TODO: It's ambiguous whether this is an error or because there is no
		//       such rule.
		return NULL;
	}
	target = (struct target*) calloc(1, sizeof(struct target));
	if ( !target )
		return NULL;
	target->rule = rule;
	if ( !bstree_insert(&make->targets, target, compare_target) )
		return free(target), (struct target*) NULL;
	return target;
}

// TODO: Double-check this logic is the correct one.
// TODO: Is it true that targets with no prerequisites are always out of date?
static bool make_target_needs_update(struct makefile* makefile,
                                     struct make* make,
                                     struct target* target)
{
	if ( make->always_make )
		return true;

	struct rule* rule = target->rule;

	if ( makefile_is_prerequisite_of(makefile, rule->name, ".PHONY") )
		return true;

	if ( !make_cached_stat(target) )
	{
		if ( errno == ENOENT )
			return true;
		// TODO: This aborts the process, but is that the intended behavior?
		//       See options like -k.
		err(2, "*** stat: `%s'", rule->name);
	}

	bool all_prerequisites_older = true;

	for ( size_t i = 0; i < rule->prerequisites_used; i++ )
	{
		const char* prerequisite = rule->prerequisites[i];

		// TODO: What is the correct behavior when a target depends on a phony
		//       target?
		// TODO: What if that that prerequisite has its own prerequisite that is
		//       phony (transitive property)?
		if ( makefile_is_prerequisite_of(makefile, prerequisite, ".PHONY") )
		{
			all_prerequisites_older = false;
			break;
		}

		struct target* prereq_target =
			make_create_target(makefile, make, prerequisite);
		assert(prereq_target);

		if ( make_cached_stat(prereq_target) )
		{
			struct timespec target_ts = target->st.st_mtim;
			struct timespec prerequisite_ts = prereq_target->st.st_mtim;
			bool is_older = prerequisite_ts.tv_sec < target_ts.tv_sec ||
			                (prerequisite_ts.tv_sec == target_ts.tv_sec &&
			                 prerequisite_ts.tv_nsec < target_ts.tv_nsec);
			if ( !is_older )
			{
				all_prerequisites_older = false;
				break;
			}
		}
	}

	return !all_prerequisites_older;
}

static void makefile_set_internal_paths(struct makefile* makefile,
                                        char name_c,
                                        const char* value,
                                        const char* value_d,
                                        const char* value_f)
{
	struct variable* variable;

	char name[] = { name_c, '\0' };
	variable = makefile_create_variable(makefile, name);
	assert(variable);
	variable_assign(variable, value, VARIABLE_SOURCE_INTERNAL);

	char name_D[] = { name_c, 'D', '\0' };
	variable = makefile_create_variable(makefile, name_D);
	assert(variable);
	variable_assign(variable, value_d, VARIABLE_SOURCE_INTERNAL);

	char name_F[] = { name_c, 'F', '\0' };
	variable = makefile_create_variable(makefile, name_F);
	assert(variable);
	variable_assign(variable, value_f, VARIABLE_SOURCE_INTERNAL);
}

static void makefile_set_internal_path(struct makefile* makefile,
                                       char name_c,
                                       const char* value)
{
	char* value_d_raw = value ? strdup(value) : NULL;
	assert(!value || value_d_raw);
	char* value_f_raw = value ? strdup(value) : NULL;
	assert(!value || value_f_raw);

	const char* value_d = value ? dirname(value_d_raw) : NULL;
	const char* value_f = value ? basename(value_f_raw) : NULL;

	makefile_set_internal_paths(makefile, name_c, value, value_d, value_f);

	free(value_d_raw);
	free(value_f_raw);
}

static void makefile_set_internal(struct makefile* makefile,
                                  struct make* make,
                                  struct target* target)
{
	struct rule* rule = target->rule;

	const char* internal_lt_value = NULL;
	if ( rule->inferred_from )
		internal_lt_value = rule->inferred_from;
	// TODO: Is this logic correct?
	else if ( makefile_try_use_non_posix(makefile) &&
	          1 <= rule->prerequisites_used )
		internal_lt_value = rule->prerequisites[0];

	char* q_value;
	size_t q_value_size;
	FILE* q_fp = open_memstream(&q_value, &q_value_size);
	assert(q_fp);

	char* q_value_d;
	size_t q_value_d_size;
	FILE* q_fp_d = open_memstream(&q_value_d, &q_value_d_size);
	assert(q_fp_d);

	char* q_value_f;
	size_t q_value_f_size;
	FILE* q_fp_f = open_memstream(&q_value_f, &q_value_f_size);
	assert(q_fp_f);

	// TODO: $^ is an extension to POSIX.

	char* x_value;
	size_t x_value_size;
	FILE* x_fp = open_memstream(&x_value, &x_value_size);
	assert(x_fp);

	char* x_value_d;
	size_t x_value_d_size;
	FILE* x_fp_d = open_memstream(&x_value_d, &x_value_d_size);
	assert(x_fp_d);

	char* x_value_f;
	size_t x_value_f_size;
	FILE* x_fp_f = open_memstream(&x_value_f, &x_value_f_size);
	assert(x_fp_f);

	// TODO: GNU make also has $+ and $|.

	bool prerequisites_are_always_newer = false;
	if ( makefile_is_prerequisite_of(makefile, rule->name, ".PHONY") ||
	     !make_cached_stat(target) )
		prerequisites_are_always_newer = true;

	// TODO: Only do this if needed, perhaps lazily evaluated.
	const char* q_prefix = "";
	const char* x_prefix = "";
	for ( size_t i = 0; i < rule->prerequisites_used; i++ )
	{
		const char* p_name = rule->prerequisites[i];

		bool duplicate = false;
		// TODO: O(N²).
		for ( size_t n = 0; !duplicate && n < i; n++ )
		{
			if ( !strcmp(p_name, rule->prerequisites[n]) )
				duplicate = true;
		}

		bool p_newer = true;
		if ( !duplicate &&
		     !prerequisites_are_always_newer &&
		     !makefile_is_prerequisite_of(makefile, p_name, ".PHONY") )
		{
			struct target* prereq_target =
				make_create_target(makefile, make, p_name);
			assert(prereq_target);

			if ( make_cached_stat(prereq_target) )
			{
				struct timespec p_ts = target->st.st_mtim;
				struct timespec q_ts = prereq_target->st.st_mtim;
				if ( p_ts.tv_sec < q_ts.tv_sec ||
				     (p_ts.tv_sec == q_ts.tv_sec &&
				      p_ts.tv_nsec < q_ts.tv_nsec) )
					p_newer = false;
			}
		}

		char* p_name_d_raw = strdup(p_name);
		assert(p_name_d_raw);
		char* p_name_f_raw = strdup(p_name);
		assert(p_name_f_raw);

		// TODO: What do we do if the name contains blanks here?

		if ( !duplicate )
		{
			fprintf(x_fp, "%s%s", x_prefix, p_name);
			fprintf(x_fp_d, "%s%s", x_prefix, dirname(p_name_d_raw));
			fprintf(x_fp_f, "%s%s", x_prefix, basename(p_name_f_raw));
			x_prefix = " ";

			if ( p_newer )
			{
				fprintf(q_fp, "%s%s", q_prefix, p_name);
				fprintf(q_fp_d, "%s%s", q_prefix, dirname(p_name_d_raw));
				fprintf(q_fp_f, "%s%s", q_prefix, basename(p_name_f_raw));
				q_prefix = " ";
			}
		}

		free(p_name_d_raw);
		free(p_name_f_raw);
	}

	// TODO: Audit these uses of open_memstream and whether the error handling
	//       is correct.

	int q_fp_ret = fclose(q_fp);
	assert(q_fp_ret == 0);
	int q_fp_d_ret = fclose(q_fp_d);
	assert(q_fp_d_ret == 0);
	int q_fp_f_ret = fclose(q_fp_f);
	assert(q_fp_f_ret == 0);

	int x_fp_ret = fclose(x_fp);
	assert(x_fp_ret == 0);
	int x_fp_d_ret = fclose(x_fp_d);
	assert(x_fp_d_ret == 0);
	int x_fp_f_ret = fclose(x_fp_f);
	assert(x_fp_f_ret == 0);

	makefile_set_internal_path(makefile, '@', rule->name);
	makefile_set_internal_path(makefile, '<', internal_lt_value);
	makefile_set_internal_path(makefile, '*', rule->stem);
	makefile_set_internal_paths(makefile, '?', q_value, q_value_d, q_value_f);
	makefile_set_internal_paths(makefile, '^', x_value, x_value_d, x_value_f);

	free(q_value);
	free(q_value_d);
	free(q_value_f);
	free(x_value);
	free(x_value_d);
	free(x_value_f);
}

enum make_result make_target(struct makefile* makefile,
                             struct make* make,
                             struct target* target)
{
	if ( target->has_been_made )
		return target->result;

	struct rule* rule = target->rule;

	if ( target->currently_being_made )
	{
		// TODO: Perhaps the make process should not exit successfully?
		if ( !target->warned_circular_dependency )
			warnx("*** Circular %s <- %s dependency dropped.",
			      rule->name, rule->name);
		target->warned_circular_dependency = true;
		return MAKE_RESULT_WAS_UP_TO_DATE;
	}

	if ( rule->needs_to_be_inferred )
	{
		if ( make_cached_stat(target) )
			return MAKE_RESULT_WAS_UP_TO_DATE;

		warnx("*** No rule to make target '%s'.  Stop.", rule->name);
		if ( make->keep_going )
			exit(2);
		return target->has_been_made = true,
		       target->result = MAKE_RESULT_ERROR;
	}

	target->currently_being_made = true;

	enum make_result prerequisites_result = MAKE_RESULT_WAS_UP_TO_DATE;
	for ( size_t i = 0; i < rule->prerequisites_used; i++ )
	{
		const char* prerequisite = rule->prerequisites[i];
		enum make_result result = make_target_name(makefile, make, prerequisite);
		if ( result == MAKE_RESULT_UPDATED &&
		     prerequisites_result == MAKE_RESULT_WAS_UP_TO_DATE )
			prerequisites_result = MAKE_RESULT_UPDATED;
		if ( result == MAKE_RESULT_ERROR )
			prerequisites_result = MAKE_RESULT_ERROR;
	}

	target->currently_being_made = false;

	if ( prerequisites_result == MAKE_RESULT_ERROR )
		return target->has_been_made = true,
		       target->result = MAKE_RESULT_ERROR;

	if ( !make_target_needs_update(makefile, make, target) )
		return target->has_been_made = true,
		       target->result = prerequisites_result;

	if ( !rule->commands_used )
		return target->has_been_made = true,
		       target->result = prerequisites_result;

	makefile_set_internal(makefile, make, target);

	enum make_result target_result = MAKE_RESULT_WAS_UP_TO_DATE;

	bool target_marked_precious =
		makefile_is_prerequisite_of_ex(makefile, rule->name, ".PRECIOUS", true);
	bool target_marked_ignore =
		makefile_is_prerequisite_of_ex(makefile, rule->name, ".IGNORE", true);
	bool target_marked_silent =
		makefile_is_prerequisite_of_ex(makefile, rule->name, ".SILENT", true);

	target->stat_cached = false;

	for ( size_t i = 0; i < rule->commands_used; i++ )
	{
		struct command* cmd = rule->commands[i];

		for ( size_t n = 0; n < cmd->extra_count; n++)
		{
			const char* var = cmd->extra_vars[n];
			const char* value = cmd->extra_values[n];
			makefile_push_variable(makefile, var, value);
		}

		// TODO: An occurence of $(MAKE) should trigger the + behavior.
		char* raw_command = makefile_evaluate_value(makefile, cmd->cmd);
		assert(raw_command);

		for ( size_t n = 0; n < cmd->extra_count; n++ )
		{
			const char* var = cmd->extra_vars[n];
			makefile_pop_variable(makefile, var);
		}

		const char* command = raw_command;

		bool prefix_ignore = false;
		bool prefix_silent = false;
		bool prefix_force = false;

		// TODO: Is this logic correct? What if one of these prefix characters
		//       appears twice?
		while ( command[0] )
		{
			if ( command[0] == '-' )
				command++, prefix_ignore = true;
			else if ( command[0] == '@' )
				command++, prefix_silent = true;
			else if ( command[0] == '+' )
				command++, prefix_force = true;
			else
				break;
		};

		if ( target_marked_ignore )
			prefix_ignore = true;
		if ( target_marked_silent )
			prefix_silent = true;

		// TODO: Perhaps it's not best to do this for every command (but just
		//       for a single one) and what do do if one of the commands start
		//       with a plus? This mimics the GNU make semantics. The BSD make
		//       I tried didn't seem to properly deal with + and -t.
		if ( make->touch && !prefix_force )
		{
			free(raw_command);
			// TODO: Perhaps it's best not to shell out to the touch program but
			//       do it locally in this process?
			// TODO: Properly escape special characters!
			int result = asprintf(&raw_command, "touch '%s'", rule->name);
			assert(0 <= result);
			assert(raw_command);
			command = raw_command;
		}

		bool no_execute = (make->question || make->no_execute) && !prefix_force;
		bool no_print = (make->question && !prefix_force) ||
		                (prefix_silent && !make->no_execute) ||
		                make->silent;

		if ( !no_print )
		{
			printf("%s\n", command);
			fflush(stdout);
		}

		if ( no_execute )
		{
			target_result = MAKE_RESULT_UPDATED;
			free(raw_command);
			continue;
		}

		if ( !make->question && target_result == MAKE_RESULT_WAS_UP_TO_DATE )
			target_result = MAKE_RESULT_UPDATED;

		const char* shell = variable_value(makefile_find_variable(makefile, "SHELL"));
		if ( !shell || !shell[0] )
			shell = "sh";

		sigset_t saved_sigset;
		sigprocmask(SIG_BLOCK, &cleanup_signals, &saved_sigset);

		pid_t child_pid = fork();
		if ( child_pid < 0 )
		{
			sigprocmask(SIG_SETMASK, &saved_sigset, NULL);
			err(2, "*** fork()");
		}

		if ( child_pid == 0 )
		{
			sigprocmask(SIG_SETMASK, &saved_sigset, NULL);
			const char* argv[] = { shell, "-c", command, NULL };
			const char* argv_e[] = { shell, "-e", "-c", command, NULL };
			execvp(shell, (char**) (prefix_ignore ? argv : argv_e));
			_exit(127);
		}

		volatile int real_exit_code = INT_MIN;

		cleanup_desired = true;
		cleanup_in_progress = false;
		cleanup_forward_pid = child_pid;
		cleanup_exit_code_ptr = (int*) &real_exit_code;

		sigprocmask(SIG_SETMASK, &saved_sigset, NULL);

		int waitpid_result = waitpid(child_pid, (int*) &real_exit_code, 0);

		sigprocmask(SIG_BLOCK, &cleanup_signals, &saved_sigset);
		cleanup_desired = false;
		if ( cleanup_signal_arrived_flag )
			cleanup_in_progress = true;
		int the_cleanup_signal = cleanup_signal_arrived_flag;
		cleanup_forward_pid = 0;
		cleanup_exit_code_ptr = NULL;
		sigprocmask(SIG_SETMASK, &saved_sigset, NULL);

		int exit_code = real_exit_code;

		if ( waitpid_result < 0 )
			errx(2, "*** waitpid()");

		if ( the_cleanup_signal != 0 )
		{
			// TODO: Don't do this if -p.
			// TODO: Don't do this if -q.

			if ( !target_marked_precious )
			{
				if ( unlink(rule->name) == 0 )
					warnx("*** Deleting file `%s'", rule->name);
				else if ( errno != ENOENT && errno != EISDIR )
					warn("*** unlink: `%s'", rule->name);
			}

			sigprocmask(SIG_BLOCK, &cleanup_signals, &saved_sigset);
			cleanup_in_progress = false;
			sigprocmask(SIG_SETMASK, &saved_sigset, NULL);
		}

		bool fatal_error = !prefix_ignore && !make->keep_going;
		const char* err_sfx = !prefix_ignore ? "" : " (ignored)";

		if ( WIFEXITED(exit_code) && WEXITSTATUS(exit_code) == 0 )
		{
		}
		else if ( WIFEXITED(exit_code) )
		{
			printf("recipe for target '%s' failed\n", rule->name);
			fflush(stdout);
			warnx("*** [%s] Error %u%s", rule->name,
			      WEXITSTATUS(exit_code), err_sfx);
			if ( fatal_error )
				exit(2);
			if ( !prefix_ignore )
				target_result = MAKE_RESULT_ERROR;
		}
		else if ( WIFSIGNALED(exit_code) )
		{
			printf("recipe for target '%s' failed\n", rule->name);
			fflush(stdout);
			const char* signal_description = strsignal(WTERMSIG(exit_code));
			warnx("*** [%s] %s%s", rule->name, signal_description, err_sfx);
			if ( fatal_error )
				exit(2);
			if ( !prefix_ignore )
				target_result = MAKE_RESULT_ERROR;
		}
		else
		{
			printf("recipe for target '%s' failed\n", rule->name);
			fflush(stdout);
			warnx("*** [%s] Abnormal termination%s", rule->name, err_sfx);
			if ( fatal_error )
				exit(2);
			if ( !prefix_ignore )
				target_result = MAKE_RESULT_ERROR;
		}

		if ( the_cleanup_signal != 0 )
			die_through_signal(the_cleanup_signal);

		free(raw_command);

		if ( target_result == MAKE_RESULT_ERROR )
			break;
	}

	return target->has_been_made = true,
	       target->result = target_result;
}

enum make_result make_target_name(struct makefile* makefile,
                                  struct make* make,
                                  const char* name)
{
	struct target* target = make_create_target(makefile, make, name);
	if ( !target )
	{
		warnx("*** No rule to make target '%s'.  Stop.", name);
		if ( !make->keep_going )
			exit(2);
		return MAKE_RESULT_ERROR;
	}
	return make_target(makefile, make, target);
}

enum make_result make_target_goal(struct makefile* makefile,
                                  struct make* make,
                                  const char* name)
{
	enum make_result result = make_target_name(makefile, make, name);

	if ( result == MAKE_RESULT_ERROR )
		warnx("Target `%s' not remade because of errors", name);

	if ( result == MAKE_RESULT_WAS_UP_TO_DATE && !make->question )
		printf("%s: Nothing to be done for `%s'.\n", progname, name);

	return result;
}
