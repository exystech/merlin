/******************************************************************************

	COPYRIGHT(C) JONAS 'SORTIE' TERMANSEN 2011.

	This file is part of Sortix.

	Sortix is free software: you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software
	Foundation, either version 3 of the License, or (at your option) any later
	version.

	Sortix is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
	FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
	details.

	You should have received a copy of the GNU General Public License along
	with Sortix. If not, see <http://www.gnu.org/licenses/>.

	mount.cpp
	Handles system wide mount points and initialization of new file systems.

******************************************************************************/

#include "platform.h"
#include "mount.h"

namespace Sortix
{
	namespace Mount
	{
		DevFileSystem* WhichFileSystem(const char* path, size_t* pathoffset)
		{
			// TODO: Support some file systems!
			*pathoffset = 0;
			return NULL;
		}

		void Init()
		{
			
		}
	}
}
