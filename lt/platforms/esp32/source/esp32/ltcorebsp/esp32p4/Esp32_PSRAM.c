/******************************************************************************
 * Esp32_PSRAM.c                                                   ESP32-P4 BSP
 *
 * Detection, configuration and memory mapping of the in-package hex SPI PSRAM.
 *
 * This is a hand-written, LT-native equivalent of IDF's
 * components/esp_psram/esp32p4/esp_psram_impl_ap_hex.c plus the mapping step
 * esp_psram.c performs above it.  It is not a vendored copy: those files pull
 * in most of IDF's soc/, hal/ and esp_hw_support/ trees, and the app-side
 * include path here carries none of them - only the hand-written Esp32_*.h
 * headers.
 *
 * How PSRAM works on this part, in short.  Flash and PSRAM do not share a bus:
 * SOC_MEMSPI_FLASH_PSRAM_INDEPENDENT is 1, so the PSRAM has an MSPI controller
 * pair entirely of its own.  PSRAM0 is the cache and AXI side, programmed once
 * with the opcodes, line width and timing to use for cache traffic; PSRAM1 is a
 * conventional user-mode controller used to read and rewrite the die's mode
 * registers.  Nothing below touches the flash controllers, so unlike the
 * esp32s3 there is no ordering constraint against flash and no need to drop the
 * flash clock while the mode registers are being written.
 *
 * Three things differ structurally from the esp32s3 arm of this driver:
 *
 *   - PSRAM has an address window of its own, 0x48000000 to 0x4C000000, shared
 *     with nothing.  There is no arithmetic against the end of flash rodata and
 *     no chance of a collision, so the base address is a constant.
 *
 *   - The ROM does not export esp_rom_opiflash_exec_cmd() on this part, despite
 *     declaring it in rom/opi_flash.h.  A user-mode transaction is assembled
 *     from the three entry points that do exist - set_op_mode, cmd_config and
 *     cmd_start - which is what IDF's psram_ctrlr_ll_common_transaction() does.
 *
 *   - The PSRAM die's VDD comes from the MPLL (SOC_PSRAM_VDD_POWER_MPLL), which
 *     the bootloader never starts.  The MPLL divider lives behind the analog
 *     register I2C bus, and the ROM exports no regi2c entry point either, so
 *     both the bus and the PLL bring-up are open-coded here.
 *
 * Scope: the module clock is 20MHz DDR, IDF's default for this part and the
 * only non-experimental choice it offers.  It is also the speed that needs no
 * timing tuning - MSPI_TIMING_PSRAM_NEEDS_TUNING is defined only at 200MHz and
 * above - so the reference-pattern search and its per-core-clock tables are not
 * vendored.  Raising the speed later means adding that tuning pass and changing
 * kPSRAM_ModuleClockMHz; nothing else here is speed dependent.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>

#include "Esp32_SoC.h"
#include "Esp32_Registers.h"
#include "Esp32_Cache.h"
#include "Esp32_PSRAM.h"

/******************************************************************************
 * ROM entry points
 *
 * Declared here rather than in a header, following the pattern Esp32_SoC.h uses
 * for esp_rom_printf().  All of these are PROVIDEd by
 * mastering/ld/esp32p4/rom/esp32p4.rom.ld, except esp_rom_delay_us which
 * rom.api.ld aliases onto ets_delay_us.
 *****************************************************************************/

/* A user-mode MSPI transaction is three calls: pick the line width and data
   rate, describe the phases, then run it.  esp_rom_spi_cmd_t is the ROM's own
   descriptor, copied from rom/opi_flash.h - note that addr is passed by
   address, so the caller's variable has to outlive cmd_config. */
typedef struct {
    u16   nCmd;
    u16   nCmdBitLen;
    u32 * pAddr;
    u32   nAddrBitLen;
    u32 * pTxData;
    u32   nTxDataBitLen;
    u32 * pRxData;
    u32   nRxDataBitLen;
    u32   nDummyBitLen;
} Esp32_RomSpiCmd;

void esp_rom_spi_set_op_mode(int nSpiNum, int nMode);
void esp_rom_spi_cmd_config(int nSpiNum, Esp32_RomSpiCmd * pCmd);
void esp_rom_spi_cmd_start(int nSpiNum, u8 * pRxBuffer, u16 nRxLen, u8 nCsEnableMask, bool bIsWriteErase);

/* Marks every PSRAM MMU entry invalid.  The flash side has its own
   Cache_FLASH_MMU_Init(), which the bootloader has already called; this part
   keeps a separate table per external memory target
   (SOC_MMU_PER_EXT_MEM_TARGET), so the two do not overlap. */
void Cache_PSRAM_MMU_Init(void);

/* Programs the PSRAM MMU.  nVirtAddr and nPhysAddr are byte addresses,
   nPageSizeKB is 64, and nFixed maps every page to nPhysAddr when non-zero.
   Returns 0 on success, 2 for a misaligned address, 3 for a bad page size and
   4 for a virtual address outside the window. */
int Cache_PSRAM_MMU_Set(u32 nSensitive, u32 nVirtAddr, u32 nPhysAddr,
                        u32 nPageSizeKB, u32 nNumPages, u32 nFixed);

/* Stops the L2 cache issuing new transactions, returning the preload state to
   hand back to Cache_Resume_L2_Cache().  Esp32_Cache.h exposes no suspend and
   resume pair, so these are declared here in the same style as the rest. */
u32  Cache_Suspend_L2_Cache(void);
void Cache_Resume_L2_Cache(u32 nAutoload);

void esp_rom_delay_us(u32 nMicroseconds);

/******************************************************************************
 * constants
 *****************************************************************************/

/* esp_rom_spiflash_read_mode_t, from esp_rom/include/esp32p4/esp_rom_spiflash.h */
enum {
    kRomSpiFlashMode_OpiDtr             = 7,
};

/* Hex PSRAM opcodes.  Each is duplicated into both bytes because the part
 * samples the command on both clock edges. */
enum {
    kHexPSRAM_Cmd_SyncRead              = 0x0000,
    kHexPSRAM_Cmd_SyncWrite             = 0x8080,
    kHexPSRAM_Cmd_RegRead               = 0x4040,
    kHexPSRAM_Cmd_RegWrite              = 0xc0c0,

