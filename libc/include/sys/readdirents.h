/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2011, 2015.

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

    sys/readdirents.h
    Allows reading directory entries directly from a file descriptor.

*******************************************************************************/

#ifndef _SYS_READDIRENTS_H
#define _SYS_READDIRENTS_H 1

#include <sys/cdefs.h>

#include <sys/__/types.h>

#ifndef __size_t_defined
#define __size_t_defined
#define __need_size_t
#include <stddef.h>
#endif

#ifndef __ssize_t_defined
#define __ssize_t_defined
typedef __ssize_t ssize_t;
#endif

struct dirent;

#ifdef __cplusplus
extern "C" {
#endif

ssize_t readdirents(int, struct dirent*, size_t);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
