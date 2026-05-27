/*******************************************************************************
 * lt/source/lt/driver/key/LTDriverCryptoKeyManager.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/system/settings/LTSystemSettings.h>

DEFINE_LTLOG_SECTION("device.key")
#define P(...)

static const char s_publicKeySettingPrefix[]  = "/crypto/public/";    // public key setting prefix, read-only to applications.
static const char s_caKeySettingPrefix[]      = "/crypto/cakey/";     // CA public key setting prefix, read-only to applications.
static const char s_privateKeySettingPrefix[] = "/crypto/private/";   // private key setting prefix, read-only and read-protected to applications.

static void FreePublicKey(LTPublicKey *key) {
    if (!key) return;
    lt_free(key->key);
    lt_free(key->keyId);
    lt_memset(key, 0, sizeof(LTPublicKey));
}
/* The LTPublicKey is saved as type (2B) || keyLen (2B) || key || keyIdLen (2B) || keyId */
static bool SetPublicKey(const char *name, const LTPublicKey *key) {
    u16 len = sizeof(u16) + sizeof(u16) + key->keyLen + sizeof(u16) + key->keyIdLen;
    if (len > kLTDriverCrypto_MaxKeySize) return false; // no support over any key block larger than 4 KB

    u8 *buf = lt_malloc(len);
    if (!buf) return false;

    u8 *p = buf;
    // save key type
    p[0] = key->type;
    p[1] = key->type >> 8;
    p += sizeof(u16);
    // save key length
    p[0] = key->keyLen;
    p[1] = key->keyLen >> 8;
    p += sizeof(u16);
    // save key
    lt_memcpy(p, key->key, key->keyLen);
    p += key->keyLen;
    // save keyId length
    p[0] = key->keyIdLen;
    p[1] = key->keyIdLen >> 8;
    p += sizeof(u16);
    // save keyId
    lt_memcpy(p, key->keyId, key->keyIdLen);
    // save binary data
    LTSystemSettings *settings = lt_openlibrary(LTSystemSettings);
    bool bRet = !!settings;
    if (settings) bRet = settings->SetBinaryValue(name, buf, len);
    lt_closelibrary(settings);

    lt_free(buf);
    return bRet;
}

static bool GetPublicKey(const char *name, LTPublicKey *key) {
    LTSystemSettings *settings = lt_openlibrary(LTSystemSettings);
    if (!settings) return false;
    u8 *buf = NULL;
    bool bRet = false;
    lt_memset(key, 0, sizeof(LTPublicKey));

    do {
        // get length of binary data
        u32 len = 4096;
        settings->GetBinaryValue(name, NULL, &len);
        if (len < sizeof(u16) * 2 || len > kLTDriverCrypto_MaxKeySize) break;
        // get binary data
        buf = lt_malloc(len);
        if (!buf) break;
        u8 *end = buf + len;
        bRet = settings->GetBinaryValue(name, buf, &len);
        if (!bRet) break;
        u8 *p = buf;
        // get key type
        key->type = p[0] + (((u16)p[1]) << 8);
        p += sizeof(u16);
        // get key length
        key->keyLen = p[0] + (((u16)p[1]) << 8);
        p += sizeof(u16);
        if (p + key->keyLen > end) break;
        // get key
        key->key = lt_malloc(key->keyLen);
        if (!key->key) break;
        lt_memcpy(key->key, p, key->keyLen);
        p += key->keyLen;
        if (p + sizeof(u16) > end) break;
        // get keyId length
        key->keyIdLen = p[0] + (((u16)p[1]) << 8);
        p += sizeof(u16);
        if (p + key->keyIdLen != end) break;
        // get keyId
        if (key->keyIdLen) {
            key->keyId = lt_malloc(key->keyIdLen);
            if (!key->keyId) break;
            lt_memcpy(key->keyId, p, key->keyIdLen);
        }
        bRet = true;
    } while (false);

    if (!bRet) FreePublicKey(key);
    lt_closelibrary(settings);
    lt_free(buf);
    return bRet;
}

static void FreeLTPrivateKey(LTPrivateKey *key) {
    if (!key) return;
    if (key->key) lt_memset(key->key, 0, key->keyLen);  // clean private key
    lt_free(key->key);
    lt_memset(key, 0, sizeof(LTPrivateKey));
}

