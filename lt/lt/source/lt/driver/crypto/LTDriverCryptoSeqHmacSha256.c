/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoSeqHmacSha256.c
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

typedef_LTObjectImpl(LTDriverCryptoSeqHmacSha256, LTSoftwareCryptoSeqHmacSha256) {
} LTOBJECT_API;

static LT_HMAC_CTX *LTSoftwareCryptoSeqHmacSha256_CreateSeqHmac(const u8 *key, LT_SIZE keyLen) {
    LT_HMAC_SHA256_CTX *hmacHashCtx = lt_malloc(sizeof(LT_HMAC_SHA256_CTX));
    if (hmacHashCtx) {
        LT_HMAC_CTX_Impl hmacCtx = {
            .pad        = hmacHashCtx->pad,
            .hashCtx    = &hmacHashCtx->hashCtx,
            .blockLen   = SHA256_BLOCK_LENGTH,
            .digestLen  = SHA256_HASH_LENGTH,
            .HashInit   = (LT_Hash_Init)LT_SHA256_Init,
            .HashUpdate = (LT_Hash_Update)LT_SHA256_Update,
            .HashFinish = (LT_Hash_Finish)LT_SHA256_Finish,
            .HashDigest = (LT_Hash_Digest)LT_SHA256_Digest,
        };
        if (LT_HMAC_Init(&hmacCtx, key, keyLen) == kLTSystemCrypto_Result_Ok) return hmacHashCtx;
        lt_free(hmacHashCtx);
    }
    return NULL;
}

static LT_HMAC_CTX *LTSoftwareCryptoSeqHmacSha256_CloneSeqHmac(LT_HMAC_CTX *hmacHashCtx) {
    if (!hmacHashCtx) return NULL;
    LT_HMAC_SHA256_CTX *copy = lt_malloc(sizeof(LT_HMAC_SHA256_CTX));
    if (copy) *copy = *(LT_HMAC_SHA256_CTX *)hmacHashCtx;
    return copy;
}

static void LTSoftwareCryptoSeqHmacSha256_DestroySeqHmac(LT_HMAC_CTX *hmacHashCtx) {
    lt_free(hmacHashCtx);
}

static LTSystemCryptoResult LTSoftwareCryptoSeqHmacSha256_UpdateSeqHmac(LT_HMAC_CTX *hmacHashCtx, const u8 *data, LT_SIZE dataLen) {
    LT_HMAC_CTX_Impl hmacCtx = {
        .pad        = ((LT_HMAC_SHA256_CTX *)hmacHashCtx)->pad,
        .hashCtx    = &((LT_HMAC_SHA256_CTX *)hmacHashCtx)->hashCtx,
        .blockLen   = SHA256_BLOCK_LENGTH,
        .digestLen  = SHA256_HASH_LENGTH,
        .HashInit   = (LT_Hash_Init)LT_SHA256_Init,
        .HashUpdate = (LT_Hash_Update)LT_SHA256_Update,
        .HashFinish = (LT_Hash_Finish)LT_SHA256_Finish,
        .HashDigest = (LT_Hash_Digest)LT_SHA256_Digest,
    };
    return LT_HMAC_Update(&hmacCtx, data, dataLen);
}

static LTSystemCryptoResult LTSoftwareCryptoSeqHmacSha256_FinishSeqHmac(LT_HMAC_CTX *hmacHashCtx, u8 *hmac) {
    LT_HMAC_CTX_Impl hmacCtx = {
        .pad        = ((LT_HMAC_SHA256_CTX *)hmacHashCtx)->pad,
        .hashCtx    = &((LT_HMAC_SHA256_CTX *)hmacHashCtx)->hashCtx,
        .blockLen   = SHA256_BLOCK_LENGTH,
        .digestLen  = SHA256_HASH_LENGTH,
        .HashInit   = (LT_Hash_Init)LT_SHA256_Init,
        .HashUpdate = (LT_Hash_Update)LT_SHA256_Update,
        .HashFinish = (LT_Hash_Finish)LT_SHA256_Finish,
        .HashDigest = (LT_Hash_Digest)LT_SHA256_Digest,
    };
    return LT_HMAC_Finish(&hmacCtx, hmac);
}

static void LTSoftwareCryptoSeqHmacSha256_DestructObject(LTSoftwareCryptoSeqHmacSha256 *instance) {
    LT_UNUSED(instance);
}

static bool LTSoftwareCryptoSeqHmacSha256_ConstructObject(LTSoftwareCryptoSeqHmacSha256 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoSeqHmacSha256, LTSoftwareCryptoSeqHmacSha256,
    CreateSeqHmac,
    CloneSeqHmac,
    DestroySeqHmac,
    UpdateSeqHmac,
    FinishSeqHmac,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  30-Apr-25   gallienus   created
 */
