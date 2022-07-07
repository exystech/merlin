/*
 * Copyright (c) 2014, 2017 Jonas 'Sortie' Termansen.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * sortix/kernel/pci-mmio.h
 * Functions for handling PCI devices.
 */

#ifndef _INCLUDE_SORTIX_KERNEL_PCI_MMIO_H
#define _INCLUDE_SORTIX_KERNEL_PCI_MMIO_H

#include <sortix/kernel/addralloc.h>
#include <sortix/kernel/memorymanagement.h>
#include <sortix/kernel/pci.h>

namespace Sortix {

struct paddrmapped_t
{
	addr_t from;
	addr_t phys;
	size_t size;
	enum page_usage usage;
};

bool MapPCIBAR(addralloc_t* allocation, pcibar_t bar, addr_t mtype);
void UnmapPCIBar(addralloc_t* allocation);
bool AllocateAndMapPage(paddrmapped_t* ret, enum page_usage usage,
                        addr_t mtype = Memory::PAT_WB);
void FreeAllocatedAndMappedPage(paddrmapped_t* alloc);

} // namespace Sortix

#endif
