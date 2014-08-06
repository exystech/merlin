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
 * data.c
 * Data.
 */

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bstree.h"
#include "data.h"
#include "make.h"
#include "rule.h"
#include "util.h"
#include "variable.h"

// TODO: Move variable lookup to bstree.

static bool find_variable_index(struct makefile* makefile,
                                const char* name,
                                size_t* index_ptr)
{
	for ( size_t i = 0; i < makefile->variables_used; i++ )
		if ( !strcmp(name, makefile->variables[i]->name) )
			return *index_ptr = i, true;
	return false;
}

struct variable* makefile_find_variable(struct makefile* makefile,
                                        const char* name)
{
	size_t index;
	if ( !find_variable_index(makefile, name, &index) )
		return NULL;
	return makefile->variables[index];
}

struct variable* makefile_create_variable(struct makefile* makefile,
                                          const char* name)
{
	struct variable* variable = makefile_find_variable(makefile, name);
	if ( variable )
		return variable;
	variable = (struct variable*) calloc(1, sizeof(struct variable));
	if ( !variable )
		return NULL;
	variable->name = strdup(name);
	if ( !variable->name )
		return free(variable),
		       (struct variable*) NULL;
	variable->value = NULL;
	variable->source = VARIABLE_SOURCE_MAKEFILE;
	if ( !array_add((void***) &makefile->variables,
	                &makefile->variables_used,
	                &makefile->variables_length,
	                variable) )
		return free(variable->name),
		       free(variable),
		       (struct variable*) NULL;
	return variable;
}

void makefile_unset_variable(struct makefile* makefile, const char* name)
{
	size_t index;
	if ( !find_variable_index(makefile, name, &index) )
		return;
	struct variable* var = makefile->variables[index];
	// TODO: What is the desirable behavior when unsetting a variable that has
	//       been pushed?
	if ( index + 1 != makefile->variables_used )
	{
		size_t last = makefile->variables_used - 1;
		makefile->variables[index] = makefile->variables[last];
	}
	makefile->variables_used--;
	variable_free(var);
}

void makefile_push_variable(struct makefile* makefile,
                            const char* name,
                            const char* new_value)
{
	// TODO: Don't allow pushing internal names.
	struct variable* var;
	size_t index;
	if ( find_variable_index(makefile, name, &index) )
	{
		var = (struct variable*) calloc(1, sizeof(struct variable));
		var->pushed = makefile->variables[index];
		makefile->variables[index] = var;
	}
	else
	{
		var = makefile_create_variable(makefile, name);
		assert(var);
	}
	variable_assign(var, new_value, VARIABLE_SOURCE_MAKEFILE);
}

void makefile_pop_variable(struct makefile* makefile, const char* name)
{
	// TODO: Don't allow poping internal names.
	size_t index;
	if ( !find_variable_index(makefile, name, &index) )
		return;
	struct variable* var = makefile->variables[index];
	if ( var->pushed )
	{
		makefile->variables[index] = var->pushed;
		var->pushed = NULL;
		variable_free(var);
	}
	else
		makefile_unset_variable(makefile, name);
}

#include <stdio.h>

static int compare_rule(const void* a_ptr, const void* b_ptr)
{
	const struct rule* a = (const struct rule*) a_ptr;
	const struct rule* b = (const struct rule*) b_ptr;
	int result = strcmp(a->name, b->name);
	return result;
}

static int search_rule(const void* name_ptr, const void* rule_ptr)
{
	const char* name = (const char*) name_ptr;
	const struct rule* rule = (const struct rule*) rule_ptr;
	return strcmp(name, rule->name);
}

struct rule* makefile_find_rule(struct makefile* makefile, const char* name)
{
	return bstree_search(makefile->rules_tree, name, search_rule);
}

struct rule* makefile_create_rule(struct makefile* makefile,
                                  const char* name,
                                  const char* inferred_from,
                                  const char* stem)
{
	struct rule* rule = makefile_find_rule(makefile, name);
	if ( rule )
		return rule;
	rule = (struct rule*) calloc(1, sizeof(struct rule));
	if ( !rule )
		return NULL;
	if ( !(rule->name = strdup(name)) )
		return free(rule),
		       (struct rule*) NULL;
	if ( inferred_from && !(rule->inferred_from = strdup(inferred_from)) )
		return free(rule->name),
		       free(rule),
		       (struct rule*) NULL;
	if ( stem && !(rule->stem = strdup(stem)) )
		return free(rule->inferred_from),
		       free(rule->name),
		       free(rule),
		       (struct rule*) NULL;
	if ( !array_add((void***) &makefile->rules,
	                &makefile->rules_used,
	                &makefile->rules_length,
	                rule) )
		return free(rule->stem),
		       free(rule->inferred_from),
		       free(rule->name),
		       free(rule),
		       (struct rule*) NULL;
	if ( !bstree_insert(&makefile->rules_tree, rule, compare_rule) )
		return makefile->rules_used--,
		       free(rule->stem),
		       free(rule->inferred_from),
		       free(rule->name),
		       free(rule),
		       (struct rule*) NULL;
	return rule;
}

void makefile_infer_rule(struct makefile* makefile, const char* name)
{
	if ( makefile_find_rule(makefile, name) )
		return;
	struct rule* rule = makefile_create_rule(makefile, name, NULL, NULL);
	assert(rule);
	rule->needs_to_be_inferred = true;
}

bool makefile_is_prerequisite_of_ex(struct makefile* makefile,
                                    const char* prerequisite_name,
                                    const char* target_name,
                                    bool no_prerequisites_means_all)
{
	struct rule* rule = makefile_find_rule(makefile, target_name);
	if ( !rule )
		return false;
	if ( no_prerequisites_means_all && !rule->prerequisites_used )
		return true;
	for ( size_t i = 0; i < rule->prerequisites_used; i++ )
		if ( !strcmp(rule->prerequisites[i], prerequisite_name) )
			return true;
	return false;
}

bool makefile_is_prerequisite_of(struct makefile* makefile,
                                 const char* prerequisite_name,
                                 const char* target_name)
{
	return makefile_is_prerequisite_of_ex(
		makefile, prerequisite_name, target_name, false);
}

// TODO: Note how where the first-non-posix use happened so we can put it in an
//       error message later on.
void makefile_use_non_posix(struct makefile* makefile)
{
	// TODO: Perhaps this error message should only be shown once. We also need
	//       to include a source location for user convenience.
	if ( makefile->in_posix_mode )
		warnx("*** Using non-posix extension in posix mode (ignored)");

	if ( makefile->used_non_posix )
		return;

	makefile->used_non_posix = true;
}

bool makefile_can_use_non_posix(struct makefile* makefile)
{
	if ( makefile->in_posix_mode )
		return false;
	return true;
}

bool makefile_try_use_non_posix(struct makefile* makefile)
{
	if ( !makefile_can_use_non_posix(makefile) )
		return false;
	makefile_use_non_posix(makefile);
	return true;
}

void makefile_enter_posix_mode(struct makefile* makefile)
{
	if ( makefile->in_posix_mode )
		return;

	if ( makefile->used_non_posix )
		warnx("*** Late .POSIX rule, extensions have already been used (ignored)");

	makefile->in_posix_mode = true;
}
