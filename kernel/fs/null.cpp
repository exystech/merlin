/*
 * Copyright (c) 2013 Jonas 'Sortie' Termansen.
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
 * fs/null.cpp
 * Bit bucket special device.
 */

#include <sys/types.h>

#include <stdint.h>

#include <sortix/stat.h>

#include <sortix/kernel/inode.h>

#include "null.h"

namespace Sortix {

Null::Null(dev_t dev, ino_t ino, uid_t owner, gid_t group, mode_t mode)
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

Null::~Null()
{
}

ssize_t Null::read(ioctx_t* /*ctx*/, uint8_t* /*buf*/, size_t /*count*/)
{
	return 0;
}

ssize_t Null::pread(ioctx_t* /*ctx*/, uint8_t* /*buf*/, size_t /*count*/,
                    off_t /*off*/)
{
	return 0;
}

ssize_t Null::write(ioctx_t* /*ctx*/, const uint8_t* /*buf*/, size_t count)
{
	return count;
}

ssize_t Null::pwrite(ioctx_t* /*ctx*/, const uint8_t* /*buf*/, size_t count,
                     off_t /*off*/)
{
	return count;
}

} // namespace Sortix
