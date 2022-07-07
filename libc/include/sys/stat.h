/*
 * Copyright (c) 2011, 2012, 2013, 2014, 2015, 2018 Jonas 'Sortie' Termansen.
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
 * sys/stat.h
 * Data returned by the stat() function.
 */

#ifndef _INCLUDE_SYS_STAT_H
#define _INCLUDE_SYS_STAT_H

#include <sys/cdefs.h>

#include <sys/__/types.h>

#include <sortix/timespec.h>
#include <sortix/stat.h>

#ifndef __mode_t_defined
#define __mode_t_defined
typedef __mode_t mode_t;
#endif

/* POSIX mandates that we define these compatibility macros to support programs
   that are yet to embrace struct timespec. */
#define st_atime st_atim.tv_sec
#define st_ctime st_ctim.tv_sec
#define st_mtime st_mtim.tv_sec

#ifdef __cplusplus
extern "C" {
#endif

int chmod(const char* path, mode_t mode);
int fchmod(int fd, mode_t mode);
int fchmodat(int dirfd, const char* path, mode_t mode, int flags);
int fstat(int fd, struct stat* st);
int fstatat(int dirfd, const char* path, struct stat* buf, int flags);
int futimens(int fd, const struct timespec times[2]);
int lstat(const char* __restrict path, struct stat* __restrict st);
mode_t getumask(void);
int mkdir(const char* path, mode_t mode);
int mkdirat(int dirfd, const char* path, mode_t mode);
/* TODO: mkfifo */
/* TODO: mkfifoat */
/* TODO: mknod? */
/* TODO: mknodat? */
int stat(const char* __restrict path, struct stat* __restrict st);
mode_t umask(mode_t mask);
int utimens(const char* path, const struct timespec times[2]);
int utimensat(int dirfd, const char* path, const struct timespec times[2],
              int flags);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
