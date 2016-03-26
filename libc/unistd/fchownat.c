/*
 * Copyright (c) 2012, 2016 Jonas 'Sortie' Termansen.
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
 * unistd/fchownat.c
 * Changes the owner and group of a file.
 */

#include <sys/types.h>
#include <sys/syscall.h>

#include <fcntl.h>
#include <unistd.h>

#if !defined(__i386__)

DEFN_SYSCALL5(int, sys_fchownat, SYSCALL_FCHOWNAT, int, const char*, uid_t, gid_t, int);

int fchownat(int dirfd, const char* path, uid_t owner, gid_t group, int flags)
{
	return sys_fchownat(dirfd, path, owner, group, flags);
}

#else

// TODO: The i386 system call ABI doesn't have enough registers to do this with
//       64-bit uid_t and gid_t.

struct fchownat_request /* duplicated in kernel/io.cpp */
{
	int dirfd;
	const char* path;
	uid_t owner;
	gid_t group;
	int flags;
};

DEFN_SYSCALL1(int, sys_fchownat_wrapper, SYSCALL_FCHOWNAT, const struct fchownat_request*);

int fchownat(int dirfd, const char* path, uid_t owner, gid_t group, int flags)
{
	struct fchownat_request request = { dirfd, path, owner, group, flags };
	return sys_fchownat_wrapper(&request);
}

#endif
