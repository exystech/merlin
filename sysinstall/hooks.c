/*
 * Copyright (c) 2016, 2017, 2018, 2020, 2021, 2023 Jonas 'Sortie' Termansen.
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
#include <sys/utsname.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "fileops.h"
#include "hooks.h"
#include "manifest.h"
#include "release.h"
#include "string_array.h"

// TODO: After releasing Sortix 1.1, remove this compatibility. These manifests
//       may have leaked files between Sortix 1.0 and the development builds.

static const char* gawk_leaked[];
static const char* gettext_leaked[];
static const char* git_leaked[];
static const char* libav_leaked[];
static const char* libdbus_leaked[];
static const char* libfontconfig_leaked[];
static const char* libfreetype_leaked[];
static const char* libglib_leaked[];
static const char* libssl_leaked[];
static const char* libtheora_leaked[];
static const char* libxkbcommon_leaked[];
static const char* python_leaked[];
static const char* system_leaked[];

struct leaked_manifest
{
	const char* manifest;
	const char* const* leaked;
};

static const struct leaked_manifest leaked_manifests[] =
{
	{"gawk", gawk_leaked},
	{"gettext", gettext_leaked},
	{"git", git_leaked},
	{"libav", libav_leaked},
	{"libdbus", libdbus_leaked},
	{"libfontconfig", libfontconfig_leaked},
	{"libfreetype", libfreetype_leaked},
	{"libglib", libglib_leaked},
	{"libssl", libssl_leaked},
	{"libtheora", libtheora_leaked},
	{"libxkbcommon", libxkbcommon_leaked},
	{"python", python_leaked},
	{"system", system_leaked},
};

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

// If a hook needs to run a finalization step after the upgrade, it can leave
// behind a .running file in the hooks directory, which is deleted once the
// hook has run.
__attribute__((used))
static char* hook_finalization_file(const char* target_prefix, const char* hook)
{
	char* target_path;
	if ( asprintf(&target_path, "%s/share/sysinstall/hooks/%s.running",
	              target_prefix, hook) < 0 )
	{
		warn("malloc");
		_exit(2);
	}
	return target_path;
}

__attribute__((used))
static void hook_want_finalization(const char* target_prefix, const char* hook)
{
	// TODO: After releasing Sortix 1.1, remove compatibility for Sortix 1.0
	//       not having the /share/sysinstall/hooks directory.
	char* path;
	if ( asprintf(&path, "%s/share/sysinstall", target_prefix) < 0 )
	{
		warn("malloc");
		_exit(2);
	}
	mkdir(path, 0755);
	free(path);
	if ( asprintf(&path, "%s/share/sysinstall/hooks", target_prefix) < 0 )
	{
		warn("malloc");
		_exit(2);
	}
	mkdir(path, 0755);
	free(path);
	char* target_path = hook_finalization_file(target_prefix, hook);
	int fd = open(target_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if ( fd < 0 )
	{
		warn("%s", target_path);
		_exit(2);
	}
	close(fd);
	free(target_path);
}

__attribute__((used))
static bool hook_needs_finalization(const char* target_prefix, const char* hook)
{
	char* target_path = hook_finalization_file(target_prefix, hook);
	bool result = !access_or_die(target_path, F_OK);
	free(target_path);
	return result;
}

__attribute__((used))
static void hook_did_finalization(const char* target_prefix, const char* hook)
{
	char* target_path = hook_finalization_file(target_prefix, hook);
	if ( unlink(target_path) && errno != ENOENT )
	{
		warn("%s", target_path);
		_exit(2);
	}
	free(target_path);
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

	// TODO: After releasing Sortix 1.1, remove this compatibility.
	if ( version_compare(1, 0, false, old_release->version_major,
	                     old_release->version_minor, old_release->version_dev)
	     < 0 /* 1.0 < */ &&
	     hook_needs_to_be_run(source_prefix, target_prefix,
	                          "sortix-1.1-leaked-files") )
	{
		// The manifest support in sysmerge and sysupgrade didn't support
		// proper upgrades where files and directories stop existing, and if
		// a release was upgraded from 1.0 to a development build before this
		// support was introduced, then system and port files may have leaked.
		// Delete all the well known leaked files if no manifest mentions them.
		printf(" - Deleting unnecessary leaked files...\n");
		size_t leaked_manifests_count =
			sizeof(leaked_manifests) / sizeof(leaked_manifests[0]);
		for ( size_t i = 0; i < leaked_manifests_count; i++ )
		{
			const struct leaked_manifest* leaked_manifest =
				&leaked_manifests[i];
			const char* manifest = leaked_manifest->manifest;
			char* manifest_path;
			if ( asprintf(&manifest_path, "%s/tix/manifest/%s", target_prefix,
					      manifest) < 0 )
			{
				warn("malloc");
				_exit(2);
			}
			if ( access_or_die(manifest_path, F_OK) < 0 )
			{
				free(manifest_path);
				continue;
			}
			size_t installed_count;
			char** installed = read_manifest(manifest_path, &installed_count);
			if ( !installed )
			{
				warn("%s", manifest_path);
				_exit(2);
			}
			free(manifest_path);
			const char* const* leaked = leaked_manifest->leaked;
			size_t leaked_count = 0;
			while ( leaked[leaked_count] )
				leaked_count++;
			// Delete the leaked files in backwards order so directories can be
			// cleaned up after they have been emptied.
			for ( size_t i = leaked_count; i; i-- )
			{
				const char* path = leaked[i - 1];
				// Don't delete the path if it's not leaked as the regular
				// upgrade mechanism will handle this case.
				if ( string_array_contains_bsearch_strcmp(
				         (const char* const*) installed, installed_count,
				         path) )
					continue;
				char* target_path = join_paths(target_prefix, path);
				if ( !target_path )
				{
					warn("malloc");
					_exit(2);
				}
				if ( unlink(target_path) < 0 )
				{
					if ( errno == EISDIR )
					{
						if ( rmdir(target_path) < 0 &&
						     errno != ENOTEMPTY &&
						     errno != EEXIST &&
						     errno != ENOENT )
						{
							warn("%s", target_path);
							_exit(2);
						}
					}
					else if ( errno != ENOENT )
					{
						warn("%s", target_path);
						_exit(2);
					}
				}
				free(target_path);
			}
			for ( size_t i = 0; i < installed_count; i++ )
				free(installed[i]);
			free(installed);
		}
	}

	// TODO: After releasing Sortix 1.1, remove this compatibility.
	if ( hook_needs_to_be_run(source_prefix, target_prefix, "sortix-1.1-init") )
	{
		char* init_target_path = join_paths(target_prefix, "/etc/init/target");
		char* init_default_path =
			join_paths(target_prefix, "/etc/init/default");
		if ( !init_target_path || !init_default_path )
		{
			warn("malloc");
			_exit(2);
		}
		char* line = read_string_file(init_target_path);
		if ( line )
		{
			printf(" - Converting /etc/init/target to /etc/init/default...\n");
			FILE* init_default_fp = fopen(init_default_path, "w");
			if ( !init_default_fp ||
			     fprintf(init_default_fp, "require %s exit-code\n", line) < 0 ||
			     fclose(init_default_fp) == EOF )
			{
				warn("%s", init_default_path);
				_exit(2);
			}
			free(line);
			if ( unlink(init_target_path) < 0 )
			{
				warn("unlink: %s", init_target_path);
				_exit(2);
			}
		}
		else if ( errno != ENOENT )
		{
			warn("%s", init_target_path);
			_exit(2);
		}
		free(init_target_path);
		free(init_default_path);
	}

	// TODO: After releasing Sortix 1.1, remove this compatibility.
	if ( hook_needs_to_be_run(source_prefix, target_prefix,
	                          "sortix-1.1-passwd") )
	{
		char* passwd_path = join_paths(target_prefix, "/etc/passwd");
		if ( !passwd_path )
		{
			warn("malloc");
			_exit(2);
		}
		FILE* fp = fopen(passwd_path, "a");
		if ( fp )
		{
			printf(" - Including /etc/default/passwd.d/* in /etc/passwd...\n");
			if ( fprintf(fp, "include /etc/default/passwd.d/*\n") < 0 ||
			     fclose(fp) == EOF )
			{
				warn("%s", passwd_path);
				_exit(2);
			}
		}
		else if ( errno != ENOENT )
		{
			warn("%s", passwd_path);
			_exit(2);
		}
		free(passwd_path);
	}

	// TODO: After releasing Sortix 1.1, remove this compatibility.
	if ( hook_needs_to_be_run(source_prefix, target_prefix,
	                          "sortix-1.1-group") )
	{
		char* group_path = join_paths(target_prefix, "/etc/group");
		if ( !group_path )
		{
			warn("malloc");
			_exit(2);
		}
		FILE* fp = fopen(group_path, "a");
		if ( fp )
		{
			printf(" - Including /etc/default/group.d/* in /etc/group...\n");
			if ( fprintf(fp, "include /etc/default/group.d/*\n") < 0 ||
			     fclose(fp) == EOF )
			{
				warn("%s", group_path);
				_exit(2);
			}
		}
		else if ( errno != ENOENT )
		{
			warn("%s", group_path);
			_exit(2);
		}
		free(group_path);
	}

	// TODO: After releasing Sortix 1.1, remove this compatibility.
	if ( hook_needs_to_be_run(source_prefix, target_prefix,
	                          "sortix-1.1-tix-3g") )
	{
		char* path = join_paths(target_prefix, "/tix/collection.conf");
		if ( !path )
		{
			warn("malloc");
			_exit(2);
		}
		FILE* fp = fopen(path, "w");
		if ( fp )
		{
			printf(" - Migrating to tix version 3...\n");
			struct utsname uts;
			uname(&uts);
			if ( fprintf(fp, "TIX_COLLECTION_VERSION=3\n") < 0 ||
			     fprintf(fp, "PREFIX=\n") < 0 ||
			     fprintf(fp, "PLATFORM=%s-%s\n",
				         uts.machine, uts.sysname) < 0 ||
			     fclose(fp) == EOF )
			{
				warn("%s", path);
				_exit(2);
			}
		}
		else
		{
			warn("%s", path);
			_exit(2);
		}
		free(path);
		// Delay deleting installed.list since it's needed for the upgrade.
		hook_want_finalization(target_prefix, "sortix-1.1-tix-3g");
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

	if ( hook_needs_finalization(target_prefix, "sortix-1.1-tix-3g") )
	{
		printf(" - Finishing migration to tix version 3...\n");
		char* path = join_paths(target_prefix, "/tix/installed.list");
		if ( !path )
		{
			warn("malloc");
			_exit(2);
		}
		if ( unlink(path) < 0 && errno != ENOENT )
		{
			warn("%s", path);
			_exit(2);
		}
		free(path);
		path = join_paths(target_prefix, "/tix/repository.list");
		if ( !path )
		{
			warn("malloc");
			_exit(2);
		}
		if ( unlink(path) < 0 && errno != ENOENT )
		{
			warn("%s", path);
			_exit(2);
		}
		free(path);
		hook_did_finalization(target_prefix, "sortix-1.1-tix-3g");
	}
}

// TODO: After releasing Sortix 1.1, remove this compatibility. These manifests
//       may have leaked the following files between Sortix 1.0 and the
//       development builds. Some ports were upgraded and some files were not
//       installed anymore, while other ports may have been built without some
//       documentation.

static const char* gawk_leaked[] =
{
	"/bin/gawk-4.1.1",
	"/bin/igawk",
	"/lib/gawk/libtestext.a",
	"/share/man/man1/igawk.1",
	NULL,
};

static const char* gettext_leaked[] =
{
	"/lib/libgettextsrc.a",
	"/share/gettext/gettext.jar",
	"/share/gettext/libintl.jar",
	NULL,
};

static const char* git_leaked[] =
{
	"/libexec/git-core/git-relink",
	NULL,
};

static const char* libav_leaked[] =
{
	"/share/man/man1/avconv.1",
	"/share/man/man1/avprobe.1",
	"/share/man/man1/avserver.1",
	NULL,
};

