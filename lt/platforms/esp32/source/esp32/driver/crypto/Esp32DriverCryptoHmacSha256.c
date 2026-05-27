/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/Esp32DriverCryptoHmacSha256.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *******************************************************************************/

// NIST FIPS 198-1, https://csrc.nist.gov/publications/detail/fips/198/1/final

#include <lt/LTTypes.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include "Esp32DriverCrypto.h"

// Context of HMC_SHA256
struct LT_HMAC_SHA256_CTX_impl {
    u32 pad[SHA256_BLOCK_LENGTH / 4];   // ipad and opad share this pad
    LT_SHA256_CTX hashCtx;
    // temporary buffer for computation
    union {
        u32 hmacKey[SHA256_BLOCK_LENGTH >> 2];
        u8 digest[SHA256_HASH_LENGTH];
    };
};

/**
 * @brief  Initialize HMAC context
 * @param  hmacCtx  the context
 * @param  key      the key
 * @param  keyLen   the length of key
 * @return result code
 */
static LTSystemCryptoResult ESP32_HMAC_SHA256_Init(LT_HMAC_SHA256_CTX *hmacCtx, const u8 *key, LT_SIZE keyLen) {
    // sanity check
    if (!hmacCtx || !key) {
        return kLTSystemCrypto_Result_Null;
    }

    // 2.3, ipad = 0x36, opad = 0x5C. opad not set here
    lt_memset(hmacCtx->pad, 0x36, sizeof(hmacCtx->pad));

    // step 1-3
    lt_memset(hmacCtx->hmacKey, 0, SHA256_BLOCK_LENGTH);
    if (keyLen > SHA256_BLOCK_LENGTH) {
        ESP32_SHA256_Digest(key, keyLen, (u8 *)hmacCtx->hmacKey);
    } else {
        lt_memcpy(hmacCtx->hmacKey, key, keyLen);
    }

    // step 4, key ^ ipad, cast to u32 to xor
    for (LT_SIZE i = 0; i < (SHA256_BLOCK_LENGTH >> 2); ++i) {
        hmacCtx->pad[i] ^= hmacCtx->hmacKey[i];
    }

    // step 5
    ESP32_SHA256_Init(&hmacCtx->hashCtx);
    ESP32_SHA256_Update(&hmacCtx->hashCtx, (u8 *)hmacCtx->pad, SHA256_BLOCK_LENGTH);

    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Hmac input data and update internal state
 * @param  hmacCtx  the context
 * @param  data     the input data
 * @param  len      the length of data
 * @return result code
 */
static LTSystemCryptoResult ESP32_HMAC_SHA256_Update(LT_HMAC_SHA256_CTX *hmacCtx, const u8 *data, LT_SIZE len) {
    // sanity check
    if(!hmacCtx || !data) {
        return kLTSystemCrypto_Result_Null;
    }

    // step 6
    return ESP32_SHA256_Update(&hmacCtx->hashCtx, data, len);
}

/**
 * @brief  Finish hash and get the hmac
 * @param  hmacCtx the context
 * @param  hmac     the output hmac, 32 Bytes
 * @return result code
 */
static LTSystemCryptoResult ESP32_HMAC_SHA256_Finish(LT_HMAC_SHA256_CTX *hmacCtx, u8 hmac[SHA256_HASH_LENGTH]) {
    // sanity check
    if (!hmacCtx || !hmac) {
        return kLTSystemCrypto_Result_Null;
    }

    // step 7, opad ^ key, opad = 0x5C
    for (LT_SIZE i = 0; i < (SHA256_BLOCK_LENGTH >> 2); ++i) {
        hmacCtx->pad[i] ^= (0x36363636 ^ 0x5C5C5C5C);
    }

    // step 8
    ESP32_SHA256_Finish(&hmacCtx->hashCtx, hmacCtx->digest);

    // step 9
    ESP32_SHA256_Init(&hmacCtx->hashCtx);
    ESP32_SHA256_Update(&hmacCtx->hashCtx, (u8 *)hmacCtx->pad, SHA256_BLOCK_LENGTH);
    ESP32_SHA256_Update(&hmacCtx->hashCtx, hmacCtx->digest, SHA256_HASH_LENGTH);
    ESP32_SHA256_Finish(&hmacCtx->hashCtx, hmac);

    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Hmac data and get the hmac
 * @param  key     the key
 * @param  keyLen  the length of key
 * @param  data    the input data block
 * @param  len     the length of data
 * @param  hmac    the output hmac, 32 Bytes
 * @return result code
 */
static LTSystemCryptoResult ESP32_HMAC_SHA256_Digest(const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE len, u8 hmac[SHA256_HASH_LENGTH]) {
    LT_HMAC_SHA256_CTX *ctx = lt_malloc(sizeof(LT_HMAC_SHA256_CTX));
    if (!ctx) return kLTSystemCrypto_Result_OOM;

    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;
    if (kLTSystemCrypto_Result_Ok == (ret = ESP32_HMAC_SHA256_Init(ctx, key, keyLen)) &&
        kLTSystemCrypto_Result_Ok == (ret = ESP32_HMAC_SHA256_Update(ctx, data, len)) &&
        kLTSystemCrypto_Result_Ok == (ret = ESP32_HMAC_SHA256_Finish(ctx, hmac))) {
        ;
    }

    lt_memset(ctx, 0, sizeof(LT_HMAC_SHA256_CTX));
    lt_free(ctx);
    return ret;
}

typedef_LTObjectImpl(LTDriverCryptoHmacSha256, LTHardwareCryptoHmacSha256) {
} LTOBJECT_API;

static LTSystemCryptoResult LTHardwareCryptoHmacSha256_GenHmac(const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE dataLen, u8 *hmac) {
    Esp32DriverCrypto_LockShaMutex();
    ESP32_SHA_Enable();
    LTSystemCryptoResult ret = ESP32_HMAC_SHA256_Digest(key, keyLen, data, dataLen, hmac);
    ESP32_SHA_Disable();
    Esp32DriverCrypto_UnlockShaMutex();
    return ret;
}

static void LTHardwareCryptoHmacSha256_DestructObject(LTHardwareCryptoHmacSha256 *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoHmacSha256_ConstructObject(LTHardwareCryptoHmacSha256 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoHmacSha256, LTHardwareCryptoHmacSha256,
    GenHmac,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  19-May-22   gallienus   created
 */
