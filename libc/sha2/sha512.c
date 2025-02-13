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

/*** SHA-224/256/384/512 Various Length Definitions ***********************/
/* NOTE: Most of these are in sha2.h */
#define SHA512_SHORT_BLOCK_LENGTH	(SHA512_BLOCK_LENGTH - 16)

/*** ENDIAN SPECIFIC COPY MACROS **************************************/
#define BE_8_TO_32(dst, cp) do {					\
	(dst) = (uint32_t)(cp)[3] | ((uint32_t)(cp)[2] << 8) |	\
	    ((uint32_t)(cp)[1] << 16) | ((uint32_t)(cp)[0] << 24);	\
} while(0)

#define BE_8_TO_64(dst, cp) do {					\
	(dst) = (uint64_t)(cp)[7] | ((uint64_t)(cp)[6] << 8) |	\
	    ((uint64_t)(cp)[5] << 16) | ((uint64_t)(cp)[4] << 24) |	\
	    ((uint64_t)(cp)[3] << 32) | ((uint64_t)(cp)[2] << 40) |	\
	    ((uint64_t)(cp)[1] << 48) | ((uint64_t)(cp)[0] << 56);	\
} while (0)

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

#define BE_32_TO_8(cp, src) do {					\
	(cp)[0] = (src) >> 24;						\
	(cp)[1] = (src) >> 16;						\
	(cp)[2] = (src) >> 8;						\
	(cp)[3] = (src);						\
} while (0)

/*
 * Macro for incrementally adding the unsigned 64-bit integer n to the
 * unsigned 128-bit integer (represented using a two-element array of
 * 64-bit words):
 */
#define ADDINC128(w,n) do {						\
	(w)[0] += (uint64_t)(n);					\
	if ((w)[0] < (n)) {						\
		(w)[1]++;						\
	}								\
} while (0)

/*** THE SIX LOGICAL FUNCTIONS ****************************************/
/*
 * Bit shifting and rotation (used by the six SHA-XYZ logical functions:
 *
 *   NOTE:  The naming of R and S appears backwards here (R is a SHIFT and
 *   S is a ROTATION) because the SHA-224/256/384/512 description document
 *   (see http://csrc.nist.gov/cryptval/shs/sha256-384-512.pdf) uses this
 *   same "backwards" definition.
 */
/* Shift-right (used in SHA-224, SHA-256, SHA-384, and SHA-512): */
#define R(b,x) 		((x) >> (b))
/* 32-bit Rotate-right (used in SHA-224 and SHA-256): */
#define S32(b,x)	(((x) >> (b)) | ((x) << (32 - (b))))
/* 64-bit Rotate-right (used in SHA-384 and SHA-512): */
#define S64(b,x)	(((x) >> (b)) | ((x) << (64 - (b))))

/* Two of six logical functions used in SHA-224, SHA-256, SHA-384, and SHA-512: */
#define Ch(x,y,z)	(((x) & (y)) ^ ((~(x)) & (z)))
#define Maj(x,y,z)	(((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

/* Four of six logical functions used in SHA-224 and SHA-256: */
#define Sigma0_256(x)	(S32(2,  (x)) ^ S32(13, (x)) ^ S32(22, (x)))
#define Sigma1_256(x)	(S32(6,  (x)) ^ S32(11, (x)) ^ S32(25, (x)))
#define sigma0_256(x)	(S32(7,  (x)) ^ S32(18, (x)) ^ R(3 ,   (x)))
#define sigma1_256(x)	(S32(17, (x)) ^ S32(19, (x)) ^ R(10,   (x)))

/* Four of six logical functions used in SHA-384 and SHA-512: */
#define Sigma0_512(x)	(S64(28, (x)) ^ S64(34, (x)) ^ S64(39, (x)))
#define Sigma1_512(x)	(S64(14, (x)) ^ S64(18, (x)) ^ S64(41, (x)))
#define sigma0_512(x)	(S64( 1, (x)) ^ S64( 8, (x)) ^ R( 7,   (x)))
#define sigma1_512(x)	(S64(19, (x)) ^ S64(61, (x)) ^ R( 6,   (x)))