static const char* libdbus_leaked[] =
{
	"/share/doc/dbus/api",
	"/share/doc/dbus/api/annotated.html",
	"/share/doc/dbus/api/bc_s.png",
	"/share/doc/dbus/api/bdwn.png",
	"/share/doc/dbus/api/classes.html",
	"/share/doc/dbus/api/closed.png",
	"/share/doc/dbus/api/dbus-address_8c_source.html",
	"/share/doc/dbus/api/dbus-address_8h_source.html",
	"/share/doc/dbus/api/dbus-arch-deps_8h_source.html",
	"/share/doc/dbus/api/dbus-auth-script_8c_source.html",
	"/share/doc/dbus/api/dbus-auth-script_8h_source.html",
	"/share/doc/dbus/api/dbus-auth-util_8c_source.html",
	"/share/doc/dbus/api/dbus-auth_8c_source.html",
	"/share/doc/dbus/api/dbus-auth_8h_source.html",
	"/share/doc/dbus/api/dbus-bus_8c_source.html",
	"/share/doc/dbus/api/dbus-bus_8h_source.html",
	"/share/doc/dbus/api/dbus-connection-internal_8h_source.html",
	"/share/doc/dbus/api/dbus-connection_8c_source.html",
	"/share/doc/dbus/api/dbus-connection_8h_source.html",
	"/share/doc/dbus/api/dbus-credentials-util_8c_source.html",
	"/share/doc/dbus/api/dbus-credentials_8c_source.html",
	"/share/doc/dbus/api/dbus-credentials_8h_source.html",
	"/share/doc/dbus/api/dbus-dataslot_8c_source.html",
	"/share/doc/dbus/api/dbus-dataslot_8h_source.html",
	"/share/doc/dbus/api/dbus-errors_8c_source.html",
	"/share/doc/dbus/api/dbus-errors_8h_source.html",
	"/share/doc/dbus/api/dbus-file-unix_8c_source.html",
	"/share/doc/dbus/api/dbus-file-win_8c_source.html",
	"/share/doc/dbus/api/dbus-file_8c_source.html",
	"/share/doc/dbus/api/dbus-file_8h_source.html",
	"/share/doc/dbus/api/dbus-hash_8c_source.html",
	"/share/doc/dbus/api/dbus-hash_8h_source.html",
	"/share/doc/dbus/api/dbus-internals_8c_source.html",
	"/share/doc/dbus/api/dbus-internals_8h_source.html",
	"/share/doc/dbus/api/dbus-keyring_8c_source.html",
	"/share/doc/dbus/api/dbus-keyring_8h_source.html",
	"/share/doc/dbus/api/dbus-list_8c_source.html",
	"/share/doc/dbus/api/dbus-list_8h_source.html",
	"/share/doc/dbus/api/dbus-macros_8h_source.html",
	"/share/doc/dbus/api/dbus-mainloop_8c_source.html",
	"/share/doc/dbus/api/dbus-mainloop_8h_source.html",
	"/share/doc/dbus/api/dbus-marshal-basic_8c_source.html",
	"/share/doc/dbus/api/dbus-marshal-basic_8h_source.html",
	"/share/doc/dbus/api/dbus-marshal-byteswap-util_8c_source.html",
	"/share/doc/dbus/api/dbus-marshal-byteswap_8c_source.html",
	"/share/doc/dbus/api/dbus-marshal-byteswap_8h_source.html",
	"/share/doc/dbus/api/dbus-marshal-header_8c_source.html",
	"/share/doc/dbus/api/dbus-marshal-header_8h_source.html",
	"/share/doc/dbus/api/dbus-marshal-recursive-util_8c_source.html",
	"/share/doc/dbus/api/dbus-marshal-recursive_8c_source.html",
	"/share/doc/dbus/api/dbus-marshal-recursive_8h_source.html",
	"/share/doc/dbus/api/dbus-marshal-validate-util_8c_source.html",
	"/share/doc/dbus/api/dbus-marshal-validate_8c_source.html",
	"/share/doc/dbus/api/dbus-marshal-validate_8h_source.html",
	"/share/doc/dbus/api/dbus-memory_8c_source.html",
	"/share/doc/dbus/api/dbus-memory_8h_source.html",
	"/share/doc/dbus/api/dbus-mempool_8c_source.html",
	"/share/doc/dbus/api/dbus-mempool_8h_source.html",
	"/share/doc/dbus/api/dbus-message-factory_8c_source.html",
	"/share/doc/dbus/api/dbus-message-factory_8h_source.html",
	"/share/doc/dbus/api/dbus-message-internal_8h_source.html",
	"/share/doc/dbus/api/dbus-message-private_8h_source.html",
	"/share/doc/dbus/api/dbus-message-util_8c_source.html",
	"/share/doc/dbus/api/dbus-message_8c_source.html",
	"/share/doc/dbus/api/dbus-message_8h_source.html",
	"/share/doc/dbus/api/dbus-misc_8c_source.html",
	"/share/doc/dbus/api/dbus-misc_8h_source.html",
	"/share/doc/dbus/api/dbus-nonce_8c_source.html",
	"/share/doc/dbus/api/dbus-nonce_8h_source.html",
	"/share/doc/dbus/api/dbus-object-tree_8c_source.html",
	"/share/doc/dbus/api/dbus-object-tree_8h_source.html",
	"/share/doc/dbus/api/dbus-pending-call-internal_8h_source.html",
	"/share/doc/dbus/api/dbus-pending-call_8c_source.html",
	"/share/doc/dbus/api/dbus-pending-call_8h_source.html",
	"/share/doc/dbus/api/dbus-pipe-unix_8c_source.html",
	"/share/doc/dbus/api/dbus-pipe-win_8c_source.html",
	"/share/doc/dbus/api/dbus-pipe_8c_source.html",
	"/share/doc/dbus/api/dbus-pipe_8h_source.html",
	"/share/doc/dbus/api/dbus-protocol_8h_source.html",
	"/share/doc/dbus/api/dbus-resources_8c_source.html",
	"/share/doc/dbus/api/dbus-resources_8h_source.html",
	"/share/doc/dbus/api/dbus-server-debug-pipe_8c_source.html",
	"/share/doc/dbus/api/dbus-server-debug-pipe_8h_source.html",
	"/share/doc/dbus/api/dbus-server-launchd_8c_source.html",
	"/share/doc/dbus/api/dbus-server-launchd_8h_source.html",
	"/share/doc/dbus/api/dbus-server-protected_8h_source.html",
	"/share/doc/dbus/api/dbus-server-socket_8c_source.html",
	"/share/doc/dbus/api/dbus-server-socket_8h_source.html",
	"/share/doc/dbus/api/dbus-server-unix_8c_source.html",
	"/share/doc/dbus/api/dbus-server-unix_8h_source.html",
	"/share/doc/dbus/api/dbus-server-win_8c_source.html",
	"/share/doc/dbus/api/dbus-server-win_8h_source.html",
	"/share/doc/dbus/api/dbus-server_8c_source.html",
	"/share/doc/dbus/api/dbus-server_8h_source.html",
	"/share/doc/dbus/api/dbus-sha_8c_source.html",
	"/share/doc/dbus/api/dbus-sha_8h_source.html",
	"/share/doc/dbus/api/dbus-shared_8h_source.html",
	"/share/doc/dbus/api/dbus-shell_8c_source.html",
	"/share/doc/dbus/api/dbus-shell_8h_source.html",
	"/share/doc/dbus/api/dbus-signature_8c_source.html",
	"/share/doc/dbus/api/dbus-signature_8h_source.html",
	"/share/doc/dbus/api/dbus-socket-set-epoll_8c_source.html",
	"/share/doc/dbus/api/dbus-socket-set-poll_8c_source.html",
	"/share/doc/dbus/api/dbus-socket-set_8c_source.html",
	"/share/doc/dbus/api/dbus-socket-set_8h_source.html",
	"/share/doc/dbus/api/dbus-sockets-win_8h_source.html",
	"/share/doc/dbus/api/dbus-spawn-win_8c_source.html",
	"/share/doc/dbus/api/dbus-spawn_8c_source.html",
	"/share/doc/dbus/api/dbus-spawn_8h_source.html",
	"/share/doc/dbus/api/dbus-string-private_8h_source.html",
	"/share/doc/dbus/api/dbus-string-util_8c_source.html",
	"/share/doc/dbus/api/dbus-string_8c_source.html",
	"/share/doc/dbus/api/dbus-string_8h_source.html",
	"/share/doc/dbus/api/dbus-syntax_8c_source.html",
	"/share/doc/dbus/api/dbus-syntax_8h_source.html",
	"/share/doc/dbus/api/dbus-sysdeps-pthread_8c_source.html",
	"/share/doc/dbus/api/dbus-sysdeps-thread-win_8c_source.html",
	"/share/doc/dbus/api/dbus-sysdeps-unix_8c_source.html",
	"/share/doc/dbus/api/dbus-sysdeps-unix_8h_source.html",
	"/share/doc/dbus/api/dbus-sysdeps-util-unix_8c_source.html",
	"/share/doc/dbus/api/dbus-sysdeps-util-win_8c_source.html",
	"/share/doc/dbus/api/dbus-sysdeps-util_8c_source.html",
	"/share/doc/dbus/api/dbus-sysdeps-win_8c_source.html",
	"/share/doc/dbus/api/dbus-sysdeps-win_8h_source.html",
	"/share/doc/dbus/api/dbus-sysdeps-wince-glue_8c_source.html",
	"/share/doc/dbus/api/dbus-sysdeps-wince-glue_8h_source.html",
	"/share/doc/dbus/api/dbus-sysdeps_8c_source.html",
	"/share/doc/dbus/api/dbus-sysdeps_8h_source.html",
	"/share/doc/dbus/api/dbus-test-main_8c_source.html",
	"/share/doc/dbus/api/dbus-test_8c_source.html",
	"/share/doc/dbus/api/dbus-test_8h_source.html",
	"/share/doc/dbus/api/dbus-threads-internal_8h_source.html",
	"/share/doc/dbus/api/dbus-threads_8c_source.html",
	"/share/doc/dbus/api/dbus-threads_8h_source.html",
	"/share/doc/dbus/api/dbus-timeout_8c_source.html",
	"/share/doc/dbus/api/dbus-timeout_8h_source.html",
	"/share/doc/dbus/api/dbus-transport-protected_8h_source.html",
	"/share/doc/dbus/api/dbus-transport-socket_8c_source.html",
	"/share/doc/dbus/api/dbus-transport-socket_8h_source.html",
	"/share/doc/dbus/api/dbus-transport-unix_8c_source.html",
	"/share/doc/dbus/api/dbus-transport-unix_8h_source.html",
	"/share/doc/dbus/api/dbus-transport-win_8c_source.html",
	"/share/doc/dbus/api/dbus-transport-win_8h_source.html",
	"/share/doc/dbus/api/dbus-transport_8c_source.html",
	"/share/doc/dbus/api/dbus-transport_8h_source.html",
	"/share/doc/dbus/api/dbus-types_8h_source.html",
	"/share/doc/dbus/api/dbus-userdb-util_8c_source.html",
	"/share/doc/dbus/api/dbus-userdb_8c_source.html",
	"/share/doc/dbus/api/dbus-userdb_8h_source.html",
	"/share/doc/dbus/api/dbus-uuidgen_8c_source.html",
	"/share/doc/dbus/api/dbus-uuidgen_8h_source.html",
	"/share/doc/dbus/api/dbus-valgrind-internal_8h_source.html",
	"/share/doc/dbus/api/dbus-watch_8c_source.html",
	"/share/doc/dbus/api/dbus-watch_8h_source.html",
	"/share/doc/dbus/api/dbus_8h_source.html",
	"/share/doc/dbus/api/dir_aa0f429d6f87d1df49a210da102129e1.html",
	"/share/doc/dbus/api/doxygen.css",
	"/share/doc/dbus/api/doxygen.png",
	"/share/doc/dbus/api/dynsections.js",
	"/share/doc/dbus/api/files.html",
	"/share/doc/dbus/api/ftv2blank.png",
	"/share/doc/dbus/api/ftv2cl.png",
	"/share/doc/dbus/api/ftv2doc.png",
	"/share/doc/dbus/api/ftv2folderclosed.png",
	"/share/doc/dbus/api/ftv2folderopen.png",
	"/share/doc/dbus/api/ftv2lastnode.png",
	"/share/doc/dbus/api/ftv2link.png",
	"/share/doc/dbus/api/ftv2mlastnode.png",
	"/share/doc/dbus/api/ftv2mnode.png",
	"/share/doc/dbus/api/ftv2mo.png",
	"/share/doc/dbus/api/ftv2node.png",
	"/share/doc/dbus/api/ftv2ns.png",
	"/share/doc/dbus/api/ftv2plastnode.png",
	"/share/doc/dbus/api/ftv2pnode.png",
	"/share/doc/dbus/api/ftv2splitbar.png",
	"/share/doc/dbus/api/ftv2vertline.png",
	"/share/doc/dbus/api/functions.html",
	"/share/doc/dbus/api/functions_b.html",
	"/share/doc/dbus/api/functions_c.html",
	"/share/doc/dbus/api/functions_d.html",
	"/share/doc/dbus/api/functions_e.html",
	"/share/doc/dbus/api/functions_f.html",
	"/share/doc/dbus/api/functions_g.html",
	"/share/doc/dbus/api/functions_h.html",
	"/share/doc/dbus/api/functions_i.html",
	"/share/doc/dbus/api/functions_k.html",
	"/share/doc/dbus/api/functions_l.html",
	"/share/doc/dbus/api/functions_m.html",
	"/share/doc/dbus/api/functions_n.html",
	"/share/doc/dbus/api/functions_o.html",
	"/share/doc/dbus/api/functions_p.html",
	"/share/doc/dbus/api/functions_q.html",
	"/share/doc/dbus/api/functions_r.html",
	"/share/doc/dbus/api/functions_s.html",
	"/share/doc/dbus/api/functions_t.html",
	"/share/doc/dbus/api/functions_u.html",
	"/share/doc/dbus/api/functions_v.html",
	"/share/doc/dbus/api/functions_vars.html",
	"/share/doc/dbus/api/functions_vars_b.html",
	"/share/doc/dbus/api/functions_vars_c.html",
	"/share/doc/dbus/api/functions_vars_d.html",
	"/share/doc/dbus/api/functions_vars_e.html",
	"/share/doc/dbus/api/functions_vars_f.html",
	"/share/doc/dbus/api/functions_vars_g.html",
	"/share/doc/dbus/api/functions_vars_h.html",
	"/share/doc/dbus/api/functions_vars_i.html",
	"/share/doc/dbus/api/functions_vars_k.html",
	"/share/doc/dbus/api/functions_vars_l.html",
	"/share/doc/dbus/api/functions_vars_m.html",
	"/share/doc/dbus/api/functions_vars_n.html",
	"/share/doc/dbus/api/functions_vars_o.html",
	"/share/doc/dbus/api/functions_vars_p.html",
	"/share/doc/dbus/api/functions_vars_q.html",
	"/share/doc/dbus/api/functions_vars_r.html",
	"/share/doc/dbus/api/functions_vars_s.html",
	"/share/doc/dbus/api/functions_vars_t.html",
	"/share/doc/dbus/api/functions_vars_u.html",
	"/share/doc/dbus/api/functions_vars_v.html",
	"/share/doc/dbus/api/functions_vars_w.html",
	"/share/doc/dbus/api/functions_vars_z.html",
	"/share/doc/dbus/api/functions_w.html",
	"/share/doc/dbus/api/functions_z.html",
	"/share/doc/dbus/api/group__DBus.html",
	"/share/doc/dbus/api/group__DBusAddress.html",
	"/share/doc/dbus/api/group__DBusAddressInternals.html",
	"/share/doc/dbus/api/group__DBusAuth.html",
	"/share/doc/dbus/api/group__DBusAuthInternals.html",
	"/share/doc/dbus/api/group__DBusBus.html",
	"/share/doc/dbus/api/group__DBusBusInternals.html",
	"/share/doc/dbus/api/group__DBusConnection.html",
	"/share/doc/dbus/api/group__DBusConnectionInternals.html",
	"/share/doc/dbus/api/group__DBusCredentials.html",
	"/share/doc/dbus/api/group__DBusCredentialsInternals.html",
	"/share/doc/dbus/api/group__DBusDataSlot.html",
	"/share/doc/dbus/api/group__DBusErrorInternals.html",
	"/share/doc/dbus/api/group__DBusErrors.html",
	"/share/doc/dbus/api/group__DBusFile.html",
	"/share/doc/dbus/api/group__DBusHashTable.html",
	"/share/doc/dbus/api/group__DBusHashTableInternals.html",
	"/share/doc/dbus/api/group__DBusInternals.html",
	"/share/doc/dbus/api/group__DBusInternalsUtils.html",
	"/share/doc/dbus/api/group__DBusInternalsUuidgen.html",
	"/share/doc/dbus/api/group__DBusKeyring.html",
	"/share/doc/dbus/api/group__DBusKeyringInternals.html",
	"/share/doc/dbus/api/group__DBusList.html",
	"/share/doc/dbus/api/group__DBusListInternals.html",
	"/share/doc/dbus/api/group__DBusMacros.html",
	"/share/doc/dbus/api/group__DBusMarshal.html",
	"/share/doc/dbus/api/group__DBusMemPool.html",
	"/share/doc/dbus/api/group__DBusMemPoolInternals.html",
	"/share/doc/dbus/api/group__DBusMemory.html",
	"/share/doc/dbus/api/group__DBusMemoryInternals.html",
	"/share/doc/dbus/api/group__DBusMessage.html",
	"/share/doc/dbus/api/group__DBusMessageInternals.html",
	"/share/doc/dbus/api/group__DBusMisc.html",
	"/share/doc/dbus/api/group__DBusObjectTree.html",
	"/share/doc/dbus/api/group__DBusPendingCall.html",
	"/share/doc/dbus/api/group__DBusPendingCallInternals.html",
	"/share/doc/dbus/api/group__DBusProtocol.html",
	"/share/doc/dbus/api/group__DBusResources.html",
	"/share/doc/dbus/api/group__DBusResourcesInternals.html",
	"/share/doc/dbus/api/group__DBusSHA.html",
	"/share/doc/dbus/api/group__DBusSHAInternals.html",
	"/share/doc/dbus/api/group__DBusServer.html",
	"/share/doc/dbus/api/group__DBusServerInternals.html",
	"/share/doc/dbus/api/group__DBusServerLaunchd.html",
	"/share/doc/dbus/api/group__DBusServerSocket.html",
	"/share/doc/dbus/api/group__DBusServerUnix.html",
	"/share/doc/dbus/api/group__DBusServerWin.html",
	"/share/doc/dbus/api/group__DBusShared.html",
	"/share/doc/dbus/api/group__DBusSignature.html",
	"/share/doc/dbus/api/group__DBusString.html",
	"/share/doc/dbus/api/group__DBusStringInternals.html",
	"/share/doc/dbus/api/group__DBusSyntax.html",
	"/share/doc/dbus/api/group__DBusSysdeps.html",
	"/share/doc/dbus/api/group__DBusSysdepsUnix.html",
	"/share/doc/dbus/api/group__DBusThreads.html",
	"/share/doc/dbus/api/group__DBusThreadsInternals.html",
	"/share/doc/dbus/api/group__DBusTimeout.html",
	"/share/doc/dbus/api/group__DBusTimeoutInternals.html",
	"/share/doc/dbus/api/group__DBusTransport.html",
	"/share/doc/dbus/api/group__DBusTransportSocket.html",
	"/share/doc/dbus/api/group__DBusTransportUnix.html",
	"/share/doc/dbus/api/group__DBusTypes.html",
	"/share/doc/dbus/api/group__DBusWatch.html",
	"/share/doc/dbus/api/group__DBusWatchInternals.html",
	"/share/doc/dbus/api/index.html",
	"/share/doc/dbus/api/modules.html",
	"/share/doc/dbus/api/nav_f.png",
	"/share/doc/dbus/api/nav_g.png",
	"/share/doc/dbus/api/nav_h.png",
	"/share/doc/dbus/api/open.png",
	"/share/doc/dbus/api/pages.html",
	"/share/doc/dbus/api/sd-daemon_8c_source.html",
	"/share/doc/dbus/api/sd-daemon_8h_source.html",
	"/share/doc/dbus/api/structBusData.html",
	"/share/doc/dbus/api/structDBus8ByteStruct.html",
	"/share/doc/dbus/api/structDBusAddressEntry.html",
	"/share/doc/dbus/api/structDBusAllocatedSlot.html",
	"/share/doc/dbus/api/structDBusArrayLenFixup.html",
	"/share/doc/dbus/api/structDBusAtomic.html",
	"/share/doc/dbus/api/structDBusAuth.html",
	"/share/doc/dbus/api/structDBusAuthClient.html",
	"/share/doc/dbus/api/structDBusAuthCommandName.html",
	"/share/doc/dbus/api/structDBusAuthMechanismHandler.html",
	"/share/doc/dbus/api/structDBusAuthServer.html",
	"/share/doc/dbus/api/structDBusAuthStateData.html",
	"/share/doc/dbus/api/structDBusBabysitter.html",
	"/share/doc/dbus/api/structDBusCMutex.html",
	"/share/doc/dbus/api/structDBusCondVar.html",
	"/share/doc/dbus/api/structDBusConnection.html",
	"/share/doc/dbus/api/structDBusCounter.html",
	"/share/doc/dbus/api/structDBusCredentials.html",
	"/share/doc/dbus/api/structDBusDataSlot.html",
	"/share/doc/dbus/api/structDBusDataSlotAllocator.html",
	"/share/doc/dbus/api/structDBusDataSlotList.html",
	"/share/doc/dbus/api/structDBusDirIter.html",
	"/share/doc/dbus/api/structDBusError.html",
	"/share/doc/dbus/api/structDBusFreedElement.html",
	"/share/doc/dbus/api/structDBusGroupInfo.html",
	"/share/doc/dbus/api/structDBusHashEntry.html",
	"/share/doc/dbus/api/structDBusHashIter.html",
	"/share/doc/dbus/api/structDBusHashTable.html",
	"/share/doc/dbus/api/structDBusHeader.html",
	"/share/doc/dbus/api/structDBusHeaderField.html",
	"/share/doc/dbus/api/structDBusKey.html",
	"/share/doc/dbus/api/structDBusKeyring.html",
	"/share/doc/dbus/api/structDBusList.html",
	"/share/doc/dbus/api/structDBusMemBlock.html",
	"/share/doc/dbus/api/structDBusMemPool.html",
	"/share/doc/dbus/api/structDBusMessage.html",
	"/share/doc/dbus/api/structDBusMessageFilter.html",
	"/share/doc/dbus/api/structDBusMessageIter.html",
	"/share/doc/dbus/api/structDBusMessageLoader.html",
	"/share/doc/dbus/api/structDBusMessageRealIter.html",
	"/share/doc/dbus/api/structDBusNonceFile.html",
	"/share/doc/dbus/api/structDBusObjectPathVTable.html",
	"/share/doc/dbus/api/structDBusObjectSubtree.html",
	"/share/doc/dbus/api/structDBusObjectTree.html",
	"/share/doc/dbus/api/structDBusPendingCall.html",
	"/share/doc/dbus/api/structDBusPipe.html",
	"/share/doc/dbus/api/structDBusPollFD.html",
	"/share/doc/dbus/api/structDBusPreallocatedSend.html",
	"/share/doc/dbus/api/structDBusRMutex.html",
	"/share/doc/dbus/api/structDBusRealError.html",
	"/share/doc/dbus/api/structDBusRealHashIter.html",
	"/share/doc/dbus/api/structDBusRealString.html",
	"/share/doc/dbus/api/structDBusSHAContext.html",
	"/share/doc/dbus/api/structDBusServer.html",
	"/share/doc/dbus/api/structDBusServerSocket.html",
	"/share/doc/dbus/api/structDBusServerVTable.html",
	"/share/doc/dbus/api/structDBusSignatureIter.html",
	"/share/doc/dbus/api/structDBusSignatureRealIter.html",
	"/share/doc/dbus/api/structDBusStat.html",
	"/share/doc/dbus/api/structDBusString.html",
	"/share/doc/dbus/api/structDBusThreadFunctions.html",
	"/share/doc/dbus/api/structDBusTimeout.html",
	"/share/doc/dbus/api/structDBusTimeoutList.html",
	"/share/doc/dbus/api/structDBusTransport.html",
	"/share/doc/dbus/api/structDBusTransportSocket.html",
	"/share/doc/dbus/api/structDBusTransportVTable.html",
	"/share/doc/dbus/api/structDBusTypeReader.html",
	"/share/doc/dbus/api/structDBusTypeReaderClass.html",
	"/share/doc/dbus/api/structDBusTypeWriter.html",
	"/share/doc/dbus/api/structDBusUserInfo.html",
	"/share/doc/dbus/api/structDBusWatch.html",
	"/share/doc/dbus/api/structDBusWatchList.html",
	"/share/doc/dbus/api/structDIR.html",
	"/share/doc/dbus/api/structHeaderFieldType.html",
	"/share/doc/dbus/api/structReplacementBlock.html",
	"/share/doc/dbus/api/structShutdownClosure.html",
	"/share/doc/dbus/api/structdirent.html",
	"/share/doc/dbus/api/sync_off.png",
	"/share/doc/dbus/api/sync_on.png",
	"/share/doc/dbus/api/tab_a.png",
	"/share/doc/dbus/api/tab_b.png",
	"/share/doc/dbus/api/tab_h.png",
	"/share/doc/dbus/api/tab_s.png",
	"/share/doc/dbus/api/tabs.css",
	"/share/doc/dbus/api/todo.html",
	"/share/doc/dbus/api/unionDBusBasicValue.html",
	"/share/doc/dbus/api/unionDBusGUID.html",
	"/share/doc/dbus/api/unionsockaddr__union.html",
	"/share/doc/dbus/dbus.devhelp",
	"/var/run",
	"/var/run/dbus",
	NULL,
};

