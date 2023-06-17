/*
 * Copyright (c) 2015, 2016, 2020, 2021, 2022, 2023 Jonas 'Sortie' Termansen.
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
 * sysinstall.c
 * Operating system installer.
 */

#include <sys/display.h>
#include <sys/ioctl.h>
#include <sys/kernelinfo.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <assert.h>
#include <brand.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fstab.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

// Sortix libc doesn't have its own proper <limits.h> at this time.
#if defined(__sortix__)
#include <sortix/limits.h>
#endif

#include <mount/blockdevice.h>
#include <mount/devices.h>
#include <mount/filesystem.h>
#include <mount/harddisk.h>
#include <mount/partition.h>
#include <mount/uuid.h>

#include "autoconf.h"
#include "conf.h"
#include "devices.h"
#include "execute.h"
#include "fileops.h"
#include "interactive.h"
#include "manifest.h"
#include "release.h"

const char* prompt_man_section = "7";
const char* prompt_man_page = "installation";

static struct partition_table* search_bios_boot_pt(struct filesystem* root_fs)
{
	char firmware[64];
	if ( kernelinfo("firmware", firmware, sizeof(firmware)) != 0 )
		return NULL;
	if ( strcmp(firmware, "bios") != 0 )
		return NULL;
	struct blockdevice* bdev = root_fs->bdev;
	while ( bdev->p )
		bdev = bdev->p->parent_bdev;
	if ( !bdev->pt )
		return NULL;
	struct partition_table* pt = bdev->pt;
	if ( pt->type != PARTITION_TABLE_TYPE_GPT )
		return NULL;
	return pt;
}

static struct partition* search_bios_boot_search(struct partition_table* pt)
{
	for ( size_t i = 0; i < pt->partitions_count; i++ )
	{
		struct partition* p = pt->partitions[i];
		if ( p->bdev.fs && !strcmp(p->bdev.fs->fstype_name, "biosboot") )
			return p;
	}
	return NULL;
}

static struct partition* search_bios_boot_partition(struct filesystem* root_fs)
{
	struct partition_table* pt = search_bios_boot_pt(root_fs);
	if ( !pt )
		return NULL;
	return search_bios_boot_search(pt);
}

static bool missing_bios_boot_partition(struct filesystem* root_fs)
{
	struct partition_table* pt = search_bios_boot_pt(root_fs);
	if ( !pt )
		return NULL;
	return !search_bios_boot_search(pt);
}

static bool should_install_bootloader_path(const char* mnt,
                                           struct blockdevice* bdev)
{
	char* release_errpath;
	if ( asprintf(&release_errpath, "%s: /etc/sortix-release",
	              path_of_blockdevice(bdev)) < 0 )
	{
		warn("malloc");
		return false;
	}
	char* release_path;
	if ( asprintf(&release_path, "%s/etc/sortix-release", mnt) < 0 )
	{
		warn("malloc");
		free(release_errpath);
		return false;
	}
	struct release release;
	if ( !os_release_load(&release, release_path, release_errpath) )
	{
		free(release_path);
		free(release_errpath);
		return false;
	}
	free(release_path);
	free(release_errpath);
	release_free(&release);
	char* conf_path;
	if ( asprintf(&conf_path, "%s/etc/upgrade.conf", mnt) < 0 )
	{
		warn("malloc");
		return false;
	}
	struct conf conf;
	conf_init(&conf);
	bool result = false;
	if ( conf_load(&conf, conf_path) )
		result = conf.grub;
	else if ( errno != ENOENT )
		warn("%s: /etc/upgrade.conf", path_of_blockdevice(bdev));
	conf_free(&conf);
	free(conf_path);
	return result;
}

static bool should_install_bootloader_bdev(struct blockdevice* bdev)
{
	if ( !bdev->fs )
		return false;
	if ( bdev->fs->flags & FILESYSTEM_FLAG_NOT_FILESYSTEM )
		return false;
	if ( !bdev->fs->driver )
		return false;
	char mnt[] = "/tmp/fs.XXXXXX";
	if ( !mkdtemp(mnt) )
	{
		warn("mkdtemp: %s", "/tmp/fs.XXXXXX");
		return false;
	}
	struct mountpoint mp = { 0 };
	mp.absolute = mnt;
	mp.fs = bdev->fs;
	mp.entry.fs_file = mnt;
	if ( !mountpoint_mount(&mp) )
	{
		rmdir(mnt);
		return false;
	}
	bool should = should_install_bootloader_path(mnt, bdev);
	mountpoint_unmount(&mp);
	rmdir(mnt);
	return should;
}

static bool should_install_bootloader(void)
{
	bool any_systems = false;
	for ( size_t i = 0; i < hds_count; i++ )
	{
		struct harddisk* hd = hds[i];
		if ( hd->bdev.pt )
		{
			for ( size_t n = 0; n < hd->bdev.pt->partitions_count; n++ )
			{
				any_systems = true;
				struct partition* p = hd->bdev.pt->partitions[n];
				if ( should_install_bootloader_bdev(&p->bdev) )
					return true;
			}
		}
		else if ( hd->bdev.fs )
		{
			any_systems = true;
			if ( should_install_bootloader_bdev(&hd->bdev) )
				return true;
		}
	}
	return !any_systems;
}

static bool passwd_check(const char* passwd_path,
                         bool (*check)(struct passwd*, void*),
                         void* check_ctx)
{
	FILE* passwd = fopen(passwd_path, "r");
	if ( !passwd )
	{
		if ( errno != ENOENT )
			warn("%s", passwd_path);
		return false;
	}
	char* line = NULL;
	size_t size = 0;
	ssize_t length;
	while ( 0 < (length = getline(&line, &size, passwd) ) )
	{
		if ( line[size - 1] == '\n' )
			line[--size] = '\0';
		struct passwd pwd;
		if ( scanpwent(line, &pwd) && check(&pwd, check_ctx) )
		{
			free(line);
			fclose(passwd);
			return true;
		}
	}
	free(line);
	if ( ferror(passwd) )
		warn("%s", passwd_path);
	fclose(passwd);
	return false;
}

static bool passwd_has_uid_check(struct passwd* pwd, void* ctx)
{
	return pwd->pw_uid == *(uid_t*) ctx;
}

static bool passwd_has_uid(const char* passwd_path, uid_t uid)
{
	return passwd_check(passwd_path, passwd_has_uid_check, &uid);
}

static bool passwd_has_name_check(struct passwd* pwd, void* ctx)
{
	return !strcmp(pwd->pw_name, (const char*) ctx);
}

static bool passwd_has_name(const char* passwd_path, const char* name)
{
	return passwd_check(passwd_path, passwd_has_name_check, (void*) name);
}

static void install_skel(const char* home, uid_t uid, gid_t gid)
{
	const char* argv[] = { "cp", "-RT", "--", "etc/skel", home, NULL };
	execute(argv, "ug", uid, gid);
}

