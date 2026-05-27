/******************************************************************************
 * <lt/driver/crypto/LTDriverCrypto.h>
 *
 * LTDriverCrypto objects are implemented as the following objects.
 *   - LTSoftwareCrypto objects are implemented in LTDriverCrypto, and created as lt_createobject_typed(LTDriverCryptoXXX, LTSoftwareCryptoXXX).
 *   - LTHardwareCrypto objects are implemented in {Platform}DriverCrypto, and created as lt_createobject_typed(LTDriverCryptoXXX, LTHardwareCryptoXXX).
 *   - LTSecureCrypto objects are implemented in {Platform}DriverCrypto, and created as lt_createobject(LTSecureCryptoXXX).
 *   - LTDriverCryptoEntropy objects are implemented in {Platform}DriverCrypto, and created as lt_createobject(LTDriverCryptoEntropy).
 *
 * Key and certificate management objects are implemented as
 *   - LTDriverCryptoKeyManager is implemented in LTDriverCrypto, and created as lt_createobject(LTDriverCryptoKeyManager), and uses settings and LTSecureKeyManager if needed.
 *   - LTSecureKeyManager is implemented in {Platform}DriverCrypto, and created as lt_createobject(LTSecureKeyManager).
 *   - LTDriverCryptoCertManager is implemented in LTDriverCrypto, and created as lt_createobject(LTDriverCryptoCertManager), and uses settings and LTSecureCertManager if needed.
 *   - LTSecureCertManager is implemented in {Platform}DriverCrypto, and created as lt_createobject(LTSecureCertManager).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef LT_INCLUDE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTO_H
#define LT_INCLUDE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTO_H

#include <lt/LT.h>
#include <lt/system/crypto/LTSystemCrypto.h>

LT_EXTERN_C_BEGIN

enum {
    kLTDriverCrypto_MaxKeySize                = 2048,             // in Bytes
};

/* LTDriverCrypto_ProvisionId defines provisioned key types.
 * These keys are usually persistent and must be protected in device.
 * {Platform}DriverCryptoProvisionedData implements the mapping of the provisioned keys to the keys in device storage.
 */
typedef_LTENUM_SIZED(LTDriverCrypto_ProvisionId, u16) {
    // Symmetric key
    kLTDriverCrypto_ProvisionId_Uds           = 0x0000,          // Unique device secret
    kLTDriverCrypto_ProvisionId_AesKey1       = 0x0001,          // AES key 1, or derived key, platform-specific
    kLTDriverCrypto_ProvisionId_AesKey2       = 0x0002,          // AES key 2, or derived key, platform-specific
    kLTDriverCrypto_ProvisionId_SwupKey,                         // SWUP key, derived from AES key 2, platform-specific
    // Private key
    // kLTDriverCrypto_ProvisionId_IdPrivate     = 0x1000,         // "id", the device private key. NOT STORED, but keep this key ID.
    kLTDriverCrypto_ProvisionId_CommPrivate   = 0x1001,          // "comm", the device communication private key
    kLTDriverCrypto_ProvisionId_AliasPrivate  = 0x1002,          // "alias", the device alias private key
    // Add more key IDs if application has more key to provision.
    kLTDriverCrypto_ProvisionId_Invalid       = LT_U16_MAX,
};

typedef_LTENUM_SIZED(LTDriverCrypto_KeyType, u16) {
    // Symmetric key
    kLTDriverCrypto_KeyType_Key128            = 0x0000,           // 128-bit key
    kLTDriverCrypto_KeyType_Key256            = 0x0001,           // 256-bit key
    // Private key, same as signature algorithms, RFC 8446, Section 4.2.3
    kLTDriverCrypto_KeyType_P256              = SIGNATURE_ECDSA_SECP256R1_SHA256,
    kLTDriverCrypto_KeyType_ED25519           = SIGNATURE_ED25519,
    // More key types
    kLTDriverCrypto_KeyType_Invalid           = LT_U16_MAX,
};

