/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/Esp32DriverCryptoSha512.c
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *******************************************************************************/

// SHA512, FIPS 180-4, https://csrc.nist.gov/publications/detail/fips/180/4/final

#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTCore.h>
#include "Esp32DriverCrypto.h"
#include "Esp32DriverCryptoBigNum.h"

/**
 * @brief Wait shortTimeout for the SHA engine to compele an operation.
 * 
 * @return true, operation completes succfully.
 * @return false, operation is timeout.
 */
static bool WaitIdle(void) {
    bool bTimeout = false;
    LTTime t = LT_GetCore()->GetKernelTime();
    while (ESP32_REG(SHA_512_BUSY) != 0 && !bTimeout) {
        bTimeout = LTTime_IsGreaterThan(LTTime_Subtract(LT_GetCore()->GetKernelTime(), t), ESP32_TIMEOUT_SHORT);
    }
    return (ESP32_REG(SHA_512_BUSY) == 0);
}

/**
 * @brief Fill a block of data to the SHA engine
 * 
 * @param data  128 Bytes of data
 */
static void FillBlock(const u8 data[SHA512_BLOCK_LENGTH], u32 d[SHA512_BLOCK_LENGTH / 4]) {
    u32 *in; // data has to be aligned with u32
    if (((LT_SIZE)data & 0x3) != 0) {
        // data not aligned with u32
        lt_memcpy(d, data, SHA512_BLOCK_LENGTH);
        in = d;
    } else {
        in = (u32 *)data;
    }
    volatile u32 *reg = ESP32_REG_ADDR(SHA_TEXT);
    for (LT_SIZE i = 0; i < SHA512_BLOCK_LENGTH / 4; ++i) {
        reg[i] = HAL_SWAP32(in[i]);
    }
}

/**
 * @brief Hash one block of data
 * 
 * @param ctx   the SHA context
 * @param block  the block of data, 128 Bytes
 * @return  result code  
 */
static LTSystemCryptoResult HashBlock(ESP32_SHA512_CTX *ctx, const u8 block[SHA512_BLOCK_LENGTH]) {
    FillBlock(block, ctx->d);

    if (ctx->bFirst) {
        // start hashing
        ESP32_REG(SHA_512_START) = 1;
        ctx->bFirst = false;
    } else {
        // continue hashing
        ESP32_REG(SHA_512_CONTINUE) = 1;
    }
    if (!WaitIdle()) {
        return kLTSystemCrypto_Result_Error;
    }

    return  kLTSystemCrypto_Result_Ok;
}

/**
 * @brief Read digest
 * 
 * @param digest  64 Bytes
 * @return  result code 
 */
static LTSystemCryptoResult ReadDigest(u8 digest[SHA512_HASH_LENGTH], u32 h[SHA512_HASH_LENGTH / 4]) {
    ESP32_REG(SHA_512_LOAD) = 1;
    if (!WaitIdle()) {
        return kLTSystemCrypto_Result_Error;
    }

    volatile u32 *reg = ESP32_REG_ADDR(SHA_TEXT);
    ;
    int i;
    for (i = 0; i < SHA512_HASH_LENGTH / 4; ++i) {
        h[i] = HAL_SWAP32(reg[i]);
    }

    /* Fault injection check: verify SHA engine actually ran, hash is not all zeroes. */
    for (i = 0; i < SHA512_HASH_LENGTH / 4; ++i) {
        if (h[i] != 0) {
            lt_memcpy(digest, h, SHA512_HASH_LENGTH);
            return kLTSystemCrypto_Result_Ok;
        }
    }

    return kLTSystemCrypto_Result_Error;
}

/**
 * @brief  Initialize SHA512 context
 * @param  ctx  the context
 * @return  result code
 */
