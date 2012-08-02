/*******************************************************************************

	Copyright(C) Jonas 'Sortie' Termansen 2012.

	This file is part of Sortix.

	Sortix is free software: you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software
	Foundation, either version 3 of the License, or (at your option) any later
	version.

	Sortix is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
	FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
	details.

	You should have received a copy of the GNU General Public License along with
	Sortix. If not, see <http://www.gnu.org/licenses/>.

	worker.h
	Kernel worker thread.

*******************************************************************************/

#ifndef SORTIX_WORKER_H
#define SORTIX_WORKER_H

namespace Sortix {
namespace Worker {

void Init();
void Schedule(void (*func)(void*), void* user = NULL);
bool TrySchedule(void (*func)(void*), void* user = NULL);
void Thread(void* user = NULL);

} // namespace Worker
} // namespace Sortix

#endif
