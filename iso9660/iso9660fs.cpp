/*
 * Copyright (c) 2013, 2014, 2015, 2016, 2022 Jonas 'Sortie' Termansen.
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
 * iso9660fs.cpp
 * Implementation of the ISO 9660 filesystem.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <dirent.h>
#include <endian.h>
#include <err.h>
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

#include "block.h"
#include "device.h"
#include "filesystem.h"
#include "inode.h"
#include "ioleast.h"
#include "iso9660.h"
#include "iso9660fs.h"
#include "util.h"

uid_t request_uid;
uid_t request_gid;

mode_t HostModeFromFsMode(uint32_t mode)
{
	mode_t hostmode = mode & 07777;
	if ( ISO9660_S_ISSOCK(mode) ) hostmode |= S_IFSOCK;
	if ( ISO9660_S_ISLNK(mode) ) hostmode |= S_IFLNK;
	if ( ISO9660_S_ISREG(mode) ) hostmode |= S_IFREG;
	if ( ISO9660_S_ISBLK(mode) ) hostmode |= S_IFBLK;
	if ( ISO9660_S_ISDIR(mode) ) hostmode |= S_IFDIR;
	if ( ISO9660_S_ISCHR(mode) ) hostmode |= S_IFCHR;
	if ( ISO9660_S_ISFIFO(mode) ) hostmode |= S_IFIFO;
	return hostmode;
}

uint32_t FsModeFromHostMode(mode_t hostmode)
{
	uint32_t mode = hostmode & 07777;
	if ( S_ISSOCK(hostmode) ) mode |= ISO9660_S_IFSOCK;
	if ( S_ISLNK(hostmode) ) mode |= ISO9660_S_IFLNK;
	if ( S_ISREG(hostmode) ) mode |= ISO9660_S_IFREG;
	if ( S_ISBLK(hostmode) ) mode |= ISO9660_S_IFBLK;
	if ( S_ISDIR(hostmode) ) mode |= ISO9660_S_IFDIR;
	if ( S_ISCHR(hostmode) ) mode |= ISO9660_S_IFCHR;
	if ( S_ISFIFO(hostmode) ) mode |= ISO9660_S_IFIFO;
	return mode;
}

uint8_t HostDTFromFsDT(uint8_t fsdt)
{
	switch ( fsdt )
	{
	case ISO9660_FT_UNKNOWN: return DT_UNKNOWN;
	case ISO9660_FT_REG_FILE: return DT_REG;
	case ISO9660_FT_DIR: return DT_DIR;
	case ISO9660_FT_CHRDEV: return DT_CHR;
	case ISO9660_FT_BLKDEV: return DT_BLK;
	case ISO9660_FT_FIFO: return DT_FIFO;
	case ISO9660_FT_SOCK: return DT_SOCK;
	case ISO9660_FT_SYMLINK: return DT_LNK;
	}
	return DT_UNKNOWN;
}

void StatInode(Inode* inode, struct stat* st)
{
	memset(st, 0, sizeof(*st));
	st->st_ino = inode->inode_id;
	st->st_mode = HostModeFromFsMode(inode->Mode());
	st->st_nlink = inode->nlink;
	st->st_uid = inode->uid;
	st->st_gid = inode->gid;
	st->st_size = inode->size;
	// TODO: Rock Ridge.
	st->st_atim.tv_sec = inode->Time();
	st->st_atim.tv_nsec = 0;
	// TODO: Rock Ridge.
	st->st_ctim.tv_sec = inode->Time();
	st->st_ctim.tv_nsec = 0;
	// TODO: Rock Ridge.
	st->st_mtim.tv_sec = inode->Time();
	st->st_mtim.tv_nsec = 0;
	st->st_blksize = inode->filesystem->block_size;
	st->st_blocks = divup(st->st_size, (off_t) 512);
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
			case 'r': break;
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
		{
		}
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

	int fd = open(device_path, O_RDONLY);
	if ( fd < 0 )
		error(1, errno, "`%s'", device_path);

	// Search for the Primary Volume Descriptor.
	off_t block_size = 2048;
	struct iso9660_pvd pvd;
	off_t pvd_offset;
	for ( uint32_t i = 16; true; i++ )
	{
		pvd_offset = block_size * i;
		if ( preadall(fd, &pvd, sizeof(pvd), pvd_offset) != sizeof(pvd) )
		{
			if ( errno == EEOF )
				errx(1, "Not a valid ISO 9660 filesystem: %s", device_path);
			else
				err(1, "read: %s", device_path);
		}
		if ( memcmp(pvd.standard_identifier, "CD001", 5) != 0 )
			errx(1, "Not a valid ISO 9660 filesystem: %s", device_path);
		if ( pvd.type == TYPE_PRIMARY_VOLUME_DESCRIPTOR )
			break;
		if ( pvd.type == TYPE_VOLUME_DESCRIPTOR_SET_TERMINATOR )
			errx(1, "Not a valid ISO 9660 filesystem: %s", device_path);
	}

	if ( pvd.version != 1 || pvd.file_structure_version != 1 )
		errx(1, "Unsupported ISO 9660 filesystem version: %s", device_path);
	// TODO: Test if appropriate power of two and allow.
	if ( le32toh(pvd.logical_block_size_le) != block_size )
		errx(1, "Unsupported ISO 9660 block size: %s", device_path);
	block_size = le32toh(pvd.logical_block_size_le);

	Device* dev = new Device(fd, device_path, block_size);
	if ( !dev )
		error(1, errno, "malloc");
	Filesystem* fs = new Filesystem(dev, pretend_mount_path, pvd_offset);
	if ( !fs )
		error(1, errno, "malloc");

	// Change the root inode to be its own . entry instead of the pvd record, so
	// parent directories correctly .. to it, and so Rock Ridge extensions work.
	Inode* root = fs->GetInode(fs->root_ino);
	if ( !root )
		error(1, errno, "GetInode");
	fs->root_ino = 0;
	uint32_t root_lba;
	uint32_t root_size;
	memcpy(&root_lba, pvd.root_dirent + 2, sizeof(root_lba));
	memcpy(&root_size, pvd.root_dirent + 10, sizeof(root_size));
	root->FindParentInode(root_lba, root_size);
	fs->root_ino = root->parent_inode_id;
	root->Unref();

	if ( !mount_path )
		return 0;

#if defined(__sortix__)
	return fsmarshall_main(argv0, mount_path, foreground, fs, dev);
#else
	return iso9660_fuse_main(argv0, mount_path, foreground, fs, dev);
#endif
}
