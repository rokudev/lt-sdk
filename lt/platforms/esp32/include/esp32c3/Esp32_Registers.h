/******************************************************************************
 * Esp32_Registers.h                                               ESP32-C3 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * This is the esp32c3 counterpart of include/esp32s3/Esp32_Registers.h.  It
 * covers what the second stage bootloader, the Esp32c3 drivers and
 * Esp32_LTCoreBSP reference; blocks with no caller on this part are not
 * reproduced.
 *
 * The esp32c3 is a RISC-V part, and that shows up here in two places beyond the
 * addresses.  It is single core, so SYSTEM has no CORE_1_CONTROL registers and
 * every register after CPU_PER_CONF sits lower in the block than its esp32s3
 * counterpart - none of the SYSTEM offsets carry over.  And it has no Xtensa
 * CCOMPARE, so the kernel tick comes from the SYSTIMER block, which the esp32s3
 * header has no need to describe at all.
 *
 * Every value below was taken from the vendored esp32c3 soc headers in
 * source/esp32/ltbootloader/include/esp32c3/soc (soc.h, system_reg.h,
 * rtc_cntl_reg.h, uart_reg.h, gpio_reg.h, io_mux_reg.h, interrupt_core0_reg.h,
 * usb_serial_jtag_reg.h, systimer_reg.h, extmem_reg.h, spi_mem_reg.h,
 * cache_memory.h) rather than adapted from the esp32s3 numbering.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32C3_REGISTERS_H
#define PLATFORMS_ESP32_INCLUDE_ESP32C3_REGISTERS_H

/*
 * Peripheral base addresses
 *
 * There is one UART pair, one general purpose SPI host, and no RTCIO block -
 * the esp32s3's RTCIO, SPI3 and UART2 have no counterpart here.
 */
typedef u32 Esp32_RegisterBase;
enum Esp32_RegisterBase {
    kEsp32_RegisterBase_SYSTEM       = 0x600c0000,
    kEsp32_RegisterBase_SENSITIVE    = 0x600c1000,
    kEsp32_RegisterBase_INTERRUPT    = 0x600c2000,
    kEsp32_RegisterBase_EXTMEM       = 0x600c4000,
    kEsp32_RegisterBase_ASSIST_DEBUG = 0x600ce000,
    kEsp32_RegisterBase_AES          = 0x6003a000,
    kEsp32_RegisterBase_SHA          = 0x6003b000,
    kEsp32_RegisterBase_RSA          = 0x6003c000,
    kEsp32_RegisterBase_UART0        = 0x60000000,
    kEsp32_RegisterBase_UART1        = 0x60010000,
    kEsp32_RegisterBase_GPIO         = 0x60004000,
    kEsp32_RegisterBase_RTC_CNTL     = 0x60008000,
    kEsp32_RegisterBase_EFUSE        = 0x60008800,
    kEsp32_RegisterBase_IO_MUX       = 0x60009000,
    kEsp32_RegisterBase_LEDC         = 0x60019000,
    kEsp32_RegisterBase_TIMG0        = 0x6001f000,
    kEsp32_RegisterBase_TIMG1        = 0x60020000,
    kEsp32_RegisterBase_SYSTIMER     = 0x60023000,
    kEsp32_RegisterBase_APB_CTRL     = 0x60026000,
    kEsp32_RegisterBase_SPI0         = 0x60003000,
    kEsp32_RegisterBase_SPI1         = 0x60002000,
    kEsp32_RegisterBase_SPI2         = 0x60024000,
    kEsp32_RegisterBase_USB_DEVICE   = 0x60043000,
};

#define ESP32_REG_BASE(n)                       (kEsp32_RegisterBase_ ## n)

/*
 * Register Definitions
 */
typedef u32 Esp32_Register;

/* RTC_CNTL Registers */
enum Esp32_RegisterRTC_CNTL {
    kEsp32_RegisterRTC_CNTL_OPTIONS0                  = ESP32_REG_BASE(RTC_CNTL),
    /* System reset - RTC_CNTL_SW_SYS_RST, BIT(31) */
    kEsp32_RegisterRTC_CNTL_SW_SYS_RST_V              = 0x80000000,

    /*
     * SW_SYS_RST is named for a system reset but delivers a core reset - "reset
     * the whole digital system except RTC sub-system", per the table at the head
     * of soc/reset_reasons.h - and it reports itself as RESET_REASON_CORE_SW.  So
     * this register, sitting in the RTC sub-system, resets nothing of itself, and
     * neither does anything else here: the BBPLL's power state, the watchdogs,
     * USB_CONF and the retention words all carry through to the warm boot.
     *
     * It is written as a bare store rather than read-modify-write, matching what
     * the ROM bootloader does in bootloader_utility.c.  The zeroes that go into
     * the other fields are the point rather than collateral: they clear both the
     * FORCE_ISO and the FORCE_NOISO bits, and both XTL force bits, handing the
     * isolation cells and the crystal back to automatic hardware control.
     */

