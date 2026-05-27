/******************************************************************************
 * include/lt/system/crypto/LTSystemCrypto.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_SYSTEM_CRYPTO_LTSYSTEMCRYPTO_H
#define ROKU_LT_INCLUDE_LT_SYSTEM_CRYPTO_LTSYSTEMCRYPTO_H

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>

#include "LTSystemCryptoDefs.h"

LT_EXTERN_C_BEGIN

/******************************************************************************
 * Enumerate crypto values here.
 * Add crypto methods in both LTSystemCryptoMethod and LTSystemCryptoOptions.
 */

/**
 * Available crypto methods.
 * @ingroup ltsystem_crypto_enum
 */
typedef enum {
    kLTSystemCrypto_Method_Random = 0,    ///< random
    kLTSystemCrypto_Method_SHA1,          ///< hash
    kLTSystemCrypto_Method_SHA256,        ///< hash
    kLTSystemCrypto_Method_HMAC_SHA1,     ///< hmac
    kLTSystemCrypto_Method_HMAC_SHA256,   ///< hmac
    kLTSystemCrypto_Method_AES128_GCM,    ///< symmetric
    kLTSystemCrypto_Method_AES128_CTR,    ///< symmetric
    kLTSystemCrypto_Method_AES128_CBC,    ///< symmetric
    kLTSystemCrypto_Method_AES128_XTS,    ///< symmetric
    kLTSystemCrypto_Method_EdDSA,         ///< signature, ed25519
    kLTSystemCrypto_Method_ECDHE,         ///< key exchange
    kLTSystemCrypto_Method_SeqSHA1,       ///< sequential hash
    kLTSystemCrypto_Method_SeqSHA256,     ///< sequential hash
    kLTSystemCrypto_Method_SeqHMAC_SHA1,  ///< sequential hmac
    kLTSystemCrypto_Method_SeqHMAC_SHA256,///< sequential hmac
    kLTSystemCrypto_Method_ECDSA,         ///< signature, ecdsa-p256-sha256
} LTSystemCryptoMethod;

/**
 * Crypto option bits.
 * @ingroup ltsystem_crypto_struct
 */
typedef union {
    struct {
        u32 enabled       : 1;             ///< 0: no,  1: yes,  default: 0
        u32 random        : 1;             ///< prng
        u32 sha1          : 1;             ///< hash
        u32 sha256        : 1;             ///< hash
        u32 hmacSha1      : 1;             ///< hmac
        u32 hmacSha256    : 1;             ///< hmac
        u32 aes128Gcm     : 1;             ///< symmetric
        u32 aes128Ctr     : 1;             ///< symmetric
        u32 aes128Cbc     : 1;             ///< symmetric
        u32 aes128Xts     : 1;             ///< symmetric
        u32 eddsa         : 1;             ///< signature, ed25519
        u32 ecdhe         : 1;             ///< key exchange
        u32 seqSha1       : 1;             ///< sequential hash, three separated init, update and finish hash functions
        u32 seqSha256     : 1;             ///< sequential hash, three separated init, update and finish hash functions
        u32 seqHmacSha1   : 1;             ///< hmac using sequential hash context
        u32 seqHmacSha256 : 1;             ///< hmac using sequential hash context
        u32 ecdsa         : 1;             ///< signature, ecdsa-p256
    };
    u32 val;  // a helper variable, for now, up to 32 crypto methods

} LTSystemCryptoOptions;

/**
 * Crypto status, enabled or disabled, by hardware or by software.
 * @ingroup ltsystem_crypto_enum
 */
typedef enum {
    kLTSystemCrypto_Status_Disabled          = 0,
    kLTSystemCrypto_Status_Enabled_HW_Only   = 1,
    kLTSystemCrypto_Status_Enabled_SW_Only   = 2,
    kLTSystemCrypto_Status_Enabled_HW_SW     = 3,
} LTSystemCryptoStatus;

/************************** end of crypto enumeration ************************/

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTSystemCrypto, 1);

/**
 * @brief LTSystemCrypto Library Root Interface.
 * Applications shall call crypto in this interface.
 * @ingroup ltsystem_crypto
 */
struct LTSystemCryptoApi {

    INHERIT_LIBRARY_BASE

    void (*GetStatus)(LTSystemCryptoMethod eMethod, LTSystemCryptoStatus *pStatus);
        /**< Get if a crypto method is enabled by hardware or software, or disabled.
         *
         *   @param[in]  eMethod  A crypto method.
         *   @param[out] pStatus  The output status.
         *
         *   @internal
         *   @note
         *       Usage example :
         *       LTSystemCryptoStatus st;
         *       s_pLCrypto->GetStatus(SHA256, &st);
         */

    const LTSystemCryptoConsts* (*GetCryptoConsts)(void);
        /**< Get address to crypto constants */

        void (* GetOptions)(LTSystemCryptoOptions *options);
        /**< Get available crypto options.
         *
         *   @param[out] options  A pointer to an option buffer; cannot be NULL.
         *
         *   @internal
         *   @note
         *       Usage example :
         *       LTSystemCryptoOptions opts;
         *       s_pLCrypto->GetOptions(&opts);
         */

    bool (*GenRandomBytes)(u8 *output, LT_SIZE outputLen);
        /**< Generate a sequence of cryptographically secure random bytes.
         *
         *   @param[out]  output    A pointer to a buffer to receive the random bytes; cannot be NULL.
         *   @param[in]   outputLen The number of random bytes to fill; cannot be 0.
         *   @return true if bytes generated, or false if no bytes.
         *
         *   @internal
         *   @note
         *       Usage example :
         *       u8 rnd[32];
         *       bool ret = s_pLCrypto->GenRandomBytes(rnd, 32);
         */

    LTSystemCryptoResult (*GenDigestSHA1)(const u8 *data, LT_SIZE dataLen, u8 digest[SHA1_HASH_LENGTH]);
        /**< Generate a hash digest of input, using SHA1.
         *
         *   @param[in]  data     A pointer to the input data block; cannot be NULL.
         *   @param[in]  dataLen  The length of data, in bytes; may be 0.
         *   @param[out] digest   The output digest (20 bytes).
         *   @return result code.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null    for data, digest
         *
         *       Usage example :
         *       u8 data[48] = {0x6d,0xd6,0xef,0xd6,0xf6,0xca,0xa6,0x3b,0x72,0x9a,0xa8,0x18,0x6e,0x30,0x8b,0xc1,
         *                      0xbd,0xa0,0x63,0x07,0xc0,0x5a,0x2c,0x0a,0xe5,0xa3,0x68,0x4e,0x6e,0x46,0x08,0x11,
         *                      0x74,0x86,0x90,0xdc,0x2b,0x58,0x77,0x59,0x67,0xcf,0xcc,0x64,0x5f,0xd8,0x20,0x64};
         *       u8 digest[20];
         *       LTSystemCryptoResult ret = s_pLCrypto->GenDigestSHA1(data, sizeof(data), digest);
         */

    LTSystemCryptoResult (*GenDigestSHA256)(const u8 *data, LT_SIZE dataLen, u8 digest[SHA256_HASH_LENGTH]);
        /**< Generate a hash digest of input, using SHA256
         *
         *   @param[in]  data     A pointer to the input data block; cannot be NULL.
         *   @param[in]  dataLen  The length of data in bytes; may be 0.
         *   @param[out] digest   The output digest (32 bytes).
         *   @return result code.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null    for data, digest
         *
         *       Usage example :
         *       u8 data[48] = {0x6d,0xd6,0xef,0xd6,0xf6,0xca,0xa6,0x3b,0x72,0x9a,0xa8,0x18,0x6e,0x30,0x8b,0xc1,
         *                      0xbd,0xa0,0x63,0x07,0xc0,0x5a,0x2c,0x0a,0xe5,0xa3,0x68,0x4e,0x6e,0x46,0x08,0x11,
         *                      0x74,0x86,0x90,0xdc,0x2b,0x58,0x77,0x59,0x67,0xcf,0xcc,0x64,0x5f,0xd8,0x20,0x64};
         *       u8 digest[32];
         *       LTSystemCryptoResult ret = s_pLCrypto->GenDigestSHA256(data, sizeof(data), digest);
         */

