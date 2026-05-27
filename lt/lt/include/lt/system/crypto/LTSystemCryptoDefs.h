/******************************************************************************
 * include/lt/system/crypto/LTSystemCryptoDefs.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_SYSTEM_LTSYSTEMCRYPTODEFS_H
#define ROKU_LT_INCLUDE_LT_SYSTEM_LTSYSTEMCRYPTODEFS_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/******************************************************************************
 * Macros used by each specific crypto function.
 */

/**
 * Crypto API return codes.
 * @ingroup ltsystem_crypto_enum
 */
typedef enum {
    kLTSystemCrypto_Result_Ok                =  0,    ///< success
    kLTSystemCrypto_Result_Error             = -1,    ///< unspecified error
    kLTSystemCrypto_Result_Null              = -2,    ///< null pointer or zero length
    kLTSystemCrypto_Result_WrongLength       = -3,    ///< not in a range or match a required length
    kLTSystemCrypto_Result_WrongVerification = -4,    ///< wrong verification result
    kLTSystemCrypto_Result_NeedToReseed      = -5,    ///< need to reseed
    kLTSystemCrypto_Result_OOM               = -6,    ///< out of memory
    kLTSystemCrypto_Result_InvalidPoint      = -7,    ///< invalid curve point
    kLTSystemCrypto_Result_EndOfStream       = -8,    ///< Crypto function was finalised and cannot accept new operations
    kLTSystemCrypto_Result_Busy              = -9,    ///< hardware crypto is currently in use - try again later or use SW crypto
    kLTSystemCrypto_Result_Timeout           = -10,   ///< hardware crypto timeout
    kLTSystemCrypto_Result_HwError           = -11,   ///< hardware crypto error
    kLTSystemCrypto_Result_NoSupport         = -12,   ///< unsupported crypto
} LTSystemCryptoResult;

#define LTSYSTEMCRYPTO_RSA512LEN              (16)         // in u32
#define LTSYSTEMCRYPTO_RSA1024LEN             (32)         // in u32
#define LTSYSTEMCRYPTO_RSA2048LEN             (64)         // in u32

#define LTSYSTEMCRYPTO_BYTES_PER_U32           (4)
#define LTSYSTEMCRYPTO_BITS_PER_U32           (32)
#define LTSYSTEMCRYPTO_BYTES_PER_U256         (32)
#define LTSYSTEMCRYPTO_U32_PER_U256            (8)

typedef u32 u256[LTSYSTEMCRYPTO_U32_PER_U256];

// sha1
#define SHA1_BLOCK_LENGTH                     (64)    // in bytes
#define SHA1_HASH_LENGTH                      (20)    // in bytes
#define SHA1_STATE_LENGTH                      (5)    // in u32

/**
 * Context of SHA1.
 * @ingroup ltsystem_crypto_struct
 */
typedef struct LT_SHA1_CTX_impl LT_SHA1_CTX;

// sha256
#define SHA256_BLOCK_LENGTH                   (64)    // in bytes
#define SHA256_HASH_LENGTH                    (32)    // in bytes
#define SHA256_STATE_LENGTH                    (8)    // in u32

/**
 * Context of SHA256.
 * @ingroup ltsystem_crypto_struct
 */
typedef struct LT_SHA256_CTX_impl LT_SHA256_CTX;

// hmac_sha1
#define HMAC_SHA1_KEY_MAX_LENGTH              (64)    // the max length of hmac_sha1 key, in bytes

/**
 * Context of HMAC SHA1.
 * @ingroup ltsystem_crypto_struct
 */
typedef struct LT_HMAC_SHA1_CTX_impl LT_HMAC_SHA1_CTX;

// hmac_sha256
#define HMAC_SHA256_KEY_MAX_LENGTH            (64)    // the max length of hmac_sha256 key, in bytes

/**
 * Context of HMAC SHA256.
 * @ingroup ltsystem_crypto_struct
 */
typedef struct LT_HMAC_SHA256_CTX_impl LT_HMAC_SHA256_CTX;

typedef void LT_SHA_CTX;
typedef void LT_HMAC_CTX;

// sha512, not offered as an API yet, but needed by EdDSA
#define SHA512_BLOCK_LENGTH                  (128)    // in bytes
#define SHA512_HASH_LENGTH                    (64)    // in bytes
#define SHA512_STATE_LENGTH                    (8)    // in u64

