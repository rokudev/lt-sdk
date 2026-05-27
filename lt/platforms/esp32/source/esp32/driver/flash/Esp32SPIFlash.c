/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/Esp32SPIFlash.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Modified by Roku, Inc. Please see changelog below for more information.
//

#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTCore.h>

#include "Esp32_Registers.h"
#include "Esp32_Irq.h"
#include "Esp32_SoC.h"
#include "Esp32SPIFlash.h"
#include "FlashChipsCommands.h"
#include "Esp32SPIFlashCache.h"

DEFINE_LTLOG_SECTION("esp32.spiflash");

/*******************************************************************************
 * macros
*******************************************************************************/
#define BIT(n)                                      (1UL << (n))

/*******************************************************************************
 * consts
*******************************************************************************/
#define SPI1_R_QIO_ADDR_BITSLEN                     31
#define SPI1_R_QIO_DUMMY_CYCLELEN                   3
#define SPI1_R_DIO_ADDR_BITSLEN                     31
#define SPI1_R_FAST_ADDR_BITSLEN                    23
#define SPI1_R_FAST_DUMMY_CYCLELEN                  7
#define SPI1_R_SIO_ADDR_BITSLEN                     23

// the constants below are not from the ESP32 chip, but from the flash chip. If
// we end up supporting some other chip that doesn't support the same consts, then
// they will need to be abstracted into the chip specific structure
#define SPIFLASH_BYTES_LEN                      24
#define SPIFLASH_BUFF_BYTE_WRITE_NUM            32
#define SPIFLASH_BUFF_BYTE_READ_NUM             64
#define SPIFLASH_BP_MASK_ISSI                   (BIT(7) | BIT(5) | BIT(4) | BIT(3) | BIT(2))
#define SPIFLASH_W_SIO_ADDR_BITSLEN             23
#define SPIFLASH_QE                             BIT(9)

#define SPIFLASH_BUSY_FLAG                      BIT(0)
#define SPIFLASH_WRENABLE_FLAG                  BIT(1)
#define SPIFLASH_BP0                            BIT(2)
#define SPIFLASH_BP1                            BIT(3)
#define SPIFLASH_BP2                            BIT(4)
#define SPIFLASH_WR_PROTECT                     (SPIFLASH_BP0 | SPIFLASH_BP1 | SPIFLASH_BP2)

/*******************************************************************************
 * static variables
*******************************************************************************/
static const ESP32_MEM_REGION(DRAM) SPIFlashChipCommands * s_pChipCommands = NULL;

/*******************************************************************************
 * ROM
*******************************************************************************/
// ROM externs
extern u8 g_rom_spiflash_dummy_len_plus[];
// SPI flash chip in ROM
extern Esp32SPIFlash_Chip g_rom_spiflash_chip;
// ROM functions
extern int esp_rom_spiflash_read_user_cmd(u32 *status, u8 cmd);

/*******************************************************************************
 *
 * implementation
 *
*******************************************************************************/

static inline bool IsISSIChip(void) {
    return (((g_rom_spiflash_chip.deviceId >> 16) & 0xff) == 0x9D);
}

/*******************************************************************************
 * Initializes the driver by setting the chip commands structure
 * Returns a pointer to the flash chip
*******************************************************************************/
const Esp32SPIFlash_Chip * Esp32SPIFlash_Init(const char* pChipName) {
    s_pChipCommands = Esp32SPIFlash_GetChipCmds(pChipName);

#if 0
    esp_mspi_pin_init();

    /*
     * For Octal flash, it's hard to implement a read_id function in OPI mode for all vendors.
     *   So we have to read it here in SPI mode, before entering the OPI mode.
     */
    bootloader_flash_update_id();

    /*
     * This function initialise the Flash chip to the user-defined settings.
     *   In bootloader, we only init Flash (and MSPI) to a preliminary state, for being flexible to
     *   different chips. In this stage, we re-configure the Flash (and MSPI) to required configuration
     */
    spi_flash_init_chip_state();
#endif

    return (s_pChipCommands != NULL ? &g_rom_spiflash_chip : NULL);
}

/*******************************************************************************
 * Checks if the driver has been initialized
*******************************************************************************/
static bool Esp32SPIFlash_IsInited(void) {
    return (s_pChipCommands != NULL);
}

