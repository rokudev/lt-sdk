/*******************************************************************************
 * source/lt/system/crypto/LTSystemCrypto.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>

DEFINE_LTLOG_SECTION("sys.crypt")
#define P(...)

// HW Crypto
static LTHardwareCrypto     *s_hwCrypto  = NULL;
static LTSystemCryptoOptions s_hwCryptoOptions = {0};
// SW Crypto
static LTSoftwareCrypto     *s_swCrypto = NULL;
static LTSystemCryptoOptions s_swCryptoOptions = {0};
// Crypto constants for applications to use
static LTSystemCryptoConsts  s_kCryptoConsts = {0};

/* Load crypto objects to improve performance, and prefer hw crypto over sw crypto.
 * Hw-based cryptos may fall back to sw crypto IF implemented in a specific platform.
 */
static struct {
    LTDriverCryptoRandom        *random;
    LTDriverCryptoSha1          *sha1;
    LTDriverCryptoSha256        *sha256;
    LTDriverCryptoSeqSha1       *seqSha1;
    LTDriverCryptoSeqSha256     *seqSha256;
    LTDriverCryptoHmacSha1      *hmacSha1;
    LTDriverCryptoHmacSha256    *hmacSha256;
    LTDriverCryptoSeqHmacSha1   *seqHmacSha1;
    LTDriverCryptoSeqHmacSha256 *seqHmacSha256;
    LTDriverCryptoAes128Cbc     *aes128Cbc;
    LTDriverCryptoAes128Ctr     *aes128Ctr;
    LTDriverCryptoAes128Gcm     *aes128Gcm;
    LTDriverCryptoAes128Xts     *aes128Xts;
    LTDriverCryptoEcdsaP256     *ecdsaP256;
    LTDriverCryptoEd25519       *ed25519;
    LTDriverCryptoX25519        *x25519;
} s_loadedCryptos;

static void LogWarningNoSupport(char *cryptoName) {
    LTLOG_YELLOWALERT("no.support", "%s", cryptoName);
}

/*******************************************************************************
 * Crypto functions, descriptions in lt/system/crypto/LTSystemCrypto.h
 */
static void LTSystemCrypto_GetOptions(LTSystemCryptoOptions *options) {
    if (options) {
        options->val = (s_hwCryptoOptions.enabled ? s_hwCryptoOptions.val : 0) | (s_swCryptoOptions.enabled ? s_swCryptoOptions.val : 0);
    }
}

static bool LTSystemCrypto_GenRandomBytes(u8 *output, LT_SIZE outputLen) {
    if (s_loadedCryptos.random) return s_loadedCryptos.random->API->GenRandomBytes(output, outputLen);

    LogWarningNoSupport("Random");
    return false;
}