typedef_LTENUM_SIZED(LTDriverCrypto_SecureKeyType, u16) {
    kLTDriverCrypto_SecureKeyType_Plain       = 0,                // Unsecured. Defined but only use if really needed because this enum is for secure key.
    kLTDriverCrypto_SecureKeyType_Provision   = 1,                // Provisioned key
    kLTDriverCrypto_SecureKeyType_Wrapped     = 2,                // Wrapped key
    kLTDriverCrypto_SecureKeyType_KeyId       = 3,                // Internal key id in key storage
    // More secure key types
    kLTDriverCrypto_SecureKeyType_Invalid     = LT_U16_MAX,
};

/* Key reference is used in key management and secure cryptos that do not access keys directly.
 * A device needs to implement the specs of key reference suitable to its hardware key storage and secure cryptos.
 */
typedef struct {
    LTDriverCrypto_SecureKeyType secureKeyType;
    LTDriverCrypto_KeyType       keyType;
    union {
        // For kLTDriverCrypto_SecureKeyType_Provision
        LTDriverCrypto_ProvisionId provisionId;
        // For kLTDriverCrypto_SecureKeyType_Plain and kLTDriverCrypto_SecureKeyType_Wrapped
        struct {
            u32 referenceLen;     // The length of the reference buffer.
            u8  referenceBuf[0];  // The reference buffer, device-specific.
        };
        // For kLTDriverCrypto_SecureKeyType_KeyId
        u32 keyId;
    };
} LTDriverCrypto_KeyReference;

/*******************************************************************************
 * Objects to initialize driver cryptos. This object doesn't perform cryptos. */
typedef_LTObject(LTSoftwareCrypto, 1) {
    void (*GetOptions)(LTSystemCryptoOptions *softwareOptions);
        /**< Get available software crypto options from LTDriverCrypto.
         *   The returned options may include more options than LTHardwareCrypto's GetOptions.
         *
         *   @param[out] softwareOptions  A pointer to an option buffer, cannot be NULL.
         */

} LTOBJECT_API;

typedef_LTObject(LTHardwareCrypto, 1) {
    void (*GetOptions)(LTSystemCryptoOptions *hardwareOptions, LTSystemCryptoOptions *softwareOptions);
        /**< Get available hardware and platform-specific software crypto options.
         *
         *   @param[out] hardwareOptions  A pointer to an option buffer, cannot be NULL.
         *   @param[out] softwareOptions  A pointer to an option buffer, cannot be NULL.
         */
} LTOBJECT_API;

/*******************************************************************************
 * LTSoftwareCrypto and LTHardwareCrypto objects */
// Entropy: only implemented in hardware.
typedef_LTObject(LTDriverCryptoEntropy, 1) {
    bool (*GetEntropy)(u8 *output, LT_SIZE outputLen);
} LTOBJECT_API;

// Random: LTSoftwareCryptoRandom, LTHardwareCryptoRandom
typedef_LTObject(LTDriverCryptoRandom, 1) {
    bool (*GenRandomBytes)(u8 *output, LT_SIZE outputLen);
} LTOBJECT_API;

// SHA1: LTSoftwareCryptoSha1, LTHardwareCryptoSha1
typedef_LTObject(LTDriverCryptoSha1, 1) {
    LTSystemCryptoResult (*GenDigest)(const u8 *data, LT_SIZE dataLen, u8 *digest);
} LTOBJECT_API;

// SHA256: LTSoftwareCryptoSha256, LTHardwareCryptoSha256
typedef_LTObject(LTDriverCryptoSha256, 1) {
    LTSystemCryptoResult (*GenDigest)(const u8 *data, LT_SIZE dataLen, u8 *digest);
} LTOBJECT_API;