    kHexPSRAM_CmdBitLen                 = 16,
    kHexPSRAM_AddrBitLen                = 32,

    /* Read latency 10 and write latency 5, doubled because DDR counts both
     * edges, less one because the hardware field is "cycles - 1". */
    kHexPSRAM_RdDummyBitLen             = 2 * (10 - 1),
    kHexPSRAM_WrDummyBitLen             = 2 * (5 - 1),
};

/* Mode register addresses, and the fields this driver reads or writes.  Each
 * register is a byte; the part auto-increments, so a 16-bit access at an even
 * address returns or takes a pair.  s_modeReg below holds them by number. */
enum {
    kHexPSRAM_MR0                       = 0,
    kHexPSRAM_MR1                       = 1,
    kHexPSRAM_MR2                       = 2,
    kHexPSRAM_MR3                       = 3,
    kHexPSRAM_MR4                       = 4,
    kHexPSRAM_MR8                       = 8,
    kHexPSRAM_NumModeRegs               = 9,
};

/* MR0 - drive strength, read latency, latency type */
#define PSRAM_MR0_DRIVE_STR(v)          (((v) >> 0) & 0x03)
#define PSRAM_MR0_READ_LATENCY(v)       (((v) >> 2) & 0x07)
#define PSRAM_MR0_LT(v)                 (((v) >> 5) & 0x01)
/* MR1 - manufacturer */
#define PSRAM_MR1_VENDOR_ID(v)          (((v) >> 0) & 0x1f)
/* MR2 - density, die revision, known good die */
#define PSRAM_MR2_DENSITY(v)            (((v) >> 0) & 0x07)
#define PSRAM_MR2_DEV_ID(v)             (((v) >> 3) & 0x03)
#define PSRAM_MR2_KGD(v)                (((v) >> 5) & 0x07)
/* MR3 - self refresh rate.  Two bits wide here, and there is no supply voltage
 * field: the die is in package and its rail is not a board choice. */
#define PSRAM_MR3_SRF(v)                (((v) >> 4) & 0x03)
/* MR4 - write latency */
#define PSRAM_MR4_WR_LATENCY(v)         (((v) >> 5) & 0x07)
/* MR8 - burst length, burst type, row boundary crossing, bus width */
#define PSRAM_MR8_BL(v)                 (((v) >> 0) & 0x03)
#define PSRAM_MR8_BT(v)                 (((v) >> 2) & 0x01)
#define PSRAM_MR8_RBX(v)                (((v) >> 3) & 0x01)
#define PSRAM_MR8_X16(v)                (((v) >> 6) & 0x01)

enum {
    /* Only APMemory parts are fitted in this package. */
    kHexPSRAM_VendorId_APMemory         = 0x0d,

    /* Known-good-die code the vendor programs on a part that passed test. */
    kHexPSRAM_KgdPass                   = 6,

    /* MR2 density encodings.  The ESP32-P4NRW32 reports 0x7. */
    kHexPSRAM_Density_32Mbit            = 0x1,
    kHexPSRAM_Density_64Mbit            = 0x3,
    kHexPSRAM_Density_128Mbit           = 0x5,
    kHexPSRAM_Density_256Mbit           = 0x7,
    kHexPSRAM_Density_512Mbit           = 0x6,
};

/* The mode register values this driver programs.  Fixed latency at 2, which MR0
 * encodes as 2*2+6 = 10 read cycles, matching kHexPSRAM_RdDummyBitLen, and
 * maximum drive strength.  MR8 asks for the longest burst, linear order, row
 * boundary crossing and the full sixteen-bit bus. */
enum {
    kHexPSRAM_Set_LatencyType_Fixed     = 1,
    kHexPSRAM_Set_ReadLatency           = 2,
    kHexPSRAM_Set_WriteLatency          = 2,
    kHexPSRAM_Set_DriveStrength         = 0,
    kHexPSRAM_Set_BurstLength           = 3,
    kHexPSRAM_Set_BurstType_Linear      = 0,
    kHexPSRAM_Set_RowBoundaryCross      = 1,
    kHexPSRAM_Set_X16                   = 1,
};

/* Chip select timing, in module clocks. */
enum {
    kHexPSRAM_CsSetupTime               = 4,
    kHexPSRAM_CsHoldTime                = 4,
    kHexPSRAM_CsHoldDelay               = 3,
};

/* The two PSRAM MSPI controllers, by the numbering the ROM's SPI entry points
 * use.  2 is SPIMEM2, the cache and AXI side; 3 is SPIMEM3, user mode. */
enum {
    kMSPI_Cache                         = 2,
    kMSPI_User                          = 3,
};

/* Chip select mask for esp_rom_spi_cmd_start - bit 1 is CS1, which is where the
 * PSRAM die sits even though it is the only thing on this bus. */
enum {
    kCsMask_PSRAM                       = 0x02,
};

/* Clocking.  The MPLL is the only source that can reach the PSRAM, and IDF
 * fixes it at 400MHz on this part; the module clock is that divided down. */
enum {
    kMPLL_FreqMHz                       = 400,
    kMPLL_XtalFreqMHz                   = 40,
    kPSRAM_ModuleClockMHz               = 20,
    kPSRAM_ClockDivider                 = kMPLL_FreqMHz / kPSRAM_ModuleClockMHz,

    /* Drive strength for the twenty MSPI pads, matching IDF's
     * mspi_timing_ll_pin_drv_set(2). */
    kMSPI_PadDriveStrength              = 2,

    /* Spin bound for the hardware handshakes below.  Both complete in tens of
     * cycles; a bound only exists so that silicon which never answers reports
     * itself instead of wedging the boot before the console is usable. */
    kPSRAM_PollLimit                    = 1000000,
};

/* The external LDO that supplies the PSRAM die and the MPLL.  1.8V is IDF's
 * default for this rail on this part.  DREF 6 with MUL 5 lands on exactly
 * 1800mV with the uncalibrated constants, and is the starting point the eFuse
 * trim replaces when the die carries one. */