/* The LTPrivateKey is saved as type (2B) || keyLen (2B) || key */
static bool GetPrivateKey(const char *name, LTPrivateKey *key) {
    LTSystemSettings *settings = lt_openlibrary(LTSystemSettings);
    if (!settings) return false;
    u8 *buf = NULL;
    u32 len = 0;
    bool bRet = false;
    lt_memset(key, 0, sizeof(LTPrivateKey));

    do {
        // get length of binary data
        settings->GetBinaryValue(name, (u8 *)" ", &len);
        if (len < sizeof(u16) * 2 || len > kLTDriverCrypto_MaxKeySize) break;
        // get binary data
        buf = lt_malloc(len);
        if (!buf) break;
        u8 *end = buf + len;
        bRet = settings->GetBinaryValue(name, buf, &len);
        if (!bRet) break;
        u8 *p = buf;
        // get key type
        key->type = p[0] + (((u16)p[1]) << 8);
        p += sizeof(u16);
        // get key length
        key->keyLen = p[0] + (((u16)p[1]) << 8);
        p += sizeof(u16);
        if (p + key->keyLen != end) break;
        // get key
        key->key = lt_malloc(key->keyLen);
        if (!key->key) break;
        lt_memcpy(key->key, p, key->keyLen);
        bRet = true;
    } while (false);

    if (!bRet) FreeLTPrivateKey(key);
    lt_closelibrary(settings);
    if (buf) lt_memset(buf, 0, len);  // clean private key in buffer
    lt_free(buf);
    return bRet;
}

static bool GetLTPrivateKey(const char *keyName, LTPrivateKey *privateKey) {
    if (!keyName || !privateKey) return false;
    LTString fullName = ltstring_create(s_privateKeySettingPrefix);
    if (!fullName) return false;
    ltstring_append(&fullName, keyName);    // TODO need to check if appending succeeds.
    bool bRet = GetPrivateKey(fullName, privateKey);
    ltstring_destroy(fullName);
    return bRet;
}

typedef_LTObjectImpl(LTDriverCryptoKeyManager, LTDriverCryptoKeyManagerImpl) {
} LTOBJECT_API;

static bool LTDriverCryptoKeyManagerImpl_SetPublicKey(const char *keyName, const LTPublicKey *publicKey, bool bDevice) {
    LT_UNUSED(bDevice); // TODO support if needed.
    if (!keyName || !publicKey) return false;
    LTString fullName = ltstring_create(s_publicKeySettingPrefix);
    if (!fullName) return false;
    ltstring_append(&fullName, keyName);    // TODO need to check if appending succeeds.
    bool bRet = SetPublicKey(fullName, publicKey);
    ltstring_destroy(fullName);
    return bRet;
}

static bool LTDriverCryptoKeyManagerImpl_GetPublicKey(const char *keyName, LTPublicKey *publicKey) {
    if (!keyName || !publicKey) return false;
    LTString fullName = ltstring_create(s_publicKeySettingPrefix);
    if (!fullName) return false;
    ltstring_append(&fullName, keyName);    // TODO need to check if appending succeeds.
    bool bRet = GetPublicKey(fullName, publicKey);
    ltstring_destroy(fullName);
    return bRet;
}

static bool LTDriverCryptoKeyManagerImpl_SetCaPublicKey(const char *caName, const LTPublicKey *publicKey, bool bDevice) {
    LT_UNUSED(bDevice); // TODO support if needed.
    if (!caName || !publicKey) return false;
    LTString fullName = ltstring_create(s_caKeySettingPrefix);
    if (!fullName) return false;
    ltstring_append(&fullName, caName);
    bool bRet = SetPublicKey(fullName, publicKey);
    ltstring_destroy(fullName);
    return bRet;
}

static bool LTDriverCryptoKeyManagerImpl_GetCaPublicKey(const char *caName, LTPublicKey *publicKey) {
    if (!caName || !publicKey) return false;
    LTString fullName = ltstring_create(s_caKeySettingPrefix);
    if (!fullName) return false;
    ltstring_append(&fullName, caName);
    bool bRet = GetPublicKey(fullName, publicKey);
    ltstring_destroy(fullName);
    return bRet;
}

static void LTDriverCryptoKeyManagerImpl_FreePublicKey(LTPublicKey *key) {
    FreePublicKey(key);
}