static const char* libfontconfig_leaked[] =
{
	"/etc/fonts/conf.d/30-urw-aliases.conf",
	"/share/fontconfig/conf.avail/30-urw-aliases.conf",
	NULL,
};

static const char* libfreetype_leaked[] =
{
	"/include/freetype2/freetype/ftxf86.h",
	"/include/ft2build.h",
	NULL,
};

static const char* libglib_leaked[] =
{
	"/share/man/man1/gapplication.1",
	"/share/man/man1/gdbus-codegen.1",
	"/share/man/man1/gdbus.1",
	"/share/man/man1/gio-querymodules.1",
	"/share/man/man1/glib-compile-resources.1",
	"/share/man/man1/glib-compile-schemas.1",
	"/share/man/man1/glib-genmarshal.1",
	"/share/man/man1/glib-gettextize.1",
	"/share/man/man1/glib-mkenums.1",
	"/share/man/man1/gobject-query.1",
	"/share/man/man1/gresource.1",
	"/share/man/man1/gsettings.1",
	"/share/man/man1/gtester-report.1",
	"/share/man/man1/gtester.1",
	NULL,
};

static const char* libssl_leaked[] =
{
	"/include/openssl/asn1_mac.h",
	"/include/openssl/krb5_asn.h",
	"/share/man/man3/BIO.3",
	"/share/man/man3/CMS_sign_add1_signer.3",
	"/share/man/man3/CRYPTO_THREADID_get_callback.3",
	"/share/man/man3/CRYPTO_THREADID_set_callback.3",
	"/share/man/man3/CRYPTO_add_lock.3",
	"/share/man/man3/CRYPTO_destroy_dynlockid.3",
	"/share/man/man3/CRYPTO_get_new_dynlockid.3",
	"/share/man/man3/CRYPTO_num_locks.3",
	"/share/man/man3/CRYPTO_set_dynlock_create_callback.3",
	"/share/man/man3/CRYPTO_set_dynlock_destroy_callback.3",
	"/share/man/man3/CRYPTO_set_dynlock_lock_callback.3",
	"/share/man/man3/CRYPTO_set_id_callback.3",
	"/share/man/man3/CRYPTO_set_locking_callback.3",
	"/share/man/man3/DH_get_default_openssl_method.3",
	"/share/man/man3/DH_set_default_openssl_method.3",
	"/share/man/man3/DSA_get_default_openssl_method.3",
	"/share/man/man3/DSA_set_default_openssl_method.3",
	"/share/man/man3/EC_POINT_set_Jprojective_coordinates.3",
	"/share/man/man3/ERR_load_UI_strings.3",
	"/share/man/man3/EVP_PKEVP_PKEY_CTX_set_app_data.3",
	"/share/man/man3/EVP_PKEY_CTX_set_rsa_rsa_keygen_bits.3",
	"/share/man/man3/EVP_PKEY_ctrl_str.3",
	"/share/man/man3/EVP_PKEY_get_default_digest.3",
	"/share/man/man3/EVP_md2.3",
	"/share/man/man3/EVP_rc5_32_12_16_cbc.3",
	"/share/man/man3/EVP_rc5_32_12_16_cfb.3",
	"/share/man/man3/EVP_rc5_32_12_16_ecb.3",
	"/share/man/man3/EVP_rc5_32_12_16_ofb.3",
	"/share/man/man3/MD2.3",
	"/share/man/man3/MD2_Final.3",
	"/share/man/man3/MD2_Init.3",
	"/share/man/man3/MD2_Update.3",
	"/share/man/man3/PEM.3",
	"/share/man/man3/RAND.3",
	"/share/man/man3/RSA_PKCS1_RSAref.3",
	"/share/man/man3/RSA_get_default_openssl_method.3",
	"/share/man/man3/RSA_null_method.3",
	"/share/man/man3/RSA_padding_add_SSLv23.3",
	"/share/man/man3/RSA_padding_check_SSLv23.3",
	"/share/man/man3/RSA_set_default_openssl_method.3",
	"/share/man/man3/SSL_CTX_need_tmp_rsa.3",
	"/share/man/man3/SSL_CTX_sess_set_remove.3",
	"/share/man/man3/SSL_CTX_set_psk_client_callback.3",
	"/share/man/man3/SSL_CTX_set_psk_server_callback.3",
	"/share/man/man3/SSL_CTX_use_psk_identity_hint.3",
	"/share/man/man3/SSL_add_session.3",
	"/share/man/man3/SSL_flush_sessions.3",
	"/share/man/man3/SSL_get_accept_state.3",
	"/share/man/man3/SSL_get_msg_callback_arg.3",
	"/share/man/man3/SSL_get_psk_identity.3",
	"/share/man/man3/SSL_get_psk_identity_hint.3",
	"/share/man/man3/SSL_need_tmp_rsa.3",
	"/share/man/man3/SSL_remove_session.3",
	"/share/man/man3/SSL_set_psk_client_callback.3",
	"/share/man/man3/SSL_set_psk_server_callback.3",
	"/share/man/man3/SSL_use_psk_identity_hint.3",
	"/share/man/man3/SSLv3_client_method.3",
	"/share/man/man3/SSLv3_method.3",
	"/share/man/man3/SSLv3_server_method.3",
	"/share/man/man3/bn.3",
	"/share/man/man3/bn_internal.3",
	"/share/man/man3/bn_print.3",
	"/share/man/man3/callback.3",
	"/share/man/man3/crypto_dispatch.3",
	"/share/man/man3/crypto_done.3",
	"/share/man/man3/crypto_freereq.3",
	"/share/man/man3/crypto_freesession.3",
	"/share/man/man3/crypto_get_driverid.3",
	"/share/man/man3/crypto_getreq.3",
	"/share/man/man3/crypto_newsession.3",
	"/share/man/man3/crypto_register.3",
	"/share/man/man3/crypto_unregister.3",
	"/share/man/man3/d2i_PKCS8PrivateKey.3",
	"/share/man/man3/des_read_2passwords.3",
	"/share/man/man3/des_read_password.3",
	"/share/man/man3/dh.3",
	"/share/man/man3/dsa.3",
	"/share/man/man3/ec.3",
	"/share/man/man3/ecdsa.3",
	"/share/man/man3/engine.3",
	"/share/man/man3/lhash.3",
	"/share/man/man3/pem_passwd_cb.3",
	"/share/man/man3/rsa.3",
	"/share/man/man3/tls_config_set_ecdhecurve.3",
	"/share/man/man3/tmp_rsa_callback.3",
	"/share/man/man3/ui.3",
	"/share/man/man3/ui_compat.3",
	"/share/man/man3/x509.3",
	NULL,
};

