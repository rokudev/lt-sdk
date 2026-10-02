/******************************************************************************
 * Esp32_MipiDsi.h                                                 ESP32-P4 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The esp32p4's MIPI-DSI transmitter, which is two blocks rather than one.
 *
 *   DSI_HOST  a Synopsys DesignWare MIPI-DSI host controller with its own
 *             D-PHY.  It owns the protocol - packet headers, virtual channels,
 *             escape mode, lane states - and everything in it is Synopsys', so
 *             the register names below are the ones in the DWC databook rather
 *             than anything Espressif chose.
 *   DSI_BRG   Espressif's own block in front of it.  The host expects a DPI
 *             pixel stream on a parallel bus; nothing on this part produces one,
 *             so the bridge reads pixels out of memory by DMA and generates DPI
 *             timing from them.  Frame geometry is therefore programmed twice,
 *             once into each block, in two different sets of units.
 *
 * Values here were taken from the ESP-IDF v5.4 esp32p4 soc and hal headers -
 * dsi_host_struct.h, dsi_brg_struct.h, mipi_dsi_host_ll.h, mipi_dsi_phy_ll.h
 * and mipi_dsi_brg_ll.h - and are limited to what the driver touches, following
 * Esp32_Registers.h.  The D-PHY's own control registers are not memory mapped:
 * they are reached through the PHY_TST_CTRL0/1 test interface, which is why
 * there are no addresses for them.
 *
 * Two register groups look like duplicates and are not.  The host's VID_*
 * timings are in lane byte clock cycles horizontally and lines vertically; the
 * bridge's DPI_[HV]_CFG* are in pixel clocks and lines, and are cumulative
 * (total, not porch).  Writing one set does not set the other.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32P4_MIPIDSI_H
#define PLATFORMS_ESP32_INCLUDE_ESP32P4_MIPIDSI_H

#include "Esp32_Registers.h"

/*
 * Hardware limits.
 *
 * The part brings out two data lanes, not the four the DSI specification
 * allows, and the D-PHY PLL will not lock outside 80..1500Mbps per lane.  The
 * bridge's DPI clock divider is eight bits and takes one less than the divisor.
 */
enum Esp32_MipiDsiLimits {
    kEsp32_MipiDsi_MaxDataLanes          = 2,
    kEsp32_MipiDsi_MinLaneRateMbps       = 80,
    kEsp32_MipiDsi_MaxLaneRateMbps       = 1500,
    kEsp32_MipiDsi_MaxDpiClockDivider    = 256,
};

/*
 * DSI_HOST
 */
enum Esp32_RegisterDSI_HOST {
    kEsp32_RegisterDSI_HOST_PWR_UP                    = ESP32_REG_BASE(DSI_HOST) + 0x04,
    kEsp32_RegisterDSI_HOST_PWR_UP_SHUTDOWNZ_M        = 0x01 << 0,   /* 0 resets the controller */

    /*
     * Both divisors are off the high speed byte clock, which is the lane bit
     * rate over eight.  The escape clock has to land between 2 and 20MHz or
     * low power transmission is out of specification.
     */
    kEsp32_RegisterDSI_HOST_CLKMGR_CFG                = ESP32_REG_BASE(DSI_HOST) + 0x08,
    kEsp32_RegisterDSI_HOST_CLKMGR_CFG_TX_ESC_CLK_DIVISION_S = 0,
    kEsp32_RegisterDSI_HOST_CLKMGR_CFG_TX_ESC_CLK_DIVISION_M = 0xff << 0,
    kEsp32_RegisterDSI_HOST_CLKMGR_CFG_TO_CLK_DIVISION_S     = 8,
    kEsp32_RegisterDSI_HOST_CLKMGR_CFG_TO_CLK_DIVISION_M     = 0xff << 8,

    kEsp32_RegisterDSI_HOST_DPI_VCID                  = ESP32_REG_BASE(DSI_HOST) + 0x0c,

    /*
     * LOOSELY18_EN picks between the two 18 bit codings: set, RGB666 travels a
     * pixel to three bytes; clear, it is packed into 18 bits on the wire.
     */
    kEsp32_RegisterDSI_HOST_DPI_COLOR_CODING          = ESP32_REG_BASE(DSI_HOST) + 0x10,
    kEsp32_RegisterDSI_HOST_DPI_COLOR_CODING_CODING_S = 0,
    kEsp32_RegisterDSI_HOST_DPI_COLOR_CODING_CODING_M = 0x0f << 0,
    kEsp32_RegisterDSI_HOST_DPI_COLOR_CODING_LOOSELY18_EN_M = 0x01 << 8,
    kEsp32_RegisterDSI_HOST_DPI_COLOR_CODING_RGB565_V = 0,
    kEsp32_RegisterDSI_HOST_DPI_COLOR_CODING_RGB666_V = 3,
    kEsp32_RegisterDSI_HOST_DPI_COLOR_CODING_RGB888_V = 5,

