/***************************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoDrbg.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

//NIST SP 800-90A Rev.1, https://csrc.nist.gov/publications/detail/sp/800-90a/rev-1/final#pubs-documentation

#include <lt/LT.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include "LTDriverCrypto.h"
#include "LTDriverCryptoBigNum.h"

// 10.3.1
/**
 * @brief Derivative function, internal use only
 * @param drbgCtx    the context
 * @param input      the output
 * @param inputLen   the length of output
 * @param output     the output
 * @param outputLen  the length of output
 */
static void LT_DRBG_HASH_DF(LT_DRBG_HASH_CTX *drbgCtx, const u8 *input, LT_SIZE inputLen, u8 *output, LT_SIZE outputLen) {
    u8  counter      = 1;
    u32 outputBitLen = (outputLen << 3);    /* size in bits */
    u32 bitsToReturn = LT_BE32(outputBitLen);

    while (outputLen > 0) {
        LT_SHA256_Init(&drbgCtx->tmp.hashCtx);
        LT_SHA256_Update(&drbgCtx->tmp.hashCtx, &counter, sizeof(counter));
        LT_SHA256_Update(&drbgCtx->tmp.hashCtx, (u8 *)&bitsToReturn, sizeof(bitsToReturn));
        LT_SHA256_Update(&drbgCtx->tmp.hashCtx, input, inputLen);
        if (HASH_OUTPUT_LENGTH <= outputLen ) {
            LT_SHA256_Finish(&drbgCtx->tmp.hashCtx, output);
        } else {
            LT_SHA256_Finish(&drbgCtx->tmp.hashCtx, drbgCtx->tmp.digest);
            lt_memcpy(output, drbgCtx->tmp.digest, outputLen);
            break;
        }
        outputLen -= HASH_OUTPUT_LENGTH;
        output    += HASH_OUTPUT_LENGTH;
        ++counter;
    }
    lt_memset(&drbgCtx->tmp.hashCtx, 0, sizeof(drbgCtx->tmp.hashCtx));
}

/**
 * @brief Seed DRBG, internal use only
 * @param drbgCtx          the context
 * @param seedMaterial     the output
 * @param seedMaterialLen  the length of output
 */
static void LT_DRBG_HASH_Seed(LT_DRBG_HASH_CTX *drbgCtx, const u8 *seedMaterial, LT_SIZE seedMaterialLen) {
    LT_DRBG_HASH_DF(drbgCtx, seedMaterial, seedMaterialLen, drbgCtx->V, SEED_LENGTH_BYTE);

    drbgCtx->tmp.buffer[0] = 0;
    lt_memcpy(drbgCtx->tmp.buffer + 1, drbgCtx->V, SEED_LENGTH_BYTE);

    LT_DRBG_HASH_DF(drbgCtx, drbgCtx->tmp.buffer, (1 + SEED_LENGTH_BYTE), drbgCtx->C, SEED_LENGTH_BYTE);

    drbgCtx->reseedCounter = 1;
    lt_memset(drbgCtx->tmp.buffer, 1, sizeof(drbgCtx->tmp.buffer));
}

/**
 * @brief Generate hash bytes, internal use only
 * @param drbgCtx    the context
 * @param output     the output
 * @param outputLen  the length of output
 */
static void LT_DRBG_HASH_Gen(LT_DRBG_HASH_CTX *drbgCtx, u8 *output, LT_SIZE outputLen) {
    // step 2
    lt_memcpy(drbgCtx->tmp.data, drbgCtx->V, SEED_LENGTH_BYTE);
    lt_memset(drbgCtx->tmp.d32, 0, sizeof(drbgCtx->tmp.d32));
    LT_BN_Copy_B2L((u8 *)drbgCtx->tmp.d32, sizeof(drbgCtx->tmp.d32), drbgCtx->tmp.data, SEED_LENGTH_BYTE);

    // step 4
    u8 digest[HASH_OUTPUT_LENGTH];
    while (outputLen > 0) {
        // step 4.1, w = Hash(data)
        LT_SHA256_Init(&drbgCtx->tmp.hashCtx);
        LT_SHA256_Update(&drbgCtx->tmp.hashCtx, drbgCtx->tmp.data, SEED_LENGTH_BYTE);
        if (HASH_OUTPUT_LENGTH <= outputLen) {
            LT_SHA256_Finish(&drbgCtx->tmp.hashCtx, output);
            // step 4.3, data = data+1
            LT_BN_Increment_Unsigned(drbgCtx->tmp.d32, SEED_LENGTH_U32);
            LT_BN_Copy_L2B(drbgCtx->tmp.data, SEED_LENGTH_BYTE, (u8 *)drbgCtx->tmp.d32, SEED_LENGTH_BYTE);
        } else {
            LT_SHA256_Finish(&drbgCtx->tmp.hashCtx, digest);
            lt_memcpy(output, digest, outputLen);
            break;
        }
        output    += HASH_OUTPUT_LENGTH;
        outputLen -= HASH_OUTPUT_LENGTH;
    }
}

