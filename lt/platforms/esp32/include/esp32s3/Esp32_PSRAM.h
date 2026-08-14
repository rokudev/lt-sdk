/******************************************************************************
 * Esp32_PSRAM.h                                                   ESP32-S3 BSP
 *
 * - Detects, configures and memory-maps external SPI PSRAM
 * - Called once from LTCoreBSP_Initialize(), before the heap is built
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_ESP32_PSRAM_H
#define PLATFORMS_ESP32_INCLUDE_ESP32S3_ESP32_PSRAM_H

/*
 * Where the PSRAM ended up and how much of it there is.  Both fields are zero
 * when no part was found, which is the normal outcome on a board that has
 * none - it is not an error.
 *
 * Unlike the esp32, pBase is not a fixed address on this part.  Flash rodata
 * and PSRAM are both reached through the same DRAM0 cache bus, and the MMU
 * decides page by page which device a virtual page targets, so the base can
 * only be known once the size is known.
 */
typedef struct Esp32_PSRAM_Info {
    u8 * pBase;             /**< Mapped virtual base address, NULL if absent */
    u32  nSizeInBytes;      /**< Mapped size in bytes, 0 if absent */
} Esp32_PSRAM_Info;

/*
 * Probes for a PSRAM part, and if one is present configures the MSPI pads and
 * timing, programs the part's mode registers for octal DDR operation, and maps
 * it into the CPU's data address space.
 *
 * Returns true if PSRAM was found and mapped, in which case *pInfo describes
 * it.  Returns false and zeroes *pInfo if no part responded; that is not
 * fatal, and the caller should simply carry on with internal RAM only.
 *
 * Must be called before any code depends on the flash cache running at its
 * final speed - it reprograms SPI0 - and only once.
 *
 * It also pins the instruction and data cache sizes at 32KB each, before doing
 * anything else and regardless of whether a part is found.  That is not
 * incidental: the data cache is carved out of the top of SRAM2, so the 32KB
 * choice is what leaves dram_app3_seg - heap3 - free at 0x3FCF0000.  Any caller
 * registering that region has to call this first.  The cache pinning belongs in
 * the chip's call_start_cpu0 and will move there once one exists.
 */
bool Esp32_PSRAM_Initialize(Esp32_PSRAM_Info * pInfo);

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_ESP32_PSRAM_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  28-Jul-26   claudius    created
 */
