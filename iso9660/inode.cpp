/*
 * Copyright (c) 2013, 2014, 2015, 2018, 2022 Jonas 'Sortie' Termansen.
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
 * Filesystem inode.
 */

#define __STDC_CONSTANT_MACROS
#define __STDC_LIMIT_MACROS

#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <endian.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "block.h"
#include "device.h"
#include "filesystem.h"
#include "inode.h"
#include "iso9660fs.h"
#include "util.h"

#ifndef S_SETABLE
#define S_SETABLE 02777
#endif
#ifndef O_WRITE
#define O_WRITE (O_WRONLY | O_RDWR)
#endif

Inode::Inode(Filesystem* filesystem, iso9660_ino_t inode_id)
{
	this->prev_inode = NULL;
	this->next_inode = NULL;
	this->prev_hashed = NULL;
	this->next_hashed = NULL;
	this->data_block = NULL;
	this->data = NULL;
	this->filesystem = filesystem;
	this->reference_count = 1;
	this->remote_reference_count = 0;
	this->inode_id = inode_id;
	this->parent_inode_id = 0;
}

Inode::~Inode()
{
	if ( data_block )
		data_block->Unref();
	Unlink();
}

void Inode::Parse()
{
	const uint8_t* block_data = (const uint8_t*) data;
	uid = 0;
	gid = 0;
	time = 0;
	uint8_t file_flags = block_data[25];
	bool is_directory = file_flags & ISO9660_DIRENT_FLAG_DIR;
	mode = 0555 | (is_directory ? ISO9660_S_IFDIR : ISO9660_S_IFREG);
	uint32_t u32;
	memcpy(&u32, block_data + 10, sizeof(u32));
	size = le32toh(u32);
	nlink = 1;
	const uint8_t* time_bytes = block_data + 18;
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = time_bytes[0];
	tm.tm_mon = time_bytes[1] - 1;
	tm.tm_mday = time_bytes[2];
	tm.tm_hour = time_bytes[3];
	tm.tm_min = time_bytes[4];
	tm.tm_sec = time_bytes[5];
	// TODO: Is this accurate?
	time_t tz_offset = (-48 + time_bytes[6]) * 15 * 60;
	// TODO: The timezone offset should've been mixed in with mktime, somehow.
	time = mktime(&tm) + tz_offset;
	uint8_t dirent_len = block_data[0];
	uint8_t name_len = block_data[32];
	size_t extended_off = 33 + name_len + !(name_len & 1);
	for ( size_t i = extended_off; i < dirent_len && 3 <= dirent_len - i; )
	{
		const uint8_t* field = block_data + i;
		uint8_t len = field[2];
		if ( !len || dirent_len - i < len )
			break;
		i += len;
		if ( len == 36 && field[0] == 'P' && field[1] == 'X' && field[3] == 1 )
		{
			uint32_t bits;
			memcpy(&bits, field + 4, sizeof(bits));
			mode = le32toh(bits) & 0xffff;
			memcpy(&bits, field + 12, sizeof(bits));
			nlink = le32toh(bits);
			memcpy(&bits, field + 20, sizeof(bits));
			uid = le32toh(bits);
			memcpy(&bits, field + 28, sizeof(bits));
			gid = le32toh(bits);
		}
	}
	if ( ISO9660_S_ISLNK(mode) )
	{
		uint8_t buf[256];
		size = ReadLink(buf, sizeof(buf));
	}
}

uint32_t Inode::Mode()
{
	return mode;
}

uint32_t Inode::UserId()
{
	return uid;
}

uint32_t Inode::GroupId()
{
	return gid;
}

uint64_t Inode::Size()
{
	return size;
}

uint64_t Inode::Time()
{
	return time;
}

Block* Inode::GetBlock(uint32_t offset)
{
	uint32_t lba;
	memcpy(&lba, (uint8_t*) data + 2, sizeof(lba));
	lba = le32toh(lba);
	uint32_t block_id = lba + offset;
	return filesystem->device->GetBlock(block_id);
}

bool Inode::FindParentInode(uint64_t parent_lba, uint64_t parent_size)
{
	if ( inode_id == filesystem->root_ino )
		return parent_inode_id = inode_id, true;
	Block* block = NULL;
	uint32_t block_size = filesystem->block_size;
	uint64_t parent_offset = parent_lba * block_size;
	uint64_t block_id = 0;
	uint64_t offset = 0;
	while ( offset < parent_size )
	{
		uint64_t entry_block_id = (parent_offset + offset) / block_size;
		uint64_t entry_block_offset = (parent_offset + offset) % block_size;
		if ( block && block_id != entry_block_id )
		{
			block->Unref();
			block = NULL;
		}
		if ( !block && !(block = filesystem->device->GetBlock(entry_block_id)) )
			return false;
		const uint8_t* block_data = block->block_data + entry_block_offset;
		uint8_t dirent_len = block_data[0];
		if ( !dirent_len )
		{
			offset = (entry_block_id + 1) * block_size;
			continue;
		}
		uint8_t name_len = block_data[32];
		const uint8_t* name_data = block_data + 33;
		if ( name_len == 0 || !name_data[0] )
		{
			parent_inode_id = parent_offset + offset;
			return true;
		}
		// TODO: Can dirent_len be misaligned?
		uint64_t reclen = dirent_len + (dirent_len & 1);
		if ( !reclen )
			return errno = EINVAL, false;
		offset += reclen;
	}
	return errno = EINVAL, false;
}

