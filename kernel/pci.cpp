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

    pci.cpp
    Functions for handling PCI devices.

*******************************************************************************/

#include <assert.h>
#include <endian.h>

#include <sortix/kernel/cpu.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/pci.h>

namespace Sortix {
namespace PCI {

static kthread_mutex_t pci_lock = KTHREAD_MUTEX_INITIALIZER;

const uint16_t CONFIG_ADDRESS = 0xCF8;
const uint16_t CONFIG_DATA = 0xCFC;

uint32_t MakeDevAddr(uint8_t bus, uint8_t slot, uint8_t func)
{
	//assert(bus < 1UL<<8UL); // bus is 8 bit anyways.
	assert(slot < 1UL<<5UL);
	assert(func < 1UL<<3UL);
	return func << 8U | slot << 11U | bus << 16U | 1 << 31U;
}

void SplitDevAddr(uint32_t devaddr, uint8_t* vals /* bus, slot, func */)
{
	vals[0] = devaddr >> 16U & ((1UL<<8UL)-1);
	vals[1] = devaddr >> 11U & ((1UL<<3UL)-1);
	vals[2] = devaddr >>  8U & ((1UL<<5UL)-1);
}

uint32_t ReadRaw32(uint32_t devaddr, uint8_t off)
{
	CPU::OutPortL(CONFIG_ADDRESS, devaddr + off);
	return CPU::InPortL(CONFIG_DATA);
}

void WriteRaw32(uint32_t devaddr, uint8_t off, uint32_t val)
{
	CPU::OutPortL(CONFIG_ADDRESS, devaddr + off);
	CPU::OutPortL(CONFIG_DATA, val);
}

uint32_t Read32(uint32_t devaddr, uint8_t off)
{
	return le32toh(ReadRaw32(devaddr, off));
}

void Write32(uint32_t devaddr, uint8_t off, uint32_t val)
{
	WriteRaw32(devaddr, off, htole32(val));
}

uint16_t Read16(uint32_t devaddr, uint8_t off)
{
	assert((off & 0x1) == 0);
	uint8_t alignedoff = off & ~0x3;
	union { uint16_t val16[2]; uint32_t val32; };
	val32 = ReadRaw32(devaddr, alignedoff);
	uint16_t ret = off & 0x2 ? val16[0] : val16[1];
	return le16toh(ret);
}

uint8_t Read8(uint32_t devaddr, uint8_t off)
{
	uint8_t alignedoff = off & ~0x1;
	union { uint8_t val8[2]; uint32_t val16; };
	val16 = htole16(Read16(devaddr, alignedoff));
	uint8_t ret = off & 0x1 ? val8[0] : val8[1];
	return ret;
}

uint32_t CheckDevice(uint8_t bus, uint8_t slot, uint8_t func)
{
	return Read32(MakeDevAddr(bus, slot, func), 0x0);
}

pciid_t GetDeviceId(uint32_t devaddr)
{
	pciid_t ret;
	ret.deviceid = Read16(devaddr, 0x00);
	ret.vendorid = Read16(devaddr, 0x02);
	return ret;
}

pcitype_t GetDeviceType(uint32_t devaddr)
{
	pcitype_t ret;
	ret.classid = Read8(devaddr, 0x08);
	ret.subclassid = Read8(devaddr, 0x09);
	ret.progif = Read8(devaddr, 0x0A);
	ret.revid = Read8(devaddr, 0x0B);
	return ret;
}

static bool MatchesSearchCriteria(uint32_t devaddr, pcifind_t pcifind)
{
	pciid_t id = GetDeviceId(devaddr);
	if ( id.vendorid == 0xFFFF && id.deviceid == 0xFFFF )
		return false;
	pcitype_t type = GetDeviceType(devaddr);
	if ( pcifind.vendorid != 0xFFFF && id.vendorid != pcifind.vendorid )
		return false;
	if ( pcifind.deviceid != 0xFFFF && id.deviceid != pcifind.deviceid )
		return false;
	if ( pcifind.classid != 0xFF && type.classid != pcifind.classid )
		return false;
	if ( pcifind.subclassid != 0xFF && type.subclassid != pcifind.subclassid )
		return false;
	if ( pcifind.progif != 0xFF && type.progif != pcifind.progif )
		return false;
	if ( pcifind.revid != 0xFF && type.revid != pcifind.revid )
		return false;
	return true;
}

static uint32_t SearchForDeviceOnBus(uint8_t bus, pcifind_t pcifind)
{
	for ( unsigned slot = 0; slot < 32; slot++ )
	{
		unsigned numfuncs = 1;
		for ( unsigned func = 0; func < numfuncs; func++ )
		{
			uint32_t devaddr = MakeDevAddr(bus, slot, func);
			if ( MatchesSearchCriteria(devaddr, pcifind) )
				return devaddr;
			uint8_t header = Read8(devaddr, 0x0D); // Secondary Bus Number.
			if ( header & 0x80 ) // Multi function device.
				numfuncs = 8;
			if ( (header & 0x7F) == 0x01 ) // PCI to PCI bus.
			{
				uint8_t subbusid = Read8(devaddr, 0x1A);
				uint32_t recret = SearchForDeviceOnBus(subbusid, pcifind);
				if ( recret )
					return recret;
			}
		}
	}
	return 0;
}

uint32_t SearchForDevice(pcifind_t pcifind)
{
	// Search on bus 0 and recurse on other detected busses.
	return SearchForDeviceOnBus(0, pcifind);
}

pcibar_t GetBAR(uint32_t devaddr, uint8_t bar)
{
	ScopedLock lock(&pci_lock);

	uint32_t low = PCI::Read32(devaddr, 0x10 + 4 * (bar+0));

	pcibar_t result;
	result.addr_raw = low;
	result.size_raw = 0;
	if ( result.is_64bit() )
	{
		uint32_t high = PCI::Read32(devaddr, 0x10 + 4 * (bar+1));
		result.addr_raw |= (uint64_t) high << 32;
		PCI::Write32(devaddr, 0x10 + 4 * (bar+0), 0xFFFFFFFF);
		PCI::Write32(devaddr, 0x10 + 4 * (bar+1), 0xFFFFFFFF);
		uint32_t size_low = PCI::Read32(devaddr, 0x10 + 4 * (bar+0));
		uint32_t size_high = PCI::Read32(devaddr, 0x10 + 4 * (bar+1));
		PCI::Write32(devaddr, 0x10 + 4 * (bar+0), low);
		PCI::Write32(devaddr, 0x10 + 4 * (bar+1), high);
		result.size_raw = (uint64_t) size_high << 32 | (uint64_t) size_low << 0;
		result.size_raw = ~(result.size_raw & 0xFFFFFFFFFFFFFFF0) + 1;
	}
	else if ( result.is_32bit() )
	{
		PCI::Write32(devaddr, 0x10 + 4 * (bar+0), 0xFFFFFFFF);
		uint32_t size_low = PCI::Read32(devaddr, 0x10 + 4 * (bar+0));
		PCI::Write32(devaddr, 0x10 + 4 * (bar+0), low);
		result.size_raw = (uint64_t) size_low << 0;
		result.size_raw = ~(result.size_raw & 0xFFFFFFF0) + 1;
		result.size_raw &= 0xFFFFFFFF;
	}
	else if ( result.is_iospace() )
	{
		PCI::Write32(devaddr, 0x10 + 4 * (bar+0), 0xFFFFFFFF);
		uint32_t size_low = PCI::Read32(devaddr, 0x10 + 4 * (bar+0));
		PCI::Write32(devaddr, 0x10 + 4 * (bar+0), low);
		result.size_raw = (uint64_t) size_low << 0;
		result.size_raw = ~(result.size_raw & 0xFFFFFFFC) + 1;
		result.size_raw &= 0xFFFFFFFF;
	}

	return result;
}

pcibar_t GetExpansionROM(uint32_t devaddr)
{
	const uint32_t ROM_ADDRESS_MASK = ~UINT32_C(0x7FF);

	ScopedLock lock(&pci_lock);

	uint32_t low = PCI::Read32(devaddr, 0x30);
	PCI::Write32(devaddr, 0x30, ROM_ADDRESS_MASK | low);
	uint32_t size_low = PCI::Read32(devaddr, 0x30);
	PCI::Write32(devaddr, 0x30, low);

	pcibar_t result;
	result.addr_raw = (low & ROM_ADDRESS_MASK) | PCIBAR_TYPE_32BIT;
	result.size_raw = ~(size_low & ROM_ADDRESS_MASK) + 1;
	return result;
}

void EnableExpansionROM(uint32_t devaddr)
{
	ScopedLock lock(&pci_lock);

	PCI::Write32(devaddr, 0x30, PCI::Read32(devaddr, 0x30) | 0x1);
}

void DisableExpansionROM(uint32_t devaddr)
{
	ScopedLock lock(&pci_lock);

	PCI::Write32(devaddr, 0x30, PCI::Read32(devaddr, 0x30) & ~UINT32_C(0x1));
}

bool IsExpansionROMEnabled(uint32_t devaddr)
{
	ScopedLock lock(&pci_lock);

	return PCI::Read32(devaddr, 0x30) & 0x1;
}

void Init()
{
}

} // namespace PCI
} // namespace Sortix
