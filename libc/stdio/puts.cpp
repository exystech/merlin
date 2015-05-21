/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2011, 2014, 2015.

    This file is part of the Sortix C Library.

    The Sortix C Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    The Sortix C Library is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
    License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with the Sortix C Library. If not, see <http://www.gnu.org/licenses/>.

    stdio/puts.cpp
    Write a string and newline to stdout.

*******************************************************************************/

#include <stdio.h>
#include <string.h>

extern "C" int puts(const char* str)
{
	int ret = 0;
	flockfile(stdout);
	size_t stringlen = strlen(str);
	if ( fwrite_unlocked(str, 1, stringlen, stdout) < stringlen ||
	     fputc_unlocked('\n', stdout) == EOF )
		ret = EOF;
	funlockfile(stdout);
	return ret;
}
