/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/Esp32DriverCryptoEngine.c>
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

#include "Esp32_Clock.h"
#include "Esp32DriverCrypto.h"

/**
 * @brief Enable SHA engine
 */
void ESP32_SHA_Enable(void) {
    Esp32_ClockEnableCryptoClock(kEsp32_ClockCrypto_SHA,
                                 kEsp32_ClockCrypto_SHA | 
                                 kEsp32_ClockCrypto_SECUREBOOT);
}

/**
 * @brief Disable SHA engine
 */
void ESP32_SHA_Disable(void) {
    Esp32_ClockDisableCryptoClock(kEsp32_ClockCrypto_SHA);
}

/**
 * @brief Enable AES engine
 */
void ESP32_AES_Enable(void) {
    Esp32_ClockEnableCryptoClock(kEsp32_ClockCrypto_AES,
                                 kEsp32_ClockCrypto_AES | 
                                 kEsp32_ClockCrypto_SECUREBOOT | 
                                 kEsp32_ClockCrypto_DIGITAL_SIGNATURE);
}

/**
 * @brief Disable AES engine
 */
void ESP32_AES_Disable(void) {
    Esp32_ClockDisableCryptoClock(kEsp32_ClockCrypto_AES);
}

/**
 * @brief Enable RSA engine
 */
bool ESP32_RSA_Enable(void) {
    Esp32_ClockEnableCryptoClock(kEsp32_ClockCrypto_RSA,
                                 kEsp32_ClockCrypto_RSA | 
                                 kEsp32_ClockCrypto_DIGITAL_SIGNATURE);
    ESP32_REG(DPORT_RSA_PD_CTRL) &= ~ESP32_REG_MASK(DPORT_RSA, PD);
    bool bTimeout = false;
    LTTime t = LT_GetCore()->GetKernelTime();
    while (ESP32_REG(RSA_CLEAN) != 1 && !bTimeout) {
        bTimeout = LTTime_IsGreaterThan(LTTime_Subtract(LT_GetCore()->GetKernelTime(), t), ESP32_TIMEOUT_LONG);
    }
    return (ESP32_REG(RSA_CLEAN) == 1);
}

/**
 * @brief Disable RSA engine
 */
void ESP32_RSA_Disable(void) {
    ESP32_REG(DPORT_RSA_PD_CTRL) |= ESP32_REG_MASK(DPORT_RSA, PD);
    Esp32_ClockDisableCryptoClock(kEsp32_ClockCrypto_RSA);
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  19-May-22   gallienus   created
 */