LTSystemCryptoResult ESP32_SHA512_Init(ESP32_SHA512_CTX *ctx) {
    // sanity check
    if (!ctx) {
        return kLTSystemCrypto_Result_Null;
    }

    lt_memset(ctx, 0, sizeof(ESP32_SHA512_CTX));
    ctx->bFirst = true;
    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Hash input data and update internal state, block by block
 * @param  ctx   the context
 * @param  data  the input data
 * @param  len   the length of data
 * @return  result code
 */
LTSystemCryptoResult ESP32_SHA512_Update(ESP32_SHA512_CTX *ctx, const u8 *data, LT_SIZE len) {
    // sanity check
    if (!ctx || !data) {
        return kLTSystemCrypto_Result_Null;
    }

    ctx->left = ctx->dataLen[0] & 0x7F;
    ctx->fill = SHA512_BLOCK_LENGTH - ctx->left;

    ctx->l = ctx->dataLen[0] + len;
    if (ctx->l < ctx->dataLen[0]) {
        ++ctx->dataLen[1];
    }
    ctx->dataLen[0] = ctx->l;

    // some leftover from previous update, so fill in one block
    if (ctx->left && len >= ctx->fill) {
        lt_memcpy(ctx->data + ctx->left, data, ctx->fill);
        if (kLTSystemCrypto_Result_Ok != HashBlock(ctx, ctx->data)) {
            return kLTSystemCrypto_Result_Error;
        }
        data += ctx->fill;
        len  -= ctx->fill;
        ctx->left  = 0;
    }

    // hash the data block by block
    while (len >= SHA512_BLOCK_LENGTH) {
        if (kLTSystemCrypto_Result_Ok != HashBlock(ctx, ctx->data)) {
            return kLTSystemCrypto_Result_Error;
        }
        data += SHA512_BLOCK_LENGTH;
        len  -= SHA512_BLOCK_LENGTH;
    }

    // buffer the leftover
    if (len > 0) lt_memcpy(ctx->data + ctx->left, data, len);

    return kLTSystemCrypto_Result_Ok;
}

// 5.1.2, 6.4.2
/**
 * @brief  Finish hash and get the digest
 * @param  ctx     the context
 * @param  digest  the output digest, 64 Bytes
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA512_Finish(ESP32_SHA512_CTX *ctx, u8 digest[SHA512_HASH_LENGTH]) {
    // sanity check
    if (!ctx || !digest) {
        return kLTSystemCrypto_Result_Null;
    }

    u32 used = ctx->dataLen[0] & 0x7F;
 
    // 5.1.2, pad ctx data
    ctx->data[used++] = 0x80;
    if (used <= 112) {
        lt_memset(ctx->data + used, 0, 112 - used);
    } else {
        lt_memset(ctx->data + used, 0, SHA512_BLOCK_LENGTH - used);
        if (kLTSystemCrypto_Result_Ok != HashBlock(ctx, ctx->data)) {
            return kLTSystemCrypto_Result_Error;
        }
        lt_memset(ctx->data, 0, 112);
    }

    // 5.1.2, append bitlen
    u64 bitLen[2];
    bitLen[0] = ctx->dataLen[0] << 3;
    bitLen[1] = (ctx->dataLen[1] << 3) | (ctx->dataLen[0] >> 61);
    ESP32_BN_Copy_L2B(ctx->data + 112, 16, (u8 *)bitLen, 16);

    // hash the padded block
    if (kLTSystemCrypto_Result_Ok != HashBlock(ctx, ctx->data)) {
        return kLTSystemCrypto_Result_Error;
    }

    // last step in 6.4.2, copy the final state (little endian) to the output hash (big endian).
    if (kLTSystemCrypto_Result_Ok != ReadDigest(digest, ctx->h)) {
        return kLTSystemCrypto_Result_Error;
    }

    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Hash data and get the digest
 * @param  data    the input data block
 * @param  len     the length of data
 * @param  digest  the output digest, 64 Bytes
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA512_Digest(const u8 *data, LT_SIZE len, u8 digest[SHA512_HASH_LENGTH]) {
    ESP32_SHA512_CTX *ctx = lt_malloc(sizeof(ESP32_SHA512_CTX));
    if (!ctx) return kLTSystemCrypto_Result_OOM;

    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;
    if (kLTSystemCrypto_Result_Ok == (ret = ESP32_SHA512_Init(ctx)) &&
        kLTSystemCrypto_Result_Ok == (ret = ESP32_SHA512_Update(ctx, data, len)) &&
        kLTSystemCrypto_Result_Ok == (ret = ESP32_SHA512_Finish(ctx, digest))) {
        ;
    }

    lt_memset(ctx, 0, sizeof(ESP32_SHA512_CTX));
    lt_free(ctx);
    return ret;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-May-22   gallienus   created
 */
