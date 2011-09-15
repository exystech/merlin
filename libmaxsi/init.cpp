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

	init.cpp
	Initializes the process by setting up the heap, signal handling,
	static memory and other useful things.

******************************************************************************/

#include "platform.h"
#include "signal.h"

namespace Maxsi
{
	extern "C" void init_error_functions();

	extern "C" void initialize_standard_library()
	{
		// Initialize stuff such as errno.
		init_error_functions();

		// It's probably best to initialize the Unix signals early on.
		Signal::Init();
	}
}
