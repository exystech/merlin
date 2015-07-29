/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2011, 2012.

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

    type.cpp
    Lets you move the tty cursor around and easily issue ANSI escape codes.

*******************************************************************************/

#include <sys/keycodes.h>
#include <sys/termmode.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>

static void help(FILE* fp, const char* argv0)
{
	fprintf(fp, "Usage: %s [OPTION]...\n", argv0);
	fprintf(fp, "Lets you type freely onto the tty.\n");
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
		else
		{
			fprintf(stderr, "%s: unknown option: %s\n", argv0, arg);
			help(stderr, argv0);
			exit(1);
		}
	}

	if ( !isatty(0) || !isatty(1) )
	{
		error(1, errno, "standard output and input must be a tty");
	}

	bool lastwasesc = false;
	unsigned termmode = TERMMODE_KBKEY | TERMMODE_UNICODE | TERMMODE_SIGNAL;
	if ( settermmode(0, termmode) ) { error(1, errno, "settermmode"); }
	while ( true )
	{
		uint32_t codepoint;
		ssize_t numbytes = read(0, &codepoint, sizeof(codepoint));
		if ( !numbytes ) { break; }
		if ( numbytes < 0 ) { break; }
		int kbkey = KBKEY_DECODE(codepoint);
		if ( kbkey < 0 ) { continue; }
		if ( kbkey == KBKEY_UP ) { printf("\e[A"); fflush(stdout); continue; }
		if ( kbkey == KBKEY_DOWN ) { printf("\e[B"); fflush(stdout); continue; }
		if ( kbkey == KBKEY_RIGHT ) { printf("\e[C"); fflush(stdout); continue; }
		if ( kbkey == KBKEY_LEFT ) { printf("\e[D"); fflush(stdout); continue; }
		if ( kbkey == KBKEY_ESC ) { printf("\e["); fflush(stdout); lastwasesc = true; continue; }
		if ( kbkey ) { continue; }
		if ( lastwasesc && codepoint == '[' ) { continue; }

		if ( codepoint >= 0x80 ) { continue; }

		putchar(codepoint & 0xFF);
		lastwasesc = false;
		fflush(stdout);
	}

	return 0;
}
