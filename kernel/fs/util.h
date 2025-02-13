/*
 * Copyright (c) 2012 Jonas 'Sortie' Termansen.
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
 * fs/util.h
 * Utility classes for kernel filesystems.
 */

#ifndef SORTIX_FS_UTIL_H
#define SORTIX_FS_UTIL_H

#include <sortix/kernel/inode.h>

namespace Sortix {

class UtilMemoryBuffer : public AbstractInode
{
public:
	UtilMemoryBuffer(dev_t dev, ino_t ino, uid_t owner, gid_t group,
	                 mode_t mode, uint8_t* buf, off_t bufsize,
	                 bool write = true, bool deletebuf = true);
	virtual ~UtilMemoryBuffer();
	virtual int truncate(ioctx_t* ctx, off_t length);
	virtual off_t lseek(ioctx_t* ctx, off_t offset, int whence);
	virtual ssize_t pread(ioctx_t* ctx, uint8_t* buf, size_t count,
	                      off_t off);
	virtual ssize_t pwrite(ioctx_t* ctx, const uint8_t* buf, size_t count,
	                       off_t off);

private:
	kthread_mutex_t filelock;
	uint8_t* buf;
	off_t bufsize;
	bool write;
	bool deletebuf;

};

} // namespace Sortix

#endif
