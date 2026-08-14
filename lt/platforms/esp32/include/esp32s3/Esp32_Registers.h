/******************************************************************************
 * Esp32_Registers.h                                               ESP32-S3 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * This is the esp32s3 counterpart of include/esp32/Esp32_Registers.h.  It covers
 * what the second stage bootloader, the Esp32s3 drivers and Esp32_LTCoreBSP
 * reference; the esp32 header's SPI2/SPI3, LEDC, I2C and AES/SHA/RSA blocks have
 * no caller on this part yet and are not reproduced.
 *
 * Nothing carries over from the esp32 header mechanically.  The esp32s3 replaces
 * DPORT with SYSTEM, moves the interrupt multiplexer into INTERRUPT_CORE0 and
 * INTERRUPT_CORE1, and relocates every peripheral, so each value below was taken
 * from the vendored esp32s3 soc headers in
 * source/esp32/ltbootloader/include/esp32s3/soc (soc.h, system_reg.h,
 * rtc_cntl_reg.h, uart_reg.h, gpio_reg.h, io_mux_reg.h, interrupt_core0_reg.h,
 * usb_serial_jtag_reg.h) rather than adapted from the esp32 numbering.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_REGISTERS_H
#define PLATFORMS_ESP32_INCLUDE_ESP32S3_REGISTERS_H

/*
 * Peripheral base addresses
 *
 * The esp32s3 has no DPORT; its system and peripheral clock/reset control lives in
 * the SYSTEM block instead.
 */
typedef u32 Esp32_RegisterBase;
enum Esp32_RegisterBase {
    kEsp32_RegisterBase_SYSTEM       = 0x600c0000,
    kEsp32_RegisterBase_SENSITIVE    = 0x600c1000,
    kEsp32_RegisterBase_INTERRUPT    = 0x600c2000,
    kEsp32_RegisterBase_ASSIST_DEBUG = 0x600ce000,
    kEsp32_RegisterBase_AES          = 0x6003a000,
    kEsp32_RegisterBase_SHA          = 0x6003b000,
    kEsp32_RegisterBase_RSA          = 0x6003c000,
    kEsp32_RegisterBase_UART0        = 0x60000000,
    kEsp32_RegisterBase_UART1        = 0x60010000,
    kEsp32_RegisterBase_UART2        = 0x6002e000,
    kEsp32_RegisterBase_GPIO         = 0x60004000,
    kEsp32_RegisterBase_RTC_CNTL     = 0x60008000,
    kEsp32_RegisterBase_RTCIO        = 0x60008400,
    kEsp32_RegisterBase_IO_MUX       = 0x60009000,
    kEsp32_RegisterBase_EFUSE        = 0x60007000,
    kEsp32_RegisterBase_TIMG0        = 0x6001f000,
    kEsp32_RegisterBase_TIMG1        = 0x60020000,
    kEsp32_RegisterBase_SYSTIMER     = 0x60023000,
    kEsp32_RegisterBase_APB_CTRL     = 0x60026000,
    kEsp32_RegisterBase_SPI0         = 0x60003000,
    kEsp32_RegisterBase_SPI1         = 0x60002000,
    kEsp32_RegisterBase_SPI2         = 0x60024000,
    kEsp32_RegisterBase_SPI3         = 0x60025000,
    kEsp32_RegisterBase_LEDC         = 0x60019000,
    kEsp32_RegisterBase_EXTMEM       = 0x600c4000,
    kEsp32_RegisterBase_USB_DEVICE   = 0x60038000,
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
     * SW_SYS_RST above is named for a system reset but delivers a core reset -
     * "reset the whole digital system except RTC sub-system", per the table at
     * the head of soc/reset_reasons.h - and it reports itself as
     * RESET_REASON_CORE_SW.  So this register, sitting in the RTC sub-system,
     * resets nothing of itself, and neither does anything else here: the BBPLL's
     * power state, the analog I2C configuration, the watchdogs, USB_CONF and the
     * retention words all carry through to the warm boot.
     *
     * It is written as a bare store rather than read-modify-write, matching what
     * the ROM bootloader does in bootloader_utility.c.  The zeroes that go into
     * the other fields are the point rather than collateral: they clear both the
     * FORCE_ISO and the FORCE_NOISO bits, and both XTL force bits, handing the
     * isolation cells and the crystal back to automatic hardware control.
     *
     * See Esp32s3DriverWatchdog_Reboot() for what must not be added to that
     * store, and why the console depends on it staying this small.
     */

