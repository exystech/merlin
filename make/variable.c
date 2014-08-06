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
 * variable.c
 * Variables.
 */

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data.h"
#include "make.h"
#include "variable.h"
#include "util.h"

void variable_free(struct variable* next)
{
	while ( next )
	{
		struct variable* var = next;
		next = var->pushed;
		free(var->name);
		free(var->value);
		free(var);
	}
}

const char* variable_value_def(struct variable* variable,
                               const char* default_value)
{
	if ( !variable || !variable->value )
		return default_value;
	return variable->value;
}

const char* variable_value(struct variable* variable)
{
	return variable_value_def(variable, "");
}

void variable_assign(struct variable* variable,
                     const char* value,
                     enum variable_source source)
{
	assert(variable);

	if ( variable->source < source )
		return;

	free(variable->value);
	variable->value = NULL;

	if ( value )
	{
		variable->value = strdup(value);
		assert(variable->value);
	}
	variable->source = source;
}

// TODO: These expansions may throw quoting information away when quoting
//       support is added to word_split. It might even be desirable to quote the
//       output words in the same manner as the input words.

// TODO: Make implementations seems to have some wildcard support here that's
//       not in POSIX. Add that.
static void expand_replace(const char* word,
                           const char* search,
                           const char* replace,
                           FILE* fp)
{
	size_t location = 0;
	if ( search[0] )
	{
		while ( word[location] )
		{
			size_t matched = 0;
			for ( size_t i = 0;
			      search[i] &&
			      word[location + i] == search[i];
			      i++ )
				matched++;
			if ( search[matched] == '\0' )
			{
				fputs(replace, fp);
				location += matched;
			}
			else
			{
				fputc((unsigned char) word[location++], fp);
			}
		}
	}
	else
	{
		fputs(word, fp);
		fputs(replace, fp);
	}
}

// TODO: Does this fully match the BSD make semantics?
static void expand_extension(const char* word, FILE* fp)
{
	char* last_dot = strchr(word, '.');
	if ( last_dot )
		fputs(last_dot + 1, fp);
}

// TODO: Does this fully match the BSD make semantics?
static void expand_dirname(const char* word, FILE* fp)
{
	char* word_dup = strdup(word);
	assert(word_dup);
	char* result = dirname(word_dup);
	fputs(result, fp);
	free(word_dup);
}

// TODO: Does this fully match the BSD make semantics?
static void expand_lowercase(const char* word, FILE* fp)
{
	// TODO: Is locale aware lowercase desirable here?
	for ( size_t i = 0; word[i]; i++ )
		fputc(tolower((unsigned char) word[i]), fp);
}

// TODO: Does this fully match the BSD make semantics?
static void expand_uppercase(const char* word, FILE* fp)
{
	// TODO: Is locale aware uppercase desirable here?
	for ( size_t i = 0; word[i]; i++ )
		fputc(toupper((unsigned char) word[i]), fp);
}

// TODO: Does this fully match the BSD make semantics?
static void expand_not_extension(const char* word, FILE* fp)
{
	char* last_dot = strchr(word, '.');
	if ( !last_dot )
		return;
	while ( word != last_dot )
		fputc((unsigned char) *word++, fp);
}

// TODO: Does this fully match the BSD make semantics?
static void expand_basename(const char* word, FILE* fp)
{
	char* word_dup = strdup(word);
	assert(word_dup);
	char* result = basename(word_dup);
	fputs(result, fp);
	free(word_dup);
}