// HMAC-SHA1: LTSoftwareCryptoHmacSha1, LTHardwareCryptoHmacSha1
typedef_LTObject(LTDriverCryptoHmacSha1, 1) {
    LTSystemCryptoResult (*GenHmac)(const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE dataLen, u8 *hmac);
} LTOBJECT_API;

// HMAC-SHA256:  LTSoftwareCryptoHmacSha256, LTHardwareCryptoHmacSha256
typedef_LTObject(LTDriverCryptoHmacSha256, 1) {
    LTSystemCryptoResult (*GenHmac)(const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE dataLen, u8 *hmac);
} LTOBJECT_API;

// AES128-CBC: LTSoftwareCryptoAes128Cbc, LTHardwareCryptoAes128Cbc
typedef_LTObject(LTDriverCryptoAes128Cbc, 1) {
    LTSystemCryptoResult (*Encrypt)(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText);
    LTSystemCryptoResult (*Decrypt)(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText);
} LTOBJECT_API;

// AES128-CTR: LTSoftwareCryptoAes128Ctr, LTHardwareCryptoAes128Ctr
typedef_LTObject(LTDriverCryptoAes128Ctr, 1) {
    LTSystemCryptoResult (*Encrypt)(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText);
    LTSystemCryptoResult (*Decrypt)(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText);
} LTOBJECT_API;

// AES128-XTS: LTSoftwareCryptoAes128Xts, LTHardwareCryptoAes128Xts
typedef_LTObject(LTDriverCryptoAes128Xts, 1) {
    LTSystemCryptoResult (*Encrypt)(const u8 key1[AES128_KEY_LENGTH], const u8 key2[AES128_KEY_LENGTH], const u8 iv[AES128_XTS_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText);
    LTSystemCryptoResult (*Decrypt)(const u8 key1[AES128_KEY_LENGTH], const u8 key2[AES128_KEY_LENGTH], const u8 iv[AES128_XTS_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText);
} LTOBJECT_API;

// AES128-GCM: LTSoftwareCryptoAes128Gcm, LTHardwareCryptoAes128Gcm
typedef_LTObject(LTDriverCryptoAes128Gcm, 1) {
    LTSystemCryptoResult (*Encrypt)(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_GCM_IV_LENGTH], const u8 *aad, LT_SIZE aadLen, const u8 *plainText, LT_SIZE textLen, u8 *cipherText, u8 *tag, LT_SIZE tagLen);
    LTSystemCryptoResult (*Decrypt)(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_GCM_IV_LENGTH], const u8 *aad, LT_SIZE aadLen, const u8 *cipherText, LT_SIZE textLen, const u8 *tag, LT_SIZE tagLen, u8 *plainText);
} LTOBJECT_API;

// Sequential SHA1: LTSoftwareCryptoSeqSha1, LTHardwareCryptoSeqSha1
typedef_LTObject(LTDriverCryptoSeqSha1, 1) {
    LT_SHA_CTX * (*CreateSeqSha)(void);
    LT_SHA_CTX * (*CloneSeqSha)(LT_SHA_CTX *ctx);
    void (*DestroySeqSha)(LT_SHA_CTX *ctx);
    LTSystemCryptoResult (*UpdateSeqSha)(LT_SHA_CTX *ctx, const u8 *data, LT_SIZE dataLen);
    LTSystemCryptoResult (*FinishSeqSha)(LT_SHA_CTX *ctx, u8 *digest);
} LTOBJECT_API;

// Sequential SHA256: LTSoftwareCryptoSeqSha256, LTHardwareCryptoSeqSha256
typedef_LTObject(LTDriverCryptoSeqSha256, 1) {
    LT_SHA_CTX * (*CreateSeqSha)(void);
    LT_SHA_CTX * (*CloneSeqSha)(LT_SHA_CTX *ctx);
    void (*DestroySeqSha)(LT_SHA_CTX *ctx);
    LTSystemCryptoResult (*UpdateSeqSha)(LT_SHA_CTX *ctx, const u8 *data, LT_SIZE dataLen);
    LTSystemCryptoResult (*FinishSeqSha)(LT_SHA_CTX *ctx, u8 *digest);
} LTOBJECT_API;

