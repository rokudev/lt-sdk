/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoAes128Gcm.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 *
 * GCM, SP SP 800-38D, https://csrc.nist.gov/publications/detail/sp/800-38d/final
 * GCM is implemented in the big endian byte order.
 * The left most byte is the most significant byte at the lowest address.
 *
 *******************************************************************************
 *
 * Ported Galois multiplication from mbedtls/library/gcm.c: static void gcm_mult
 *
 * The copyright and license in the header of the original gcm.c are as follows.
 *
 *******************************************************************************
 *
 * Copyright The Mbed TLS Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 ( the "License" ); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include "LTDriverCryptoAes128.h"

// Context of AES128_GCM
typedef struct LT_AES128_GCM_CTX {
    u32 eKey[44];    // expanded keys
    u32 CB[4];       // counter block, IV||Counter, to be encrypted, then XOR with input, to produce output
    u32 CB3;         // the last u32 in counter block, but in LE, i.e. CB[3] = swap32(CB3).
    u32 HB[4];       // ghash block, to XOR with output, then multiple H, to produce next ghash block
    u32 J0[4];       // first encrypted counter block for computing tag
    u64 dataLen;     // data length, in bytes
    u64 aadLen;      // aad length, in bytes, additional authentication data
    u64 HL[16];      // precalculated HTable low
    u64 HH[16];      // precalculated HTable high
    // temporary buffer for computation
    union {
        u32 H[AES128_BLOCK_LENGTH / 4];
        struct {
            u32 buf[AES128_BLOCK_LENGTH / 4];
            u32 eCB[AES128_BLOCK_LENGTH / 4]; // encrypted counter block
        };
    };
} LT_AES128_GCM_CTX;

typedef enum {
    kCommonDriverCrypto_AESMode_Encrypt = 1,
    kCommonDriverCrypto_AESMode_Decrypt = 0,
} CommonDriverCryptoAESMode;

static const u64 last4[16] = {0x0000,0x1c20,0x3840,0x2460,0x7080,0x6ca0,0x48c0,0x54e0,
                              0xe100,0xfd20,0xd940,0xc560,0x9180,0x8da0,0xa9c0,0xb5e0};

/*
 * Sets output to x times H using the precomputed tables.
 * x and output are seen as elements of GF(2^128) as in [MGV].
 */
static void GMult(LT_AES128_GCM_CTX *ctx, const u8 x[AES128_BLOCK_LENGTH], u32 output[AES128_BLOCK_LENGTH / 4]) {
    int i;
    u8 lo, hi, rem;
    u64 zh, zl;

    lo = x[15] & 0x0f;
    zh = ctx->HH[lo];
    zl = ctx->HL[lo];

    for (i = 15; i >= 0; --i) {
        lo = x[i] & 0xf;
        hi = (x[i] >> 4) & 0xf;

        if (i != 15) {
            rem = zl & 0x0f;
            zl  = (zh << 60) | (zl >> 4);
            zh  = (zh >> 4);
            zh ^= ((u64)last4[rem]) << 48;
            zh ^= ctx->HH[lo];
            zl ^= ctx->HL[lo];
        }
        rem = zl & 0x0f;
        zl = (zh << 60) | (zl >> 4);
        zh = (zh >> 4);
        zh ^= ((u64)last4[rem]) << 48;
        zh ^= ctx->HH[hi];
        zl ^= ctx->HL[hi];
    }
    output[0] = LT_BE32(zh >> 32);
    output[1] = LT_BE32((u32)zh);
    output[2] = LT_BE32(zl >> 32);
    output[3] = LT_BE32((u32)zl);
}

