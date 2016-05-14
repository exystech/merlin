/*
 * Copyright (c) 2012, 2015, 2016 Jonas 'Sortie' Termansen.
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

#include <sys/types.h>
#include <errno.h>
#include <error.h>
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
                   uint8_t* dest, size_t size)
{
	(void) sb;
	if ( inode->size < size ) { errno = EINVAL; return false; }
	return preadall(fd, dest, size, inode->dataoffset) == size;
}

uint8_t* GetInodeDataSize(int fd, initrd_superblock_t* sb,
                          initrd_inode_t* inode, size_t size)
{
	uint8_t* buf = (uint8_t*) malloc(size);
	if ( !buf ) { return NULL; }
	if ( !ReadInodeData(fd, sb, inode, buf, size) ) { free(buf); return NULL; }
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
		if ( !ReadInodeData(fd, sb, inode, buffer, count) ) { return false; }
		if ( writeall(1, buffer, count) != count ) { return false; }
		sofar += count;
	}
	return true;
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

static void help(FILE* fp, const char* argv0)
{
	fprintf(fp, "Usage: %s [OPTION]... INITRD (ls | cat) PATH\n", argv0);
	fprintf(fp, "Accesses data in a Sortix kernel init ramdisk.\n");
}

static void version(FILE* fp, const char* argv0)
{
	fprintf(fp, "%s (Sortix) %s\n", argv0, VERSIONSTR);
}

int main(int argc, char* argv[])
{
	bool all = false;
	const char* argv0 = argv[0];
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
			default:
				fprintf(stderr, "%s: unknown option -- '%c'\n", argv0, c);
				help(stderr, argv0);
				exit(1);
			}
		}
		else if ( !strcmp(arg, "--help") )
			help(stdout, argv0), exit(0);
		else if ( !strcmp(arg, "--version") )
			version(stdout, argv0), exit(0);
		else if ( !strcmp(arg, "-a") )
			all = true;
		else
		{
			fprintf(stderr, "%s: unknown option: %s\n", argv0, arg);
			help(stderr, argv0);
			exit(1);
		}
	}

	compact_arguments(&argc, &argv);

	if ( argc == 1 )
		error(1, 0, "No initrd specified");
	const char* initrd = argv[1];
	if ( argc == 2 )
		error(1, 0, "No command specified");
	const char* cmd = argv[2];
	if ( argc == 3 )
		error(1, 0, "No path specified");
	const char* path = argv[3];

	int fd = open(initrd, O_RDONLY);
	if ( fd < 0 ) { error(1, errno, "open: %s", initrd); }

	initrd_superblock_t* sb = GetSuperBlock(fd);
	if ( !sb ) { error(1, errno, "read: %s", initrd); }

	if ( path[0] != '/' ) { error(1, ENOENT, "%s", path); }

	initrd_inode_t* root = GetInode(fd, sb, sb->root);
	if ( !root ) { error(1, errno, "read: %s", initrd); }

	initrd_inode_t* inode = ResolvePath(fd, sb, root, path+1);
	if ( !inode ) { error(1, errno, "%s", path); }

	free(root);

	if ( !strcmp(cmd, "cat") )
	{
		if ( !PrintFile(fd, sb, inode) ) { error(1, errno, "%s", path); }
	}
	else if ( !strcmp(cmd, "ls") )
	{
		initrd_inode_t* dir = inode;
		if ( !ListDirectory(fd, sb, dir, all) ) { error(1, errno, "%s", path); }
	}
	else
	{
		fprintf(stderr, "%s: unrecognized command: %s", argv0, cmd);
		exit(1);
	}

	free(inode);
	free(sb);
	close(fd);

	return 0;
}