__attribute__((format(printf, 3, 4)))
static bool install_configurationf(const char* path,
                                   const char* mode,
                                   const char* format,
                                   ...)
{
	FILE* fp = fopen(path, mode);
	if ( !fp )
	{
		warn("%s", path);
		return false;
	}
	va_list ap;
	va_start(ap, format);
	int status = vfprintf(fp, format, ap);
	va_end(ap);
	if ( status < 0 )
	{
		warn("%s", path);
		fclose(fp);
		return false;
	}
	if ( fclose(fp) == EOF )
	{
		warn("%s", path);
		return false;
	}
	return true;
}

static void grub_hash_password(char* buffer, size_t buffer_size, const char* pw)
{
	int pipe_fds[2];
	if ( pipe(pipe_fds) < 0 )
		err(2, "pipe");
	pid_t pid = fork();
	if ( pid < 0 )
		err(2, "fork");
	if ( pid == 0 )
	{
		close(pipe_fds[0]);
		if ( dup2(pipe_fds[1], 1) < 0 )
			_exit(2);
		close(pipe_fds[1]);
		const char* argv[] = { "grub-mkpasswd-pbkdf2", "-p", pw, NULL };
		execvp(argv[0], (char* const*) argv);
		_exit(127);
	}
	close(pipe_fds[1]);
	size_t done = 0;
	while ( done < buffer_size )
	{
		ssize_t amount = read(pipe_fds[0], buffer + done, buffer_size - done);
		if ( amount < 0 )
			err(2, "read");
		if ( amount == 0 )
			break;
		done += amount;
	}
	if ( done && buffer[done-1] == '\n' )
		done--;
	if ( done == buffer_size )
		done--;
	buffer[done] = '\0';
	close(pipe_fds[0]);
	int exit_code;
	waitpid(pid, &exit_code, 0);
	if ( !WIFEXITED(exit_code) || WEXITSTATUS(exit_code) != 0 )
		errx(2, "grub password hash failed");
}

static pid_t main_pid;
static struct mountpoint* mountpoints;
static size_t mountpoints_used;
static bool etc_made = false;
static char etc[] = "/tmp/etc.XXXXXX";
static bool fs_made = false;
static char fs[] = "/tmp/fs.XXXXXX";
static int exit_gui_code = -1;

static void unmount_all_but_root(void)
{
	for ( size_t n = mountpoints_used; n != 0; n-- )
	{
		size_t i = n - 1;
		struct mountpoint* mountpoint = &mountpoints[i];
		if ( !strcmp(mountpoint->entry.fs_file, "/") )
			continue;
		mountpoint_unmount(mountpoint);
	}
}

void exit_handler(void)
{
	if ( getpid() != main_pid )
		 return;
	chdir("/");
	for ( size_t n = mountpoints_used; n != 0; n-- )
	{
		size_t i = n - 1;
		struct mountpoint* mountpoint = &mountpoints[i];
		mountpoint_unmount(mountpoint);
	}
	if ( fs_made )
		rmdir(fs);
	if ( etc_made )
		execute((const char*[]) { "rm", "-rf", etc, NULL }, "");
	if ( 0 <= exit_gui_code )
		gui_shutdown(exit_gui_code);
}

void exit_gui(int code)
{
	exit_gui_code = code;
	exit(code);
}

static void cancel_on_sigint(int signum)
{
	(void) signum;
	errx(2, "fatal: Installation canceled");
}

