/*****************************************************************************
 * platforms/esp32/source/esp32/driver/flash/esp32c3/Esp32c3SPIFlash.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *****************************************************************************/

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32C3SPIFLASH_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32C3SPIFLASH_H

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
 *
 * Like the esp32s3 driver, and unlike the esp32 one, this driver issues flash
 * operations through the ROM's legacy SPI flash API rather than driving the SPI1
 * registers itself.  Those routines are ROM resident, so calling them with the
 * cache down is safe, and they already know the read mode, dummy cycle count and
 * opcode set the bootloader left the MSPI in.  There is therefore no chip
 * command table here; the esp32's FlashChipsCommands.c has no counterpart.
 *
 * The legacy API sends a three byte address and so cannot reach past 16MB.  The
 * esp32s3 driver works around that with hand built four byte address commands;
 * there is nothing of the sort here, because no esp32c3 part carries more than
 * 16MB of in-package flash and the ROM exports no general purpose MSPI
 * transaction driver to build such a command with - esp_rom_opiflash_exec_cmd()
 * is an esp32s3 ROM entry point only.
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

/* Layout must match the ROM's esp_rom_spiflash_chip_t */
typedef struct
{
    u32 deviceId;
    u32 chipSize;
    u32 blockSize;
    u32 sectorSize;
    u32 pageSize;
    u32 statusMask;
} Esp32c3SPIFlash_Chip;

typedef enum {
    Esp32c3SPIFlash_PartitionFlags_Encrypted = 0x01,
} Esp32c3SPIFlash_PartitionFlags;

typedef struct {
    u16 magic;
    u8  type;
    u8  subtype;
    u32 offset;
    u32 size;
    u8  label[16];
    u32 flags;
} Esp32c3SPIFlash_PartitionInfo;

/*****************************************************************************
 * Public Function
 *****************************************************************************/

/*****************************************************************************
 * Initialize the driver and returns a pointer to the chip
 * pChipName is accepted for symmetry with the esp32 driver and ignored; the ROM
 * carries the opcode set for the chip the bootloader configured
 *****************************************************************************/
const Esp32c3SPIFlash_Chip * Esp32c3SPIFlash_Init(const char* pChipName);

/*****************************************************************************
 * SPI write unprotection
 *****************************************************************************/
bool Esp32c3SPIFlash_WriteUnprotect(void);

/*****************************************************************************
 * SPI write protect
 *****************************************************************************/
bool Esp32c3SPIFlash_WriteProtect(void);

/*****************************************************************************
 * Returns whether write protection is on or not
 *****************************************************************************/
bool Esp32c3SPIFlash_IsLocked(void);

/*****************************************************************************
 * Erase the entire flash chip
 *****************************************************************************/
bool Esp32c3SPIFlash_EraseChip(void);

/*****************************************************************************
 * Erase a block
 *****************************************************************************/
bool Esp32c3SPIFlash_EraseBlock(u32 block);

/*****************************************************************************
 * Erase a sector
 *****************************************************************************/
bool Esp32c3SPIFlash_EraseSector(u32 sector);

/****************************************************************************
 * Returns whether flash encryption is enabled or not
 ****************************************************************************/
bool Esp32c3SPIFlash_IsEncryptionEnabled(void);

/*****************************************************************************
 * Write Data to Flash (does NOT perform an erase)
 *   addr must be 32-byte aligned if bEncrypt is set
 *   size must be 16-byte aligned if bEncrypt is set
 *****************************************************************************/
bool Esp32c3SPIFlash_Write(u32 addr, const u8 * pBuffer, u32 size, bool bEncrypt);

/*****************************************************************************
 * Read Data from Flash
 * Note: When bDecrypt is set the flash is mapped and the read is done over the
 *       cache, so the data is decrypted on the way through
 *****************************************************************************/
bool Esp32c3SPIFlash_Read(u32 srcAddr, u8 * pDest, u32 len, bool bDecrypt);

#ifdef __cplusplus
}
#endif

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-Sep-26   claudius    created
 */

#endif /* PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32C3SPIFLASH_H */
