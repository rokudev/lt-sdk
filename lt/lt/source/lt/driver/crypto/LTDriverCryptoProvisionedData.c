/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoProvisionedData.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/identity/LTDeviceIdentity.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCryptoDefs.h>
#include <lt/system/settings/LTSystemSettings.h>

DEFINE_LTLOG_SECTION("soft.provisioned")

static const char s_privateKeySettingPrefix[] = "/crypto/private/";   // private key setting prefix, read-only and read-protected to applications.

void LTSoftwareCrypto_FreePrivateKey(LTPrivateKey *key) {
    if (!key) return;
    if (key->key) lt_memset(key->key, 0, key->keyLen);  // clean private key
    lt_free(key->key);
    lt_memset(key, 0, sizeof(LTPrivateKey));
}

/* The LTPrivateKey is saved as type (2B) || keyLen (2B) || key */
static bool GetPrivateKey(const char *name, LTPrivateKey *key) {
    u8 *buf = NULL;
    u16 bufLen = 0;
    bool bRet = false;
    lt_memset(key, 0, sizeof(LTPrivateKey));
    LTSystemSettings *settings;

    do {
        // get length of binary data
        settings = lt_openlibrary(LTSystemSettings);
        if (!settings) break;
        u32 len = 4096;
        if (!settings->GetBinaryValue(name, NULL, &len)) break;
        if (len < sizeof(u16) * 2 || len > kLTDriverCrypto_MaxKeySize) break;
        // get binary data
        bufLen = len;
        buf = lt_malloc(bufLen);
        if (!buf) break;
        u8 *end = buf + bufLen;
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
        lt_memset(buf, 0, len);
        bRet = true;
    } while (false);

    if (!bRet) LTSoftwareCrypto_FreePrivateKey(key);
    if (buf) {
        lt_memset(buf, 0, bufLen);
        lt_free(buf);
    }
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

bool LTSoftwareCrypto_GetPrivateKeyByProvisionId(LTDriverCrypto_ProvisionId ProvisionId, LTPrivateKey *privateKey) {
    const char *keyName = NULL;
    switch (ProvisionId) {
        case kLTDriverCrypto_ProvisionId_CommPrivate:
            keyName = "comm";
            break;
        case kLTDriverCrypto_ProvisionId_AliasPrivate:
            keyName = "alias";
            break;
        default: ;
    }
    if (!keyName) {
        LTLOG_YELLOWALERT("no.keyid", "0x%lX", LT_Pu32(ProvisionId));
        return false;
    }
    return GetLTPrivateKey(keyName, privateKey);
}

/** Return key length on success, or 0 on failure */
u16 LTSoftwareCrypto_GetAesKeyByProvisionId(LTDriverCrypto_ProvisionId provisionId, u8 *key) {
    LTDeviceIdentity *identity = lt_openlibrary(LTDeviceIdentity);
    if (!identity) return 0;
    u16 keyLen = 0;
    if (provisionId == kLTDriverCrypto_ProvisionId_AesKey1) {
        if (identity->GetAESKey(key, 1)) keyLen = AES128_KEY_LENGTH;
    } else if (provisionId == kLTDriverCrypto_ProvisionId_AesKey2 || provisionId == kLTDriverCrypto_ProvisionId_SwupKey) {
        // In this common driver, SWUP key is the same as AES key 2.
        // If SWUP key is derived from AES key 2, it should be implemented in platform-specific driver.
        if (identity->GetAESKey(key, 2)) keyLen = AES128_KEY_LENGTH;
    } else if (provisionId == kLTDriverCrypto_ProvisionId_Uds) {
        if (identity->GetUds(key)) keyLen = LTSYSTEMCRYPTO_BYTES_PER_U256;
    }
    lt_closelibrary(identity);
    return keyLen;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Jun-25   gallienus   created
 */