bool Inode::ReadDirectory(uint64_t* offset_inout,
                          Block** block_inout,
                          uint64_t* block_id_inout,
                          char* name,
                          uint8_t* file_type_out,
                          iso9660_ino_t* inode_id_out)
{
	uint64_t offset = *offset_inout;
next_block:
	uint64_t filesize = Size();
	if ( filesize <= offset )
		return errno = 0, false;
	uint64_t entry_block_id = offset / filesystem->block_size;
	uint64_t entry_block_offset = offset % filesystem->block_size;
	if ( *block_inout && *block_id_inout != entry_block_id )
	{
		(*block_inout)->Unref();
		(*block_inout) = NULL;
	}
	if ( !*block_inout &&
	     !(*block_inout = GetBlock(*block_id_inout = entry_block_id)) )
		return false;
	const uint8_t* block_data = (*block_inout)->block_data + entry_block_offset;
	uint8_t dirent_len = block_data[0];
	if ( !dirent_len )
	{
		offset = (entry_block_id + 1) * filesystem->block_size;
		goto next_block;
	}
	if ( name )
	{
		uint8_t name_len = block_data[32];
		const uint8_t* name_data = block_data + 33;
		size_t extended_off = 33 + name_len + !(name_len & 1);
		iso9660_ino_t entry_inode_id =
			((*block_inout)->block_id * filesystem->block_size) +
			entry_block_offset;
		// TODO: The root directory inode should be that of its . entry.
		if ( name_len == 0 || !name_data[0] )
		{
			name[0] = '.';
			name[1] = '\0';
			name_len = 1;
			entry_inode_id = inode_id;
		}
		else if ( name_len == 1 && name_data[0] == 1 )
		{
			name[0] = '.';
			name[1] = '.';
			name[2] = '\0';
			name_len = 2;
			if ( !parent_inode_id )
			{
				uint32_t parent_lba;
				memcpy(&parent_lba, block_data + 2, sizeof(parent_lba));
				parent_lba = le32toh(parent_lba);
				uint32_t parent_size;
				memcpy(&parent_size, block_data + 10, sizeof(parent_size));
				parent_size = le32toh(parent_size);
				if ( !FindParentInode(parent_lba, parent_size) )
					return false;
			}
			entry_inode_id = parent_inode_id;
		}
		else
		{
			for ( size_t i = 0; i < name_len; i++ )
			{
				if ( name_data[i] == ';' )
				{
					name_len = i;
					break;
				}
				name[i] = tolower(name_data[i]);
			}
			name[name_len] = '\0';
		}
		for ( size_t i = extended_off; i < dirent_len && 3 <= dirent_len - i; )
		{
			const uint8_t* field = block_data + i;
			uint8_t len = field[2];
			if ( !len || dirent_len - i < len )
				break;
			i += len;
			if ( 5 <= len && field[0] == 'N' && field[1] == 'M' &&
				 field[3] == 1 )
			{
				uint8_t nm_flags = field[4];
				if ( nm_flags & (1 << 0) ) // TODO: Continued names.
					break;
				name_len = len - 5;
				memcpy(name, field + 5, name_len);
				name[name_len] = '\0';
			}
			// TODO: Other extensions.
		}
		uint8_t file_flags = block_data[25];
		// TODO: Rock Ridge.
		bool is_directory = file_flags & ISO9660_DIRENT_FLAG_DIR;
		uint8_t file_type = is_directory ? ISO9660_FT_DIR : ISO9660_FT_REG_FILE;
		*file_type_out = file_type;
		*inode_id_out = entry_inode_id;
	}
	// TODO: Can dirent_len be misaligned?
	uint64_t reclen = dirent_len + (dirent_len & 1);
	if ( !reclen )
		return errno = EINVAL, false;
	offset += reclen;
	*offset_inout = offset;
	return true;
}

