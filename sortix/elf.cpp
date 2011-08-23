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

	elf.h
	Constructs processes from ELF files.

******************************************************************************/

#include "platform.h"
#include <libmaxsi/memory.h>
#include "elf.h"
#include "memorymanagement.h"
#include "panic.h"

using namespace Maxsi;

namespace Sortix
{
	namespace ELF
	{
		addr_t Construct32(const void* file, size_t filelen)
		{
			if ( filelen < sizeof(Header32) ) { return 0; }
			const Header32* header = (const Header32*) file;

			// Check for little endian.
			if ( header->dataencoding != DATA2LSB ) { return 0; }
			if ( header->version != CURRENTVERSION ) { return 0; }

			addr_t entry = header->entry;

			// Find the location of the program headers.
			addr_t phtbloffset = header->programheaderoffset;
			if ( filelen < phtbloffset ) { return 0; }
			addr_t phtblpos = ((addr_t) file) + phtbloffset;
			size_t phsize = header->programheaderentrysize;
			const ProgramHeader32* phtbl = (const ProgramHeader32*) phtblpos;

			// Validate that all program headers are present.
			uint16_t numprogheaders = header->numprogramheaderentries;
			size_t neededfilelen = phtbloffset + numprogheaders * phsize;
			if ( filelen < neededfilelen ) { return 0; } 

			// Create all the segments in the final process.
			// TODO: Handle errors on bad/malicious input or out-of-mem!
			for ( uint16_t i = 0; i < numprogheaders; i++ )
			{
				const ProgramHeader32* pht = &(phtbl[i]);
				if ( pht->type != PT_LOAD ) { continue; }
				addr_t virtualaddr = pht->virtualaddr;
				addr_t mapto = Page::AlignDown(virtualaddr);
				addr_t mapbytes = virtualaddr - mapto + pht->memorysize;
				ASSERT(pht->offset % pht->align == virtualaddr % pht->align);
				ASSERT(pht->offset + pht->filesize < filelen);
				ASSERT(pht->filesize <= pht->memorysize);

				if ( !VirtualMemory::MapRangeUser(mapto, mapbytes) )
				{
					PanicF("Could not user-map at %p and %zu bytes onwards", mapto, mapbytes);
				}

				// Copy as much data as possible and memset the rest to 0.
				byte* memdest = (byte*) virtualaddr;
				byte* memsource = (byte*) ( (addr_t)file + pht->offset);
				Memory::Copy(memdest, memsource, pht->filesize);
				Memory::Set(memdest + pht->filesize, 0, pht->memorysize - pht->filesize);
			}

			// MEMORY LEAK: There is no system in place to delete the sections
			// once the process has terminated.

			return entry;
		}

		addr_t Construct64(const void* /*file*/, size_t /*filelen*/)
		{
			return 0;
		}

		addr_t Construct(const void* file, size_t filelen)
		{
			if ( filelen < sizeof(Header) ) { return 0; }
			const Header* header = (const Header*) file;

			if ( !(header->magic[0] == 0x7F && header->magic[1] == 'E' &&
                   header->magic[2] == 'L'  && header->magic[3] == 'F'  ) )
			{
				return 0;
			}

			switch ( header->fileclass )
			{
				case CLASS32:
					return Construct32(file, filelen);
				case CLASS64:
					return Construct64(file, filelen);
				default:
					return 0;
			}		
		}
	}
}