    /* One bit per DPI signal, set to make that signal active low */
    kEsp32_RegisterDSI_HOST_DPI_CFG_POL               = ESP32_REG_BASE(DSI_HOST) + 0x14,
    kEsp32_RegisterDSI_HOST_DPI_CFG_POL_DATAEN_M      = 0x01 << 0,
    kEsp32_RegisterDSI_HOST_DPI_CFG_POL_VSYNC_M       = 0x01 << 1,
    kEsp32_RegisterDSI_HOST_DPI_CFG_POL_HSYNC_M       = 0x01 << 2,
    kEsp32_RegisterDSI_HOST_DPI_CFG_POL_SHUTD_M       = 0x01 << 3,
    kEsp32_RegisterDSI_HOST_DPI_CFG_POL_COLORM_M      = 0x01 << 4,

    /* Packet handling: end of transmission packets, bus turnaround, and the
     * two receive side checks.  ECC and CRC reception must be on for the
     * corresponding INT_ST1 error bits to mean anything. */
    kEsp32_RegisterDSI_HOST_PCKHDL_CFG                = ESP32_REG_BASE(DSI_HOST) + 0x2c,
    kEsp32_RegisterDSI_HOST_PCKHDL_CFG_EOTP_TX_EN_M   = 0x01 << 0,
    kEsp32_RegisterDSI_HOST_PCKHDL_CFG_EOTP_RX_EN_M   = 0x01 << 1,
    kEsp32_RegisterDSI_HOST_PCKHDL_CFG_BTA_EN_M       = 0x01 << 2,
    kEsp32_RegisterDSI_HOST_PCKHDL_CFG_ECC_RX_EN_M    = 0x01 << 3,
    kEsp32_RegisterDSI_HOST_PCKHDL_CFG_CRC_RX_EN_M    = 0x01 << 4,
    kEsp32_RegisterDSI_HOST_PCKHDL_CFG_EOTP_TX_LP_EN_M = 0x01 << 5,

    kEsp32_RegisterDSI_HOST_GEN_VCID                  = ESP32_REG_BASE(DSI_HOST) + 0x30,
    kEsp32_RegisterDSI_HOST_GEN_VCID_RX_S             = 0,
    kEsp32_RegisterDSI_HOST_GEN_VCID_RX_M             = 0x03 << 0,

    /* Resets to 1 - command mode - which is where the driver keeps it until
     * StartVideo, so panel initialisation is never racing a pixel stream */
    kEsp32_RegisterDSI_HOST_MODE_CFG                  = ESP32_REG_BASE(DSI_HOST) + 0x34,
    kEsp32_RegisterDSI_HOST_MODE_CFG_CMD_VIDEO_MODE_M = 0x01 << 0,

    /*
     * The LP_*_EN bits say which blanking intervals the host may drop to low
     * power in.  All of them are set: staying in high speed through blanking
     * burns power for nothing, and a panel that cannot cope with it is rare
     * enough to be a panel driver's problem rather than this one's.
     */
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG              = ESP32_REG_BASE(DSI_HOST) + 0x38,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_VID_MODE_TYPE_S = 0,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_VID_MODE_TYPE_M = 0x03 << 0,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_LP_VSA_EN_M  = 0x01 << 8,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_LP_VBP_EN_M  = 0x01 << 9,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_LP_VFP_EN_M  = 0x01 << 10,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_LP_VACT_EN_M = 0x01 << 11,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_LP_HBP_EN_M  = 0x01 << 12,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_LP_HFP_EN_M  = 0x01 << 13,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_FRAME_BTA_ACK_EN_M = 0x01 << 14,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_LP_CMD_EN_M  = 0x01 << 15,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_VPG_EN_M     = 0x01 << 16,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_NON_BURST_SYNC_PULSES_V = 0,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_NON_BURST_SYNC_EVENTS_V = 1,
    kEsp32_RegisterDSI_HOST_VID_MODE_CFG_BURST_V      = 2,

    kEsp32_RegisterDSI_HOST_VID_PKT_SIZE              = ESP32_REG_BASE(DSI_HOST) + 0x3c,
    kEsp32_RegisterDSI_HOST_VID_NUM_CHUNKS            = ESP32_REG_BASE(DSI_HOST) + 0x40,
    kEsp32_RegisterDSI_HOST_VID_NULL_SIZE             = ESP32_REG_BASE(DSI_HOST) + 0x44,

