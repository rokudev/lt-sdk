/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoSHA1.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

// SHA1, FIPS 180-4, https://csrc.nist.gov/publications/detail/fips/180/4/final

#include <lt/driver/crypto/LTDriverCrypto.h>
#include "LTDriverCrypto.h"
#include "LTDriverCryptoBigNum.h"

// 2.2.2
#define ROT(x, y) ((((u32)(x)) << (y)) | ((x) >> (32 - (y))))
// 4.1.1
// standard
//#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
//#define MJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
// improved
#define CH(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))           /*  0 <= t <= 19 */
#define MJ(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))   /* 40 <= t <= 59 */
#define PY(x, y, z) ((x) ^ (y) ^ (z))                     /* 20 <= t <= 39, 60 <= t <= 79 */

// 4.2.1
static const u32 k[4] = {0x5a827999,    /*  0 <= t <= 19 */
                         0x6ed9eba1,    /* 20 <= t <= 39 */
                         0x8f1bbcdc,    /* 40 <= t <= 59 */
                         0xca62c1d6};   /* 60 <= t <= 79 */

// 5.3.1, 6.1.1
/**
 * @brief  Initialize SHA1 context
 * @param  ctx  the context
 * @return result code
 */
LTSystemCryptoResult LT_SHA1_Init(LT_SHA1_CTX *ctx) {
    // sanity check
    if (!ctx) {
        return kLTSystemCrypto_Result_Null;
    }

    ctx->DATALen = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xc3d2e1f0;

    return kLTSystemCrypto_Result_Ok;
}

// 6.1.3. Use alternative method to save stack.
/**
 * @brief  Hash one data block, 64 Bytes, internal use only
 * @param  ctx   the context
 * @param  data  the input data block
 * @return result code
 */
static void LT_SHA1_Block(LT_SHA1_CTX *ctx, const u8 data[SHA1_BLOCK_LENGTH]) {
    // step 2
    lt_memcpy(ctx->tmp.s, ctx->state, sizeof(ctx->tmp.s));

    u32 i, j, g, r;
    for (i = 0, j = 0, g = 0; i < 80; ++i, ++j) {
        r = i & 0xf;
        if (j >= 20) {
            j = 0;
            ++g;
        }

        // step 1, merged with step 3 because they can share the same loop
        if (i < 16) {
            LT_BN_Copy_B2L((u8 *)(ctx->tmp.m + i), 4, data + (i << 2), 4);
        } else {
            ctx->tmp.m[r] = ROT(ctx->tmp.m[(r + 13) & 0xf] ^ ctx->tmp.m[(r + 8) & 0xf] ^ ctx->tmp.m[(r + 2) & 0xf] ^ ctx->tmp.m[r], 1);
        }

        // step 3
        switch (g) {
            case 0:
                ctx->tmp.t = CH(ctx->tmp.s[1], ctx->tmp.s[2], ctx->tmp.s[3]);
                break;
            case 2:
                ctx->tmp.t = MJ(ctx->tmp.s[1], ctx->tmp.s[2], ctx->tmp.s[3]);
                break;
            default:
                ctx->tmp.t = PY(ctx->tmp.s[1], ctx->tmp.s[2], ctx->tmp.s[3]);
        }
        ctx->tmp.t += ROT(ctx->tmp.s[0], 5) + ctx->tmp.s[4] + k[g] + ctx->tmp.m[r];
        ctx->tmp.s[4] = ctx->tmp.s[3];
        ctx->tmp.s[3] = ctx->tmp.s[2];
        ctx->tmp.s[2] = ROT(ctx->tmp.s[1], 30);
        ctx->tmp.s[1] = ctx->tmp.s[0];
        ctx->tmp.s[0] = ctx->tmp.t;
    }

    // step 4
    for (i = 0; i < SHA1_STATE_LENGTH; ++i) {
        ctx->state[i] += ctx->tmp.s[i];
    }
}

// 6.1.2
/**
 * @brief  Hash input data and update internal state, block by block
 * @param  ctx   the context
 * @param  data  the input data
 * @param  len   the length of data
 * @return result code
 */
