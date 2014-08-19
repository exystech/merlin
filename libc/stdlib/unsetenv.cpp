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

    stdlib/unsetenv.cpp
    Unset an environment variable.

*******************************************************************************/

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static inline
bool matches_environment_variable(const char* what,
                                  const char* name, size_t name_length)
{
	return !strncmp(what, name, name_length) && what[name_length] == '=';
}

extern "C" int unsetenv(const char* name)
{
	if ( !name )
		return errno = EINVAL, -1;

	if ( !name[0] )
		return errno = EINVAL, -1;

	// Verify the name doesn't contain a '=' character.
	size_t name_length = 0;
	while ( name[name_length] )
		if ( name[name_length++] == '=' )
			return errno = EINVAL, -1;

	if ( !environ )
		return 0;

	// Delete all variables from the environment with the given name.
	for ( size_t i = 0; environ[i]; i++ )
	{
		if ( matches_environment_variable(environ[i], name, name_length) )
		{
			// Free the string and move the array around in constant time if
			// the setenv implementation is in control of the array.
			if ( environ == __environ_malloced )
			{
				free(environ[i]);
				environ[i] = environ[__environ_used-1];
				environ[--__environ_used] = NULL;
			}

			// Otherwise, we'll simply clear that entry in environ, but we can't
			// do it in constant time as we don't know how large the array is.
			else
			{
				for ( size_t n = i; environ[n]; n++ )
					environ[n] = environ[n+1];
			}

			// This entry was deleted so we'll need to process it again.
			i--;
		}
	}

	return 0;
}
