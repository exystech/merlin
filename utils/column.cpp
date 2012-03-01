#define _SORTIX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <error.h>
#include <string.h>

int termwidth = 80;

size_t linesused = 0;
size_t lineslength = 0;
char** lines = 0;
size_t longestline = 0;

bool processline(char* line)
{
	if ( linesused == lineslength )
	{
		size_t newlineslength = lineslength ? 2 * lineslength : 32;
		char** newlines = new char*[newlineslength];
		memcpy(newlines, lines, sizeof(char*) * linesused);
		delete[] lines;
		lines = newlines;
		lineslength = newlineslength;
	}

	lines[linesused++] = line;
	size_t linelen = strlen(line);
	if ( longestline < linelen ) { longestline = linelen; }
	return true;
}

bool processfp(const char* inputname, FILE* fp)
{
	if ( strcmp(inputname, "-") == 0 ) { inputname = "<stdin>"; }
	while ( true )
	{
		size_t linesize;
		char* line = NULL;
		ssize_t linelen = getline(&line, &linesize, fp);
		if ( linelen < 0 )
		{
			if ( feof(fp) ) { return true; }
			error(0, errno, "error reading: %s (%i)", inputname, fileno(fp));
			return false;
		}
		line[--linelen] = 0;
		if ( !processline(line) ) { exit(1); }
	}
}

bool doformat()
{
	size_t width = termwidth;
	size_t columnwidth = longestline + 2;
	size_t columns = width / columnwidth;
	if ( !columns ) { columns = 1; }

	bool newline = false;
	for ( size_t i = 0; i < linesused; i++ )
	{
		newline = false;
		size_t column = i % columns;
		size_t offset = column * columnwidth;
		printf("\e[%zuG%s", offset+1, lines[i]);
		if ( columns <= column + 1 ) { printf("\n"); newline = true; }
	}
	if ( linesused && !newline ) { printf("\n"); }

	return true;
}

void usage(const char* argv0)
{
	printf("usage: %s [--help | --version | --usage] [-c width]  [<FILE> ...]\n", argv0);
}

void help(const char* argv0)
{
	usage(argv0);
}

void version(const char* argv0)
{
	printf("%s (sortix)\n", argv0);
}

int main(int argc, char* argv[])
{
	const char* argv0 = argv[0];

	for ( int i = 1; i < argc; i++ )
	{
		const char* arg = argv[i];
		if ( strcmp(arg, "--") == 0 ) { break; }
		if ( strcmp(arg, "--usage") == 0 ) { usage(argv0); exit(0); }
		if ( strcmp(arg, "--help") == 0 ) { help(argv0); exit(0); }
		if ( strcmp(arg, "--version") == 0 ) { version(argv0); exit(0); }
		if ( strcmp(arg, "-c") == 0 )
		{
			if ( i+1 == argc ) { usage(argv0); exit(1); }
			termwidth = atoi(argv[++i]);
			argv[i] = (char*) "-c"; // TODO: This is a bit hacky.
			continue;
		}
		if ( strcmp(arg, "-") == 0 ) { continue; }
		if ( arg[0] != '-') { continue; }
		error(0, 0, "unknown option %s", arg);
		usage(argv0);
		return 1;
	}
	
	bool parsing = true;
	bool hadany = false;
	for ( int i = 1; i < argc; i++ )
	{
		const char* arg = argv[i];
		if ( strcmp(arg, "--") == 0 && parsing ) { parsing = false; break; }
		bool isstdin = strcmp(arg, "-") == 0;
		if ( parsing && arg[0] == '-' && !isstdin ) { continue; }
		errno = 0;
		FILE* fp = isstdin ? stdin : fopen(arg, "r");
		if ( !fp ) { error(1, errno, "fopen failed: %s", arg); }
		hadany = true;
		if ( !processfp(arg, fp) ) { return 1; }
		if ( fp != stdin ) { fclose(fp); }
	}

	if ( !hadany )
	{
		if ( !processfp("-", stdin) ) { return 1; }
	}

	if ( !doformat() ) { exit(1); }
	return 0;
}

