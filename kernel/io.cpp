/*
 * Copyright (c) 2011, 2012, 2013, 2014, 2015, 2016 Jonas 'Sortie' Termansen.
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
 * io.cpp
 * Provides system calls for input and output.
 */

#include <sys/socket.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <fsmarshall-msg.h>
#include <stddef.h>
#include <stdint.h>

#include <sortix/dirent.h>
#include <sortix/fcntl.h>
#include <sortix/ioctl.h>
#include <sortix/seek.h>
#include <sortix/socket.h>
#include <sortix/stat.h>
#include <sortix/statvfs.h>
#include <sortix/uio.h>
#include <sortix/unistd.h>

#include <sortix/kernel/copy.h>
#include <sortix/kernel/descriptor.h>
#include <sortix/kernel/dtable.h>
#include <sortix/kernel/ioctx.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/process.h>
#include <sortix/kernel/refcount.h>
#include <sortix/kernel/string.h>
#include <sortix/kernel/syscall.h>
#include <sortix/kernel/thread.h>
#include <sortix/kernel/vnode.h>

#include "partition.h"

namespace Sortix {

static Ref<Descriptor> PrepareLookup(const char* path, int dirfd)
{
	if ( path[0] == '/' )
		return CurrentProcess()->GetRoot();
	if ( dirfd == AT_FDCWD )
		return CurrentProcess()->GetCWD();
	return CurrentProcess()->GetDescriptor(dirfd);
}

ssize_t sys_write(int fd, const void* buffer, size_t count)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	ssize_t ret = desc->write(&ctx, (const uint8_t*) buffer, count);
	return ret;
}

ssize_t sys_pwrite(int fd, const void* buffer, size_t count, off_t off)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	ssize_t ret = desc->pwrite(&ctx, (const uint8_t*) buffer, count, off);
	return ret;
}

ssize_t sys_read(int fd, void* buffer, size_t count)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	ssize_t ret = desc->read(&ctx, (uint8_t*) buffer, count);
	return ret;
}

ssize_t sys_pread(int fd, void* buffer, size_t count, off_t off)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	ssize_t ret = desc->pread(&ctx, (uint8_t*) buffer, count, off);
	return ret;
}

off_t sys_lseek(int fd, off_t offset, int whence)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	off_t ret = desc->lseek(&ctx, offset, whence);
	return ret;
}

int sys_close(int fd)
{
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<DescriptorTable> dtable = CurrentProcess()->GetDTable();
	Ref<Descriptor> desc = dtable->FreeKeep(fd);
	dtable.Reset();
	if ( !desc )
		return -1;
	// TODO: This can be a significant bottleneck. All we need to do is ask the
	//       filesystem for whether any errors occurred that should be reported
	//       at close time.
	//return desc->sync(&ctx);
	return 0;
}

int sys_closefrom(int fd)
{
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<DescriptorTable> dtable = CurrentProcess()->GetDTable();
	int ret = dtable->CloseFrom(fd);
	dtable.Reset();
	return ret;
}

int sys_dup(int fd)
{
	Ref<DescriptorTable> dtable = CurrentProcess()->GetDTable();
	Ref<Descriptor> desc = dtable->Get(fd);
	return dtable->Allocate(desc, 0);
}

int sys_dup3(int oldfd, int newfd, int flags)
{
	if ( flags & ~(O_CLOEXEC | O_CLOFORK) )
		return errno = EINVAL, -1;
	int fd_flags = 0;
	fd_flags |= flags & O_CLOEXEC ? FD_CLOEXEC : 0;
	fd_flags |= flags & O_CLOFORK ? FD_CLOFORK : 0;
	Ref<DescriptorTable> dtable = CurrentProcess()->GetDTable();
	return dtable->Copy(oldfd, newfd, fd_flags);
}

int sys_dup2(int oldfd, int newfd)
{
	if ( oldfd < 0 || newfd < 0 )
		return errno = EINVAL, -1;
	int ret = sys_dup3(oldfd, newfd, 0);
	if ( ret < 0 && errno == EINVAL )
		return errno = 0, newfd;
	return ret;
}

