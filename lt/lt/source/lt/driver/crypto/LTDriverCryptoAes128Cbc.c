/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoAes128Cbc.c
 *
 * AES128 CBC, Section 6.2 https://doi.org/10.6028/NIST.SP.800-38A
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

// Context of AES128_CBC
typedef struct LT_AES128_CBC_CTX {
    u8  iv1[AES128_CBC_IV_LENGTH];          // iv scratch pad for decrypt
    u8  iv2[AES128_CBC_IV_LENGTH];          // iv scratch pad for decrypt
    u8 *iv;                                 // Pointer to IV for next block
    u8  *prevCipherBlock;                   // Pointer to buffer for previous cipher block during decrypt
    u32 eKey[(AES128_KEY_LENGTH/4) * 11];   // expand key to round keys, 11 rounds stored as u32
    u8 *outputBlock;                        // Pointer to output buffer
    bool encrypt;                           // Encrypt or Decrypt
} LT_AES128_CBC_CTX;

/**
 * @brief  Initialize the CBC context
 * @param  ctx  the context to initialize
 * @param  key  the key
 * @param  iv   the IV
 * @param  encrypt true for encrypting, false for decrypting
 * @param  output Pointer to output buffer. Must not be NULL
 * @return result code
 */
static LTSystemCryptoResult LT_AES128_CBC_Init(LT_AES128_CBC_CTX *ctx,
                                              const u8 key[AES128_KEY_LENGTH],
                                              const u8 iv[AES128_CBC_IV_LENGTH],
                                              bool encrypt,
                                              u8 *output) {
    // sanity check
    if (!ctx || !key || !iv || !output) {
        return kLTSystemCrypto_Result_Null;
    }

    // init ctx
    lt_memset(ctx, 0, sizeof(LT_AES128_CBC_CTX));
    LT_AES128_Keysched(key, ctx->eKey);
    ctx->outputBlock = output;
    ctx->encrypt = encrypt;
    lt_memcpy(ctx->iv1, iv, AES128_CBC_IV_LENGTH);
    ctx->iv = ctx->iv1;
    ctx->prevCipherBlock = ctx->iv2;

    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Encrypt/Decrypt Block with AES128 CBC
 * @param  ctx  AES128-CBC context
 * @param  input Pointer to a block of plain/ciphertext data
 * @return result code
 */
static LTSystemCryptoResult LT_AES128_CBC_ProcessBlock(LT_AES128_CBC_CTX *ctx, const u8 *input) {
    if (!ctx) return kLTSystemCrypto_Result_Error;

    if (ctx->encrypt) {
        // XOR first
        for (int i = 0; i < AES128_BLOCK_LENGTH; i++) {
            ctx->outputBlock[i] = input[i] ^ ctx->iv[i];
        }
        // Then encrypt
        LT_AES128_Encrypt(ctx->outputBlock, ctx->eKey, ctx->outputBlock);
        ctx->iv = ctx->outputBlock;
    } else {
        // Store the ciphertext as IV for next block
        lt_memcpy(ctx->prevCipherBlock, input, AES128_BLOCK_LENGTH);
        //Decrypt first
        LT_AES128_Decrypt(input, ctx->eKey, ctx->outputBlock);
        // Then Xor
        for (int i = 0; i < AES128_BLOCK_LENGTH; i++) {
            ctx->outputBlock[i] = ctx->outputBlock[i] ^ ctx->iv[i];
        }
        ctx->iv = ctx->prevCipherBlock;
        ctx->prevCipherBlock = (ctx->prevCipherBlock == ctx->iv1) ? ctx->iv2 : ctx->iv1;
    }
    ctx->outputBlock = ctx->outputBlock + AES128_BLOCK_LENGTH;

    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Encrypt/Decrypt buffer with AES128 CBC.
 *         This function maintains the encryption context and manages the block-wise decryption of the data supplied.
 *         No padding scheme assumed
 * @param  key      Key
 * @param  iv       IV
 * @param  encrypt  True to encrypt, false to decrypt
 * @param  input    Pointer to plain/ciphertext data
 * @param  output   Pointer to buffer for output
 * @param  length   Length of data. Must be multiple of AES128_BLOCK_LENGTH
 * @return result code
 */
static LTSystemCryptoResult LT_AES128_CBC_Process(const u8 key[AES128_KEY_LENGTH],
                                                  const u8 iv[AES128_CBC_IV_LENGTH],
                                                  bool encrypt,
                                                  const u8 *input,
                                                  u8 *output,
                                                  LT_SIZE length) {
    if (!length || (length % AES128_BLOCK_LENGTH) != 0) return kLTSystemCrypto_Result_WrongLength;

    LT_AES128_CBC_CTX *ctx = (LT_AES128_CBC_CTX *)lt_malloc(sizeof(LT_AES128_CBC_CTX));

    if(!ctx) return kLTSystemCrypto_Result_OOM;

    LTSystemCryptoResult ret = LT_AES128_CBC_Init(ctx, key, iv, encrypt, output);
    if (ret == kLTSystemCrypto_Result_Ok) {
        while (ret == kLTSystemCrypto_Result_Ok && length) {
            // If less than 2 blocks available, consume all of it
            ret = LT_AES128_CBC_ProcessBlock(ctx, input);
            input += AES128_BLOCK_LENGTH;
            length -= AES128_BLOCK_LENGTH;
        }
    }

    // Must be zero'd before freeing for security
    lt_memset(ctx, 0, sizeof(LT_AES128_CBC_CTX));
    lt_free(ctx);
    return ret;
}

typedef_LTObjectImpl(LTDriverCryptoAes128Cbc, LTSoftwareCryptoAes128Cbc) {
} LTOBJECT_API;

static LTSystemCryptoResult LTSoftwareCryptoAes128Cbc_Encrypt(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText) {
    return LT_AES128_CBC_Process(key, iv, true, plainText, cipherText, textLen);
}

static LTSystemCryptoResult LTSoftwareCryptoAes128Cbc_Decrypt(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText) {
    return LT_AES128_CBC_Process(key, iv, false, cipherText, plainText, textLen);
}

static void LTSoftwareCryptoAes128Cbc_DestructObject(LTSoftwareCryptoAes128Cbc *instance) {
    LT_UNUSED(instance);
}

static bool LTSoftwareCryptoAes128Cbc_ConstructObject(LTSoftwareCryptoAes128Cbc *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoAes128Cbc, LTSoftwareCryptoAes128Cbc,
    Encrypt,
    Decrypt,
);