/*******************************************************************************
 * Runs the given command using the command register.
*******************************************************************************/
static bool ESP32_MEM_REGION(IRAM) Esp32SPIFlash_RunCmd(u32 cmd) {
    /* IRAM section begin - Code from here on must run from IRAM */
    ESP32_SPI1_REG(CMD) = cmd;
    volatile u16 timeout = LT_U16_MAX;
    while (ESP32_SPI1_REG(CMD) != 0 && --timeout != 0);

    return (ESP32_SPI1_REG(CMD) == 0);
    /* IRAM section continue - Code should keep running from IRAM until the flash access operation is completed */
}

/*******************************************************************************
 * Read the chip status
 * @note this function waits for the flash to finish any running operation and
 * returns the status after the busy flag is cleared
*******************************************************************************/
static void ESP32_MEM_REGION(IRAM) Esp32SPIFlash_ReadStatus(u32 * pStatus) {
    u32 statusValue = SPIFLASH_BUSY_FLAG;

    /* IRAM section begin - Code from here on must run from IRAM */

    if (g_rom_spiflash_dummy_len_plus[1] == 0) {
        volatile u16 timeout = LT_U16_MAX;
        while (SPIFLASH_BUSY_FLAG == (statusValue & SPIFLASH_BUSY_FLAG) && --timeout != 0) {
            ESP32_SPI1_REG(RD_STATUS) = 0;

            Esp32SPIFlash_RunCmd(s_pChipCommands->RDSR);

            statusValue = ESP32_SPI1_REG(RD_STATUS) & (g_rom_spiflash_chip.statusMask);
        }
    } else {
        volatile u16 timeout = LT_U16_MAX;
        while (SPIFLASH_BUSY_FLAG == (statusValue & SPIFLASH_BUSY_FLAG) && --timeout != 0) {
            esp_rom_spiflash_read_user_cmd(&statusValue, s_pChipCommands->RDSRC);
        }
    }

    /* IRAM section end - Code from here on can run from flash */

    *pStatus = statusValue;
}

/*******************************************************************************
 * Waits for any previous actions to be completed.
*******************************************************************************/
static void ESP32_MEM_REGION(IRAM) Esp32SPIFlash_WaitForIdle(void) {
    volatile u16 timeout = LT_U16_MAX;

    /* IRAM section begin - Code from here on must run from IRAM */

    // wait for spi control ready
    while (ESP32_SPI1_REG(EXT2) & ESP32_REG_MASK(SPI_EXT2, SPI_ST) && --timeout != 0) {
    }
    timeout = LT_U16_MAX;
    while (ESP32_SPI0_REG(EXT2) & ESP32_REG_MASK(SPI_EXT2, SPI_ST) && --timeout != 0) {
    }

    u32 status = 0;
    Esp32SPIFlash_ReadStatus(&status);

    /* IRAM section end - Code from here on can run from flash */
}

/*******************************************************************************
 * Enable write. This is required before performing any operation that writes
 * to the flash including the status
*******************************************************************************/
static bool ESP32_MEM_REGION(IRAM)  Esp32SPIFlash_EnableWrite(void) {
    /* IRAM section begin - Code from here on must run from IRAM */

    Esp32SPIFlash_WaitForIdle();

    // enable write
    Esp32SPIFlash_RunCmd(s_pChipCommands->WREN);     // enable write operation
    // make sure the flash is ready for writing
    volatile u16 timeout = LT_U16_MAX;
    u32 status  = 0;
    while (SPIFLASH_WRENABLE_FLAG != (status & SPIFLASH_WRENABLE_FLAG) && --timeout != 0) {
        Esp32SPIFlash_ReadStatus(&status);
    }
    Esp32SPIFlash_WaitForIdle();

    /* IRAM section end - Code from here on can run from flash */

    return (timeout != 0);
}

/*******************************************************************************
 * Read two-byte status
*******************************************************************************/
static void ESP32_MEM_REGION(IRAM) Esp32SPIFlash_ReadStatusHigh(u32 * pStatus) {
    /* IRAM section begin - Code from here on must run from IRAM */
    Esp32SPIFlash_WaitForIdle();
    esp_rom_spiflash_read_user_cmd(pStatus, s_pChipCommands->RDSRCH);
    *pStatus = *pStatus << 8;
    /* IRAM section end - Code from here on can run from flash */
}

/*******************************************************************************
 * Write the given status to the chip
*******************************************************************************/
static void ESP32_MEM_REGION(IRAM) Esp32SPIFlash_WriteStatus(u32 statusValue) {
    /* IRAM section begin - Code from here on must run from IRAM */
    Esp32SPIFlash_WaitForIdle();
    ESP32_SPI1_REG(RD_STATUS) = statusValue;
    Esp32SPIFlash_RunCmd(s_pChipCommands->WRSR);
    Esp32SPIFlash_WaitForIdle();
    /* IRAM section end - Code from here on can run from flash */
}

