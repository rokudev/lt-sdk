/******************************************************************************
 * Esp32_Registers.h                                               ESP32-P4 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * This is the esp32p4 counterpart of include/esp32c3/Esp32_Registers.h.  It
 * covers what the second stage bootloader, the Esp32p4 drivers and
 * Esp32_LTCoreBSP reference; blocks with no caller on this part are not
 * reproduced.
 *
 * Almost nothing carries over from the esp32c3 numbering, and three structural
 * differences matter more than the addresses do:
 *
 *   - There is no RTC_CNTL block.  Software reset, the IO_MUX reset hold and
 *     the retention words live in LP_SYS, and the RTC watchdog and the super
 *     watchdog have a block of their own, LP_WDT.
 *   - The cache MMU is not a memory mapped table.  It is an index register and
 *     a content register in the flash MSPI, written as a pair, so there is no
 *     array to index and no kEsp32_RegisterMMU_TABLE here.
 *   - The interrupt controller is a CLIC rather than a multiplexer with a
 *     priority file.  The matrix still exists and still routes sources, but the
 *     per line enable, priority and trigger type are CLIC control words.  See
 *     Esp32_Irq.h, which is where that block is described.
 *
 * Every value below was taken from the ESP-IDF v5.4 esp32p4 soc headers
 * (register/soc/reg_base.h, lp_wdt_reg.h, lp_system_reg.h, hp_system_reg.h,
 * lp_iomux_reg.h, timer_group_reg.h, systimer_reg.h, efuse_reg.h, uart_reg.h,
 * gpio_reg.h, io_mux_reg.h, interrupt_core0_reg.h, spi_mem_c_reg.h,
 * spi1_mem_c_reg.h, spi_mem_s_reg.h, spi1_mem_s_reg.h, iomux_mspi_pin_reg.h,
 * hp_sys_clkrst_reg.h, lp_clkrst_reg.h, pmu_reg.h, lpperi_reg.h,
 * i2c_ana_mst_reg.h, cache_reg.h, include/soc/ext_mem_defs.h and
 * include/soc/wdev_reg.h) rather than adapted from the esp32c3 numbering.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32P4_REGISTERS_H
#define PLATFORMS_ESP32_INCLUDE_ESP32P4_REGISTERS_H

/*
 * Peripheral base addresses
 *
 * reg_base.h builds these from four group bases - HPCPUTCP 0x3ff00000,
 * HPPERIPH0 0x50000000, HPPERIPH1 0x500c0000, LPAON 0x50110000 and LPPERIPH
 * 0x50120000 - and the sums are spelled out here.  The one that looks wrong is
 * CACHE: the cache controller sits with the CPU's tightly coupled blocks at
 * 0x3ff10000, not with the rest of the peripherals.
 *
 * The CLIC is not a peripheral at all.  It is in the core's own address space
 * at 0x20800000, which no group base reaches.
 */
typedef u32 Esp32_RegisterBase;
enum Esp32_RegisterBase {
    kEsp32_RegisterBase_CACHE        = 0x3ff10000,
    kEsp32_RegisterBase_SPI0         = 0x5008c000,   /* FLASH_SPI0, the cache side */
    kEsp32_RegisterBase_SPI1         = 0x5008d000,   /* FLASH_SPI1, user transactions */
    kEsp32_RegisterBase_PSRAM0       = 0x5008e000,   /* PSRAM_MSPI0, the cache and AXI side */
    kEsp32_RegisterBase_PSRAM1       = 0x5008f000,   /* PSRAM_MSPI1, user transactions */
    kEsp32_RegisterBase_TIMG0        = 0x500c2000,
    kEsp32_RegisterBase_TIMG1        = 0x500c3000,
    kEsp32_RegisterBase_UART0        = 0x500ca000,
    kEsp32_RegisterBase_UART1        = 0x500cb000,
    kEsp32_RegisterBase_INTERRUPT    = 0x500d6000,
    kEsp32_RegisterBase_GPIO         = 0x500e0000,
    kEsp32_RegisterBase_IO_MUX       = 0x500e1000,
    kEsp32_RegisterBase_MSPI_IOMUX   = 0x500e1200,   /* the MSPI pads, not in IO_MUX's pad file */
    kEsp32_RegisterBase_SYSTIMER     = 0x500e2000,
    kEsp32_RegisterBase_HP_SYS       = 0x500e5000,
    kEsp32_RegisterBase_HP_CLKRST    = 0x500e6000,
    kEsp32_RegisterBase_LP_SYS       = 0x50110000,
    kEsp32_RegisterBase_LP_CLKRST    = 0x50111000,
    kEsp32_RegisterBase_PMU          = 0x50115000,
    kEsp32_RegisterBase_LP_WDT       = 0x50116000,
    kEsp32_RegisterBase_LPPERI       = 0x50120000,
    kEsp32_RegisterBase_I2C_ANA_MST  = 0x50124000,
    kEsp32_RegisterBase_LP_IO_MUX    = 0x5012b000,
    kEsp32_RegisterBase_EFUSE        = 0x5012d000,
    kEsp32_RegisterBase_CLIC         = 0x20800000,
    kEsp32_RegisterBase_CLIC_CTRL    = 0x20801000,
};

#define ESP32_REG_BASE(n)                       (kEsp32_RegisterBase_ ## n)

/*
 * Register Definitions
 */
typedef u32 Esp32_Register;

/*
 * LP_SYS - the always-on block, and what is left of the esp32c3's RTC_CNTL.
 *
 * There is no RTC_CNTL on this part and no RTC_CNTL_OPTIONS0: software reset is
 * one bit of SYS_CTRL, written read-modify-write rather than as the bare store
 * the esp32c3 uses, because the other fields here are not spare - DIG_FIB and
 * the LP_FIB bits configure which faults are allowed to reset the chip at all.
 */
enum Esp32_RegisterLP_SYS {
    kEsp32_RegisterLP_SYS_SYS_CTRL                    = ESP32_REG_BASE(LP_SYS) + 0x08,
    /* Write 1 to reset the digital system.  Write-triggered, self clearing. */
    kEsp32_RegisterLP_SYS_SYS_CTRL_SW_RST_M           = 0x01 << 1,
    /*
     * Hold the IO_MUX configuration across a system reset.  The esp32c3 spells
     * the same thing RTC_CNTL_USB_CONF's IO_MUX_RESET_DISABLE; here it is a
     * field of SYS_CTRL, and there is no USB half to the register because the
     * USB serial/JTAG device is not the console on this part.
     */
    kEsp32_RegisterLP_SYS_SYS_CTRL_IO_MUX_RESET_DISABLE_M = 0x01 << 11,

    /*
     * The RTC slow clock period in microseconds, Q13.19, as measured by whoever
     * last calibrated it - LP_STORE1, which rom/rtc.h aliases
     * RTC_SLOW_CLK_CAL_REG.  Reads as zero if nothing has calibrated.
     */
    kEsp32_RegisterLP_SYS_STORE1                      = ESP32_REG_BASE(LP_SYS) + 0x30,

    /*
     * The XTAL frequency in MHz as the ROM bootloader left it - LP_STORE4,
     * aliased RTC_XTAL_FREQ_REG.  Stored as two identical 16-bit halves.
     */
    kEsp32_RegisterLP_SYS_STORE4                      = ESP32_REG_BASE(LP_SYS) + 0x3c,
};

/*
 * LP_WDT - the RTC watchdog (RWDT) and the super watchdog (SWD)
 *
 * Field positions are the esp32c3's RTC_CNTL_WDTCONFIG0 rearranged, so the
 * esp32c3's packed setup constants cannot be reused and the fields are named
 * individually here instead.  Two fields the esp32c3 has are simply gone:
 * WDT_CHIP_RESET_EN and WDT_CHIP_RESET_WIDTH, which is why nothing on this part
 * corresponds to the low byte of its 0xc001eb80.
 *
 * The stage action encodings (hal/lpwdt_ll.h) are 0 off, 1 interrupt, 2 reset
 * CPU, 3 reset system, 4 reset RTC.  Reset length is an index into
 * 100/200/300/400/500/800/1600/3200ns.
 *
 * CONFIG0 + 4 is CONFIG1, whose whole 32 bits are the stage 0 hold count, so
 * Esp32SetTimeoutRTCWatchdog() can index it as an array element exactly as on
 * the esp32c3.
 *
 * SWD takes the same key as RWDT on this part - LP_WDT_SWD_WKEY_VALUE and
 * LP_WDT_WKEY_VALUE are both 0x50D83AA1, where the esp32c3 gives SWD its own.
 */
enum Esp32_RegisterLP_WDT {
    kEsp32_RegisterLP_WDT_CONFIG0                     = ESP32_REG_BASE(LP_WDT) + 0x00,