// TODO: If this function fails the file may still have been created. Does a
// standard prohibit this and is that the wrong thing?
int sys_openat(int dirfd, const char* path, int flags, mode_t mode)
{
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	int fdflags = 0;
	if ( flags & O_CLOEXEC ) fdflags |= FD_CLOEXEC;
	if ( flags & O_CLOFORK ) fdflags |= FD_CLOFORK;
	flags &= ~(O_CLOEXEC | O_CLOFORK);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	if ( !from ) { delete[] pathcopy; return -1; }
	Ref<Descriptor> desc = from->open(&ctx, pathcopy, flags, mode);
	from.Reset();
	delete[] pathcopy;
	if ( !desc )
		return -1;
	Ref<DescriptorTable> dtable = CurrentProcess()->GetDTable();
	int ret = dtable->Allocate(desc, fdflags);
	if ( ret < 0 )
	{
		// TODO: We should use a fail-safe dtable reservation mechanism that
		//       causes this error earlier before we have side effects.
	}
	return ret;
}

int sys_faccessat(int dirfd, const char* path, int mode, int flags)
{
	if ( flags & ~(AT_SYMLINK_NOFOLLOW | AT_EACCESS) )
		return errno = EINVAL, -1;
	if ( mode & ~(X_OK | W_OK | R_OK) )
		return errno = EINVAL, -1;
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	if ( !from ) { delete[] pathcopy; return -1; }
	int open_flags = O_READ | (flags & AT_SYMLINK_NOFOLLOW ? O_SYMLINK_NOFOLLOW : 0);
	Ref<Descriptor> desc = from->open(&ctx, pathcopy, open_flags);
	delete[] pathcopy;
	if ( !desc )
		return -1;
	ioctx_t kctx; SetupKernelIOCtx(&kctx);
	struct stat st;
	if ( desc->stat(&kctx, &st) < 0 )
		return -1;
	mode_t mask;
	if ( kctx.uid == st.st_uid )
		mask = 0700;
	else if ( kctx.gid == st.st_gid )
		mask = 0070;
	else
		mask = 0007;
	if ( (mode & X_OK) && !(st.st_mode & 0111 & mask) )
		return errno = EACCES, -1;
	if ( (mode & W_OK) && !(st.st_mode & 0222 & mask) )
		return errno = EACCES, -1;
	if ( (mode & R_OK) && !(st.st_mode & 0444 & mask) )
		return errno = EACCES, -1;
	return 0;
}

int sys_unlinkat(int dirfd, const char* path, int flags)
{
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	if ( !from ) { delete[] pathcopy; return -1; }
	int ret = from->unlinkat(&ctx, pathcopy, flags);
	delete[] pathcopy;
	return ret;
}

int sys_mkdirat(int dirfd, const char* path, mode_t mode)
{
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	if ( !from ) { delete[] pathcopy; return -1; }
	int ret = from->mkdir(&ctx, pathcopy, mode);
	delete[] pathcopy;
	return ret;
}

int sys_truncateat(int dirfd, const char* path, off_t length)
{
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	if ( !from ) { delete[] pathcopy; return -1; }
	Ref<Descriptor> desc = from->open(&ctx, pathcopy, O_WRITE);
	delete[] pathcopy;
	if ( !desc )
		return -1;
	return desc->truncate(&ctx, length);
}

int sys_ftruncate(int fd, off_t length)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->truncate(&ctx, length);
}

int sys_fstatat(int dirfd, const char* path, struct stat* st, int flags)
{
	if ( flags & ~(AT_SYMLINK_NOFOLLOW) )
		return errno = EINVAL;
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	if ( !from ) { delete[] pathcopy; return -1; }
	int open_flags = O_READ | (flags & AT_SYMLINK_NOFOLLOW ? O_SYMLINK_NOFOLLOW : 0);
	Ref<Descriptor> desc = from->open(&ctx, pathcopy, open_flags);
	delete[] pathcopy;
	if ( !desc )
		return -1;
	return desc->stat(&ctx, st);
}

int sys_fstat(int fd, struct stat* st)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->stat(&ctx, st);
}

int sys_fstatvfs(int fd, struct statvfs* stvfs)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->statvfs(&ctx, stvfs);
}

