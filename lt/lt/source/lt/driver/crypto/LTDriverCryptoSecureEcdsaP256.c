/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoSecureEcdsaP256.c
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

typedef_LTObjectImpl(LTSecureCryptoEcdsaP256, LTSecureCryptoEcdsaP256Impl) {
} LTOBJECT_API;

static LTSystemCryptoResult LTSecureCryptoEcdsaP256Impl_SignHash(const LTDriverCrypto_KeyReference *keyReference, const u8 hash[SHA256_HASH_LENGTH], u8 signature[ECDSA_P256_SIGNATURE_LENGTH], u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    if (!keyReference || !hash || !signature) return kLTSystemCrypto_Result_Null;
    if (keyReference->secureKeyType != kLTDriverCrypto_SecureKeyType_Provision) return kLTSystemCrypto_Result_Error;
    LTPrivateKey privateKey;
    if (!LTSoftwareCrypto_GetPrivateKeyByProvisionId(keyReference->provisionId, &privateKey)) return kLTSystemCrypto_Result_Error;
    LTSystemCrypto *crypto = lt_openlibrary(LTSystemCrypto);
    if (!crypto) return kLTSystemCrypto_Result_Error;
    LTSystemCryptoResult ret = crypto->SignEcdsaHash(privateKey.key, hash, signature, publicKey);
    LTSoftwareCrypto_FreePrivateKey(&privateKey);
    lt_closelibrary(crypto);
    return ret;
}

static LTSystemCryptoResult LTSecureCryptoEcdsaP256Impl_Sign(const LTDriverCrypto_KeyReference *keyReference, const u8 *data, LT_SIZE dataLen, u8 signature[ECDSA_P256_SIGNATURE_LENGTH], u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    if (!keyReference || !data || !signature) return kLTSystemCrypto_Result_Null;
    if (keyReference->secureKeyType != kLTDriverCrypto_SecureKeyType_Provision) return kLTSystemCrypto_Result_Error;
    LTPrivateKey privateKey;
    if (!LTSoftwareCrypto_GetPrivateKeyByProvisionId(keyReference->provisionId, &privateKey)) return kLTSystemCrypto_Result_Error;
    LTSystemCrypto *crypto = lt_openlibrary(LTSystemCrypto);
    if (!crypto) return kLTSystemCrypto_Result_Error;
    LTSystemCryptoResult ret = crypto->SignEcdsa(privateKey.key, data, dataLen, signature, publicKey);
    LTSoftwareCrypto_FreePrivateKey(&privateKey);
    lt_closelibrary(crypto);
    return ret;
}

static LTSystemCryptoResult LTSecureCryptoEcdsaP256Impl_VerifyHash(const u8 *hash, const u8 *signature, const u8 *publicKey) {
    if (!hash || !signature || !publicKey) return kLTSystemCrypto_Result_Null;
    LTSystemCrypto *crypto = lt_openlibrary(LTSystemCrypto);
    if (!crypto) return kLTSystemCrypto_Result_Error;
    LTSystemCryptoResult ret = crypto->VerifyEcdsaHash(hash, signature, publicKey);
    lt_closelibrary(crypto);
    return ret;
}

static LTSystemCryptoResult LTSecureCryptoEcdsaP256Impl_Verify(const u8 *data, LT_SIZE dataLen, const u8 *signature, const u8 *publicKey) {
    if (!data || !signature || !publicKey) return kLTSystemCrypto_Result_Null;
    LTSystemCrypto *crypto = lt_openlibrary(LTSystemCrypto);
    if (!crypto) return kLTSystemCrypto_Result_Error;
    LTSystemCryptoResult ret = crypto->VerifyEcdsa(data, dataLen, signature, publicKey);
    lt_closelibrary(crypto);
    return ret;
}

static LTSystemCryptoResult LTSecureCryptoEcdsaP256Impl_GenPublicKey(const LTDriverCrypto_KeyReference *keyReference, u8 *publicKey) {
    if (!keyReference || !publicKey) return kLTSystemCrypto_Result_Null;
    if (keyReference->secureKeyType != kLTDriverCrypto_SecureKeyType_Provision) return kLTSystemCrypto_Result_Error;
    LTPrivateKey privateKey;
    if (!LTSoftwareCrypto_GetPrivateKeyByProvisionId(keyReference->provisionId, &privateKey)) return kLTSystemCrypto_Result_Error;
    LTSystemCrypto *crypto = lt_openlibrary(LTSystemCrypto);
    if (!crypto) return kLTSystemCrypto_Result_Error;
    LTSystemCryptoResult ret = crypto->GenEcdsaPublicKey(privateKey.key, publicKey);
    lt_closelibrary(crypto);
    return ret;
}

static void LTSecureCryptoEcdsaP256Impl_DestructObject(LTSecureCryptoEcdsaP256Impl *instance) {
    LT_UNUSED(instance);
}

static bool LTSecureCryptoEcdsaP256Impl_ConstructObject(LTSecureCryptoEcdsaP256Impl *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTSecureCryptoEcdsaP256, LTSecureCryptoEcdsaP256Impl,
    SignHash,
    Sign,
    VerifyHash,
    Verify,
    GenPublicKey,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Jun-25   gallienus   created
 */
