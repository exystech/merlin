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
 * disambiguate.c
 * Disambiguates makefile rules.
 */

#include <sys/stat.h>

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "data.h"
#include "execute.h"
#include "disambiguate.h"
#include "make.h"
#include "rule.h"
#include "util.h"
#include "variable.h"

static void makefile_copy_prerequisiteness(struct makefile* makefile,
                                           const char* destination,
                                           const char* source,
                                           const char* target_name)
{
	if ( !makefile_is_prerequisite_of(makefile, destination, target_name) )
		return;
	// TODO: This is not needed when rule_add_prerequisite does it.
	if ( makefile_is_prerequisite_of(makefile, source, target_name) )
		return;
	struct rule* target = makefile_find_rule(makefile, target_name);
	if ( !target )
		return;
	bool success = rule_add_prerequisite(target, destination);
	assert(success);
}

static void makefile_redirect_rule(struct makefile* makefile,
                                   struct make_disambiguate* disambiguate,
                                   struct rule* rule,
                                   const char* new_name)
{
	for ( size_t i = 0; i < makefile->rules_used; i++ )
	{
		struct rule* i_rule = makefile->rules[i];
		for ( size_t n = 0; n < i_rule->prerequisites_used; n++ )
		{
			if ( strcmp(rule->name, i_rule->prerequisites[n]) )
				continue;
			// TODO: We might make a duplicate here, don't do that.
			free(i_rule->prerequisites[n]);
			i_rule->prerequisites[n] = strdup(new_name);
			assert(i_rule->prerequisites[n]);
		}
	}

	struct vpath_redirect* redirect =
		(struct vpath_redirect*) malloc(sizeof(struct vpath_redirect));
	assert(redirect);

	redirect->from = strdup(rule->name);
	assert(redirect->from);
	redirect->to = strdup(new_name);
	assert(redirect->to);

	if ( !array_add((void***) &disambiguate->vpath_redirects,
	                &disambiguate->vpath_redirects_used,
	                &disambiguate->vpath_redirects_length,
	                redirect) )
	{
		assert(false);
	}

	// TODO: The rule was renamed here. But this isn't valid because it breaks
	//       the rule search tree. Since the new_name is inferred anyway, this
	//       should work. The best thing might be to delete the old rule, though
	//       that isn't implemented yet. (And would break the append invariant
	//       in makefile_disambiguate.)
#if 0
	// TODO: We might make a duplicate rule here, don't do that.
	free(rule->name);
	rule->name = strdup(new_name);
	assert(rule->name);
#endif
}

static void make_instantiate_rule(struct makefile* makefile,
                                  struct rule* rule,
                                  struct rule* template_rule,
                                  const char* inferred_from)
{
	rule->needs_to_be_inferred = false;

	if ( inferred_from )
	{
		assert(!rule->inferred_from);
		rule->inferred_from = strdup(inferred_from);
		assert(rule->inferred_from);
	}
	else
	{
		rule->inferred_from = NULL;
	}

	assert(!rule->commands);
	assert(!rule->commands_used);

	size_t commands_size = sizeof(struct commands*) * template_rule->commands_used;
	rule->commands = (struct command**) malloc(commands_size);
	assert(rule->commands);
	rule->commands_used = template_rule->commands_used;
	rule->commands_length = template_rule->commands_used;
	for ( size_t i = 0; i < template_rule->commands_used; i++ )
	{
		rule->commands[i] = command_dup(template_rule->commands[i]);
		assert(rule->commands[i]);
	}

	if ( inferred_from && strcmp(rule->name, inferred_from) )
		rule_add_prerequisite(rule, rule->inferred_from);
	for ( size_t i = 0; i < template_rule->prerequisites_used; i++ )
		rule_add_prerequisite(rule, template_rule->prerequisites[i]);

	makefile_copy_prerequisiteness(makefile, rule->name,
	                               template_rule->name, ".PHONY");
	makefile_copy_prerequisiteness(makefile, rule->name,
	                               template_rule->name, ".PRECIOUS");
	makefile_copy_prerequisiteness(makefile, rule->name,
	                               template_rule->name, ".IGNORE");
	makefile_copy_prerequisiteness(makefile, rule->name,
	                               template_rule->name, ".SILENT");
}

