/******************************************************************************
 * Esp32_SoC.h                                                     ESP32-C3 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32C3_SOC_H
#define PLATFORMS_ESP32_INCLUDE_ESP32C3_SOC_H

/*
 * Memory Regions
 */
#define ESP32_MEM_REGION_IRAM    ".iram1.text"
#define ESP32_MEM_REGION_DRAM    ".dram1.text"
#define ESP32_MEM_REGION(x)      __attribute__((section(ESP32_MEM_REGION_ ## x)))

/* ESP32_IRAM_FUNC -- a function whose body must genuinely be in IRAM, because
 * it runs with the instruction cache suspended or not yet configured.
 *
 * ESP32_MEM_REGION(IRAM) alone is not enough for one of these.  The section
 * attribute binds the function's own body to .iram1.text, but it does not
 * survive inlining: a static that gcc folds into a caller in .flash.text is
 * emitted as part of that caller, in flash, section attribute and all.  The
 * result is code that suspends the instruction cache and then tries to fetch
 * its next instruction through it, which stalls the CPU with no exception and
 * no watchdog to end it.  Nothing warns about this - the build is clean and the
 * symbol simply vanishes from the map.
 *
 * So spell any such function ESP32_IRAM_FUNC and let the macro remember the
 * LT_NOINLINE for you.  To check after the fact, confirm the function still has
 * its own symbol at a 0x4038xxxx (IRAM) address rather than 0x42xxxxxx (flash):
 *
 *   riscv32-esp-elf-nm -n LTFirmwareImage.elf | grep <function>
 */
#define ESP32_IRAM_FUNC          ESP32_MEM_REGION(IRAM) LT_NOINLINE

/*
 * CPU Number
 *
 * The esp32c3 is single core.  kEsp32_CPU0 is kept so code shared with the
 * dual-core parts still has a CPU argument to pass; there is no CPU1.
 */
typedef u32 Esp32_CPU;
enum Esp32_CPU {
    kEsp32_CPU0  = 0
};

/*
 * Reset Reason
 *
 * Numerically identical to the esp32s3's list - see rom/rtc.h.  Both parts
 * renumbered against the original esp32, which had an SDIO reset at 0x06 and
 * put the brown out detector elsewhere, so a table indexed by these values can
 * be shared with the esp32s3 but never with the esp32.
 */
typedef u32 Esp32_ResetReason;
enum Esp32_ResetReason {
    kEsp32_ResetReason_PowerOnReset    = 0x01,
    kEsp32_ResetReason_RtcSwSys        = 0x03,
    kEsp32_ResetReason_CoreDeepSleep   = 0x05,
    kEsp32_ResetReason_CoreMWDT0       = 0x07,
    kEsp32_ResetReason_CoreMWDT1       = 0x08,
    kEsp32_ResetReason_CoreRTCWDT      = 0x09,
    kEsp32_ResetReason_Intrusion       = 0x0a,
    kEsp32_ResetReason_Cpu0MWDT0       = 0x0b,
    kEsp32_ResetReason_RtcSwCpu        = 0x0c,
    kEsp32_ResetReason_Cpu0RTCWDT      = 0x0d,
    kEsp32_ResetReason_SysBrownOut     = 0x0f,
    kEsp32_ResetReason_SysRTCWDT       = 0x10,
    kEsp32_ResetReason_Cpu1MWDT1       = 0x11,
    kEsp32_ResetReason_SuperWDT        = 0x12,
    kEsp32_ResetReason_GlitchRTC       = 0x13,
    kEsp32_ResetReason_EFuse           = 0x14,
    kEsp32_ResetReason_UsbUartChip     = 0x15,
    kEsp32_ResetReason_UsbJtagChip     = 0x16,
    kEsp32_ResetReason_PowerGlitch     = 0x17,
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

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32C3_SOC_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-Sep-26   claudius    created
 */
