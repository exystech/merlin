/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2011, 2012, 2013.

    This program is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
    more details.

    You should have received a copy of the GNU General Public License along with
    this program. If not, see <http://www.gnu.org/licenses/>.

    cp.cpp
    Copy files and directories.

*******************************************************************************/

#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(VERSIONSTR)
#define VERSIONSTR "unknown version"
#endif

const char* BaseName(const char* path)
{
	size_t len = strlen(path);
	while ( len-- ) { if ( path[len] == '/' ) { return path + len + 1; } }
	return path;
}

char* AddElemToPath(const char* path, const char* elem)
{
	size_t pathlen = strlen(path);
	size_t elemlen = strlen(elem);
	if ( pathlen && path[pathlen-1] == '/' )
	{
		char* ret = (char*) malloc(sizeof(char) * (pathlen + elemlen + 1));
		stpcpy(stpcpy(ret, path), elem);
		return ret;
	}
	char* ret = (char*) malloc(sizeof(char) * (pathlen + 1 + elemlen + 1));
	stpcpy(stpcpy(stpcpy(ret, path), "/"), elem);
	return ret;
}

void CompactArguments(int* argc, char*** argv)
{
	for ( int i = 0; i < *argc; i++ )
	while ( i < *argc && !(*argv)[i] )
	{
		for ( int n = i; n < *argc; n++ )
			(*argv)[n] = (*argv)[n+1];
		(*argc)--;
	}
}

const int FLAG_RECURSIVE = 1 << 0;
const int FLAG_VERBOSE = 1 << 1;
const int FLAG_KEEP_GOING = 1 << 2;
const int FLAG_TARGET_DIR = 1 << 3;
const int FLAG_NO_TARGET_DIR = 1 << 4;
const int FLAG_UPDATE = 1 << 5;
const int FLAG_NO_DEREFERENCE = 1 << 6;

bool CopyFileContents(int srcfd, const char* srcpath,
                      int dstfd, const char* dstpath, int flags);
bool CopyDirectoryContents(int srcfd, const char* srcpath,
                           int dstfd, const char* dstpath, int flags);
bool CopyToDest(int srcdirfd, const char* srcrel, const char* srcpath,
                int dstdirfd, const char* dstrel, const char* dstpath,
                int flags);
bool CopyIntoDirectory(int srcdirfd, const char* srcrel, const char* srcpath,
                       int dstdirfd, const char* dstrel, const char* dstpath,
                       int flags);
bool CopyAmbigious(int srcdirfd, const char* srcrel, const char* srcpath,
                   int dstdirfd, const char* dstrel, const char* dstpath,
                   int flags);

bool CopyFileContents(int srcfd, const char* srcpath,
                      int dstfd, const char* dstpath, int flags)
{
	struct stat srcst, dstst;
	if ( fstat(srcfd, &srcst) )
	{
		error(0, errno, "stat: %s", srcpath);
		return false;
	}
	if ( fstat(dstfd, &dstst) )
	{
		error(0, errno, "stat: %s", dstpath);
		return false;
	}
	if ( srcst.st_dev == dstst.st_dev && srcst.st_ino == dstst.st_ino )
	{
		error(0, 0, "`%s' and `%s' are the same file", srcpath, dstpath);
		return false;
	}
	if ( S_ISDIR(dstst.st_mode) )
	{
		error(0, 0, "cannot overwrite directory `%s' with non-directory",
	                dstpath);
		return false;
	}
	if ( lseek(srcfd, 0, SEEK_SET) )
	{
		error(0, errno, "can't seek: %s", srcpath);
		return false;
	}
	if ( lseek(dstfd, 0, SEEK_SET) )
	{
		error(0, errno, "can't seek: %s", dstpath);
		return false;
	}
	if ( flags & FLAG_VERBOSE )
		printf("`%s' -> `%s'\n", srcpath, dstpath);
	ftruncate(dstfd, srcst.st_size);
	size_t BUFFER_SIZE = 16 * 1024;
	uint8_t buffer[BUFFER_SIZE];
	while ( true )
	{
		ssize_t numbytes = read(srcfd, buffer, BUFFER_SIZE);
		if ( numbytes == 0 )
			break;
		if ( numbytes < 0 )
		{
			error(0, errno, "read: %s", srcpath);
			return false;
		}
		size_t sofar = 0;
		while ( sofar < (size_t) numbytes )
		{
			ssize_t amount = write(dstfd, buffer + sofar, numbytes - sofar);
			if ( amount <= 0 )
			{
				error(0, errno, "write: %s", dstpath);
				return false;
			}
			sofar += amount;
		}
	}
	return true;
}