    /* Horizontal timings in lane byte clock cycles, HLINE being the whole line
     * and not a porch; vertical timings in lines */
    kEsp32_RegisterDSI_HOST_VID_HSA_TIME              = ESP32_REG_BASE(DSI_HOST) + 0x48,
    kEsp32_RegisterDSI_HOST_VID_HBP_TIME              = ESP32_REG_BASE(DSI_HOST) + 0x4c,
    kEsp32_RegisterDSI_HOST_VID_HLINE_TIME            = ESP32_REG_BASE(DSI_HOST) + 0x50,
    kEsp32_RegisterDSI_HOST_VID_VSA_LINES             = ESP32_REG_BASE(DSI_HOST) + 0x54,
    kEsp32_RegisterDSI_HOST_VID_VBP_LINES             = ESP32_REG_BASE(DSI_HOST) + 0x58,
    kEsp32_RegisterDSI_HOST_VID_VFP_LINES             = ESP32_REG_BASE(DSI_HOST) + 0x5c,
    kEsp32_RegisterDSI_HOST_VID_VACTIVE_LINES         = ESP32_REG_BASE(DSI_HOST) + 0x60,

    /*
     * One bit per transmission kind, and the sense is the opposite of what the
     * names suggest: set means send that kind in low power escape mode, clear
     * means high speed.  Resets to all set, so commands are LP unless something
     * says otherwise - which is what SetCommandsUseHighSpeed changes.
     */
    kEsp32_RegisterDSI_HOST_CMD_MODE_CFG              = ESP32_REG_BASE(DSI_HOST) + 0x68,
    kEsp32_RegisterDSI_HOST_CMD_MODE_CFG_TEAR_FX_EN_M = 0x01 << 0,
    kEsp32_RegisterDSI_HOST_CMD_MODE_CFG_ACK_RQST_EN_M = 0x01 << 1,
    kEsp32_RegisterDSI_HOST_CMD_MODE_CFG_ALL_LP_M     = 0x010f7f00,  /* every GEN_* and DCS_* bit */

    /*
     * The packet header, written last: writing this register is what starts a
     * transmission, so any payload has to be in GEN_PLD_DATA first.  WC is the
     * word count for a long packet and the two data bytes for a short one.
     */
    kEsp32_RegisterDSI_HOST_GEN_HDR                   = ESP32_REG_BASE(DSI_HOST) + 0x6c,
    kEsp32_RegisterDSI_HOST_GEN_HDR_DT_S              = 0,
    kEsp32_RegisterDSI_HOST_GEN_HDR_DT_M              = 0x3f << 0,
    kEsp32_RegisterDSI_HOST_GEN_HDR_VC_S              = 6,
    kEsp32_RegisterDSI_HOST_GEN_HDR_VC_M              = 0x03 << 6,
    kEsp32_RegisterDSI_HOST_GEN_HDR_WC_LSBYTE_S       = 8,
    kEsp32_RegisterDSI_HOST_GEN_HDR_WC_LSBYTE_M       = 0xff << 8,
    kEsp32_RegisterDSI_HOST_GEN_HDR_WC_MSBYTE_S       = 16,
    kEsp32_RegisterDSI_HOST_GEN_HDR_WC_MSBYTE_M       = 0xff << 16,

    /* Four payload bytes per access, little endian, read and write */
    kEsp32_RegisterDSI_HOST_GEN_PLD_DATA              = ESP32_REG_BASE(DSI_HOST) + 0x70,

    kEsp32_RegisterDSI_HOST_CMD_PKT_STATUS            = ESP32_REG_BASE(DSI_HOST) + 0x74,
    kEsp32_RegisterDSI_HOST_CMD_PKT_STATUS_GEN_CMD_EMPTY_M   = 0x01 << 0,
    kEsp32_RegisterDSI_HOST_CMD_PKT_STATUS_GEN_CMD_FULL_M    = 0x01 << 1,
    kEsp32_RegisterDSI_HOST_CMD_PKT_STATUS_GEN_PLD_W_EMPTY_M = 0x01 << 2,
    kEsp32_RegisterDSI_HOST_CMD_PKT_STATUS_GEN_PLD_W_FULL_M  = 0x01 << 3,
    kEsp32_RegisterDSI_HOST_CMD_PKT_STATUS_GEN_PLD_R_EMPTY_M = 0x01 << 4,
    kEsp32_RegisterDSI_HOST_CMD_PKT_STATUS_GEN_PLD_R_FULL_M  = 0x01 << 5,
    kEsp32_RegisterDSI_HOST_CMD_PKT_STATUS_GEN_RD_CMD_BUSY_M = 0x01 << 6,

    /* Zero in any of these disables that timeout rather than making it immediate */
    kEsp32_RegisterDSI_HOST_TO_CNT_CFG                = ESP32_REG_BASE(DSI_HOST) + 0x78,
    kEsp32_RegisterDSI_HOST_HS_RD_TO_CNT              = ESP32_REG_BASE(DSI_HOST) + 0x7c,
    kEsp32_RegisterDSI_HOST_LP_RD_TO_CNT              = ESP32_REG_BASE(DSI_HOST) + 0x80,
    kEsp32_RegisterDSI_HOST_HS_WR_TO_CNT              = ESP32_REG_BASE(DSI_HOST) + 0x84,
    kEsp32_RegisterDSI_HOST_LP_WR_TO_CNT              = ESP32_REG_BASE(DSI_HOST) + 0x88,
    kEsp32_RegisterDSI_HOST_BTA_TO_CNT                = ESP32_REG_BASE(DSI_HOST) + 0x8c,

