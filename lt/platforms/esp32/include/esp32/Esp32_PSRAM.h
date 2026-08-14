/******************************************************************************
 * Esp32_PSRAM.h                                                      ESP32 BSP
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

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32_ESP32_PSRAM_H
#define PLATFORMS_ESP32_INCLUDE_ESP32_ESP32_PSRAM_H

/*
 * Where the PSRAM ended up and how much of it there is.  Both fields are zero
 * when no part was found, which is the normal outcome on a board that has
 * none - it is not an error.
 */
typedef struct Esp32_PSRAM_Info {
    u8 * pBase;             /**< Mapped virtual base address, NULL if absent */
    u32  nSizeInBytes;      /**< Mapped size in bytes, 0 if absent */
} Esp32_PSRAM_Info;

/*
 * Probes for a PSRAM part, and if one is present configures the SPI pads, puts
 * the part into quad mode and maps it into the CPU's data address space.
 *
 * Returns true if PSRAM was found and mapped, in which case *pInfo describes
 * it.  Returns false and zeroes *pInfo if no part responded or if the chip
 * package is one this driver does not support; neither is fatal, and the
 * caller should simply carry on with internal RAM only.
 *
 * Must be called before any code depends on the flash cache running at its
 * final speed - it reprograms SPI0 - and only once.
 */
bool Esp32_PSRAM_Initialize(Esp32_PSRAM_Info * pInfo);

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32_ESP32_PSRAM_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  28-Jul-26   claudius    created
 */