    /*
     * RTC watchdog.  The esp32c3 keeps the esp32's WDTCONFIG0 field layout bit
     * for bit and so takes the same setup magic, but the whole group sits at its
     * own offsets - 0x90 here against 0x8c on the esp32 and 0x98 on the esp32s3:
     *
     *   WDT_EN               BIT(31)   = 1
     *   WDT_STG0             [30:28]   = 4 (reset RTC, i.e. the whole chip)
     *   WDT_STG1..3                    = 0 (off)
     *   WDT_CPU_RESET_LENGTH [18:16]   = 1
     *   WDT_SYS_RESET_LENGTH [15:13]   = 7
     *   WDT_FLASHBOOT_MOD_EN BIT(12)   = 0
     *   WDT_PROCPU_RESET_EN  BIT(11)   = 1
     *   WDT_PAUSE_IN_SLP     BIT(9)    = 1
     *   WDT_CHIP_RESET_EN    BIT(8)    = 1
     *   WDT_CHIP_RESET_WIDTH [7:0]     = 0x80
     *
     * FLASHBOOT_MOD_EN is left clear for the reason the esp32s3 header sets out
     * at length: it is a second arming path that does not go through WDT_EN, the
     * ROM turns it on so a bootloader that never finishes still resets the part,
     * and carrying it in these constants would make Esp32DisableRTCWatchdog()
     * re-assert it in the same store that clears WDT_EN.
     *
     * WDT_CONFIG0 + 4 is WDTCONFIG1, whose whole 32 bits are the stage 0 hold
     * count, so Esp32SetTimeoutRTCWatchdog() can index it as an array element.
     */
    kEsp32_RegisterRTC_CNTL_WDT_CONFIG0               = ESP32_REG_BASE(RTC_CNTL) + 0x90,
    kEsp32_RegisterRTC_CNTL_WDT_SETUP_EN_V            = 0xc001eb80,
    kEsp32_RegisterRTC_CNTL_WDT_SETUP_DIS_V           = 0x4001eb80,

    kEsp32_RegisterRTC_CNTL_WDT_ENABLED_M             = 0x80000000,
    kEsp32_RegisterRTC_CNTL_WDT_FLASHBOOT_MOD_EN_M    = 0x01 << 12,

    kEsp32_RegisterRTC_CNTL_WDTFEED                   = ESP32_REG_BASE(RTC_CNTL) + 0xa4,
    /* Write only, RTC_CNTL_WDT_FEED, BIT(31) */
    kEsp32_RegisterRTC_CNTL_WDT_FEED_V                = 0x80000000,

    kEsp32_RegisterRTC_CNTL_WDTWPROTECT               = ESP32_REG_BASE(RTC_CNTL) + 0xa8,
    kEsp32_RegisterRTC_CNTL_WDT_UNPROTECT_V           = 0x50d83aa1,
    kEsp32_RegisterRTC_CNTL_WDT_PROTECT_V             = 0x0,

    /*
     * Super watchdog.  It cannot be disabled, only fed, and AUTO_FEED_EN hands
     * that to hardware - which is how the bootloader survives it
     * (bootloader_super_wdt_auto_feed(), the first thing bootloader_init() does).
     * It sits behind its own key, not WDTWPROTECT's.
     */
    kEsp32_RegisterRTC_CNTL_SWD_CONF                  = ESP32_REG_BASE(RTC_CNTL) + 0xac,
    kEsp32_RegisterRTC_CNTL_SWD_CONF_AUTO_FEED_EN_M   = 0x01u << 31,

    kEsp32_RegisterRTC_CNTL_SWDWPROTECT               = ESP32_REG_BASE(RTC_CNTL) + 0xb0,
    kEsp32_RegisterRTC_CNTL_SWD_UNPROTECT_V           = 0x8f1d312a,
    kEsp32_RegisterRTC_CNTL_SWD_PROTECT_V             = 0x0,

    /*
     * Pad hold.  The esp32s3 needed two registers because it has 49 pads; the
     * esp32c3's 22, GPIO0..GPIO21, all fit in PAD_HOLD at bit == pad number, so
     * there is no DIG_PAD_HOLD counterpart here.
     */
    kEsp32_RegisterRTC_CNTL_PAD_HOLD                  = ESP32_REG_BASE(RTC_CNTL) + 0xd0,

    /*
     * USB serial/JTAG reset behaviour.  The esp32s3 has two bits here, one
     * holding the USB device and one the IO_MUX, across a system reset.  The
     * esp32c3 has only the IO_MUX one - there is no USB_RESET_DISABLE on this
     * part - so the device itself always re-enumerates on reset and a host
     * watching the boot log will lose its port each time.
     */
    kEsp32_RegisterRTC_CNTL_USB_CONF                  = ESP32_REG_BASE(RTC_CNTL) + 0xec,
    kEsp32_RegisterRTC_CNTL_USB_CONF_IO_MUX_RESET_DISABLE_M  = 0x01 << 18,

    /*
     * Wi-Fi/BT power domain, shared by both radios on this part.  Clearing
     * WIFI_FORCE_PD powers the domain up and clearing WIFI_FORCE_ISO releases
     * the isolation cells; the reverse order powers it back down.
     */
    kEsp32_RegisterRTC_CNTL_DIG_PWC                   = ESP32_REG_BASE(RTC_CNTL) + 0x88,
    kEsp32_RegisterRTC_CNTL_DIG_PWC_WIFI_FORCE_PD_M   = 0x01 << 17,

    kEsp32_RegisterRTC_CNTL_DIG_ISO                   = ESP32_REG_BASE(RTC_CNTL) + 0x8c,
    kEsp32_RegisterRTC_CNTL_DIG_ISO_WIFI_FORCE_ISO_M  = 0x01 << 28,

