/*
 * Copyright (c) 2013, 2014, 2017 Jonas 'Sortie' Termansen.
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
 * fs/zero.cpp
 * Bit bucket special device.
 */

#include <sys/types.h>

#include <stdint.h>
#include <string.h>

#include <sortix/stat.h>

#include <sortix/kernel/inode.h>
#include <sortix/kernel/ioctx.h>

#include "zero.h"

namespace Sortix {

Zero::Zero(dev_t dev, ino_t ino, uid_t owner, gid_t group, mode_t mode)
{
	inode_type = INODE_TYPE_STREAM;
	if ( !dev )
		dev = (dev_t) this;
	if ( !ino )
		ino = (ino_t) this;
	this->type = S_IFCHR;
	this->stat_uid = owner;
	this->stat_gid = group;
	this->stat_mode = (mode & S_SETABLE) | this->type;
	this->stat_size = 0;
	this->dev = dev;
	this->ino = ino;
}

Zero::~Zero()
{
}

ssize_t Zero::read(ioctx_t* ctx, uint8_t* buf, size_t count)
{
	if ( !ctx->zero_dest(buf, count) )
		return -1;
	return (ssize_t) count;
}

ssize_t Zero::pread(ioctx_t* ctx, uint8_t* buf, size_t count, off_t /*off*/)
{
	return read(ctx, buf, count);
}

ssize_t Zero::write(ioctx_t* /*ctx*/, const uint8_t* /*buf*/, size_t count)
{
	return count;
}

ssize_t Zero::pwrite(ioctx_t* /*ctx*/, const uint8_t* /*buf*/, size_t count,
                    off_t /*off*/)
{
	return count;
}

} // namespace Sortix
