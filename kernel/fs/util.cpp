/*
 * Copyright (c) 2012, 2017 Jonas 'Sortie' Termansen.
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
 * fs/util.cpp
 * Utility classes for kernel filesystems.
 */

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <sortix/stat.h>
#include <sortix/seek.h>

#include <sortix/kernel/inode.h>
#include <sortix/kernel/interlock.h>
#include <sortix/kernel/ioctx.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/refcount.h>

#include "util.h"

namespace Sortix {

UtilMemoryBuffer::UtilMemoryBuffer(dev_t dev, ino_t ino, uid_t owner,
                                   gid_t group, mode_t mode, uint8_t* buf,
                                   off_t bufsize, bool write, bool deletebuf)
{
	assert(0 <= bufsize);
	inode_type = INODE_TYPE_FILE;
	this->filelock = KTHREAD_MUTEX_INITIALIZER;
	this->stat_uid = owner;
	this->stat_gid = group;
	this->type = S_IFREG;
	this->stat_mode = (mode & S_SETABLE) | this->type;
	this->stat_size = bufsize;
	this->dev = dev;
	this->ino = ino ? ino : (ino_t) this;
	this->buf = buf;
	this->bufsize = bufsize;
	this->write = write;
	this->deletebuf = deletebuf;
}

UtilMemoryBuffer::~UtilMemoryBuffer()
{
	if ( deletebuf )
		delete[] buf;
}

int UtilMemoryBuffer::truncate(ioctx_t* /*ctx*/, off_t length)
{
	ScopedLock lock(&filelock);
	if ( length != bufsize )
		return errno = EPERM, -1;
	return 0;
}

off_t UtilMemoryBuffer::lseek(ioctx_t* /*ctx*/, off_t offset, int whence)
{
	ScopedLock lock(&filelock);
	if ( whence == SEEK_END && offset == 0 )
		return bufsize;
	return errno = EINVAL, -1;
}

ssize_t UtilMemoryBuffer::pread(ioctx_t* ctx, uint8_t* dest, size_t count,
                                off_t off)
{
	ScopedLock lock(&filelock);
	if ( bufsize < off )
		return 0;
	off_t available = bufsize - off;
	if ( (uintmax_t) available < (uintmax_t) count )
		count = available;
	if ( !ctx->copy_to_dest(dest, buf + off, count) )
		return -1;
	return count;
}

ssize_t UtilMemoryBuffer::pwrite(ioctx_t* ctx, const uint8_t* src, size_t count,
                                 off_t off)
{
	ScopedLock lock(&filelock);
	if ( !write )
		return errno = EROFS, -1;
	if ( bufsize < off )
		return errno = EFBIG, -1;
	off_t available = bufsize - off;
	if ( available == 0 && count )
		return errno = EFBIG, -1;
	if ( (uintmax_t) available < (uintmax_t) count )
		count = available;
	if ( !ctx->copy_from_src(buf + off, src, count) )
		return -1;
	return count;
}

} // namespace Sortix