    /*
     * TXREQUESTCLKHS runs the clock lane in high speed.  AUTO_CLKLANE_CTRL
     * hands the decision to hardware, which drops the clock lane to low power
     * whenever the data lanes are idle - so it is the bit that makes a
     * non-continuous clock non-continuous.
     */
    kEsp32_RegisterDSI_HOST_LPCLK_CTRL                = ESP32_REG_BASE(DSI_HOST) + 0x94,
    kEsp32_RegisterDSI_HOST_LPCLK_CTRL_TXREQUESTCLKHS_M  = 0x01 << 0,
    kEsp32_RegisterDSI_HOST_LPCLK_CTRL_AUTO_CLKLANE_CTRL_M = 0x01 << 1,

    /* Lane turnaround times, in lane byte clock cycles */
    kEsp32_RegisterDSI_HOST_PHY_TMR_LPCLK_CFG         = ESP32_REG_BASE(DSI_HOST) + 0x98,
    kEsp32_RegisterDSI_HOST_PHY_TMR_LPCLK_CFG_CLKLP2HS_TIME_S = 0,
    kEsp32_RegisterDSI_HOST_PHY_TMR_LPCLK_CFG_CLKLP2HS_TIME_M = 0x3ff << 0,
    kEsp32_RegisterDSI_HOST_PHY_TMR_LPCLK_CFG_CLKHS2LP_TIME_S = 16,
    kEsp32_RegisterDSI_HOST_PHY_TMR_LPCLK_CFG_CLKHS2LP_TIME_M = 0x3ff << 16,

    kEsp32_RegisterDSI_HOST_PHY_TMR_CFG               = ESP32_REG_BASE(DSI_HOST) + 0x9c,
    kEsp32_RegisterDSI_HOST_PHY_TMR_CFG_LP2HS_TIME_S  = 0,
    kEsp32_RegisterDSI_HOST_PHY_TMR_CFG_LP2HS_TIME_M  = 0x3ff << 0,
    kEsp32_RegisterDSI_HOST_PHY_TMR_CFG_HS2LP_TIME_S  = 16,
    kEsp32_RegisterDSI_HOST_PHY_TMR_CFG_HS2LP_TIME_M  = 0x3ff << 16,

    /* All four are active high releases, so zero holds the PHY in reset */
    kEsp32_RegisterDSI_HOST_PHY_RSTZ                  = ESP32_REG_BASE(DSI_HOST) + 0xa0,
    kEsp32_RegisterDSI_HOST_PHY_RSTZ_SHUTDOWNZ_M      = 0x01 << 0,
    kEsp32_RegisterDSI_HOST_PHY_RSTZ_RSTZ_M           = 0x01 << 1,
    kEsp32_RegisterDSI_HOST_PHY_RSTZ_ENABLECLK_M      = 0x01 << 2,
    kEsp32_RegisterDSI_HOST_PHY_RSTZ_FORCEPLL_M       = 0x01 << 3,

    /* N_LANES holds one less than the lane count */
    kEsp32_RegisterDSI_HOST_PHY_IF_CFG                = ESP32_REG_BASE(DSI_HOST) + 0xa4,
    kEsp32_RegisterDSI_HOST_PHY_IF_CFG_N_LANES_S      = 0,
    kEsp32_RegisterDSI_HOST_PHY_IF_CFG_N_LANES_M      = 0x03 << 0,
    kEsp32_RegisterDSI_HOST_PHY_IF_CFG_STOP_WAIT_TIME_S = 8,
    kEsp32_RegisterDSI_HOST_PHY_IF_CFG_STOP_WAIT_TIME_M = 0xff << 8,

    /* Request and exit are separate bits, and neither self clears */
    kEsp32_RegisterDSI_HOST_PHY_ULPS_CTRL             = ESP32_REG_BASE(DSI_HOST) + 0xa8,
    kEsp32_RegisterDSI_HOST_PHY_ULPS_CTRL_TXREQULPSCLK_M  = 0x01 << 0,
    kEsp32_RegisterDSI_HOST_PHY_ULPS_CTRL_TXEXITULPSCLK_M = 0x01 << 1,
    kEsp32_RegisterDSI_HOST_PHY_ULPS_CTRL_TXREQULPSLAN_M  = 0x01 << 2,
    kEsp32_RegisterDSI_HOST_PHY_ULPS_CTRL_TXEXITULPSLAN_M = 0x01 << 3,