    /*
     * The RTC slow clock period in microseconds, Q13.19, as measured by whoever
     * last calibrated it.  A retention word, RTC_CNTL_STORE1, aliased
     * RTC_SLOW_CLK_CAL_REG.  Reads as zero if nothing has calibrated.
     */
    kEsp32_RegisterRTC_CNTL_STORE1                    = ESP32_REG_BASE(RTC_CNTL) + 0x54,

    /*
     * The XTAL frequency in MHz, as the ROM bootloader left it - the retention
     * word RTC_CNTL_STORE4, aliased RTC_XTAL_FREQ_REG.  Stored as two identical
     * 16-bit halves.
     */
    kEsp32_RegisterRTC_CNTL_STORE4                    = ESP32_REG_BASE(RTC_CNTL) + 0xb8,
};

/*
 * Radio clock gating and reset, in the APB_CTRL block (IDF calls the same block
 * SYSCON here, and names these two registers SYSTEM_WIFI_CLK_EN_REG and
 * SYSTEM_WIFI_RST_EN_REG).  Wi-Fi and BT share the COMMON clock bits, so
 * whichever radio comes up second must not clear them.
 */
enum Esp32_RegisterAPB_CTRL {
    kEsp32_RegisterAPB_CTRL_WIFI_CLK_EN               = ESP32_REG_BASE(APB_CTRL) + 0x14,
    kEsp32_RegisterAPB_CTRL_WIFI_CLK_WIFI_BT_COMMON_M = 0x0078078f,

    kEsp32_RegisterAPB_CTRL_WIFI_RST_EN               = ESP32_REG_BASE(APB_CTRL) + 0x18,
    /* MODEM_RESET_FIELD_WHEN_PU: WIFIBB, FE, WIFIMAC, BTBB, BTMAC, RW_BTMAC,
     * RW_BTMAC_REG and BTBB_REG, pulsed after the power domain comes up. */
    kEsp32_RegisterAPB_CTRL_WIFI_RST_MODEM_WHEN_PU_M  = 0x00002a1f,
    /* SYSTEM_WIFIMAC_RST, pulsed on its own to reset just the Wi-Fi MAC. */
    kEsp32_RegisterAPB_CTRL_WIFI_RST_WIFIMAC_M        = 0x01 << 2,
};

/*
 * Timer group watchdogs (MWDT0 and MWDT1), one per timer group.
 *
 * Only what is needed to make sure they are off.  MWDT0 matters at boot for the
 * same reason RWDT does: the ROM arms it with flash boot protection, and the
 * bootloader clearing that bit is the only thing that stops it firing later.
 * MWDT1 the ROM leaves alone, but it costs one register write to be sure.
 *
 * Both groups share one layout, so the addresses are spelled out per group and
 * the fields named once against a TIMG prefix.  The write protect key is the
 * same 0x50d83aa1 RWDT uses, but each group has its own lock.
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
 * SYSTEM
 *
 * None of these offsets match the esp32s3's.  The esp32s3 opens the block with
 * two CORE_1_CONTROL registers for its second core; the esp32c3 has no second
 * core and no such registers, so everything from CPU_PER_CONF on sits eight
 * bytes lower, and SYSCLK_CONF ends up at 0x58 rather than 0x60.  Take every
 * value here from system_reg.h, never by adapting the esp32s3 header.
 */
enum Esp32_RegisterSYSTEM {
    /*
     * Source of the radio low power clock.  LPCLK_SEL_XTAL means the divided
     * main crystal rather than the RTC slow clock, which fixes the light sleep
     * calibration the radio blobs ask for at a known rate.
     */
    kEsp32_RegisterSYSTEM_BT_LPCK_DIV_FRAC            = ESP32_REG_BASE(SYSTEM) + 0x24,
    kEsp32_RegisterSYSTEM_BT_LPCK_DIV_FRAC_LPCLK_SEL_XTAL_M = 0x01 << 26,

    /* CPU clock divider select.  The esp32c3 tops out at 160MHz, so unlike the
     * esp32s3 there is no 240MHz setting - CPUPERIOD_SEL is two bits with only
     * values 0 and 1 defined. */
    kEsp32_RegisterSYSTEM_CPU_PER_CONF                = ESP32_REG_BASE(SYSTEM) + 0x08,
    kEsp32_RegisterSYSTEM_CPU_PER_CONF_CPUPERIOD_SEL_S = 0,
    kEsp32_RegisterSYSTEM_CPU_PER_CONF_CPUPERIOD_SEL_M = 0x03 << 0,
    kEsp32_RegisterSYSTEM_CPUPERIOD_80M_V             = 0,
    kEsp32_RegisterSYSTEM_CPUPERIOD_160M_V            = 1,

    kEsp32_RegisterSYSTEM_PERIP_CLK_EN0               = ESP32_REG_BASE(SYSTEM) + 0x10,
    kEsp32_RegisterSYSTEM_PERIP_CLK_EN1               = ESP32_REG_BASE(SYSTEM) + 0x14,
    kEsp32_RegisterSYSTEM_PERIP_RST_EN0               = ESP32_REG_BASE(SYSTEM) + 0x18,
    kEsp32_RegisterSYSTEM_PERIP_RST_EN1               = ESP32_REG_BASE(SYSTEM) + 0x1c,

