/*
 * Copyright (c) 2013, 2014, 2015, 2022 Jonas 'Sortie' Termansen.
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
 * filesystem.cpp
 * ISO 9660 filesystem implementation.
 */

#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <stdio.h> // DEBUG

#include "block.h"
#include "device.h"
#include "filesystem.h"
#include "inode.h"
#include "util.h"

Filesystem::Filesystem(Device* device, const char* mount_path,
                       uint64_t pvd_offset)
{
	// TODO: This should be replaced by the . entry within the root directory
	//       so the Rock Ridge extensions are available.
	uint32_t pvd_block_id = pvd_offset / device->block_size;
	this->pvd_block = device->GetBlock(pvd_block_id);
	assert(pvd_block); // TODO: This can fail.
	this->pvd = (struct iso9660_pvd*)
	            (pvd_block->block_data + pvd_offset % device->block_size);
	this->root_ino = pvd_offset + offsetof(struct iso9660_pvd, root_dirent);
	this->device = device;
	this->mount_path = mount_path;
	this->block_size = device->block_size;
	this->mru_inode = NULL;
	this->lru_inode = NULL;
	for ( size_t i = 0; i < INODE_HASH_LENGTH; i++ )
		this->hash_inodes[i] = NULL;
}

Filesystem::~Filesystem()
{
	while ( mru_inode )
		delete mru_inode;
	pvd_block->Unref();
}

Inode* Filesystem::GetInode(iso9660_ino_t inode_id)
{
	size_t bin = inode_id % INODE_HASH_LENGTH;
	for ( Inode* iter = hash_inodes[bin]; iter; iter = iter->next_hashed )
		if ( iter->inode_id == inode_id )
			return iter->Refer(), iter;

	uint32_t block_id = inode_id / block_size;
	uint32_t offset = inode_id % block_size;

	Block* block = device->GetBlock(block_id);
	if ( !block )
		return (Inode*) NULL;
	Inode* inode = new Inode(this, inode_id);
	if ( !inode )
		return block->Unref(), (Inode*) NULL;
	inode->data_block = block;
	uint8_t* buf = inode->data_block->block_data + offset;
	inode->data = (struct iso9660_dirent*) buf;
	inode->Prelink();
	inode->Parse();

	return inode;
}