Inode* Inode::Open(const char* elem, int flags, mode_t mode)
{
	if ( !ISO9660_S_ISDIR(Mode()) )
		return errno = ENOTDIR, (Inode*) NULL;
	size_t elem_length = strlen(elem);
	if ( elem_length == 0 )
		return errno = ENOENT, (Inode*) NULL;
	uint64_t offset = 0;
	Block* block = NULL;
	uint64_t block_id = 0;
	char name[256];
	uint8_t file_type;
	iso9660_ino_t inode_id;
	while ( ReadDirectory(&offset, &block, &block_id, name, &file_type,
	                      &inode_id) )
	{
		size_t name_len = strlen(name);
		if ( name_len == elem_length && memcmp(elem, name, elem_length) == 0 )
		{
			block->Unref();
			if ( (flags & O_CREAT) && (flags & O_EXCL) )
				return errno = EEXIST, (Inode*) NULL;
			if ( (flags & O_DIRECTORY) &&
			     file_type != ISO9660_FT_UNKNOWN &&
			     file_type != ISO9660_FT_DIR &&
			     file_type != ISO9660_FT_SYMLINK )
				return errno = ENOTDIR, (Inode*) NULL;
			Inode* inode = filesystem->GetInode(inode_id);
			if ( !inode )
				return (Inode*) NULL;
			if ( flags & O_DIRECTORY &&
			     !ISO9660_S_ISDIR(inode->Mode()) &&
			     !ISO9660_S_ISLNK(inode->Mode()) )
			{
				inode->Unref();
				return errno = ENOTDIR, (Inode*) NULL;
			}
			if ( flags & O_WRITE )
			{
				inode->Unref();
				return errno = EROFS, (Inode*) NULL;
			}
			return inode;
		}
	}
	if ( block )
		block->Unref();
	if ( flags & O_CREAT )
	{
		(void) mode;
		return errno = EROFS, (Inode*) NULL;
	}
	return errno = ENOENT, (Inode*) NULL;
}

bool Inode::Link(const char* elem, Inode* dest)
{
	if ( !ISO9660_S_ISDIR(Mode()) )
		return errno = ENOTDIR, false;
	if ( ISO9660_S_ISDIR(dest->Mode()) )
		return errno = EISDIR, false;

	size_t elem_length = strlen(elem);
	if ( elem_length == 0 )
		return errno = ENOENT, false;
	uint64_t offset = 0;
	Block* block = NULL;
	uint64_t block_id = 0;
	char name[256];
	uint8_t file_type;
	iso9660_ino_t inode_id;
	while ( ReadDirectory(&offset, &block, &block_id, name, &file_type,
	                      &inode_id) )
	{
		size_t name_len = strlen(name);
		if ( name_len == elem_length && memcmp(elem, name, elem_length) == 0 )
		{
			block->Unref();
			return errno = EEXIST, false;
		}
	}
	if ( block )
		block->Unref();
	return errno = EROFS, false;
}

Inode* Inode::UnlinkKeep(const char* elem, bool directories, bool force)
{
	if ( !ISO9660_S_ISDIR(Mode()) )
		return errno = ENOTDIR, (Inode*) NULL;
	size_t elem_length = strlen(elem);
	if ( elem_length == 0 )
		return errno = ENOENT, (Inode*) NULL;
	uint64_t offset = 0;
	Block* block = NULL;
	uint64_t block_id = 0;
	char name[256];
	uint8_t file_type;
	iso9660_ino_t inode_id;
	while ( ReadDirectory(&offset, &block, &block_id, name, &file_type,
	                      &inode_id) )
	{
		size_t name_len = strlen(name);
		if ( name_len == elem_length && memcmp(elem, name, elem_length) == 0 )
		{
			(void) directories;
			(void) force;
			block->Unref();
			return errno = EROFS, (Inode*) NULL;
		}
	}
	if ( block )
		block->Unref();
	return errno = ENOENT, (Inode*) NULL;
}

bool Inode::Unlink(const char* elem, bool directories, bool force)
{
	Inode* result = UnlinkKeep(elem, directories, force);
	if ( !result )
		return false;
	result->Unref();
	return true;
}