    /* RSA operand memory power gating */
    kEsp32_RegisterSYSTEM_RSA_PD_CTRL                 = ESP32_REG_BASE(SYSTEM) + 0x38,
    kEsp32_RegisterSYSTEM_RSA_PD_MEM_PD_M             = 0x01 << 0,

    /*
     * SOC_CLK_SEL: 0 -> XTAL, 1 -> PLL, 2 -> FOSC (RC fast), 3 -> reserved.
     * PRE_DIV_CNT only applies while XTAL or FOSC is selected.
     */
    kEsp32_RegisterSYSTEM_SYSCLK_CONF                 = ESP32_REG_BASE(SYSTEM) + 0x58,
    kEsp32_RegisterSYSTEM_SYSCLK_CONF_SOC_CLK_SEL_S   = 10,
    kEsp32_RegisterSYSTEM_SYSCLK_CONF_SOC_CLK_SEL_M   = 0x03 << 10,
    kEsp32_RegisterSYSTEM_SYSCLK_CONF_PRE_DIV_CNT_S   = 0,
    kEsp32_RegisterSYSTEM_SYSCLK_CONF_PRE_DIV_CNT_M   = 0x3ffu << 0,
    kEsp32_RegisterSYSTEM_SOC_CLK_XTAL_V              = 0,
    kEsp32_RegisterSYSTEM_SOC_CLK_PLL_V               = 1,
    kEsp32_RegisterSYSTEM_SOC_CLK_FOSC_V              = 2,
};

/*
 * Interrupt multiplexer.
 *
 * One core, so one block: INTERRUPT_CORE0, with no CORE1 counterpart.  The map
 * registers are one word per source in ETS_*_INTR_SOURCE order, so
 * Esp32_ExternalIrq doubles as the array index - UART0 is source 21 and its map
 * register is at offset 0x54.  Writing 0 detaches the source; writing 1..31
 * attaches it to that CPU interrupt line.
 *
 * Unlike the Xtensa parts, where the CPU interrupt lines have fixed levels and
 * types burned into the core, everything about a line here is programmable:
 * CPU_INT_TYPE picks level or edge per line, CPU_INT_PRI_n gives line n a
 * priority 1..15, CPU_INT_THRESH is the priority the core will accept, and
 * CPU_INT_ENABLE is the mask.  A line only fires when its priority is strictly
 * above the threshold, so a threshold of 0 accepts everything.
 */
enum Esp32_RegisterINTERRUPT {
    kEsp32_RegisterINTERRUPT_CORE0_IRQ_MAP             = ESP32_REG_BASE(INTERRUPT) + 0x000,

    /*
     * Per source, whether that source is asserting into the multiplexer right
     * now - 64 sources across two registers, source n being bit n%32 of
     * INTR_STATUS_(n/32).  This is the one observation point between "the
     * peripheral says it wants attention" and "the core sees a pending line": if
     * a source reads 1 here while CPU_INT_EIP_STATUS shows nothing, the map
     * register or the priority is at fault; if it reads 0 the peripheral never
     * asserted and the multiplexer is blameless.
     */
    kEsp32_RegisterINTERRUPT_CORE0_INTR_STATUS_0       = ESP32_REG_BASE(INTERRUPT) + 0x0f8,
    kEsp32_RegisterINTERRUPT_CORE0_INTR_STATUS_1       = ESP32_REG_BASE(INTERRUPT) + 0x0fc,

    kEsp32_RegisterINTERRUPT_CORE0_CPU_INT_ENABLE      = ESP32_REG_BASE(INTERRUPT) + 0x104,
    /* 0 -> level triggered, 1 -> edge triggered, bit per CPU interrupt line */
    kEsp32_RegisterINTERRUPT_CORE0_CPU_INT_TYPE        = ESP32_REG_BASE(INTERRUPT) + 0x108,
    /* Write 1 to clear a pending edge triggered line.  Level lines ignore it -
     * they clear when the peripheral stops asserting. */
    kEsp32_RegisterINTERRUPT_CORE0_CPU_INT_CLEAR       = ESP32_REG_BASE(INTERRUPT) + 0x10c,
    /* Which lines are pending and above threshold, bit per line */
    kEsp32_RegisterINTERRUPT_CORE0_CPU_INT_EIP_STATUS  = ESP32_REG_BASE(INTERRUPT) + 0x110,

    /* One word per CPU interrupt line, holding its priority in the low 4 bits;
     * indexed by line number, PRI_0 through PRI_31 at 0x114..0x190 */
    kEsp32_RegisterINTERRUPT_CORE0_CPU_INT_PRI_0       = ESP32_REG_BASE(INTERRUPT) + 0x114,
    kEsp32_RegisterINTERRUPT_CORE0_CPU_INT_THRESH      = ESP32_REG_BASE(INTERRUPT) + 0x194,
};

/*
 * SYSTIMER
 *
 * The kernel tick source.  The esp32s3 BSP has no use for this block - Xtensa
 * gives it CCOMPARE - but a RISC-V part has no such core timer, and the esp32c3
 * has no RISC-V mtime either, so the tick comes from here.
 *
 * Two 52-bit up counters (UNIT0, UNIT1) and three comparators (TARGET0..2), any
 * comparator attachable to either counter.  A counter is read by writing UPDATE
 * to its OP register, spinning on VALUE_VALID, then reading VALUE_HI/VALUE_LO;
 * the hardware latches the pair so the two reads cannot tear.
 *
 * A comparator in period mode reloads itself: PERIOD is a 26 bit tick count and
 * the comparator re-arms at now + PERIOD each time it fires, which is what makes
 * a free running tick possible without the ISR having to reprogram anything.
 * Writes to TARGETn_HI/LO and TARGETn_CONF are staged and only take effect when
 * the matching COMPn_LOAD bit is written.
 *
 * The counter runs at a fixed 16MHz regardless of CPU frequency.
 */
