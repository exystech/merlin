/*
 * Copyright (c) 2014, 2015, 2018, 2021 Jonas 'Sortie' Termansen.
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
 * sort.c
 * Sort, merge, or sequence check text files.
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODIFIER_BLANK (1 << 0)
#define MODIFIER_DICTIONARY (1 << 1)
#define MODIFIER_IGNORE_CASE (1 << 2)
#define MODIFIER_GENERAL_NUMERIC (1 << 3)
#define MODIFIER_HUMAN (1 << 4)
#define MODIFIER_IGNORE_NONPRINTING (1 << 5)
#define MODIFIER_MONTH (1 << 6)
#define MODIFIER_NUMERIC (1 << 7)
#define MODIFIER_REVERSE (1 << 8)
#define MODIFIER_RANDOM (1 << 9)
#define MODIFIER_VERSION (1 << 10)
#define MODIFIER_UNIQUE (1 << 11)

static char separator;
static bool separator_is_blank = true;
static int modifiers;

struct key
{
	size_t start_field;
	size_t first_character;
	size_t end_field;
	size_t last_character;
	int start_modifiers;
	int end_modifiers;
};

static struct key* keys;
static size_t keys_count;

static size_t field_length(const char* string)
{
	size_t length = 0;
	if ( separator_is_blank )
	{
		while ( string[length] && isblank((unsigned char) string[length]) )
			length++;
	}
	while ( string[length] &&
	       (separator_is_blank ?
	        !isblank((unsigned char) string[length]) :
	        string[length] != separator) )
		length++;
	return length;
}

static size_t separator_length(const char* string)
{
	if ( separator_is_blank )
		return 0;
	return string[0] == separator ? 1 : 0;
}

static int strdoublecmp(const char* a, const char* b)
{
	double ad = strtod(a, NULL);
	double bd = strtod(b, NULL);
	return ad < bd ? -1 : ad > bd ? 1 : 0;
}

static int month_index(const char* string)
{
	const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	for ( int i = 0; i < 12; i++ )
		if ( !strncasecmp(string, months[i], 3) )
			return i + 1;
	return 0;
}

static int strmonthcmp(const char* a, const char* b)
{
	int am = month_index(a);
	int bm = month_index(b);
	return am < bm ? -1 : am > bm ? 1 : 0;
}

static int strnumcmp(const char* a, const char* b, bool human)
{
	while ( isspace((unsigned char) *a) )
		a++;
	while ( isspace((unsigned char) *b) )
		b++;
	bool a_negative = *a == '-';
	if ( a_negative )
		a++;
	bool b_negative = *b == '-';
	if ( b_negative )
		b++;
	while ( *a == '0' )
		a++;
	while ( *b == '0' )
		b++;
	size_t a_length = 0;
	size_t b_length = 0;
	while ( isdigit((unsigned char) a[a_length]) )
		a_length++;
	while ( isdigit((unsigned char) b[b_length]) )
		b_length++;
	size_t a_fractionals = 0;
	bool a_nonzero = false;
	if ( a[a_length] == '.' )
	{
		a_fractionals = 1;
		while ( isdigit((unsigned char) a[a_length + a_fractionals]) )
		{
			if ( a[a_length + a_fractionals] != '0' )
				a_nonzero = true;
			a_fractionals++;
		}
	}
	size_t b_fractionals = 0;
	bool b_nonzero = false;
	if ( b[b_length] == '.' )
	{
		b_fractionals = 1;
		while ( isdigit((unsigned char) b[b_length + b_fractionals]) )
		{
			if ( b[b_length + b_fractionals] != '0' )
				b_nonzero = true;
			b_fractionals++;
		}
	}
	// All zero representations are equal regardless of sign.
	if ( !human && !a_length && !a_nonzero && !b_length && !b_nonzero )
		return 0;
	if ( a_negative && !b_negative )
		return -1;
	if ( !a_negative && b_negative )
		return 1;
	int bigger = a_negative ? -1 : 1;
	if ( human )
	{
		if ( !(a_length || a_fractionals) && (b_length || b_fractionals) )
			return -bigger;
		if ( (a_length || a_fractionals) && !(b_length || b_fractionals) )
			return bigger;
		const char* magnitudes = "YZEPTGMK";
		unsigned char a_unit = (unsigned char) a[a_length + a_fractionals];
		unsigned char b_unit = (unsigned char) b[b_length + b_fractionals];
		if ( a_unit == 'k' )
			a_unit = 'K';
		if ( b_unit == 'k' )
			b_unit = 'K';
		const char* a_magnitude = strchrnul(magnitudes, a_unit);
		const char* b_magnitude = strchrnul(magnitudes, b_unit);
		if ( a_magnitude > b_magnitude )
			return -bigger;
		if ( a_magnitude < b_magnitude )
			return bigger;
	}
	if ( a_length < b_length )
		return -bigger;
	if ( a_length > b_length )
		return bigger;
	for ( size_t i = 0; i < a_length; i++ )
	{
		if ( a[i] < b[i] )
			return -bigger;
		if ( a[i] > b[i] )
			return bigger;
	}
	for ( size_t i = 1; i < a_fractionals || i < b_fractionals; i++ )
	{
		char a_digit = i < a_fractionals ? a[a_length + i] : '0';
		char b_digit = i < b_fractionals ? b[b_length + i] : '0';
		if ( a_digit < b_digit )
			return -bigger;
		if ( a_digit > b_digit )
			return bigger;
	}
	return 0;
}

static int is_dictionary(int c)
{
	return isblank(c) ||
	       ('a' <= c && c <= 'z') ||
	       ('A' <= c && c <= 'Z') ||
	       ('0' <= c && c <= '9');
}

static int string_compare(const char* a,
                          const char* b,
                          bool dictionary,
                          bool uppercase,
                          bool printable)
{
	size_t ai = 0;
	size_t bi = 0;
	while ( true )
	{
		unsigned char ac = (unsigned char) a[ai];
		unsigned char bc = (unsigned char) b[bi];
		if ( !ac && !bc )
			break;
		if ( uppercase )
		{
			ac = toupper(ac);
			bc = toupper(bc);
		}
		if ( ac && ((dictionary && !is_dictionary(ac)) ||
		            (printable && !isprint(ac))) )
		{
			ai++;
			continue;
		}
		if ( bc && ((dictionary && !is_dictionary(bc)) ||
		            (printable && !isprint(bc))) )
		{
			bi++;
			continue;
		}
		if ( ac < bc )
			return -1;
		if ( ac > bc )
			return 1;
		if ( ac )
			ai++;
		if ( bc )
			bi++;
	}
	return 0;
}

static int relate_field(char* a, char* b, int field_modifiers)
{
	int rel = 0;
	if ( field_modifiers & MODIFIER_HUMAN )
		rel = strnumcmp(a, b, true);
	else if ( field_modifiers & MODIFIER_GENERAL_NUMERIC )
		rel = strdoublecmp(a, b);
	else if ( field_modifiers & MODIFIER_MONTH )
		rel = strmonthcmp(a, b);
	else if ( field_modifiers & MODIFIER_NUMERIC )
		rel = strnumcmp(a, b, false);
	else if ( field_modifiers & MODIFIER_VERSION )
		rel = strverscmp(a, b);
	else if ( field_modifiers & (MODIFIER_DICTIONARY |
	                             MODIFIER_IGNORE_CASE |
	                             MODIFIER_IGNORE_NONPRINTING) )
		rel = string_compare(a, b, field_modifiers & MODIFIER_DICTIONARY,
		                     field_modifiers & MODIFIER_IGNORE_CASE,
		                     field_modifiers & MODIFIER_IGNORE_NONPRINTING);
	else
		rel = strcoll(a, b);
	if ( field_modifiers & MODIFIER_REVERSE )
		rel = rel < 0 ? 1 : 0 < rel ? -1 : 0;
	return rel;
}

static int relate(char* a, char* b, int modifiers)
{
	size_t a_len = strlen(a);
	size_t b_len = strlen(b);
	bool was_trivial = true;
	for ( size_t key_index = 0; key_index < keys_count; key_index++ )
	{
		const struct key* key = &keys[key_index];
		size_t a_start = a_len;
		size_t b_start = b_len;
		size_t a_end = a_len;
		size_t b_end = b_len;
		int start_modifiers = key->start_modifiers | key->end_modifiers ?
		                      key->start_modifiers : modifiers;
		int end_modifiers = key->start_modifiers | key->end_modifiers ?
		                    key->end_modifiers : modifiers;
		for ( size_t field = 0, a_offset = 0, b_offset = 0;
		      (field <= key->start_field || field <= key->end_field) &&
		      (a_offset < a_len || b_offset < b_len);
		      field++ )
		{
			size_t a_field = a_offset;
			size_t b_field = b_offset;
			size_t a_field_length = field_length(a + a_field);
			size_t b_field_length = field_length(b + b_field);
			a_offset += a_field_length;
			b_offset += b_field_length;
			a_offset += separator_length(a + a_offset);
			b_offset += separator_length(b + b_offset);
			if ( field == key->start_field )
			{
				size_t a_start_field = a_field;
				size_t a_start_field_length = a_field_length;
				size_t b_start_field = b_field;
				size_t b_start_field_length = b_field_length;
				if ( start_modifiers & MODIFIER_BLANK )
				{
					while ( a_start_field_length &&
					        isblank((unsigned char) a[a_start_field]) )
					{
						a_start_field++;
						a_start_field_length--;
					}
					while ( b_start_field_length &&
					        isblank((unsigned char) b[b_start_field]) )
					{
						b_start_field++;
						b_start_field_length--;
					}
				}
				size_t a_left = a_len - a_start_field;
				size_t b_left = b_len - b_start_field;
				size_t a_first = key->first_character <= a_left ?
				                 key->first_character : a_left;
				size_t b_first = key->first_character <= b_left ?
				                 key->first_character : b_left;
				a_start = a_start_field + a_first;
				b_start = b_start_field + b_first;
			}
			if ( field == key->end_field )
			{
				size_t a_end_field = a_field;
				size_t a_end_field_length = a_field_length;
				size_t b_end_field = b_field;
				size_t b_end_field_length = b_field_length;
				if ( end_modifiers & MODIFIER_BLANK )
				{
					while ( a_end_field_length &&
					        isblank((unsigned char) a[a_end_field]) )
					{
						a_end_field++;
						a_end_field_length--;
					}
					while ( b_end_field_length &&
					        isblank((unsigned char) b[b_end_field]) )
					{
						b_end_field++;
						b_end_field_length--;
					}
				}
				size_t a_last = key->last_character <= a_end_field_length ?
				                key->last_character : a_end_field_length;
				size_t b_last = key->last_character <= b_end_field_length ?
				                key->last_character : b_end_field_length;
				a_end = a_end_field + a_last + 1;
				b_end = b_end_field + b_last + 1;
				if ( a_len < a_end )
					a_end = a_len;
				if ( b_len < b_end )
					b_end = b_len;
			}
		}
		if ( a_end < a_start )
			a_start = a_end;
		if ( b_end < b_start )
			b_start = b_end;
		char a_separator = a[a_end];
		char b_separator = b[b_end];
		a[a_end] = '\0';
		b[b_end] = '\0';
		int key_modifiers = start_modifiers | end_modifiers;
		if ( a_start != 0 || b_start != 0 || a_end != a_len || b_end != b_len ||
		     (key_modifiers & ~MODIFIER_RANDOM) != 0 )
			was_trivial = false;
		int rel = relate_field(a + a_start, b + b_start, key_modifiers);
		a[a_end] = a_separator;
		b[b_end] = b_separator;
		if ( rel != 0 )
			return rel;
	}
	if ( was_trivial || (modifiers & MODIFIER_UNIQUE) )
		return 0;
	return strcoll(a, b);
}

static int compare(char* a, char* b)
{
	int rel = relate(a, b, modifiers & ~MODIFIER_REVERSE);
	if ( modifiers & MODIFIER_REVERSE )
		rel = rel < 0 ? 1 : 0 < rel ? -1 : 0;
	return rel;
}

static int indirect_compare(const void* a_ptr, const void* b_ptr)
{
	char* a = *(char* const*) a_ptr;
	char* b = *(char* const*) b_ptr;
	return compare(a, b);
}

static size_t pick_uniform(size_t upper)
{
	if ( upper < 2 )
		return 0;
	size_t minimum = -upper % upper;
	size_t selection;
	do arc4random_buf(&selection, sizeof(selection));
	while ( selection < minimum );
	return selection % upper;
}

struct input_stream
{
	const char* const* files;
	size_t files_current;
	size_t files_length;
	FILE* current_file;
	const char* last_file_path;
	uintmax_t last_line_number;
};

static char* read_line(FILE* fp, const char* fpname, int delim)
{
	char* line = NULL;
	size_t line_size = 0;
	ssize_t amount = getdelim(&line, &line_size, delim, fp);
	if ( amount < 0 )
	{
		free(line);
		if ( ferror(fp) )
			err(2, "read: %s", fpname);
		return NULL;
	}
	if ( (unsigned char) line[amount-1] == (unsigned char) delim )
		line[amount-1] = '\0';
	return line;
}

static char* read_input_stream_line(struct input_stream* is, int delim)
{
	if ( !is->files_length )
	{
		char* result = read_line(stdin, "<stdin>", delim);
		is->last_file_path = "-";
		if ( result )
			is->last_line_number++;
		return result;
	}
	while ( is->files_current < is->files_length )
	{
		const char* path = is->files[is->files_current];
		if ( !is->current_file )
		{
			is->last_line_number = 0;
			if ( !strcmp(path, "-") )
				is->current_file = stdin;
			else if ( !(is->current_file = fopen(path, "r")) )
				err(2, "%s", path);
		}
		char* result = read_line(is->current_file, path, delim);
		if ( !result )
		{
			if ( is->current_file != stdin )
				fclose(is->current_file);
			is->current_file = NULL;
			is->files_current++;
			continue;
		}
		is->last_file_path = path;
		is->last_line_number++;
		return result;
	}
	return NULL;
}

static char** read_input_stream_lines(size_t* result_num_lines,
                                      struct input_stream* is,
                                      int delim)
{
	char** lines = NULL;
	size_t lines_used = 0;
	size_t lines_length = 0;

	char* line;
	while ( (line = read_input_stream_line(is, delim)) )
	{
		if ( lines_used == lines_length )
		{
			size_t old_lines_length = lines_length ? lines_length : 64;
			char** new_lines = (char**) reallocarray(lines, old_lines_length,
			                                         2 * sizeof(char*));
			if ( !new_lines )
				err(2, "malloc");
			lines = new_lines;
			lines_length = 2 * old_lines_length;
		}
		lines[lines_used++] = line;
	}

	return *result_num_lines = lines_used, lines;
}

static size_t parse_modifiers(const char* keystring, int* modifiers)
{
	size_t offset = 0;
	while ( keystring[offset] )
	{
		switch ( keystring[offset] )
		{
		case 'b': *modifiers |= MODIFIER_BLANK; break;
		case 'd': *modifiers |= MODIFIER_DICTIONARY; break;
		case 'f': *modifiers |= MODIFIER_IGNORE_CASE; break;
		case 'g': *modifiers |= MODIFIER_GENERAL_NUMERIC; break;
		case 'h': *modifiers |= MODIFIER_HUMAN; break;
		case 'i': *modifiers |= MODIFIER_IGNORE_NONPRINTING; break;
		case 'M': *modifiers |= MODIFIER_MONTH; break;
		case 'n': *modifiers |= MODIFIER_NUMERIC; break;
		case 'R': *modifiers |= MODIFIER_RANDOM; break;
		case 'r': *modifiers |= MODIFIER_REVERSE; break;
		case 'V': *modifiers |= MODIFIER_VERSION; break;
		default: return offset;
		}
		offset++;
	}
	return offset;
}

static void check_modifiers(int modifiers)
{
	modifiers &= ~(MODIFIER_BLANK | MODIFIER_UNIQUE | MODIFIER_REVERSE);
	bool incompatible = false;
	if ( modifiers & (MODIFIER_DICTIONARY |
	                  MODIFIER_IGNORE_CASE |
	                  MODIFIER_IGNORE_NONPRINTING) )
	{
		if ( modifiers & ~(MODIFIER_DICTIONARY |
		                   MODIFIER_IGNORE_CASE |
		                   MODIFIER_IGNORE_NONPRINTING) )
			incompatible = true;
	}
	else if ( modifiers & MODIFIER_NUMERIC )
	{
		if ( modifiers & ~(MODIFIER_NUMERIC | MODIFIER_IGNORE_CASE) )
			incompatible = true;
	}
	else if ( modifiers && 1 << (ffs(modifiers) - 1) != modifiers )
		incompatible = true;
	if ( incompatible )
	{
		char options[sizeof(int) * CHAR_BIT];
		size_t offset = 0;
		if ( modifiers & MODIFIER_DICTIONARY ) options[offset++] = 'd';
		if ( modifiers & MODIFIER_IGNORE_CASE ) options[offset++] = 'f';
		if ( modifiers & MODIFIER_GENERAL_NUMERIC ) options[offset++] = 'g';
		if ( modifiers & MODIFIER_HUMAN ) options[offset++] = 'h';
		if ( modifiers & MODIFIER_IGNORE_NONPRINTING ) options[offset++] = 'i';
		if ( modifiers & MODIFIER_MONTH ) options[offset++] = 'M';
		if ( modifiers & MODIFIER_NUMERIC ) options[offset++] = 'n';
		if ( modifiers & MODIFIER_RANDOM ) options[offset++] = 'R';
		if ( modifiers & MODIFIER_VERSION ) options[offset++] = 'V';
		options[offset] = '\0';
		errx(2, "options '-%s' are incompatible", options);
	}
}

static void parse_key(const char* keystring)
{

	const char* str = keystring;
	if ( !(keys = reallocarray(keys, sizeof(struct key), ++keys_count)) )
		err(2, "malloc");
	struct key* key = &keys[keys_count - 1];
	memset(key, 0, sizeof(*key));
	for ( size_t i = 0; str[i]; i++ )
	{
		if ( isblank((unsigned char) str[i]) ||
		     str[i] == '-' || str[i] == '+' )
			errx(2, "invalid key: %s", keystring);
	}
	char* end;
	uintmax_t value;
	errno = 0;
	value = strtoumax(str, &end, 10);
	if ( end == str || errno || value != (size_t) value )
		errx(2, "invalid key starting field: %s", keystring);
	str += end - str;
	if ( value == 0 )
		errx(2, "invalid key with zero field: %s", keystring);
	key->start_field = value - 1;
	if ( *str == '.' )
	{
		str++;
		errno = 0;
		value = strtoumax(str, &end, 10);
		if ( end == str || errno || value != (size_t) value )
			errx(2, "invalid key starting character offset: %s", keystring);
		str += end - str;
		if ( value == 0 )
			errx(2, "invalid key with zero character offset: %s", keystring);
		key->first_character = value - 1;
	}
	else
		key->first_character = 0;
	str += parse_modifiers(str, &key->start_modifiers);
	if ( !*str )
	{
		key->end_field = SIZE_MAX - 1;
		key->last_character = SIZE_MAX;
		key->end_modifiers = 0;
		return;
	}
	if ( *str != ',' )
		errx(2, "invalid key modifier '%c': %s", *str, keystring);
	str++;
	errno = 0;
	value = strtoumax(str, &end, 10);
	if ( end == str || errno || value != (size_t) value )
		errx(2, "invalid key ending field: %s", keystring);
	key->end_field = value - 1;
	if ( value == 0 )
		errx(2, "invalid key with zero field: %s", keystring);
	str += end - str;
	if ( *str == '.' )
	{
		str++;
		errno = 0;
		value = strtoumax(str, &end, 10);
		if ( end == str || errno || value != (size_t) value )
			errx(2, "invalid key ending character offset: %s", keystring);
		str += end - str;
		if ( value == 0 )
			key->last_character = SIZE_MAX;
		else
			key->last_character = value - 1;
	}
	else
		key->last_character = SIZE_MAX;
	str += parse_modifiers(str, &key->end_modifiers);
	if ( *str )
		errx(2, "invalid key modifier '%c': %s", *str, keystring);
	check_modifiers(key->start_modifiers | key->end_modifiers);
}

static void parse_separator(const char* parameter)
{
	if ( strlen(parameter) != 1 )
		errx(2, "invalid separator: %s", parameter);
	separator = parameter[0];
	separator_is_blank = false;
}

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

int main(int argc, char* argv[])
{
	setlocale(LC_ALL, "");

	bool check = false;
	bool check_quiet = false;
	bool merge = false;
	const char* output = NULL;
	const char* parameter = NULL;
	bool unique = false;
	bool zero_terminated = false;

	for ( int i = 1; i < argc; i++ )
	{
		const char* arg = argv[i];
		if ( arg[0] != '-' || !arg[1] )
			continue;
		argv[i] = NULL;
		if ( !strcmp(arg, "--") )
			break;
		if ( arg[1] != '-' )
		{
			char c;
			while ( (c = *++arg) ) switch ( c )
			{
			case 'b': modifiers |= MODIFIER_BLANK; break;
			case 'C': check = true, check_quiet = true; break;
			case 'c': check = true, check_quiet = false; break;
			case 'd': modifiers |= MODIFIER_DICTIONARY; break;
			case 'f': modifiers |= MODIFIER_IGNORE_CASE; break;
			case 'g': modifiers |= MODIFIER_GENERAL_NUMERIC; break;
			case 'i': modifiers |= MODIFIER_IGNORE_NONPRINTING; break;
			case 'k':
				if ( !*(parameter = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "parameter requires an argument -- 'k'");
					parameter = argv[i+1];
					argv[++i] = NULL;
				}
				parse_key(parameter);
				arg = "k";
				break;
			case 'h': modifiers |= MODIFIER_HUMAN; break;
			case 'm': merge = true; break;
			case 'M': modifiers |= MODIFIER_MONTH; break;
			case 'n': modifiers |= MODIFIER_NUMERIC; break;
			case 'o':
				if ( !*(output = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "option requires an argument -- 'o'");
					output = argv[i+1];
					argv[++i] = NULL;
				}
				arg = "o";
				break;
			case 'R': modifiers |= MODIFIER_RANDOM; break;
			case 'r': modifiers |= MODIFIER_REVERSE; break;
			case 't':
				if ( !*(parameter = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(2, "parameter requires an argument -- 't'");
					parameter = argv[i+1];
					argv[++i] = NULL;
				}
				parse_separator(parameter);
				arg = "t";
				break;
			case 'u': unique = true; break;
			case 'V': modifiers |= MODIFIER_VERSION; break;
			case 'z': zero_terminated = true; break;
			default:
				errx(2, "unknown option -- '%c'", c);
			}
		}
		else if ( !strcmp(arg, "--ignore-leading-blanks") )
			modifiers |= MODIFIER_BLANK;
		else if ( !strcmp(arg, "--dictionary-order") )
			modifiers |= MODIFIER_DICTIONARY;
		else if ( !strcmp(arg, "--ignore-case") )
			modifiers |= MODIFIER_IGNORE_CASE;
		else if ( !strcmp(arg, "--general-numeric-sort") )
			modifiers |= MODIFIER_GENERAL_NUMERIC;
		else if ( !strcmp(arg, "--ignore-nonprinting") )
			modifiers |= MODIFIER_IGNORE_NONPRINTING;
		else if ( !strcmp(arg, "--human-numeric-sort") )
			modifiers |= MODIFIER_HUMAN;
		else if ( !strcmp(arg, "--month-sort") )
			modifiers |= MODIFIER_MONTH;
		else if ( !strncmp(arg, "--key=", strlen("--key=")) )
			parse_key(arg + strlen("--key="));
		else if ( !strcmp(arg, "--key") )
		{
			if ( i + 1 == argc )
				errx(2, "option '--key' requires an argument");
			parse_key(argv[i+1]);
			argv[++i] = NULL;
		}
		else if ( !strncmp(arg, "--field-separator=", strlen("--field-separator=")) )
			parse_separator(arg + strlen("--field-separator="));
		else if ( !strcmp(arg, "--field-separator") )
		{
			if ( i + 1 == argc )
				errx(2, "option '--field-separator' requires an argument");
			parse_separator(argv[i+1]);
			argv[++i] = NULL;
		}
		else if ( !strcmp(arg, "--sort") ||
		          !strncmp(arg, "--sort=", strlen("--sort=")) )
		{
			if ( !strncmp(arg, "--sort=", strlen("--sort=")) )
				parameter = arg + strlen("--sort=");
			else
			{
				if ( i + 1 == argc )
					errx(2, "option '--sort' requires an argument");
				parameter = argv[i+1];
				argv[++i] = NULL;
			}
			if ( !strcmp(parameter, "general-numeric") )
				modifiers |= MODIFIER_GENERAL_NUMERIC;
			else if ( !strcmp(parameter, "human-numeric") )
				modifiers |= MODIFIER_HUMAN;
			else if ( !strcmp(parameter, "month") )
				modifiers |= MODIFIER_MONTH;
			else if ( !strcmp(parameter, "numeric") )
				modifiers |= MODIFIER_NUMERIC;
			else if ( !strcmp(parameter, "random") )
				modifiers |= MODIFIER_RANDOM;
			else if ( !strcmp(parameter, "version") )
				modifiers |= MODIFIER_VERSION;
			else
				errx(1, "invalid sort: %s", parameter);
		}
		else if ( !strcmp(arg, "--check") ||
		          !strcmp(arg, "--check=diagnose-first") )
			check = true, check_quiet = false;
		else if ( !strcmp(arg, "--check=quiet") ||
		          !strcmp(arg, "--check=silent") )
			check = true, check_quiet = true;
		else if ( !strcmp(arg, "--merge") )
			merge = true;
		else if ( !strcmp(arg, "--numeric-sort") )
			modifiers |= MODIFIER_NUMERIC;
		else if ( !strncmp(arg, "--output=", strlen("--output=")) )
			output = arg + strlen("--output=");
		else if ( !strcmp(arg, "--output") )
		{
			if ( i + 1 == argc )
				errx(2, "option '--output' requires an argument");
			output = argv[i+1];
			argv[++i] = NULL;
		}
		else if ( !strcmp(arg, "--random-sort") )
			modifiers |= MODIFIER_RANDOM;
		else if ( !strcmp(arg, "--reverse") )
			modifiers |= MODIFIER_REVERSE;
		else if ( !strcmp(arg, "--unique") )
			unique = true;
		else if ( !strcmp(arg, "--version-sort") )
			modifiers |= MODIFIER_VERSION;
		else if ( !strcmp(arg, "--zero-terminated") )
			zero_terminated = true;
		else
			errx(2, "unknown option: %s", arg);
	}

	compact_arguments(&argc, &argv);

	if ( check_quiet && output )
		errx(2, "the -C and -o options are incompatible");
	if ( check && output )
		errx(2, "the -c and -o options are incompatible");
	if ( check_quiet && (modifiers & MODIFIER_RANDOM) )
		errx(2, "the -C and -R options are incompatible");
	if ( check && (modifiers & MODIFIER_RANDOM) )
		errx(2, "the -c and -R options are incompatible");

	check_modifiers(modifiers);

	int delim = zero_terminated ? '\0' : '\n';

	if ( !keys_count )
	{
		keys = malloc(sizeof(struct key));
		if ( !keys )
			err(2, "malloc");
		keys->start_field = 0;
		keys->first_character = 0;
		keys->end_field = SIZE_MAX - 1;
		keys->last_character = SIZE_MAX;
		keys->start_modifiers = 0;
		keys->end_modifiers = 0;
		keys_count = 1;
	}

	struct input_stream is;
	memset(&is, 0, sizeof(is));
	is.files = (const char* const*) (argv + 1);
	is.files_current = 0;
	is.files_length = argc - 1;

	if ( check )
	{
		if ( unique )
			modifiers |= MODIFIER_UNIQUE;

		int needed_relation = unique ? 1 : 0;
		char* prev_line = NULL;
		char* line;
		while ( (line = read_input_stream_line(&is, delim)) )
		{
			if ( prev_line && compare(line, prev_line) < needed_relation )
			{
				if ( check_quiet )
					return 1;
				errx(1, "%s:%ju: disorder: %s", is.last_file_path,
				     is.last_line_number, line);
			}
			free(prev_line);
			prev_line = line;
		}
		free(prev_line);
	}
	else
	{
		(void) merge;

		size_t lines_used = 0;
		char** lines = read_input_stream_lines(&lines_used, &is, delim);

		if ( !(modifiers & MODIFIER_RANDOM) || unique )
			qsort(lines, lines_used, sizeof(*lines), indirect_compare);

		if ( (modifiers & MODIFIER_RANDOM) )
		{
			if ( unique )
			{
				size_t o = 0;
				for ( size_t i = 0; i < lines_used; i++ )
				{
					if ( o && compare(lines[i], lines[o - 1]) == 0 )
						continue;
					lines[o++] = lines[i];
				}
				lines_used = o;
			}
			for ( size_t i = 0; i < lines_used; i++ )
			{
				size_t left = lines_used - i;
				size_t choice = i + pick_uniform(left);
				if ( choice != i )
				{
					char* tmp = lines[i];
					lines[i] = lines[choice];
					lines[choice] = tmp;
				}
			}
		}

		if ( output && !freopen(output, "w", stdout) )
			err(2, "%s", output);

		if ( unique )
			modifiers |= MODIFIER_UNIQUE;

		for ( size_t i = 0; i < lines_used; i++ )
		{
			if ( unique && i && compare(lines[i-1], lines[i]) == 0 )
				continue;
			if ( fputs(lines[i], stdout) == EOF || fputc(delim, stdout) == EOF )
				err(2, "%s", output ? output : "<stdout>");
		}
		if ( fflush(stdout) == EOF )
			err(2, "%s", output ? output : "<stdout>");
	}

	return 0;
}
