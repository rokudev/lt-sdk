/******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/esp32s3/Esp32DriverCryptoRandom.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *****************************************************************************/

#include <lt/LTTypes.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>

#include "Esp32_Clock.h"
#include "Esp32DriverCrypto.h"

/* Defs for accessing entropy-related values */
#define CCOUNT		           (234)
#define APB_CYCLE_WAIT_NUM     (16)
#define APB_FREQ_MHZ           (80)
#define RSR(reg, curval)       asm volatile ("rsr %0, " #reg : "=r" (curval));

static volatile u32 s_nLastCount = 0;

static u32 CpuGetCycleCount(void) {
    u32 res;
    RSR(CCOUNT, res);
    return res;
}

static u32 ReadRandom(void) {
    /* The RNG needs a fresh APB cycle between reads.  The esp32 driver takes the
     * CPU:APB ratio from the ROM's g_ticks_per_us_pro, which the esp32s3 ROM does
     * not export, so derive it from the clock the bootloader left us running at. */
    u32 cpuFreqMHz = Esp32_ClockGetMHz();
    u32 cpuToApbFreqRatio = (cpuFreqMHz < APB_FREQ_MHZ) ? 1 : cpuFreqMHz / APB_FREQ_MHZ;

    u32 CCount = 0;
    u32 res = 0;
    do {
        CCount = CpuGetCycleCount();
        res ^= ESP32_REG(WDEV_RND);
    } while (CCount - s_nLastCount < cpuToApbFreqRatio * APB_CYCLE_WAIT_NUM);
    s_nLastCount = CCount;
    return res ^ ESP32_REG(WDEV_RND);
}

static void ESP32_GetEntropy(u8 *entropy, LT_SIZE entropyLen) {
    u32 word = 0;
    u8 len = 0;
    while (entropyLen > 0) {
        word = ReadRandom();
        len = entropyLen > sizeof(word) ? sizeof(word) : entropyLen;
        lt_memcpy(entropy, &word, len);
        entropy += len;
        entropyLen -= len;
    }
}

// Objects: Entropy, Random
typedef_LTObjectImpl(LTDriverCryptoEntropy, LTDriverCryptoEntropyImpl) {
} LTOBJECT_API;

static bool LTDriverCryptoEntropyImpl_GetEntropy(u8 *entropy, LT_SIZE entropyLen) {
    if (!entropy) return false;
    ESP32_GetEntropy(entropy, entropyLen);
    return true;
}

static void LTDriverCryptoEntropyImpl_DestructObject(LTDriverCryptoEntropyImpl *instance) {
    LT_UNUSED(instance);
}

static bool LTDriverCryptoEntropyImpl_ConstructObject(LTDriverCryptoEntropyImpl *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoEntropy, LTDriverCryptoEntropyImpl,
    GetEntropy,
);

typedef_LTObjectImpl(LTDriverCryptoRandom, LTHardwareCryptoRandom) {
} LTOBJECT_API;

static bool LTHardwareCryptoRandom_GenRandomBytes(u8 *output, LT_SIZE outputLen) {
    if (!output) return false;

    Esp32DriverCrypto_LockShaMutex();
    u32 random[SHA256_HASH_LENGTH / 4];
    u8 len = 0;
    while (outputLen > 0) {
        ESP32_GetEntropy((u8 *)random, SHA256_HASH_LENGTH);
        ESP32_SHA256_Digest((u8 *)random, SHA256_HASH_LENGTH, (u8 *)random);
        len = outputLen > SHA256_HASH_LENGTH ? SHA256_HASH_LENGTH : outputLen;
        lt_memcpy(output, random, len);
        outputLen -= len;
        output += len;
    }
    Esp32DriverCrypto_UnlockShaMutex();
    return true;
}

static void LTHardwareCryptoRandom_DestructObject(LTHardwareCryptoRandom *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoRandom_ConstructObject(LTHardwareCryptoRandom *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoRandom, LTHardwareCryptoRandom,
    GenRandomBytes,
);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  27-Aug-26   claudius    created, from the esp32 driver
 */
