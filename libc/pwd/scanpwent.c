/*
 * Copyright (c) 2013, 2023 Jonas 'Sortie' Termansen.
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
 * pwd/scanfpwent.c
 * Parse passwd entry.
 */

#include <ctype.h>
#include <inttypes.h>
#include <pwd.h>
#include <stdbool.h>
#include <stddef.h>

static char* next_field(char** current)
{
	char* result = *current;
	if ( result )
	{
		char* next = result;
		while ( *next && *next != ':' )
			next++;
		if ( !*next )
			next = NULL;
		else
			*next++ = '\0';
		*current = next;
	}
	return result;
}

static bool next_field_uintmax(char** current, uintmax_t* result)
{
	char* id_str = next_field(current);
	if ( !id_str )
		return false;
	char* id_endptr;
	uintmax_t id_umax = strtoumax(id_str, &id_endptr, 10);
	if ( *id_endptr )
		return false;
	*result = id_umax;
	return true;
}

static bool next_field_gid(char** current, gid_t* result)
{
	uintmax_t id_umax;
	if ( !next_field_uintmax(current, &id_umax) )
		return false;
	gid_t gid = (gid_t) id_umax;
	if ( (uintmax_t) gid != (uintmax_t) id_umax )
		return false;
	*result = gid;
	return true;
}

static bool next_field_uid(char** current, uid_t* result)
{
	uintmax_t id_umax;
	if ( !next_field_uintmax(current, &id_umax) )
		return false;
	uid_t uid = (uid_t) id_umax;
	if ( (uintmax_t) uid != (uintmax_t) id_umax )
		return false;
	*result = uid;
	return true;
}

int scanpwent(char* str, struct passwd* passwd)
{
	while ( *str && isspace((unsigned char) *str) )
		str++;
	if ( !*str || *str == '#' ||
	     !(passwd->pw_name = next_field(&str)) ||
	     !(passwd->pw_passwd = next_field(&str)) ||
	     !next_field_uid(&str, &passwd->pw_uid) ||
	     !next_field_gid(&str, &passwd->pw_gid) ||
	     !(passwd->pw_gecos = next_field(&str)) ||
	     !(passwd->pw_dir = next_field(&str)) ||
	     !(passwd->pw_shell = next_field(&str)) )
		return 0;
	return 1;
}
