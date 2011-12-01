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

	heap.cpp
	Functions that allocate/free memory from a dynamic memory heap.

******************************************************************************/

#include "platform.h"
#include "memory.h"
#include "error.h"

#ifdef SORTIX_KERNEL
#define HEAP_GROWS_DOWNWARDS
#endif

#define PARANOIA 1

#ifdef SORTIX_KERNEL
#include <sortix/platform.h>
#include <sortix/log.h> // DEBUG
#include <sortix/memorymanagement.h>
#include <sortix/panic.h>
#endif

namespace Maxsi
{
	namespace Memory
	{
		//
		// This first section is just magic compiler/platform stuff, you should
		// skip ahead to the actual algorithm.
		//

#ifdef PLATFORM_X64
		const size_t MAGIC = 0xDEADDEADDEADDEADUL;
		const size_t ALIGNMENT = 16UL;
#else
		const size_t MAGIC = 0xDEADDEADUL;
		const size_t ALIGNMENT = 8UL;
#endif

		const size_t PAGESIZE = 4UL * 1024UL; // 4 KiB
		const size_t NUMBINS = 8UL * sizeof(size_t);

		addr_t GetHeapStart()
		{
			#ifdef SORTIX_KERNEL
				return Sortix::Memory::HEAPUPPER;
			#endif
			return 0; // TODO: Not implemented in User-Space yet!
		}

		size_t GetHeapMaxSize()
		{
			#ifdef SORTIX_KERNEL
				return Sortix::Memory::HEAPUPPER - Sortix::Memory::HEAPLOWER;
			#endif
			return 0; // TODO: Not implemented in User-Space yet!
		}

#ifdef SORTIX_KERNEL
		void FreeMemory(addr_t where, size_t bytes)
		{
			ASSERT( (bytes & (PAGESIZE-1UL)) == 0 );

			while ( bytes )
			{
				addr_t page = Sortix::Memory::UnmapKernel(where);
				Sortix::Page::Put(page);

				bytes -= PAGESIZE;
				where += PAGESIZE;
			}
		}

		bool AllocateMemory(addr_t where, size_t bytes)
		{
			ASSERT( (bytes & (PAGESIZE-1UL)) == 0 );

			addr_t pos = where;

			while ( bytes )
			{
				addr_t page = Sortix::Page::Get();
				if ( !page )
				{
					FreeMemory(where, pos-where);
					return false;
				}

				if ( !Sortix::Memory::MapKernel(page, pos) )
				{
					Sortix::Page::Put(page);
					FreeMemory(where, pos-where);
					return false;
				}

				bytes -= PAGESIZE;
				pos += PAGESIZE;
			}

			return true;
		}
#else
		void FreeMemory(addr_t where, size_t bytes)
		{
		}

		bool AllocateMemory(addr_t where, size_t bytes)
		{
			Error::Set(ENOMEM);
			return false;
		}
#endif

		// TODO: BitScanForward and BitScanReverse are x86 instructions, but
		// directly using them messes with the optimizer. Once possible, use
		// the inline assembly instead of the C-version of the functions.

		// Returns the index of the most significant set bit.
		inline size_t BSR(size_t Value)
		{
#if 1
			ASSERT(Value > 0);
			for ( size_t I = 8*sizeof(size_t); I > 0; I-- )
			{
				if ( Value & ( 1UL << (I-1) ) ) { return I-1; }
			}
			return 0;
#else
			size_t Result;
			asm("bsr %0, %1" : "=r"(Result) : "r"(Value));
			return Result;
#endif
		}

		// Returns the index of the least significant set bit.
		inline size_t BSF(size_t Value)
		{
#if 1
			ASSERT(Value > 0);
			for ( size_t I = 0; I < 8*sizeof(size_t); I++ )
			{
				if ( Value & ( 1UL << I ) ) { return I; }
			}
			return 0;
#else
			size_t Result;
			asm("bsf %0, %1" : "=r"(Result) : "r"(Value));
			return Result;
#endif
		}

		//
		// This is where the actual memory allocation algorithm starts.
		//

		struct Chunk;
		struct Trailer;

		// The location where the heap originally grows from.
		addr_t heapstart;

		// If heap grows down: Location of the first mapped page.
		// If heap grows up: Location of the first not-mapped page.
		addr_t wilderness;

		// How many bytes remain in the wilderness.
		size_t wildernesssize;

