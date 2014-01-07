/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2014.

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

    sys/utsname.h
    System name structure.

*******************************************************************************/

#ifndef INCLUDE_SYS_UTSNAME_H
#define INCLUDE_SYS_UTSNAME_H

#include <sys/cdefs.h>

__BEGIN_DECLS

#define _UTSNAME_LENGTH 65

struct utsname
{
	char sysname[_UTSNAME_LENGTH];
	char nodename[_UTSNAME_LENGTH];
	char release[_UTSNAME_LENGTH];
	char version[_UTSNAME_LENGTH];
	char machine[_UTSNAME_LENGTH];
	char processor[_UTSNAME_LENGTH];
	char hwplatform[_UTSNAME_LENGTH];
	char opsysname[_UTSNAME_LENGTH];
	char domainname[_UTSNAME_LENGTH];
};

int uname(struct utsname*);

__END_DECLS

#endif
