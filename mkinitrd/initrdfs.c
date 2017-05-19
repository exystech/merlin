/*
 * Copyright (c) 2012, 2015, 2016, 2017 Jonas 'Sortie' Termansen.
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
 * initrdfs.c
 * Provides access to filesystems in the Sortix kernel initrd format.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <ioleast.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sortix/initrd.h>

#include "serialize.h"

char* Substring(const char* str, size_t start, size_t length)
{
	char* result = (char*) malloc(length+1);
	strncpy(result, str + start, length);
	result[length] = 0;
	return result;
}

bool ReadSuperBlock(int fd, initrd_superblock_t* dest)
{
	if ( preadall(fd, dest, sizeof(*dest), 0) != sizeof(*dest) )
		return false;
	import_initrd_superblock(dest);
	return true;
}

initrd_superblock_t* GetSuperBlock(int fd)
{
	size_t sbsize = sizeof(initrd_superblock_t);
	initrd_superblock_t* sb = (initrd_superblock_t*) malloc(sbsize);
	if ( !sb ) { return NULL; }
	if ( !ReadSuperBlock(fd, sb) ) { free(sb); return NULL; }
	return sb;
}

bool ReadInode(int fd, initrd_superblock_t* sb, uint32_t ino,
               initrd_inode_t* dest)
{
	uint32_t inodepos = sb->inodeoffset + sb->inodesize * ino;
	if ( preadall(fd, dest, sizeof(*dest), inodepos) != sizeof(*dest) )
		return false;
	import_initrd_inode(dest);
	return true;
}

initrd_inode_t* GetInode(int fd, initrd_superblock_t* sb, uint32_t ino)
{
	initrd_inode_t* inode = (initrd_inode_t*) malloc(sizeof(initrd_inode_t));
	if ( !inode ) { return NULL; }
	if ( !ReadInode(fd, sb, ino, inode) ) { free(inode); return NULL; }
	return inode;
}

initrd_inode_t* CloneInode(const initrd_inode_t* src)
{
	initrd_inode_t* result = (initrd_inode_t*) malloc(sizeof(*src));
	if ( !result ) { return NULL; }
	memcpy(result, src, sizeof(*src));
	return result;
}

bool ReadInodeData(int fd, initrd_superblock_t* sb, initrd_inode_t* inode,
                   uint8_t* dest, size_t size, off_t offset)
{
	(void) sb;
	if ( offset < 0 )
		return errno = EINVAL, false;
	if ( inode->size < (uintmax_t) offset )
		return errno = EINVAL, false;
	size_t available = inode->size - offset;
	if ( inode->size < available )
		return errno = EINVAL, false;
	return preadall(fd, dest, size, inode->dataoffset + offset) == size;
}

uint8_t* GetInodeDataSize(int fd, initrd_superblock_t* sb,
                          initrd_inode_t* inode, size_t size)
{
	uint8_t* buf = (uint8_t*) malloc(size);
	if ( !buf )
		return NULL;
	if ( !ReadInodeData(fd, sb, inode, buf, size, 0) )
	{
		free(buf);
		return NULL;
	}
	return buf;
}

uint8_t* GetInodeData(int fd, initrd_superblock_t* sb, initrd_inode_t* inode)
{
	return GetInodeDataSize(fd, sb, inode, inode->size);
}

uint32_t Traverse(int fd, initrd_superblock_t* sb, initrd_inode_t* inode,
                  const char* name)
{
	if ( !INITRD_S_ISDIR(inode->mode) ) { errno = ENOTDIR; return 0; }
	uint8_t* direntries = GetInodeData(fd, sb, inode);
	if ( !direntries ) { return 0; }
	uint32_t result = 0;
	uint32_t offset = 0;
	while ( offset < inode->size )
	{
		initrd_dirent_t* dirent = (initrd_dirent_t*) (direntries + offset);
		import_initrd_dirent(dirent);
		if ( dirent->namelen && !strcmp(dirent->name, name) )
		{
			result = dirent->inode;
			export_initrd_dirent(dirent);
			break;
		}
		offset += dirent->reclen;
		export_initrd_dirent(dirent);
	}
	free(direntries);
	if ( !result ) { errno = ENOENT; }
	return result;
}

initrd_inode_t* ResolvePath(int fd, initrd_superblock_t* sb,
                            initrd_inode_t* inode, const char* path)
{
	if ( !path[0] ) { return CloneInode(inode); }
	if ( path[0] == '/' )
	{
		if ( !INITRD_S_ISDIR(inode->mode) ) { errno = ENOTDIR; return NULL; }
		return ResolvePath(fd, sb, inode, path+1);
	}
	size_t elemlen = strcspn(path, "/");
	char* elem = Substring(path, 0, elemlen);
	uint32_t ino = Traverse(fd, sb, inode, elem);
	free(elem);
	if ( !ino ) { return NULL; }
	initrd_inode_t* child = GetInode(fd, sb, ino);
	if ( !child ) { return NULL; }
	if ( !path[elemlen] ) { return child; }
	initrd_inode_t* result = ResolvePath(fd, sb, child, path + elemlen);
	free(child);
	return result;
}

bool ListDirectory(int fd, initrd_superblock_t* sb, initrd_inode_t* dir,
                   bool all)
{
	if ( !INITRD_S_ISDIR(dir->mode) ) { errno = ENOTDIR; return false; }
	uint8_t* direntries = GetInodeData(fd, sb, dir);
	if ( !direntries ) { return false; }
	uint32_t offset = 0;
	while ( offset < dir->size )
	{
		initrd_dirent_t* dirent = (initrd_dirent_t*) (direntries + offset);
		if ( dirent->namelen && (all || dirent->name[0] != '.'))
		{
			printf("%s\n", dirent->name);
		}
		offset += dirent->reclen;
	}
	free(direntries);
	return true;
}

bool PrintFile(int fd, initrd_superblock_t* sb, initrd_inode_t* inode)
{
	if ( INITRD_S_ISDIR(inode->mode ) ) { errno = EISDIR; return false; }
	uint32_t sofar = 0;
	while ( sofar < inode->size )
	{
		const size_t BUFFER_SIZE = 16UL * 1024UL;
		uint8_t buffer[BUFFER_SIZE];
		uint32_t available = inode->size - sofar;
		uint32_t count = available < BUFFER_SIZE ? available : BUFFER_SIZE;
		if ( !ReadInodeData(fd, sb, inode, buffer, count, 0) ) { return false; }
		if ( writeall(1, buffer, count) != count ) { return false; }
		sofar += count;
	}
	return true;
}

void Extract(int fd,
             const char* fd_path,
             initrd_superblock_t* sb,
             initrd_inode_t* inode,
             const char* out_path,
             bool verbose)
{
	if ( verbose )
		printf("%s\n", out_path);
	if ( INITRD_S_ISLNK(inode->mode) )
	{
		char* buffer = (char*) malloc(inode->size + 1);
		if ( !buffer )
			err(1, "malloc");
		if ( !ReadInodeData(fd, sb, inode, (uint8_t*) buffer, inode->size, 0) )
			err(1, "%s", fd_path);
		buffer[inode->size] = '\0';
		// TODO: What if it already exists.
		if ( symlink(buffer, out_path) < 0 )
			err(1, "%s", out_path);
		return;
	}
	else if ( INITRD_S_ISREG(inode->mode) )
	{
		int out_fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0200);
		if ( out_fd < 0 )
			err(1, "%s", out_path);
		uint32_t sofar = 0;
		while ( sofar < inode->size )
		{
			const size_t BUFFER_SIZE = 16UL * 1024UL;
			uint8_t buffer[BUFFER_SIZE];
			uint32_t available = inode->size - sofar;
			uint32_t count = available < BUFFER_SIZE ? available : BUFFER_SIZE;
			if ( !ReadInodeData(fd, sb, inode, buffer, count, sofar) )
				err(1, "%s", fd_path);
			if ( writeall(out_fd, buffer, count) != count )
				err(1, "%s", out_path);
			sofar += count;
		}
		if ( fchmod(out_fd, inode->mode & 07777) < 0 )
			err(1, "%s", out_path);
		close(out_fd);
		return;
	}
	else if ( !INITRD_S_ISDIR(inode->mode) )
		errx(1, "%s: Unsupported kind of file", out_path);
	bool made = true;
	if ( mkdir(out_path, 0700) < 0 )
	{
		if ( errno != EEXIST )
			err(1, "%s", out_path);
		made = false;
	}
	uint8_t* direntries = GetInodeData(fd, sb, inode);
	if ( !direntries )
		err(1, "%s", out_path);
	uint32_t offset = 0;
	while ( offset < inode->size )
	{
		initrd_dirent_t* dirent = (initrd_dirent_t*) (direntries + offset);
		offset += dirent->reclen;
		if ( !dirent->namelen ) // TODO: Possible?
			continue;
		if ( !strcmp(dirent->name, ".") || !strcmp(dirent->name, "..") )
			continue;
		char* child_path;
		if ( asprintf(&child_path, "%s/%s", out_path, dirent->name) < 0 )
			err(1, "asprintf");
		initrd_inode_t* child_inode = ResolvePath(fd, sb, inode, dirent->name);
		if ( !child_inode )
			err(1, "%s: %s", fd_path, out_path);
		Extract(fd, fd_path, sb, child_inode, child_path, verbose);
		free(child_path);
	}
	free(direntries);
	// TODO: Time of check to time of use race condition, a concurrent rename
	//       and we may assign the permissions to the wrong file, potentially
	//       exploitable.
	if ( made && chmod(out_path, inode->mode & 07777) < 0 )
		err(1, " %s", out_path);
}

static void compact_arguments(int* argc, char*** argv)
{
	for ( int i = 0; i < *argc; i++ )
	{
		while ( i < *argc && !(*argv)[i] )
		{
			for ( int n = i; n < *argc; n++ )
				(*argv)[n] = (*argv)[n+1];
			(*argc)--;
		}
	}
}

int main(int argc, char* argv[])
{
	bool all = false;
	const char* destination = ".";
	bool verbose = false;

	for ( int i = 1; i < argc; i++ )
	{
		const char* arg = argv[i];
		if ( arg[0] != '-' || !arg[1] )
			continue;
		argv[i] = NULL;
		if ( !strcmp(arg, "--") )
			break;
		if ( arg[1] != '-' )
		{
			char c;
			while ( (c = *++arg) ) switch ( c )
			{
			case 'a': all = true; break;
			case 'C':
				if ( !*(destination = arg + 1) )
				{
					if ( i + 1 == argc )
						errx(1, "option requires an argument -- 'C'");
					destination = argv[i+1];
					argv[++i] = NULL;
				}
				arg = "C";
				break;
			case 'v': verbose = true; break;
			default:
				errx(1, "unknown option -- '%c'", c);
			}
		}
		else
			errx(1, "unknown option: %s", arg);
	}

	compact_arguments(&argc, &argv);

	if ( argc == 1 )
		errx(1, "No initrd specified");
	const char* initrd = argv[1];
	if ( argc == 2 )
		errx(1, "No command specified");
	const char* cmd = argv[2];

	int fd = open(initrd, O_RDONLY);
	if ( fd < 0 )
		err(1, "open: %s", initrd);

	initrd_superblock_t* sb = GetSuperBlock(fd);
	if ( !sb )
		err(1, "read: %s", initrd);

	initrd_inode_t* root = GetInode(fd, sb, sb->root);
	if ( !root )
		err(1, "read: %s", initrd);

	for ( int i = 3; i < argc; i++ )
	{
		const char* path = argv[i];
		if ( path[0] != '/' )
		{
			errno = ENOENT;
			errx(1, "%s", path);
		}

		initrd_inode_t* inode = ResolvePath(fd, sb, root, path+1);
		if ( !inode )
			err(1, "%s", path);

		if ( !strcmp(cmd, "cat") )
		{
			if ( !PrintFile(fd, sb, inode) )
				err(1, "%s", path);
		}
		else if ( !strcmp(cmd, "ls") )
		{
			if ( !ListDirectory(fd, sb, inode, all) )
				err(1, "%s", path);
		}
		else if ( !strcmp(cmd, "extract") )
			Extract(fd, initrd, sb, inode, destination, verbose);
		else
			errx(1, "unrecognized command: %s", cmd);

		free(inode);
	}

	return 0;
}
