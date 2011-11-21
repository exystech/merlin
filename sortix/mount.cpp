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
#include <libmaxsi/memory.h>
#include <libmaxsi/string.h>
#include "panic.h"
#include "mount.h"
#include "fs/ramfs.h"
#include "fs/initfs.h"

using namespace Maxsi;

namespace Sortix
{
	class MountPoint
	{
	public:
		MountPoint(DevFileSystem* fs, char* path);
		~MountPoint();

	public:
		MountPoint* prev;
		MountPoint* next;

	public:
		DevFileSystem* fs;
		char* path;

	};

	MountPoint::MountPoint(DevFileSystem* fs, char* path)
	{
		this->path = path;
		this->fs = fs;
		this->fs->Refer();
		prev = NULL;
		next = NULL;
	}

	MountPoint::~MountPoint()
	{
		fs->Unref();
		delete[] path;
	}

	namespace Mount
	{
		MountPoint* root;

		bool MatchesMountPath(const char* path, const char* mount)
		{
			size_t mountlen = String::Length(mount);
			if ( !String::StartsWith(path, mount) ) { return false; }
			int c = path[mountlen];
			return c == '/' || c == '\0';
		}

		DevFileSystem* WhichFileSystem(const char* path, size_t* pathoffset)
		{
			DevFileSystem* result = NULL;
			*pathoffset = 0;

			for ( MountPoint* tmp = root; tmp; tmp = tmp->next )
			{
				if ( MatchesMountPath(path, tmp->path) )
				{
					result = tmp->fs;
					*pathoffset = String::Length(tmp->path);
				}
			}

			return result;
		}

		bool Register(DevFileSystem* fs, const char* path)
		{
			char* newpath = String::Clone(path);
			if ( !newpath ) { return false; }

			MountPoint* mp = new MountPoint(fs, newpath);
			if ( !mp ) { delete[] newpath; return false; }

			if ( !root ) { root = mp; return true; }

			if ( String::Compare(path, root->path) < 0 )
			{
				mp->next = root;
				root->prev = mp;
				root = mp;
				return false;
			}

			for ( MountPoint* tmp = root; tmp; tmp = tmp->next )
			{
				if ( tmp->next == NULL || String::Compare(path, tmp->next->path) < 0 )
				{
					mp->next = tmp->next;
					tmp->next = mp;
					return true;
				}
			}

			return false; // Shouldn't happen.
		}

		void Init()
		{
			root = NULL;

			DevFileSystem* rootfs = new DevRAMFS();
			if ( !rootfs || !Register(rootfs, "") ) { Panic("Unable to allocate rootfs"); }
			DevFileSystem* initfs = new DevInitFS();
			if ( !initfs || !Register(initfs, "/bin") ) { Panic("Unable to allocate initfs"); }
		}
	}
}
