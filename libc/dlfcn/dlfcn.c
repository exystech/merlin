/*
 * Copyright (c) 2012, 2013 Jonas 'Sortie' Termansen.
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
 * dlfcn/dlfcn.c
 * Dynamic linking.
 */

#include <stdio.h>
#include <dlfcn.h>

static const char* dlerrormsg = NULL;

void* dlopen(const char* filename, int mode)
{
	(void) filename;
	(void) mode;
	dlerrormsg = "Dynamic linking is not implemented";
	return NULL;
}

void* dlsym(void* handle, const char* name)
{
	(void) handle;
	(void) name;
	dlerrormsg = "Dynamic linking is not implemented";
	return NULL;
}

int dlclose(void* handle)
{
	(void) handle;
	return 0;
}

char* dlerror(void)
{
	const char* result = dlerrormsg;
	dlerrormsg = NULL;
	return (char*) result;
}
