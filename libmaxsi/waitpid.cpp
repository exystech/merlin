/*******************************************************************************

	Copyright(C) Jonas 'Sortie' Termansen 2011, 2012.

	This file is part of LibMaxsi.

	LibMaxsi is free software: you can redistribute it and/or modify it under
	the terms of the GNU Lesser General Public License as published by the Free
	Software Foundation, either version 3 of the License, or (at your option)
	any later version.

	LibMaxsi is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
	FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
	details.

	You should have received a copy of the GNU Lesser General Public License
	along with LibMaxsi. If not, see <http://www.gnu.org/licenses/>.

	waitpid.cpp
	Wait for child process.

*******************************************************************************/

#include <sys/syscall.h>
#include <sys/wait.h>

DEFN_SYSCALL3(pid_t, sys_wait, SYSCALL_WAIT, pid_t, int*, int);

extern "C" pid_t waitpid(pid_t pid, int* status, int options)
{
	return sys_wait(pid, status, options);
}
