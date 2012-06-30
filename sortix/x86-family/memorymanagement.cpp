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

	memorymanagement.cpp
	Handles memory for the x86 family of architectures.

******************************************************************************/

#include <sortix/kernel/platform.h>
#include <libmaxsi/error.h>
#include <libmaxsi/memory.h>
#include "multiboot.h"
#include <sortix/kernel/panic.h>
#include <sortix/kernel/memorymanagement.h>
#include "memorymanagement.h"
#include "syscall.h"
#include "msr.h"

using namespace Maxsi;

namespace Sortix
{
	extern size_t end;

	namespace Page
	{
		void InitPushRegion(addr_t position, size_t length);
		size_t pagesnotonstack;
		size_t stackused;
		size_t stacklength;
		size_t totalmem;
	}

	namespace Memory
	{
		addr_t currentdir;

		void InitCPU();
		void AllocateKernelPMLs();
		int SysMemStat(size_t* memused, size_t* memtotal);
		addr_t PAT2PMLFlags[PAT_NUM];

		void Init(multiboot_info_t* bootinfo)
		{
			const size_t MAXKERNELEND = 0x400000UL; /* 4 MiB */
			addr_t kernelend = Page::AlignUp((addr_t) &end);
			if ( MAXKERNELEND < kernelend )
			{
				Log::PrintF("Warning: The kernel is too big! It ends at 0x%zx, "
				            "but the highest ending address supported is 0x%zx. "
				            "The system may not boot correctly.\n", kernelend,
				            MAXKERNELEND);
			}

			Page::pagesnotonstack = 0;
			Page::totalmem = 0;

			if ( !( bootinfo->flags & MULTIBOOT_INFO_MEM_MAP ) )
			{
				Panic("memorymanagement.cpp: The memory map flag was't set in "
				      "the multiboot structure. Are your bootloader multiboot "
				      "specification compliant?");
			}

			// If supported, setup the Page Attribute Table feature that allows
			// us to control the memory type (caching) of memory more precisely.
			if ( MSR::IsPATSupported() )
			{
				MSR::InitializePAT();
				for ( addr_t i = 0; i < PAT_NUM; i++ )
					PAT2PMLFlags[i] = EncodePATAsPMLFlag(i);
			}
			// Otherwise, reroute all requests to the backwards compatible
			// scheme. TODO: Not all early 32-bit x86 CPUs supports these
			// values, so we need yet another fallback.
			else
			{
				PAT2PMLFlags[PAT_UC] = PML_WRTHROUGH | PML_NOCACHE;
				PAT2PMLFlags[PAT_WC] = PML_WRTHROUGH | PML_NOCACHE; // Approx.
				PAT2PMLFlags[2] = 0; // No such flag.
				PAT2PMLFlags[3] = 0; // No such flag.
				PAT2PMLFlags[PAT_WT] = PML_WRTHROUGH;
				PAT2PMLFlags[PAT_WP] = PML_WRTHROUGH; // Approx.
				PAT2PMLFlags[PAT_WB] = 0;
				PAT2PMLFlags[PAT_UCM] = PML_NOCACHE;
			}

			// Initialize CPU-specific things.
			InitCPU();

			typedef const multiboot_memory_map_t* mmap_t;

			// Loop over every detected memory region.
			for (
			     mmap_t mmap = (mmap_t) bootinfo->mmap_addr;
			     (addr_t) mmap < bootinfo->mmap_addr + bootinfo->mmap_length;
			     mmap = (mmap_t) ((addr_t) mmap + mmap->size + sizeof(mmap->size))
			    )
			{
				// Check that we can use this kind of RAM.
				if ( mmap->type != 1 ) { continue; }

				// The kernel's code may split this memory area into multiple pieces.
				addr_t base = (addr_t) mmap->addr;
				size_t length = Page::AlignDown(mmap->len);

	#ifdef PLATFORM_X86
				// Figure out if the memory area is addressable (are our pointers big enough?)
				if ( 0xFFFFFFFFULL < mmap->addr ) { continue; }
				if ( 0xFFFFFFFFULL < mmap->addr + mmap->len ) { length = 0x100000000ULL - mmap->addr;  }
	#endif

				// Count the amount of usable RAM (even if reserved for kernel).
				Page::totalmem += length;

				// Give all the physical memory to the physical memory allocator
				// but make sure not to give it things we already use.
				addr_t regionstart = mmap->addr;
				addr_t regionend = mmap->addr + mmap->len;
				addr_t processed = regionstart;
				while ( processed < regionend )
				{
					addr_t lowest = processed;
					addr_t highest = regionend;

					// Don't allocate the kernel.
					if ( lowest < kernelend ) { processed = kernelend; continue; }

					// Don't give any of our modules to the physical page
					// allocator, we'll need them.
					bool continuing = false;
					uint32_t* modules = (uint32_t*) bootinfo->mods_addr;
					for ( uint32_t i = 0; i < bootinfo->mods_count; i++ )
					{
						size_t modsize = (size_t) (modules[2*i+1] - modules[2*i+0]);
						addr_t modstart = (addr_t) modules[2*i+0];
						addr_t modend = modstart + modsize;
						if ( modstart <= processed && processed < modend )
						{
							processed = modend;
							continuing = true;
							break;
						}
						if ( lowest <= modstart && modstart < highest )
						{
							highest = modstart;
						}
					}

					if ( continuing ) { continue; }

					if ( highest <= lowest ) { break; }

					// Now that we have a continious area not used by anything,
					// let's forward it to the physical page allocator.
					lowest = Page::AlignUp(lowest);
					highest = Page::AlignUp(highest);
					size_t size = highest - lowest;
					Page::InitPushRegion(lowest, size);
					processed = highest;
				}
			}

			// If the physical allocator couldn't handle the vast amount of
			// physical pages, it may decide to drop some. This shouldn't happen
			// until the pebibyte era of RAM.
			if ( 0 < Page::pagesnotonstack )
			{
				Log::PrintF("%zu bytes of RAM aren't used due to technical "
				            "restrictions.\n", Page::pagesnotonstack * 0x1000UL);
			}

			// Finish allocating the top level PMLs for the kernels use.
			AllocateKernelPMLs();

			Syscall::Register(SYSCALL_MEMSTAT, (void*) SysMemStat);
		}

