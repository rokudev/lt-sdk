/******************************************************************************
 * Esp32_Registers.h                                                  ESP32 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32_REGISTERS_H
#define PLATFORMS_ESP32_INCLUDE_ESP32_REGISTERS_H

/*
 * Peripheral base addresses
 */
typedef u32 Esp32_RegisterBase;
enum Esp32_RegisterBase {
    kEsp32_RegisterBase_DPORT        = 0x3ff00000,
    kEsp32_RegisterBase_AES          = 0x3ff01000,
    kEsp32_RegisterBase_RSA          = 0x3ff02000,
    kEsp32_RegisterBase_SHA          = 0x3ff03000,
    kEsp32_RegisterBase_UART0        = 0x3ff40000,
    kEsp32_RegisterBase_UART1        = 0x3ff50000,
    kEsp32_RegisterBase_UART2        = 0x3ff60000,
    kEsp32_RegisterBase_GPIO         = 0x3ff44000,
    kEsp32_RegisterBase_RTC_CNTL     = 0x3ff48000,
    kEsp32_RegisterBase_RTCIO        = 0x3ff48400,
    kEsp32_RegisterBase_IO_MUX       = 0x3ff49000,
    kEsp32_RegisterBase_EFUSE        = 0x3ff5a000,
    kEsp32_RegisterBase_TIMG0        = 0x3ff5f000,
    kEsp32_RegisterBase_TIMG1        = 0x3ff60000,
    kEsp32_RegisterBase_APB_CTRL     = 0x3ff66000,
    kEsp32_RegisterBase_SPI1         = 0x3ff42000,
    kEsp32_RegisterBase_SPI0         = 0x3ff43000,
    kEsp32_RegisterBase_SPI2         = 0x3ff64000,
    kEsp32_RegisterBase_SPI3         = 0x3ff65000,
    kEsp32_RegisterBase_FlashEncrypt = 0x3ff5b000,
    kEsp32_RegisterBase_LEDC         = 0x3ff59000,
};

#define ESP32_REG_BASE(n)                       (kEsp32_RegisterBase_ ## n)

/*
 * Register Definitions
 */
typedef u32 Esp32_Register;

/* DPORT Registers */
enum Esp32_RegisterDPORT {
    /*
     * NOTE: CPU0 is referred to as "PRO" and and CPU1 is referred to as "APP" in
     *  the documentation, but this is misleading as there is no fundamental
     *  restriction on what can be run on either processor.
     */

    /* CPU1 Control Register B */
    kEsp32_RegisterDPORT_CPU1_CTRL_B                  = ESP32_REG_BASE(DPORT) + 0x030,
    kEsp32_RegisterDPORT_CPU1_CLKGATE_EN_M            = 0x1,

    /* Peripheral clock enables and resets */
    /* See Esp32_Clock.h for more information */
    kEsp32_RegisterDPORT_PERI_CLK_EN                  = ESP32_REG_BASE(DPORT) + 0x01c,
    kEsp32_RegisterDPORT_PERI_RST_EN                  = ESP32_REG_BASE(DPORT) + 0x020,

    kEsp32_RegisterDPORT_PERIP_CLK_EN                 = ESP32_REG_BASE(DPORT) + 0x0c0,
    kEsp32_RegisterDPORT_PERIP_RST_EN                 = ESP32_REG_BASE(DPORT) + 0x0c4,

    /* CPU clock selection */
    kEsp32_RegisterDPORT_CPU_PER_CONF                 = ESP32_REG_BASE(DPORT) + 0x03c,
    kEsp32_RegisterDPORT_CPU_PER_CONF_80M_V           = 0,
    kEsp32_RegisterDPORT_CPU_PER_CONF_160M_V          = 1,
    kEsp32_RegisterDPORT_CPU_PER_CONF_240M_V          = 2,

    kEsp32_RegisterDPORT_WIFI_CLK_EN                  = ESP32_REG_BASE(DPORT) + 0x0cc,
    kEsp32_RegisterDPORT_WIFI_CLK_EN_WIFI_BT_COMMON_M = 0x000003c9,
    kEsp32_RegisterDPORT_WIFI_CLK_EN_WIFI_EN          = 0x00000406,
    kEsp32_RegisterDPORT_WIFI_CLK_EN_WIFI_EN_M        = 0x00000406,
    kEsp32_RegisterDPORT_WIFI_CLK_EN_WIFI_EN_V        = 0x406,
    kEsp32_RegisterDPORT_WIFI_CLK_EN_WIFI_EN_S        = 0x0,

    kEsp32_RegisterDPORT_WIFI_CLK_BT_EN_S             = 11,
    kEsp32_RegisterDPORT_WIFI_CLK_BT_EN_V             = 0x61,
    kEsp32_RegisterDPORT_WIFI_CLK_BT_EN_M             = kEsp32_RegisterDPORT_WIFI_CLK_BT_EN_V <<
                                                        kEsp32_RegisterDPORT_WIFI_CLK_BT_EN_S,

