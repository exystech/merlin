/*
 * Copyright (c) 2011, 2014, 2013, 2014, 2015, 2016 Jonas 'Sortie' Termansen.
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
 * panic.cpp
 * Displays an error whenever something critical happens.
 */

#include <brand.h>
#include <string.h>

#include <sortix/kernel/interrupt.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/log.h>
#include <sortix/kernel/panic.h>

namespace Sortix {

#if defined(PANIC_SHORT)
const bool longpanic = false;
#else
const bool longpanic = true;
#endif

static bool panicing = false;
static bool doublepanic = false;
static bool logrecovering = false;

void PanicInit()
{
	// TODO: Some panics are soft the console rendering and core features are
	//       perfectly online, they do not need too paranoid handling here,
	//       while others (like a real kernel crash) is critical and this is
	//       needed. Supply multiple kernel panicing interfaces and switch code
	//       to using them instead.

	// This is a kernel emergency. We will need to disable preemption, such that
	// this is the only thread running. This means that we cannot acquire locks
	// and the data protected by them may be inconsistent.
	Interrupt::Disable();

	// Detect if we are really early and we don't even have a log yet.
	if ( !Log::device_callback )
		HaltKernel();

	// Detect whether a panic happened during the log recovery.
	if ( logrecovering )
	{
		// Oh no! We paniced during the log recovery that we will do momentarily
		// - this means that there probably isn't anything we can do but halt.
		HaltKernel();
	}
	logrecovering = true;

	// The kernel log normally uses locks internally and the console may be
	// rendered by a background thread. This means that we cannot use the normal
	// kernel log, but that we rather need to switch to the kernel emergency
	// log, which is able to cope with the potential inconsistencies.

	Log::device_callback = Log::emergency_device_callback;
	Log::device_width = Log::emergency_device_width;
	Log::device_height = Log::emergency_device_height;
	Log::device_get_cursor = Log::emergency_device_get_cursor;
	Log::device_sync = Log::emergency_device_sync;
	Log::device_pointer = Log::emergency_device_pointer;

	// Check whether the panic condition left the kernel log unharmed.
	if ( !Log::emergency_device_is_impaired(Log::emergency_device_pointer) )
	{
		// The kernel log device transitioned ideally to the emergency state.
	}

	// Attempt to repair inconsistent state of the emergency log device.
	else if ( Log::emergency_device_recoup(Log::emergency_device_pointer) )
	{
		// The kernel log was successfully repaired and is ready for use in the
		// current emergency state.
	}

	// It was not possible to repair the emergency device properly, so instead
	// we will need to perform a hard reset of the emergency device.
	else
	{
		Log::emergency_device_reset(Log::emergency_device_pointer);
		// The kernel log was successfully repaired and is ready for use in the
		// current emergency state.
	}

	// Handle the case where the panic code caused another system crash.
	if ( panicing )
	{
		Log::Print("Panic while panicing:\n");
		doublepanic = true;
		return;
	}
	panicing = true;

	// Render a notice that the system has crashed and forcefully shut down.
	if ( longpanic )
	{
		Log::Print("\e[m\e[31;40m\e[2J\e[H");
		Log::Center(BRAND_LOGO_PANIC);
		Log::Center("KERNEL PANIC");
		Log::Print("\n\nThe operating system encountered an unrecoverable "
		           "error.\n\nTechincal information:\n");
	}
	else
	{
		Log::Print("\e[m\e[31m\e[0Jkernel: panic: ");
	}
}

extern "C" __attribute__((noreturn)) void Panic(const char* error)
{
	PanicInit();
	Log::Print(error);
	HaltKernel();
}

extern "C" __attribute__((noreturn)) void PanicF(const char* format, ...)
{
	PanicInit();
	va_list list;
	va_start(list, format);
	Log::PrintFV(format, list);
	va_end(list);
	HaltKernel();
}

} // namespace Sortix
