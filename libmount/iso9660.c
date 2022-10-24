/*
 * Copyright (c) 2022 Jonas 'Sortie' Termansen.
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
 * iso9660.c
 * iso9660 filesystem.
 */

#include <endian.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <mount/blockdevice.h>
#include <mount/filesystem.h>
#include <mount/iso9660.h>
#include <mount/uuid.h>

#include "util.h"

static size_t iso9660_probe_amount(struct blockdevice* bdev)
{
	(void) bdev;
	size_t block_size = 2048 /* TODO: Actual block size? */;
	return 17 * block_size;
}

static bool iso9660_probe(struct blockdevice* bdev,
                          const unsigned char* leading,
                          size_t amount)
{
	(void) bdev;
	size_t block_size = 2048 /* TODO: Actual block size? */;
	if ( amount < 17 * block_size )
		return false;
	size_t offset = 16 * block_size;
	return !memcmp(leading + offset + 1, "CD001", 5);
}

static void iso9660_release(struct filesystem* fs)
{
	if ( !fs )
		return;
	free(fs);
}

static enum filesystem_error iso9660_inspect(struct filesystem** fs_ptr,
                                             struct blockdevice* bdev)
{
	*fs_ptr = NULL;
	struct filesystem* fs = CALLOC_TYPE(struct filesystem);
	if ( !fs )
		return FILESYSTEM_ERROR_ERRNO;
	fs->bdev = bdev;
	fs->handler = &iso9660_handler;
	fs->handler_private = NULL;
	fs->fstype_name = "iso9660";
	fs->driver = "iso9660fs";
	size_t block_size = 2048 /* TODO: Actual block size? */;
	size_t offset = 16 * block_size;
	uint8_t pvd[2048];
	if ( blockdevice_preadall(bdev, pvd, sizeof(pvd), offset) != sizeof(pvd) )
		return iso9660_release(fs), FILESYSTEM_ERROR_ERRNO;
	// TODO: LABEL support using the volume name.
	char vol_set[128 + 1];
	memcpy(vol_set, pvd + 190, 128);
	vol_set[128] = '\0';
	for ( size_t i = 128; i && vol_set[i-1] == ' '; i-- )
		vol_set[i-1] = '\0';
	if ( uuid_validate(vol_set) )
	{
		fs->flags |= FILESYSTEM_FLAG_UUID;
		uuid_from_string(fs->uuid, vol_set);
	}
	return *fs_ptr = fs, FILESYSTEM_ERROR_NONE;
}

const struct filesystem_handler iso9660_handler =
{
	.handler_name = "iso9660",
	.probe_amount = iso9660_probe_amount,
	.probe = iso9660_probe,
	.inspect = iso9660_inspect,
	.release = iso9660_release,
};
