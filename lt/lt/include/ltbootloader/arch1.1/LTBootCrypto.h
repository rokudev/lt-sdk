/*******************************************************************************
 * LTBootCrypto.h                                                  LT Bootloader
 *                                                                (Arch Ver 1.1)
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef __LTBOOT_CRYPTO_H_
#define __LTBOOT_CRYPTO_H_

#include "LTBoot.h"

enum {
    LTSYSTEMCRYPTO_U32_PER_U256 = 8
};

typedef u32 u256[LTSYSTEMCRYPTO_U32_PER_U256];

/* SHA256 */

enum {
    SHA256_BLOCK_LENGTH = 64,
    SHA256_HASH_LENGTH  = 32,
    SHA256_STATE_LENGTH = 8     // in u32
};

typedef struct {
    union {
        u64 DATALen;                   /* Total data length in bytes. */
        u32 dataLen[2];
    };
    u8  data[SHA256_BLOCK_LENGTH];     /* Buffered data to make a full block, big endian, aligned to u32. */
    u32 state[SHA256_STATE_LENGTH];
    /* local variable for block hash */
    struct {
        u32 s[SHA256_STATE_LENGTH];
        u32 m[SHA256_BLOCK_LENGTH];
        u32 t;
    } tmp;
} LT_SHA256_CTX;

void LTBootCrypto_SHA256_Init(LT_SHA256_CTX * ctx);
void LTBootCrypto_SHA256_Update(LT_SHA256_CTX * ctx, const u8 * data, u32 len);
void LTBootCrypto_SHA256_Finish(LT_SHA256_CTX * ctx, u8 digest[SHA256_HASH_LENGTH]);

/* SHA512 */

enum {
    SHA512_BLOCK_LENGTH = 128,
    SHA512_HASH_LENGTH  = 64,
    SHA512_STATE_LENGTH = 8     // in u64
};

typedef struct LT_SHA512_CTX {
    u64 dataLen[2];                   /* total data length, in bytes */
    u8  data[SHA512_BLOCK_LENGTH];    /* buffered data to make a full block 64 Bytes, big endian */
    u64 state[SHA512_STATE_LENGTH];   /* hash state, little endian for computation */
    /* local variables in block hash */
    struct {
        u64 s[SHA512_STATE_LENGTH];
        u64 m[80];
        u64 t;
        u32 left;
        u32 fill;
        u64 l;
    } tmp;
} LT_SHA512_CTX;

void LTBootCrypto_SHA512_Init(LT_SHA512_CTX * ctx);
void LTBootCrypto_SHA512_Update(LT_SHA512_CTX * ctx, const u8 * data, u32 len);
void LTBootCrypto_SHA512_Finish(LT_SHA512_CTX * ctx, u8 digest[SHA512_HASH_LENGTH]);

/* Ed25519 */

enum {
    EdDSA_SIGNATURE_LENGTH = 64,
    EdDSA_KEY_LENGTH       = 32
};

/* Affine coordinates of Ed25519 point */
typedef struct EdPoint {
    u256 x;
    u256 y;
} EdPoint;

typedef struct {
    const EdPoint kEd25519_BP;                           // Ed25519 base point
    const u32 kEd25519_N[LTSYSTEMCRYPTO_U32_PER_U256];   // Ed25519 modulus
    const u32 kEd25519_L[LTSYSTEMCRYPTO_U32_PER_U256];   // Ed25519 group order
    const u32 kEd25519_N1;                               // -1/N % R, then take the lowest u32
    const u32 kEd25519_R2[LTSYSTEMCRYPTO_U32_PER_U256];  // R^2 % N
    const u32 kEd25519_N2[LTSYSTEMCRYPTO_U32_PER_U256];  // N - 2
    const u32 kEd25519_L1;                               // -1/L % R, then take the lowest u32
    const u32 kEd25519_Q2[LTSYSTEMCRYPTO_U32_PER_U256];  // R^2 % L
    const u32 kEd25519_d[LTSYSTEMCRYPTO_U32_PER_U256];
    const u32 kEd25519_d2[LTSYSTEMCRYPTO_U32_PER_U256];  // d * 2
    const u32 kEd25519_SQ[LTSYSTEMCRYPTO_U32_PER_U256];  // SQ = 2^((N-1)/4) % N
    const u32 kX25519_A24[LTSYSTEMCRYPTO_U32_PER_U256];
    const u32 kX25519_BP[LTSYSTEMCRYPTO_U32_PER_U256];   // X25519 base point (generator)
} LTBootCryptoConsts;

LTBootSecurityCheck LTBootCrypto_Ed25519_Verify(const u8 * data,
                                                   u32 dataLen,
                                                   const u8 signature[EdDSA_SIGNATURE_LENGTH],
                                                   const u8 pubKey[EdDSA_KEY_LENGTH],
                                                   LTBootSecurityCheck * pCheck);

#endif