		// How many bytes are the heap allow to grow to (including wilderness).
		size_t heapmaxsize;

		// How many bytes are currently used for chunks in the heap, which
		// excludes the wilderness.
		size_t heapsize;

		// bins[N] contain a linked list of chunks that are at least 2^(N+1)
		// bytes, but less than 2^(N+2) bytes. By selecting the proper bin in
		// constant time, we can allocate chunks in constant time.
		Chunk* bins[NUMBINS];

		// Bit N is set if bin[N] contains a chunk.
		size_t bincontainschunks;

		// A preamble to every chunk providing meta-information.
		struct Chunk
		{
		public:
			size_t size;
			union
			{
				size_t magic;
				Chunk* nextunused;
			};

		public:
			bool IsUsed() { return magic == MAGIC; }
			Trailer* GetTrailer();
			Chunk* LeftNeighbor();
			Chunk* RightNeighbor();
			bool IsSane();

		};

		// A trailer ro every chunk providing meta-information.
		struct Trailer
		{
		public:
			union
			{
				size_t magic;
				Chunk* prevunused;
			};
			size_t size;

		public:
			bool IsUsed() { return magic == MAGIC; }
			Chunk* GetChunk();

		};

		// This is how a real chunk actually looks:
		//struct RealChunk
		//{
		//	Chunk header;
		//	byte data[...];
		//	Trailer footer;
		// };

		Trailer* Chunk::GetTrailer()
		{
			return (Trailer*) (((addr_t) this) + size - sizeof(Trailer));
		}

		Chunk* Chunk::LeftNeighbor()
		{
			Trailer* trailer = (Trailer*) (((addr_t) this) - sizeof(Trailer));
			return trailer->GetChunk();
		}

		Chunk* Chunk::RightNeighbor()
		{
			return (Chunk*) (((addr_t) this) + size);
		}

		Chunk* Trailer::GetChunk()
		{
			return (Chunk*) (((addr_t) this) + sizeof(Trailer) - size);
		}

		bool Chunk::IsSane()
		{
			if ( !size ) { return false; }
			size_t binindex = BSR(size);
			Trailer* trailer = GetTrailer();
			if ( trailer->size != size ) { return false; }
			if ( IsUsed() )
			{
				if ( bins[binindex] == this ) { return false; }
				if ( magic != MAGIC || trailer->magic != magic ) { return false; }
			}
			if ( !IsUsed() )
			{
				if ( ((addr_t) nextunused) & (ALIGNMENT-1UL) ) { return false; }
				if ( ((addr_t) trailer->prevunused) & (ALIGNMENT-1UL) ) { return false; }
				if ( nextunused && nextunused->GetTrailer()->prevunused != this ) { return false; }

				if ( trailer->prevunused )
				{
					if ( bins[binindex] == this ) { return false; }
					if ( trailer->prevunused->nextunused != this ) { return false; }
				}
				if ( !trailer->prevunused )
				{
					if ( bins[binindex] != this ) { return false; }
					if ( !(bincontainschunks & (1UL << binindex)) ) { return false; }
				}
			}
			return true;
		}

		void InsertChunk(Chunk* chunk)
		{
			// Insert the chunk into the right bin.
			size_t binindex = BSR(chunk->size);
			chunk->GetTrailer()->prevunused = NULL;
			chunk->nextunused = bins[binindex];
			if ( chunk->nextunused )
			{
				ASSERT(chunk->nextunused->IsSane());
				chunk->nextunused->GetTrailer()->prevunused = chunk;
			}
			bins[binindex] = chunk;
			bincontainschunks |= (1UL << binindex);
			ASSERT(chunk->IsSane());
		}

		bool ValidateHeap()
		{
			bool foundbin[NUMBINS];
			for ( size_t i = 0; i < NUMBINS; i++ ) { foundbin[i] = false; }

			#ifdef HEAP_GROWS_DOWNWARDS
			Chunk* chunk = (Chunk*) (wilderness + wildernesssize);
			while ( (addr_t) chunk < heapstart )
			#else
			Chunk* chunk = (Chunk*) heapstart;
			while ( (addr_t) chunk < wilderness - wildernesssize )
			#endif
			{
				size_t timesfound = 0;
				for ( size_t i = 0; i < NUMBINS; i++ )
				{
					if ( chunk == bins[i] ) { foundbin[i] = true; timesfound++; }
				}
				if ( 1 < timesfound ) { return false; }

				if ( !chunk->IsSane() ) { return false; }
				chunk = chunk->RightNeighbor();
			}

			for ( size_t i = 0; i < NUMBINS; i++ )
			{
				if ( !bins[i] )
				{
					if ( foundbin[i] ) { return false; }
					continue;
				}
				if ( !foundbin[i] ) { return false; }
				if ( !bins[i]->IsSane() ) { return false; }
			}

			return true;
		}