enum {
    kLDO_TargetMilliVolts               = 1800,
    kLDO_UncalibratedDRef               = 6,
    kLDO_UncalibratedMul                = 5,
};

/* The analog register I2C bus.  The MPLL block answers at slave id 0x63; only
 * three of its registers are touched. */
enum {
    kRegI2C_Block_MPLL                  = 0x63,

    kRegI2C_MPLL_IrCalRstb              = 1,
    kRegI2C_MPLL_IrCalRstb_M            = 0x20,     /* bit 5 */
    kRegI2C_MPLL_Div                    = 2,
    kRegI2C_MPLL_DHRef                  = 3,
    kRegI2C_MPLL_DHRef_S                = 4,
    kRegI2C_MPLL_DHRef_M                = 0x30,     /* bits 5:4 */
    kRegI2C_MPLL_DHRef_V                = 3,

    /* MPLL_Freq = XTAL_Freq * (div + 1) / (ref_div + 1), with ref_div 1 and the
     * divider in bits 7:3 - so 400MHz off a 40MHz crystal is div 19. */
    kRegI2C_MPLL_RefDiv                 = 1,
    kRegI2C_MPLL_DivValue               = kMPLL_FreqMHz / 20 - 1,
    kRegI2C_MPLL_DivWord                = (kRegI2C_MPLL_DivValue << 3) | kRegI2C_MPLL_RefDiv,
};

/* PSRAM MMU.  The window is 64MB wide and starts at a fixed address. */
enum {
    kMMU_PageSizeKB                     = 64,
    kMMU_PageSizeBytes                  = kMMU_PageSizeKB * 1024,
    kMMU_NotSensitive                   = 0,

    kExtRam_VAddrLow                    = 0x48000000,
    kExtRam_VAddrHigh                   = 0x4c000000,
};

/******************************************************************************
 * register access
 *****************************************************************************/

/* r is the register's field-name prefix, as ESP32_REG_MASK takes it. */
#define PSRAM_SET_FIELD(reg, r, f, v)   do {                                                                        \
        u32 nTmp_ = (reg);                                                                                          \
        nTmp_ &= ~(u32)ESP32_REG_MASK(r, f);                                                                        \
        nTmp_ |= ((u32)(v) << ESP32_REG_SHIFT(r, f)) & (u32)ESP32_REG_MASK(r, f);                                   \
        (reg) = nTmp_;                                                                                              \
    } while (0)

/******************************************************************************
 * static variables
 *****************************************************************************/

/* The mode register bytes, indexed by register number.  Word aligned because
 * the ROM's transaction descriptor types its data pointers as u32 *. */
static u8 s_modeReg[kHexPSRAM_NumModeRegs] LT_ALIGNED(4);

/******************************************************************************
 * analog register I2C bus
 *
 * The MPLL's divider is not a memory mapped register.  It is reached over an
 * on-chip I2C bus that I2C_ANA_MST fronts: one control word carries the slave
 * id, the register address, a direction flag and a data byte, and the BUSY bit
 * is polled either side of it.  The ROM exports no entry point for this on this
 * part, and the only copy in the tree - esp_rom_regi2c_esp32p4.c - is
 * bootloader side, so it is open-coded here.
 *****************************************************************************/

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_RegI2CEnableMPLL(void) {
    /* Clock the master, then point it at the MPLL block.  Upstream calls
     * regi2c_ctrl_ll_master_enable_clock(true), whose macro wrapper declares an
     * RCC atomic environment this tree has no counterpart for; the clock bit is
     * written directly instead, and its reset default is already 1. */
    ESP32_REG(LPPERI_CLK_EN) |= ESP32_REG_MASK(LPPERI_CLK_EN, I2CMST);
    ESP32_REG(I2C_ANA_MST_CLK160M) |= ESP32_REG_MASK(I2C_ANA_MST_CLK160M, SEL);

    ESP32_REG(I2C_ANA_MST_ANA_CONF1) &= ~(u32)ESP32_REG_MASK(I2C_ANA_MST_ANA_CONF, SEL);
    ESP32_REG(I2C_ANA_MST_ANA_CONF2) &= ~(u32)ESP32_REG_MASK(I2C_ANA_MST_ANA_CONF, SEL);
    ESP32_REG(I2C_ANA_MST_ANA_CONF2) |= ESP32_REG_MASK(I2C_ANA_MST_ANA_CONF2, MSPI_SEL);
}

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_RegI2CWait(void) {
    u32 nSpins = kPSRAM_PollLimit;

    while (ESP32_REG(I2C_ANA_MST_I2C0_CTRL) & ESP32_REG_MASK(I2C_ANA_MST_I2C0_CTRL, BUSY)) {
        if (--nSpins == 0) {
            /* Giving up leaves the divider unwritten, which the MPLL lock check
             * downstream catches. */
            return;
        }
    }
}

static u8 ESP32_IRAM_FUNC
_Esp32_PSRAM_RegI2CRead(u8 nBlock, u8 nRegister) {
    _Esp32_PSRAM_RegI2CWait();
    ESP32_REG(I2C_ANA_MST_I2C0_CTRL) = ((u32)nBlock    << ESP32_REG_SHIFT(I2C_ANA_MST_I2C0_CTRL, SLAVE_ID))
                                     | ((u32)nRegister << ESP32_REG_SHIFT(I2C_ANA_MST_I2C0_CTRL, ADDR));
    _Esp32_PSRAM_RegI2CWait();
    return (u8)((ESP32_REG(I2C_ANA_MST_I2C0_CTRL) & ESP32_REG_MASK(I2C_ANA_MST_I2C0_CTRL, DATA))
                >> ESP32_REG_SHIFT(I2C_ANA_MST_I2C0_CTRL, DATA));
}

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_RegI2CWrite(u8 nBlock, u8 nRegister, u8 nData) {
    _Esp32_PSRAM_RegI2CWait();
    ESP32_REG(I2C_ANA_MST_I2C0_CTRL) = ((u32)nBlock    << ESP32_REG_SHIFT(I2C_ANA_MST_I2C0_CTRL, SLAVE_ID))
                                     | ((u32)nRegister << ESP32_REG_SHIFT(I2C_ANA_MST_I2C0_CTRL, ADDR))
                                     | ((u32)nData     << ESP32_REG_SHIFT(I2C_ANA_MST_I2C0_CTRL, DATA))
                                     | (u32)ESP32_REG_MASK(I2C_ANA_MST_I2C0_CTRL, WR);
    _Esp32_PSRAM_RegI2CWait();
}