    kEsp32_RegisterLP_WDT_CONFIG0_PAUSE_IN_SLP_S      = 9,
    kEsp32_RegisterLP_WDT_CONFIG0_APPCPU_RESET_EN_S   = 10,
    kEsp32_RegisterLP_WDT_CONFIG0_PROCPU_RESET_EN_S   = 11,
    kEsp32_RegisterLP_WDT_CONFIG0_SYS_RESET_LENGTH_S  = 13,
    kEsp32_RegisterLP_WDT_CONFIG0_CPU_RESET_LENGTH_S  = 16,
    kEsp32_RegisterLP_WDT_CONFIG0_STG3_S              = 19,
    kEsp32_RegisterLP_WDT_CONFIG0_STG2_S              = 22,
    kEsp32_RegisterLP_WDT_CONFIG0_STG1_S              = 25,
    kEsp32_RegisterLP_WDT_CONFIG0_STG0_S              = 28,

    /*
     * The two arming paths.  FLASHBOOT_MOD_EN resets to 1, so the watchdog is
     * armed out of reset whether or not anything wrote WDT_EN, and any sequence
     * that means to disable the watchdog has to clear both - see
     * Esp32DisableRTCWatchdog().
     */
    kEsp32_RegisterLP_WDT_WDT_ENABLED_M               = 0x01u << 31,
    kEsp32_RegisterLP_WDT_WDT_FLASHBOOT_MOD_EN_M      = 0x01 << 12,

    /*
     * Enabled: stage 0 resets the RTC (i.e. the whole chip), stages 1 to 3 off,
     * the CPU held in reset for 200ns and the system for 3200ns, paused while
     * the part sleeps, and PROCPU reset enabled.  FLASHBOOT_MOD_EN left clear,
     * for the reason above.
     *
     *   WDT_EN               BIT(31)   = 1
     *   WDT_STG0             [30:28]   = 4 (reset RTC)
     *   WDT_STG1..3                    = 0 (off)
     *   WDT_CPU_RESET_LENGTH [18:16]   = 1
     *   WDT_SYS_RESET_LENGTH [15:13]   = 7
     *   WDT_FLASHBOOT_MOD_EN BIT(12)   = 0
     *   WDT_PROCPU_RESET_EN  BIT(11)   = 1
     *   WDT_PAUSE_IN_SLP     BIT(9)    = 1
     */
    kEsp32_RegisterLP_WDT_WDT_SETUP_EN_V              = 0xc001ea00,
    /* The same word with WDT_EN cleared */
    kEsp32_RegisterLP_WDT_WDT_SETUP_DIS_V             = 0x4001ea00,

    kEsp32_RegisterLP_WDT_CONFIG1                     = ESP32_REG_BASE(LP_WDT) + 0x04,

    kEsp32_RegisterLP_WDT_WDTFEED                     = ESP32_REG_BASE(LP_WDT) + 0x14,
    /* Write only, LP_WDT_FEED, BIT(31) */
    kEsp32_RegisterLP_WDT_WDT_FEED_V                  = 0x80000000,

    kEsp32_RegisterLP_WDT_WDTWPROTECT                 = ESP32_REG_BASE(LP_WDT) + 0x18,
    kEsp32_RegisterLP_WDT_WDT_UNPROTECT_V             = 0x50d83aa1,
    kEsp32_RegisterLP_WDT_WDT_PROTECT_V               = 0x0,

    /*
     * Super watchdog.  It cannot be disabled, only fed, and AUTO_FEED_EN hands
     * that to hardware - which is how the bootloader survives it.  It sits
     * behind its own lock, though the key written to it is the same value.
     */
    kEsp32_RegisterLP_WDT_SWD_CONF                    = ESP32_REG_BASE(LP_WDT) + 0x1c,
    kEsp32_RegisterLP_WDT_SWD_CONF_AUTO_FEED_EN_M     = 0x01 << 18,

    kEsp32_RegisterLP_WDT_SWDWPROTECT                 = ESP32_REG_BASE(LP_WDT) + 0x20,
    kEsp32_RegisterLP_WDT_SWD_UNPROTECT_V             = 0x50d83aa1,
    kEsp32_RegisterLP_WDT_SWD_PROTECT_V               = 0x0,
};

/*
 * Pad hold
 *
 * Three registers where the esp32c3 has one, because the pads are split between
 * two power domains: LP pads 0..15 hold from LP_IO_MUX, and digital pads 16..54
 * from HP_SYS in two banks, bit (pad - 16) and bit (pad - 48).
 *
 * IDF's own gpio_ll.h calls the feature "not usable on P4" - the HP_SYSTEM half
 * is in the TOP power domain and is cleared on deep sleep wake, so a hold set
 * on a digital pad does not survive the thing hold exists for.  The LP half
 * does.  Kept here because the GPIO driver's interface has the call in it on
 * every part, and because the hold is still honoured across a plain reset.
 */
enum Esp32_RegisterPAD_HOLD {
    kEsp32_RegisterLP_IO_MUX_PAD_HOLD                 = ESP32_REG_BASE(LP_IO_MUX) + 0x4c,
    kEsp32_RegisterHP_SYS_GPIO_HOLD_CTRL0             = ESP32_REG_BASE(HP_SYS) + 0x74,
    kEsp32_RegisterHP_SYS_GPIO_HOLD_CTRL1             = ESP32_REG_BASE(HP_SYS) + 0x78,
    /* Highest pad held by LP_IO_MUX; 16 and up are HP_SYS's */
    kEsp32_RegisterPAD_HOLD_LP_MAX_PAD                = 15,
};

/*
 * Timer group watchdogs (MWDT0 and MWDT1), one per timer group.
 *
 * Only what is needed to make sure they are off.  This block is the one part of
 * the watchdog story that carries over from the esp32c3 unchanged - same
 * offsets, same field positions, same key.
 *
 * Note that v5.4's esp32p4 timer_group_reg.h defines TIMG_WDT_WKEY as the field
 * mask 0xffffffff and does not emit the key value at all, though hal/mwdt_ll.h
 * uses it; 0x50d83aa1 is taken from the vendored esp32c3 and esp32 copies of
 * the same header, which agree.
 */
enum Esp32_RegisterTIMG {
    kEsp32_RegisterTIMG0_WDT_CONFIG0                  = ESP32_REG_BASE(TIMG0) + 0x48,
    kEsp32_RegisterTIMG1_WDT_CONFIG0                  = ESP32_REG_BASE(TIMG1) + 0x48,
    kEsp32_RegisterTIMG0_WDTWPROTECT                  = ESP32_REG_BASE(TIMG0) + 0x64,
    kEsp32_RegisterTIMG1_WDTWPROTECT                  = ESP32_REG_BASE(TIMG1) + 0x64,

    kEsp32_RegisterTIMG_WDT_EN_M                      = 0x01u << 31,
    kEsp32_RegisterTIMG_WDT_FLASHBOOT_MOD_EN_M        = 0x01 << 14,
    kEsp32_RegisterTIMG_WDT_UNPROTECT_V               = 0x50d83aa1,
    kEsp32_RegisterTIMG_WDT_PROTECT_V                 = 0x0,
};

/*
 * HP_SYS_CLKRST - clock gating and peripheral reset
 *
 * The esp32c3 gates everything from two SYSTEM registers, one bit per
 * peripheral.  Here the same job is spread across some twenty registers with no
 * regular layout, so there is no bitmask type to hand around and the individual
 * registers are named instead; Esp32_Clock.h packs an offset and a bit position
 * together to describe a single gate.
 *
 * Everything this BSP needs - UART0, SYSTIMER, both timer groups and IO_MUX -
 * is enabled out of reset, so unlike the esp32c3 nothing here has to be turned
 * on before use.  Only I2C and LEDC default off.
 */
enum Esp32_RegisterHP_CLKRST {
    kEsp32_RegisterHP_CLKRST_SOC_CLK_CTRL0            = ESP32_REG_BASE(HP_CLKRST) + 0x14,
    kEsp32_RegisterHP_CLKRST_SOC_CLK_CTRL0_PSRAM_SYS_CLK_EN_M = 0x01u << 31,

