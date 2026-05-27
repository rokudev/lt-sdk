/*******************************************************************************
 * platforms/linux/source/linux/driver/crypto/OpenSSLDriverCryptoImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

/*
 * Map opaque LT types to opaque OpenSSL types. This removes the need
 * for pointer type casting below and therefore help to avoid bugs.
 */
#define	LT_SHA1_CTX_impl	evp_md_ctx_st
#define	LT_HMAC_SHA1_CTX_impl	hmac_ctx_st
#define	LT_SHA256_CTX_impl	evp_md_ctx_st
#define	LT_HMAC_SHA256_CTX_impl	hmac_ctx_st

#include <lt/LTTypes.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>

DEFINE_LTLOG_SECTION("openssl.drv.crypto");

// Must select hardware crypto options here
static const LTSystemCryptoOptions s_kHwOptions = {
    .enabled       = 1,
    .random        = 1,
    .sha1          = 1,
    .sha256        = 1,
    .hmacSha1      = 1,
    .hmacSha256    = 1,
    .aes128Gcm     = 1,
    .aes128Ctr     = 1,
    .aes128Cbc     = 1,
    .aes128Xts     = 1,
    .seqSha1       = 1,
    .seqHmacSha1   = 1,
    .seqSha256     = 1,
    .seqHmacSha256 = 1,
};

// Must select software crypto options here, match LTSoftwareCrypto.mk
static const LTSystemCryptoOptions s_kSwOptions = {
    .enabled       = 1,
    .random        = 1,
    .sha256        = 1,
    .hmacSha256    = 1,
    .ecdhe         = 1,
    .ecdsa         = 1,
};

/******************************************************************************
 * OpenSSLDriverCrypto driver object and library
 */
typedef_LTObjectImpl(LTHardwareCrypto, LTHardwareCryptoImpl) {
} LTOBJECT_API;

static void LTHardwareCryptoImpl_GetOptions(LTSystemCryptoOptions *hardwareOptions, LTSystemCryptoOptions *softwareOptions) {
    if (hardwareOptions) hardwareOptions->val = s_kHwOptions.val;
    if (softwareOptions) softwareOptions->val = s_kSwOptions.val;
}

static void LTHardwareCryptoImpl_DestructObject(LTHardwareCryptoImpl *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoImpl_ConstructObject(LTHardwareCryptoImpl *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTHardwareCrypto, LTHardwareCryptoImpl,
    GetOptions,
);

static void OpenSSLDriverCrypto_LibFini(void) {
}

static bool OpenSSLDriverCrypto_LibInit(void) {
    if (RAND_poll() == 1) return true;
    LTLOG_YELLOWALERT("init", "Failed to initialize OpenSSL RNG");
    return false;
}

define_LTObjectLibrary(1, OpenSSLDriverCrypto_LibInit, OpenSSLDriverCrypto_LibFini);

// OpenSSL wrapper
static LTSystemCryptoResult
PerformHashOp(const EVP_MD *evp, const u8 *input, LT_SIZE inputLen, u8 *output, LT_SIZE outputLen) {
    LTSystemCryptoResult  result = kLTSystemCrypto_Result_OOM;
    EVP_MD_CTX           *ctx = NULL;

    do {
        ctx = EVP_MD_CTX_new();
        if (!ctx) break;
        result = kLTSystemCrypto_Result_Error;

        if (EVP_DigestInit_ex(ctx, evp, NULL) != 1) break;
        if (EVP_DigestUpdate(ctx, input, inputLen) != 1) break;

        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int  digestLen;
        if (EVP_DigestFinal_ex(ctx, digest, &digestLen) != 1) break;
        if (digestLen != outputLen) {
            result = kLTSystemCrypto_Result_WrongLength;
            break;
        }

        lt_memcpy(output, digest, outputLen);
        result = kLTSystemCrypto_Result_Ok;
    } while (false);

    EVP_MD_CTX_free(ctx);
    return result;
}

static LTSystemCryptoResult
PerformHmacOp(const EVP_MD *evp, const u8 *key, LT_SIZE keyLen,
              const u8 *input, LT_SIZE inputLen,
              u8 *output, LT_SIZE outputLen) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digestLen;
    if (HMAC(evp, key, keyLen, input, inputLen, digest, &digestLen) == NULL) {
        return kLTSystemCrypto_Result_Error;
    }

    if (digestLen != outputLen) {
        return kLTSystemCrypto_Result_WrongLength;
    }

    lt_memcpy(output, digest, outputLen);
    return kLTSystemCrypto_Result_Ok;
}

