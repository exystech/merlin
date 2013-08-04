/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2011, 2012, 2014.

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

    x86/process.cpp
    CPU-specific process code.

*******************************************************************************/

#include <sys/types.h>

#include <stdint.h>
#include <string.h>

#include <sortix/fork.h>

#include <sortix/kernel/kernel.h>
#include <sortix/kernel/process.h>

namespace Sortix {

void Process::ExecuteCPU(int argc, char** argv, int envc, char** envp,
                         addr_t stackpos, addr_t entry,
                         CPU::InterruptRegisters* regs)
{
	memset(regs, 0, sizeof(*regs));
	regs->eax = argc;
	regs->ebx = (size_t) argv;
	regs->edx = envc;
	regs->ecx = (size_t) envp;
	regs->eip = entry;
	regs->esp = stackpos & ~(15UL);
	regs->ebp = regs->esp;
	regs->cs = UCS | URPL;
	regs->ds = UDS | URPL;
	regs->ss = UDS | URPL;
	regs->eflags = FLAGS_RESERVED1 | FLAGS_INTERRUPT | FLAGS_ID;
	regs->signal_pending = 0;
}

void InitializeThreadRegisters(CPU::InterruptRegisters* regs,
                               const struct tfork* requested)
{
	memset(regs, 0, sizeof(*regs));
	regs->eip = requested->eip;
	regs->esp = requested->esp;
	regs->eax = requested->eax;
	regs->ebx = requested->ebx;
	regs->ecx = requested->ecx;
	regs->edx = requested->edx;
	regs->edi = requested->edi;
	regs->esi = requested->esi;
	regs->ebp = requested->ebp;
	regs->cs = UCS | URPL;
	regs->ds = UDS | URPL;
	regs->ss = UDS | URPL;
	regs->eflags = FLAGS_RESERVED1 | FLAGS_INTERRUPT | FLAGS_ID;
}

} // namespace Sortix
