/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/Esp32DriverCryptoImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTThread.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include "Esp32DriverCrypto.h"

// DEFINE_LTLOG_SECTION("esp32.drv.crypto");

// Must select hardware crypto options here
static const LTSystemCryptoOptions s_kHwOptions = {
    .enabled       = 1,
    .random        = 1,
    .sha256        = 1,
    .hmacSha256    = 1,
    .aes128Gcm     = 1,
    .ecdhe         = 1,
};

// Must select software crypto options here, match LTSoftwareCrypto.mk
static const LTSystemCryptoOptions s_kSwOptions = {
    .enabled       = 1,
    .random        = 1,
    .sha1          = 1,
    .sha256        = 1,
    .hmacSha256    = 1,
    .seqSha1       = 1,
    .seqSha256     = 1,
    .seqHmacSha256 = 1,
    .ecdsa         = 1,
};

static LTMutex *s_shaMutex = NULL; // For both SHA/HMAC and RSA
static LTMutex *s_aesMutex = NULL;

void Esp32DriverCrypto_LockShaMutex(void) {
    s_shaMutex->API->Lock(s_shaMutex);
}

void Esp32DriverCrypto_UnlockShaMutex(void) {
    s_shaMutex->API->Unlock(s_shaMutex);
}

void Esp32DriverCrypto_LockAesMutex(void) {
    s_aesMutex->API->Lock(s_aesMutex);
}

void Esp32DriverCrypto_UnlockAesMutex(void) {
    s_aesMutex->API->Unlock(s_aesMutex);
}

/******************************************************************************
 * Esp32DriverCrypto driver object and library
 */
typedef_LTObjectImpl(LTHardwareCrypto, LTHardwareCryptoImpl) {
} LTOBJECT_API;

static void LTHardwareCryptoImpl_GetOptions(LTSystemCryptoOptions *hardwareOptions, LTSystemCryptoOptions *softwareOptions) {
    if (hardwareOptions) hardwareOptions->val = s_kHwOptions.val;
    if (softwareOptions) softwareOptions->val = s_kSwOptions.val;
}

static void LTHardwareCryptoImpl_DestructObject(LTHardwareCryptoImpl *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoImpl_ConstructObject(LTHardwareCryptoImpl *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTHardwareCrypto, LTHardwareCryptoImpl,
    GetOptions,
);

/******************************************************************************
 * Must do: Library initialization and deinitialization:
 */

static void Esp32DriverCrypto_LibFini(void) {
    // Global disable to make sure all engines off when library is closed.
    ESP32_SHA_Disable();
    ESP32_AES_Disable();
    ESP32_RSA_Disable();
    lt_destroyobject(s_shaMutex);
    lt_destroyobject(s_aesMutex);
    s_shaMutex = NULL;
    s_aesMutex = NULL;
}

static bool Esp32DriverCrypto_LibInit(void) {
    /* We are not doing global engine enable to save some energy.
     * To avoid conflicting enable/disable and mutex
     * Only the crypto functions in this file shall enable or disable engine and shall acquire or release mutex.
     * No functions in other files of this crypto library shall do. */
    do {
        s_shaMutex = lt_createobject(LTMutex);
        s_aesMutex = lt_createobject(LTMutex);
        return true;
    } while (false);
    Esp32DriverCrypto_LibFini();
    return false;
}

define_LTObjectLibrary(1, Esp32DriverCrypto_LibInit, Esp32DriverCrypto_LibFini);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  26-May-22   gallienus   created
 */