    /*
     * RTC watchdog.  The esp32s3 keeps the esp32's WDTCONFIG0 field layout bit
     * for bit but moves the whole group four bytes up, from 0x8c to 0x98, so the
     * setup values below are the same magic the esp32 header carries:
     *
     *   WDT_EN               BIT(31)   = 1
     *   WDT_STG0             [30:28]   = 4 (reset RTC, i.e. the whole chip)
     *   WDT_STG1..3                    = 0 (off)
     *   WDT_CPU_RESET_LENGTH [18:16]   = 1
     *   WDT_SYS_RESET_LENGTH [15:13]   = 7
     *   WDT_FLASHBOOT_MOD_EN BIT(12)   = 0   <- differs from the esp32 header
     *   WDT_PROCPU_RESET_EN  BIT(11)   = 1
     *   WDT_PAUSE_IN_SLP     BIT(9)    = 1
     *   WDT_CHIP_RESET_EN    BIT(8)    = 1
     *   WDT_CHIP_RESET_WIDTH [7:0]     = 0x80
     *
     * FLASHBOOT_MOD_EN is the one deliberate difference from the esp32's copy of
     * these constants, which has it set.  It is flash boot protection, a second
     * way to arm the watchdog that does not go through WDT_EN: the ROM turns it on
     * so that a bootloader which never finishes still resets the part, and IDF's
     * bootloader turns it back off before handing over.  IDF also warns about
     * exactly this on rwdt_ll_disable() - "this function does not disable the
     * flashboot mode ... a timeout can still occur if the flashboot mode is
     * simultaneously enabled".  Carrying the bit in these two constants means that
     * Esp32DisableRTCWatchdog() clears WDT_EN and re-asserts the other arming path
     * in the same store, and Esp32EnableRTCWatchdog() arms both when the caller
     * asked for one - neither of which was intended.  Clearing it costs nothing and
     * makes both functions do what their names say.
     *
     * Whether that fires in practice is a separate question, and on the evidence so
     * far it does not: this image has run for long stretches past that write without
     * being reset, and the esp32 has shipped with the bit set for years.  The
     * hardware appears to stop honouring flash boot protection once boot is over.
     * So treat this as making the register writes honest rather than as a fix for
     * any particular reset.
     *
     * WDT_CONFIG0 + 4 is WDTCONFIG1, whose whole 32 bits are the stage 0 hold
     * count, so Esp32SetTimeoutRTCWatchdog() can index it as an array element.
     */
    kEsp32_RegisterRTC_CNTL_WDT_CONFIG0               = ESP32_REG_BASE(RTC_CNTL) + 0x98,
    kEsp32_RegisterRTC_CNTL_WDT_SETUP_EN_V            = 0xc001eb80,
    kEsp32_RegisterRTC_CNTL_WDT_SETUP_DIS_V           = 0x4001eb80,

    kEsp32_RegisterRTC_CNTL_WDT_ENABLED_M             = 0x80000000,
    kEsp32_RegisterRTC_CNTL_WDT_FLASHBOOT_MOD_EN_M    = 0x01 << 12,

    kEsp32_RegisterRTC_CNTL_WDTFEED                   = ESP32_REG_BASE(RTC_CNTL) + 0xac,
    /* Write only, RTC_CNTL_WDT_FEED, BIT(31) */
    kEsp32_RegisterRTC_CNTL_WDT_FEED_V                = 0x80000000,

    kEsp32_RegisterRTC_CNTL_WDTWPROTECT               = ESP32_REG_BASE(RTC_CNTL) + 0xb0,
    kEsp32_RegisterRTC_CNTL_WDT_UNPROTECT_V           = 0x50d83aa1,
    kEsp32_RegisterRTC_CNTL_WDT_PROTECT_V             = 0x0,

    /*
     * Super watchdog, which the esp32 does not have.  It cannot be disabled, only
     * fed, and AUTO_FEED_EN hands that to hardware - which is how the bootloader
     * survives it (bootloader_super_wdt_auto_feed(), the first thing
     * bootloader_init() does).  It sits behind its own key, not WDTWPROTECT's.
     */
    kEsp32_RegisterRTC_CNTL_SWD_CONF                  = ESP32_REG_BASE(RTC_CNTL) + 0xb4,
    kEsp32_RegisterRTC_CNTL_SWD_CONF_AUTO_FEED_EN_M   = 0x01u << 31,

    kEsp32_RegisterRTC_CNTL_SWDWPROTECT               = ESP32_REG_BASE(RTC_CNTL) + 0xb8,
    kEsp32_RegisterRTC_CNTL_SWD_UNPROTECT_V           = 0x8f1d312a,
    kEsp32_RegisterRTC_CNTL_SWD_PROTECT_V             = 0x0,

