/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoSeqSha256.c
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

typedef_LTObjectImpl(LTDriverCryptoSeqSha256, LTSoftwareCryptoSeqSha256) {
} LTOBJECT_API;

static LT_SHA_CTX *LTSoftwareCryptoSeqSha256_CreateSeqSha(void) {
    LT_SHA256_CTX *ctx = lt_malloc(sizeof(LT_SHA256_CTX));
    if (ctx) {
        if (LT_SHA256_Init(ctx) == kLTSystemCrypto_Result_Ok) return ctx;
        lt_free(ctx);
    }
    return NULL;
}

static LT_SHA_CTX *LTSoftwareCryptoSeqSha256_CloneSeqSha(LT_SHA_CTX *ctx) {
    if (!ctx) return NULL;

    LT_SHA256_CTX *copy = lt_malloc(sizeof(LT_SHA256_CTX));
    if (copy) *copy = *(LT_SHA256_CTX *)ctx;
    return copy;
}

static void LTSoftwareCryptoSeqSha256_DestroySeqSha(LT_SHA_CTX *ctx) {
    lt_free(ctx);
}

static LTSystemCryptoResult LTSoftwareCryptoSeqSha256_UpdateSeqSha(LT_SHA_CTX *ctx, const u8 *data, LT_SIZE dataLen) {
    return LT_SHA256_Update((LT_SHA256_CTX *)ctx, data, dataLen);
}

static LTSystemCryptoResult LTSoftwareCryptoSeqSha256_FinishSeqSha(LT_SHA_CTX *ctx, u8 digest[SHA256_HASH_LENGTH]) {
    return LT_SHA256_Finish((LT_SHA256_CTX *)ctx, digest);
}

static void LTSoftwareCryptoSeqSha256_DestructObject(LTSoftwareCryptoSeqSha256 *instance) {
    LT_UNUSED(instance);
}

static bool LTSoftwareCryptoSeqSha256_ConstructObject(LTSoftwareCryptoSeqSha256 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoSeqSha256, LTSoftwareCryptoSeqSha256,
    CreateSeqSha,
    CloneSeqSha,
    DestroySeqSha,
    UpdateSeqSha,
    FinishSeqSha,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  30-Apr-25   gallienus   created
 */