    kEsp32_RegisterDPORT_WIFI_RST_EN                  = ESP32_REG_BASE(DPORT) + 0x0d0,
    kEsp32_RegisterDPORT_WIFI_RST_EN_MAC_RST_EN       = (1 << 2),
    kEsp32_RegisterDPORT_WIFI_RST_EN_MAC_RST_EN_M     = (1 << 2),
    /*
     * The CPU IRQ maps are arrays that map peripherals to CPU Irqs, indexed by
     *   peripheral. Please see Esp32_Irq.h for the peripheral enumeration.
     */
    kEsp32_RegisterDPORT_CPU0_IRQ_MAP                 = ESP32_REG_BASE(DPORT) + 0x104,
    kEsp32_RegisterDPORT_CPU1_IRQ_MAP                 = ESP32_REG_BASE(DPORT) + 0x218,

    /* DPORT_RSA_PD */
    kEsp32_RegisterDPORT_RSA_PD_CTRL                  = ESP32_REG_BASE(DPORT) + 0x490,
    kEsp32_RegisterDPORT_RSA_PD_M                     = (1 << 0),

    /*
     * DPORT cache register
     */
    kEsp32_RegisterDPORT_PRO_CACHE_CTRL               = ESP32_REG_BASE(DPORT) + 0x040,
    kEsp32_RegisterDPORT_APP_CACHE_CTRL               = ESP32_REG_BASE(DPORT) + 0x058,
    kEsp32_RegisterDPORT_CACHE_CTRL_ENABLE_M          = (1 << 3),
    kEsp32_RegisterDPORT_CACHE_CTRL_FLUSH_ENA_M       = (1 << 4),
    kEsp32_RegisterDPORT_CACHE_CTRL_FLUSH_DONE_M      = (1 << 5),
    kEsp32_RegisterDPORT_CACHE_MMU_TABLE              = ESP32_REG_BASE(DPORT) + 0x10000,
    kEsp32_RegisterDPORT_CACHE_MMU_TABLE_INVALID_V    = 0x100,
    kEsp32_RegisterDPORT_PRO_CACHE_CTRL1              = ESP32_REG_BASE(DPORT) + 0x044,
    kEsp32_RegisterDPORT_CACHE_CTRL1_CACHE_M          = 0x3F,
    kEsp32_RegisterDPORT_PRO_DCACHE_DBUG0             = ESP32_REG_BASE(DPORT) + 0x3F0,
    kEsp32_RegisterDPORT_DCACHE_DBUG0_STATE_M         = 0x00000FFF,
    kEsp32_RegisterDPORT_DCACHE_DBUG0_STATE_S         = 7,

    /*
     * DPORT SPI flash encryption
     */
    kEsp32_RegisterDPORT_SLAVE_SPI_CONFIG             = ESP32_REG_BASE(DPORT) + 0x0c8,
    kEsp32_RegisterDPORT_SPI_DECRYPT_ENABLE_M         = 0x01 << 12,
    kEsp32_RegisterDPORT_SPI_ENCRYPT_ENABLE_M         = 0x01 << 8,
};

/* RTC_CNTL Registers */
enum Esp32_RegisterRTC_CNTL {

    kEsp32_RegisterRTC_CNTL_OPTIONS0                  = ESP32_REG_BASE(RTC_CNTL),
    /* System reset */
    kEsp32_RegisterRTC_CNTL_SW_SYS_RST_V              = 0x80000000,
    /* 0x40540 = (BIAS_I2C_FORCE_PD | BBPLL_FORCE_PD | BBPLL_I2C_FORCE_PD | BB_I2C_FORCE_PD
     *                 BIT(18)      |    BIT(10)     |        BIT(8)      |     BIT(6)  */
    kEsp32_RegisterRTC_CNTL_PLL_FORCE_PD_M            = 0x00040540,

    kEsp32_RegisterRTC_CNTL_WDT_CONFIG0               = ESP32_REG_BASE(RTC_CNTL) + 0x8c,
    /* 0x4001fb80 = stage 0 RTC reset only | 7 cycle resets | reset both CPUs | pause in sleep */
    kEsp32_RegisterRTC_CNTL_WDT_SETUP_EN_V            = 0xc001fb80,
    kEsp32_RegisterRTC_CNTL_WDT_SETUP_DIS_V           = 0x4001fb80,
    kEsp32_RegisterRTC_CNTL_WDT_ENABLED_M             = 0x80000000,

    kEsp32_RegisterRTC_CNTL_WDTFEED                   = ESP32_REG_BASE(RTC_CNTL) + 0xa0,
    kEsp32_RegisterRTC_CNTL_WDT_FEED_V                = 0xba5eba11,

    kEsp32_RegisterRTC_CNTL_WDTWPROTECT               = ESP32_REG_BASE(RTC_CNTL) + 0xa4,
    kEsp32_RegisterRTC_CNTL_WDT_UNPROTECT_V           = 0x50d83aa1,
    kEsp32_RegisterRTC_CNTL_WDT_PROTECT_V             = 0x0,