    /*
     * Read only.  The ULPSACTIVENOT bits are inverted - a lane is in ultra low
     * power when its bit reads zero - and only lanes 0 and 1 are reported,
     * which is the part's lane count showing through.
     */
    kEsp32_RegisterDSI_HOST_PHY_STATUS                = ESP32_REG_BASE(DSI_HOST) + 0xb0,
    kEsp32_RegisterDSI_HOST_PHY_STATUS_LOCK_M         = 0x01 << 0,
    kEsp32_RegisterDSI_HOST_PHY_STATUS_DIRECTION_M    = 0x01 << 1,
    kEsp32_RegisterDSI_HOST_PHY_STATUS_STOPSTATECLK_M = 0x01 << 2,
    kEsp32_RegisterDSI_HOST_PHY_STATUS_ULPSACTIVENOTCLK_M = 0x01 << 3,
    kEsp32_RegisterDSI_HOST_PHY_STATUS_STOPSTATE0_M   = 0x01 << 4,
    kEsp32_RegisterDSI_HOST_PHY_STATUS_ULPSACTIVENOT0_M = 0x01 << 5,
    kEsp32_RegisterDSI_HOST_PHY_STATUS_RXULPSESC0_M   = 0x01 << 6,
    kEsp32_RegisterDSI_HOST_PHY_STATUS_STOPSTATE1_M   = 0x01 << 7,
    kEsp32_RegisterDSI_HOST_PHY_STATUS_ULPSACTIVENOT1_M = 0x01 << 8,

    /* The D-PHY's own register file is behind these two - see
     * Esp32p4DriverMipiDsi_PhyWrite for the handshake they want */
    kEsp32_RegisterDSI_HOST_PHY_TST_CTRL0             = ESP32_REG_BASE(DSI_HOST) + 0xb4,
    kEsp32_RegisterDSI_HOST_PHY_TST_CTRL0_TESTCLR_M   = 0x01 << 0,
    kEsp32_RegisterDSI_HOST_PHY_TST_CTRL0_TESTCLK_M   = 0x01 << 1,

    kEsp32_RegisterDSI_HOST_PHY_TST_CTRL1             = ESP32_REG_BASE(DSI_HOST) + 0xb8,
    kEsp32_RegisterDSI_HOST_PHY_TST_CTRL1_TESTDIN_S   = 0,
    kEsp32_RegisterDSI_HOST_PHY_TST_CTRL1_TESTDIN_M   = 0xff << 0,
    kEsp32_RegisterDSI_HOST_PHY_TST_CTRL1_TESTEN_M    = 0x01 << 16,

    /* INT_ST0 is what the panel reported back in an acknowledge, plus the
     * D-PHY's own error lines; INT_ST1 is what the host itself noticed.  Both
     * clear on read, so an ISR must not read one twice. */
    kEsp32_RegisterDSI_HOST_INT_ST0                   = ESP32_REG_BASE(DSI_HOST) + 0xbc,
    kEsp32_RegisterDSI_HOST_INT_ST0_ACK_WITH_ERR_M    = 0x0000ffff,
    kEsp32_RegisterDSI_HOST_INT_ST0_ACK_ECC_MULTI_M   = 0x01 << 9,
    kEsp32_RegisterDSI_HOST_INT_ST0_ACK_CHECKSUM_M    = 0x01 << 10,
    kEsp32_RegisterDSI_HOST_INT_ST0_DPHY_ERRORS_M     = 0x001f0000,

    kEsp32_RegisterDSI_HOST_INT_ST1                   = ESP32_REG_BASE(DSI_HOST) + 0xc0,
    kEsp32_RegisterDSI_HOST_INT_ST1_TO_HS_TX_M        = 0x01 << 0,
    kEsp32_RegisterDSI_HOST_INT_ST1_TO_LP_RX_M        = 0x01 << 1,
    kEsp32_RegisterDSI_HOST_INT_ST1_ECC_SINGLE_ERR_M  = 0x01 << 2,
    kEsp32_RegisterDSI_HOST_INT_ST1_ECC_MULTI_ERR_M   = 0x01 << 3,
    kEsp32_RegisterDSI_HOST_INT_ST1_CRC_ERR_M         = 0x01 << 4,
    kEsp32_RegisterDSI_HOST_INT_ST1_PKT_SIZE_ERR_M    = 0x01 << 5,
    kEsp32_RegisterDSI_HOST_INT_ST1_EOTP_ERR_M        = 0x01 << 6,
    kEsp32_RegisterDSI_HOST_INT_ST1_DPI_PLD_WR_ERR_M  = 0x01 << 7,
    kEsp32_RegisterDSI_HOST_INT_ST1_GEN_CMD_WR_ERR_M  = 0x01 << 8,
    kEsp32_RegisterDSI_HOST_INT_ST1_GEN_PLD_WR_ERR_M  = 0x01 << 9,
    kEsp32_RegisterDSI_HOST_INT_ST1_GEN_PLD_SEND_ERR_M = 0x01 << 10,
    kEsp32_RegisterDSI_HOST_INT_ST1_GEN_PLD_RD_ERR_M  = 0x01 << 11,
    kEsp32_RegisterDSI_HOST_INT_ST1_GEN_PLD_RECEV_ERR_M = 0x01 << 12,
    kEsp32_RegisterDSI_HOST_INT_ST1_DPI_BUFF_PLD_UNDER_M = 0x01 << 19,