		void Init()
		{
			heapstart = GetHeapStart();
			heapmaxsize = GetHeapMaxSize();
			heapsize = 0;
			wilderness = heapstart;
			wildernesssize = 0;
			for ( size_t i = 0; i < NUMBINS; i++ ) { bins[i] = NULL; }
			bincontainschunks = 0;
		}

		// Attempts to expand the wilderness such that it contains at least
		// bytesneeded bytes. This is done by mapping new pages onto into the
		// virtual address-space.
		bool ExpandWilderness(size_t bytesneeded)
		{
			if ( bytesneeded <= wildernesssize ) { return true; }

			bytesneeded -= wildernesssize;

			// Align the increase on page boundaries.
			const size_t PAGEMASK = ~(PAGESIZE - 1UL);
			bytesneeded = ( bytesneeded + PAGESIZE - 1UL ) & PAGEMASK;

			ASSERT(bytesneeded >= PAGESIZE);

			// TODO: Overflow MAY happen here!
			if ( heapmaxsize <= heapsize + wildernesssize + bytesneeded )
			{
				Error::Set(ENOMEM);
				return true;
			}

			#ifdef HEAP_GROWS_DOWNWARDS
			addr_t newwilderness = wilderness - bytesneeded;
			#else
			addr_t newwilderness = wilderness + bytesneeded;
			#endif

			// Attempt to map pages so our wilderness grows.
			if ( !AllocateMemory(newwilderness, bytesneeded) ) { return false; }

			wildernesssize += bytesneeded;
			wilderness = newwilderness;

			return true;
		}

		DUAL_FUNCTION(void*, malloc, Allocate, (size_t size))
		{
			#if 0 < PARANOIA
			ASSERT(ValidateHeap());
			#endif

			const size_t OVERHEAD = sizeof(Chunk) + sizeof(Trailer);
			size += OVERHEAD;

			// Round up to nearest alignment.
			size = (size + ALIGNMENT - 1UL) & (~(ALIGNMENT-1UL));

			// Find the index of the smallest usable bin.
			size_t minbinindex = BSR(size-1UL)+1UL;

			// Make a bitmask that filter away all bins that are too small.
			size_t minbinmask = ~((1UL << minbinindex) - 1UL);

			// Figure out which bins are usable for our chunk.
			size_t availablebins = bincontainschunks & minbinmask;

			if ( availablebins )
			{
				// Find the smallest available bin.
				size_t binindex = BSF(availablebins);

				Chunk* chunk = bins[binindex];
				ASSERT(chunk->IsSane());
				bins[binindex] = chunk->nextunused;

				size_t binsize = 1UL << binindex;

				// Mark the bin as empty if we emptied it.
				if ( !bins[binindex] )
				{
					bincontainschunks ^= binsize;
				}
				else
				{
					Trailer* trailer = bins[binindex]->GetTrailer();
					trailer->prevunused = NULL;
				}

				ASSERT(!bins[binindex] || bins[binindex]->IsSane());

				// If we don't use the entire chunk.
				if ( OVERHEAD <= binsize - size )
				{
					size_t left = binsize - size;
					chunk->size -= left;
					chunk->GetTrailer()->size = chunk->size;

					size_t leftbinindex = BSR(left);
					Chunk* leftchunk = chunk->RightNeighbor();
					leftchunk->size = left;
					Trailer* lefttrailer = leftchunk->GetTrailer();
					lefttrailer->size = left;

					InsertChunk(leftchunk);
				}

				chunk->magic = MAGIC;
				chunk->GetTrailer()->magic = MAGIC;

				#if 0 < PARANOIA
				ASSERT(ValidateHeap());
				#endif

				addr_t result = ((addr_t) chunk) + sizeof(Chunk);
				return (void*) result;
			}

			// If no bins are available, try to allocate from the wilderness.

			// Check if the wilderness can meet our requirements.
			if ( wildernesssize < size && !ExpandWilderness(size) )
			{
				Error::Set(ENOMEM);
				return NULL;
			}

			// Carve a new chunk out of the wilderness and initialize it.
			#ifdef HEAP_GROWS_DOWNWARDS
			Chunk* chunk = (Chunk*) (wilderness + wildernesssize - size);
			#else
			Chunk* chunk = (Chunk*) (wilderness - wildernesssize);
			#endif
			wildernesssize -= size;
			heapsize += size;
			chunk->size = size;
			Trailer* trailer = chunk->GetTrailer();
			trailer->size = size;
			chunk->magic = MAGIC;
			trailer->magic = MAGIC;

			#if 0 < PARANOIA
			ASSERT(ValidateHeap());
			#endif

			addr_t result = ((addr_t) chunk) + sizeof(Chunk);
			return (void*) result;
		}