static const char* libtheora_leaked[] =
{
	"/share/doc/libtheora-1.1.1/html",
	"/share/doc/libtheora-1.1.1/html/annotated.html",
	"/share/doc/libtheora-1.1.1/html/bc_s.png",
	"/share/doc/libtheora-1.1.1/html/bdwn.png",
	"/share/doc/libtheora-1.1.1/html/classes.html",
	"/share/doc/libtheora-1.1.1/html/closed.png",
	"/share/doc/libtheora-1.1.1/html/codec_8h.html",
	"/share/doc/libtheora-1.1.1/html/codec_8h_source.html",
	"/share/doc/libtheora-1.1.1/html/dir_0f91760d62c578de767c41a0aaae5482.html",
	"/share/doc/libtheora-1.1.1/html/dir_d44c64559bbebec7f509842c48db8b23.html",
	"/share/doc/libtheora-1.1.1/html/doxygen.css",
	"/share/doc/libtheora-1.1.1/html/doxygen.png",
	"/share/doc/libtheora-1.1.1/html/dynsections.js",
	"/share/doc/libtheora-1.1.1/html/files.html",
	"/share/doc/libtheora-1.1.1/html/ftv2blank.png",
	"/share/doc/libtheora-1.1.1/html/ftv2cl.png",
	"/share/doc/libtheora-1.1.1/html/ftv2doc.png",
	"/share/doc/libtheora-1.1.1/html/ftv2folderclosed.png",
	"/share/doc/libtheora-1.1.1/html/ftv2folderopen.png",
	"/share/doc/libtheora-1.1.1/html/ftv2lastnode.png",
	"/share/doc/libtheora-1.1.1/html/ftv2link.png",
	"/share/doc/libtheora-1.1.1/html/ftv2mlastnode.png",
	"/share/doc/libtheora-1.1.1/html/ftv2mnode.png",
	"/share/doc/libtheora-1.1.1/html/ftv2mo.png",
	"/share/doc/libtheora-1.1.1/html/ftv2node.png",
	"/share/doc/libtheora-1.1.1/html/ftv2ns.png",
	"/share/doc/libtheora-1.1.1/html/ftv2plastnode.png",
	"/share/doc/libtheora-1.1.1/html/ftv2pnode.png",
	"/share/doc/libtheora-1.1.1/html/ftv2splitbar.png",
	"/share/doc/libtheora-1.1.1/html/ftv2vertline.png",
	"/share/doc/libtheora-1.1.1/html/functions.html",
	"/share/doc/libtheora-1.1.1/html/functions_vars.html",
	"/share/doc/libtheora-1.1.1/html/globals.html",
	"/share/doc/libtheora-1.1.1/html/globals_defs.html",
	"/share/doc/libtheora-1.1.1/html/globals_enum.html",
	"/share/doc/libtheora-1.1.1/html/globals_eval.html",
	"/share/doc/libtheora-1.1.1/html/globals_func.html",
	"/share/doc/libtheora-1.1.1/html/globals_type.html",
	"/share/doc/libtheora-1.1.1/html/globals_vars.html",
	"/share/doc/libtheora-1.1.1/html/group__basefuncs.html",
	"/share/doc/libtheora-1.1.1/html/group__decfuncs.html",
	"/share/doc/libtheora-1.1.1/html/group__encfuncs.html",
	"/share/doc/libtheora-1.1.1/html/group__oldfuncs.html",
	"/share/doc/libtheora-1.1.1/html/index.html",
	"/share/doc/libtheora-1.1.1/html/modules.html",
	"/share/doc/libtheora-1.1.1/html/nav_f.png",
	"/share/doc/libtheora-1.1.1/html/nav_g.png",
	"/share/doc/libtheora-1.1.1/html/nav_h.png",
	"/share/doc/libtheora-1.1.1/html/open.png",
	"/share/doc/libtheora-1.1.1/html/structth__comment.html",
	"/share/doc/libtheora-1.1.1/html/structth__huff__code.html",
	"/share/doc/libtheora-1.1.1/html/structth__img__plane.html",
	"/share/doc/libtheora-1.1.1/html/structth__info.html",
	"/share/doc/libtheora-1.1.1/html/structth__quant__info.html",
	"/share/doc/libtheora-1.1.1/html/structth__quant__ranges.html",
	"/share/doc/libtheora-1.1.1/html/structth__stripe__callback.html",
	"/share/doc/libtheora-1.1.1/html/structtheora__comment.html",
	"/share/doc/libtheora-1.1.1/html/structtheora__info.html",
	"/share/doc/libtheora-1.1.1/html/structtheora__state.html",
	"/share/doc/libtheora-1.1.1/html/structyuv__buffer.html",
	"/share/doc/libtheora-1.1.1/html/sync_off.png",
	"/share/doc/libtheora-1.1.1/html/sync_on.png",
	"/share/doc/libtheora-1.1.1/html/tab_a.png",
	"/share/doc/libtheora-1.1.1/html/tab_b.png",
	"/share/doc/libtheora-1.1.1/html/tab_h.png",
	"/share/doc/libtheora-1.1.1/html/tab_s.png",
	"/share/doc/libtheora-1.1.1/html/tabs.css",
	"/share/doc/libtheora-1.1.1/html/theora_8h.html",
	"/share/doc/libtheora-1.1.1/html/theora_8h_source.html",
	"/share/doc/libtheora-1.1.1/html/theoradec_8h.html",
	"/share/doc/libtheora-1.1.1/html/theoradec_8h_source.html",
	"/share/doc/libtheora-1.1.1/html/theoraenc_8h.html",
	"/share/doc/libtheora-1.1.1/html/theoraenc_8h_source.html",
	"/share/doc/libtheora-1.1.1/latex",
	"/share/doc/libtheora-1.1.1/latex/Makefile",
	"/share/doc/libtheora-1.1.1/latex/annotated.tex",
	"/share/doc/libtheora-1.1.1/latex/codec_8h.tex",
	"/share/doc/libtheora-1.1.1/latex/dir_0f91760d62c578de767c41a0aaae5482.tex",
	"/share/doc/libtheora-1.1.1/latex/dir_d44c64559bbebec7f509842c48db8b23.tex",
	"/share/doc/libtheora-1.1.1/latex/doxygen.sty",
	"/share/doc/libtheora-1.1.1/latex/files.tex",
	"/share/doc/libtheora-1.1.1/latex/group__basefuncs.tex",
	"/share/doc/libtheora-1.1.1/latex/group__decfuncs.tex",
	"/share/doc/libtheora-1.1.1/latex/group__encfuncs.tex",
	"/share/doc/libtheora-1.1.1/latex/group__oldfuncs.tex",
	"/share/doc/libtheora-1.1.1/latex/index.tex",
	"/share/doc/libtheora-1.1.1/latex/modules.tex",
	"/share/doc/libtheora-1.1.1/latex/refman.tex",
	"/share/doc/libtheora-1.1.1/latex/structth__comment.tex",
	"/share/doc/libtheora-1.1.1/latex/structth__huff__code.tex",
	"/share/doc/libtheora-1.1.1/latex/structth__img__plane.tex",
	"/share/doc/libtheora-1.1.1/latex/structth__info.tex",
	"/share/doc/libtheora-1.1.1/latex/structth__quant__info.tex",
	"/share/doc/libtheora-1.1.1/latex/structth__quant__ranges.tex",
	"/share/doc/libtheora-1.1.1/latex/structth__stripe__callback.tex",
	"/share/doc/libtheora-1.1.1/latex/structtheora__comment.tex",
	"/share/doc/libtheora-1.1.1/latex/structtheora__info.tex",
	"/share/doc/libtheora-1.1.1/latex/structtheora__state.tex",
	"/share/doc/libtheora-1.1.1/latex/structyuv__buffer.tex",
	"/share/doc/libtheora-1.1.1/latex/theora_8h.tex",
	"/share/doc/libtheora-1.1.1/latex/theoradec_8h.tex",
	"/share/doc/libtheora-1.1.1/latex/theoraenc_8h.tex",
	NULL,
};

static const char* libxkbcommon_leaked[] =
{
	"/share/doc/libxkbcommon",
	"/share/doc/libxkbcommon/annotated.html",
	"/share/doc/libxkbcommon/annotated.js",
	"/share/doc/libxkbcommon/bc_s.png",
	"/share/doc/libxkbcommon/bdwn.png",
	"/share/doc/libxkbcommon/classes.html",
	"/share/doc/libxkbcommon/closed.png",
	"/share/doc/libxkbcommon/dir_34c01ee884f305be60d2204fef47a48f.html",
	"/share/doc/libxkbcommon/dir_34c01ee884f305be60d2204fef47a48f.js",
	"/share/doc/libxkbcommon/doxygen.css",
	"/share/doc/libxkbcommon/doxygen.png",
	"/share/doc/libxkbcommon/dynsections.js",
	"/share/doc/libxkbcommon/files.html",
	"/share/doc/libxkbcommon/files.js",
	"/share/doc/libxkbcommon/ftv2blank.png",
	"/share/doc/libxkbcommon/ftv2cl.png",
	"/share/doc/libxkbcommon/ftv2doc.png",
	"/share/doc/libxkbcommon/ftv2folderclosed.png",
	"/share/doc/libxkbcommon/ftv2folderopen.png",
	"/share/doc/libxkbcommon/ftv2lastnode.png",
	"/share/doc/libxkbcommon/ftv2link.png",
	"/share/doc/libxkbcommon/ftv2mlastnode.png",
	"/share/doc/libxkbcommon/ftv2mnode.png",
	"/share/doc/libxkbcommon/ftv2mo.png",
	"/share/doc/libxkbcommon/ftv2node.png",
	"/share/doc/libxkbcommon/ftv2ns.png",
	"/share/doc/libxkbcommon/ftv2plastnode.png",
	"/share/doc/libxkbcommon/ftv2pnode.png",
	"/share/doc/libxkbcommon/ftv2splitbar.png",
	"/share/doc/libxkbcommon/ftv2vertline.png",
	"/share/doc/libxkbcommon/functions.html",
	"/share/doc/libxkbcommon/functions_func.html",
	"/share/doc/libxkbcommon/functions_type.html",
	"/share/doc/libxkbcommon/functions_vars.html",
	"/share/doc/libxkbcommon/globals.html",
	"/share/doc/libxkbcommon/globals_defs.html",
	"/share/doc/libxkbcommon/globals_enum.html",
	"/share/doc/libxkbcommon/globals_eval.html",
	"/share/doc/libxkbcommon/globals_func.html",
	"/share/doc/libxkbcommon/globals_type.html",
	"/share/doc/libxkbcommon/group__components.html",
	"/share/doc/libxkbcommon/group__components.js",
	"/share/doc/libxkbcommon/group__context.html",
	"/share/doc/libxkbcommon/group__context.js",
	"/share/doc/libxkbcommon/group__include-path.html",
	"/share/doc/libxkbcommon/group__include-path.js",
	"/share/doc/libxkbcommon/group__keymap.html",
	"/share/doc/libxkbcommon/group__keymap.js",
	"/share/doc/libxkbcommon/group__keysyms.html",
	"/share/doc/libxkbcommon/group__keysyms.js",
	"/share/doc/libxkbcommon/group__logging.html",
	"/share/doc/libxkbcommon/group__logging.js",
	"/share/doc/libxkbcommon/group__state.html",
	"/share/doc/libxkbcommon/group__state.js",
	"/share/doc/libxkbcommon/index.html",
	"/share/doc/libxkbcommon/jquery.js",
	"/share/doc/libxkbcommon/modules.html",
	"/share/doc/libxkbcommon/modules.js",
	"/share/doc/libxkbcommon/nav_f.png",
	"/share/doc/libxkbcommon/nav_g.png",
	"/share/doc/libxkbcommon/nav_h.png",
	"/share/doc/libxkbcommon/navtree.css",
	"/share/doc/libxkbcommon/navtree.js",
	"/share/doc/libxkbcommon/navtreeindex0.js",
	"/share/doc/libxkbcommon/navtreeindex1.js",
	"/share/doc/libxkbcommon/open.png",
	"/share/doc/libxkbcommon/pages.html",
	"/share/doc/libxkbcommon/resize.js",
	"/share/doc/libxkbcommon/structxkb__context.html",
	"/share/doc/libxkbcommon/structxkb__context.js",
	"/share/doc/libxkbcommon/structxkb__keymap.html",
	"/share/doc/libxkbcommon/structxkb__keymap.js",
	"/share/doc/libxkbcommon/structxkb__rule__names.html",
	"/share/doc/libxkbcommon/structxkb__rule__names.js",
	"/share/doc/libxkbcommon/structxkb__state.html",
	"/share/doc/libxkbcommon/structxkb__state.js",
	"/share/doc/libxkbcommon/sync_off.png",
	"/share/doc/libxkbcommon/sync_on.png",
	"/share/doc/libxkbcommon/tab_a.png",
	"/share/doc/libxkbcommon/tab_b.png",
	"/share/doc/libxkbcommon/tab_h.png",
	"/share/doc/libxkbcommon/tab_s.png",
	"/share/doc/libxkbcommon/tabs.css",
	"/share/doc/libxkbcommon/todo.html",
	"/share/doc/libxkbcommon/xkbcommon-compat_8h_source.html",
	"/share/doc/libxkbcommon/xkbcommon-keysyms_8h_source.html",
	"/share/doc/libxkbcommon/xkbcommon-names_8h.html",
	"/share/doc/libxkbcommon/xkbcommon-names_8h.js",
	"/share/doc/libxkbcommon/xkbcommon-names_8h_source.html",
	"/share/doc/libxkbcommon/xkbcommon_8h.html",
	"/share/doc/libxkbcommon/xkbcommon_8h.js",
	"/share/doc/libxkbcommon/xkbcommon_8h_source.html",
	NULL,
};

