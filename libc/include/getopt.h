/*
 * Copyright (c) 2013 Jonas 'Sortie' Termansen.
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
 * getopt.h
 * Command-line parsing facility.
 */

#ifndef _INCLUDE_GETOPT_H
#define _INCLUDE_GETOPT_H

#include <sys/cdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

/* These declarations are repeated in <unistd.h>. */
#ifndef __getopt_unistd_shared_declared
#define __getopt_unistd_shared_declared
extern char* optarg;
extern int opterr;
extern int optind;
extern int optopt;

int getopt(int, char* const*, const char*);
#endif

struct option
{
	const char* name;
	int has_arg;
	int* flag;
	int val;
};

#define no_argument 0
#define required_argument 1
#define optional_argument 2

int getopt_long(int, char* const*, const char*, const struct option*, int*);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