static void GTable(LT_AES128_GCM_CTX *ctx, const u32 h[AES128_BLOCK_LENGTH / 4]) {
    u64 vl, vh;
    u32 i, j;
    // pack h as two 64-bits ints, big-endian
    vh = LT_BE32(h[0]);
    vh = (vh << 32) | LT_BE32(h[1]);
    vl = LT_BE32(h[2]);
    vl = (vl << 32) | LT_BE32(h[3]);

    // 8 = 1000 corresponds to 1 in GF(2^128)
    ctx->HL[8] = vl;
    ctx->HH[8] = vh;
    // 0 corresponds to 0 in GF(2^128)
    ctx->HH[0] = 0;
    ctx->HL[0] = 0;

    for(i = 4; i > 0; i >>= 1) {
        u32 T = (vl & 1) * 0xe1000000UL;
        vl = (vh << 63) | (vl >> 1);
        vh = (vh >> 1) ^ ((u64)T << 32);
        ctx->HL[i] = vl;
        ctx->HH[i] = vh;
    }
    for (i = 2; i <= 8; i <<= 1 ) {
        u64 *HiL = ctx->HL + i;
        u64 *HiH = ctx->HH + i;
        vh = *HiH;
        vl = *HiL;
        for(j = 1; j < i; ++j) {
            HiH[j] = vh ^ ctx->HH[j];
            HiL[j] = vl ^ ctx->HL[j];
        }
    }
}

/**
 * @brief  Initialize the GCM context
 * @param  ctx     the context to initialize
 * @param  key     the key
 * @param  iv      the IV
 * @param  ivLen   the length of IV
 * @param  aad     the additional authentication data
 * @param  aadLen  the length of aad
 * @return result code
 */
