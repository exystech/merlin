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

    seteuid.cpp
    Set effective user id.

*******************************************************************************/

#include <sys/syscall.h>
#include <sys/types.h>

#include <unistd.h>

DEFN_SYSCALL1(uid_t, sys_seteuid, SYSCALL_GETEUID, uid_t);

extern "C" int seteuid(uid_t euid)
{
	return sys_seteuid(euid);
}
