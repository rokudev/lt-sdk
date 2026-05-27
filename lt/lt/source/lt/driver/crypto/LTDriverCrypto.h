/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCrypto.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef LT_SOURCE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTO_H
#define LT_SOURCE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTO_H

#include <lt/LT.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCryptoDefs.h>

LT_EXTERN_C_BEGIN

struct LT_SHA1_CTX_impl {
    union {
        u64 DATALen;                                  ///< Total data length in bytes.
        u32 dataLen[2];
    };
    u8  data[SHA1_BLOCK_LENGTH];                      ///< Buffered data to make a full block, big endian, must be aligned with u32.
    u32 state[SHA1_STATE_LENGTH];                     ///< Hash state, little endian for computation.
    // local variable for block hash
    struct {
        u32 s[SHA1_STATE_LENGTH];
        u32 m[16];
        u32 t;
    } tmp;
};

struct LT_HMAC_SHA1_CTX_impl {
    u32 pad[SHA1_BLOCK_LENGTH >> 2];
    LT_SHA1_CTX hashCtx;
};

struct LT_SHA256_CTX_impl {
    union {
        u64 DATALen;                                  ///< Total data length in bytes.
        u32 dataLen[2];
    };
    u8  data[SHA256_BLOCK_LENGTH];                    ///< Buffered data to make a full block, big endian, must be aligned with u32.
    u32 state[SHA256_STATE_LENGTH];                   ///< Hash state, little endian for computation.
    // local variable for block hash
    struct {
        u32 s[SHA256_STATE_LENGTH];
        u32 m[SHA256_BLOCK_LENGTH];
        u32 t;
    } tmp;
};

struct LT_HMAC_SHA256_CTX_impl {
    u32 pad[SHA256_BLOCK_LENGTH >> 2];
    LT_SHA256_CTX hashCtx;
};

// only public crypto structures and functions that are exposed to LTSoftwareCrypto.c

/**
 * @brief  Initialize SHA1 context
 * @param  ctx  the context
 * @return result code
 */
LTSystemCryptoResult LT_SHA1_Init(LT_SHA1_CTX *ctx);

/**
 * @brief  Hash input data and update internal state, block by block
 * @param  ctx   the context
 * @param  data  the input data
 * @param  len   the length of data
 * @return result code
 */
LTSystemCryptoResult LT_SHA1_Update(LT_SHA1_CTX *ctx, const u8 *data, LT_SIZE len);

/**
 * @brief  Finish hash and get the digest
 * @param  ctx     the context
 * @param  digest  the output digest
 * @return result code
 */
LTSystemCryptoResult LT_SHA1_Finish(LT_SHA1_CTX *ctx, u8 digest[SHA1_HASH_LENGTH]);

/**
 * @brief  Hash data and get the digest
 * @param  data    the input data block
 * @param  len     the length of data
 * @param  digest  the output digest, 20 Bytes
 * @return result code
 */
LTSystemCryptoResult LT_SHA1_Digest(const u8 *data, LT_SIZE len, u8 digest[SHA1_HASH_LENGTH]);

/**
 * @brief  Initialize SHA256 context
 * @param  ctx  the context
 * @return result code
 */
LTSystemCryptoResult LT_SHA256_Init(LT_SHA256_CTX *ctx);

/**
 * @brief  Hash input data and update internal state, block by block
 * @param  ctx   the context
 * @param  data  the input data
 * @param  len   the length of data
 * @return result code
 */
LTSystemCryptoResult LT_SHA256_Update(LT_SHA256_CTX *ctx, const u8 *data, LT_SIZE len);

/**
 * @brief  Finish hash and get the digest
 * @param  ctx     the context
 * @param  digest  the output digest
 * @return result code
 */
LTSystemCryptoResult LT_SHA256_Finish(LT_SHA256_CTX *ctx, u8 digest[SHA256_HASH_LENGTH]);

/**
 * @brief  Hash data and get the digest
 * @param  data    the input data block
 * @param  len     the length of data
 * @param  digest  the output digest, 32 Bytes
 * @return result code
 */
LTSystemCryptoResult LT_SHA256_Digest(const u8 *data, LT_SIZE len, u8 digest[SHA256_HASH_LENGTH]);

// Context of SHA512
typedef struct LT_SHA512_CTX {
    u64 dataLen[2];                   // total data length, in Bytes
    u8  data[SHA512_BLOCK_LENGTH];    // buffered data to make a full block 64 Bytes, big endian
    u64 state[SHA512_STATE_LENGTH];   // hash state, little endian for computation
    // local variables in block hash
    struct {
        u64 s[SHA512_STATE_LENGTH];
        u64 m[80];
        u64 t;
        u32 left;
        u32 fill;
        u64 l;
    } tmp;
} LT_SHA512_CTX;

/**
 * @brief  Initialize SHA512 context
 * @param  ctx  the context
 * @return result code
 */
LTSystemCryptoResult LT_SHA512_Init(LT_SHA512_CTX *ctx);

/**
 * @brief  Hash input data and update internal state, block by block
 * @param  ctx   the context
 * @param  data  the input data
 * @param  len   the length of data
 * @return result code
 */
LTSystemCryptoResult LT_SHA512_Update(LT_SHA512_CTX *ctx, const u8 *data, LT_SIZE len);

/**
 * @brief  Finish hash and get the digest
 * @param  ctx     the context
 * @param  digest  the output digest, 64 Bytes
 * @return result code
 */
LTSystemCryptoResult LT_SHA512_Finish(LT_SHA512_CTX *ctx, u8 digest[SHA512_HASH_LENGTH]);