    /* A set bit masks the source off, so zero is everything enabled */
    kEsp32_RegisterDSI_HOST_INT_MSK0                  = ESP32_REG_BASE(DSI_HOST) + 0xc4,
    kEsp32_RegisterDSI_HOST_INT_MSK1                  = ESP32_REG_BASE(DSI_HOST) + 0xc8,

    kEsp32_RegisterDSI_HOST_PHY_TMR_RD_CFG            = ESP32_REG_BASE(DSI_HOST) + 0xf4,
    kEsp32_RegisterDSI_HOST_PHY_TMR_RD_CFG_MAX_RD_TIME_S = 0,
    kEsp32_RegisterDSI_HOST_PHY_TMR_RD_CFG_MAX_RD_TIME_M = 0x7fff << 0,

    kEsp32_RegisterDSI_HOST_VID_SHADOW_CTRL           = ESP32_REG_BASE(DSI_HOST) + 0x100,
    kEsp32_RegisterDSI_HOST_VID_SHADOW_CTRL_EN_M      = 0x01 << 0,
};

/*
 * DSI_BRG
 */
enum Esp32_RegisterDSI_BRG {
    kEsp32_RegisterDSI_BRG_CLK_EN                     = ESP32_REG_BASE(DSI_BRG) + 0x00,
    kEsp32_RegisterDSI_BRG_CLK_EN_M                   = 0x01 << 0,

    kEsp32_RegisterDSI_BRG_EN                         = ESP32_REG_BASE(DSI_BRG) + 0x04,
    kEsp32_RegisterDSI_BRG_EN_M                       = 0x01 << 0,

    kEsp32_RegisterDSI_BRG_DMA_REQ_CFG                = ESP32_REG_BASE(DSI_BRG) + 0x08,
    kEsp32_RegisterDSI_BRG_DMA_REQ_CFG_BURST_LEN_S    = 0,
    kEsp32_RegisterDSI_BRG_DMA_REQ_CFG_BURST_LEN_M    = 0xfff << 0,

    /*
     * How much pixel data makes a frame, counted in 64 bit words rather than
     * pixels or bytes.  The SET bit is the write trigger: the count is not
     * taken until it goes up.
     */
    kEsp32_RegisterDSI_BRG_RAW_NUM_CFG                = ESP32_REG_BASE(DSI_BRG) + 0x0c,
    kEsp32_RegisterDSI_BRG_RAW_NUM_CFG_TOTAL_S        = 0,
    kEsp32_RegisterDSI_BRG_RAW_NUM_CFG_TOTAL_M        = 0x3fffff << 0,
    kEsp32_RegisterDSI_BRG_RAW_NUM_CFG_TOTAL_SET_M    = 0x01 << 22,
    kEsp32_RegisterDSI_BRG_RAW_NUM_CFG_UNALIGN_64BIT_EN_M = 0x01 << 23,

    kEsp32_RegisterDSI_BRG_RAW_BUF_CREDIT_CTL         = ESP32_REG_BASE(DSI_BRG) + 0x10,
    kEsp32_RegisterDSI_BRG_RAW_BUF_CREDIT_CTL_THRD_S  = 0,
    kEsp32_RegisterDSI_BRG_RAW_BUF_CREDIT_CTL_THRD_M  = 0xffff << 0,
    kEsp32_RegisterDSI_BRG_RAW_BUF_CREDIT_CTL_BURST_THRD_S = 16,
    kEsp32_RegisterDSI_BRG_RAW_BUF_CREDIT_CTL_BURST_THRD_M = 0x7fff << 16,
    kEsp32_RegisterDSI_BRG_RAW_BUF_CREDIT_CTL_RESET_M = 0x01u << 31,

    kEsp32_RegisterDSI_BRG_FIFO_FLOW_STATUS           = ESP32_REG_BASE(DSI_BRG) + 0x14,
    kEsp32_RegisterDSI_BRG_FIFO_FLOW_STATUS_RAW_BUF_DEPTH_M = 0x3fff << 0,

