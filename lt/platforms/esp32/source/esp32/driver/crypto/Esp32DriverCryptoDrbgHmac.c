/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/Esp32DriverCryptoDrbgHmac.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

//NIST SP 800-90A Rev.1, https://csrc.nist.gov/publications/detail/sp/800-90a/rev-1/final#pubs-documentation

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include "Esp32DriverCrypto.h"

// 10.1.2.2
/**
 * @brief Update drbg internal state
 * @param ctx       the pointer to the context
 * @param input     the pointer to the input data
 * @param inputLen  the length of input, in bytes
 */
static void ESP32_DRBG_HMAC_Update(ESP32_DRBG_HMAC_CTX *ctx, const u8 *input, LT_SIZE inputLen) {
    // step 1
    u8 mid = 0;
    ESP32_HMAC_SHA256_Init(&ctx->hmacCtx, (u8 *)ctx->K, HASH_OUTPUT_LENGTH);
    ESP32_HMAC_SHA256_Update(&ctx->hmacCtx, (u8 *)ctx->V, HASH_OUTPUT_LENGTH);
    ESP32_HMAC_SHA256_Update(&ctx->hmacCtx, &mid, sizeof(mid));
    if (input && inputLen) {
        ESP32_HMAC_SHA256_Update(&ctx->hmacCtx, input, inputLen);
    }
    ESP32_HMAC_SHA256_Finish(&ctx->hmacCtx, (u8 *)ctx->K);

    // step 2
    ESP32_HMAC_SHA256_Digest((u8 *)ctx->K, HASH_OUTPUT_LENGTH, (u8 *)ctx->V, HASH_OUTPUT_LENGTH, (u8 *)ctx->V);

    if (input && inputLen) {
        // step 4
        mid = 1;
        ESP32_HMAC_SHA256_Init(&ctx->hmacCtx, (u8 *)ctx->K, HASH_OUTPUT_LENGTH);
        ESP32_HMAC_SHA256_Update(&ctx->hmacCtx, (u8 *)ctx->V, HASH_OUTPUT_LENGTH);
        ESP32_HMAC_SHA256_Update(&ctx->hmacCtx, &mid, sizeof(mid));
        ESP32_HMAC_SHA256_Update(&ctx->hmacCtx, input, inputLen);
        ESP32_HMAC_SHA256_Finish(&ctx->hmacCtx, (u8 *)ctx->K);

        // step 5
        ESP32_HMAC_SHA256_Digest((u8 *)ctx->K, HASH_OUTPUT_LENGTH, (u8 *)ctx->V, HASH_OUTPUT_LENGTH, (u8 *)ctx->V);
    }
}

// 10.1.2.3, 10.1.2.4
/**
 * @brief Seed drbg
 * @param ctx              the pointer to the context
 * @param seedMaterial     the pointer to the seed buffer
 * @param seedMaterialLen  the length of seed, in bytes
 */
static void ESP32_DRBG_HMAC_Seed(ESP32_DRBG_HMAC_CTX *ctx, const u8 *seedMaterial, LT_SIZE seedMaterialLen) {
    ESP32_DRBG_HMAC_Update(ctx, seedMaterial, seedMaterialLen);
    ctx->reseedCounter = 1;
}

// 10.1.2.3
/**
 * @brief  Initialize drbg context
 * @param  ctx         the pointer to the context
 * @param  entropy     the pointer to the entropy buffer
 * @param  entropyLen  the length of entropy, in bytes
 * @param  nonce       the pointer to the nonce buffer
 * @param  nonceLen    the length of nonce, in bytes
 * @param  person      the pointer to the personal string buffer
 * @param  personLen   the length of personal string, in bytes
 * @return result code
 */
LTSystemCryptoResult ESP32_DRBG_HMAC_Init(ESP32_DRBG_HMAC_CTX *ctx, const u8 *entropy, LT_SIZE entropyLen, const u8 *nonce, LT_SIZE nonceLen, const u8 *person, LT_SIZE personLen) {
    // sanity check
    if (!ctx || !entropy || !entropyLen || !nonce || !nonceLen) {
        return kLTSystemCrypto_Result_Null;
    }
    if (entropyLen < SECURITY_STRENGTH) {
        return kLTSystemCrypto_Result_WrongLength;
    }
    if (nonceLen > MAX_INPUT_STRING_LENGTH || personLen > MAX_INPUT_STRING_LENGTH) {
        return kLTSystemCrypto_Result_WrongLength;
    }

    // step 1
    LT_SIZE seedMaterialLen = entropyLen + nonceLen + personLen;
    u8 * seedMaterial = lt_malloc(seedMaterialLen);
    if (!seedMaterial) return kLTSystemCrypto_Result_OOM;

    u8 * p = seedMaterial;
    lt_memcpy(p, entropy, entropyLen);
    p += entropyLen;
    lt_memcpy(p, nonce, nonceLen);
    p += nonceLen;
    lt_memcpy(p, person, personLen);

    // step 2
    lt_memset(ctx->K, 0, sizeof(ctx->K));
    
    // step 3
    lt_memset(ctx->V, 1, sizeof(ctx->V));

    ESP32_DRBG_HMAC_Seed(ctx, seedMaterial, seedMaterialLen);

    lt_memset(seedMaterial, 0, seedMaterialLen);
    lt_free(seedMaterial);
    return kLTSystemCrypto_Result_Ok;
}

