/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/Esp32DriverCrypto25519.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_CRYPTO_ESP32DRIVERCRYPTO25519_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_CRYPTO_ESP32DRIVERCRYPTO25519_H

#include <lt/LTTypes.h>
#include <lt/system/crypto/LTSystemCrypto.h>
LT_EXTERN_C_BEGIN

// Extended homogeneous coordinates
typedef struct EdPointExt {
    u256 X;                  // x = X/Z
    u256 Y;                  // y = Y/Z
    u256 Z;
    u256 T;                  // x*y = T/Z
} EdPointExt;

// data struct for temporary data, so we don't typedef it.
struct Esp32C25519ExtLocal {
    u256 A;
    u256 B;
    u256 E;
    u256 F;
    u256 H;
};

struct Esp32C25519ExEdLocal {
    u256 z1;
};

struct Esp32C25519ExMulLocal {
    EdPointExt r0;
    EdPointExt r1;
    struct Esp32C25519ExtLocal eb;
};

struct Esp32C25519EdMulLocal {
    EdPointExt P;
    union {
        struct Esp32C25519ExMulLocal mb;
        struct Esp32C25519ExEdLocal eb;
    };
};

struct Esp32C25519XMulLocal {
    u256 x1;
    u256 x2;
    u256 z2;
    u256 x3;
    u256 z3;
    u256 A;
    u256 B;
    u256 C;
    u256 D;
    u32 *X2;
    u32 *Z2;
    u32 *X3;
    u32 *Z3;
    u32 *Tmp2;
    u32 *Tmp3;
};

const u32 *Esp32_GetC25519_N(void);
u32 Esp32_GetC25519_N1(void);
const u32 *Esp32_GetC25519_R2(void);
const u32 *Esp32_GetC25519_N2(void);

LT_EXTERN_C_END
#endif // PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_CRYPTO_ESP32DRIVERCRYPTO25519_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  05-Apr-23   gallienus   created
 */