    /*
     * Pad hold.  The esp32 kept both hold registers in the RTCIO block; the
     * esp32s3 moved them into RTC_CNTL and split them by pad kind.  PAD_HOLD
     * holds the 22 RTC capable pads, GPIO0..GPIO21, at bit == pad number.
     * DIG_PAD_HOLD holds the digital pads, GPIO22..GPIO48, at bit == pad - 21,
     * which is why bit 0 is unused - see the GPIO_HOLD_MASK table in IDF's
     * soc/esp32s3/gpio_periph.c.
     */
    kEsp32_RegisterRTC_CNTL_PAD_HOLD                  = ESP32_REG_BASE(RTC_CNTL) + 0xd8,
    kEsp32_RegisterRTC_CNTL_DIG_PAD_HOLD              = ESP32_REG_BASE(RTC_CNTL) + 0xdc,

    /*
     * USB serial/JTAG reset behaviour.  Both bits reset to 0, meaning a system
     * reset takes the USB serial/JTAG device and the IO_MUX down with the CPU:
     * the CDC device drops off the bus and re-enumerates, so a host watching the
     * boot log loses its port on every reset.  Setting them holds both across a
     * system reset and keeps the port open.
     */
    kEsp32_RegisterRTC_CNTL_USB_CONF                  = ESP32_REG_BASE(RTC_CNTL) + 0x120,
    kEsp32_RegisterRTC_CNTL_USB_CONF_USB_RESET_DISABLE_M     = 0x01 << 17,
    kEsp32_RegisterRTC_CNTL_USB_CONF_IO_MUX_RESET_DISABLE_M  = 0x01 << 18,
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
 * SYSTEM - the esp32s3's replacement for DPORT.
 *
 * The peripheral clock gates and resets split across two registers here.  EN0
 * happens to keep almost all of the esp32's DPORT_PERIP_CLK_EN bit positions,
 * but UART2 and the crypto engines moved into EN1, so the two chips' clock
 * enable enums are not interchangeable even where the bits coincide.
 */
enum Esp32_RegisterSYSTEM {
    /* CPU1 clock gate and reset.  The esp32 spelled this DPORT_CPU1_CTRL_B */
    kEsp32_RegisterSYSTEM_CORE_1_CONTROL_0            = ESP32_REG_BASE(SYSTEM) + 0x00,
    kEsp32_RegisterSYSTEM_CORE_1_RESETING_M           = 0x01 << 2,
    kEsp32_RegisterSYSTEM_CORE_1_CLKGATE_EN_M         = 0x01 << 1,

    kEsp32_RegisterSYSTEM_CORE_1_CONTROL_1            = ESP32_REG_BASE(SYSTEM) + 0x04,

    /* CPU clock divider select: 0 -> 80MHz, 1 -> 160MHz, 2 -> 240MHz */
    kEsp32_RegisterSYSTEM_CPU_PER_CONF                = ESP32_REG_BASE(SYSTEM) + 0x10,
    kEsp32_RegisterSYSTEM_CPU_PER_CONF_CPUPERIOD_SEL_S = 0,
    kEsp32_RegisterSYSTEM_CPU_PER_CONF_CPUPERIOD_SEL_M = 0x03 << 0,
    kEsp32_RegisterSYSTEM_CPUPERIOD_80M_V             = 0,
    kEsp32_RegisterSYSTEM_CPUPERIOD_160M_V            = 1,
    kEsp32_RegisterSYSTEM_CPUPERIOD_240M_V            = 2,

    kEsp32_RegisterSYSTEM_PERIP_CLK_EN0               = ESP32_REG_BASE(SYSTEM) + 0x18,
    kEsp32_RegisterSYSTEM_PERIP_CLK_EN1               = ESP32_REG_BASE(SYSTEM) + 0x1c,
    kEsp32_RegisterSYSTEM_PERIP_RST_EN0               = ESP32_REG_BASE(SYSTEM) + 0x20,
    kEsp32_RegisterSYSTEM_PERIP_RST_EN1               = ESP32_REG_BASE(SYSTEM) + 0x24,

    /*
     * SOC_CLK_SEL: 0 -> XTAL, 1 -> PLL, 2 -> FOSC (RC fast), 3 -> reserved.
     * PRE_DIV_CNT only applies while XTAL or FOSC is selected.
     */
    kEsp32_RegisterSYSTEM_SYSCLK_CONF                 = ESP32_REG_BASE(SYSTEM) + 0x60,
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
 * The esp32 mapped peripheral interrupt sources to CPU interrupt lines through
 * two arrays of registers in DPORT, one per core.  The esp32s3 gives each core
 * its own register block: INTERRUPT_CORE0 at the base below and INTERRUPT_CORE1
 * 0x800 further on.  Within a block the map registers are one word per source in
 * ETS_*_INTR_SOURCE order, so Esp32_ExternalIrq doubles as the array index -
 * UART0 is source 27 and its map register is at offset 0x6c.
 *
 * Writing 0 to a map register detaches the source; writing 1..31 attaches it to
 * that CPU interrupt line.
 */
enum Esp32_RegisterINTERRUPT {
    kEsp32_RegisterINTERRUPT_CORE0_IRQ_MAP             = ESP32_REG_BASE(INTERRUPT) + 0x000,