    /*
     * The bridge's own pixel format numbering, which is not the host's colour
     * coding numbering - the two blocks disagree and both have to be told.
     */
    kEsp32_RegisterDSI_BRG_PIXEL_TYPE                 = ESP32_REG_BASE(DSI_BRG) + 0x18,
    kEsp32_RegisterDSI_BRG_PIXEL_TYPE_RAW_TYPE_S      = 0,
    kEsp32_RegisterDSI_BRG_PIXEL_TYPE_RAW_TYPE_M      = 0x0f << 0,
    kEsp32_RegisterDSI_BRG_PIXEL_TYPE_DPI_CONFIG_S    = 4,
    kEsp32_RegisterDSI_BRG_PIXEL_TYPE_DPI_CONFIG_M    = 0x03 << 4,
    kEsp32_RegisterDSI_BRG_PIXEL_TYPE_DATA_IN_TYPE_M  = 0x01 << 6,   /* 0 RGB, 1 YUV */
    kEsp32_RegisterDSI_BRG_PIXEL_TYPE_RGB888_V        = 0,
    kEsp32_RegisterDSI_BRG_PIXEL_TYPE_RGB666_V        = 1,
    kEsp32_RegisterDSI_BRG_PIXEL_TYPE_RGB565_V        = 2,

    kEsp32_RegisterDSI_BRG_DMA_BLOCK_INTERVAL         = ESP32_REG_BASE(DSI_BRG) + 0x1c,
    kEsp32_RegisterDSI_BRG_DMA_REQ_INTERVAL           = ESP32_REG_BASE(DSI_BRG) + 0x20,

    /* DPISHUTDN and DPICOLORM drive the host's DPI sideband signals; UPDATECFG
     * is how a timing change already written is made to take effect */
    kEsp32_RegisterDSI_BRG_DPI_LCD_CTL                = ESP32_REG_BASE(DSI_BRG) + 0x24,
    kEsp32_RegisterDSI_BRG_DPI_LCD_CTL_DPISHUTDN_M    = 0x01 << 0,
    kEsp32_RegisterDSI_BRG_DPI_LCD_CTL_DPICOLORM_M    = 0x01 << 1,
    kEsp32_RegisterDSI_BRG_DPI_LCD_CTL_DPIUPDATECFG_M = 0x01 << 2,

    /*
     * Cumulative timing, in pixel clocks and lines.  TOTAL is the whole frame
     * or line; BANK is the back porch; the front porch is what is left over.
     */
    kEsp32_RegisterDSI_BRG_DPI_V_CFG0                 = ESP32_REG_BASE(DSI_BRG) + 0x30,
    kEsp32_RegisterDSI_BRG_DPI_V_CFG0_VTOTAL_S        = 0,
    kEsp32_RegisterDSI_BRG_DPI_V_CFG0_VTOTAL_M        = 0xfff << 0,
    kEsp32_RegisterDSI_BRG_DPI_V_CFG0_VDISP_S         = 16,
    kEsp32_RegisterDSI_BRG_DPI_V_CFG0_VDISP_M         = 0xfff << 16,

    kEsp32_RegisterDSI_BRG_DPI_V_CFG1                 = ESP32_REG_BASE(DSI_BRG) + 0x34,
    kEsp32_RegisterDSI_BRG_DPI_V_CFG1_VBANK_S         = 0,
    kEsp32_RegisterDSI_BRG_DPI_V_CFG1_VBANK_M         = 0xfff << 0,
    kEsp32_RegisterDSI_BRG_DPI_V_CFG1_VSYNC_S         = 16,
    kEsp32_RegisterDSI_BRG_DPI_V_CFG1_VSYNC_M         = 0xfff << 16,

    kEsp32_RegisterDSI_BRG_DPI_H_CFG0                 = ESP32_REG_BASE(DSI_BRG) + 0x38,
    kEsp32_RegisterDSI_BRG_DPI_H_CFG0_HTOTAL_S        = 0,
    kEsp32_RegisterDSI_BRG_DPI_H_CFG0_HTOTAL_M        = 0xfff << 0,
    kEsp32_RegisterDSI_BRG_DPI_H_CFG0_HDISP_S         = 16,
    kEsp32_RegisterDSI_BRG_DPI_H_CFG0_HDISP_M         = 0xfff << 16,

    kEsp32_RegisterDSI_BRG_DPI_H_CFG1                 = ESP32_REG_BASE(DSI_BRG) + 0x3c,
    kEsp32_RegisterDSI_BRG_DPI_H_CFG1_HBANK_S         = 0,
    kEsp32_RegisterDSI_BRG_DPI_H_CFG1_HBANK_M         = 0xfff << 0,
    kEsp32_RegisterDSI_BRG_DPI_H_CFG1_HSYNC_S         = 16,
    kEsp32_RegisterDSI_BRG_DPI_H_CFG1_HSYNC_M         = 0xfff << 16,

