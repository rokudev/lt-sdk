/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/Esp32DriverCrypto25519.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *******************************************************************************/
#include <lt/LTTypes.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>

// Shared curve constants for X25519 and Ed25519
/* C25519_N = 0x7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffed, curve modulus */
static const u32 kC25519_N[LTSYSTEMCRYPTO_U32_PER_U256] = {0xFFFFFFED,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0x7FFFFFFF};
/* C25519_N1 = -1/N % R, then take the lowest u32 */
static const u32 kC25519_N1 = 0x286BCA1B;
/* C25519_R2 = R^2 % N = 0x1fd110, must be wide as RSA512 register */
static const u32 kC25519_R2[LTSYSTEMCRYPTO_U32_PER_U256] = {0x001FD110,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000};
/* C25519_N2 = N - 2 */
static const u32 kC25519_N2[LTSYSTEMCRYPTO_U32_PER_U256] = {0xFFFFFFEB,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0x7FFFFFFF};

const u32 *Esp32_GetC25519_N(void) {
    return kC25519_N;
}

u32 Esp32_GetC25519_N1(void) {
    return kC25519_N1;
}

const u32 *Esp32_GetC25519_R2(void) {
    return kC25519_R2;
}

const u32 *Esp32_GetC25519_N2(void) {
    return kC25519_N2;
}