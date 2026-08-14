/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/esp32s3/Esp32s3SPIFlash.c
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
#include "Esp32s3SPIFlash.h"
#include "Esp32s3SPIFlashCache.h"

DEFINE_LTLOG_SECTION("esp32s3.spiflash");

/*******************************************************************************
 * consts
*******************************************************************************/
/*
 * These are properties of the flash chip rather than of the esp32s3.  The block
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

/*
 * The ROM's legacy read, write and erase routines send a 24 bit address, so on
 * a part larger than 16MB they silently wrap: a read of 0x1ffb000 comes back
 * with the contents of 0xffb000 and reports success.  Anything reaching at or
 * above this boundary therefore goes out with the four byte address command set
 * instead - see the Cmd4B helpers below.
 */
enum {
    k24BitAddressLimit      = 0x1000000u,
    kFlashPageSize          = 0x100u,
};

/*
 * Four byte address opcodes, and the plain JEDEC identify.
 *
 * These are the 4B command variants rather than the EN4B mode switch on
 * purpose.  EN4B puts the whole chip into four byte addressing, including the
 * path the cache uses to fetch instructions and rodata, which the bootloader
 * has already configured for three; the 4B opcodes carry the wide address in
 * the transaction alone and leave that path untouched.
 */
enum {
    kFlashCmd_ReadId        = 0x9f,
    kFlashCmd_FastRead4B    = 0x0c,
    kFlashCmd_PageProgram4B = 0x12,
    kFlashCmd_EraseSector4B = 0x21,
    kFlashCmd_EraseBlock4B  = 0xdc,
};

/* SPI1 port number for esp_rom_opiflash_exec_cmd(), and the read mode that
 * makes it drive a plain single line SPI transaction */
enum {
    kSpiPort_User           = 1,
    kRomSpiFlashMode_FastRd = 4,
    kCsMask_Flash           = 0x01,

    /* The MSPI data buffer a transaction reads from and writes back into is
     * W0 to W15, so no single transaction can move more than 64 bytes */
    kMSPI_DataBufferBytes   = 64,
};

/*******************************************************************************
 * ROM
*******************************************************************************/
typedef u32 Esp32s3SPIFlash_RomResult;
enum Esp32s3SPIFlash_RomResult {
    kEsp32s3SPIFlash_RomResult_OK       = 0,
    kEsp32s3SPIFlash_RomResult_Error    = 1,
    kEsp32s3SPIFlash_RomResult_Timeout  = 2,
};

/*
 * The esp32 kept the ROM's notion of the attached flash chip in a fixed global,
 * g_rom_spiflash_chip.  The esp32s3 ROM instead exports a pointer to a block
 * holding the chip description followed by the per-mode extra dummy cycle
 * counts; layout must match the ROM's spiflash_legacy_data_t.
 */
typedef struct {
    Esp32s3SPIFlash_Chip    chip;
    u8                      dummyLenPlus[3];
    u8                      sigMatrix;
} Esp32s3SPIFlash_LegacyData;

extern Esp32s3SPIFlash_LegacyData * rom_spiflash_legacy_data;

extern Esp32s3SPIFlash_RomResult esp_rom_spiflash_read_status(Esp32s3SPIFlash_Chip * pChip, u32 * pStatus);
extern Esp32s3SPIFlash_RomResult esp_rom_spiflash_read_statushigh(Esp32s3SPIFlash_Chip * pChip, u32 * pStatus);
extern Esp32s3SPIFlash_RomResult esp_rom_spiflash_write_status(Esp32s3SPIFlash_Chip * pChip, u32 nStatus);
extern Esp32s3SPIFlash_RomResult esp_rom_spiflash_wait_idle(Esp32s3SPIFlash_Chip * pChip);
extern Esp32s3SPIFlash_RomResult esp_rom_spiflash_unlock(void);
extern Esp32s3SPIFlash_RomResult esp_rom_spiflash_erase_chip(void);
extern Esp32s3SPIFlash_RomResult esp_rom_spiflash_erase_block(u32 nBlockNumber);
extern Esp32s3SPIFlash_RomResult esp_rom_spiflash_erase_sector(u32 nSectorNumber);
extern Esp32s3SPIFlash_RomResult esp_rom_spiflash_write(u32 nDestAddr, const u32 * pSrc, s32 nLen);
extern Esp32s3SPIFlash_RomResult esp_rom_spiflash_read(u32 nSrcAddr, u32 * pDest, s32 nLen);
extern Esp32s3SPIFlash_RomResult esp_rom_spiflash_write_encrypted(u32 nFlashAddr, u32 * pData, u32 nLen);
extern void esp_rom_spiflash_write_encrypted_enable(void);
extern void esp_rom_spiflash_write_encrypted_disable(void);

