/******************************************************************************
 * Esp32_PSRAM.c                                                   ESP32-S3 BSP
 *
 * Detection, configuration and memory mapping of external octal SPI PSRAM.
 *
 * This now runs on hardware - an ESP32-S3R8, meaning an 8MB octal APMemory die
 * at 3.3V.  The header note that used to sit here, warning that the s3 arm
 * could not master a firmware image and that nothing below had ever been
 * compiled, is obsolete.
 *
 * This is a hand-written, LT-native equivalent of IDF's
 * components/esp_hw_support/port/esp32s3/opiram_psram.c, plus the parts of
 * components/spi_flash/spi_flash_timing_tuning.c and
 * components/spi_flash/esp32s3/spi_timing_config.c that it leans on.  It is
 * not a vendored copy: those files pull in most of IDF's soc/, hal/ and
 * driver/ trees, and the app-side include path here carries none of them -
 * only the hand-written Esp32_*.h headers.
 *
 * How PSRAM works on this part, in short.  The PSRAM die sits on the same MSPI
 * bus as the flash, distinguished only by chip select: flash on CS0, PSRAM on
 * CS1.  Two peripherals see that bus.  SPI0 is the cache controller - it
 * issues reads and writes on the CPU's behalf whenever an access lands in a
 * mapped window, and is never programmed a transaction at a time.  SPI1 is a
 * conventional user-mode controller sharing the same pads.  Bringing PSRAM up
 * means talking to the part over SPI1 to read and rewrite its mode registers,
 * then programming SPI0 with the opcodes, line width and timing to use for
 * cache traffic, then pointing the data MMU at it.
 *
 * Two things differ structurally from the esp32 arm of this driver:
 *
 *   - The part is octal DDR, not quad SDR, and its identity lives in mode
 *     registers reached with 16-bit opcodes rather than in a 0x9f JEDEC read.
 *     The ROM's esp_rom_opiflash_exec_cmd() drives those transactions, so the
 *     hand-rolled SPI1 user-mode sequencer the esp32 arm needs is not repeated
 *     here.
 *
 *   - PSRAM has no dedicated address window.  Flash rodata and PSRAM are both
 *     reached through the DRAM0 cache bus, and the MMU decides page by page
 *     which device a virtual page targets.  Following IDF, PSRAM is mapped
 *     against the top of the external-RAM span and the mapping is refused if
 *     it would reach down into the pages flash rodata already occupies.  That
 *     is why Esp32_PSRAM_Info carries a base address at all.
 *
 * Scope of this implementation, and why:
 *
 *   - The PSRAM module clock is fixed at 40MHz DDR (80MB/s), which is the
 *     fastest setting that needs no timing tuning: IDF tunes octal PSRAM only
 *     above 40MHz (SPI_TIMING_PSRAM_NEEDS_TUNING in
 *     spi_flash/esp32s3/spi_timing_config.h).  Tuning would mean vendoring the
 *     reference-pattern search, its per-core-clock configuration tables and
 *     the cache freeze plumbing - several hundred lines - to buy a 2x PSRAM
 *     bandwidth improvement that nothing in LT is asking for yet.  Going to
 *     80MHz DDR later is a contained change: add the tuning pass and raise
 *     kPSRAM_ModuleClockMHz.
 *
 *   - Consequently the MSPI core clock is left exactly as the bootloader set
 *     it, and must be 80MHz.  At 80MHz core the flash divider is 1 (80MHz SDR,
 *     which also needs no tuning) and the PSRAM divider is 2 (40MHz DDR).  If
 *     some future bootloader change raises the core clock, this refuses to
 *     bring PSRAM up rather than guess - see _Esp32_PSRAM_CheckCoreClock().
 *
 *   - Quad PSRAM parts are not supported.  The ESP32-S3R8 this targets is
 *     octal; a quad part would need the whole spiram_psram.c command
 *     sequencer, which is the esp32 arm's shape, not this one.
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
#include "Esp32_Cache.h"
#include "Esp32_PSRAM.h"

/******************************************************************************
 * ROM entry points
 *
 * Declared here rather than in a header, following the pattern Esp32_SoC.h
 * uses for esp_rom_printf().  All of these are PROVIDEd by
 * mastering/ld/esp32s3/rom/esp32s3.rom.ld.
 *****************************************************************************/

/* Executes one MSPI transaction in user mode.  nMode selects the line width
   and data rate; kRomSpiFlashMode_OpiDtr below is the only one used here.
   nCsMask picks the chip select - bit 1 is CS1, the external RAM. */