static LTSystemCryptoResult LTSystemCrypto_GenDigestSHA1(const u8 *data, LT_SIZE dataLen, u8 digest[SHA1_HASH_LENGTH]) {
    if (s_loadedCryptos.sha1) return s_loadedCryptos.sha1->API->GenDigest(data, dataLen, digest);

    LogWarningNoSupport("Sha1");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_GenDigestSHA256(const u8 *data, LT_SIZE dataLen, u8 digest[SHA256_HASH_LENGTH]) {
    if (s_loadedCryptos.sha256) return s_loadedCryptos.sha256->API->GenDigest(data, dataLen, digest);

    LogWarningNoSupport("Sha256");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_GenHMACSHA1(const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE dataLen, u8 hmac[SHA1_HASH_LENGTH]) {
    if (s_loadedCryptos.hmacSha1) return s_loadedCryptos.hmacSha1->API->GenHmac(key, keyLen, data, dataLen, hmac);

    LogWarningNoSupport("HmacSha1");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_GenHMACSHA256(const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE dataLen, u8 hmac[SHA256_HASH_LENGTH]) {
    if (s_loadedCryptos.hmacSha256) return s_loadedCryptos.hmacSha256->API->GenHmac(key, keyLen, data, dataLen, hmac);

    LogWarningNoSupport("HmacSha256");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_EncryptAES128GCM(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_GCM_IV_LENGTH], const u8 *aad, LT_SIZE aadLen, const u8 *plainText, LT_SIZE textLen, u8 *cipherText, u8 *tag, LT_SIZE tagLen) {
    if (s_loadedCryptos.aes128Gcm) return s_loadedCryptos.aes128Gcm->API->Encrypt(key, iv, aad, aadLen, plainText, textLen, cipherText, tag, tagLen);

    LogWarningNoSupport("Aes128Gcm");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_DecryptAES128GCM(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_GCM_IV_LENGTH], const u8 *aad, LT_SIZE aadLen, const u8 *cipherText, LT_SIZE textLen, const u8 *tag, LT_SIZE tagLen, u8 *plainText) {
    if (s_loadedCryptos.aes128Gcm) return s_loadedCryptos.aes128Gcm->API->Decrypt(key, iv, aad, aadLen, cipherText, textLen, tag, tagLen, plainText);

    LogWarningNoSupport("Aes128Gcm");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_EncryptAES128CTR(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText) {
    if (s_loadedCryptos.aes128Ctr) return s_loadedCryptos.aes128Ctr->API->Encrypt(key, iv, plainText, textLen, cipherText);

    LogWarningNoSupport("Aes128Ctr");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_DecryptAES128CTR(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText) {
    if (s_loadedCryptos.aes128Ctr) return s_loadedCryptos.aes128Ctr->API->Decrypt(key, iv, cipherText, textLen, plainText);

    LogWarningNoSupport("Aes128Ctr");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_EncryptAES128CBC(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CBC_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText) {
    if (s_loadedCryptos.aes128Cbc) return s_loadedCryptos.aes128Cbc->API->Encrypt(key, iv, plainText, textLen, cipherText);

    LogWarningNoSupport("Aes128Cbc");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_DecryptAES128CBC(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CBC_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText) {
    if (s_loadedCryptos.aes128Cbc) return s_loadedCryptos.aes128Cbc->API->Decrypt(key, iv, cipherText, textLen, plainText);

    LogWarningNoSupport("Aes128Cbc");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_EncryptAES128XTS(const u8 key1[AES128_KEY_LENGTH], const u8 key2[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText) {
    if (s_loadedCryptos.aes128Xts) return s_loadedCryptos.aes128Xts->API->Encrypt(key1, key2, iv, plainText, textLen, cipherText);

    LogWarningNoSupport("Aes128Xts");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_DecryptAES128XTS(const u8 key1[AES128_KEY_LENGTH], const u8 key2[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText) {
    if (s_loadedCryptos.aes128Xts) return s_loadedCryptos.aes128Xts->API->Decrypt(key1, key2, iv, cipherText, textLen, plainText);

    LogWarningNoSupport("Aes128Xts");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_GenEddsaPublicKey(const u8 privateKey[EdDSA_KEY_LENGTH], u8 publicKey[EdDSA_KEY_LENGTH]) {
    if (s_loadedCryptos.ed25519) return s_loadedCryptos.ed25519->API->GenPublicKey(privateKey, publicKey);

    LogWarningNoSupport("Ed25519");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_SignEddsa(const u8 privateKey[EdDSA_KEY_LENGTH], const u8 *data, LT_SIZE dataLen, u8 signature[EdDSA_SIGNATURE_LENGTH], u8 publicKey[EdDSA_KEY_LENGTH]) {
    if (s_loadedCryptos.ed25519) return s_loadedCryptos.ed25519->API->Sign(privateKey, data, dataLen, signature, publicKey);

    LogWarningNoSupport("Ed25519");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_VerifyEddsa(const u8 *data, LT_SIZE dataLen, const u8 signature[EdDSA_SIGNATURE_LENGTH], const u8 publicKey[EdDSA_KEY_LENGTH]) {
    if (s_loadedCryptos.ed25519) return s_loadedCryptos.ed25519->API->Verify(data, dataLen, signature, publicKey);

    LogWarningNoSupport("Ed25519");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_GenKeyEcdhe(const u8 k[ECDHE_KEY_LENGTH], const u8 u[ECDHE_KEY_LENGTH], u8 key[ECDHE_KEY_LENGTH]) {
    if (s_loadedCryptos.x25519) return s_loadedCryptos.x25519->API->GenKey(k, u, key);

    LogWarningNoSupport("X25519");
    return kLTSystemCrypto_Result_NoSupport;
}

static LT_SHA1_CTX *LTSystemCrypto_CreateSeqSHA1(void) {
    if (s_loadedCryptos.seqSha1) return s_loadedCryptos.seqSha1->API->CreateSeqSha();

    LogWarningNoSupport("SeqSha1");
    return NULL;
}

static LT_SHA1_CTX *LTSystemCrypto_CloneSeqSHA1(LT_SHA1_CTX *ctx) {
    if (s_loadedCryptos.seqSha1) return s_loadedCryptos.seqSha1->API->CloneSeqSha(ctx);

    LogWarningNoSupport("SeqSha1");
    return NULL;
}

static void LTSystemCrypto_DestroySeqSHA1(LT_SHA1_CTX *ctx) {
    if (s_loadedCryptos.seqSha1) s_loadedCryptos.seqSha1->API->DestroySeqSha(ctx);
    else LogWarningNoSupport("SeqSha1");
}

static LTSystemCryptoResult LTSystemCrypto_UpdateSeqSHA1(LT_SHA1_CTX *ctx, const u8 *data, LT_SIZE dataLen) {
    if (s_loadedCryptos.seqSha1) return s_loadedCryptos.seqSha1->API->UpdateSeqSha(ctx, data, dataLen);

    LogWarningNoSupport("SeqSha1");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_FinishSeqSHA1(LT_SHA1_CTX *ctx, u8 digest[SHA1_HASH_LENGTH]) {
    if (s_loadedCryptos.seqSha1) return s_loadedCryptos.seqSha1->API->FinishSeqSha(ctx, digest);

    LogWarningNoSupport("SeqSha1");
    return kLTSystemCrypto_Result_NoSupport;
}

static LT_SHA256_CTX *LTSystemCrypto_CreateSeqSHA256(void) {
    if (s_loadedCryptos.seqSha256) return s_loadedCryptos.seqSha256->API->CreateSeqSha();

    LogWarningNoSupport("SeqSha256");
    return NULL;
}

static LT_SHA256_CTX *LTSystemCrypto_CloneSeqSHA256(LT_SHA256_CTX *ctx) {
    if (s_loadedCryptos.seqSha256) return s_loadedCryptos.seqSha256->API->CloneSeqSha(ctx);

    LogWarningNoSupport("SeqSha256");
    return NULL;
}

static void LTSystemCrypto_DestroySeqSHA256(LT_SHA256_CTX *ctx) {
    if (s_loadedCryptos.seqSha256) s_loadedCryptos.seqSha256->API->DestroySeqSha(ctx);
    else LogWarningNoSupport("SeqSha256");
}

static LTSystemCryptoResult LTSystemCrypto_UpdateSeqSHA256(LT_SHA256_CTX *ctx, const u8 *data, LT_SIZE dataLen) {
    if (s_loadedCryptos.seqSha256) return s_loadedCryptos.seqSha256->API->UpdateSeqSha(ctx, data, dataLen);

    LogWarningNoSupport("SeqSha256");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_FinishSeqSHA256(LT_SHA256_CTX *ctx, u8 digest[SHA256_HASH_LENGTH]) {
    if (s_loadedCryptos.seqSha256) return s_loadedCryptos.seqSha256->API->FinishSeqSha(ctx, digest);

    LogWarningNoSupport("SeqSha256");
    return kLTSystemCrypto_Result_NoSupport;
}

static LT_HMAC_SHA1_CTX *LTSystemCrypto_CreateSeqHMACSHA1(const u8 *key, LT_SIZE keyLen) {
    if (s_loadedCryptos.seqHmacSha1) return s_loadedCryptos.seqHmacSha1->API->CreateSeqHmac(key, keyLen);

    LogWarningNoSupport("SeqHmacSha1");
    return NULL;
}

static LT_HMAC_SHA1_CTX *LTSystemCrypto_CloneSeqHMACSHA1(LT_HMAC_SHA1_CTX *ctx) {
    if (s_loadedCryptos.seqHmacSha1) return s_loadedCryptos.seqHmacSha1->API->CloneSeqHmac(ctx);

    LogWarningNoSupport("SeqHmacSha1");
    return NULL;
}

static void LTSystemCrypto_DestroySeqHMACSHA1(LT_HMAC_SHA1_CTX *ctx) {
    if (s_loadedCryptos.seqHmacSha1) s_loadedCryptos.seqHmacSha1->API->DestroySeqHmac(ctx);
    else LogWarningNoSupport("SeqHmacSha1");
}

static LTSystemCryptoResult LTSystemCrypto_UpdateSeqHMACSHA1(LT_HMAC_SHA1_CTX *ctx, const u8 *data, LT_SIZE dataLen) {
    if (s_loadedCryptos.seqHmacSha1) return s_loadedCryptos.seqHmacSha1->API->UpdateSeqHmac(ctx, data, dataLen);

    LogWarningNoSupport("SeqHmacSha1");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_FinishSeqHMACSHA1(LT_HMAC_SHA1_CTX *ctx, u8 digest[SHA1_HASH_LENGTH]) {
    if (s_loadedCryptos.seqHmacSha1) return s_loadedCryptos.seqHmacSha1->API->FinishSeqHmac(ctx, digest);

    LogWarningNoSupport("SeqHmacSha1");
    return kLTSystemCrypto_Result_NoSupport;
}

static LT_HMAC_SHA256_CTX *LTSystemCrypto_CreateSeqHMACSHA256(const u8 *key, LT_SIZE keyLen) {
    if (s_loadedCryptos.seqHmacSha256) return s_loadedCryptos.seqHmacSha256->API->CreateSeqHmac(key, keyLen);

    LogWarningNoSupport("SeqHmacSha256");
    return NULL;
}

static LT_HMAC_SHA256_CTX *LTSystemCrypto_CloneSeqHMACSHA256(LT_HMAC_SHA256_CTX *ctx) {
    if (s_loadedCryptos.seqHmacSha256) return s_loadedCryptos.seqHmacSha256->API->CloneSeqHmac(ctx);

    LogWarningNoSupport("SeqHmacSha256");
    return NULL;
}

static void LTSystemCrypto_DestroySeqHMACSHA256(LT_HMAC_SHA256_CTX *ctx) {
    if (s_loadedCryptos.seqHmacSha256) s_loadedCryptos.seqHmacSha256->API->DestroySeqHmac(ctx);
    else LogWarningNoSupport("SeqHmacSha256");
}

static LTSystemCryptoResult LTSystemCrypto_UpdateSeqHMACSHA256(LT_HMAC_SHA256_CTX *ctx, const u8 *data, LT_SIZE dataLen) {
    if (s_loadedCryptos.seqHmacSha256) return s_loadedCryptos.seqHmacSha256->API->UpdateSeqHmac(ctx, data, dataLen);

    LogWarningNoSupport("SeqHmacSha256");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_FinishSeqHMACSHA256(LT_HMAC_SHA256_CTX *ctx, u8 digest[SHA256_HASH_LENGTH]) {
    if (s_loadedCryptos.seqHmacSha256) return s_loadedCryptos.seqHmacSha256->API->FinishSeqHmac(ctx, digest);

    LogWarningNoSupport("SeqHmacSha256");
    return kLTSystemCrypto_Result_NoSupport;
}

LTSystemCryptoResult LTSystemCrypto_GenEcdsaPublicKey(const u8 privateKey[ECDSA_P256_PRIVATEKEY_LENGTH], u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    if (s_loadedCryptos.ecdsaP256) return s_loadedCryptos.ecdsaP256->API->GenPublicKey(privateKey, publicKey);

    LogWarningNoSupport("EcdsaP256");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_SignEcdsaHash(const u8 privateKey[ECDSA_P256_PRIVATEKEY_LENGTH], const u8 hash[SHA256_HASH_LENGTH], u8 signature[ECDSA_P256_SIGNATURE_LENGTH], u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    if (s_loadedCryptos.ecdsaP256) return s_loadedCryptos.ecdsaP256->API->SignHash(privateKey, hash, signature, publicKey);

    LogWarningNoSupport("EcdsaP256");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_SignEcdsa(const u8 privateKey[ECDSA_P256_PRIVATEKEY_LENGTH], const u8 *data, LT_SIZE dataLen, u8 signature[ECDSA_P256_SIGNATURE_LENGTH], u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    if (s_loadedCryptos.ecdsaP256) return s_loadedCryptos.ecdsaP256->API->Sign(privateKey, data, dataLen, signature, publicKey);

    LogWarningNoSupport("EcdsaP256");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_VerifyEcdsaHash(const u8 hash[SHA256_HASH_LENGTH], const u8 signature[ECDSA_P256_SIGNATURE_LENGTH], const u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    if (s_loadedCryptos.ecdsaP256) return s_loadedCryptos.ecdsaP256->API->VerifyHash(hash, signature, publicKey);

    LogWarningNoSupport("EcdsaP256");
    return kLTSystemCrypto_Result_NoSupport;
}

static LTSystemCryptoResult LTSystemCrypto_VerifyEcdsa(const u8 *data, LT_SIZE dataLen, const u8 signature[ECDSA_P256_SIGNATURE_LENGTH], const u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    if (s_loadedCryptos.ecdsaP256) return s_loadedCryptos.ecdsaP256->API->Verify(data, dataLen, signature, publicKey);

    LogWarningNoSupport("EcdsaP256");
    return kLTSystemCrypto_Result_NoSupport;
}

static void LTSystemCrypto_GetStatus(LTSystemCryptoMethod eMethod, LTSystemCryptoStatus *status) {
    if (!status)  return;

    switch (eMethod) {

        case kLTSystemCrypto_Method_Random :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.random) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.random) << 1);
            break;

        case kLTSystemCrypto_Method_SHA1 :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.sha1) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.sha1) << 1);
            break;

        case kLTSystemCrypto_Method_SHA256 :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.sha256) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.sha256) << 1);
            break;

        case kLTSystemCrypto_Method_HMAC_SHA1 :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.hmacSha1) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.hmacSha1) << 1);
            break;

        case kLTSystemCrypto_Method_HMAC_SHA256 :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.hmacSha256) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.hmacSha256) << 1);
            break;

        case kLTSystemCrypto_Method_AES128_GCM :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.aes128Gcm) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.aes128Gcm) << 1);
            break;

        case kLTSystemCrypto_Method_AES128_CBC :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.aes128Cbc) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.aes128Cbc) << 1);
            break;

        case kLTSystemCrypto_Method_AES128_CTR :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.aes128Ctr) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.aes128Ctr) << 1);
            break;

        case kLTSystemCrypto_Method_AES128_XTS :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.aes128Xts) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.aes128Xts) << 1);
            break;

        case kLTSystemCrypto_Method_EdDSA :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.eddsa) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.eddsa) << 1);
            break;

        case kLTSystemCrypto_Method_ECDHE :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.ecdhe) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.ecdhe) << 1);
            break;

        case kLTSystemCrypto_Method_SeqSHA1 :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.seqSha1) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.seqSha1) << 1);
            break;

        case kLTSystemCrypto_Method_SeqSHA256 :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.seqSha256) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.seqSha256) << 1);
            break;

        case kLTSystemCrypto_Method_SeqHMAC_SHA1 :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.seqHmacSha1) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.seqHmacSha1) << 1);
            break;

        case kLTSystemCrypto_Method_SeqHMAC_SHA256 :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.seqHmacSha256) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.seqHmacSha256) << 1);
            break;

        case kLTSystemCrypto_Method_ECDSA :
            *status = (s_hwCryptoOptions.enabled & s_hwCryptoOptions.ecdsa) | ((s_swCryptoOptions.enabled & s_swCryptoOptions.ecdsa) << 1);
            break;

        default:
            *status = kLTSystemCrypto_Status_Disabled;
    }
}

