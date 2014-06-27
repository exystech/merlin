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

    sys/socket/getsockname.cpp
    Get socket name.

*******************************************************************************/

#include <sys/socket.h>
#include <sys/syscall.h>

DEFN_SYSCALL3(int, sys_getsockname, SYSCALL_GETSOCKNAME, int, struct sockaddr*, socklen_t*);

extern "C"
int getsockname(int fd,
                struct sockaddr* restrict addr,
                socklen_t* restrict addrsize)
{
	return sys_getsockname(fd, addr, addrsize);
}
