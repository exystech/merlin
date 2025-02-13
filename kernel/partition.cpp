/*
 * Copyright (c) 2013, 2015, 2017 Jonas 'Sortie' Termansen.
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
 * partition.cpp
 * Block device representing a partition of another block device.
 */

#include <sys/types.h>

#include <assert.h>
#include <errno.h>

#include <sortix/seek.h>
#include <sortix/stat.h>

#include <sortix/kernel/inode.h>
#include <sortix/kernel/ioctx.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/refcount.h>

#include "partition.h"

namespace Sortix {

Partition::Partition(Ref<Inode> inner_inode, off_t start, off_t length)
{
	assert(0 <= start);
	assert(0 <= length);
	assert(length <= OFF_MAX - start);
	assert(!(inner_inode->type & S_IFNEVERWRAP));
	assert(S_ISBLK(inner_inode->type) || S_ISREG(inner_inode->type));
	this->dev = (dev_t) this;
	this->ino = (ino_t) this;
	this->type = inner_inode->type;
	this->inode_type = INODE_TYPE_FILE;
	this->inner_inode = inner_inode;
	this->start = start;
	this->length = length;
}

Partition::~Partition()
{
}

int Partition::sync(ioctx_t* ctx)
{
	return inner_inode->sync(ctx);
}

int Partition::truncate(ioctx_t* /*ctx*/, off_t new_length)
{
	if ( new_length != length )
		return errno = EPERM, -1;
	return 0;
}

off_t Partition::lseek(ioctx_t* /*ctx*/, off_t offset, int whence)
{
	if ( whence == SEEK_END && offset == 0 )
		return length;
	return errno = EINVAL, -1;
}

ssize_t Partition::pread(ioctx_t* ctx, uint8_t* buf, size_t count, off_t off)
{
	if ( off < 0 )
		return errno = EINVAL, -1;
	if ( length <= off )
		return 0;
	off_t available = length - off;
	if ( (uintmax_t) available < (uintmax_t) count )
		count = available;
	if ( count == 0 )
		return 0;
	off_t final_offset;
	if ( __builtin_add_overflow(start, off, &final_offset) )
		return errno = EOVERFLOW, -1;
	assert(start <= final_offset);
	assert(final_offset < start + length);
	return inner_inode->pread(ctx, buf, count, final_offset);
}

ssize_t Partition::pwrite(ioctx_t* ctx, const uint8_t* buf, size_t count,
                          off_t off)
{
	if ( off < 0 )
		return errno = EINVAL, -1;
	if ( length <= off )
		return count ? (errno = EFBIG, -1) : 0;
	off_t available = length - off;
	if ( (uintmax_t) available < (uintmax_t) count )
		count = available;
	if ( count == 0 )
		return 0;
	off_t final_offset;
	if ( __builtin_add_overflow(start, off, &final_offset) )
		return errno = EOVERFLOW, -1;
	assert(start <= final_offset);
	assert(final_offset < start + length);
	return inner_inode->pwrite(ctx, buf, count, final_offset);
}

int Partition::stat(ioctx_t* ctx, struct stat* st)
{
	if ( inner_inode->stat(ctx, st) < 0 )
		return -1;
	struct stat myst;
	if ( !ctx->copy_from_src(&myst, st, sizeof(myst)) )
		return -1;
	myst.st_size = length;
	myst.st_blocks = length / 512;
	if ( !ctx->copy_to_dest(st, &myst, sizeof(myst)) )
		return -1;
	return 0;
}

}