    kEsp32_RegisterRTC_CNTL_CLK_CONF                  = ESP32_REG_BASE(RTC_CNTL) + 0x70,
    kEsp32_RegisterRTC_CNTL_SOC_CLK_SEL_M             = 0x3 << 27,
    kEsp32_RegisterRTC_CNTL_SOC_CLK_SEL_XTAL_V        = 0x0 << 27,
    kEsp32_RegisterRTC_CNTL_SOC_CLK_SEL_PLL_V         = 0x1 << 27,

    kEsp32_RegisterRTC_CNTL_VREG                      = ESP32_REG_BASE(RTC_CNTL) + 0x7c,
    kEsp32_RegisterRTC_CNTL_DIG_DBIAS_WAK_M           = 0x7 << 11,
    kEsp32_RegisterRTC_CNTL_DIG_DBIAS_WAK_S           = 11,
    kEsp32_RegisterRTC_CNTL_XTAL_FREQ                 = ESP32_REG_BASE(RTC_CNTL) + 0xb0,
    kEsp32_RegisterRTC_CNTL_STORE5                    = ESP32_REG_BASE(RTC_CNTL) + 0xb4,
    /* e.g.: 40M_40M == (40e6 >> 12) << 16 + (40e6 >> 12) */
    kEsp32_RegisterRTC_CNTL_APB_FREQ_40M_40M_V        = 0x26252625,
    kEsp32_RegisterRTC_CNTL_APB_FREQ_80M_80M_V        = 0x4c4b4c4b,
};

/* EFUSE Registers */
enum Esp32_RegisterEFUSE {
    kEsp32_RegisterEFUSE_RD_BLK0_RDATA5               = ESP32_REG_BASE(EFUSE) + 0x14,
    kEsp32_RegisterEFUSE_RD_VOL_LEVEL_HP_INV_M        = 0x3 << 22,
    kEsp32_RegisterEFUSE_RD_VOL_LEVEL_HP_INV_S        = 22,

    kEsp32_RegisterEFUSE_BLK0_RDATA0                  = ESP32_REG_BASE(EFUSE),
    kEsp32_RegisterEFUSE_FLASH_CRYPT_CNT_S            = 20,
    kEsp32_RegisterEFUSE_FLASH_CRYPT_CNT_M            = 0x0000007f,
};

/* Timer Group 0 */
enum Esp32_RegisterTIMG0 {
    kEsp32_RegisterTIMG0_RTCCALICFG                   = ESP32_REG_BASE(TIMG0) + 0x68,
    kEsp32_RegisterTIMG0_RTCCAL_RTC_CLK_SEL_M         = 0x00006000,
    kEsp32_RegisterTIMG0_RTCCAL_CLK_SEL_S             = 13,
    kEsp32_RegisterTIMG0_RTCCAL_RTC_CLK_SEL_V         = 0x00000000,
    kEsp32_RegisterTIMG0_RTCCAL_MAX_M                 = 0x7fff0000,
    kEsp32_RegisterTIMG0_RTCCAL_MAX_S                 = 16,
    kEsp32_RegisterTIMG0_RTCCAL_START_V               = 0x80000000,
    kEsp32_RegisterTIMG0_RTCCAL_READY_M               = 0x00008000,

    kEsp32_RegisterTIMG0_RTCCALICFG1                  = ESP32_REG_BASE(TIMG0) + 0x6c,
};

/* APB Control */
enum Esp32_RegisterAPB_CTRL {
    kEsp32_RegisterAPB_CTRL_SYSCLK_CONF               = ESP32_REG_BASE(APB_CTRL),
    kEsp32_RegisterAPB_CTRL_PRE_DIV_CNT_M             = 0x3ff,

    kEsp32_RegisterAPB_CTRL_XTAL_TICK_CONF            = ESP32_REG_BASE(APB_CTRL) + 0x4,
};

/* UART */
enum Esp32_RegisterUART {
    kEsp32_RegisterUART_CONF0                         = 0x20,

    /*
     * Bit positions for UART_CONF0:
     */
    kEsp32_RegisterUART_CONF0_TXFIFO_RST_S            = 18,
    kEsp32_RegisterUART_CONF0_RXFIFO_RST_S            = 17,
    kEsp32_RegisterUART_CONF0_STOP_BIT_NUM_S          = 4,
    kEsp32_RegisterUART_CONF0_BIT_NUM_S               = 2,
    kEsp32_RegisterUART_CONF0_PARITY_EN_S             = 1,
    kEsp32_RegisterUART_CONF0_PARITY_S                = 0,

    kEsp32_RegisterUART_CONF1                         = 0x24,

    /*
     * Bit positions for UART_CONF1:
     */
    kEsp32_RegisterUART_CONF1_RX_TOUT_EN_S            = 31,
    kEsp32_RegisterUART_CONF1_RX_TOUT_THRHD_S         = 24,
    kEsp32_RegisterUART_CONF1_RX_TOUT_THRHD_M         = 0x7f000000,
    kEsp32_RegisterUART_CONF1_TXFIFO_EMPTY_THRHD_S    = 8,
    kEsp32_RegisterUART_CONF1_TXFIFO_EMPTY_THRHD_M    = 0x00007f00,
    kEsp32_RegisterUART_CONF1_RXFIFO_FULL_THRHD_S     = 0,
    kEsp32_RegisterUART_CONF1_RXFIFO_FULL_THRHD_M     = 0x0000007f,

