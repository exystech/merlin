/*	$OpenBSD: sha2.c,v 1.28 2019/07/23 12:35:22 dtucker Exp $	*/

/*
 * FILE:	sha2.c
 * AUTHOR:	Aaron D. Gifford <me@aarongifford.com>
 *
 * Copyright (c) 2000-2001, Aaron D. Gifford
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTOR(S) ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTOR(S) BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $From: sha2.c,v 1.1 2001/11/08 00:01:51 adg Exp adg $
 */

#include <sys/types.h>

#include <endian.h>
#include <sha2.h>
#include <stdint.h>
#include <string.h>

#include "sha2.h"

#define BE_64_TO_8(cp, src) do {					\
	(cp)[0] = (src) >> 56;						\
        (cp)[1] = (src) >> 48;						\
	(cp)[2] = (src) >> 40;						\
	(cp)[3] = (src) >> 32;						\
	(cp)[4] = (src) >> 24;						\
	(cp)[5] = (src) >> 16;						\
	(cp)[6] = (src) >> 8;						\
	(cp)[7] = (src);						\
} while (0)

/*** SHA-XYZ INITIAL HASH VALUES AND CONSTANTS ************************/
/* Initial hash value H for SHA-512-256 */
static const uint64_t sha512_256_initial_hash_value[8] = {
	0x22312194fc2bf72cULL,
	0x9f555fa3c84c64c2ULL,
	0x2393b86b6f53b151ULL,
	0x963877195940eabdULL,
	0x96283ee2a88effe3ULL,
	0xbe5e1e2553863992ULL,
	0x2b0199fc2c85b8aaULL,
	0x0eb72ddc81c52ca2ULL
};

/*** SHA-512/256: *********************************************************/
void
SHA512_256Init(SHA2_CTX *context)
{
	memcpy(context->state.st64, sha512_256_initial_hash_value,
	    sizeof(sha512_256_initial_hash_value));
	memset(context->buffer, 0, sizeof(context->buffer));
	context->bitcount[0] = context->bitcount[1] = 0;
}

void
SHA512_256Transform(uint64_t state[8], const uint8_t data[SHA512_256_BLOCK_LENGTH])
{
	SHA512Transform(state, data);
}

void
SHA512_256Update(SHA2_CTX *context, const uint8_t *data, size_t len)
{
	SHA512Update(context, data, len);
}

void
SHA512_256Pad(SHA2_CTX *context)
{
	SHA512Pad(context);
}

void
SHA512_256Final(uint8_t digest[SHA512_256_DIGEST_LENGTH], SHA2_CTX *context)
{
	SHA512_256Pad(context);

#if BYTE_ORDER == LITTLE_ENDIAN
	int	i;

	/* Convert TO host byte order */
	for (i = 0; i < 4; i++)
		BE_64_TO_8(digest + i * 8, context->state.st64[i]);
#else
	memcpy(digest, context->state.st64, SHA512_256_DIGEST_LENGTH);
#endif
	/* Zero out state data */
	explicit_bzero(context, sizeof(*context));
}
