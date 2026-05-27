/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/Esp32DriverCrypto.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_CRYPTO_ESP32DRIVERCRYPTO_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_CRYPTO_ESP32DRIVERCRYPTO_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

#include <lt/core/LTTime.h>
#include <lt/core/LTStdlib.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include "esp32/Esp32_Registers.h"

#define HAL_SWAP16(d) __builtin_bswap16((d))
#define HAL_SWAP32(d) __builtin_bswap32((d))
#define HAL_SWAP64(d) __builtin_bswap64((d))

#define ESP32_TIMEOUT_SHORT  (LTTimeInitializer_Microseconds(100))     /* For SHA and AES, engine should complete in 10 us */
#define ESP32_TIMEOUT_LONG   (LTTimeInitializer_Microseconds(100000))  /* For RSA, engine should complete in 20 ms. \
                                                                          But, if 2048-bit, engine needs 80 ms to complete one RSA operation. */

typedef struct ESP32_RSA_CTX {
    const u32 *R2;          // point to a static address holding R*R % N
    u8         dataLen;     // length of data, in u32, dataLen <= nRSALen
    u8         RSALen;      // length of RSA register, in u32, support at most 4096-bit
} ESP32_RSA_CTX;

/* Enable and disable SHA engine. */
void ESP32_SHA_Enable(void);
void ESP32_SHA_Disable(void);

/* Enable and disable AES engine. */
void ESP32_AES_Enable(void);
void ESP32_AES_Disable(void);

/* Enable and disable RSA engine. */
bool ESP32_RSA_Enable(void);
void ESP32_RSA_Disable(void);

void Esp32DriverCrypto_LockShaMutex(void);
void Esp32DriverCrypto_UnlockShaMutex(void);
void Esp32DriverCrypto_LockAesMutex(void);
void Esp32DriverCrypto_UnlockAesMutex(void);

/* Overlapping structure for use with either software or hardware hash */
struct LT_SHA256_CTX_impl {
    union {
        u64 DATALen;                      // total data length, in Bytes
        u32 dataLen[2];
    };
    u8  data[SHA256_BLOCK_LENGTH];        // buffered data to make a full block 64 Bytes, big endian, must be aligned with u32
    struct {
        union {
            u32 d[SHA256_BLOCK_LENGTH / 4];
            u32 h[SHA256_HASH_LENGTH / 4];
        };
    };
    bool bFirst;                          // true if first data block
};

/**
 * @brief  Initialize SHA256 context
 * @param  ctx  the context
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA256_Init(LT_SHA256_CTX *ctx);

/**
 * @brief  Hash input data and update internal state, block by block
 * @param  ctx   the context
 * @param  data  the input data
 * @param  len   the length of data
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA256_Update(LT_SHA256_CTX *ctx, const u8 *data, LT_SIZE len);

/**
 * @brief  Finish hash and get the digest
 * @param  ctx     the context
 * @param  digest  the output digest
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA256_Finish(LT_SHA256_CTX *ctx, u8 digest[SHA256_HASH_LENGTH]);

/**
 * @brief  Hash data and get the digest
 * @param  data    the input data block
 * @param  den     the length of data
 * @param  digest  the output digest, 32 Bytes
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA256_Digest(const u8 *data, LT_SIZE len, u8 digest[SHA256_HASH_LENGTH]);

// Context of SHA512
typedef struct ESP32_SHA512_CTX {
    u64 dataLen[2];                   // total data length, in Bytes
    u8  data[SHA512_BLOCK_LENGTH];    // buffered data to make a full block 64 Bytes, big endian
    // temporary buffer for computation
    struct {
        union {
            u32 d[SHA512_BLOCK_LENGTH / 4];
            u32 h[SHA512_HASH_LENGTH / 4];
        };
        u32 left;
        u32 fill;
        u64 l;
    };
    bool bFirst;                      // true if first data block
} ESP32_SHA512_CTX;

/**
 * @brief  Initialize SHA512 context
 * @param  ctx  the context
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA512_Init(ESP32_SHA512_CTX *ctx);

/**
 * @brief  Hash input data and update internal state, block by block
 * @param  ctx   the context
 * @param  data  the input data
 * @param  len   the length of data
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA512_Update(ESP32_SHA512_CTX *ctx, const u8 *data, LT_SIZE len);

/**
 * @brief  Finish hash and get the digest
 * @param  ctx     the context
 * @param  digest  the output digest, 64 Bytes
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA512_Finish(ESP32_SHA512_CTX *ctx, u8 digest[SHA512_HASH_LENGTH]);

/**
 * @brief  Hash data and get the digest
 * @param  data    the input data block
 * @param  len     the length of data
 * @param  digest  the output digest, 64 Bytes
 * @return result code
 */
LTSystemCryptoResult ESP32_SHA512_Digest(const u8 *data, LT_SIZE len, u8 digest[SHA512_HASH_LENGTH]);

LT_EXTERN_C_END
#endif // PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_CRYPTO_ESP32DRIVERCRYPTO_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  16-May-22   gallienus   created
 */