int sys_fstatvfsat(int dirfd, const char* path, struct statvfs* stvfs, int flags)
{
	if ( flags & ~(AT_SYMLINK_NOFOLLOW) )
		return errno = EINVAL;
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	if ( !from ) { delete[] pathcopy; return -1; }
	int open_flags = O_READ | (flags & AT_SYMLINK_NOFOLLOW ? O_SYMLINK_NOFOLLOW : 0);
	Ref<Descriptor> desc = from->open(&ctx, pathcopy, open_flags);
	delete[] pathcopy;
	if ( !desc )
		return -1;
	return desc->statvfs(&ctx, stvfs);
}

int sys_fcntl(int fd, int cmd, uintptr_t arg)
{
	// Operations on the descriptor table.
	Ref<DescriptorTable> dtable = CurrentProcess()->GetDTable();
	if ( cmd == F_PREVFD )
		return dtable->Previous(fd);
	if ( cmd == F_NEXTFD )
		return dtable->Next(fd);

	// Operations on the file descriptior.
	if ( cmd == F_SETFD )
		return dtable->SetFlags(fd, (int) arg) ? 0 : -1;
	if ( cmd == F_GETFD )
		return dtable->GetFlags(fd);

	// Operations on the file description.
	if ( F_DECODE_CMD(F_DECODE_CMD_RAW(cmd)) == F_DUPFD_NUM )
	{
		int fd_flags = F_DECODE_FLAGS(F_DECODE_CMD_RAW(cmd));
		return dtable->Allocate(fd, fd_flags, (int) arg);
	}
	Ref<Descriptor> desc = dtable->Get(fd);
	if ( !desc )
		return -1;
	if ( cmd == F_SETFL )
		return desc->SetFlags((int) arg) ? 0 : -1;
	if ( cmd == F_GETFL )
		return desc->GetFlags();

	return errno = EINVAL, -1;
}

int sys_ioctl(int fd, int cmd, uintptr_t arg)
{
	switch ( cmd )
	{
	case TIOCGWINSZ:
		return sys_tcgetwinsize(fd, (struct winsize*) arg);
	default:
		return errno = EINVAL, -1;
	}
}

ssize_t sys_readdirents(int fd, struct dirent* dirent, size_t size)
{
	if ( SSIZE_MAX < size )
		size = SSIZE_MAX;
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->readdirents(&ctx, dirent, size);
}

int sys_fchdir(int fd)
{
	Process* process = CurrentProcess();
	Ref<Descriptor> desc = process->GetDescriptor(fd);
	if ( !desc )
		return -1;
	if ( !S_ISDIR(desc->type) )
		return errno = ENOTDIR, -1;
	process->SetCWD(desc);
	return 0;
}

int sys_fchdirat(int dirfd, const char* path)
{
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	Ref<Descriptor> desc = from->open(&ctx, pathcopy, O_READ | O_DIRECTORY);
	from.Reset();
	delete[] pathcopy;
	if ( !desc )
		return -1;
	CurrentProcess()->SetCWD(desc);
	return 0;
}

int sys_fchroot(int fd)
{
	Process* process = CurrentProcess();
	Ref<Descriptor> desc = process->GetDescriptor(fd);
	if ( !desc )
		return -1;
	if ( !S_ISDIR(desc->type) )
		return errno = ENOTDIR, -1;
	process->SetRoot(desc);
	return 0;
}

int sys_fchrootat(int dirfd, const char* path)
{
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	Ref<Descriptor> desc = from->open(&ctx, pathcopy, O_READ | O_DIRECTORY);
	from.Reset();
	delete[] pathcopy;
	if ( !desc )
		return -1;
	CurrentProcess()->SetRoot(desc);
	return 0;
}

int sys_fchown(int fd, uid_t owner, gid_t group)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->chown(&ctx, owner, group);
}

int sys_fchownat(int dirfd, const char* path, uid_t owner, gid_t group, int flags)
{
	if ( flags & ~(AT_SYMLINK_NOFOLLOW) )
		return errno = EINVAL, -1;
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	if ( !from ) { delete[] pathcopy; return -1; }
	int open_flags = O_WRITE | (flags & AT_SYMLINK_NOFOLLOW ? O_SYMLINK_NOFOLLOW : 0);
	Ref<Descriptor> desc = from->open(&ctx, pathcopy, open_flags);
	from.Reset();
	delete[] pathcopy;
	if ( !desc )
		return -1;
	return desc->chown(&ctx, owner, group);
}