// 10.1.1.2, B.1.1
/**
 * @brief  Initialize DRBG context
 * @param  drbgCtx     the context
 * @param  entropy     the entropy
 * @param  entropyLen  the length of entropy
 * @param  nonce       the nonce
 * @param  nonceLen    the length of nonce
 * @param  person      the personal string
 * @param  personLen   the length of personal string
 * @return result code
 */
LTSystemCryptoResult LT_DRBG_HASH_Init(LT_DRBG_HASH_CTX *drbgCtx, const u8 *entropy, LT_SIZE entropyLen, const u8 *nonce, LT_SIZE nonceLen, const u8 *person, LT_SIZE personLen) {
    // sanity check
    if (!drbgCtx || !entropy || !entropyLen) {
        return kLTSystemCrypto_Result_Null;
    }
    if (entropyLen < SECURITY_STRENGTH) {
        return kLTSystemCrypto_Result_WrongLength;
    }
    if (nonceLen > MAX_INPUT_STRING_LENGTH || nonceLen > MAX_INPUT_STRING_LENGTH) {
        return kLTSystemCrypto_Result_WrongLength;
    }

    // prepare seed material
    u32 seedMaterialLen = entropyLen + nonceLen + personLen;
    u8 * seedMaterial = lt_malloc(seedMaterialLen);
    if (!seedMaterial) return kLTSystemCrypto_Result_OOM;

    u8 * p = seedMaterial;
    lt_memcpy(p, entropy, entropyLen);
    p += entropyLen;
    lt_memcpy(p, nonce, nonceLen);
    p += nonceLen;
    lt_memcpy(p, person, personLen);
    LT_DRBG_HASH_Seed(drbgCtx, seedMaterial, seedMaterialLen);

    lt_memset(seedMaterial, 0, seedMaterialLen);
    lt_free(seedMaterial);
    return kLTSystemCrypto_Result_Ok;
}

// 10.1.1.3
/**
 * @brief  Reseed DRBG
 * @param  drbgCtx        the context
 * @param  entropy        the entropy
 * @param  entropyLen     the length of entropy
 * @param  additional     the additional string
 * @param  additionalLen  the length of additional string
 * @return result code
 */
LTSystemCryptoResult LT_DRBG_HASH_Reseed(LT_DRBG_HASH_CTX *drbgCtx, const u8 *entropy, LT_SIZE entropyLen, const u8 *additional, LT_SIZE additionalLen) {
    // sanity check
    if (!drbgCtx || !entropy || !entropyLen) {
        return kLTSystemCrypto_Result_Null;
    }
    if (entropyLen < SECURITY_STRENGTH) {
        return kLTSystemCrypto_Result_WrongLength;
    }
    if (additionalLen > MAX_INPUT_STRING_LENGTH) {
        return kLTSystemCrypto_Result_WrongLength;
    }

    // prepare seed material
    u32 seedMaterialLen = 1 + SEED_LENGTH_BYTE + entropyLen + additionalLen;
    u8 * seedMaterial = lt_malloc(seedMaterialLen);
    if (!seedMaterial) return kLTSystemCrypto_Result_OOM;

    seedMaterial[0] = 1;
    u8 * p = seedMaterial + 1;
    lt_memcpy(p, drbgCtx->V, SEED_LENGTH_BYTE);
    p += SEED_LENGTH_BYTE;
    lt_memcpy(p, entropy, entropyLen);
    p += entropyLen;
    lt_memcpy(p, additional, additionalLen);
    LT_DRBG_HASH_Seed(drbgCtx, seedMaterial, seedMaterialLen);

    lt_memset(seedMaterial, 0, seedMaterialLen);
    lt_free(seedMaterial);
    return kLTSystemCrypto_Result_Ok;
}

// 10.1.1.4
/**
 * @brief  Generate random bytes
 * @param  drbgCtx        the context
 * @param  additional     the additional string
 * @param  additionalLen  the length of additional string
 * @param  output         the output
 * @param  outputLen      the length of output
 * @return result code
 */