void esp_rom_opiflash_exec_cmd(int nSpiNum, int nMode,
                               u32 nCmd, int nCmdBitLen,
                               u32 nAddr, int nAddrBitLen,
                               int nDummyBits,
                               u8 * pMosiData, int nMosiBitLen,
                               u8 * pMisoData, int nMisoBitLen,
                               u32 nCsMask,
                               bool bIsWriteEraseOperation);

/* Selects whether the FIFO halves are swapped for DDR reads and writes. */
void esp_rom_spi_set_dtr_swap_mode(int nSpiNum, bool bWriteSwap, bool bReadSwap);

/* Routes the full octal MSPI pad set - D4 to D7 and DQS as well as the four
   pads a quad part needs - to their MSPI functions.  See _Esp32_PSRAM_InitPins()
   for why this is needed here even though the flash is quad. */
void esp_rom_opiflash_pin_config(void);

/* Programs the data bus MMU.  nAccess selects the target device
   (kMMU_Access_SPIRAM here), nVirtAddr and nPhysAddr are byte addresses,
   nPageSizeKB is 64, and nFixed maps every page to nPhysAddr when non-zero.
   Returns 0 on success. */
int Cache_Dbus_MMU_Set(u32 nAccess, u32 nVirtAddr, u32 nPhysAddr,
                       u32 nPageSizeKB, u32 nNumPages, u32 nFixed);

/******************************************************************************
 * constants
 *****************************************************************************/

/* esp_rom_spiflash_read_mode_t, from esp_rom/include/esp32s3/rom/spi_flash.h */
enum {
    kRomSpiFlashMode_OpiDtr             = 7,
};

/* Octal PSRAM opcodes.  Each is duplicated into both bytes because the part
   samples the command on both clock edges. */
enum {
    kOpiPSRAM_Cmd_SyncRead              = 0x0000,
    kOpiPSRAM_Cmd_SyncWrite             = 0x8080,
    kOpiPSRAM_Cmd_RegRead               = 0x4040,
    kOpiPSRAM_Cmd_RegWrite              = 0xc0c0,

    kOpiPSRAM_CmdBitLen                 = 16,
    kOpiPSRAM_AddrBitLen                = 32,

    /* Read latency 10 and write latency 5, doubled because DDR counts both
     * edges, less one because the hardware field is "cycles - 1". */
    kOpiPSRAM_RdDummyBitLen             = 2 * (10 - 1),
    kOpiPSRAM_WrDummyBitLen             = 2 * (5 - 1),
};

/* Mode register indexes, and the fields this driver reads or writes.  The
 * registers are a byte each; s_modeReg below holds them indexed by number. */
enum {
    kOpiPSRAM_MR0                       = 0,
    kOpiPSRAM_MR1                       = 1,
    kOpiPSRAM_MR2                       = 2,
    kOpiPSRAM_MR3                       = 3,
    kOpiPSRAM_MR4                       = 4,
    kOpiPSRAM_MR8                       = 8,
    kOpiPSRAM_NumModeRegs               = 9,
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
#define PSRAM_MR2_GB(v)                 (((v) >> 7) & 0x01)
/* MR3 - self refresh rate, supply voltage */
#define PSRAM_MR3_SRF(v)                (((v) >> 5) & 0x01)
#define PSRAM_MR3_VCC(v)                (((v) >> 6) & 0x01)
/* MR8 - burst length and type */
#define PSRAM_MR8_BL(v)                 (((v) >> 0) & 0x03)
#define PSRAM_MR8_BT(v)                 (((v) >> 2) & 0x01)

enum {
    /* Only APMemory parts are qualified for this package. */
    kOpiPSRAM_VendorId_APMemory         = 0x0d,

    /* MR2 density encodings.  The board fitted here reports 0x3. */
    kOpiPSRAM_Density_32Mbit            = 0x1,
    kOpiPSRAM_Density_64Mbit            = 0x3,
    kOpiPSRAM_Density_128Mbit           = 0x5,
    kOpiPSRAM_Density_256Mbit           = 0x7,
};

/* The mode register values this driver programs.  Fixed latency at 2 (which
 * MR0 encodes as read latency 2*2+6 = 10 cycles, matching
 * kOpiPSRAM_RdDummyBitLen) and maximum drive strength. */
enum {
    kOpiPSRAM_Set_LatencyType_Fixed     = 1,
    kOpiPSRAM_Set_ReadLatency           = 2,
    kOpiPSRAM_Set_DriveStrength         = 0,
};

/* Chip select timing, in MSPI core clocks. */
enum {
    kOpiPSRAM_CsSetupTime               = 3,
    kOpiPSRAM_CsHoldTime                = 3,
    kOpiPSRAM_CsHoldDelay               = 2,
};

/* MSPI ports.  SPI0 drives the cache, SPI1 drives user-mode transactions. */
enum {
    kSpiPort_Cache                      = 0,
    kSpiPort_User                       = 1,
};

/* Chip select mask for esp_rom_opiflash_exec_cmd - bit 1 is CS1. */
enum {
    kCsMask_PSRAM                       = 0x02,