		void Statistics(size_t* amountused, size_t* totalmem)
		{
			size_t memfree = Page::stackused << 12UL;
			size_t memused = Page::totalmem - memfree;
			if ( amountused ) { *amountused = memused; }
			if ( totalmem ) { *totalmem = Page::totalmem; }
		}

		int SysMemStat(size_t* memused, size_t* memtotal)
		{
			size_t used;
			size_t total;
			Statistics(&used, &total);
			// TODO: Check if legal user-space buffers!
			*memused = used;
			*memtotal = total;
			return 0;
		}

		// Prepare the non-forkable kernel PMLs such that forking the kernel
		// address space will always keep the kernel mapped.
		void AllocateKernelPMLs()
		{
			const addr_t flags = PML_PRESENT | PML_WRITABLE;

			PML* const pml = PMLS[TOPPMLLEVEL];

			size_t start = ENTRIES / 2;
			size_t end = ENTRIES;

			for ( size_t i = start; i < end; i++ )
			{
				if ( pml->entry[i] & PML_PRESENT ) { continue; }

				addr_t page = Page::Get();
				if ( !page ) { Panic("out of memory allocating boot PMLs"); }

				pml->entry[i] = page | flags;

				// Invalidate the new PML and reset it to zeroes.
				addr_t pmladdr = (addr_t) (PMLS[TOPPMLLEVEL-1] + i);
				InvalidatePage(pmladdr);
				Maxsi::Memory::Set((void*) pmladdr, 0, sizeof(PML));
			}
		}
	}

	namespace Page
	{
		void ExtendStack()
		{
			// This call will always succeed, if it didn't, then the stack
			// wouldn't be full, and thus this function won't be called.
			addr_t page = Get();

			// This call will also succeed, since there are plenty of physical
			// pages available and it might need some.
			if ( !Memory::MapKernel(page, (addr_t) (STACK + stacklength)) )
			{
				Panic("Unable to extend page stack, which should have worked");
			}

			// TODO: This may not be needed during the boot process!
			//Memory::InvalidatePage((addr_t) (STACK + stacklength));

			stacklength += 4096UL / sizeof(addr_t);
		}