LTSystemCryptoResult LT_DRBG_HASH_Random_Bytes(LT_DRBG_HASH_CTX *drbgCtx, const u8 *additional, LT_SIZE additionalLen, u8 *output, LT_SIZE outputLen) {
    // sanity check
    if (!drbgCtx || !output || !outputLen) {
        return kLTSystemCrypto_Result_Null;
    }
    if (outputLen > MAX_BYTE_COUNT_PER_REQUEST || additionalLen > MAX_INPUT_STRING_LENGTH) {
        return kLTSystemCrypto_Result_WrongLength;
    }
    if (drbgCtx->reseedCounter > RESEED_INTERVAL) {
        return kLTSystemCrypto_Result_NeedToReseed;
    }

    lt_memset(drbgCtx->tmp.v, 0, sizeof(drbgCtx->tmp.v));
    LT_BN_Copy_B2L((u8 *)drbgCtx->tmp.v, sizeof(drbgCtx->tmp.v), drbgCtx->V, SEED_LENGTH_BYTE);

    lt_memset(drbgCtx->tmp.w, 0, sizeof(drbgCtx->tmp.w));
    // step 2
    if (additional && additionalLen) {
        // step 2.1, w = Hash(0x02 || V || additional)
        drbgCtx->tmp.pre = 2;
        LT_SHA256_Init(&drbgCtx->tmp.hashCtx);
        LT_SHA256_Update(&drbgCtx->tmp.hashCtx, &drbgCtx->tmp.pre, sizeof(drbgCtx->tmp.pre));
        LT_SHA256_Update(&drbgCtx->tmp.hashCtx, drbgCtx->V, SEED_LENGTH_BYTE);
        LT_SHA256_Update(&drbgCtx->tmp.hashCtx, additional, additionalLen);
        LT_SHA256_Finish(&drbgCtx->tmp.hashCtx, drbgCtx->tmp.digest);
        // step 2.2, V = (V+w), w is digest
        LT_BN_Copy_B2L((u8*)drbgCtx->tmp.w, sizeof(drbgCtx->tmp.w), drbgCtx->tmp.digest, sizeof(drbgCtx->tmp.digest));
        LT_BN_Add_Unsigned(drbgCtx->tmp.v, drbgCtx->tmp.v, drbgCtx->tmp.w, SEED_LENGTH_U32);
        LT_BN_Copy_L2B(drbgCtx->V, SEED_LENGTH_BYTE, (u8 *)drbgCtx->tmp.v, SEED_LENGTH_BYTE);
    }

    // step 3
    LT_DRBG_HASH_Gen(drbgCtx, output, outputLen);

    // step 4, H = Hash(0x03 || V)
    drbgCtx->tmp.pre = 3;
    LT_SHA256_Init(&drbgCtx->tmp.hashCtx);
    LT_SHA256_Update(&drbgCtx->tmp.hashCtx, &drbgCtx->tmp.pre, sizeof(drbgCtx->tmp.pre));
    LT_SHA256_Update(&drbgCtx->tmp.hashCtx, drbgCtx->V, SEED_LENGTH_BYTE);
    LT_SHA256_Finish(&drbgCtx->tmp.hashCtx, drbgCtx->tmp.digest);
    // step 5, V = V+H, w is H is digest
    LT_BN_Copy_B2L((u8*)drbgCtx->tmp.w, sizeof(drbgCtx->tmp.w), drbgCtx->tmp.digest, sizeof(drbgCtx->tmp.digest));
    LT_BN_Add_Unsigned(drbgCtx->tmp.v, drbgCtx->tmp.v, drbgCtx->tmp.w, SEED_LENGTH_U32);
    // step 5, V = V+C, w is C
    LT_BN_Copy_B2L((u8 *)drbgCtx->tmp.w, sizeof(drbgCtx->tmp.w), drbgCtx->C, SEED_LENGTH_BYTE);
    LT_BN_Add_Unsigned(drbgCtx->tmp.v, drbgCtx->tmp.v, drbgCtx->tmp.w, SEED_LENGTH_U32);
    // step 5, V = V+reseed_counter
    lt_memset(drbgCtx->tmp.w, 0, sizeof(drbgCtx->tmp.w));
    lt_memcpy(drbgCtx->tmp.w, &(drbgCtx->reseedCounter), sizeof(drbgCtx->reseedCounter));
    LT_BN_Add_Unsigned(drbgCtx->tmp.v, drbgCtx->tmp.v, drbgCtx->tmp.w, SEED_LENGTH_U32);
    // step 5, get back big endian V
    LT_BN_Copy_L2B(drbgCtx->V, SEED_LENGTH_BYTE, (u8 *)drbgCtx->tmp.v, SEED_LENGTH_BYTE);

    // step 6
    ++drbgCtx->reseedCounter;

    lt_memset(&drbgCtx->tmp, 0, sizeof(drbgCtx->tmp));
    return kLTSystemCrypto_Result_Ok;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  15-Feb-22   gallienus   created
 */