const LTSystemCryptoConsts* LTSystemCrypto_GetCryptoConsts(void) {
    return &s_kCryptoConsts;
}

/** End of LTSystemCrypto functions *******************************************/

/*___________________________________________
 / LTSystemCrypto library initialization */
static void ShutDownCrypto(void) {
    lt_destroyobject(s_loadedCryptos.random);
    lt_destroyobject(s_loadedCryptos.sha1);
    lt_destroyobject(s_loadedCryptos.sha256);
    lt_destroyobject(s_loadedCryptos.seqSha1);
    lt_destroyobject(s_loadedCryptos.seqSha256);
    lt_destroyobject(s_loadedCryptos.hmacSha1);
    lt_destroyobject(s_loadedCryptos.hmacSha256);
    lt_destroyobject(s_loadedCryptos.seqHmacSha1);
    lt_destroyobject(s_loadedCryptos.seqHmacSha256);
    lt_destroyobject(s_loadedCryptos.aes128Cbc);
    lt_destroyobject(s_loadedCryptos.aes128Ctr);
    lt_destroyobject(s_loadedCryptos.aes128Gcm);
    lt_destroyobject(s_loadedCryptos.aes128Xts);
    lt_destroyobject(s_loadedCryptos.ecdsaP256);
    lt_destroyobject(s_loadedCryptos.ed25519);
    lt_destroyobject(s_loadedCryptos.x25519);
    lt_destroyobject(s_hwCrypto);
    lt_destroyobject(s_swCrypto);
}