    kEsp32_RegisterHP_CLKRST_SOC_CLK_CTRL1            = ESP32_REG_BASE(HP_CLKRST) + 0x18,
    kEsp32_RegisterHP_CLKRST_SOC_CLK_CTRL2            = ESP32_REG_BASE(HP_CLKRST) + 0x1c,
    kEsp32_RegisterHP_CLKRST_SOC_CLK_CTRL3            = ESP32_REG_BASE(HP_CLKRST) + 0x20,
    /*
     * The PSRAM module clock: a source select, a gate on the PLL output and a
     * gate on the core clock.  Source 0 is XTAL, 1 the MPLL, 2 the SPLL and 3
     * the CPLL; only the MPLL can power the PSRAM die on this part.
     */
    kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL00          = ESP32_REG_BASE(HP_CLKRST) + 0x30,
    kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL00_PSRAM_CLK_SRC_SEL_S = 12,
    kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL00_PSRAM_CLK_SRC_SEL_M = 0x03 << 12,
    kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL00_PSRAM_PLL_CLK_EN_M  = 0x01 << 14,
    kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL00_PSRAM_CORE_CLK_EN_M = 0x01 << 15,
    kEsp32_RegisterHP_CLKRST_PSRAM_CLK_SRC_MPLL_V     = 1,

    kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL10          = ESP32_REG_BASE(HP_CLKRST) + 0x40,
    kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL110         = ESP32_REG_BASE(HP_CLKRST) + 0x68,
    kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL111         = ESP32_REG_BASE(HP_CLKRST) + 0x6c,
    kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL20          = ESP32_REG_BASE(HP_CLKRST) + 0x94,
    kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL21          = ESP32_REG_BASE(HP_CLKRST) + 0x98,
    kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL22          = ESP32_REG_BASE(HP_CLKRST) + 0x9c,
    kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL26          = ESP32_REG_BASE(HP_CLKRST) + 0xac,
    /*
     * MPLL self calibration.  CAL_STOP is inverted: clearing it starts the
     * calibration, and CAL_END reads back when the PLL has settled.
     */
    kEsp32_RegisterHP_CLKRST_ANA_PLL_CTRL0            = ESP32_REG_BASE(HP_CLKRST) + 0xbc,
    kEsp32_RegisterHP_CLKRST_ANA_PLL_CTRL0_MSPI_CAL_END_M  = 0x01 << 8,
    kEsp32_RegisterHP_CLKRST_ANA_PLL_CTRL0_MSPI_CAL_STOP_M = 0x01 << 9,

    /* The PSRAM MSPI pair's resets, pulsed before its clock source is chosen */
    kEsp32_RegisterHP_CLKRST_HP_RST_EN0               = ESP32_REG_BASE(HP_CLKRST) + 0xc0,
    kEsp32_RegisterHP_CLKRST_HP_RST_EN0_DUAL_MSPI_AXI_M = 0x01 << 23,
    kEsp32_RegisterHP_CLKRST_HP_RST_EN0_DUAL_MSPI_APB_M = 0x01 << 25,

    kEsp32_RegisterHP_CLKRST_HP_RST_EN1               = ESP32_REG_BASE(HP_CLKRST) + 0xc4,

    /*
     * The CPU source clock frequency, read only, in steps of 0.25MHz - so the
     * CPU clock in MHz is this divided by four.  The esp32c3 has to work its
     * frequency out from a divider select and a PLL setting; this part reports
     * it.
     */
    kEsp32_RegisterHP_CLKRST_CPU_SRC_FREQ0            = ESP32_REG_BASE(HP_CLKRST) + 0xdc,
    kEsp32_RegisterHP_CLKRST_CPU_SRC_FREQ_PER_MHZ_V   = 4,
};

/*
 * Interrupt matrix
 *
 * One word per peripheral source in ETS_*_INTR_SOURCE order, so
 * Esp32_ExternalIrq doubles as the array index.  The field is six bits wide and
 * names a CLIC line, biased by 16 - see Esp32_Irq.h, which owns that bias and
 * every other detail of the CLIC.  Writing 0 detaches the source.
 *
 * Unlike the esp32c3's INTERRUPT_CORE0 block this is only a router: there is no
 * enable mask, no priority file and no threshold here.  All of that moved into
 * the CLIC.
 */
enum Esp32_RegisterINTERRUPT {
    kEsp32_RegisterINTERRUPT_CORE0_IRQ_MAP            = ESP32_REG_BASE(INTERRUPT) + 0x000,
    /* Sources 0..127, ETS_MAX_INTR_SOURCE */
    kEsp32_RegisterINTERRUPT_SOURCE_COUNT             = 128,
};

/*
 * SYSTIMER
 *
 * The kernel tick source, and the one block that is the esp32c3's byte for byte
 * - every offset and every field position below was re-read from the esp32p4
 * systimer_reg.h and matches.  The tick rate matches too: the counter is fed
 * from the 40MHz crystal through a fixed 2.5 divider, so 16MHz as on the
 * esp32c3, independent of the CPU clock.
 *
 * Two 52-bit up counters (UNIT0, UNIT1) and three comparators (TARGET0..2), any
 * comparator attachable to either counter.  A counter is read by writing UPDATE
 * to its OP register, spinning on VALUE_VALID, then reading VALUE_HI/VALUE_LO;
 * the hardware latches the pair so the two reads cannot tear.
 *
 * A comparator in period mode reloads itself: PERIOD is a 26 bit tick count and
 * the comparator re-arms at now + PERIOD each time it fires.  Writes to
 * TARGETn_HI/LO and TARGETn_CONF are staged and only take effect when the
 * matching COMPn_LOAD bit is written.
 *
 * One difference worth knowing: UNIT0_WORK_EN resets to 1 on this part, so the
 * counter is already running when the BSP finds it.
 */
enum Esp32_RegisterSYSTIMER {
    kEsp32_RegisterSYSTIMER_CONF                      = ESP32_REG_BASE(SYSTIMER) + 0x00,
    kEsp32_RegisterSYSTIMER_CONF_CLK_EN_M             = 0x01u << 31,
    kEsp32_RegisterSYSTIMER_CONF_UNIT0_WORK_EN_M      = 0x01 << 30,
    /* Freeze UNIT0 while the core is halted by the debugger, so a breakpoint
     * does not produce a burst of catch-up ticks on resume.  There is a CORE1
     * counterpart at bit 27; LT runs one core and does not use it. */
    kEsp32_RegisterSYSTIMER_CONF_UNIT0_CORE0_STALL_EN_M = 0x01 << 28,
    kEsp32_RegisterSYSTIMER_CONF_TARGET0_WORK_EN_M    = 0x01 << 24,

    kEsp32_RegisterSYSTIMER_UNIT0_OP                  = ESP32_REG_BASE(SYSTIMER) + 0x04,
    /* Reads 1 once the latched VALUE_HI/VALUE_LO pair is ready */
    kEsp32_RegisterSYSTIMER_UNIT0_OP_VALUE_VALID_M    = 0x01 << 29,
    /* Write 1 to latch the counter into VALUE_HI/VALUE_LO */
    kEsp32_RegisterSYSTIMER_UNIT0_OP_UPDATE_M         = 0x01 << 30,

    kEsp32_RegisterSYSTIMER_UNIT0_LOAD_HI             = ESP32_REG_BASE(SYSTIMER) + 0x0c,
    kEsp32_RegisterSYSTIMER_UNIT0_LOAD_LO             = ESP32_REG_BASE(SYSTIMER) + 0x10,

    kEsp32_RegisterSYSTIMER_TARGET0_HI                = ESP32_REG_BASE(SYSTIMER) + 0x1c,
    kEsp32_RegisterSYSTIMER_TARGET0_LO                = ESP32_REG_BASE(SYSTIMER) + 0x20,

    kEsp32_RegisterSYSTIMER_TARGET0_CONF              = ESP32_REG_BASE(SYSTIMER) + 0x34,
    kEsp32_RegisterSYSTIMER_TARGET0_CONF_PERIOD_S     = 0,
    kEsp32_RegisterSYSTIMER_TARGET0_CONF_PERIOD_M     = 0x03ffffff,
    /* 0 -> fire once at the absolute value in TARGET0_HI/LO, 1 -> reload */
    kEsp32_RegisterSYSTIMER_TARGET0_CONF_PERIOD_MODE_M = 0x01 << 30,
    /* Which counter this comparator watches: 0 -> UNIT0, 1 -> UNIT1 */
    kEsp32_RegisterSYSTIMER_TARGET0_CONF_UNIT_SEL_M   = 0x01u << 31,

    kEsp32_RegisterSYSTIMER_UNIT0_VALUE_HI            = ESP32_REG_BASE(SYSTIMER) + 0x40,
    kEsp32_RegisterSYSTIMER_UNIT0_VALUE_LO            = ESP32_REG_BASE(SYSTIMER) + 0x44,

    /* Write 1 to commit the staged TARGET0_HI/LO and TARGET0_CONF writes */
    kEsp32_RegisterSYSTIMER_COMP0_LOAD                = ESP32_REG_BASE(SYSTIMER) + 0x50,
    kEsp32_RegisterSYSTIMER_COMP0_LOAD_M              = 0x01 << 0,

