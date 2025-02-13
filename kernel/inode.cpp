/*
 * Copyright (c) 2012-2017, 2021, 2022 Jonas 'Sortie' Termansen.
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
 * inode.cpp
 * Interfaces and utility classes for implementing inodes.
 */

#include <sys/uio.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <string.h>

#include <sortix/clock.h>
#include <sortix/stat.h>
#include <sortix/statvfs.h>

#include <sortix/kernel/inode.h>
#include <sortix/kernel/interlock.h>
#include <sortix/kernel/ioctx.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/memorymanagement.h>
#include <sortix/kernel/poll.h>
#include <sortix/kernel/refcount.h>
#include <sortix/kernel/time.h>

namespace Sortix {

AbstractInode::AbstractInode()
{
	metalock = KTHREAD_MUTEX_INITIALIZER;
	inode_type = INODE_TYPE_UNKNOWN;
	stat_mode = 0;
	stat_nlink = 0;
	stat_uid = 0;
	stat_gid = 0;
	stat_size = 0;
	stat_atim = Time::Get(CLOCK_REALTIME);
	stat_ctim = Time::Get(CLOCK_REALTIME);
	stat_mtim = Time::Get(CLOCK_REALTIME);
	stat_blksize = Page::Size();
	stat_blocks = 0;
	supports_iovec = false;
}

AbstractInode::~AbstractInode()
{
}

bool AbstractInode::pass()
{
	return true;
}

void AbstractInode::unpass()
{
}

void AbstractInode::linked()
{
	InterlockedIncrement(&stat_nlink);
}

void AbstractInode::unlinked()
{
	InterlockedDecrement(&stat_nlink);
}

int AbstractInode::sync(ioctx_t* /*ctx*/)
{
	return 0;
}

int AbstractInode::stat(ioctx_t* ctx, struct stat* st)
{
	struct stat retst;
	ScopedLock lock(&metalock);
	memset(&retst, 0, sizeof(retst));
	retst.st_dev = dev;
	retst.st_rdev = dev;
	retst.st_ino = ino;
	retst.st_mode = stat_mode;
	retst.st_nlink = (nlink_t) stat_nlink;
	retst.st_uid = stat_uid;
	retst.st_gid = stat_gid;
	retst.st_size = stat_size;
	retst.st_atim = stat_atim;
	retst.st_ctim = stat_ctim;
	retst.st_mtim = stat_mtim;
	retst.st_blksize = stat_blksize;
	retst.st_blocks = stat_size / 512;
	if ( !ctx->copy_to_dest(st, &retst, sizeof(retst)) )
		return -1;
	return 0;
}

// TODO: Provide an easier mechanism for letting subclasses give this
//       information than overriding this method. Additionally, what should be
//       done in abstract kernel objects where this call doesn't make that much
//       sense?
int AbstractInode::statvfs(ioctx_t* ctx, struct statvfs* stvfs)
{
	struct statvfs retstvfs;
	ScopedLock lock(&metalock);
	memset(&retstvfs, 0, sizeof(retstvfs));
	retstvfs.f_bsize = 0;
	retstvfs.f_frsize = 0;
	retstvfs.f_blocks = 0;
	retstvfs.f_bfree = 0;
	retstvfs.f_bavail = 0;
	retstvfs.f_files = 0;
	retstvfs.f_ffree = 0;
	retstvfs.f_favail = 0;
	retstvfs.f_fsid = dev;
	retstvfs.f_flag = ST_NOSUID;
	retstvfs.f_namemax = ULONG_MAX;
	if ( !ctx->copy_to_dest(stvfs, &retstvfs, sizeof(retstvfs)) )
		return -1;
	return 0;
}

int AbstractInode::chmod(ioctx_t* /*ctx*/, mode_t mode)
{
	ScopedLock lock(&metalock);
	stat_mode = (mode & S_SETABLE) | this->type;
	return 0;
}

int AbstractInode::chown(ioctx_t* /*ctx*/, uid_t owner, gid_t group)
{
	ScopedLock lock(&metalock);
	if ( owner != (uid_t) -1 )
		stat_uid = owner;
	if ( group != (gid_t) -1 )
		stat_gid = group;
	return 0;
}

int AbstractInode::truncate(ioctx_t* /*ctx*/, off_t /*length*/)
{
	if ( inode_type == INODE_TYPE_DIR )
		return errno = EISDIR, -1;
	return errno = EINVAL, -1;
}

off_t AbstractInode::lseek(ioctx_t* /*ctx*/, off_t /*offset*/, int /*whence*/)
{
	return errno = ESPIPE, -1;
}

ssize_t AbstractInode::read(ioctx_t* ctx, uint8_t* buf, size_t count)
{
	if ( !supports_iovec )
	{
		if ( inode_type == INODE_TYPE_DIR )
			return errno = EISDIR, -1;
		return errno = EBADF, -1;
	}
	struct iovec iov;
	memset(&iov, 0, sizeof(iov));
	iov.iov_base = (void*) buf;
	iov.iov_len = count;
	return readv(ctx, &iov, 1);
}

ssize_t AbstractInode::readv(ioctx_t* ctx, const struct iovec* iov, int iovcnt)
{
	if ( supports_iovec )
	{
		if ( inode_type == INODE_TYPE_DIR )
			return errno = EISDIR, -1;
		return errno = EBADF, -1;
	}
	ssize_t sofar = 0;
	for ( int i = 0; i < iovcnt && sofar < SSIZE_MAX; i++ )
	{
		size_t maxcount = SSIZE_MAX - sofar;
		uint8_t* buf = (uint8_t*) iov[i].iov_base;
		size_t count = iov[i].iov_len;
		if ( maxcount < count )
			count = maxcount;
		int old_dflags = ctx->dflags;
		if ( sofar )
			ctx->dflags |= O_NONBLOCK;
		ssize_t amount = read(ctx, buf, count);
		ctx->dflags = old_dflags;
		if ( amount < 0 )
			return sofar ? sofar : -1;
		if ( amount == 0 )
			break;
		sofar += amount;
		if ( (size_t) amount < count )
			break;
	}
	return sofar;
}

ssize_t AbstractInode::pread(ioctx_t* ctx, uint8_t* buf, size_t count,
                             off_t off)
{
	if ( !supports_iovec )
	{
		if ( inode_type == INODE_TYPE_STREAM || inode_type == INODE_TYPE_TTY )
			return errno = ESPIPE, -1;
		if ( inode_type == INODE_TYPE_DIR )
			return errno = EISDIR, -1;
		return errno = EBADF, -1;
	}
	struct iovec iov;
	memset(&iov, 0, sizeof(iov));
	iov.iov_base = (void*) buf;
	iov.iov_len = count;
	return preadv(ctx, &iov, 1, off);
}

ssize_t AbstractInode::preadv(ioctx_t* ctx, const struct iovec* iov, int iovcnt,
	                          off_t off)
{
	if ( supports_iovec )
	{
		if ( inode_type == INODE_TYPE_STREAM || inode_type == INODE_TYPE_TTY )
			return errno = ESPIPE, -1;
		if ( inode_type == INODE_TYPE_DIR )
			return errno = EISDIR, -1;
		return errno = EBADF, -1;
	}
	ssize_t sofar = 0;
	for ( int i = 0; i < iovcnt && sofar < SSIZE_MAX; i++ )
	{
		size_t maxcount = SSIZE_MAX - sofar;
		uint8_t* buf = (uint8_t*) iov[i].iov_base;
		size_t count = iov[i].iov_len;
		if ( maxcount < count )
			count = maxcount;
		off_t offset;
		if ( __builtin_add_overflow(off, sofar, &offset) )
			return sofar ? sofar : (errno = EOVERFLOW, -1);
		int old_dflags = ctx->dflags;
		if ( sofar )
			ctx->dflags |= O_NONBLOCK;
		ssize_t amount = pread(ctx, buf, count, offset);
		ctx->dflags = old_dflags;
		if ( amount < 0 )
			return sofar ? sofar : -1;
		if ( amount == 0 )
			break;
		sofar += amount;
		if ( (size_t) amount < count )
			break;
	}
	return sofar;
}

ssize_t AbstractInode::write(ioctx_t* ctx, const uint8_t* buf, size_t count)
{
	if ( !supports_iovec )
		return errno = EBADF, -1;
	struct iovec iov;
	memset(&iov, 0, sizeof(iov));
	iov.iov_base = (void*) buf;
	iov.iov_len = count;
	return writev(ctx, &iov, 1);
}

ssize_t AbstractInode::writev(ioctx_t* ctx, const struct iovec* iov, int iovcnt)
{
	if ( supports_iovec )
		return errno = EBADF, -1;
	ssize_t sofar = 0;
	for ( int i = 0; i < iovcnt && sofar < SSIZE_MAX; i++ )
	{
		size_t maxcount = SSIZE_MAX - sofar;
		const uint8_t* buf = (uint8_t*) iov[i].iov_base;
		size_t count = iov[i].iov_len;
		if ( maxcount < count )
			count = maxcount;
		int old_dflags = ctx->dflags;
		if ( sofar )
			ctx->dflags |= O_NONBLOCK;
		ssize_t amount = write(ctx, buf, count);
		ctx->dflags = old_dflags;
		if ( amount < 0 )
			return sofar ? sofar : -1;
		if ( amount == 0 )
			break;
		sofar += amount;
		if ( (size_t) amount < count )
			break;
	}
	return sofar;
}

ssize_t AbstractInode::pwrite(ioctx_t* ctx, const uint8_t* buf, size_t count,
                              off_t off)
{
	if ( !supports_iovec )
	{
		if ( inode_type == INODE_TYPE_STREAM || inode_type == INODE_TYPE_TTY )
			return errno = ESPIPE, -1;
		return errno = EBADF, -1;
	}
	struct iovec iov;
	memset(&iov, 0, sizeof(iov));
	iov.iov_base = (void*) buf;
	iov.iov_len = count;
	return pwritev(ctx, &iov, 1, off);
}

ssize_t AbstractInode::pwritev(ioctx_t* ctx, const struct iovec* iov,
                               int iovcnt, off_t off)
{
	if ( supports_iovec )
	{
		if ( inode_type == INODE_TYPE_STREAM || inode_type == INODE_TYPE_TTY )
			return errno = ESPIPE, -1;
		return errno = EBADF, -1;
	}
	ssize_t sofar = 0;
	for ( int i = 0; i < iovcnt && sofar < SSIZE_MAX; i++ )
	{
		size_t maxcount = SSIZE_MAX - sofar;
		uint8_t* buf = (uint8_t*) iov[i].iov_base;
		size_t count = iov[i].iov_len;
		if ( maxcount < count )
			count = maxcount;
		off_t offset;
		if ( __builtin_add_overflow(off, sofar, &offset) )
			return sofar ? sofar : (errno = EOVERFLOW, -1);
		int old_dflags = ctx->dflags;
		if ( sofar )
			ctx->dflags |= O_NONBLOCK;
		ssize_t amount = pwrite(ctx, buf, count, offset);
		ctx->dflags = old_dflags;
		if ( amount < 0 )
			return sofar ? sofar : -1;
		if ( amount == 0 )
			break;
		sofar += amount;
		if ( (size_t) amount < count )
			break;
	}
	return sofar;
}

int AbstractInode::utimens(ioctx_t* /*ctx*/, const struct timespec* times)
{
	ScopedLock lock(&metalock);
	struct timespec now = { 0, 0 };
	if ( times[0].tv_nsec == UTIME_NOW || times[1].tv_nsec == UTIME_NOW )
		now = Time::Get(CLOCK_REALTIME);
	if ( times[0].tv_nsec == UTIME_NOW )
		stat_atim = now;
	else if ( times[0].tv_nsec != UTIME_OMIT )
		stat_atim = times[0];
	if ( times[1].tv_nsec == UTIME_NOW )
		stat_mtim = now;
	else if ( times[1].tv_nsec != UTIME_OMIT )
		stat_mtim = times[1];
	return 0;
}

int AbstractInode::isatty(ioctx_t* /*ctx*/)
{
	if ( inode_type == INODE_TYPE_TTY )
		return 1;
	return errno = ENOTTY, 0;
}

ssize_t AbstractInode::readdirents(ioctx_t* /*ctx*/,
                                   struct dirent* /*dirent*/,
                                   size_t /*size*/,
                                   off_t /*start*/)
{
	if ( inode_type == INODE_TYPE_DIR )
		return errno = ENOTDIR, -1;
	return errno = ENOTDIR, -1;
}

Ref<Inode> AbstractInode::open(ioctx_t* /*ctx*/, const char* /*filename*/,
                               int /*flags*/, mode_t /*mode*/)
{
	if ( inode_type == INODE_TYPE_DIR )
		return errno = EBADF, Ref<Inode>(NULL);
	return errno = ENOTDIR, Ref<Inode>(NULL);
}

Ref<Inode> AbstractInode::factory(ioctx_t* /*ctx*/, const char* /*filename*/,
                                  int /*flags*/, mode_t /*mode*/)
{
	if ( inode_type == INODE_TYPE_DIR )
		return errno = EBADF, Ref<Inode>(NULL);
	return errno = ENOTDIR, Ref<Inode>(NULL);
}

int AbstractInode::mkdir(ioctx_t* /*ctx*/, const char* /*filename*/,
                         mode_t /*mode*/)
{
	if ( inode_type == INODE_TYPE_DIR )
		return errno = EBADF, -1;
	return errno = ENOTDIR, -1;
}

int AbstractInode::link(ioctx_t* /*ctx*/, const char* /*filename*/,
                        Ref<Inode> /*node*/)
{
	if ( inode_type == INODE_TYPE_DIR )
		return errno = EBADF, -1;
	return errno = ENOTDIR, -1;
}

int AbstractInode::link_raw(ioctx_t* /*ctx*/, const char* /*filename*/,
                            Ref<Inode> /*node*/)
{
	if ( inode_type == INODE_TYPE_DIR )
		return errno = EBADF, -1;
	return errno = ENOTDIR, -1;
}

int AbstractInode::unlink(ioctx_t* /*ctx*/, const char* /*filename*/)
{
	if ( inode_type == INODE_TYPE_DIR )
		return errno = EBADF, -1;
	return errno = ENOTDIR, -1;
}

int AbstractInode::unlink_raw(ioctx_t* /*ctx*/, const char* /*filename*/)
{
	if ( inode_type == INODE_TYPE_DIR )
		return errno = EBADF, -1;
	return errno = ENOTDIR, -1;
}

int AbstractInode::rmdir(ioctx_t* /*ctx*/, const char* /*filename*/)
{
	if ( inode_type == INODE_TYPE_DIR )
		return errno = EBADF, -1;
	return errno = ENOTDIR, -1;
}

int AbstractInode::rmdir_me(ioctx_t* /*ctx*/)
{
	if ( inode_type == INODE_TYPE_DIR )
		return errno = EBADF, -1;
	return errno = ENOTDIR, -1;
}

int AbstractInode::symlink(ioctx_t* /*ctx*/, const char* /*oldname*/,
                           const char* /*filename*/)
{
	if ( inode_type == INODE_TYPE_DIR )
		return errno = EBADF, -1;
	return errno = ENOTDIR, -1;
}

ssize_t AbstractInode::readlink(ioctx_t* /*ctx*/, char* /*buf*/,
                                size_t /*bufsiz*/)
{
	return errno = EINVAL, -1;
}

int AbstractInode::tcgetwincurpos(ioctx_t* /*ctx*/, struct wincurpos* /*wcp*/)
{
	if ( inode_type == INODE_TYPE_TTY )
		return errno = EBADF, -1;
	return errno = ENOTTY, -1;
}

int AbstractInode::ioctl(ioctx_t* /*ctx*/, int /*cmd*/, uintptr_t /*arg*/)
{
	return errno = ENOTTY, -1;
}

int AbstractInode::tcsetpgrp(ioctx_t* /*ctx*/, pid_t /*pgid*/)
{
	if ( inode_type == INODE_TYPE_TTY )
		return errno = EBADF, -1;
	return errno = ENOTTY, -1;
}

pid_t AbstractInode::tcgetpgrp(ioctx_t* /*ctx*/)
{
	if ( inode_type == INODE_TYPE_TTY )
		return errno = EBADF, -1;
	return errno = ENOTTY, -1;
}

int AbstractInode::settermmode(ioctx_t* /*ctx*/, unsigned /*mode*/)
{
	if ( inode_type == INODE_TYPE_TTY )
		return errno = EBADF, -1;
	return errno = ENOTTY, -1;
}

int AbstractInode::gettermmode(ioctx_t* /*ctx*/, unsigned* /*mode*/)
{
	if ( inode_type == INODE_TYPE_TTY )
		return errno = EBADF, -1;
	return errno = ENOTTY, -1;
}

int AbstractInode::poll(ioctx_t* /*ctx*/, PollNode* node)
{
	short status = POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM;
	if ( !(status & node->events) )
		return errno = EAGAIN, -1;
	node->master->revents |= status & node->events;
	return 0;
}

int AbstractInode::rename_here(ioctx_t* /*ctx*/, Ref<Inode> /*from*/,
                               const char* /*oldname*/, const char* /*newname*/)
{
	if ( inode_type == INODE_TYPE_DIR )
		return errno = EBADF, -1;
	return errno = ENOTDIR, -1;
}

Ref<Inode> AbstractInode::accept4(ioctx_t* /*ctx*/, uint8_t* /*addr*/,
                                  size_t* /*addrlen*/, int /*flags*/)
{
	return errno = ENOTSOCK, Ref<Inode>();
}

int AbstractInode::bind(ioctx_t* /*ctx*/, const uint8_t* /*addr*/,
                        size_t /*addrlen*/)
{
	return errno = ENOTSOCK, -1;
}

int AbstractInode::connect(ioctx_t* /*ctx*/, const uint8_t* /*addr*/,
                           size_t /*addrlen*/)
{
	return errno = ENOTSOCK, -1;
}

int AbstractInode::listen(ioctx_t* /*ctx*/, int /*backlog*/)
{
	return errno = ENOTSOCK, -1;
}

ssize_t AbstractInode::recv(ioctx_t* /*ctx*/, uint8_t* /*buf*/,
                            size_t /*count*/, int /*flags*/)
{
	return errno = ENOTSOCK, -1;
}

ssize_t AbstractInode::recvmsg(ioctx_t* /*ctx*/, struct msghdr* /*msg*/,
                               int /*flags*/)
{
	return errno = ENOTSOCK, -1;
}

ssize_t AbstractInode::send(ioctx_t* /*ctx*/, const uint8_t* /*buf*/,
                            size_t /*count*/, int /*flags*/)
{
	return errno = ENOTSOCK, -1;
}

ssize_t AbstractInode::sendmsg(ioctx_t* /*ctx*/, const struct msghdr* /*msg*/,
                               int /*flags*/)
{
	return errno = ENOTSOCK, -1;
}

int AbstractInode::getsockopt(ioctx_t* /*ctx*/, int /*level*/, int /*option_name*/,
                              void* /*option_value*/, size_t* /*option_size_ptr*/)
{
	return errno = ENOTSOCK, -1;
}

int AbstractInode::setsockopt(ioctx_t* /*ctx*/, int /*level*/, int /*option_name*/,
                              const void* /*option_value*/, size_t /*option_size*/)
{
	return errno = ENOTSOCK, -1;
}

ssize_t AbstractInode::tcgetblob(ioctx_t* /*ctx*/, const char* /*name*/, void* /*buffer*/, size_t /*count*/)
{
	return errno = ENOTTY, -1;
}

ssize_t AbstractInode::tcsetblob(ioctx_t* /*ctx*/, const char* /*name*/, const void* /*buffer*/, size_t /*count*/)
{
	return errno = ENOTTY, -1;
}

int AbstractInode::unmounted(ioctx_t* /*ctx*/)
{
	return 0;
}

int AbstractInode::tcdrain(ioctx_t* /*ctx*/)
{
	return errno = ENOTTY, -1;
}

int AbstractInode::tcflow(ioctx_t* /*ctx*/, int /*action*/)
{
	return errno = ENOTTY, -1;
}

int AbstractInode::tcflush(ioctx_t* /*ctx*/, int /*queue_selector*/)
{
	return errno = ENOTTY, -1;
}

int AbstractInode::tcgetattr(ioctx_t* /*ctx*/, struct termios* /*tio*/)
{
	return errno = ENOTTY, -1;
}

pid_t AbstractInode::tcgetsid(ioctx_t* /*ctx*/)
{
	return errno = ENOTTY, -1;
}

int AbstractInode::tcsendbreak(ioctx_t* /*ctx*/, int /*duration*/)
{
	return errno = ENOTTY, -1;
}

int AbstractInode::tcsetattr(ioctx_t* /*ctx*/, int /*actions*/, const struct termios* /*tio*/)
{
	return errno = ENOTTY, -1;
}

int AbstractInode::shutdown(ioctx_t* /*ctx*/, int /*how*/)
{
	return errno = ENOTSOCK, -1;
}

int AbstractInode::getpeername(ioctx_t* /*ctx*/, uint8_t* /*addr*/,
                               size_t* /*addrsize*/)
{
	return errno = ENOTSOCK, -1;
}

int AbstractInode::getsockname(ioctx_t* /*ctx*/, uint8_t* /*addr*/,
                               size_t* /*addrsize*/)
{
	return errno = ENOTSOCK, -1;
}

} // namespace Sortix