LTSystemCryptoResult LT_SHA1_Update(LT_SHA1_CTX *ctx, const u8 *data, LT_SIZE len) {
    // sanity check
    if (!ctx || !data) {
        return kLTSystemCrypto_Result_Null;
    }

    u32 left = ctx->dataLen[0] & 0x3F;
    u32 fill = SHA1_BLOCK_LENGTH - left;

    ctx->DATALen += len;

    // some leftover from previous update, so fill in one block
    if (fill && len >= fill) {
        lt_memcpy(ctx->data + left, data, fill);
        LT_SHA1_Block(ctx, ctx->data);
        data += fill;
        len  -= fill;
        left  = 0;
    }

    // hash the data block by block
    while (len >= SHA1_BLOCK_LENGTH) {
        LT_SHA1_Block(ctx, data);
        data += SHA1_BLOCK_LENGTH;
        len  -= SHA1_BLOCK_LENGTH;
    }

    // buffer the leftover
    if (len > 0) {
        lt_memcpy(ctx->data + left, data, len);
    }

    return kLTSystemCrypto_Result_Ok;
}

// 5.1.1, 6.1.2
/**
 * @brief  Finish hash and get the digest
 * @param  ctx     the context
 * @param  digest  the output digest
 * @return result code
 */
LTSystemCryptoResult LT_SHA1_Finish(LT_SHA1_CTX *ctx, u8 digest[SHA1_HASH_LENGTH]) {
    // sanity check
    if (!ctx || !digest) {
        return kLTSystemCrypto_Result_Null;
    }

    u32 used = ctx->dataLen[0] & 0x3F;

    // 5.1.1, pad ctx data
    ctx->data[used++] = 0x80;
    if (used <= 56) {
        lt_memset(ctx->data + used, 0, 56 - used);
    } else {
        lt_memset(ctx->data + used, 0, SHA1_BLOCK_LENGTH - used);
        LT_SHA1_Block(ctx, ctx->data);
        lt_memset(ctx->data, 0, 56);
    }

    // 5.1.1, append bitlen
    u64 nBitLen = ctx->DATALen << 3;
    *((u32 *)(ctx->data + 56)) = LT_BE32(nBitLen >> 32);
    *((u32 *)(ctx->data + 60)) = LT_BE32((u32)nBitLen);

    // hash the padded block
    LT_SHA1_Block(ctx, ctx->data);

    // last step in 6.1.2, copy the final state (little endian) to the output hash (big endian).
    for (u32 i = 0; i < SHA1_STATE_LENGTH; ++i) {
        LT_BN_Copy_L2B(digest + (i << 2), 4, (u8 *)&ctx->state[i], 4);
    }

    lt_memset(ctx, 0, sizeof(LT_SHA1_CTX));
    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Hash data and get the digest
 * @param  data    the input data block
 * @param  len     the length of data
 * @param  digest  the output digest, 20 Bytes
 * @return result code
 */
LTSystemCryptoResult LT_SHA1_Digest(const u8 *data, LT_SIZE len, u8 digest[SHA1_HASH_LENGTH]) {
    LT_SHA1_CTX *ctx = lt_malloc(sizeof(LT_SHA1_CTX));
    if (!ctx) return kLTSystemCrypto_Result_OOM;

    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;

    if (kLTSystemCrypto_Result_Ok == (ret = LT_SHA1_Init(ctx)) &&
        kLTSystemCrypto_Result_Ok == (ret = LT_SHA1_Update(ctx, data, len)) &&
        kLTSystemCrypto_Result_Ok == (ret = LT_SHA1_Finish(ctx, digest))) {
        ;
    }

    lt_memset(ctx, 0, sizeof(LT_SHA1_CTX));
    lt_free(ctx);
    return ret;
}

typedef_LTObjectImpl(LTDriverCryptoSha1, LTSoftwareCryptoSha1) {
} LTOBJECT_API;

static LTSystemCryptoResult LTSoftwareCryptoSha1_GenDigest(const u8 *data, LT_SIZE dataLen, u8 *digest) {
    return LT_SHA1_Digest(data, dataLen, digest);
}

static void LTSoftwareCryptoSha1_DestructObject(LTSoftwareCryptoSha1 *instance) {
    LT_UNUSED(instance);
}

static bool LTSoftwareCryptoSha1_ConstructObject(LTSoftwareCryptoSha1 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoSha1, LTSoftwareCryptoSha1,
    GenDigest,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  14-Jul-20   gallienus   created */
