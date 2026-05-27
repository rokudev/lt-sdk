/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoHmacSha256.c
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

typedef_LTObjectImpl(LTDriverCryptoHmacSha256, LTSoftwareCryptoHmacSha256) {
} LTOBJECT_API;

LTSystemCryptoResult LTSoftwareCryptoHmacSha256_GenHmac(const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE dataLen, u8 *hmac) {
    LT_HMAC_SHA256_CTX *hmacShaCtx = lt_malloc(sizeof(LT_HMAC_SHA256_CTX));
    if (!hmacShaCtx) return kLTSystemCrypto_Result_OOM;

    LT_HMAC_CTX_Impl hmacCtx = {
        .pad        = hmacShaCtx->pad,
        .hashCtx    = &hmacShaCtx->hashCtx,
        .blockLen   = SHA256_BLOCK_LENGTH,
        .digestLen  = SHA256_HASH_LENGTH,
        .HashInit   = (LT_Hash_Init)LT_SHA256_Init,
        .HashUpdate = (LT_Hash_Update)LT_SHA256_Update,
        .HashFinish = (LT_Hash_Finish)LT_SHA256_Finish,
        .HashDigest = (LT_Hash_Digest)LT_SHA256_Digest,
    };
    LTSystemCryptoResult ret = LT_HMAC_Digest(&hmacCtx, key, keyLen, data, dataLen, hmac);

    lt_memset(hmacShaCtx, 0, sizeof(LT_HMAC_SHA256_CTX));
    lt_free(hmacShaCtx);
    return ret;
}

static void LTSoftwareCryptoHmacSha256_DestructObject(LTSoftwareCryptoHmacSha256 *instance) {
    LT_UNUSED(instance);
}

static bool LTSoftwareCryptoHmacSha256_ConstructObject(LTSoftwareCryptoHmacSha256 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoHmacSha256, LTSoftwareCryptoHmacSha256,
    GenHmac,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  30-Apr-25   gallienus   created
 */
