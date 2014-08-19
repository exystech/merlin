/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2013, 2014.

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

    stdlib/mkstemp.cpp
    Make a unique temporary filepath and opens it.

*******************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

extern "C" int mkstemp(char* templ)
{
	size_t templ_length = strlen(templ);
	for ( size_t i = 0; i < 6; i++ )
	{
		if ( templ_length - i  == 0 )
			return errno = EINVAL, -1;
		if ( templ[templ_length - i - 1] != 'X' )
			return errno = EINVAL, -1;
	}

	int fd;
	do
	{
		for ( size_t i = templ_length - 6; i < templ_length; i++ )
			templ[i] = '0' + rand() % 10;
	} while ( (fd = open(templ, O_RDWR | O_EXCL | O_CREAT, 0666)) < 0 &&
	          (errno == EEXIST) );

	return fd;
}