enum Esp32_RegisterSYSTIMER {
    kEsp32_RegisterSYSTIMER_CONF                      = ESP32_REG_BASE(SYSTIMER) + 0x00,
    kEsp32_RegisterSYSTIMER_CONF_CLK_EN_M             = 0x01u << 31,
    kEsp32_RegisterSYSTIMER_CONF_UNIT0_WORK_EN_M      = 0x01 << 30,
    /* Freeze UNIT0 while the core is halted by the debugger, so a breakpoint
     * does not produce a burst of catch-up ticks on resume */
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
 */
enum Esp32_RegisterEFUSE {
    kEsp32_RegisterEFUSE_RD_REPEAT_DATA1              = ESP32_REG_BASE(EFUSE) + 0x034,
    kEsp32_RegisterEFUSE_SPI_BOOT_CRYPT_CNT_S         = 18,
    kEsp32_RegisterEFUSE_SPI_BOOT_CRYPT_CNT_M         = 0x07u << 18,

    /*
     * The factory base MAC, EFUSE_RD_MAC_SPI_SYS_0/1.  Word 0 holds the low 32
     * bits and the low half of word 1 the high 16, most significant byte first.
     * There is no MAC CRC beside it - bits 31:16 of word 1 are SPI_PAD_CONF_0 -
     * so readers must not check one.
     */
    kEsp32_RegisterEFUSE_RD_MAC_SPI_SYS_0             = ESP32_REG_BASE(EFUSE) + 0x044,
    kEsp32_RegisterEFUSE_RD_MAC_SPI_SYS_1             = ESP32_REG_BASE(EFUSE) + 0x048,
};

/* UART.  Register offsets and field positions are the esp32s3's unchanged; only
 * the block bases differ. */
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

    /* Bit positions for UART_STATUS.  Both FIFO counters are 10 bits wide. */
    kEsp32_RegisterUART_STATUS_TXFIFO_CNT_S           = 16,
    kEsp32_RegisterUART_STATUS_TXFIFO_CNT_M           = 0x03ff0000,
    kEsp32_RegisterUART_STATUS_RXFIFO_CNT_S           = 0,
    kEsp32_RegisterUART_STATUS_RXFIFO_CNT_M           = 0x000003ff,

    /* Transmitter and receiver state machines, in their own register */
    kEsp32_RegisterUART_FSM_STATUS                    = 0x6c,
    kEsp32_RegisterUART_FSM_STATUS_UTX_OUT_S          = 4,
    kEsp32_RegisterUART_FSM_STATUS_UTX_OUT_M          = 0x000000f0,
    kEsp32_RegisterUART_FSM_STATUS_URX_OUT_S          = 0,
    kEsp32_RegisterUART_FSM_STATUS_URX_OUT_M          = 0x0000000f,
};

/*
 * MSPI (SPI0 / SPI1) - the memory SPI controllers, IDF's SPI_MEM_* block.
 *
 * SPI0 is the cache side: it fetches flash on the cache's behalf and is never
 * driven by software transactions.  SPI1 shares the same pads and register
 * layout but runs user mode transactions.
 *
 * The esp32c3 supports no external RAM, so the whole PSRAM half of the esp32s3's
 * copy of this block - SRAM_CMD, SRAM_CLK, CACHE_SCTRL, every SPI_SMEM_* - does
 * not exist here.  What remains keeps the esp32s3's offsets except CORE_CLK_SEL,
 * which moves from 0x0ec to 0x0e0.
 */
enum Esp32_RegisterSPIMEM {
    kEsp32_RegisterSPIMEM_CMD                         = 0x000,
    kEsp32_RegisterSPIMEM_CTRL                        = 0x008,
    kEsp32_RegisterSPIMEM_CLOCK                       = 0x014,
    kEsp32_RegisterSPIMEM_MISC                        = 0x034,
    kEsp32_RegisterSPIMEM_CACHE_FCTRL                 = 0x03c,
    /* W0 is the first of the sixteen data buffer words, W0 to W15 */
    kEsp32_RegisterSPIMEM_W0                          = 0x058,
    kEsp32_RegisterSPIMEM_CORE_CLK_SEL                = 0x0e0,
    kEsp32_RegisterSPIMEM_DATE                        = 0x3fc,

    /* CLOCK - flash side divider.  f_SPI = f_core / (CLKCNT_N + 1) */
    kEsp32_RegisterSPIMEM_CLOCK_CLK_EQU_SYSCLK_M      = 0x01u << 31,
    kEsp32_RegisterSPIMEM_CLOCK_CLKCNT_N_S            = 16,
    kEsp32_RegisterSPIMEM_CLOCK_CLKCNT_H_S            = 8,
    kEsp32_RegisterSPIMEM_CLOCK_CLKCNT_L_S            = 0,

    /* MISC - CS1 has no device behind it on this part, so it stays disabled */
    kEsp32_RegisterSPIMEM_MISC_CS1_DIS_M              = 0x01 << 1,