static LTSystemCryptoResult AES128_GCM_Init(LT_AES128_GCM_CTX *ctx, const u8 *key, const u8 *iv, LT_SIZE ivLen, const u8 *aad, LT_SIZE aadLen) {
    // sanity check
    if (!ctx || !key || !iv || !ivLen) {
        return kLTSystemCrypto_Result_Null;
    }
    if (ivLen != AES128_GCM_IV_LENGTH || aadLen > AES128_GCM_MAX_AAD_LENGTH) {
        return kLTSystemCrypto_Result_WrongLength;
    }

    // clean ctx
    lt_memset(ctx, 0, sizeof(LT_AES128_GCM_CTX));
    LT_SIZE blockLen;
    lt_memcpy(ctx->H, key, AES128_KEY_LENGTH);
    LT_AES128_Keysched((u8 *)ctx->H, ctx->eKey);

    // 7.1, step 1
    lt_memset(ctx->H, 0, sizeof(ctx->H));
    LT_AES128_Encrypt((u8 *)ctx->H, ctx->eKey, (u8 *)ctx->H);
    GTable(ctx, ctx->H);
    lt_memset(ctx->H, 0, sizeof(ctx->H));

    // 7.1, step 2
    // this implementation only takes 12 Bytes IV
    lt_memcpy(ctx->CB, iv, ivLen);
    ctx->CB3 = 1;
    ctx->CB[3] = LT_SWAP32(ctx->CB3);
    #if 0
    // TODO: maybe useful if we need to use IV of more than 12 Byes.
    u32 buf[AES128_BLOCK_LENGTH / 4];
    if(ivLen == 12) {
        // 96-bit IV
        lt_memcpy(ctx->CB, iv, ivLen);
        ctx->CB[AES128_BLOCK_LENGTH - 1] = 1;
    } else {
        // keep the last 16 bytes, value as the bit length of IV
        lt_memset(buf, 0x00, sizeof(buf));
        buf[3] = LT_BE32(ivLen << 3);
        // GHash IV..0||0..Len(IV)
        // !!!!!! TODO need to align pIV with u32 !!!!!!
        p = iv;
        // ctx->CB is 0 initially
        while(ivLen > 0) {
            nBlockLen = (ivLen < AES128_BLOCK_LENGTH ) ? ivLen : AES128_BLOCK_LENGTH;
            LT_BN_Xor_Unsigned((u32 *)ctx->CB, (u32 *)ctx->CB, (u32 *)p, nBlockLen >> 2);
            GMult(ctx, ctx->CB, ctx->CB);
            ivLen -= nBlockLen;
            p     += nBlockLen;
        }
        LT_BN_Xor_Unsigned((u32 *)ctx->CB, (u32 *)ctx->CB, buf, AES128_BLOCK_LENGTH >> 2);
        GMult(ctx, ctx->CB, ctx->CB);
    }
    #endif

    // 7.1, step 3, make J for the later auth tag
    LT_AES128_Encrypt((u8 *)ctx->CB, ctx->eKey, (u8 *)ctx->J0);

    // 7.1, step 4, pad AAD with 0 to full blocks
    // 7.1, step 5, GHash AAD string, make the first HB
    ctx->aadLen = aadLen;
    while (aadLen > 0 && aad) {
        blockLen = (aadLen < AES128_BLOCK_LENGTH) ? aadLen : AES128_BLOCK_LENGTH;
        lt_memcpy(ctx->H, aad, blockLen);
        lt_memset((u8 *)ctx->H + blockLen, 0, AES128_BLOCK_LENGTH - blockLen);
        for (u32 i = 0; i < ((blockLen + 3) >> 2); ++i) {
            ctx->HB[i] ^= ctx->H[i];
        }
        GMult(ctx, (u8 *)ctx->HB, ctx->HB);
        aadLen -= blockLen;
        aad    += blockLen;
    }

    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Encrypt an input block
 * @param  ctx       the context
 * @param  eMode     the mode, ENCRYPT or DECRYPT
 * @param  input     the input block to encrypt
 * @param  inputLen  the length of input
 * @param  output    the output of encryption
 * @return result code
 */
static LTSystemCryptoResult AES128_GCM_Update(LT_AES128_GCM_CTX *ctx, CommonDriverCryptoAESMode eMode, const u8 *input, LT_SIZE inputLen, u8 *output) {
    // sanity check
    if (!ctx || !input || !output) {
        return kLTSystemCrypto_Result_Null;
    }

    ctx->dataLen += inputLen;

    LT_SIZE blockLen, i;

    // 6.4, step 1-2, notations, no code needed
    // 6.5, step 1-4, notations, no code needed

    while (inputLen > 0) {
        blockLen = (inputLen < AES128_BLOCK_LENGTH) ? inputLen : AES128_BLOCK_LENGTH;

        // 6.5, step 5, increment the counter
        ++ctx->CB3;
        ctx->CB[3] = LT_SWAP32(ctx->CB3);

        // 6.5, step 6, encrypt the counter block
        LT_AES128_Encrypt((u8 *)ctx->CB, ctx->eKey, (u8 *)ctx->eCB);

        // 6.5, step 6, encrypted counter block XOR input to produce output
        // 6.4, step 3, previous ghash block XOR output (padded with 0 to a full block) to produce next ghash block
        lt_memcpy(ctx->buf, input, blockLen);
        if (eMode == kCommonDriverCrypto_AESMode_Encrypt) {
            for (i = 0; i < ((blockLen + 3) >> 2); ++i) {
                ctx->buf[i] ^= ctx->eCB[i];
            }
            lt_memset((u8 *)ctx->buf + blockLen, 0, AES128_BLOCK_LENGTH - blockLen);
            for (i = 0; i < ((blockLen + 3) >> 2); ++i) {
                ctx->HB[i] ^= ctx->buf[i];
            }
        } else {
            lt_memset((u8 *)ctx->buf + blockLen, 0, AES128_BLOCK_LENGTH - blockLen);
            for (i = 0; i < ((blockLen + 3) >> 2); ++i) {
                ctx->HB[i] ^= ctx->buf[i];
                ctx->buf[i] ^= ctx->eCB[i];
            }
        }
        lt_memcpy(output, ctx->buf, blockLen);

        // 6.4, step 3, produce new ghash block
        GMult(ctx, (u8 *)ctx->HB, ctx->HB);

        inputLen -= blockLen;
        input    += blockLen;
        output   += blockLen;
    }

    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Finish the encryption
 * @param  ctx     the context
 * @param  tag     the tag after encryption
 * @param  tagLen  the length of tag
 * @return result code
 */
static LTSystemCryptoResult AES128_GCM_Finish(LT_AES128_GCM_CTX *ctx, u8 *tag, LT_SIZE tagLen) {
    // sanity check
    if (!ctx || !tag) {
        return kLTSystemCrypto_Result_Null;
    }
    if (tagLen < AES128_GCM_MIN_TAG_LENGTH || tagLen > AES128_GCM_MAX_TAG_LENGTH) {
        return kLTSystemCrypto_Result_WrongLength;
    }

    // 7.1, step 6, len(A)||len(C), then GHash
    u64 dataBitLen = ctx->dataLen << 3;
    u64 aadBitLen  = ctx->aadLen << 3;
    ctx->HB[0] ^= LT_BE32(aadBitLen >> 32);
    ctx->HB[1] ^= LT_BE32((u32)aadBitLen);
    ctx->HB[2] ^= LT_BE32(dataBitLen >> 32);
    ctx->HB[3] ^= LT_BE32((u32)dataBitLen);
    GMult(ctx, (u8 *)ctx->HB, ctx->HB);
    // 7.1, step 7
    for (u32 i = 0; i < AES128_BLOCK_LENGTH / 4; ++i) {
        ctx->J0[i] ^= ctx->HB[i];
    }
    lt_memcpy(tag, ctx->J0, tagLen);

    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Complete process of encryption/decryption
 * @param  ctx       the context
 * @param  eMode     the mode (encryption or decryption)
 * @param  key       the key
 * @param  iv        the IV
 * @param  ivLen     the length of IV
 * @param  aad       the additional authentication data
 * @param  aadLen    the length of aad
 * @param  input     the input data
 * @param  inputLen  the length of input
 * @param  output    the output
 * @param  tag       the tag
 * @param  tagLen    the length of tag
 * @return result code
 */
static LTSystemCryptoResult AES128_GCM_Process(LT_AES128_GCM_CTX *ctx, CommonDriverCryptoAESMode eMode, const u8 *key, const u8 *iv, LT_SIZE ivLen, const u8 *aad, LT_SIZE aadLen, const u8 *input, LT_SIZE inputLen, u8 *output, u8 *tag, LT_SIZE tagLen) {
    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;

    if (kLTSystemCrypto_Result_Ok == (ret = AES128_GCM_Init(ctx, key, iv, ivLen, aad, aadLen)) &&
        kLTSystemCrypto_Result_Ok == (ret = AES128_GCM_Update(ctx, eMode, input, inputLen, output)) &&
        kLTSystemCrypto_Result_Ok == (ret = AES128_GCM_Finish(ctx, tag, tagLen))) {
        ;
    }

    return ret;
}

/**
 * @brief  Encrypt data
 * @param  key         the key, must be of 16 Bytes.
 * @param  iv          a pointer to an IV, initial vector, cannot be NULL.
 * @param  ivLen       the number of IV bytes, cannot be 0, must be smaller than MAX_IV_LENGTH (64), recommend 12.
 * @param  aad         a pointer to an additional authentication data, may be NULL.
 * @param  aadLen      the length of aad, may be 0, must be smaller than MAX_AAD_LENGTH (64).
 * @param  plainText   a pointer to the input plaintext, must point to a buffer of ptextLen Bytes.
 * @param  textLen     the length of plaintext/ciphertext, in bytes
 * @param  cipherText  a pointer to the output ciphertext, must point to a buffer of ptextLen Bytes.
 * @param  tag         a pointer to the output authentication tag, must point to a buffer of tagLen Bytes.
 * @param  tagLen      the number of tag Bytes, must be 12, 13, 14, 15, or 16.
 * @return result code
 */
static LTSystemCryptoResult LT_AES128_GCM_Encrypt(const u8 key[AES128_KEY_LENGTH], const u8 *iv, LT_SIZE ivLen, const u8 *aad, LT_SIZE aadLen, const u8 *plainText, LT_SIZE textLen, u8 *cipherText, u8 *tag, LT_SIZE tagLen) {
    LT_AES128_GCM_CTX *ctx = lt_malloc(sizeof(LT_AES128_GCM_CTX));
    if (!ctx) return kLTSystemCrypto_Result_OOM;

    // encryption and produce tag
    LTSystemCryptoResult ret = AES128_GCM_Process(ctx, kCommonDriverCrypto_AESMode_Encrypt, key, iv, ivLen, aad, aadLen, plainText, textLen, cipherText, tag, tagLen);

    lt_memset(ctx, 0, sizeof(LT_AES128_GCM_CTX));
    lt_free(ctx);
    return ret;
}

/**
 * @brief  Decrypt data
 * @param  key         a pointer to a key, must point to a buffer of 16 Bytes.
 * @param  iv          a pointer to an IV, initial vector, cannot be NULL.
 * @param  ivLen       the number of IV bytes, cannot be 0, must be smaller than MAX_IV_LENGTH (64), recommend 12.
 * @param  aad         a pointer to an additional authentication data, may be NULL.
 * @param  aadLen      the length of aad, may be 0, must be smaller than MAX_AAD_LENGTH (64).
 * @param  cipherText  a pointer to the input ciphertext, must point to a buffer of ctextLen Bytes.
 * @param  textLen     the length of plaintext/ciphertext, in bytes
 * @param  tag         a pointer to the input authentication tag, must point to a buffer of tagLen Bytes.
 * @param  tagLen      the number of tag Bytes, must be 12, 13, 14, 15, or 16.
 * @param  plainText   a pointer to the output plaintext, must point to a buffer of ctextLen Bytes.
 * @return result code
 */
static LTSystemCryptoResult LT_AES128_GCM_Decrypt(const u8 key[AES128_KEY_LENGTH], const u8 *iv, LT_SIZE ivLen, const u8 *aad, LT_SIZE aadLen, const u8 *cipherText, LT_SIZE textLen, const u8 *tag, LT_SIZE tagLen, u8 *plainText) {
    LT_AES128_GCM_CTX *ctx = lt_malloc(sizeof(LT_AES128_GCM_CTX));
    if (!ctx) return kLTSystemCrypto_Result_OOM;

    // decryption and reproduce tag
    u8 newTag[AES128_GCM_MAX_TAG_LENGTH];
    LTSystemCryptoResult ret = AES128_GCM_Process(ctx, kCommonDriverCrypto_AESMode_Decrypt, key, iv, ivLen, aad, aadLen, cipherText, textLen, plainText, newTag, tagLen);

    lt_memset(ctx, 0, sizeof(LT_AES128_GCM_CTX));
    lt_free(ctx);

    if (kLTSystemCrypto_Result_Ok != ret) return ret;

    // verify tag
    bool match = true;
    for(u32 i = 0; i < tagLen; ++i) {
        match &= (tag[i] == newTag[i]); // prevent timing attack
    }
    if (!match) {
        lt_memset(plainText, 0, textLen); // prevent cache and side-channel attack
        return kLTSystemCrypto_Result_WrongVerification;
    }

    return kLTSystemCrypto_Result_Ok;
}

typedef_LTObjectImpl(LTDriverCryptoAes128Gcm, LTSoftwareCryptoAes128Gcm) {
} LTOBJECT_API;

static LTSystemCryptoResult LTSoftwareCryptoAes128Gcm_Encrypt(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_GCM_IV_LENGTH], const u8 *aad, LT_SIZE aadLen, const u8 *plainText, LT_SIZE textLen, u8 *cipherText, u8 *tag, LT_SIZE tagLen) {
    return LT_AES128_GCM_Encrypt(key, iv, AES128_GCM_IV_LENGTH, aad, aadLen, plainText, textLen, cipherText, tag, tagLen);
}

static LTSystemCryptoResult LTSoftwareCryptoAes128Gcm_Decrypt(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_GCM_IV_LENGTH], const u8 *aad, LT_SIZE aadLen, const u8 *cipherText, LT_SIZE textLen, const u8 *tag, LT_SIZE tagLen, u8 *plainText) {
    return LT_AES128_GCM_Decrypt(key, iv, AES128_GCM_IV_LENGTH, aad, aadLen, cipherText, textLen, tag, tagLen, plainText);
}

static void LTSoftwareCryptoAes128Gcm_DestructObject(LTSoftwareCryptoAes128Gcm *instance) {
    LT_UNUSED(instance);
}

static bool LTSoftwareCryptoAes128Gcm_ConstructObject(LTSoftwareCryptoAes128Gcm *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoAes128Gcm, LTSoftwareCryptoAes128Gcm,
    Encrypt,
    Decrypt,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  09-Feb-22   gallienus   created
 */
