/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2012, 2013, 2014.

    This file is part of Sortix.

    Sortix is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version.

    Sortix is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
    details.

    You should have received a copy of the GNU General Public License along
    with Sortix. If not, see <http://www.gnu.org/licenses/>.

    mkinitrd.cpp
    Produces a simple ramdisk filesystem readable by the Sortix kernel.

*******************************************************************************/

#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <ioleast.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sortix/initrd.h>

#if !defined(VERSIONSTR)
#define VERSIONSTR "unknown version"
#endif

#define DEFAULT_FORMAT "sortix-initrd-2"

#include "crc32.h"
#include "rules.h"

uint32_t HostModeToInitRD(mode_t mode)
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

mode_t InitRDModeToHost(uint32_t mode)
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

struct Node;
struct DirEntry;

struct DirEntry
{
	char* name;
	Node* node;
};

struct Node
{
	char* path;
	uint32_t ino;
	uint32_t nlink;
	size_t direntsused;
	size_t direntslength;
	DirEntry* dirents;
	mode_t mode;
	time_t ctime;
	time_t mtime;
	bool written;
	size_t refcount;
};

void FreeNode(Node* node)
{
	if ( 1 < node->nlink ) { node->nlink--; return; }
	for ( size_t i = 0; i < node->direntsused; i++ )
	{
		DirEntry* entry = node->dirents + i;
		if ( strcmp(entry->name, ".") != 0 && strcmp(entry->name, "..") != 0 )
		{
			if ( --entry->node->refcount == 0 )
				FreeNode(entry->node);
		}
		free(entry->name);
	}
	free(node->dirents);
	free(node->path);
	free(node);
}

struct CacheEntry
{
	ino_t ino;
	dev_t dev;
	Node* node;
};

size_t cacheused = 0;
size_t cachelen = 0;
CacheEntry* cache = NULL;

InclusionRules path_filter;

Node* LookupCache(dev_t dev, ino_t ino)
{
	for ( size_t i = 0; i < cacheused; i++ )
		if ( cache[i].dev == dev && cache[i].ino == ino )
			return cache[i].node;
	return NULL;
}

bool AddToCache(Node* node, dev_t dev, ino_t ino)
{
	if ( cacheused == cachelen )
	{
		size_t newcachelen = cachelen ? 2 * cachelen : 256;
		size_t newcachesize = newcachelen * sizeof(CacheEntry);
		CacheEntry* newcache = (CacheEntry*) realloc(cache, newcachesize);
		if ( !newcache )
			return false;
		cache = newcache;
		cachelen = newcachelen;
	}
	size_t index = cacheused++;
	cache[index].ino = ino;
	cache[index].dev = dev;
	cache[index].node = node;
	return true;
}

