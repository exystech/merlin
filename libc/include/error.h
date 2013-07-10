/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2011.

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

    error.h
    Error reporting functions.

*******************************************************************************/

#ifndef _ERROR_H
#define _ERROR_H 1

#include <features.h>

__BEGIN_DECLS

void gnu_error(int status, int errnum, const char *format, ...);
#if __SORTIX_STDLIB_REDIRECTS
void error(int status, int errnum, const char *format, ...) asm("gnu_error");
#endif

__END_DECLS

#endif