#if defined(__i386__)
struct fchownat_request /* duplicated in libc/unistd/fchownat.c */
{
	int dirfd;
	const char* path;
	uid_t owner;
	gid_t group;
	int flags;
};

int sys_fchownat_wrapper(const struct fchownat_request* user_request)
{
	struct fchownat_request request;
	if ( !CopyFromUser(&request, user_request, sizeof(request)) )
		return -1;
	return sys_fchownat(request.dirfd, request.path, request.owner,
	                    request.group, request.flags);
}
#endif

int sys_fchmod(int fd, mode_t mode)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->chmod(&ctx, mode);
}

int sys_fchmodat(int dirfd, const char* path, mode_t mode, int flags)
{
	if ( flags & ~(AT_SYMLINK_NOFOLLOW) )
		return errno = EINVAL, -1;
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	if ( !from ) { delete[] pathcopy; return -1; }
	int open_flags = O_WRITE | (flags & AT_SYMLINK_NOFOLLOW ? O_SYMLINK_NOFOLLOW : 0);
	Ref<Descriptor> desc = from->open(&ctx, pathcopy, open_flags);
	from.Reset();
	delete[] pathcopy;
	if ( !desc )
		return -1;
	return desc->chmod(&ctx, mode);
}

int sys_futimens(int fd, const struct timespec* user_times)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->utimens(&ctx, user_times);
}

int sys_utimensat(int dirfd, const char* path,
                  const struct timespec* user_times, int flags)
{
	if ( flags & ~(AT_SYMLINK_NOFOLLOW) )
		return errno = EINVAL, -1;
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	if ( !from ) { delete[] pathcopy; return -1; }
	int open_flags = O_WRITE | (flags & AT_SYMLINK_NOFOLLOW ? O_SYMLINK_NOFOLLOW : 0);
	Ref<Descriptor> desc = from->open(&ctx, pathcopy, open_flags);
	from.Reset();
	delete[] pathcopy;
	if ( !desc )
		return -1;
	return desc->utimens(&ctx, user_times);
}

int sys_linkat(int olddirfd, const char* oldpath,
               int newdirfd, const char* newpath,
               int flags)
{
	if ( flags & ~(AT_SYMLINK_FOLLOW) )
		return errno = EINVAL, -1;

	ioctx_t ctx; SetupUserIOCtx(&ctx);

	char* newpathcopy = GetStringFromUser(newpath);
	if ( !newpathcopy )
		return -1;
	Ref<Descriptor> newfrom(PrepareLookup(newpathcopy, newdirfd));
	if ( !newfrom ) { delete[] newpathcopy; return -1; }

	char* final_elem;
	Ref<Descriptor> dir = OpenDirContainingPath(&ctx, newfrom, newpathcopy,
	                                            &final_elem);
	delete[] newpathcopy;
	if ( !dir )
		return -1;

	char* oldpathcopy = GetStringFromUser(oldpath);
	if ( !oldpathcopy ) { delete[] final_elem; return -1; }
	Ref<Descriptor> oldfrom = PrepareLookup(oldpathcopy, olddirfd);
	if ( !oldfrom ) { delete[] oldpathcopy; delete[] final_elem; return -1; }

	int open_flags = O_READ | (flags & AT_SYMLINK_FOLLOW ? 0 : O_SYMLINK_NOFOLLOW);
	Ref<Descriptor> file = oldfrom->open(&ctx, oldpathcopy, open_flags);
	delete[] oldpathcopy;
	if ( !file ) { delete[] final_elem; return -1; }

	int ret = dir->link(&ctx, final_elem, file);
	delete[] final_elem;
	return ret;
}