Node* RecursiveSearch(const char* real_path, const char* virt_path,
                      uint32_t* ino, Node* parent = NULL)
{
	printf("%s\n", virt_path);

	if ( virt_path[0] == '/' && !virt_path[1] )
		virt_path = "";

	struct stat st;
	if ( lstat(real_path, &st) != 0 )
	{
		error(0, errno, "stat: %s", real_path);
		return NULL;
	}

	Node* cached = LookupCache(st.st_dev, st.st_ino);
	if ( cached )
		return cached->nlink++, cached->refcount++, cached;

	Node* node = (Node*) calloc(1, sizeof(Node));
	if ( !node )
		return NULL;

	node->nlink = 1;
	node->refcount = 1;
	node->mode = st.st_mode;
	node->ino = (*ino)++;
	node->ctime = st.st_ctim.tv_sec;
	node->mtime = st.st_mtim.tv_sec;

	char* real_path_clone = strdup(real_path);
	if ( !real_path_clone )
	{
		error(0, errno, "strdup");
		free(node);
		return NULL;
	}

	node->path = real_path_clone;

	if ( !S_ISDIR(st.st_mode) )
	{
		if ( !AddToCache(node, st.st_dev, st.st_ino) )
		{
			free(real_path_clone);
			free(node);
			return NULL;
		}
		return node;
	}

	DIR* dir = opendir(real_path);
	if ( !dir )
	{
		error(0, errno, "opendir: %s", real_path);
		FreeNode(node);
		return NULL;
	}

	size_t real_path_len = strlen(real_path);
	size_t virt_path_len = strlen(virt_path);

	bool successful = true;
	struct dirent* entry;
	while ( (entry = readdir(dir)) )
	{
		size_t namelen = strlen(entry->d_name);

		size_t virt_subpath_len = virt_path_len + 1 + namelen;
		char* virt_subpath = (char*) malloc(virt_subpath_len+1);
		if ( !virt_subpath )
		{
			error(0, errno, "malloc");
			successful = false;
			break;
		}
		stpcpy(stpcpy(stpcpy(virt_subpath, virt_path), "/"), entry->d_name);

		if ( strcmp(entry->d_name, ".") != 0 &&
		     strcmp(entry->d_name, "..") != 0 &&
		     !path_filter.IncludesPath(virt_subpath) )
		{
			free(virt_subpath);
			continue;
		}

		size_t real_subpath_len = real_path_len + 1 + namelen;
		char* real_subpath = (char*) malloc(real_subpath_len+1);
		if ( !real_subpath )
		{
			free(virt_subpath);
			error(0, errno, "malloc");
			successful = false;
			break;
		}
		stpcpy(stpcpy(stpcpy(real_subpath, real_path), "/"), entry->d_name);

		Node* child = NULL;
		if ( !strcmp(entry->d_name, ".") )
			child = node;
		if ( !strcmp(entry->d_name, "..") )
			child = parent ? parent : node;
		if ( !child )
			child = RecursiveSearch(real_subpath, virt_subpath, ino, node);
		free(real_subpath);
		free(virt_subpath);
		if ( !child )
		{
			successful = false;
			break;
		}

		if ( node->direntsused == node->direntslength )
		{
			size_t oldlength = node->direntslength;
			size_t newlength = oldlength ? 2 * oldlength : 8;
			size_t newsize = sizeof(DirEntry) * newlength;
			DirEntry* newdirents = (DirEntry*) realloc(node->dirents, newsize);
			if ( !newdirents )
			{
				error(0, errno, "realloc");
				successful = false;
				break;
			}
			node->dirents = newdirents;
			node->direntslength = newlength;
		}

		char* nameclone = strdup(entry->d_name);
		if ( !nameclone )
		{
			error(0, errno, "strdup");
			successful = false;
			break;
		}

		DirEntry* entry = node->dirents + node->direntsused++;

		entry->name = nameclone;
		entry->node = child;
	}

	closedir(dir);
	if ( !successful || !AddToCache(node, st.st_dev, st.st_ino) )
	{
		FreeNode(node);
		return NULL;
	}
	return node;
}

bool WriteNode(struct initrd_superblock* sb, int fd, const char* outputname,
               Node* node)
{
	if ( node->written )
		return true;

	uint32_t filesize = 0;
	uint32_t origfssize = sb->fssize;
	uint32_t dataoff = origfssize;
	uint32_t filestart = dataoff;

	if ( S_ISLNK(node->mode) ) // Symbolic link
	{
		const size_t NAME_SIZE = 1024UL;
		char name[NAME_SIZE];
		ssize_t namelen = readlink(node->path, name, NAME_SIZE);
		if ( namelen < 0 )
			return error(0, errno, "readlink: %s", node->path), false;
		filesize = (uint32_t) namelen;
		if ( pwriteall(fd, name, filesize, dataoff) < filesize )
			return error(0, errno, "read: %s", node->path), false;
		dataoff += filesize;
	}
	else if ( S_ISREG(node->mode) ) // Regular file
	{
		int nodefd = open(node->path, O_RDONLY);
		if ( nodefd < 0 )
			return error(0, errno, "open: %s", node->path), false;
		const size_t BUFFER_SIZE = 16UL * 1024UL;
		uint8_t buffer[BUFFER_SIZE];
		ssize_t amount;
		while ( 0 < (amount = read(nodefd, buffer, BUFFER_SIZE)) )
		{
			if ( pwriteall(fd, buffer, amount, dataoff) < (size_t) amount )
			{
				close(nodefd);
				return error(0, errno, "write: %s", outputname), false;
			}
			dataoff += amount;
			filesize += amount;
		}
		close(nodefd);
		if ( amount < 0 )
			return error(0, errno, "read: %s", node->path), false;
	}
	else if ( S_ISDIR(node->mode) ) // Directory
	{
		for ( size_t i = 0; i < node->direntsused; i++ )
		{
			DirEntry* entry = node->dirents + i;
			const char* name = entry->name;
			size_t namelen = strlen(entry->name);
			struct initrd_dirent dirent;
			dirent.inode = entry->node->ino;
			dirent.namelen = (uint16_t) namelen;
			dirent.reclen = sizeof(dirent) + dirent.namelen + 1;
			dirent.reclen = (dirent.reclen+3)/4*4; // Align entries.
			size_t entsize = sizeof(dirent);
			ssize_t hdramt = pwriteall(fd, &dirent, entsize, dataoff);
			ssize_t nameamt = pwriteall(fd, name, namelen+1, dataoff + entsize);
			if ( hdramt < (ssize_t) entsize || nameamt < (ssize_t) (namelen+1) )
				return error(0, errno, "write: %s", outputname), false;
			size_t padding = dirent.reclen - (entsize + (namelen+1));
			for ( size_t i = 0; i < padding; i++ )
			{
				uint8_t nul = 0;
				if ( pwrite(fd, &nul, 1, dataoff+entsize+namelen+1+i) != 1 )
					return error(0, errno, "write: %s", outputname), false;
			}
			filesize += dirent.reclen;
			dataoff += dirent.reclen;
		}
	}

	struct initrd_inode inode;
	inode.mode = HostModeToInitRD(node->mode);
	inode.uid = 1;
	inode.gid = 1;
	inode.nlink = node->nlink;
	inode.ctime = (uint64_t) node->ctime;
	inode.mtime = (uint64_t) node->mtime;
	inode.dataoffset = filestart;
	inode.size = filesize;

	uint32_t inodepos = sb->inodeoffset + node->ino * sb->inodesize;
	uint32_t inodesize = sizeof(inode);
	if ( pwriteall(fd, &inode, inodesize, inodepos) < inodesize )
		return error(0, errno, "write: %s", outputname), false;

	uint32_t increment = dataoff - origfssize;
	sb->fssize += increment;

	return node->written = true;
}