/* The LTPrivateKey is saved as type (2B) || keyLen (2B) || key */
static bool LTDriverCryptoKeyManagerImpl_SetPrivateKey(const char *keyName, const LTPrivateKey *privateKey, bool bDevice) {
    if (!keyName || !privateKey) return false;

    u16 len = sizeof(u16) + sizeof(u16) + privateKey->keyLen;
    if (len > kLTDriverCrypto_MaxKeySize) return false; // no support over any key block larger than 2 KB

    LTString fullName = NULL;
    u8 *buf = NULL;
    bool bRet = false;
    do {
        if (!(fullName = ltstring_create(s_privateKeySettingPrefix))) break;
        ltstring_append(&fullName, keyName);

        buf = lt_malloc(len);
        if (!buf) break;

        u8 *p = buf;
        // save key type
        p[0] = privateKey->type;
        p[1] = privateKey->type >> 8;
        p += sizeof(u16);
        // save key length
        p[0] = privateKey->keyLen;
        p[1] = privateKey->keyLen >> 8;
        p += sizeof(u16);
        // save key
        lt_memcpy(p, privateKey->key, privateKey->keyLen);
        p += privateKey->keyLen;
        // save binary data
        LTSystemSettings *settings = lt_openlibrary(LTSystemSettings);
        bRet = (settings && settings->SetBinaryValue(fullName, buf, len));
        lt_closelibrary(settings);

        if (bRet && bDevice) {
            // If available, save to device-specific secure storage.
            LTSecureKeyManager *secKeyManager = lt_createobject(LTSecureKeyManager);
            if (secKeyManager) {
                bRet = secKeyManager->API->SetPrivateKey(keyName, privateKey);
                lt_destroyobject(secKeyManager);
            }
        }
    } while (false);

    lt_memset(buf, 0, len);  // clean private key in buffer
    lt_free(buf);
    ltstring_destroy(fullName);
    return bRet;
}

static LTDriverCrypto_KeyReference* LTDriverCryptoKeyManagerImpl_GetPrivateKeyReference(const char *keyName) {
    if (!keyName) return NULL;

    LTDriverCrypto_ProvisionId provisionId = kLTDriverCrypto_ProvisionId_Invalid;
    if (lt_strncmp(keyName, "comm", 4) == 0) provisionId = kLTDriverCrypto_ProvisionId_CommPrivate;
    else if (lt_strncmp(keyName, "alias", 5) == 0) provisionId = kLTDriverCrypto_ProvisionId_AliasPrivate;
    else {
        LTLOG_YELLOWALERT("no.keyname", "%s", keyName);
        return NULL;
    }

    LTPrivateKey privateKey;
    if (!GetLTPrivateKey(keyName, &privateKey)) return NULL;
    LTDriverCrypto_KeyType keyType = privateKey.type;
    FreeLTPrivateKey(&privateKey);

    LTDriverCrypto_KeyReference *keyReference = lt_malloc(sizeof(LTDriverCrypto_KeyReference));
    if (!keyReference) return NULL;
    keyReference->keyType = keyType;
    keyReference->secureKeyType = kLTDriverCrypto_SecureKeyType_Provision;
    keyReference->provisionId = provisionId;
    return keyReference;
}

static LTDriverCrypto_KeyReference* LTDriverCryptoKeyManagerImpl_CopyKeyReference(const LTDriverCrypto_KeyReference *keyReference) {
    // If secure crypto driver implements this API, call driver's.
    LTSecureKeyManager *secureKeyManager = lt_createobject(LTSecureKeyManager);
    if (secureKeyManager) return secureKeyManager->API->CopyKeyReference(keyReference);

    // Only copy LTDriverCrypto_KeyReference, not the referenced data bytes.
    LTDriverCrypto_KeyReference *copy = lt_malloc(sizeof(LTDriverCrypto_KeyReference));
    if (copy) lt_memcpy(copy, keyReference, sizeof(LTDriverCrypto_KeyReference));
    return copy;
}

static void LTDriverCryptoKeyManagerImpl_FreeKeyReference(LTDriverCrypto_KeyReference *keyReference) {
    // If secure crypto driver implements this API, call driver's.
    LTSecureKeyManager *secKeyManager = lt_createobject(LTSecureKeyManager);
    if (secKeyManager) return secKeyManager->API->FreeKeyReference(keyReference);

    lt_free(keyReference);
}

static void LTDriverCryptoKeyManagerImpl_RemoveKey(const LTDriverCrypto_KeyReference *keyReference) {
    // If secure crypto driver implements this API, call driver's.
    LTSecureKeyManager *secKeyManager = lt_createobject(LTSecureKeyManager);
    if (secKeyManager) return secKeyManager->API->RemoveKey(keyReference);

    // Nothing else to do here.
}

static void LTDriverCryptoKeyManagerImpl_DestructObject(LTDriverCryptoKeyManagerImpl *instance) {
    LT_UNUSED(instance);
    P("destruct", NULL);
}

static bool LTDriverCryptoKeyManagerImpl_ConstructObject(LTDriverCryptoKeyManagerImpl *instance) {
    LT_UNUSED(instance);
    P("construct", NULL);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoKeyManager, LTDriverCryptoKeyManagerImpl,
    SetPublicKey,
    GetPublicKey,
    SetCaPublicKey,
    GetCaPublicKey,
    FreePublicKey,
    SetPrivateKey,
    GetPrivateKeyReference,
    CopyKeyReference,
    FreeKeyReference,
    RemoveKey,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  25-Oct-24   gallienus   created
 *  06-Jun-25   gallienus   refactored to objects
 */