static LTSystemCryptoResult
PerformCipherOp(const EVP_CIPHER *evp, const u8 *key, const u8 *iv,
                const u8 *input, LT_SIZE inputLen, u8 *output, bool encrypt) {
    int blockSize = EVP_CIPHER_get_block_size(evp);
    if (inputLen % blockSize) return kLTSystemCrypto_Result_WrongLength;

    int (*initFunc)(EVP_CIPHER_CTX *, const EVP_CIPHER *, ENGINE *,
                    const unsigned char *, const unsigned char *);
    int (*updateFunc)(EVP_CIPHER_CTX *, unsigned char *, int *,
                      const unsigned char *, int);
    int (*finalFunc)(EVP_CIPHER_CTX *, unsigned char *, int *);
    if (encrypt) {
        initFunc = EVP_EncryptInit_ex;
        updateFunc = EVP_EncryptUpdate;
        finalFunc = EVP_EncryptFinal;
    } else {
        initFunc = EVP_DecryptInit_ex;
        updateFunc = EVP_DecryptUpdate;
        finalFunc = EVP_DecryptFinal;
    }

    LTSystemCryptoResult  result = kLTSystemCrypto_Result_OOM;
    EVP_CIPHER_CTX       *ctx = NULL;

    do {
        ctx = EVP_CIPHER_CTX_new();
        if (!ctx) break;
        result = kLTSystemCrypto_Result_Error;

        int outputLen = 0;
        if (initFunc(ctx, evp, NULL, key, iv) != 1) break;
        if (EVP_CIPHER_CTX_set_padding(ctx, 0) != 1) break;
        if (updateFunc(ctx, output, &outputLen, input, inputLen) != 1) break;

        output += outputLen;
        if (finalFunc(ctx, output, &outputLen) != 1) break;

        result = kLTSystemCrypto_Result_Ok;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    return result;
}

static LTSystemCryptoResult
PerformAES128XTSOp(const u8 *key1, const u8 *key2, const u8 *iv,
                   const u8 *input, LT_SIZE inputLen, u8 *output,
                   bool encrypt) {
    u8 key[AES128_KEY_LENGTH + AES128_KEY_LENGTH];
    lt_memcpy(&key[0], key1, AES128_KEY_LENGTH);
    lt_memcpy(&key[AES128_KEY_LENGTH], key2, AES128_KEY_LENGTH);
    LTSystemCryptoResult result = PerformCipherOp(EVP_aes_128_xts(), key, iv,
                                                  input, inputLen, output,
                                                  encrypt);
    lt_memset(key, 0, sizeof(key));
    return result;
}

static EVP_CIPHER_CTX *
PrepareES128GCMOp(const u8 *key, const u8 *iv, const u8 *aad, LT_SIZE aadLen,
                  bool encrypt) {
    int (*initFunc)(EVP_CIPHER_CTX *, const EVP_CIPHER *, ENGINE *,
                    const unsigned char *, const unsigned char *);
    int (*updateFunc)(EVP_CIPHER_CTX *, unsigned char *, int *,
                      const unsigned char *, int);
    if (encrypt) {
        initFunc = EVP_EncryptInit_ex;
        updateFunc = EVP_EncryptUpdate;
    } else {
        initFunc = EVP_DecryptInit_ex;
        updateFunc = EVP_DecryptUpdate;
    }

    EVP_CIPHER_CTX *ctx = NULL;

    do {
        ctx = EVP_CIPHER_CTX_new();
        if (!ctx) break;

        if (initFunc(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1) break;

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                                AES128_GCM_IV_LENGTH, NULL) != 1) break;

        if (initFunc(ctx, NULL, NULL, key, iv) != 1) break;

        int outputLen = 0;
        if (updateFunc(ctx, NULL, &outputLen, aad, aadLen) != 1) break;

        return ctx;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    return NULL;
}

static EVP_MD_CTX *PrepareSeqHash(const EVP_MD *evp) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx) {
        if (EVP_DigestInit_ex(ctx, evp, NULL) == 1) {
            return ctx;
        }
        EVP_MD_CTX_free(ctx);
    }
    return NULL;
}