bool WriteNodeRecursive(struct initrd_superblock* sb, int fd,
                        const char* outputname, Node* node)
{
	if ( !WriteNode(sb, fd, outputname, node) )
		return false;

	if ( !S_ISDIR(node->mode) )
		return true;

	for ( size_t i = 0; i < node->direntsused; i++ )
	{
		DirEntry* entry = node->dirents + i;
		const char* name = entry->name;
		Node* child = entry->node;
		if ( !strcmp(name, ".") || !strcmp(name, ".." ) )
			continue;
		if ( !WriteNodeRecursive(sb, fd, outputname, child) )
			return false;
	}

	return true;
}

bool Format(const char* outputname, int fd, uint32_t inodecount, Node* root)
{
	struct initrd_superblock sb;
	memset(&sb, 0, sizeof(sb));
	strncpy(sb.magic, "sortix-initrd-2", sizeof(sb.magic));
	sb.revision = 0;
	sb.fssize = sizeof(sb);
	sb.inodesize = sizeof(initrd_inode);
	sb.inodeoffset = sizeof(sb);
	sb.inodecount = inodecount;
	sb.root = root->ino;

	uint32_t inodebytecount = sb.inodesize * sb.inodecount;
	sb.fssize += inodebytecount;

	if ( !WriteNodeRecursive(&sb, fd, outputname, root) )
		return false;

	uint32_t crcsize = sizeof(uint32_t);
	sb.sumalgorithm = INITRD_ALGO_CRC32;
	sb.sumsize = crcsize;
	sb.fssize += sb.sumsize;

	if ( pwriteall(fd, &sb, sizeof(sb), 0) < sizeof(sb) )
	{
		error(0, errno, "write: %s", outputname);
		return false;
	}

	uint32_t checksize = sb.fssize - sb.sumsize;
	uint32_t crc;
	if ( !CRC32File(&crc, outputname, fd, 0, checksize) )
		return false;
	if ( pwriteall(fd, &crc, crcsize, checksize) < crcsize )
		return false;

	return true;
}

