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

/*
 * The ROM's legacy read, write and erase routines send a 24 bit address, so on
 * a part larger than 16MB they silently wrap: a write meant for 0x1200000 lands
 * at 0x200000 and the ROM reports success (measured on a 32MB part).  Anything
 * reaching at or above this boundary therefore goes out with the four byte
 * address command set instead - see the 4B helpers below.
 */
enum {
    k24BitAddressLimit      = 0x1000000u,
    kFlashPageSize          = 0x100u,
};

/*
 * Four byte address opcodes.
 *
 * These are the 4B command variants rather than the EN4B mode switch on
 * purpose.  EN4B puts the whole chip into four byte addressing, including the
 * path the ROM boots from and the cache fetches instructions through, both of
 * which are configured for three: a chip left in that mode across a reset would
 * not boot.  The 4B opcodes carry the wide address in the transaction alone and
 * leave every other path untouched.
 */
enum {
    kFlashCmd_FastRead4B    = 0x0c,
    kFlashCmd_PageProgram4B = 0x12,
    kFlashCmd_EraseSector4B = 0x21,
    kFlashCmd_EraseBlock4B  = 0xdc,
};

/* The MSPI data buffer a transaction reads from and writes back into is W0 to
 * W15, so no single transaction can move more than 64 bytes */