// Sequential HMAC-SHA1: LTSoftwareCryptoSeqHmacSha1, LTHardwareCryptoSeqHmacSha1
typedef_LTObject(LTDriverCryptoSeqHmacSha1, 1) {
    LT_HMAC_CTX * (*CreateSeqHmac)(const u8 *key, LT_SIZE keyLen);
    LT_HMAC_CTX * (*CloneSeqHmac)(LT_HMAC_CTX *ctx);
    void (*DestroySeqHmac)(LT_HMAC_CTX *ctx);
    LTSystemCryptoResult (*UpdateSeqHmac)(LT_HMAC_CTX *hmacHashCtx, const u8 *data, LT_SIZE dataLen);
    LTSystemCryptoResult (*FinishSeqHmac)(LT_HMAC_CTX *hmacHashCtx, u8 *hmac);
} LTOBJECT_API;

// Sequential HMAC-SHA256: LTSoftwareCryptoSeqHmacSha256, LTHardwareCryptoSeqHmacSha256
typedef_LTObject(LTDriverCryptoSeqHmacSha256, 1) {
    LT_HMAC_CTX * (*CreateSeqHmac)(const u8 *key, LT_SIZE keyLen);
    LT_HMAC_CTX * (*CloneSeqHmac)(LT_HMAC_CTX *ctx);
    void (*DestroySeqHmac)(LT_HMAC_CTX *ctx);
    LTSystemCryptoResult (*UpdateSeqHmac)(LT_HMAC_CTX *hmacHashCtx, const u8 *data, LT_SIZE dataLen);
    LTSystemCryptoResult (*FinishSeqHmac)(LT_HMAC_CTX *hmacHashCtx, u8 *hmac);
} LTOBJECT_API;

// Ed25519: LTSoftwareCryptoEd25519, LTHardwareCryptoEd25519
typedef_LTObject(LTDriverCryptoEd25519, 1) {
    LTSystemCryptoResult (*GenPublicKey)(const u8 *privateKey, u8 *publicKey);
    LTSystemCryptoResult (*Sign)(const u8 *privateKey, const u8 *data, LT_SIZE dataLen, u8 *signature, u8 *publicKey);
    LTSystemCryptoResult (*Verify)(const u8 *data, LT_SIZE dataLen, const u8 *signature, const u8 *publicKey);
} LTOBJECT_API;

// Ecdsa-P256: LTSoftwareCryptoEcdsaP256, LTHardwareCryptoEcdsaP256
typedef_LTObject(LTDriverCryptoEcdsaP256, 1) {
    LTSystemCryptoResult (*GenPublicKey)(const u8 *privateKey, u8 *publicKey);
    LTSystemCryptoResult (*SignHash)(const u8 *privateKey, const u8 *hash, u8 *signature, u8 *publicKey);
    LTSystemCryptoResult (*Sign)(const u8 *privateKey, const u8 *data, LT_SIZE dataLen, u8 *signature, u8 *publicKey);
    LTSystemCryptoResult (*VerifyHash)(const u8 *hash, const u8 *signature, const u8 *publicKey);
    LTSystemCryptoResult (*Verify)(const u8 *data, LT_SIZE dataLen, const u8 *signature, const u8 *publicKey);
} LTOBJECT_API;

// X25519: LTSoftwareCryptoX25519, LTHardwareCryptoX25519
typedef_LTObject(LTDriverCryptoX25519, 1) {
    LTSystemCryptoResult (*GenKey)(const u8 *k, const u8 *u, u8 *key);
    const u32* (*GetBasePoint)(void);
} LTOBJECT_API;

/*******************************************************************************
 * Only choose either a hardware secure crypto or a software secure crypto. !!! NOT BOTH !!!
 * Hardware secure cryptos in platform/build/project/{Platform}DriverCrypto.mk
 * Software secure cryptos in platform/build/project/LTSoftwareCrypto.mk */