    kEsp32_RegisterUART_CLKDIV                        = 0x14,
    kEsp32_RegisterUART_SLEEP_CONF                    = 0x38,
    kEsp32_RegisterUART_STATUS                        = 0x1c,

    /*
     * Bit positions for UART_STATUS:
     */
    kEsp32_RegisterUART_STATUS_TXFIFO_CNT_S           = 16,
    kEsp32_RegisterUART_STATUS_TXFIFO_CNT_M           = 0x00ff0000,
    kEsp32_RegisterUART_STATUS_RXFIFO_CNT_S           = 0,
    kEsp32_RegisterUART_STATUS_RXFIFO_CNT_M           = 0x000000ff,
    /* UART Transmitter state */
    kEsp32_RegisterUART_STATUS_UTX_OUT_S              = 24,
    kEsp32_RegisterUART_STATUS_UTX_OUT_M              = 0x0f000000,

    kEsp32_RegisterUART_FIFO                          = 0x00,
    kEsp32_RegisterUART_MEM_CONF                      = 0x58,

    /*
     * Bit positions for UART_MEM_CONF:
     */
    kEsp32_RegisterUART_MEM_CONF_TX_SIZE_S            = 7,
    kEsp32_RegisterUART_MEM_CONF_TX_SIZE_M            = 0x00000780,
    kEsp32_RegisterUART_MEM_CONF_RX_SIZE_S            = 3,
    kEsp32_RegisterUART_MEM_CONF_RX_SIZE_M            = 0x00000078,

    kEsp32_RegisterUART_MEM_CNT_STATUS                = 0x64,

    /*
     * Bit positions for UART_MEM_CNT_STATUS:
     */
    kEsp32_RegisterUART_MEM_CNT_STATUS_TX_MEM_CNT_S   = 3,
    kEsp32_RegisterUART_MEM_CNT_STATUS_TX_MEM_CNT_M   = 0x00000038,
    kEsp32_RegisterUART_MEM_CNT_STATUS_RX_MEM_CNT_S   = 0,
    kEsp32_RegisterUART_MEM_CNT_STATUS_RX_MEM_CNT_M   = 0x00000007,


    kEsp32_RegisterUART_INT_RAW                       = 0x04,
    kEsp32_RegisterUART_INT_ST                        = 0x08,
    kEsp32_RegisterUART_INT_ENA                       = 0x0c,
    kEsp32_RegisterUART_INT_CLR                       = 0x10,

    /*
     * Bit positions for UART_INT_RAW, UART_INT_ST,
     *                   UART_INT_ENA, and UART_INT_CLR:
     */
    kEsp32_RegisterUART_INT_TX_DONE_S                 = 14,
    kEsp32_RegisterUART_INT_GLITCH_DET_S              = 11,
    kEsp32_RegisterUART_INT_RXFIFO_TOUT_S             = 8,
    kEsp32_RegisterUART_INT_BRK_DET_S                 = 7,
    kEsp32_RegisterUART_INT_RXFIFO_OVF_S              = 4,
    kEsp32_RegisterUART_INT_FRM_ERR_S                 = 3,
    kEsp32_RegisterUART_INT_TXFIFO_EMPTY_S            = 1,
    kEsp32_RegisterUART_INT_RXFIFO_FULL_S             = 0,
};
/*
 * The UART registers are organized into three identical groups of registers,
 * one each for UART0, UART1, and UART2:
 */
#define ESP32_UART_REG(u, r)               (*(volatile u32 *)((ESP32_REG_BASE(UART ## u)) + kEsp32_RegisterUART_ ## r))

/* SPI */
enum Esp32_RegisterSPI {
    /* SPI_CTRL_REG */
    kEsp32_RegisterSPI_CTRL                           = 0x08,
    /* the mask below is for SPI_WP according to documentation; however, the code
     * uses it as two-byte status enable */
    kEsp32_RegisterSPI_CTRL_TWO_BYTES_STATUS_EN_M     = 0x01 << 22,
    kEsp32_RegisterSPI_CTRL_SPI_WR_BIT_ORDER_M        = 0x01 << 26,
    kEsp32_RegisterSPI_CTRL_SPI_RD_BIT_ORDER_M        = 0x01 << 25,
    kEsp32_RegisterSPI_CTRL_SPI_FREAD_QIO_M           = 0x01 << 24,
    kEsp32_RegisterSPI_CTRL_SPI_FREAD_DIO_M           = 0x01 << 23,
    kEsp32_RegisterSPI_CTRL_SPI_WP_M                  = 0x01 << 21,
    kEsp32_RegisterSPI_CTRL_SPI_FREAD_QUAD_M          = 0x01 << 20,
    kEsp32_RegisterSPI_CTRL_SPI_FREAD_DUAL_M          = 0x01 << 14,
    kEsp32_RegisterSPI_CTRL_SPI_FASTRD_MODE_M         = 0x01 << 13,

