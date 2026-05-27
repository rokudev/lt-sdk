/*******************************************************************************
 * platforms/apple/source/macos/driver/crypto/MacOSDriverCrypto.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_APPLE_SOURCE_MACOS_DRIVER_CRYPTO_MACOSDRIVERCRYPTO_H
#define PLATFORMS_APPLE_SOURCE_MACOS_DRIVER_CRYPTO_MACOSDRIVERCRYPTO_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

#include <lt/system/crypto/LTSystemCrypto.h>

/**
 * @brief  Get entropy bytes
 *
 * @param entropy     The buffer to hold the entropy bytes
 * @param entropyLen  The length of entropy to read
 */
void MacOS_GetEntropy(u8 * entropy, LT_SIZE entropyLen);

LT_EXTERN_C_END
#endif  // PLATFORMS_APPLE_SOURCE_MACOS_DRIVER_CRYPTO_MACOSDRIVERCRYPTO_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  02-Mar-22   gallienus   created
 *  03-Jul-23   constantine copied from LinuxDriverCrypto
 */