// 10.1.2.4
/**
 * @brief Reseed drbg
 * @param ctx            the pointer to the context
 * @param entropy        the pointer to the entropy buffer
 * @param entropyLen     the length of entropy, in bytes
 * @param additional     the pointer to the additional string
 * @param additionalLen  the length of additional string, in bytes
 * @return result code
 */
LTSystemCryptoResult ESP32_DRBG_HMAC_Reseed(ESP32_DRBG_HMAC_CTX *ctx, const u8 *entropy, LT_SIZE entropyLen, const u8 *additional, LT_SIZE additionalLen) {
    // sanity check
    if (!ctx || !entropy || !entropyLen) {
        return kLTSystemCrypto_Result_Null;
    }
    if (entropyLen < SECURITY_STRENGTH) {
        return kLTSystemCrypto_Result_WrongLength;
    }
    if (additionalLen > MAX_INPUT_STRING_LENGTH) {
        return kLTSystemCrypto_Result_WrongLength;
    }

    // step 1
    LT_SIZE seedMaterialLen = entropyLen + additionalLen;
    u8 * seedMaterial = lt_malloc(seedMaterialLen);
    if (!seedMaterial) return kLTSystemCrypto_Result_OOM;

    u8 * p = seedMaterial;
    lt_memcpy(p, entropy, entropyLen);
    p += entropyLen;
    lt_memcpy(p, additional, additionalLen);

    ESP32_DRBG_HMAC_Seed(ctx, seedMaterial, seedMaterialLen);

    lt_memset(seedMaterial, 0, seedMaterialLen);
    lt_free(seedMaterial);
    return kLTSystemCrypto_Result_Ok;
}

// 10.1.2.5
/**
 * @brief Generate random bytes
 * @param ctx            the pointer to the context
 * @param additional     the pointer to the additional string buffer
 * @param additionalLen  the length of additional string, in bytes
 * @param output         the pointer to the output buffer
 * @param outputLen      the length of output, in bytes
 * @return result code
 */
LTSystemCryptoResult ESP32_DRBG_HMAC_GenRandomBytes(ESP32_DRBG_HMAC_CTX *ctx, const u8 *additional, LT_SIZE additionalLen, u8 *output, LT_SIZE outputLen) {
    // sanity check
    if (!ctx || !output || !outputLen) {
        return kLTSystemCrypto_Result_Null;
    }
    if (outputLen > MAX_BYTE_COUNT_PER_REQUEST || additionalLen > MAX_INPUT_STRING_LENGTH) {
        return kLTSystemCrypto_Result_WrongLength;
    }

    // step 1
    if (ctx->reseedCounter > RESEED_INTERVAL) {
        return kLTSystemCrypto_Result_NeedToReseed;
    }

    // step 2
    if (additional && additionalLen) {
        ESP32_DRBG_HMAC_Update(ctx, additional, additionalLen);
    }

    // step 4
    while (outputLen > 0) {
        ESP32_HMAC_SHA256_Digest((u8 *)ctx->K, HASH_OUTPUT_LENGTH, (u8 *)ctx->V, HASH_OUTPUT_LENGTH, (u8 *)ctx->V);
        // step 4.2
        if (HASH_OUTPUT_LENGTH <= outputLen ) {
            lt_memcpy(output, ctx->V, HASH_OUTPUT_LENGTH);
        } else {
            lt_memcpy(output, ctx->V, outputLen);
            break;
        }
        output += HASH_OUTPUT_LENGTH;
        outputLen -= HASH_OUTPUT_LENGTH;
    }

    // step 6
    ESP32_DRBG_HMAC_Update(ctx, additional, additionalLen);

    // step 7
    ++ctx->reseedCounter;

    return kLTSystemCrypto_Result_Ok;
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  09-May-22   gallienus   created
 */
