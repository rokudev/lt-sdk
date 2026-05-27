/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/FlashChipsCommands.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_FLASHCHIPSCOMMANDS_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_FLASHCHIPSCOMMANDS_H

#include <lt/LTTypes.h>

/*
 * flash chip commands structure
 * @note the chips command structs must be in DRAM as they are accessed when
 * the cache is disabled
 */
typedef struct {
    const char * pChipName;
    u32 WREN;   // write enable
    u32 WRDI;   // write disable
    u32 RDID;   // read ID
    u32 RDSR;   // read status register
    u32 WRSR;   // write status register
    u32 PP;     // page program
    u32 SE;     // sector erase
    u32 BE;     // block erase
    u32 CE;     // chip erase
    u32 DP;     // power down
    u32 USR;    // user command
    u32 FRQ;    // fast read quad
    u32 FRD;    // fast read dual
    u32 FRQO;   // fast read quad output
    u32 FRDO;   // fast read dual output
    u32 FR;     // fast read
    u32 RD;     // read
    u32 RDSRC;  // read status register using a command byte
    u32 RDSRCH; // read high status register using a command byte
} SPIFlashChipCommands;

/*******************************************************************************
 * Returns the commands structure for the chip with the given name; NULL if not
 * supported.
 * If pChipName is NULL, then the generic commands are used
 ******************************************************************************/
const SPIFlashChipCommands * Esp32SPIFlash_GetChipCmds(const char * pChipName);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jun-22   vitellius   created
 */

#endif /* PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_FLASHCHIPSCOMMANDS_H */