int main(void)
{
	shlvl();

	if ( !isatty(0) )
		errx(2, "fatal: stdin is not a terminal");
	if ( !isatty(1) )
		errx(2, "fatal: stdout is not a terminal");
	if ( !isatty(2) )
		errx(2, "fatal: stderr is not a terminal");

	if ( getuid() != 0 )
		errx(2, "You need to be root to install %s", BRAND_DISTRIBUTION_NAME);
	if ( getgid() != 0 )
		errx(2, "You need to be group root to install %s", BRAND_DISTRIBUTION_NAME);

	main_pid = getpid();
	if ( atexit(exit_handler) != 0 )
		err(2, "atexit");

	if ( !mkdtemp(etc) )
		err(2, "mkdtemp: %s", "/tmp/etc.XXXXXX");
	etc_made = true;
	// Export for the convenience of users escaping to a shell.
	setenv("SYSINSTALL_ETC", fs, 1);

	if ( chdir(etc) < 0 )
		err(2, "chdir: %s", etc);

	struct utsname uts;
	uname(&uts);

	struct conf conf;
	conf_init(&conf);
	if ( !conf_load(&conf, "/etc/upgrade.conf") && errno != ENOENT )
		warn("/etc/upgrade.conf");

	autoconf_load("/etc/autoinstall.conf");

	char* accepts_defaults = autoconf_eval("accept_defaults");
	bool non_interactive = accepts_defaults &&
	                       !strcasecmp(accepts_defaults, "yes");
	free(accepts_defaults);

	static char input[256];

	textf("Hello and welcome to the " BRAND_DISTRIBUTION_NAME " " VERSIONSTR ""
	      " installer for %s.\n\n", uts.machine);

	if ( non_interactive ||
	     (autoconf_has("ready") &&
	      (autoconf_has("disked") || autoconf_has("confirm_install"))) )
	{
		int countdown = 10;
		if ( autoconf_has("countdown") )
		{
			char* string = autoconf_eval("countdown");
			countdown = atoi(string);
			free(string);
		}
		sigset_t old_set;
		sigset_t new_set;
		sigemptyset(&new_set);
		sigaddset(&new_set, SIGINT);
		sigprocmask(SIG_BLOCK, &new_set, &old_set);
		struct sigaction old_sa;
		struct sigaction new_sa = { 0 };
		new_sa.sa_handler = cancel_on_sigint;
		sigaction(SIGINT, &new_sa, &old_sa);
		for ( ; 0 < countdown; countdown-- )
		{
			textf("Automatically installing " BRAND_DISTRIBUTION_NAME " "
			      VERSIONSTR " in %i %s... (Control-C to cancel)\n", countdown,
			      countdown != 1 ? "seconds" : "second");
			sigprocmask(SIG_SETMASK, &old_set, NULL);
			sleep(1);
			sigprocmask(SIG_BLOCK, &new_set, &old_set);
		}
		textf("Automatically installing " BRAND_DISTRIBUTION_NAME " "
		      VERSIONSTR "...\n");
		text("\n");
		sigaction(SIGINT, &old_sa, NULL);
		sigprocmask(SIG_SETMASK, &old_set, NULL);
	}

	// '|' rather than '||' is to ensure side effects.
	if ( missing_program("cut") |
	     missing_program("dash") |
	     missing_program("fsck.ext2") |
	     missing_program("grub-install") |
	     missing_program("man") |
	     missing_program("sed") |
	     missing_program("xargs") )
	{
		text("Warning: This system does not have the necessary third party "
		     "software installed to properly install this operating system.\n");
		while ( true )
		{
			prompt(input, sizeof(input), "ignore_missing_programs",
			       "Sure you want to proceed?", "no");
			if ( strcasecmp(input, "no") == 0 )
				return 0;
			if ( strcasecmp(input, "yes") == 0 )
				break;
		}
		text("\n");
	}

	text("You are about to install a new operating system on this computer. "
	     "This is not something you should do on a whim or when you are "
	     "impatient. Take the time to read the documentation and be patient "
	     "while you learn the new system. This is a very good time to start an "
	     "external music player that plays soothing classical music on loop.\n\n");

	if ( !access_or_die("/tix/tixinfo/ssh", F_OK) &&
	     access_or_die("/root/.ssh/authorized_keys", F_OK) < 0 )
		text("If you wish to ssh into your new installation, it's recommended "
		     "to first add your public keys to the .iso and obtain "
		     "fingerprints per release-iso-modification(7) before installing."
		     "\n\n");

	const char* readies[] =
	{
		"Ready",
		"Yes",
		"Yeah",
		"Yep",
		"Let's go",
		"Let's do this",
		"Betcha",
		"Sure am",
		"You bet",
		"It's very good music",
	};
	size_t num_readies = sizeof(readies) / sizeof(readies[0]);
	const char* ready = readies[arc4random_uniform(num_readies)];
	if ( autoconf_has("disked") )
		text("Warning: This installer will perform automatic harddisk "
		     "partitioning!\n");
	if ( autoconf_has("confirm_install") )
		text("Warning: This installer will automatically install an operating "
		     "system!\n");
	prompt(input, sizeof(input), "ready", "Ready?", ready);
	text("\n");

	text("This is not yet a fully fledged operating system. You should adjust "
	     "your expectations accordingly. You should not consider the system safe "
	     "for multi-user use. Filesystem permissions are not enforced yet. There "
	     "are many security issues so setuid(2) blatantly allows every user to "
	     "become root if they want to.\n\n");

	text("You can always escape to a shell by answering '!' to any regular "
	     "prompt. You can view the installation(7) manual page by "
	     "answering '!man'. Default answers are in []'s and can be selected by "
	     "pressing enter.\n\n");
	// TODO: You can leave this program by pressing ^C but it can leave your
	//       system in an inconsistent state.

	install_configurationf("upgrade.conf", "a", "src = yes\n");

	bool kblayout_setable = 0 <= tcgetblob(0, "kblayout", NULL, 0) ||
	                        getenv("DISPLAY_SOCKET");
	while ( kblayout_setable )
	{
		// TODO: Detect the name of the current keyboard layout.
		prompt(input, sizeof(input), "kblayout",
		       "Choose your keyboard layout ('?' or 'L' for list)", "default");
		if ( !strcmp(input, "?") ||
		     !strcmp(input, "l") ||
		     !strcmp(input, "L") )
		{
			DIR* dir = opendir("/share/kblayout");
			if ( !dir )
			{
				warn("%s", "/share/kblayout");
				continue;
			}
			bool space = false;
			struct dirent* entry;
			while ( (entry = readdir(dir)) )
			{
				if ( entry->d_name[0] == '.' )
					continue;
				if ( space )
					putchar(' ');
				fputs(entry->d_name, stdout);
				space = true;
			}
			closedir(dir);
			if ( !space )
				fputs("(No keyboard layouts available)", stdout);
			putchar('\n');
			continue;
		}
		if ( !strcmp(input, "default") )
			break;
		const char* argv[] = { "chkblayout", "--", input, NULL };
		if ( execute(argv, "f") == 0 )
			break;
	}
	if ( kblayout_setable )
	{
		if ( !input[0] || !strcmp(input, "default") )
			text("/etc/kblayout will not be created (default).\n");
		else
		{
			textf("/etc/kblayout will be set to \"%s\".\n", input);
			mode_t old_umask = getumask();
			umask(022);
			install_configurationf("kblayout", "w", "%s\n", input);
			umask(old_umask);
		}
		text("\n");
	}

	struct tiocgdisplay display;
	struct tiocgdisplays gdisplays;
	memset(&gdisplays, 0, sizeof(gdisplays));
	gdisplays.count = 1;
	gdisplays.displays = &display;
	struct dispmsg_get_driver_name dgdn = { 0 };
	dgdn.msgid = DISPMSG_GET_DRIVER_NAME;
	dgdn.device = 0;
	dgdn.driver_index = 0;
	dgdn.name.byte_size = 0;
	dgdn.name.str = NULL;
	if ( ioctl(1, TIOCGDISPLAYS, &gdisplays) == 0 &&
	     0 < gdisplays.count &&
	     (dgdn.device = display.device, true) &&
	     (dispmsg_issue(&dgdn, sizeof(dgdn)) == 0 || errno != ENODEV) )
	{
		struct dispmsg_get_crtc_mode get_mode;
		memset(&get_mode, 0, sizeof(get_mode));
		get_mode.msgid = DISPMSG_GET_CRTC_MODE;
		get_mode.device = 0;
		get_mode.connector = 0;
		bool good = false;
		if ( dispmsg_issue(&get_mode, sizeof(get_mode)) == 0 )
		{
			good = (get_mode.mode.control & DISPMSG_CONTROL_VALID) &&
			       (get_mode.mode.control & DISPMSG_CONTROL_GOOD_DEFAULT);
			if ( get_mode.mode.control & DISPMSG_CONTROL_VM_AUTO_SCALE )
			{
				text("The display resolution will automatically change to "
				     "match the size of the virtual machine window.\n\n");
				good = true;
			}
		}
		const char* def = non_interactive || good ? "no" : "yes";
		while ( true )
		{
			prompt(input, sizeof(input), "videomode",
			       "Select display resolution? "
			       "(yes/no/WIDTHxHEIGHTxBPP)", def);
			unsigned int xres, yres, bpp;
			bool set = sscanf(input, "%ux%ux%u", &xres, &yres, &bpp) == 3;
			if ( !strcasecmp(input, "no") )
			{
				input[0] = '\0';
				break;
			}
			const char* r = set ? input : NULL;
			if ( execute((const char*[]) { "chvideomode", r, NULL }, "f") != 0 )
				continue;
			input[0] = '\0';
			if ( dispmsg_issue(&get_mode, sizeof(get_mode)) < 0 ||
			     !(get_mode.mode.control & DISPMSG_CONTROL_VALID) ||
			     get_mode.mode.control & DISPMSG_CONTROL_VGA )
				break;
			snprintf(input, sizeof(input), "%ux%ux%u",
			         get_mode.mode.view_xres,
			         get_mode.mode.view_yres,
			         get_mode.mode.fb_format);
			break;
		}

		if ( !input[0] )
			text("/etc/videomode will not be created.\n");
		else
		{
			textf("/etc/videomode will be set to \"%s\".\n", input);
			mode_t old_umask = getumask();
			umask(022);
			install_configurationf("videomode", "w", "%s\n", input);
			umask(old_umask);
		}
		text("\n");
	}

	text("Searching for existing installations...\n");
	scan_devices();
	bool bootloader_default = should_install_bootloader();
	text("\n");

	textf("You need a bootloader to start the operating system. GRUB is the "
	      "standard %s bootloader and this installer comes with a copy. "
	      "This copy is however unable to automatically detect other operating "
	      "systems.\n\n", BRAND_DISTRIBUTION_NAME);
	text("Single-boot installations should accept this bootloader.\n");
	text("Dual-boot systems should refuse it and manually arrange for "
	     "bootloading by configuring any existing multiboot compliant "
	     "bootloader.\n");
	text("\n");
	char accept_grub[10];
	char accept_grub_password[10];
	char grub_password[512];
	while ( true )
	{
		const char* def = bootloader_default ? "yes" : "no";
		prompt(accept_grub, sizeof(accept_grub), "grub",
		       "Install a new GRUB bootloader?", def);
		if ( strcasecmp(accept_grub, "no") == 0 ||
		     strcasecmp(accept_grub, "yes") == 0 )
			break;
	}
	text("\n");

	if ( strcasecmp(accept_grub, "yes") == 0 )
	{
		install_configurationf("upgrade.conf", "a", "grub = yes\n");

		text("If an unauthorized person has access to the bootloader command "
		     "line, then the whole system security can be compromised. You can "
		     "prevent this by password protecting interactive use of the "
		     "bootloader, but still allowing anyone to start the system "
		     "normally. Similarly you may wish to manually go into your "
		     "firmware and password protect it.\n");
		text("\n");
		while ( true )
		{
			const char* def =
				non_interactive &&
				!autoconf_has("grub_password_hash") ? "no" : "yes";
			prompt(accept_grub_password, sizeof(accept_grub_password),
			       "grub_password",
			       "Password protect interactive bootloader? (yes/no)", def);
			if ( strcasecmp(accept_grub_password, "no") == 0 ||
			     strcasecmp(accept_grub_password, "yes") == 0 )
				break;
		}
		if ( autoconf_has("grub_password_hash") )
		{
			char* hash = autoconf_eval("grub_password_hash");
			install_configurationf("grubpw", "w", "%s\n", hash);
			free(hash);
		}
		else while ( !strcasecmp(accept_grub_password, "yes") )
		{
			char first[128];
			char second[128];
			password(first, sizeof(first),
			         "Bootloader root password? (will not echo)");
			password(second, sizeof(second),
			         "Bootloader root password? (again)");
			if ( strcmp(first, second) != 0 )
			{
				printf("Passwords do not match, try again.\n");
				continue;
			}
			explicit_bzero(second, sizeof(second));
			if ( !strcmp(first, "") )
			{
				char answer[32];
				prompt(answer, sizeof(answer), "grub_password_empty",
				       "Empty password is stupid, are you sure? (yes/no)", "no");
				if ( strcasecmp(answer, "yes") != 0 )
					continue;
			}
			grub_hash_password(grub_password, sizeof(grub_password), first);
			textf("/etc/grubpw will be made with grub-mkpasswd-pbkdf2.\n");
			mode_t old_umask = getumask();
			umask(077);
			install_configurationf("grubpw", "w", "%s\n", grub_password);
			umask(old_umask);
			break;
		}
		text("\n");
	}

	// TODO: Offer the user an automatic layout of partitions if the disk is
	//       empty.

	// TODO: Perhaps let the user know the size of the system that will be
	//       installed?

	text("You need to select a root filesystem and other mountpoints now. You "
	     "will now be dumped into a partition editor. Create and format a "
	     "root filesystem partition as needed.\n");
	text("\n");
	const char* mktable_tip = "";
	if ( check_lacking_partition_table() )
		mktable_tip = "Type mktable to make a new partition table. ";
	const char* devices_tip = "";
	if ( check_multiple_harddisks() )
		devices_tip = "Type devices to list the devices. "
		              "Type device 1 to switch to device 1. ";
	textf("Type ls to list partitions on the device. "
	      "%s"
	      "%s"
	      "Type mkpart to make a new partition. "
	      "Type mount 2 / to create a mountpoint for partition 2. "
	      "Type exit when done. "
	      "There is partitioning advice in installation(7). "
	      "Type man 8 disked to display the disked(8) man page.\n",
	      mktable_tip, devices_tip);
	struct filesystem* root_filesystem = NULL;
	struct filesystem* boot_filesystem = NULL;
	struct filesystem* bootloader_filesystem = NULL;
	bool not_first = false;
	while ( true )
	{
		if ( not_first )
			text("Type man to display the disked(8) man page.\n");
		not_first = true;
		const char* argv[] = { "disked", "--fstab=fstab", NULL };
		char* disked_input = autoconf_eval("disked");
		if ( execute(argv, "fi", disked_input) != 0 )
		{
			free(disked_input);
			// TODO: We also end up here on SIGINT.
			// TODO: Offer a shell here instead of failing?
			warnx("partitioning failed");
			sleep(1);
			continue;
		}
		free(disked_input);
		free_mountpoints(mountpoints, mountpoints_used);
		mountpoints = NULL;
		mountpoints_used = 0;
		scan_devices();
		if ( !load_mountpoints("fstab", &mountpoints, &mountpoints_used) )
		{
			if ( errno == ENOENT )
				text("You have not created any mountpoints. Try again.\n");
			else
				warn("fstab");
			continue;
		}
		bool found_rootfs = false;
		for ( size_t i = 0; !found_rootfs && i < mountpoints_used; i++ )
			if ( !strcmp(mountpoints[i].entry.fs_file, "/") )
				found_rootfs = true;
		if ( !found_rootfs )
		{
			text("You have no root filesystem mountpoint. Try again.\n");
			continue;
		}
		root_filesystem = NULL;
		boot_filesystem = NULL;
		bool cant_mount = false;
		for ( size_t i = 0; i < mountpoints_used; i++ )
		{
			struct mountpoint* mnt = &mountpoints[i];
			const char* spec = mnt->entry.fs_spec;
			if ( !(mnt->fs = search_for_filesystem_by_spec(spec)) )
			{
				warnx("fstab: %s: Found no mountable filesystem matching `%s'",
				      mnt->entry.fs_file, spec);
				cant_mount = true;
				continue;
			}
			if ( !mnt->fs->driver )
			{
				warnx("fstab: %s: %s: Don't know how to mount this %s filesystem",
				      mnt->entry.fs_file,
				      path_of_blockdevice(mnt->fs->bdev),
				      mnt->fs->fstype_name);
				cant_mount = true;
				continue;
			}
			if ( !strcmp(mnt->entry.fs_file, "/") )
				root_filesystem = mnt->fs;
			if ( !strcmp(mnt->entry.fs_file, "/boot") )
				boot_filesystem = mnt->fs;
		}
		if ( cant_mount )
			continue;
		assert(root_filesystem);
		bootloader_filesystem = boot_filesystem ? boot_filesystem : root_filesystem;
		assert(bootloader_filesystem);
		if ( !strcasecmp(accept_grub, "yes") &&
		     missing_bios_boot_partition(bootloader_filesystem) )
		{
			const char* where = boot_filesystem ? "/boot" : "root";
			const char* dev = device_path_of_blockdevice(bootloader_filesystem->bdev);
			assert(dev);
			textf("You are a installing BIOS bootloader and the %s "
			      "filesystem is located on a GPT partition, but you haven't "
			      "made a BIOS boot partition on the %s GPT disk. Pick "
			      "biosboot during mkpart and make a 1 MiB partition.\n",
			      where, dev);
			char return_to_disked[10];
			while ( true )
			{
				prompt(return_to_disked, sizeof(return_to_disked),
				       "missing_bios_boot_partition",
				       "Return to disked to make a BIOS boot partition?", "yes");
				if ( strcasecmp(accept_grub, "no") == 0 ||
					 strcasecmp(accept_grub, "yes") == 0 )
					break;
			}
			if ( !strcasecmp(return_to_disked, "yes") )
				continue;
			text("Proceeding, but expect the installation to fail.\n");
		}
		break;
	}
	text("\n");

	textf("We are now ready to install %s %s. Take a moment to verify "
	      "everything is in order.\n", BRAND_DISTRIBUTION_NAME, VERSIONSTR);
	text("\n");
	printf("  %-16s  system architecture\n", uts.machine);
	for ( size_t i = 0; i < mountpoints_used; i++ )
	{
		struct mountpoint* mnt = &mountpoints[i];
		const char* devname = path_of_blockdevice(mnt->fs->bdev);
		const char* where = mnt->entry.fs_file;
		printf("  %-16s  use as %s\n", devname, where);
	}
	if ( strcasecmp(accept_grub, "yes") == 0 )
	{
		struct partition* bbp = search_bios_boot_partition(bootloader_filesystem);
		if ( bbp )
			printf("  %-16s  bios boot partition\n",
			       path_of_blockdevice(&bbp->bdev));
		printf("  %-16s  bootloader installation target\n",
		       device_path_of_blockdevice(bootloader_filesystem->bdev));
	}
	text("\n");

	while ( true )
	{
		prompt(input, sizeof(input), "confirm_install",
		       "Install " BRAND_DISTRIBUTION_NAME "? "
		       "(yes/no/exit/poweroff/reboot/halt)", "yes");
		if ( !strcasecmp(input, "yes") )
			break;
		else if ( !strcasecmp(input, "no") )
		{
			text("Answer '!' to get a shell. Type !man to view the "
			     "installation(7) manual page.\n");
			text("Alternatively, you can answer 'poweroff', 'reboot', or "
			     "'halt' to cancel the installation.\n");
			continue;
		}
		else if ( !strcasecmp(input, "exit") )
			exit(0);
		else if ( !strcasecmp(input, "poweroff") )
			exit_gui(0);
		else if ( !strcasecmp(input, "reboot") )
			exit_gui(1);
		else if ( !strcasecmp(input, "halt") )
			exit_gui(2);
		else
			continue;
	}
	text("\n");

	text("Installing " BRAND_DISTRIBUTION_NAME " " VERSIONSTR " now:\n");

	printf(" - Mounting filesystems...\n");

	if ( !mkdtemp(fs) )
		err(2, "mkdtemp: %s", "/tmp/fs.XXXXXX");
	fs_made = true;
	// Export for the convenience of users escaping to a shell.
	setenv("SYSINSTALL_TARGET", fs, 1);

	for ( size_t i = 0; i < mountpoints_used; i++ )
	{
		struct mountpoint* mnt = &mountpoints[i];
		char* absolute;
		if ( asprintf(&absolute, "%s%s", fs, mnt->absolute) < 0 )
			err(2, "asprintf");
		free(mnt->absolute);
		mnt->absolute = absolute;
		if ( mkdir_p(mnt->absolute, 0755) < 0 )
			err(2, "mkdir: %s", mnt->absolute);
		if ( !mountpoint_mount(mnt) )
			exit(2);
	}

	if ( chdir(fs) < 0 )
		err(2, "chdir: %s", fs);

	pid_t install_pid = fork();
	if ( install_pid < 0 )
		err(2, "fork");
	if ( install_pid == 0 )
	{
		printf(" - Populating root filesystem...\n");
		chmod(".", 0755);
		if ( access("tix/collection.conf", F_OK) < 0 )
			execute((const char*[]) { "tix-collection", ".", "create",
			                          "--prefix=", NULL }, "_e");
		install_manifests_detect("", ".", true, true, true);
		// TODO: Preserve the existing /src if it exists like in sysupgrade.
		if ( has_manifest("src") )
			install_manifest("src", "", ".", (const char*[]){}, 0);
		printf(" - Creating configuration files...\n");
		// TODO: Preserve mode/ownership/timestamps?
		execute((const char*[]) { "cp", "-RTP", etc, "etc", NULL }, "_e");
		// TODO: Auto detect appropriate bcrypt rounds and set up etc/login.conf
		//       and use those below instead of blowfish,a.
		if ( access_or_die("boot/random.seed", F_OK) < 0 )
		{
			printf(" - Creating random seed...\n");
			write_random_seed("boot/random.seed");
		}
		printf(" - Creating initrd...\n");
		execute((const char*[]) { "update-initrd", "--sysroot", fs, NULL }, "_e");
		if ( strcasecmp(accept_grub, "yes") == 0 )
		{
			printf(" - Installing bootloader...\n");
			execute((const char*[]) { "chroot", "-d", ".", "grub-install",
			        device_path_of_blockdevice(bootloader_filesystem->bdev), NULL },
			        "_eqQ");
			printf(" - Configuring bootloader...\n");
			execute((const char*[]) { "chroot", "-d", ".", "update-grub", NULL },
			        "_eqQ");
		}
		else if ( access_or_die("/etc/grub.d/10_sortix", F_OK) == 0 )
		{
			// Help dual booters by making /etc/grub.d/10_sortix.cache.
			printf(" - Creating bootloader fragment...\n");
			execute((const char*[]) { "chroot", "-d", ".",
			                          "/etc/grub.d/10_sortix", NULL }, "_eq");
		}
		printf(" - Finishing installation...\n");
		_exit(0);
	}
	int install_code;
	waitpid(install_pid, &install_code, 0);
	if ( WIFEXITED(install_code) && WEXITSTATUS(install_code) == 0 )
	{
	}
	else if ( WIFEXITED(install_code) )
		errx(2, "installation failed with exit status %i", WEXITSTATUS(install_code));
	else if ( WIFSIGNALED(install_code) )
		errx(2, "installation failed: %s", strsignal(WTERMSIG(install_code)));
	else
		errx(2, "installation failed: unknown waitpid code %i", install_code);

	unsetenv("SYSINSTALL_ETC");
	execute((const char*[]) { "rm", "-r", etc, NULL }, "");
	etc_made = false;

	text("\n");
	text("System files are now installed. We'll now make the system functional "
	     "by configuring a few essential matters.\n\n");

	umask(0022);

	if ( access("etc/hostname", F_OK) == 0 )
		textf("/etc/hostname already exists, skipping creating it.\n");
	else while ( true )
	{
		char defhost[HOST_NAME_MAX + 1] = "";
		if ( non_interactive )
			gethostname(defhost, sizeof(defhost));
		FILE* defhost_fp = fopen("etc/hostname", "r");
		if ( defhost_fp )
		{
			fgets(defhost, sizeof(defhost), defhost_fp);
			size_t defhostlen = strlen(defhost);
			if ( defhostlen && defhost[defhostlen-1] == '\n' )
				defhost[defhostlen-1] = '\0';
			fclose(defhost_fp);
		}
		char hostname[HOST_NAME_MAX + 1] = "";
		prompt(hostname, sizeof(hostname), "hostname", "System hostname?",
		       defhost[0] ? defhost : NULL);
		if ( !install_configurationf("etc/hostname", "w", "%s\n", hostname) )
			continue;
		textf("/etc/hostname was set to \"%s\".\n", hostname);
		break;
	}
	text("\n");

	if ( mkdir("root", 0700) < 0 )
	{
		if ( errno == EEXIST )
		{
			if ( chmod("root", 0700) < 0 )
				warn("chmod: root");
		}
		else
			warn("mkdir: root");
	}
	if ( passwd_has_uid("etc/passwd", 0) ||
	     passwd_has_name("etc/passwd", "root") )
	{
		textf("Root account already exists, skipping creating it.\n");
	}
	else if ( non_interactive || autoconf_has("password_hash_root") )
	{
		char* hash = autoconf_eval("password_hash_root");
		if ( !hash && !(hash = strdup("x")) )
			err(2, "malloc");
		if ( !install_configurationf("etc/passwd", "a",
			"root:%s:0:0:root:/root:sh\n"
			"include /etc/default/passwd.d/*\n", hash) )
			err(2, "etc/passwd");
		textf("User '%s' added to /etc/passwd\n", "root");
		if ( !install_configurationf("etc/group", "a",
			"root::0:root\n"
			"include /etc/default/group.d/*\n") )
			err(2, "etc/passwd");
		install_skel("/root", 0, 0);
		textf("Group '%s' added to /etc/group.\n", "root");
		free(hash);
	}
	else while ( true )
	{
		char first[128];
		char second[128];
		password(first, sizeof(first), "Password for root account? (will not echo)");
		password(second, sizeof(second), "Password for root account? (again)");
		if ( strcmp(first, second) != 0 )
		{
			printf("Passwords do not match, try again.\n");
			continue;
		}
		explicit_bzero(second, sizeof(second));
		if ( !strcmp(first, "") )
		{
			char answer[32];
			prompt(answer, sizeof(answer), "empty_password",
			       "Empty password is stupid, are you sure? (yes/no)", "no");
			if ( strcasecmp(answer, "yes") != 0 )
				continue;
		}
		char hash[128];
		if ( crypt_newhash(first, "blowfish,a", hash, sizeof(hash)) < 0 )
		{
			explicit_bzero(first, sizeof(first));
			warn("crypt_newhash");
			continue;
		}
		explicit_bzero(first, sizeof(first));
		if ( !install_configurationf("etc/passwd", "a",
			"root:%s:0:0:root:/root:sh\n"
			"include /etc/default/passwd.d/*\n", hash) )
			continue;
		textf("User '%s' added to /etc/passwd\n", "root");
		if ( !install_configurationf("etc/group", "a",
			"root::0:root\n"
			"include /etc/default/group.d/*\n") )
			continue;
		install_skel("/root", 0, 0);
		textf("Group '%s' added to /etc/group.\n", "root");
		break;
	}

	struct ssh_file
	{
		const char* key;
		const char* path;
		const char* pub;
	};
	const struct ssh_file ssh_files[] =
	{
		{"copy_ssh_authorized_keys_root", "/root/.ssh/authorized_keys", NULL},
		{"copy_ssh_config_root", "/root/.ssh/config", NULL},
		{"copy_ssh_id_rsa_root", "/root/.ssh/id_rsa", "/root/.ssh/id_rsa.pub"},
		{"copy_ssh_known_hosts_root", "/root/.ssh/known_hosts", NULL},
	};
	size_t ssh_files_count = sizeof(ssh_files) / sizeof(ssh_files[0]);
	bool any_ssh_keys = false;
	for ( size_t i = 0; i < ssh_files_count; i++ )
	{
		const struct ssh_file* file = &ssh_files[i];
		if ( access_or_die(file->path, F_OK) < 0 )
			continue;
		text("\n");
		textf("Found %s\n", file->path);
		if ( file->pub && !access_or_die(file->pub, F_OK) )
			textf("Found %s\n", file->pub);
		while ( true )
		{
			char question[256];
			snprintf(question, sizeof(question),
			         "Copy %s from installer environment? (yes/no)",
			         file->path);
			prompt(input, sizeof(input), file->key, question, "yes");
			if ( strcasecmp(input, "no") == 0 )
				break;
			if ( strcasecmp(input, "yes") != 0 )
				continue;
			mkdir_or_chmod_or_die("root/.ssh", 0700);
			textf("Copying %s -> %s\n", file->path, file->path + 1);
			execute((const char*[])
				{"cp", file->path, file->path+ 1, NULL }, "f");
			if ( file->pub )
			{
				textf("Copying %s -> %s\n", file->pub, file->pub + 1);
				execute((const char*[])
					{"cp", file->pub, file->pub + 1, NULL }, "f");
			}
			any_ssh_keys = true;
			break;
		}
	}
	text("\n");

	if ( mkdir("etc/init", 0755) < 0 )
	{
		if ( errno == EEXIST )
		{
			if ( chmod("etc/init", 0755) < 0 )
				warn("chmod: etc/init");
		}
		else
			warn("mkdir: etc/init");
	}
	install_configurationf("etc/init/default", "w",
		"require multi-user exit-code\n");

	text("Congratulations, the system is now functional! This is a good time "
	     "to do further customization of the system.\n\n");

	// TODO: autoconf users support.
	bool made_user = false;
	for ( uid_t uid = 1000; !has_autoconf; )
	{
		while ( passwd_has_uid("etc/passwd", uid) )
			uid++;
		gid_t gid = (gid_t) uid;
		static char userstr[256];
		const char* question = "Setup a user? (enter username or 'no')";
		if ( made_user )
			question = "Setup another user? (enter username or 'no')";
		prompt(userstr, sizeof(userstr), NULL, question, "no");
		if ( !strcmp(userstr, "no") )
			break;
		if ( !strcmp(userstr, "yes") )
			continue;
		const char* user = userstr;
		while ( user[0] == ' ')
			user++;
		if ( passwd_has_name("etc/passwd", user) )
		{
			textf("Account '%s' already exists.\n", user);
			continue;
		}
		static char name[256];
		prompt(name, sizeof(name), NULL, "Full name of user?", user);
		char first[128];
		char second[128];
		while ( true )
		{
			password(first, sizeof(first), "Password for user? (will not echo)");
			password(second, sizeof(second), "Password for user? (again)");
			if ( strcmp(first, second) != 0 )
			{
				printf("Passwords do not match, try again.\n");
				continue;
			}
			explicit_bzero(second, sizeof(second));
			if ( !strcmp(first, "") )
			{
				char answer[32];
				prompt(answer, sizeof(answer), "empty_password",
				       "Empty password is stupid, are you sure? (yes/no)", "no");
				if ( strcasecmp(answer, "yes") != 0 )
					continue;
			}
			break;
		}
		char hash[128];
		if ( crypt_newhash(first, "blowfish,a", hash, sizeof(hash)) < 0 )
		{
			explicit_bzero(first, sizeof(first));
			warn("crypt_newhash");
			continue;
		}
		explicit_bzero(first, sizeof(first));
		if ( !install_configurationf("etc/passwd", "a",
				"%s:%s:%" PRIuUID ":%" PRIuGID ":%s:/home/%s:sh\n",
				user, hash, uid, gid, name, user) )
			continue;
		if ( !install_configurationf("etc/group", "a",
				"%s::%" PRIuGID ":%s\n", user, gid, user) )
			continue;
		char* home;
		if ( asprintf(&home, "home/%s", user) < 0 )
		{
			warn("asprintf");
			continue;
		}
		if ( mkdir(home, 0700) < 0 && errno != EEXIST )
		{
			warn("mkdir: %s", home);
			free(home);
			continue;
		}
		chown(home, uid, gid);
		install_skel(home, uid, gid);
		free(home);
		textf("User '%s' added to /etc/passwd\n", user);
		textf("Group '%s' added to /etc/group.\n", user);
		text("\n");
		uid++;
		made_user = true;
	}
	// TODO: autoconf support.
	if ( !has_autoconf )
		text("\n");

	// TODO: Ask if networking should be disabled / enabled.

	while ( true )
	{
		prompt(input, sizeof(input), "enable_gui",
			   "Enable graphical user interface?",
		       getenv("DISPLAY_SOCKET") ? "yes" : "no");
		if ( strcasecmp(input, "no") == 0 )
			break;
		if ( strcasecmp(input, "yes") != 0 )
			continue;
		if ( !install_configurationf("etc/session", "w",
		                             "#!sh\nexec display\n") ||
		     chmod("etc/session", 0755) < 0 )
		{
			warn("etc/session");
			continue;
		}
		text("Added 'exec display' to /etc/session\n");
		break;
	}
	text("\n");

	if ( !access_or_die("/tix/tixinfo/ntpd", F_OK) )
	{
		text("A Network Time Protocol client (ntpd) has been installed that "
		     "can automatically synchronize the current time with the internet."
		     "\n\n");
		text("Privacy notice: If enabled, the default configuration will "
		     "obtain time from pool.ntp.org and time.cloudflare.com; and "
		     "compare with HTTPS timestamps from quad9 and www.google.com. "
		     "You are encouraged to edit /etc/ntpd.conf per the ntpd.conf(5) "
		     "manual with your preferences.\n\n");
		bool copied = false;
		while ( true )
		{
			prompt(input, sizeof(input), "enable_ntpd",
			       "Automatically get time from the network? (yes/no/edit/man)",
			       copied ? "yes" : "no");
			if ( strcasecmp(input, "no") == 0 )
				break;
			if ( strcasecmp(input, "man") == 0 )
			{
				execute((const char*[]) {"man", "5", "ntpd.conf", NULL}, "fi");
				continue;
			}
			if ( strcasecmp(input, "edit") == 0 )
			{
				if ( !copied )
				{
					execute((const char*[]) {"cp", "etc/default/ntpd.conf",
						                     "etc/ntpd.conf", NULL}, "f");
					copied = true;
				}
				const char* editor =
					getenv("EDITOR") ? getenv("EDITOR") : "editor";
				execute((const char*[]) {editor, "etc/ntpd.conf", NULL}, "f");
				text("Created /etc/ntpd.conf from /etc/default/ntpd.conf\n");
				continue;
			}
			if ( strcasecmp(input, "yes") != 0 )
				continue;
			if ( !install_configurationf("etc/init/local", "a",
			                             "require ntpd optional\n") )
			{
				warn("etc/init/local");
				continue;
			}
			if ( !install_configurationf("etc/init/time", "a",
			                             "furthermore\n"
			                             "require ntpd optional\n") )
			{
				warn("etc/init/time");
				continue;
			}
			text("Added 'require ntpd optional' to /etc/init/local\n");
			text("Added 'require ntpd optional' to /etc/init/time\n");
			break;
		}
		text("\n");
	}

	struct sshd_key_file
	{
		const char* pri;
		const char* pub;
	};
	const struct sshd_key_file sshd_key_files[] =
	{
		{"/etc/ssh_host_ecdsa_key", "/etc/ssh_host_ecdsa_key.pub"},
		{"/etc/ssh_host_ed25519_key", "/etc/ssh_host_ed25519_key.pub"},
		{"/etc/ssh_host_rsa_key", "/etc/ssh_host_rsa_key.pub"},
	};
	size_t sshd_key_files_count
		= sizeof(sshd_key_files) / sizeof(sshd_key_files[0]);
	bool any_sshd_keys = false;
	for ( size_t i = 0; i < sshd_key_files_count; i++ )
	{
		if ( !access_or_die(sshd_key_files[i].pri, F_OK) )
		{
			textf("Found %s\n", sshd_key_files[i].pri);
			any_sshd_keys = true;
		}
	}

	bool enabled_sshd = false;
	if ( !access_or_die("/tix/tixinfo/ssh", F_OK) )
	{
		text("A ssh server has been installed. You have the option of starting "
		     "it on boot to allow remote login over a cryptographically secure "
		     "channel. Answer no if you don't know what ssh is.\n\n");
		if ( !any_sshd_keys )
			text("Warning: " BRAND_DISTRIBUTION_NAME " does not yet collect "
			     "entropy for secure random numbers. Unless you type '!' and "
			     "escape to a shell and put 256 bytes of actual randomness "
			     "into boot/random.seed, the first boot will use the "
			     "randomness of this installer environment to generate ssh "
			     "keys. This initial randomness may be as weak as the wall "
			     "time when you booted the installer, which is easily guessed "
			     "by an attacker. The same warning applies to outgoing secure "
			     "connections as well.\n\n");
		bool might_want_sshd =
			any_ssh_keys ||
			any_sshd_keys ||
			!access_or_die("/etc/sshd_config", F_OK);
		while ( true )
		{
			prompt(input, sizeof(input), "enable_sshd",
			       "Enable ssh server? (yes/no)",
			       might_want_sshd ? "yes" : "no");
			if ( strcasecmp(input, "no") == 0 )
				break;
			if ( strcasecmp(input, "yes") != 0 )
				continue;
			if ( !install_configurationf("etc/init/local", "a",
			                             "require sshd optional\n") )
			{
				warn("etc/init/local");
				continue;
			}
			enabled_sshd = true;
			text("Added 'require sshd optional' to /etc/init/local\n");
			text("The ssh server will be started when the system boots.\n");
			break;
		}
		text("\n");
	}

	bool has_sshd_config = false;
	if ( !access_or_die("/etc/sshd_config", F_OK) )
	{
		while ( true )
		{
			prompt(input, sizeof(input), "copy_sshd_config",
			       "Copy /etc/sshd_config from installer environment? (yes/no)",
			       "yes");
			if ( strcasecmp(input, "no") == 0 )
				break;
			if ( strcasecmp(input, "yes") != 0 )
				continue;
			const char* file =  "/etc/sshd_config";
			textf("Copying %s -> %s\n", file, file + 1);
			execute((const char*[]) {"cp", file, file + 1}, "f");
			has_sshd_config = true;
			break;
		}
		text("\n");
	}

	if ( enabled_sshd && !has_sshd_config )
	{
		text("Password authentication has been disabled by default in sshd to "
		     "prevent remotely guessing insecure passwords. The recommended "
		     "approach is to put your public key in the installation .iso and "
		     "generate the sshd credentials ahead of time as documented in "
		     "release-iso-modification(7). However, you could enable password "
		     "authentication if you picked a very strong password.\n\n");
		bool enable_sshd_password = false;
		while ( true )
		{
			prompt(input, sizeof(input), "enable_sshd_password",
				   "Enable sshd password authentication? (yes/no)", "no");
			if ( strcasecmp(input, "no") == 0 )
				break;
			if ( strcasecmp(input, "yes") != 0 )
				continue;
			if ( !install_configurationf("etc/sshd_config", "a",
			                             "PasswordAuthentication yes\n") )
			{
				warn("etc/sshd_config");
				continue;
			}
			enable_sshd_password = true;
			text("Added 'PasswordAuthentication yes' to /etc/sshd_config\n");
			break;
		}
		while ( enable_sshd_password )
		{
			prompt(input, sizeof(input), "enable_sshd_root_password",
				   "Enable sshd password authentication for root? (yes/no)",
			       "no");
			if ( strcasecmp(input, "no") == 0 )
				break;
			if ( strcasecmp(input, "yes") != 0 )
				continue;
			if ( !install_configurationf("etc/sshd_config", "a",
			                             "PermitRootLogin yes\n") )
			{
				warn("etc/sshd_config");
				continue;
			}
			text("Added 'PermitRootLogin yes' to /etc/sshd_config\n");
			break;
		}
		text("\n");
	}

	if ( any_sshd_keys )
	{
		while ( true )
		{
			const char* question =
				"Copy sshd private keys from installer environment? (yes/no)";
			prompt(input, sizeof(input), "copy_sshd_private_keys",
			       question,
			       "yes");
			if ( strcasecmp(input, "no") == 0 )
				break;
			if ( strcasecmp(input, "yes") != 0 )
				continue;
			for ( size_t i = 0; i < sshd_key_files_count; i++ )
			{
				const struct sshd_key_file* file = &sshd_key_files[i];
				if ( access_or_die(file->pri, F_OK) < 0 )
					continue;
				textf("Copying %s -> %s\n", file->pri, file->pri + 1);
				execute((const char*[])
					{"cp", file->pri, file->pri + 1, NULL }, "f");
				textf("Copying %s -> %s\n", file->pub, file->pub + 1);
				execute((const char*[])
					{"cp", file->pub, file->pub + 1, NULL }, "f");
			}
			break;
		}
		text("\n");
	}

	text("It's time to boot into the newly installed system.\n\n");

	if ( strcasecmp(accept_grub, "no") == 0 )
		text("You did not accept a bootloader and need to set up bootloading "
		     "yourself. /etc/grub.d/10_sortix.cache is a GRUB configuration "
		     "fragment that boots the newly installed system.\n\n");

	text("Upon boot, you'll be greeted with a login screen. Enter your "
	     "credentials to get a command line. Login as user 'poweroff' as "
	     "described in login(8) to power off the machine or run poweroff(8). "
	     "After logging in, type 'man user-guide' to view the introductory "
	     "documentation.\n");
	text("\n");

	while ( true )
	{
		prompt(input, sizeof(input), "finally",
		       "What now? (exit/poweroff/reboot/halt/boot/chroot)", "boot");
		if ( !strcasecmp(input, "exit") )
			exit(0);
		else if ( !strcasecmp(input, "poweroff") )
			exit_gui(0);
		else if ( !strcasecmp(input, "reboot") )
			exit_gui(1);
		else if ( !strcasecmp(input, "halt") )
			exit_gui(2);
		else if ( !strcasecmp(input, "boot") )
		{
			if ( !access("/etc/fstab", F_OK) )
			{
				printf("Only a live environment can reinit installations.\n");
				continue;
			}
			execute((const char*[]) {"mkdir", "-p", "/etc/init", NULL }, "ef");
			execute((const char*[]) {"cp", "etc/fstab", "/etc/fstab", NULL },
			        "ef");
			execute((const char*[]) {"sh", "-c",
			                         "echo 'require chain exit-code' > "
			                         "/etc/init/default", NULL },
			        "ef");
			exit_gui(3);
		}
		else if ( !strcasecmp(input, "chroot") )
		{
			unmount_all_but_root();
			unsetenv("SYSINSTALL_TARGET");
			unsetenv("SHLVL");
			unsetenv("INIT_PID");
			exit(execute((const char*[]) { "chroot", "-d", fs,
			                               "/sbin/init", NULL }, "f"));
		}
	}
}
