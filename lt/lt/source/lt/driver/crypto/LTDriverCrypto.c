/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCrypto.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/LT.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include "LTDriverCrypto.h"

// Must select software crypto options here
static const LTSystemCryptoOptions s_kSwOptions = {
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
    .eddsa         = 1,
    .ecdhe         = 1,
    .seqSha1       = 1,
    .seqSha256     = 1,
    .seqHmacSha1   = 1,
    .seqHmacSha256 = 1,
    .ecdsa         = 1,
};

static LTMutex *s_mutex = NULL;

// Internal APIs
void LTSoftwareCrypto_LockMutex(void) {
    s_mutex->API->Lock(s_mutex);
}

void LTSoftwareCrypto_UnlockMutex(void) {
    s_mutex->API->Unlock(s_mutex);
}

// Object APIs
typedef_LTObjectImpl(LTSoftwareCrypto, LTSoftwareCryptoImpl) {
} LTOBJECT_API;

static void LTSoftwareCryptoImpl_GetOptions(LTSystemCryptoOptions *softwareOptions) {
    if (softwareOptions) softwareOptions->val = s_kSwOptions.val;
}

static void LTSoftwareCryptoImpl_DestructObject(LTSoftwareCryptoImpl *instance) {
    LT_UNUSED(instance);
}

static bool LTSoftwareCryptoImpl_ConstructObject(LTSoftwareCryptoImpl *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTSoftwareCrypto, LTSoftwareCryptoImpl,
    GetOptions,
);

static void LTDriverCrypto_LibFini(void) {
    lt_destroyobject(s_mutex);
    s_mutex = NULL;
}

static bool LTDriverCrypto_LibInit(void) {
    return (s_mutex = lt_createobject(LTMutex));
}

define_LTObjectLibrary(1, LTDriverCrypto_LibInit, LTDriverCrypto_LibFini);