/**
 * @brief  Hash data and get the digest
 * @param  data   the input data block
 * @param  len    the length of data
 * @param  digest  the output digest, 64 Bytes
 * @return result code
 */
LTSystemCryptoResult LT_SHA512_Digest(const u8 *data, LT_SIZE len, u8 digest[SHA512_HASH_LENGTH]);

/* Common types of hash functions.
 * Each hash function has a fixed-length digest, so we don't require digest length as a parameter.
 */
typedef LTSystemCryptoResult (* LT_Hash_Init)(void *hashCtx);
typedef LTSystemCryptoResult (* LT_Hash_Update)(void *hashCtx, const u8 *data, LT_SIZE dataLen);
typedef LTSystemCryptoResult (* LT_Hash_Finish)(void *hashCtx, u8 *digest);
typedef LTSystemCryptoResult (* LT_Hash_Digest)(const u8 *data, LT_SIZE dataLen, u8 *digest);

typedef struct LT_HMAC_CTX_Impl {
    u32           *pad;
    void          *hashCtx;
    LT_SIZE        blockLen;
    LT_SIZE        digestLen;
    LT_Hash_Init   HashInit;
    LT_Hash_Update HashUpdate;
    LT_Hash_Finish HashFinish;
    LT_Hash_Digest HashDigest;
} LT_HMAC_CTX_Impl;

/**
 * @brief  Initialize HMAC context
 * @param  hmacCtx  the context
 * @param  key      the key
 * @param  keyLen   the length of key
 * @return result code
 */
LTSystemCryptoResult LT_HMAC_Init(LT_HMAC_CTX_Impl *hmacCtx, const u8 *key, LT_SIZE keyLen);

/**
 * @brief  Hmac input data and update internal state
 * @param  hmacCtx  the context
 * @param  data     the input data
 * @param  len      the length of data
 * @return result code
 */
LTSystemCryptoResult LT_HMAC_Update(LT_HMAC_CTX_Impl *hmacCtx, const u8 *data, LT_SIZE len);

/**
 * @brief  Finish hash and get the hmac
 * @param  hmacCtx the context
 * @param  hmac    the output hmac
 * @return result code
 */
LTSystemCryptoResult LT_HMAC_Finish(LT_HMAC_CTX_Impl *hmacCtx, u8 *hmac);

/**
 * @brief  Hmac data and get the hmac
 * @param  hmacCtx the context
 * @param  key     the key
 * @param  keyLen  the length of key
 * @param  data    the input data block
 * @param  len     the length of data
 * @param  hmac    the output hmac
 * @return result code
 */
LTSystemCryptoResult LT_HMAC_Digest(LT_HMAC_CTX_Impl *hmacCtx, const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE len, u8 *hmac);

// Context of DRBG_HASH
typedef struct {
    u64 reseedCounter;
    u8 V[SEED_LENGTH_BYTE];   // V is a big-endian number
    u8 C[SEED_LENGTH_BYTE];   // C is a big-endian number
    // local variables in random_hash and hash_df
    struct {
        u32 d32[SEED_LENGTH_U32];  // holding little endian value of data, data is big endian
        u8  data[SEED_LENGTH_BYTE];
        u8  digest[HASH_OUTPUT_LENGTH];
        u32 v[SEED_LENGTH_U32];    // holding little endian value of V, V is big endian
        LT_SHA256_CTX hashCtx;
        u32 w[SEED_LENGTH_U32];
        u8 buffer[1 + SEED_LENGTH_BYTE];
        u8 pre;
    } tmp;
} LT_DRBG_HASH_CTX;

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
LTSystemCryptoResult LT_DRBG_HASH_Init(LT_DRBG_HASH_CTX *drbgCtx, const u8 *entropy, LT_SIZE entropyLen, const u8 *nonce, LT_SIZE nonceLen, const u8 *person, LT_SIZE personLen);

/**
 * @brief  Generate random bytes
 * @param  drbgCtx        the context
 * @param  additional     the additional string
 * @param  additionalLen  the length of additional string
 * @param  output         the output
 * @param  outputLen      the length of output
 * @return result code
 */
LTSystemCryptoResult LT_DRBG_HASH_Random_Bytes(LT_DRBG_HASH_CTX *drbgCtx, const u8 *additional, LT_SIZE additionalLen, u8 *output, LT_SIZE outputLen);

/**
 * @brief  Reseed DRBG
 * @param  drbgCtx        the context
 * @param  entropy        the entropy
 * @param  entropyLen     the length of entropy
 * @param  additional     the additional string
 * @param  additionalLen  the length of additional string
 * @return result code
 */
LTSystemCryptoResult LT_DRBG_HASH_Reseed(LT_DRBG_HASH_CTX *drbgCtx, const u8 *entropy, LT_SIZE entropyLen, const u8 *additional, LT_SIZE additionalLen);

void LTSoftwareCrypto_LockMutex(void);
void LTSoftwareCrypto_UnlockMutex(void);

LTSystemCryptoResult LTSoftwareCryptoHmacSha256_GenHmac(const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE dataLen, u8 *hmac);

void LTSoftwareCrypto_FreePrivateKey(LTPrivateKey *key);
bool LTSoftwareCrypto_GetPrivateKeyByProvisionId(LTDriverCrypto_ProvisionId ProvisionId, LTPrivateKey *privateKey);
u16 LTSoftwareCrypto_GetAesKeyByProvisionId(LTDriverCrypto_ProvisionId provisionId, u8 *key);

LT_EXTERN_C_END
#endif  // LT_SOURCE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTO_H
