/******************************************************************************
 * Esp32_PSRAM.c                                                      ESP32 BSP
 *
 * Detection, configuration and memory mapping of external SPI PSRAM.
 *
 * This is a hand-written, LT-native equivalent of IDF's
 * components/esp_hw_support/port/esp32/spiram_psram.c.  It is not a vendored
 * copy: that file pulls in most of IDF's soc/, hal/ and driver/ trees, and the
 * app-side include path here carries none of them - only the seven
 * hand-written Esp32_*.h headers.  The register sequences below therefore
 * follow IDF's closely enough to be checked against it line by line, but the
 * plumbing is LT's.
 *
 * How PSRAM works on this part, in short.  The PSRAM die sits on the *same*
 * SPI bus as the flash, distinguished only by chip select: flash on CS0, PSRAM
 * on CS1.  Two peripherals see that bus.  SPI0 is the cache controller - it
 * issues reads and writes on the CPU's behalf whenever an access lands in a
 * mapped window, and is never programmed a transaction at a time.  SPI1 is a
 * conventional user-mode controller sharing the same pads.  Bringing PSRAM up
 * means talking to the part over SPI1 to identify it and switch it into quad
 * mode, then programming SPI0 with the opcodes and timing to use for cache
 * traffic, then pointing the external-RAM MMU at it.
 *
 * Scope of this implementation, and why:
 *
 *   - Only the D0WDQ6 and D0WDQ5 packages are supported.  Every other package
 *     (D2WD, PICO-D2/D4, PICO-V3-02, D0WDR2-V3) needs the VDDSDIO rail voltage
 *     checked or changed before the part is safe to drive, and LT has no
 *     rtc_vddsdio_get_config() equivalent.  The package table below is data,
 *     so adding one later is a small edit.  D0WDQ6/D0WDQ5 covers the WROVER
 *     modules and the ESP32-CAM, which is what LT runs on today.
 *
 *   - The cache mode is fixed at flash 80MHz / PSRAM 40MHz.  LT masters its
 *     images with `esptool.py elf2image --flash_freq 80m` (see
 *     Esp32_LTBootloader.mk and Esp32_MasterFirmwareImage.mk), so the 40M/40M
 *     mode would quietly halve flash throughput.  80M/80M is not offered
 *     because the obsolete 32Mbit v0 parts cannot do it without claiming HSPI
 *     or VSPI outright.
 *
 *   - 2T mode and bank switching (himem) are not implemented.  Both trade
 *     capacity or complexity for cases LT does not have.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 ******************************************************************************/

#include <lt/LT.h>

#include "Esp32_SoC.h"
#include "Esp32_Registers.h"
#include "Esp32_GPIO.h"
#include "Esp32_PSRAM.h"

/******************************************************************************
 * ROM entry points
 *
 * Declared here rather than in a header, following the pattern Esp32_SoC.h
 * uses for esp_rom_printf().  All of these are PROVIDEd by
 * mastering/ld/esp32/rom/esp32.rom.ld and esp32.rom.api.ld.
 *****************************************************************************/

/* Programs the external-RAM MMU: maps `nNumPages` pages of `nPageSizeKB` KB
   each, from physical `nPhysAddr` to virtual `nVirtAddr`, for CPU `nCpuNo`.
   Returns 0 on success.

   IDF ships a replacement for this that adds DPORT access workarounds, SPI
   cache guards and a fix for vaddr offsets beyond 2MB.  None of that applies
   here: we map from the very start of the window, the guards do not exist yet
   at BSP init time (IDF falls back to this same ROM routine in that case, see
   cache_sram_mmu_set() in esp_hw_support/port/esp32/cache_sram_mmu.c), and the
   ROM routine disables the cache internally rather than relying on a caller
   supplied Cache_Read_Disable()/Cache_Flush()/Cache_Read_Enable() bracket. */
u32 cache_sram_mmu_set_rom(int nCpuNo, int nPid, u32 nVirtAddr, u32 nPhysAddr,
                           int nPageSizeKB, int nNumPages);

/* Sets the SPI clock divider for one port.  Port 0 is the cache, port 1 the
   user-mode flash controller. */
int esp_rom_spiflash_config_clk(u8 nFreqDiv, u8 nSpiPort);

/* GPIO matrix routing. */
void esp_rom_gpio_connect_out_signal(u32 nPin, u32 nSignal, bool bInvertOut, bool bInvertEnable);
void esp_rom_gpio_connect_in_signal(u32 nPin, u32 nSignal, bool bInvertIn);

/* Returns the burned SPI pad configuration, or one of the two "default"
   sentinels below when the pads have not been customised. */
u32 esp_rom_efuse_get_flash_gpio_info(void);

/* Extra dummy cycles the ROM flash routines insert, indexed by SPI port.  The
   ROM reads this whenever it drives flash, so it has to be kept consistent
   with the clock dividers we set. */
extern u8 g_rom_spiflash_dummy_len_plus[];

/* The ROM's flash chip descriptor.  Only the first field is used here; the
   remaining fields are named so the layout is checkable against the ROM's
   esp_rom_spiflash_chip_t. */
typedef struct Esp32_RomFlashChip {
    u32 nDeviceId;
    u32 nChipSize;
    u32 nBlockSize;
    u32 nSectorSize;
    u32 nPageSize;
    u32 nStatusMask;
} Esp32_RomFlashChip;
extern Esp32_RomFlashChip g_rom_flashchip;

/******************************************************************************
 * constants
 *****************************************************************************/

/* PSRAM opcodes */
enum {
    kPSRAM_Cmd_FastReadQuad             = 0xeb,
    kPSRAM_Cmd_FastReadQuadDummy        = 0x05,     /* dummy cycles for 0xeb   */
    kPSRAM_Cmd_QuadWrite                = 0x38,
    kPSRAM_Cmd_EnterQMode               = 0x35,
    kPSRAM_Cmd_ExitQMode                = 0xf5,
    kPSRAM_Cmd_DeviceId                 = 0x9f,
};

/* Device ID decode.  The 64 bit ID reads back little endian; everything of
   interest lives in the low word.  KGD ("known good die") identifies the part
   as an Espressif PSRAM at all, and the top three bits of the EID give the
   capacity. */
enum {
    kPSRAM_Id_KGD_S                     = 8,
    kPSRAM_Id_KGD_M                     = 0xff,
    kPSRAM_Id_KGD_Valid                 = 0x5d,
    kPSRAM_Id_EID_S                     = 16,
    kPSRAM_Id_EID_M                     = 0xff,
    kPSRAM_Id_EIDSize_S                 = 5,
    kPSRAM_Id_EIDSize_M                 = 0x07,

    kPSRAM_EIDSize_16Mbit               = 0,
    kPSRAM_EIDSize_32Mbit               = 1,
    kPSRAM_EIDSize_64Mbit               = 2,

    /* The original 32Mbit ESP-PSRAM32 - EID 0x20 - needs two extra clock
       cycles after CS deasserts, which is what the DCLK clock mode provides. */
    kPSRAM_EID_32MbitVer0               = 0x20,
    /* Trial 64Mbit parts report a size field that does not decode. */
    kPSRAM_EID_64MbitTrial              = 0x26,
};