    /* Write 1 to commit the staged UNIT0_LOAD_HI/LO writes */
    kEsp32_RegisterSYSTIMER_UNIT0_LOAD                = ESP32_REG_BASE(SYSTIMER) + 0x5c,
    kEsp32_RegisterSYSTIMER_UNIT0_LOAD_M              = 0x01 << 0,

    kEsp32_RegisterSYSTIMER_INT_ENA                   = ESP32_REG_BASE(SYSTIMER) + 0x64,
    kEsp32_RegisterSYSTIMER_INT_RAW                   = ESP32_REG_BASE(SYSTIMER) + 0x68,
    kEsp32_RegisterSYSTIMER_INT_CLR                   = ESP32_REG_BASE(SYSTIMER) + 0x6c,
    kEsp32_RegisterSYSTIMER_INT_ST                    = ESP32_REG_BASE(SYSTIMER) + 0x70,
    /* The four INT_ registers share this layout, one bit per comparator */
    kEsp32_RegisterSYSTIMER_INT_TARGET0_M             = 0x01 << 0,

    /* Counter tick rate, fixed and independent of the CPU clock */
    kEsp32_RegisterSYSTIMER_TICK_HZ                   = 16000000,
};

/*
 * EFUSE - only the fields the drivers read.
 *
 * The offsets are the esp32c3's unchanged, and so is SPI_BOOT_CRYPT_CNT's place
 * in RD_REPEAT_DATA1.  Only the MAC word names differ: this part calls them
 * RD_MAC_SYS_0/1 where the esp32c3 says RD_MAC_SPI_SYS_0/1.
 */
enum Esp32_RegisterEFUSE {
    kEsp32_RegisterEFUSE_RD_REPEAT_DATA1              = ESP32_REG_BASE(EFUSE) + 0x034,
    kEsp32_RegisterEFUSE_SPI_BOOT_CRYPT_CNT_S         = 18,
    kEsp32_RegisterEFUSE_SPI_BOOT_CRYPT_CNT_M         = 0x07u << 18,

    /*
     * Multiplier applied to the RWDT stage 0 hold count, EFUSE_WDT_DELAY_SEL:
     * 0 -> x2, 1 -> x4, 2 -> x8, 3 -> x16.  A timeout computed without it is
     * short by at least a factor of two.
     */
    kEsp32_RegisterEFUSE_WDT_DELAY_SEL_S              = 16,
    kEsp32_RegisterEFUSE_WDT_DELAY_SEL_M              = 0x03u << 16,

    /*
     * The factory base MAC.  Word 0 holds the low 32 bits and the low half of
     * word 1 the high 16, most significant byte first.  There is no MAC CRC
     * beside it, so readers must not check one.
     */
    kEsp32_RegisterEFUSE_RD_MAC_SYS_0                 = ESP32_REG_BASE(EFUSE) + 0x044,
    kEsp32_RegisterEFUSE_RD_MAC_SYS_1                 = ESP32_REG_BASE(EFUSE) + 0x048,

    /*
     * Per die trim for external LDO channel 2, the rail that feeds both the
     * PSRAM and the MPLL.  Only valid once the eFuse block version is at least
     * 1, and only when both fields are non-zero; the uncalibrated pair is
     * exact at 1.8V anyway, so these are a refinement rather than a
     * requirement.
     */
    kEsp32_RegisterEFUSE_RD_MAC_SYS_2                 = ESP32_REG_BASE(EFUSE) + 0x04c,
    kEsp32_RegisterEFUSE_BLK_VERSION_MINOR_S          = 8,
    kEsp32_RegisterEFUSE_BLK_VERSION_MINOR_M          = 0x07u << 8,
    kEsp32_RegisterEFUSE_BLK_VERSION_MAJOR_S          = 11,
    kEsp32_RegisterEFUSE_BLK_VERSION_MAJOR_M          = 0x03u << 11,
    kEsp32_RegisterEFUSE_LDO_VO2_DREF_S               = 28,
    kEsp32_RegisterEFUSE_LDO_VO2_DREF_M               = 0x0fu << 28,

    kEsp32_RegisterEFUSE_RD_MAC_SYS_3                 = ESP32_REG_BASE(EFUSE) + 0x050,
    kEsp32_RegisterEFUSE_LDO_VO2_MUL_S                = 3,
    kEsp32_RegisterEFUSE_LDO_VO2_MUL_M                = 0x07u << 3,
};

/*
 * UART
 *
 * The console is UART0 on GPIO37 and GPIO38, through the board's CH343 bridge -
 * this part does not use the USB serial/JTAG device for the console the way the
 * esp32c3 does, so there is no USB_SERIAL_JTAG block in this header.
 *
 * Offsets up to STATUS and all the interrupt bit positions are the esp32c3's,
 * but two things after that differ and both matter:
 *
 *   - The FIFO counters are 8 bits, not 10.  The FIFO is 128 bytes.
 *   - FSM_STATUS moved from 0x6c to 0x70, and 0x6c is now MEM_RX_STATUS, which
 *     holds the receive SRAM's read and write offsets rather than a count.
 *     There is no MEM_CNT_STATUS register on this part; occupancy is
 *     RX_SRAM_WADDR - RX_SRAM_RADDR.
 */
enum Esp32_RegisterUART {
    kEsp32_RegisterUART_FIFO                          = 0x00,
    kEsp32_RegisterUART_INT_RAW                       = 0x04,
    kEsp32_RegisterUART_INT_ST                        = 0x08,
    kEsp32_RegisterUART_INT_ENA                       = 0x0c,
    kEsp32_RegisterUART_INT_CLR                       = 0x10,
    kEsp32_RegisterUART_STATUS                        = 0x1c,

    /*
     * Interrupt bits.  The four INT_ registers above share this layout, so the
     * shifts are named against a single UART_INT prefix rather than per register.
     */
    kEsp32_RegisterUART_INT_RXFIFO_FULL_S             = 0,
    kEsp32_RegisterUART_INT_TXFIFO_EMPTY_S            = 1,
    kEsp32_RegisterUART_INT_RXFIFO_OVF_S              = 4,
    kEsp32_RegisterUART_INT_RXFIFO_TOUT_S             = 8,

    /* Bit positions for UART_STATUS.  Both FIFO counters are 8 bits wide. */
    kEsp32_RegisterUART_STATUS_TXFIFO_CNT_S           = 16,
    kEsp32_RegisterUART_STATUS_TXFIFO_CNT_M           = 0x00ff0000,
    kEsp32_RegisterUART_STATUS_RXFIFO_CNT_S           = 0,
    kEsp32_RegisterUART_STATUS_RXFIFO_CNT_M           = 0x000000ff,

    /* Receive SRAM occupancy, as a pair of offsets rather than a count */
    kEsp32_RegisterUART_MEM_RX_STATUS                 = 0x6c,
    kEsp32_RegisterUART_MEM_RX_STATUS_RADDR_S         = 0,
    kEsp32_RegisterUART_MEM_RX_STATUS_RADDR_M         = 0x000000ff,
    kEsp32_RegisterUART_MEM_RX_STATUS_WADDR_S         = 9,
    kEsp32_RegisterUART_MEM_RX_STATUS_WADDR_M         = 0x0001fe00,

    /* Transmitter and receiver state machines, in their own register */
    kEsp32_RegisterUART_FSM_STATUS                    = 0x70,
    kEsp32_RegisterUART_FSM_STATUS_UTX_OUT_S          = 4,
    kEsp32_RegisterUART_FSM_STATUS_UTX_OUT_M          = 0x000000f0,
    kEsp32_RegisterUART_FSM_STATUS_URX_OUT_S          = 0,
    kEsp32_RegisterUART_FSM_STATUS_URX_OUT_M          = 0x0000000f,

    /* Hardware FIFO depth, SOC_UART_FIFO_LEN */
    kEsp32_RegisterUART_FIFO_LENGTH                   = 128,
};

/*
 * MSPI (SPI0 / SPI1) - the memory SPI controllers, IDF's SPI_MEM_* block.
 *
 * SPI0 is the cache side: it fetches flash on the cache's behalf, and on this
 * part it also carries the MMU index and content registers.  SPI1 shares the
 * pads and runs user mode transactions - it is the base esptool calls
 * SPI_REG_BASE.
 *
 * IDF splits the two into separate headers here (spi_mem_c_reg.h for SPI0,
 * spi1_mem_c_reg.h for SPI1) rather than describing one block twice as it does
 * on the esp32c3, but the offsets below are common to both, so a single enum
 * still serves ESP32_SPIMEM_REG(n, r).  The exception is the MMU group, which
 * exists only on SPI0 and so is named separately.
 *
 * There is no CORE_CLK_SEL here.  MSPI clocking on this part is configured from
 * HP_SYS_CLKRST, not from a register inside the controller.
 */
