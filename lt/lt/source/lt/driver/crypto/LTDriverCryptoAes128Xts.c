/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoAes128Xts.c
 *
 * AES128 XTS, https://csrc.nist.gov/pubs/sp/800/38/e/final
 * Further document with Pseudo code: https://luca-giuzzi.unibs.it/corsi/Support/papers-cryptography/1619-2007-NIST-Submission.pdf
 *
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

// Context of AES128_XTS
typedef struct LT_AES128_XTS_CTX {
    u8 tweak[AES128_BLOCK_LENGTH];            // tweak
    u8 prevTweak[AES128_BLOCK_LENGTH];        // previous tweak for decryption of last block
    u32 eKey1[(AES128_KEY_LENGTH/4) * 11];    // expand key to round keys, 11 rounds stored as u32
    u32 eKey2[(AES128_KEY_LENGTH/4) * 11];    // expand key to round keys, 11 rounds stored as u32
    u8 *prevBlock;
    u8 *outputBlock;
    bool encrypt;
    bool finished;
} LT_AES128_XTS_CTX;

void LT_AES128_XTS_multiply_GF128(u8 *block) {
    // Standard irreducible polynomial 0x87 (128 + 4 + 2 + 1)
    u8 carry = (block[15] & 0x80) ? 0x87 : 0x00;

    for (u32 i = 15; i > 0; i--) {
        block[i] = (block[i] << 1) | (block[i - 1] >> 7);
    }
    block[0] <<= 1;
    block[0] ^= carry;
}

/**
 * @brief  Initialize the XTS context
 * @param  ctx  the context to initialize
 * @param  key1  the data key
 * @param  key2  the tweak key
 * @param  iv   the IV, e.g. AES-XTS-PLAIN64 : 0xSSSSSSSSSSSSSSSS0000000000000000 where S is 64-bit sector number in little endian
 * @param  encrypt true for encrypting, false for decrypting
 * @param  output Pointer to output buffer. Must not be NULL
 * @return result code
 */