int sys_symlinkat(const char* oldpath, int newdirfd, const char* newpath)
{
	ioctx_t ctx; SetupUserIOCtx(&ctx);

	char* newpath_copy = GetStringFromUser(newpath);
	if ( !newpath_copy )
		return -1;
	char* oldpath_copy = GetStringFromUser(oldpath);
	if ( !oldpath_copy )
		return delete[] newpath_copy, -1;

	Ref<Descriptor> newfrom(PrepareLookup(newpath_copy, newdirfd));
	if ( !newfrom )
		return delete[] newpath_copy, -1;

	int ret = newfrom->symlink(&ctx, oldpath, newpath_copy);

	delete[] oldpath_copy;
	delete[] newpath_copy;

	return ret;
}

int sys_settermmode(int fd, unsigned mode)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->settermmode(&ctx, mode);
}

int sys_gettermmode(int fd, unsigned* mode)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->gettermmode(&ctx, mode);
}

int sys_isatty(int fd)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return 0;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->isatty(&ctx);
}

int sys_tcgetwincurpos(int fd, struct wincurpos* wcp)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->tcgetwincurpos(&ctx, wcp);
}


int sys_tcgetwinsize(int fd, struct winsize* ws)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->tcgetwinsize(&ctx, ws);
}

int sys_tcsetpgrp(int fd, pid_t pgid)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->tcsetpgrp(&ctx, pgid);
}

int sys_tcgetpgrp(int fd)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->tcgetpgrp(&ctx);
}

static int sys_renameat_inner(int olddirfd, const char* oldpath,
                              int newdirfd, const char* newpath)
{
	Ref<Descriptor> olddir(PrepareLookup(oldpath, olddirfd));
	if ( !olddir )
		return -1;

	Ref<Descriptor> newdir(PrepareLookup(newpath, newdirfd));
	if ( !newdir )
		return -1;

	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return newdir->rename_here(&ctx, olddir, oldpath, newpath);
}

int sys_renameat(int olddirfd, const char* oldpath,
                 int newdirfd, const char* newpath)
{
	char* oldpathcopy = GetStringFromUser(oldpath);
	if ( !oldpathcopy ) return -1;
	char* newpathcopy = GetStringFromUser(newpath);
	if ( !newpathcopy ) { delete[] oldpathcopy; return -1; }
	int ret = sys_renameat_inner(olddirfd, oldpathcopy, newdirfd, newpathcopy);
	delete[] newpathcopy;
	delete[] oldpathcopy;
	return ret;
}

// TODO: This should probably be moved into user-space. It'd be nice if
// user-space could just open the symlink and read/write it like a regular file.
ssize_t sys_readlinkat(int dirfd, const char* path, char* buf, size_t size)
{
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	if ( !from ) { delete[] pathcopy; return -1; }
	Ref<Descriptor> desc = from->open(&ctx, pathcopy, O_READ | O_SYMLINK_NOFOLLOW);
	delete[] pathcopy;
	if ( !desc )
		return -1;
	return (int) desc->readlink(&ctx, buf, size);
}

int sys_fsync(int fd)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->sync(&ctx);
}

int sys_accept4(int fd, void* addr, size_t* addrlen, int flags)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	int fdflags = 0;
	if ( flags & SOCK_CLOEXEC ) fdflags |= FD_CLOEXEC;
	if ( flags & SOCK_CLOFORK ) fdflags |= FD_CLOFORK;
	flags &= ~(SOCK_CLOEXEC | SOCK_CLOFORK);
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> conn = desc->accept(&ctx, (uint8_t*) addr, addrlen, flags);
	if ( !conn )
		return -1;
	if ( flags & SOCK_NONBLOCK )
		conn->SetFlags(conn->GetFlags() | O_NONBLOCK);
	return CurrentProcess()->GetDTable()->Allocate(conn, fdflags);
}

int sys_bind(int fd, const void* addr, size_t addrlen)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->bind(&ctx, (const uint8_t*) addr, addrlen);
}

int sys_connect(int fd, const void* addr, size_t addrlen)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->connect(&ctx, (const uint8_t*) addr, addrlen);
}

int sys_listen(int fd, int backlog)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->listen(&ctx, backlog);
}

ssize_t sys_recv(int fd, void* buffer, size_t count, int flags)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->recv(&ctx, (uint8_t*) buffer, count, flags);
}

