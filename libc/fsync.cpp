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

    fsync.cpp
    Open a file relative to directory.

*******************************************************************************/

#include <sys/syscall.h>

#include <unistd.h>

DEFN_SYSCALL1(int, sys_fsync, SYSCALL_FSYNC, int);

extern "C" int fsync(int fd)
{
	return sys_fsync(fd);
}