    kEsp32_RegisterDSI_BRG_DPI_MISC_CONFIG            = ESP32_REG_BASE(DSI_BRG) + 0x40,
    kEsp32_RegisterDSI_BRG_DPI_MISC_CONFIG_DPI_EN_M   = 0x01 << 0,
    kEsp32_RegisterDSI_BRG_DPI_MISC_CONFIG_FIFO_UNDERRUN_DISCARD_VCNT_S = 4,
    kEsp32_RegisterDSI_BRG_DPI_MISC_CONFIG_FIFO_UNDERRUN_DISCARD_VCNT_M = 0xfff << 4,

    kEsp32_RegisterDSI_BRG_DPI_CONFIG_UPDATE          = ESP32_REG_BASE(DSI_BRG) + 0x44,
    kEsp32_RegisterDSI_BRG_DPI_CONFIG_UPDATE_M        = 0x01 << 0,

    kEsp32_RegisterDSI_BRG_INT_ENA                    = ESP32_REG_BASE(DSI_BRG) + 0x50,
    kEsp32_RegisterDSI_BRG_INT_CLR                    = ESP32_REG_BASE(DSI_BRG) + 0x54,
    kEsp32_RegisterDSI_BRG_INT_RAW                    = ESP32_REG_BASE(DSI_BRG) + 0x58,
    kEsp32_RegisterDSI_BRG_INT_ST                     = ESP32_REG_BASE(DSI_BRG) + 0x5c,
    kEsp32_RegisterDSI_BRG_INT_UNDERRUN_M             = 0x01 << 0,

    /* The gap the bridge leaves between frames, and the bit that says the
     * frame arrives as several DMA blocks rather than one */
    kEsp32_RegisterDSI_BRG_DMA_FRAME_INTERVAL         = ESP32_REG_BASE(DSI_BRG) + 0x6c,
    kEsp32_RegisterDSI_BRG_DMA_FRAME_INTERVAL_MULTIBLK_EN_M = 0x01 << 28,

    kEsp32_RegisterDSI_BRG_HOST_CTRL                  = ESP32_REG_BASE(DSI_BRG) + 0x80,
    kEsp32_RegisterDSI_BRG_HOST_CTRL_CFG_REF_CLK_EN_M = 0x01 << 0,

    kEsp32_RegisterDSI_BRG_MEM_CLK_CTRL                = ESP32_REG_BASE(DSI_BRG) + 0x84,
    kEsp32_RegisterDSI_BRG_MEM_CLK_CTRL_DSI_MEM_CLK_FORCE_ON_M = 0x01 << 0,

    /* Resets set, meaning the bridge paces the transfer rather than the DMA */
    kEsp32_RegisterDSI_BRG_DMA_FLOW_CTRL              = ESP32_REG_BASE(DSI_BRG) + 0x88,
    kEsp32_RegisterDSI_BRG_DMA_FLOW_CTRL_CONTROLLER_M = 0x01 << 0,
    kEsp32_RegisterDSI_BRG_DMA_FLOW_CTRL_MULTIBLK_NUM_S = 4,
    kEsp32_RegisterDSI_BRG_DMA_FLOW_CTRL_MULTIBLK_NUM_M = 0x0f << 4,

    kEsp32_RegisterDSI_BRG_RAW_BUF_ALMOST_EMPTY_THRD  = ESP32_REG_BASE(DSI_BRG) + 0x8c,
    kEsp32_RegisterDSI_BRG_RAW_BUF_ALMOST_EMPTY_THRD_M = 0x7ff << 0,
};

/*
 * MIPI DSI data types, the DT field of a packet header.  Protocol constants
 * rather than register contents, but the only thing that writes them is
 * GEN_HDR, so they live here.
 */
typedef u32 Esp32_MipiDsiDataType;
enum Esp32_MipiDsiDataTypes {
    kEsp32_MipiDsiDataType_GenericShortWrite0        = 0x03,
    kEsp32_MipiDsiDataType_GenericShortWrite1        = 0x13,
    kEsp32_MipiDsiDataType_GenericShortWrite2        = 0x23,
    kEsp32_MipiDsiDataType_GenericRead0              = 0x04,
    kEsp32_MipiDsiDataType_GenericRead1              = 0x14,
    kEsp32_MipiDsiDataType_GenericRead2              = 0x24,
    kEsp32_MipiDsiDataType_DcsShortWrite0            = 0x05,
    kEsp32_MipiDsiDataType_DcsShortWrite1            = 0x15,
    kEsp32_MipiDsiDataType_DcsRead0                  = 0x06,
    kEsp32_MipiDsiDataType_SetMaximumReturnPacket    = 0x37,
    kEsp32_MipiDsiDataType_GenericLongWrite          = 0x29,
    kEsp32_MipiDsiDataType_DcsLongWrite              = 0x39,
};

#endif /* PLATFORMS_ESP32_INCLUDE_ESP32P4_MIPIDSI_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  30-Sep-26   dwoodward   created
 */
