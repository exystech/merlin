#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/readdirents.h>
#include <libmaxsi/platform.h>
#include <libmaxsi/process.h>

using namespace Maxsi;

int ls(const char* path)
{
	int fd = open(path, O_SEARCH | O_DIRECTORY);
	if ( fd < 0 ) { printf("ls: %s: open() failed\n", path); return 2; }

	const size_t BUFFER_SIZE = 512;
	char buffer[BUFFER_SIZE];
	sortix_dirent* dirent = (sortix_dirent*) buffer;

	// TODO: Hack until mountpoints work correctly.
	if ( strcmp(path, "/") == 0 ) { printf("bin\n"); }

	while ( true )
	{
		if ( readdirents(fd, dirent, BUFFER_SIZE) )
		{
			printf("readdirents() failed\n");
			return 1;
		}

		for ( sortix_dirent* iter = dirent; iter; iter = iter->d_next )
		{
			if ( iter->d_namelen == 0 ) { return 0; }
			printf("%s\n", iter->d_name);
		}
	}
}

int main(int argc, char* argv[])
{
	const char* path = "/";
	if ( 1 < argc ) { path = argv[1]; }

	return ls(path);
}