/******************************************************************************
 * external LDO
 *
 * Channel 2 is one rail feeding two loads: the PSRAM die and the MPLL that
 * clocks it.  Nothing here works until it is up, and the bootloader leaves it
 * alone, so it is the first thing this file touches.  The reset default powers
 * only channel 1, the 3.3V flash rail.
 *****************************************************************************/

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_StartLDO(void) {
    u8 nDRef = kLDO_UncalibratedDRef;
    u8 nMul  = kLDO_UncalibratedMul;

    /* Prefer the die's own trim for this rail when it has one.  Both fields
     * read zero on an untrimmed part, and zero is not a legal pair. */
    u32 nMacSys2 = ESP32_REG(EFUSE_RD_MAC_SYS_2);
    u32 nBlockVersion = ((nMacSys2 & ESP32_REG_MASK(EFUSE, BLK_VERSION_MAJOR)) >> ESP32_REG_SHIFT(EFUSE, BLK_VERSION_MAJOR))
                      * 100
                      + ((nMacSys2 & ESP32_REG_MASK(EFUSE, BLK_VERSION_MINOR)) >> ESP32_REG_SHIFT(EFUSE, BLK_VERSION_MINOR));

    if (nBlockVersion >= 100) {
        u8 nFuseDRef = (u8)((nMacSys2 & ESP32_REG_MASK(EFUSE, LDO_VO2_DREF)) >> ESP32_REG_SHIFT(EFUSE, LDO_VO2_DREF));
        u8 nFuseMul  = (u8)((ESP32_REG(EFUSE_RD_MAC_SYS_3) & ESP32_REG_MASK(EFUSE, LDO_VO2_MUL))
                            >> ESP32_REG_SHIFT(EFUSE, LDO_VO2_MUL));

        if ((nFuseDRef != 0) && (nFuseMul != 0)) {
            nDRef = nFuseDRef;
            nMul  = nFuseMul;
        }
    }

    /* Regulate rather than pass the 3.3V input through, then set the target. */
    ESP32_REG(PMU_EXT_LDO_CHAN2) &= ~(u32)ESP32_REG_MASK(PMU_EXT_LDO_CHAN2, TIEH);
    PSRAM_SET_FIELD(ESP32_REG(PMU_EXT_LDO_CHAN2_ANA), PMU_EXT_LDO_CHAN2_ANA, DREF, nDRef);
    PSRAM_SET_FIELD(ESP32_REG(PMU_EXT_LDO_CHAN2_ANA), PMU_EXT_LDO_CHAN2_ANA, MUL,  nMul);

    /* Take the channel off the eFuse's control and onto TIEH. */
    ESP32_REG(PMU_EXT_LDO_CHAN2) |= ESP32_REG_MASK(PMU_EXT_LDO_CHAN2, FORCE_TIEH_SEL);
    ESP32_REG(PMU_EXT_LDO_CHAN2) &= ~(u32)ESP32_REG_MASK(PMU_EXT_LDO_CHAN2, TIEH_SEL);

    ESP32_REG(PMU_EXT_LDO_CHAN2_ANA) |= ESP32_REG_MASK(PMU_EXT_LDO_CHAN2_ANA, EN_VDET);
    ESP32_REG(PMU_EXT_LDO_CHAN2)     |= ESP32_REG_MASK(PMU_EXT_LDO_CHAN2, XPD);

    /* Let the rail reach its target before anything on it is clocked. */
    esp_rom_delay_us(500);
}

/******************************************************************************
 * MPLL
 *
 * The PSRAM die's VDD is derived from the MPLL, so nothing on the bus answers
 * until it is running.  The bootloader does not start it - there is no MPLL
 * code anywhere in ltbootloader/esp32p4 - so it is configured and enabled here.
 *****************************************************************************/

static bool ESP32_IRAM_FUNC
_Esp32_PSRAM_StartMPLL(void) {
    /* Power the MSPI PHY and ungate the PLL's output first.  The calibration
     * below runs on the PLL itself, so with it unpowered CAL_END never
     * asserts. */
    ESP32_REG(PMU_RF_PWC) |= ESP32_REG_MASK(PMU_RF_PWC, MSPI_PHY_XPD);
    ESP32_REG(LP_CLKRST_HP_CLK_CTRL) |= ESP32_REG_MASK(LP_CLKRST_HP_CLK_CTRL, MPLL_EN);

    _Esp32_PSRAM_RegI2CEnableMPLL();

    /* Self-calibration runs while the divider is written, and stops when it is
     * done: clearing CAL_STOP starts it, CAL_END reports completion. */
    ESP32_REG(HP_CLKRST_ANA_PLL_CTRL0) &= ~(u32)ESP32_REG_MASK(HP_CLKRST_ANA_PLL_CTRL0, MSPI_CAL_STOP);

    u8 nDHRef = _Esp32_PSRAM_RegI2CRead(kRegI2C_Block_MPLL, kRegI2C_MPLL_DHRef);
    _Esp32_PSRAM_RegI2CWrite(kRegI2C_Block_MPLL, kRegI2C_MPLL_DHRef,
                             (u8)(nDHRef | (kRegI2C_MPLL_DHRef_V << kRegI2C_MPLL_DHRef_S)));

    /* Pulse the calibration reset low then high before loading the divider. */
    u8 nRstb = _Esp32_PSRAM_RegI2CRead(kRegI2C_Block_MPLL, kRegI2C_MPLL_IrCalRstb);
    _Esp32_PSRAM_RegI2CWrite(kRegI2C_Block_MPLL, kRegI2C_MPLL_IrCalRstb, (u8)(nRstb & ~kRegI2C_MPLL_IrCalRstb_M));
    _Esp32_PSRAM_RegI2CWrite(kRegI2C_Block_MPLL, kRegI2C_MPLL_IrCalRstb, (u8)(nRstb | kRegI2C_MPLL_IrCalRstb_M));

    _Esp32_PSRAM_RegI2CWrite(kRegI2C_Block_MPLL, kRegI2C_MPLL_Div, (u8)kRegI2C_MPLL_DivWord);

    u32 nSpins = kPSRAM_PollLimit;
    while (! (ESP32_REG(HP_CLKRST_ANA_PLL_CTRL0) & ESP32_REG_MASK(HP_CLKRST_ANA_PLL_CTRL0, MSPI_CAL_END))) {
        if (--nSpins == 0) {
            return false;
        }
    }
    ESP32_REG(HP_CLKRST_ANA_PLL_CTRL0) |= ESP32_REG_MASK(HP_CLKRST_ANA_PLL_CTRL0, MSPI_CAL_STOP);

    /* The die needs its rail to settle before it will decode a command. */
    esp_rom_delay_us(100);

    return true;
}

