/*******************************************************************************
 * LTBootCryptoSHA512.c                                            LT Bootloader
 *                                                                (Arch Ver 1.1)
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTBoot.h"
#include "LTBootCrypto.h"

/* SHA512, FIPS 180-4, https://csrc.nist.gov/publications/detail/fips/180/4/final */

/* 2.2.2 */
#define ROT(x, y) ((((u64)(x)) >> (y)) | ((x) << (64 - (y))))

/* 4.1.3 */
#define CH(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define MJ(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define SP0(x) (ROT(x, 28) ^ ROT(x, 34) ^ ROT(x, 39))
#define SP1(x) (ROT(x, 14) ^ ROT(x, 18) ^ ROT(x, 41))
#define SP2(x) (ROT(x, 1) ^ ROT(x, 8) ^ ((x) >> 7))
#define SP3(x) (ROT(x, 19) ^ ROT(x, 61) ^ ((x) >> 6))

/* 4.2.3 */
static const u64 k[80] = {
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc,
    0x3956c25bf348b538, 0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118,
    0xd807aa98a3030242, 0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2,
    0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235, 0xc19bf174cf692694,
    0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
    0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5,
    0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4,
    0xc6e00bf33da88fc2, 0xd5a79147930aa725, 0x06ca6351e003826f, 0x142929670a0e6e70,
    0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df,
    0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
    0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30,
    0xd192e819d6ef5218, 0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8,
    0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8,
    0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3,
    0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
    0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b,
    0xca273eceea26619c, 0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178,
    0x06f067aa72176fba, 0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b,
    0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c,
    0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817
};

static void EndianSwap(u8 * dst, const u8 * src, u32 len) {
    for (src += len - 1; len > 0; len--) {
        *dst++ = *src--;
    }
}

/* 6.4.2 Hash one data block */
static void LT_SHA512_Block(LT_SHA512_CTX *ctx, const u8 data[SHA512_BLOCK_LENGTH]) {
    /* step 2 */
    LTBootPlatform_memcpy(ctx->tmp.s, ctx->state, sizeof(ctx->tmp.s));

    u32 i;
    for (i = 0; i < 80; ++i) {
        /* step 1, merged with step 3 because they can share the same loop */
        if (i < 16) {
            /* Copy and convert BE data to LE state */
            EndianSwap((u8 *)(ctx->tmp.m + i), data + (i << 3), 8);
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
    for (i = 0; i < SHA512_STATE_LENGTH; ++i) {
        ctx->state[i] += ctx->tmp.s[i];
    }
}

/* 5.3.5, 6.4.1 */
void LTBootCrypto_SHA512_Init(LT_SHA512_CTX *ctx) {
    ctx->dataLen[0] = ctx->dataLen[1] = 0;
    ctx->state[0]   = 0x6a09e667f3bcc908;
    ctx->state[1]   = 0xbb67ae8584caa73b;
    ctx->state[2]   = 0x3c6ef372fe94f82b;
    ctx->state[3]   = 0xa54ff53a5f1d36f1;
    ctx->state[4]   = 0x510e527fade682d1;
    ctx->state[5]   = 0x9b05688c2b3e6c1f;
    ctx->state[6]   = 0x1f83d9abfb41bd6b;
    ctx->state[7]   = 0x5be0cd19137e2179;
}

/* 6.4.2 Hash input data and update internal state, block by block */
void LTBootCrypto_SHA512_Update(LT_SHA512_CTX *ctx, const u8 *data, LT_SIZE len) {
    ctx->tmp.left = ctx->dataLen[0] & 0x7F;
    ctx->tmp.fill = SHA512_BLOCK_LENGTH - ctx->tmp.left;

    ctx->tmp.l = ctx->dataLen[0] + len;
    if (ctx->tmp.l < ctx->dataLen[0]) {
        ++ctx->dataLen[1];
    }
    ctx->dataLen[0] = ctx->tmp.l;

    /* some leftover from previous update, so fill in one block */
    if (ctx->tmp.left && len >= ctx->tmp.fill) {
        LTBootPlatform_memcpy(ctx->data + ctx->tmp.left, data, ctx->tmp.fill);
        LT_SHA512_Block(ctx, ctx->data);
        data += ctx->tmp.fill;
        len  -= ctx->tmp.fill;
        ctx->tmp.left  = 0;
    }

    /* hash the data block by block */
    while (len >= SHA512_BLOCK_LENGTH) {
        LT_SHA512_Block(ctx, data);
        data += SHA512_BLOCK_LENGTH;
        len  -= SHA512_BLOCK_LENGTH;
    }

    /* buffer the leftover */
    if (len > 0) LTBootPlatform_memcpy(ctx->data + ctx->tmp.left, data, len);
}

/* 5.1.2, 6.4.2 */
void LTBootCrypto_SHA512_Finish(LT_SHA512_CTX *ctx, u8 digest[SHA512_HASH_LENGTH]) {
    u32 used = ctx->dataLen[0] & 0x7F;

    /* 5.1.2, pad ctx data */
    ctx->data[used++] = 0x80;
    if (used <= 112) {
        LTBootPlatform_memset(ctx->data + used, 0, 112 - used);
    } else {
        LTBootPlatform_memset(ctx->data + used, 0, SHA512_BLOCK_LENGTH - used);
        LT_SHA512_Block(ctx, ctx->data);
        LTBootPlatform_memset(ctx->data, 0, 112);
    }

    /* 5.1.2, append bitlen */
    u64 bitLen[2];
    bitLen[0] = ctx->dataLen[0] << 3;
    bitLen[1] = (ctx->dataLen[1] << 3) | (ctx->dataLen[0] >> 61);
    EndianSwap(ctx->data + 112, (u8 *)bitLen, 16);

    /* hash the padded block */
    LT_SHA512_Block(ctx, ctx->data);

    /* last step in 6.4.2, copy the final state (little endian) to the output hash (big endian). */
    for (u32 i = 0; i < SHA512_STATE_LENGTH; ++i) {
        EndianSwap(digest + (i << 3), (u8 *)&ctx->state[i], 8);
    }
}