#define PSRAM_KGD(id)                   (((id) >> kPSRAM_Id_KGD_S) & kPSRAM_Id_KGD_M)
#define PSRAM_EID(id)                   (((id) >> kPSRAM_Id_EID_S) & kPSRAM_Id_EID_M)
#define PSRAM_SIZE_ID(id)               ((PSRAM_EID(id) >> kPSRAM_Id_EIDSize_S) & kPSRAM_Id_EIDSize_M)
#define PSRAM_IS_VALID(id)              (PSRAM_KGD(id) == kPSRAM_Id_KGD_Valid)
#define PSRAM_IS_32MBIT_VER0(id)        (PSRAM_EID(id) == kPSRAM_EID_32MbitVer0)
#define PSRAM_IS_64MBIT_TRIAL(id)       (PSRAM_EID(id) == kPSRAM_EID_64MbitTrial)

/* GPIO matrix signal indices, from table 17 of the reference manual */
enum {
    kSignal_SPICLK_OUT                  = 0,
    kSignal_SPIQ_IN                     = 1,
    kSignal_SPIQ_OUT                    = 1,
    kSignal_SPID_IN                     = 2,
    kSignal_SPID_OUT                    = 2,
    kSignal_SPIHD_IN                    = 3,
    kSignal_SPIHD_OUT                   = 3,
    kSignal_SPIWP_IN                    = 4,
    kSignal_SPIWP_OUT                   = 4,
    kSignal_SPICS0_OUT                  = 5,
    kSignal_SPICS1_OUT                  = 6,
    /* Two spare matrix signals used as a delay line, see below */
    kSignal_FUNC224_IN                  = 224,
    kSignal_FUNC225_IN                  = 225,
    /* Routes the pin's own GPIO output register rather than a peripheral */
    kSignal_GPIO_OUT                    = 256,
};

/* Pin assignments.  PSRAM shares every pad with the flash except CS and CLK,
   so these numbers pin down the flash wiring too. */
enum {
    kPin_FlashClk_IOMux                 = 6,        /* default SPI pads        */
    kPin_FlashCs_IOMux                  = 11,
    kPin_PSRAM_Q_SD0                    = 7,
    kPin_PSRAM_D_SD1                    = 8,
    kPin_PSRAM_HD_SD2                   = 9,
    kPin_PSRAM_WP_SD3                   = 10,

    kPin_FlashClk_HSPI                  = 14,       /* HSPI pads               */
    kPin_FlashCs_HSPI                   = 15,
    kPin_PSRAM_Q_SD0_HSPI               = 12,
    kPin_PSRAM_D_SD1_HSPI               = 13,
    kPin_PSRAM_HD_SD2_HSPI              = 4,
    kPin_PSRAM_WP_SD3_HSPI              = 2,

    /* D0WD PSRAM clock and chip select.  This is the WROVER / ESP32-CAM
       wiring; other packages differ, and are rejected above. */
    kPin_PSRAMClk_D0WD                  = 17,
    kPin_PSRAMCs_D0WD                   = 16,

    /* Two pins that exist in silicon but are not bonded out in any package.
       Used as scratch to build a clock delay line. */
    kPin_Internal28                     = 28,
    kPin_Internal29                     = 29,
};

/* IO_MUX pad functions used here */
enum {
    kIOMuxFunc_SD_CLK_SPICLK            = 1,        /* pad 6's native SPI clock */
    kIOMuxFunc_GPIO                     = 2,
};

/* Sentinels returned by esp_rom_efuse_get_flash_gpio_info() */
enum {
    kEfuseSpiConfig_DefaultSPI          = 0,
    kEfuseSpiConfig_DefaultHSPI         = 1,
};

#define EFUSE_SPICONFIG_RET_SPICLK(r)   ((r) & 0x3f)
#define EFUSE_SPICONFIG_RET_SPIQ(r)     (((r) >> 6) & 0x3f)
#define EFUSE_SPICONFIG_RET_SPID(r)     (((r) >> 12) & 0x3f)
#define EFUSE_SPICONFIG_RET_SPICS0(r)   (((r) >> 18) & 0x3f)
#define EFUSE_SPICONFIG_RET_SPIHD(r)    (((r) >> 24) & 0x3f)

/* Chip package identifiers, from the EFUSE_RD_CHIP_VER_PKG field */
enum {
    kChipPackage_D0WDQ6                 = 0,
    kChipPackage_D0WDQ5                 = 1,
};

/* SPI clock dividers for the 80MHz APB clock */
enum {
    kSpiClkDiv_80M                      = 1,
    kSpiClkDiv_40M                      = 2,
};

/* Ports, as indexed by g_rom_spiflash_dummy_len_plus[] and by
   esp_rom_spiflash_config_clk() */
enum {
    kSpiPort_Cache                      = 0,        /* SPI0 */
    kSpiPort_Flash                      = 1,        /* SPI1 */
};

/* Extra dummy cycles the GPIO matrix costs at each speed.  The pads run
   through the matrix rather than IO_MUX once PSRAM is in play, and the round
   trip eats setup time that has to be paid for in dummy cycles. */
enum {
    kIOMatrixDummy_40M                  = 1,
    kIOMatrixDummy_80M                  = 2,
};

/* Dummy cycles SPI0 uses for flash reads, by read mode */
enum {
    kSpi0ReadDummy_QIO                  = 3,
    kSpi0ReadDummy_DIO                  = 1,
    kSpi0ReadDummy_Fast                 = 7,
    kSpi0DioAddrBitLen                  = 27,
};

/* CS hold, in SPI clock cycles.  Zero for the flash-80M / PSRAM-40M mode this
   driver uses; the 80M/80M mode would need 1. */
#define PSRAM_CS_HOLD_TIME              0

/* 1.8V GigaDevice flash needs every shared pad driven hard at 80MHz */
#define FLASH_ID_GD25LQ32C              0xc86016

/* The external RAM window.  Fixed in silicon, and declared to the linker as
   extern_ram_seg in mastering/ld/esp32/memory.ld. */
enum {
    kPSRAM_WindowBase                   = 0x3f800000,
    kPSRAM_WindowSize                   = 4 * 1024 * 1024,
};

/* MMU page size, in KB.  Must agree with the CMMU_SRAM_PAGE_MODE field
   programmed in _Esp32_PSRAM_CacheInit(). */
#define PSRAM_MMU_PAGE_SIZE_KB          32

/******************************************************************************
 * types
 *****************************************************************************/

/*
 * Two extra clock cycles after CS goes high, or not.  The obsolete 32Mbit v0
 * part requires them; everything since runs in the normal mode.
 */
typedef u8 Esp32_PSRAM_ClkMode;
enum Esp32_PSRAM_ClkMode {
    kEsp32_PSRAM_ClkMode_Norm           = 0,
    kEsp32_PSRAM_ClkMode_DClk           = 1,
};

/* Where each of the shared pads landed */
typedef struct Esp32_PSRAM_Io {
    u8 nFlashClk;
    u8 nFlashCs;
    u8 nPsramClk;
    u8 nPsramCs;
    u8 nPsramQ_SD0;
    u8 nPsramD_SD1;
    u8 nPsramWP_SD3;
    u8 nPsramHD_SD2;
} Esp32_PSRAM_Io;