/*******************************************************************************
 * Unlock the flash
 * The flash chips support protecting a range of blocks or predefined blocks, but
 * does not support specific individual blocks
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32SPIFlash_WriteUnprotect(void) {
    u32 status      = 0;
    u32 newStatus  = 0;

    if (!Esp32SPIFlash_IsInited()) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32SPIFlashCache_DisableCache();

    Esp32SPIFlash_WaitForIdle();

    if (IsISSIChip()) {
        Esp32SPIFlash_ReadStatus(&status);

        newStatus = status & (~SPIFLASH_BP_MASK_ISSI);
        ESP32_SPI1_REG(CTRL) &= ~ESP32_REG_MASK(SPI_CTRL, TWO_BYTES_STATUS_EN);
    } else {
        Esp32SPIFlash_ReadStatusHigh(&status);

        newStatus = status & SPIFLASH_QE;
        ESP32_SPI1_REG(CTRL) |= ESP32_REG_MASK(SPI_CTRL, TWO_BYTES_STATUS_EN);
    }

    // need to enable write to write to the status register
    bool bSuccess = Esp32SPIFlash_EnableWrite();
    if (bSuccess) {
        // set unlocked status
        Esp32SPIFlash_WriteStatus(newStatus);
        Esp32SPIFlash_WaitForIdle();
    }

    Esp32SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * Returns whether the flash is locked or unlock
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32SPIFlash_IsLocked(void) {
    u32 status = 0;

    if (!Esp32SPIFlash_IsInited()) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32SPIFlashCache_DisableCache();

    if (IsISSIChip()) {
        Esp32SPIFlash_ReadStatus(&status);
    } else {
        Esp32SPIFlash_ReadStatusHigh(&status);
    }

    Esp32SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return (status & SPIFLASH_WR_PROTECT);
}

/*******************************************************************************
 * Write to flash
  * Notes:
 *   addr must be 4-byte aligned
*******************************************************************************/
static bool ESP32_MEM_REGION(IRAM) Esp32SPIFlash_ProgramPage(u32 addr, u32 * pSrcAddr, u32 len) {
    // check 4byte alignment
    if (len & 0x3) {
        return false;
    }

    // check if write in one page
    if ((g_rom_spiflash_chip.pageSize) < ((addr % (g_rom_spiflash_chip.pageSize)) + len)) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32SPIFlashCache_DisableCache();

    // disable dummy writes as there's nothing to read
    ESP32_SPI1_REG(USER) &= ~ESP32_REG_MASK(SPI_USER, DUMMY);
    ESP32_SPI1_REG(USER1) = (ESP32_SPI1_REG(USER1) & ~ESP32_REG_MASK(SPI_USER1, ADDR_BITLEN)) |
                                     (SPIFLASH_W_SIO_ADDR_BITSLEN << ESP32_REG_SHIFT(SPI_USER1, ADDR_BITLEN));

    s32 sLen = len;
    bool bSuccess = true;
    while (sLen > 0 && bSuccess) {
        bSuccess = Esp32SPIFlash_EnableWrite();
        if (bSuccess) {
            if (sLen >= SPIFLASH_BUFF_BYTE_WRITE_NUM) {

                ESP32_SPI1_REG(ADDR) = (addr & 0xffffff) | ( SPIFLASH_BUFF_BYTE_WRITE_NUM << SPIFLASH_BYTES_LEN); // 32 byte a block

                for (u32 i = 0; i < (SPIFLASH_BUFF_BYTE_WRITE_NUM >> 2); ++i) {
                    ESP32_SPI_W_REG(SPI1, i) = *pSrcAddr++;
                }
                sLen -= 32;
                addr += 32;
            } else {
                ESP32_SPI1_REG(ADDR) = (addr & 0xffffff) | (sLen << SPIFLASH_BYTES_LEN);

                u8 remaining = ((sLen & 0x3) == 0) ? (sLen >> 2) : (sLen >> 2) + 1;
                for (u8 i = 0; i < remaining; ++i, ++pSrcAddr) {
                    ESP32_SPI_W_REG(SPI1, i) = *pSrcAddr;
                }
                sLen = 0;
            }
            Esp32SPIFlash_RunCmd(s_pChipCommands->PP);
            Esp32SPIFlash_WaitForIdle();
        }
    }

    Esp32SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * Lock the flash
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32SPIFlash_WriteProtect(void) {
    if (!Esp32SPIFlash_IsInited()) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32SPIFlashCache_DisableCache();

    u32 status = 0;
    Esp32SPIFlash_ReadStatusHigh(&status);
    // enable 2 byte status writing
    ESP32_SPI1_REG(CTRL) |= ESP32_REG_MASK(SPI_CTRL, TWO_BYTES_STATUS_EN);

    bool bSuccess = Esp32SPIFlash_EnableWrite();
    if (bSuccess) {
        Esp32SPIFlash_WriteStatus(status | SPIFLASH_WR_PROTECT);
    }

    Esp32SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * erase the entire flash
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32SPIFlash_EraseChip(void) {
    if (!Esp32SPIFlash_IsInited()) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32SPIFlashCache_DisableCache();

    bool bSuccess = Esp32SPIFlash_EnableWrite();

    if (bSuccess) {
        Esp32SPIFlash_RunCmd(s_pChipCommands->CE);
        Esp32SPIFlash_WaitForIdle();
    }

    Esp32SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * erase a block
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32SPIFlash_EraseBlock(u32 blockNum) {
    if (!Esp32SPIFlash_IsInited()) {
        return false;
    }

    if (blockNum >= ((g_rom_spiflash_chip.chipSize) / (g_rom_spiflash_chip.blockSize))) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32SPIFlashCache_DisableCache();

    bool bSuccess = Esp32SPIFlash_EnableWrite();

    if (bSuccess) {
        // disable dummy writes as there's nothing to read
        ESP32_SPI1_REG(USER) &= ~ESP32_REG_MASK(SPI_USER, DUMMY);
        ESP32_SPI1_REG(USER1) = (ESP32_SPI1_REG(USER1) & ~ESP32_REG_MASK(SPI_USER1, ADDR_BITLEN)) |
                                        (SPIFLASH_W_SIO_ADDR_BITSLEN << ESP32_REG_SHIFT(SPI_USER1, ADDR_BITLEN));


        u32 addr = blockNum * g_rom_spiflash_chip.blockSize;
        ESP32_SPI1_REG(ADDR) = addr & 0xffffff;
        Esp32SPIFlash_RunCmd(s_pChipCommands->BE);
        Esp32SPIFlash_WaitForIdle();
    }
    Esp32SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return true;
}

/*******************************************************************************
 * erase a sector
*******************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32SPIFlash_EraseSector(u32 sectorNum) {
    if (!Esp32SPIFlash_IsInited()) {
        return false;
    }

    if (sectorNum >= ((g_rom_spiflash_chip.chipSize) / (g_rom_spiflash_chip.sectorSize))) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32SPIFlashCache_DisableCache();

    bool bSuccess = Esp32SPIFlash_EnableWrite();
    if (bSuccess) {
        // disable dummy writes as there's nothing to read
        ESP32_SPI1_REG(USER) &= ~ESP32_REG_MASK(SPI_USER, DUMMY);
        ESP32_SPI1_REG(USER1) = (ESP32_SPI1_REG(USER1) & ~ESP32_REG_MASK(SPI_USER1, ADDR_BITLEN)) |
                                        (SPIFLASH_W_SIO_ADDR_BITSLEN << ESP32_REG_SHIFT(SPI_USER1, ADDR_BITLEN));

        u32 addr = sectorNum * g_rom_spiflash_chip.sectorSize;
        ESP32_SPI1_REG(ADDR) = addr & 0xffffff;
        Esp32SPIFlash_RunCmd(s_pChipCommands->SE);
        Esp32SPIFlash_WaitForIdle();
    }

    Esp32SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * write to flash. Chip must be unlocked and erased first
 * Notes:
 *   target must be 4-byte aligned
*******************************************************************************/
static bool Esp32SPIFlash_WritePriv(u32 target, const u8 * pBuffer, u32 len) {
    if (!Esp32SPIFlash_IsInited()) {
        return false;
    }

    // check program size
    if ((target + len) > (g_rom_spiflash_chip.chipSize)) {
        return false;
    }

    u32 pageSize = g_rom_spiflash_chip.pageSize;
    u32 progLen = pageSize - (target % pageSize);
    if (len < progLen) {
        if (!Esp32SPIFlash_ProgramPage(target, (u32 *)pBuffer, len)) {
            return false;
        }
    } else {
        if (!Esp32SPIFlash_ProgramPage(target, (u32 *)pBuffer, progLen)) {
            return false;
        }

        // whole page program
        u32 progNum = (len - progLen) / pageSize;
        for (u32 i = 0; i < progNum; i++) {
            if (!Esp32SPIFlash_ProgramPage(target + progLen, (u32 *)pBuffer + (progLen >> 2), pageSize)) {
                return false;
            }
            progLen += pageSize;
        }

        if (!Esp32SPIFlash_ProgramPage(target + progLen, (u32 *)pBuffer + (progLen >> 2), len - progLen)) {
            return false;
        }
    }
    return true;
}

/*******************************************************************************
 * Writes encrypted data to flash
 *   pData must be 32-byte aligned
 *   len must be 16-byte aligned
*******************************************************************************/
static bool ESP32_MEM_REGION(IRAM) Esp32SPIFlash_WriteEncrypted(u32 addr, const u8 * pData, u32 len) {
    if (!Esp32SPIFlash_IsInited()) {
        return false;
    }

    // check addr and len for alignment
    if ((addr & 0x1f) || (len & 0xf)) {
        return false;
    }

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32SPIFlashCache_DisableCache();

    // disable dummy writes as there's nothing to read
    ESP32_SPI1_REG(USER) &= ~ESP32_REG_MASK(SPI_USER, DUMMY);
    ESP32_SPI1_REG(USER1) = (ESP32_SPI1_REG(USER1) & ~ESP32_REG_MASK(SPI_USER1, ADDR_BITLEN)) |
                                     (SPIFLASH_W_SIO_ADDR_BITSLEN << ESP32_REG_SHIFT(SPI_USER1, ADDR_BITLEN));

    // enable encryption
    ESP32_REG(DPORT_SLAVE_SPI_CONFIG) |= ESP32_REG_MASK(DPORT_SPI_ENCRYPT, ENABLE);

    s32 lenLeft = len;
    bool bSuccess = true;
    for (u32 i = 0; lenLeft > 0 && bSuccess;) {
        // prepare encryption
        ESP32_REG(FLASH_ENCRYPT_ADDRESS) = addr;
        for (u8 x = 0; x < (SPIFLASH_BUFF_BYTE_WRITE_NUM >> 2) && i < len; ++x, i += 4) {
            ESP32_REG_ARRAY_VALUE(FLASH_ENCRYPT_BUFFER_0, x) = *(u32 *)&pData[i];
        }
        // start the encryption
        ESP32_REG(FLASH_ENCRYPT_START) = ESP32_REG_VAL(FLASH_ENCRYPT, START);
        // wait until done
        volatile u16 timeout = LT_U16_MAX;
        while (ESP32_REG(FLASH_ENCRYPT_DONE) == 0 && --timeout != 0);
        bSuccess = (timeout != 0);
        if (bSuccess) {
            // the data written to flash here is the encrypted data and not what's in pData
            bSuccess = Esp32SPIFlash_ProgramPage(addr, (u32 *)pData, LT_MIN((u32)lenLeft, (u32)SPIFLASH_BUFF_BYTE_WRITE_NUM));
            addr    += SPIFLASH_BUFF_BYTE_WRITE_NUM;
            lenLeft -= SPIFLASH_BUFF_BYTE_WRITE_NUM;
        }
    }

    // disable encryption
    ESP32_REG(DPORT_SLAVE_SPI_CONFIG) &= ~ESP32_REG_MASK(DPORT_SPI_ENCRYPT, ENABLE);

    Esp32SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return bSuccess;
}

/*******************************************************************************
 * Sets up the SPI registers for a read operation.
 * @note this function must run from IRAM because the cache needs to be disabled
*******************************************************************************/
static void ESP32_MEM_REGION(IRAM) Esp32SPIFlash_ReadDirect(u32 addr, u8 * pBuffer, u32 len) {

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32SPIFlashCache_DisableCache();

    // QIO or SIO, non-QIO regard as SIO
    u32 modeBits        = ESP32_SPI1_REG(CTRL);
    bool qIOMode        = modeBits & ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_QIO);
    bool quadMode       = modeBits & ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_QUAD);
    bool dIOMode        = modeBits & ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_DIO);
    bool fastReadMode   = modeBits & ESP32_REG_MASK(SPI_CTRL, SPI_FASTRD_MODE);
    bool dualMode       = modeBits & ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_DUAL);
    if (qIOMode && fastReadMode) {
        ESP32_SPI1_REG(USER) &= ~ESP32_REG_MASK(SPI_USER, MOSI);
        ESP32_SPI1_REG(USER) |= ESP32_REG_MASK(SPI_USER, MISO) | ESP32_REG_MASK(SPI_USER, DUMMY) | ESP32_REG_MASK(SPI_USER, ADDR);
        ESP32_SPI1_REG(USER1) = (ESP32_SPI1_REG(USER1) & ~ESP32_REG_MASK(SPI_USER1, ADDR_BITLEN)) |
                                         (SPI1_R_QIO_ADDR_BITSLEN << ESP32_REG_SHIFT(SPI_USER1, ADDR_BITLEN));
        ESP32_SPI1_REG(USER1) = (ESP32_SPI1_REG(USER1) & ~ESP32_REG_MASK(SPI_USER1, DUMMY_CYCLELEN)) |
                                         ((SPI1_R_QIO_DUMMY_CYCLELEN + g_rom_spiflash_dummy_len_plus[1]) << ESP32_REG_SHIFT(SPI_USER1, DUMMY_CYCLELEN));
        ESP32_SPI1_REG(USER2) = (0x7 << ESP32_REG_SHIFT(SPI_USER2, COMMAND_BITLEN)) | s_pChipCommands->FRQ;
    } else if (fastReadMode) {
        ESP32_SPI1_REG(USER) &= ~ESP32_REG_MASK(SPI_USER, MOSI);
        ESP32_SPI1_REG(USER) |=  ESP32_REG_MASK(SPI_USER, MISO) | ESP32_REG_MASK(SPI_USER, ADDR);
        if (dIOMode) {
            if (g_rom_spiflash_dummy_len_plus[1] == 0) {
                ESP32_SPI1_REG(USER) &= ~ESP32_REG_MASK(SPI_USER, DUMMY);
                ESP32_SPI1_REG(USER1) = (ESP32_SPI1_REG(USER1) & ~ESP32_REG_MASK(SPI_USER1, ADDR_BITLEN)) |
                                                 (SPI1_R_DIO_ADDR_BITSLEN << ESP32_REG_SHIFT(SPI_USER1, ADDR_BITLEN));
                ESP32_SPI1_REG(USER2) = (0x7 << ESP32_REG_SHIFT(SPI_USER2, COMMAND_BITLEN)) | s_pChipCommands->FRD;
            } else {
                ESP32_SPI1_REG(USER) |= ESP32_REG_MASK(SPI_USER, DUMMY);
                ESP32_SPI1_REG(USER1) = (ESP32_SPI1_REG(USER1) & ~ESP32_REG_MASK(SPI_USER1, ADDR_BITLEN)) |
                                                 (SPI1_R_DIO_ADDR_BITSLEN << ESP32_REG_SHIFT(SPI_USER1, ADDR_BITLEN));
                ESP32_SPI1_REG(USER1) = (ESP32_SPI1_REG(USER1) & ~ESP32_REG_MASK(SPI_USER1, DUMMY_CYCLELEN)) |
                                                 ((g_rom_spiflash_dummy_len_plus[1] - 1) << ESP32_REG_SHIFT(SPI_USER1, DUMMY_CYCLELEN));
                ESP32_SPI1_REG(USER2) = (ESP32_SPI1_REG(USER2) & ~ESP32_REG_MASK(SPI_USER2, COMMAND_VALUE)) |
                                                 (0xBB << ESP32_REG_SHIFT(SPI_USER2, COMMAND_VALUE));
            }
        } else {
            if (quadMode) {
                ESP32_SPI1_REG(USER2) = (0x7 << ESP32_REG_SHIFT(SPI_USER2, COMMAND_BITLEN)) | s_pChipCommands->FRQO;
            } else if (dualMode) {
                ESP32_SPI1_REG(USER2) = (0x7 << ESP32_REG_SHIFT(SPI_USER2, COMMAND_BITLEN)) | s_pChipCommands->FRDO;
            } else {
                ESP32_SPI1_REG(USER2) = (0x7 << ESP32_REG_SHIFT(SPI_USER2, COMMAND_BITLEN)) | s_pChipCommands->FR;
            }
            ESP32_SPI1_REG(USER) |= ESP32_REG_MASK(SPI_USER, DUMMY);

            ESP32_SPI1_REG(USER1) = (ESP32_SPI1_REG(USER1) & ~ESP32_REG_MASK(SPI_USER1, ADDR_BITLEN)) |
                                                (SPI1_R_FAST_ADDR_BITSLEN << ESP32_REG_SHIFT(SPI_USER1, ADDR_BITLEN));
            ESP32_SPI1_REG(USER1) = (ESP32_SPI1_REG(USER1) & ~ESP32_REG_MASK(SPI_USER1, DUMMY_CYCLELEN)) |
                                                 ((SPI1_R_FAST_DUMMY_CYCLELEN + g_rom_spiflash_dummy_len_plus[1]) << ESP32_REG_SHIFT(SPI_USER1, DUMMY_CYCLELEN));
        }
    } else {
        ESP32_SPI1_REG(USER) &= ~ESP32_REG_MASK(SPI_USER, MOSI);
        if (g_rom_spiflash_dummy_len_plus[1] == 0) {
            ESP32_SPI1_REG(USER) &= ~ESP32_REG_MASK(SPI_USER, DUMMY);
        } else {
            ESP32_SPI1_REG(USER) |= ESP32_REG_MASK(SPI_USER, DUMMY);
            ESP32_SPI1_REG(USER1) = (ESP32_SPI1_REG(USER1) & ~ESP32_REG_MASK(SPI_USER1, DUMMY_CYCLELEN)) |
                                                 ((g_rom_spiflash_dummy_len_plus[1] - 1) << ESP32_REG_SHIFT(SPI_USER1, DUMMY_CYCLELEN));
        }
        ESP32_SPI1_REG(USER) |=  ESP32_REG_MASK(SPI_USER, MISO) | ESP32_REG_MASK(SPI_USER, ADDR);

        ESP32_SPI1_REG(USER1) = (ESP32_SPI1_REG(USER1) & ~ESP32_REG_MASK(SPI_USER1, ADDR_BITLEN)) |
                                                 (SPI1_R_SIO_ADDR_BITSLEN << ESP32_REG_SHIFT(SPI_USER1, ADDR_BITLEN));

        ESP32_SPI1_REG(USER2) = (0x7 << ESP32_REG_SHIFT(SPI_USER2, COMMAND_BITLEN)) | s_pChipCommands->RD;
    }

    // now that the SPI bus is configured, read the data
    s32 sLen = len;
    while (sLen > 0) {
        ESP32_SPI1_REG(MISO_DLEN) = ((SPIFLASH_BUFF_BYTE_READ_NUM << 3) - 1) << ESP32_REG_SHIFT(SPI_MISO_DLEN, DBITLEN);
        ESP32_SPI1_REG(ADDR) = addr << 8;
        Esp32SPIFlash_RunCmd(s_pChipCommands->USR);

        for (u8 i = 0; i < (SPIFLASH_BUFF_BYTE_READ_NUM >> 2) && sLen > 0; ++i, pBuffer += 4) {
            u32 data = ESP32_SPI_W_REG(SPI1, i);
            u8 rem = LT_MIN((s32)sLen, (s32)sizeof(data));
            for (u32 n = 0; n < rem; ++n) {
                pBuffer[n] = ((u8 *)&data)[n];
            }
            sLen -= rem;
        }
        addr += SPIFLASH_BUFF_BYTE_READ_NUM;
    }

    Esp32SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);
}

