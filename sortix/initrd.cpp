/*******************************************************************************

	Copyright(C) Jonas 'Sortie' Termansen 2011, 2012.

	This file is part of Sortix.

	Sortix is free software: you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software
	Foundation, either version 3 of the License, or (at your option) any later
	version.

	Sortix is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
	FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
	details.

	You should have received a copy of the GNU General Public License along with
	Sortix. If not, see <http://www.gnu.org/licenses/>.

	initrd.cpp
	Provides low-level access to a Sortix init ramdisk.

*******************************************************************************/

#include <sortix/kernel/platform.h>
#include <sortix/kernel/memorymanagement.h>
#include <sortix/stat.h>
#include <sortix/mman.h>
#include <sortix/initrd.h>
#include <libmaxsi/error.h>
#include <libmaxsi/memory.h>
#include <libmaxsi/string.h>
#include <libmaxsi/crc32.h>
#include "initrd.h"
#include "syscall.h"

using namespace Maxsi;

namespace Sortix {
namespace InitRD {

uint8_t* initrd;
size_t initrdsize;
const initrd_superblock_t* sb;

static uint32_t HostModeToInitRD(mode_t mode)
{
	uint32_t result = mode & 0777; // Lower 9 bits per POSIX and tradition.
	if ( S_ISVTX & mode ) { result |= INITRD_S_ISVTX; }
	if ( S_ISSOCK(mode) ) { result |= INITRD_S_IFSOCK; }
	if ( S_ISLNK(mode) ) { result |= INITRD_S_IFLNK; }
	if ( S_ISREG(mode) ) { result |= INITRD_S_IFREG; }
	if ( S_ISBLK(mode) ) { result |= INITRD_S_IFBLK; }
	if ( S_ISDIR(mode) ) { result |= INITRD_S_IFDIR; }
	if ( S_ISCHR(mode) ) { result |= INITRD_S_IFCHR; }
	if ( S_ISFIFO(mode) ) { result |= INITRD_S_IFIFO; }
	return result;
}

static mode_t InitRDModeToHost(uint32_t mode)
{
	mode_t result = mode & 0777; // Lower 9 bits per POSIX and tradition.
	if ( INITRD_S_ISVTX & mode ) { result |= S_ISVTX; }
	if ( INITRD_S_ISSOCK(mode) ) { result |= S_IFSOCK; }
	if ( INITRD_S_ISLNK(mode) ) { result |= S_IFLNK; }
	if ( INITRD_S_ISREG(mode) ) { result |= S_IFREG; }
	if ( INITRD_S_ISBLK(mode) ) { result |= S_IFBLK; }
	if ( INITRD_S_ISDIR(mode) ) { result |= S_IFDIR; }
	if ( INITRD_S_ISCHR(mode) ) { result |= S_IFCHR; }
	if ( INITRD_S_ISFIFO(mode) ) { result |= S_IFIFO; }
	return result;
}

uint32_t Root()
{
	return sb->root;
}

static const initrd_inode_t* GetInode(uint32_t inode)
{
	if ( sb->inodecount <= inode ) { Error::Set(EINVAL); return NULL; }
	uint32_t pos = sb->inodeoffset + sb->inodesize * inode;
	return (const initrd_inode_t*) (initrd + pos);
}

bool Stat(uint32_t ino, struct stat* st)
{
	const initrd_inode_t* inode = GetInode(ino);
	if ( !inode ) { return false; }
	st->st_ino = ino;
	st->st_mode = HostModeToInitRD(inode->mode);
	st->st_nlink = inode->nlink;
	st->st_uid = inode->uid;
	st->st_gid = inode->gid;
	st->st_size = inode->size;
	st->st_atime = inode->mtime;
	st->st_ctime = inode->ctime;
	st->st_mtime = inode->mtime;
	st->st_blksize = 1;
	st->st_blocks = inode->size;
	return true;
}

uint8_t* Open(uint32_t ino, size_t* size)
{
	const initrd_inode_t* inode = GetInode(ino);
	if ( !inode ) { return NULL; }
	*size = inode->size;
	return initrd + inode->dataoffset;
}

uint32_t Traverse(uint32_t ino, const char* name)
{
	const initrd_inode_t* inode = GetInode(ino);
	if ( !inode ) { return 0; }
	if ( !INITRD_S_ISDIR(inode->mode) ) { Error::Set(ENOTDIR); return 0; }
	uint32_t offset = 0;
	while ( offset < inode->size )
	{
		uint32_t pos = inode->dataoffset + offset;
		const initrd_dirent* dirent = (const initrd_dirent*) (initrd + pos);
		if ( dirent->namelen && !String::Compare(dirent->name, name) )
		{
			return dirent->inode;
		}
		offset += dirent->reclen;
	}
	Error::Set(ENOENT);
	return 0;
}


const char* GetFilename(uint32_t dir, size_t index)
{
	const initrd_inode_t* inode = GetInode(dir);
	if ( !inode ) { return 0; }
	if ( !INITRD_S_ISDIR(inode->mode) ) { Error::Set(ENOTDIR); return 0; }
	uint32_t offset = 0;
	while ( offset < inode->size )
	{
		uint32_t pos = inode->dataoffset + offset;
		const initrd_dirent* dirent = (const initrd_dirent*) (initrd + pos);
		if ( index-- == 0 ) { return dirent->name; }
		offset += dirent->reclen;
	}
	Error::Set(EINVAL);
	return NULL;
}

size_t GetNumFiles(uint32_t dir)
{
	const initrd_inode_t* inode = GetInode(dir);
	if ( !inode ) { return 0; }
	if ( !INITRD_S_ISDIR(inode->mode) ) { Error::Set(ENOTDIR); return 0; }
	uint32_t offset = 0;
	size_t numentries = 0;
	while ( offset < inode->size )
	{
		uint32_t pos = inode->dataoffset + offset;
		const initrd_dirent* dirent = (const initrd_dirent*) (initrd + pos);
		numentries++;
		offset += dirent->reclen;
	}
	return numentries;
}

void CheckSum()
{
	uint32_t amount = sb->fssize - sb->sumsize;
	uint8_t* filesum = initrd + amount;
	if ( sb->sumalgorithm != INITRD_ALGO_CRC32 )
	{
		Log::PrintF("Warning: InitRD checksum algorithm not supported\n");
		return;
	}
	uint32_t crc32 = *((uint32_t*) filesum);
	uint32_t filecrc32 = CRC32::Hash(initrd, amount);
	if ( crc32 != filecrc32 )
	{
		PanicF("InitRD had checksum %X, expected %X: this means the ramdisk "
		       "may have been corrupted by the bootloader.", filecrc32, crc32);
	}
}

void Init(addr_t phys, size_t size)
{
	// First up, map the initrd onto the kernel's address space.
	addr_t virt = Memory::GetInitRD();
	size_t amount = 0;
	while ( amount < size )
	{
		if ( !Memory::Map(phys + amount, virt + amount, PROT_KREAD) )
		{
			Panic("Unable to map the init ramdisk into virtual memory");
		}
		amount += 0x1000UL;
	}

	Memory::Flush();

	initrd = (uint8_t*) virt;
	initrdsize = size;

	if ( size < sizeof(*sb) ) { PanicF("initrd is too small"); }
	sb = (const initrd_superblock_t*) initrd;

	if ( !String::StartsWith(sb->magic, "sortix-initrd") )
	{
		Panic("Invalid magic value in initrd. This means the ramdisk may have "
		      "been corrupted by the bootloader, or that an incompatible file "
		      "has been passed to the kernel.");
	}

	if ( String::Compare(sb->magic, "sortix-initrd-1") == 0 )
	{
		Panic("Sortix initrd format version 1 is no longer supported.");
	}

	if ( String::Compare(sb->magic, "sortix-initrd-2") != 0 )
	{
		Panic("The initrd has a format that isn't supported. Perhaps it is "
		      "too new? Try downgrade or regenerate the initrd.");
	}

	if ( size < sb->fssize )
	{
		PanicF("The initrd said it is %u bytes, but the kernel was only passed "
		       "%zu bytes by the bootloader, which is not enough.", sb->fssize,
		       size);
	}

	CheckSum();
}

} // namespace InitRD
} // namespace Sortix