		void InitPushRegion(addr_t position, size_t length)
		{
			// Align our entries on page boundaries.
			addr_t newposition = Page::AlignUp(position);
			length = Page::AlignDown((position + length) - newposition);
			position = newposition;

			while ( length )
			{
				if ( unlikely(stackused == stacklength) )
				{
					if ( stackused == MAXSTACKLENGTH )
					{
						pagesnotonstack += length / 4096UL;
						return;
					}

					ExtendStack();
				}

				STACK[stackused++] = position;

				length -= 4096UL;
				position += 4096UL;
			}
		}

		addr_t Get()
		{
			if ( unlikely(stackused == 0) ) { Error::Set(ENOMEM); return 0; }
			addr_t result = STACK[--stackused];
			ASSERT(result == AlignDown(result));
			return result;
		}

		void Put(addr_t page)
		{
			ASSERT(page == AlignDown(page));
			ASSERT(stackused < MAXSTACKLENGTH);
			STACK[stackused++] = page;
		}
	}

	namespace Memory
	{
		void InvalidatePage(addr_t /*addr*/)
		{
			// TODO: Actually just call the instruction.
			Flush();
		}

		// Flushes the Translation Lookaside Buffer (TLB).
		void Flush()
		{
			asm volatile("mov %0, %%cr3":: "r"(currentdir));
		}

		addr_t SwitchAddressSpace(addr_t addrspace)
		{
			// Have fun debugging this.
			if ( currentdir != Page::AlignDown(currentdir) )
			{
				PanicF("The variable containing the current address space "
				       "contains garbage all of sudden: it isn't page-aligned. "
				        "It contains the value 0x%zx.", currentdir);
			}

			// Don't switch if we are already there.
			if ( addrspace == currentdir ) { return currentdir; }

			if ( addrspace & 0xFFFUL ) { PanicF("addrspace 0x%zx was not page-aligned!", addrspace); }

			addr_t previous = currentdir;

			// Switch and flush the TLB.
			asm volatile("mov %0, %%cr3":: "r"(addrspace));

			currentdir = addrspace;

			return previous;
		}

		bool MapRangeKernel(addr_t where, size_t bytes)
		{
			for ( addr_t page = where; page < where + bytes; page += 4096UL )
			{
				addr_t physicalpage = Page::Get();
				if ( physicalpage == 0 )
				{
					while ( where < page )
					{
						page -= 4096UL;
						physicalpage = UnmapKernel(page);
						Page::Put(physicalpage);
					}
					return false;
				}

				MapKernel(physicalpage, page);
			}

			return true;
		}

		void UnmapRangeKernel(addr_t where, size_t bytes)
		{
			for ( addr_t page = where; page < where + bytes; page += 4096UL )
			{
				addr_t physicalpage = UnmapKernel(page);
				Page::Put(physicalpage);
			}
		}

		bool MapRangeUser(addr_t where, size_t bytes)
		{
			for ( addr_t page = where; page < where + bytes; page += 4096UL )
			{
				addr_t physicalpage = Page::Get();
				if ( physicalpage == 0 || !MapUser(physicalpage, page) )
				{
					while ( where < page )
					{
						page -= 4096UL;
						physicalpage = UnmapUser(page);
						Page::Put(physicalpage);
					}
					return false;
				}
			}

			return true;
		}

		void UnmapRangeUser(addr_t where, size_t bytes)
		{
			for ( addr_t page = where; page < where + bytes; page += 4096UL )
			{
				addr_t physicalpage = UnmapUser(page);
				Page::Put(physicalpage);
			}
		}