enum Esp32_RegisterSPIMEM {
    kEsp32_RegisterSPIMEM_CMD                         = 0x000,
    kEsp32_RegisterSPIMEM_ADDR                        = 0x004,
    kEsp32_RegisterSPIMEM_CTRL                        = 0x008,
    kEsp32_RegisterSPIMEM_CLOCK                       = 0x014,
    kEsp32_RegisterSPIMEM_USER                        = 0x018,
    kEsp32_RegisterSPIMEM_USER1                       = 0x01c,
    kEsp32_RegisterSPIMEM_USER2                       = 0x020,
    kEsp32_RegisterSPIMEM_MISC                        = 0x034,
    kEsp32_RegisterSPIMEM_CACHE_FCTRL                 = 0x03c,
    /* W0 is the first of the sixteen data buffer words, W0 to W15 */
    kEsp32_RegisterSPIMEM_W0                          = 0x058,
    kEsp32_RegisterSPIMEM_DATE                        = 0x3fc,

    /* CLOCK - flash side divider.  f_SPI = f_core / (CLKCNT_N + 1) */
    kEsp32_RegisterSPIMEM_CLOCK_CLK_EQU_SYSCLK_M      = 0x01u << 31,
    kEsp32_RegisterSPIMEM_CLOCK_CLKCNT_N_S            = 16,
    kEsp32_RegisterSPIMEM_CLOCK_CLKCNT_H_S            = 8,
    kEsp32_RegisterSPIMEM_CLOCK_CLKCNT_L_S            = 0,

    /* MISC - CS1 has no device behind it on the flash MSPI, so it stays disabled */
    kEsp32_RegisterSPIMEM_MISC_CS1_DIS_M              = 0x01 << 1,

    /* CTRL - when set, WRSR sends a 16 bit status word instead of 8 bits */
    kEsp32_RegisterSPIMEM_CTRL_WRSR_2B_M              = 0x01 << 22,

    /* CMD - hardware generated flash commands.  Setting a bit starts the
     * command; the bit self clears when it completes */
    kEsp32_RegisterSPIMEM_CMD_USR_M                   = 0x01 << 18,
    kEsp32_RegisterSPIMEM_CMD_FLASH_WREN_M            = 0x01u << 30,
    kEsp32_RegisterSPIMEM_CMD_FLASH_READ_M            = 0x01u << 31,

    /* USER - which phases a user transaction has */
    kEsp32_RegisterSPIMEM_USER_USR_MOSI_M             = 0x01 << 27,
    kEsp32_RegisterSPIMEM_USER_USR_MISO_M             = 0x01 << 28,
    kEsp32_RegisterSPIMEM_USER_USR_DUMMY_M            = 0x01 << 29,
    kEsp32_RegisterSPIMEM_USER_USR_ADDR_M             = 0x01u << 30,
    kEsp32_RegisterSPIMEM_USER_USR_COMMAND_M          = 0x01u << 31,

    /* CACHE_FCTRL - selects a 4 byte address for the cache's flash reads */
    kEsp32_RegisterSPIMEM_CACHE_FCTRL_USR_ADDR_4BYTE_M = 0x01 << 1,
};

/*
 * Cache MMU
 *
 * Not a memory mapped table on this part.  An entry is written by putting its
 * index in MMU_ITEM_INDEX and then its content in MMU_ITEM_CONTENT, and read by
 * writing the index and reading the content back - so the two registers are a
 * port, and any sequence using them is not re-entrant.  Both live in SPI0.
 *
 * 1024 entries of 64KB, covering the whole 0x40000000..0x44000000 cached flash
 * window.  Unlike the esp32c3 there is no separate IBUS and DBUS window to keep
 * apart: instruction and data reach flash through the same addresses, so
 * index = (vaddr & VADDR_MASK) / PAGE_SIZE with no per-bus offset, and the
 * linker has nothing to split.
 *
 * An entry holds a physical page number in its low bits with VALID set and
 * ACCESS_FLASH (0) selecting flash over PSRAM; the whole word reads back as
 * INVALID (0) when nothing is mapped.  SENSITIVE joins them once flash
 * encryption is burned in.
 */
enum Esp32_RegisterMMU {
    kEsp32_RegisterMMU_ITEM_CONTENT                   = ESP32_REG_BASE(SPI0) + 0x37c,
    kEsp32_RegisterMMU_ITEM_INDEX                     = ESP32_REG_BASE(SPI0) + 0x380,
    kEsp32_RegisterMMU_POWER_CTRL                     = ESP32_REG_BASE(SPI0) + 0x384,
    /* Page size select: 0 -> 64KB, 1 -> 32KB, 2 -> 16KB, 3 -> 8KB */
    kEsp32_RegisterMMU_POWER_CTRL_PAGE_SIZE_S         = 3,
    kEsp32_RegisterMMU_POWER_CTRL_PAGE_SIZE_M         = 0x03 << 3,

    kEsp32_RegisterMMU_ENTRY_COUNT                    = 1024,
    kEsp32_RegisterMMU_PAGE_SIZE                      = 0x10000,

    kEsp32_RegisterMMU_VALID_V                        = 0x01 << 12,
    kEsp32_RegisterMMU_INVALID_V                      = 0,
    kEsp32_RegisterMMU_ACCESS_FLASH_V                 = 0,
    kEsp32_RegisterMMU_ADDRESS_M                      = 0x7ff,

    /*
     * Marks the page as needing decryption on the way through the cache.  It
     * must be set on every flash page whenever flash encryption is burned in,
     * or reads through the cache return ciphertext.  The PSRAM MMU carries the
     * same bit one place lower, at bit 12.
     */
    kEsp32_RegisterMMU_SENSITIVE_V                    = 0x01 << 13,

    /* (PAGE_SIZE * ENTRY_COUNT) - 1 */
    kEsp32_RegisterMMU_VADDR_MASK                     = 0x03ffffff,

    /* The cached flash window, one window for both buses */
    kEsp32_RegisterMMU_FLASH_LOW                      = 0x40000000,
    kEsp32_RegisterMMU_FLASH_HIGH                     = 0x44000000,
};

/*
 * PSRAM MSPI
 *
 * Flash and PSRAM do not share a bus on this part - SOC_MEMSPI_FLASH_PSRAM_-
 * INDEPENDENT is 1 - so the PSRAM has a controller pair of its own, PSRAM0 for
 * the cache and AXI side and PSRAM1 for user transactions, and programming it
 * does not disturb the running flash cache.  The two halves do not share a
 * register layout either, which is why the offsets below are absolute and not
 * an offset enum indexed by unit as kEsp32_RegisterSPIMEM_ is.
 *
 * Only what Esp32_PSRAM.c writes is reproduced.  Almost all of the cache phase
 * configuration lands on PSRAM0 even for fields whose name says smem; the two
 * DLL calibration bits are the trap, because the PSRAM1 one is also a PSRAM0
 * register, just a different one.
 */
enum Esp32_RegisterPSRAM {
    /* PSRAM0 - AXI request splicing */
    kEsp32_RegisterPSRAM_CTRL1                        = ESP32_REG_BASE(PSRAM0) + 0x00c,
    kEsp32_RegisterPSRAM_CTRL1_AR_SPLICE_EN_M         = 0x01 << 25,
    kEsp32_RegisterPSRAM_CTRL1_AW_SPLICE_EN_M         = 0x01 << 26,

    /* PSRAM0 - the AXI interface to the cache */
    kEsp32_RegisterPSRAM_CACHE_FCTRL                  = ESP32_REG_BASE(PSRAM0) + 0x03c,
    kEsp32_RegisterPSRAM_CACHE_FCTRL_AXI_REQ_EN_M     = 0x01 << 0,
    kEsp32_RegisterPSRAM_CACHE_FCTRL_CLOSE_AXI_INF_EN_M = 0x01u << 31,

    /* PSRAM0 - the cache phase description: command, address and dummy lengths */
    kEsp32_RegisterPSRAM_CACHE_SCTRL                  = ESP32_REG_BASE(PSRAM0) + 0x040,
    kEsp32_RegisterPSRAM_CACHE_SCTRL_USR_SADDR_4BYTE_M = 0x01 << 0,
    kEsp32_RegisterPSRAM_CACHE_SCTRL_USR_WR_DUMMY_M   = 0x01 << 3,
    kEsp32_RegisterPSRAM_CACHE_SCTRL_USR_RD_DUMMY_M   = 0x01 << 4,
    kEsp32_RegisterPSRAM_CACHE_SCTRL_USR_RCMD_M       = 0x01 << 5,
    kEsp32_RegisterPSRAM_CACHE_SCTRL_RDUMMY_CYCLELEN_S = 6,
    kEsp32_RegisterPSRAM_CACHE_SCTRL_RDUMMY_CYCLELEN_M = 0x3f << 6,
    kEsp32_RegisterPSRAM_CACHE_SCTRL_ADDR_BITLEN_S    = 14,
    kEsp32_RegisterPSRAM_CACHE_SCTRL_ADDR_BITLEN_M    = 0x3f << 14,
    kEsp32_RegisterPSRAM_CACHE_SCTRL_USR_WCMD_M       = 0x01 << 20,
    kEsp32_RegisterPSRAM_CACHE_SCTRL_SRAM_OCT_M       = 0x01 << 21,
    kEsp32_RegisterPSRAM_CACHE_SCTRL_WDUMMY_CYCLELEN_S = 22,
    kEsp32_RegisterPSRAM_CACHE_SCTRL_WDUMMY_CYCLELEN_M = 0x3f << 22,