typedef_LTObject(LTSecureCryptoSha256, 1) {
    LTSystemCryptoResult (*GenDigest)(const LTDriverCrypto_KeyReference *keyReference, const u8 *data, LT_SIZE dataLen, u8 hash[SHA256_HASH_LENGTH]);
    /**< SHA256 with a secure key and data.
     *    SHA256(key | data) -> hash
     *
     *   @param[in]  keyReference  The secure key reference
     *   @param[in]  data          The data
     *   @param[in]  dataLen       The length of data
     *   @param[out] hash          The output hash for the new key
     *   @return  result code
     */
} LTOBJECT_API;

typedef_LTObject(LTSecureCryptoHmacSha256, 1) {
    LTSystemCryptoResult (*GenHmac)(const LTDriverCrypto_KeyReference *keyReference, const u8 *data, LT_SIZE dataLen, u8 hmac[SHA256_HASH_LENGTH]);
    /**< HMAC-SHA256 with a secure key and data.
     *    HMACSHA256(key, data) -> hmac
     *
     *   @param[in]  keyReference  The secure key reference
     *   @param[in]  data          The data
     *   @param[in]  dataLen       The length of data
     *   @param[out] hmac          The output hmac for the new key
     *   @return  result code
     */
} LTOBJECT_API;

typedef_LTObject(LTSecureCryptoEcdsaP256, 1) {
    LTSystemCryptoResult (*GenPublicKey)(const LTDriverCrypto_KeyReference *keyReference, u8 *publicKey);
    LTSystemCryptoResult (*SignHash)(const LTDriverCrypto_KeyReference *keyReference, const u8 *hash, u8 *signature, u8 *publicKey);
    LTSystemCryptoResult (*Sign)(const LTDriverCrypto_KeyReference *keyReference, const u8 *data, LT_SIZE dataLen, u8 *signature, u8 *publicKey);
    /**< ECDSA-P256 with a secure private key and data
     *
     *   @param[in]  keyReference  The secure key reference
     *   @param[in]  data          The data
     *   @param[in]  dataLen       The length of data
     *   @param[out] signature     The output signature
     *   @param[out] publicKey     The output public key, optional
     *   @return  result code
     *   @note  hash is the hash of data to be signed.
     */
    LTSystemCryptoResult (*VerifyHash)(const u8 *hash, const u8 *signature, const u8 *publicKey);
    LTSystemCryptoResult (*Verify)(const u8 *data, LT_SIZE dataLen, const u8 *signature, const u8 *publicKey);
} LTOBJECT_API;

typedef_LTObject(LTSecureCryptoEd25519, 1) {
    LTSystemCryptoResult (*Sign)(const LTDriverCrypto_KeyReference *keyReference, const u8 *data, LT_SIZE dataLen, u8 *signature, u8 *publicKey);
    /**< Ed25519 with a secure private key and data
     *
     *   @param[in]  keyReference  The secure key reference
     *   @param[in]  data          The data
     *   @param[in]  dataLen       The length of data
     *   @param[out] signature     The output signature
     *   @param[out] publicKey     The output public key, optional
     *   @return  result code
     */
    LTSystemCryptoResult (*Verify)(const u8 *data, LT_SIZE dataLen, const u8 *signature, const u8 *publicKey);
    LTSystemCryptoResult (*GenPublicKey)(const u8 *privateKey, u8 *publicKey);
} LTOBJECT_API;