/******************************************************************************
 * PSRAM MSPI clocking
 *****************************************************************************/

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_StartModuleClock(void) {
    ESP32_REG(HP_CLKRST_SOC_CLK_CTRL0)   |= ESP32_REG_MASK(HP_CLKRST_SOC_CLK_CTRL0, PSRAM_SYS_CLK_EN);
    ESP32_REG(HP_CLKRST_PERI_CLK_CTRL00) |= ESP32_REG_MASK(HP_CLKRST_PERI_CLK_CTRL00, PSRAM_PLL_CLK_EN);

    /* Reset both halves of the controller pair: the AXI side that serves the
     * cache and the APB side that user mode transactions go through. */
    ESP32_REG(HP_CLKRST_HP_RST_EN0) |=  ESP32_REG_MASK(HP_CLKRST_HP_RST_EN0, DUAL_MSPI_AXI);
    ESP32_REG(HP_CLKRST_HP_RST_EN0) &= ~(u32)ESP32_REG_MASK(HP_CLKRST_HP_RST_EN0, DUAL_MSPI_AXI);
    ESP32_REG(HP_CLKRST_HP_RST_EN0) |=  ESP32_REG_MASK(HP_CLKRST_HP_RST_EN0, DUAL_MSPI_APB);
    ESP32_REG(HP_CLKRST_HP_RST_EN0) &= ~(u32)ESP32_REG_MASK(HP_CLKRST_HP_RST_EN0, DUAL_MSPI_APB);

    /* One source select covers both controllers. */
    PSRAM_SET_FIELD(ESP32_REG(HP_CLKRST_PERI_CLK_CTRL00), HP_CLKRST_PERI_CLK_CTRL00, PSRAM_CLK_SRC_SEL,
                    ESP32_REG_VAL(HP_CLKRST_PSRAM_CLK_SRC, MPLL));
}

/* Encodes a divider into the three-counter form both clock registers use.  A
   divider of 1 is a special case with its own bit. */
static u32 ESP32_IRAM_FUNC
_Esp32_PSRAM_ClockDividerBits(u32 nDivider) {
    if (nDivider <= 1) return ESP32_REG_MASK(PSRAM_CLK, EQU_SYSCLK);
    return ((nDivider - 1)     << ESP32_REG_SHIFT(PSRAM_CLK, CLKCNT_N))
         | ((nDivider / 2 - 1) << ESP32_REG_SHIFT(PSRAM_CLK, CLKCNT_H))
         | ((nDivider - 1)     << ESP32_REG_SHIFT(PSRAM_CLK, CLKCNT_L));
}

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_SetBusClock(u32 nDivider) {
    u32 nBits = _Esp32_PSRAM_ClockDividerBits(nDivider);
    ESP32_REG(PSRAM_SRAM_CLK) = nBits;
    ESP32_REG(PSRAM_USER_CLK) = nBits;
}

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_EnableDLL(void) {
    /* Both bits live in PSRAM0 even though one of them belongs to the user mode
     * controller - they are two different registers, not the same register in
     * two blocks, and that asymmetry is easy to get wrong. */
    ESP32_REG(PSRAM_SMEM_TIMING_CALI) |= ESP32_REG_MASK(PSRAM_TIMING_CALI, DLL);
    ESP32_REG(PSRAM_TIMING_CALI)      |= ESP32_REG_MASK(PSRAM_TIMING_CALI, DLL);
}

/******************************************************************************
 * pads and chip select timing
 *****************************************************************************/

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_InitPads(void) {
    /* The twenty PSRAM pads are dedicated - there is no matrix select to write
     * and no GPIO number to claim - so all that is configurable is drive
     * strength, and the words run consecutively so they can be walked. */
    for (u32 nAddr = kEsp32_RegisterMSPI_IOMUX_PAD_FIRST;
         nAddr <= kEsp32_RegisterMSPI_IOMUX_PAD_LAST;
         nAddr += 4) {
        if (nAddr == kEsp32_RegisterMSPI_IOMUX_PAD_DQS0 || nAddr == kEsp32_RegisterMSPI_IOMUX_PAD_DQS1) {
            PSRAM_SET_FIELD(ESP32_MSPI_PAD_REG(nAddr), MSPI_IOMUX_DQS, DRV, kMSPI_PadDriveStrength);
        } else {
            PSRAM_SET_FIELD(ESP32_MSPI_PAD_REG(nAddr), MSPI_IOMUX_PAD, DRV, kMSPI_PadDriveStrength);
        }
    }

    /* Power up both strobe receivers.  Without them a DDR read returns nothing,
     * because the data is clocked in against DQS rather than the bus clock. */
    ESP32_MSPI_PAD_REG(kEsp32_RegisterMSPI_IOMUX_PAD_DQS0) |= ESP32_REG_MASK(MSPI_IOMUX_DQS, XPD);
    ESP32_MSPI_PAD_REG(kEsp32_RegisterMSPI_IOMUX_PAD_DQS1) |= ESP32_REG_MASK(MSPI_IOMUX_DQS, XPD);
}

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_SetCsTiming(void) {
    volatile u32 * pAc = ESP32_REG_ADDR(PSRAM_SMEM_AC);

    *pAc |= ESP32_REG_MASK(PSRAM_SMEM_AC, CS_SETUP)
          | ESP32_REG_MASK(PSRAM_SMEM_AC, CS_HOLD);
    PSRAM_SET_FIELD(*pAc, PSRAM_SMEM_AC, CS_SETUP_TIME, kHexPSRAM_CsSetupTime - 1);
    PSRAM_SET_FIELD(*pAc, PSRAM_SMEM_AC, CS_HOLD_TIME,  kHexPSRAM_CsHoldTime - 1);
    PSRAM_SET_FIELD(*pAc, PSRAM_SMEM_AC, CS_HOLD_DELAY, kHexPSRAM_CsHoldDelay - 1);
}

