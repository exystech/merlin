/*
 * Copyright (c) 2012, 2013, 2014, 2015, 2016, 2021 Jonas 'Sortie' Termansen.
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
 * logterminal.cpp
 * A simple terminal that writes to the kernel log.
 */

#include <sys/types.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <sortix/display.h>
#include <sortix/fcntl.h>
#include <sortix/ioctl.h>
#include <sortix/keycodes.h>
#include <sortix/poll.h>
#include <sortix/signal.h>
#include <sortix/stat.h>
#include <sortix/termios.h>
#include <sortix/termmode.h>
#include <sortix/winsize.h>

#include <sortix/kernel/inode.h>
#include <sortix/kernel/interlock.h>
#include <sortix/kernel/ioctx.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/keyboard.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/poll.h>
#include <sortix/kernel/process.h>
#include <sortix/kernel/ptable.h>
#include <sortix/kernel/refcount.h>
#include <sortix/kernel/scheduler.h>
#include <sortix/kernel/thread.h>

#include "logterminal.h"

#define CONTROL(x) (((x) - 64) & 127)
#define M_CONTROL(x) (128 + CONTROL(x))

namespace Sortix {

static const int MODIFIER_ALT = 1 << 0;
static const int MODIFIER_LSHIFT = 1 << 1;
static const int MODIFIER_RSHIFT = 1 << 2;
static const int MODIFIER_LCONTROL = 1 << 3;
static const int MODIFIER_RCONTROL = 1 << 4;

static const int SEQUENCE_1IFMOD = 1 << 0;
static const int SEQUENCE_OSHORT = 1 << 1;

struct kbkey_sequence
{
	const char* sequence;
	int kbkey;
	int flags;
};

static const struct kbkey_sequence kbkey_sequences[] =
{
	{ "\e[A", KBKEY_UP, SEQUENCE_1IFMOD },
	{ "\e[B", KBKEY_DOWN, SEQUENCE_1IFMOD},
	{ "\e[C", KBKEY_RIGHT, SEQUENCE_1IFMOD },
	{ "\e[D", KBKEY_LEFT, SEQUENCE_1IFMOD },
	{ "\e[F", KBKEY_END, SEQUENCE_1IFMOD },
	{ "\e[H", KBKEY_HOME, SEQUENCE_1IFMOD },
	{ "\e[2~", KBKEY_INSERT, 0 },
	{ "\e[3~", KBKEY_DELETE, 0 },
	{ "\e[5~", KBKEY_PGUP, 0 },
	{ "\e[6~", KBKEY_PGDOWN, 0 },
	{ "\e[1P", KBKEY_F1, SEQUENCE_OSHORT },
	{ "\e[1Q", KBKEY_F2, SEQUENCE_OSHORT },
	{ "\e[1R", KBKEY_F3, SEQUENCE_OSHORT },
	{ "\e[1S", KBKEY_F4, SEQUENCE_OSHORT },
	{ "\e[15~", KBKEY_F5, 0 },
	{ "\e[17~", KBKEY_F6, 0 },
	{ "\e[18~", KBKEY_F7, 0 },
	{ "\e[19~", KBKEY_F8, 0 },
	{ "\e[20~", KBKEY_F9, 0 },
	{ "\e[21~", KBKEY_F10, 0 },
	{ "\e[23~", KBKEY_F11, 0 },
	{ "\e[24~", KBKEY_F12, 0 },
};

static inline const struct kbkey_sequence* LookupKeystrokeSequence(int kbkey)
{
	size_t count = sizeof(kbkey_sequences) / sizeof(kbkey_sequences[0]);
	for ( size_t i = 0; i < count; i++ )
		if ( kbkey_sequences[i].kbkey == kbkey )
			return &kbkey_sequences[i];
	return NULL;
}

LogTerminal::LogTerminal(dev_t dev, mode_t mode, uid_t owner, gid_t group,
                         Keyboard* keyboard, KeyboardLayoutExecutor* kblayout,
                         const char* name)
	: TTY(dev, 0, mode, owner, group, name)
{
	this->keyboard = keyboard;
	this->kblayout = kblayout;
	this->modifiers = 0;
	this->report_cursor_offset = 0;
	keyboard->SetOwner(this, NULL);
}

LogTerminal::~LogTerminal()
{
	delete keyboard;
	delete kblayout;
}

void LogTerminal::tty_output(const unsigned char* buffer, size_t length)
{
	const char* report_cursor = "\e[6n";
	while ( length )
	{
		size_t amount = 0;
		bool found_report_cursor = false;
		while ( !found_report_cursor && amount < length )
		{
			if ( buffer[amount++] == report_cursor[report_cursor_offset++] )
				found_report_cursor = !report_cursor[report_cursor_offset];
			else
				report_cursor_offset = 0;
		}
		Log::PrintData(buffer, amount);
		buffer += amount;
		length -= amount;
		if ( found_report_cursor )
		{
			size_t column, row;
			Log::GetCursor(&column, &row);
			char buf[64];
			snprintf(buf, sizeof(buf), "\e[%zu;%zuR", row + 1, column + 1);
			for ( size_t n = 0; buf[n]; n++ )
				if ( !linebuffer.Push(buf[n]) )
					break;
			CommitLineBuffer();
		}
	}
}

int LogTerminal::sync(ioctx_t* /*ctx*/)
{
	ScopedLock lock(&termlock);
	if ( hungup )
		return errno = EIO, -1;
	return Log::Sync() ? 0 : -1;
}

void LogTerminal::OnKeystroke(Keyboard* kb, void* /*user*/)
{
	ScopedLock lock(&termlock);
	while ( kb->HasPending() )
	{
		int kbkey = kb->Read();
		if ( kbkey == KBKEY_LALT )
			modifiers |= MODIFIER_ALT;
		else if ( kbkey == -KBKEY_LALT )
			modifiers &= ~MODIFIER_ALT;
		else if ( kbkey == KBKEY_LSHIFT )
			modifiers |= MODIFIER_LSHIFT;
		else if ( kbkey == -KBKEY_LSHIFT )
			modifiers &= ~MODIFIER_LSHIFT;
		else if ( kbkey == KBKEY_RSHIFT )
			modifiers |= MODIFIER_RSHIFT;
		else if ( kbkey == -KBKEY_RSHIFT )
			modifiers &= ~MODIFIER_RSHIFT;
		else if ( kbkey == KBKEY_LCTRL )
			modifiers |= MODIFIER_LCONTROL;
		else if ( kbkey == -KBKEY_LCTRL )
			modifiers &= ~MODIFIER_LCONTROL;
		else if ( kbkey == KBKEY_RCTRL )
			modifiers |= MODIFIER_RCONTROL;
		else if ( kbkey == -KBKEY_RCTRL )
			modifiers &= ~MODIFIER_RCONTROL;
		uint32_t unicode = kblayout->Translate(kbkey);
		if ( !(tio.c_cflag & CREAD) )
			continue;
		ProcessKeystroke(kbkey);
		if ( !unicode )
			continue;
		if ( unicode == '\n' )
			unicode = '\r';
		bool control = modifiers & (MODIFIER_LCONTROL | MODIFIER_RCONTROL);
		if ( !(tio.c_cflag & ISORTIX_TERMMODE) && unicode == '\b' )
			unicode = 127;
		if ( modifiers & MODIFIER_ALT && !(tio.c_lflag & ISORTIX_KBKEY) )
			ProcessByte('\e');
		if ( control && unicode == L' ' )
			ProcessByte(0, unicode);
		else if ( control && (L'`' <= unicode && unicode <= L'}') )
			ProcessByte(unicode - L'`', unicode);
		else if ( control && (L'@' <= unicode && unicode <= L'_') )
			ProcessByte(unicode - L'@', unicode);
		else if ( control && unicode == L'?' )
			ProcessByte(127, unicode);
		else
			ProcessUnicode(unicode);
	}
}

void LogTerminal::ProcessKeystroke(int kbkey)
{
	if ( tio.c_lflag & ISORTIX_32BIT )
	{
		if ( tio.c_lflag & ISORTIX_KBKEY )
		{
			uint32_t unikbkey = KBKEY_ENCODE(kbkey);
			if ( !linebuffer.Push(unikbkey) )
				return;
			if ( !(tio.c_lflag & ICANON) )
				CommitLineBuffer();
		}
		return;
	}

	if ( kbkey < 0 )
		return;

	if ( kbkey == KBKEY_ESC )
	{
		ProcessByte('\e');
		return;
	}

	const struct kbkey_sequence* seq = LookupKeystrokeSequence(kbkey);
	if ( !seq )
		return;

	const char* str = seq->sequence;
	size_t len = strlen(str);

	int mods = 0;
	if ( modifiers & (MODIFIER_LSHIFT | MODIFIER_RSHIFT) )
		mods |= 1;
	if ( modifiers & MODIFIER_ALT )
		mods |= 2;
	if ( modifiers & (MODIFIER_LCONTROL | MODIFIER_RCONTROL) )
		mods |= 4;

	if ( (seq->flags & SEQUENCE_OSHORT) && mods == 0 )
	{
		ProcessByte('\e');
		ProcessByte('O');
		ProcessByte((unsigned char) str[len-1]);
		return;
	}

	for ( size_t i = 0; i < len - 1; i++ )
		ProcessByte((unsigned char) str[i]);
	if ( seq->flags & SEQUENCE_1IFMOD && mods != 0 )
		ProcessByte('1');
	if ( mods )
	{
		ProcessByte(';');
		ProcessByte('1' + mods);
	}
	ProcessByte(str[len-1]);
}

ssize_t LogTerminal::tcgetblob(ioctx_t* ctx, const char* name, void* buffer, size_t count)
{
	ScopedLock lock(&termlock);
	if ( hungup )
		return errno = EIO, -1;
	if ( !name )
	{
		static const char index[] = "kblayout\0";
		size_t index_size = sizeof(index) - 1;
		if ( buffer && count < index_size )
			return errno = ERANGE, -1;
		if ( buffer && !ctx->copy_to_dest(buffer, &index, index_size) )
			return -1;
		return (ssize_t) index_size;
	}
	else if ( !strcmp(name, "kblayout") )
	{
		const uint8_t* data;
		size_t size;
		if ( !kblayout->Download(&data, &size) )
			return -1;
		if ( buffer && count < size )
			return errno = ERANGE, -1;
		if ( buffer && !ctx->copy_to_dest(buffer, data, size) )
			return -1;
		return (ssize_t) size;
	}
	else
		return errno = ENOENT, -1;
}

ssize_t LogTerminal::tcsetblob(ioctx_t* ctx, const char* name, const void* buffer, size_t count)
{
	ScopedLock lock(&termlock);
	if ( hungup )
		return errno = EIO, -1;
	if ( !name )
		return errno = EPERM, -1;
	else if ( !strcmp(name, "kblayout") )
	{
		uint8_t* data = new uint8_t[count];
		if ( !data )
			return -1;
		if ( !ctx->copy_from_src(data, buffer, count) )
			return -1;
		if ( !kblayout->Upload(data, count) )
			return -1;
		delete[] data;
		return (ssize_t) count;
	}
	else
		return errno = ENOENT, -1;
}

int LogTerminal::tcgetwincurpos(ioctx_t* ctx, struct wincurpos* wcp)
{
	ScopedLock lock(&termlock);
	if ( hungup )
		return errno = EIO, -1;
	struct wincurpos retwcp;
	memset(&retwcp, 0, sizeof(retwcp));
	size_t cursor_column, cursor_row;
	Log::GetCursor(&cursor_column, &cursor_row);
	retwcp.wcp_col = cursor_column;
	retwcp.wcp_row = cursor_row;
	if ( !ctx->copy_to_dest(wcp, &retwcp, sizeof(retwcp)) )
		return -1;
	return 0;
}

int LogTerminal::ioctl(ioctx_t* ctx, int cmd, uintptr_t arg)
{
	ScopedLock lock(&termlock);
	if ( hungup )
		return errno = EIO, -1;
	if ( cmd == TIOCGWINSZ )
	{
		struct winsize* ws = (struct winsize*) arg;
		struct winsize retws;
		memset(&retws, 0, sizeof(retws));
		retws.ws_col = Log::Width();
		retws.ws_row = Log::Height();
		if ( !ctx->copy_to_dest(ws, &retws, sizeof(retws)) )
			return -1;
		return 0;
	}
	else if ( cmd == TIOCGDISPLAYS )
	{
		struct tiocgdisplays* input = (struct tiocgdisplays*) arg;
		struct tiocgdisplays gdisplays;
		if ( !ctx->copy_from_src(&gdisplays, input, sizeof(gdisplays)) )
			return -1;
		if ( 0 < gdisplays.count )
		{
			struct tiocgdisplay display;
			memset(&display, 0, sizeof(display));
			display.device = 0;
			display.connector = 0;
			if ( !ctx->copy_to_dest(gdisplays.displays, &display,
			                        sizeof(display)) )
				return -1;
		}
		gdisplays.count = 1;
		if ( !ctx->copy_to_dest(input, &gdisplays, sizeof(gdisplays)) )
			return -1;
		return 0;
	}
	lock.Reset();
	return TTY::ioctl(ctx, cmd, arg);
}

} // namespace Sortix