ssize_t sys_send(int fd, const void* buffer, size_t count, int flags)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->send(&ctx, (const uint8_t*) buffer, count, flags);
}

// TODO: We need to move these vector operations into the file descriptors or
//       inodes themselves to ensure that they are atomic. Currently these
//       operations may overlap and cause nasty bugs/race conditions when
//       multiple threads concurrently operates on a file.
// TODO: There is quite a bit of boiler plate code here. Can we do better?

static struct iovec* FetchIOV(const struct iovec* user_iov, int iovcnt)
{
	if ( iovcnt < 0 )
		return errno = EINVAL, (struct iovec*) NULL;
	struct iovec* ret = new struct iovec[iovcnt];
	if ( !ret )
		return NULL;
	if ( !CopyFromUser(ret, user_iov, sizeof(struct iovec) * (size_t) iovcnt) )
	{
		delete[] ret;
		return NULL;
	}
	return ret;
}

ssize_t sys_readv(int fd, const struct iovec* user_iov, int iovcnt)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	struct iovec* iov = FetchIOV(user_iov, iovcnt);
	if ( !iov )
		return -1;
	ssize_t so_far = 0;
	for ( int i = 0; i < iovcnt && so_far != SSIZE_MAX; i++ )
	{
		uint8_t* buffer = (uint8_t*) iov[i].iov_base;
		size_t amount = iov[i].iov_len;
		ssize_t max_left = SSIZE_MAX - so_far;
		if ( (size_t) max_left < amount )
			amount = (size_t) max_left;
		ssize_t num_bytes = desc->read(&ctx, buffer, amount);
		if ( num_bytes < 0 )
		{
			delete[] iov;
			return so_far ? so_far : -1;
		}
		if ( num_bytes == 0 )
			break;
		so_far += num_bytes;

		// TODO: Is this the correct behavior?
		if ( (size_t) num_bytes != amount )
			break;
	}
	delete[] iov;
	return so_far;
}

ssize_t sys_preadv(int fd, const struct iovec* user_iov, int iovcnt, off_t offset)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	struct iovec* iov = FetchIOV(user_iov, iovcnt);
	if ( !iov )
		return -1;
	ssize_t so_far = 0;
	for ( int i = 0; i < iovcnt && so_far != SSIZE_MAX; i++ )
	{
		uint8_t* buffer = (uint8_t*) iov[i].iov_base;
		size_t amount = iov[i].iov_len;
		ssize_t max_left = SSIZE_MAX - so_far;
		if ( (size_t) max_left < amount )
			amount = (size_t) max_left;
		ssize_t num_bytes = desc->pread(&ctx, buffer, amount, offset + so_far);
		if ( num_bytes < 0 )
		{
			delete[] iov;
			return so_far ? so_far : -1;
		}
		if ( num_bytes == 0 )
			break;
		so_far += num_bytes;

		// TODO: Is this the correct behavior?
		if ( (size_t) num_bytes != amount )
			break;
	}
	delete[] iov;
	return so_far;
}

ssize_t sys_writev(int fd, const struct iovec* user_iov, int iovcnt)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	struct iovec* iov = FetchIOV(user_iov, iovcnt);
	if ( !iov )
		return -1;
	ssize_t so_far = 0;
	for ( int i = 0; i < iovcnt && so_far != SSIZE_MAX; i++ )
	{
		const uint8_t* buffer = (const uint8_t*) iov[i].iov_base;
		size_t amount = iov[i].iov_len;
		ssize_t max_left = SSIZE_MAX - so_far;
		if ( (size_t) max_left < amount )
			amount = (size_t) max_left;
		ssize_t num_bytes = desc->write(&ctx, buffer, amount);
		if ( num_bytes < 0 )
		{
			delete[] iov;
			return so_far ? so_far : -1;
		}
		if ( num_bytes == 0 )
			break;
		so_far += num_bytes;

		// TODO: Is this the correct behavior?
		if ( (size_t) num_bytes != amount )
			break;
	}
	delete[] iov;
	return so_far;
}