enum {
    kMSPI_DataBufferBytes   = 64,
    kMSPI_DataBufferWords   = kMSPI_DataBufferBytes / 4,
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
 * Send one single line transaction on SPI1 to the flash
 *
 * The esp32s3 driver does this with the ROM's esp_rom_opiflash_exec_cmd(); the
 * esp32p4 ROM has no such entry point, so the transaction is built in the
 * registers, the way the second stage bootloader's own
 * bootloader_flash_execute_command_common() does, which is the one routine in
 * this tree already known to work on this part.  Only the fields that matter are
 * touched, because the rest of USER, USER1 and CTRL carry the controller's
 * timing, and CTRL, USER and USER1 are put back afterwards for the ROM's own
 * routines, which assume them.
 *
 * nMosiBytes and nMisoBytes are at most the 64 byte data buffer.
 * @note the cache must be disabled before calling this function
*******************************************************************************/
static void ESP32_IRAM_FUNC Esp32p4SPIFlash_Cmd(u32 nCmd, u32 nAddr, int nAddrBits, int nDummyBits,
                                                const u32 * pMosi, u32 nMosiBytes,
                                                u32 * pMiso, u32 nMisoBytes) {
    volatile u32 * pW = &ESP32_SPIMEM_REG(1, W0);

    const u32 nOldCtrl   = ESP32_SPIMEM_REG(1, CTRL);
    const u32 nOldUser   = ESP32_SPIMEM_REG(1, USER);
    const u32 nOldUser1  = ESP32_SPIMEM_REG(1, USER1);
    const u32 nOldFctrl  = ESP32_SPIMEM_REG(1, CACHE_FCTRL);

    /* Keep WP high while idle, otherwise a flash in DIO mode leaves it */
    ESP32_SPIMEM_REG(1, CTRL) = ESP32_REG_MASK(SPIMEM, CTRL_WP);

    /* Without this bit the controller's user mode address is 24 bits wide: a 32 bit
     * address phase goes out as the low three bytes of ADDR followed by a zero byte,
     * so (A & 0xffffff) << 8 reaches the flash and the top byte never does.  A
     * program at 0x1fd0000 landed at 0x1000000 (measured), and the chunks after it
     * 0x2000 apart.  IDF sets the same bit for a 32 bit address in
     * spimem_flash_ll_set_addr_bitlen(), and clears it for anything else. */
    if (nAddrBits == 32) {
        ESP32_SPIMEM_REG(1, CACHE_FCTRL) = nOldFctrl | ESP32_REG_MASK(SPIMEM, CACHE_FCTRL_USR_ADDR_4BYTE);
    }

    /* The data buffer keeps whatever the last transaction left in it, and a read
     * only overwrites the words it fills, so a short read would otherwise pick
     * up the tail of the one before */
    if (nMisoBytes > 0) {
        for (u32 i = 0; i < kMSPI_DataBufferWords; ++i) {
            pW[i] = 0;
        }
    }
    if (nMosiBytes > 0) {
        for (u32 i = 0; i < (nMosiBytes + 3) / 4; ++i) {
            pW[i] = pMosi[i];
        }
    }

    /* command phase, eight bits */
    ESP32_SPIMEM_REG(1, USER2) = (nCmd << ESP32_REG_SHIFT(SPIMEM, USER2_USR_COMMAND_VALUE))
                               | (7u   << ESP32_REG_SHIFT(SPIMEM, USER2_USR_COMMAND_BITLEN));

    /* address phase */
    u32 nUser1 = nOldUser1 & ~(ESP32_REG_MASK(SPIMEM, USER1_USR_ADDR_BITLEN)
                             | ESP32_REG_MASK(SPIMEM, USER1_USR_DUMMY_CYCLELEN));
    if (nAddrBits > 0) {
        nUser1 |= ((u32)(nAddrBits - 1) << ESP32_REG_SHIFT(SPIMEM, USER1_USR_ADDR_BITLEN));
    }
    ESP32_SPIMEM_REG(1, ADDR) = nAddr;

    /* dummy phase, which only a read has.  The ROM keeps an extra cycle count
     * per read mode to compensate for the timing it tuned; entry 1 is the one
     * for a user transaction, and the bootloader adds it the same way */
    u32 nUser = nOldUser & ~(ESP32_REG_MASK(SPIMEM, USER_USR_MOSI)
                           | ESP32_REG_MASK(SPIMEM, USER_USR_MISO)
                           | ESP32_REG_MASK(SPIMEM, USER_USR_DUMMY)
                           | ESP32_REG_MASK(SPIMEM, USER_USR_ADDR)
                           | ESP32_REG_MASK(SPIMEM, USER_USR_COMMAND));
    nUser |= ESP32_REG_MASK(SPIMEM, USER_USR_COMMAND);
    if (nAddrBits > 0) {
        nUser |= ESP32_REG_MASK(SPIMEM, USER_USR_ADDR);
    }
    if (nMisoBytes > 0) {
        u32 nTotalDummy = (u32)nDummyBits + rom_spiflash_legacy_data->dummyLenPlus[1];
        if (nTotalDummy > 0) {
            nUser  |= ESP32_REG_MASK(SPIMEM, USER_USR_DUMMY);
            nUser1 |= ((nTotalDummy - 1) << ESP32_REG_SHIFT(SPIMEM, USER1_USR_DUMMY_CYCLELEN));
        }
        nUser |= ESP32_REG_MASK(SPIMEM, USER_USR_MISO);
        ESP32_SPIMEM_REG(1, MISO_DLEN) = (nMisoBytes * 8 - 1) << ESP32_REG_SHIFT(SPIMEM, MISO_DLEN_USR_MISO_DBITLEN);
    }
    if (nMosiBytes > 0) {
        nUser |= ESP32_REG_MASK(SPIMEM, USER_USR_MOSI);
        ESP32_SPIMEM_REG(1, MOSI_DLEN) = (nMosiBytes * 8 - 1) << ESP32_REG_SHIFT(SPIMEM, MOSI_DLEN_USR_MOSI_DBITLEN);
    }
    ESP32_SPIMEM_REG(1, USER1) = nUser1;
    ESP32_SPIMEM_REG(1, USER)  = nUser;

    /* go, and wait for the bit to clear */
    ESP32_SPIMEM_REG(1, CMD) = ESP32_REG_MASK(SPIMEM, CMD_USR);
    while (ESP32_SPIMEM_REG(1, CMD) & ESP32_REG_MASK(SPIMEM, CMD_USR)) {
    }

    ESP32_SPIMEM_REG(1, CTRL)  = nOldCtrl;
    ESP32_SPIMEM_REG(1, USER)  = nOldUser;
    ESP32_SPIMEM_REG(1, USER1) = nOldUser1;
    ESP32_SPIMEM_REG(1, CACHE_FCTRL) = nOldFctrl;

    if (nMisoBytes > 0) {
        for (u32 i = 0; i < (nMisoBytes + 3) / 4; ++i) {
            pMiso[i] = pW[i];
        }
    }
}

/*******************************************************************************
 * Read with a four byte address
 * nSize must be no more than the 64 byte MSPI data buffer
 * @note the cache must be disabled before calling this function
*******************************************************************************/
static bool ESP32_IRAM_FUNC Esp32p4SPIFlash_Read4B(u32 nAddr, u32 * pWords, u32 nSize) {
    if (nSize == 0 || nSize > kMSPI_DataBufferBytes) {
        return false;
    }

    /* 0x0c is fast read, which spends eight cycles turning the bus around
     * before the first data byte */
    Esp32p4SPIFlash_Cmd(kFlashCmd_FastRead4B, nAddr, 32, 8, NULL, 0, pWords, nSize);
    return true;
}

/*******************************************************************************
 * Program with a four byte address
 * @note the cache must be disabled before calling this function
*******************************************************************************/
static bool ESP32_IRAM_FUNC Esp32p4SPIFlash_Program4B(u32 nAddr, const u32 * pWords, u32 nSize) {
    const u8 * pBytes = (const u8 *)pWords;

    while (nSize > 0) {
        /* A page program that runs off the end of a 256 byte page wraps back to
         * the start of the same page rather than carrying into the next one */
        u32 nRoom  = kFlashPageSize - (nAddr & (kFlashPageSize - 1));
        u32 nCount = LT_MIN(nSize, LT_MIN(nRoom, (u32)kMSPI_DataBufferBytes));

        if (!Esp32p4SPIFlash_EnableWrite()) {
            return false;
        }

        Esp32p4SPIFlash_Cmd(kFlashCmd_PageProgram4B, nAddr, 32, 0, (const u32 *)pBytes, nCount, NULL, 0);

        if (esp_rom_spiflash_wait_idle(GetChip()) != kEsp32p4SPIFlash_RomResult_OK) {
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
static bool ESP32_IRAM_FUNC Esp32p4SPIFlash_Erase4B(u32 nCmd, u32 nAddr) {
    if (!Esp32p4SPIFlash_EnableWrite()) {
        return false;
    }

    Esp32p4SPIFlash_Cmd(nCmd, nAddr, 32, 0, NULL, 0, NULL, 0);

    return (esp_rom_spiflash_wait_idle(GetChip()) == kEsp32p4SPIFlash_RomResult_OK);
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

    u32 nAddr = nBlockNumber * GetChip()->blockSize;
    bool bSuccess = (nAddr >= k24BitAddressLimit)
                        ? Esp32p4SPIFlash_Erase4B(kFlashCmd_EraseBlock4B, nAddr)
                        : (esp_rom_spiflash_erase_block(nBlockNumber) == kEsp32p4SPIFlash_RomResult_OK);

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

    u32 nAddr = nSectorNumber * GetChip()->sectorSize;
    bool bSuccess = (nAddr >= k24BitAddressLimit)
                        ? Esp32p4SPIFlash_Erase4B(kFlashCmd_EraseSector4B, nAddr)
                        : (esp_rom_spiflash_erase_sector(nSectorNumber) == kEsp32p4SPIFlash_RomResult_OK);

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

        /* Never let one transaction cross the 16MB line, so that each is wholly
         * three byte addressed or wholly four */
        if (nAligned < k24BitAddressLimit && (nAddr + nWanted) > k24BitAddressLimit) {
            nWanted = k24BitAddressLimit - nAddr;
        }

        u32 nReadLen = (nSkip + nWanted + 3) & ~0x3u;

        if (nAligned >= k24BitAddressLimit) {
            bSuccess = Esp32p4SPIFlash_Read4B(nAligned, nChunk, nReadLen);
        } else {
            bSuccess = (esp_rom_spiflash_read(nAligned, nChunk, (s32)nReadLen) == kEsp32p4SPIFlash_RomResult_OK);
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
        /* The cache fetches with the read command the bootloader configured,
         * which carries a three byte address, so the mapped path cannot reach
         * the top of a part larger than 16MB.  Refuse rather than hand back the
         * contents of the address it would wrap to */
        if (nSrcAddr + nSize > k24BitAddressLimit) {
            LTLOG_YELLOWALERT("dec.unreachable", "Cannot decrypt at 0x%lx - beyond the mapped 16MB", LT_Pu32(nSrcAddr));
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
    } else if (nAddr >= k24BitAddressLimit) {
        bSuccess = Esp32p4SPIFlash_Program4B(nAddr, pWords, nSize);
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

        /* Never let one chunk cross the 16MB line, so that each is wholly three
         * byte addressed or wholly four */
        if (nAddr < k24BitAddressLimit && (nAddr + nCount) > k24BitAddressLimit) {
            nCount = k24BitAddressLimit - nAddr;
        }

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
        /* The ROM's encrypted write sends a three byte address and there is no
         * four byte address form of it to fall back on, since the encryption
         * happens inside the ROM routine */
        if (nAddr + nSize > k24BitAddressLimit) {
            LTLOG_YELLOWALERT("enc.unreachable", "Cannot encrypt at 0x%lx - beyond the addressable 16MB", LT_Pu32(nAddr));
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
 *  03-Nov-26   claudius    four byte address commands for offsets of 16MB and up, from the
 *                          esp32s3 driver, for parts with more than 16MB of flash
 */