ssize_t Inode::ReadLink(uint8_t* buf, size_t bufsize)
{
	size_t result = 0;
	const uint8_t* block_data = (const uint8_t*) data;
	uint8_t dirent_len = block_data[0];
	uint8_t name_len = block_data[32];
	size_t extended_off = 33 + name_len + !(name_len & 1);
	bool continued = true;
	for ( size_t i = extended_off; i < dirent_len && 3 <= dirent_len - i; )
	{
		const uint8_t* field = block_data + i;
		uint8_t len = field[2];
		if ( !len || dirent_len - i < len )
			break;
		i += len;
		if ( 5 <= len && field[0] == 'S' && field[1] == 'L' && field[3] == 1 )
		{
			for ( size_t n = 5; n < len && 2 <= len - n; )
			{
				uint8_t comp_flags = field[n + 0];
				uint8_t comp_len = field[n + 1];
				if ( len - (n + 2) < comp_len )
					break;
				const char* data = (const char*) (field + n + 2);
				size_t datalen = comp_len;
				// TODO: How is a trailing slash encoded?
				if ( !continued || (comp_flags & (1 << 3) /* root */) )
				{
					buf[result++] = '/';
					if ( result == bufsize )
						return (ssize_t) result;
				}
				if ( comp_flags & (1 << 1) )
				{
					data = ".";
					datalen = 1;
				}
				else if ( comp_flags & (1 << 2) )
				{
					data = "..";
					datalen = 2;
				}
				size_t possible = bufsize - result;
				size_t count = datalen < possible ? datalen : possible;
				memcpy(buf + result, data, count);
				result += count;
				if ( result == bufsize )
					return (ssize_t) result;
				continued = comp_flags & (1 << 0);
				n += 2 + comp_len;
			}
		}
	}
	return (ssize_t) result;
}

ssize_t Inode::ReadAt(uint8_t* buf, size_t s_count, off_t o_offset)
{
	if ( !ISO9660_S_ISREG(Mode()) && !ISO9660_S_ISLNK(Mode()) )
		return errno = EISDIR, -1;
	if ( o_offset < 0 )
		return errno = EINVAL, -1;
	if ( SSIZE_MAX < s_count )
		s_count = SSIZE_MAX;
	uint64_t sofar = 0;
	uint64_t count = (uint64_t) s_count;
	uint64_t offset = (uint64_t) o_offset;
	uint64_t file_size = Size();
	if ( file_size <= offset )
		return 0;
	if ( file_size - offset < count )
		count = file_size - offset;
	while ( sofar < count )
	{
		uint64_t block_id = offset / filesystem->block_size;
		uint32_t block_offset = offset % filesystem->block_size;
		uint32_t block_left = filesystem->block_size - block_offset;
		Block* block = GetBlock(block_id);
		if ( !block )
			return sofar ? sofar : -1;
		size_t amount = count - sofar < block_left ? count - sofar : block_left;
		memcpy(buf + sofar, block->block_data + block_offset, amount);
		sofar += amount;
		offset += amount;
		block->Unref();
	}
	return (ssize_t) sofar;
}

bool Inode::Rename(Inode* olddir, const char* oldname, const char* newname)
{
	if ( !strcmp(oldname, ".") || !strcmp(oldname, "..") ||
	     !strcmp(newname, ".") || !strcmp(newname, "..") )
		return errno = EPERM, false;
	Inode* src_inode = olddir->Open(oldname, O_RDONLY, 0);
	if ( !src_inode )
		return false;
	if ( Inode* dst_inode = Open(newname, O_RDONLY, 0) )
	{
		bool same_inode = src_inode->inode_id == dst_inode->inode_id;
		dst_inode->Unref();
		if ( same_inode )
			return src_inode->Unref(), true;
	}
	src_inode->Unref();
	return errno = EROFS, false;
}

bool Inode::RemoveDirectory(const char* path)
{
	return UnlinkKeep(path, true);
}

void Inode::Refer()
{
	reference_count++;
}

void Inode::Unref()
{
	assert(0 < reference_count);
	reference_count--;
	if ( !reference_count && !remote_reference_count )
		delete this;
}

void Inode::RemoteRefer()
{
	remote_reference_count++;
}

void Inode::RemoteUnref()
{
	assert(0 < remote_reference_count);
	remote_reference_count--;
	if ( !reference_count && !remote_reference_count )
		delete this;
}

void Inode::Use()
{
	data_block->Use();
	Unlink();
	Prelink();
}

void Inode::Unlink()
{
	(prev_inode ? prev_inode->next_inode : filesystem->mru_inode) = next_inode;
	(next_inode ? next_inode->prev_inode : filesystem->lru_inode) = prev_inode;
	size_t bin = inode_id % INODE_HASH_LENGTH;
	(prev_hashed ? prev_hashed->next_hashed : filesystem->hash_inodes[bin]) = next_hashed;
	if ( next_hashed ) next_hashed->prev_hashed = prev_hashed;
}

void Inode::Prelink()
{
	prev_inode = NULL;
	next_inode = filesystem->mru_inode;
	if ( filesystem->mru_inode )
		filesystem->mru_inode->prev_inode = this;
	filesystem->mru_inode = this;
	if ( !filesystem->lru_inode )
		filesystem->lru_inode = this;
	size_t bin = inode_id % INODE_HASH_LENGTH;
	prev_hashed = NULL;
	next_hashed = filesystem->hash_inodes[bin];
	filesystem->hash_inodes[bin] = this;
	if ( next_hashed )
		next_hashed->prev_hashed = this;
}