/* One user-mode SPI1 transaction */
typedef struct Esp32_PSRAM_Cmd {
    u16   nCmd;
    u16   nCmdBitLen;
    u32   nAddr;
    u16   nAddrBitLen;
    u32 * pTxData;
    u16   nTxDataBitLen;
    u32 * pRxData;
    u16   nRxDataBitLen;
    u32   nDummyBitLen;
} Esp32_PSRAM_Cmd;

/******************************************************************************
 * register access
 *
 * The SPI register blocks are identical, so the helpers below take a base
 * address rather than being generated per port.  Only SPI0 (the cache) and
 * SPI1 (user mode) are touched.
 *****************************************************************************/
#define PSRAM_SPI_REG(base, r)          (*(volatile u32 *)((base) + kEsp32_RegisterSPI_ ## r))
#define PSRAM_SPI_W_REG(base, n)        (*(volatile u32 *)((base) + kEsp32_RegisterSPI_W0 + ((n) * 4)))

enum {
    kSpi0Base                           = ESP32_REG_BASE(SPI0),  /* cache      */
    kSpi1Base                           = ESP32_REG_BASE(SPI1),  /* user mode  */
};

/******************************************************************************
 * static variables
 *****************************************************************************/
static Esp32_PSRAM_ClkMode s_clkMode        = kEsp32_PSRAM_ClkMode_DClk;
static u32                 s_nPsramId       = 0;
static u32                 s_nExtraDummy    = 0;

/* SPI1 user-mode register backup, restored after every transaction so the ROM
   flash routines find the port as they left it.  IDF keeps one of these per
   port; every call site is kSpi1Base, so one set is enough. */
static u32                 s_nBackupUser    = 0;
static u32                 s_nBackupUser1   = 0;
static u32                 s_nBackupUser2   = 0;

/******************************************************************************
 * zeroes the 16 word SPI data FIFO
 *****************************************************************************/
static void ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_ClearFifo(u32 nSpiBase) {
    for (u32 nIdx = 0; nIdx < 16; nIdx++) {
        PSRAM_SPI_W_REG(nSpiBase, nIdx) = 0;
    }
}

/******************************************************************************
 * selects single-line or quad-line data phases for user mode transactions
 *****************************************************************************/
static void ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_SetLineMode(u32 nSpiBase, bool bQuad) {
    u32 nWriteModes = ESP32_REG_MASK(SPI_USER, FWRITE_QIO)  |
                      ESP32_REG_MASK(SPI_USER, FWRITE_DIO)  |
                      ESP32_REG_MASK(SPI_USER, FWRITE_QUAD) |
                      ESP32_REG_MASK(SPI_USER, FWRITE_DUAL);
    u32 nReadModes  = ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_QIO)  |
                      ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_DIO)  |
                      ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_QUAD) |
                      ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_DUAL);

    PSRAM_SPI_REG(nSpiBase, USER) &= ~nWriteModes;
    PSRAM_SPI_REG(nSpiBase, CTRL) &= ~nReadModes;
    if (bQuad) {
        PSRAM_SPI_REG(nSpiBase, USER) |= ESP32_REG_MASK(SPI_USER, FWRITE_QIO);
        PSRAM_SPI_REG(nSpiBase, CTRL) |= ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_QIO);
    }
}

/******************************************************************************
 * loads a transaction into the user mode registers, saving the previous
 * contents for _Esp32_PSRAM_CmdEnd() to put back
 *****************************************************************************/
static void ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_CmdConfig(u32 nSpiBase, const Esp32_PSRAM_Cmd * pCmd) {
    while (PSRAM_SPI_REG(nSpiBase, CMD) & ESP32_REG_MASK(SPI_CMD, SPI_USR));

    s_nBackupUser  = PSRAM_SPI_REG(nSpiBase, USER);
    s_nBackupUser1 = PSRAM_SPI_REG(nSpiBase, USER1);
    s_nBackupUser2 = PSRAM_SPI_REG(nSpiBase, USER2);

    /* command phase - at most 16 bits */
    u32 nUser2 = PSRAM_SPI_REG(nSpiBase, USER2) & ~(ESP32_REG_MASK(SPI_USER2, COMMAND_BITLEN) |
                                                    ESP32_REG_MASK(SPI_USER2, COMMAND_VALUE));
    if (pCmd->nCmdBitLen != 0) {
        nUser2 |= ((u32)(pCmd->nCmdBitLen - 1) << ESP32_REG_SHIFT(SPI_USER2, COMMAND_BITLEN)) &
                  ESP32_REG_MASK(SPI_USER2, COMMAND_BITLEN);
        nUser2 |= ((u32)pCmd->nCmd << ESP32_REG_SHIFT(SPI_USER2, COMMAND_VALUE)) &
                  ESP32_REG_MASK(SPI_USER2, COMMAND_VALUE);
        PSRAM_SPI_REG(nSpiBase, USER) |= ESP32_REG_MASK(SPI_USER, COMMAND);
    } else {
        PSRAM_SPI_REG(nSpiBase, USER) &= ~ESP32_REG_MASK(SPI_USER, COMMAND);
    }
    PSRAM_SPI_REG(nSpiBase, USER2) = nUser2;

    /* address phase */
    u32 nUser1 = PSRAM_SPI_REG(nSpiBase, USER1) & ~ESP32_REG_MASK(SPI_USER1, ADDR_BITLEN);
    if (pCmd->nAddrBitLen != 0) {
        nUser1 |= ((u32)(pCmd->nAddrBitLen - 1) << ESP32_REG_SHIFT(SPI_USER1, ADDR_BITLEN)) &
                  ESP32_REG_MASK(SPI_USER1, ADDR_BITLEN);
        PSRAM_SPI_REG(nSpiBase, USER) |= ESP32_REG_MASK(SPI_USER, ADDR);
        PSRAM_SPI_REG(nSpiBase, ADDR) = pCmd->nAddr;
    } else {
        PSRAM_SPI_REG(nSpiBase, USER) &= ~ESP32_REG_MASK(SPI_USER, ADDR);
    }

    /* dummy phase */
    nUser1 &= ~ESP32_REG_MASK(SPI_USER1, DUMMY_CYCLELEN);
    if (pCmd->nDummyBitLen != 0) {
        nUser1 |= ((pCmd->nDummyBitLen - 1) << ESP32_REG_SHIFT(SPI_USER1, DUMMY_CYCLELEN)) &
                  ESP32_REG_MASK(SPI_USER1, DUMMY_CYCLELEN);
        PSRAM_SPI_REG(nSpiBase, USER) |= ESP32_REG_MASK(SPI_USER, DUMMY);
    } else {
        PSRAM_SPI_REG(nSpiBase, USER) &= ~ESP32_REG_MASK(SPI_USER, DUMMY);
    }
    PSRAM_SPI_REG(nSpiBase, USER1) = nUser1;

    /* transmit phase - the FIFO doubles as the send buffer */
    u32 nMosiLen = 0;
    if (pCmd->nTxDataBitLen != 0) {
        PSRAM_SPI_REG(nSpiBase, USER) |= ESP32_REG_MASK(SPI_USER, MOSI);
        if (pCmd->pTxData != NULL) {
            u32 nWords = (pCmd->nTxDataBitLen + 31) / 32;
            for (u32 nIdx = 0; nIdx < nWords; nIdx++) {
                PSRAM_SPI_W_REG(nSpiBase, nIdx) = pCmd->pTxData[nIdx];
            }
        }
        nMosiLen = (u32)(pCmd->nTxDataBitLen - 1);
    } else {
        PSRAM_SPI_REG(nSpiBase, USER) &= ~ESP32_REG_MASK(SPI_USER, MOSI);
    }
    PSRAM_SPI_REG(nSpiBase, MOSI_DLEN) = nMosiLen << ESP32_REG_SHIFT(SPI_MOSI_DLEN, DBITLEN);

    /* receive phase */
    u32 nMisoLen = 0;
    if (pCmd->nRxDataBitLen != 0) {
        PSRAM_SPI_REG(nSpiBase, USER) |= ESP32_REG_MASK(SPI_USER, MISO);
        nMisoLen = (u32)(pCmd->nRxDataBitLen - 1);
    } else {
        PSRAM_SPI_REG(nSpiBase, USER) &= ~ESP32_REG_MASK(SPI_USER, MISO);
    }
    PSRAM_SPI_REG(nSpiBase, MISO_DLEN) = nMisoLen << ESP32_REG_SHIFT(SPI_MISO_DLEN, DBITLEN);
}