bool CopyDirectoryContentsInner(DIR* srcdir, const char* srcpath,
                                DIR* dstdir, const char* dstpath, int flags)
{
	if ( flags & FLAG_VERBOSE )
		printf("`%s' -> `%s'\n", srcpath, dstpath);
	bool ret = true;
	struct dirent* entry;
	while ( (entry = readdir(srcdir)) )
	{
		const char* name = entry->d_name;
		if ( !strcmp(name, ".") || !strcmp(name, "..") )
			continue;
		char* srcpath_new = AddElemToPath(srcpath, name);
		char* dstpath_new = AddElemToPath(dstpath, name);
		bool ok = CopyToDest(dirfd(srcdir), name, srcpath_new,
		                     dirfd(dstdir), name, dstpath_new, flags);
		free(srcpath_new);
		free(dstpath_new);
		if ( !ok )
		{
			ret = false;
			if ( !(flags & FLAG_KEEP_GOING) )
				break;
		}
	}
	return ret;
}

bool CopyDirectoryContentsOuter(int srcfd, const char* srcpath,
                                int dstfd, const char* dstpath, int flags)
{
	struct stat srcst, dstst;
	if ( fstat(srcfd, &srcst) )
	{
		error(0, errno, "stat: %s", srcpath);
		return false;
	}
	if ( fstat(dstfd, &dstst) )
	{
		error(0, errno, "stat: %s", dstpath);
	}
	if ( srcst.st_dev == dstst.st_dev && srcst.st_ino == dstst.st_ino )
	{
		error(0, 0, "error: `%s' and `%s' are the same file", srcpath, dstpath);
		return false;
	}
	DIR* srcdir = fdopendir(srcfd);
	if ( !srcdir ) { perror("fdopendir"); return false; }
	DIR* dstdir = fdopendir(dstfd);
	if ( !dstdir ) { perror("fdopendir"); closedir(srcdir); return false; }
	bool ret = CopyDirectoryContentsInner(srcdir, srcpath, dstdir, dstpath,
	                                      flags);
	closedir(dstdir);
	closedir(srcdir);
	return ret;
}

bool CopyDirectoryContents(int srcfd, const char* srcpath,
                           int dstfd, const char* dstpath, int flags)
{
	int srcfd_copy = dup(srcfd);
	if ( srcfd_copy < 0 ) { perror("dup"); return false; }
	int dstfd_copy = dup(dstfd);
	if ( dstfd_copy < 0 ) { perror("dup"); close(srcfd_copy); return false; }
	bool ret = CopyDirectoryContentsOuter(srcfd_copy, srcpath,
	                                      dstfd_copy, dstpath, flags);
	close(dstfd_copy);
	close(srcfd_copy);
	return ret;
}