// TODO: The stack may overflow, such as when variables refer to themselves.
//       Handle this gracefully, probably with a high limit.
static void makefile_evaluate_value_fp(struct makefile* makefile,
                                       const char* value,
                                       FILE* fp)
{
	size_t offset = 0;
	while ( value[offset] )
	{
		if ( value[offset] == '$' )
		{
			offset++;

			if ( !value[offset] )
				break;

			// TODO: Paran and brace match verification.
			if ( value[offset] == '(' || value[offset] == '{' )
			{
				offset++;

				size_t content_length = 0;
				size_t depth = 1;
				for ( content_length = 0; value[offset + content_length]; content_length++ )
				{
					if ( value[offset + content_length] == '(' ||
					     value[offset + content_length] == '{' )
						depth++;
					if ( value[offset + content_length] == ')' ||
					     value[offset + content_length] == '}' )
						depth--;
					if ( depth == 0 )
						break;
				}

				if ( !value[offset + content_length] )
					break;

				char* raw_content = strndup(value + offset, content_length);
				assert(raw_content);

				// TODO: This is non-POSIX. Perhaps detect if a recursive
				//       macro expansion happens and then say a non-POSIX
				//       feature was used. But note this kind of expansion may
				//       leak into a strictly POSIX makefile from sys.mk or user
				//       input, in which case it is counterproductive to warn.
				// TODO: This expands before parsing, but it would be better to
				//       parse, then expand the components.
				char* content = makefile_evaluate_value(makefile, raw_content);
				assert(content);
				free(raw_content);

				size_t name_length = strcspn(content, ":");
				char* name = strndup(content, name_length);

				const char* raw_value =
					variable_value(makefile_find_variable(makefile, name));
				char* value = makefile_evaluate_value(makefile, raw_value);
				assert(value);

				// TODO: The colon may be escaped with a blackslash according to
				//       OpenBSD make.1?
				if ( content[name_length] == ':' )
				{
					char** words;
					size_t words_count;
					word_split(value, &words, &words_count);

					const char* modifiers = content + name_length + 1;
					size_t modifer_equal_position = strcspn(modifiers, "=:");
					if ( modifiers[modifer_equal_position] == '=' )
					{
						char* search = strndup(modifiers, modifer_equal_position);
						assert(search);
						char* replace = strdup(modifiers + modifer_equal_position + 1);
						assert(replace);

						for ( size_t i = 0; i < words_count; i++ )
						{
							// TODO: This may discard significant whitespace.
							if ( i != 0 )
								fputc(' ', fp);
							expand_replace(words[i], search, replace, fp);
						}

						free(replace);
						free(search);
					}
					else if ( !strcmp(modifiers, "E") ||
					          !strcmp(modifiers, "H") ||
					          !strcmp(modifiers, "L") ||
					          !strcmp(modifiers, "U") ||
					          // TODO: :Mpattern
					          // TODO: :Npattern
					          //!strcmp(modifiers, "Q") ||
					          //!strcmp(modifiers, "QL") ||
					          !strcmp(modifiers, "R") ||
					          // TODO: :S/old_string/new_string/[1g]
					          // TODO: :C/pattern/replacement/[1g]
					          !strcmp(modifiers, "T") )
					{
						for ( size_t i = 0; i < words_count; i++ )
						{
							// TODO: This may discard significant whitespace.
							if ( i != 0 )
								fputc(' ', fp);
							if ( !strcmp(modifiers, "E") )
								expand_extension(words[i], fp);
							else if ( !strcmp(modifiers, "H") )
								expand_dirname(words[i], fp);
							else if ( !strcmp(modifiers, "L") )
								expand_lowercase(words[i], fp);
							else if ( !strcmp(modifiers, "U") )
								expand_uppercase(words[i], fp);
							else if ( !strcmp(modifiers, "R") )
								expand_not_extension(words[i], fp);
							else if ( !strcmp(modifiers, "T") )
								expand_basename(words[i], fp);
						}
					}
					else
					{
						warnx("*** Ignoring unsupported variable expansion modifier '%s'",
						      modifiers);
						fputs(value, fp);
					}

					for ( size_t i = 0; i < words_count; i++ )
						free(words[i]);
					free(words);
				}
				else
				{
					fputs(value, fp);
				}

				free(value);
				free(name);
				free(content);

				offset += content_length + 1;
			}
			else if ( value[offset] != '$' )
			{
				char name[2] = { value[offset++], '\0' };
				const char* value = variable_value(makefile_find_variable(makefile, name));
				makefile_evaluate_value_fp(makefile, value, fp);
			}
			else
			{
				fputc((unsigned char) value[offset++], fp);
			}
		}
		else
		{
			fputc((unsigned char) value[offset++], fp);
		}
	}
}

char* makefile_evaluate_value(struct makefile* makefile, const char* value)
{
	char* result;
	size_t result_size;

	FILE* fp = open_memstream(&result, &result_size);
	assert(fp);

	makefile_evaluate_value_fp(makefile, value, fp);

	assert(!ferror(fp));
	int close_result = fclose(fp);
	assert(close_result == 0);

	return result;
}

char* make_escape_value(const char* value)
{
	char* result;
	size_t result_size;

	FILE* fp = open_memstream(&result, &result_size);
	assert(fp);

	for ( size_t i = 0; value[i]; i++ )
	{
		if ( value[i] == '$' )
			fputc('$', fp),
			fputc('$', fp);
		else
			fputc((unsigned char) value[i], fp);
	}

	assert(!ferror(fp));
	int close_result = fclose(fp);
	assert(close_result == 0);

	return result;
}

char* shell_singly_escape_value(const char* value)
{
	char* result;
	size_t result_size;

	FILE* fp = open_memstream(&result, &result_size);
	assert(fp);

	fputc('\'', fp);

	for ( size_t i = 0; value[i]; i++ )
	{
		if ( value[i] == '\'' )
			fputc('\'', fp),
			fputc('\\', fp),
			fputc('\'', fp),
			fputc('\'', fp);
		else
			fputc((unsigned char) value[i], fp);
	}

	fputc('\'', fp);

	assert(!ferror(fp));
	int close_result = fclose(fp);
	assert(close_result == 0);

	return result;
}