static EVP_MD_CTX *CloneSeqHash(EVP_MD_CTX *ctx) {
    if (ctx) {
        EVP_MD_CTX *copy = EVP_MD_CTX_new();
        if (copy) {
            if (EVP_MD_CTX_copy(copy, ctx) == 1) {
                return copy;
            }
            EVP_MD_CTX_free(copy);
        }
    }
    return NULL;
}

static LTSystemCryptoResult UpdateSeqHash(EVP_MD_CTX *ctx, const u8 *data, LT_SIZE len) {
    return (EVP_DigestUpdate(ctx, data, len) == 1) ?
           kLTSystemCrypto_Result_Ok : kLTSystemCrypto_Result_Error;
}

static LTSystemCryptoResult FinishSeqHash(EVP_MD_CTX *ctx, u8 *output, LT_SIZE outputLen) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digestLen;
    if (EVP_DigestFinal_ex(ctx, digest, &digestLen) != 1) {
        return kLTSystemCrypto_Result_Error;
    }

    if (digestLen != outputLen) {
        return kLTSystemCrypto_Result_WrongLength;
    }

    lt_memcpy(output, digest, outputLen);
    return kLTSystemCrypto_Result_Ok;
}

static HMAC_CTX *PrepareSeqHMACHash(const EVP_MD *evp, const void *key, int key_len) {
    HMAC_CTX *ctx = HMAC_CTX_new();
    if (ctx) {
        if (HMAC_Init_ex(ctx, key, key_len, evp, NULL) == 1) {
            return ctx;
        }
        HMAC_CTX_free(ctx);
    }
    return NULL;
}

static HMAC_CTX *CloneSeqHMACHash(HMAC_CTX *ctx) {
    if (ctx) {
        HMAC_CTX *copy = HMAC_CTX_new();
        if (copy) {
            if (HMAC_CTX_copy(copy, ctx) == 1) {
                return copy;
            }
            HMAC_CTX_free(copy);
        }
    }
    return NULL;
}

static LTSystemCryptoResult UpdateSeqHMACHash(HMAC_CTX *ctx, const u8 *data, LT_SIZE len) {
    return (HMAC_Update(ctx, data, len) == 1) ?
           kLTSystemCrypto_Result_Ok : kLTSystemCrypto_Result_Error;
}

static LTSystemCryptoResult FinishSeqHMACHash(HMAC_CTX *ctx, u8 *output, LT_SIZE outputLen) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digestLen;
    if (HMAC_Final(ctx, digest, &digestLen) != 1) {
        return kLTSystemCrypto_Result_Error;
    }

    if (digestLen != outputLen) {
        return kLTSystemCrypto_Result_WrongLength;
    }

    lt_memcpy(output, digest, outputLen);
    return kLTSystemCrypto_Result_Ok;
}

// Objects: Entropy, Random
typedef_LTObjectImpl(LTDriverCryptoEntropy, LTDriverCryptoEntropyImpl) {
} LTOBJECT_API;

static bool LTDriverCryptoEntropyImpl_GetEntropy(u8 *entropy, LT_SIZE entropyLen) {
    return (RAND_bytes(entropy, entropyLen) == 1);
}

static void LTDriverCryptoEntropyImpl_DestructObject(LTDriverCryptoEntropyImpl *instance) {
    LT_UNUSED(instance);
}

static bool LTDriverCryptoEntropyImpl_ConstructObject(LTDriverCryptoEntropyImpl *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoEntropy, LTDriverCryptoEntropyImpl,
    GetEntropy,
);