static bool LTSystemCryptoImpl_LibInit(void) {
    // Open hw crypto
    lt_memset(&s_hwCryptoOptions, 0, sizeof(LTSystemCryptoOptions));
    lt_memset(&s_swCryptoOptions, 0, sizeof(LTSystemCryptoOptions));
    s_hwCrypto = lt_createobject(LTHardwareCrypto);
    if (s_hwCrypto) {
        s_hwCrypto->API->GetOptions(&s_hwCryptoOptions, &s_swCryptoOptions);
    }

    // Open sw crypto
    s_swCrypto = lt_createobject(LTSoftwareCrypto);
    if (s_swCrypto) {
        // Because no hw cryptos, read sw cryptos.
        if (!s_hwCrypto) s_swCrypto->API->GetOptions(&s_swCryptoOptions);
    }

    if (!s_hwCrypto && !s_swCrypto) {
        LTLOG_YELLOWALERT("init.fail", NULL);
        ShutDownCrypto();
        return false;
    }

    // Load crypto objects to improve performance.
    lt_memset(&s_loadedCryptos, 0, sizeof(s_loadedCryptos));
    if (s_hwCryptoOptions.random) s_loadedCryptos.random = lt_createobject_typed(LTDriverCryptoRandom, LTHardwareCryptoRandom);
    if (!s_loadedCryptos.random && s_swCryptoOptions.random) s_loadedCryptos.random = lt_createobject_typed(LTDriverCryptoRandom, LTSoftwareCryptoRandom);
    if (s_hwCryptoOptions.sha1) s_loadedCryptos.sha1 = lt_createobject_typed(LTDriverCryptoSha1, LTHardwareCryptoSha1);
    if (!s_loadedCryptos.sha1 && s_swCryptoOptions.sha1) s_loadedCryptos.sha1 = lt_createobject_typed(LTDriverCryptoSha1, LTSoftwareCryptoSha1);
    if (s_hwCryptoOptions.sha256) s_loadedCryptos.sha256 = lt_createobject_typed(LTDriverCryptoSha256, LTHardwareCryptoSha256);
    if (!s_loadedCryptos.sha256 && s_swCryptoOptions.sha256) s_loadedCryptos.sha256 = lt_createobject_typed(LTDriverCryptoSha256, LTSoftwareCryptoSha256);
    if (s_hwCryptoOptions.seqSha1) s_loadedCryptos.seqSha1 = lt_createobject_typed(LTDriverCryptoSeqSha1, LTHardwareCryptoSeqSha1);
    if (!s_loadedCryptos.seqSha1 && s_swCryptoOptions.seqSha1) s_loadedCryptos.seqSha1 = lt_createobject_typed(LTDriverCryptoSeqSha1, LTSoftwareCryptoSeqSha1);
    if (s_hwCryptoOptions.seqSha256) s_loadedCryptos.seqSha256 = lt_createobject_typed(LTDriverCryptoSeqSha256, LTHardwareCryptoSeqSha256);
    if (!s_loadedCryptos.seqSha256 && s_swCryptoOptions.seqSha256) s_loadedCryptos.seqSha256 = lt_createobject_typed(LTDriverCryptoSeqSha256, LTSoftwareCryptoSeqSha256);
    if (s_hwCryptoOptions.hmacSha1) s_loadedCryptos.hmacSha1 = lt_createobject_typed(LTDriverCryptoHmacSha1, LTHardwareCryptoHmacSha1);
    if (!s_loadedCryptos.hmacSha1 && s_swCryptoOptions.hmacSha1) s_loadedCryptos.hmacSha1 = lt_createobject_typed(LTDriverCryptoHmacSha1, LTSoftwareCryptoHmacSha1);
    if (s_hwCryptoOptions.hmacSha256) s_loadedCryptos.hmacSha256 = lt_createobject_typed(LTDriverCryptoHmacSha256, LTHardwareCryptoHmacSha256);
    if (!s_loadedCryptos.hmacSha256 && s_swCryptoOptions.hmacSha256) s_loadedCryptos.hmacSha256 = lt_createobject_typed(LTDriverCryptoHmacSha256, LTSoftwareCryptoHmacSha256);
    if (s_hwCryptoOptions.seqHmacSha1) s_loadedCryptos.seqHmacSha1 = lt_createobject_typed(LTDriverCryptoSeqHmacSha1, LTHardwareCryptoSeqHmacSha1);
    if (!s_loadedCryptos.seqHmacSha1 && s_swCryptoOptions.seqHmacSha1) s_loadedCryptos.seqHmacSha1 = lt_createobject_typed(LTDriverCryptoSeqHmacSha1, LTSoftwareCryptoSeqHmacSha1);
    if (s_hwCryptoOptions.seqHmacSha256) s_loadedCryptos.seqHmacSha256 = lt_createobject_typed(LTDriverCryptoSeqHmacSha256, LTHardwareCryptoSeqHmacSha256);
    if (!s_loadedCryptos.seqHmacSha256 && s_swCryptoOptions.seqHmacSha256) s_loadedCryptos.seqHmacSha256 = lt_createobject_typed(LTDriverCryptoSeqHmacSha256, LTSoftwareCryptoSeqHmacSha256);
    if (s_hwCryptoOptions.aes128Cbc) s_loadedCryptos.aes128Cbc = lt_createobject_typed(LTDriverCryptoAes128Cbc, LTHardwareCryptoAes128Cbc);
    if (!s_loadedCryptos.aes128Cbc && s_swCryptoOptions.aes128Cbc) s_loadedCryptos.aes128Cbc = lt_createobject_typed(LTDriverCryptoAes128Cbc, LTSoftwareCryptoAes128Cbc);
    if (s_hwCryptoOptions.aes128Ctr) s_loadedCryptos.aes128Ctr = lt_createobject_typed(LTDriverCryptoAes128Ctr, LTHardwareCryptoAes128Ctr);
    if (!s_loadedCryptos.aes128Ctr && s_swCryptoOptions.aes128Ctr) s_loadedCryptos.aes128Ctr = lt_createobject_typed(LTDriverCryptoAes128Ctr, LTSoftwareCryptoAes128Ctr);
    if (s_hwCryptoOptions.aes128Gcm) s_loadedCryptos.aes128Gcm = lt_createobject_typed(LTDriverCryptoAes128Gcm, LTHardwareCryptoAes128Gcm);
    if (!s_loadedCryptos.aes128Gcm && s_swCryptoOptions.aes128Gcm) s_loadedCryptos.aes128Gcm = lt_createobject_typed(LTDriverCryptoAes128Gcm, LTSoftwareCryptoAes128Gcm);
    if (s_hwCryptoOptions.aes128Xts) s_loadedCryptos.aes128Xts = lt_createobject_typed(LTDriverCryptoAes128Xts, LTHardwareCryptoAes128Xts);
    if (!s_loadedCryptos.aes128Xts && s_swCryptoOptions.aes128Xts) s_loadedCryptos.aes128Xts = lt_createobject_typed(LTDriverCryptoAes128Xts, LTSoftwareCryptoAes128Xts);
    if (s_hwCryptoOptions.ecdsa) s_loadedCryptos.ecdsaP256 = lt_createobject_typed(LTDriverCryptoEcdsaP256, LTHardwareCryptoEcdsaP256);
    if (!s_loadedCryptos.ecdsaP256 && s_swCryptoOptions.ecdsa) s_loadedCryptos.ecdsaP256 = lt_createobject_typed(LTDriverCryptoEcdsaP256, LTSoftwareCryptoEcdsaP256);
    if (s_hwCryptoOptions.eddsa) s_loadedCryptos.ed25519 = lt_createobject_typed(LTDriverCryptoEd25519, LTHardwareCryptoEd25519);
    if (!s_loadedCryptos.ed25519 && s_swCryptoOptions.eddsa) s_loadedCryptos.ed25519 = lt_createobject_typed(LTDriverCryptoEd25519, LTSoftwareCryptoEd25519);
    if (s_hwCryptoOptions.ecdhe) s_loadedCryptos.x25519 = lt_createobject_typed(LTDriverCryptoX25519, LTHardwareCryptoX25519);
    if (!s_loadedCryptos.x25519 && s_swCryptoOptions.ecdhe) s_loadedCryptos.x25519 = lt_createobject_typed(LTDriverCryptoX25519, LTSoftwareCryptoX25519);
    if (s_loadedCryptos.x25519) s_kCryptoConsts.kX25519_BP = s_loadedCryptos.x25519->API->GetBasePoint();

    return true;
}