    /* The MSPI data buffer, W0 to W15, which a transaction reads from and
     * writes back into. */
    kMSPI_NumDataBufferWords            = 16,
};

/* Pads.  The first stage bootloader routed only what a quad flash part needs:
 * CLK, CS0 and D0 to D3.  CS1 and the upper half of the octal bus are left to
 * whoever wants them - see _Esp32_PSRAM_InitPins(). */
enum {
    kPin_PsramCs                        = 26,
    kIOMuxFunc_SPICS1                   = 0,
    kPadDriveStrength_Max               = 3,
};

/* The MSPI data and strobe pads: D0 to D7 followed by DQS.  These are the pads
 * IDF gives maximum drive to in spi_timing_set_pin_drive_strength(). */
static const u8 s_mspiDataPins[] = { 27, 28, 31, 32, 33, 34, 35, 36, 37 };

/* Clocking.  See the scope note at the top of the file for why these are
 * fixed rather than derived from a build option. */
enum {
    kMSPI_CoreClockSel_80M              = 0,
    kMSPI_CoreClockMHz                  = 80,
    kPSRAM_ModuleClockMHz               = 40,

    /* While the mode registers are being written both flash and PSRAM run at
     * core/4 = 20MHz, which every part tolerates without tuning. */
    kMSPI_LowSpeedDivider               = 4,
};

/* Data MMU. */
enum {
    kMMU_Access_SPIRAM                  = 0x8000,
    kMMU_PageSizeKB                     = 64,
    kMMU_PageSizeBytes                  = kMMU_PageSizeKB * 1024,
};

/* The span of the DRAM0 cache bus reserved for external RAM: 0x3D000000 to
 * 0x3E000000, 16MB.  Flash rodata is mapped from 0x3C000000 upwards through
 * the same bus, so anything mapped here has to stay clear of it. */
enum {
    kExtRam_VAddrLow                    = 0x3d000000,
    kExtRam_VAddrHigh                   = 0x3e000000,
};

/* End of the flash rodata that occupies MMU pages, from
 * mastering/ld/esp32s3/sections.ld. */
extern int _rodata_reserved_end;

/******************************************************************************
 * register access
 *****************************************************************************/

#define PSRAM_MSPI_REG(port, r)         (*(volatile u32 *)(((port) == kSpiPort_Cache ? ESP32_REG_BASE(SPI0)         \
                                                                                     : ESP32_REG_BASE(SPI1))        \
                                                           + kEsp32_RegisterSPIMEM_ ## r))

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

static u8  s_modeReg[kOpiPSRAM_NumModeRegs];

/******************************************************************************
 * MSPI clocking
 *
 * The flash and PSRAM module clocks are both divided down from a shared MSPI
 * core clock.  Only the dividers are touched here; the core clock itself is
 * whatever the bootloader left, and is verified rather than set.
 *****************************************************************************/

/* Encodes a divider into the three-counter form both clock registers use.  A
   divider of 1 is a special case with its own bit. */
static u32 ESP32_IRAM_FUNC
_Esp32_PSRAM_ClockDividerBits(u32 nDivider, u32 nEquSysclkMask, u32 nShiftN, u32 nShiftH, u32 nShiftL) {
    if (nDivider <= 1) return nEquSysclkMask;
    return ((nDivider - 1) << nShiftN)
         | ((nDivider / 2 - 1) << nShiftH)
         | ((nDivider - 1) << nShiftL);
}

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_SetFlashClockDivider(int nPort, u32 nDivider) {
    u32 nBits = _Esp32_PSRAM_ClockDividerBits(nDivider,
                                              ESP32_REG_MASK(SPIMEM_CLOCK, CLK_EQU_SYSCLK),
                                              ESP32_REG_SHIFT(SPIMEM_CLOCK, CLKCNT_N),
                                              ESP32_REG_SHIFT(SPIMEM_CLOCK, CLKCNT_H),
                                              ESP32_REG_SHIFT(SPIMEM_CLOCK, CLKCNT_L));
    if (nPort == kSpiPort_Cache) PSRAM_MSPI_REG(kSpiPort_Cache, CLOCK) = nBits;
    else                         PSRAM_MSPI_REG(kSpiPort_User,  CLOCK) = nBits;
}

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_SetPsramClockDivider(u32 nDivider) {
    /* Only SPI0 reaches external RAM, so there is no port to choose. */
    PSRAM_MSPI_REG(kSpiPort_Cache, SRAM_CLK) =
        _Esp32_PSRAM_ClockDividerBits(nDivider,
                                      ESP32_REG_MASK(SPIMEM_SRAM_CLK, SCLK_EQU_SYSCLK),
                                      ESP32_REG_SHIFT(SPIMEM_SRAM_CLK, SCLKCNT_N),
                                      ESP32_REG_SHIFT(SPIMEM_SRAM_CLK, SCLKCNT_H),
                                      ESP32_REG_SHIFT(SPIMEM_SRAM_CLK, SCLKCNT_L));
}