// drbg, 10.1, Table 2, with sha256
#define RESEED_INTERVAL                  (0x10000)    // 2^16. In the table, 2^48, maybe too long?
#define MAX_BYTE_COUNT_PER_REQUEST        (0x1000)    // 2^12. In the table, 2^16, maybe too large for LT?
#define HASH_OUTPUT_LENGTH    (SHA256_HASH_LENGTH)    // in bytes, 256 b
#define HASH_BLOCK_LENGTH    (SHA256_BLOCK_LENGTH)    // in bytes, 512 b
#define SECURITY_STRENGTH                     (16)    // in bytes, 128 b
#define SEED_LENGTH_BYTE                      (55)    // in bytes, 440 b
#define SEED_LENGTH_U32                     (56/4)    // in u32 (4 bytes), 448 b
#define MAX_INPUT_STRING_LENGTH               (64)    // in bytes, maximum length for person_string and addition_string

// aes128_gcm, 5.2.1.1
#define AES128_KEY_LENGTH                     (16)    // in bytes, the length of AES128 key
#define AES128_STATE_LENGTH                   (16)    // in bytes, the length of AES128 state
#define AES128_BLOCK_LENGTH                   (16)    // in bytes, the length of AES128 data block
#define AES128_GCM_IV_LENGTH                  (12)    // in bytes, the length of IV
#define AES128_GCM_MAX_AAD_LENGTH             (32)    // in bytes, maximum length for AAD
#define AES128_GCM_MIN_TAG_LENGTH             (12)    // in bytes, minimum length for TAG
#define AES128_GCM_MAX_TAG_LENGTH             (16)    // in bytes, maximum length for TAG

#define AES128_CTR_IV_LENGTH                  (16)    // in bytes, the length of IV
#define AES128_CBC_IV_LENGTH                  (16)    // in bytes, the length of IV
#define AES128_XTS_IV_LENGTH                  (16)    // in bytes, the length of IV

// eddsa, ed25519
#define EdDSA_KEY_LENGTH                      (LTSYSTEMCRYPTO_BYTES_PER_U256)        // in bytes
#define EdDSA_SIGNATURE_LENGTH                (LTSYSTEMCRYPTO_BYTES_PER_U256 * 2)    // in bytes

// ecdhe
#define ECDHE_KEY_LENGTH                      (LTSYSTEMCRYPTO_BYTES_PER_U256)        // in bytes

// ecdsa, p256
#define ECDSA_P256_PRIVATEKEY_LENGTH          (LTSYSTEMCRYPTO_BYTES_PER_U256)        // in bytes
#define ECDSA_P256_PUBLICKEY_LENGTH           (LTSYSTEMCRYPTO_BYTES_PER_U256 * 2)    // in bytes
#define ECDSA_P256_SIGNATURE_LENGTH           (LTSYSTEMCRYPTO_BYTES_PER_U256 * 2)    // in bytes
#define ECDSA_P256_SIGNATURE_X962_MAX_LENGTH  (72)                                   // in bytes, 30,46,0221...0221...

/* Public key, including a public key and an optional serial number */
typedef struct LTPublicKey {
    u16 type;                                  // TLS 1.3 signature type, 0x0807 for ed25519, 0x0403 for ecdsa_secp256r1_sha256
    u16 keyLen;                                // length of public key, in bytes.
    u8 *key;                                   // public key
    u16 keyIdLen;                              // length of key id, in bytes. In X509, it is 20.
    u8 *keyId;                                 // key identifier in X509 extension. If not available, set to NULL. In X509, it is SHA1 of the public key
} LTPublicKey;

typedef struct LTPrivateKey {
    u16 type;                                  // TLS 1.3 signature type, 0x0807 for ed25519, 0x0403 for ecdsa_secp256r1_sha256
    u16 keyLen;                                // length of private key, in bytes.
    u8 *key;                                   // private key
} LTPrivateKey;

/* Affine coordinates of Ed25519 point */
typedef struct EdPoint {
    u256 x;
    u256 y;
} EdPoint;

/* Affine coordinates of P256 point */
typedef struct P256Point {
    u256 x;
    u256 y;
    bool inf;  // true if the point is infinity.
} P256Point;

/* Supported signature algorithms, RFC 8446, Section 4.2.2 */
#define SIGNATURE_ECDSA_SECP256R1_SHA256              0x0403
#define SIGNATURE_ED25519                             0x0807

/* Crypto constants needed by applications */
typedef struct LTSystemCryptoConsts {
    const u32 *kX25519_BP;   // X25519 base point (generator), {0x00000009,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000};
} LTSystemCryptoConsts;

LT_EXTERN_C_END
#endif // ROKU_LT_INCLUDE_LT_SYSTEM_LTSYSTEMCRYPTODEFS_H