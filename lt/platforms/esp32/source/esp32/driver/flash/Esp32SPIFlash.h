/*****************************************************************************
 * platforms/esp32/source/esp32/driver/flash/Esp32SPIFlash.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *****************************************************************************/

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32SPIFLASH_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32SPIFLASH_H

/*****************************************************************************
 * IMPORTANT
 * The cache must be disabled to prevent it from accessing the SPI0/1 bus or the
 * flash; after disabling the cache, all code must run from IRAM and any data
 * access must be from DRAM.
 * Below is the sequence for any kind of flash access (read/write/erase):
 *  1. Disable interrupts. That way no interrupts can cause flash reads
 *  2. Disable the cache. This prevents it from accessing the SPI bus and the flash
 *  3. Perform any operations (run from IRAM)
 *  4. Re-enable the cache
 *  5. Re-enable the interrupts
 *****************************************************************************/

/*****************************************************************************
 * Included Files
 *****************************************************************************/

#include <lt/LTTypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*****************************************************************************
 * typedefs
 *****************************************************************************/

typedef struct
{
    u32 deviceId;
    u32 chipSize;
    u32 blockSize;
    u32 sectorSize;
    u32 pageSize;
    u32 statusMask;
} Esp32SPIFlash_Chip;

typedef enum {
    Esp32SPIFlash_PartitionFlags_Encrypted = 0x01,
} Esp32SPIFlash_PartitionFlags;

typedef struct {
    u16 magic;
    u8  type;
    u8  subtype;
    u32 offset;
    u32 size;
    u8  label[16];
    u32 flags;
} Esp32SPIFlash_PartitionInfo;

/*****************************************************************************
 * Public Function
 *****************************************************************************/

/*****************************************************************************
 * Initialize the driver and returns a pointer to the chip
 * If pChipName is NULL, then the generic commands are used
 *****************************************************************************/
const Esp32SPIFlash_Chip * Esp32SPIFlash_Init(const char* pChipName);

/*****************************************************************************
 * SPI write unprotection
 *****************************************************************************/
bool Esp32SPIFlash_WriteUnprotect(void);

/*****************************************************************************
 * SPI write protect
 *****************************************************************************/
bool Esp32SPIFlash_WriteProtect(void);

/*****************************************************************************
 * Returns whether write protection is on or not
 *****************************************************************************/
bool Esp32SPIFlash_IsLocked(void);

/*****************************************************************************
 * Erase the entire flash chip
 *****************************************************************************/
bool Esp32SPIFlash_EraseChip(void);

/*****************************************************************************
 * Erase a block
 *****************************************************************************/
bool Esp32SPIFlash_EraseBlock(u32 block);

/*****************************************************************************
 * Erase a sector
 *****************************************************************************/
bool Esp32SPIFlash_EraseSector(u32 sector);

/****************************************************************************
 * Returns whether flash encryption is enabled or not
 ****************************************************************************/
bool Esp32SPIFlash_IsEncryptionEnabled(void);

/*****************************************************************************
 * Write Data to Flash (does NOT perform an erase)
 *   pData must be 32-byte aligned if bEncrypt is set
 *   len must be 16-byte aligned if bEncrypt is set
 *****************************************************************************/
bool Esp32SPIFlash_Write(u32 addr, const u8 * pBuffer, u32 size, bool bEncrypt);

/*****************************************************************************
 * Read Data from Flash
 * Note: The flash is mapped and the read is done over the cache, so the data
 *       will be decrypted automatically if needed
 *****************************************************************************/
bool Esp32SPIFlash_Read(u32 srcAddr, u8 * pDest, u32 len, bool bDecrypt);

#ifdef __cplusplus
}
#endif

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  25-May-22   vitellius   created
 */

#endif /* PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32SPIFLASH_H */