    /*
     * The multiplexer also reports, per source, whether that source is asserting
     * into it right now - 128 sources across four registers, source n being bit
     * n%32 of STATUS_(n/32).  This is the one observation point between "the
     * peripheral says it wants attention" and "the core sees a pending line": if
     * a source reads 1 here while the core's INTERRUPT register shows nothing,
     * the map register is the thing at fault, and if it reads 0 the peripheral
     * never asserted and the multiplexer is blameless.  USB Serial/JTAG is
     * source 96, so it lands in STATUS_3 bit 0.
     */
    kEsp32_RegisterINTERRUPT_CORE0_STATUS_0            = ESP32_REG_BASE(INTERRUPT) + 0x18c,
    kEsp32_RegisterINTERRUPT_CORE0_STATUS_1            = ESP32_REG_BASE(INTERRUPT) + 0x190,
    kEsp32_RegisterINTERRUPT_CORE0_STATUS_2            = ESP32_REG_BASE(INTERRUPT) + 0x194,
    kEsp32_RegisterINTERRUPT_CORE0_STATUS_3            = ESP32_REG_BASE(INTERRUPT) + 0x198,

    kEsp32_RegisterINTERRUPT_CORE1_IRQ_MAP             = ESP32_REG_BASE(INTERRUPT) + 0x800,
};

/*
 * EFUSE - only the fields the drivers read.  The esp32 reported flash
 * encryption through EFUSE_BLK0_RDATA0.FLASH_CRYPT_CNT; the esp32s3 replaced
 * the whole block layout and reports it through RD_REPEAT_DATA1.
 */
enum Esp32_RegisterEFUSE {
    kEsp32_RegisterEFUSE_RD_REPEAT_DATA1              = ESP32_REG_BASE(EFUSE) + 0x034,
    kEsp32_RegisterEFUSE_SPI_BOOT_CRYPT_CNT_S         = 18,
    kEsp32_RegisterEFUSE_SPI_BOOT_CRYPT_CNT_M         = 0x07u << 18,
};

/* UART */
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

    /*
     * Bit positions for UART_STATUS.  Both FIFO counters widened to 10 bits on
     * the esp32s3, and the transmitter state field is no longer in this register.
     */
    kEsp32_RegisterUART_STATUS_TXFIFO_CNT_S           = 16,
    kEsp32_RegisterUART_STATUS_TXFIFO_CNT_M           = 0x03ff0000,
    kEsp32_RegisterUART_STATUS_RXFIFO_CNT_S           = 0,
    kEsp32_RegisterUART_STATUS_RXFIFO_CNT_M           = 0x000003ff,

    /*
     * UART transmitter state.  On the esp32 this was UART_STATUS[27:24]; the
     * esp32s3 moved it to its own FSM_STATUS register at offset 0x6c.
     */
    kEsp32_RegisterUART_FSM_STATUS                    = 0x6c,
    kEsp32_RegisterUART_FSM_STATUS_UTX_OUT_S          = 4,
    kEsp32_RegisterUART_FSM_STATUS_UTX_OUT_M          = 0x000000f0,
    kEsp32_RegisterUART_FSM_STATUS_URX_OUT_S          = 0,
    kEsp32_RegisterUART_FSM_STATUS_URX_OUT_M          = 0x0000000f,

