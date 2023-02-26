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
 * logterminal.h
 * A simple terminal that writes to the kernel log.
 */

#ifndef SORTIX_LOGTERMINAL_H
#define SORTIX_LOGTERMINAL_H

#include "tty.h"

namespace Sortix {

class LogTerminal : public TTY, public KeyboardOwner
{
public:
	LogTerminal(dev_t dev, mode_t mode, uid_t owner, gid_t group,
	            Keyboard* keyboard, KeyboardLayoutExecutor* kblayout,
	            const char* name);
	virtual ~LogTerminal();

public:
	virtual int sync(ioctx_t* ctx);
	virtual ssize_t tcgetblob(ioctx_t* ctx, const char* name, void* buffer, size_t count);
	virtual ssize_t tcsetblob(ioctx_t* ctx, const char* name, const void* buffer, size_t count);
	virtual int tcgetwincurpos(ioctx_t* ctx, struct wincurpos* wcp);
	virtual int ioctl(ioctx_t* ctx, int cmd, uintptr_t arg);

public:
	virtual void OnKeystroke(Keyboard* keyboard, void* user);

protected:
	virtual void tty_output(const unsigned char* buffer, size_t length);

private:
	void ProcessKeystroke(int kbkey);

private:
	Keyboard* keyboard;
	KeyboardLayoutExecutor* kblayout;
	int modifiers;
	size_t report_cursor_offset;

};

} // namespace Sortix

#endif