/* True if the core clock is the 80MHz this driver's fixed dividers assume. */
static bool ESP32_IRAM_FUNC
_Esp32_PSRAM_CheckCoreClock(void) {
    u32 nSel = (PSRAM_MSPI_REG(kSpiPort_Cache, CORE_CLK_SEL) & (u32)ESP32_REG_MASK(SPIMEM, CORE_CLK_SEL))
             >> ESP32_REG_SHIFT(SPIMEM, CORE_CLK_SEL);
    return nSel == kMSPI_CoreClockSel_80M;
}

/******************************************************************************
 * PSRAM mode registers
 *****************************************************************************/

/* Reads nBitLen bits of mode register space starting at nRegIndex.  The part
   auto-increments, so two registers come back per 16-bit read. */
static void ESP32_IRAM_FUNC
_Esp32_PSRAM_ReadModeRegs(u32 nRegIndex, u8 * pOut, int nBitLen) {
    /* esp_rom_opiflash_exec_cmd() copies the SPI1 data buffer out whether or
     * not the part drove the bus, so a read of a silent part returns whatever
     * the last transaction left behind.  IDF zeroes the buffer before every
     * PSRAM read for this reason (spi_timing_config.c, s_psram_read_data());
     * without it a stale write value is indistinguishable from a reply. */
    for (u32 nIx = 0; nIx < kMSPI_NumDataBufferWords; nIx++) {
        (&PSRAM_MSPI_REG(kSpiPort_User, W0))[nIx] = 0;
    }

    esp_rom_opiflash_exec_cmd(kSpiPort_User, kRomSpiFlashMode_OpiDtr,
                              kOpiPSRAM_Cmd_RegRead, kOpiPSRAM_CmdBitLen,
                              nRegIndex, kOpiPSRAM_AddrBitLen,
                              kOpiPSRAM_RdDummyBitLen,
                              NULL, 0,
                              pOut, nBitLen,
                              kCsMask_PSRAM,
                              false);
}

/* Sets the latency and drive strength fields of MR0.  Read-modify-write,
   because the reserved bits of MR0 must be preserved. */
static void ESP32_IRAM_FUNC
_Esp32_PSRAM_InitModeReg0(void) {
    u8 nPair[2] = { 0, 0 };     /* MR0 and MR1 come back together */

    _Esp32_PSRAM_ReadModeRegs(kOpiPSRAM_MR0, nPair, 16);

    nPair[0] &= (u8)~0x3f;      /* drive strength, read latency and LT */
    nPair[0] |= (u8)((kOpiPSRAM_Set_DriveStrength   & 0x03) << 0);
    nPair[0] |= (u8)((kOpiPSRAM_Set_ReadLatency     & 0x07) << 2);
    nPair[0] |= (u8)((kOpiPSRAM_Set_LatencyType_Fixed & 0x01) << 5);

    /* Only MR0 is written back; the write is 16 bits wide because that is the
     * transfer granularity, and the part ignores the second byte for a
     * single-register write at address 0. */
    esp_rom_opiflash_exec_cmd(kSpiPort_User, kRomSpiFlashMode_OpiDtr,
                              kOpiPSRAM_Cmd_RegWrite, kOpiPSRAM_CmdBitLen,
                              kOpiPSRAM_MR0, kOpiPSRAM_AddrBitLen,
                              0,
                              nPair, 16,
                              NULL, 0,
                              kCsMask_PSRAM,
                              false);
}

/* Fills s_modeReg with everything the info dump and the size decode need. */
static void ESP32_IRAM_FUNC
_Esp32_PSRAM_ReadAllModeRegs(void) {
    _Esp32_PSRAM_ReadModeRegs(kOpiPSRAM_MR0, &s_modeReg[kOpiPSRAM_MR0], 16);   /* MR0, MR1 */
    _Esp32_PSRAM_ReadModeRegs(kOpiPSRAM_MR2, &s_modeReg[kOpiPSRAM_MR2], 16);   /* MR2, MR3 */
    _Esp32_PSRAM_ReadModeRegs(kOpiPSRAM_MR4, &s_modeReg[kOpiPSRAM_MR4], 8);
    _Esp32_PSRAM_ReadModeRegs(kOpiPSRAM_MR8, &s_modeReg[kOpiPSRAM_MR8], 8);
}