    /* SPI_CMD_REG */
    kEsp32_RegisterSPI_CMD                            = 0x00,
    kEsp32_RegisterSPI_CMD_SPI_USR_M                  = 0x01 << 18,
    /* the following commands are not documented. They were extracted from code */
    kEsp32_RegisterSPI_CMD_SPI_WREN_M                 = 0x01 << 30,

    /* SPI_EXT2_REG */
    kEsp32_RegisterSPI_EXT2                           = 0xf8,
    kEsp32_RegisterSPI_EXT2_SPI_ST_M                  = 0x07,

    /* SPI_ADDR_REG */
    kEsp32_RegisterSPI_ADDR                           = 0x04,

    /* SPI_W0 */
    kEsp32_RegisterSPI_W0                             = 0x80,

    /* SPI_RD_STATUS_REG */
    kEsp32_RegisterSPI_RD_STATUS                      = 0x10,

    /* SPI_MISO_DLEN_REG */
    kEsp32_RegisterSPI_MISO_DLEN                      = 0x2c,
    kEsp32_RegisterSPI_MISO_DLEN_DBITLEN_S            = 0x00,

    /* SPI_USER_REG */
    kEsp32_RegisterSPI_USER                           = 0x1c,
    kEsp32_RegisterSPI_USER_MOSI_M                    = 0x01 << 27,
    kEsp32_RegisterSPI_USER_MISO_M                    = 0x01 << 28,
    kEsp32_RegisterSPI_USER_DUMMY_M                   = 0x01 << 29,
    kEsp32_RegisterSPI_USER_ADDR_M                    = 0x01 << 30,

    /* SPI_USER1_REG */
    kEsp32_RegisterSPI_USER1                          = 0x20,
    kEsp32_RegisterSPI_USER1_ADDR_BITLEN_S            = 26,
    kEsp32_RegisterSPI_USER1_ADDR_BITLEN_M            = 0xfc000000,
    kEsp32_RegisterSPI_USER1_DUMMY_CYCLELEN_S         = 0x00,
    kEsp32_RegisterSPI_USER1_DUMMY_CYCLELEN_M         = 0xff,

    /* SPI_USER2_REG */
    kEsp32_RegisterSPI_USER2                          = 0x24,
    kEsp32_RegisterSPI_USER2_COMMAND_BITLEN_S         = 28,
    kEsp32_RegisterSPI_USER2_COMMAND_VALUE_S          = 0x00,
    kEsp32_RegisterSPI_USER2_COMMAND_VALUE_M          = 0xffff,
};

/*
 * The SPI registers are organized into four identical groups of registers,
 * one each for SPI0, SPI1, SPI2, and SPI3:
 */
#define ESP32_SPI_REG(p, r)                (*(volatile u32 *)(ESP32_REG_BASE(p) + kEsp32_RegisterSPI_ ## r))
#define ESP32_SPI0_REG(r)                  (ESP32_SPI_REG(SPI0, r))
#define ESP32_SPI1_REG(r)                  (ESP32_SPI_REG(SPI1, r))

/*
 * The SPI data buffers are W0 through W15 for each SPI port
 */
#define ESP32_SPI_W_REG(p, n)              (*(volatile u32 *)(ESP32_REG_BASE(p) + kEsp32_RegisterSPI_W0 + (n * 4)))

/* Flash encryption */
enum Esp32_RegisterFlashEncrypt {
    /* Flash encryption registers */
    kEsp32_RegisterFLASH_ENCRYPT_BUFFER_0             = ESP32_REG_BASE(FlashEncrypt) + 0x00,
    kEsp32_RegisterFLASH_ENCRYPT_START                = ESP32_REG_BASE(FlashEncrypt) + 0x20,
    kEsp32_RegisterFLASH_ENCRYPT_START_V              = 0x01,
    kEsp32_RegisterFLASH_ENCRYPT_ADDRESS              = ESP32_REG_BASE(FlashEncrypt) + 0x24,
    kEsp32_RegisterFLASH_ENCRYPT_DONE                 = ESP32_REG_BASE(FlashEncrypt) + 0x28,
};