static void LTSystemCryptoImpl_LibFini(void) {
    ShutDownCrypto();
}

/* LTSystemCrypto library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTSystemCrypto)
    .GetStatus               = &LTSystemCrypto_GetStatus,
    .GetCryptoConsts         = &LTSystemCrypto_GetCryptoConsts,
    .GetOptions              = &LTSystemCrypto_GetOptions,
    .GenRandomBytes          = &LTSystemCrypto_GenRandomBytes,
    .GenDigestSHA1           = &LTSystemCrypto_GenDigestSHA1,
    .GenDigestSHA256         = &LTSystemCrypto_GenDigestSHA256,
    .GenHMACSHA1             = &LTSystemCrypto_GenHMACSHA1,
    .GenHMACSHA256           = &LTSystemCrypto_GenHMACSHA256,
    .EncryptAES128GCM        = &LTSystemCrypto_EncryptAES128GCM,
    .DecryptAES128GCM        = &LTSystemCrypto_DecryptAES128GCM,
    .EncryptAES128CTR        = &LTSystemCrypto_EncryptAES128CTR,
    .DecryptAES128CTR        = &LTSystemCrypto_DecryptAES128CTR,
    .EncryptAES128CBC        = &LTSystemCrypto_EncryptAES128CBC,
    .DecryptAES128CBC        = &LTSystemCrypto_DecryptAES128CBC,
    .EncryptAES128XTS        = &LTSystemCrypto_EncryptAES128XTS,
    .DecryptAES128XTS        = &LTSystemCrypto_DecryptAES128XTS,
    .GenEddsaPublicKey       = &LTSystemCrypto_GenEddsaPublicKey,
    .SignEddsa               = &LTSystemCrypto_SignEddsa,
    .VerifyEddsa             = &LTSystemCrypto_VerifyEddsa,
    .GenKeyEcdhe             = &LTSystemCrypto_GenKeyEcdhe,
    .CreateSeqSHA1           = &LTSystemCrypto_CreateSeqSHA1,
    .CloneSeqSHA1            = &LTSystemCrypto_CloneSeqSHA1,
    .DestroySeqSHA1          = &LTSystemCrypto_DestroySeqSHA1,
    .UpdateSeqSHA1           = &LTSystemCrypto_UpdateSeqSHA1,
    .FinishSeqSHA1           = &LTSystemCrypto_FinishSeqSHA1,
    .CreateSeqSHA256         = &LTSystemCrypto_CreateSeqSHA256,
    .CloneSeqSHA256          = &LTSystemCrypto_CloneSeqSHA256,
    .DestroySeqSHA256        = &LTSystemCrypto_DestroySeqSHA256,
    .UpdateSeqSHA256         = &LTSystemCrypto_UpdateSeqSHA256,
    .FinishSeqSHA256         = &LTSystemCrypto_FinishSeqSHA256,
    .CreateSeqHMACSHA1       = &LTSystemCrypto_CreateSeqHMACSHA1,
    .CloneSeqHMACSHA1        = &LTSystemCrypto_CloneSeqHMACSHA1,
    .DestroySeqHMACSHA1      = &LTSystemCrypto_DestroySeqHMACSHA1,
    .UpdateSeqHMACSHA1       = &LTSystemCrypto_UpdateSeqHMACSHA1,
    .FinishSeqHMACSHA1       = &LTSystemCrypto_FinishSeqHMACSHA1,
    .CreateSeqHMACSHA256     = &LTSystemCrypto_CreateSeqHMACSHA256,
    .CloneSeqHMACSHA256      = &LTSystemCrypto_CloneSeqHMACSHA256,
    .DestroySeqHMACSHA256    = &LTSystemCrypto_DestroySeqHMACSHA256,
    .UpdateSeqHMACSHA256     = &LTSystemCrypto_UpdateSeqHMACSHA256,
    .FinishSeqHMACSHA256     = &LTSystemCrypto_FinishSeqHMACSHA256,
    .GenEcdsaPublicKey       = &LTSystemCrypto_GenEcdsaPublicKey,
    .SignEcdsaHash           = &LTSystemCrypto_SignEcdsaHash,
    .SignEcdsa               = &LTSystemCrypto_SignEcdsa,
    .VerifyEcdsaHash         = &LTSystemCrypto_VerifyEcdsaHash,
    .VerifyEcdsa             = &LTSystemCrypto_VerifyEcdsa,
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  24-Jan-22   gallienus   created
 *  05-May-25   gallienus   refactor to use crypto objects.
 */