/******************************************************************************
 * runs the transaction loaded by _Esp32_PSRAM_CmdConfig() and, if the caller
 * asked for one, copies the reply out of the FIFO
 *****************************************************************************/
static void ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_CmdRecvStart(u32 nSpiBase, u32 * pRxData, u16 nRxByteLen, bool bQpi) {
    u32 nReadModes = ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_QIO)  |
                     ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_DIO)  |
                     ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_QUAD) |
                     ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_DUAL);

    /* aim the shared pads at CS1 (PSRAM) instead of CS0 (flash) */
    PSRAM_SPI_REG(kSpi1Base, PIN) &= ~ESP32_REG_MASK(SPI_PIN, CS1_DIS);
    PSRAM_SPI_REG(kSpi1Base, PIN) |=  ESP32_REG_MASK(SPI_PIN, CS0_DIS);

    u32 nWriteModeBackup = (PSRAM_SPI_REG(nSpiBase, USER) >>
                            ESP32_REG_SHIFT(SPI_USER, FWRITE_DUAL)) & 0x0f;
    u32 nReadModeBackup  = PSRAM_SPI_REG(nSpiBase, CTRL) & nReadModes;

    _Esp32_PSRAM_SetLineMode(nSpiBase, bQpi);

    /* SPI0 must be idle, or the cache would be mid-transaction on pads we are
       about to take over */
    while (PSRAM_SPI_REG(kSpi0Base, EXT2) != 0);
    ESP32_REG(DPORT_HOST_INF_SEL) |= ESP32_REG_MASK(DPORT_HOST_INF_SEL, PSRAM);

    PSRAM_SPI_REG(nSpiBase, CMD) |= ESP32_REG_MASK(SPI_CMD, SPI_USR);
    while (PSRAM_SPI_REG(nSpiBase, CMD) & ESP32_REG_MASK(SPI_CMD, SPI_USR));

    ESP32_REG(DPORT_HOST_INF_SEL) &= ~ESP32_REG_MASK(DPORT_HOST_INF_SEL, PSRAM);

    /* Put the line modes back.  USER is restored wholesale by
       _Esp32_PSRAM_CmdEnd() a moment later, so this is belt and braces; CTRL
       is not, so that half matters. */
    u32 nUser = PSRAM_SPI_REG(nSpiBase, USER);
    nUser &= ~(0x0fu << ESP32_REG_SHIFT(SPI_USER, FWRITE_DUAL));
    nUser |= nWriteModeBackup << ESP32_REG_SHIFT(SPI_USER, FWRITE_DUAL);
    PSRAM_SPI_REG(nSpiBase, USER) = nUser;

    PSRAM_SPI_REG(nSpiBase, CTRL) &= ~nReadModes;
    PSRAM_SPI_REG(nSpiBase, CTRL) |= nReadModeBackup;

    /* and hand the pads back to the flash */
    PSRAM_SPI_REG(kSpi1Base, PIN) |=  ESP32_REG_MASK(SPI_PIN, CS1_DIS);
    PSRAM_SPI_REG(kSpi1Base, PIN) &= ~ESP32_REG_MASK(SPI_PIN, CS0_DIS);

    if (pRxData != NULL) {
        u32 nWords = (nRxByteLen / 4) + ((nRxByteLen % 4) ? 1 : 0);
        for (u32 nIdx = 0; nIdx < nWords; nIdx++) {
            pRxData[nIdx] = PSRAM_SPI_W_REG(nSpiBase, nIdx);
        }
    }
}

/******************************************************************************
 * restores the user mode registers saved by _Esp32_PSRAM_CmdConfig()
 *****************************************************************************/
static void ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_CmdEnd(u32 nSpiBase) {
    while (PSRAM_SPI_REG(nSpiBase, CMD) & ESP32_REG_MASK(SPI_CMD, SPI_USR));
    PSRAM_SPI_REG(nSpiBase, USER)  = s_nBackupUser;
    PSRAM_SPI_REG(nSpiBase, USER1) = s_nBackupUser1;
    PSRAM_SPI_REG(nSpiBase, USER2) = s_nBackupUser2;
}

/******************************************************************************
 * drops the part back into single-line SPI mode.  Safe to issue when it is
 * already there - the part reads the sequence as a no-op - which is why it is
 * the first thing sent after reset.
 *****************************************************************************/
static void ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_DisableQioMode(u32 nSpiBase) {
    Esp32_PSRAM_Cmd cmd = { 0, 0, 0, 0, NULL, 0, NULL, 0, 0 };
    u32 nExitQpi = kPSRAM_Cmd_ExitQMode;

    cmd.nTxDataBitLen = 8;
    if (s_clkMode == kEsp32_PSRAM_ClkMode_DClk) {
        /* pad the opcode out to two bytes so the trailing zero byte supplies
           the two extra clock cycles the v0 part wants */
        nExitQpi = kPSRAM_Cmd_ExitQMode << 8;
        cmd.nTxDataBitLen = 16;
    }
    cmd.pTxData = &nExitQpi;

    _Esp32_PSRAM_CmdConfig(nSpiBase, &cmd);
    _Esp32_PSRAM_CmdRecvStart(nSpiBase, NULL, 0, true);
    _Esp32_PSRAM_CmdEnd(nSpiBase);
}

/******************************************************************************
 * puts the part into quad mode
 *****************************************************************************/
static void ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_EnableQioMode(u32 nSpiBase) {
    Esp32_PSRAM_Cmd cmd = { 0, 0, 0, 0, NULL, 0, NULL, 0, 0 };

    /* the opcode rides in the top byte of the address phase */
    cmd.nAddr       = kPSRAM_Cmd_EnterQMode << 24;
    cmd.nAddrBitLen = 8;
    if (s_clkMode == kEsp32_PSRAM_ClkMode_DClk) {
        cmd.nCmdBitLen = 2;     /* two bits of nothing, to delay two cycles */
    }

    _Esp32_PSRAM_CmdConfig(nSpiBase, &cmd);
    _Esp32_PSRAM_CmdRecvStart(nSpiBase, NULL, 0, false);
    _Esp32_PSRAM_CmdEnd(nSpiBase);
}

