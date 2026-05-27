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

DEFINE_LTLOG_SECTION("device.cert")
#define P(...)

static const char s_certSettingPrefix[] = "/crypto/cert/";  // certificate setting prefix, read-only to applications.  Could be a certificate chain.

typedef_LTObjectImpl(LTDriverCryptoCertManager, LTDriverCryptoCertManagerImpl) {
} LTOBJECT_API;

static bool LTDriverCryptoCertManagerImpl_SetCertificate(const char *certificateName, const u8 *certificate, u16 certificateLen, bool bChain, bool bDevice) {
    if (!certificateName) return false;

    LTString fullName = NULL;
    LTSystemSettings *settings = NULL;
    u8 *cert = NULL;

    bool bRet = false;
    do {
        if (!(fullName = ltstring_create(s_certSettingPrefix))) break;
        ltstring_append(&fullName, certificateName);
        if (!(settings = lt_openlibrary(LTSystemSettings))) break;

        // Save to settings.
        bRet = true;
        if (certificate && certificateLen) {
            bRet = settings->SetBinaryValue(fullName, certificate, certificateLen);
        }

        // If available, save to device-specific storage.
        if (bRet && bDevice) {
            P("save.to.device", NULL);
            // Because no certificate is passed in, get the certificate from settings.
            if (!certificate) {
                u32 certLen = 4096;
                // No cert in settings, so skip and ignore.
                if (!settings->GetBinaryValue(fullName, NULL, &certLen)) break;
                if (!certLen) break;
                // Read cert from settings and save to device.
                bRet = false;
                if (!(cert = lt_malloc(certLen))) break;
                settings->GetBinaryValue(fullName, cert, &certLen);
                P("read.from.device", "%s %lu", fullName, LT_Pu32(certLen));
                certificate = cert;
                certificateLen = certLen;
            }
            LTSecureCertManager *certManager = lt_createobject(LTSecureCertManager);
            if (certManager) {
                bRet = certManager->API->SetCertificate(certificateName, certificate, certificateLen, bChain);
                lt_destroyobject(certManager);
            }
        }
    } while (false);

    if (!bRet) LTLOG_YELLOWALERT("set.fail", "%s", certificateName);
    lt_free(cert);
    lt_closelibrary(settings);
    ltstring_destroy(fullName);
    return bRet;
}

static bool LTDriverCryptoCertManagerImpl_GetCertificate(const char *certificateName, u8 *certificate, u16 *certificateLen) {
    if (!certificateName || !certificateLen) return false;
    LTSystemSettings *settings = lt_openlibrary(LTSystemSettings);
    if (!settings) return false;
    LTString fullName = ltstring_create(s_certSettingPrefix);
    if (!fullName) {
        lt_closelibrary(settings);
        return false;
    }
    ltstring_append(&fullName, certificateName);
    // If certificate is NULL, the call is to get the size, and need to have a large initial size so that settings can return the actual size.
    u32 len = certificate ? *certificateLen : 4096;
    bool bRet = settings->GetBinaryValue(fullName, certificate, &len);
    if (bRet) *certificateLen = len;
    ltstring_destroy(fullName);
    lt_closelibrary(settings);
    return bRet;
}

static void LTDriverCryptoCertManagerImpl_DestructObject(LTDriverCryptoCertManagerImpl *instance) {
    LT_UNUSED(instance);
    P("destruct", NULL);
}

static bool LTDriverCryptoCertManagerImpl_ConstructObject(LTDriverCryptoCertManagerImpl *instance) {
    LT_UNUSED(instance);
    P("construct", NULL);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoCertManager, LTDriverCryptoCertManagerImpl,
    SetCertificate,
    GetCertificate,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  25-Oct-24   gallienus   created
 */