/******************************************************************************
 * PSRAM mode registers
 *
 * Driven over the user mode controller.  The ROM does not export
 * esp_rom_opiflash_exec_cmd() on this part - rom/opi_flash.h declares it, but
 * esp32p4.rom.ld does not PROVIDE it - so the transaction is assembled from the
 * three entry points that do exist, exactly as IDF's
 * psram_ctrlr_ll_common_transaction() does.
 *****************************************************************************/

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_Transaction(u32 nCmd, u32 nAddr, u32 nDummyBits,
                         u8 * pMosiData, u32 nMosiBitLen,
                         u8 * pMisoData, u32 nMisoBitLen) {
    /* The transaction copies the controller's data buffer out whether or not
     * the part drove the bus, so a read of a silent part returns whatever the
     * last transaction left behind.  IDF zeroes the buffer before every PSRAM
     * read for this reason; without it a stale write value is
     * indistinguishable from a reply. */
    if (nMisoBitLen != 0) {
        for (u32 nIx = 0; nIx < kEsp32_RegisterPSRAM_USER_NUM_DATA_WORDS; nIx++) {
            ESP32_REG_ARRAY_VALUE(PSRAM_USER_W0, nIx) = 0;
        }
    }

    /* nAddr is taken by address, so it has to stay live across cmd_start. */
    u32 nAddrValue = nAddr;
    Esp32_RomSpiCmd cmd = {
        .nCmd           = (u16)nCmd,
        .nCmdBitLen     = kHexPSRAM_CmdBitLen,
        .pAddr          = &nAddrValue,
        .nAddrBitLen    = kHexPSRAM_AddrBitLen,
        .pTxData        = (u32 *)pMosiData,
        .nTxDataBitLen  = nMosiBitLen,
        .pRxData        = (u32 *)pMisoData,
        .nRxDataBitLen  = nMisoBitLen,
        .nDummyBitLen   = nDummyBits,
    };

    esp_rom_spi_set_op_mode(kMSPI_User, kRomSpiFlashMode_OpiDtr);
    esp_rom_spi_cmd_config(kMSPI_User, &cmd);
    esp_rom_spi_cmd_start(kMSPI_User, pMisoData, (u16)(nMisoBitLen / 8), kCsMask_PSRAM, false);
}

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_ReadModeRegs(u32 nAddr, u8 * pOut, u32 nBitLen) {
    _Esp32_PSRAM_Transaction(kHexPSRAM_Cmd_RegRead, nAddr, kHexPSRAM_RdDummyBitLen,
                             NULL, 0, pOut, nBitLen);
}

/* Writes are always 16 bits wide because that is the transfer granularity; the
   part takes only as many registers as the address range covers. */
static void ESP32_IRAM_FUNC
_Esp32_PSRAM_WriteModeRegs(u32 nAddr, u8 * pIn) {
    _Esp32_PSRAM_Transaction(kHexPSRAM_Cmd_RegWrite, nAddr, 0, pIn, 16, NULL, 0);
}

/* Programs latency, drive strength, burst and bus width.  Read-modify-write
   throughout, because the reserved bits of each register must be preserved. */
static void ESP32_IRAM_FUNC
_Esp32_PSRAM_InitModeRegs(void) {
    u8 nPair[2] LT_ALIGNED(4) = { 0, 0 };

    /* MR0 and MR1 come back together; only MR0 is changed. */
    _Esp32_PSRAM_ReadModeRegs(kHexPSRAM_MR0, nPair, 16);
    nPair[0] &= (u8)~0x3f;      /* drive strength, read latency and LT */
    nPair[0] |= (u8)((kHexPSRAM_Set_DriveStrength       & 0x03) << 0);
    nPair[0] |= (u8)((kHexPSRAM_Set_ReadLatency         & 0x07) << 2);
    nPair[0] |= (u8)((kHexPSRAM_Set_LatencyType_Fixed   & 0x01) << 5);
    _Esp32_PSRAM_WriteModeRegs(kHexPSRAM_MR0, nPair);

    /* MR4 and MR8 come back together; only MR4 is changed here, because MR8
     * needs the value the part reports at its own address to be preserved. */
    _Esp32_PSRAM_ReadModeRegs(kHexPSRAM_MR4, nPair, 16);
    nPair[0] &= (u8)~0xe0;      /* write latency */
    nPair[0] |= (u8)((kHexPSRAM_Set_WriteLatency & 0x07) << 5);
    _Esp32_PSRAM_WriteModeRegs(kHexPSRAM_MR4, nPair);

    _Esp32_PSRAM_ReadModeRegs(kHexPSRAM_MR8, nPair, 8);
    nPair[0] &= (u8)~0x4f;      /* burst length and type, RBX, X16 */
    nPair[0] |= (u8)((kHexPSRAM_Set_BurstLength      & 0x03) << 0);
    nPair[0] |= (u8)((kHexPSRAM_Set_BurstType_Linear & 0x01) << 2);
    nPair[0] |= (u8)((kHexPSRAM_Set_RowBoundaryCross & 0x01) << 3);
    nPair[0] |= (u8)((kHexPSRAM_Set_X16              & 0x01) << 6);
    _Esp32_PSRAM_WriteModeRegs(kHexPSRAM_MR8, nPair);
}

