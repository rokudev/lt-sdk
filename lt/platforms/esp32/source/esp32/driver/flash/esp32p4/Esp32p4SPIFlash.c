/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/esp32p4/Esp32p4SPIFlash.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTCore.h>

#include "Esp32_Registers.h"
#include "Esp32_Irq.h"
#include "Esp32_SoC.h"
#include "Esp32p4SPIFlash.h"
#include "Esp32p4SPIFlashCache.h"

DEFINE_LTLOG_SECTION("esp32p4.spiflash");

/*******************************************************************************
 * consts
*******************************************************************************/
/*
 * These are properties of the flash chip rather than of the esp32p4.  The block
 * protect bits live in the low status byte and the quad enable bit in the high
 * one, which is why writing the low byte alone can clear QE on parts that
 * implement a one byte WRSR - hence the two byte write below.
 */
enum {
    kSPIFlashBP0            = 0x01u << 2,
    kSPIFlashBP1            = 0x01u << 3,
    kSPIFlashBP2            = 0x01u << 4,
    kSPIFlashWriteProtect   = kSPIFlashBP0 | kSPIFlashBP1 | kSPIFlashBP2,
};

/* Transfer granularity for the bounce buffers.  32 bytes matches the write
 * quantum the flash device unit advertises and the 16 byte multiple the ROM's
 * encrypted write demands */
enum {
    kWriteChunkBytes        = 32,
    kWriteChunkWords        = kWriteChunkBytes / 4,
    kReadChunkBytes         = 64,
    kReadChunkWords         = kReadChunkBytes / 4,
};

/*******************************************************************************
 * ROM
*******************************************************************************/
typedef u32 Esp32p4SPIFlash_RomResult;
enum Esp32p4SPIFlash_RomResult {
    kEsp32p4SPIFlash_RomResult_OK       = 0,
    kEsp32p4SPIFlash_RomResult_Error    = 1,
    kEsp32p4SPIFlash_RomResult_Timeout  = 2,
};

/*
 * The esp32 kept the ROM's notion of the attached flash chip in a fixed global,
 * g_rom_spiflash_chip.  The esp32p4 ROM instead exports a pointer to a block
 * holding the chip description followed by the per-mode extra dummy cycle
 * counts; layout must match the ROM's spiflash_legacy_data_t.
 */
typedef struct {
    Esp32p4SPIFlash_Chip    chip;
    u8                      dummyLenPlus[3];
    u8                      sigMatrix;
} Esp32p4SPIFlash_LegacyData;

extern Esp32p4SPIFlash_LegacyData * rom_spiflash_legacy_data;

extern Esp32p4SPIFlash_RomResult esp_rom_spiflash_read_status(Esp32p4SPIFlash_Chip * pChip, u32 * pStatus);
extern Esp32p4SPIFlash_RomResult esp_rom_spiflash_read_statushigh(Esp32p4SPIFlash_Chip * pChip, u32 * pStatus);
extern Esp32p4SPIFlash_RomResult esp_rom_spiflash_write_status(Esp32p4SPIFlash_Chip * pChip, u32 nStatus);
extern Esp32p4SPIFlash_RomResult esp_rom_spiflash_wait_idle(Esp32p4SPIFlash_Chip * pChip);
extern Esp32p4SPIFlash_RomResult esp_rom_spiflash_unlock(void);
extern Esp32p4SPIFlash_RomResult esp_rom_spiflash_erase_chip(void);
extern Esp32p4SPIFlash_RomResult esp_rom_spiflash_erase_block(u32 nBlockNumber);
extern Esp32p4SPIFlash_RomResult esp_rom_spiflash_erase_sector(u32 nSectorNumber);
extern Esp32p4SPIFlash_RomResult esp_rom_spiflash_write(u32 nDestAddr, const u32 * pSrc, s32 nLen);
extern Esp32p4SPIFlash_RomResult esp_rom_spiflash_read(u32 nSrcAddr, u32 * pDest, s32 nLen);
extern Esp32p4SPIFlash_RomResult esp_rom_spiflash_write_encrypted(u32 nFlashAddr, u32 * pData, u32 nLen);
extern void esp_rom_spiflash_write_encrypted_enable(void);
extern void esp_rom_spiflash_write_encrypted_disable(void);

/*******************************************************************************
 * static variables
*******************************************************************************/
static bool s_bInited = false;

