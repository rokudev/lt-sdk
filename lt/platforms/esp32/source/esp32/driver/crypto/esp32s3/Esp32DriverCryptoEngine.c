/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/esp32s3/Esp32DriverCryptoEngine.c>
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

/*
 * The esp32s3 has no separate crypto clock register: the AES, SHA and RSA gates
 * are ordinary module clocks in SYSTEM_PERIP_CLK_EN1, so each engine is brought
 * up on its own rather than alongside the secure boot and digital signature
 * blocks the esp32 had to share a register with.
 */

/**
 * @brief Enable SHA engine
 */
void ESP32_SHA_Enable(void) {
    Esp32_ClockEnableModuleClock(kEsp32_ClockModule_CRYPTO_SHA);
}

/**
 * @brief Disable SHA engine
 */
void ESP32_SHA_Disable(void) {
    Esp32_ClockDisableModuleClock(kEsp32_ClockModule_CRYPTO_SHA);
}

/**
 * @brief Enable AES engine
 */
void ESP32_AES_Enable(void) {
    Esp32_ClockEnableModuleClock(kEsp32_ClockModule_CRYPTO_AES);
}

/**
 * @brief Disable AES engine
 */
void ESP32_AES_Disable(void) {
    Esp32_ClockDisableModuleClock(kEsp32_ClockModule_CRYPTO_AES);
}

/**
 * @brief Enable RSA engine
 */
bool ESP32_RSA_Enable(void) {
    Esp32_ClockEnableModuleClock(kEsp32_ClockModule_CRYPTO_RSA);
    ESP32_REG(SYSTEM_RSA_PD_CTRL) &= ~ESP32_REG_MASK(SYSTEM_RSA_PD, MEM_PD);
    bool bTimeout = false;
    LTTime t = LT_GetCore()->GetKernelTime();
    while (ESP32_REG(RSA_QUERY_CLEAN) != 1 && !bTimeout) {
        bTimeout = LTTime_IsGreaterThan(LTTime_Subtract(LT_GetCore()->GetKernelTime(), t), ESP32_TIMEOUT_LONG);
    }
    /* The driver polls for completion, so keep the engine's interrupt masked. */
    ESP32_REG(RSA_INTERRUPT_ENA) = 0;
    return (ESP32_REG(RSA_QUERY_CLEAN) == 1);
}

/**
 * @brief Disable RSA engine
 */
void ESP32_RSA_Disable(void) {
    ESP32_REG(SYSTEM_RSA_PD_CTRL) |= ESP32_REG_MASK(SYSTEM_RSA_PD, MEM_PD);
    Esp32_ClockDisableModuleClock(kEsp32_ClockModule_CRYPTO_RSA);
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  27-Aug-26   claudius    created, from the esp32 driver
 */