    /*
     * There is deliberately no MEM_CNT_STATUS here.  The esp32 exposed a
     * 128 byte APB FIFO through UART_STATUS.RXFIFO_CNT and the rest of its
     * 1KB shared RAM through a second counter, MEM_CNT_STATUS.RX_MEM_CNT, so
     * draining the receiver meant watching both.  The esp32s3 widened
     * RXFIFO_CNT to 10 bits, enough for the whole block, and replaced
     * MEM_CNT_STATUS with MEM_RX_STATUS, which reports read and write offsets
     * rather than a count.  RXFIFO_CNT alone is the receive occupancy here.
     */
};

/*
 * MSPI (SPI0 / SPI1) - the memory SPI controllers, IDF's SPI_MEM_* block.
 *
 * SPI0 is the cache side: it fetches flash and external RAM on the cache's
 * behalf and is never driven by software transactions.  SPI1 shares the same
 * pads and register layout but runs user mode transactions, which is how the
 * PSRAM part's mode registers get written before the cache is pointed at it.
 *
 * These are a different peripheral from SPI2/SPI3 (the general purpose hosts)
 * and share none of their layout, hence the separate enum.
 */
enum Esp32_RegisterSPIMEM {
    kEsp32_RegisterSPIMEM_CMD                         = 0x000,
    kEsp32_RegisterSPIMEM_CTRL                        = 0x008,
    kEsp32_RegisterSPIMEM_CLOCK                       = 0x014,
    kEsp32_RegisterSPIMEM_MISC                        = 0x034,
    kEsp32_RegisterSPIMEM_CACHE_FCTRL                 = 0x03c,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL                 = 0x040,
    kEsp32_RegisterSPIMEM_SRAM_CMD                    = 0x044,
    kEsp32_RegisterSPIMEM_SRAM_DRD_CMD                = 0x048,
    kEsp32_RegisterSPIMEM_SRAM_DWR_CMD                = 0x04c,
    kEsp32_RegisterSPIMEM_SRAM_CLK                    = 0x050,
    /* W0 is the first of the sixteen data buffer words, W0 to W15 */
    kEsp32_RegisterSPIMEM_W0                          = 0x058,
    kEsp32_RegisterSPIMEM_SPI_SMEM_AC                 = 0x0dc,
    kEsp32_RegisterSPIMEM_DDR                         = 0x0e0,
    kEsp32_RegisterSPIMEM_SPI_SMEM_DDR                = 0x0e4,
    kEsp32_RegisterSPIMEM_CORE_CLK_SEL                = 0x0ec,
    kEsp32_RegisterSPIMEM_DATE                        = 0x3fc,

    /* CLOCK - flash side divider.  f_SPI = f_core / (CLKCNT_N + 1) */
    kEsp32_RegisterSPIMEM_CLOCK_CLK_EQU_SYSCLK_M      = 0x01u << 31,
    kEsp32_RegisterSPIMEM_CLOCK_CLKCNT_N_S            = 16,
    kEsp32_RegisterSPIMEM_CLOCK_CLKCNT_H_S            = 8,
    kEsp32_RegisterSPIMEM_CLOCK_CLKCNT_L_S            = 0,

    /* SRAM_CLK - external RAM side divider, same encoding */
    kEsp32_RegisterSPIMEM_SRAM_CLK_SCLK_EQU_SYSCLK_M  = 0x01u << 31,
    kEsp32_RegisterSPIMEM_SRAM_CLK_SCLKCNT_N_S        = 16,
    kEsp32_RegisterSPIMEM_SRAM_CLK_SCLKCNT_H_S        = 8,
    kEsp32_RegisterSPIMEM_SRAM_CLK_SCLKCNT_L_S        = 0,

    /* MISC - CS1 is the external RAM chip select, CS0 is flash */
    kEsp32_RegisterSPIMEM_MISC_CS1_DIS_M              = 0x01 << 1,

    /* CTRL - when set, WRSR sends a 16 bit status word instead of 8 bits */
    kEsp32_RegisterSPIMEM_CTRL_WRSR_2B_M              = 0x01 << 22,

    /* CMD - hardware generated flash commands.  Setting a bit starts the
     * command; the bit self clears when it completes */
    kEsp32_RegisterSPIMEM_CMD_FLASH_WREN_M            = 0x01u << 30,

    /* CACHE_FCTRL - flash cache command phase */
    kEsp32_RegisterSPIMEM_CACHE_FCTRL_USR_CMD_4BYTE_M = 0x01 << 1,

    /* CACHE_SCTRL - external RAM cache command/address/dummy phases */
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_USR_SCMD_4BYTE_M = 0x01 << 0,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_USR_SRAM_DIO_M   = 0x01 << 1,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_USR_SRAM_QIO_M   = 0x01 << 2,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_USR_WR_DUMMY_M   = 0x01 << 3,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_USR_RD_DUMMY_M   = 0x01 << 4,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_USR_RCMD_M       = 0x01 << 5,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_RDUMMY_CYCLELEN_S = 6,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_RDUMMY_CYCLELEN_M = 0x3fu << 6,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_ADDR_BITLEN_S    = 14,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_ADDR_BITLEN_M    = 0x3fu << 14,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_USR_WCMD_M       = 0x01 << 20,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_SRAM_OCT_M       = 0x01 << 21,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_WDUMMY_CYCLELEN_S = 22,
    kEsp32_RegisterSPIMEM_CACHE_SCTRL_WDUMMY_CYCLELEN_M = 0x3fu << 22,