// TODO: We have a slight - but not completely identical - duplication of the
//       vpath search semantics in this file.
static char* makefile_search_vpath(struct makefile* makefile, const char* name)
{
	struct rule* new_rule;
	if ( (new_rule = makefile_find_rule(makefile, name)) )
	{
		if ( !new_rule->needs_to_be_inferred )
		{
			char* result = strdup(name);
			assert(result);
			return result;
		}
	}

	if ( access(name, F_OK) == 0 )
	{
		char* result = strdup(name);
		assert(result);
		return result;
	}

	// We don't do the VPATH case for absolute paths.
	if ( name[0] == '/' )
		return NULL;

	// VPATH is not part of POSIX make.
	if ( !makefile_try_use_non_posix(makefile) )
	{
		// TODO: Perhaps show a warning if vpath is actually set to something.
		return NULL;
	}

	const char* vpath_raw = variable_value_def(makefile_find_variable(makefile, "VPATH"), NULL);
	if ( !vpath_raw )
		return NULL;

	// TODO: It should be sufficient to evaluate the VPATH only once. It might
	//       even have side effects.
	char* vpath = makefile_evaluate_value(makefile, vpath_raw);
	assert(vpath);

	// TODO: There are no escape characters here?
	char* vpath_input = vpath;
	char* saved_ptr;
	char* vpath_elem;
	while ( (vpath_elem = strtok_r(vpath_input, " \t:", &saved_ptr)) )
	{
		vpath_input = NULL;

		size_t vpath_elem_length = strlen(vpath_elem);
		char* new_name = NULL;
		if ( vpath_elem_length && vpath_elem[vpath_elem_length-1] == '/' )
			new_name = print_string("%s%s", vpath_elem, name);
		else
			new_name = print_string("%s/%s", vpath_elem, name);
		assert(new_name);

		// Copy the new rule to the old name if the new rule exists, so if stuff
		// gets rebuilt, it happens in the local directory rather than in the
		// VPATH directory. This should largely match the GNU make behavior.
		// TODO: This logic is not entirely polished. What if the rule we find
		//       has not yet been disambiguated and it needs to be that? We
		//       might want to do this even if it doesn't have any commands.
		if ( (new_rule = makefile_find_rule(makefile, new_name)) )
		{
			if ( new_rule->commands_used )
			{
				free(vpath);
				return new_name;
			}
		}

		if ( access(new_name, F_OK) < 0 )
		{
			free(new_name);
			continue;
		}

		free(vpath);

		return new_name;
	}

	free(vpath);

	return NULL;
}

static bool makefile_disambiguate_rule_suffixes(struct makefile* makefile,
                                                struct rule* rule)
{
	struct rule* suffixes = makefile_find_rule(makefile, ".SUFFIXES");
	if ( !suffixes )
		return false;

	struct rule* suffix_rule = NULL;
	char* inferred_from = NULL;
	char* stem = NULL;

	const char* rule_name = rule->name;
	size_t rule_name_length = strlen(rule_name);