static const char* python_leaked[] =
{
	"/bin/2to3-3.4",
	"/bin/idle3.4",
	"/bin/pydoc3.4",
	"/bin/python3.4",
	"/bin/python3.4m",
	"/bin/pyvenv-3.4",
	"/include/python3.4m",
	"/include/python3.4m/Python-ast.h",
	"/include/python3.4m/Python.h",
	"/include/python3.4m/abstract.h",
	"/include/python3.4m/accu.h",
	"/include/python3.4m/asdl.h",
	"/include/python3.4m/ast.h",
	"/include/python3.4m/bitset.h",
	"/include/python3.4m/bltinmodule.h",
	"/include/python3.4m/boolobject.h",
	"/include/python3.4m/bytearrayobject.h",
	"/include/python3.4m/bytes_methods.h",
	"/include/python3.4m/bytesobject.h",
	"/include/python3.4m/cellobject.h",
	"/include/python3.4m/ceval.h",
	"/include/python3.4m/classobject.h",
	"/include/python3.4m/code.h",
	"/include/python3.4m/codecs.h",
	"/include/python3.4m/compile.h",
	"/include/python3.4m/complexobject.h",
	"/include/python3.4m/datetime.h",
	"/include/python3.4m/descrobject.h",
	"/include/python3.4m/dictobject.h",
	"/include/python3.4m/dtoa.h",
	"/include/python3.4m/dynamic_annotations.h",
	"/include/python3.4m/enumobject.h",
	"/include/python3.4m/errcode.h",
	"/include/python3.4m/eval.h",
	"/include/python3.4m/fileobject.h",
	"/include/python3.4m/fileutils.h",
	"/include/python3.4m/floatobject.h",
	"/include/python3.4m/frameobject.h",
	"/include/python3.4m/funcobject.h",
	"/include/python3.4m/genobject.h",
	"/include/python3.4m/graminit.h",
	"/include/python3.4m/grammar.h",
	"/include/python3.4m/import.h",
	"/include/python3.4m/intrcheck.h",
	"/include/python3.4m/iterobject.h",
	"/include/python3.4m/listobject.h",
	"/include/python3.4m/longintrepr.h",
	"/include/python3.4m/longobject.h",
	"/include/python3.4m/marshal.h",
	"/include/python3.4m/memoryobject.h",
	"/include/python3.4m/metagrammar.h",
	"/include/python3.4m/methodobject.h",
	"/include/python3.4m/modsupport.h",
	"/include/python3.4m/moduleobject.h",
	"/include/python3.4m/namespaceobject.h",
	"/include/python3.4m/node.h",
	"/include/python3.4m/object.h",
	"/include/python3.4m/objimpl.h",
	"/include/python3.4m/opcode.h",
	"/include/python3.4m/osdefs.h",
	"/include/python3.4m/parsetok.h",
	"/include/python3.4m/patchlevel.h",
	"/include/python3.4m/pgen.h",
	"/include/python3.4m/pgenheaders.h",
	"/include/python3.4m/py_curses.h",
	"/include/python3.4m/pyarena.h",
	"/include/python3.4m/pyatomic.h",
	"/include/python3.4m/pycapsule.h",
	"/include/python3.4m/pyconfig.h",
	"/include/python3.4m/pyctype.h",
	"/include/python3.4m/pydebug.h",
	"/include/python3.4m/pyerrors.h",
	"/include/python3.4m/pyexpat.h",
	"/include/python3.4m/pyfpe.h",
	"/include/python3.4m/pygetopt.h",
	"/include/python3.4m/pyhash.h",
	"/include/python3.4m/pymacconfig.h",
	"/include/python3.4m/pymacro.h",
	"/include/python3.4m/pymath.h",
	"/include/python3.4m/pymem.h",
	"/include/python3.4m/pyport.h",
	"/include/python3.4m/pystate.h",
	"/include/python3.4m/pystrcmp.h",
	"/include/python3.4m/pystrtod.h",
	"/include/python3.4m/pythonrun.h",
	"/include/python3.4m/pythread.h",
	"/include/python3.4m/pytime.h",
	"/include/python3.4m/rangeobject.h",
	"/include/python3.4m/setobject.h",
	"/include/python3.4m/sliceobject.h",
	"/include/python3.4m/structmember.h",
	"/include/python3.4m/structseq.h",
	"/include/python3.4m/symtable.h",
	"/include/python3.4m/sysmodule.h",
	"/include/python3.4m/token.h",
	"/include/python3.4m/traceback.h",
	"/include/python3.4m/tupleobject.h",
	"/include/python3.4m/typeslots.h",
	"/include/python3.4m/ucnhash.h",
	"/include/python3.4m/unicodeobject.h",
	"/include/python3.4m/warnings.h",
	"/include/python3.4m/weakrefobject.h",
	"/lib/python3.4",
	"/lib/python3.4/LICENSE.txt",
	"/lib/python3.4/__future__.py",
	"/lib/python3.4/__phello__.foo.py",
	"/lib/python3.4/_bootlocale.py",
	"/lib/python3.4/_collections_abc.py",
	"/lib/python3.4/_compat_pickle.py",
	"/lib/python3.4/_dummy_thread.py",
	"/lib/python3.4/_markupbase.py",
	"/lib/python3.4/_osx_support.py",
	"/lib/python3.4/_pyio.py",
	"/lib/python3.4/_sitebuiltins.py",
	"/lib/python3.4/_strptime.py",
	"/lib/python3.4/_sysconfigdata.py",
	"/lib/python3.4/_threading_local.py",
	"/lib/python3.4/_weakrefset.py",
	"/lib/python3.4/abc.py",
	"/lib/python3.4/aifc.py",
	"/lib/python3.4/antigravity.py",
	"/lib/python3.4/argparse.py",
	"/lib/python3.4/ast.py",
	"/lib/python3.4/asynchat.py",
	"/lib/python3.4/asyncio",
	"/lib/python3.4/asyncio/__init__.py",
	"/lib/python3.4/asyncio/base_events.py",
	"/lib/python3.4/asyncio/base_subprocess.py",
	"/lib/python3.4/asyncio/constants.py",
	"/lib/python3.4/asyncio/events.py",
	"/lib/python3.4/asyncio/futures.py",
	"/lib/python3.4/asyncio/locks.py",
	"/lib/python3.4/asyncio/log.py",
	"/lib/python3.4/asyncio/proactor_events.py",
	"/lib/python3.4/asyncio/protocols.py",
	"/lib/python3.4/asyncio/queues.py",
	"/lib/python3.4/asyncio/selector_events.py",
	"/lib/python3.4/asyncio/streams.py",
	"/lib/python3.4/asyncio/subprocess.py",
	"/lib/python3.4/asyncio/tasks.py",
	"/lib/python3.4/asyncio/test_utils.py",
	"/lib/python3.4/asyncio/transports.py",
	"/lib/python3.4/asyncio/unix_events.py",
	"/lib/python3.4/asyncio/windows_events.py",
	"/lib/python3.4/asyncio/windows_utils.py",
	"/lib/python3.4/asyncore.py",
	"/lib/python3.4/base64.py",
	"/lib/python3.4/bdb.py",
	"/lib/python3.4/binhex.py",
	"/lib/python3.4/bisect.py",
	"/lib/python3.4/bz2.py",
	"/lib/python3.4/cProfile.py",
	"/lib/python3.4/calendar.py",
	"/lib/python3.4/cgi.py",
	"/lib/python3.4/cgitb.py",
	"/lib/python3.4/chunk.py",
	"/lib/python3.4/cmd.py",
	"/lib/python3.4/code.py",
	"/lib/python3.4/codecs.py",
	"/lib/python3.4/codeop.py",
	"/lib/python3.4/collections",
	"/lib/python3.4/collections/__init__.py",
	"/lib/python3.4/collections/__main__.py",
	"/lib/python3.4/collections/abc.py",
	"/lib/python3.4/colorsys.py",
	"/lib/python3.4/compileall.py",
	"/lib/python3.4/concurrent",
	"/lib/python3.4/concurrent/__init__.py",
	"/lib/python3.4/concurrent/futures",
	"/lib/python3.4/concurrent/futures/__init__.py",
	"/lib/python3.4/concurrent/futures/_base.py",
	"/lib/python3.4/concurrent/futures/process.py",
	"/lib/python3.4/concurrent/futures/thread.py",
	"/lib/python3.4/config-3.4m",
	"/lib/python3.4/config-3.4m/Makefile",
	"/lib/python3.4/config-3.4m/Setup",
	"/lib/python3.4/config-3.4m/Setup.config",
	"/lib/python3.4/config-3.4m/Setup.local",
	"/lib/python3.4/config-3.4m/config.c",
	"/lib/python3.4/config-3.4m/config.c.in",
	"/lib/python3.4/config-3.4m/install-sh",
	"/lib/python3.4/config-3.4m/makesetup",
	"/lib/python3.4/config-3.4m/python-config.py",
	"/lib/python3.4/config-3.4m/python.o",
	"/lib/python3.4/configparser.py",
	"/lib/python3.4/contextlib.py",
	"/lib/python3.4/copy.py",
	"/lib/python3.4/copyreg.py",
	"/lib/python3.4/crypt.py",
	"/lib/python3.4/csv.py",
	"/lib/python3.4/ctypes",
	"/lib/python3.4/ctypes/__init__.py",
	"/lib/python3.4/ctypes/_endian.py",
	"/lib/python3.4/ctypes/macholib",
	"/lib/python3.4/ctypes/macholib/README.ctypes",
	"/lib/python3.4/ctypes/macholib/__init__.py",
	"/lib/python3.4/ctypes/macholib/dyld.py",
	"/lib/python3.4/ctypes/macholib/dylib.py",
	"/lib/python3.4/ctypes/macholib/fetch_macholib",
	"/lib/python3.4/ctypes/macholib/fetch_macholib.bat",
	"/lib/python3.4/ctypes/macholib/framework.py",
	"/lib/python3.4/ctypes/test",
	"/lib/python3.4/ctypes/test/__init__.py",
	"/lib/python3.4/ctypes/test/runtests.py",
	"/lib/python3.4/ctypes/test/test_anon.py",
	"/lib/python3.4/ctypes/test/test_array_in_pointer.py",
	"/lib/python3.4/ctypes/test/test_arrays.py",
	"/lib/python3.4/ctypes/test/test_as_parameter.py",
	"/lib/python3.4/ctypes/test/test_bitfields.py",
	"/lib/python3.4/ctypes/test/test_buffers.py",
	"/lib/python3.4/ctypes/test/test_bytes.py",
	"/lib/python3.4/ctypes/test/test_byteswap.py",
	"/lib/python3.4/ctypes/test/test_callbacks.py",
	"/lib/python3.4/ctypes/test/test_cast.py",
	"/lib/python3.4/ctypes/test/test_cfuncs.py",
	"/lib/python3.4/ctypes/test/test_checkretval.py",
	"/lib/python3.4/ctypes/test/test_delattr.py",
	"/lib/python3.4/ctypes/test/test_errcheck.py",
	"/lib/python3.4/ctypes/test/test_errno.py",
	"/lib/python3.4/ctypes/test/test_find.py",
	"/lib/python3.4/ctypes/test/test_frombuffer.py",
	"/lib/python3.4/ctypes/test/test_funcptr.py",
	"/lib/python3.4/ctypes/test/test_functions.py",
	"/lib/python3.4/ctypes/test/test_incomplete.py",
	"/lib/python3.4/ctypes/test/test_init.py",
	"/lib/python3.4/ctypes/test/test_integers.py",
	"/lib/python3.4/ctypes/test/test_internals.py",
	"/lib/python3.4/ctypes/test/test_keeprefs.py",
	"/lib/python3.4/ctypes/test/test_libc.py",
	"/lib/python3.4/ctypes/test/test_loading.py",
	"/lib/python3.4/ctypes/test/test_macholib.py",
	"/lib/python3.4/ctypes/test/test_memfunctions.py",
	"/lib/python3.4/ctypes/test/test_numbers.py",
	"/lib/python3.4/ctypes/test/test_objects.py",
	"/lib/python3.4/ctypes/test/test_parameters.py",
	"/lib/python3.4/ctypes/test/test_pep3118.py",
	"/lib/python3.4/ctypes/test/test_pickling.py",
	"/lib/python3.4/ctypes/test/test_pointers.py",
	"/lib/python3.4/ctypes/test/test_prototypes.py",
	"/lib/python3.4/ctypes/test/test_python_api.py",
	"/lib/python3.4/ctypes/test/test_random_things.py",
	"/lib/python3.4/ctypes/test/test_refcounts.py",
	"/lib/python3.4/ctypes/test/test_repr.py",
	"/lib/python3.4/ctypes/test/test_returnfuncptrs.py",
	"/lib/python3.4/ctypes/test/test_simplesubclasses.py",
	"/lib/python3.4/ctypes/test/test_sizes.py",
	"/lib/python3.4/ctypes/test/test_slicing.py",
	"/lib/python3.4/ctypes/test/test_stringptr.py",
	"/lib/python3.4/ctypes/test/test_strings.py",
	"/lib/python3.4/ctypes/test/test_struct_fields.py",
	"/lib/python3.4/ctypes/test/test_structures.py",
	"/lib/python3.4/ctypes/test/test_unaligned_structures.py",
	"/lib/python3.4/ctypes/test/test_unicode.py",
	"/lib/python3.4/ctypes/test/test_values.py",
	"/lib/python3.4/ctypes/test/test_varsize_struct.py",
	"/lib/python3.4/ctypes/test/test_win32.py",
	"/lib/python3.4/ctypes/test/test_wintypes.py",
	"/lib/python3.4/ctypes/util.py",
	"/lib/python3.4/ctypes/wintypes.py",
	"/lib/python3.4/curses",
	"/lib/python3.4/curses/__init__.py",
	"/lib/python3.4/curses/ascii.py",
	"/lib/python3.4/curses/has_key.py",
	"/lib/python3.4/curses/panel.py",
	"/lib/python3.4/curses/textpad.py",
	"/lib/python3.4/datetime.py",
	"/lib/python3.4/dbm",
	"/lib/python3.4/dbm/__init__.py",
	"/lib/python3.4/dbm/dumb.py",
	"/lib/python3.4/dbm/gnu.py",
	"/lib/python3.4/dbm/ndbm.py",
	"/lib/python3.4/decimal.py",
	"/lib/python3.4/difflib.py",
	"/lib/python3.4/dis.py",
	"/lib/python3.4/distutils",
	"/lib/python3.4/distutils/README",
	"/lib/python3.4/distutils/__init__.py",
	"/lib/python3.4/distutils/archive_util.py",
	"/lib/python3.4/distutils/bcppcompiler.py",
	"/lib/python3.4/distutils/ccompiler.py",
	"/lib/python3.4/distutils/cmd.py",
	"/lib/python3.4/distutils/command",
	"/lib/python3.4/distutils/command/__init__.py",
	"/lib/python3.4/distutils/command/bdist.py",
	"/lib/python3.4/distutils/command/bdist_dumb.py",
	"/lib/python3.4/distutils/command/bdist_msi.py",
	"/lib/python3.4/distutils/command/bdist_rpm.py",
	"/lib/python3.4/distutils/command/bdist_wininst.py",
	"/lib/python3.4/distutils/command/build.py",
	"/lib/python3.4/distutils/command/build_clib.py",
	"/lib/python3.4/distutils/command/build_ext.py",
	"/lib/python3.4/distutils/command/build_py.py",
	"/lib/python3.4/distutils/command/build_scripts.py",
	"/lib/python3.4/distutils/command/check.py",
	"/lib/python3.4/distutils/command/clean.py",
	"/lib/python3.4/distutils/command/command_template",
	"/lib/python3.4/distutils/command/config.py",
	"/lib/python3.4/distutils/command/install.py",
	"/lib/python3.4/distutils/command/install_data.py",
	"/lib/python3.4/distutils/command/install_egg_info.py",
	"/lib/python3.4/distutils/command/install_headers.py",
	"/lib/python3.4/distutils/command/install_lib.py",
	"/lib/python3.4/distutils/command/install_scripts.py",
	"/lib/python3.4/distutils/command/register.py",
	"/lib/python3.4/distutils/command/sdist.py",
	"/lib/python3.4/distutils/command/upload.py",
	"/lib/python3.4/distutils/config.py",
	"/lib/python3.4/distutils/core.py",
	"/lib/python3.4/distutils/cygwinccompiler.py",
	"/lib/python3.4/distutils/debug.py",
	"/lib/python3.4/distutils/dep_util.py",
	"/lib/python3.4/distutils/dir_util.py",
	"/lib/python3.4/distutils/dist.py",
	"/lib/python3.4/distutils/errors.py",
	"/lib/python3.4/distutils/extension.py",
	"/lib/python3.4/distutils/fancy_getopt.py",
	"/lib/python3.4/distutils/file_util.py",
	"/lib/python3.4/distutils/filelist.py",
	"/lib/python3.4/distutils/log.py",
	"/lib/python3.4/distutils/msvc9compiler.py",
	"/lib/python3.4/distutils/msvccompiler.py",
	"/lib/python3.4/distutils/spawn.py",
	"/lib/python3.4/distutils/sysconfig.py",
	"/lib/python3.4/distutils/tests",
	"/lib/python3.4/distutils/tests/Setup.sample",
	"/lib/python3.4/distutils/tests/__init__.py",
	"/lib/python3.4/distutils/tests/support.py",
	"/lib/python3.4/distutils/tests/test_archive_util.py",
	"/lib/python3.4/distutils/tests/test_bdist.py",
	"/lib/python3.4/distutils/tests/test_bdist_dumb.py",
	"/lib/python3.4/distutils/tests/test_bdist_msi.py",
	"/lib/python3.4/distutils/tests/test_bdist_rpm.py",
	"/lib/python3.4/distutils/tests/test_bdist_wininst.py",
	"/lib/python3.4/distutils/tests/test_build.py",
	"/lib/python3.4/distutils/tests/test_build_clib.py",
	"/lib/python3.4/distutils/tests/test_build_ext.py",
	"/lib/python3.4/distutils/tests/test_build_py.py",
	"/lib/python3.4/distutils/tests/test_build_scripts.py",
	"/lib/python3.4/distutils/tests/test_check.py",
	"/lib/python3.4/distutils/tests/test_clean.py",
	"/lib/python3.4/distutils/tests/test_cmd.py",
	"/lib/python3.4/distutils/tests/test_config.py",
	"/lib/python3.4/distutils/tests/test_config_cmd.py",
	"/lib/python3.4/distutils/tests/test_core.py",
	"/lib/python3.4/distutils/tests/test_cygwinccompiler.py",
	"/lib/python3.4/distutils/tests/test_dep_util.py",
	"/lib/python3.4/distutils/tests/test_dir_util.py",
	"/lib/python3.4/distutils/tests/test_dist.py",
	"/lib/python3.4/distutils/tests/test_extension.py",
	"/lib/python3.4/distutils/tests/test_file_util.py",
	"/lib/python3.4/distutils/tests/test_filelist.py",
	"/lib/python3.4/distutils/tests/test_install.py",
	"/lib/python3.4/distutils/tests/test_install_data.py",
	"/lib/python3.4/distutils/tests/test_install_headers.py",
	"/lib/python3.4/distutils/tests/test_install_lib.py",
	"/lib/python3.4/distutils/tests/test_install_scripts.py",
	"/lib/python3.4/distutils/tests/test_log.py",
	"/lib/python3.4/distutils/tests/test_msvc9compiler.py",
	"/lib/python3.4/distutils/tests/test_register.py",
	"/lib/python3.4/distutils/tests/test_sdist.py",
	"/lib/python3.4/distutils/tests/test_spawn.py",
	"/lib/python3.4/distutils/tests/test_sysconfig.py",
	"/lib/python3.4/distutils/tests/test_text_file.py",
	"/lib/python3.4/distutils/tests/test_unixccompiler.py",
	"/lib/python3.4/distutils/tests/test_upload.py",
	"/lib/python3.4/distutils/tests/test_util.py",
	"/lib/python3.4/distutils/tests/test_version.py",
	"/lib/python3.4/distutils/tests/test_versionpredicate.py",
	"/lib/python3.4/distutils/tests/xxmodule.c",
	"/lib/python3.4/distutils/text_file.py",
	"/lib/python3.4/distutils/unixccompiler.py",
	"/lib/python3.4/distutils/util.py",
	"/lib/python3.4/distutils/version.py",
	"/lib/python3.4/distutils/versionpredicate.py",
	"/lib/python3.4/doctest.py",
	"/lib/python3.4/dummy_threading.py",
	"/lib/python3.4/email",
	"/lib/python3.4/email/__init__.py",
	"/lib/python3.4/email/_encoded_words.py",
	"/lib/python3.4/email/_header_value_parser.py",
	"/lib/python3.4/email/_parseaddr.py",
	"/lib/python3.4/email/_policybase.py",
	"/lib/python3.4/email/architecture.rst",
	"/lib/python3.4/email/base64mime.py",
	"/lib/python3.4/email/charset.py",
	"/lib/python3.4/email/contentmanager.py",
	"/lib/python3.4/email/encoders.py",
	"/lib/python3.4/email/errors.py",
	"/lib/python3.4/email/feedparser.py",
	"/lib/python3.4/email/generator.py",
	"/lib/python3.4/email/header.py",
	"/lib/python3.4/email/headerregistry.py",
	"/lib/python3.4/email/iterators.py",
	"/lib/python3.4/email/message.py",
	"/lib/python3.4/email/mime",
	"/lib/python3.4/email/mime/__init__.py",
	"/lib/python3.4/email/mime/application.py",
	"/lib/python3.4/email/mime/audio.py",
	"/lib/python3.4/email/mime/base.py",
	"/lib/python3.4/email/mime/image.py",
	"/lib/python3.4/email/mime/message.py",
	"/lib/python3.4/email/mime/multipart.py",
	"/lib/python3.4/email/mime/nonmultipart.py",
	"/lib/python3.4/email/mime/text.py",
	"/lib/python3.4/email/parser.py",
	"/lib/python3.4/email/policy.py",
	"/lib/python3.4/email/quoprimime.py",
	"/lib/python3.4/email/utils.py",
	"/lib/python3.4/encodings",
	"/lib/python3.4/encodings/__init__.py",
	"/lib/python3.4/encodings/aliases.py",
	"/lib/python3.4/encodings/ascii.py",
	"/lib/python3.4/encodings/base64_codec.py",
	"/lib/python3.4/encodings/big5.py",
	"/lib/python3.4/encodings/big5hkscs.py",
	"/lib/python3.4/encodings/bz2_codec.py",
	"/lib/python3.4/encodings/charmap.py",
	"/lib/python3.4/encodings/cp037.py",
	"/lib/python3.4/encodings/cp1006.py",
	"/lib/python3.4/encodings/cp1026.py",
	"/lib/python3.4/encodings/cp1125.py",
	"/lib/python3.4/encodings/cp1140.py",
	"/lib/python3.4/encodings/cp1250.py",
	"/lib/python3.4/encodings/cp1251.py",
	"/lib/python3.4/encodings/cp1252.py",
	"/lib/python3.4/encodings/cp1253.py",
	"/lib/python3.4/encodings/cp1254.py",
	"/lib/python3.4/encodings/cp1255.py",
	"/lib/python3.4/encodings/cp1256.py",
	"/lib/python3.4/encodings/cp1257.py",
	"/lib/python3.4/encodings/cp1258.py",
	"/lib/python3.4/encodings/cp273.py",
	"/lib/python3.4/encodings/cp424.py",
	"/lib/python3.4/encodings/cp437.py",
	"/lib/python3.4/encodings/cp500.py",
	"/lib/python3.4/encodings/cp65001.py",
	"/lib/python3.4/encodings/cp720.py",
	"/lib/python3.4/encodings/cp737.py",
	"/lib/python3.4/encodings/cp775.py",
	"/lib/python3.4/encodings/cp850.py",
	"/lib/python3.4/encodings/cp852.py",
	"/lib/python3.4/encodings/cp855.py",
	"/lib/python3.4/encodings/cp856.py",
	"/lib/python3.4/encodings/cp857.py",
	"/lib/python3.4/encodings/cp858.py",
	"/lib/python3.4/encodings/cp860.py",
	"/lib/python3.4/encodings/cp861.py",
	"/lib/python3.4/encodings/cp862.py",
	"/lib/python3.4/encodings/cp863.py",
	"/lib/python3.4/encodings/cp864.py",
	"/lib/python3.4/encodings/cp865.py",
	"/lib/python3.4/encodings/cp866.py",
	"/lib/python3.4/encodings/cp869.py",
	"/lib/python3.4/encodings/cp874.py",
	"/lib/python3.4/encodings/cp875.py",
	"/lib/python3.4/encodings/cp932.py",
	"/lib/python3.4/encodings/cp949.py",
	"/lib/python3.4/encodings/cp950.py",
	"/lib/python3.4/encodings/euc_jis_2004.py",
	"/lib/python3.4/encodings/euc_jisx0213.py",
	"/lib/python3.4/encodings/euc_jp.py",
	"/lib/python3.4/encodings/euc_kr.py",
	"/lib/python3.4/encodings/gb18030.py",
	"/lib/python3.4/encodings/gb2312.py",
	"/lib/python3.4/encodings/gbk.py",
	"/lib/python3.4/encodings/hex_codec.py",
	"/lib/python3.4/encodings/hp_roman8.py",
	"/lib/python3.4/encodings/hz.py",
	"/lib/python3.4/encodings/idna.py",
	"/lib/python3.4/encodings/iso2022_jp.py",
	"/lib/python3.4/encodings/iso2022_jp_1.py",
	"/lib/python3.4/encodings/iso2022_jp_2.py",
	"/lib/python3.4/encodings/iso2022_jp_2004.py",
	"/lib/python3.4/encodings/iso2022_jp_3.py",
	"/lib/python3.4/encodings/iso2022_jp_ext.py",
	"/lib/python3.4/encodings/iso2022_kr.py",
	"/lib/python3.4/encodings/iso8859_1.py",
	"/lib/python3.4/encodings/iso8859_10.py",
	"/lib/python3.4/encodings/iso8859_11.py",
	"/lib/python3.4/encodings/iso8859_13.py",
	"/lib/python3.4/encodings/iso8859_14.py",
	"/lib/python3.4/encodings/iso8859_15.py",
	"/lib/python3.4/encodings/iso8859_16.py",
	"/lib/python3.4/encodings/iso8859_2.py",
	"/lib/python3.4/encodings/iso8859_3.py",
	"/lib/python3.4/encodings/iso8859_4.py",
	"/lib/python3.4/encodings/iso8859_5.py",
	"/lib/python3.4/encodings/iso8859_6.py",
	"/lib/python3.4/encodings/iso8859_7.py",
	"/lib/python3.4/encodings/iso8859_8.py",
	"/lib/python3.4/encodings/iso8859_9.py",
	"/lib/python3.4/encodings/johab.py",
	"/lib/python3.4/encodings/koi8_r.py",
	"/lib/python3.4/encodings/koi8_u.py",
	"/lib/python3.4/encodings/latin_1.py",
	"/lib/python3.4/encodings/mac_arabic.py",
	"/lib/python3.4/encodings/mac_centeuro.py",
	"/lib/python3.4/encodings/mac_croatian.py",
	"/lib/python3.4/encodings/mac_cyrillic.py",
	"/lib/python3.4/encodings/mac_farsi.py",
	"/lib/python3.4/encodings/mac_greek.py",
	"/lib/python3.4/encodings/mac_iceland.py",
	"/lib/python3.4/encodings/mac_latin2.py",
	"/lib/python3.4/encodings/mac_roman.py",
	"/lib/python3.4/encodings/mac_romanian.py",
	"/lib/python3.4/encodings/mac_turkish.py",
	"/lib/python3.4/encodings/mbcs.py",
	"/lib/python3.4/encodings/palmos.py",
	"/lib/python3.4/encodings/ptcp154.py",
	"/lib/python3.4/encodings/punycode.py",
	"/lib/python3.4/encodings/quopri_codec.py",
	"/lib/python3.4/encodings/raw_unicode_escape.py",
	"/lib/python3.4/encodings/rot_13.py",
	"/lib/python3.4/encodings/shift_jis.py",
	"/lib/python3.4/encodings/shift_jis_2004.py",
	"/lib/python3.4/encodings/shift_jisx0213.py",
	"/lib/python3.4/encodings/tis_620.py",
	"/lib/python3.4/encodings/undefined.py",
	"/lib/python3.4/encodings/unicode_escape.py",
	"/lib/python3.4/encodings/unicode_internal.py",
	"/lib/python3.4/encodings/utf_16.py",
	"/lib/python3.4/encodings/utf_16_be.py",
	"/lib/python3.4/encodings/utf_16_le.py",
	"/lib/python3.4/encodings/utf_32.py",
	"/lib/python3.4/encodings/utf_32_be.py",
	"/lib/python3.4/encodings/utf_32_le.py",
	"/lib/python3.4/encodings/utf_7.py",
	"/lib/python3.4/encodings/utf_8.py",
	"/lib/python3.4/encodings/utf_8_sig.py",
	"/lib/python3.4/encodings/uu_codec.py",
	"/lib/python3.4/encodings/zlib_codec.py",
	"/lib/python3.4/ensurepip",
	"/lib/python3.4/ensurepip/__init__.py",
	"/lib/python3.4/ensurepip/__main__.py",
	"/lib/python3.4/ensurepip/_uninstall.py",
	"/lib/python3.4/enum.py",
	"/lib/python3.4/filecmp.py",
	"/lib/python3.4/fileinput.py",
	"/lib/python3.4/fnmatch.py",
	"/lib/python3.4/formatter.py",
	"/lib/python3.4/fractions.py",
	"/lib/python3.4/ftplib.py",
	"/lib/python3.4/functools.py",
	"/lib/python3.4/genericpath.py",
	"/lib/python3.4/getopt.py",
	"/lib/python3.4/getpass.py",
	"/lib/python3.4/gettext.py",
	"/lib/python3.4/glob.py",
	"/lib/python3.4/gzip.py",
	"/lib/python3.4/hashlib.py",
	"/lib/python3.4/heapq.py",
	"/lib/python3.4/hmac.py",
	"/lib/python3.4/html",
	"/lib/python3.4/html/__init__.py",
	"/lib/python3.4/html/entities.py",
	"/lib/python3.4/html/parser.py",
	"/lib/python3.4/http",
	"/lib/python3.4/http/__init__.py",
	"/lib/python3.4/http/client.py",
	"/lib/python3.4/http/cookiejar.py",
	"/lib/python3.4/http/cookies.py",
	"/lib/python3.4/http/server.py",
	"/lib/python3.4/idlelib",
	"/lib/python3.4/idlelib/AutoComplete.py",
	"/lib/python3.4/idlelib/AutoCompleteWindow.py",
	"/lib/python3.4/idlelib/AutoExpand.py",
	"/lib/python3.4/idlelib/Bindings.py",
	"/lib/python3.4/idlelib/CREDITS.txt",
	"/lib/python3.4/idlelib/CallTipWindow.py",
	"/lib/python3.4/idlelib/CallTips.py",
	"/lib/python3.4/idlelib/ChangeLog",
	"/lib/python3.4/idlelib/ClassBrowser.py",
	"/lib/python3.4/idlelib/CodeContext.py",
	"/lib/python3.4/idlelib/ColorDelegator.py",
	"/lib/python3.4/idlelib/Debugger.py",
	"/lib/python3.4/idlelib/Delegator.py",
	"/lib/python3.4/idlelib/EditorWindow.py",
	"/lib/python3.4/idlelib/FileList.py",
	"/lib/python3.4/idlelib/FormatParagraph.py",
	"/lib/python3.4/idlelib/GrepDialog.py",
	"/lib/python3.4/idlelib/HISTORY.txt",
	"/lib/python3.4/idlelib/HyperParser.py",
	"/lib/python3.4/idlelib/IOBinding.py",
	"/lib/python3.4/idlelib/Icons",
	"/lib/python3.4/idlelib/Icons/folder.gif",
	"/lib/python3.4/idlelib/Icons/idle.icns",
	"/lib/python3.4/idlelib/Icons/idle.ico",
	"/lib/python3.4/idlelib/Icons/idle_16.gif",
	"/lib/python3.4/idlelib/Icons/idle_16.png",
	"/lib/python3.4/idlelib/Icons/idle_32.gif",
	"/lib/python3.4/idlelib/Icons/idle_32.png",
	"/lib/python3.4/idlelib/Icons/idle_48.gif",
	"/lib/python3.4/idlelib/Icons/idle_48.png",
	"/lib/python3.4/idlelib/Icons/minusnode.gif",
	"/lib/python3.4/idlelib/Icons/openfolder.gif",
	"/lib/python3.4/idlelib/Icons/plusnode.gif",
	"/lib/python3.4/idlelib/Icons/python.gif",
	"/lib/python3.4/idlelib/Icons/tk.gif",
	"/lib/python3.4/idlelib/IdleHistory.py",
	"/lib/python3.4/idlelib/MultiCall.py",
	"/lib/python3.4/idlelib/MultiStatusBar.py",
	"/lib/python3.4/idlelib/NEWS.txt",
	"/lib/python3.4/idlelib/ObjectBrowser.py",
	"/lib/python3.4/idlelib/OutputWindow.py",
	"/lib/python3.4/idlelib/ParenMatch.py",
	"/lib/python3.4/idlelib/PathBrowser.py",
	"/lib/python3.4/idlelib/Percolator.py",
	"/lib/python3.4/idlelib/PyParse.py",
	"/lib/python3.4/idlelib/PyShell.py",
	"/lib/python3.4/idlelib/README.txt",
	"/lib/python3.4/idlelib/RemoteDebugger.py",
	"/lib/python3.4/idlelib/RemoteObjectBrowser.py",
	"/lib/python3.4/idlelib/ReplaceDialog.py",
	"/lib/python3.4/idlelib/RstripExtension.py",
	"/lib/python3.4/idlelib/ScriptBinding.py",
	"/lib/python3.4/idlelib/ScrolledList.py",
	"/lib/python3.4/idlelib/SearchDialog.py",
	"/lib/python3.4/idlelib/SearchDialogBase.py",
	"/lib/python3.4/idlelib/SearchEngine.py",
	"/lib/python3.4/idlelib/StackViewer.py",
	"/lib/python3.4/idlelib/TODO.txt",
	"/lib/python3.4/idlelib/ToolTip.py",
	"/lib/python3.4/idlelib/TreeWidget.py",
	"/lib/python3.4/idlelib/UndoDelegator.py",
	"/lib/python3.4/idlelib/WidgetRedirector.py",
	"/lib/python3.4/idlelib/WindowList.py",
	"/lib/python3.4/idlelib/ZoomHeight.py",
	"/lib/python3.4/idlelib/__init__.py",
	"/lib/python3.4/idlelib/__main__.py",
	"/lib/python3.4/idlelib/aboutDialog.py",
	"/lib/python3.4/idlelib/config-extensions.def",
	"/lib/python3.4/idlelib/config-highlight.def",
	"/lib/python3.4/idlelib/config-keys.def",
	"/lib/python3.4/idlelib/config-main.def",
	"/lib/python3.4/idlelib/configDialog.py",
	"/lib/python3.4/idlelib/configHandler.py",
	"/lib/python3.4/idlelib/configHelpSourceEdit.py",
	"/lib/python3.4/idlelib/configSectionNameDialog.py",
	"/lib/python3.4/idlelib/dynOptionMenuWidget.py",
	"/lib/python3.4/idlelib/extend.txt",
	"/lib/python3.4/idlelib/help.txt",
	"/lib/python3.4/idlelib/idle.bat",
	"/lib/python3.4/idlelib/idle.py",
	"/lib/python3.4/idlelib/idle.pyw",
	"/lib/python3.4/idlelib/idle_test",
	"/lib/python3.4/idlelib/idle_test/README.txt",
	"/lib/python3.4/idlelib/idle_test/__init__.py",
	"/lib/python3.4/idlelib/idle_test/mock_idle.py",
	"/lib/python3.4/idlelib/idle_test/mock_tk.py",
	"/lib/python3.4/idlelib/idle_test/test_calltips.py",
	"/lib/python3.4/idlelib/idle_test/test_config_name.py",
	"/lib/python3.4/idlelib/idle_test/test_delegator.py",
	"/lib/python3.4/idlelib/idle_test/test_formatparagraph.py",
	"/lib/python3.4/idlelib/idle_test/test_grep.py",
	"/lib/python3.4/idlelib/idle_test/test_idlehistory.py",
	"/lib/python3.4/idlelib/idle_test/test_pathbrowser.py",
	"/lib/python3.4/idlelib/idle_test/test_rstrip.py",
	"/lib/python3.4/idlelib/idle_test/test_searchengine.py",
	"/lib/python3.4/idlelib/idle_test/test_text.py",
	"/lib/python3.4/idlelib/idle_test/test_warning.py",
	"/lib/python3.4/idlelib/idlever.py",
	"/lib/python3.4/idlelib/keybindingDialog.py",
	"/lib/python3.4/idlelib/macosxSupport.py",
	"/lib/python3.4/idlelib/rpc.py",
	"/lib/python3.4/idlelib/run.py",
	"/lib/python3.4/idlelib/tabbedpages.py",
	"/lib/python3.4/idlelib/testcode.py",
	"/lib/python3.4/idlelib/textView.py",
	"/lib/python3.4/imaplib.py",
	"/lib/python3.4/imghdr.py",
	"/lib/python3.4/imp.py",
	"/lib/python3.4/importlib",
	"/lib/python3.4/importlib/__init__.py",
	"/lib/python3.4/importlib/_bootstrap.py",
	"/lib/python3.4/importlib/abc.py",
	"/lib/python3.4/importlib/machinery.py",
	"/lib/python3.4/importlib/util.py",
	"/lib/python3.4/inspect.py",
	"/lib/python3.4/io.py",
	"/lib/python3.4/ipaddress.py",
	"/lib/python3.4/json",
	"/lib/python3.4/json/__init__.py",
	"/lib/python3.4/json/decoder.py",
	"/lib/python3.4/json/encoder.py",
	"/lib/python3.4/json/scanner.py",
	"/lib/python3.4/json/tool.py",
	"/lib/python3.4/keyword.py",
	"/lib/python3.4/lib-dynload",
	"/lib/python3.4/lib2to3",
	"/lib/python3.4/lib2to3/Grammar.txt",
	"/lib/python3.4/lib2to3/Grammar3.4.3.final.0.pickle",
	"/lib/python3.4/lib2to3/PatternGrammar.txt",
	"/lib/python3.4/lib2to3/PatternGrammar3.4.3.final.0.pickle",
	"/lib/python3.4/lib2to3/__init__.py",
	"/lib/python3.4/lib2to3/__main__.py",
	"/lib/python3.4/lib2to3/btm_matcher.py",
	"/lib/python3.4/lib2to3/btm_utils.py",
	"/lib/python3.4/lib2to3/fixer_base.py",
	"/lib/python3.4/lib2to3/fixer_util.py",
	"/lib/python3.4/lib2to3/fixes",
	"/lib/python3.4/lib2to3/fixes/__init__.py",
	"/lib/python3.4/lib2to3/fixes/fix_apply.py",
	"/lib/python3.4/lib2to3/fixes/fix_asserts.py",
	"/lib/python3.4/lib2to3/fixes/fix_basestring.py",
	"/lib/python3.4/lib2to3/fixes/fix_buffer.py",
	"/lib/python3.4/lib2to3/fixes/fix_callable.py",
	"/lib/python3.4/lib2to3/fixes/fix_dict.py",
	"/lib/python3.4/lib2to3/fixes/fix_except.py",
	"/lib/python3.4/lib2to3/fixes/fix_exec.py",
	"/lib/python3.4/lib2to3/fixes/fix_execfile.py",
	"/lib/python3.4/lib2to3/fixes/fix_exitfunc.py",
	"/lib/python3.4/lib2to3/fixes/fix_filter.py",
	"/lib/python3.4/lib2to3/fixes/fix_funcattrs.py",
	"/lib/python3.4/lib2to3/fixes/fix_future.py",
	"/lib/python3.4/lib2to3/fixes/fix_getcwdu.py",
	"/lib/python3.4/lib2to3/fixes/fix_has_key.py",
	"/lib/python3.4/lib2to3/fixes/fix_idioms.py",
	"/lib/python3.4/lib2to3/fixes/fix_import.py",
	"/lib/python3.4/lib2to3/fixes/fix_imports.py",
	"/lib/python3.4/lib2to3/fixes/fix_imports2.py",
	"/lib/python3.4/lib2to3/fixes/fix_input.py",
	"/lib/python3.4/lib2to3/fixes/fix_intern.py",
	"/lib/python3.4/lib2to3/fixes/fix_isinstance.py",
	"/lib/python3.4/lib2to3/fixes/fix_itertools.py",
	"/lib/python3.4/lib2to3/fixes/fix_itertools_imports.py",
	"/lib/python3.4/lib2to3/fixes/fix_long.py",
	"/lib/python3.4/lib2to3/fixes/fix_map.py",
	"/lib/python3.4/lib2to3/fixes/fix_metaclass.py",
	"/lib/python3.4/lib2to3/fixes/fix_methodattrs.py",
	"/lib/python3.4/lib2to3/fixes/fix_ne.py",
	"/lib/python3.4/lib2to3/fixes/fix_next.py",
	"/lib/python3.4/lib2to3/fixes/fix_nonzero.py",
	"/lib/python3.4/lib2to3/fixes/fix_numliterals.py",
	"/lib/python3.4/lib2to3/fixes/fix_operator.py",
	"/lib/python3.4/lib2to3/fixes/fix_paren.py",
	"/lib/python3.4/lib2to3/fixes/fix_print.py",
	"/lib/python3.4/lib2to3/fixes/fix_raise.py",
	"/lib/python3.4/lib2to3/fixes/fix_raw_input.py",
	"/lib/python3.4/lib2to3/fixes/fix_reduce.py",
	"/lib/python3.4/lib2to3/fixes/fix_reload.py",
	"/lib/python3.4/lib2to3/fixes/fix_renames.py",
	"/lib/python3.4/lib2to3/fixes/fix_repr.py",
	"/lib/python3.4/lib2to3/fixes/fix_set_literal.py",
	"/lib/python3.4/lib2to3/fixes/fix_standarderror.py",
	"/lib/python3.4/lib2to3/fixes/fix_sys_exc.py",
	"/lib/python3.4/lib2to3/fixes/fix_throw.py",
	"/lib/python3.4/lib2to3/fixes/fix_tuple_params.py",
	"/lib/python3.4/lib2to3/fixes/fix_types.py",
	"/lib/python3.4/lib2to3/fixes/fix_unicode.py",
	"/lib/python3.4/lib2to3/fixes/fix_urllib.py",
	"/lib/python3.4/lib2to3/fixes/fix_ws_comma.py",
	"/lib/python3.4/lib2to3/fixes/fix_xrange.py",
	"/lib/python3.4/lib2to3/fixes/fix_xreadlines.py",
	"/lib/python3.4/lib2to3/fixes/fix_zip.py",
	"/lib/python3.4/lib2to3/main.py",
	"/lib/python3.4/lib2to3/patcomp.py",
	"/lib/python3.4/lib2to3/pgen2",
	"/lib/python3.4/lib2to3/pgen2/__init__.py",
	"/lib/python3.4/lib2to3/pgen2/conv.py",
	"/lib/python3.4/lib2to3/pgen2/driver.py",
	"/lib/python3.4/lib2to3/pgen2/grammar.py",
	"/lib/python3.4/lib2to3/pgen2/literals.py",
	"/lib/python3.4/lib2to3/pgen2/parse.py",
	"/lib/python3.4/lib2to3/pgen2/pgen.py",
	"/lib/python3.4/lib2to3/pgen2/token.py",
	"/lib/python3.4/lib2to3/pgen2/tokenize.py",
	"/lib/python3.4/lib2to3/pygram.py",
	"/lib/python3.4/lib2to3/pytree.py",
	"/lib/python3.4/lib2to3/refactor.py",
	"/lib/python3.4/lib2to3/tests",
	"/lib/python3.4/lib2to3/tests/__init__.py",
	"/lib/python3.4/lib2to3/tests/data",
	"/lib/python3.4/lib2to3/tests/data/README",
	"/lib/python3.4/lib2to3/tests/data/bom.py",
	"/lib/python3.4/lib2to3/tests/data/crlf.py",
	"/lib/python3.4/lib2to3/tests/data/different_encoding.py",
	"/lib/python3.4/lib2to3/tests/data/false_encoding.py",
	"/lib/python3.4/lib2to3/tests/data/fixers",
	"/lib/python3.4/lib2to3/tests/data/fixers/bad_order.py",
	"/lib/python3.4/lib2to3/tests/data/fixers/myfixes",
	"/lib/python3.4/lib2to3/tests/data/fixers/myfixes/__init__.py",
	"/lib/python3.4/lib2to3/tests/data/fixers/myfixes/fix_explicit.py",
	"/lib/python3.4/lib2to3/tests/data/fixers/myfixes/fix_first.py",
	"/lib/python3.4/lib2to3/tests/data/fixers/myfixes/fix_last.py",
	"/lib/python3.4/lib2to3/tests/data/fixers/myfixes/fix_parrot.py",
	"/lib/python3.4/lib2to3/tests/data/fixers/myfixes/fix_preorder.py",
	"/lib/python3.4/lib2to3/tests/data/fixers/no_fixer_cls.py",
	"/lib/python3.4/lib2to3/tests/data/fixers/parrot_example.py",
	"/lib/python3.4/lib2to3/tests/data/infinite_recursion.py",
	"/lib/python3.4/lib2to3/tests/data/py2_test_grammar.py",
	"/lib/python3.4/lib2to3/tests/data/py3_test_grammar.py",
	"/lib/python3.4/lib2to3/tests/pytree_idempotency.py",
	"/lib/python3.4/lib2to3/tests/support.py",
	"/lib/python3.4/lib2to3/tests/test_all_fixers.py",
	"/lib/python3.4/lib2to3/tests/test_fixers.py",
	"/lib/python3.4/lib2to3/tests/test_main.py",
	"/lib/python3.4/lib2to3/tests/test_parser.py",
	"/lib/python3.4/lib2to3/tests/test_pytree.py",
	"/lib/python3.4/lib2to3/tests/test_refactor.py",
	"/lib/python3.4/lib2to3/tests/test_util.py",
	"/lib/python3.4/linecache.py",
	"/lib/python3.4/locale.py",
	"/lib/python3.4/logging",
	"/lib/python3.4/logging/__init__.py",
	"/lib/python3.4/logging/config.py",
	"/lib/python3.4/logging/handlers.py",
	"/lib/python3.4/lzma.py",
	"/lib/python3.4/macpath.py",
	"/lib/python3.4/macurl2path.py",
	"/lib/python3.4/mailbox.py",
	"/lib/python3.4/mailcap.py",
	"/lib/python3.4/mimetypes.py",
	"/lib/python3.4/modulefinder.py",
	"/lib/python3.4/multiprocessing",
	"/lib/python3.4/multiprocessing/__init__.py",
	"/lib/python3.4/multiprocessing/connection.py",
	"/lib/python3.4/multiprocessing/context.py",
	"/lib/python3.4/multiprocessing/dummy",
	"/lib/python3.4/multiprocessing/dummy/__init__.py",
	"/lib/python3.4/multiprocessing/dummy/connection.py",
	"/lib/python3.4/multiprocessing/forkserver.py",
	"/lib/python3.4/multiprocessing/heap.py",
	"/lib/python3.4/multiprocessing/managers.py",
	"/lib/python3.4/multiprocessing/pool.py",
	"/lib/python3.4/multiprocessing/popen_fork.py",
	"/lib/python3.4/multiprocessing/popen_forkserver.py",
	"/lib/python3.4/multiprocessing/popen_spawn_posix.py",
	"/lib/python3.4/multiprocessing/popen_spawn_win32.py",
	"/lib/python3.4/multiprocessing/process.py",
	"/lib/python3.4/multiprocessing/queues.py",
	"/lib/python3.4/multiprocessing/reduction.py",
	"/lib/python3.4/multiprocessing/resource_sharer.py",
	"/lib/python3.4/multiprocessing/semaphore_tracker.py",
	"/lib/python3.4/multiprocessing/sharedctypes.py",
	"/lib/python3.4/multiprocessing/spawn.py",
	"/lib/python3.4/multiprocessing/synchronize.py",
	"/lib/python3.4/multiprocessing/util.py",
	"/lib/python3.4/netrc.py",
	"/lib/python3.4/nntplib.py",
	"/lib/python3.4/ntpath.py",
	"/lib/python3.4/nturl2path.py",
	"/lib/python3.4/numbers.py",
	"/lib/python3.4/opcode.py",
	"/lib/python3.4/operator.py",
	"/lib/python3.4/optparse.py",
	"/lib/python3.4/os.py",
	"/lib/python3.4/pathlib.py",
	"/lib/python3.4/pdb.py",
	"/lib/python3.4/pickle.py",
	"/lib/python3.4/pickletools.py",
	"/lib/python3.4/pipes.py",
	"/lib/python3.4/pkgutil.py",
	"/lib/python3.4/plat-sortix",
	"/lib/python3.4/plat-sortix/IN.py",
	"/lib/python3.4/plat-sortix/regen",
	"/lib/python3.4/platform.py",
	"/lib/python3.4/plistlib.py",
	"/lib/python3.4/poplib.py",
	"/lib/python3.4/posixpath.py",
	"/lib/python3.4/pprint.py",
	"/lib/python3.4/profile.py",
	"/lib/python3.4/pstats.py",
	"/lib/python3.4/pty.py",
	"/lib/python3.4/py_compile.py",
	"/lib/python3.4/pyclbr.py",
	"/lib/python3.4/pydoc.py",
	"/lib/python3.4/pydoc_data",
	"/lib/python3.4/pydoc_data/__init__.py",
	"/lib/python3.4/pydoc_data/_pydoc.css",
	"/lib/python3.4/pydoc_data/topics.py",
	"/lib/python3.4/queue.py",
	"/lib/python3.4/quopri.py",
	"/lib/python3.4/random.py",
	"/lib/python3.4/re.py",
	"/lib/python3.4/reprlib.py",
	"/lib/python3.4/rlcompleter.py",
	"/lib/python3.4/runpy.py",
	"/lib/python3.4/sched.py",
	"/lib/python3.4/selectors.py",
	"/lib/python3.4/shelve.py",
	"/lib/python3.4/shlex.py",
	"/lib/python3.4/shutil.py",
	"/lib/python3.4/site-packages",
	"/lib/python3.4/site-packages/README",
	"/lib/python3.4/site.py",
	"/lib/python3.4/smtpd.py",
	"/lib/python3.4/smtplib.py",
	"/lib/python3.4/sndhdr.py",
	"/lib/python3.4/socket.py",
	"/lib/python3.4/socketserver.py",
	"/lib/python3.4/sqlite3",
	"/lib/python3.4/sqlite3/__init__.py",
	"/lib/python3.4/sqlite3/dbapi2.py",
	"/lib/python3.4/sqlite3/dump.py",
	"/lib/python3.4/sqlite3/test",
	"/lib/python3.4/sqlite3/test/__init__.py",
	"/lib/python3.4/sqlite3/test/dbapi.py",
	"/lib/python3.4/sqlite3/test/dump.py",
	"/lib/python3.4/sqlite3/test/factory.py",
	"/lib/python3.4/sqlite3/test/hooks.py",
	"/lib/python3.4/sqlite3/test/regression.py",
	"/lib/python3.4/sqlite3/test/transactions.py",
	"/lib/python3.4/sqlite3/test/types.py",
	"/lib/python3.4/sqlite3/test/userfunctions.py",
	"/lib/python3.4/sre_compile.py",
	"/lib/python3.4/sre_constants.py",
	"/lib/python3.4/sre_parse.py",
	"/lib/python3.4/ssl.py",
	"/lib/python3.4/stat.py",
	"/lib/python3.4/statistics.py",
	"/lib/python3.4/string.py",
	"/lib/python3.4/stringprep.py",
	"/lib/python3.4/struct.py",
	"/lib/python3.4/subprocess.py",
	"/lib/python3.4/sunau.py",
	"/lib/python3.4/symbol.py",
	"/lib/python3.4/symtable.py",
	"/lib/python3.4/sysconfig.py",
	"/lib/python3.4/tabnanny.py",
	"/lib/python3.4/tarfile.py",
	"/lib/python3.4/telnetlib.py",
	"/lib/python3.4/tempfile.py",
	"/lib/python3.4/textwrap.py",
	"/lib/python3.4/this.py",
	"/lib/python3.4/threading.py",
	"/lib/python3.4/timeit.py",
	"/lib/python3.4/tkinter",
	"/lib/python3.4/tkinter/__init__.py",
	"/lib/python3.4/tkinter/__main__.py",
	"/lib/python3.4/tkinter/_fix.py",
	"/lib/python3.4/tkinter/colorchooser.py",
	"/lib/python3.4/tkinter/commondialog.py",
	"/lib/python3.4/tkinter/constants.py",
	"/lib/python3.4/tkinter/dialog.py",
	"/lib/python3.4/tkinter/dnd.py",
	"/lib/python3.4/tkinter/filedialog.py",
	"/lib/python3.4/tkinter/font.py",
	"/lib/python3.4/tkinter/messagebox.py",
	"/lib/python3.4/tkinter/scrolledtext.py",
	"/lib/python3.4/tkinter/simpledialog.py",
	"/lib/python3.4/tkinter/test",
	"/lib/python3.4/tkinter/test/README",
	"/lib/python3.4/tkinter/test/__init__.py",
	"/lib/python3.4/tkinter/test/runtktests.py",
	"/lib/python3.4/tkinter/test/support.py",
	"/lib/python3.4/tkinter/test/test_tkinter",
	"/lib/python3.4/tkinter/test/test_tkinter/__init__.py",
	"/lib/python3.4/tkinter/test/test_tkinter/test_font.py",
	"/lib/python3.4/tkinter/test/test_tkinter/test_loadtk.py",
	"/lib/python3.4/tkinter/test/test_tkinter/test_misc.py",
	"/lib/python3.4/tkinter/test/test_tkinter/test_text.py",
	"/lib/python3.4/tkinter/test/test_tkinter/test_variables.py",
	"/lib/python3.4/tkinter/test/test_tkinter/test_widgets.py",
	"/lib/python3.4/tkinter/test/test_ttk",
	"/lib/python3.4/tkinter/test/test_ttk/__init__.py",
	"/lib/python3.4/tkinter/test/test_ttk/test_extensions.py",
	"/lib/python3.4/tkinter/test/test_ttk/test_functions.py",
	"/lib/python3.4/tkinter/test/test_ttk/test_style.py",
	"/lib/python3.4/tkinter/test/test_ttk/test_widgets.py",
	"/lib/python3.4/tkinter/test/widget_tests.py",
	"/lib/python3.4/tkinter/tix.py",
	"/lib/python3.4/tkinter/ttk.py",
	"/lib/python3.4/token.py",
	"/lib/python3.4/tokenize.py",
	"/lib/python3.4/trace.py",
	"/lib/python3.4/traceback.py",
	"/lib/python3.4/tracemalloc.py",
	"/lib/python3.4/tty.py",
	"/lib/python3.4/turtle.py",
	"/lib/python3.4/turtledemo",
	"/lib/python3.4/turtledemo/__init__.py",
	"/lib/python3.4/turtledemo/__main__.py",
	"/lib/python3.4/turtledemo/about_turtle.txt",
	"/lib/python3.4/turtledemo/about_turtledemo.txt",
	"/lib/python3.4/turtledemo/bytedesign.py",
	"/lib/python3.4/turtledemo/chaos.py",
	"/lib/python3.4/turtledemo/clock.py",
	"/lib/python3.4/turtledemo/colormixer.py",
	"/lib/python3.4/turtledemo/demohelp.txt",
	"/lib/python3.4/turtledemo/forest.py",
	"/lib/python3.4/turtledemo/fractalcurves.py",
	"/lib/python3.4/turtledemo/lindenmayer.py",
	"/lib/python3.4/turtledemo/minimal_hanoi.py",
	"/lib/python3.4/turtledemo/nim.py",
	"/lib/python3.4/turtledemo/paint.py",
	"/lib/python3.4/turtledemo/peace.py",
	"/lib/python3.4/turtledemo/penrose.py",
	"/lib/python3.4/turtledemo/planet_and_moon.py",
	"/lib/python3.4/turtledemo/round_dance.py",
	"/lib/python3.4/turtledemo/tree.py",
	"/lib/python3.4/turtledemo/turtle.cfg",
	"/lib/python3.4/turtledemo/two_canvases.py",
	"/lib/python3.4/turtledemo/wikipedia.py",
	"/lib/python3.4/turtledemo/yinyang.py",
	"/lib/python3.4/types.py",
	"/lib/python3.4/unittest",
	"/lib/python3.4/unittest/__init__.py",
	"/lib/python3.4/unittest/__main__.py",
	"/lib/python3.4/unittest/case.py",
	"/lib/python3.4/unittest/loader.py",
	"/lib/python3.4/unittest/main.py",
	"/lib/python3.4/unittest/mock.py",
	"/lib/python3.4/unittest/result.py",
	"/lib/python3.4/unittest/runner.py",
	"/lib/python3.4/unittest/signals.py",
	"/lib/python3.4/unittest/suite.py",
	"/lib/python3.4/unittest/test",
	"/lib/python3.4/unittest/test/__init__.py",
	"/lib/python3.4/unittest/test/__main__.py",
	"/lib/python3.4/unittest/test/_test_warnings.py",
	"/lib/python3.4/unittest/test/dummy.py",
	"/lib/python3.4/unittest/test/support.py",
	"/lib/python3.4/unittest/test/test_assertions.py",
	"/lib/python3.4/unittest/test/test_break.py",
	"/lib/python3.4/unittest/test/test_case.py",
	"/lib/python3.4/unittest/test/test_discovery.py",
	"/lib/python3.4/unittest/test/test_functiontestcase.py",
	"/lib/python3.4/unittest/test/test_loader.py",
	"/lib/python3.4/unittest/test/test_program.py",
	"/lib/python3.4/unittest/test/test_result.py",
	"/lib/python3.4/unittest/test/test_runner.py",
	"/lib/python3.4/unittest/test/test_setups.py",
	"/lib/python3.4/unittest/test/test_skipping.py",
	"/lib/python3.4/unittest/test/test_suite.py",
	"/lib/python3.4/unittest/test/testmock",
	"/lib/python3.4/unittest/test/testmock/__init__.py",
	"/lib/python3.4/unittest/test/testmock/__main__.py",
	"/lib/python3.4/unittest/test/testmock/support.py",
	"/lib/python3.4/unittest/test/testmock/testcallable.py",
	"/lib/python3.4/unittest/test/testmock/testhelpers.py",
	"/lib/python3.4/unittest/test/testmock/testmagicmethods.py",
	"/lib/python3.4/unittest/test/testmock/testmock.py",
	"/lib/python3.4/unittest/test/testmock/testpatch.py",
	"/lib/python3.4/unittest/test/testmock/testsentinel.py",
	"/lib/python3.4/unittest/test/testmock/testwith.py",
	"/lib/python3.4/unittest/util.py",
	"/lib/python3.4/urllib",
	"/lib/python3.4/urllib/__init__.py",
	"/lib/python3.4/urllib/error.py",
	"/lib/python3.4/urllib/parse.py",
	"/lib/python3.4/urllib/request.py",
	"/lib/python3.4/urllib/response.py",
	"/lib/python3.4/urllib/robotparser.py",
	"/lib/python3.4/uu.py",
	"/lib/python3.4/uuid.py",
	"/lib/python3.4/venv",
	"/lib/python3.4/venv/__init__.py",
	"/lib/python3.4/venv/__main__.py",
	"/lib/python3.4/venv/scripts",
	"/lib/python3.4/venv/scripts/posix",
	"/lib/python3.4/venv/scripts/posix/activate",
	"/lib/python3.4/venv/scripts/posix/activate.csh",
	"/lib/python3.4/venv/scripts/posix/activate.fish",
	"/lib/python3.4/warnings.py",
	"/lib/python3.4/wave.py",
	"/lib/python3.4/weakref.py",
	"/lib/python3.4/webbrowser.py",
	"/lib/python3.4/wsgiref",
	"/lib/python3.4/wsgiref/__init__.py",
	"/lib/python3.4/wsgiref/handlers.py",
	"/lib/python3.4/wsgiref/headers.py",
	"/lib/python3.4/wsgiref/simple_server.py",
	"/lib/python3.4/wsgiref/util.py",
	"/lib/python3.4/wsgiref/validate.py",
	"/lib/python3.4/xdrlib.py",
	"/lib/python3.4/xml",
	"/lib/python3.4/xml/__init__.py",
	"/lib/python3.4/xml/dom",
	"/lib/python3.4/xml/dom/NodeFilter.py",
	"/lib/python3.4/xml/dom/__init__.py",
	"/lib/python3.4/xml/dom/domreg.py",
	"/lib/python3.4/xml/dom/expatbuilder.py",
	"/lib/python3.4/xml/dom/minicompat.py",
	"/lib/python3.4/xml/dom/minidom.py",
	"/lib/python3.4/xml/dom/pulldom.py",
	"/lib/python3.4/xml/dom/xmlbuilder.py",
	"/lib/python3.4/xml/etree",
	"/lib/python3.4/xml/etree/ElementInclude.py",
	"/lib/python3.4/xml/etree/ElementPath.py",
	"/lib/python3.4/xml/etree/ElementTree.py",
	"/lib/python3.4/xml/etree/__init__.py",
	"/lib/python3.4/xml/etree/cElementTree.py",
	"/lib/python3.4/xml/parsers",
	"/lib/python3.4/xml/parsers/__init__.py",
	"/lib/python3.4/xml/parsers/expat.py",
	"/lib/python3.4/xml/sax",
	"/lib/python3.4/xml/sax/__init__.py",
	"/lib/python3.4/xml/sax/_exceptions.py",
	"/lib/python3.4/xml/sax/expatreader.py",
	"/lib/python3.4/xml/sax/handler.py",
	"/lib/python3.4/xml/sax/saxutils.py",
	"/lib/python3.4/xml/sax/xmlreader.py",
	"/lib/python3.4/xmlrpc",
	"/lib/python3.4/xmlrpc/__init__.py",
	"/lib/python3.4/xmlrpc/client.py",
	"/lib/python3.4/xmlrpc/server.py",
	"/lib/python3.4/zipfile.py",
	"/share/man/man1/python3.4.1",
	NULL,
};

static const char* system_leaked[] =
{
	"/bin/chroot",
	"/bin/unmount",
	"/include/sortix/__/dt.h",
	"/include/sortix/__/stat.h",
	NULL,
};