/* Fills s_modeReg with everything the info dump and the size decode need. */
static void ESP32_IRAM_FUNC
_Esp32_PSRAM_ReadAllModeRegs(void) {
    _Esp32_PSRAM_ReadModeRegs(kHexPSRAM_MR0, &s_modeReg[kHexPSRAM_MR0], 16);   /* MR0, MR1 */
    _Esp32_PSRAM_ReadModeRegs(kHexPSRAM_MR2, &s_modeReg[kHexPSRAM_MR2], 16);   /* MR2, MR3 */
    _Esp32_PSRAM_ReadModeRegs(kHexPSRAM_MR4, &s_modeReg[kHexPSRAM_MR4], 8);
    _Esp32_PSRAM_ReadModeRegs(kHexPSRAM_MR8, &s_modeReg[kHexPSRAM_MR8], 8);
}

/* Bytes of array, or 0 if the density code is one this driver does not know. */
static u32 ESP32_IRAM_FUNC
_Esp32_PSRAM_DecodeSize(void) {
    switch (PSRAM_MR2_DENSITY(s_modeReg[kHexPSRAM_MR2])) {
        case kHexPSRAM_Density_32Mbit:  return  4 * 1024 * 1024;
        case kHexPSRAM_Density_64Mbit:  return  8 * 1024 * 1024;
        case kHexPSRAM_Density_128Mbit: return 16 * 1024 * 1024;
        case kHexPSRAM_Density_256Mbit: return 32 * 1024 * 1024;
        case kHexPSRAM_Density_512Mbit: return 64 * 1024 * 1024;
        default:                        return 0;
    }
}

/******************************************************************************
 * cache access configuration
 *
 * Everything the cache side controller needs to know to fetch and write back a
 * cache line over a hex DDR bus: which opcodes, how wide the address is, how
 * many dummy cycles, and how many lines each phase goes out on.
 *****************************************************************************/

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_ConfigCachePhases(void) {
    volatile u32 * pSctrl = ESP32_REG_ADDR(PSRAM_CACHE_SCTRL);

    /* Write command phase */
    *pSctrl |= ESP32_REG_MASK(PSRAM_CACHE_SCTRL, USR_WCMD);
    PSRAM_SET_FIELD(ESP32_REG(PSRAM_SRAM_DWR_CMD), PSRAM_SRAM_CMD, BITLEN, kHexPSRAM_CmdBitLen - 1);
    PSRAM_SET_FIELD(ESP32_REG(PSRAM_SRAM_DWR_CMD), PSRAM_SRAM_CMD, VALUE,  kHexPSRAM_Cmd_SyncWrite);

    /* Read command phase */
    *pSctrl |= ESP32_REG_MASK(PSRAM_CACHE_SCTRL, USR_RCMD);
    PSRAM_SET_FIELD(ESP32_REG(PSRAM_SRAM_DRD_CMD), PSRAM_SRAM_CMD, BITLEN, kHexPSRAM_CmdBitLen - 1);
    PSRAM_SET_FIELD(ESP32_REG(PSRAM_SRAM_DRD_CMD), PSRAM_SRAM_CMD, VALUE,  kHexPSRAM_Cmd_SyncRead);

    /* Address phase - 32 bits, and the four-byte address flag that goes with it */
    PSRAM_SET_FIELD(*pSctrl, PSRAM_CACHE_SCTRL, ADDR_BITLEN, kHexPSRAM_AddrBitLen - 1);
    *pSctrl |= ESP32_REG_MASK(PSRAM_CACHE_SCTRL, USR_SADDR_4BYTE);

    /* Dummy phases.  The variable dummy bit lets the part shorten the read
     * latency when it is not mid-refresh; the fixed latency programmed into MR0
     * is the worst case. */
    *pSctrl |= ESP32_REG_MASK(PSRAM_CACHE_SCTRL, USR_RD_DUMMY)
             | ESP32_REG_MASK(PSRAM_CACHE_SCTRL, USR_WR_DUMMY);
    PSRAM_SET_FIELD(*pSctrl, PSRAM_CACHE_SCTRL, RDUMMY_CYCLELEN, kHexPSRAM_RdDummyBitLen - 1);
    PSRAM_SET_FIELD(*pSctrl, PSRAM_CACHE_SCTRL, WDUMMY_CYCLELEN, kHexPSRAM_WrDummyBitLen - 1);
    ESP32_REG(PSRAM_SRAM_CMD) |= ESP32_REG_MASK(PSRAM_SRAM_CMD, SDUMMY_WOUT);

    /* Double data rate, with the FIFO halves in their natural order */
    ESP32_REG(PSRAM_SMEM_DDR) &= ~(ESP32_REG_MASK(PSRAM_SMEM_DDR, WDAT_SWP)
                                 | ESP32_REG_MASK(PSRAM_SMEM_DDR, RDAT_SWP));
    ESP32_REG(PSRAM_SMEM_DDR) |= ESP32_REG_MASK(PSRAM_SMEM_DDR, EN);

    /* Eight lines for command and address, sixteen for data */
    ESP32_REG(PSRAM_SRAM_CMD) |= ESP32_REG_MASK(PSRAM_SRAM_CMD, SCMD_OCT)
                               | ESP32_REG_MASK(PSRAM_SRAM_CMD, SADDR_OCT)
                               | ESP32_REG_MASK(PSRAM_SRAM_CMD, SDOUT_OCT)
                               | ESP32_REG_MASK(PSRAM_SRAM_CMD, SDIN_OCT)
                               | ESP32_REG_MASK(PSRAM_SRAM_CMD, SDOUT_HEX)
                               | ESP32_REG_MASK(PSRAM_SRAM_CMD, SDIN_HEX);
    *pSctrl |= ESP32_REG_MASK(PSRAM_CACHE_SCTRL, SRAM_OCT);

    /* Open the AXI interface the cache reaches this controller through, and let
     * it merge adjacent bursts in both directions. */
    ESP32_REG(PSRAM_CACHE_FCTRL) |=  ESP32_REG_MASK(PSRAM_CACHE_FCTRL, AXI_REQ_EN);
    ESP32_REG(PSRAM_CACHE_FCTRL) &= ~(u32)ESP32_REG_MASK(PSRAM_CACHE_FCTRL, CLOSE_AXI_INF_EN);
    ESP32_REG(PSRAM_CTRL1) |= ESP32_REG_MASK(PSRAM_CTRL1, AW_SPLICE_EN)
                            | ESP32_REG_MASK(PSRAM_CTRL1, AR_SPLICE_EN);
}

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_EnableVariableDummy(void) {
    ESP32_REG(PSRAM_SMEM_DDR) |= ESP32_REG_MASK(PSRAM_SMEM_DDR, VAR_DUMMY);
    ESP32_REG(PSRAM_USER_DDR) |= ESP32_REG_MASK(PSRAM_USER_DDR, VAR_DUMMY);
}

