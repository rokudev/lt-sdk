/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoSecureEd25519.c
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

typedef_LTObjectImpl(LTSecureCryptoEd25519, LTSecureCryptoEd25519Impl) {
} LTOBJECT_API;

static LTSystemCryptoResult LTSecureCryptoEd25519Impl_Sign(const LTDriverCrypto_KeyReference *keyReference, const u8 *data, LT_SIZE dataLen, u8 signature[EdDSA_SIGNATURE_LENGTH], u8 publicKey[EdDSA_KEY_LENGTH]) {
    if (!keyReference || !data || !signature) return kLTSystemCrypto_Result_Null;
    if (keyReference->secureKeyType != kLTDriverCrypto_SecureKeyType_Provision) return kLTSystemCrypto_Result_Error;
    LTPrivateKey privateKey;
    if (!LTSoftwareCrypto_GetPrivateKeyByProvisionId(keyReference->provisionId, &privateKey)) return kLTSystemCrypto_Result_Error;
    LTSystemCrypto *crypto = lt_openlibrary(LTSystemCrypto);
    if (!crypto) return kLTSystemCrypto_Result_Error;
    LTSystemCryptoResult ret = crypto->SignEddsa(privateKey.key, data, dataLen, signature, publicKey);
    LTSoftwareCrypto_FreePrivateKey(&privateKey);
    lt_closelibrary(crypto);
    return ret;
}

static LTSystemCryptoResult LTSecureCryptoEd25519Impl_Verify(const u8 *data, LT_SIZE dataLen, const u8 *signature, const u8 *publicKey) {
    if (!data || !signature || !publicKey) return kLTSystemCrypto_Result_Null;
    LTSystemCrypto *crypto = lt_openlibrary(LTSystemCrypto);
    if (!crypto) return kLTSystemCrypto_Result_Error;
    LTSystemCryptoResult ret = crypto->VerifyEddsa(data, dataLen, signature, publicKey);
    lt_closelibrary(crypto);
    return ret;
}

static LTSystemCryptoResult LTSecureCryptoEd25519Impl_GenPublicKey(const LTDriverCrypto_KeyReference *keyReference, u8 *publicKey) {
    if (!keyReference || !publicKey) return kLTSystemCrypto_Result_Null;
    if (keyReference->secureKeyType != kLTDriverCrypto_SecureKeyType_Provision) return kLTSystemCrypto_Result_Error;
    LTPrivateKey privateKey;
    if (!LTSoftwareCrypto_GetPrivateKeyByProvisionId(keyReference->provisionId, &privateKey)) return kLTSystemCrypto_Result_Error;
    LTSystemCrypto *crypto = lt_openlibrary(LTSystemCrypto);
    if (!crypto) return kLTSystemCrypto_Result_Error;
    LTSystemCryptoResult ret = crypto->GenEddsaPublicKey(privateKey.key, publicKey);
    lt_closelibrary(crypto);
    return ret;
}

static void LTSecureCryptoEd25519Impl_DestructObject(LTSecureCryptoEd25519Impl *instance) {
    LT_UNUSED(instance);
}

static bool LTSecureCryptoEd25519Impl_ConstructObject(LTSecureCryptoEd25519Impl *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTSecureCryptoEd25519, LTSecureCryptoEd25519Impl,
    Sign,
    Verify,
    GenPublicKey,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Jun-25   gallienus   created
 */
