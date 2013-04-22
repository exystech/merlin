/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2013.

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

    mblen.cpp
    Determine number of bytes in next multibyte character.

*******************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

extern "C" int mblen(const char* s, size_t n)
{
	static mbstate_t ps;
	if ( !s )
	{
		memset(&ps, 0, sizeof(ps));
		return 0; // TODO: Give the correct return value depending on ps.
	}
	size_t ret = mbrlen(s, n, &ps);
	if ( ret == (size_t) -2 )
		return -1;
	if ( ret == (size_t) -1 )
		return -1;
	return (int) ret;
}