    /* CTRL - when set, WRSR sends a 16 bit status word instead of 8 bits */
    kEsp32_RegisterSPIMEM_CTRL_WRSR_2B_M              = 0x01 << 22,

    /* CMD - hardware generated flash commands.  Setting a bit starts the
     * command; the bit self clears when it completes */
    kEsp32_RegisterSPIMEM_CMD_FLASH_WREN_M            = 0x01u << 30,

    /* CACHE_FCTRL - flash cache address phase.  The esp32s3 names bit 1
     * CACHE_USR_CMD_4BYTE; here it is CACHE_USR_ADDR_4BYTE, and it selects a
     * 4 byte address rather than a 4 byte command. */
    kEsp32_RegisterSPIMEM_CACHE_FCTRL_USR_ADDR_4BYTE_M = 0x01 << 1,

    /* CORE_CLK_SEL - 0: 80MHz, 1: 120MHz, 2: 160MHz */
    kEsp32_RegisterSPIMEM_CORE_CLK_SEL_S              = 0,
    kEsp32_RegisterSPIMEM_CORE_CLK_SEL_M              = 0x03 << 0,
};

/*
 * EXTMEM - the cache controller
 *
 * One cache, 16KB, four way, fixed: none of the esp32s3's size, way or block
 * size mode bits exist here, and there is no data cache at all.  The two SHUT
 * bits are named by bus rather than by core, the esp32s3's SHUT_CORE0/1 having
 * no meaning on a single core part.
 *
 * Both SHUT bits reset to 1 and ENABLE resets to 0, so nothing reaches flash
 * text until something clears them - the second stage bootloader clears the SHUT
 * bits in bootloader_reset_mmu(), and Esp32_LTChipStart.c does the enable.
 */
enum Esp32_RegisterEXTMEM {
    kEsp32_RegisterEXTMEM_ICACHE_CTRL                 = ESP32_REG_BASE(EXTMEM) + 0x000,
    kEsp32_RegisterEXTMEM_ICACHE_CTRL_ENABLE_M        = 0x01 << 0,

    kEsp32_RegisterEXTMEM_ICACHE_CTRL1                = ESP32_REG_BASE(EXTMEM) + 0x004,
    kEsp32_RegisterEXTMEM_ICACHE_CTRL1_SHUT_IBUS_M    = 0x01 << 0,
    kEsp32_RegisterEXTMEM_ICACHE_CTRL1_SHUT_DBUS_M    = 0x01 << 1,

    kEsp32_RegisterEXTMEM_CACHE_STATE                 = ESP32_REG_BASE(EXTMEM) + 0x0b0,
    kEsp32_RegisterEXTMEM_CACHE_STATE_ICACHE_S        = 0,
    kEsp32_RegisterEXTMEM_CACHE_STATE_ICACHE_M        = 0xfffu << 0,
    /* The state field reads 1 when the cache is idle */
    kEsp32_RegisterEXTMEM_CACHE_STATE_IDLE_V          = 1,
};

/*
 * Cache MMU
 *
 * A single 128 entry table in its own address space, shared by IBUS and DBUS, so
 * a page's table index is the same however it is reached:
 * index = (vaddr & MMU_BUS_ADDR_MASK) / MMU_PAGE_SIZE.  Since the two windows
 * are 0x3c000000 and 0x42000000, both start at index 0, and DROM and IROM have
 * to be given disjoint index ranges or they overwrite each other.
 *
 * That split is made by the linker: sections.ld lays flash text out first and
 * then skips as many drom pages as it occupies (.flash_rodata_dummy), so IROM
 * holds the low indices and DROM follows it, both below MMU_DROM_MAX_END.
 * Esp32_LTChipStart.c passes the resulting boundary to the ROM's
 * Cache_Set_IDROM_MMU_Size() so the cache agrees with the layout.
 *
 * An entry holds an 8 bit physical page number and BIT(8) marks it invalid.  The
 * esp32s3's BIT(15) external RAM select has no counterpart - flash is the only
 * backing store on this part - so the table is 128 entries of 8 bits where the
 * esp32s3 has 512 of 14, and neither the masks nor the entry count carry over.
 * 128 pages of 64KB is the 8MB the cached windows span.
 */
enum Esp32_RegisterMMU {
    kEsp32_RegisterMMU_TABLE                          = 0x600c5000,
    /* soc/cache_memory.h spells this ICACHE_MMU_SIZE, in bytes (0x200) */
    kEsp32_RegisterMMU_ENTRY_COUNT                    = 0x200 / 4,

    kEsp32_RegisterMMU_INVALID_V                      = 0x100,
    kEsp32_RegisterMMU_ADDRESS_M                      = 0xff,

    kEsp32_RegisterMMU_PAGE_SIZE                      = 0x10000,
    kEsp32_RegisterMMU_BUS_ADDR_MASK                  = 0x7fffff,

    /* Highest DROM index, in entries.  soc/cache_memory.h spells this
     * CACHE_DROM_MMU_MAX_END, in bytes (0x200), hence the /4 */
    kEsp32_RegisterMMU_DROM_MAX_END                   = 0x200 / 4,

    /* Cached windows onto flash, one per bus */
    kEsp32_RegisterMMU_DBUS_LOW                       = 0x3c000000,
    kEsp32_RegisterMMU_DBUS_HIGH                      = 0x3c800000,
    kEsp32_RegisterMMU_IBUS_LOW                       = 0x42000000,
    kEsp32_RegisterMMU_IBUS_HIGH                      = 0x42800000,
};