    /* SRAM_CMD - which phases of an external RAM cache access go out octal */
    kEsp32_RegisterSPIMEM_SRAM_CMD_SDIN_OCT_M         = 0x01 << 18,
    kEsp32_RegisterSPIMEM_SRAM_CMD_SDOUT_OCT_M        = 0x01 << 19,
    kEsp32_RegisterSPIMEM_SRAM_CMD_SADDR_OCT_M        = 0x01 << 20,
    kEsp32_RegisterSPIMEM_SRAM_CMD_SCMD_OCT_M         = 0x01 << 21,
    kEsp32_RegisterSPIMEM_SRAM_CMD_SDUMMY_OUT_M       = 0x01 << 22,

    /* SRAM_DRD_CMD / SRAM_DWR_CMD - the opcodes SPI0 issues to external RAM */
    kEsp32_RegisterSPIMEM_SRAM_CMD_VALUE_S            = 0,
    kEsp32_RegisterSPIMEM_SRAM_CMD_VALUE_M            = 0xffffu << 0,
    kEsp32_RegisterSPIMEM_SRAM_CMD_BITLEN_S           = 28,
    kEsp32_RegisterSPIMEM_SRAM_CMD_BITLEN_M           = 0x0fu << 28,

    /* SPI_SMEM_AC - external RAM chip select timing.  SPI0 and SPI1 share
     * these, so only the SPI0 copy needs writing */
    kEsp32_RegisterSPIMEM_SPI_SMEM_AC_CS_SETUP_M      = 0x01 << 0,
    kEsp32_RegisterSPIMEM_SPI_SMEM_AC_CS_HOLD_M       = 0x01 << 1,
    kEsp32_RegisterSPIMEM_SPI_SMEM_AC_CS_SETUP_TIME_S = 2,
    kEsp32_RegisterSPIMEM_SPI_SMEM_AC_CS_SETUP_TIME_M = 0x1fu << 2,
    kEsp32_RegisterSPIMEM_SPI_SMEM_AC_CS_HOLD_TIME_S  = 7,
    kEsp32_RegisterSPIMEM_SPI_SMEM_AC_CS_HOLD_TIME_M  = 0x1fu << 7,
    kEsp32_RegisterSPIMEM_SPI_SMEM_AC_CS_HOLD_DELAY_S = 25,
    kEsp32_RegisterSPIMEM_SPI_SMEM_AC_CS_HOLD_DELAY_M = 0x3fu << 25,

    /* DDR - flash side double data rate control */
    kEsp32_RegisterSPIMEM_DDR_FMEM_VAR_DUMMY_M        = 0x01 << 1,

    /* SPI_SMEM_DDR - external RAM side double data rate control */
    kEsp32_RegisterSPIMEM_SPI_SMEM_DDR_EN_M           = 0x01 << 0,
    kEsp32_RegisterSPIMEM_SPI_SMEM_DDR_VAR_DUMMY_M    = 0x01 << 1,
    kEsp32_RegisterSPIMEM_SPI_SMEM_DDR_RDAT_SWP_M     = 0x01 << 2,
    kEsp32_RegisterSPIMEM_SPI_SMEM_DDR_WDAT_SWP_M     = 0x01 << 3,

    /* CORE_CLK_SEL - 0: 80MHz, 1: 120MHz, 2: 160MHz, 3: 240MHz */
    kEsp32_RegisterSPIMEM_CORE_CLK_SEL_S              = 0,
    kEsp32_RegisterSPIMEM_CORE_CLK_SEL_M              = 0x03 << 0,

    /* DATE - carries the SPI_CLK pad drive strength overrides in its low bits */
    kEsp32_RegisterSPIMEM_DATE_SMEM_SPICLK_FUN_DRV_S  = 0,
    kEsp32_RegisterSPIMEM_DATE_SMEM_SPICLK_FUN_DRV_M  = 0x03 << 0,
    kEsp32_RegisterSPIMEM_DATE_FMEM_SPICLK_FUN_DRV_S  = 2,
    kEsp32_RegisterSPIMEM_DATE_FMEM_SPICLK_FUN_DRV_M  = 0x03 << 2,
    kEsp32_RegisterSPIMEM_DATE_SPICLK_PAD_DRV_CTL_EN_M = 0x01 << 4,
};

/* EXTMEM - the cache controller */
enum Esp32_RegisterEXTMEM {
    kEsp32_RegisterEXTMEM_DCACHE_CTRL                 = ESP32_REG_BASE(EXTMEM) + 0x000,
    /* Resets to 0 - neither cache comes out of reset enabled */
    kEsp32_RegisterEXTMEM_DCACHE_CTRL_ENABLE_M        = 0x01 << 0,
    /* 0: 32KB, 1: 64KB.  The 32KB setting is what leaves the upper half of
     * SRAM2 free for dram_app3_seg - see mastering/ld/esp32s3/memory.ld */
    kEsp32_RegisterEXTMEM_DCACHE_CTRL_SIZE_MODE_M     = 0x01 << 2,

