/*******************************************************************************
 * platforms/linux/source/linux/driver/crypto/LinuxDriverCryptoImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>

// DEFINE_LTLOG_SECTION("linux.drv.crypto");

// Must select hardware crypto options here
static const LTSystemCryptoOptions s_kHwOptions = {
    .enabled       = 0,   /* Entropy is enabled as long as the hw crypto is loaded. */
};

// Must select software crypto options here, match LTSoftwareCrypto.mk
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
    .ecdhe         = 1,
    .seqSha1       = 1,
    .seqSha256     = 1,
    .seqHmacSha256 = 1,
    .ecdsa         = 1,
};

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

static void LinuxDriverCrypto_LibFini(void) {
}

static bool LinuxDriverCrypto_LibInit(void) {
    return true;
}

define_LTObjectLibrary(1, LinuxDriverCrypto_LibInit, LinuxDriverCrypto_LibFini);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  24-Jan-22   gallienus   created
 *  05-May-25   gallienus   refactor to objects.
 */
