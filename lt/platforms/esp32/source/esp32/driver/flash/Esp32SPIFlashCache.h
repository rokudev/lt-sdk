/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/Esp32SPIFlashCache.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LTTypes.h>

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32SPIFLASHCACHE_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32SPIFLASHCACHE_H

/****************************************************************************
 * typedefs
 ****************************************************************************/
typedef struct {
    u32     srcAddr;    /* offset on flash to map */
    u32     size;       /* size to map */
    u32     startPage;  /* MMU page index */
    u32     pageCount;  /* number of mapped MMU pages */
    void  * ptr;        /* mapped pointer for read access */
} Esp32SPIFlash_MapInfo;

/****************************************************************************
 * SPI flash cache functions
 ****************************************************************************/
void Esp32SPIFlashCache_Flush(void);
void Esp32SPIFlashCache_Mmap(Esp32SPIFlash_MapInfo * pInfo);
void Esp32SPIFlashCache_Ummap(const Esp32SPIFlash_MapInfo * pInfo);
bool Esp32SPIFlashCache_BusAddressToByteOffset(void * pAddress, u32 * pByteOffset);
void Esp32SPIFlashCache_EnableCache(u32 state);
u32 Esp32SPIFlashCache_DisableCache(void);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jun-22   vitellius   created
 */

#endif /* PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32SPIFLASHCACHE_H */