typedef_LTObjectImpl(LTDriverCryptoRandom, LTHardwareCryptoRandom) {
} LTOBJECT_API;

static bool LTHardwareCryptoRandom_GenRandomBytes(u8 *output, LT_SIZE outputLen) {
    return (RAND_bytes(output, outputLen) == 1);
}

static void LTHardwareCryptoRandom_DestructObject(LTHardwareCryptoRandom *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoRandom_ConstructObject(LTHardwareCryptoRandom *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoRandom, LTHardwareCryptoRandom,
    GenRandomBytes,
);

// Objects: SHA1, SHA256, HMAC-SHA1, HMAC-SHA256
typedef_LTObjectImpl(LTDriverCryptoSha1, LTHardwareCryptoSha1) {
} LTOBJECT_API;

static LTSystemCryptoResult LTHardwareCryptoSha1_GenDigest(const u8 *data, LT_SIZE dataLen, u8 *digest) {
    return PerformHashOp(EVP_sha1(), data, dataLen, digest, SHA1_HASH_LENGTH);
}

static void LTHardwareCryptoSha1_DestructObject(LTHardwareCryptoSha1 *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoSha1_ConstructObject(LTHardwareCryptoSha1 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoSha1, LTHardwareCryptoSha1,
    GenDigest,
);

typedef_LTObjectImpl(LTDriverCryptoSha256, LTHardwareCryptoSha256) {
} LTOBJECT_API;

static LTSystemCryptoResult LTHardwareCryptoSha256_GenDigest(const u8 *data, LT_SIZE dataLen, u8 *digest) {
    return PerformHashOp(EVP_sha256(), data, dataLen, digest, SHA256_HASH_LENGTH);
}

static void LTHardwareCryptoSha256_DestructObject(LTHardwareCryptoSha256 *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoSha256_ConstructObject(LTHardwareCryptoSha256 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoSha256, LTHardwareCryptoSha256,
    GenDigest,
);

typedef_LTObjectImpl(LTDriverCryptoHmacSha1, LTHardwareCryptoHmacSha1) {
} LTOBJECT_API;

static LTSystemCryptoResult LTHardwareCryptoHmacSha1_GenHmac(const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE dataLen, u8 *hmac) {
    return PerformHmacOp(EVP_sha1(), key, keyLen, data, dataLen, hmac, SHA1_HASH_LENGTH);
}

static void LTHardwareCryptoHmacSha1_DestructObject(LTHardwareCryptoHmacSha1 *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoHmacSha1_ConstructObject(LTHardwareCryptoHmacSha1 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoHmacSha1, LTHardwareCryptoHmacSha1,
    GenHmac,
);

typedef_LTObjectImpl(LTDriverCryptoHmacSha256, LTHardwareCryptoHmacSha256) {
} LTOBJECT_API;

static LTSystemCryptoResult LTHardwareCryptoHmacSha256_GenHmac(const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE dataLen, u8 *hmac) {
    return PerformHmacOp(EVP_sha256(), key, keyLen, data, dataLen, hmac, SHA256_HASH_LENGTH);
}

static void LTHardwareCryptoHmacSha256_DestructObject(LTHardwareCryptoHmacSha256 *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoHmacSha256_ConstructObject(LTHardwareCryptoHmacSha256 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoHmacSha256, LTHardwareCryptoHmacSha256,
    GenHmac,
);

// Objects: AES128-CTR, AES128-CBC, AES128-XTS, AES128-GCM
typedef_LTObjectImpl(LTDriverCryptoAes128Ctr, LTHardwareCryptoAes128Ctr) {
} LTOBJECT_API;

static LTSystemCryptoResult LTHardwareCryptoAes128Ctr_Encrypt(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText) {
    return PerformCipherOp(EVP_aes_128_ctr(), key, iv, plainText, textLen, cipherText, true);
}

static LTSystemCryptoResult LTHardwareCryptoAes128Ctr_Decrypt(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText) {
    return PerformCipherOp(EVP_aes_128_ctr(), key, iv, cipherText, textLen, plainText, false);
}

static void LTHardwareCryptoAes128Ctr_DestructObject(LTHardwareCryptoAes128Ctr *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoAes128Ctr_ConstructObject(LTHardwareCryptoAes128Ctr *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoAes128Ctr, LTHardwareCryptoAes128Ctr,
    Encrypt,
    Decrypt,
);

typedef_LTObjectImpl(LTDriverCryptoAes128Cbc, LTHardwareCryptoAes128Cbc) {
} LTOBJECT_API;

static LTSystemCryptoResult LTHardwareCryptoAes128Cbc_Encrypt(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText) {
    return PerformCipherOp(EVP_aes_128_cbc(), key, iv, plainText, textLen, cipherText, true);
}

static LTSystemCryptoResult LTHardwareCryptoAes128Cbc_Decrypt(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_CTR_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText) {
    return PerformCipherOp(EVP_aes_128_cbc(), key, iv, cipherText, textLen, plainText, false);
}

static void LTHardwareCryptoAes128Cbc_DestructObject(LTHardwareCryptoAes128Cbc *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoAes128Cbc_ConstructObject(LTHardwareCryptoAes128Cbc *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoAes128Cbc, LTHardwareCryptoAes128Cbc,
    Encrypt,
    Decrypt,
);

typedef_LTObjectImpl(LTDriverCryptoAes128Xts, LTHardwareCryptoAes128Xts) {
} LTOBJECT_API;

static LTSystemCryptoResult LTHardwareCryptoAes128Xts_Encrypt(const u8 key1[AES128_KEY_LENGTH], const u8 key2[AES128_KEY_LENGTH], const u8 iv[AES128_XTS_IV_LENGTH], const u8 *plainText, LT_SIZE textLen, u8 *cipherText) {
    return PerformAES128XTSOp(key1, key2, iv, plainText, textLen, cipherText, true);
}

static LTSystemCryptoResult LTHardwareCryptoAes128Xts_Decrypt(const u8 key1[AES128_KEY_LENGTH], const u8 key2[AES128_KEY_LENGTH], const u8 iv[AES128_XTS_IV_LENGTH], const u8 *cipherText, LT_SIZE textLen, u8 *plainText) {
    return PerformAES128XTSOp(key1, key2, iv, cipherText, textLen, plainText, false);
}

static void LTHardwareCryptoAes128Xts_DestructObject(LTHardwareCryptoAes128Xts *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoAes128Xts_ConstructObject(LTHardwareCryptoAes128Xts *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoAes128Xts, LTHardwareCryptoAes128Xts,
    Encrypt,
    Decrypt,
);

typedef_LTObjectImpl(LTDriverCryptoAes128Gcm, LTHardwareCryptoAes128Gcm) {
} LTOBJECT_API;

static LTSystemCryptoResult LTHardwareCryptoAes128Gcm_Encrypt(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_GCM_IV_LENGTH], const u8 *aad, LT_SIZE aadLen, const u8 *plainText, LT_SIZE textLen, u8 *cipherText, u8 *tag, LT_SIZE tagLen) {
    LTSystemCryptoResult  result = kLTSystemCrypto_Result_Error;
    EVP_CIPHER_CTX       *ctx = NULL;
    do {
        ctx = PrepareES128GCMOp(key, iv, aad, aadLen, true);
        if (!ctx) break;
        int outputLen;
        if (EVP_EncryptUpdate(ctx, cipherText, &outputLen, plainText, textLen) != 1) break;
        cipherText += outputLen;
        if (EVP_EncryptFinal(ctx, cipherText, &outputLen) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tagLen, tag) != 1) break;
        result = kLTSystemCrypto_Result_Ok;
    } while (false);
    EVP_CIPHER_CTX_free(ctx);
    return result;
}

static LTSystemCryptoResult LTHardwareCryptoAes128Gcm_Decrypt(const u8 key[AES128_KEY_LENGTH], const u8 iv[AES128_GCM_IV_LENGTH], const u8 *aad, LT_SIZE aadLen, const u8 *cipherText, LT_SIZE textLen, const u8 *tag, LT_SIZE tagLen, u8 *plainText) {
    LTSystemCryptoResult  result = kLTSystemCrypto_Result_Error;
    EVP_CIPHER_CTX       *ctx = NULL;
    do {
        ctx = PrepareES128GCMOp(key, iv, aad, aadLen, false);
        if (!ctx) break;
        int outputLen;
        if (EVP_DecryptUpdate(ctx, plainText, &outputLen, cipherText, textLen) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tagLen, (void *)tag) != 1) break;
        plainText += outputLen;
        if (EVP_DecryptFinal(ctx, plainText, &outputLen) != 1) break;
        result = kLTSystemCrypto_Result_Ok;
    } while (false);
    EVP_CIPHER_CTX_free(ctx);
    return result;
}

static void LTHardwareCryptoAes128Gcm_DestructObject(LTHardwareCryptoAes128Gcm *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoAes128Gcm_ConstructObject(LTHardwareCryptoAes128Gcm *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoAes128Gcm, LTHardwareCryptoAes128Gcm,
    Encrypt,
    Decrypt,
);

// Objects: Seq SHA1, Seq SHA256, Seq HMAC-SHA1, Seq HMAC-SHA256
typedef_LTObjectImpl(LTDriverCryptoSeqSha1, LTHardwareCryptoSeqSha1) {
} LTOBJECT_API;

static LT_SHA_CTX *LTHardwareCryptoSeqSha1_CreateSeqSha(void) {
    return PrepareSeqHash(EVP_sha1());
}

static LT_SHA_CTX *LTHardwareCryptoSeqSha1_CloneSeqSha(LT_SHA_CTX *ctx) {
    return CloneSeqHash(ctx);
}

static void LTHardwareCryptoSeqSha1_DestroySeqSha(LT_SHA_CTX *ctx) {
    EVP_MD_CTX_destroy(ctx);
}

static LTSystemCryptoResult LTHardwareCryptoSeqSha1_UpdateSeqSha(LT_SHA_CTX *ctx, const u8 *data, LT_SIZE dataLen) {
    return UpdateSeqHash(ctx, data, dataLen);
}

static LTSystemCryptoResult LTHardwareCryptoSeqSha1_FinishSeqSha(LT_SHA_CTX *ctx, u8 digest[SHA256_HASH_LENGTH]) {
    return FinishSeqHash(ctx, digest, SHA1_HASH_LENGTH);
}

static void LTHardwareCryptoSeqSha1_DestructObject(LTHardwareCryptoSeqSha1 *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoSeqSha1_ConstructObject(LTHardwareCryptoSeqSha1 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoSeqSha1, LTHardwareCryptoSeqSha1,
    CreateSeqSha,
    CloneSeqSha,
    DestroySeqSha,
    UpdateSeqSha,
    FinishSeqSha,
);

typedef_LTObjectImpl(LTDriverCryptoSeqSha256, LTHardwareCryptoSeqSha256) {
} LTOBJECT_API;

static LT_SHA_CTX *LTHardwareCryptoSeqSha256_CreateSeqSha(void) {
    return PrepareSeqHash(EVP_sha256());
}

static LT_SHA_CTX *LTHardwareCryptoSeqSha256_CloneSeqSha(LT_SHA_CTX *ctx) {
    return CloneSeqHash(ctx);
}

static void LTHardwareCryptoSeqSha256_DestroySeqSha(LT_SHA_CTX *ctx) {
    EVP_MD_CTX_destroy(ctx);
}

static LTSystemCryptoResult LTHardwareCryptoSeqSha256_UpdateSeqSha(LT_SHA_CTX *ctx, const u8 *data, LT_SIZE dataLen) {
    return UpdateSeqHash(ctx, data, dataLen);
}

static LTSystemCryptoResult LTHardwareCryptoSeqSha256_FinishSeqSha(LT_SHA_CTX *ctx, u8 digest[SHA256_HASH_LENGTH]) {
    return FinishSeqHash(ctx, digest, SHA256_HASH_LENGTH);
}

static void LTHardwareCryptoSeqSha256_DestructObject(LTHardwareCryptoSeqSha256 *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoSeqSha256_ConstructObject(LTHardwareCryptoSeqSha256 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoSeqSha256, LTHardwareCryptoSeqSha256,
    CreateSeqSha,
    CloneSeqSha,
    DestroySeqSha,
    UpdateSeqSha,
    FinishSeqSha,
);

typedef_LTObjectImpl(LTDriverCryptoSeqHmacSha1, LTHardwareCryptoSeqHmacSha1) {
} LTOBJECT_API;

static LT_HMAC_CTX *LTHardwareCryptoSeqHmacSha1_CreateSeqHmac(const u8 *key, LT_SIZE keyLen) {
    return PrepareSeqHMACHash(EVP_sha1(), key, keyLen);
}

static LT_HMAC_CTX *LTHardwareCryptoSeqHmacSha1_CloneSeqHmac(LT_HMAC_CTX *hmacHashCtx) {
    return CloneSeqHMACHash(hmacHashCtx);
}

static void LTHardwareCryptoSeqHmacSha1_DestroySeqHmac(LT_HMAC_CTX *hmacHashCtx) {
    HMAC_CTX_free(hmacHashCtx);
}

static LTSystemCryptoResult LTHardwareCryptoSeqHmacSha1_UpdateSeqHmac(LT_HMAC_CTX *hmacHashCtx, const u8 *data, LT_SIZE dataLen) {
    return UpdateSeqHMACHash(hmacHashCtx, data, dataLen);
}

static LTSystemCryptoResult LTHardwareCryptoSeqHmacSha1_FinishSeqHmac(LT_HMAC_CTX *hmacHashCtx, u8 *hmac) {
    return FinishSeqHMACHash(hmacHashCtx, hmac, SHA1_HASH_LENGTH);
}

static void LTHardwareCryptoSeqHmacSha1_DestructObject(LTHardwareCryptoSeqHmacSha1 *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoSeqHmacSha1_ConstructObject(LTHardwareCryptoSeqHmacSha1 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoSeqHmacSha1, LTHardwareCryptoSeqHmacSha1,
    CreateSeqHmac,
    CloneSeqHmac,
    DestroySeqHmac,
    UpdateSeqHmac,
    FinishSeqHmac,
);

typedef_LTObjectImpl(LTDriverCryptoSeqHmacSha256, LTHardwareCryptoSeqHmacSha256) {
} LTOBJECT_API;

static LT_HMAC_CTX *LTHardwareCryptoSeqHmacSha256_CreateSeqHmac(const u8 *key, LT_SIZE keyLen) {
    return PrepareSeqHMACHash(EVP_sha256(), key, keyLen);
}

static LT_HMAC_CTX *LTHardwareCryptoSeqHmacSha256_CloneSeqHmac(LT_HMAC_CTX *hmacHashCtx) {
    return CloneSeqHMACHash(hmacHashCtx);
}

static void LTHardwareCryptoSeqHmacSha256_DestroySeqHmac(LT_HMAC_CTX *hmacHashCtx) {
    HMAC_CTX_free(hmacHashCtx);
}

static LTSystemCryptoResult LTHardwareCryptoSeqHmacSha256_UpdateSeqHmac(LT_HMAC_CTX *hmacHashCtx, const u8 *data, LT_SIZE dataLen) {
    return UpdateSeqHMACHash(hmacHashCtx, data, dataLen);
}

static LTSystemCryptoResult LTHardwareCryptoSeqHmacSha256_FinishSeqHmac(LT_HMAC_CTX *hmacHashCtx, u8 *hmac) {
    return FinishSeqHMACHash(hmacHashCtx, hmac, SHA256_HASH_LENGTH);
}

static void LTHardwareCryptoSeqHmacSha256_DestructObject(LTHardwareCryptoSeqHmacSha256 *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoSeqHmacSha256_ConstructObject(LTHardwareCryptoSeqHmacSha256 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoSeqHmacSha256, LTHardwareCryptoSeqHmacSha256,
    CreateSeqHmac,
    CloneSeqHmac,
    DestroySeqHmac,
    UpdateSeqHmac,
    FinishSeqHmac,
);