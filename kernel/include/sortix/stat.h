/*
 * Copyright (c) 2012, 2013, 2014, 2015, 2016 Jonas 'Sortie' Termansen.
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
 * sortix/stat.h
 * Defines the struct stat used for file meta-information and other useful
 * macros and values relating to values stored in it.
 */

#ifndef INCLUDE_SORTIX_STAT_H
#define INCLUDE_SORTIX_STAT_H

#include <sys/cdefs.h>

#include <sys/types.h>

#include <sortix/__/dirent.h>
#include <sortix/__/dt.h>
#include <sortix/__/stat.h>

#include <sortix/timespec.h>

#ifdef __cplusplus
extern "C" {
#endif

struct stat
{
	dev_t st_dev;
	dev_t st_rdev;
	ino_t st_ino;
	mode_t st_mode;
	nlink_t st_nlink;
	uid_t st_uid;
	gid_t st_gid;
	off_t st_size;
	struct timespec st_atim;
	struct timespec st_mtim;
	struct timespec st_ctim;
	blksize_t st_blksize;
	blkcnt_t st_blocks;
};

#define S_IXOTH 01
#define S_IWOTH 02
#define S_IROTH 04
#define S_IRWXO 07
#define S_IXGRP 010
#define S_IWGRP 020
#define S_IRGRP 040
#define S_IRWXG 070
#define S_IXUSR 0100
#define S_IWUSR 0200
#define S_IRUSR 0400
#define S_IRWXU 0700
#define S_IFMT __DTTOIF(__DT_BITS)
#define S_IFSOCK __DTTOIF(__DT_SOCK)
#define S_IFLNK __DTTOIF(__DT_LNK)
#define S_IFREG __DTTOIF(__DT_REG)
#define S_IFBLK __DTTOIF(__DT_BLK)
#define S_IFDIR __DTTOIF(__DT_DIR)
#define S_IFCHR __DTTOIF(__DT_CHR)
#define S_IFIFO __DTTOIF(__DT_FIFO)
#define S_ISUID 0x0800
#define S_ISGID 0x0400
#define S_ISVTX 0x0200
#define S_SETABLE (0777 | 0x0200 | 0x0400 | 0x0800)
#define S_ISSOCK(mode) (((mode) & S_IFMT) == S_IFSOCK)
#define S_ISLNK(mode) (((mode) & S_IFMT) == S_IFLNK)
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#define S_ISBLK(mode) (((mode) & S_IFMT) == S_IFBLK)
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISCHR(mode) (((mode) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFIFO)

#define UTIME_NOW  0x3FFFFFFF
#define UTIME_OMIT 0x3FFFFFFE

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