/* Bytes of array, or 0 if the density code is one this driver does not know. */
static u32 ESP32_IRAM_FUNC
_Esp32_PSRAM_DecodeSize(void) {
    switch (PSRAM_MR2_DENSITY(s_modeReg[kOpiPSRAM_MR2])) {
        case kOpiPSRAM_Density_32Mbit:  return  4 * 1024 * 1024;
        case kOpiPSRAM_Density_64Mbit:  return  8 * 1024 * 1024;
        case kOpiPSRAM_Density_128Mbit: return 16 * 1024 * 1024;
        case kOpiPSRAM_Density_256Mbit: return 32 * 1024 * 1024;
        default:                        return 0;
    }
}

/******************************************************************************
 * pads and chip select timing
 *****************************************************************************/

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_InitPins(void) {
    /* Route the upper half of the octal bus.  This is the step that makes the
     * difference between a part that answers and one that does not, and it is
     * easy to miss: the flash on this board is quad, so the bootloader routed
     * only D0 to D3, and pads 33 to 37 come out of reset as plain GPIOs
     * (SPIIO4 is IO_MUX function 4 on GPIO33, not the default function 1).
     * An octal part is sent its command, address and data across all eight
     * lines at once, so with four of them unrouted the part never sees a
     * decodable command and never drives a reply - the symptom is a mode
     * register read that returns whatever the previous transaction left in
     * SPI1's data buffer.
     *
     * IDF does this from esp_mspi_pin_init() in cpu_start.c, well before it
     * touches PSRAM, and does it for octal PSRAM as much as for octal flash
     * (CONFIG_ESPTOOLPY_OCT_FLASH || CONFIG_SPIRAM_MODE_OCT).  LT has no
     * equivalent early hook, and nothing else on this part needs those pads,
     * so it belongs here. */
    esp_rom_opiflash_pin_config();

    /* CS1 is the other pad the bootloader leaves alone, because flash does not
     * use it.  Route it and drive it hard. */
    PSRAM_SET_FIELD(ESP32_REG(IO_MUX_SPICS1), IO_MUX, MCU_SEL, kIOMuxFunc_SPICS1);
    PSRAM_SET_FIELD(ESP32_REG(IO_MUX_SPICS1), IO_MUX, FUN_DRV, kPadDriveStrength_Max);

    /* Maximum drive on the data and strobe pads, matching IDF's
     * spi_timing_set_pin_drive_strength().  The pads just routed above come
     * out of reset at the default strength, which is not enough for DDR. */
    for (u32 nIx = 0; nIx < sizeof s_mspiDataPins / sizeof s_mspiDataPins[0]; nIx++) {
        PSRAM_SET_FIELD(ESP32_IO_MUX_PAD_REG(s_mspiDataPins[nIx]), IO_MUX, FUN_DRV, kPadDriveStrength_Max);
    }

    /* The SPI_CLK pad is shared with flash, so its drive strength cannot be
     * set through IO_MUX without affecting flash.  MSPI has a per-device
     * override for exactly this, selected by SPICLK_PAD_DRV_CTL_EN - which has
     * to be enabled for either FUN_DRV field below to take effect at all. */
    PSRAM_MSPI_REG(kSpiPort_Cache, DATE) |= ESP32_REG_MASK(SPIMEM_DATE, SPICLK_PAD_DRV_CTL_EN);
    PSRAM_SET_FIELD(PSRAM_MSPI_REG(kSpiPort_Cache, DATE), SPIMEM_DATE, SMEM_SPICLK_FUN_DRV, kPadDriveStrength_Max);
    PSRAM_SET_FIELD(PSRAM_MSPI_REG(kSpiPort_Cache, DATE), SPIMEM_DATE, FMEM_SPICLK_FUN_DRV, kPadDriveStrength_Max);
}

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_SetCsTiming(void) {
    /* SPI0 and SPI1 share these registers for external RAM, so only the SPI0
     * copy is written. */
    volatile u32 * pAc = &PSRAM_MSPI_REG(kSpiPort_Cache, SPI_SMEM_AC);

    *pAc |= ESP32_REG_MASK(SPIMEM_SPI_SMEM_AC, CS_HOLD)
          | ESP32_REG_MASK(SPIMEM_SPI_SMEM_AC, CS_SETUP);
    PSRAM_SET_FIELD(*pAc, SPIMEM_SPI_SMEM_AC, CS_HOLD_TIME,  kOpiPSRAM_CsHoldTime);
    PSRAM_SET_FIELD(*pAc, SPIMEM_SPI_SMEM_AC, CS_SETUP_TIME, kOpiPSRAM_CsSetupTime);
    PSRAM_SET_FIELD(*pAc, SPIMEM_SPI_SMEM_AC, CS_HOLD_DELAY, kOpiPSRAM_CsHoldDelay);
}