    /* PSRAM0 - which phases go out on eight lines, and which on sixteen */
    kEsp32_RegisterPSRAM_SRAM_CMD                     = ESP32_REG_BASE(PSRAM0) + 0x044,
    kEsp32_RegisterPSRAM_SRAM_CMD_SDIN_OCT_M          = 0x01 << 18,
    kEsp32_RegisterPSRAM_SRAM_CMD_SDOUT_OCT_M         = 0x01 << 19,
    kEsp32_RegisterPSRAM_SRAM_CMD_SADDR_OCT_M         = 0x01 << 20,
    kEsp32_RegisterPSRAM_SRAM_CMD_SCMD_OCT_M          = 0x01 << 21,
    kEsp32_RegisterPSRAM_SRAM_CMD_SDUMMY_WOUT_M       = 0x01 << 23,
    kEsp32_RegisterPSRAM_SRAM_CMD_SDIN_HEX_M          = 0x01 << 26,
    kEsp32_RegisterPSRAM_SRAM_CMD_SDOUT_HEX_M         = 0x01 << 27,

    /* PSRAM0 - the opcodes the cache issues */
    kEsp32_RegisterPSRAM_SRAM_DRD_CMD                 = ESP32_REG_BASE(PSRAM0) + 0x048,
    kEsp32_RegisterPSRAM_SRAM_DWR_CMD                 = ESP32_REG_BASE(PSRAM0) + 0x04c,
    kEsp32_RegisterPSRAM_SRAM_CMD_VALUE_S             = 0,
    kEsp32_RegisterPSRAM_SRAM_CMD_VALUE_M             = 0xffff << 0,
    kEsp32_RegisterPSRAM_SRAM_CMD_BITLEN_S            = 28,
    kEsp32_RegisterPSRAM_SRAM_CMD_BITLEN_M            = 0x0fu << 28,

    /*
     * PSRAM0 and PSRAM1 bus clock.  Both hold the same three counter fields at
     * the same shifts and an equal-to-sysclk bit at 31; only the offset within
     * the block differs, which is why they cannot share an offset enum.
     */
    kEsp32_RegisterPSRAM_SRAM_CLK                     = ESP32_REG_BASE(PSRAM0) + 0x050,
    kEsp32_RegisterPSRAM_USER_CLK                     = ESP32_REG_BASE(PSRAM1) + 0x014,
    kEsp32_RegisterPSRAM_CLK_CLKCNT_L_S               = 0,
    kEsp32_RegisterPSRAM_CLK_CLKCNT_H_S               = 8,
    kEsp32_RegisterPSRAM_CLK_CLKCNT_N_S               = 16,
    kEsp32_RegisterPSRAM_CLK_EQU_SYSCLK_M             = 0x01u << 31,

    /* PSRAM0 - double data rate, and the variable dummy the cache side uses */
    kEsp32_RegisterPSRAM_SMEM_DDR                     = ESP32_REG_BASE(PSRAM0) + 0x0d8,
    kEsp32_RegisterPSRAM_SMEM_DDR_EN_M                = 0x01 << 0,
    kEsp32_RegisterPSRAM_SMEM_DDR_VAR_DUMMY_M         = 0x01 << 1,
    kEsp32_RegisterPSRAM_SMEM_DDR_RDAT_SWP_M          = 0x01 << 2,
    kEsp32_RegisterPSRAM_SMEM_DDR_WDAT_SWP_M          = 0x01 << 3,

    /* PSRAM1 - the variable dummy the user side uses, its own register */
    kEsp32_RegisterPSRAM_USER_DDR                     = ESP32_REG_BASE(PSRAM1) + 0x0d4,
    kEsp32_RegisterPSRAM_USER_DDR_VAR_DUMMY_M         = 0x01 << 1,

    /*
     * PSRAM1 data buffer, W0 to W15.  A user transaction reads its write data
     * from here and leaves its read data behind, so a read of a part that never
     * answered returns whatever the last transaction left.
     */
    kEsp32_RegisterPSRAM_USER_W0                      = ESP32_REG_BASE(PSRAM1) + 0x058,
    kEsp32_RegisterPSRAM_USER_NUM_DATA_WORDS          = 16,

    /*
     * The DLL calibration enables.  Both are PSRAM0 registers even though one
     * of them is the PSRAM1 controller's: the mspi id selects which of these
     * two words is written, not which block.
     */
    kEsp32_RegisterPSRAM_TIMING_CALI                  = ESP32_REG_BASE(PSRAM0) + 0x180,
    kEsp32_RegisterPSRAM_SMEM_TIMING_CALI             = ESP32_REG_BASE(PSRAM0) + 0x190,
    kEsp32_RegisterPSRAM_TIMING_CALI_DLL_M            = 0x01 << 5,

    /* PSRAM0 - chip select setup, hold and inter-transaction delay */
    kEsp32_RegisterPSRAM_SMEM_AC                      = ESP32_REG_BASE(PSRAM0) + 0x1a0,
    kEsp32_RegisterPSRAM_SMEM_AC_CS_SETUP_M           = 0x01 << 0,
    kEsp32_RegisterPSRAM_SMEM_AC_CS_HOLD_M            = 0x01 << 1,
    kEsp32_RegisterPSRAM_SMEM_AC_CS_SETUP_TIME_S      = 2,
    kEsp32_RegisterPSRAM_SMEM_AC_CS_SETUP_TIME_M      = 0x1f << 2,
    kEsp32_RegisterPSRAM_SMEM_AC_CS_HOLD_TIME_S       = 7,
    kEsp32_RegisterPSRAM_SMEM_AC_CS_HOLD_TIME_M       = 0x1f << 7,
    kEsp32_RegisterPSRAM_SMEM_AC_CS_HOLD_DELAY_S      = 25,
    kEsp32_RegisterPSRAM_SMEM_AC_CS_HOLD_DELAY_M      = 0x3f << 25,
};

/*
 * MSPI pads
 *
 * The twenty PSRAM pads are not in IO_MUX's pad file and are not routable: the
 * data sheet calls them dedicated, so there is no matrix select to write and no
 * GPIO number to claim.  Their configuration words are twenty consecutive
 * registers from PAD_FIRST to PAD_LAST, so drive strength is set by walking
 * them rather than by naming each one.
 *
 * The two DQS pads are the exception in that walk.  They carry a phase select
 * and a strobe power-down in the low bits, which pushes their drive field three
 * bits higher than every other pad's.
 */
enum Esp32_RegisterMSPI_IOMUX {
    kEsp32_RegisterMSPI_IOMUX_PAD_FIRST               = ESP32_REG_BASE(MSPI_IOMUX) + 0x1c,
    kEsp32_RegisterMSPI_IOMUX_PAD_LAST                = ESP32_REG_BASE(MSPI_IOMUX) + 0x68,
    kEsp32_RegisterMSPI_IOMUX_PAD_DQS0                = ESP32_REG_BASE(MSPI_IOMUX) + 0x3c,
    kEsp32_RegisterMSPI_IOMUX_PAD_DQS1                = ESP32_REG_BASE(MSPI_IOMUX) + 0x68,

    kEsp32_RegisterMSPI_IOMUX_PAD_DRV_S               = 12,
    kEsp32_RegisterMSPI_IOMUX_PAD_DRV_M               = 0x03 << 12,
    kEsp32_RegisterMSPI_IOMUX_DQS_DRV_S               = 15,
    kEsp32_RegisterMSPI_IOMUX_DQS_DRV_M               = 0x03 << 15,
    /* Power up the strobe receiver.  Without it a DDR read returns nothing. */
    kEsp32_RegisterMSPI_IOMUX_DQS_XPD_M               = 0x01 << 0,
};

/*
 * PMU, LP_CLKRST, LPPERI and I2C_ANA_MST
 *
 * These four blocks are here only because the PSRAM die is powered from the
 * MPLL - SOC_PSRAM_VDD_POWER_MPLL is 1 - and the MPLL's divider lives behind
 * the analog register I2C bus rather than in any memory mapped register.  PMU
 * powers the MSPI PHY, LP_CLKRST gates the MPLL's output, and LPPERI clocks the
 * I2C master that I2C_ANA_MST fronts.
 *
 * The ROM exports no regi2c entry point on this part, so the transaction is
 * built by hand in Esp32_PSRAM.c: slave id, register address, a write flag and
 * a data byte packed into one control word, with BUSY polled either side.
 */