/*
 * GPIO
 *
 * 22 pads, GPIO0..GPIO21, all of them in the low 22 bits of one register, so
 * unlike the esp32s3 there is no second bank - no OUT1, ENABLE1, IN1 or STATUS1.
 * GPIO18 and GPIO19 are the USB D- and D+ pads.
 *
 * The matrix select fields are narrower to match: IN_SEL is 5 bits where the
 * esp32s3 has 6, and OUT_SEL 8 where it has 9.
 */
enum Esp32_RegisterGPIO {
    kEsp32_RegisterGPIO_OUT                           = ESP32_REG_BASE(GPIO) + 0x004,
    kEsp32_RegisterGPIO_OUT_W1TS                      = ESP32_REG_BASE(GPIO) + 0x008,
    kEsp32_RegisterGPIO_OUT_W1TC                      = ESP32_REG_BASE(GPIO) + 0x00c,

    kEsp32_RegisterGPIO_ENABLE                        = ESP32_REG_BASE(GPIO) + 0x020,
    kEsp32_RegisterGPIO_ENABLE_W1TS                   = ESP32_REG_BASE(GPIO) + 0x024,
    kEsp32_RegisterGPIO_ENABLE_W1TC                   = ESP32_REG_BASE(GPIO) + 0x028,

    kEsp32_RegisterGPIO_IN                            = ESP32_REG_BASE(GPIO) + 0x03c,

    kEsp32_RegisterGPIO_STATUS                        = ESP32_REG_BASE(GPIO) + 0x044,
    kEsp32_RegisterGPIO_STATUS_W1TS                   = ESP32_REG_BASE(GPIO) + 0x048,
    kEsp32_RegisterGPIO_STATUS_W1TC                   = ESP32_REG_BASE(GPIO) + 0x04c,

    /* Per pad configuration, one word per pad, indexed by pad number */
    kEsp32_RegisterGPIO_PIN0                          = ESP32_REG_BASE(GPIO) + 0x074,
    kEsp32_RegisterGPIO_PIN_PAD_DRIVER_M              = 0x01 << 2,
    kEsp32_RegisterGPIO_PIN_INT_TYPE_S                = 7,
    kEsp32_RegisterGPIO_PIN_INT_TYPE_M                = 0x07 << 7,
    kEsp32_RegisterGPIO_PIN_INT_ENA_S                 = 13,
    kEsp32_RegisterGPIO_PIN_INT_ENA_M                 = 0x1fu << 13,
    /* Bit 0 of INT_ENA is the CPU interrupt enable and bit 1 the matching NMI;
     * with one core there is a single value rather than one per core */
    kEsp32_RegisterGPIO_PIN_INT_ENA_V                 = 0x01,

    /*
     * GPIO matrix.  IN_SEL_CFG is indexed by peripheral input signal and names
     * the pad that drives it; OUT_SEL_CFG is indexed by pad and names the
     * peripheral output signal that drives it.
     */
    kEsp32_RegisterGPIO_FUNC0_IN_SEL_CFG              = ESP32_REG_BASE(GPIO) + 0x154,
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_IN_SEL_S      = 0,
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_IN_SEL_M      = 0x1fu << 0,
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_IN_INVERT_M   = 0x01 << 5,
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_USE_MATRIX_M  = 0x01 << 6,

    kEsp32_RegisterGPIO_FUNC0_OUT_SEL_CFG             = ESP32_REG_BASE(GPIO) + 0x554,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OUT_SEL_S    = 0,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OUT_SEL_M    = 0xffu << 0,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OUT_INV_M    = 0x01 << 8,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OEN_SEL_M    = 0x01 << 9,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OEN_INV_M    = 0x01 << 10,
    /* Writing this to OUT_SEL_CFG hands the pad back to the GPIO output register
     * instead of a peripheral - SIG_GPIO_OUT_IDX, GPIO_FUNC_OUT_SEL's reset
     * value.  It is 128 here against the esp32s3's 256, the field being a bit
     * narrower. */
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_GPIO_OUT_V   = 128,

    /* Highest pad number on this part */
    kEsp32_RegisterGPIO_MAX_PAD                       = 21,
};

/*
 * IO_MUX
 *
 * The pad registers run in pad order from IO_MUX + 0x04, so a pad's register is
 * simply ESP32_IO_MUX_PAD_REG(nPin) for GPIO0..GPIO21.  IO_MUX + 0x00 is
 * PIN_CTRL, not a pad.  The field positions are the esp32s3's unchanged.
 *
 * The pull up and pull down bits here work for every pad whenever it is in
 * digital mode, including the RTC capable ones, so there is no side table of
 * RTCIO registers to consult - this part has no RTCIO block at all.
 */
enum Esp32_RegisterIO_MUX {
    kEsp32_RegisterIO_MUX_PIN_CTRL                    = ESP32_REG_BASE(IO_MUX) + 0x000,
    kEsp32_RegisterIO_MUX_PAD0                        = ESP32_REG_BASE(IO_MUX) + 0x004,

