/******************************************************************************

	COPYRIGHT(C) JONAS 'SORTIE' TERMANSEN 2011.

	This file is part of LibMaxsi.

	LibMaxsi is free software: you can redistribute it and/or modify it under
	the terms of the GNU Lesser General Public License as published by the Free
	Software Foundation, either version 3 of the License, or (at your option)
	any later version.

	LibMaxsi is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
	FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for
	more details.

	You should have received a copy of the GNU Lesser General Public License
	along with LibMaxsi. If not, see <http://www.gnu.org/licenses/>.

	features.h
	Various things for various systems, programs, compabillity, and whatnot.

******************************************************************************/

#ifndef	_FEATURES_H
#define	_FEATURES_H	1

/* C++ needs to know that types and declarations are C, not C++. */
#ifdef	__cplusplus
	#define __BEGIN_DECLS	extern "C" {
	#define __END_DECLS		}
#else
	#define __BEGIN_DECLS
	#define __END_DECLS
#endif

/* By default, assume the source is compiled using the native API. */
#if !defined(_SORTIX_SOURCE) && \
    !defined(_POSIX_SOURCE) && !defined(_POSIX_C_SOURCE) && \
    !defined(_BSD_SOURCE) && \
    !defined(_SVID_SOURCE) && \
    !defined(_XOPEN_SOURCE) && !defined(_XOPEN_SOURCE_EXTENDED) && \
    !defined(_GNU_SOURCE) && \
    true
	#define _SORTIX_SOURCE 1
#endif

#ifdef _SORTIX_SOURCE
	#warning _SORTIX_SOURCE
#endif

#ifdef _POSIX_SOURCE
	#warning _POSIX_SOURCE
#endif

#ifdef _POSIX_C_SOURCE
	#warning _POSIX_C_SOURCE
#endif

#ifdef _BSD_SOURCE
	#warning _BSD_SOURCE
#endif

#ifdef _SVID_SOURCE
	#warning _SVID_SOURCE
#endif

#ifdef _XOPEN_SOURCE
	#warning _XOPEN_SOURCE
#endif

#ifdef _XOPEN_SOURCE_EXTENDED
	#warning XOPEN_SOURCE_EXTENDED
#endif

#ifdef _GNU_SOURCE
	#warning _GNU_SOURCE
#endif

#define restrict

/* TODO: Improve these declarations, perhaps like they are in glibc. */
#define __POSIX_NO_OBSOLETE

#ifdef __POSIX_NO_OBSOLETE
	#define __POSIX_OBSOLETE 999999L
#else
	#define __POSIX_OBSOLETE 200112L
#endif

/* Don't provide things from standard headers that is not implemented. */
/*#define __SORTIX_SHOW_UNIMPLEMENTED*/

#include <sortix/bits.h>


#endif