    LTSystemCryptoResult (*GenHMACSHA1)(const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE dataLen, u8 hmac[SHA1_HASH_LENGTH]);
        /**< Generate a hash message authentication code of input, using HMAC_SHA1.
         *
         *   @param[in]  key      A pointer to the key; cannot be NULL.
         *   @param[in]  keyLen   The length of key in bytes; may be 0.
         *   @param[in]  data     A pointer to the input data block; cannot be NULL.
         *   @param[in]  dataLen  The length of data in bytes; may be 0.
         *   @param[out] hmac     The output hmac (20 bytes).
         *   @return result code.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null         for key, data, hmac
         *
         *       Recommendation :
         *       keyLen = 32
         *
         *       Usage example :
         *       u8 key[32] = {0x97,0x79,0xd9,0x12,0x06,0x42,0x79,0x7f,0x17,0x47,0x02,0x5d,0x5b,0x22,0xb7,0xac,
         *                     0x97,0x79,0xd9,0x12,0x06,0x42,0x79,0x7f,0x17,0x47,0x02,0x5d,0x5b,0x22,0xb7,0xac};
         *       u8 data[64] = {0xb1,0x68,0x9c,0x25,0x91,0xea,0xf3,0xc9,0xe6,0x60,0x70,0xf8,0xa7,0x79,0x54,0xff,
         *                      0xb8,0x17,0x49,0xf1,0xb0,0x03,0x46,0xf9,0xdf,0xe0,0xb2,0xee,0x90,0x5d,0xcc,0x28,
         *                      0x8b,0xaf,0x4a,0x92,0xde,0x3f,0x40,0x01,0xdd,0x9f,0x44,0xc4,0x68,0xc3,0xd0,0x7d,
         *                      0x6c,0x6e,0xe8,0x2f,0xac,0xea,0xfc,0x97,0xc2,0xfc,0x0f,0xc0,0x60,0x17,0x19,0xd2};
         *       u8 hmac[20];
         *       LTSystemCryptoResult ret = s_pLCrypto->GenHMACSHA1(key, 32, data, 64, hmac);
         */

    LTSystemCryptoResult (*GenHMACSHA256)(const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE dataLen, u8 hmac[SHA256_HASH_LENGTH]);
        /**< Generate a hash message authentication code of input, using HMAC_SHA256.
         *
         *   @param[in]  key      A pointer to the key; cannot be NULL.
         *   @param[in]  keyLen   The length of key in bytes; may be 0.
         *   @param[in]  data     A pointer to the input data block; cannot be NULL.
         *   @param[in]  dataLen  The length of data in bytes; may be 0.
         *   @param[out] hmac     The output hmac (32 bytes).
         *   @return result code.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null         for key, data, hmac
         *
         *       Recommendation :
         *       keyLen = 32
         *
         *       Usage example :
         *       u8 key[32] = {0x97,0x79,0xd9,0x12,0x06,0x42,0x79,0x7f,0x17,0x47,0x02,0x5d,0x5b,0x22,0xb7,0xac,
         *                     0x97,0x79,0xd9,0x12,0x06,0x42,0x79,0x7f,0x17,0x47,0x02,0x5d,0x5b,0x22,0xb7,0xac};
         *       u8 data[64] = {0xb1,0x68,0x9c,0x25,0x91,0xea,0xf3,0xc9,0xe6,0x60,0x70,0xf8,0xa7,0x79,0x54,0xff,
         *                      0xb8,0x17,0x49,0xf1,0xb0,0x03,0x46,0xf9,0xdf,0xe0,0xb2,0xee,0x90,0x5d,0xcc,0x28,
         *                      0x8b,0xaf,0x4a,0x92,0xde,0x3f,0x40,0x01,0xdd,0x9f,0x44,0xc4,0x68,0xc3,0xd0,0x7d,
         *                      0x6c,0x6e,0xe8,0x2f,0xac,0xea,0xfc,0x97,0xc2,0xfc,0x0f,0xc0,0x60,0x17,0x19,0xd2};
         *       u8 hmac[32];
         *       LTSystemCryptoResult ret = s_pLCrypto->GenHMACSHA256(key, 32, data, 64, hmac);
         */

    LTSystemCryptoResult (*EncryptAES128CTR)(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText);
        /**< Encrypt a plaintext, using AES128_CTR.
         *
         *   @param[in]  key         The key (16 bytes).
         *   @param[in]  iv          The initial vector (16 bytes).
         *   @param[in]  plainText   A pointer to the input plaintext; cannot be NULL.
         *   @param[in]  textLen     The length of plaintext/ciphertext in bytes; may be 0.
         *   @param[out] cipherText  A pointer to the output ciphertext; cannot be NULL.
         *   @return result code.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCrypto_Result_OOM
         *       kLTSystemCryptoResult_Null         for key, iv, plainText, cipherText
         *
         *       Recommendation :
         *       textLen = multiple of 16
         *
         *       Usage example :
         *       u8 key[16] = {0xFE,0xFF,0xE9,0x92,0x86,0x65,0x73,0x1C,0x6D,0x6A,0x8F,0x94,0x67,0x30,0x83,0x08};
         *       u8 iv[16]  = {0xCA,0xFE,0xBA,0xBE,0xFA,0xCE,0xDB,0xAD,0xDE,0xCA,0xF8,0x88,0x67,0x30,0x83,0x08};
         *       u8 plainText[64] = {0xD9,0x31,0x32,0x25,0xF8,0x84,0x06,0xE5,0xA5,0x59,0x09,0xC5,0xAF,0xF5,0x26,0x9A,
         *                           0x86,0xA7,0xA9,0x53,0x15,0x34,0xF7,0xDA,0x2E,0x4C,0x30,0x3D,0x8A,0x31,0x8A,0x72,
         *                           0x1C,0x3C,0x0C,0x95,0x95,0x68,0x09,0x53,0x2F,0xCF,0x0E,0x24,0x49,0xA6,0xB5,0x25,
         *                           0xB1,0x6A,0xED,0xF5,0xAA,0x0D,0xE6,0x57,0xBA,0x63,0x7B,0x39,0x1A,0xAF,0xD2,0x55};
         *       u8 cipherText[64];
         *       LTSystemCryptoResult ret = s_pLCrypto->EncryptAES128CTR(key, iv, plainText, 64, cipherText);
         */