    kEsp32_RegisterIO_MUX_SLP_SEL_M                   = 0x01 << 1,
    kEsp32_RegisterIO_MUX_FUN_WPD_M                   = 0x01 << 7,
    kEsp32_RegisterIO_MUX_FUN_WPU_M                   = 0x01 << 8,
    kEsp32_RegisterIO_MUX_FUN_IE_M                    = 0x01 << 9,
    kEsp32_RegisterIO_MUX_FUN_DRV_S                   = 10,
    kEsp32_RegisterIO_MUX_FUN_DRV_M                   = 0x03 << 10,
    kEsp32_RegisterIO_MUX_MCU_SEL_S                   = 12,
    kEsp32_RegisterIO_MUX_MCU_SEL_M                   = 0x07 << 12,
};

/*
 * USB Serial/JTAG
 *
 * A USB device on GPIO18 and GPIO19 that presents a CDC ACM serial port to the
 * host without any bridge chip, and through which the ROM prints its boot
 * banner.  On the Seeed XIAO ESP32-C3 this is the only port brought out, so it
 * is the console.
 *
 * It behaves nothing like a UART.  EP1 is a 64 byte IN endpoint: bytes written
 * to it accumulate until either the endpoint fills or WR_DONE is set, and only
 * then does the host see them.  SERIAL_IN_EP_DATA_FREE reads 0 while the host
 * has yet to collect the last packet, which on an unplugged board is forever, so
 * a writer must give up rather than spin.
 *
 * Bit for bit the same peripheral as the esp32s3's; only the base moves.
 */
enum Esp32_RegisterUSB_SERIAL_JTAG {
    kEsp32_RegisterUSB_SERIAL_JTAG_EP1                = ESP32_REG_BASE(USB_DEVICE) + 0x00,
    kEsp32_RegisterUSB_SERIAL_JTAG_EP1_RDWR_BYTE_S    = 0,
    kEsp32_RegisterUSB_SERIAL_JTAG_EP1_RDWR_BYTE_M    = 0xffu << 0,

    kEsp32_RegisterUSB_SERIAL_JTAG_EP1_CONF           = ESP32_REG_BASE(USB_DEVICE) + 0x04,
    /* Write 1 to hand the accumulated IN packet to the host */
    kEsp32_RegisterUSB_SERIAL_JTAG_EP1_CONF_WR_DONE_M         = 0x01 << 0,
    /* Reads 1 while there is room in the IN endpoint */
    kEsp32_RegisterUSB_SERIAL_JTAG_EP1_CONF_IN_DATA_FREE_M    = 0x01 << 1,
    /* Reads 1 while an unread byte is waiting in the OUT endpoint */
    kEsp32_RegisterUSB_SERIAL_JTAG_EP1_CONF_OUT_DATA_AVAIL_M  = 0x01 << 2,

    kEsp32_RegisterUSB_SERIAL_JTAG_INT_RAW            = ESP32_REG_BASE(USB_DEVICE) + 0x08,
    kEsp32_RegisterUSB_SERIAL_JTAG_INT_ST             = ESP32_REG_BASE(USB_DEVICE) + 0x0c,
    kEsp32_RegisterUSB_SERIAL_JTAG_INT_ENA            = ESP32_REG_BASE(USB_DEVICE) + 0x10,
    kEsp32_RegisterUSB_SERIAL_JTAG_INT_CLR            = ESP32_REG_BASE(USB_DEVICE) + 0x14,
    /* The four INT_ registers share this layout */
    kEsp32_RegisterUSB_SERIAL_JTAG_INT_SOF_S              = 1,
    kEsp32_RegisterUSB_SERIAL_JTAG_INT_OUT_RECV_PKT_S     = 2,
    kEsp32_RegisterUSB_SERIAL_JTAG_INT_IN_EMPTY_S         = 3,
    kEsp32_RegisterUSB_SERIAL_JTAG_INT_BUS_RESET_S        = 9,

    kEsp32_RegisterUSB_SERIAL_JTAG_CONF0              = ESP32_REG_BASE(USB_DEVICE) + 0x18,
    kEsp32_RegisterUSB_SERIAL_JTAG_CONF0_PAD_ENABLE_M = 0x01 << 14,

    /* The IN and OUT endpoints are both this long */
    kEsp32_RegisterUSB_SERIAL_JTAG_PACKET_SIZE        = 64,
};

/*
 * IO_MUX pad registers are addressed by pad number.
 */
#define ESP32_IO_MUX_PAD_REG(n)            (*(volatile u32 *)(kEsp32_RegisterIO_MUX_PAD0 + ((n) * 4)))

/* WDEV_RND Register - the hardware random number generator.  Not part of any
 * peripheral block; soc/wdev_reg.h puts it at 0x600260b0 on this part. */
enum Esp32_RegisterWDEV {
    kEsp32_RegisterWDEV_RND                           = 0x600260b0,
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
 * Register accessors
 */
#define ESP32_REG(r)                       (*(volatile u32 *)kEsp32_Register ## r)
#define ESP32_REG_ADDR(r)                  ((volatile u32 *)kEsp32_Register ## r)
#define ESP32_REG_ARRAY_VALUE(r, i)        (*((volatile u32 *)kEsp32_Register ## r + (i)))

#define ESP32_REG_MASK(r, m)               (kEsp32_Register ## r ## _ ## m ## _M)
#define ESP32_REG_SHIFT(r, s)              (kEsp32_Register ## r ## _ ## s ## _S)
#define ESP32_REG_VAL(r, v)                (kEsp32_Register ## r ## _ ## v ## _V)

#endif /* PLATFORMS_ESP32_INCLUDE_ESP32C3_REGISTERS_H */