LTSystemCryptoResult LT_AES128_XTS_Init(LT_AES128_XTS_CTX *ctx,
                                        const u8 key1[AES128_KEY_LENGTH],
                                        const u8 key2[AES128_KEY_LENGTH],
                                        const u8 iv[AES128_XTS_IV_LENGTH],
                                        bool encrypt,
                                        u8 *output) {
    // sanity check
    if (!ctx || !key1 || !key2 || !iv || !output) {
        return kLTSystemCrypto_Result_Null;
    }

    // init ctx
    lt_memset(ctx, 0, sizeof(LT_AES128_XTS_CTX));
    LT_AES128_Keysched(key1, ctx->eKey1);
    LT_AES128_Keysched(key2, ctx->eKey2);
    ctx->prevBlock = NULL;
    ctx->outputBlock = output;
    ctx->encrypt = encrypt;

    // Generate the initial tweak using key2
    LT_AES128_Encrypt(iv, ctx->eKey2, ctx->tweak);
    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  XOR-Encrypt/Decrypt-XOR in-place
 * @param  ctx       the context
 * @param  input     the input
 * @return result code
 */
static void XEX(LT_AES128_XTS_CTX *ctx, const u8 *input) {
    // XOR
    for (LT_SIZE i = 0; i < AES128_BLOCK_LENGTH; i++) {
        ctx->outputBlock[i] = input[i] ^ ctx->tweak[i];
    }

    // Encrypt/Decrypt
    if (ctx->encrypt) {
        LT_AES128_Encrypt(ctx->outputBlock, ctx->eKey1, ctx->outputBlock);
    } else {
        LT_AES128_Decrypt(ctx->outputBlock, ctx->eKey1, ctx->outputBlock);
    }

    // XOR
    for (LT_SIZE i = 0; i < AES128_BLOCK_LENGTH; i++) {
        ctx->outputBlock[i] = ctx->outputBlock[i] ^ ctx->tweak[i];
    }

    // Advance context unless this is last block
    if (!ctx->finished) {
        ctx->prevBlock = ctx->outputBlock;
        ctx->outputBlock += AES128_BLOCK_LENGTH;
        LT_AES128_XTS_multiply_GF128(ctx->tweak);
    }
}

/**
 * @brief  Encrypt/Decrypt Block with AES128 XTS
 * @param  ctx  AES128-XTS context
 * @param  input Pointer to plain/ciphertext data
 * @param  inputLen  Length of plain/ciphertext data. Must be between AES128_BLOCK_LENGTH and (2 * AES128_BLOCK_LENGTH) - 1.
 *                   If larger then AES128_BLOCK_LENGTH then data will be treated as last and cipher text (un)stealing will be performed.
 * @return result code
 */
LTSystemCryptoResult LT_AES128_XTS_ProcessBlock(LT_AES128_XTS_CTX *ctx,
                                                const u8 *input,
                                                LT_SIZE inputLen) {
    if (!ctx) return kLTSystemCrypto_Result_Error;

    if (ctx->finished) return kLTSystemCrypto_Result_EndOfStream;

    // XTS is invalid for block length less than AES128 block length
    // There must be at least 1 block and at most 1 partial block
    if (inputLen < AES128_BLOCK_LENGTH || (inputLen >= AES128_BLOCK_LENGTH * 2)) return kLTSystemCrypto_Result_WrongLength;

    if (inputLen == AES128_BLOCK_LENGTH) {
        // Normal full block XEX
        XEX(ctx, input);
    } else {
        // Has partial block, decrypt last AES128_BLOCK_LENGTH first
        // to recover stolen ciphertext
        u8 temp;
        LT_SIZE partialSize = inputLen - AES128_BLOCK_LENGTH;
        ctx->finished = true; // This stops XEX() updating outputBlock, prevBlock and tweak for us

        if (!ctx->encrypt) {
            // For decrypt, we need last and second last tweak in reverse order
            lt_memcpy(ctx->prevTweak, ctx->tweak, AES128_BLOCK_LENGTH);

            // Set the tweak for the last block
            LT_AES128_XTS_multiply_GF128(ctx->tweak);
        }

        //XEX the second last cipher block to recover plaintext of end partial block and stolen cipher text
        XEX(ctx, input);

        // input buffer can be the same as output buffer.
        // Swap the unstolen cipher text with partial block plaintext
        for (LT_SIZE i = 0; i < partialSize; i++) {
            temp = input[AES128_BLOCK_LENGTH + i];
            ctx->outputBlock[AES128_BLOCK_LENGTH + i] = ctx->outputBlock[i];
            ctx->outputBlock[i] = temp;
        }

        if (ctx->encrypt) {
            // Encrypt, Set the tweak for the last block
            LT_AES128_XTS_multiply_GF128(ctx->tweak);
        } else {
            // Decrypt, Restore tweak for second last block
            lt_memcpy(ctx->tweak, ctx->prevTweak, AES128_BLOCK_LENGTH);
        }

        // XEX in place to recover second last block of plaintext
        XEX(ctx, ctx->outputBlock);
    }
    return kLTSystemCrypto_Result_Ok;
}

static LT_AES128_XTS_CTX s_aesXtsCtx;

/**
 * @brief  Encrypt/Decrypt buffer with AES128 XTS.
 *         This function maintains the encryption context and manages the block-wise decryption of the data supplied.
 * @param  key1  Data key
 * @param  key2  Tweak key
 * @param  iv    IV used to create tweak. In AES-XTS-PLAIN64 for disk encryption, this is the 64bit sector number
 * @param  encrypt True to encrypt, false to decrypt
 * @param  input Pointer to plain/ciphertext data
 * @param  output Pointer to buffer for output
 * @param  length Length of data. Must be greater than AES128_BLOCK_LENGTH
 * @return result code
 */
static LTSystemCryptoResult LT_AES128_XTS_Process( const u8 key1[AES128_KEY_LENGTH],
                                            const u8 key2[AES128_KEY_LENGTH],
                                            const u8 iv[AES128_XTS_IV_LENGTH],
                                            bool encrypt,
                                            const u8 *input,
                                            u8 *output,
                                            LT_SIZE length) {
    // XTS is invalid for block length less than AES128 block length
    // Partial blocks at the end must be processed together with the last full block
    if (length < AES128_BLOCK_LENGTH) return kLTSystemCrypto_Result_WrongLength;

    /* This crypto algorithm is used in the interrupt context, so we have to use static memory in that case. */
    LTCore *core = LT_GetCore();
    LT_AES128_XTS_CTX *ctx = (core->InsideInterruptContext() && core->InterruptsAreDisabled()) ? &s_aesXtsCtx : lt_malloc(sizeof(LT_AES128_XTS_CTX));
    if (!ctx) return kLTSystemCrypto_Result_OOM;

    LTSystemCryptoResult ret = LT_AES128_XTS_Init(ctx, key1, key2, iv, encrypt, output);
    if (ret == kLTSystemCrypto_Result_Ok) {
        while (ret == kLTSystemCrypto_Result_Ok && length >= AES128_BLOCK_LENGTH) {
            // If less than 2 blocks available, consume all of it
            ret = LT_AES128_XTS_ProcessBlock(ctx, input, (length < 2 * AES128_BLOCK_LENGTH) ? length : AES128_BLOCK_LENGTH);
            input += AES128_BLOCK_LENGTH;
            length -= AES128_BLOCK_LENGTH;
        }
    }

    // Must be zero'd before freeing for security
    lt_memset(ctx, 0, sizeof(LT_AES128_XTS_CTX));
    if (!(core->InsideInterruptContext() && core->InterruptsAreDisabled())) lt_free(ctx);
    return ret;
}

typedef_LTObjectImpl(LTDriverCryptoAes128Xts, LTSoftwareCryptoAes128Xts) {
} LTOBJECT_API;

static LTSystemCryptoResult LTSoftwareCryptoAes128Xts_Encrypt(const u8 key1[AES128_KEY_LENGTH], const u8 key2[AES128_KEY_LENGTH], const u8 iv[AES128_XTS_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText) {
    return LT_AES128_XTS_Process(key1, key2, iv, true, plainText, cipherText, textLen);
}

static LTSystemCryptoResult LTSoftwareCryptoAes128Xts_Decrypt(const u8 key1[AES128_KEY_LENGTH], const u8 key2[AES128_KEY_LENGTH], const u8 iv[AES128_XTS_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText) {
    return LT_AES128_XTS_Process(key1, key2, iv, false, cipherText, plainText, textLen);
}

static void LTSoftwareCryptoAes128Xts_DestructObject(LTSoftwareCryptoAes128Xts *instance) {
    LT_UNUSED(instance);
}

static bool LTSoftwareCryptoAes128Xts_ConstructObject(LTSoftwareCryptoAes128Xts *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoAes128Xts, LTSoftwareCryptoAes128Xts,
    Encrypt,
    Decrypt,
);