/*
 * The general purpose MSPI transaction driver.  The legacy API above is a set
 * of fixed transactions built on top of it; this is the raw form, and the only
 * way to send an opcode the legacy API has no entry point for.  The BSP's PSRAM
 * init drives CS1 with it - here it is CS0, the flash.
 *
 * nMode selects line count and data rate; kRomSpiFlashMode_FastRd is a plain
 * single line transfer.  bIsWriteEraseOperation is left false throughout and
 * the write enable and the wait for idle are done explicitly instead, so that
 * every step of a program or erase is visible here rather than in the ROM.
 */
extern void esp_rom_opiflash_exec_cmd(int nSpiNum, int nMode,
                                      u32 nCmd, int nCmdBitLen,
                                      u32 nAddr, int nAddrBitLen,
                                      int nDummyBits,
                                      u8 * pMosiData, int nMosiBitLen,
                                      u8 * pMisoData, int nMisoBitLen,
                                      u32 nCsMask,
                                      bool bIsWriteEraseOperation);

/*******************************************************************************
 * static variables
*******************************************************************************/
static bool s_bInited = false;

/*
 * Every static below that suspends the cache is ESP32_IRAM_FUNC rather than a
 * plain ESP32_MEM_REGION(IRAM), and must stay that way - a section attribute on
 * its own does not survive gcc inlining the function into a caller in flash,
 * and this file is where that was first found the hard way.  See the macro in
 * Esp32_SoC.h for the mechanism and for how to check the result:
 *
 *   xtensa-esp32s3-elf-nm -n LTFirmwareImage.elf | grep Esp32s3SPIFlash
 */

/* Forward declared because Esp32s3SPIFlash_Init() calls it; the attributes have
 * to be repeated here, since gcc applies a section attribute it first sees on
 * the definition only if nothing has already been emitted against the name */
static u32 ESP32_IRAM_FUNC Esp32s3SPIFlash_ReadChipSize(void);

/*******************************************************************************
 *
 * implementation
 *
*******************************************************************************/

static Esp32s3SPIFlash_Chip * GetChip(void) {
    return &rom_spiflash_legacy_data->chip;
}

