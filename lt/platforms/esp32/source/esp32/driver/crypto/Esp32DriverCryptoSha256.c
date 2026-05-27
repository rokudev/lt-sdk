/******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/Esp32DriverCryptoSha256.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *****************************************************************************/

#include <lt/LTTypes.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>

#include "Esp32DriverCrypto.h"
#include "Esp32DriverCryptoBigNum.h"

// 2.2.2
#define ROT(x, y) ((((u32)(x)) >> (y)) | ((x) << (32 - (y))))
// 4.1.2
// standard
//#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
//#define MJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
// improved
#define CH(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define MJ(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define SP0(x) (ROT(x, 2) ^ ROT(x, 13) ^ ROT(x, 22))
#define SP1(x) (ROT(x, 6) ^ ROT(x, 11) ^ ROT(x, 25))
#define SP2(x) (ROT(x, 7) ^ ROT(x, 18) ^ ((x) >> 3))
#define SP3(x) (ROT(x, 17) ^ ROT(x, 19) ^ ((x) >> 10))

/**
 * @brief Wait shortTimeout for the SHA engine to compele an operation.
 *
 * @return true, operation completes succfully.
 * @return false, operation is timeout.
 */
static bool WaitIdle(void) {
    bool bTimeout = false;
    LTTime t = LT_GetCore()->GetKernelTime();
    while (ESP32_REG(SHA_256_BUSY) != 0 && !bTimeout) {
        bTimeout = LTTime_IsGreaterThan(LTTime_Subtract(LT_GetCore()->GetKernelTime(), t), ESP32_TIMEOUT_SHORT);
    }
    return (ESP32_REG(SHA_256_BUSY) == 0);
}

/**
 * @brief Fill a block of data to the SHA engine
 *
 * @param data  64 Bytes of data
 */
static void FillBlock(const u8 data[SHA256_BLOCK_LENGTH], u32 d[SHA256_BLOCK_LENGTH / 4]) {
    u32 *in; // data has to be aligned with u32
    if (((LT_SIZE)data & 0x3) != 0) {
        // data not aligned with u32
        lt_memcpy(d, data, SHA256_BLOCK_LENGTH);
        in = d;
    } else {
        in = (u32 *)data;
    }
    volatile u32 *reg = ESP32_REG_ADDR(SHA_TEXT);
    for (LT_SIZE i = 0; i < SHA256_BLOCK_LENGTH / 4; ++i) {
        reg[i] = HAL_SWAP32(in[i]);
    }
}

/**
 * @brief Hash one block of data
 *
 * @param ctx    the SHA context
 * @param block  the block of data, 64 Bytes
 * @return   result code
 */
static LTSystemCryptoResult HashBlock(LT_SHA256_CTX *ctx, const u8 block[SHA256_BLOCK_LENGTH]) {
    FillBlock(block, ctx->d);

    if (ctx->bFirst) {
        // start hashing
        ESP32_REG(SHA_256_START) = 1;
        ctx->bFirst = false;
    } else {
        // continue hashing
        ESP32_REG(SHA_256_CONTINUE) = 1;
    }
    if (!WaitIdle()) {
        return kLTSystemCrypto_Result_Error;
    }

    return  kLTSystemCrypto_Result_Ok;
}

/**
 * @brief Read digest
 *
 * @param digest  32 Bytes
 * @return   result code
 */
static LTSystemCryptoResult ReadDigest(u8 digest[SHA256_HASH_LENGTH], u32 h[SHA256_HASH_LENGTH / 4]) {
    ESP32_REG(SHA_256_LOAD) = 1;
    if (!WaitIdle()) {
        return kLTSystemCrypto_Result_Error;
    }

    volatile u32 *reg = ESP32_REG_ADDR(SHA_TEXT);
    int i;
    for (i = 0; i < SHA256_HASH_LENGTH / 4; ++i) {
        h[i] = HAL_SWAP32(reg[i]);
    }

    /* Fault injection check: verify SHA engine actually ran, hash is not all zeroes. */
    for (i = 0; i < SHA256_HASH_LENGTH / 4; ++i) {
        if (h[i] != 0) {
            lt_memcpy(digest, h, SHA256_HASH_LENGTH);
            return kLTSystemCrypto_Result_Ok;
        }
    }

    return kLTSystemCrypto_Result_Error;
}

/**
 * @brief  Initialize SHA256 context
 * @param  ctx  the context
 * @return result code
 */