/*** SHA-XYZ INITIAL HASH VALUES AND CONSTANTS ************************/
/* Hash constant words K for SHA-384 and SHA-512: */
static const uint64_t K512[80] = {
	0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
	0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
	0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
	0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
	0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
	0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
	0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL,
	0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
	0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
	0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
	0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL,
	0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
	0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL,
	0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
	0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
	0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
	0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL,
	0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
	0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL,
	0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
	0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
	0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
	0xd192e819d6ef5218ULL, 0xd69906245565a910ULL,
	0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
	0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
	0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
	0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
	0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
	0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL,
	0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
	0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL,
	0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
	0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
	0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
	0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
	0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
	0x28db77f523047d84ULL, 0x32caab7b40c72493ULL,
	0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
	0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
	0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

/* Initial hash value H for SHA-512 */
static const uint64_t sha512_initial_hash_value[8] = {
	0x6a09e667f3bcc908ULL,
	0xbb67ae8584caa73bULL,
	0x3c6ef372fe94f82bULL,
	0xa54ff53a5f1d36f1ULL,
	0x510e527fade682d1ULL,
	0x9b05688c2b3e6c1fULL,
	0x1f83d9abfb41bd6bULL,
	0x5be0cd19137e2179ULL
};

/*** SHA-512: *********************************************************/
void
SHA512Init(SHA2_CTX *context)
{
	memcpy(context->state.st64, sha512_initial_hash_value,
	    sizeof(sha512_initial_hash_value));
	memset(context->buffer, 0, sizeof(context->buffer));
	context->bitcount[0] = context->bitcount[1] =  0;
}

#ifdef SHA2_UNROLL_TRANSFORM

/* Unrolled SHA-512 round macros: */

#define ROUND512_0_TO_15(a,b,c,d,e,f,g,h) do {				    \
	BE_8_TO_64(W512[j], data);					    \
	data += 8;							    \
	T1 = (h) + Sigma1_512((e)) + Ch((e), (f), (g)) + K512[j] + W512[j]; \
	(d) += T1;							    \
	(h) = T1 + Sigma0_512((a)) + Maj((a), (b), (c));		    \
	j++;								    \
} while(0)


#define ROUND512(a,b,c,d,e,f,g,h) do {					    \
	s0 = W512[(j+1)&0x0f];						    \
	s0 = sigma0_512(s0);						    \
	s1 = W512[(j+14)&0x0f];						    \
	s1 = sigma1_512(s1);						    \
	T1 = (h) + Sigma1_512((e)) + Ch((e), (f), (g)) + K512[j] +	    \
             (W512[j&0x0f] += s1 + W512[(j+9)&0x0f] + s0);		    \
	(d) += T1;							    \
	(h) = T1 + Sigma0_512((a)) + Maj((a), (b), (c));		    \
	j++;								    \
} while(0)

void
SHA512Transform(uint64_t state[8], const uint8_t data[SHA512_BLOCK_LENGTH])
{
	uint64_t	a, b, c, d, e, f, g, h, s0, s1;
	uint64_t	T1, W512[16];
	int		j;

	/* Initialize registers with the prev. intermediate value */
	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];
	f = state[5];
	g = state[6];
	h = state[7];

	j = 0;
	do {
		/* Rounds 0 to 15 (unrolled): */
		ROUND512_0_TO_15(a,b,c,d,e,f,g,h);
		ROUND512_0_TO_15(h,a,b,c,d,e,f,g);
		ROUND512_0_TO_15(g,h,a,b,c,d,e,f);
		ROUND512_0_TO_15(f,g,h,a,b,c,d,e);
		ROUND512_0_TO_15(e,f,g,h,a,b,c,d);
		ROUND512_0_TO_15(d,e,f,g,h,a,b,c);
		ROUND512_0_TO_15(c,d,e,f,g,h,a,b);
		ROUND512_0_TO_15(b,c,d,e,f,g,h,a);
	} while (j < 16);

	/* Now for the remaining rounds up to 79: */
	do {
		ROUND512(a,b,c,d,e,f,g,h);
		ROUND512(h,a,b,c,d,e,f,g);
		ROUND512(g,h,a,b,c,d,e,f);
		ROUND512(f,g,h,a,b,c,d,e);
		ROUND512(e,f,g,h,a,b,c,d);
		ROUND512(d,e,f,g,h,a,b,c);
		ROUND512(c,d,e,f,g,h,a,b);
		ROUND512(b,c,d,e,f,g,h,a);
	} while (j < 80);

	/* Compute the current intermediate hash value */
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
	state[5] += f;
	state[6] += g;
	state[7] += h;

	/* Clean up */
	a = b = c = d = e = f = g = h = T1 = 0;
}

#else /* SHA2_UNROLL_TRANSFORM */

