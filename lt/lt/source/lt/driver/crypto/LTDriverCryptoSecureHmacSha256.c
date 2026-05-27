/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoSecureHmacSha256.c
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

DEFINE_LTLOG_SECTION("soft.crypto.sechmac")

typedef_LTObjectImpl(LTSecureCryptoHmacSha256, LTSecureCryptoHmacSha256Impl) {
} LTOBJECT_API;

static LTSystemCryptoResult LTSecureCryptoHmacSha256Impl_GenHmac(const LTDriverCrypto_KeyReference *keyReference, const u8 *data, LT_SIZE dataLen, u8 hmac[SHA256_HASH_LENGTH]) {
    if (!keyReference) return kLTSystemCrypto_Result_Null;
    if (keyReference->secureKeyType != kLTDriverCrypto_SecureKeyType_Provision) return kLTSystemCrypto_Result_Error;

    u8 key[SHA256_HASH_LENGTH];
    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;
    u16 keyLen = LTSoftwareCrypto_GetAesKeyByProvisionId(keyReference->provisionId, key);
    if (keyLen) {
        ret = LTSoftwareCryptoHmacSha256_GenHmac(key, keyLen, data, dataLen, hmac);
        lt_memset(key, 0, SHA256_HASH_LENGTH);
    } else {
        LTLOG_YELLOWALERT("zero.keylen", "provisionId %u", keyReference->provisionId);
    }
    return ret;
}

static void LTSecureCryptoHmacSha256Impl_DestructObject(LTSecureCryptoHmacSha256Impl *instance) {
    LT_UNUSED(instance);
}

static bool LTSecureCryptoHmacSha256Impl_ConstructObject(LTSecureCryptoHmacSha256Impl *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTSecureCryptoHmacSha256, LTSecureCryptoHmacSha256Impl,
    GenHmac,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Jun-25   gallienus   created
 */