/****************************************************************************
 * Returns whether flash encryption is enabled or not
 ****************************************************************************/
bool Esp32SPIFlash_IsEncryptionEnabled(void) {
    u32 crypt_cnt = ESP32_REG(EFUSE_BLK0_RDATA0) >> ESP32_REG_SHIFT(EFUSE, FLASH_CRYPT_CNT);
    crypt_cnt &= ESP32_REG_MASK(EFUSE, FLASH_CRYPT_CNT);
    // if crypt_cnt has an odd number of bits (odd parity), then encryption is enabled
    return __builtin_parity(crypt_cnt);
}

/****************************************************************************
 * Reads from the flash through the cache so data is decrypted if needed
 * (no alignment needed)
 ****************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32SPIFlash_Read(u32 src, u8 * pBuffer, u32 size, bool bDecrypt) {
    if (bDecrypt && !Esp32SPIFlash_IsEncryptionEnabled()) {
        // can't decrypt
        return false;
    }

    // if no decryption is wanted and encryption is enabled, then bypass the cache and
    // read the data directly from the flash
    bool bReadDirect = !bDecrypt && Esp32SPIFlash_IsEncryptionEnabled();

    if (bReadDirect) {
        Esp32SPIFlash_ReadDirect(src, pBuffer, size);
    } else {
        Esp32SPIFlash_MapInfo map;
        map.srcAddr     = src;
        map.size        = size;

        u32 mask = Esp32DisableInterrupts();
        /* IRAM section begin - Code from here on must run from IRAM */
        u32 state = Esp32SPIFlashCache_DisableCache();

        // map the flash address so data is decrypted if it's encrypted
        Esp32SPIFlashCache_Mmap(&map);

        Esp32SPIFlashCache_EnableCache(state);
        /* IRAM section end - Code from here on can run from flash */
        Esp32EnableInterrupts(mask);

        if (map.ptr == NULL) {
            LTLOG_YELLOWALERT("fail.read.map", "Failed to map the flash");
        } else {
            // cache must be enabled before calling lt_memcpy()
            lt_memcpy(pBuffer, map.ptr, size);

            mask = Esp32DisableInterrupts();
            /* IRAM section begin - Code from here on must run from IRAM */
            state = Esp32SPIFlashCache_DisableCache();

            Esp32SPIFlashCache_Ummap(&map);

            Esp32SPIFlashCache_EnableCache(state);
            /* IRAM section end - Code from here on can run from flash */
            Esp32EnableInterrupts(mask);
        }
    }

    return true;
}

