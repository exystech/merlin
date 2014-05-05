/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2012, 2013, 2014.

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

    termios.h
    Defines values for termios.

*******************************************************************************/

/* TODO: POSIX-1.2008 compliance is only partial */

#ifndef INCLUDE_TERMIOS_H
#define INCLUDE_TERMIOS_H

#include <sys/cdefs.h>

#include <sys/__/types.h>

#include <stddef.h>

#include <sortix/termios.h>

__BEGIN_DECLS

#ifndef __ssize_t_defined
#define __ssize_t_defined
typedef __ssize_t ssize_t;
#endif

ssize_t tcgetblob(int fd, const char* name, void* buffer, size_t count);
ssize_t tcsetblob(int fd, const char* name, const void* buffer, size_t count);
int tcgetwincurpos(int fd, struct wincurpos* wcp);
int tcgetwinsize(int fd, struct winsize* ws);

__END_DECLS

#endif