/* Registers for RSA acceleration via Multiple Precision Integer ops */
enum Esp32_RegisterRSA {
    kEsp32_RegisterRSA_MEM_M_BLOCK                    = ESP32_REG_BASE(RSA),
    /* RB & Z use the same memory block, depending on phase of operation */
    kEsp32_RegisterRSA_MEM_Z_BLOCK                    = ESP32_REG_BASE(RSA) + 0x200,
    kEsp32_RegisterRSA_MEM_Y_BLOCK                    = ESP32_REG_BASE(RSA) + 0x400,
    kEsp32_RegisterRSA_MEM_X_BLOCK                    = ESP32_REG_BASE(RSA) + 0x600,
    kEsp32_RegisterRSA_M_DASH                         = ESP32_REG_BASE(RSA) + 0x800,
    kEsp32_RegisterRSA_MODEXP_MODE                    = ESP32_REG_BASE(RSA) + 0x804,
    kEsp32_RegisterRSA_MODEXP_START                   = ESP32_REG_BASE(RSA) + 0x808,
    kEsp32_RegisterRSA_MULT_MODE                      = ESP32_REG_BASE(RSA) + 0x80c,
    kEsp32_RegisterRSA_MULT_START                     = ESP32_REG_BASE(RSA) + 0x810,
    kEsp32_RegisterRSA_INTERRUPT                      = ESP32_REG_BASE(RSA) + 0x814,
    kEsp32_RegisterRSA_CLEAN                          = ESP32_REG_BASE(RSA) + 0x818,
};

/* SHA acceleration registers */
enum Esp32_RegisterSHA {
    kEsp32_RegisterSHA_TEXT                           = ESP32_REG_BASE(SHA) + 0x00,
    kEsp32_RegisterSHA_256_START                      = ESP32_REG_BASE(SHA) + 0x90,
    kEsp32_RegisterSHA_256_CONTINUE                   = ESP32_REG_BASE(SHA) + 0x94,
    kEsp32_RegisterSHA_256_LOAD                       = ESP32_REG_BASE(SHA) + 0x98,
    kEsp32_RegisterSHA_256_BUSY                       = ESP32_REG_BASE(SHA) + 0x9c,
    kEsp32_RegisterSHA_512_START                      = ESP32_REG_BASE(SHA) + 0xb0,
    kEsp32_RegisterSHA_512_CONTINUE                   = ESP32_REG_BASE(SHA) + 0xb4,
    kEsp32_RegisterSHA_512_LOAD                       = ESP32_REG_BASE(SHA) + 0xb8,
    kEsp32_RegisterSHA_512_BUSY                       = ESP32_REG_BASE(SHA) + 0xbc,
};

/* AES acceleration registers */
enum Esp32_RegisterAES {
    kEsp32_RegisterAES_START                          = ESP32_REG_BASE(AES) + 0x00,
    kEsp32_RegisterAES_IDLE                           = ESP32_REG_BASE(AES) + 0x04,
    kEsp32_RegisterAES_MODE                           = ESP32_REG_BASE(AES) + 0x08,
    kEsp32_RegisterAES_KEY                            = ESP32_REG_BASE(AES) + 0x10,
    kEsp32_RegisterAES_TEXT                           = ESP32_REG_BASE(AES) + 0x30,
    kEsp32_RegisterAES_ENDIAN                         = ESP32_REG_BASE(AES) + 0x40,
};

/* WDEV_RND Register */
enum Esp32_RegisterWDEV {
    /*  This register lies outside the standard register map and is therefore treated
     *  differently here. This register is an exception to the pattern that should be
     *  followed for defining registers in this file. */
    kEsp32_RegisterWDEV_RND                           = 0x60035144,
};

enum Esp32_RegisterMACAddr {
    /* TODO: This needs to be moved to the E-Fuse section, name must be modified to match TRM */
    kEsp32_RegisterMAC_ADDR0                          = 0x3ff5a004,
    kEsp32_RegisterMAC_ADDR1                          = 0x3ff5a008,
};

