/*
 * Copyright (c) 2016 Jonas 'Sortie' Termansen.
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
 * hooks.c
 * Upgrade compatibility hooks.
 */

#include <sys/types.h>

#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "fileops.h"
#include "hooks.h"
#include "release.h"

void upgrade_prepare(const struct release* old_release,
                     const struct release* new_release,
                     const char* source_prefix,
                     const char* target_prefix)
{
	(void) old_release;
	(void) new_release;
	(void) source_prefix;
	(void) target_prefix;
}

void upgrade_finalize(const struct release* old_release,
                      const struct release* new_release,
                      const char* source_prefix,
                      const char* target_prefix)
{
	(void) old_release;
	(void) new_release;
	(void) source_prefix;
	(void) target_prefix;

	// TODO: After releasing Sortix 1.1, remove this compatibility.
	if ( old_release->version_major < 1 ||
	     (old_release->version_major == 1 &&
	      old_release->version_minor < 1) ||
	     (old_release->version_major == 1 &&
	      old_release->version_minor == 1 &&
	      old_release->version_dev) )
	{
		char* random_seed_path;
		if ( asprintf(&random_seed_path, "%sboot/random.seed", target_prefix) < 0 )
		{
			warn("asprintf");
			_exit(1);
		}
		if ( access_or_die(random_seed_path, F_OK) < 0 )
		{
			printf(" - Creating random seed...\n");
			write_random_seed(random_seed_path);
		}
		free(random_seed_path);
	}
}