    LTSystemCryptoResult (*EncryptAES128CBC)(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CBC_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText);
        /**< Encrypt a plaintext, using AES128_CBC.
         *
         *   @param[in]  key         The key (16 bytes).
         *   @param[in]  iv          The initial vector (16 bytes).
         *   @param[in]  plainText   A pointer to the input plaintext; cannot be NULL.
         *   @param[in]  textLen     The length of plaintext in bytes; must be multiple of AES128_BLOCK_LENGTH.
         *   @param[out] cipherText  A pointer to the output ciphertext; cannot be NULL.
         *   @return result code.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCrypto_Result_OOM
         *       kLTSystemCrypto_Result_WrongLength
         *       kLTSystemCryptoResult_Null         for key, iv, plainText, cipherText
         *
         *       Recommendation :
         *       None
         *
         *       No padding is assumed
         *
         *       Usage example :
         *       u8 key[16] = {0xFE,0xFF,0xE9,0x92,0x86,0x65,0x73,0x1C,0x6D,0x6A,0x8F,0x94,0x67,0x30,0x83,0x08};
         *       u8 iv[16]  = {0xCA,0xFE,0xBA,0xBE,0xFA,0xCE,0xDB,0xAD,0xDE,0xCA,0xF8,0x88,0x67,0x30,0x83,0x08};
         *       u8 plainText[64] = {0xD9,0x31,0x32,0x25,0xF8,0x84,0x06,0xE5,0xA5,0x59,0x09,0xC5,0xAF,0xF5,0x26,0x9A,
         *                           0x86,0xA7,0xA9,0x53,0x15,0x34,0xF7,0xDA,0x2E,0x4C,0x30,0x3D,0x8A,0x31,0x8A,0x72,
         *                           0x1C,0x3C,0x0C,0x95,0x95,0x68,0x09,0x53,0x2F,0xCF,0x0E,0x24,0x49,0xA6,0xB5,0x25,
         *                           0xB1,0x6A,0xED,0xF5,0xAA,0x0D,0xE6,0x57,0xBA,0x63,0x7B,0x39,0x1A,0xAF,0xD2,0x55};
         *       u8 cipherText[64];
         *       LTSystemCryptoResult ret = s_pLCrypto->EncryptAES128CBC(key, iv, plainText, 64, cipherText);
         */
    LTSystemCryptoResult (*EncryptAES128XTS)(const u8 key1[AES128_KEY_LENGTH], const u8 key2[AES128_KEY_LENGTH], const u8 iv[AES128_XTS_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText);
        /**< Encrypt a plaintext, using AES128_XTS.
         *
         *   @param[in]  key1        The data key (16 bytes).
         *   @param[in]  key2        The tweak key (16 bytes).
         *   @param[in]  iv          The initial vector (16 bytes).
         *   @param[in]  plainText   A pointer to the input plaintext; cannot be NULL.
         *   @param[in]  textLen     The length of plaintext/ciphertext in bytes; must be at least AES128_BLOCK_LENGTH.
         *   @param[out] cipherText  A pointer to the output ciphertext; cannot be NULL.
         *   @return result code.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null         for key1, key2, iv, plainText, cipherText
         *
         *       Recommendation :
         *       textLen = multiple of 16
         *
         *       Usage example :
         *       u8 key1[16] = {0xFE,0xFF,0xE9,0x92,0x86,0x65,0x73,0x1C,0x6D,0x6A,0x8F,0x94,0x67,0x30,0x83,0x08};
         *       u8 key2[16] = {0xCA,0xFE,0xBA,0xBE,0xFA,0xCE,0xDB,0xAD,0xDE,0xCA,0xF8,0x88,0x67,0x30,0x83,0x08};
         *       u8 iv[16]  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xAB};
         *       u8 plainText[64] = {0xD9,0x31,0x32,0x25,0xF8,0x84,0x06,0xE5,0xA5,0x59,0x09,0xC5,0xAF,0xF5,0x26,0x9A,
         *                           0x86,0xA7,0xA9,0x53,0x15,0x34,0xF7,0xDA,0x2E,0x4C,0x30,0x3D,0x8A,0x31,0x8A,0x72,
         *                           0x1C,0x3C,0x0C,0x95,0x95,0x68,0x09,0x53,0x2F,0xCF,0x0E,0x24,0x49,0xA6,0xB5,0x25,
         *                           0xB1,0x6A,0xED,0xF5,0xAA,0x0D,0xE6,0x57,0xBA,0x63,0x7B,0x39,0x1A,0xAF,0xD2,0x55};
         *       u8 cipherText[64];
         *       LTSystemCryptoResult ret = s_pLCrypto->EncryptAES128XTS(key1, key2, iv, plainText, 64, cipherText);
         */

    LTSystemCryptoResult (*EncryptAES128GCM)(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_GCM_IV_LENGTH], const u8 *aad, LT_SIZE aadLen, const u8 *plainText, LT_SIZE textLen, u8 *cipherText, u8 *tag, LT_SIZE tagLen);
        /**< Encrypt a plaintext, using AES128_GCM.
         *
         *   @param[in]  key         The key (16 bytes).
         *   @param[in]  iv          The initial vector (12 bytes).
         *   @param[in]  aad         A pointer to additional authentication data; may be NULL.
         *   @param[in]  aadLen      The length of aad in bytes; may be 0, must be shorter than 32.
         *   @param[in]  plainText   A pointer to the input plaintext; cannot be NULL.
         *   @param[in]  textLen     The length of plaintext/ciphertext in bytes; may be 0.
         *   @param[out] cipherText  A pointer to the output ciphertext; cannot be NULL.
         *   @param[out] tag         A pointer to the output authentication tag; cannot be NULL.
         *   @param[in]  tagLen      The length of tag in bytes; must be 12, 13, 14, 15, or 16.
         *   @return result code.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null         for key, iv, plainText, cipherText, tag
         *       kLTSystemCryptoResult_WrongLength  for aadLen, tagLen
         *
         *       Recommendation :
         *       aadLen = 0, 16, 32
         *       tagLen = 16
         *       textLen = multiple of 16
         *
         *       Usage example :
         *       u8 key[16] = {0xFE,0xFF,0xE9,0x92,0x86,0x65,0x73,0x1C,0x6D,0x6A,0x8F,0x94,0x67,0x30,0x83,0x08};
         *       u8 iv[12]  = {0xCA,0xFE,0xBA,0xBE,0xFA,0xCE,0xDB,0xAD,0xDE,0xCA,0xF8,0x88};
         *       u8 aad[20] = {0x3A,0xD7,0x7B,0xB4,0x0D,0x7A,0x36,0x60,0xA8,0x9E,0xCA,0xF3,0x24,0x66,0xEF,0x97,0xF5,0xD3,0xD5,0x85};
         *       u8 plainText[64] = {0xD9,0x31,0x32,0x25,0xF8,0x84,0x06,0xE5,0xA5,0x59,0x09,0xC5,0xAF,0xF5,0x26,0x9A,
         *                           0x86,0xA7,0xA9,0x53,0x15,0x34,0xF7,0xDA,0x2E,0x4C,0x30,0x3D,0x8A,0x31,0x8A,0x72,
         *                           0x1C,0x3C,0x0C,0x95,0x95,0x68,0x09,0x53,0x2F,0xCF,0x0E,0x24,0x49,0xA6,0xB5,0x25,
         *                           0xB1,0x6A,0xED,0xF5,0xAA,0x0D,0xE6,0x57,0xBA,0x63,0x7B,0x39,0x1A,0xAF,0xD2,0x55};
         *       u8 cipherText[64];
         *       u8 tag[16];
         *       LTSystemCryptoResult ret = s_pLCrypto->EncryptAES128GCM(key, iv, aad, 20, plainText, 64, cipherText, tag, 16);
         *       // when no aad
         *       // ret = s_pLCrypto->EncryptAES128GCM(key, iv, NULL, 0, plainText, 64, cipherText, tag, 16);
         */

    LTSystemCryptoResult (*DecryptAES128CTR)(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText);
        /**< Decrypt a ciphertext, using AES128_CTR
         *
         *   @param[in]  key         The key (16 bytes).
         *   @param[in]  iv          The initial vector (16 bytes).
         *   @param[in]  cipherText  A pointer to the input ciphertext; cannot be NULL.
         *   @param[in]  textLen     The length of plaintext/ciphertext in bytes; may be 0.
         *   @param[out] plainText   A pointer to the output plaintext; cannot be NULL.
         *   @return result code.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null         for key, iv, cipherText, plainText
         *
         *       Recommendation :
         *       textLen = multiple of 16
         *
         *       Usage example :
         *       u8 key[16] = {0xFE,0xFF,0xE9,0x92,0x86,0x65,0x73,0x1C,0x6D,0x6A,0x8F,0x94,0x67,0x30,0x83,0x08};
         *       u8 iv[16]  = {0xCA,0xFE,0xBA,0xBE,0xFA,0xCE,0xDB,0xAD,0xDE,0xCA,0xF8,0x88,0x67,0x30,0x83,0x08};
         *       u8 cipherText[64] = {0x42,0x83,0x1E,0xC2,0x21,0x77,0x74,0x24,0x4B,0x72,0x21,0xB7,0x84,0xD0,0xD4,0x9C,
         *                            0xE3,0xAA,0x21,0x2F,0x2C,0x02,0xA4,0xE0,0x35,0xC1,0x7E,0x23,0x29,0xAC,0xA1,0x2E,
         *                            0x21,0xD5,0x14,0xB2,0x54,0x66,0x93,0x1C,0x7D,0x8F,0x6A,0x5A,0xAC,0x84,0xAA,0x05,
         *                            0x1B,0xA3,0x0B,0x39,0x6A,0x0A,0xAC,0x97,0x3D,0x58,0xE0,0x91,0x47,0x3F,0x59,0x85};
         *       u8 plainText[64];
         *       LTSystemCryptoResult ret = s_pLCrypto->DecryptAES128GCM(key, iv, cipherText, 64, plainText);
         */