/******************************************************************************
 * SPI0 cache access configuration
 *
 * Everything SPI0 needs to know to fetch and write back a cache line over an
 * octal DDR bus: which opcodes, how wide the address is, how many dummy
 * cycles, and which phases go out on eight lines rather than one.
 *****************************************************************************/

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_ConfigCachePhases(void) {
    volatile u32 * pSctrl = &PSRAM_MSPI_REG(kSpiPort_Cache, CACHE_SCTRL);

    /* Write command phase */
    *pSctrl |= ESP32_REG_MASK(SPIMEM_CACHE_SCTRL, USR_WCMD);
    PSRAM_SET_FIELD(PSRAM_MSPI_REG(kSpiPort_Cache, SRAM_DWR_CMD), SPIMEM_SRAM_CMD, BITLEN, kOpiPSRAM_CmdBitLen - 1);
    PSRAM_SET_FIELD(PSRAM_MSPI_REG(kSpiPort_Cache, SRAM_DWR_CMD), SPIMEM_SRAM_CMD, VALUE,  kOpiPSRAM_Cmd_SyncWrite);

    /* Read command phase */
    *pSctrl |= ESP32_REG_MASK(SPIMEM_CACHE_SCTRL, USR_RCMD);
    PSRAM_SET_FIELD(PSRAM_MSPI_REG(kSpiPort_Cache, SRAM_DRD_CMD), SPIMEM_SRAM_CMD, BITLEN, kOpiPSRAM_CmdBitLen - 1);
    PSRAM_SET_FIELD(PSRAM_MSPI_REG(kSpiPort_Cache, SRAM_DRD_CMD), SPIMEM_SRAM_CMD, VALUE,  kOpiPSRAM_Cmd_SyncRead);

    /* Address phase - 32 bits, and the four-byte command flag that goes with it */
    PSRAM_SET_FIELD(*pSctrl, SPIMEM_CACHE_SCTRL, ADDR_BITLEN, kOpiPSRAM_AddrBitLen - 1);
    *pSctrl |= ESP32_REG_MASK(SPIMEM_CACHE_SCTRL, USR_SCMD_4BYTE);

    /* Dummy phases.  The variable dummy bit lets the part shorten the read
     * latency when it is not mid-refresh; the fixed latency programmed into
     * MR0 is the worst case. */
    *pSctrl |= ESP32_REG_MASK(SPIMEM_CACHE_SCTRL, USR_RD_DUMMY)
             | ESP32_REG_MASK(SPIMEM_CACHE_SCTRL, USR_WR_DUMMY);
    PSRAM_SET_FIELD(*pSctrl, SPIMEM_CACHE_SCTRL, RDUMMY_CYCLELEN, kOpiPSRAM_RdDummyBitLen - 1);
    PSRAM_SET_FIELD(*pSctrl, SPIMEM_CACHE_SCTRL, WDUMMY_CYCLELEN, kOpiPSRAM_WrDummyBitLen - 1);
    PSRAM_MSPI_REG(kSpiPort_Cache, SPI_SMEM_DDR) |= ESP32_REG_MASK(SPIMEM_SPI_SMEM_DDR, VAR_DUMMY);

    /* Double data rate, with the FIFO halves in their natural order */
    PSRAM_MSPI_REG(kSpiPort_Cache, SPI_SMEM_DDR) &= ~(ESP32_REG_MASK(SPIMEM_SPI_SMEM_DDR, WDAT_SWP)
                                                    | ESP32_REG_MASK(SPIMEM_SPI_SMEM_DDR, RDAT_SWP));
    PSRAM_MSPI_REG(kSpiPort_Cache, SPI_SMEM_DDR) |= ESP32_REG_MASK(SPIMEM_SPI_SMEM_DDR, EN);

    /* Eight lines for every phase */
    PSRAM_MSPI_REG(kSpiPort_Cache, SRAM_CMD) |= ESP32_REG_MASK(SPIMEM_SRAM_CMD, SDUMMY_OUT)
                                              | ESP32_REG_MASK(SPIMEM_SRAM_CMD, SCMD_OCT)
                                              | ESP32_REG_MASK(SPIMEM_SRAM_CMD, SADDR_OCT)
                                              | ESP32_REG_MASK(SPIMEM_SRAM_CMD, SDOUT_OCT)
                                              | ESP32_REG_MASK(SPIMEM_SRAM_CMD, SDIN_OCT);
    *pSctrl |= ESP32_REG_MASK(SPIMEM_CACHE_SCTRL, SRAM_OCT);

    /* Let SPI0 assert CS1 */
    PSRAM_MSPI_REG(kSpiPort_Cache, MISC) &= ~ESP32_REG_MASK(SPIMEM_MISC, CS1_DIS);
}