	for ( size_t i = 0; i < suffixes->prerequisites_used; i++ )
	{
		if ( suffix_rule )
			break;

		const char* s1 = suffixes->prerequisites[i];
		size_t s1_length = strlen(s1);

		if ( rule_name_length < s1_length )
			continue;
		size_t stem_length = rule_name_length - s1_length;
		if ( strcmp(rule_name + stem_length, s1) != 0 )
			continue;

		for ( size_t n = 0; n < suffixes->prerequisites_used; n++ )
		{
			const char* s2 = suffixes->prerequisites[n];
			size_t s2_length = strlen(s2);

			char* suffix_rule_name = (char*) malloc(s1_length + s2_length + 1);
			assert(suffix_rule_name);
			stpcpy(stpcpy(suffix_rule_name, s2), s1);

			suffix_rule = makefile_find_rule(makefile, suffix_rule_name);
			if ( !suffix_rule || suffix_rule->needs_to_be_inferred )
			{
				free(suffix_rule_name);
				suffix_rule = NULL;
				continue;
			}

			char* search = (char*) malloc(stem_length + s2_length + 1);
			assert(search);
			memcpy(search, rule_name, stem_length);
			strcpy(search + stem_length, s2);

			inferred_from = makefile_search_vpath(makefile, search);
			free(search);

			if ( !inferred_from )
			{
				free(suffix_rule_name);
				suffix_rule = NULL;
				continue;
			}

			// TODO: What is the stem in the case the VPATH was used?
			stem = strndup(rule_name, stem_length);
			assert(stem);

			break;
		}
	}

	for ( size_t i = 0; i < suffixes->prerequisites_used; i++ )
	{
		if ( suffix_rule )
			break;

		const char* s1 = suffixes->prerequisites[i];
		size_t s1_length = strlen(s1);

		suffix_rule = makefile_find_rule(makefile, s1);
		if ( !suffix_rule || suffix_rule->needs_to_be_inferred )
		{
			suffix_rule = NULL;
			continue;
		}

		char* search = (char*) malloc(rule_name_length + s1_length + 1);
		assert(search);
		stpcpy(stpcpy(search, rule_name), s1);

		inferred_from = makefile_search_vpath(makefile, search);
		free(search);

		if ( !inferred_from )
		{
			suffix_rule = NULL;
			continue;
		}

		// TODO: What is the stem in the case the VPATH was used?
		// TODO: Is this the correct value here?
		stem = strdup(rule_name);
		assert(stem);

		break;
	}

	if ( !suffix_rule )
		return false;

	make_instantiate_rule(makefile, rule, suffix_rule, inferred_from);
	free(inferred_from);

	free(rule->stem);
	rule->stem = stem;

	return true;
}

static
bool makefile_disambiguate_rule_vpath(struct makefile* makefile,
                                      struct make_disambiguate* disambiguate,
                                      struct rule* rule)
{
	// We don't do the VPATH case for absolute paths.
	if ( rule->name[0] == '/' )
		return false;

	// VPATH is not part of POSIX make.
	if ( !makefile_try_use_non_posix(makefile) )
	{
		// TODO: Perhaps show a warning if vpath is actually set to something.
		return NULL;
	}

	const char* vpath_raw = variable_value_def(makefile_find_variable(makefile, "VPATH"), NULL);
	if ( !vpath_raw )
		return NULL;

	// We don't do the VPATH case if the file already exists normally, unless
	// the found file has a rule with commands.
	bool file_exists_normally = access(rule->name, F_OK) == 0;

	// TODO: It should be sufficient to evaluate the VPATH only once. It might
	//       even have side effects.
	char* vpath = makefile_evaluate_value(makefile, vpath_raw);
	assert(vpath);