    LTSystemCryptoResult (*DecryptAES128GCM)(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_GCM_IV_LENGTH], const u8 *aad, LT_SIZE aadLen, const u8 *cipherText, LT_SIZE textLen, const u8 *tag, LT_SIZE tagLen, u8 *plainText);
        /**< Decrypt a ciphertext, using AES128_GCM
         *
         *   @param[in]  key         The key (16 bytes).
         *   @param[in]  iv          The initial vector (12 bytes).
         *   @param[in]  aad         A pointer to an additional authentication data; may be NULL.
         *   @param[in]  aadLen      The length of aad in bytes; may be 0, must be shorter than 32.
         *   @param[in]  cipherText  A pointer to the input ciphertext; cannot be NULL.
         *   @param[in]  textLen     The length of plaintext/ciphertext in bytes; may be 0.
         *   @param[in]  tag         A pointer to the input authentication tag; cannot be NULL.
         *   @param[in]  tagLen      The length of tag in bytes; must be 12, 13, 14, 15, or 16.
         *   @param[out] plainText   A pointer to the output plaintext; cannot be NULL.
         *   @return result code.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null         for key, iv, cipherText, tag, plainText
         *       kLTSystemCryptoResult_WrongLength  for aadLen, tagLen
         *       kLTSystemCryptoResult_WrongVerification  for tag authentication failure (decryption only)
         *
         *       Recommendation :
         *       aadLen = 0, 16, 32
         *       tagLen = 16
         *       textLen = multiple of 16
         *
         *       Usage example :
         *       u8 key[16] = {0xFE,0xFF,0xE9,0x92,0x86,0x65,0x73,0x1C,0x6D,0x6A,0x8F,0x94,0x67,0x30,0x83,0x08};
         *       u8 iv[12] = {0xCA,0xFE,0xBA,0xBE,0xFA,0xCE,0xDB,0xAD,0xDE,0xCA,0xF8,0x88};
         *       u8 aad[20] = {0x3A,0xD7,0x7B,0xB4,0x0D,0x7A,0x36,0x60,0xA8,0x9E,0xCA,0xF3,0x24,0x66,0xEF,0x97,0xF5,0xD3,0xD5,0x85};
         *       u8 cipherText[64] = {0x42,0x83,0x1E,0xC2,0x21,0x77,0x74,0x24,0x4B,0x72,0x21,0xB7,0x84,0xD0,0xD4,0x9C,
         *                            0xE3,0xAA,0x21,0x2F,0x2C,0x02,0xA4,0xE0,0x35,0xC1,0x7E,0x23,0x29,0xAC,0xA1,0x2E,
         *                            0x21,0xD5,0x14,0xB2,0x54,0x66,0x93,0x1C,0x7D,0x8F,0x6A,0x5A,0xAC,0x84,0xAA,0x05,
         *                            0x1B,0xA3,0x0B,0x39,0x6A,0x0A,0xAC,0x97,0x3D,0x58,0xE0,0x91,0x47,0x3F,0x59,0x85};
         *       u8 tag[16] = {0x4D,0x5C,0x2A,0xF3,0x27,0xCD,0x64,0xA6,0x2C,0xF3,0x5A,0xBD,0x2B,0xA6,0xFA,0xB4};
         *       u8 plainText[64];
         *       LTSystemCryptoResult ret = s_pLCrypto->DecryptAES128GCM(key, iv, aad, 20, cipherText, 64, tag, 16, plainText);
         *       // when no aad
         *       // ret = s_pLCrypto->DecryptAES128GCM(key, iv, NULL, 0, cipherText, 64, tag, 16, plainText);
         */

    LTSystemCryptoResult (*DecryptAES128CBC)(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CBC_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText);
        /**< Decrypt a ciphertext, using AES128_CBC
         *
         *   @param[in]  key         The key (16 bytes).
         *   @param[in]  iv          The initial vector (16 bytes).
         *   @param[in]  cipherText  A pointer to the input ciphertext; cannot be NULL.
         *   @param[in]  textLen     The length of plaintext/ciphertext in bytes; must multiple of AES128_BLOCK_LENGTH.
         *   @param[out] plainText   A pointer to the output plaintext; cannot be NULL.
         *   @return result code.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCrypto_Result_OOM
         *       kLTSystemCrypto_Result_WrongLength
         *       kLTSystemCryptoResult_Null         for key, iv, cipherText, plainText
         *
         *       No padding is assumed
         *
         *       Usage example :
         *       u8 key[16] = {0xFE,0xFF,0xE9,0x92,0x86,0x65,0x73,0x1C,0x6D,0x6A,0x8F,0x94,0x67,0x30,0x83,0x08};
         *       u8 iv[16]  = {0xCA,0xFE,0xBA,0xBE,0xFA,0xCE,0xDB,0xAD,0xDE,0xCA,0xF8,0x88,0x67,0x30,0x83,0x08};
         *       u8 cipherText[64] = {0x42,0x83,0x1E,0xC2,0x21,0x77,0x74,0x24,0x4B,0x72,0x21,0xB7,0x84,0xD0,0xD4,0x9C,
         *                            0xE3,0xAA,0x21,0x2F,0x2C,0x02,0xA4,0xE0,0x35,0xC1,0x7E,0x23,0x29,0xAC,0xA1,0x2E,
         *                            0x21,0xD5,0x14,0xB2,0x54,0x66,0x93,0x1C,0x7D,0x8F,0x6A,0x5A,0xAC,0x84,0xAA,0x05,
         *                            0x1B,0xA3,0x0B,0x39,0x6A,0x0A,0xAC,0x97,0x3D,0x58,0xE0,0x91,0x47,0x3F,0x59,0x85};
         *       u8 plainText[64];
         *       LTSystemCryptoResult ret = s_pLCrypto->DecryptAES128CBC(key, iv, cipherText, 64, plainText);
         */

    LTSystemCryptoResult (*DecryptAES128XTS)(const u8 key1[AES128_KEY_LENGTH], const u8 key2[AES128_KEY_LENGTH], const u8 iv[AES128_XTS_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText);
        /**< Decrypt a ciphertext, using AES128_XTS
         *
         *   @param[in]  key1        The data key (16 bytes).
         *   @param[in]  key2        The tweak key (16 bytes).
         *   @param[in]  iv          The initial vector (16 bytes).
         *   @param[in]  cipherText  A pointer to the input ciphertext; cannot be NULL.
         *   @param[in]  textLen     The length of plaintext/ciphertext in bytes; must be at least AES128_BLOCK_LENGTH.
         *   @param[out] plainText   A pointer to the output plaintext; cannot be NULL.
         *   @return result code.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCrypto_Result_OOM
         *       kLTSystemCryptoResult_Null         for key, iv, cipherText, plainText
         *
         *       Recommendation :
         *       textLen = multiple of 16
         *
         *       Usage example :
         *       u8 key1[16] = {0xFE,0xFF,0xE9,0x92,0x86,0x65,0x73,0x1C,0x6D,0x6A,0x8F,0x94,0x67,0x30,0x83,0x08};
         *       u8 key2[16] = {0xCA,0xFE,0xBA,0xBE,0xFA,0xCE,0xDB,0xAD,0xDE,0xCA,0xF8,0x88,0x67,0x30,0x83,0x08};
         *       u8 iv[16]  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xAB}};
         *       u8 cipherText[64] = {0x42,0x83,0x1E,0xC2,0x21,0x77,0x74,0x24,0x4B,0x72,0x21,0xB7,0x84,0xD0,0xD4,0x9C,
         *                            0xE3,0xAA,0x21,0x2F,0x2C,0x02,0xA4,0xE0,0x35,0xC1,0x7E,0x23,0x29,0xAC,0xA1,0x2E,
         *                            0x21,0xD5,0x14,0xB2,0x54,0x66,0x93,0x1C,0x7D,0x8F,0x6A,0x5A,0xAC,0x84,0xAA,0x05,
         *                            0x1B,0xA3,0x0B,0x39,0x6A,0x0A,0xAC,0x97,0x3D,0x58,0xE0,0x91,0x47,0x3F,0x59,0x85};
         *       u8 plainText[64];
         *       LTSystemCryptoResult ret = s_pLCrypto->DecryptAES128XTS(key1, key2, iv, cipherText, 64, plainText);
         */

    LTSystemCryptoResult (*GenEddsaPublicKey)(const u8 privateKey[EdDSA_KEY_LENGTH], u8 publicKey[EdDSA_KEY_LENGTH]);
        /**< Generate public key from private key
         *
         *   @param[in]  privateKey  Private key; cannot be NULL.
         *   @param[out] publicKey   The output public key; cannot be NULL.
         *   @return result code.
         *
         *   @internal
         *   @note
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null          for privateKey, publicKey
         */

    LTSystemCryptoResult (*SignEddsa)(const u8 privateKey[EdDSA_KEY_LENGTH], const u8 *data, LT_SIZE dataLen, u8 signature[EdDSA_SIGNATURE_LENGTH], u8 publicKey[EdDSA_KEY_LENGTH]);
        /**< Sign a block of data to produce a signature.
         *
         *   @param[in]  privateKey  Private key to sign; cannot be NULL.
         *   @param[in]  data        A pointer to the data; cannot be NULL.
         *   @param[in]  dataLen     The length of data in bytes; may be 0.
         *   @param[out] signature   The output signature; cannot be NULL.
         *   @param[out] publicKey   The output public key; cannot be NULL, used later for verifying signature.
         *   @return result code.
         *
         *   @internal
         *   @note
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null          for privateKey, data, signature, publicKey
         *
         *       Usage example :
         *       u8 data[64] = {0xdd,0xaf,0x35,0xa1,0x93,0x61,0x7a,0xba,0xcc,0x41,0x73,0x49,0xae,0x20,0x41,0x31,
         *                      0x12,0xe6,0xfa,0x4e,0x89,0xa9,0x7e,0xa2,0x0a,0x9e,0xee,0xe6,0x4b,0x55,0xd3,0x9a,
         *                      0x21,0x92,0x99,0x2a,0x27,0x4f,0xc1,0xa8,0x36,0xba,0x3c,0x23,0xa3,0xfe,0xeb,0xbd,
         *                      0x45,0x4d,0x44,0x23,0x64,0x3c,0xe8,0x0e,0x2a,0x9a,0xc9,0x4f,0xa5,0x4c,0xa4,0x9f};
         *       u8 privateKey[32] = {0x83,0x3F,0xE6,0x24,0x09,0x23,0x7B,0x9D,0x62,0xEC,0x77,0x58,0x75,0x20,0x91,0x1E,
                                      0x9A,0x75,0x9C,0xEC,0x1D,0x19,0x75,0x5B,0x7D,0xA9,0x01,0xB9,0x6D,0xCA,0x3D,0x42};
         *       u8 signature[64];
         *       u8 publicKey[32];
         *       LTSystemCryptoResult ret = s_pLCrypto->SignEdDSA(privateKey, data, 64, signature, publicKey);
         */

    LTSystemCryptoResult (*VerifyEddsa)(const u8 *data, LT_SIZE dataLen, const u8 signature[EdDSA_SIGNATURE_LENGTH], const u8 publicKey[EdDSA_KEY_LENGTH]);
        /**< Verify a signature of a block of data.
         *
         *   @param[in]  data       A pointer to the data; cannot be NULL.
         *   @param[in]  dataLen    The length of data in bytes; may be 0.
         *   @param[in]  signature  The signature; cannot be NULL.
         *   @param[in]  publicKey  The public key; cannot be NULL.
         *   @return result code.
         *   @retval kLTSystemCrypto_Result_Ok The signature is verified.
         *   @retval kLTSystemCrypto_Result_WrongVerification The signature failed verification.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null          for data, signature, pubKey
         *       kLTSystemCryptoResult_WrongVerification
         *
         *       Usage example :
         *       u8 data[64] = {0xdd,0xaf,0x35,0xa1,0x93,0x61,0x7a,0xba,0xcc,0x41,0x73,0x49,0xae,0x20,0x41,0x31,
         *                      0x12,0xe6,0xfa,0x4e,0x89,0xa9,0x7e,0xa2,0x0a,0x9e,0xee,0xe6,0x4b,0x55,0xd3,0x9a,
         *                      0x21,0x92,0x99,0x2a,0x27,0x4f,0xc1,0xa8,0x36,0xba,0x3c,0x23,0xa3,0xfe,0xeb,0xbd,
         *                      0x45,0x4d,0x44,0x23,0x64,0x3c,0xe8,0x0e,0x2a,0x9a,0xc9,0x4f,0xa5,0x4c,0xa4,0x9f};
         *       u8 signature[64] = {0xdc,0x2a,0x44,0x59,0xe7,0x36,0x96,0x33,0xa5,0x2b,0x1b,0xf2,0x77,0x83,0x9a,0x00,
         *                           0x20,0x10,0x09,0xa3,0xef,0xbf,0x3e,0xcb,0x69,0xbe,0xa2,0x18,0x6c,0x26,0xb5,0x89,
         *                           0x09,0x35,0x1f,0xc9,0xac,0x90,0xb3,0xec,0xfd,0xfb,0xc7,0xc6,0x64,0x31,0xe0,0x30,
         *                           0x3d,0xca,0x17,0x9c,0x13,0x8a,0xc1,0x7a,0xd9,0xbe,0xf1,0x17,0x73,0x31,0xa7,0x04};
         *       u8 publicKey[32] = {0xec,0x17,0x2b,0x93,0xad,0x5e,0x56,0x3b,0xf4,0x93,0x2c,0x70,0xe1,0x24,0x50,0x34,
         *                           0xc3,0x54,0x67,0xef,0x2e,0xfd,0x4d,0x64,0xeb,0xf8,0x19,0x68,0x34,0x67,0xe2,0xbf};
         *       LTSystemCryptoResult ret = s_pLCrypto->VerifyEdDSA(data, 64, signature, publicKey);
         */

    LTSystemCryptoResult (*GenKeyEcdhe)(const u8 k[ECDHE_KEY_LENGTH], const u8 u[ECDHE_KEY_LENGTH], u8 key[ECDHE_KEY_LENGTH]);
        /**< Generate a ECDHE key by X255199(k, u)
         *
         *   @param[in]  k    A scalar; cannot be NULL.
         *   @param[in]  u    A X25519 point; cannot be NULL.
         *   @param[out] key  The resulting key or the resulting X25519 point; cannot be NULL.
         *   @return result code.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null          for k, u, key
         *
         *       Usage example : complete a Diffie-Hellman key exchange
         *       // X25519 base point
         *       u32 u[8] = {0x00000009,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000};
         *       // pri is the private key (usually a random number)
         *       u8 pri[32] = {0x70,0x07,0x6D,0x0A,0x73,0x18,0xA5,0x7D,0x3C,0x16,0xC1,0x72,0x51,0xB2,0x66,0x45,
         *                     0xDF,0x4C,0x2F,0x87,0xEB,0xC0,0x99,0x2A,0xB1,0x77,0xFB,0xA5,0x1D,0xB9,0x2C,0x6A};
         *       u8 x[32];     // the key sent to the other party
         *       LTSystemCryptoResult ret = s_pLCrypto->GenKeyECDHE(pri, (u8 *)u, x)
         *       // now, send x to the other party, and receive y from the other party
         *       u8 y[32];     // the key received from the other party
         *       u8 dhKey[32]; // the key produced by DH
         *       LTSystemCryptoResult ret = s_pLCrypto->GenKeyECDHE(pri, y, dhKey)
         */

        /***************************************************************************
         * The following three Sequential Hash functions must be used in a correct sequence, as shown in the example.
         * If Init succeeds, must call Finish later, even if error occurs in Update.
         *
         * Usage example:
         *       u8 d1[48] = {0x6d,0xd6,0xef,0xd6,0xf6,0xca,0xa6,0x3b,0x72,0x9a,0xa8,0x18,0x6e,0x30,0x8b,0xc1,
         *                    0xbd,0xa0,0x63,0x07,0xc0,0x5a,0x2c,0x0a,0xe5,0xa3,0x68,0x4e,0x6e,0x46,0x08,0x11,
         *                    0x74,0x86,0x90,0xdc,0x2b,0x58,0x77,0x59,0x67,0xcf,0xcc,0x64,0x5f,0xd8,0x20,0x64};
         *       u8 d2[64] = {0xb1,0x68,0x9c,0x25,0x91,0xea,0xf3,0xc9,0xe6,0x60,0x70,0xf8,0xa7,0x79,0x54,0xff,
         *                    0xb8,0x17,0x49,0xf1,0xb0,0x03,0x46,0xf9,0xdf,0xe0,0xb2,0xee,0x90,0x5d,0xcc,0x28,
         *                    0x8b,0xaf,0x4a,0x92,0xde,0x3f,0x40,0x01,0xdd,0x9f,0x44,0xc4,0x68,0xc3,0xd0,0x7d,
         *                    0x6c,0x6e,0xe8,0x2f,0xac,0xea,0xfc,0x97,0xc2,0xfc,0x0f,0xc0,0x60,0x17,0x19,0xd2};
         *       u8 digest[20];
         *       LT_SHA1_CTX *ctx = s_pLCrypto->CreateSeqSHA1();
         *       if (!ctx) ... // handle error
         *       if (s_pLCrypto->UpdateSeqSHA1(&ctx, d1, sizeof(d1)) != kLTSystemCrypto_Result_Ok)
         *           ... // handle error
         *       if (s_pLCrypto->UpdateSeqSHA1(&ctx, d2, sizeof(d2)) != kLTSystemCrypto_Result_Ok)
         *           ... // handle error
         *       if (s_pLCrypto->FinishSeqSHA1(&ctx, digest) != kLTSystemCrypto_Result_Ok)
         *           ... // handle error
         *       s_pLCrypto->DestroySeqSHA1(ctx);
         */
    LT_SHA1_CTX * (*CreateSeqSHA1)(void);
        /**< Create a SHA1 context.
         *
         *   @return Pointer to a SHA1 context or NULL in case of an error
         */

    LT_SHA1_CTX * (*CloneSeqSHA1)(LT_SHA1_CTX *ctx);
        /**< Return a clone of the SHA1 context which can be used independently.
         *   The cloned context needs to be destroyed separately with DestroySeqSHA1().
         *
         *   @param[in]  ctx     A pointer to a SHA1 context.
         *   @return Pointer to the cloned SHA1 context or NULL in case of an error
         */

    void (*DestroySeqSHA1)(LT_SHA1_CTX *ctx);
        /**< Destroy a SHA1 context.
         *
         *   @param[in] Pointer to a SHA1 context.
         */

    LTSystemCryptoResult (*UpdateSeqSHA1)(LT_SHA1_CTX *ctx, const u8 *data, LT_SIZE dataLen);
        /**< Update SHA1.
         *
         *   @param[in]  ctx      A pointer to a SHA1 context.
         *   @param[in]  data     A pointer to the data buffer.
         *   @param[in]  dataLen  The length of data in bytes.
         *   @return result code.
         */

    LTSystemCryptoResult (*FinishSeqSHA1)(LT_SHA1_CTX *ctx, u8 digest[SHA1_HASH_LENGTH]);
        /**< Finish SHA1 and clear ctx.
         *
         *   @param[in]  ctx     A pointer to a SHA1 context.
         *   @param[out] digest  The resulting digest.
         *   @return result code.
         */

        /***************************************************************************
         * The following three Sequential Hash functions must be used in a correct sequence, as shown in the example.
         * If Init succeeds, must call Finish later, even if error occurs in Update.
         *
         * Usage example:
         *       u8 d1[48] = {0x6d,0xd6,0xef,0xd6,0xf6,0xca,0xa6,0x3b,0x72,0x9a,0xa8,0x18,0x6e,0x30,0x8b,0xc1,
         *                    0xbd,0xa0,0x63,0x07,0xc0,0x5a,0x2c,0x0a,0xe5,0xa3,0x68,0x4e,0x6e,0x46,0x08,0x11,
         *                    0x74,0x86,0x90,0xdc,0x2b,0x58,0x77,0x59,0x67,0xcf,0xcc,0x64,0x5f,0xd8,0x20,0x64};
         *       u8 d2[64] = {0xb1,0x68,0x9c,0x25,0x91,0xea,0xf3,0xc9,0xe6,0x60,0x70,0xf8,0xa7,0x79,0x54,0xff,
         *                    0xb8,0x17,0x49,0xf1,0xb0,0x03,0x46,0xf9,0xdf,0xe0,0xb2,0xee,0x90,0x5d,0xcc,0x28,
         *                    0x8b,0xaf,0x4a,0x92,0xde,0x3f,0x40,0x01,0xdd,0x9f,0x44,0xc4,0x68,0xc3,0xd0,0x7d,
         *                    0x6c,0x6e,0xe8,0x2f,0xac,0xea,0xfc,0x97,0xc2,0xfc,0x0f,0xc0,0x60,0x17,0x19,0xd2};
         *       u8 digest[32];
         *       LT_SHA256_CTX *ctx = s_pLCrypto->CreateSeqSHA256();
         *       if (!ctx) ... // handle error
         *       if (s_pLCrypto->UpdateSeqSHA256(&ctx, d1, sizeof(d1)) != kLTSystemCrypto_Result_Ok)
         *           ... // handle error
         *       if (s_pLCrypto->UpdateSeqSHA256(&ctx, d2, sizeof(d2)) != kLTSystemCrypto_Result_Ok)
         *           ... // handle error
         *       if (s_pLCrypto->FinishSeqSHA1(&ctx, digest) != kLTSystemCrypto_Result_Ok)
         *           ... // handle error
         *       s_pLCrypto->DestroySeqSHA256(ctx);
         */
    LT_SHA256_CTX * (*CreateSeqSHA256)(void);
        /**< Create a SHA256 context.
         *
         *   @return Pointer to a SHA256 context or NULL in case of an error
         */

    LT_SHA256_CTX * (*CloneSeqSHA256)(LT_SHA256_CTX *ctx);
        /**< Return a clone of the SHA256 context which can be used independently.
         *   The cloned context needs to be destroyed separately with DestroySeqSHA256().
         *
         *   @param[in]  ctx     A pointer to a SHA256 context.
         *   @return Pointer to the cloned SHA256 context or NULL in case of an error
         */

     void (*DestroySeqSHA256)(LT_SHA256_CTX *ctx);
        /**< Destroy a SHA256 context.
         *
         *   @param[in] Pointer to a SHA256 context
         */

    LTSystemCryptoResult (*UpdateSeqSHA256)(LT_SHA256_CTX *ctx, const u8 *data, LT_SIZE dataLen);
        /**< Update SHA256
         *
         *   @param[in]  ctx      A pointer to a SHA256 context.
         *   @param[in]  data     A pointer to the data buffer.
         *   @param[in]  dataLen  The length of data in bytes.
         *   @return result code.
         */

    LTSystemCryptoResult (*FinishSeqSHA256)(LT_SHA256_CTX *ctx, u8 digest[SHA256_HASH_LENGTH]);
        /**< Finish SHA256 and clear ctx.
         *
         *   @param[in]  ctx     A pointer to a SHA256 context.
         *   @param[out] digest  The resulting digest.
         *   @return result code.
         */

        /***************************************************************************
         * The following three Sequential HMAC functions must be used in a correct sequence, as shown in the example.
         * If Init succeeds, must call Finish later, even if error occurs in Update.
         *
         * Usage example
         *       u8 key[32] = {0x97,0x79,0xd9,0x12,0x06,0x42,0x79,0x7f,0x17,0x47,0x02,0x5d,0x5b,0x22,0xb7,0xac,
         *                     0x97,0x79,0xd9,0x12,0x06,0x42,0x79,0x7f,0x17,0x47,0x02,0x5d,0x5b,0x22,0xb7,0xac};
         *       u8 d1[48] = {0x6d,0xd6,0xef,0xd6,0xf6,0xca,0xa6,0x3b,0x72,0x9a,0xa8,0x18,0x6e,0x30,0x8b,0xc1,
         *                    0xbd,0xa0,0x63,0x07,0xc0,0x5a,0x2c,0x0a,0xe5,0xa3,0x68,0x4e,0x6e,0x46,0x08,0x11,
         *                    0x74,0x86,0x90,0xdc,0x2b,0x58,0x77,0x59,0x67,0xcf,0xcc,0x64,0x5f,0xd8,0x20,0x64};
         *       u8 d2[64] = {0xb1,0x68,0x9c,0x25,0x91,0xea,0xf3,0xc9,0xe6,0x60,0x70,0xf8,0xa7,0x79,0x54,0xff,
         *                    0xb8,0x17,0x49,0xf1,0xb0,0x03,0x46,0xf9,0xdf,0xe0,0xb2,0xee,0x90,0x5d,0xcc,0x28,
         *                    0x8b,0xaf,0x4a,0x92,0xde,0x3f,0x40,0x01,0xdd,0x9f,0x44,0xc4,0x68,0xc3,0xd0,0x7d,
         *                    0x6c,0x6e,0xe8,0x2f,0xac,0xea,0xfc,0x97,0xc2,0xfc,0x0f,0xc0,0x60,0x17,0x19,0xd2};
         *       u8 hmac[20];
         *       LT_HMAC_SHA1_CTX *ctx = s_pLCrypto->CreatetSeqHMACSHA1(key, 32);
         *       if (!ctx) ... // handle error
         *       if (s_pLCrypto->UpdateSeqHMACSHA1(ctx, d1, sizeof(d1)) != kLTSystemCrypto_Result_Ok)
         *           ... // handle error
         *       if (s_pLCrypto->UpdateSeqHMACSHA1(ctx, d2, sizeof(d2)) != kLTSystemCrypto_Result_Ok)
         *           ... // handle error
         *       if (s_pLCrypto->FinishSeqHMACSHA1(ctx, hmac) != kLTSystemCrypto_Result_Ok)
         *           ... // handle error
         *       s_pLCrypto->DestroySeqHMACSHA1(ctx);
         */
    LT_HMAC_SHA1_CTX * (*CreateSeqHMACSHA1)(const u8 *key, LT_SIZE keyLen);
        /**< Create a HMAC-SHA1 context.
         *
         *   @param[in]  key         A pointer to the key; cannot be NULL.
         *   @param[in]  keyLen      The length of key in bytes; may be 0.
         *   @return Pointer to a HMAC-SHA1 context or NULL in case of an error
         */

    LT_HMAC_SHA1_CTX * (*CloneSeqHMACSHA1)(LT_HMAC_SHA1_CTX *ctx);
        /**< Return a clone of the HMAC-SHA1 context which can be used independently.
         *    The cloned context needs to be destroyed separately with DestroySeqHMACSHA1().
         *
         *    @param[in]  ctx     A pointer to a SHA1 context.
         *    @return Pointer to the cloned HMAC-SHA1 context or NULL in case of an error
         */

    void (*DestroySeqHMACSHA1)(LT_HMAC_SHA1_CTX *ctx);
        /**< Destroy a HMAC-SHA1 context.
         *
         *    @param[in] Pointer to a HMAC-SHA1 context
         */

    LTSystemCryptoResult (*UpdateSeqHMACSHA1)(LT_HMAC_SHA1_CTX *hmacHashCtx, const u8 *data, LT_SIZE dataLen);
        /**< Update HMAC-SHA1.
         *
         *   @param[in]  hmacHashCtx A pointer to a HMAC-SHA1 context.
         *   @param[in]  data        A pointer to the data buffer.
         *   @param[in]  dataLen     The length of data in bytes.
         *   @return result code.
         */

    LTSystemCryptoResult (*FinishSeqHMACSHA1)(LT_HMAC_SHA1_CTX *hmacHashCtx, u8 hmac[SHA1_HASH_LENGTH]);
        /**< Finish HMAC-SHA1 and clear hmacHashCtx.
         *
         *   @param[in]  hmacHashCtx  a pointer to a HMAC-SHA1 context
         *   @param[in]  hmac         the resulting HMAC
         *   @return result code.
         */

        /***************************************************************************
         * The following three Sequential HMAC functions must be used in a correct sequence, as shown in the example.
         * If Init succeeds, must call Finish later, even if error occurs in Update.
         *
         * Usage example:
         *       u8 key[32] = {0x97,0x79,0xd9,0x12,0x06,0x42,0x79,0x7f,0x17,0x47,0x02,0x5d,0x5b,0x22,0xb7,0xac,
         *                     0x97,0x79,0xd9,0x12,0x06,0x42,0x79,0x7f,0x17,0x47,0x02,0x5d,0x5b,0x22,0xb7,0xac};
         *       u8 d1[48] = {0x6d,0xd6,0xef,0xd6,0xf6,0xca,0xa6,0x3b,0x72,0x9a,0xa8,0x18,0x6e,0x30,0x8b,0xc1,
         *                    0xbd,0xa0,0x63,0x07,0xc0,0x5a,0x2c,0x0a,0xe5,0xa3,0x68,0x4e,0x6e,0x46,0x08,0x11,
         *                    0x74,0x86,0x90,0xdc,0x2b,0x58,0x77,0x59,0x67,0xcf,0xcc,0x64,0x5f,0xd8,0x20,0x64};
         *       u8 d2[64] = {0xb1,0x68,0x9c,0x25,0x91,0xea,0xf3,0xc9,0xe6,0x60,0x70,0xf8,0xa7,0x79,0x54,0xff,
         *                    0xb8,0x17,0x49,0xf1,0xb0,0x03,0x46,0xf9,0xdf,0xe0,0xb2,0xee,0x90,0x5d,0xcc,0x28,
         *                    0x8b,0xaf,0x4a,0x92,0xde,0x3f,0x40,0x01,0xdd,0x9f,0x44,0xc4,0x68,0xc3,0xd0,0x7d,
         *                    0x6c,0x6e,0xe8,0x2f,0xac,0xea,0xfc,0x97,0xc2,0xfc,0x0f,0xc0,0x60,0x17,0x19,0xd2};
         *       u8 hmac[32];
         *       LT_HMAC_SHA256_CTX *ctx = s_pLCrypto->CreatetSeqHMACSHA256(key, 32);
         *       if (!ctx) ... // handle error
         *       if (s_pLCrypto->UpdateSeqHMACSHA256(ctx, d1, sizeof(d1)) != kLTSystemCrypto_Result_Ok)
         *           ... // handle error
         *       if (s_pLCrypto->UpdateSeqHMACSHA256(ctx, d2, sizeof(d2)) != kLTSystemCrypto_Result_Ok)
         *           ... // handle error
         *       if (s_pLCrypto->FinishSeqHMACSHA256(ctx, hmac) != kLTSystemCrypto_Result_Ok)
         *           ... // handle error
         *       s_pLCrypto->DestroySeqHMACSHA256(ctx);
         */
    LT_HMAC_SHA256_CTX * (*CreateSeqHMACSHA256)(const u8 *key, LT_SIZE keyLen);
        /**< Create a HMAC-SHA256 context.
         *
         *   @param[in]  key         A pointer to the key; cannot be NULL.
         *   @param[in]  keyLen      The length of key in bytes; may be 0.
         *   @return Pointer to a HMAC-SHA256 context or NULL in case of an error
         */

    LT_HMAC_SHA256_CTX * (*CloneSeqHMACSHA256)(LT_HMAC_SHA256_CTX *ctx);
        /**< Return a clone of the HMAC-SHA256 context which can be used independently.
         *    The cloned context needs to be destroyed separately with DestroySeqHMACSHA256().
         *
         *    @param[in]  ctx     A pointer to a SHA256 context.
         *    @return Pointer to the cloned HMAC-SHA256 context or NULL in case of an error
         */

    void (*DestroySeqHMACSHA256)(LT_HMAC_SHA256_CTX *ctx);
        /**< Destroy an HMAC-SHA256 context.
         *
         *    @param[in] Pointer to a HMAC-SHA256 context
         */

    LTSystemCryptoResult (*UpdateSeqHMACSHA256)(LT_HMAC_SHA256_CTX *hmacHashCtx, const u8 *data, LT_SIZE dataLen);
        /**< Update HMAC-SHA256.
         *
         *   @param[in]  hmacHashCtx A pointer to a HMAC-SHA256 context.
         *   @param[in]  data        A pointer to the data buffer.
         *   @param[in]  dataLen     The length of data in bytes.
         *   @return result code.
         */

    LTSystemCryptoResult (*FinishSeqHMACSHA256)(LT_HMAC_SHA256_CTX *hmacHashCtx, u8 hmac[SHA256_HASH_LENGTH]);
        /**< Finish HMAC-SHA256 and clear hmacHashCtx.
         *
         *   @param[in]  hmacHashCtx  a pointer to a HMAC-SHA256 context
         *   @param[in]  hmac         the resulting HMAC
         *   @return result code.
         */

    LTSystemCryptoResult (*GenEcdsaPublicKey)(const u8 privateKey[ECDSA_P256_PRIVATEKEY_LENGTH], u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]);
        /**< Generate public key from private key
         *
         *   @param[in]  privateKey  Private key; cannot be NULL.
         *   @param[out] publicKey   The output public key; cannot be NULL.
         *   @return result code.
         *
         *   @internal
         *   @note
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null          for privateKey, publicKey
         *       kLTSystemCryptoResult_OOM
         *       kLTSystemCrypto_Result_InvalidPoint
         */

    LTSystemCryptoResult (*SignEcdsaHash)(const u8 privateKey[ECDSA_P256_PRIVATEKEY_LENGTH], const u8 hash[SHA256_HASH_LENGTH], u8 signature[ECDSA_P256_SIGNATURE_LENGTH], u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]);
    LTSystemCryptoResult (*SignEcdsa)(const u8 privateKey[ECDSA_P256_PRIVATEKEY_LENGTH], const u8 *data, LT_SIZE dataLen, u8 signature[ECDSA_P256_SIGNATURE_LENGTH], u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]);
        /**< Sign a block of data to produce a signature.
         *
         *   @param[in]  privateKey  Private key to sign; cannot be NULL.
         *   @param[in]  data        A pointer to the data; cannot be NULL.
         *   @param[in]  dataLen     The length of data in bytes; may be 0.
         *   @param[out] signature   The output signature, R || S; cannot be NULL.
         *   @param[out] publicKey   The output public key; If NULL, public key won't be computed.
         *   @return result code.
         *   @note  hash is the hash of data to be signed.
         *
         *   @internal
         *   @note
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null          for privateKey, data, signature
         *       kLTSystemCryptoResult_OOM
         *       kLTSystemCrypto_Result_InvalidPoint
         *
         *       Usage example :
         *       u8 data[64] = {0xdd,0xaf,0x35,0xa1,0x93,0x61,0x7a,0xba,0xcc,0x41,0x73,0x49,0xae,0x20,0x41,0x31,
         *                      0x12,0xe6,0xfa,0x4e,0x89,0xa9,0x7e,0xa2,0x0a,0x9e,0xee,0xe6,0x4b,0x55,0xd3,0x9a,
         *                      0x21,0x92,0x99,0x2a,0x27,0x4f,0xc1,0xa8,0x36,0xba,0x3c,0x23,0xa3,0xfe,0xeb,0xbd,
         *                      0x45,0x4d,0x44,0x23,0x64,0x3c,0xe8,0x0e,0x2a,0x9a,0xc9,0x4f,0xa5,0x4c,0xa4,0x9f};
         *       u8 privateKey[32] = {0x83,0x3F,0xE6,0x24,0x09,0x23,0x7B,0x9D,0x62,0xEC,0x77,0x58,0x75,0x20,0x91,0x1E,
         *                            0x9A,0x75,0x9C,0xEC,0x1D,0x19,0x75,0x5B,0x7D,0xA9,0x01,0xB9,0x6D,0xCA,0x3D,0x42};
         *       u8 signature[64];
         *       u8 publicKey[64];
         *       LTSystemCryptoResult ret = s_pLCrypto->SignEcdsa(privateKey, data, 64, signature, publicKey);
         */

    LTSystemCryptoResult (*VerifyEcdsaHash)(const u8 hash[SHA256_HASH_LENGTH], const u8 signature[ECDSA_P256_SIGNATURE_LENGTH], const u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]);
    LTSystemCryptoResult (*VerifyEcdsa)(const u8 *data, LT_SIZE dataLen, const u8 signature[ECDSA_P256_SIGNATURE_LENGTH], const u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]);
        /**< Verify a signature of a block of data.
         *
         *   @param[in]  data       A pointer to the data; cannot be NULL.
         *   @param[in]  dataLen    The length of data in bytes; may be 0.
         *   @param[in]  signature  The signature, R || S; cannot be NULL.
         *   @param[in]  publicKey  The public key; cannot be NULL.
         *   @return result code.
         *   @retval kLTSystemCrypto_Result_Ok The signature is verified.
         *   @retval kLTSystemCrypto_Result_WrongVerification The signature failed verification.
         *   @note  hash is the hash of data to be verified.
         *
         *   @internal
         *   @note
         *       Possible result code :
         *       kLTSystemCryptoResult_Ok
         *       kLTSystemCryptoResult_Error
         *       kLTSystemCryptoResult_Null          for data, signature, pubKey
         *       kLTSystemCryptoResult_WrongVerification
         *       kLTSystemCryptoResult_OOM
         *       kLTSystemCrypto_Result_InvalidPoint
         *
         *       Usage example :
         *       u8 data[64] = {0xdd,0xaf,0x35,0xa1,0x93,0x61,0x7a,0xba,0xcc,0x41,0x73,0x49,0xae,0x20,0x41,0x31,
         *                      0x12,0xe6,0xfa,0x4e,0x89,0xa9,0x7e,0xa2,0x0a,0x9e,0xee,0xe6,0x4b,0x55,0xd3,0x9a,
         *                      0x21,0x92,0x99,0x2a,0x27,0x4f,0xc1,0xa8,0x36,0xba,0x3c,0x23,0xa3,0xfe,0xeb,0xbd,
         *                      0x45,0x4d,0x44,0x23,0x64,0x3c,0xe8,0x0e,0x2a,0x9a,0xc9,0x4f,0xa5,0x4c,0xa4,0x9f};
         *       u8 signature[64] = {0xdc,0x2a,0x44,0x59,0xe7,0x36,0x96,0x33,0xa5,0x2b,0x1b,0xf2,0x77,0x83,0x9a,0x00,
         *                           0x20,0x10,0x09,0xa3,0xef,0xbf,0x3e,0xcb,0x69,0xbe,0xa2,0x18,0x6c,0x26,0xb5,0x89,
         *                           0x09,0x35,0x1f,0xc9,0xac,0x90,0xb3,0xec,0xfd,0xfb,0xc7,0xc6,0x64,0x31,0xe0,0x30,
         *                           0x3d,0xca,0x17,0x9c,0x13,0x8a,0xc1,0x7a,0xd9,0xbe,0xf1,0x17,0x73,0x31,0xa7,0x04};
         *       u8 publicKey[64] = {0x1c,0xcb,0xe9,0x1c,0x07,0x5f,0xc7,0xf4,0xf0,0x33,0xbf,0xa2,0x48,0xdb,0x8f,0xcc,
         *                           0xd3,0x56,0x5d,0xe9,0x4b,0xbf,0xb1,0x2f,0x3c,0x59,0xff,0x46,0xc2,0x71,0xbf,0x83,
         *                           0xce,0x40,0x14,0xc6,0x88,0x11,0xf9,0xa2,0x1a,0x1f,0xdb,0x2c,0x0e,0x61,0x13,0xe0,
         *                           0x6d,0xb7,0xca,0x93,0xb7,0x40,0x4e,0x78,0xdc,0x7c,0xcd,0x5c,0xa8,0x9a,0x4c,0xa9};
         *       LTSystemCryptoResult ret = s_pLCrypto->VerifyEcdsa(data, 64, signature, publicKey);
         */

};