/*
 * Every static below that suspends the cache is ESP32_IRAM_FUNC rather than a
 * plain ESP32_MEM_REGION(IRAM), and must stay that way - a section attribute on
 * its own does not survive gcc inlining the function into a caller in flash.
 * See the macro in Esp32_SoC.h for the mechanism and for how to check the
 * result:
 *
 *   riscv32-esp-elf-nm -n LTFirmwareImage.elf | grep Esp32p4SPIFlash
 */

/*******************************************************************************
 *
 * implementation
 *
*******************************************************************************/

static Esp32p4SPIFlash_Chip * GetChip(void) {
    return &rom_spiflash_legacy_data->chip;
}

/*******************************************************************************
 * Work out how big the chip is from the JEDEC id the bootloader read
 * Returns 0 if the id does not decode to a size
*******************************************************************************/
static u32 Esp32p4SPIFlash_ChipSizeFromDeviceId(void) {
    /*
     * The size the ROM is holding did not come from the chip.  The second stage
     * bootloader takes it from the flash size nibble of its own image header,
     * which is whatever esptool was told at mastering time, and hands it to
     * esp_rom_spiflash_config_param().  Get it wrong and everything past the
     * declared size is refused by the bounds checks below.
     *
     * The id, on the other hand, did come from the chip -
     * bootloader_flash_update_id() issues a RDID before the app is loaded - and
     * bootloader_read_flash_id() leaves it in JEDEC order, manufacturer in the
     * top byte and the capacity in the bottom one.  Every part this would be
     * built against encodes that capacity as the log2 of the size in bytes.
     */
    u32 nCapacity = GetChip()->deviceId & 0xffu;
    if (nCapacity < 20 || nCapacity > 26) {   /* 1MB to 64MB */
        return 0;
    }

    return 1u << nCapacity;
}

/*******************************************************************************
 * Initializes the driver
 * Returns a pointer to the flash chip the bootloader configured
*******************************************************************************/
const Esp32p4SPIFlash_Chip * Esp32p4SPIFlash_Init(const char* pChipName) {
    LT_UNUSED(pChipName);

    /* The ROM populates this while loading the second stage bootloader; if it is
     * empty then nothing has configured the MSPI and there is nothing to talk to */
    if (rom_spiflash_legacy_data == NULL || GetChip()->chipSize == 0 || GetChip()->sectorSize == 0) {
        return NULL;
    }

    s_bInited = true;

    u32 nChipSize = Esp32p4SPIFlash_ChipSizeFromDeviceId();
    if (nChipSize != 0 && nChipSize != GetChip()->chipSize) {
        LTLOG_DEBUG("chip.size", "Flash is %lu MB, image header said %lu MB",
                   LT_Pu32(nChipSize / (1024 * 1024)), LT_Pu32(GetChip()->chipSize / (1024 * 1024)));
        GetChip()->chipSize = nChipSize;
    }

    return GetChip();
}

/*******************************************************************************
 * Checks if the driver has been initialized
*******************************************************************************/
static bool Esp32p4SPIFlash_IsInited(void) {
    return s_bInited;
}

/*******************************************************************************
 * Issue the hardware generated write enable command
 * @note the cache must be disabled before calling this function
*******************************************************************************/
static bool ESP32_IRAM_FUNC Esp32p4SPIFlash_EnableWrite(void) {
    /* IRAM section begin - Code from here on must run from IRAM */

    if (esp_rom_spiflash_wait_idle(GetChip()) != kEsp32p4SPIFlash_RomResult_OK) {
        return false;
    }

    ESP32_SPIMEM_REG(1, CMD) = ESP32_REG_MASK(SPIMEM, CMD_FLASH_WREN);
    volatile u16 nTimeout = LT_U16_MAX;
    while (ESP32_SPIMEM_REG(1, CMD) != 0 && --nTimeout != 0);

    /* IRAM section end - Code from here on can run from flash */

    return (nTimeout != 0);
}