	// TODO: There are no escape characters here?
	char* vpath_input = vpath;
	char* saved_ptr;
	char* vpath_elem;
	while ( (vpath_elem = strtok_r(vpath_input, " \t:", &saved_ptr)) )
	{
		vpath_input = NULL;

		size_t vpath_elem_length = strlen(vpath_elem);
		char* new_name = NULL;
		if ( vpath_elem_length && vpath_elem[vpath_elem_length-1] == '/' )
			new_name = print_string("%s%s", vpath_elem, rule->name);
		else
			new_name = print_string("%s/%s", vpath_elem, rule->name);
		assert(new_name);

		// Copy the new rule to the old name if the new rule exists, so if stuff
		// gets rebuilt, it happens in the local directory rather than in the
		// VPATH directory. This should largely match the GNU make behavior.
		// TODO: This logic is not entirely polished. What if the rule we find
		//       has not yet been disambiguated and it needs to be that? We
		//       might want to do this even if it doesn't have any commands.
		struct rule* new_rule;
		if ( (new_rule = makefile_find_rule(makefile, new_name)) )
		{
			if ( new_rule->commands_used )
			{
				make_instantiate_rule(makefile, rule, new_rule, new_rule->inferred_from);
				free(new_name);
				free(vpath);
				return true;
			}
		}

		if ( file_exists_normally || access(new_name, F_OK) < 0 )
		{
			free(new_name);
			continue;
		}

		makefile_redirect_rule(makefile, disambiguate, rule, new_name);
		makefile_infer_rule(makefile, new_name);

		free(new_name);
		free(vpath);

		// rule->needs_to_be_inferred is still set because we didn't make this
		// into a proper rule, but just a rule talking about an existing file.

		return true;
	}

	free(vpath);

	return false;
}

static bool makefile_disambiguate_rule_default(struct makefile* makefile,
                                               struct rule* rule)
{
	struct rule* default_rule = makefile_find_rule(makefile, ".DEFAULT");
	if ( !default_rule )
		return false;
	if ( default_rule->needs_to_be_inferred )
		return false;

	make_instantiate_rule(makefile, rule, default_rule, rule->name);

	return true;
}

void makefile_disambiguate_rule(struct makefile* makefile,
                                struct make_disambiguate* disambiguate,
                                struct rule* rule)
{
	if ( !strcmp(rule->name, ".DEFAULT") ||
	     !strcmp(rule->name, ".SUFFIXES") )
		return;

	for ( size_t i = 0; i < rule->prerequisites_used; i++ )
		makefile_infer_rule(makefile, rule->prerequisites[i]);

	if ( !rule->needs_to_be_inferred && 0 < rule->commands_used )
	{
		// This is a full and healthy rule with commands. No need to infer
		// anything, the job is already done.
	}
	else if ( makefile_is_prerequisite_of(makefile, rule->name, ".SUFFIXES") )
	{
		// This is a suffix rule. It doesn't make sense to infer anything about
		// this rule.
	}
	// The called function should use VPATH when searching.
	else if ( makefile_disambiguate_rule_suffixes(makefile, rule) )
	{
		// We used a suffix rule to finish this rule.
	}
	else if ( !rule->needs_to_be_inferred && rule->commands_used == 0 )
	{
		// Since this is an actual rule from the makefile (and not created from
		// a dependency list) we'll make this a rule with no commands rather
		// than using the default rule (if one exists).
	}
	else if ( makefile_disambiguate_rule_vpath(makefile, disambiguate, rule) )
	{
		// A matching file was found in the VPATH.
	}
	else if ( makefile_disambiguate_rule_default(makefile, rule) )
	{
		// We used the default rule to finish this rule.
	}
	else
	{
		// We failed to figure out how to infer this rule. The rule is still
		// marked as needing-to-be-inferred and the build will fail later on if
		// this rule becomes a target.
	}

	// Do it again as more prerequisites might have been added or changed.
	for ( size_t i = 0; i < rule->prerequisites_used; i++ )
		makefile_infer_rule(makefile, rule->prerequisites[i]);
}

void makefile_disambiguate(struct makefile* makefile,
                           struct make_disambiguate* disambiguate)
{
	// TODO: This isn't really robust against changes to the list of rules, but
	//       assumes generated rules gets appended to the list.
	for ( size_t i = 0; i < makefile->rules_used; i++ )
		makefile_disambiguate_rule(makefile, disambiguate, makefile->rules[i]);
}

const char* makefile_vpath_detour(struct make_disambiguate* disambiguate,
                                  const char* name)
{
	for ( size_t i = 0; i < disambiguate->vpath_redirects_used; i++ )
		if ( !strcmp(name, disambiguate->vpath_redirects[i]->from) )
			name = disambiguate->vpath_redirects[i]->to;
	return name;
}