enum Esp32_RegisterPMU {
    kEsp32_RegisterPMU_RF_PWC                         = ESP32_REG_BASE(PMU) + 0x15c,
    kEsp32_RegisterPMU_RF_PWC_MSPI_PHY_XPD_M          = 0x01 << 24,

    /*
     * External LDO channel 2 - the 1.8V rail that powers the PSRAM die and the
     * MPLL together.  The analog design numbers the outputs VO1..VO4 from one
     * while the PMU's register file is a six element array indexed from zero,
     * and the two orders do not agree: channel 2 is array slot 3, whose pair
     * of words the SoC header names PMU_EXT_LDO_P1_0P1A_REG (0x1d0) and
     * PMU_EXT_LDO_P1_0P1A_ANA_REG (0x1d4).
     *
     * Output voltage is Vref * (1 + 0.25 * MUL), with Vref selected by DREF -
     * 0.5V + 0.05V per step below 9, then 1V + 0.1V per step.  TIEH forces the
     * 3.3V input rail through instead and must stay clear for 1.8V.
     * FORCE_TIEH_SEL hands control to software rather than to the eFuse.
     */
    kEsp32_RegisterPMU_EXT_LDO_CHAN2                  = ESP32_REG_BASE(PMU) + 0x1d0,
    kEsp32_RegisterPMU_EXT_LDO_CHAN2_FORCE_TIEH_SEL_M = 0x01 << 7,
    kEsp32_RegisterPMU_EXT_LDO_CHAN2_XPD_M            = 0x01 << 8,
    kEsp32_RegisterPMU_EXT_LDO_CHAN2_TIEH_SEL_S       = 9,
    kEsp32_RegisterPMU_EXT_LDO_CHAN2_TIEH_SEL_M       = 0x07 << 9,
    kEsp32_RegisterPMU_EXT_LDO_CHAN2_TIEH_M           = 0x01 << 14,

    kEsp32_RegisterPMU_EXT_LDO_CHAN2_ANA              = ESP32_REG_BASE(PMU) + 0x1d4,
    kEsp32_RegisterPMU_EXT_LDO_CHAN2_ANA_MUL_S        = 23,
    kEsp32_RegisterPMU_EXT_LDO_CHAN2_ANA_MUL_M        = 0x07 << 23,
    kEsp32_RegisterPMU_EXT_LDO_CHAN2_ANA_EN_VDET_M    = 0x01 << 26,
    kEsp32_RegisterPMU_EXT_LDO_CHAN2_ANA_DREF_S       = 28,
    kEsp32_RegisterPMU_EXT_LDO_CHAN2_ANA_DREF_M       = 0x0fu << 28,
};

enum Esp32_RegisterLP_CLKRST {
    kEsp32_RegisterLP_CLKRST_HP_CLK_CTRL              = ESP32_REG_BASE(LP_CLKRST) + 0x40,
    kEsp32_RegisterLP_CLKRST_HP_CLK_CTRL_MPLL_EN_M    = 0x01 << 28,
};

enum Esp32_RegisterLPPERI {
    kEsp32_RegisterLPPERI_CLK_EN                      = ESP32_REG_BASE(LPPERI) + 0x00,
    kEsp32_RegisterLPPERI_CLK_EN_I2CMST_M             = 0x01 << 27,
};

enum Esp32_RegisterI2C_ANA_MST {
    kEsp32_RegisterI2C_ANA_MST_I2C0_CTRL              = ESP32_REG_BASE(I2C_ANA_MST) + 0x00,
    kEsp32_RegisterI2C_ANA_MST_I2C0_CTRL_SLAVE_ID_S   = 0,
    kEsp32_RegisterI2C_ANA_MST_I2C0_CTRL_ADDR_S       = 8,
    kEsp32_RegisterI2C_ANA_MST_I2C0_CTRL_DATA_S       = 16,
    kEsp32_RegisterI2C_ANA_MST_I2C0_CTRL_DATA_M       = 0xff << 16,
    kEsp32_RegisterI2C_ANA_MST_I2C0_CTRL_WR_M         = 0x01 << 24,
    kEsp32_RegisterI2C_ANA_MST_I2C0_CTRL_BUSY_M       = 0x01 << 25,

    /* Which analog block the master talks to.  One bit per block; MSPI is 0x63. */
    kEsp32_RegisterI2C_ANA_MST_ANA_CONF1              = ESP32_REG_BASE(I2C_ANA_MST) + 0x1c,
    kEsp32_RegisterI2C_ANA_MST_ANA_CONF2              = ESP32_REG_BASE(I2C_ANA_MST) + 0x20,
    kEsp32_RegisterI2C_ANA_MST_ANA_CONF_SEL_M         = 0x00ffffff,
    kEsp32_RegisterI2C_ANA_MST_ANA_CONF2_MSPI_SEL_M   = 0x01 << 9,

    /* The master's own clock source.  The bootloader already selects 160MHz for
     * its PLL work, but the bit is cheap to reassert and the app cannot see
     * what the bootloader did. */
    kEsp32_RegisterI2C_ANA_MST_CLK160M                = ESP32_REG_BASE(I2C_ANA_MST) + 0x34,
    kEsp32_RegisterI2C_ANA_MST_CLK160M_SEL_M          = 0x01 << 0,
};

/*
 * CACHE
 *
 * Two levels: per core L1 instruction and data caches, and a shared L2.  The
 * registers here hold only SHUT bits and bypass selects - there is no enable
 * bit anywhere in the block, so a cache is brought up through the ROM's
 * Cache_Enable_* entry points and there is nothing to test to find out whether
 * it is already up.  See Esp32_Cache.c.
 *
 * The L1 SHUT bits reset to 0, so the esp32c3's "un-shut both buses first" step
 * has no counterpart here; only L2's SHUT_DMA resets set.
 */
enum Esp32_RegisterCACHE {
    kEsp32_RegisterCACHE_L1_ICACHE_CTRL               = ESP32_REG_BASE(CACHE) + 0x000,
    /* Cut core 0's instruction bus off from L1-ICache.  0 enables, 1 disables. */
    kEsp32_RegisterCACHE_L1_ICACHE_CTRL_SHUT_IBUS0_M  = 0x01 << 0,

    kEsp32_RegisterCACHE_L1_DCACHE_CTRL               = ESP32_REG_BASE(CACHE) + 0x004,
    kEsp32_RegisterCACHE_L1_DCACHE_CTRL_SHUT_DBUS0_M  = 0x01 << 0,
    kEsp32_RegisterCACHE_L1_DCACHE_CTRL_SHUT_DMA_M    = 0x01 << 4,

    kEsp32_RegisterCACHE_L1_BYPASS_CONF               = ESP32_REG_BASE(CACHE) + 0x008,
    kEsp32_RegisterCACHE_L1_BYPASS_CONF_ICACHE0_EN_M  = 0x01 << 0,

    kEsp32_RegisterCACHE_L2_CACHE_CTRL                = ESP32_REG_BASE(CACHE) + 0x270,
    /* The only field in the register, and the only SHUT bit that resets set */
    kEsp32_RegisterCACHE_L2_CACHE_CTRL_SHUT_DMA_M     = 0x01 << 4,

    kEsp32_RegisterCACHE_L2_BYPASS_CONF               = ESP32_REG_BASE(CACHE) + 0x274,
    kEsp32_RegisterCACHE_L2_BYPASS_CONF_EN_M          = 0x01 << 5,
};

/*
 * GPIO
 *
 * 55 pads, GPIO0..GPIO54, so there is a second bank throughout - OUT1, ENABLE1,
 * IN1 and STATUS1 - and pad n is bit n%32 of bank n/32.  The register file
 * defines a PIN56 configuration word, but SOC_GPIO_PIN_COUNT is 55 and
 * everything above GPIO54 is unusable.
 *
 * The matrix select fields are a bit wider than the esp32c3's: IN_SEL is 6 bits
 * and OUT_SEL 9, which puts every flag above them one bit higher and makes the
 * "hand the pad back to GPIO_OUT" value 256 rather than 128.
 */
