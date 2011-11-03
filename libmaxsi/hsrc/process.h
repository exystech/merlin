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

	process.h
	Exposes system calls for process creation and management.

******************************************************************************/

#ifndef LIBMAXSI_PROCESS_H
#define LIBMAXSI_PROCESS_H

namespace Maxsi
{
	struct FileInfo
	{
		mode_t permissions;
		char name[128];
	};

	namespace Process
	{
		int Execute(const char* filepath, int argc, const char** argv);
		void PrintPathFiles();
		size_t GetNumFiles();
		int GetFileInfo(size_t index, FileInfo* fileinfo);
		void Abort();
		void Exit(int code);
		pid_t Fork();
		pid_t GetPID();
		pid_t GetParentPID();
	}
}

#endif

