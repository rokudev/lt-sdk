/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoRandom.c
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

static LT_DRBG_HASH_CTX s_randomCtx;
static u8 s_addition[16];
static bool s_bInited = false;

static void SetEntropy(u8 entropy[SHA256_HASH_LENGTH], u8 *extra, LT_SIZE extraLen) {
    LTTime t;
    LTDriverCryptoEntropy *hwEntropy = lt_createobject(LTDriverCryptoEntropy);
    bool g = hwEntropy &&
             hwEntropy->API->GetEntropy(entropy, SHA256_HASH_LENGTH) &&
             (kLTSystemCrypto_Result_Ok == LT_SHA256_Digest(entropy, SHA256_HASH_LENGTH, entropy));
    if (!g) {
        // if the hw entropy source fails, then hash the kernel time
        t = LT_GetCore()->GetKernelTime();
        // take the time and whatever in stack
        LT_SHA256_Digest((u8 *)&t, SHA256_HASH_LENGTH, entropy);
    }

    t = LT_GetCore()->GetKernelTime();
    lt_memset(extra, 0, extraLen);
    lt_memcpy(extra, (u8 *)&t, extraLen < sizeof(t) ? extraLen : sizeof(t));
}

static bool InitRandom(void) {
    // get entropy and nonce
    struct InitLocal {
        u8 entropy[SHA256_HASH_LENGTH];
        u8 nonce[16];
    };
    struct InitLocal *tmp = lt_malloc(sizeof(struct InitLocal));
    if (!tmp) return false;

    SetEntropy(tmp->entropy, tmp->nonce, sizeof(tmp->nonce));

    // init random
    int ret = LT_DRBG_HASH_Init(&s_randomCtx, tmp->entropy, sizeof(tmp->entropy), tmp->nonce, sizeof(tmp->nonce), NULL, 0);

    // clean up
    lt_memset(s_addition, 0, sizeof(s_addition));
    lt_memcpy(s_addition, tmp->nonce, sizeof(s_addition) < sizeof(tmp->nonce) ? sizeof(s_addition) : sizeof(tmp->nonce));

    lt_memset(tmp, 0, sizeof(struct InitLocal));
    lt_free(tmp);

    s_bInited = (ret == kLTSystemCrypto_Result_Ok);
    return s_bInited;
}

typedef_LTObjectImpl(LTDriverCryptoRandom, LTSoftwareCryptoRandom) {
} LTOBJECT_API;

static bool LTSoftwareCryptoRandom_GenRandomBytes(u8 *output, LT_SIZE outputLen) {
    LTSoftwareCrypto_LockMutex();

    if (!s_bInited) {
        if (!InitRandom()) {
            LTSoftwareCrypto_UnlockMutex();
            return false;
        }
    }

    LTSystemCryptoResult ret = LT_DRBG_HASH_Random_Bytes(&s_randomCtx, s_addition, sizeof(s_addition), output, outputLen);

    // need to reseed and then generate bytes
    if (kLTSystemCrypto_Result_NeedToReseed == ret) {
        u8 entropy[SHA256_HASH_LENGTH];
        SetEntropy(entropy, s_addition, sizeof(s_addition));
        LT_DRBG_HASH_Reseed(&s_randomCtx, entropy, sizeof(entropy), s_addition, sizeof(s_addition));
        lt_memset(entropy, 0, sizeof(entropy));
        ret = LT_DRBG_HASH_Random_Bytes(&s_randomCtx, s_addition, sizeof(s_addition), output, outputLen);
    }

    LTSoftwareCrypto_UnlockMutex();
    return (kLTSystemCrypto_Result_Ok == ret);
}

static void LTSoftwareCryptoRandom_DestructObject(LTSoftwareCryptoRandom *instance) {
    LT_UNUSED(instance);
}

static bool LTSoftwareCryptoRandom_ConstructObject(LTSoftwareCryptoRandom *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoRandom, LTSoftwareCryptoRandom,
    GenRandomBytes,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Jun-25   gallienus   created
 */