static LTSystemCryptoResult InitSHA256(LT_SHA256_CTX *ctx) {
    // sanity check
    if (!ctx) return kLTSystemCrypto_Result_Null;
    lt_memset(ctx, 0, sizeof(LT_SHA256_CTX));
    ctx->bFirst = true;
    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Hash input data and update internal state, block by block
 * @param  ctx   the context
 * @param  data  the input data
 * @param  len   the length of data
 * @return result code
 */
static LTSystemCryptoResult UpdateSHA256(LT_SHA256_CTX *ctx, const u8 *data, LT_SIZE len) {
    // sanity check
    if (!ctx || !data) {
        return kLTSystemCrypto_Result_Null;
    }

    u32 nLeft = ctx->dataLen[0] & 0x3F;
    u32 nFill = SHA256_BLOCK_LENGTH - nLeft;

    ctx->DATALen += len;

    // some leftover from previous update, so fill in one block
    if (nFill && len >= nFill) {
        lt_memcpy(ctx->data + nLeft, data, nFill);
        if (kLTSystemCrypto_Result_Ok != HashBlock(ctx, ctx->data)) {
            return kLTSystemCrypto_Result_Error;
        }
        data += nFill;
        len  -= nFill;
        nLeft  = 0;
    }

    // hash the data block by block
    while (len >= SHA256_BLOCK_LENGTH) {
        if (kLTSystemCrypto_Result_Ok != HashBlock(ctx, data)) {
            return kLTSystemCrypto_Result_Error;
        }
        data += SHA256_BLOCK_LENGTH;
        len  -= SHA256_BLOCK_LENGTH;
    }

    // buffer the leftover
    if (len > 0) {
        lt_memcpy(ctx->data + nLeft, data, len);
    }

    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Finish hash and get the digest
 * @param  ctx     the context
 * @param  digest  the output digest
 * @return result code
 */
static LTSystemCryptoResult FinishSHA256(LT_SHA256_CTX *ctx, u8 digest[SHA256_HASH_LENGTH]) {
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
        lt_memset(ctx->data + used, 0, SHA256_BLOCK_LENGTH - used);
        if (kLTSystemCrypto_Result_Ok != HashBlock(ctx, ctx->data)) {
            return kLTSystemCrypto_Result_Error;
        }
        lt_memset(ctx->data, 0, 56);
    }

    // 5.1.1, append bitlen
    u64 bitLen = ctx->DATALen << 3;
    *((u32 *)(ctx->data + 56)) = LT_BE32(bitLen >> 32);
    *((u32 *)(ctx->data + 60)) = LT_BE32((u32)bitLen);

    // hash the padded block
    if (kLTSystemCrypto_Result_Ok != HashBlock(ctx, ctx->data)) {
        return kLTSystemCrypto_Result_Error;
    }

    // last step in 6.2.2, copy the final state (little endian) to the output hash (big endian).
    if (kLTSystemCrypto_Result_Ok != ReadDigest(digest, ctx->h)) {
        return kLTSystemCrypto_Result_Error;
    }
    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Initialize SHA256 context
 * @param  ctx  the context
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA256_Init(LT_SHA256_CTX *ctx) {
    return InitSHA256(ctx);
}

/**
 * @brief  Hash input data and update internal state, block by block
 * @param  ctx   the context
 * @param  data  the input data
 * @param  len   the length of data
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA256_Update(LT_SHA256_CTX *ctx, const u8 *data, LT_SIZE len) {
    return UpdateSHA256(ctx, data, len);
}

/**
 * @brief  Finish hash and get the digest
 * @param  ctx     the context
 * @param  digest  the output digest
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA256_Finish(LT_SHA256_CTX *ctx, u8 digest[SHA256_HASH_LENGTH]) {
    return FinishSHA256(ctx, digest);
}

/**
 * @brief  Hash data and get the digest
 * @param  data    the input data block
 * @param  len     the length of data
 * @param  digest  the output digest, 32 Bytes
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA256_Digest(const u8 *data, LT_SIZE len, u8 digest[SHA256_HASH_LENGTH]) {
    LT_SHA256_CTX *ctx = lt_malloc(sizeof(LT_SHA256_CTX));
    if (!ctx) return kLTSystemCrypto_Result_OOM;

    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;
    if (kLTSystemCrypto_Result_Ok == (ret = ESP32_SHA256_Init(ctx)) &&
        kLTSystemCrypto_Result_Ok == (ret = ESP32_SHA256_Update(ctx, data, len)) &&
        kLTSystemCrypto_Result_Ok == (ret = ESP32_SHA256_Finish(ctx, digest))) {
        ;
    }

    lt_memset(ctx, 0, sizeof(LT_SHA256_CTX));
    lt_free(ctx);
    return ret;
}

typedef_LTObjectImpl(LTDriverCryptoSha256, LTHardwareCryptoSha256) {
} LTOBJECT_API;

static LTSystemCryptoResult LTHardwareCryptoSha256_GenDigest(const u8 *data, LT_SIZE dataLen, u8 *digest) {
    Esp32DriverCrypto_LockShaMutex();
    ESP32_SHA_Enable();
    LTSystemCryptoResult ret = ESP32_SHA256_Digest(data, dataLen, digest);
    ESP32_SHA_Disable();
    Esp32DriverCrypto_UnlockShaMutex();
    return ret;
}

static void LTHardwareCryptoSha256_DestructObject(LTHardwareCryptoSha256 *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoSha256_ConstructObject(LTHardwareCryptoSha256 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoSha256, LTHardwareCryptoSha256,
    GenDigest,
);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  18-May-22   gallienus   created
 */
