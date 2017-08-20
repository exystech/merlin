/*
 * Copyright (c) 2017, 2023 Jonas 'Sortie' Termansen.
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
 * autoconf.c
 * Parser for autoinstall.conf(5) and autoupgrade.conf(5).
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "autoconf.h"
#include "execute.h"

static char** keyvalues = NULL;
static size_t keyvalues_used = 0;
static size_t keyvalues_length = 0;

bool has_autoconf = false;

bool autoconf_has(const char* name)
{
	if ( !name )
		return false;
	size_t len = strlen(name);
	for ( size_t i = 0; i < keyvalues_used; i++ )
	{
		if ( strncmp(keyvalues[i], name, len) )
			continue;
		if ( keyvalues[i][len] == '=' )
			return true;
		if ( keyvalues[i][len] == '!' && keyvalues[i][len + 1] == '=' )
			return true;
	 }
	return false;
}

const char* autoconf_get(const char* name)
{
	if ( !name )
		return NULL;
	size_t len = strlen(name);
	for ( size_t i = 0; i < keyvalues_used; i++ )
		if ( !strncmp(keyvalues[i], name, len) && keyvalues[i][len] == '=' )
			return keyvalues[i] + len + 1;
	return NULL;
}

char* autoconf_eval(const char* name)
{
	if ( !name )
		return NULL;
	char* shname;
	if ( asprintf(&shname, "%s!", name) < 0 )
		err(1, "malloc");
	const char* script;
	char* output = NULL;
	if ( (script = autoconf_get(shname)) )
	{
		char** out_ptr = autoconf_get(name) ? NULL : &output;
		execute((const char*[]) { "sh", "-c", script, NULL }, "efo", out_ptr);
		if ( out_ptr )
		{
			size_t len = strlen(output);
			if ( len && output[len - 1] == '\n' )
				output[len - 1] = '\0';
		}
	}
	free(shname);
	if ( autoconf_get(name) )
	{
		free(output);
		if ( !(output = strdup(autoconf_get(name))) )
			err(1, "malloc");
	}
	return output;
}

bool autoconf_set(const char* name, const char* value)
{
	char* keyvalue;
	if ( asprintf(&keyvalue, "%s=%s", name, value) < 0 )
		return false;
	size_t len = strlen(name);
	for ( size_t i = 0; i < keyvalues_used; i++ )
	{
		if ( !strncmp(keyvalues[i], name, len) && keyvalues[i][len] == '=' )
		{
			free(keyvalues[i]);
			keyvalues[i] = keyvalue;
			return true;
		}
	}
	if ( keyvalues_used == keyvalues_length )
	{
		size_t new_length = keyvalues_length ? 2 * keyvalues_length : 4;
		char** new_keyvalues =
			reallocarray(keyvalues, new_length, sizeof(char*));
		if ( !new_keyvalues )
			return free(keyvalue), false;
		keyvalues = new_keyvalues;
		keyvalues_length = new_length;
	}
	keyvalues[keyvalues_used++] = keyvalue;
	return true;
}

void autoconf_load(const char* path)
{
	FILE* fp = fopen(path, "r");
	if ( !fp )
	{
		if ( errno != ENOENT )
			warn("%s", path);
		return;
	}
	char* line = NULL;
	size_t line_size = 0;
	ssize_t line_length;
	off_t line_number = 0;
	while ( 0 < (line_length = getline(&line, &line_size, fp)) )
	{
		line_number++;
		if ( line[line_length - 1] == '\n' )
			line[--line_length] = '\0';
		line_length = 0;
		while ( line[line_length] && line[line_length] != '#' )
			line_length++;
		line[line_length] = '\0';
		char* name = line;
		if ( !*name || *name == '=' )
			continue;
		size_t name_length = 1;
		while ( name[name_length] &&
		        name[name_length] != '=' &&
		        name[name_length] != '+' )
			name_length++;
		if ( name[name_length + 0] == '+' &&
		     name[name_length + 1] == '+' &&
		     name[name_length + 2] == '=' )
		{
			name[name_length + 0] = '\0';
			char* value = name + name_length + 3;
			const char* existing = autoconf_get(name);
			if ( existing )
			{
				char* full;
				if ( asprintf(&full, "%s\n%s", existing, value) < 0 )
					err(2, "%s: malloc", path);
				if ( !autoconf_set(name, full) )
					err(2, "%s: malloc", path);
				free(full);
			}
			else if ( !autoconf_set(name, value) )
				err(2, "%s: malloc", path);
		}
		else if ( name[name_length + 0] == '+' &&
		          name[name_length + 1] == '=' )
		{
			name[name_length + 0] = '\0';
			char* value = name + name_length + 2;
			const char* existing = autoconf_get(name);
			if ( existing )
			{
				char* full;
				if ( asprintf(&full, "%s %s", existing, value) < 0 )
					err(2, "%s: malloc", path);
				if ( !autoconf_set(name, full) )
					err(2, "%s: malloc", path);
				free(full);
			}
			else if ( !autoconf_set(name, value) )
				err(2, "%s: malloc", path);
		}
		else if ( name[name_length + 0] == '=' )
		{
			name[name_length + 0] = '\0';
			char* value = name + name_length + 1;
			if ( !autoconf_set(name, value) )
				err(2, "%s: malloc", path);
		}
		else
			errx(2, "%s:%ji: Invalid line: %s",
			     path, (intmax_t) line_number, line);
		char* value = name + name_length;
		while ( *value && isblank((unsigned char) *value) )
			value++;
		if ( *value != '=' )
			continue;
		value++;
		while ( *value && isblank((unsigned char) *value) )
			value++;
		name[name_length] = '\0';
	}
	if ( ferror(fp) )
		err(2, "%s", path);
	free(line);
	fclose(fp);
	has_autoconf = true;
}