/* LEDC Register */
enum Esp32_RegisterLEDC {
    // channel config
    kEsp32_RegisterLEDC_HSCH_CONF0                    = ESP32_REG_BASE(LEDC) + 0x000,
    kEsp32_RegisterLEDC_HSCH_CONF0_OUT_EN_M           = (1 << 2),
    kEsp32_RegisterLEDC_HSCH_CONF0_OUT_LV_M           = (1 << 3),
    kEsp32_RegisterLEDC_HSCH_CONF1                    = ESP32_REG_BASE(LEDC) + 0x00c,
    kEsp32_RegisterLEDC_HSCH_CONF1_DUTY_SCALE_S       = 0,
    kEsp32_RegisterLEDC_HSCH_CONF1_DUTY_CYCLE_S       = 10,
    kEsp32_RegisterLEDC_HSCH_CONF1_DUTY_NUM_S         = 20,
    kEsp32_RegisterLEDC_HSCH_CONF1_DUTY_NUM_M         = 0x3FF00000,
    kEsp32_RegisterLEDC_HSCH_CONF1_DUTY_INC_M         = (1 << 30),
    kEsp32_RegisterLEDC_HSCH_CONF1_DUTY_START_M       = (1 << 31),
    kEsp32_RegisterLEDC_HSCH_DUTY                     = ESP32_REG_BASE(LEDC) + 0x008,
    kEsp32_RegisterLEDC_HSCH_DUTY_R                   = ESP32_REG_BASE(LEDC) + 0x010,
    kEsp32_RegisterLEDC_HSCH_HPOINT                   = ESP32_REG_BASE(LEDC) + 0x004,
    // timer config
    kEsp32_RegisterLEDC_HSTIMER_CONF                  = ESP32_REG_BASE(LEDC) + 0x140,
    kEsp32_RegisterLEDC_HSTIMER_CONF_DUTY_RES_S       = 0,
    kEsp32_RegisterLEDC_HSTIMER_CONF_DUTY_RES_MAX_V   = 20,
    kEsp32_RegisterLEDC_HSTIMER_CONF_CLK_DIV_NUM_S    = 5,
    kEsp32_RegisterLEDC_HSTIMER_CONF_PAUSE_M          = (1 << 23),
    kEsp32_RegisterLEDC_HSTIMER_CONF_RST_M            = (1 << 24),
    kEsp32_RegisterLEDC_HSTIMER_CONF_TICK_SEL_APB_M   = (1 << 25),
    kEsp32_RegisterLEDC_HSTIMER_VALUE                 = ESP32_REG_BASE(LEDC) + 0x144,
    // LEDC config
    kEsp32_RegisterLEDC_CONF                          = ESP32_REG_BASE(LEDC) + 0x190,
    kEsp32_RegisterLEDC_CONF_APB_CLK_SEL_8M_V         = 0x00,
    kEsp32_RegisterLEDC_CONF_APB_CLK_SEL_80M_V        = 0x01,
    // interrupts
    kEsp32_RegisterLEDC_INT_ST                        = ESP32_REG_BASE(LEDC) + 0x184,
    kEsp32_RegisterLEDC_INT_ENA                       = ESP32_REG_BASE(LEDC) + 0x188,
    kEsp32_RegisterLEDC_INT_CLR                       = ESP32_REG_BASE(LEDC) + 0x18c,
    kEsp32_RegisterLEDC_INT_DUTY_CHNG_S               = 8,
};
/*
 * There are 4 timers with 8 bytes offsets for the config registers
 * t - high speed timer CONF or VALUE registers
 */
#define ESP32_LEDC_TIMER_REG(t, n)         (*(volatile u32 *)(kEsp32_RegisterLEDC_HSTIMER_ ## t + (n * 8)))
/*
 * There are 8 channels with 16 bytes offsets for the config registers
 * t - high speed timer CONF0, CONF1, DUTY, DUTY_R, HPOINT, etc registers
 */
#define ESP32_LEDC_CHANNEL_REG(t, n)       (*(volatile u32 *)(kEsp32_RegisterLEDC_HSCH_ ## t + (n * 0x14)))

/* GPIO registers */
enum Esp32_RegisterGPIO {
    /* interrupt status */
    kEsp32_RegisterGPIO_STATUS                        = ESP32_REG_BASE(GPIO) + 0x44, // pins 0-31
    kEsp32_RegisterGPIO_STATUS_W1TC                   = ESP32_REG_BASE(GPIO) + 0x4c,
    kEsp32_RegisterGPIO_STATUS1                       = ESP32_REG_BASE(GPIO) + 0x50, // pins 32-39
    kEsp32_RegisterGPIO_STATUS1_W1TC                  = ESP32_REG_BASE(GPIO) + 0x58,

    /* pins */
    kEsp32_RegisterGPIO_PIN0                          = ESP32_REG_BASE(GPIO) + 0x88,
    kEsp32_RegisterGPIO_PIN_OPENDRAIN_M               = (1 << 2),
    kEsp32_RegisterGPIO_IN                            = ESP32_REG_BASE(GPIO) + 0x3c,
    kEsp32_RegisterGPIO_IN1                           = ESP32_REG_BASE(GPIO) + 0x40,
    /* interrupt enable */
    kEsp32_RegisterGPIO_PIN_PRO_INT_ENA_S             = 13,
    kEsp32_RegisterGPIO_PIN_PRO_INT_ENA_V             = (1 << 2),
    /* interrupt type shift */
    kEsp32_RegisterGPIO_PIN_INT_TYPE_S                = 7,
    /* output */
    /* pins 0-31 */
    kEsp32_RegisterGPIO_OUT_ENABLE                    = ESP32_REG_BASE(GPIO) + 0x20,
    kEsp32_RegisterGPIO_OUTPUT_ENABLE_W1TS            = ESP32_REG_BASE(GPIO) + 0x24,
    kEsp32_RegisterGPIO_OUTPUT_ENABLE_W1TC            = ESP32_REG_BASE(GPIO) + 0x28,
    kEsp32_RegisterGPIO_OUT                           = ESP32_REG_BASE(GPIO) + 0x04,
    kEsp32_RegisterGPIO_OUT_W1TS                      = ESP32_REG_BASE(GPIO) + 0x08,
    kEsp32_RegisterGPIO_OUT_W1TC                      = ESP32_REG_BASE(GPIO) + 0x0c,
    /* pins 32-39 */
    kEsp32_RegisterGPIO_OUT_ENABLE1                   = ESP32_REG_BASE(GPIO) + 0x2c,
    kEsp32_RegisterGPIO_OUTPUT_ENABLE1_W1TS           = ESP32_REG_BASE(GPIO) + 0x30,
    kEsp32_RegisterGPIO_OUTPUT_ENABLE1_W1TC           = ESP32_REG_BASE(GPIO) + 0x34,
    kEsp32_RegisterGPIO_OUT1                          = ESP32_REG_BASE(GPIO) + 0x10,
    kEsp32_RegisterGPIO_OUT1_W1TS                     = ESP32_REG_BASE(GPIO) + 0x14,
    kEsp32_RegisterGPIO_OUT1_W1TC                     = ESP32_REG_BASE(GPIO) + 0x18,

