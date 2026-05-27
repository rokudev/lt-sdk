/*******************************************************************************
 * LTBootCryptoSHA256.c                                            LT Bootloader
 *                                                                (Arch Ver 1.1)
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTBoot.h"
#include "LTBootCrypto.h"

/* SHA256, FIPS 180-4, https://csrc.nist.gov/publications/detail/fips/180/4/final */

/* 2.2.2 */
#define ROT(x, y) ((((u32)(x)) >> (y)) | ((x) << (32 - (y))))
/* 4.1.2 */
#define CH(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define MJ(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define SP0(x) (ROT(x, 2) ^ ROT(x, 13) ^ ROT(x, 22))
#define SP1(x) (ROT(x, 6) ^ ROT(x, 11) ^ ROT(x, 25))
#define SP2(x) (ROT(x, 7) ^ ROT(x, 18) ^ ((x) >> 3))
#define SP3(x) (ROT(x, 17) ^ ROT(x, 19) ^ ((x) >> 10))

/* 4.2.2 */
static const u32 k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void EndianSwap(u8 * dst, const u8 * src, u32 len) {
    for (src += len - 1; len > 0; len--) {
        *dst++ = *src--;
    }
}

/* 6.2.2 */
static void LT_SHA256_Block(LT_SHA256_CTX * ctx, const u8 data[SHA256_BLOCK_LENGTH]) {
    /* step 2 */
    LTBootPlatform_memcpy(ctx->tmp.s, ctx->state, sizeof(ctx->tmp.s));

    u32 i;
    for (i = 0; i < 64; ++i) {
        /* step 1, merged with step 3 because they can share the same loop */
        if (i < 16) {
            /* Convert from big to little-endian */
            EndianSwap((u8 *)(ctx->tmp.m + i), data + (i << 2), 4);
        } else {
            ctx->tmp.m[i] = SP3(ctx->tmp.m[i - 2]) + ctx->tmp.m[i - 7] + SP2(ctx->tmp.m[i - 15]) + ctx->tmp.m[i - 16];
        }

        /* step 3 */
        ctx->tmp.t     = ctx->tmp.s[7] + SP1(ctx->tmp.s[4]) + CH(ctx->tmp.s[4], ctx->tmp.s[5], ctx->tmp.s[6]) + k[i] + ctx->tmp.m[i];
        ctx->tmp.s[3] += ctx->tmp.t;
        ctx->tmp.t    += SP0(ctx->tmp.s[0]) + MJ(ctx->tmp.s[0], ctx->tmp.s[1], ctx->tmp.s[2]);
        ctx->tmp.s[7]  = ctx->tmp.s[6];
        ctx->tmp.s[6]  = ctx->tmp.s[5];
        ctx->tmp.s[5]  = ctx->tmp.s[4];
        ctx->tmp.s[4]  = ctx->tmp.s[3];
        ctx->tmp.s[3]  = ctx->tmp.s[2];
        ctx->tmp.s[2]  = ctx->tmp.s[1];
        ctx->tmp.s[1]  = ctx->tmp.s[0];
        ctx->tmp.s[0]  = ctx->tmp.t;
    }

    /* step 4 */
    for (i = 0; i < SHA256_STATE_LENGTH; ++i) {
        ctx->state[i] += ctx->tmp.s[i];
    }
}

/* 5.3.3, 6.2.1 */
void LTBootCrypto_SHA256_Init(LT_SHA256_CTX * ctx) {
    ctx->DATALen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

/* 6.2.2 */
void LTBootCrypto_SHA256_Update(LT_SHA256_CTX * ctx, const u8 * data, u32 len) {
    u32 left = ctx->dataLen[0] & 0x3F;
    u32 fill = SHA256_BLOCK_LENGTH - left;

    ctx->DATALen += len;

    /* some leftover from previous update, so fill in one block */
    if (fill && len >= fill) {
        LTBootPlatform_memcpy(ctx->data + left, data, fill);
        LT_SHA256_Block(ctx, ctx->data);
        data += fill;
        len  -= fill;
        left  = 0;
    }

    /* hash the data block by block */
    while (len >= SHA256_BLOCK_LENGTH) {
        LT_SHA256_Block(ctx, data);
        data += SHA256_BLOCK_LENGTH;
        len  -= SHA256_BLOCK_LENGTH;
    }

    /* buffer the leftover */
    if (len > 0) {
        LTBootPlatform_memcpy(ctx->data + left, data, len);
    }
}

/* 5.1.1, 6.2.2 */
void LTBootCrypto_SHA256_Finish(LT_SHA256_CTX * ctx, u8 digest[SHA256_HASH_LENGTH]) {
    u32 used = ctx->dataLen[0] & 0x3F;

    /* 5.1.1, pad ctx data */
    ctx->data[used++] = 0x80;
    if (used <= 56) {
        LTBootPlatform_memset(ctx->data + used, 0, 56 - used);
    } else {
        LTBootPlatform_memset(ctx->data + used, 0, SHA256_BLOCK_LENGTH - used);
        LT_SHA256_Block(ctx, ctx->data);
        LTBootPlatform_memset(ctx->data, 0, 56);
    }

    /* 5.1.1, append bitlen */
    u64 nBitLen = ctx->DATALen << 3;
    *((u32 *)(ctx->data + 56)) = LT_BE32(nBitLen >> 32);
    *((u32 *)(ctx->data + 60)) = LT_BE32((u32)nBitLen);

    /* hash the padded block */
    LT_SHA256_Block(ctx, ctx->data);

    /* last step in 6.2.2, copy the final state (little endian) to the output hash (big endian). */
    for (u32 i = 0; i < SHA256_STATE_LENGTH; ++i) {
        EndianSwap(digest + (i << 2), (u8 *)&ctx->state[i], 4);
    }

    LTBootPlatform_memset(ctx, 0, sizeof(LT_SHA256_CTX));
}