bool CopyToDest(int srcdirfd, const char* srcrel, const char* srcpath,
                int dstdirfd, const char* dstrel, const char* dstpath,
                int flags)
{
	struct stat srcst;
	bool ret = false;
	int srcfd = openat(srcdirfd, srcrel, O_RDONLY);
	if ( srcfd < 0 )
	{
		error(0, errno, "%s", srcpath);
		goto out_done;
	}
	if ( fstat(srcfd, &srcst) )
	{
		error(0, errno, "stat: %s", srcpath);
		goto out_cleanup_srcfd;
	}
	if ( S_ISDIR(srcst.st_mode) )
	{
		if ( !(flags & FLAG_RECURSIVE) )
		{
			error(0, 0, "omitting directory `%s'", srcpath);
			goto out_cleanup_srcfd;
		}
		int dstfd = openat(dstdirfd, dstrel, O_RDONLY | O_DIRECTORY);
		if ( dstfd < 0 )
		{
			if ( errno != ENOENT )
			{
				error(0, errno, "%s", dstpath);
				goto out_cleanup_srcfd;
			}
			if ( mkdirat(dstdirfd, dstrel, srcst.st_mode & 03777) )
			{
				error(0, errno, "cannot create directory `%s'", dstpath);
				goto out_cleanup_srcfd;
			}
			if ( (dstfd = openat(dstdirfd, dstrel, O_RDONLY | O_DIRECTORY)) < 0 )
			{
				error(0, errno, "%s", dstpath);
				goto out_cleanup_srcfd;
			}
		}
		ret = CopyDirectoryContents(srcfd, srcpath, dstfd, dstpath, flags);
		close(dstfd);
	}
	else
	{
		int dstfd = openat(dstdirfd, dstrel, O_WRONLY | O_CREAT, srcst.st_mode & 03777);
		if ( dstfd < 0 )
		{
			error(0, errno, "%s", dstpath);
			goto out_cleanup_srcfd;
		}
		ret = CopyFileContents(srcfd, srcpath, dstfd, dstpath, flags);
		close(dstfd);
	}

out_cleanup_srcfd:
	close(srcfd);
out_done:
	return ret;
}

bool CopyIntoDirectory(int srcdirfd, const char* srcrel, const char* srcpath,
                       int dstdirfd, const char* dstrel, const char* dstpath,
                       int flags)
{
	const char* src_basename = BaseName(srcrel);
	int dstfd = openat(dstdirfd, dstrel, O_RDONLY | O_DIRECTORY);
	if ( dstfd < 0 )
	{
		error(0, errno, "%s", dstpath);
		return false;
	}
	char* dstpath_new = AddElemToPath(dstpath, src_basename);
	bool ret = CopyToDest(srcdirfd, srcrel, srcpath,
	                      dstfd, src_basename, dstpath_new, flags);
	free(dstpath_new);
	return ret;
}

bool CopyAmbigious(int srcdirfd, const char* srcrel, const char* srcpath,
                   int dstdirfd, const char* dstrel, const char* dstpath,
                   int flags)
{
	struct stat dstst;
	if ( fstatat(dstdirfd, dstrel, &dstst, 0) )
	{
		if ( errno != ENOENT )
		{
			error(0, errno, "%s", dstpath);
			return false;
		}
		dstst.st_mode = S_IFREG;
	}
	if ( S_ISDIR(dstst.st_mode) )
		return CopyIntoDirectory(srcdirfd, srcrel, srcpath,
	                             dstdirfd, dstrel, dstpath, flags);
	else
		return CopyToDest(srcdirfd, srcrel, srcpath,
		                  dstdirfd, dstrel, dstpath, flags);
}

void Usage(FILE* fp, const char* argv0)
{
	fprintf(fp, "Usage: %s [OPTION]... [-T] SOURCE DEST\n", argv0);
	fprintf(fp, "  or:  %s [OPTION]... SOURCE... DIRECTORY\n", argv0);
	fprintf(fp, "  or:  %s [OPTION]... -t DIRECTORY SOURCE...\n", argv0);
	fprintf(fp, "Copy SOURCE to DEST, or multiple SOURCE(s) to DIRECTORY.\n");
}

void Help(FILE* fp, const char* argv0)
{
	Usage(fp, argv0);
}

void Version(FILE* fp, const char* argv0)
{
	fprintf(fp, "%s (Sortix) %s\n", argv0, VERSIONSTR);
	fprintf(fp, "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n");
	fprintf(fp, "This is free software: you are free to change and redistribute it.\n");
	fprintf(fp, "There is NO WARRANTY, to the extent permitted by law.\n");
}