/******************************************************************************
 * reads the 64 bit device ID.  The part must be in single-line mode first.
 *****************************************************************************/
static void ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_ReadId(u32 nSpiBase, u32 * pnIdLow) {
    Esp32_PSRAM_Cmd cmd = { 0, 0, 0, 0, NULL, 0, NULL, 0, 0 };
    u32 nId[2] = { 0, 0 };

    cmd.nCmd          = kPSRAM_Cmd_DeviceId;
    cmd.nCmdBitLen    = 8;
    cmd.nAddr         = 0;
    cmd.nAddrBitLen   = 3 * 8;
    if (s_clkMode == kEsp32_PSRAM_ClkMode_DClk) {
        /* fold the opcode into the address phase and use a two bit command
           phase purely as a two cycle delay */
        cmd.nCmd        = 0;
        cmd.nCmdBitLen  = 2;
        cmd.nAddr       = kPSRAM_Cmd_DeviceId << 24;
        cmd.nAddrBitLen = 4 * 8;
    }
    cmd.nRxDataBitLen = 8 * 8;
    cmd.pRxData       = nId;
    cmd.nDummyBitLen  = s_nExtraDummy;

    _Esp32_PSRAM_CmdConfig(nSpiBase, &cmd);
    _Esp32_PSRAM_ClearFifo(nSpiBase);
    _Esp32_PSRAM_CmdRecvStart(nSpiBase, cmd.pRxData, cmd.nRxDataBitLen / 8, false);
    _Esp32_PSRAM_CmdEnd(nSpiBase);

    /* everything the size and revision decode needs is in the low word */
    *pnIdLow = nId[0];
}

/******************************************************************************
 * chip select setup and hold timing
 *****************************************************************************/
static void ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_SetCsTiming(u32 nSpiBase, Esp32_PSRAM_ClkMode clkMode) {
    u32 nCsBits = ESP32_REG_MASK(SPI_USER, CS_HOLD) | ESP32_REG_MASK(SPI_USER, CS_SETUP);

    if (clkMode == kEsp32_PSRAM_ClkMode_Norm) {
        PSRAM_SPI_REG(nSpiBase, USER) |= nCsBits;

        u32 nCtrl2 = PSRAM_SPI_REG(nSpiBase, CTRL2);
        nCtrl2 &= ~(ESP32_REG_MASK(SPI_CTRL2, HOLD_TIME) | ESP32_REG_MASK(SPI_CTRL2, SETUP_TIME));
        nCtrl2 |= (PSRAM_CS_HOLD_TIME << ESP32_REG_SHIFT(SPI_CTRL2, HOLD_TIME)) &
                  ESP32_REG_MASK(SPI_CTRL2, HOLD_TIME);
        PSRAM_SPI_REG(nSpiBase, CTRL2) = nCtrl2;
    } else {
        /* DCLK gets its extra cycles from padded opcodes instead */
        PSRAM_SPI_REG(nSpiBase, USER) &= ~nCsBits;
    }
}

/******************************************************************************
 * puts SPI1 into a known state for user mode transactions
 *****************************************************************************/
static void ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_SpiInit(u32 nSpiBase) {
    PSRAM_SPI_REG(nSpiBase, SLAVE) &= ~ESP32_REG_MASK(SPI_SLAVE, TRANS_INTEN);

    /* CPOL and CPHA both zero */
    PSRAM_SPI_REG(nSpiBase, PIN)  &= ~ESP32_REG_MASK(SPI_PIN, CK_IDLE_EDGE);
    PSRAM_SPI_REG(nSpiBase, USER) &= ~ESP32_REG_MASK(SPI_USER, CK_OUT_EDGE);

    /* MSB first, both directions */
    PSRAM_SPI_REG(nSpiBase, CTRL) &= ~(ESP32_REG_MASK(SPI_CTRL, SPI_WR_BIT_ORDER) |
                                       ESP32_REG_MASK(SPI_CTRL, SPI_RD_BIT_ORDER));

    /* half duplex */
    PSRAM_SPI_REG(nSpiBase, USER) &= ~ESP32_REG_MASK(SPI_USER, DOUTDIN);

    PSRAM_SPI_REG(nSpiBase, USER1) = 0;
    PSRAM_SPI_REG(nSpiBase, SLAVE) &= ~ESP32_REG_MASK(SPI_SLAVE, MODE);

    _Esp32_PSRAM_ClearFifo(nSpiBase);
    _Esp32_PSRAM_SetCsTiming(nSpiBase, s_clkMode);
}

/******************************************************************************
 * pad routing, clock dividers and drive strengths
 *
 * Once PSRAM is in the picture the shared pads have to come out of the GPIO
 * matrix rather than IO_MUX, because two peripherals need to reach them.  That
 * costs setup time, which is paid back in extra dummy cycles - hence the
 * bookkeeping against g_rom_spiflash_dummy_len_plus[], which the ROM's own
 * flash routines consult.
 *****************************************************************************/
