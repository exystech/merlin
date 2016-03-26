/*
 * Copyright (c) 2013, 2014, 2015, 2016 Jonas 'Sortie' Termansen.
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
 * extfs.cpp
 * Implementation of the extended filesystem.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__sortix__)
#include "fsmarshall.h"
#else
#include "fuse.h"
#endif

#include "ext-constants.h"
#include "ext-structs.h"

#include "blockgroup.h"
#include "block.h"
#include "device.h"
#include "extfs.h"
#include "filesystem.h"
#include "inode.h"
#include "ioleast.h"

// These must be kept up to date with libmount/ext2.c.
static const uint32_t EXT2_FEATURE_COMPAT_SUPPORTED = 0;
static const uint32_t EXT2_FEATURE_INCOMPAT_SUPPORTED = \
                      EXT2_FEATURE_INCOMPAT_FILETYPE;
static const uint32_t EXT2_FEATURE_RO_COMPAT_SUPPORTED = \
                      EXT2_FEATURE_RO_COMPAT_LARGE_FILE;

uid_t request_uid;
uid_t request_gid;

mode_t HostModeFromExtMode(uint32_t extmode)
{
	mode_t hostmode = extmode & 0777;
	if ( extmode & EXT2_S_ISVTX ) hostmode |= S_ISVTX;
	if ( extmode & EXT2_S_ISGID ) hostmode |= S_ISGID;
	if ( extmode & EXT2_S_ISUID ) hostmode |= S_ISUID;
	if ( EXT2_S_ISSOCK(extmode) ) hostmode |= S_IFSOCK;
	if ( EXT2_S_ISLNK(extmode) ) hostmode |= S_IFLNK;
	if ( EXT2_S_ISREG(extmode) ) hostmode |= S_IFREG;
	if ( EXT2_S_ISBLK(extmode) ) hostmode |= S_IFBLK;
	if ( EXT2_S_ISDIR(extmode) ) hostmode |= S_IFDIR;
	if ( EXT2_S_ISCHR(extmode) ) hostmode |= S_IFCHR;
	if ( EXT2_S_ISFIFO(extmode) ) hostmode |= S_IFIFO;
	return hostmode;
}

uint32_t ExtModeFromHostMode(mode_t hostmode)
{
	uint32_t extmode = hostmode & 0777;
	if ( hostmode & S_ISVTX ) extmode |= EXT2_S_ISVTX;
	if ( hostmode & S_ISGID ) extmode |= EXT2_S_ISGID;
	if ( hostmode & S_ISUID ) extmode |= EXT2_S_ISUID;
	if ( S_ISSOCK(hostmode) ) extmode |= EXT2_S_IFSOCK;
	if ( S_ISLNK(hostmode) ) extmode |= EXT2_S_IFLNK;
	if ( S_ISREG(hostmode) ) extmode |= EXT2_S_IFREG;
	if ( S_ISBLK(hostmode) ) extmode |= EXT2_S_IFBLK;
	if ( S_ISDIR(hostmode) ) extmode |= EXT2_S_IFDIR;
	if ( S_ISCHR(hostmode) ) extmode |= EXT2_S_IFCHR;
	if ( S_ISFIFO(hostmode) ) extmode |= EXT2_S_IFIFO;
	return extmode;
}

uint8_t HostDTFromExtDT(uint8_t extdt)
{
	switch ( extdt )
	{
	case EXT2_FT_UNKNOWN: return DT_UNKNOWN;
	case EXT2_FT_REG_FILE: return DT_REG;
	case EXT2_FT_DIR: return DT_DIR;
	case EXT2_FT_CHRDEV: return DT_CHR;
	case EXT2_FT_BLKDEV: return DT_BLK;
	case EXT2_FT_FIFO: return DT_FIFO;
	case EXT2_FT_SOCK: return DT_SOCK;
	case EXT2_FT_SYMLINK: return DT_LNK;
	}
	return DT_UNKNOWN;
}

void StatInode(Inode* inode, struct stat* st)
{
	memset(st, 0, sizeof(*st));
	st->st_ino = inode->inode_id;
	st->st_mode = HostModeFromExtMode(inode->Mode());
	st->st_nlink = inode->data->i_links_count;
	st->st_uid = inode->UserId();
	st->st_gid = inode->GroupId();
	st->st_size = inode->Size();
	st->st_atim.tv_sec = inode->data->i_atime;
	st->st_atim.tv_nsec = 0;
	st->st_ctim.tv_sec = inode->data->i_ctime;
	st->st_ctim.tv_nsec = 0;
	st->st_mtim.tv_sec = inode->data->i_mtime;
	st->st_mtim.tv_nsec = 0;
	st->st_blksize = inode->filesystem->block_size;
	st->st_blocks = inode->data->i_blocks;
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

static void help(FILE* fp, const char* argv0)
{
	fprintf(fp, "Usage: %s [OPTION]... DEVICE [MOUNT-POINT]\n", argv0);
}

static void version(FILE* fp, const char* argv0)
{
	fprintf(fp, "%s (Sortix) %s\n", argv0, VERSIONSTR);
}

int main(int argc, char* argv[])
{
	const char* argv0 = argv[0];
	const char* pretend_mount_path = NULL;
	bool foreground = false;
	bool read = false;
	bool write = false;
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
			while ( char c = *++arg ) switch ( c )
			{
			case 'r': read = true; break;
			case 'w': write = true; break;
			default:
				fprintf(stderr, "%s: unknown option -- '%c'\n", argv0, c);
				help(stderr, argv0);
				exit(1);
			}
		}
		else if ( !strcmp(arg, "--help") )
			help(stdout, argv0), exit(0);
		else if ( !strcmp(arg, "--version") )
			version(stdout, argv0), exit(0);
		else if ( !strcmp(arg, "--background") )
			foreground = false;
		else if ( !strcmp(arg, "--foreground") )
			foreground = true;
		else if ( !strcmp(arg, "--read") )
			read = true;
		else if ( !strcmp(arg, "--write") )
			write = true;
		else if ( !strncmp(arg, "--pretend-mount-path=", strlen("--pretend-mount-path=")) )
			pretend_mount_path = arg + strlen("--pretend-mount-path=");
		else if ( !strcmp(arg, "--pretend-mount-path") )
		{
			if ( i+1 == argc )
			{
				fprintf(stderr, "%s: --pretend-mount-path: Missing operand\n", argv0);
				exit(1);
			}
			pretend_mount_path = argv[++i];
			argv[i] = NULL;
		}
		else
		{
			fprintf(stderr, "%s: unknown option: %s\n", argv0, arg);
			help(stderr, argv0);
			exit(1);
		}
	}

	// It doesn't make sense to have a write-only filesystem.
	read = read || write;

	// Default to read and write filesystem access.
	bool default_access = !read && !write ? read = write = true : false;

	if ( argc == 1 )
	{
		help(stdout, argv0);
		exit(0);
	}

	compact_arguments(&argc, &argv);

	const char* device_path = 2 <= argc ? argv[1] : NULL;
	const char* mount_path = 3 <= argc ? argv[2] : NULL;

	if ( !device_path )
	{
		help(stderr, argv0);
		exit(1);
	}

	if ( !pretend_mount_path )
		pretend_mount_path = mount_path;

	int fd = open(device_path, write ? O_RDWR : O_RDONLY);
	if ( fd < 0 )
		error(1, errno, "`%s'", device_path);

	// Read the super block from the filesystem so we can verify it.
	struct ext_superblock sb;
	if ( preadall(fd, &sb, sizeof(sb), 1024) != sizeof(sb) )
	{
		if ( errno == EEOF )
			error(1, 0, "`%s' isn't a valid extended filesystem", device_path);
		else
			error(1, errno, "read: `%s'", device_path);
	}

	// Verify the magic value to detect a compatible filesystem.
	if ( sb.s_magic != EXT2_SUPER_MAGIC )
		error(1, 0, "`%s' isn't a valid extended filesystem", device_path);

	// Test whether this revision of the extended filesystem is supported.
	if ( sb.s_rev_level == EXT2_GOOD_OLD_REV )
		error(1, 0, "`%s' is formatted with an obsolete filesystem revision",
		            device_path);

	// Verify that no incompatible features are in use.
	if ( sb.s_feature_compat & ~EXT2_FEATURE_INCOMPAT_SUPPORTED )
		error(1, 0, "`%s' uses unsupported and incompatible features",
		            device_path);

	// Verify that no incompatible features are in use if opening for write.
	if ( !default_access && write &&
	     sb.s_feature_ro_compat & ~EXT2_FEATURE_RO_COMPAT_SUPPORTED )
		error(1, 0, "`%s' uses unsupported and incompatible features, "
		            "read-only access is possible, but write-access was "
		            "requested", device_path);

	if ( write && sb.s_feature_ro_compat & ~EXT2_FEATURE_RO_COMPAT_SUPPORTED )
	{
		fprintf(stderr, "Warning: `%s' uses unsupported and incompatible "
		                "features, falling back to read-only access\n",
		                device_path);
		// TODO: Modify the file descriptor such that writing fails!
		read = true;
	}

	// Check whether any features are in use that we can safely disregard.
	if ( sb.s_feature_compat & ~EXT2_FEATURE_COMPAT_SUPPORTED )
		fprintf(stderr, "Note: filesystem uses unsupported but compatible "
		                "features\n");

	// Check the block size is sane. 64 KiB may have issues, 32 KiB then.
	if ( sb.s_log_block_size > (15-10) /* 32 KiB blocks */ )
		error(1, 0, "`%s': excess block size", device_path);

	// Check whether the filesystem was unmounted cleanly.
	if ( sb.s_state != EXT2_VALID_FS )
		fprintf(stderr, "Warning: `%s' wasn't unmounted cleanly\n",
	                    device_path);

	uint32_t block_size = 1024U << sb.s_log_block_size;

	Device* dev = new Device(fd, device_path, block_size, write);
	if ( !dev ) // TODO: Use operator new nothrow!
		error(1, errno, "malloc");
	Filesystem* fs = new Filesystem(dev, pretend_mount_path);
	if ( !fs ) // TODO: Use operator new nothrow!
		error(1, errno, "malloc");

	fs->block_groups = new BlockGroup*[fs->num_groups];
	if ( !fs->block_groups ) // TODO: Use operator new nothrow!
		error(1, errno, "malloc");
	for ( size_t i = 0; i < fs->num_groups; i++ )
		fs->block_groups[i] = NULL;

	if ( !mount_path )
		return 0;

#if defined(__sortix__)
	return fsmarshall_main(argv0, mount_path, foreground, fs, dev);
#else
	return ext2_fuse_main(argv0, mount_path, foreground, fs, dev);
#endif
}