    kEsp32_RegisterEXTMEM_DCACHE_CTRL1                = ESP32_REG_BASE(EXTMEM) + 0x004,
    kEsp32_RegisterEXTMEM_DCACHE_CTRL1_SHUT_CORE0_M   = 0x01 << 0,
    kEsp32_RegisterEXTMEM_DCACHE_CTRL1_SHUT_CORE1_M   = 0x01 << 1,

    /*
     * The instruction cache, 0x60 further up the block.  Both SHUT bits reset to
     * 1 and ENABLE resets to 0, so nothing reaches flash text until something
     * clears them - the second stage bootloader clears the SHUT bits in
     * bootloader_reset_mmu(), and Esp32_LTChipStart.c does the enable.
     */
    kEsp32_RegisterEXTMEM_ICACHE_CTRL                 = ESP32_REG_BASE(EXTMEM) + 0x060,
    kEsp32_RegisterEXTMEM_ICACHE_CTRL_ENABLE_M        = 0x01 << 0,
    /* 0: 4-way, 1: 8-way */
    kEsp32_RegisterEXTMEM_ICACHE_CTRL_WAY_MODE_M      = 0x01 << 1,
    /* 0: 16KB, 1: 32KB.  Note that this is the opposite sense to DCACHE's, whose
     * two sizes are 32KB and 64KB - the caches are not the same size range. */
    kEsp32_RegisterEXTMEM_ICACHE_CTRL_SIZE_MODE_M     = 0x01 << 2,
    /* 0: 16 byte lines, 1: 32 byte lines */
    kEsp32_RegisterEXTMEM_ICACHE_CTRL_BLOCKSIZE_MODE_M = 0x01 << 3,

    kEsp32_RegisterEXTMEM_ICACHE_CTRL1                = ESP32_REG_BASE(EXTMEM) + 0x064,
    kEsp32_RegisterEXTMEM_ICACHE_CTRL1_SHUT_CORE0_M   = 0x01 << 0,
    kEsp32_RegisterEXTMEM_ICACHE_CTRL1_SHUT_CORE1_M   = 0x01 << 1,

    kEsp32_RegisterEXTMEM_CACHE_STATE                 = ESP32_REG_BASE(EXTMEM) + 0x130,
    kEsp32_RegisterEXTMEM_CACHE_STATE_DCACHE_S        = 12,
    kEsp32_RegisterEXTMEM_CACHE_STATE_DCACHE_M        = 0xfffu << 12,
    kEsp32_RegisterEXTMEM_CACHE_STATE_ICACHE_S        = 0,
    kEsp32_RegisterEXTMEM_CACHE_STATE_ICACHE_M        = 0xfffu << 0,
    /* Both state fields read 1 when that cache is idle */
    kEsp32_RegisterEXTMEM_CACHE_STATE_IDLE_V          = 1,
};

/*
 * Cache MMU
 *
 * The esp32 gave the instruction and data buses their own MMU page tables
 * inside DPORT.  The esp32s3 has a single 512 entry table in its own address
 * space, shared by IBUS and DBUS, so a page's table index is the same however
 * it is reached: index = (vaddr & MMU_BUS_ADDR_MASK) / MMU_PAGE_SIZE.  Since the
 * two windows are 0x3c000000 and 0x42000000, both start at index 0, and DROM and
 * IROM have to be given disjoint index ranges or they overwrite each other.
 *
 * That split is made by the linker: sections.ld lays flash text out first and then
 * skips as many drom pages as it occupies (.flash_rodata_dummy), so IROM holds the
 * low indices and DROM follows it, both below MMU_DROM_MAX_END.  Indices from
 * there up are where PSRAM is mapped.  Esp32_LTChipStart.c passes the resulting
 * boundary to the ROM's Cache_Set_IDROM_MMU_Size() so the cache agrees with the
 * layout.
 *
 * An entry holds a 14 bit physical page number; BIT(14) marks it invalid and
 * BIT(15) selects external RAM instead of flash as the backing store.
 */
enum Esp32_RegisterMMU {
    kEsp32_RegisterMMU_TABLE                          = 0x600c5000,
    kEsp32_RegisterMMU_ENTRY_COUNT                    = 512,

    kEsp32_RegisterMMU_INVALID_V                      = 0x4000,
    kEsp32_RegisterMMU_ADDRESS_M                      = 0x3fff,
    kEsp32_RegisterMMU_ACCESS_SPIRAM_M                = 0x8000,

    kEsp32_RegisterMMU_PAGE_SIZE                      = 0x10000,
    kEsp32_RegisterMMU_BUS_ADDR_MASK                  = 0x1ffffff,

    /* Highest DROM index, in entries.  soc/cache_memory.h spells this
     * CACHE_DROM_MMU_MAX_END, in bytes (0x400), hence the /4 */
    kEsp32_RegisterMMU_DROM_MAX_END                   = 0x400 / 4,