    /* matrix function */
    kEsp32_RegisterGPIO_FUNC0_IN_SEL_CFG              = ESP32_REG_BASE(GPIO) + 0x130,
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_IN_INVERT_M   = (1 << 6),
    kEsp32_RegisterGPIO_FUNC_IN_SEL_CFG_USE_MATRIX_M  = (1 << 7),

    kEsp32_RegisterGPIO_FUNC0_OUT_SEL_CFG             = ESP32_REG_BASE(GPIO) + 0x530,
    kEsp32_RegisterGPIO_FUNC_OUT_SEL_CFG_OUT_INV_M    = (1 << 9),
};

/* IO_MUX registers */
enum Esp32_RegisterIO_MUX {
    kEsp32_RegisterGPIO_IO_MUX_PIN_CTRL               = ESP32_REG_BASE(IO_MUX) + 0x00,

    kEsp32_RegisterGPIO_IO_MUX_MCU_SEL_S              = 12,
    kEsp32_RegisterGPIO_IO_MUX_FUN_IE_M               = (1 << 9),
    kEsp32_RegisterGPIO_IO_MUX_FUN_WPU_M              = (1 << 8),
    kEsp32_RegisterGPIO_IO_MUX_FUN_WPD_M              = (1 << 7),
    kEsp32_RegisterGPIO_IO_MUX_FUN_DRV_S              = 10,
};
#define ESP32_GPIO_IO_MUX_REG(offset)             (*(volatile u32 *)(kEsp32_RegisterGPIO_IO_MUX_PIN_CTRL + offset))

/* RTCIO */
enum Esp32_RegisterRTCIO {
    kEsp32_RegisterRTCIO_DIG_PAD_HOLD                 = ESP32_REG_BASE(RTCIO) + 0x74,
    kEsp32_RegisterRTCIO_SENSOR_PADS                  = ESP32_REG_BASE(RTCIO) + 0x7c,
    kEsp32_RegisterRTCIO_ADC_PAD                      = ESP32_REG_BASE(RTCIO) + 0x80,
    kEsp32_RegisterRTCIO_PAD_DAC1                     = ESP32_REG_BASE(RTCIO) + 0x84,
    kEsp32_RegisterRTCIO_PAD_DAC2                     = ESP32_REG_BASE(RTCIO) + 0x88,
    kEsp32_RegisterRTCIO_XTAL_32K_PAD                 = ESP32_REG_BASE(RTCIO) + 0x8c,
    kEsp32_RegisterRTCIO_TOUCH_PAD0                   = ESP32_REG_BASE(RTCIO) + 0x94,

    /* the following bits are special bits for PU and PD and are not documented
       in some of the registers */
    kEsp32_RegisterRTCIO_PU_M                         = (1 << 27),
    kEsp32_RegisterRTCIO_PD_M                         = (1 << 28),
    kEsp32_RegisterRTCIO_PIN32_PU_M                   = (1 << 22),
    kEsp32_RegisterRTCIO_PIN32_PD_M                   = (1 << 23),
};

/*
 * Register Access
 *   Examples
 *     Read:
 *       u32 nValue = ESP32_REG(RTC_CNTL_CLK_CONF);
 *     Write:
 *       ESP32_REG(REG_NAME) = nValue;
 *     Read-Modify-Write:
 *       u32 nValue = ESP32_REG(RTC_CNTL_CLK_CONF);
 *       nValue &= ~ESP32_REG_MASK(RTC_CNTL, SOC_CLK_SEL);
 *       nValue |= ESP32_REG_VAL(RTC_CNTL, SOC_CLK_SEL_PLL);
 *       ESP32_REG(RTC_CNTL_CLK_CONF) = nValue;
 */
#define ESP32_REG(r)                       (*(volatile u32 *)kEsp32_Register ## r)
#define ESP32_REG_ADDR(r)                  ((volatile u32 *)kEsp32_Register ## r)

/* Register array access (at index) */
#define ESP32_REG_ARRAY_VALUE(r, i)        (*((volatile u32 *)kEsp32_Register ## r + (i)))

/* Obtain register masks, shifts and values */
#define ESP32_REG_MASK(r, m)               (kEsp32_Register ## r ## _ ## m ## _M)
#define ESP32_REG_SHIFT(r, s)              (kEsp32_Register ## r ## _ ## s ## _S)
#define ESP32_REG_VAL(r, v)                (kEsp32_Register ## r ## _ ## v ## _V)

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32_REGISTERS_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  26-May-22   tiberius    created
 */