static void ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_GpioConfig(const Esp32_PSRAM_Io * pIo) {
    /* how many dummy cycles SPI0 already uses for flash reads depends on the
       read mode the bootloader left it in */
    u32 nCacheDummy;
    u32 nReadMode = PSRAM_SPI_REG(kSpi0Base, CTRL);
    if (nReadMode & ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_QIO)) {
        nCacheDummy = kSpi0ReadDummy_QIO;
    } else if (nReadMode & ESP32_REG_MASK(SPI_CTRL, SPI_FREAD_DIO)) {
        nCacheDummy = kSpi0ReadDummy_DIO;
        u32 nUser1 = PSRAM_SPI_REG(kSpi0Base, USER1) & ~ESP32_REG_MASK(SPI_USER1, ADDR_BITLEN);
        nUser1 |= ((u32)kSpi0DioAddrBitLen << ESP32_REG_SHIFT(SPI_USER1, ADDR_BITLEN)) &
                  ESP32_REG_MASK(SPI_USER1, ADDR_BITLEN);
        PSRAM_SPI_REG(kSpi0Base, USER1) = nUser1;
    } else {
        nCacheDummy = kSpi0ReadDummy_Fast;
    }

    /* Flash at 80MHz, PSRAM at 40MHz.  See the file header for why this mode
       and not one of the other two. */
    s_nExtraDummy = kIOMatrixDummy_40M;
    g_rom_spiflash_dummy_len_plus[kSpiPort_Cache] = kIOMatrixDummy_80M;
    g_rom_spiflash_dummy_len_plus[kSpiPort_Flash] = kIOMatrixDummy_40M;

    u32 nUser1 = PSRAM_SPI_REG(kSpi0Base, USER1) & ~ESP32_REG_MASK(SPI_USER1, DUMMY_CYCLELEN);
    nUser1 |= ((nCacheDummy + kIOMatrixDummy_80M) << ESP32_REG_SHIFT(SPI_USER1, DUMMY_CYCLELEN)) &
              ESP32_REG_MASK(SPI_USER1, DUMMY_CYCLELEN);
    PSRAM_SPI_REG(kSpi0Base, USER1) = nUser1;

    esp_rom_spiflash_config_clk(kSpiClkDiv_80M, kSpiPort_Cache);
    esp_rom_spiflash_config_clk(kSpiClkDiv_40M, kSpiPort_Flash);

    Esp32GPIO_ConfigPinDriveStrength(pIo->nFlashClk, 3);
    Esp32GPIO_ConfigPinDriveStrength(pIo->nPsramClk, 2);

    PSRAM_SPI_REG(kSpi0Base, USER) |= ESP32_REG_MASK(SPI_USER, DUMMY);

    /* The bootloader has already routed most of these; repeat the work in case
       an older bootloader is in flash. */
    esp_rom_gpio_connect_out_signal(pIo->nFlashCs,    kSignal_SPICS0_OUT, false, false);
    esp_rom_gpio_connect_out_signal(pIo->nPsramCs,    kSignal_SPICS1_OUT, false, false);
    esp_rom_gpio_connect_out_signal(pIo->nPsramQ_SD0, kSignal_SPIQ_OUT,   false, false);
    esp_rom_gpio_connect_in_signal (pIo->nPsramQ_SD0, kSignal_SPIQ_IN,    false);
    esp_rom_gpio_connect_out_signal(pIo->nPsramD_SD1, kSignal_SPID_OUT,   false, false);
    esp_rom_gpio_connect_in_signal (pIo->nPsramD_SD1, kSignal_SPID_IN,    false);
    esp_rom_gpio_connect_out_signal(pIo->nPsramWP_SD3, kSignal_SPIWP_OUT, false, false);
    esp_rom_gpio_connect_in_signal (pIo->nPsramWP_SD3, kSignal_SPIWP_IN,  false);
    esp_rom_gpio_connect_out_signal(pIo->nPsramHD_SD2, kSignal_SPIHD_OUT, false, false);
    esp_rom_gpio_connect_in_signal (pIo->nPsramHD_SD2, kSignal_SPIHD_IN,  false);

    /* The flash clock can stay on IO_MUX - and should, it is the faster path -
       as long as it is on its native pad and PSRAM is not sharing it. */
    if (pIo->nFlashClk == kPin_FlashClk_IOMux && pIo->nFlashClk != pIo->nPsramClk) {
        Esp32GPIO_ConfigPinFunction(pIo->nFlashClk, kIOMuxFunc_SD_CLK_SPICLK);
    } else {
        Esp32GPIO_ConfigPinFunction(pIo->nFlashClk, kIOMuxFunc_GPIO);
    }
    Esp32GPIO_ConfigPinFunction(pIo->nFlashCs,     kIOMuxFunc_GPIO);
    Esp32GPIO_ConfigPinFunction(pIo->nPsramCs,     kIOMuxFunc_GPIO);
    Esp32GPIO_ConfigPinFunction(pIo->nPsramClk,    kIOMuxFunc_GPIO);
    Esp32GPIO_ConfigPinFunction(pIo->nPsramQ_SD0,  kIOMuxFunc_GPIO);
    Esp32GPIO_ConfigPinFunction(pIo->nPsramD_SD1,  kIOMuxFunc_GPIO);
    Esp32GPIO_ConfigPinFunction(pIo->nPsramHD_SD2, kIOMuxFunc_GPIO);
    Esp32GPIO_ConfigPinFunction(pIo->nPsramWP_SD3, kIOMuxFunc_GPIO);

    if (g_rom_flashchip.nDeviceId == FLASH_ID_GD25LQ32C) {
        /* 1.8V part at 80MHz - drive everything on the bus as hard as we can */
        Esp32GPIO_ConfigPinDriveStrength(pIo->nFlashCs,     3);
        Esp32GPIO_ConfigPinDriveStrength(pIo->nFlashClk,    3);
        Esp32GPIO_ConfigPinDriveStrength(pIo->nPsramCs,     3);
        Esp32GPIO_ConfigPinDriveStrength(pIo->nPsramClk,    3);
        Esp32GPIO_ConfigPinDriveStrength(pIo->nPsramQ_SD0,  3);
        Esp32GPIO_ConfigPinDriveStrength(pIo->nPsramD_SD1,  3);
        Esp32GPIO_ConfigPinDriveStrength(pIo->nPsramHD_SD2, 3);
        Esp32GPIO_ConfigPinDriveStrength(pIo->nPsramWP_SD3, 3);
    }
}

/******************************************************************************
 * teaches SPI0 - the cache controller - how to read and write the part
 *****************************************************************************/