/*******************************************************************************
 * Initializes the driver
 * Returns a pointer to the flash chip the bootloader configured
*******************************************************************************/
const Esp32s3SPIFlash_Chip * Esp32s3SPIFlash_Init(const char* pChipName) {
    LT_UNUSED(pChipName);

    /* The ROM populates this while loading the second stage bootloader; if it is
     * empty then nothing has configured the MSPI and there is nothing to talk to */
    if (rom_spiflash_legacy_data == NULL || GetChip()->chipSize == 0 || GetChip()->sectorSize == 0) {
        return NULL;
    }

    s_bInited = true;

    /*
     * The size the ROM is holding did not come from the chip.  The second stage
     * bootloader takes it from the flash size nibble of its own image header,
     * which is whatever esptool was told at mastering time, and hands it to
     * esp_rom_spiflash_config_param().  Get it wrong and everything past the
     * declared size is refused by the bounds checks below, which is how a
     * partition in the top of a 32MB part comes back unreadable on a board
     * mastered as 4MB.  esptool's image header cannot express more than 16MB in
     * any case, so ask the chip and believe the answer.
     */
    u32 nChipSize = Esp32s3SPIFlash_ReadChipSize();
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
static bool Esp32s3SPIFlash_IsInited(void) {
    return s_bInited;
}

/*******************************************************************************
 * Issue the hardware generated write enable command
 * @note the cache must be disabled before calling this function
*******************************************************************************/
static bool ESP32_IRAM_FUNC Esp32s3SPIFlash_EnableWrite(void) {
    /* IRAM section begin - Code from here on must run from IRAM */

    if (esp_rom_spiflash_wait_idle(GetChip()) != kEsp32s3SPIFlash_RomResult_OK) {
        return false;
    }

    ESP32_SPIMEM_REG(1, CMD) = ESP32_REG_MASK(SPIMEM, CMD_FLASH_WREN);
    volatile u16 nTimeout = LT_U16_MAX;
    while (ESP32_SPIMEM_REG(1, CMD) != 0 && --nTimeout != 0);

    /* IRAM section end - Code from here on can run from flash */

    return (nTimeout != 0);
}

/*******************************************************************************
 * Send one single line transaction on SPI1 to the flash
 * @note the cache must be disabled before calling this function
*******************************************************************************/
static void ESP32_IRAM_FUNC Esp32s3SPIFlash_Cmd(u32 nCmd, u32 nAddr, int nAddrBits, int nDummyBits,
                                                const u32 * pMosi, u32 nMosiBytes,
                                                u32 * pMiso, u32 nMisoBytes) {
    /* The data buffer keeps whatever the last transaction left in it, and a
     * read only overwrites the words it fills, so a short read would otherwise
     * pick up the tail of the one before */
    if (nMisoBytes > 0) {
        volatile u32 * pW = &ESP32_SPIMEM_REG(1, W0);
        for (u32 i = 0; i < (kMSPI_DataBufferBytes / 4); ++i) {
            pW[i] = 0;
        }
    }

    esp_rom_opiflash_exec_cmd(kSpiPort_User, kRomSpiFlashMode_FastRd,
                              nCmd, 8,
                              nAddr, nAddrBits,
                              nDummyBits,
                              (u8 *)pMosi, (int)(nMosiBytes * 8),
                              (u8 *)pMiso, (int)(nMisoBytes * 8),
                              kCsMask_Flash,
                              false);
}

/*******************************************************************************
 * Read with a four byte address
 * nSize must be no more than the 64 byte MSPI data buffer
 * @note the cache must be disabled before calling this function
*******************************************************************************/
static bool ESP32_IRAM_FUNC Esp32s3SPIFlash_Read4B(u32 nAddr, u32 * pWords, u32 nSize) {
    if (nSize == 0 || nSize > kMSPI_DataBufferBytes) {
        return false;
    }

    /* 0x0c is fast read, which spends eight cycles turning the bus around
     * before the first data byte */
    Esp32s3SPIFlash_Cmd(kFlashCmd_FastRead4B, nAddr, 32, 8, NULL, 0, pWords, nSize);
    return true;
}

/*******************************************************************************
 * Program with a four byte address
 * @note the cache must be disabled before calling this function
*******************************************************************************/
static bool ESP32_IRAM_FUNC Esp32s3SPIFlash_Program4B(u32 nAddr, const u32 * pWords, u32 nSize) {
    const u8 * pBytes = (const u8 *)pWords;

    while (nSize > 0) {
        /* A page program that runs off the end of a 256 byte page wraps back to
         * the start of the same page rather than carrying into the next one */
        u32 nRoom  = kFlashPageSize - (nAddr & (kFlashPageSize - 1));
        u32 nCount = LT_MIN(nSize, LT_MIN(nRoom, (u32)kMSPI_DataBufferBytes));

        if (!Esp32s3SPIFlash_EnableWrite()) {
            return false;
        }

        Esp32s3SPIFlash_Cmd(kFlashCmd_PageProgram4B, nAddr, 32, 0, (const u32 *)pBytes, nCount, NULL, 0);

        if (esp_rom_spiflash_wait_idle(GetChip()) != kEsp32s3SPIFlash_RomResult_OK) {
            return false;
        }

        pBytes += nCount;
        nAddr  += nCount;
        nSize  -= nCount;
    }

    return true;
}

/*******************************************************************************
 * Erase with a four byte address
 * @note the cache must be disabled before calling this function
*******************************************************************************/
static bool ESP32_IRAM_FUNC Esp32s3SPIFlash_Erase4B(u32 nCmd, u32 nAddr) {
    if (!Esp32s3SPIFlash_EnableWrite()) {
        return false;
    }

    Esp32s3SPIFlash_Cmd(nCmd, nAddr, 32, 0, NULL, 0, NULL, 0);

    return (esp_rom_spiflash_wait_idle(GetChip()) == kEsp32s3SPIFlash_RomResult_OK);
}

/*******************************************************************************
 * Ask the chip how big it is
 * Returns 0 if it answers with something that is not a size
*******************************************************************************/
static u32 ESP32_IRAM_FUNC Esp32s3SPIFlash_ReadChipSize(void) {
    u32 nId = 0;

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32s3SPIFlashCache_DisableCache();

    Esp32s3SPIFlash_Cmd(kFlashCmd_ReadId, 0, 0, 0, NULL, 0, &nId, 3);

    Esp32s3SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    /* JEDEC answers with the manufacturer, then the memory type, then the
     * capacity, and the bytes land in the data buffer in that order, so the
     * capacity is the third one.  Every part this would be built against
     * encodes it as the log2 of the size in bytes */
    u32 nCapacity = (nId >> 16) & 0xffu;
    if (nCapacity < 20 || nCapacity > 26) {   /* 1MB to 64MB */
        return 0;
    }

    return 1u << nCapacity;
}

/*******************************************************************************
 * Unlock the flash
 * The flash chips support protecting a range of blocks or predefined blocks, but
 * does not support specific individual blocks
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32s3SPIFlash_WriteUnprotect(void) {
    if (!Esp32s3SPIFlash_IsInited()) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32s3SPIFlashCache_DisableCache();

    /* The ROM routine knows which vendors keep their protect bits where, and
     * preserves the quad enable bit, so there is no reason to hand roll this */
    bool bSuccess = (esp_rom_spiflash_unlock() == kEsp32s3SPIFlash_RomResult_OK);

    Esp32s3SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * Lock the flash
 * @note esp_rom_spiflash_lock() is declared by the ROM headers but is not one of
 * the entry points the esp32s3 ROM exports, so the sequence is open coded here
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32s3SPIFlash_WriteProtect(void) {
    if (!Esp32s3SPIFlash_IsInited()) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32s3SPIFlashCache_DisableCache();

    /* read_statushigh returns the second status byte already shifted into
     * position, so the two halves can simply be ored together */
    u32 nStatus     = 0;
    u32 nStatusHigh = 0;
    bool bSuccess = (esp_rom_spiflash_read_status(GetChip(), &nStatus) == kEsp32s3SPIFlash_RomResult_OK)
                 && (esp_rom_spiflash_read_statushigh(GetChip(), &nStatusHigh) == kEsp32s3SPIFlash_RomResult_OK);

    if (bSuccess) {
        /* Send both status bytes.  A single byte WRSR would leave the quad
         * enable bit in the second byte undefined on some parts, which would
         * cost us the cached flash mapping we are running from */
        u32 nCtrl = ESP32_SPIMEM_REG(1, CTRL);
        ESP32_SPIMEM_REG(1, CTRL) = nCtrl | ESP32_REG_MASK(SPIMEM, CTRL_WRSR_2B);

        bSuccess = Esp32s3SPIFlash_EnableWrite();
        if (bSuccess) {
            bSuccess = (esp_rom_spiflash_write_status(GetChip(), nStatusHigh | kSPIFlashWriteProtect)
                            == kEsp32s3SPIFlash_RomResult_OK);
        }

        ESP32_SPIMEM_REG(1, CTRL) = nCtrl;
    }

    Esp32s3SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * Returns whether the flash is locked or unlocked
 * @note the block protect bits are in the low status byte.  The esp32 driver
 * tests them against the high byte instead, which can never match
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32s3SPIFlash_IsLocked(void) {
    if (!Esp32s3SPIFlash_IsInited()) {
        return false;
    }

    u32 nStatus = 0;

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32s3SPIFlashCache_DisableCache();

    bool bSuccess = (esp_rom_spiflash_read_status(GetChip(), &nStatus) == kEsp32s3SPIFlash_RomResult_OK);

    Esp32s3SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return (bSuccess && (nStatus & kSPIFlashWriteProtect) != 0);
}

/*******************************************************************************
 * erase the entire flash
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32s3SPIFlash_EraseChip(void) {
    if (!Esp32s3SPIFlash_IsInited()) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32s3SPIFlashCache_DisableCache();

    bool bSuccess = (esp_rom_spiflash_erase_chip() == kEsp32s3SPIFlash_RomResult_OK);

    Esp32s3SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * erase a block
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32s3SPIFlash_EraseBlock(u32 nBlockNumber) {
    if (!Esp32s3SPIFlash_IsInited()) {
        return false;
    }

    if (nBlockNumber >= (GetChip()->chipSize / GetChip()->blockSize)) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32s3SPIFlashCache_DisableCache();

    u32 nAddr = nBlockNumber * GetChip()->blockSize;
    bool bSuccess = (nAddr >= k24BitAddressLimit)
                        ? Esp32s3SPIFlash_Erase4B(kFlashCmd_EraseBlock4B, nAddr)
                        : (esp_rom_spiflash_erase_block(nBlockNumber) == kEsp32s3SPIFlash_RomResult_OK);

    Esp32s3SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * erase a sector
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32s3SPIFlash_EraseSector(u32 nSectorNumber) {
    if (!Esp32s3SPIFlash_IsInited()) {
        return false;
    }

    if (nSectorNumber >= (GetChip()->chipSize / GetChip()->sectorSize)) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32s3SPIFlashCache_DisableCache();

    u32 nAddr = nSectorNumber * GetChip()->sectorSize;
    bool bSuccess = (nAddr >= k24BitAddressLimit)
                        ? Esp32s3SPIFlash_Erase4B(kFlashCmd_EraseSector4B, nAddr)
                        : (esp_rom_spiflash_erase_sector(nSectorNumber) == kEsp32s3SPIFlash_RomResult_OK);

    Esp32s3SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/****************************************************************************
 * Returns whether flash encryption is enabled or not
 ****************************************************************************/
bool Esp32s3SPIFlash_IsEncryptionEnabled(void) {
    /* The esp32 reported this through EFUSE_BLK0_RDATA0.FLASH_CRYPT_CNT; the
     * esp32s3 renamed and moved it to RD_REPEAT_DATA1.SPI_BOOT_CRYPT_CNT, but
     * the odd parity convention is the same */
    u32 nCryptCount = (ESP32_REG(EFUSE_RD_REPEAT_DATA1) & ESP32_REG_MASK(EFUSE, SPI_BOOT_CRYPT_CNT))
                          >> ESP32_REG_SHIFT(EFUSE, SPI_BOOT_CRYPT_CNT);
    return __builtin_parity(nCryptCount);
}

/****************************************************************************
 * Reads straight off the flash, bypassing the cache, so encrypted content
 * comes back as it is stored (no alignment needed)
 ****************************************************************************/
static bool ESP32_IRAM_FUNC Esp32s3SPIFlash_ReadDirect(u32 nAddr, u8 * pBuffer, u32 nSize) {
    /* The ROM read wants a word aligned address, length and destination, so
     * everything lands in a DRAM bounce buffer first */
    u32 nChunk[kReadChunkWords];
    bool bSuccess = true;

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32s3SPIFlashCache_DisableCache();

    while (nSize > 0 && bSuccess) {
        u32 nAligned = nAddr & ~0x3u;
        u32 nSkip    = nAddr - nAligned;
        u32 nWanted  = LT_MIN(nSize, (u32)kReadChunkBytes - nSkip);

        /* Never let one transaction cross the 16MB line, so that each is wholly
         * three byte addressed or wholly four */
        if (nAligned < k24BitAddressLimit && (nAddr + nWanted) > k24BitAddressLimit) {
            nWanted = k24BitAddressLimit - nAddr;
        }

        u32 nReadLen = (nSkip + nWanted + 3) & ~0x3u;

        if (nAligned >= k24BitAddressLimit) {
            bSuccess = Esp32s3SPIFlash_Read4B(nAligned, nChunk, nReadLen);
        } else {
            bSuccess = (esp_rom_spiflash_read(nAligned, nChunk, (s32)nReadLen) == kEsp32s3SPIFlash_RomResult_OK);
        }
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

    Esp32s3SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/****************************************************************************
 * Reads through the cache so encrypted content is decrypted on the way
 * (no alignment needed)
 ****************************************************************************/
static bool ESP32_IRAM_FUNC Esp32s3SPIFlash_ReadMapped(u32 nAddr, u8 * pBuffer, u32 nSize) {
    Esp32s3SPIFlash_MapInfo map;
    map.srcAddr = nAddr;
    map.size    = nSize;

    u32 mask = Esp32DisableInterrupts();
    /* IRAM section begin - Code from here on must run from IRAM */
    u32 state = Esp32s3SPIFlashCache_DisableCache();

    Esp32s3SPIFlashCache_Mmap(&map);

    Esp32s3SPIFlashCache_EnableCache(state);
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
    state = Esp32s3SPIFlashCache_DisableCache();

    Esp32s3SPIFlashCache_Ummap(&map);

    Esp32s3SPIFlashCache_EnableCache(state);
    /* IRAM section end - Code from here on can run from flash */
    Esp32EnableInterrupts(mask);

    return true;
}

/****************************************************************************
 * Read from the flash (no alignment needed)
 ****************************************************************************/
bool Esp32s3SPIFlash_Read(u32 nSrcAddr, u8 * pBuffer, u32 nSize, bool bDecrypt) {
    if (!Esp32s3SPIFlash_IsInited()) {
        return false;
    }

    if (nSize == 0) {
        return true;
    }

    if (nSrcAddr + nSize > GetChip()->chipSize) {
        return false;
    }

    if (bDecrypt) {
        if (!Esp32s3SPIFlash_IsEncryptionEnabled()) {
            // can't decrypt
            return false;
        }
        /* The cache fetches with the read command the bootloader configured,
         * which carries a three byte address, so the mapped path cannot reach
         * the top of a part larger than 16MB.  Refuse rather than hand back the
         * contents of the address it would wrap to */
        if (nSrcAddr + nSize > k24BitAddressLimit) {
            LTLOG_YELLOWALERT("dec.unreachable", "Cannot decrypt at 0x%lx - beyond the mapped 16MB", LT_Pu32(nSrcAddr));
            return false;
        }
        /* only the cached path decrypts */
        return Esp32s3SPIFlash_ReadMapped(nSrcAddr, pBuffer, nSize);
    }

    return Esp32s3SPIFlash_ReadDirect(nSrcAddr, pBuffer, nSize);
}

/****************************************************************************
 * Write one chunk of word aligned data
 * The caller has already staged the data in DRAM, because the source may
 * itself live in cached flash
 ****************************************************************************/
static bool ESP32_IRAM_FUNC Esp32s3SPIFlash_WriteChunk(u32 nAddr, const u32 * pWords, u32 nSize, bool bEncrypt) {
    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32s3SPIFlashCache_DisableCache();

    bool bSuccess;
    if (bEncrypt) {
        esp_rom_spiflash_write_encrypted_enable();
        bSuccess = (esp_rom_spiflash_write_encrypted(nAddr, (u32 *)pWords, nSize) == kEsp32s3SPIFlash_RomResult_OK);
        esp_rom_spiflash_write_encrypted_disable();
    } else if (nAddr >= k24BitAddressLimit) {
        bSuccess = Esp32s3SPIFlash_Program4B(nAddr, pWords, nSize);
    } else {
        bSuccess = (esp_rom_spiflash_write(nAddr, pWords, (s32)nSize) == kEsp32s3SPIFlash_RomResult_OK);
    }

    Esp32s3SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/****************************************************************************
 * Write word aligned data, staging it a chunk at a time
 * nAddr and nSize must be 4-byte aligned
 ****************************************************************************/
static bool Esp32s3SPIFlash_WriteAligned(u32 nAddr, const u8 * pBuffer, u32 nSize, bool bEncrypt) {
    bool bSuccess = true;

    while (nSize > 0 && bSuccess) {
        u32 nChunk[kWriteChunkWords];
        u32 nCount = LT_MIN(nSize, (u32)kWriteChunkBytes);

        /* Never let one chunk cross the 16MB line, so that each is wholly three
         * byte addressed or wholly four */
        if (nAddr < k24BitAddressLimit && (nAddr + nCount) > k24BitAddressLimit) {
            nCount = k24BitAddressLimit - nAddr;
        }

        // the source may be in cached flash, so stage it while the cache is up
        lt_memcpy(nChunk, pBuffer, nCount);

        bSuccess = Esp32s3SPIFlash_WriteChunk(nAddr, nChunk, nCount, bEncrypt);

        pBuffer += nCount;
        nAddr   += nCount;
        nSize   -= nCount;
    }

    return bSuccess;
}

/****************************************************************************
 * Write data to Flash (no alignment needed unless encrypting)
 ****************************************************************************/
bool Esp32s3SPIFlash_Write(u32 nAddr, const u8 * pBuffer, u32 nSize, bool bEncrypt) {
    if (!Esp32s3SPIFlash_IsInited()) {
        return false;
    }

    if (nSize == 0) {
        return true;
    }

    if (nAddr + nSize > GetChip()->chipSize) {
        return false;
    }

    if (bEncrypt) {
        if (!Esp32s3SPIFlash_IsEncryptionEnabled()) {
            LTLOG_YELLOWALERT("enc.disabled", "bEncrypt is set, but encryption is not enabled");
            return false;
        }
        if ((nAddr & 0x1f) || (nSize & 0xf)) {
            LTLOG_YELLOWALERT("invalid.enc.align", "Encryption data size must be 16-byte aligned and address must be 32-byte aligned");
            return false;
        }
        /* The ROM's encrypted write sends a three byte address and there is no
         * four byte address form of it to fall back on, since the encryption
         * happens inside the ROM routine */
        if (nAddr + nSize > k24BitAddressLimit) {
            LTLOG_YELLOWALERT("enc.unreachable", "Cannot encrypt at 0x%lx - beyond the addressable 16MB", LT_Pu32(nAddr));
            return false;
        }
        return Esp32s3SPIFlash_WriteAligned(nAddr, pBuffer, nSize, true);
    }

    bool bSuccess = true;

    /* leading partial word - read it back, patch it and rewrite the whole word */
    if (nAddr & 0x3) {
        u32 nAligned = nAddr & ~0x3u;
        u32 nOffset  = nAddr - nAligned;
        u32 nCount   = LT_MIN(nSize, (u32)sizeof(u32) - nOffset);
        u32 nWord    = LT_U32_MAX;

        bSuccess = Esp32s3SPIFlash_Read(nAligned, (u8 *)&nWord, sizeof(nWord), false);
        if (bSuccess) {
            lt_memcpy((u8 *)&nWord + nOffset, pBuffer, nCount);
            bSuccess = Esp32s3SPIFlash_WriteAligned(nAligned, (const u8 *)&nWord, sizeof(nWord), false);

            pBuffer += nCount;
            nAddr   += nCount;
            nSize   -= nCount;
        }
    }

    /* whole words */
    u32 nWholeWords = nSize & ~0x3u;
    if (bSuccess && nWholeWords > 0) {
        bSuccess = Esp32s3SPIFlash_WriteAligned(nAddr, pBuffer, nWholeWords, false);

        pBuffer += nWholeWords;
        nAddr   += nWholeWords;
        nSize   -= nWholeWords;
    }

    /* trailing partial word */
    if (bSuccess && nSize > 0) {
        u32 nWord = LT_U32_MAX;

        bSuccess = Esp32s3SPIFlash_Read(nAddr, (u8 *)&nWord, sizeof(nWord), false);
        if (bSuccess) {
            lt_memcpy(&nWord, pBuffer, nSize);
            bSuccess = Esp32s3SPIFlash_WriteAligned(nAddr, (const u8 *)&nWord, sizeof(nWord), false);
        }
    }

    return bSuccess;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jul-26   claudius    created
 *  05-Aug-26   claudius    kept the cache suspending helpers out of flash with
 *                          LT_NOINLINE, now spelled ESP32_IRAM_FUNC
 *  05-Aug-26   claudius    took the chip size from the chip rather than from the
 *                          image header; reached past 16MB with four byte
 *                          address commands
 */