bool Format(const char* pathname, uint32_t inodecount, Node* root)
{
	int fd = open(pathname, O_RDWR | O_CREAT | O_TRUNC, 0666);
	bool result = Format(pathname, fd, inodecount, root);
	close(fd);
	return result;
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

bool get_option_variable(const char* option, char** varptr,
                         const char* arg, int argc, char** argv, int* ip,
                         const char* argv0)
{
	size_t option_len = strlen(option);
	if ( strncmp(option, arg, option_len) != 0 )
		return false;
	if ( arg[option_len] == '=' )
	{
		*varptr = strdup(arg + option_len + 1);
		return true;
	}
	if ( arg[option_len] != '\0' )
		return false;
	if ( *ip + 1 == argc )
	{
		fprintf(stderr, "%s: expected operand after `%s'\n", argv0, option);
		exit(1);
	}
	*varptr = strdup(argv[++*ip]), argv[*ip] = NULL;
	return true;
}

#define GET_OPTION_VARIABLE(str, varptr) \
        get_option_variable(str, varptr, arg, argc, argv, &i, argv0)

static void help(FILE* fp, const char* argv0)
{
	fprintf(fp, "Usage: %s [OPTION]... ROOT -o OUTPUT\n", argv0);
	fprintf(fp, "Creates a init ramdisk for the Sortix kernel.\n");
	fprintf(fp, "\n");
	fprintf(fp, "Mandatory arguments to long options are mandatory for short options too.\n");
	fprintf(fp, "      --filter=FILE         import filter rules from FILE\n");
	fprintf(fp, "      --format=FORMAT       format version [%s]\n", DEFAULT_FORMAT);
	fprintf(fp, "  -o, --output=FILE         write result to FILE\n");
	fprintf(fp, "      --help                display this help and exit\n");
	fprintf(fp, "      --version             output version information and exit\n");
}

static void version(FILE* fp, const char* argv0)
{
	fprintf(fp, "%s (Sortix) %s\n", argv0, VERSIONSTR);
	fprintf(fp, "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n");
	fprintf(fp, "This is free software: you are free to change and redistribute it.\n");
	fprintf(fp, "There is NO WARRANTY, to the extent permitted by law.\n");
}

int main(int argc, char* argv[])
{
	char* arg_filter = NULL;
	char* arg_format = strdup(DEFAULT_FORMAT);
	char* arg_output = NULL;

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
			while ( char c = *++arg ) switch ( c )
			{
			case 'o':
				free(arg_output);
				if ( *(arg+1) )
					arg_output = strdup(arg + 1);
				else
				{
					if ( i + 1 == argc )
					{
						error(0, 0, "option requires an argument -- 'o'");
						fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
						exit(125);
					}
					arg_output = strdup(argv[i+1]);
					argv[++i] = NULL;
				}
				arg = "o";
				break;
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

		else if ( GET_OPTION_VARIABLE("--filter", &arg_filter) )
		{
			FILE* fp = fopen(arg_filter, "r");
			if ( !fp )
				error(1, errno, "%s", arg_filter);
			if ( !path_filter.AddRulesFromFile(fp, stderr, arg_filter) )
				exit(1);
			fclose(fp);
			free(arg_filter);
			arg_filter = NULL;
		}
		else if ( GET_OPTION_VARIABLE("--format", &arg_format) ) { }
		else if ( GET_OPTION_VARIABLE("--output", &arg_output) ) { }
		else
		{
			fprintf(stderr, "%s: unknown option: %s\n", argv0, arg);
			help(stderr, argv0);
			exit(1);
		}
	}

	if ( argc < 2 )
	{
		help(stdout, argv0);
		exit(1);
	}

	compact_arguments(&argc, &argv);

	if ( argc < 2 )
	{
		fprintf(stderr, "%s: No root specified\n", argv0),
		help(stderr, argv0);
		exit(1);
	}

	if (  2 < argc  )
	{
		fprintf(stderr, "%s: Too many roots specified\n", argv0),
		help(stderr, argv0);
		exit(1);
	}

	if (  !arg_output  )
	{
		fprintf(stderr, "%s: No output file specified\n", argv0),
		help(stderr, argv0);
		exit(1);
	}

	const char* format = arg_format;

	if ( !strcmp(format, "default") )
		format = DEFAULT_FORMAT;

	if ( strcmp(format, "sortix-initrd-2") != 0 )
	{
		fprintf(stderr, "%s: Unsupported format `%s'\n", argv0, format);
		fprintf(stderr, "Try `%s --help' for more information.\n", argv0);
		exit(1);
	}

	const char* rootstr = argv[1];

	uint32_t inodecount = 1;
	Node* root = RecursiveSearch(rootstr, "/", &inodecount);
	if ( !root )
		exit(1);

	if ( !Format(arg_output, inodecount, root) )
		exit(1);

	FreeNode(root);

	return 0;
}
