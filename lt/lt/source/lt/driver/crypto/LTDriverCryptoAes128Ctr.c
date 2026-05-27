/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoAes128Ctr.c
 *
 * AES128 CTR, Section 6.5, https://csrc.nist.gov/publications/detail/sp/800-38a/final
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/LT.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include "LTDriverCrypto.h"
#include "LTDriverCryptoAes128.h"

// Context of AES128_CTR
typedef struct LT_AES128_CTR_CTX {
    u32 eKey[44];    // expanded keys
    u8  CB[16];      // counter block to be encrypted, then XOR with input, to produce output
    // temporary buffer for computation
    union {
        u32 H[AES128_BLOCK_LENGTH / 4];
        u32 eCB[AES128_BLOCK_LENGTH / 4]; // encrypted counter block
    };
} LT_AES128_CTR_CTX;

/**
 * @brief  Initialize the CTR context
 * @param  ctx  the context to initialize
 * @param  key  the key
 * @param  iv   the IV, or the initial counter
 * @return result code
 */
static LTSystemCryptoResult AES128_CTR_Init(LT_AES128_CTR_CTX *ctx, const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH]) {
    // sanity check
    if (!ctx || !key || !iv) {
        return kLTSystemCrypto_Result_Null;
    }

    // init ctx
    lt_memset(ctx, 0, sizeof(LT_AES128_CTR_CTX));
    lt_memcpy(ctx->H, key, AES128_KEY_LENGTH);
    LT_AES128_Keysched((u8 *)ctx->H, ctx->eKey);
    lt_memcpy(ctx->CB, iv, sizeof(ctx->CB));

    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Encrypt an input block (same as decrypt)
 * @param  ctx       the context
 * @param  input     the input block to encrypt
 * @param  inputLen  the length of input
 * @param  output    the output of encryption
 * @return result code
 */
static LTSystemCryptoResult AES128_CTR_Update(LT_AES128_CTR_CTX *ctx, const u8 *input, LT_SIZE inputLen, u8 *output) {
    // sanity check
    if (!ctx || !input || !output) {
        return kLTSystemCrypto_Result_Null;
    }

    LT_SIZE blockLen;
    int i;

    while (inputLen > 0) {
        blockLen = (inputLen < AES128_BLOCK_LENGTH) ? inputLen : AES128_BLOCK_LENGTH;

        // encrypt the counter block
        LT_AES128_Encrypt(ctx->CB, ctx->eKey, (u8 *)ctx->eCB);

        // xor the encrypted counter block with input block
        for(i = 0; i < (int)blockLen; ++i) {
            output[i]   = ((u8 *)ctx->eCB)[i] ^ input[i];
        }

        // increment counter
        for(i = 15; i >= 0; --i) {
            if ((++ctx->CB[i]) != 0) {
                break;
            }
        }

        inputLen -= blockLen;
        input    += blockLen;
        output   += blockLen;
    }

    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Complete process of encryption/decryption
 * @param  key       the key
 * @param  iv        the IV
 * @param  input     the input data
 * @param  inputLen  the length of input
 * @param  output    the output
 * @return result code
 */
static LTSystemCryptoResult LT_AES128_CTR_Process(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *input, LT_SIZE inputLen, u8 *output) {
    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;
    LT_AES128_CTR_CTX *ctx = lt_malloc(sizeof(LT_AES128_CTR_CTX));

    if (kLTSystemCrypto_Result_Ok == (ret = AES128_CTR_Init(ctx, key, iv)) &&
        kLTSystemCrypto_Result_Ok == (ret = AES128_CTR_Update(ctx, input, inputLen, output))) {
        ;
    }

    lt_memset(ctx, 0, sizeof(LT_AES128_CTR_CTX));
    lt_free(ctx);
    return ret;
}

typedef_LTObjectImpl(LTDriverCryptoAes128Ctr, LTSoftwareCryptoAes128Ctr) {
} LTOBJECT_API;

static LTSystemCryptoResult LTSoftwareCryptoAes128Ctr_Encrypt(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText) {
    return LT_AES128_CTR_Process(key, iv, plainText, textLen, cipherText);
}

static LTSystemCryptoResult LTSoftwareCryptoAes128Ctr_Decrypt(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText) {
    return LT_AES128_CTR_Process(key, iv, cipherText, textLen, plainText);
}

static void LTSoftwareCryptoAes128Ctr_DestructObject(LTSoftwareCryptoAes128Ctr *instance) {
    LT_UNUSED(instance);
}

static bool LTSoftwareCryptoAes128Ctr_ConstructObject(LTSoftwareCryptoAes128Ctr *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoAes128Ctr, LTSoftwareCryptoAes128Ctr,
    Encrypt,
    Decrypt,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  15-Jul-20   gallienus   created */