/******************************************************************************
 * diagnostics
 *
 * Printed with esp_rom_printf rather than the LT log, because this runs inside
 * LTCoreBSP_Initialize, long before logging exists.
 *****************************************************************************/

static void ESP32_IRAM_FUNC
_Esp32_PSRAM_PrintInfo(void) {
    u8 nMr0 = s_modeReg[kOpiPSRAM_MR0];
    u8 nMr2 = s_modeReg[kOpiPSRAM_MR2];
    u8 nMr3 = s_modeReg[kOpiPSRAM_MR3];
    u8 nMr8 = s_modeReg[kOpiPSRAM_MR8];

    esp_rom_printf("psram: vendor 0x%02x, generation %d, %s die, %s refresh, %s\n",
                   PSRAM_MR1_VENDOR_ID(s_modeReg[kOpiPSRAM_MR1]),
                   PSRAM_MR2_DEV_ID(nMr2) + 1,
                   PSRAM_MR2_GB(nMr2) ? "good" : "FAILED",
                   PSRAM_MR3_SRF(nMr3) ? "fast" : "slow",
                   PSRAM_MR3_VCC(nMr3) ? "3.3V" : "1.8V");
    esp_rom_printf("psram: %s latency %d cycles, drive 1/%d, burst %s\n",
                   PSRAM_MR0_LT(nMr0) ? "fixed" : "variable",
                   PSRAM_MR0_READ_LATENCY(nMr0) * 2 + 6,
                   1 << PSRAM_MR0_DRIVE_STR(nMr0),
                   PSRAM_MR8_BT(nMr8) && (PSRAM_MR8_BL(nMr8) != 3) ? "hybrid wrap" : "linear");
}

/******************************************************************************
 * public interface
 *****************************************************************************/