void
SHA512Transform(uint64_t state[8], const uint8_t data[SHA512_BLOCK_LENGTH])
{
	uint64_t	a, b, c, d, e, f, g, h, s0, s1;
	uint64_t	T1, T2, W512[16];
	int		j;

	/* Initialize registers with the prev. intermediate value */
	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];
	f = state[5];
	g = state[6];
	h = state[7];

	j = 0;
	do {
		BE_8_TO_64(W512[j], data);
		data += 8;
		/* Apply the SHA-512 compression function to update a..h */
		T1 = h + Sigma1_512(e) + Ch(e, f, g) + K512[j] + W512[j];
		T2 = Sigma0_512(a) + Maj(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + T1;
		d = c;
		c = b;
		b = a;
		a = T1 + T2;

		j++;
	} while (j < 16);

	do {
		/* Part of the message block expansion: */
		s0 = W512[(j+1)&0x0f];
		s0 = sigma0_512(s0);
		s1 = W512[(j+14)&0x0f];
		s1 =  sigma1_512(s1);

		/* Apply the SHA-512 compression function to update a..h */
		T1 = h + Sigma1_512(e) + Ch(e, f, g) + K512[j] +
		     (W512[j&0x0f] += s1 + W512[(j+9)&0x0f] + s0);
		T2 = Sigma0_512(a) + Maj(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + T1;
		d = c;
		c = b;
		b = a;
		a = T1 + T2;

		j++;
	} while (j < 80);

	/* Compute the current intermediate hash value */
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
	state[5] += f;
	state[6] += g;
	state[7] += h;

	/* Clean up */
	a = b = c = d = e = f = g = h = T1 = T2 = 0;
}

#endif /* SHA2_UNROLL_TRANSFORM */

void
SHA512Update(SHA2_CTX *context, const uint8_t *data, size_t len)
{
	size_t	freespace, usedspace;

	/* Calling with no data is valid (we do nothing) */
	if (len == 0)
		return;

	usedspace = (context->bitcount[0] >> 3) % SHA512_BLOCK_LENGTH;
	if (usedspace > 0) {
		/* Calculate how much free space is available in the buffer */
		freespace = SHA512_BLOCK_LENGTH - usedspace;

		if (len >= freespace) {
			/* Fill the buffer completely and process it */
			memcpy(&context->buffer[usedspace], data, freespace);
			ADDINC128(context->bitcount, freespace << 3);
			len -= freespace;
			data += freespace;
			SHA512Transform(context->state.st64, context->buffer);
		} else {
			/* The buffer is not yet full */
			memcpy(&context->buffer[usedspace], data, len);
			ADDINC128(context->bitcount, len << 3);
			/* Clean up: */
			usedspace = freespace = 0;
			return;
		}
	}
	while (len >= SHA512_BLOCK_LENGTH) {
		/* Process as many complete blocks as we can */
		SHA512Transform(context->state.st64, data);
		ADDINC128(context->bitcount, SHA512_BLOCK_LENGTH << 3);
		len -= SHA512_BLOCK_LENGTH;
		data += SHA512_BLOCK_LENGTH;
	}
	if (len > 0) {
		/* There's left-overs, so save 'em */
		memcpy(context->buffer, data, len);
		ADDINC128(context->bitcount, len << 3);
	}
	/* Clean up: */
	usedspace = freespace = 0;
}

void
SHA512Pad(SHA2_CTX *context)
{
	unsigned int	usedspace;

	usedspace = (context->bitcount[0] >> 3) % SHA512_BLOCK_LENGTH;
	if (usedspace > 0) {
		/* Begin padding with a 1 bit: */
		context->buffer[usedspace++] = 0x80;

		if (usedspace <= SHA512_SHORT_BLOCK_LENGTH) {
			/* Set-up for the last transform: */
			memset(&context->buffer[usedspace], 0, SHA512_SHORT_BLOCK_LENGTH - usedspace);
		} else {
			if (usedspace < SHA512_BLOCK_LENGTH) {
				memset(&context->buffer[usedspace], 0, SHA512_BLOCK_LENGTH - usedspace);
			}
			/* Do second-to-last transform: */
			SHA512Transform(context->state.st64, context->buffer);

			/* And set-up for the last transform: */
			memset(context->buffer, 0, SHA512_BLOCK_LENGTH - 2);
		}
	} else {
		/* Prepare for final transform: */
		memset(context->buffer, 0, SHA512_SHORT_BLOCK_LENGTH);

		/* Begin padding with a 1 bit: */
		*context->buffer = 0x80;
	}
	/* Store the length of input data (in bits) in big endian format: */
	BE_64_TO_8(&context->buffer[SHA512_SHORT_BLOCK_LENGTH],
	    context->bitcount[1]);
	BE_64_TO_8(&context->buffer[SHA512_SHORT_BLOCK_LENGTH + 8],
	    context->bitcount[0]);

	/* Final transform: */
	SHA512Transform(context->state.st64, context->buffer);

	/* Clean up: */
	usedspace = 0;
}

void
SHA512Final(uint8_t digest[SHA512_DIGEST_LENGTH], SHA2_CTX *context)
{
	SHA512Pad(context);

#if BYTE_ORDER == LITTLE_ENDIAN
	int	i;

	/* Convert TO host byte order */
	for (i = 0; i < 8; i++)
		BE_64_TO_8(digest + i * 8, context->state.st64[i]);
#else
	memcpy(digest, context->state.st64, SHA512_DIGEST_LENGTH);
#endif
	explicit_bzero(context, sizeof(*context));
}