static void ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_CacheInit(void) {
    /* Flash 80MHz / PSRAM 40MHz.  SPI0's own clock is divided by two for the
       SRAM half of the bus; DATE bit 31 tells the flash half to keep running
       undivided.  Bit 30 - a further pre-divide - stays clear. */
    u32 nClock = PSRAM_SPI_REG(kSpi0Base, CLOCK);
    nClock &= ~(ESP32_REG_MASK(SPI_CLOCK, CLK_EQU_SYSCLK) |
                ESP32_REG_MASK(SPI_CLOCK, CLKDIV_PRE)     |
                ESP32_REG_MASK(SPI_CLOCK, CLKCNT_N)       |
                ESP32_REG_MASK(SPI_CLOCK, CLKCNT_H)       |
                ESP32_REG_MASK(SPI_CLOCK, CLKCNT_L));
    nClock |= (1u << ESP32_REG_SHIFT(SPI_CLOCK, CLKCNT_N));
    nClock |= (1u << ESP32_REG_SHIFT(SPI_CLOCK, CLKCNT_L));
    PSRAM_SPI_REG(kSpi0Base, CLOCK) = nClock;

    PSRAM_SPI_REG(kSpi0Base, DATE) |=  (1u << 31);
    PSRAM_SPI_REG(kSpi0Base, DATE) &= ~(1u << 30);

    /* quad mode, user-supplied read and write opcodes, 24 bit addresses */
    u32 nSctrl = PSRAM_SPI_REG(kSpi0Base, CACHE_SCTRL);
    nSctrl &= ~(ESP32_REG_MASK(SPI_CACHE_SCTRL, USR_SRAM_DIO) |
                ESP32_REG_MASK(SPI_CACHE_SCTRL, SRAM_ADDR_BITLEN) |
                ESP32_REG_MASK(SPI_CACHE_SCTRL, SRAM_DUMMY_CYCLELEN));
    nSctrl |= ESP32_REG_MASK(SPI_CACHE_SCTRL, USR_SRAM_QIO)      |
              ESP32_REG_MASK(SPI_CACHE_SCTRL, SRAM_USR_RCMD)     |
              ESP32_REG_MASK(SPI_CACHE_SCTRL, SRAM_USR_WCMD)     |
              ESP32_REG_MASK(SPI_CACHE_SCTRL, USR_RD_SRAM_DUMMY);
    nSctrl |= (23u << ESP32_REG_SHIFT(SPI_CACHE_SCTRL, SRAM_ADDR_BITLEN)) &
              ESP32_REG_MASK(SPI_CACHE_SCTRL, SRAM_ADDR_BITLEN);
    nSctrl |= ((kPSRAM_Cmd_FastReadQuadDummy + s_nExtraDummy) <<
               ESP32_REG_SHIFT(SPI_CACHE_SCTRL, SRAM_DUMMY_CYCLELEN)) &
              ESP32_REG_MASK(SPI_CACHE_SCTRL, SRAM_DUMMY_CYCLELEN);
    PSRAM_SPI_REG(kSpi0Base, CACHE_SCTRL) = nSctrl;

    /* In DCLK mode the opcodes get a trailing zero byte, which buys the two
       extra clock cycles the v0 part needs; the command length grows to match. */
    u32 nCmdBitLen = 7;
    u32 nReadCmd   = kPSRAM_Cmd_FastReadQuad;
    u32 nWriteCmd  = kPSRAM_Cmd_QuadWrite;
    if (s_clkMode == kEsp32_PSRAM_ClkMode_DClk) {
        nCmdBitLen = 15;
        nReadCmd   = kPSRAM_Cmd_FastReadQuad << 8;
        nWriteCmd  = kPSRAM_Cmd_QuadWrite << 8;
    }
    PSRAM_SPI_REG(kSpi0Base, SRAM_DRD_CMD) =
        ((nCmdBitLen << ESP32_REG_SHIFT(SPI_SRAM_CMD, BITLEN)) & ESP32_REG_MASK(SPI_SRAM_CMD, BITLEN)) |
        ((nReadCmd   << ESP32_REG_SHIFT(SPI_SRAM_CMD, VALUE))  & ESP32_REG_MASK(SPI_SRAM_CMD, VALUE));
    PSRAM_SPI_REG(kSpi0Base, SRAM_DWR_CMD) =
        ((nCmdBitLen << ESP32_REG_SHIFT(SPI_SRAM_CMD, BITLEN)) & ESP32_REG_MASK(SPI_SRAM_CMD, BITLEN)) |
        ((nWriteCmd  << ESP32_REG_SHIFT(SPI_SRAM_CMD, VALUE))  & ESP32_REG_MASK(SPI_SRAM_CMD, VALUE));

    /* Present the whole external window as one contiguous range rather than
       interleaving or splitting it between the two CPUs. */
    ESP32_REG(DPORT_PRO_CACHE_CTRL) &= ~(ESP32_REG_MASK(DPORT, PRO_DRAM_HL) |
                                         ESP32_REG_MASK(DPORT, DRAM_SPLIT));
    ESP32_REG(DPORT_APP_CACHE_CTRL) &= ~(ESP32_REG_MASK(DPORT, APP_DRAM_HL) |
                                         ESP32_REG_MASK(DPORT, DRAM_SPLIT));

    /* Route external SRAM accesses through DRAM1, and set 32KB MMU pages to
       match what cache_sram_mmu_set_rom() will be told below. */
    u32 nProCtrl1 = ESP32_REG(DPORT_PRO_CACHE_CTRL1);
    nProCtrl1 &= ~(ESP32_REG_MASK(DPORT_CACHE_CTRL1, MASK_DRAM1)   |
                   ESP32_REG_MASK(DPORT_CACHE_CTRL1, MASK_OPSDRAM) |
                   ESP32_REG_MASK(DPORT_CACHE_CTRL1, CMMU_SRAM_PAGE_MODE));
    ESP32_REG(DPORT_PRO_CACHE_CTRL1) = nProCtrl1;

    u32 nAppCtrl1 = ESP32_REG(DPORT_APP_CACHE_CTRL1);
    nAppCtrl1 &= ~(ESP32_REG_MASK(DPORT_CACHE_CTRL1, MASK_DRAM1)   |
                   ESP32_REG_MASK(DPORT_CACHE_CTRL1, MASK_OPSDRAM) |
                   ESP32_REG_MASK(DPORT_CACHE_CTRL1, CMMU_SRAM_PAGE_MODE));
    ESP32_REG(DPORT_APP_CACHE_CTRL1) = nAppCtrl1;

    /* finally, let SPI0 assert CS1 */
    PSRAM_SPI_REG(kSpi0Base, PIN) &= ~ESP32_REG_MASK(SPI_PIN, CS1_DIS);
}

/******************************************************************************
 * decodes the capacity from the device ID.  Returns 0 for an ID that does not
 * decode, which the caller treats as "no usable part".
 *****************************************************************************/
static u32 ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_GetSizeInBytes(u32 nId) {
    if (PSRAM_SIZE_ID(nId) == kPSRAM_EIDSize_64Mbit || PSRAM_IS_64MBIT_TRIAL(nId)) {
        return 8 * 1024 * 1024;
    }
    if (PSRAM_SIZE_ID(nId) == kPSRAM_EIDSize_32Mbit) {
        return 4 * 1024 * 1024;
    }
    if (PSRAM_SIZE_ID(nId) == kPSRAM_EIDSize_16Mbit) {
        return 2 * 1024 * 1024;
    }
    return 0;
}

/******************************************************************************
 * fills in the pad assignments from the efuse SPI configuration
 *****************************************************************************/
static void ESP32_MEM_REGION(IRAM)
_Esp32_PSRAM_ResolvePads(Esp32_PSRAM_Io * pIo) {
    u32 nSpiConfig = esp_rom_efuse_get_flash_gpio_info();

    if (nSpiConfig == kEfuseSpiConfig_DefaultSPI) {
        pIo->nFlashClk    = kPin_FlashClk_IOMux;
        pIo->nFlashCs     = kPin_FlashCs_IOMux;
        pIo->nPsramQ_SD0  = kPin_PSRAM_Q_SD0;
        pIo->nPsramD_SD1  = kPin_PSRAM_D_SD1;
        pIo->nPsramWP_SD3 = kPin_PSRAM_WP_SD3;
        pIo->nPsramHD_SD2 = kPin_PSRAM_HD_SD2;
    } else if (nSpiConfig == kEfuseSpiConfig_DefaultHSPI) {
        pIo->nFlashClk    = kPin_FlashClk_HSPI;
        pIo->nFlashCs     = kPin_FlashCs_HSPI;
        pIo->nPsramQ_SD0  = kPin_PSRAM_Q_SD0_HSPI;
        pIo->nPsramD_SD1  = kPin_PSRAM_D_SD1_HSPI;
        pIo->nPsramWP_SD3 = kPin_PSRAM_WP_SD3_HSPI;
        pIo->nPsramHD_SD2 = kPin_PSRAM_HD_SD2_HSPI;
    } else {
        pIo->nFlashClk    = (u8)EFUSE_SPICONFIG_RET_SPICLK(nSpiConfig);
        pIo->nFlashCs     = (u8)EFUSE_SPICONFIG_RET_SPICS0(nSpiConfig);
        pIo->nPsramQ_SD0  = (u8)EFUSE_SPICONFIG_RET_SPIQ(nSpiConfig);
        pIo->nPsramD_SD1  = (u8)EFUSE_SPICONFIG_RET_SPID(nSpiConfig);
        pIo->nPsramHD_SD2 = (u8)EFUSE_SPICONFIG_RET_SPIHD(nSpiConfig);
        /* The write protect pin is not in the efuse word.  IDF derives it from
           the package; for the two packages this driver accepts it is always
           the default pad. */
        pIo->nPsramWP_SD3 = kPin_PSRAM_WP_SD3;
    }
}

/******************************************************************************
 * Detects, configures and maps external PSRAM.  See Esp32_PSRAM.h.
 *****************************************************************************/
