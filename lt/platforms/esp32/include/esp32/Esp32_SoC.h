/******************************************************************************
 * Esp32_SoC.h                                                        ESP32 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32_SOC_H
#define PLATFORMS_ESP32_INCLUDE_ESP32_SOC_H

/*
 * Memory Regions
 */
#define ESP32_MEM_REGION_IRAM    ".iram1.text"
#define ESP32_MEM_REGION_DRAM    ".dram1.text"
#define ESP32_MEM_REGION(x)      __attribute__((section(ESP32_MEM_REGION_ ## x)))

/*
 * CPU Number
 */
typedef u32 Esp32_CPU;
enum Esp32_CPU {
    kEsp32_CPU0  = 0,
    kEsp32_CPU1  = 1
};

/*
 * Reset Reason
 */
typedef u32 Esp32_ResetReason;
enum Esp32_ResetReason {
    kEsp32_ResetReason_PowerOnReset   = 0x01,
    kEsp32_ResetReason_CoreSoftware   = 0x03,
    kEsp32_ResetReason_CoreDeepSleep  = 0x05,
    kEsp32_ResetReason_CoreSDIO       = 0x06,
    kEsp32_ResetReason_CoreMWDT0      = 0x07,
    kEsp32_ResetReason_CoreMWDT1      = 0x08,
    kEsp32_ResetReason_CoreRTCWDT     = 0x09,
    kEsp32_ResetReason_Cpu0MWDT0      = 0x0b,
    kEsp32_ResetReason_Cpu1MWDT1      = 0x0b,
    kEsp32_ResetReason_Cpu0Software   = 0x0c,
    kEsp32_ResetReason_Cpu1Software   = 0x0c,
    kEsp32_ResetReason_Cpu0RTCWDT     = 0x0d,
    kEsp32_ResetReason_Cpu1RTCWDT     = 0x0d,
    kEsp32_ResetReason_Cpu1Cpu0       = 0x0e,
    kEsp32_ResetReason_SysBrownOut    = 0x0f,
    kEsp32_ResetReason_SysRTCWDT      = 0x10,
};

/*
 * Security
 */
typedef u32 Esp32_SecurityCheckStatus;
enum Esp32_SecurityCheckStatus {
    kEsp32_SecurityCheckStatus_Success  = 0x3A5A5AA5,
};

enum {
    kEsp32_LTATSignatureSize            = 1216,
};

/*
 * ROM Functions
 */
int esp_rom_printf(const char * pFormatString, ...);
Esp32_ResetReason esp_rom_get_reset_reason(int nCpuNo);
Esp32_SecurityCheckStatus ets_secure_boot_verify_signature(const u8 * pSignature, const u8 * pDigest,
                                                              const u8 * pTrustedDigest, u8 * pVerifiedDigest);

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32_SOC_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  27-Jun-22   tiberius    created
 */