ssize_t sys_pwritev(int fd, const struct iovec* user_iov, int iovcnt, off_t offset)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	struct iovec* iov = FetchIOV(user_iov, iovcnt);
	if ( !iov )
		return -1;
	ssize_t so_far = 0;
	for ( int i = 0; i < iovcnt && so_far != SSIZE_MAX; i++ )
	{
		const uint8_t* buffer = (const uint8_t*) iov[i].iov_base;
		size_t amount = iov[i].iov_len;
		ssize_t max_left = SSIZE_MAX - so_far;
		if ( (size_t) max_left < amount )
			amount = (size_t) max_left;
		ssize_t num_bytes = desc->pwrite(&ctx, buffer, amount, offset + so_far);
		if ( num_bytes < 0 )
		{
			delete[] iov;
			return so_far ? so_far : -1;
		}
		if ( num_bytes == 0 )
			break;
		so_far += num_bytes;

		// TODO: Is this the correct behavior?
		if ( (size_t) num_bytes != amount )
			break;
	}
	delete[] iov;
	return so_far;
}

int sys_mkpartition(int fd, off_t start, off_t length, int flags)
{
	int fdflags = 0;
	if ( flags & O_CLOEXEC ) fdflags |= FD_CLOEXEC;
	if ( flags & O_CLOFORK ) fdflags |= FD_CLOFORK;

	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;

	int dflags = desc->GetFlags();
	Ref<Inode> inner_inode = desc->vnode->inode;
	desc.Reset();

	Ref<Inode> partition(new Partition(inner_inode, start, length));
	if ( !partition )
		return -1;
	inner_inode.Reset();

	Ref<Vnode> partition_vnode(new Vnode(partition, Ref<Vnode>(NULL), 0, 0));
	if ( !partition_vnode )
		return -1;
	partition.Reset();

	Ref<Descriptor> partition_desc(new Descriptor(partition_vnode, dflags));
	if ( !partition_desc )
		return -1;
	partition_vnode.Reset();

	return CurrentProcess()->GetDTable()->Allocate(partition_desc, fdflags);
}

ssize_t sys_sendmsg(int fd, const struct msghdr* user_msg, int flags)
{
	struct msghdr msg;
	if ( !CopyFromUser(&msg, user_msg, sizeof(msg)) )
		return -1;
	// TODO: MSG_DONTWAIT and MSG_NOSIGNAL aren't actually supported here!
	if ( flags & ~(MSG_EOR | MSG_DONTWAIT | MSG_NOSIGNAL) )
		return errno = EINVAL, -1;
	if ( msg.msg_name )
		return errno = EINVAL, -1;
	if ( msg.msg_control && msg.msg_controllen )
		return errno = EINVAL, -1;
	return sys_writev(fd, msg.msg_iov, msg.msg_iovlen);
}

ssize_t sys_recvmsg(int fd, struct msghdr* user_msg, int flags)
{
	struct msghdr msg;
	if ( !CopyFromUser(&msg, user_msg, sizeof(msg)) )
		return -1;
	if ( flags & ~(MSG_CMSG_CLOEXEC | MSG_DONTWAIT) )
		return errno = EINVAL, -1;
	if ( msg.msg_name )
		return errno = EINVAL, -1;
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;

	// TODO: This is not atomic.
	int old_flags = desc->GetFlags();
	desc->SetFlags(old_flags | O_NONBLOCK);
	ssize_t result = sys_readv(fd, msg.msg_iov, msg.msg_iovlen);
	desc->SetFlags(old_flags);

	msg.msg_flags = 0;
	if ( !CopyToUser(&user_msg->msg_flags, &msg.msg_flags, sizeof(msg.msg_flags)) )
		return -1;

	return result;
}

int sys_getsockopt(int fd, int level, int option_name,
                   void* option_value, size_t* option_size_ptr)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;

	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->getsockopt(&ctx, level, option_name, option_value, option_size_ptr);
}

int sys_setsockopt(int fd, int level, int option_name,
                   const void* option_value, size_t option_size)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;

	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->setsockopt(&ctx, level, option_name, option_value, option_size);
}

ssize_t sys_tcgetblob(int fd, const char* name, void* buffer, size_t count)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;

	char* name_copy = NULL;
	if ( name && !(name_copy = GetStringFromUser(name)) )
		return -1;

	ioctx_t ctx; SetupUserIOCtx(&ctx);
	ssize_t result = desc->tcgetblob(&ctx, name_copy, buffer, count);
	delete[] name_copy;
	return result;
}