/*******************************************************************************
 * Unlock the flash
 * The flash chips support protecting a range of blocks or predefined blocks, but
 * does not support specific individual blocks
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32p4SPIFlash_WriteUnprotect(void) {
    if (!Esp32p4SPIFlash_IsInited()) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32p4SPIFlashCache_DisableCache();

    /* The ROM routine knows which vendors keep their protect bits where, and
     * preserves the quad enable bit, so there is no reason to hand roll this */
    bool bSuccess = (esp_rom_spiflash_unlock() == kEsp32p4SPIFlash_RomResult_OK);

    Esp32p4SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * Lock the flash
 * @note esp_rom_spiflash_lock() is declared by the ROM headers but is not one of
 * the entry points the esp32p4 ROM exports, so the sequence is open coded here
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32p4SPIFlash_WriteProtect(void) {
    if (!Esp32p4SPIFlash_IsInited()) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32p4SPIFlashCache_DisableCache();

    /* read_statushigh returns the second status byte already shifted into
     * position, so the two halves can simply be ored together */
    u32 nStatus     = 0;
    u32 nStatusHigh = 0;
    bool bSuccess = (esp_rom_spiflash_read_status(GetChip(), &nStatus) == kEsp32p4SPIFlash_RomResult_OK)
                 && (esp_rom_spiflash_read_statushigh(GetChip(), &nStatusHigh) == kEsp32p4SPIFlash_RomResult_OK);

    if (bSuccess) {
        /* Send both status bytes.  A single byte WRSR would leave the quad
         * enable bit in the second byte undefined on some parts, which would
         * cost us the cached flash mapping we are running from */
        u32 nCtrl = ESP32_SPIMEM_REG(1, CTRL);
        ESP32_SPIMEM_REG(1, CTRL) = nCtrl | ESP32_REG_MASK(SPIMEM, CTRL_WRSR_2B);

        bSuccess = Esp32p4SPIFlash_EnableWrite();
        if (bSuccess) {
            bSuccess = (esp_rom_spiflash_write_status(GetChip(), nStatusHigh | kSPIFlashWriteProtect)
                            == kEsp32p4SPIFlash_RomResult_OK);
        }

        ESP32_SPIMEM_REG(1, CTRL) = nCtrl;
    }

    Esp32p4SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * Returns whether the flash is locked or unlocked
 * @note the block protect bits are in the low status byte
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32p4SPIFlash_IsLocked(void) {
    if (!Esp32p4SPIFlash_IsInited()) {
        return false;
    }

    u32 nStatus = 0;

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32p4SPIFlashCache_DisableCache();

    bool bSuccess = (esp_rom_spiflash_read_status(GetChip(), &nStatus) == kEsp32p4SPIFlash_RomResult_OK);

    Esp32p4SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return (bSuccess && (nStatus & kSPIFlashWriteProtect) != 0);
}

/*******************************************************************************
 * erase the entire flash
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32p4SPIFlash_EraseChip(void) {
    if (!Esp32p4SPIFlash_IsInited()) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32p4SPIFlashCache_DisableCache();

    bool bSuccess = (esp_rom_spiflash_erase_chip() == kEsp32p4SPIFlash_RomResult_OK);

    Esp32p4SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * erase a block
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32p4SPIFlash_EraseBlock(u32 nBlockNumber) {
    if (!Esp32p4SPIFlash_IsInited()) {
        return false;
    }

    if (nBlockNumber >= (GetChip()->chipSize / GetChip()->blockSize)) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32p4SPIFlashCache_DisableCache();

    bool bSuccess = (esp_rom_spiflash_erase_block(nBlockNumber) == kEsp32p4SPIFlash_RomResult_OK);

    Esp32p4SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * erase a sector
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32p4SPIFlash_EraseSector(u32 nSectorNumber) {
    if (!Esp32p4SPIFlash_IsInited()) {
        return false;
    }

    if (nSectorNumber >= (GetChip()->chipSize / GetChip()->sectorSize)) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32p4SPIFlashCache_DisableCache();

    bool bSuccess = (esp_rom_spiflash_erase_sector(nSectorNumber) == kEsp32p4SPIFlash_RomResult_OK);

    Esp32p4SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/****************************************************************************
 * Returns whether flash encryption is enabled or not
 ****************************************************************************/
bool Esp32p4SPIFlash_IsEncryptionEnabled(void) {
    /* The esp32 reported this through EFUSE_BLK0_RDATA0.FLASH_CRYPT_CNT; the
     * esp32p4, like the esp32s3, renamed and moved it to
     * RD_REPEAT_DATA1.SPI_BOOT_CRYPT_CNT, but the odd parity convention is the
     * same */
    u32 nCryptCount = (ESP32_REG(EFUSE_RD_REPEAT_DATA1) & ESP32_REG_MASK(EFUSE, SPI_BOOT_CRYPT_CNT))
                          >> ESP32_REG_SHIFT(EFUSE, SPI_BOOT_CRYPT_CNT);
    return __builtin_parity(nCryptCount);
}

/****************************************************************************
 * Reads straight off the flash, bypassing the cache, so encrypted content
 * comes back as it is stored (no alignment needed)
 ****************************************************************************/
static bool ESP32_IRAM_FUNC Esp32p4SPIFlash_ReadDirect(u32 nAddr, u8 * pBuffer, u32 nSize) {
    /* The ROM read wants a word aligned address, length and destination, so
     * everything lands in a DRAM bounce buffer first */
    u32 nChunk[kReadChunkWords];
    bool bSuccess = true;

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32p4SPIFlashCache_DisableCache();

    while (nSize > 0 && bSuccess) {
        u32 nAligned = nAddr & ~0x3u;
        u32 nSkip    = nAddr - nAligned;
        u32 nWanted  = LT_MIN(nSize, (u32)kReadChunkBytes - nSkip);
        u32 nReadLen = (nSkip + nWanted + 3) & ~0x3u;

        bSuccess = (esp_rom_spiflash_read(nAligned, nChunk, (s32)nReadLen) == kEsp32p4SPIFlash_RomResult_OK);
        if (bSuccess) {
            /* open coded so the copy stays in IRAM - lt_memcpy() is in flash */
            const u8 * pSrc = (const u8 *)nChunk + nSkip;
            for (u32 i = 0; i < nWanted; ++i) {
                pBuffer[i] = pSrc[i];
            }
            pBuffer += nWanted;
            nAddr   += nWanted;
            nSize   -= nWanted;
        }
    }

    Esp32p4SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/****************************************************************************
 * Reads through the cache so encrypted content is decrypted on the way
 * (no alignment needed)
 ****************************************************************************/
static bool ESP32_IRAM_FUNC Esp32p4SPIFlash_ReadMapped(u32 nAddr, u8 * pBuffer, u32 nSize) {
    Esp32p4SPIFlash_MapInfo map;
    map.srcAddr = nAddr;
    map.size    = nSize;

    u32 mask = Esp32DisableInterrupts();
    /* IRAM section begin - Code from here on must run from IRAM */
    u32 state = Esp32p4SPIFlashCache_DisableCache();

    Esp32p4SPIFlashCache_Mmap(&map);

    Esp32p4SPIFlashCache_EnableCache(state);
    /* IRAM section end - Code from here on can run from flash */
    Esp32EnableInterrupts(mask);

    if (map.ptr == NULL) {
        LTLOG_YELLOWALERT("fail.read.map", "Failed to map the flash");
        return false;
    }

    // cache must be enabled before calling lt_memcpy()
    lt_memcpy(pBuffer, map.ptr, nSize);

    mask = Esp32DisableInterrupts();
    /* IRAM section begin - Code from here on must run from IRAM */
    state = Esp32p4SPIFlashCache_DisableCache();

    Esp32p4SPIFlashCache_Ummap(&map);

    Esp32p4SPIFlashCache_EnableCache(state);
    /* IRAM section end - Code from here on can run from flash */
    Esp32EnableInterrupts(mask);

    return true;
}

/****************************************************************************
 * Read from the flash (no alignment needed)
 ****************************************************************************/
bool Esp32p4SPIFlash_Read(u32 nSrcAddr, u8 * pBuffer, u32 nSize, bool bDecrypt) {
    if (!Esp32p4SPIFlash_IsInited()) {
        return false;
    }

    if (nSize == 0) {
        return true;
    }

    if (nSrcAddr > GetChip()->chipSize || nSize > GetChip()->chipSize - nSrcAddr) {
        return false;
    }

    if (bDecrypt) {
        if (!Esp32p4SPIFlash_IsEncryptionEnabled()) {
            // can't decrypt
            return false;
        }
        /* only the cached path decrypts */
        return Esp32p4SPIFlash_ReadMapped(nSrcAddr, pBuffer, nSize);
    }

    return Esp32p4SPIFlash_ReadDirect(nSrcAddr, pBuffer, nSize);
}

/****************************************************************************
 * Write one chunk of word aligned data
 * The caller has already staged the data in DRAM, because the source may
 * itself live in cached flash
 ****************************************************************************/
static bool ESP32_IRAM_FUNC Esp32p4SPIFlash_WriteChunk(u32 nAddr, const u32 * pWords, u32 nSize, bool bEncrypt) {
    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32p4SPIFlashCache_DisableCache();

    bool bSuccess;
    if (bEncrypt) {
        esp_rom_spiflash_write_encrypted_enable();
        bSuccess = (esp_rom_spiflash_write_encrypted(nAddr, (u32 *)pWords, nSize) == kEsp32p4SPIFlash_RomResult_OK);
        esp_rom_spiflash_write_encrypted_disable();
    } else {
        bSuccess = (esp_rom_spiflash_write(nAddr, pWords, (s32)nSize) == kEsp32p4SPIFlash_RomResult_OK);
    }

    Esp32p4SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/****************************************************************************
 * Write word aligned data, staging it a chunk at a time
 * nAddr and nSize must be 4-byte aligned
 ****************************************************************************/
static bool Esp32p4SPIFlash_WriteAligned(u32 nAddr, const u8 * pBuffer, u32 nSize, bool bEncrypt) {
    bool bSuccess = true;

    while (nSize > 0 && bSuccess) {
        u32 nChunk[kWriteChunkWords];
        u32 nCount = LT_MIN(nSize, (u32)kWriteChunkBytes);

        // the source may be in cached flash, so stage it while the cache is up
        lt_memcpy(nChunk, pBuffer, nCount);

        bSuccess = Esp32p4SPIFlash_WriteChunk(nAddr, nChunk, nCount, bEncrypt);

        pBuffer += nCount;
        nAddr   += nCount;
        nSize   -= nCount;
    }

    return bSuccess;
}

/****************************************************************************
 * Write data to Flash (no alignment needed unless encrypting)
 ****************************************************************************/
bool Esp32p4SPIFlash_Write(u32 nAddr, const u8 * pBuffer, u32 nSize, bool bEncrypt) {
    if (!Esp32p4SPIFlash_IsInited()) {
        return false;
    }

    if (nSize == 0) {
        return true;
    }

    if (nAddr + nSize > GetChip()->chipSize) {
        return false;
    }

    if (bEncrypt) {
        if (!Esp32p4SPIFlash_IsEncryptionEnabled()) {
            LTLOG_YELLOWALERT("enc.disabled", "bEncrypt is set, but encryption is not enabled");
            return false;
        }
        if ((nAddr & 0x1f) || (nSize & 0xf)) {
            LTLOG_YELLOWALERT("invalid.enc.align", "Encryption data size must be 16-byte aligned and address must be 32-byte aligned");
            return false;
        }
        return Esp32p4SPIFlash_WriteAligned(nAddr, pBuffer, nSize, true);
    }

    bool bSuccess = true;

    /* leading partial word - read it back, patch it and rewrite the whole word */
    if (nAddr & 0x3) {
        u32 nAligned = nAddr & ~0x3u;
        u32 nOffset  = nAddr - nAligned;
        u32 nCount   = LT_MIN(nSize, (u32)sizeof(u32) - nOffset);
        u32 nWord    = LT_U32_MAX;

        bSuccess = Esp32p4SPIFlash_Read(nAligned, (u8 *)&nWord, sizeof(nWord), false);
        if (bSuccess) {
            lt_memcpy((u8 *)&nWord + nOffset, pBuffer, nCount);
            bSuccess = Esp32p4SPIFlash_WriteAligned(nAligned, (const u8 *)&nWord, sizeof(nWord), false);

            pBuffer += nCount;
            nAddr   += nCount;
            nSize   -= nCount;
        }
    }

    /* whole words */
    u32 nWholeWords = nSize & ~0x3u;
    if (bSuccess && nWholeWords > 0) {
        bSuccess = Esp32p4SPIFlash_WriteAligned(nAddr, pBuffer, nWholeWords, false);

        pBuffer += nWholeWords;
        nAddr   += nWholeWords;
        nSize   -= nWholeWords;
    }

    /* trailing partial word */
    if (bSuccess && nSize > 0) {
        u32 nWord = LT_U32_MAX;

        bSuccess = Esp32p4SPIFlash_Read(nAddr, (u8 *)&nWord, sizeof(nWord), false);
        if (bSuccess) {
            lt_memcpy(&nWord, pBuffer, nSize);
            bSuccess = Esp32p4SPIFlash_WriteAligned(nAddr, (const u8 *)&nWord, sizeof(nWord), false);
        }
    }

    return bSuccess;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Sep-26   claudius    created, from the esp32c3 driver
 */