/******************************************************************************
 * diagnostics
 *
 * Printed with esp_rom_printf rather than the LT log, because this runs inside
 * LTCoreBSP_Initialize, long before logging exists.
 *****************************************************************************/

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_PrintInfo(void) {
    u8 nMr0 = s_modeReg[kHexPSRAM_MR0];
    u8 nMr2 = s_modeReg[kHexPSRAM_MR2];
    u8 nMr3 = s_modeReg[kHexPSRAM_MR3];
    u8 nMr8 = s_modeReg[kHexPSRAM_MR8];

    esp_rom_printf("psram: vendor 0x%02x, generation %d, %s die, %s refresh\n",
                   PSRAM_MR1_VENDOR_ID(s_modeReg[kHexPSRAM_MR1]),
                   PSRAM_MR2_DEV_ID(nMr2) + 1,
                   PSRAM_MR2_KGD(nMr2) == kHexPSRAM_KgdPass ? "good" : "FAILED",
                   PSRAM_MR3_SRF(nMr3) == 1 ? "fast" : "slow");
    esp_rom_printf("psram: %s latency %d cycles, drive %d ohm, %s bus, burst %s\n",
                   PSRAM_MR0_LT(nMr0) ? "fixed" : "variable",
                   PSRAM_MR0_READ_LATENCY(nMr0) * 2 + 6,
                   PSRAM_MR0_DRIVE_STR(nMr0) < 2 ? 25 * (PSRAM_MR0_DRIVE_STR(nMr0) + 1)
                                                 : 100 * (PSRAM_MR0_DRIVE_STR(nMr0) - 1),
                   PSRAM_MR8_X16(nMr8) ? "x16" : "x8",
                   PSRAM_MR8_BT(nMr8) && (PSRAM_MR8_BL(nMr8) != 3) ? "hybrid wrap" : "linear");
}

/******************************************************************************
 * public interface
 *****************************************************************************/

bool ESP32_MEM_REGION(IRAM)
Esp32_PSRAM_Initialize(Esp32_PSRAM_Info * pInfo) {

    pInfo->pBase        = NULL;
    pInfo->nSizeInBytes = 0;

    _Esp32_PSRAM_StartLDO();

    if (! _Esp32_PSRAM_StartMPLL()) {
        esp_rom_printf("psram: MPLL did not lock, PSRAM disabled\n");
        return false;
    }

    _Esp32_PSRAM_StartModuleClock();

    _Esp32_PSRAM_InitPads();
    _Esp32_PSRAM_SetCsTiming();

    /* 400MHz MPLL divided by 20 - the module clock this driver runs at, and the
     * clock the die's mode registers are written at too.  IDF uses one speed
     * for both, so there is no fast and slow phase to switch between. */
    _Esp32_PSRAM_SetBusClock(kPSRAM_ClockDivider);
    _Esp32_PSRAM_EnableDLL();

    _Esp32_PSRAM_InitModeRegs();
    _Esp32_PSRAM_ReadAllModeRegs();

    if (PSRAM_MR1_VENDOR_ID(s_modeReg[kHexPSRAM_MR1]) != kHexPSRAM_VendorId_APMemory) {
        esp_rom_printf("psram: vendor id 0x%02x not recognised, no part fitted or wrong line mode, PSRAM disabled\n",
                       PSRAM_MR1_VENDOR_ID(s_modeReg[kHexPSRAM_MR1]));
        return false;
    }

    u32 nSize = _Esp32_PSRAM_DecodeSize();
    if (nSize == 0) {
        esp_rom_printf("psram: density code 0x%02x not recognised, PSRAM disabled\n",
                       PSRAM_MR2_DENSITY(s_modeReg[kHexPSRAM_MR2]));
        return false;
    }

    _Esp32_PSRAM_PrintInfo();

    _Esp32_PSRAM_ConfigCachePhases();
    _Esp32_PSRAM_EnableVariableDummy();

    /* The window is 64MB wide, so a larger part would be mapped from its start
     * and the tail simply not reachable.  No part this large exists today. */
    if (nSize > (u32)(kExtRam_VAddrHigh - kExtRam_VAddrLow)) {
        esp_rom_printf("psram: part is %u MB, mapping the low %u MB\n",
                       nSize / (1024 * 1024),
                       (u32)(kExtRam_VAddrHigh - kExtRam_VAddrLow) / (1024 * 1024));
        nSize = (u32)(kExtRam_VAddrHigh - kExtRam_VAddrLow);
    }

    /* PSRAM keeps an MMU table of its own on this part, so mapping it from the
     * base of its window cannot disturb the flash mapping the bootloader left.
     * The L2 cache is suspended anyway, because it is the cache that reaches
     * external memory and its in-flight transactions would see a half-written
     * table. */
    u32 nAutoload = Cache_Suspend_L2_Cache();

    Cache_PSRAM_MMU_Init();
    int nResult = Cache_PSRAM_MMU_Set(kMMU_NotSensitive, kExtRam_VAddrLow, 0,
                                      kMMU_PageSizeKB, nSize / kMMU_PageSizeBytes, 0);

    Cache_Resume_L2_Cache(nAutoload);

    if (nResult != 0) {
        esp_rom_printf("psram: MMU refused the mapping (%d), PSRAM disabled\n", nResult);
        return false;
    }

    esp_rom_printf("psram: %u MB mapped at 0x%08x, %dMHz DDR hex\n",
                   nSize / (1024 * 1024), (u32)kExtRam_VAddrLow, kPSRAM_ModuleClockMHz);

    pInfo->pBase        = (u8 *)kExtRam_VAddrLow;
    pInfo->nSizeInBytes = nSize;
    return true;
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  28-Sep-26   claudius    created
 */