typedef_LTObject(LTSecureCryptoAes128Cbc, 1) {
    LTSystemCryptoResult (*Encrypt)(const LTDriverCrypto_KeyReference *keyReference, const u8 iv[AES128_CBC_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText);
    /**< Encrypt a plaintext, using AES128-CBC and a secure key.
     *   @param[in]  secureKey   The secure key reference
     *   @param[in]  iv          The initial vector (16 bytes)
     *   @param[in]  plainText   The input plaintext
     *   @param[in]  textLen     The length of plaintext/ciphertext in bytes; must multiple of AES128_BLOCK_LENGTH.
     *   @param[out] cipherText  The output ciphertext
     *   @return  Result code.
     */

     LTSystemCryptoResult (*Decrypt)(const LTDriverCrypto_KeyReference *keyReference, const u8 iv[AES128_CBC_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText);
    /**< Decrypt a ciphertext, using AES128-CBC and a secure key.
     *   @param[in]  secureKey   The secure key reference
     *   @param[in]  iv          The initial vector (16 bytes)
     *   @param[in]  cipherText  The input ciphertext
     *   @param[in]  textLen     The length of plaintext/ciphertext in bytes; must multiple of AES128_BLOCK_LENGTH.
     *   @param[out] plainText   The output plaintext
     *   @return  Result code.
     */
} LTOBJECT_API;

/*******************************************************************************
 * Key and certificate management objects */
// Object to manage keys in settings. May also use LTSecureKeyManager if available.
typedef_LTObject(LTDriverCryptoKeyManager, 1) {
    /** Public key management APIs ********************************************/
    bool (*SetPublicKey)(const char *keyName, const LTPublicKey *publicKey, bool bDevice);
    bool (*GetPublicKey)(const char *keyName, LTPublicKey *publicKey);
    /**< Set/get a public key. Not for CA keys.
     *
     *   @param  keyName    The name of key, such as "comm", without setting's prefix.
     *   @param  publicKey  The public key
     *   @param  bDevice    True to save to a device-specific storage if available.

     *   @return  true if the key is saved.
     *            false on failure.
     */

    bool (*SetCaPublicKey)(const char *caName, const LTPublicKey *caPublicKey, bool bDevice);
    bool (*GetCaPublicKey)(const char *caName, LTPublicKey *caPublicKey);
    /**< Set/get a CA public key.
     *
     *   @param  caName       The CA name, such as "ROKUCA", without setting's prefix.
     *   @param  caPublicKey  The public key
     *   @param  bDevice      True to save to a device-specific storage if available.
     *
     *   @return  true if CA key is saved.
     *            false on failure.
     */

    void (*FreePublicKey)(LTPublicKey *publicKey);
    /**< Free internal data (actual key etc) allocated for the publicKey, but not the publicKey itself.
     *   Only if the publicKey is in heap, then call lt_free() to free the publicKey.
    */

    /** Private key management APIs *******************************************/
    bool (*SetPrivateKey)(const char *keyName, const LTPrivateKey *privateKey, bool bDevice);
    /**< Set a private key for a certificate (chain).
     *
     *   @param  keyName     The name of key, such as "comm", without setting's prefix.
     *   @param  privateKey  The private key
     *   @param  bDevice     True to also save to a device-specific storage if available.
     *
     *   @return  true if the private key is saved to only /crypto/private/ when privateKey is not NULL and bDevice is false, or
     *                 if the private key is saved to /crypto/private/ and device-specific storage when privateKey is not NULL and bDevice is true, or
     *                 if no private key is saved when privateKey is NULL or device-specific storage is not available.
     *            false on failure.
     */

     LTDriverCrypto_KeyReference* (*GetPrivateKeyReference)(const char *keyName);
    /**< Get a private key reference.
     *
     *   @param[in] instance  The instance
     *   @param[in] keyName   The name of key
     *   @return  The key reference on success, or NULL on failure.
     *   @note    The returned key reference is allocated in heap.
     *            Call FreeKeyReference() to free the key reference.
     */

    /** Key reference APIs ****************************************************/
    LTDriverCrypto_KeyReference* (*CopyKeyReference)(const LTDriverCrypto_KeyReference *keyReference);
    /**< Copy the key reference, not the referenced data bytes inside. */

    void (*FreeKeyReference)(LTDriverCrypto_KeyReference *keyReference);
    /**< Free the key reference. The referenced key is still in key storage. */

    void (*RemoveKey)(const LTDriverCrypto_KeyReference *keyReference);
    /**< Remove the key from key storage. The key reference is still in heap. */
} LTOBJECT_API;

// Object to manage keys in hardware secure storage.
typedef_LTObject(LTSecureKeyManager, 1) {
    bool (*SetPrivateKey)(const char *keyName, const LTPrivateKey *privateKey);

    LTDriverCrypto_KeyReference* (*SetKey)(const u8 *plainKey, u16 keyLen, LTDriverCrypto_KeyType keyType, LTDriverCrypto_SecureKeyType targetType);
    /**< Secure a key to the target secure type.
     *   @param[in]  plainKey    The input plaintext key.
     *   @param[in]  keyLen      The length of the input key.
     *   @param[in]  keyType     The type of the input key.
     *   @param[in]  targetType  The type of secure key.
     *   @return  A secure key reference on success. Null on failure.
     *   @note    The returned key reference is allocated in heap, and its content is device-dependent.
     *            Call FreeKeyReference() to free the key reference.
     *            Call RemoveKey() to remove the secure key from the key storage (if available in device).
     */

    LTDriverCrypto_KeyReference* (*CopyKeyReference)(const LTDriverCrypto_KeyReference *keyReference);
    /**< Copy the key reference, not the referenced data bytes inside. */

    void (*FreeKeyReference)(LTDriverCrypto_KeyReference *keyReference);
    /**< Free the memory of the key reference. The referenced key is still in key storage. */

    void (*RemoveKey)(const LTDriverCrypto_KeyReference *keyReference);
    /**< Remove the referenced key from key storage. The key reference is still in heap. */
} LTLIBRARY_INTERFACE;

// Object to manage certs in settings. May also use LTSecureCertManager if available.
typedef_LTObject(LTDriverCryptoCertManager, 1) {
    bool (*SetCertificate)(const char *certificateName, const u8 *certificate, u16 certificateLen, bool bChain, bool bDevice);
    bool (*GetCertificate)(const char *certificateName, u8 *certificate, u16 *certificateLen);
    /**< Set/get a certificate (chain).
     *
     *   @param  certificateName  The certificate name, such as "comm", without setting's prefix.
     *   @param  certificate      The certificate in DER
     *   @param  certificateLen   The certificate length
     *   @param  bChain           True if the certificate is a chain.
     *   @param  bDevice          True to also save to a device-specific storage if available.
     *
     *   @return  true if the certificate is saved to only /crypto/cert/ when certificate is not NULL and bDevice is false, or
     *                 if the certificate is saved to /crypto/cert/ and device-specific storage when certificate is not NULL and bDevice is true, or
     *                 if the certificate is copied from /crypto/cert/ to device-specific storage when certificate is NULL and bDevice is true, or
     *                 if no certificate is saved when certificate is NULL or device-specific storage is not available.
     *
     *            false on failure.
     *
     *   @note  GetCertificate always gets the certificate from /crypto/cert/.
     */

} LTOBJECT_API;

// Object to manage certs in hardware secure storage.
typedef_LTObject(LTSecureCertManager, 1) {
    /** Certificate management APIs *******************************************/
    bool (*SetCertificate)(const char *certificateName, const u8 *certificate, u16 certificateLen, bool bChain);
    /**< Set a certificate (chain) in device-specific certificate storage.
     *
     *   @param[in]  certificateName  The certificate name
     *   @param[in]  certificate      The certificate
     *   @param[in]  certificateLen   The certificate length
     *   @param[in]  bChain           True if the certificate is a chain.
     *
     *   @return  true if the certificate is saved.
     *            false on failure.
     */
} LTOBJECT_API;

LT_EXTERN_C_END
#endif // LT_INCLUDE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTO_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  11-Jul-20   gallienus   created */