		template <bool userspace, bool invalidate>
		bool Map(addr_t physical, addr_t mapto, addr_t extraflags = 0)
		{
			const addr_t userflags = userspace ? (PML_USERSPACE | PML_FORK) : 0;
			const addr_t flags = userflags | PML_PRESENT | PML_WRITABLE;

			// Translate the virtual address into PML indexes.
			const size_t MASK = (1<<TRANSBITS)-1;
			size_t pmlchildid[TOPPMLLEVEL + 1];
			for ( size_t i = 1; i <= TOPPMLLEVEL; i++ )
			{
				pmlchildid[i] = (mapto >> (12+(i-1)*TRANSBITS)) & MASK;
			}

			// For each PML level, make sure it exists, and that we may use it.
			size_t offset = 0;
			for ( size_t i = TOPPMLLEVEL; i > 1; i-- )
			{
				size_t childid = pmlchildid[i];
				PML* pml = PMLS[i] + offset;

				addr_t& entry = pml->entry[childid];

				// Find the index of the next PML in the fractal mapped memory.
				size_t childoffset = offset * ENTRIES + childid;

				if ( !(entry & PML_PRESENT) )
				{
					// TODO: Possible memory leak when page allocation fails.
					addr_t page = Page::Get();
					if ( page == 0 ) { return false; }
					entry = page | flags | extraflags;

					// Invalidate the new PML and reset it to zeroes.
					addr_t pmladdr = (addr_t) (PMLS[i-1] + childoffset);
					InvalidatePage(pmladdr);
					Maxsi::Memory::Set((void*) pmladdr, 0, sizeof(PML));
				}
				else if ( userspace && !(entry & PML_USERSPACE) )
				{
					PanicF("attempted to map physical page %p to virtual page "
					       "%p with userspace permissions, but the virtual page "
					       "wasn't in an userspace PML[%zu]. This is a bug in the "
					       "code calling this function", physical, mapto, i-1);
				}

				offset = childoffset;
			}

			// Actually map the physical page to the virtual page.
			const addr_t entry = physical | flags | extraflags;
			(PMLS[1] + offset)->entry[pmlchildid[1]] = entry;

			if ( invalidate )
			{
				InvalidatePage(mapto);
			}

			return true;
		}

		template <bool userspace, bool invalidate>
		addr_t Unmap(addr_t mapto)
		{
			// Translate the virtual address into PML indexes.
			const size_t MASK = (1<<TRANSBITS)-1;
			size_t pmlchildid[TOPPMLLEVEL + 1];
			for ( size_t i = 1; i <= TOPPMLLEVEL; i++ )
			{
				pmlchildid[i] = (mapto >> (12+(i-1)*TRANSBITS)) & MASK;
			}

			// For each PML level, make sure it exists, and that it belongs to
			// user-space.
			size_t offset = 0;
			for ( size_t i = TOPPMLLEVEL; i > 1; i-- )
			{
				size_t childid = pmlchildid[i];
				PML* pml = PMLS[i] + offset;

				addr_t& entry = pml->entry[childid];

				if ( !(entry & PML_PRESENT) )
				{
					PanicF("attempted to unmap virtual page %p with userspace, "
					       " but the virtual page wasn't mapped. This is a bug "
					       "in the code calling this function", mapto);
				}
				else if ( userspace && !(entry & PML_USERSPACE) )
				{
					PanicF("attempted to unmap virtual page %p it wasn't in an "
					       "userspace PML[%zu]. This is a bug in the code "
					       "calling this function", mapto, i-1);
				}

				// Find the index of the next PML in the fractal mapped memory.
				offset = offset * ENTRIES + childid;
			}

			addr_t& entry = (PMLS[1] + offset)->entry[pmlchildid[1]];
			addr_t result = entry & PML_ADDRESS;
			entry = 0;

			// TODO: If all the entries in PML[N] are not-present, then who
			// unmaps its entry from PML[N-1]?

			if ( invalidate )
			{
				Flush();
			}

			return result;
		}

		bool MapKernelPAT(addr_t physical, addr_t mapto, addr_t mtype)
		{
			addr_t extraflags = PAT2PMLFlags[mtype];
			return Map<false, false>(physical, mapto, extraflags);
		}

		bool MapKernel(addr_t physical, addr_t mapto)
		{
			return MapKernelPAT(physical, mapto, PAT_WB);
		}

