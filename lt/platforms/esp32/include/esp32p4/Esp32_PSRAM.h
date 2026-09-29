/******************************************************************************
 * Esp32_PSRAM.h                                                   ESP32-P4 BSP
 *
 * - Detects, configures and memory-maps the in-package HEX PSRAM
 * - Called once from LTCoreBSP_Initialize(), before the heap is built
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32P4_ESP32_PSRAM_H
#define PLATFORMS_ESP32_INCLUDE_ESP32P4_ESP32_PSRAM_H

/*
 * Where the PSRAM ended up and how much of it there is.  Both fields are zero
 * when no part answered, which is a normal outcome and not an error.
 *
 * pBase is always 0x48000000 when it is not NULL.  External RAM has a window of
 * its own on this part, 64MB wide and shared with nothing, so unlike the
 * esp32s3 there is no arithmetic to do against the end of flash rodata: the
 * only thing the probe discovers is how much of the window is backed.
 */
typedef struct Esp32_PSRAM_Info {
    u8 * pBase;             /**< Mapped virtual base address, NULL if absent */
    u32  nSizeInBytes;      /**< Mapped size in bytes, 0 if absent */
} Esp32_PSRAM_Info;

/*
 * Probes for a PSRAM die, and if one answers brings up the MPLL that powers it,
 * configures the PSRAM MSPI pads and clocks, programs the die's mode registers
 * for hex line DDR operation, and maps it into the external RAM window.
 *
 * Returns true if PSRAM was found and mapped, in which case *pInfo describes
 * it.  Returns false and zeroes *pInfo if nothing responded; that is not fatal,
 * and the caller should simply carry on with internal RAM only.
 *
 * Call once, and only after the console exists - failures are reported through
 * esp_rom_printf, which is all that works this early.  Flash is on a separate
 * MSPI here, so unlike the esp32s3 this does not disturb the flash cache and
 * has no ordering constraint against it.
 */
bool Esp32_PSRAM_Initialize(Esp32_PSRAM_Info * pInfo);

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32P4_ESP32_PSRAM_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  28-Sep-26   claudius    created
 */