    /* Cached windows onto flash, one per bus */
    kEsp32_RegisterMMU_DBUS_LOW                       = 0x3c000000,
    kEsp32_RegisterMMU_DBUS_HIGH                      = 0x3e000000,
    kEsp32_RegisterMMU_IBUS_LOW                       = 0x42000000,
    kEsp32_RegisterMMU_IBUS_HIGH                      = 0x44000000,
};

/*
 * GPIO
 *
 * 49 pads, GPIO0..GPIO48, so the bit banked registers come in pairs: the low
 * register covers GPIO0..31 and the high one GPIO32..48 in its low 17 bits.
 * GPIO22..GPIO25 do not exist on this part - see Esp32_GPIO.c, which rejects
 * them - but they still consume their slot in every per pad array.
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
     * The esp32 numbered this field by core - PRO CPU enable was bit 2 of it.
     * On the esp32s3 bit 0 is the enable for whichever core the source is
     * routed to by INTERRUPT_CORE0/1, and bit 1 the matching NMI, so there is
     * one value rather than one per core (IDF's GPIO_LL_INTR_ENA).
     */
    kEsp32_RegisterGPIO_PIN_INT_ENA_V                 = 0x01,

    /*
     * GPIO matrix.  IN_SEL_CFG is indexed by peripheral input signal and names
     * the pad that drives it; OUT_SEL_CFG is indexed by pad and names the
     * peripheral output signal that drives it.
     */
    kEsp32_RegisterGPIO_FUNC0_IN_SEL_CFG              = ESP32_REG_BASE(GPIO) + 0x154,
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_IN_SEL_S      = 0,
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_IN_SEL_M      = 0x3fu << 0,
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_IN_INVERT_M   = 0x01 << 6,
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_USE_MATRIX_M  = 0x01 << 7,

    kEsp32_RegisterGPIO_FUNC0_OUT_SEL_CFG             = ESP32_REG_BASE(GPIO) + 0x554,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OUT_SEL_S    = 0,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OUT_SEL_M    = 0x1ffu << 0,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OUT_INV_M    = 0x01 << 9,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OEN_SEL_M    = 0x01 << 10,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OEN_INV_M    = 0x01 << 11,
    /* Writing this to OUT_SEL_CFG hands the pad back to the GPIO output
     * register instead of a peripheral - GPIO_FUNC_OUT_SEL's reset value */
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_GPIO_OUT_V   = 0x100,
};

/*
 * IO_MUX
 *
 * Unlike the esp32, whose pad registers are in an order unrelated to the pad
 * number, the esp32s3's run in pad order from IO_MUX + 0x04, so a pad's register
 * is simply ESP32_IO_MUX_PAD_REG(nPin).  IO_MUX + 0x00 is PIN_CTRL, not a pad.
 *
 * The pull up and pull down bits here work for every pad, including the RTC
 * capable ones, whenever the pad is in digital mode.  The esp32 needed a side
 * table of RTCIO registers for those pads; the esp32s3 does not.
 */
enum Esp32_RegisterIO_MUX {
    kEsp32_RegisterIO_MUX_PIN_CTRL                    = ESP32_REG_BASE(IO_MUX) + 0x000,
    kEsp32_RegisterIO_MUX_PAD0                        = ESP32_REG_BASE(IO_MUX) + 0x004,

    /* GPIO26, the MSPI CS1 pad that selects external RAM */
    kEsp32_RegisterIO_MUX_SPICS1                      = ESP32_REG_BASE(IO_MUX) + 0x06c,

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
 * The esp32s3 brings a USB device out on GPIO19 and GPIO20 that presents a CDC
 * ACM serial port to the host without any bridge chip, and the ROM prints its
 * boot banner through it.  Boards that expose only the part's own USB - the
 * Waveshare ESP32-S3-Touch-AMOLED among them - have no path to UART0 at all, so
 * this is the console.
 *
 * It behaves nothing like a UART.  EP1 is a 64 byte IN endpoint: bytes written
 * to it accumulate until either the endpoint fills or WR_DONE is set, and only
 * then does the host see them.  SERIAL_IN_EP_DATA_FREE reads 0 while the host
 * has yet to collect the last packet, which on an unplugged board is forever, so
 * a writer must give up rather than spin.
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
 * peripheral block; the esp32 places it at 0x60035144, the esp32s3 at 0x6003507c
 * (soc/wdev_reg.h). */
enum Esp32_RegisterWDEV {
    kEsp32_RegisterWDEV_RND                           = 0x6003507c,
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

#endif /* PLATFORMS_ESP32_INCLUDE_ESP32S3_REGISTERS_H */