enum Esp32_RegisterGPIO {
    kEsp32_RegisterGPIO_OUT                           = ESP32_REG_BASE(GPIO) + 0x004,
    kEsp32_RegisterGPIO_OUT_W1TS                      = ESP32_REG_BASE(GPIO) + 0x008,
    kEsp32_RegisterGPIO_OUT_W1TC                      = ESP32_REG_BASE(GPIO) + 0x00c,
    kEsp32_RegisterGPIO_OUT1                          = ESP32_REG_BASE(GPIO) + 0x010,
    kEsp32_RegisterGPIO_OUT1_W1TS                     = ESP32_REG_BASE(GPIO) + 0x014,
    kEsp32_RegisterGPIO_OUT1_W1TC                     = ESP32_REG_BASE(GPIO) + 0x018,

    kEsp32_RegisterGPIO_ENABLE                        = ESP32_REG_BASE(GPIO) + 0x020,
    kEsp32_RegisterGPIO_ENABLE_W1TS                   = ESP32_REG_BASE(GPIO) + 0x024,
    kEsp32_RegisterGPIO_ENABLE_W1TC                   = ESP32_REG_BASE(GPIO) + 0x028,
    kEsp32_RegisterGPIO_ENABLE1                       = ESP32_REG_BASE(GPIO) + 0x02c,
    kEsp32_RegisterGPIO_ENABLE1_W1TS                  = ESP32_REG_BASE(GPIO) + 0x030,
    kEsp32_RegisterGPIO_ENABLE1_W1TC                  = ESP32_REG_BASE(GPIO) + 0x034,

    kEsp32_RegisterGPIO_IN                            = ESP32_REG_BASE(GPIO) + 0x03c,
    kEsp32_RegisterGPIO_IN1                           = ESP32_REG_BASE(GPIO) + 0x040,

    kEsp32_RegisterGPIO_STATUS                        = ESP32_REG_BASE(GPIO) + 0x044,
    kEsp32_RegisterGPIO_STATUS_W1TS                   = ESP32_REG_BASE(GPIO) + 0x048,
    kEsp32_RegisterGPIO_STATUS_W1TC                   = ESP32_REG_BASE(GPIO) + 0x04c,
    kEsp32_RegisterGPIO_STATUS1                       = ESP32_REG_BASE(GPIO) + 0x050,
    kEsp32_RegisterGPIO_STATUS1_W1TS                  = ESP32_REG_BASE(GPIO) + 0x054,
    kEsp32_RegisterGPIO_STATUS1_W1TC                  = ESP32_REG_BASE(GPIO) + 0x058,

    /* Per pad configuration, one word per pad, indexed by pad number */
    kEsp32_RegisterGPIO_PIN0                          = ESP32_REG_BASE(GPIO) + 0x074,
    kEsp32_RegisterGPIO_PIN_PAD_DRIVER_M              = 0x01 << 2,
    kEsp32_RegisterGPIO_PIN_INT_TYPE_S                = 7,
    kEsp32_RegisterGPIO_PIN_INT_TYPE_M                = 0x07 << 7,
    kEsp32_RegisterGPIO_PIN_INT_ENA_S                 = 13,
    kEsp32_RegisterGPIO_PIN_INT_ENA_M                 = 0x1fu << 13,
    /*
     * This part has four GPIO interrupt outputs, GPIO_INTR0..3, and INT_ENA has
     * a bit for each along with the NMI variants.  Bit 0 is GPIO_INTR0, which is
     * the only one IDF wires up and the only one this BSP routes.
     */
    kEsp32_RegisterGPIO_PIN_INT_ENA_V                 = 0x01,

    /*
     * GPIO matrix.  IN_SEL_CFG is indexed by peripheral input signal and names
     * the pad that drives it; OUT_SEL_CFG is indexed by pad and names the
     * peripheral output signal that drives it.
     *
     * An IN_SEL of 0x3f reads a constant 1 and 0x3e a constant 0, which is also
     * IN_SEL's reset value - an unconfigured input signal sees a steady high.
     */
    kEsp32_RegisterGPIO_FUNC0_IN_SEL_CFG              = ESP32_REG_BASE(GPIO) + 0x158,
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_IN_SEL_S      = 0,
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_IN_SEL_M      = 0x3fu << 0,
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_IN_INVERT_M   = 0x01 << 6,
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_USE_MATRIX_M  = 0x01 << 7,

    kEsp32_RegisterGPIO_FUNC0_OUT_SEL_CFG             = ESP32_REG_BASE(GPIO) + 0x558,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OUT_SEL_S    = 0,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OUT_SEL_M    = 0x1ffu << 0,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OUT_INV_M    = 0x01 << 9,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OEN_SEL_M    = 0x01 << 10,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OEN_INV_M    = 0x01 << 11,
    /* Writing this to OUT_SEL_CFG hands the pad back to the GPIO output register
     * instead of a peripheral - SIG_GPIO_OUT_IDX, and OUT_SEL's reset value */
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_GPIO_OUT_V   = 256,

    /* Highest pad number on this part */
    kEsp32_RegisterGPIO_MAX_PAD                       = 54,
};

/*
 * IO_MUX
 *
 * The pad registers run in pad order from IO_MUX + 0x04, so a pad's register is
 * ESP32_IO_MUX_PAD_REG(nPin) for GPIO0..GPIO54.  IO_MUX + 0x00 is not a pad.
 * The field positions are the esp32c3's unchanged, though v5.4 names the two
 * pull bits FUN_PD and FUN_PU where the older headers say FUN_WPD and FUN_WPU;
 * the names here follow the older spelling the GPIO driver already uses.
 *
 * FILTER_EN has no esp32c3 counterpart: it drops input pulses shorter than two
 * IO_MUX clock cycles.  Left alone by this BSP.
 */
enum Esp32_RegisterIO_MUX {
    kEsp32_RegisterIO_MUX_PAD0                        = ESP32_REG_BASE(IO_MUX) + 0x004,

    kEsp32_RegisterIO_MUX_SLP_SEL_M                   = 0x01 << 1,
    kEsp32_RegisterIO_MUX_FUN_WPD_M                   = 0x01 << 7,
    kEsp32_RegisterIO_MUX_FUN_WPU_M                   = 0x01 << 8,
    kEsp32_RegisterIO_MUX_FUN_IE_M                    = 0x01 << 9,
    kEsp32_RegisterIO_MUX_FUN_DRV_S                   = 10,
    kEsp32_RegisterIO_MUX_FUN_DRV_M                   = 0x03 << 10,
    kEsp32_RegisterIO_MUX_MCU_SEL_S                   = 12,
    kEsp32_RegisterIO_MUX_MCU_SEL_M                   = 0x07 << 12,
    kEsp32_RegisterIO_MUX_FILTER_EN_M                 = 0x01 << 15,
};

/*
 * IO_MUX pad registers are addressed by pad number.
 */
#define ESP32_IO_MUX_PAD_REG(n)            (*(volatile u32 *)(kEsp32_RegisterIO_MUX_PAD0 + ((n) * 4)))

/* WDEV_RND Register - the hardware random number generator.  Not part of any
 * peripheral block; soc/wdev_reg.h puts it at 0x501101a4 on this part. */
enum Esp32_RegisterWDEV {
    kEsp32_RegisterWDEV_RND                           = 0x501101a4,
};

/*
 * UART registers are addressed by unit number and register offset.
 */
#define ESP32_UART_REG(u, r)               (*(volatile u32 *)((ESP32_REG_BASE(UART ## u)) + kEsp32_RegisterUART_ ## r))

/*
 * MSPI registers are likewise addressed by unit number (0 or 1) and offset.
 */
#define ESP32_SPIMEM_REG(n, r)             (*(volatile u32 *)((ESP32_REG_BASE(SPI ## n)) + kEsp32_RegisterSPIMEM_ ## r))

/*
 * The MSPI pad configuration words are walked by address rather than named, so
 * they are reached through a plain address rather than a register accessor.
 */
#define ESP32_MSPI_PAD_REG(a)              (*(volatile u32 *)(a))

/*
 * Register accessors
 */
#define ESP32_REG(r)                       (*(volatile u32 *)kEsp32_Register ## r)
#define ESP32_REG_ADDR(r)                  ((volatile u32 *)kEsp32_Register ## r)
#define ESP32_REG_ARRAY_VALUE(r, i)        (*((volatile u32 *)kEsp32_Register ## r + (i)))

#define ESP32_REG_MASK(r, m)               (kEsp32_Register ## r ## _ ## m ## _M)
#define ESP32_REG_SHIFT(r, s)              (kEsp32_Register ## r ## _ ## s ## _S)
#define ESP32_REG_VAL(r, v)                (kEsp32_Register ## r ## _ ## v ## _V)

#endif /* PLATFORMS_ESP32_INCLUDE_ESP32P4_REGISTERS_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Sep-26   claudius    created
 *  28-Sep-26   claudius    added the PSRAM controller, MSPI pads, MPLL and
 *                          external LDO registers the PSRAM bring-up needs
 *  29-Sep-26   claudius    added the flash MMU sensitive bit
 */