bool ESP32_MEM_REGION(IRAM)
Esp32_PSRAM_Initialize(Esp32_PSRAM_Info * pInfo) {
    Esp32_PSRAM_Io io = { 0, 0, 0, 0, 0, 0, 0, 0 };

    pInfo->pBase        = NULL;
    pInfo->nSizeInBytes = 0;

    /* Which package is this?  The identifier is split across two fields of the
       same efuse word - three bits at [11:9] and a fourth at [2]. */
    u32 nRdata3  = ESP32_REG(EFUSE_BLK0_RDATA3);
    u32 nPackage = ((nRdata3 & ESP32_REG_MASK(EFUSE, RD_CHIP_VER_PKG)) >>
                    ESP32_REG_SHIFT(EFUSE, RD_CHIP_VER_PKG));
    if (nRdata3 & ESP32_REG_MASK(EFUSE, RD_CHIP_VER_PKG_4BIT)) {
        nPackage |= (1u << 3);
    }

    if (nPackage != kChipPackage_D0WDQ6 && nPackage != kChipPackage_D0WDQ5) {
        /* Everything else needs the VDDSDIO rail inspected or raised first,
           which LT cannot do.  Not fatal - just no PSRAM. */
        esp_rom_printf("psram: chip package %d is not supported, PSRAM disabled\n", (int)nPackage);
        return false;
    }
    io.nPsramClk = kPin_PSRAMClk_D0WD;
    io.nPsramCs  = kPin_PSRAMCs_D0WD;

    _Esp32_PSRAM_ResolvePads(&io);

    /* Reset the hold logic left over from whatever the ROM was last doing on
       this bus, then bring SPI1 into a known state. */
    PSRAM_SPI_REG(kSpi0Base, EXT3) = 0x1;
    PSRAM_SPI_REG(kSpi1Base, USER) &= ~ESP32_REG_MASK(SPI_USER, PREP_HOLD);

    _Esp32_PSRAM_SpiInit(kSpi1Base);

    /* Start out assuming a v0 part, which is the awkward case: it needs the
       PSRAM clock delayed relative to the SPI peripheral's own.  There is no
       delay line in the pad logic, so build one by bouncing the signal through
       the GPIO matrix twice, using two pins that exist in silicon but are not
       bonded out in any package:

         SPI CLK -> GPIO28 -> signal 224 -> GPIO29 -> signal 225 -> PSRAM CLK

       If the ID read below says this is a modern part, all of this is undone. */
    esp_rom_gpio_connect_out_signal(kPin_Internal28, kSignal_SPICLK_OUT, false, false);
    esp_rom_gpio_connect_in_signal (kPin_Internal28, kSignal_FUNC224_IN, false);
    esp_rom_gpio_connect_out_signal(kPin_Internal29, kSignal_FUNC224_IN, false, false);
    esp_rom_gpio_connect_in_signal (kPin_Internal29, kSignal_FUNC225_IN, false);
    esp_rom_gpio_connect_out_signal(io.nPsramClk,    kSignal_FUNC225_IN, false, false);

    /* VDDSDIO is already at the right voltage: the bootloader set it for the
       flash in bootloader_common_vddsdio_configure(), and PSRAM shares the
       rail.  1.8V parts on a 3.3V rail are caught by the package check above. */

    _Esp32_PSRAM_GpioConfig(&io);

    _Esp32_PSRAM_DisableQioMode(kSpi1Base);
    _Esp32_PSRAM_ReadId(kSpi1Base, &s_nPsramId);
    if (!PSRAM_IS_VALID(s_nPsramId)) {
        /* 16Mbit parts are known to garble the first ID read after reset.
           Treat that one as a warm-up and ask again. */
        _Esp32_PSRAM_ReadId(kSpi1Base, &s_nPsramId);
        if (!PSRAM_IS_VALID(s_nPsramId)) {
            /* No part fitted, or it is not answering.  Either way, carry on
               with internal RAM. */
            return false;
        }
    }

    if (PSRAM_IS_32MBIT_VER0(s_nPsramId)) {
        s_clkMode = kEsp32_PSRAM_ClkMode_DClk;
    } else {
        /* A modern part needs no trailing clock cycles, so tear the delay line
           back down and drive the PSRAM clock straight from SPI0.

           Note the two disconnects go straight at the GPIO matrix registers
           rather than through Esp32GPIO_ConfigMatrixPin(): the signal index
           for "drive from the pin's own output register" is 256, which does
           not fit that function's u8 signal parameter. */
        ESP32_REG_ARRAY_VALUE(GPIO_FUNC0_OUT_SEL_CFG, kPin_Internal28) = kSignal_GPIO_OUT;
        ESP32_REG_ARRAY_VALUE(GPIO_FUNC0_OUT_SEL_CFG, kPin_Internal29) = kSignal_GPIO_OUT;
        esp_rom_gpio_connect_out_signal(io.nPsramClk, kSignal_SPICLK_OUT, false, false);

        s_clkMode = kEsp32_PSRAM_ClkMode_Norm;
    }

    _Esp32_PSRAM_SetCsTiming(kSpi1Base, s_clkMode);
    _Esp32_PSRAM_SetCsTiming(kSpi0Base, s_clkMode);
    _Esp32_PSRAM_EnableQioMode(kSpi1Base);

    u32 nSizeInBytes = _Esp32_PSRAM_GetSizeInBytes(s_nPsramId);
    if (nSizeInBytes == 0) {
        esp_rom_printf("psram: unrecognised device id 0x%08x, PSRAM disabled\n", s_nPsramId);
        return false;
    }

    _Esp32_PSRAM_CacheInit();

    /* Only 4MB of address space exists for external RAM on this part, so a
       larger die is simply mapped in part.  (IDF's bank switching would reach
       the rest a window at a time; LT does not implement it.) */
    u32 nMappedBytes = nSizeInBytes;
    if (nMappedBytes > kPSRAM_WindowSize) {
        nMappedBytes = kPSRAM_WindowSize;
    }
    int nPages = (int)(nMappedBytes / 1024 / PSRAM_MMU_PAGE_SIZE_KB);

    if (cache_sram_mmu_set_rom(0, 0, kPSRAM_WindowBase, 0, PSRAM_MMU_PAGE_SIZE_KB, nPages) != 0) {
        esp_rom_printf("psram: failed to map %d KB, PSRAM disabled\n", (int)(nMappedBytes / 1024));
        return false;
    }
    /* Map it for CPU1 as well.  The second core is not started today, but the
       APP CPU stack is reserved for it (see mastering/ld/esp32/memory.ld) and
       leaving its MMU unprogrammed would make PSRAM look like a hole from that
       side the moment it does come up. */
    ESP32_REG(DPORT_APP_CACHE_CTRL1) &= ~ESP32_REG_MASK(DPORT_CACHE_CTRL1, MASK_DRAM1);
    cache_sram_mmu_set_rom(1, 0, kPSRAM_WindowBase, 0, PSRAM_MMU_PAGE_SIZE_KB, nPages);

    esp_rom_printf("psram: %d KB found, %d KB mapped at 0x%08x\n",
                   (int)(nSizeInBytes / 1024), (int)(nMappedBytes / 1024), kPSRAM_WindowBase);

    pInfo->pBase        = (u8 *)kPSRAM_WindowBase;
    pInfo->nSizeInBytes = nMappedBytes;
    return true;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Jul-26   claudius    created
 */