// Encoders
typedef enum {
    kLTSystemCryptoEncoding_Certificate_PEM,
    kLTSystemCryptoEncoding_Certificate_DER,
    kLTSystemCryptoEncoding_Certificate_LT,         // LT's TLS encoding: Cert length (3 Bytes big endian) + DER cert + "\x00\x00"
    kLTSystemCryptoEncoding_PrivateKey_PEM,
    kLTSystemCryptoEncoding_PrivateKey_DER,
    kLTSystemCryptoEncoding_PrivateKey_LT,          // LTPrivateKey
    kLTSystemCryptoEncoding_PublicKey_PEM,
    kLTSystemCryptoEncoding_PublicKey_DER,
    kLTSystemCryptoEncoding_PublicKey_LT,           // LTPublicKey
    kLTSystemCryptoEncoding_Signature_X962,
    kLTSystemCryptoEncoding_Signature_LT,           // R+S raw bytes
    // Add more here
} LTSystemCryptoEncoding;

typedef_LTObject(LTSystemCryptoEncoder, 1) {
    bool (*EncodeEcdsaSignature)(const u8 *signature, u16 sigLen, u8 *x962Signature, u16 x962MaxLen, u16 *x962Len);
    /**< Encode ecdsa signature from R+S only to X9.62.
     *
     *   @param[in]  signature      The signature of R+S, cannot be NULL.
     *   @param[in]  sigLen         The length of signature, in bytes, must be an even number, but cannot be 0.
     *   @param[out] x962Signature  The signature in X9.62 format, cannot be NULL.
     *   @param[in]  x962MaxLen     The max length of x962Signature, in bytes, i.e. the buffer size.
     *   @param[out] x962Len        The output length of x962Signature, in bytes, cannot be NULL.
     *   @return  true if encode succeeds.
     *            false on failure.
     */

    bool (*DecodeEcdsaSignature)(const u8 *x962Signature, u16 x962Len, u8 *signature, u16 sigLen);
    /**< Decode ecdsa signature from X9.62 format to R+S only.
     *   If signature is NULL, only check if the x962Signature is valid.
     *
     *   @param[in]  instance       This instance.
     *   @param[in]  x962Signature  The signature in X9.62 format, cannot be NULL.
     *   @param[in]  x962Len        The length of x962Signature, in bytes, cannot be 0.
     *   @param[out] signature      The signature of R+S, could be NULL.
     *   @param[in]  sigLen         The length of signature, in bytes, must be a even number, but cannot be 0.
     *   @return  true if signature is valid.
     *            false on failure.
     */

    u32 (*ConvertDerToPem)(LTSystemCryptoEncoding type, const u8 *derData, u32 derLength, char *pemData, u32 pemLength);
    /**< Convert DER data to PEM.
     *   If derData is certificate, it is one single certificate, not a certificate chain.
     *
     *   @param[in]  instance   This instance.
     *   @param[in]  type       The type of DER data: certificate or key.
     *   @param[in]  derData    The input DER data. Cannot be NULL.
     *   @param[in]  derLength  The length of DER data. Cannot be 0.
     *   @param[out] pemData    The output PEM data.
     *   @param[in]  pemLength  The max length of output buffer, including '\0'.
     *   @return  lt_strlen() of PEM. '\0' at the end of PEM is not counted in the returned length.
     *            On fail, 0.
     *
     *   @note  This API assumes the output buffer is large enough to hold all data.
     *          To avoid buffer overflow, call ConvertDerToPem(pemData=NULL) to determine the output buffer length.
     *          When pemData is NULL, this API returns the required PEM buffer length, including '\0'.
     */

    // TODO: u32 (*ConvertPemToDer)(LTSystemCryptoEncoding type, const char *pemData, u32 pemLength, u8 *derData, u32 derLength);

    u32 (*EncodePrivateKeyToPem)(const LTPrivateKey *privateKey, char **pemData);
    /**< Encode a private key to PEM.
     *
     *   @param[in]  instance    This instance.
     *   @param[in]  privateKey  The private key in LT format. Cannot be NULL.
     *   @param[out] pemData     The pointer to set the PEM data. Cannot be NULL.
     *   @return  lt_strlen() of PEM. '\0' at the end of PEM is not counted in the returned length.
     *            On fail, 0.
     *
     *   @note  This API allocates PEM in heap and sets pemData to the PEM data, i.e. *pemData = PEM in heap.
     *          After use of PEM, must free *pemData to avoid memory leak.
     */

    u32 (*EncodeCertChainToPem)(const u8 *certChain, u32 certLength, char **pemData);
    /**< Encode a certificate chain to PEM.
     *
     *   @param[in]  instance    This instance.
     *   @param[in]  certChain   The certificate chain in TLS format. Cannot be NULL.
     *   @param[in]  certLength  The length of certificate chain. Cannot be 0.
     *   @param[out] pemData     The pointer to set the PEM data. Cannot be NULL.
     *   @return  lt_strlen() of PEM. '\0' at the end of PEM is not counted in the returned length.
     *            On fail, 0.
     *
     *   @note  This API allocates PEM in heap and sets pemData to the PEM data, i.e. *pemData = PEM in heap.
     *          After use of PEM, must free *pemData to avoid memory leak.
     *
     *          To encode a single DER certificate, call ConvertDerToPem.
     */
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* ROKU_LT_INCLUDE_LT_SYSTEM_CRYPTO_LTSYSTEMCRYPTO_H */