		bool IsLeftmostChunk(Chunk* chunk)
		{
			#ifdef HEAP_GROWS_DOWNWARDS
			return (addr_t) chunk <= wilderness + wildernesssize;
			#else
			return heapstart <= (addr_t) chunk;
			#endif
		}

		bool IsRightmostChunk(Chunk* chunk)
		{
			#ifdef HEAP_GROWS_DOWNWARDS
			return heapstart <= (addr_t) chunk + chunk->size;
			#else
			return (addr_t) chunk + chunk->size <= heapstart + heapsize;
			#endif
		}

		// Removes a chunk from its bin.
		void UnlinkChunk(Chunk* chunk)
		{
			ASSERT(chunk->IsSane());
			Trailer* trailer = chunk->GetTrailer();
			if ( trailer->prevunused )
			{
				ASSERT(trailer->prevunused->IsSane());
				trailer->prevunused->nextunused = chunk->nextunused;
				if ( chunk->nextunused )
				{
					ASSERT(chunk->nextunused->IsSane());
					chunk->nextunused->GetTrailer()->prevunused = trailer->prevunused;
				}
			}
			else
			{
				if ( chunk->nextunused )
				{
					ASSERT(chunk->nextunused->IsSane());
					chunk->nextunused->GetTrailer()->prevunused = NULL;
				}
				size_t binindex = BSR(chunk->size);
				ASSERT(bins[binindex] == chunk);
				bins[binindex] = chunk->nextunused;
				if ( !bins[binindex] ) { bincontainschunks ^= 1UL << binindex; }
				else { ASSERT(bins[binindex]->IsSane()); }
			}
		}

		// Transforms a chunk and its neighbors into a single chunk if possible.
		void UnifyNeighbors(Chunk** chunk)
		{
			if ( !IsLeftmostChunk(*chunk) )
			{
				Chunk* neighbor = (*chunk)->LeftNeighbor();
				if ( !neighbor->IsUsed() )
				{
					size_t size = neighbor->size;
					size_t chunksize = (*chunk)->size;
					UnlinkChunk(neighbor);
					*chunk = neighbor;
					(*chunk)->size = size + chunksize;
					(*chunk)->GetTrailer()->size = (*chunk)->size;
				}
			}

			if ( !IsRightmostChunk(*chunk) )
			{
				Chunk* neighbor = (*chunk)->RightNeighbor();
				if ( !neighbor->IsUsed() )
				{
					UnlinkChunk(neighbor);
					(*chunk)->size += neighbor->size;
					(*chunk)->GetTrailer()->size = (*chunk)->size;
				}
			}
		}

		DUAL_FUNCTION(void, free, Free, (void* addr))
		{
			#if 0 < PARANOIA
			ASSERT(ValidateHeap());
			#endif

			if ( !addr) { return; }

			Chunk* chunk = (Chunk*) ((addr_t) addr - sizeof(Chunk));
			ASSERT(chunk->IsUsed());
			ASSERT(chunk->IsSane());

			UnifyNeighbors(&chunk);

			#ifdef HEAP_GROWS_DOWNWARDS
			bool nexttowilderness = IsLeftmostChunk(chunk);
			#else
			bool nexttowilderness = IsRightmostChunk(chunk);
			#endif

			// If possible, let the wilderness regain the memory.
			if ( nexttowilderness )
			{
				heapsize -= chunk->size;
				wildernesssize += chunk->size;
				return;
			}

			InsertChunk(chunk);

			#if 0 < PARANOIA
			ASSERT(ValidateHeap());
			#endif
		}
	}
}
