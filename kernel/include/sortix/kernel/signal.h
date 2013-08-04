/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2011, 2012, 2013, 2014.

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

    sortix/kernel/signal.h
    Asynchronous user-space thread interruption.

*******************************************************************************/

#ifndef INCLUDE_SORTIX_KERNEL_SIGNAL_H
#define INCLUDE_SORTIX_KERNEL_SIGNAL_H

#include <sortix/signal.h>
#include <sortix/sigset.h>

#include <sortix/kernel/cpu.h>

namespace Sortix {

extern "C" volatile unsigned long asm_signal_is_pending;

namespace Signal {

void Init();
inline bool IsPending() { return asm_signal_is_pending != 0; }
void DispatchHandler(CPU::InterruptRegisters* regs, void* user);
void ReturnHandler(CPU::InterruptRegisters* regs, void* user);

} // namespace Signal

} // namespace Sortix

#endif