int main(int argc, char* argv[])
{
	const char* argv0 = argv[0];
	int flags = FLAG_KEEP_GOING;
	for ( int i = 1; i < argc; i++ )
	{
		const char* arg = argv[i];
		if ( arg[0] != '-' )
			continue;
		argv[i] = NULL;
		if ( !strcmp(arg, "--") )
			break;
		if ( arg[1] != '-' )
		{
			while ( char c = *++arg ) switch ( c )
			{
			case 'r':
			case 'R': flags |= FLAG_RECURSIVE; break;
			case 'v': flags |= FLAG_VERBOSE; break;
			case 't': flags |= FLAG_TARGET_DIR; break;
			case 'T': flags |= FLAG_NO_TARGET_DIR; break;
			case 'u': flags |= FLAG_UPDATE; break;
			case 'P': flags |= FLAG_NO_DEREFERENCE; break;
			default:
				fprintf(stderr, "%s: unknown option -- '%c'\n", argv0, c);
				Usage(stderr, argv0);
				exit(1);
			}
		}
		else if ( !strcmp(arg, "--help") ) { Help(stdout, argv0); exit(0); }
		else if ( !strcmp(arg, "--usage") ) { Usage(stdout, argv0); exit(0); }
		else if ( !strcmp(arg, "--version") ) { Version(stdout, argv0); exit(0); }
		else if ( !strcmp(arg, "--recursive") )
			flags |= FLAG_RECURSIVE;
		else if ( !strcmp(arg, "--verbose") )
			flags |= FLAG_VERBOSE;
		else if ( !strcmp(arg, "--target-directory") )
			flags |= FLAG_TARGET_DIR;
		else if ( !strcmp(arg, "--no-target-directory") )
			flags |= FLAG_NO_TARGET_DIR;
		else if ( !strcmp(arg, "--update") )
			flags |= FLAG_UPDATE;
		else if ( !strcmp(arg, "--no-dereference") )
			flags |= FLAG_NO_DEREFERENCE;
		else
		{
			fprintf(stderr, "%s: unknown option: %s\n", argv0, arg);
			Usage(stderr, argv0);
			exit(1);
		}
	}

	if ( flags & FLAG_UPDATE )
		error(0, 0, "ignoring unsupported, but known -u, --update flag");
	if ( flags & FLAG_NO_DEREFERENCE )
		error(0, 0, "ignoring unsupported, but known -P, --no-dereference flag");

	CompactArguments(&argc, &argv);

	if ( flags & FLAG_NO_TARGET_DIR || argc <= 3 )
	{
		if ( argc < 2 )
			error(1, 0, "missing file operand");
		const char* src = argv[1];
		if ( argc < 3 )
			error(1, 0, "missing destination file operand after `%s'", src);
		const char* dst = argv[2];
		if ( 3 < argc )
			error(1, 0, "extra operand `%s'", argv[3]);
		if ( !(flags & FLAG_NO_TARGET_DIR) && argc == 3 )
			return CopyAmbigious(AT_FDCWD, src, src, AT_FDCWD, dst, dst, flags) ? 0 : 1;
		return CopyToDest(AT_FDCWD, src, src, AT_FDCWD, dst, dst, flags) ? 0 : 1;
	}

	int dst_index = flags & FLAG_TARGET_DIR ? 1 : argc-1;
	const char* dst = argv[dst_index]; argv[dst_index] = NULL;
	CompactArguments(&argc, &argv);

	if ( argc < 2 )
		error(1, 0, "missing file operand");

	bool success = true;
	for ( int i = 1; i < argc; i++ )
	{
		const char* src = argv[i];
		if ( !CopyIntoDirectory(AT_FDCWD, src, src, AT_FDCWD, dst, dst, flags) )
			success = false;
	}

	return success ? 0 : 1;
}