bool ESP32_MEM_REGION(IRAM)
Esp32_PSRAM_Initialize(Esp32_PSRAM_Info * pInfo) {

    pInfo->pBase        = NULL;
    pInfo->nSizeInBytes = 0;

    if (! _Esp32_PSRAM_CheckCoreClock()) {
        esp_rom_printf("psram: MSPI core clock is not %dMHz, PSRAM disabled\n", kMSPI_CoreClockMHz);
        return false;
    }

    _Esp32_PSRAM_InitPins();
    _Esp32_PSRAM_SetCsTiming();

    /* Drop both module clocks to core/4 while the part's mode registers are
     * rewritten.  The flash divider has to come down with it: SPI0 and SPI1
     * share the input delay registers, so leaving flash fast while PSRAM runs
     * slow leaves neither correctly sampled. */
    _Esp32_PSRAM_SetFlashClockDivider(kSpiPort_Cache, kMSPI_LowSpeedDivider);
    _Esp32_PSRAM_SetFlashClockDivider(kSpiPort_User,  kMSPI_LowSpeedDivider);
    _Esp32_PSRAM_SetPsramClockDivider(kMSPI_LowSpeedDivider);

    /* esp_rom_opiflash_exec_cmd drives CS1 through SPI1's flash-side path, so
     * the flash-side DDR controls are what govern these transactions.  SPI1 is
     * shared with the flash write and erase path, which goes back to quad SDR
     * the moment this function returns, so the register is put back on the way
     * out - on both paths, PSRAM or no PSRAM. */
    u32 nSavedDdr = PSRAM_MSPI_REG(kSpiPort_User, DDR);
    PSRAM_MSPI_REG(kSpiPort_User, DDR) |= ESP32_REG_MASK(SPIMEM_DDR, FMEM_VAR_DUMMY);
    esp_rom_spi_set_dtr_swap_mode(kSpiPort_User, false, false);

    _Esp32_PSRAM_InitModeReg0();
    _Esp32_PSRAM_ReadAllModeRegs();

    if (PSRAM_MR1_VENDOR_ID(s_modeReg[kOpiPSRAM_MR1]) != kOpiPSRAM_VendorId_APMemory) {
        esp_rom_printf("psram: vendor id 0x%02x not recognised, no part fitted or wrong line mode, PSRAM disabled\n",
                       PSRAM_MR1_VENDOR_ID(s_modeReg[kOpiPSRAM_MR1]));
        goto restore;
    }

    u32 nSize = _Esp32_PSRAM_DecodeSize();
    if (nSize == 0) {
        esp_rom_printf("psram: density code 0x%02x not recognised, PSRAM disabled\n",
                       PSRAM_MR2_DENSITY(s_modeReg[kOpiPSRAM_MR2]));
        goto restore;
    }

    _Esp32_PSRAM_PrintInfo();

    /* Back up to the running speed.  Flash returns to core/1 = 80MHz SDR,
     * PSRAM settles at core/2 = 40MHz DDR. */
    _Esp32_PSRAM_SetFlashClockDivider(kSpiPort_Cache, 1);
    _Esp32_PSRAM_SetFlashClockDivider(kSpiPort_User,  1);
    _Esp32_PSRAM_SetPsramClockDivider(kMSPI_CoreClockMHz / kPSRAM_ModuleClockMHz);

    /* IDF calls spi_flash_set_vendor_required_regs() here; with a quad flash
     * part that reduces to this one line, putting SPI1's cache command width
     * back after the octal traffic above. */
    PSRAM_MSPI_REG(kSpiPort_User, CACHE_FCTRL) &= ~ESP32_REG_MASK(SPIMEM_CACHE_FCTRL, USR_CMD_4BYTE);

    /* And the other half of handing SPI1 back: the variable dummy cycle logic
     * the octal mode register traffic needed is not what the ROM's quad SDR
     * read and write path expects to find. */
    PSRAM_MSPI_REG(kSpiPort_User, DDR) = nSavedDdr;

    _Esp32_PSRAM_ConfigCachePhases();

    /* Decide where it goes.  The window is 16MB wide, so a larger part is
     * mapped from its start and the tail is simply not reachable. */
    if (nSize > (u32)(kExtRam_VAddrHigh - kExtRam_VAddrLow)) {
        esp_rom_printf("psram: part is %u MB, mapping the low %u MB\n",
                       nSize / (1024 * 1024),
                       (u32)(kExtRam_VAddrHigh - kExtRam_VAddrLow) / (1024 * 1024));
        nSize = (u32)(kExtRam_VAddrHigh - kExtRam_VAddrLow);
    }

    u32 nVAddr = (u32)kExtRam_VAddrHigh - nSize;

    /* Flash rodata is mapped through the same bus from the bottom up.  If the
     * image has grown far enough to reach the pages PSRAM wants, refuse rather
     * than overwrite live mappings. */
    u32 nRodataEnd = ((u32)&_rodata_reserved_end + (kMMU_PageSizeBytes - 1)) & ~(u32)(kMMU_PageSizeBytes - 1);
    if (nVAddr < nRodataEnd) {
        esp_rom_printf("psram: image rodata reaches 0x%08x, no room to map %u MB at 0x%08x, PSRAM disabled\n",
                       nRodataEnd, nSize / (1024 * 1024), nVAddr);
        return false;
    }

    u32 nAutoload = Esp32_CacheSuspendDCache();

    int nResult = Cache_Dbus_MMU_Set(kMMU_Access_SPIRAM, nVAddr, 0,
                                     kMMU_PageSizeKB, nSize / kMMU_PageSizeBytes, 0);

    /* Both CPUs get to see it.  CPU1 is not started today, but its stack is
     * reserved for a later bring-up, and leaving its data bus shut would make
     * every PSRAM address a fault for it. */
    ESP32_REG(EXTMEM_DCACHE_CTRL1) &= ~(ESP32_REG_MASK(EXTMEM_DCACHE_CTRL1, SHUT_CORE0)
                                      | ESP32_REG_MASK(EXTMEM_DCACHE_CTRL1, SHUT_CORE1));

    Esp32_CacheResumeDCache(nAutoload);

    if (nResult != 0) {
        esp_rom_printf("psram: MMU refused the mapping (%d), PSRAM disabled\n", nResult);
        return false;
    }

    esp_rom_printf("psram: %u MB mapped at 0x%08x, %dMHz DDR octal\n",
                   nSize / (1024 * 1024), nVAddr, kPSRAM_ModuleClockMHz);

    pInfo->pBase        = (u8 *)nVAddr;
    pInfo->nSizeInBytes = nSize;
    return true;

restore:
    /* Put the bus back the way the bootloader left it before giving up, so a
     * board with no PSRAM fitted still runs its flash at full speed - and with
     * SPI1's dummy cycle logic as the ROM's read and write path expects it. */
    _Esp32_PSRAM_SetFlashClockDivider(kSpiPort_Cache, 1);
    _Esp32_PSRAM_SetFlashClockDivider(kSpiPort_User,  1);
    PSRAM_MSPI_REG(kSpiPort_User, DDR) = nSavedDdr;
    return false;
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  28-Jul-26   claudius    created
 *  03-Aug-26   claudius    moved the cache geometry out to Esp32_Cache.c
 *  05-Aug-26   claudius    routed the octal MSPI pads; cleared SPI1's data
 *                          buffer before each mode register read
 *  05-Aug-26   claudius    gave SPI1's variable dummy setting back to the ROM's
 *                          read and write path on the way out
 */
