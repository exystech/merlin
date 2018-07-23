/*
 * Copyright (c) 2016, 2017, 2018 Jonas 'Sortie' Termansen.
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

// Files in the /share/sysinstall/hooks directory are added whenever an
// incompatible operating system change is made that needs additional actions.
// These files are part of the system manifest and their lack can be tested
// in upgrade_prepare, but not in upgrade_finalize (as they would have been
// installed there). If a file is lacking, then a hook should be run taking
// the needed action. For instance, if /etc/foo becomes the different /etc/bar,
// then /share/sysinstall/hooks/osname-x.y-bar would be made, and if it is
// absent then upgrade_prepare converts /etc/foo to /etc/bar. The file is then
// made when the system manifest is upgraded.
//
// Hooks are meant to run once. However, they must handle if the upgrade fails
// between the hook running and the hook file being installed when the system
// manifest is installed.
//
// If this system is used, follow the instructions in following-development(7)
// and add an entry in that manual page about the change.
__attribute__((used))
static bool hook_needs_to_be_run(const char* target_prefix, const char* hook)
{
	char* path;
	if ( asprintf(&path, "%sshare/sysinstall/hooks/%s",
	              target_prefix, hook) < 0 )
	{
		warn("asprintf");
		_exit(2);
	}
	bool result = access_or_die(path, F_OK) < 0;
	free(path);
	return result;
}

void upgrade_prepare(const struct release* old_release,
                     const struct release* new_release,
                     const char* source_prefix,
                     const char* target_prefix)
{
	(void) old_release;
	(void) new_release;
	(void) source_prefix;
	(void) target_prefix;

	// TODO: After releasing Sortix 1.1, remove this compatibility.
	if ( hook_needs_to_be_run(target_prefix, "sortix-1.1-random-seed") )
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

void upgrade_finalize(const struct release* old_release,
                      const struct release* new_release,
                      const char* source_prefix,
                      const char* target_prefix)
{
	(void) old_release;
	(void) new_release;
	(void) source_prefix;
	(void) target_prefix;
}
