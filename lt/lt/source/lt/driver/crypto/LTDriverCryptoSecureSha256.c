/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoSecureSha256.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/LT.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCryptoDefs.h>
#include "LTDriverCrypto.h"

DEFINE_LTLOG_SECTION("soft.crypto.secsha")

typedef_LTObjectImpl(LTSecureCryptoSha256, LTSecureCryptoSha256Impl) {
} LTOBJECT_API;

static LTSystemCryptoResult LTSecureCryptoSha256Impl_GenDigest(const LTDriverCrypto_KeyReference *keyReference, const u8 *data, LT_SIZE dataLen, u8 hash[SHA256_HASH_LENGTH]) {
    if (!keyReference) return kLTSystemCrypto_Result_Null;
    if (keyReference->secureKeyType != kLTDriverCrypto_SecureKeyType_Provision) return kLTSystemCrypto_Result_Error;
    u8 *toHash = lt_malloc(AES128_KEY_LENGTH + dataLen);
    if (!toHash) return kLTSystemCrypto_Result_OOM;

    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;
    u16 keyLen = LTSoftwareCrypto_GetAesKeyByProvisionId(keyReference->provisionId, toHash);
    if (keyLen == AES128_KEY_LENGTH) {
        lt_memcpy(toHash + AES128_KEY_LENGTH, data, dataLen);
        ret = LT_SHA256_Digest(toHash, AES128_KEY_LENGTH + dataLen, hash);
    } else {
        LTLOG_YELLOWALERT("wrong.keylen", "provisionId %u len %u", keyReference->provisionId, keyLen);
    }

    lt_memset(toHash, 0, AES128_KEY_LENGTH);
    lt_free(toHash);
    return ret;
}

static void LTSecureCryptoSha256Impl_DestructObject(LTSecureCryptoSha256Impl *instance) {
    LT_UNUSED(instance);
}

static bool LTSecureCryptoSha256Impl_ConstructObject(LTSecureCryptoSha256Impl *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTSecureCryptoSha256, LTSecureCryptoSha256Impl,
    GenDigest,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Jun-25   gallienus   created
 */