		bool MapUserPAT(addr_t physical, addr_t mapto, addr_t mtype)
		{
			addr_t extraflags = PAT2PMLFlags[mtype];
			return Map<true, false>(physical, mapto, extraflags);
		}

		bool MapUser(addr_t physical, addr_t mapto)
		{
			return MapUserPAT(physical, mapto, PAT_WB);
		}

		addr_t UnmapKernel(addr_t mapto)
		{
			return Unmap<false, false>(mapto);
		}

		addr_t UnmapUser(addr_t mapto)
		{
			return Unmap<true, false>(mapto);
		}

		void ForkCleanup(size_t i, size_t level)
		{
			PML* destpml = FORKPML + level;
			if ( !i ) { return; }
			for ( size_t n = 0; n < i-1; n++ )
			{
				addr_t entry = destpml->entry[i];
				if ( !(entry & PML_FORK ) ) { continue; }
				addr_t phys = entry & PML_ADDRESS;
				if ( 1 < level )
				{
					addr_t destaddr = (addr_t) (FORKPML + level-1);
					MapKernel(phys, destaddr);
					InvalidatePage(destaddr);
					ForkCleanup(ENTRIES+1UL, level-1);
				}
				Page::Put(phys);
			}
		}

		// TODO: Copying every frame is endlessly useless in many uses. It'd be
		// nice to upgrade this to a copy-on-write algorithm.
		bool Fork(size_t level, size_t pmloffset)
		{
			PML* destpml = FORKPML + level;
			for ( size_t i = 0; i < ENTRIES; i++ )
			{
				addr_t entry = (PMLS[level] + pmloffset)->entry[i];

				// Link the entry if it isn't supposed to be forked.
				if ( !(entry & PML_FORK ) )
				{
					destpml->entry[i] = entry;
					continue;
				}

				addr_t phys = Page::Get();
				if ( unlikely(!phys) ) { ForkCleanup(i, level); return false; }

				addr_t flags = entry & PML_FLAGS;
				destpml->entry[i] = phys | flags;

				// Map the destination page.
				addr_t destaddr = (addr_t) (FORKPML + level-1);
				MapKernel(phys, destaddr);
				InvalidatePage(destaddr);

				size_t offset = pmloffset * ENTRIES + i;

				if ( 1 < level )
				{
					if ( !Fork(level-1, offset) )
					{
						Page::Put(phys);
						ForkCleanup(i, level);
						return false;
					}
					continue;
				}

				// Determine the source page's address.
				const void* src = (const void*) (offset * 4096UL);

				// Determine the destination page's address.
				void* dest = (void*) (FORKPML + level - 1);

				Maxsi::Memory::Copy(dest, src, 4096UL);
			}

			return true;
		}

		bool Fork(addr_t dir, size_t level, size_t pmloffset)
		{
			PML* destpml = FORKPML + level;

			// This call always succeeds.
			MapKernel(dir, (addr_t) destpml);
			InvalidatePage((addr_t) destpml);

			return Fork(level, pmloffset);
		}

		// Create an exact copy of the current address space.
		addr_t Fork()
		{
			addr_t dir = Page::Get();
			if ( dir == 0 ) { return 0; }
			if ( !Fork(dir, TOPPMLLEVEL, 0) ) { Page::Put(dir); return 0; }

			// Now, the new top pml needs to have its fractal memory fixed.
			const addr_t flags = PML_PRESENT | PML_WRITABLE;
			addr_t mapto;
			addr_t childaddr;

			(FORKPML + TOPPMLLEVEL)->entry[ENTRIES-1] = dir | flags;
			childaddr = (FORKPML + TOPPMLLEVEL)->entry[ENTRIES-2] & PML_ADDRESS;

			for ( size_t i = TOPPMLLEVEL-1; i > 0; i-- )
			{
				mapto = (addr_t) (FORKPML + i);
				MapKernel(childaddr, mapto);
				InvalidatePage(mapto);
				(FORKPML + i)->entry[ENTRIES-1] = dir | flags;
				childaddr = (FORKPML + i)->entry[ENTRIES-2] & PML_ADDRESS;
			}
			return dir;
		}
	}
}

