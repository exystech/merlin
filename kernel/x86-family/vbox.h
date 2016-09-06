/*
 * Copyright (c) 2016 Jonas 'Sortie' Termansen.
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
 * x86-family/vbox.h
 * VirtualBox Guest Additions.
 */

#ifndef SORTIX_X86_FAMILY_VBOX_H
#define SORTIX_X86_FAMILY_VBOX_H

namespace Sortix {
namespace VBox {

class GuestAdditions
{
public:
	virtual ~GuestAdditions() { }

public:
	virtual bool IsSupportedVideoMode(uint32_t display, uint32_t xres,
	                                  uint32_t yres, uint32_t bpp) = 0;
	virtual bool GetBestVideoMode(uint32_t display, uint32_t* xres,
	                              uint32_t* yres, uint32_t* bpp) = 0;
	virtual bool RegisterVideoDevice(uint64_t device_id) = 0;
	virtual void UnregisterVideoDevice(uint64_t device_id) = 0;

};

void Init();
GuestAdditions* GetGuestAdditions();

} // namespace VBox
} // namespace Sortix

#endif