ssize_t sys_tcsetblob(int fd, const char* name, const void* buffer, size_t count)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;

	char* name_copy = NULL;
	if ( name && !(name_copy = GetStringFromUser(name)) )
		return -1;

	ioctx_t ctx; SetupUserIOCtx(&ctx);
	ssize_t result = desc->tcsetblob(&ctx, name_copy, buffer, count);
	delete[] name_copy;
	return result;
}

int sys_getpeername(int fd, struct sockaddr* addr, socklen_t* addrsize)
{
	(void) fd;
	(void) addr;
	(void) addrsize;
	return errno = ENOSYS, -1;
}

int sys_getsockname(int fd, struct sockaddr* addr, socklen_t* addrsize)
{
	(void) fd;
	(void) addr;
	(void) addrsize;
	return errno = ENOSYS, -1;
}

int sys_shutdown(int fd, int how)
{
	(void) fd;
	(void) how;
	return errno = ENOSYS, -1;
}

int sys_unmountat(int dirfd, const char* path, int flags)
{
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	if ( !from )
		return delete[] pathcopy, -1;
	int ret = from->unmount(&ctx, pathcopy, flags);
	delete[] pathcopy;
	return ret;
}

int sys_fsm_fsbind(int rootfd, int mpointfd, int flags)
{
	if ( flags & ~(0) )
		return errno = EINVAL, -1;
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(rootfd);
	if ( !desc )
		return -1;
	Ref<Descriptor> mpoint = CurrentProcess()->GetDescriptor(mpointfd);
	if ( !mpoint )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return mpoint->fsm_fsbind(&ctx, desc, flags);
}

int sys_fsm_mountat(int dirfd, const char* path, const struct stat* rootst, int flags)
{
	if ( flags & ~(FSM_MOUNT_CLOEXEC | FSM_MOUNT_CLOFORK |
	               FSM_MOUNT_NOFOLLOW | FSM_MOUNT_NONBLOCK) )
		return -1;
	int fdflags = 0;
	if ( flags & FSM_MOUNT_CLOEXEC ) fdflags |= FD_CLOEXEC;
	if ( flags & FSM_MOUNT_CLOFORK ) fdflags |= FD_CLOFORK;
	flags &= ~(FSM_MOUNT_CLOEXEC | FSM_MOUNT_CLOFORK);
	char* pathcopy = GetStringFromUser(path);
	if ( !pathcopy )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	Ref<Descriptor> from = PrepareLookup(pathcopy, dirfd);
	if ( !from )
		return delete[] pathcopy, -1;
	Ref<Descriptor> desc = from->fsm_mount(&ctx, pathcopy, rootst, flags);
	delete[] pathcopy;
	if ( !desc )
		return -1;
	Ref<DescriptorTable> dtable = CurrentProcess()->GetDTable();
	int ret = dtable->Allocate(desc, fdflags);
	if ( ret < 0 )
	{
		// TODO: We should use a fail-safe dtable reservation mechanism that
		//       causes this error earlier before we have side effects.
		int errnum = errno;
		from->unmount(&ctx, pathcopy, 0);
		return errno = errnum, -1;
	}
	return ret;
}

int sys_tcdrain(int fd)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->tcdrain(&ctx);
}

int sys_tcflow(int fd, int action)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->tcflow(&ctx, action);
}

int sys_tcflush(int fd, int queue_selector)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->tcflush(&ctx, queue_selector);
}

int sys_tcgetattr(int fd, struct termios* tio)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->tcgetattr(&ctx, tio);
}

pid_t sys_tcgetsid(int fd)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->tcgetsid(&ctx);
}

int sys_tcsendbreak(int fd, int duration)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->tcsendbreak(&ctx, duration);
}

int sys_tcsetattr(int fd, int actions, const struct termios* tio)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(fd);
	if ( !desc )
		return -1;
	ioctx_t ctx; SetupUserIOCtx(&ctx);
	return desc->tcsetattr(&ctx, actions, tio);
}

} // namespace Sortix
