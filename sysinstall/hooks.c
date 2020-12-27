/*
 * Copyright (c) 2016, 2017, 2018, 2020, 2021 Jonas 'Sortie' Termansen.
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
 * Upgrade hooks.
 */

#include <sys/stat.h>
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
// installed by then). If a file is lacking, then a hook should be run taking
// the needed action. For instance, if /etc/foo becomes the different /etc/bar,
// then /share/sysinstall/hooks/osname-x.y-bar would be made, and if it is
// absent then upgrade_prepare converts /etc/foo to /etc/bar. The file is then
// made when the system manifest is upgraded.
//
// Hooks are meant to run once. However, they must handle if the upgrade fails
// between the hook running and the hook file being installed when the system
// manifest is installed. I.e. they need to be reentrant and idempotent.
//
// If this system is used, follow the instructions in following-development(7)
// and add an entry in that manual page about the change.
__attribute__((used))
static bool hook_needs_to_be_run(const char* source_prefix,
                                 const char* target_prefix,
                                 const char* hook)
{
	char* source_path;
	char* target_path;
	if ( asprintf(&source_path, "%s/share/sysinstall/hooks/%s",
	              source_prefix, hook) < 0 ||
	     asprintf(&target_path, "%s/share/sysinstall/hooks/%s",
	              target_prefix, hook) < 0 )
	{
		warn("malloc");
		_exit(2);
	}
	bool result = access_or_die(source_path, F_OK) == 0 &&
	              access_or_die(target_path, F_OK) < 0;
	free(source_path);
	free(target_path);
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
	if ( hook_needs_to_be_run(source_prefix, target_prefix,
	                          "sortix-1.1-random-seed") )
	{
		char* random_seed_path = join_paths(target_prefix, "/boot/random.seed");
		if ( !random_seed_path )
		{
			warn("malloc");
			_exit(2);
		}
		if ( access_or_die(random_seed_path, F_OK) < 0 )
		{
			printf(" - Creating random seed...\n");
			write_random_seed(random_seed_path);
		}
		free(random_seed_path);
	}

	// TODO: After releasing Sortix 1.1, remove this compatibility.
	if ( hook_needs_to_be_run(source_prefix, target_prefix,
	                          "sortix-1.1-tix-manifest-mode") )
	{
		// The mode of /tix/manifest was accidentally set to 7555 (decimal)
		// instead of 0755 (octal) in sysinstall.c, i.e. mode 06603. Fix it to
		// 0755.
		char* path = join_paths(target_prefix, "/tix/manifest");
		if ( !path )
		{
			warn("malloc");
			_exit(2);
		}
		struct stat st;
		if ( stat(path, &st) < 0 )
		{
			warn("%s", path);
			_exit(2);
		}
		if ( (st.st_mode & 07777) == (7555 & 07777) )
		{
			printf(" - Fixing /tix/manifest permissions...\n");
			if ( chmod(path, 0755) < 0 )
			{
				warn("chmod: %s", path);
				_exit(2);
			}
		}
		free(path);
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