/****************************************************************************
 * Write data to Flash (no alignment needed)
 ****************************************************************************/
bool ESP32_MEM_REGION(IRAM) Esp32SPIFlash_Write(u32 addr, const u8 * pBuffer, u32 size, bool bEncrypt) {
    if (bEncrypt && !Esp32SPIFlash_IsEncryptionEnabled()) {
        LTLOG_YELLOWALERT("enc.disabled", "bEncrypt is set, but encryption is not enabled");
        return false;
    }

    if (bEncrypt && ((addr & 0x1f) || (size & 0xf))) {
        LTLOG_YELLOWALERT("invalid.enc.align", "Encryption data size must be 16-byte aligned and address must be 32-byte aligned");
        return false;
    }

    bool bSuccess = true;
    if (addr & 0x3) {
        // address is not aligned, read prev aligned word
        u32 wordBuff        = LT_U32_MAX;
        u32 nonAlignedAddr  = addr;
        // align the address
        addr &= ~0x3;
        // read an aligned word
        Esp32SPIFlash_Read(addr, (u8 *)&wordBuff, sizeof(wordBuff), bEncrypt);
        // offset into the read word to write non-aligned data to
        u8 wordOffset = nonAlignedAddr - addr;
        // size to write into the read word
        u32 rem = sizeof(wordBuff) - wordOffset;
        u8 * wordPtr = (u8 *)&wordBuff;
        // copy at the offset and size
        lt_memcpy(wordPtr + wordOffset, pBuffer, LT_MIN(rem, size));
        if (bEncrypt) {
            // shouldn't get here
            bSuccess = false;
            LT_ASSERT(false);
        } else {
            bSuccess = Esp32SPIFlash_WritePriv(addr, (u8 *)&wordBuff, sizeof(wordBuff));
        }
        size    -= LT_MIN(rem, size);
        addr    += sizeof(wordBuff);
        pBuffer += rem;
    }
    if (bSuccess && size > 0) {
        u32 lenWords = size & ~0x3;
        if (lenWords > 0) {
            if (bEncrypt) {
                bSuccess = Esp32SPIFlash_WriteEncrypted(addr, pBuffer, lenWords);
            } else {
                bSuccess = Esp32SPIFlash_WritePriv(addr, pBuffer, lenWords);
            }
            pBuffer += lenWords;
            addr    += lenWords;
            size     = (size < lenWords ? 0 : size - lenWords);
        }

        // if there is some remaining, we need to write the last word
        if (size > 0 && bSuccess) {
            u32 wordBuff = 0;
            Esp32SPIFlash_Read(addr, (u8 *)&wordBuff, sizeof(wordBuff), bEncrypt);
            lt_memcpy(&wordBuff, pBuffer, size);
            if (bEncrypt) {
                // shouldn't get here
                bSuccess = false;
                LT_ASSERT(false);
            } else {
                bSuccess = Esp32SPIFlash_WritePriv(addr, (u8 *)&wordBuff, sizeof(wordBuff));
            }
        }
    }

    return bSuccess;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  25-May-22   vitellius   created
 *  25-May-22   vitellius   Functions were modified and fixed by Roku LT
 */
