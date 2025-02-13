/*
 * Copyright (c) 2012, 2014, 2015, 2016 Jonas 'Sortie' Termansen.
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
 * sortix/kernel/video.h
 * Framework for Sortix video drivers.
 */

#ifndef _INCLUDE_SORTIX_KERNEL_VIDEO_H
#define _INCLUDE_SORTIX_KERNEL_VIDEO_H

#include <sys/types.h>

#include <sortix/display.h>

#include <sortix/kernel/ioctx.h>
#include <sortix/kernel/refcount.h>

namespace Sortix {

class TextBuffer;
class TextBufferHandle;

class VideoDevice
{
public:
	virtual ~VideoDevice() { }
	virtual uint64_t GetConnectorCount() = 0;
	virtual bool GetDefaultMode(uint64_t connector, struct dispmsg_crtc_mode* mode) = 0;
	virtual bool GetCurrentMode(uint64_t connector, struct dispmsg_crtc_mode* mode) = 0;
	virtual bool SwitchMode(uint64_t connector, struct dispmsg_crtc_mode mode) = 0;
	virtual bool Supports(uint64_t connector, struct dispmsg_crtc_mode mode) = 0;
	virtual struct dispmsg_crtc_mode* GetModes(uint64_t connector, size_t* nummodes) = 0;
	virtual off_t FrameSize() = 0;
	virtual ssize_t WriteAt(ioctx_t* ctx, off_t off, const void* buf, size_t count) = 0;
	virtual ssize_t ReadAt(ioctx_t* ctx, off_t off, void* buf, size_t count) = 0;
	virtual TextBuffer* CreateTextBuffer(uint64_t connector, struct dispmsg_crtc_mode mode) = 0;

public:
	uint64_t device_index;

};

} // namespace Sortix

namespace Sortix {
namespace Video {

bool RegisterDevice(const char* name, VideoDevice* device);
bool ConfigureDevice(VideoDevice* device);
bool ResizeDisplay(uint64_t device, uint64_t connector, uint32_t xres,
                   uint32_t yres, uint32_t bpp);

} // namespace Video
} // namespace Sortix

#endif
