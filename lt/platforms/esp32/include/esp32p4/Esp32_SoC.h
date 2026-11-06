/******************************************************************************
 * Esp32_SoC.h                                                     ESP32-P4 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * Chip wide definitions for the esp32p4: where code and data can be placed, how
 * many cores there are, why the part last reset, and the handful of ROM
 * routines the BSP calls.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32P4_SOC_H
#define PLATFORMS_ESP32_INCLUDE_ESP32P4_SOC_H

#include <lt/LTTypes.h>

/*
 * Placement in on-chip SRAM rather than in flash.
 *
 * The esp32p4's instruction and data SRAM are one window rather than two views
 * of the same memory, so the two section names below differ only in what the
 * linker script does with them - but keeping them apart still matters, because
 * .iram1.text is placed with the vector table and the sections that must be
 * reachable when the caches are down.
 *
 * ESP32_IRAM_FUNC must stay LT_NOINLINE.  An inlined IRAM function is emitted
 * into its caller's section, which for most callers is flash; code that then
 * suspends the cache and calls it stalls the CPU fetching an instruction it
 * just made unreachable, with no diagnostic - the part simply stops.
 */
#define ESP32_MEM_REGION_IRAM              ".iram1.text"
#define ESP32_MEM_REGION_DRAM              ".dram1.text"
#define ESP32_MEM_REGION(x)                __attribute__((section(ESP32_MEM_REGION_ ## x)))
#define ESP32_IRAM_FUNC                    ESP32_MEM_REGION(IRAM) LT_NOINLINE

/*
 * The part has two RISC-V cores.  LT has no SMP, so only core 0 is started and
 * core 1 is left in reset; the type exists to keep the interrupt routing calls
 * the same shape they have on the parts where it matters.
 */
typedef u32 Esp32_CPU;
enum Esp32_CPU {
    kEsp32_CPU0 = 0,
};

/*
 * Reset reasons as esp_rom_get_reset_reason() reports them, soc_reset_reason_t.
 *
 * The numbering is not the esp32c3's and cannot be borrowed from it.  Four of
 * its reasons do not exist here (0x08, 0x0a, 0x11, 0x15), two new ones do
 * (0x18, 0x1a), and 0x16 and 0x17 mean the opposite of what they mean there -
 * a table copied across would mislabel a USB reset as a power glitch.
 */
typedef u32 Esp32_ResetReason;
enum Esp32_ResetReason {
    kEsp32_ResetReason_ChipPowerOn       = 0x01,
    kEsp32_ResetReason_CoreSW            = 0x03,
    /* Also reported on a PMU initiated power down */
    kEsp32_ResetReason_CoreDeepSleep     = 0x05,
    kEsp32_ResetReason_CoreMWDT          = 0x07,
    kEsp32_ResetReason_CoreRWDT          = 0x09,
    kEsp32_ResetReason_CpuMWDT           = 0x0b,
    kEsp32_ResetReason_CpuSW             = 0x0c,
    kEsp32_ResetReason_CpuRWDT           = 0x0d,
    kEsp32_ResetReason_SysBrownOut       = 0x0f,
    kEsp32_ResetReason_SysRWDT           = 0x10,
    kEsp32_ResetReason_SysSuperWDT       = 0x12,
    kEsp32_ResetReason_CorePowerGlitch   = 0x13,
    kEsp32_ResetReason_CoreEFuseCRC      = 0x14,
    kEsp32_ResetReason_CoreUsbJtag       = 0x16,
    kEsp32_ResetReason_CoreUsbUart       = 0x17,
    kEsp32_ResetReason_CpuJtag           = 0x18,
    kEsp32_ResetReason_CpuLockup         = 0x1a,
};

/*
 * Written by the bootloader once it has checked the image, and read by the BSP
 * to tell a verified boot from a jump into a half written image.
 */
enum Esp32_SecurityCheckStatus {
    kEsp32_SecurityCheckStatus_Success   = 0x3A5A5AA5,
};

/* Size of the signature block in the LTAT partition */
enum Esp32_LTAT {
    kEsp32_LTATSignatureSize             = 1216,
};

/*
 * ROM routines.  Declared here rather than pulled from a vendored header
 * because these two are all the BSP needs and the headers that declare them
 * bring the rest of IDF with them.  Both are in esp32p4.rom.ld.
 */
int esp_rom_printf(const char * pFormatString, ...);
Esp32_ResetReason esp_rom_get_reset_reason(int nCpuNo);

#endif /* PLATFORMS_ESP32_INCLUDE_ESP32P4_SOC_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Sep-26   claudius    created
 */
