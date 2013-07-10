/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2011, 2012.

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

    open.cpp
    Open a file.

*******************************************************************************/

#include <sys/syscall.h>
#include <fcntl.h>
#include <stdarg.h>

DEFN_SYSCALL3(int, sys_open, SYSCALL_OPEN, const char*, int, mode_t);

extern "C" int open(const char* path, int flags, ...)
{
	mode_t mode = 0;
	if ( flags & O_CREAT )
	{
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	return sys_open(path, flags, mode);
}
