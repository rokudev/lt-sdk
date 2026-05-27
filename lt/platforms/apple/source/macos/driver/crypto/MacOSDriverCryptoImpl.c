/*******************************************************************************
 * platforms/macos/source/macos/driver/crypto/MacOSDriverCryptoImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/device/crypto/LTDeviceCrypto.h>
#include "MacOSDriverCrypto.h"

// Must select crypto options here
// We are using LTSoftwareCrypto in MacOS
static const LTSystemCryptoOptions s_kOptions = {
    .enabled     = 0,
    .random      = 0,
    .sha256      = 0,
    .hmacSha256  = 0,
    .aes128Gcm   = 0,
    .eddsa       = 0,
    .ecdhe       = 0,
    .seqSha256   = 0,
};

// DEFINE_LTLOG_SECTION("macos.drv.crypto");

/******************************************************************************
 * ILTCrypto functions
 * Function descriptions in lt/system/crypto/LTSystemCrypto.h
 * Only GetOptions is needed in MacOS
 */

static void MacOSDriverCrypto_GetEntropy(u8 entropy[SHA256_HASH_LENGTH]) {
    MacOS_GetEntropy(entropy, SHA256_HASH_LENGTH);
}

static void MacOSDriverCrypto_GetOptions(LTSystemCryptoOptions * options) {
    if (options) {
        options->val = s_kOptions.val;
    }
}

static bool MacOSDriverCrypto_InitCrypto(const LTSystemCryptoConsts * cryptoConsts) {
    LT_UNUSED(cryptoConsts);
    return true;
}
/************************ end of ILTCrypto functions *************************/

define_LTLIBRARY_INTERFACE(ILTCrypto) {
    .InitCrypto       = &MacOSDriverCrypto_InitCrypto,
    .GetEntropy       = &MacOSDriverCrypto_GetEntropy,
    .GetOptions       = &MacOSDriverCrypto_GetOptions,
} LTLIBRARY_DEFINITION;

/******************************************************************************
 * MacOSDriverCrypto driver library prototype functions
 */

static ILTCrypto s_ILTCrypto;

static u32 MacOSDriverCryptoImpl_GetNumDeviceUnits(void) {
    return 1;
}

static LTDeviceUnit MacOSDriverCryptoImpl_CreateDeviceUnitHandle(u32 deviceUnitNumber) {
    return (kLTDeviceCryptoUnit_Normal == deviceUnitNumber) ? LT_GetCore()->CreateHandle((LTInterface *)&s_ILTCrypto, 1) : 0;
}

// static void OnDestroyHandle(LTHandle hDevice) { return; }

/******************************************************************************
 * Must do: Library initialization and deinitialization:
 */

static bool MacOSDriverCryptoImpl_LibInit(void) {
    return true;
}

static void MacOSDriverCryptoImpl_LibFini(void) {
    return;
}

/******************************************************************************
 * LTDriverCrypto public interface api implementation
 */

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceCrypto, MacOSDriverCrypto);

LTLIBRARY_EXPORT_INTERFACES(MacOSDriverCrypto, (ILTCrypto))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  24-Jan-22   gallienus   created
 *  03-Jul-23   constantine copied from LinuxDriverCryptoImpl
 */
