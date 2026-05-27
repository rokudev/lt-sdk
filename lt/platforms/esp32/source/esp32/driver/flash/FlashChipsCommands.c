/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/FlashChipsCommands.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTStdlib.h>

#include "FlashChipsCommands.h"
#include "Esp32_SoC.h"

#define BIT(n)                                  (1UL << (n))

/*******************************************************************************
 * Using this command set as a generic start point and tested on:
 *   1. ZB25VQ32 flash by Zbit
 *   2. GD25Q32C by GigaDevice
 * @note the chips command structs must be in DRAM as they are accessed when
 * the cache is disabled
*******************************************************************************/
static const SPIFlashChipCommands ESP32_MEM_REGION(DRAM) s_genericChipCmds = {
    .pChipName  = "generic",
    .WREN       = BIT(30),
    .WRDI       = BIT(29),
    .RDID       = BIT(28),
    .RDSR       = BIT(27),
    .WRSR       = BIT(26),
    .PP         = BIT(25),
    .SE         = BIT(24),
    .BE         = BIT(23),
    .CE         = BIT(22),
    .DP         = BIT(21),
    .USR        = BIT(18),
    .FRQ        = 0xeb,
    .FRD        = 0xbb,
    .FRQO       = 0x6b,
    .FRDO       = 0x3b,
    .FR         = 0x0b,
    .RD         = 0x03,
    .RDSRC      = 0x05,
    .RDSRCH     = 0x35,
};

/*******************************************************************************
 * Supported chips commands
 ******************************************************************************/
static const SPIFlashChipCommands * kSupportedChips[] = {
    &s_genericChipCmds,
    NULL
};

/*******************************************************************************
 * Returns the commands structure for the chip with the given name
 ******************************************************************************/
const SPIFlashChipCommands * Esp32SPIFlash_GetChipCmds(const char * pChipName) {
    const SPIFlashChipCommands * pSelectedChip = (pChipName == NULL ? &s_genericChipCmds : NULL);
    for (u32 i = 0; kSupportedChips[i] != NULL && pSelectedChip == NULL; ++i) {
        if (lt_strcasecmp(kSupportedChips[i]->pChipName, pChipName) == 0) {
            pSelectedChip = kSupportedChips[i];
        }
    }
    return pSelectedChip;
};

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jun-22   vitellius   created
 */
