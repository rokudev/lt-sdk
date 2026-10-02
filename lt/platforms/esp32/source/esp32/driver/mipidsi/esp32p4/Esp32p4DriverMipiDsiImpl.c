/******************************************************************************
 * Esp32p4DriverMipiDsiImpl.c                                      ESP32-P4 BSP
 *
 * LTDriverMipiDsi for the esp32p4's MIPI-DSI display output.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The pixel path here is three pieces of hardware in series, not one.
 *
 *   framebuffer -> GDMA channel -> DSI bridge -> DSI host -> D-PHY -> panel
 *
 * The DSI host is a Synopsys DWC controller and knows nothing about memory; it
 * takes a DPI video stream on one side and drives lanes on the other.  The DSI
 * bridge is Espressif's, and its job is to turn a stream of pixel words arriving
 * at one fixed address into that DPI stream with the right timing.  Getting the
 * words there is the GDMA's job.
 *
 * Which means the frame geometry has to be programmed twice, in two different
 * sets of units: the host counts horizontal time in lane byte clocks and the
 * bridge counts it in pixel clocks, and the host wants each porch separately
 * while the bridge wants cumulative totals.  See Esp32p4DriverMipiDsi_SetTiming.
 *
 * The D-PHY's PLL is not in the register map at all.  It is reached through a
 * two-wire test interface - clock a register address in, clock a value in - and
 * the register numbers are Synopsys's, undocumented here beyond what the PLL
 * configuration needs.
 */

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTThread.h>

#include <lt/driver/mipidsi/LTDriverMipiDsi.h>

#include "Esp32_Clock.h"
#include "Esp32_Gdma.h"
#include "Esp32_Irq.h"
#include "Esp32_MipiDsi.h"
#include "Esp32_Registers.h"
#include "Esp32_SoC.h"

/*_________________________________
  Esp32p4DriverMipiDsi #defines  */
DEFINE_LTLOG_SECTION("esp32p4.drv.mipidsi");
#define DO_DLOG 0
    #if DO_DLOG
        #define DLOG LTLOG
    #else
        #define DLOG LTLOG_LOGNULL
    #endif

/* Busy-waits.  PROVIDEd by mastering/ld/esp32p4/rom/rom.api.ld as an alias for
   ets_delay_us; declared here in the style Esp32_SoC.h uses for its ROM entry
   points rather than pulled in from a header. */
void esp_rom_delay_us(u32 nMicroseconds);

/* Writes dirty cache lines covering [nAddr, nAddr + nSize) back to memory.
   nMap selects the caches; PROVIDEd by mastering/ld/esp32p4/rom/esp32p4.rom.ld.
   Both the L1 data cache and the L2 have to be named: the L2 is what backs
   PSRAM, and the L1 sits in front of it. */
int Cache_WriteBack_Addr(u32 nMap, u32 nAddr, u32 nSize);

#define CACHE_MAP_L1_DCACHE 0x10
#define CACHE_MAP_L2_CACHE  0x20

enum Esp32p4DriverMipiDsiConstants {
    /*
     * The D-PHY reference clock.  PLL_F20M is the only source whose rate is
     * fixed independently of the CPU clock, so it is the one chosen; the PLL
     * multiplies up from it.  Revisions from 3.0 on can source the PLL
     * reference separately from the configuration clock and default it to the
     * 40MHz XTAL instead - see Esp32p4DriverMipiDsi_PhyReferenceClockMHz.
     */
    kPhyReferenceClockMHz           = 20,
    kPhyReferenceClockXtalMHz       = 40,

    /* The first wafer revision whose D-PHY clock controls moved */
    kFirstSplitClockRevision        = 300,

    /*
     * The DPI clock's source.  PLL_F160M divides down to pixel rates with a
     * whole divider across the range of panels this part drives.
     */
    kDpiSourceClockMHz              = 160,

    /*
     * The escape clock must land between 2 and 20MHz, and the timeout clock is
     * conventionally 10MHz.  Both are divided down from the lane byte clock,
     * which is the lane rate over eight.
     *
     * The divider is a truncating divide, so a lane byte rate below the target
     * would ask for zero and stop the clock outright.  Escape mode carries the
     * low power commands and the ULPS entry and exit signalling, so a stopped
     * escape clock reads as a link that answers nothing rather than as a bad
     * divider; two is the smallest the host accepts.
     */
    kEscapeClockMHz                 = 18,
    kTimeoutClockMHz                = 10,
    kMinimumClockDivider            = 2,

    /*
     * LP-to-HS and HS-to-LP switching times, in lane byte clocks.  The D-PHY
     * does not report what it needs and the host cannot measure it, so these
     * are the values Espressif qualified this PHY at.
     */
    kDataHs2LpTime                  = 50,
    kDataLp2HsTime                  = 104,
    kClockHs2LpTime                 = 46,
    kClockLp2HsTime                 = 128,

    /* How long the PHY holds the lanes in stop state before a HS transmission */
    kStopWaitTime                   = 0x3f,

    /* The read response deadline, in lane byte clocks */
    kMaxReadTime                    = 6000,

    /*
     * The GDMA channel the pixel path uses.  One channel, fixed: this driver is
     * the only user of the GDMA in the system and a second DSI host does not
     * exist on this part.
     */
    kPixelDmaChannel                = 0,

    /* The AXI burst length both sides of the pixel transfer use */
    kPixelDmaBurstLength            = 16,

    /* One less than the outstanding AXI requests a channel may have in flight */
    kPixelDmaOutstandingLimit       = 15,

    /*
     * The bridge's pixel FIFO is 1024 words deep.  Asking the DMA for more when
     * a burst's worth of room has opened up keeps it ahead of the scanout
     * without a request per word.
     */
    kBridgeFifoWords                = 1024,
    kBridgeBurstLength              = 256,

    /*
     * Spin limits.  The PHY's PLL locks in microseconds and the command FIFOs
     * drain in the time a packet takes on the wire, so these are bounded loops
     * rather than sleeps - a sleep would be longer than the wait.
     */
    kPhyReadySpinLimit              = 100000,
    kFifoSpinLimit                  = 1000000,

    /* Milliseconds to wait for the lanes to settle after a ULPS transition.
       The wakeup is the specified minimum of one millisecond, rounded up. */
    kUlpsSettleMs                   = 2,
    kUlpsSettleAttempts             = 10,
    kUlpsWakeupMs                   = 2,

    /*
     * The D-PHY analog supply, LDO channel 3 (VO3).  2500mV is the rail
     * voltage for VDD_MIPI_DPHY; DREF 9 with MUL 6 is exactly that with the
     * uncalibrated constants, and the eFuse trim refines it when the die
     * carries one.  Until this rail is up the PHY has no analog power at all
     * and its PLL cannot lock.
     */
    kPhyLdoTargetMilliVolts         = 2500,
    kPhyLdoUncalibratedDRef         = 9,
    kPhyLdoUncalibratedMul          = 6,

    /* Microseconds for the D-PHY rail to reach its target before it is used */
    kPhyLdoSettleMicroseconds       = 500,

    /*
     * The widest cache line in front of PSRAM.  The L1 data cache is 64 bytes
     * and the L2 is 64 or 128 depending on its configured size, so 128 covers
     * either; it is only used to round a writeback outwards, where overshooting
     * costs nothing but a few clean lines.
     */
    kCacheLineBytes                 = 128,
};

/*
 * D-PHY test interface registers.  Synopsys's numbering, reachable only through
 * the test interface; the PLL is configured entirely through these four.
 */
enum Esp32p4DriverMipiDsiPhyRegisters {
    kPhyRegister_HsFreqRange        = 0x44,
    kPhyRegister_PllInputDivider    = 0x17,
    kPhyRegister_PllLoopDivider     = 0x18,
    kPhyRegister_PllChargePump      = 0x19,

    /* The charge pump setting that goes with every lane rate this part
       supports; the PHY has no other operating point. */
    kPhyChargePumpValue             = 0x30,

    /* The loop divider is written in two halves: the low five bits, then the
       rest with this flag saying "this is the high half". */
    kPhyLoopDividerHighFlag         = 0x80,
};

/*
 * The PHY's frequency range selector.  The PHY needs to be told roughly what
 * rate it will run at, in bands; the table is the one in the D-PHY databook and
 * there is no formula for it.
 */
typedef struct Esp32p4DriverMipiDsi_FrequencyRange {
    u16 nLowMbps;
    u16 nHighMbps;
    u8  nSelector;
} Esp32p4DriverMipiDsi_FrequencyRange;

static const Esp32p4DriverMipiDsi_FrequencyRange s_frequencyRanges[] = {
    {   80,   89, 0x00 }, {   90,   99, 0x10 }, {  100,  109, 0x20 }, {  110,  129, 0x01 },
    {  130,  139, 0x11 }, {  140,  149, 0x21 }, {  150,  169, 0x02 }, {  170,  179, 0x12 },
    {  180,  199, 0x22 }, {  200,  219, 0x03 }, {  220,  239, 0x13 }, {  240,  249, 0x23 },
    {  250,  269, 0x04 }, {  270,  299, 0x14 }, {  300,  329, 0x05 }, {  330,  359, 0x15 },
    {  360,  399, 0x25 }, {  400,  449, 0x06 }, {  450,  499, 0x16 }, {  500,  549, 0x07 },
    {  550,  599, 0x17 }, {  600,  649, 0x08 }, {  650,  699, 0x18 }, {  700,  749, 0x09 },
    {  750,  799, 0x19 }, {  800,  849, 0x29 }, {  850,  899, 0x39 }, {  900,  949, 0x0a },
    {  950,  999, 0x1a }, { 1000, 1049, 0x2a }, { 1050, 1099, 0x3a }, { 1100, 1149, 0x0b },
    { 1150, 1199, 0x1b }, { 1200, 1249, 0x2b }, { 1250, 1299, 0x3b }, { 1300, 1349, 0x0c },
    { 1350, 1399, 0x1c }, { 1400, 1449, 0x2c }, { 1450, 1500, 0x3c },
};

#define kFrequencyRangeCount    (sizeof(s_frequencyRanges) / sizeof(s_frequencyRanges[0]))

/*_______________________________________________________________
  Esp32p4DriverMipiDsi LTObjectImpl with private data members  */
typedef_LTObjectImpl(LTDriverMipiDsi, Esp32p4DriverMipiDsi) {
    LTDeviceMipiDsi_Config        config;
    const void                   *pFrameBuffer;
    u32                           nFrameBufferBytes;
    LTDriverMipiDsi_ErrorProc    *pErrorProc;
    void                         *pErrorClientData;
    u32                           nLaneRateMbps;      /* what the PLL actually produced */
    u32                           nDpiClockMHz;       /* what the DPI divider actually produced */
    bool                          bConfigured;
    bool                          bPoweredOn;
    bool                          bVideoActive;
} LTOBJECT_API;

/*_______________________________________
  Esp32p4DriverMipiDsi static variables */

/*
 * The GDMA link list item describing the whole framebuffer.  One item: the
 * channel's block transfer count is wide enough to carry any framebuffer this
 * part has memory for, so there is never a second.
 *
 * Static because the hardware reads it by address and wants it 64-byte aligned,
 * and LTCore's allocator has no aligned entry point.  There is one DSI host on
 * this part, so there is never a second driver instance wanting its own.
 */
static Esp32_GdmaDescriptor LT_ALIGNED(kEsp32_Gdma_DescriptorAlignment) s_pixelDescriptor;

/* The sole instance, and how the ISR reaches it - an LT interrupt vector takes
   no argument, so there is nowhere else for it to come from. */
static Esp32p4DriverMipiDsi *s_driverMipiDsi;

/*________________________________________________
  Esp32p4DriverMipiDsi forward declarations     */
static void Esp32p4DriverMipiDsi_StopVideo(Esp32p4DriverMipiDsi *dsi);

/*____________________________________________
  Esp32p4DriverMipiDsi private functions    */

/*
 * Clock a byte into a D-PHY test register.
 *
 * The test interface is a shift register with a clock line driven by hand: the
 * address is presented with the enable bit set and clocked in, then the value is
 * presented without it and clocked in.  Both edges are explicit writes.
 */
static void Esp32p4DriverMipiDsi_PhyWrite(u8 nAddress, u8 nValue) {
    ESP32_REG(DSI_HOST_PHY_TST_CTRL0) = 0;
    ESP32_REG(DSI_HOST_PHY_TST_CTRL1) = ESP32_REG_MASK(DSI_HOST_PHY_TST_CTRL1, TESTEN) | nAddress;
    ESP32_REG(DSI_HOST_PHY_TST_CTRL0) = ESP32_REG_MASK(DSI_HOST_PHY_TST_CTRL0, TESTCLK);
    ESP32_REG(DSI_HOST_PHY_TST_CTRL0) = 0;

    ESP32_REG(DSI_HOST_PHY_TST_CTRL1) = nValue;
    ESP32_REG(DSI_HOST_PHY_TST_CTRL0) = ESP32_REG_MASK(DSI_HOST_PHY_TST_CTRL0, TESTCLK);
    ESP32_REG(DSI_HOST_PHY_TST_CTRL0) = 0;
}

/* Bits per pixel on the wire, and zero for anything this hardware cannot carry. */
static u32 Esp32p4DriverMipiDsi_BitsPerPixel(LTDeviceMipiDsi_PixelFormat format) {
    switch (format) {
        case kLTDeviceMipiDsi_PixelFormat_RGB565:       return 16;
        case kLTDeviceMipiDsi_PixelFormat_RGB666Packed: return 18;
        case kLTDeviceMipiDsi_PixelFormat_RGB666Loose:  return 24;
        case kLTDeviceMipiDsi_PixelFormat_RGB888:       return 24;
        default: break;
    }
    return 0;
}

/* Total pixels in a frame, blanking included. */
static u64 Esp32p4DriverMipiDsi_FramePixels(const LTDeviceMipiDsi_Timing *pTiming) {
    u64 nHorizontalTotal = (u64)pTiming->nHorizontalActive + pTiming->nHorizontalSyncWidth
                         + pTiming->nHorizontalBackPorch + pTiming->nHorizontalFrontPorch;
    u64 nVerticalTotal   = (u64)pTiming->nVerticalActive + pTiming->nVerticalSyncWidth
                         + pTiming->nVerticalBackPorch + pTiming->nVerticalFrontPorch;
    return nHorizontalTotal * nVerticalTotal;
}

/*
 * The lane rate the link needs to carry a given frame geometry, in Mbps.
 *
 * The porches count: in the non-burst modes the link transmits blanking in real
 * time rather than compressing it away.
 */
static u32 Esp32p4DriverMipiDsi_DeriveLaneRate(const LTDeviceMipiDsi_Config *pConfig) {
    u32 nBitsPerPixel = Esp32p4DriverMipiDsi_BitsPerPixel(pConfig->pixelFormat);
    if (! nBitsPerPixel || ! pConfig->nDataLanes || ! pConfig->timing.nFrameRateHz) return 0;

    u64 nBitsPerSecond = Esp32p4DriverMipiDsi_FramePixels(&pConfig->timing)
                       * nBitsPerPixel * pConfig->timing.nFrameRateHz;
    return (u32)(nBitsPerSecond / pConfig->nDataLanes / 1000000u);
}

/* The pixel clock the DPI side has to run at to meet the configured frame rate. */
static u32 Esp32p4DriverMipiDsi_DerivePixelClockMHz(const LTDeviceMipiDsi_Timing *pTiming) {
    u64 nPixelsPerSecond = Esp32p4DriverMipiDsi_FramePixels(pTiming) * pTiming->nFrameRateHz;
    u32 nMHz = (u32)(nPixelsPerSecond / 1000000u);
    return nMHz ? nMHz : 1;
}

/*
 * The wafer revision as major*100 + minor.  ECO2 parts read 100.
 */
static u32 Esp32p4DriverMipiDsi_WaferRevision(void) {
    u32 nMacSys2 = ESP32_REG(EFUSE_RD_MAC_SYS_2);
    u32 nMajor =
        ((nMacSys2 & ESP32_REG_MASK(EFUSE, WAFER_VERSION_MAJOR_HI))
            >> ESP32_REG_SHIFT(EFUSE, WAFER_VERSION_MAJOR_HI) << 2)
      | ((nMacSys2 & ESP32_REG_MASK(EFUSE, WAFER_VERSION_MAJOR_LO))
            >> ESP32_REG_SHIFT(EFUSE, WAFER_VERSION_MAJOR_LO));
    u32 nMinor = (nMacSys2 & ESP32_REG_MASK(EFUSE, WAFER_VERSION_MINOR))
                    >> ESP32_REG_SHIFT(EFUSE, WAFER_VERSION_MINOR);
    return nMajor * 100 + nMinor;
}

/*
 * The rate the D-PHY PLL counts its reference in.
 *
 * Up to revision 3.0 the PLL reference is the same clock as the PHY
 * configuration clock, selected by PERI_CLK_CTRL02, and PowerOn points it at
 * PLL_F20M.  From 3.0 on the two are separate: CTRL03 gained its own source
 * select and divider for the PLL reference, and both come out of reset
 * selecting the 40MHz XTAL undivided.  PowerOn leaves those reset values
 * alone, so the reference follows the silicon.
 */
static u32 Esp32p4DriverMipiDsi_PhyReferenceClockMHz(void) {
    return Esp32p4DriverMipiDsi_WaferRevision() >= kFirstSplitClockRevision
         ? kPhyReferenceClockXtalMHz : kPhyReferenceClockMHz;
}

/*
 * Program the D-PHY PLL for a lane rate, and return what it will actually
 * produce.
 *
 * The PLL is M/N times the reference.  N is constrained by the PLL's input
 * range - the reference divided by N has to land between 5 and 40MHz - and M
 * has to be even.  Every N in range is tried and the pair whose rate comes
 * closest to the one asked for wins; taking the first even M instead would
 * settle for whatever the truncating divide left, which can be several percent
 * low.
 */
static u32 Esp32p4DriverMipiDsi_ConfigurePll(u32 nLaneRateMbps) {
    u32 nSelector = 0;
    bool bFound = false;
    for (u32 i = 0; i < kFrequencyRangeCount; i++) {
        if (nLaneRateMbps >= s_frequencyRanges[i].nLowMbps
         && nLaneRateMbps <= s_frequencyRanges[i].nHighMbps) {
            nSelector = s_frequencyRanges[i].nSelector;
            bFound = true;
            break;
        }
    }
    if (! bFound) return 0;
    /* The VCO runs at the lane rate: one bit per VCO cycle on each lane. */
    u32 nReferenceMHz = Esp32p4DriverMipiDsi_PhyReferenceClockMHz();
    u32 nMinDivider   = (nReferenceMHz / 40) ? (nReferenceMHz / 40) : 1;
    u32 nMaxDivider   = nReferenceMHz / 5;

    u32 nInputDivider = 0, nLoopDivider = 0, nBestError = 0xffffffffu;
    for (u32 n = nMinDivider; n <= nMaxDivider; n++) {
        u32 m = nLaneRateMbps * n / nReferenceMHz;
        if (! m || (m & 1)) continue;
        u32 nError = nLaneRateMbps - nReferenceMHz * m / n;
        if (nError < nBestError) {
            nBestError    = nError;
            nInputDivider = n;
            nLoopDivider  = m;
            if (! nError) break;
        }
    }
    if (! nInputDivider) return 0;

    Esp32p4DriverMipiDsi_PhyWrite(kPhyRegister_HsFreqRange,     (u8)(nSelector << 1));
    Esp32p4DriverMipiDsi_PhyWrite(kPhyRegister_PllChargePump,   kPhyChargePumpValue);
    Esp32p4DriverMipiDsi_PhyWrite(kPhyRegister_PllInputDivider, (u8)(nInputDivider - 1));
    Esp32p4DriverMipiDsi_PhyWrite(kPhyRegister_PllLoopDivider,  (u8)((nLoopDivider - 1) & 0x1f));
    Esp32p4DriverMipiDsi_PhyWrite(kPhyRegister_PllLoopDivider,
                                  (u8)(kPhyLoopDividerHighFlag | (((nLoopDivider - 1) >> 5) & 0x0f)));

    return nReferenceMHz * nLoopDivider / nInputDivider;
}

/*
 * Program the frame geometry into both blocks.
 *
 * The host counts horizontal time in lane byte clocks because that is what it
 * is clocked by, so every horizontal figure is scaled by the ratio between the
 * lane byte rate and the pixel rate before being written there.  Vertical
 * figures are whole lines in both blocks and need no scaling.  The bridge takes
 * pixels directly, and wants totals rather than the individual porches.
 */
static void Esp32p4DriverMipiDsi_SetTiming(Esp32p4DriverMipiDsi *dsi) {
    const LTDeviceMipiDsi_Timing *pTiming = &dsi->config.timing;

    u32 nHorizontalTotal = pTiming->nHorizontalActive + pTiming->nHorizontalSyncWidth
                         + pTiming->nHorizontalBackPorch + pTiming->nHorizontalFrontPorch;
    u32 nVerticalTotal   = pTiming->nVerticalActive + pTiming->nVerticalSyncWidth
                         + pTiming->nVerticalBackPorch + pTiming->nVerticalFrontPorch;

    /* Multiplied before dividing: the ratio is well under one for any real
       panel and would round to zero the other way round. */
    u32 nLaneByteRate = dsi->nLaneRateMbps / 8;
    #define SCALE_TO_LANE_BYTES(pixels)  ((u32)(((u64)(pixels) * nLaneByteRate) / dsi->nDpiClockMHz))

    ESP32_REG(DSI_HOST_VID_HSA_TIME)   = SCALE_TO_LANE_BYTES(pTiming->nHorizontalSyncWidth);
    ESP32_REG(DSI_HOST_VID_HBP_TIME)   = SCALE_TO_LANE_BYTES(pTiming->nHorizontalBackPorch);
    ESP32_REG(DSI_HOST_VID_HLINE_TIME) = SCALE_TO_LANE_BYTES(nHorizontalTotal);

    #undef SCALE_TO_LANE_BYTES

    ESP32_REG(DSI_HOST_VID_VSA_LINES)     = pTiming->nVerticalSyncWidth;
    ESP32_REG(DSI_HOST_VID_VBP_LINES)     = pTiming->nVerticalBackPorch;
    ESP32_REG(DSI_HOST_VID_VFP_LINES)     = pTiming->nVerticalFrontPorch;
    ESP32_REG(DSI_HOST_VID_VACTIVE_LINES) = pTiming->nVerticalActive;

    ESP32_REG(DSI_BRG_DPI_H_CFG0) = (nHorizontalTotal << ESP32_REG_SHIFT(DSI_BRG_DPI_H_CFG0, HTOTAL))
                                  | (pTiming->nHorizontalActive << ESP32_REG_SHIFT(DSI_BRG_DPI_H_CFG0, HDISP));
    ESP32_REG(DSI_BRG_DPI_H_CFG1) = (pTiming->nHorizontalBackPorch << ESP32_REG_SHIFT(DSI_BRG_DPI_H_CFG1, HBANK))
                                  | (pTiming->nHorizontalSyncWidth << ESP32_REG_SHIFT(DSI_BRG_DPI_H_CFG1, HSYNC));
    ESP32_REG(DSI_BRG_DPI_V_CFG0) = (nVerticalTotal << ESP32_REG_SHIFT(DSI_BRG_DPI_V_CFG0, VTOTAL))
                                  | (pTiming->nVerticalActive << ESP32_REG_SHIFT(DSI_BRG_DPI_V_CFG0, VDISP));
    ESP32_REG(DSI_BRG_DPI_V_CFG1) = (pTiming->nVerticalBackPorch << ESP32_REG_SHIFT(DSI_BRG_DPI_V_CFG1, VBANK))
                                  | (pTiming->nVerticalSyncWidth << ESP32_REG_SHIFT(DSI_BRG_DPI_V_CFG1, VSYNC));
}

/* The two blocks number the pixel formats differently and both have to be told. */
static void Esp32p4DriverMipiDsi_SetColorCoding(LTDeviceMipiDsi_PixelFormat format) {
    u32 nCoding, nBridgeType, nLoose = 0;

    switch (format) {
        case kLTDeviceMipiDsi_PixelFormat_RGB565:
            nCoding     = ESP32_REG_VAL(DSI_HOST_DPI_COLOR_CODING, RGB565);
            nBridgeType = ESP32_REG_VAL(DSI_BRG_PIXEL_TYPE, RGB565);
            break;
        case kLTDeviceMipiDsi_PixelFormat_RGB666Packed:
            nCoding     = ESP32_REG_VAL(DSI_HOST_DPI_COLOR_CODING, RGB666);
            nBridgeType = ESP32_REG_VAL(DSI_BRG_PIXEL_TYPE, RGB666);
            break;
        case kLTDeviceMipiDsi_PixelFormat_RGB666Loose:
            nCoding     = ESP32_REG_VAL(DSI_HOST_DPI_COLOR_CODING, RGB666);
            nBridgeType = ESP32_REG_VAL(DSI_BRG_PIXEL_TYPE, RGB666);
            nLoose      = ESP32_REG_MASK(DSI_HOST_DPI_COLOR_CODING, LOOSELY18_EN);
            break;
        default:
            nCoding     = ESP32_REG_VAL(DSI_HOST_DPI_COLOR_CODING, RGB888);
            nBridgeType = ESP32_REG_VAL(DSI_BRG_PIXEL_TYPE, RGB888);
            break;
    }

    ESP32_REG(DSI_HOST_DPI_COLOR_CODING) =
        (nCoding << ESP32_REG_SHIFT(DSI_HOST_DPI_COLOR_CODING, CODING)) | nLoose;

    /* DATA_IN_TYPE is left clear for RGB; the bridge's YUV input path is
       unreachable because the host cannot carry YUV away from it. */
    ESP32_REG(DSI_BRG_PIXEL_TYPE) = nBridgeType << ESP32_REG_SHIFT(DSI_BRG_PIXEL_TYPE, RAW_TYPE);
}

/* Drive the clock lane continuously, or let hardware drop it to LP when the
   data lanes go idle. */
static void Esp32p4DriverMipiDsi_SetClockLaneContinuous(bool bContinuous) {
    ESP32_REG(DSI_HOST_LPCLK_CTRL) = bContinuous
        ? ESP32_REG_MASK(DSI_HOST_LPCLK_CTRL, TXREQUESTCLKHS)
        : (ESP32_REG_MASK(DSI_HOST_LPCLK_CTRL, TXREQUESTCLKHS)
         | ESP32_REG_MASK(DSI_HOST_LPCLK_CTRL, AUTO_CLKLANE_CTRL));
}

/*
 * The PHY status bits that must all read set for the configured lanes to be in
 * stop state.  Lanes the configuration does not use are not checked; they never
 * leave reset and would never report stopped.
 */
static u32 Esp32p4DriverMipiDsi_StopStateMask(u32 nDataLanes) {
    u32 nMask = ESP32_REG_MASK(DSI_HOST_PHY_STATUS, STOPSTATECLK);
    if (nDataLanes >= 1) nMask |= ESP32_REG_MASK(DSI_HOST_PHY_STATUS, STOPSTATE0);
    if (nDataLanes >= 2) nMask |= ESP32_REG_MASK(DSI_HOST_PHY_STATUS, STOPSTATE1);
    return nMask;
}

/* Translate what the host's two interrupt status registers reported into the
   device level's error flags. */
static u32 LT_ISR_SAFE Esp32p4DriverMipiDsi_TranslateErrors(u32 nStatus0, u32 nStatus1) {
    u32 nErrors = kLTDeviceMipiDsi_Error_None;

    /* INT_ST0 is the panel talking back: an acknowledge-and-error report, whose
       low sixteen bits are the panel's own error codes. */
    if (nStatus0 & ESP32_REG_MASK(DSI_HOST_INT_ST0, ACK_WITH_ERR)) {
        nErrors |= kLTDeviceMipiDsi_Error_PanelReported;
    }
    if (nStatus0 & ESP32_REG_MASK(DSI_HOST_INT_ST0, ACK_ECC_MULTI)) {
        nErrors |= kLTDeviceMipiDsi_Error_EccUncorrectable;
    }
    if (nStatus0 & ESP32_REG_MASK(DSI_HOST_INT_ST0, ACK_CHECKSUM)) {
        nErrors |= kLTDeviceMipiDsi_Error_Checksum;
    }

    if (nStatus1 & (ESP32_REG_MASK(DSI_HOST_INT_ST1, TO_HS_TX)
                  | ESP32_REG_MASK(DSI_HOST_INT_ST1, TO_LP_RX))) {
        nErrors |= kLTDeviceMipiDsi_Error_Timeout;
    }
    if (nStatus1 & ESP32_REG_MASK(DSI_HOST_INT_ST1, ECC_MULTI_ERR)) {
        nErrors |= kLTDeviceMipiDsi_Error_EccUncorrectable;
    }
    if (nStatus1 & ESP32_REG_MASK(DSI_HOST_INT_ST1, CRC_ERR)) {
        nErrors |= kLTDeviceMipiDsi_Error_Checksum;
    }
    /* The host raises this when a line carried a different number of pixels
       than the configured width; it does not say which way. */
    if (nStatus1 & ESP32_REG_MASK(DSI_HOST_INT_ST1, PKT_SIZE_ERR)) {
        nErrors |= kLTDeviceMipiDsi_Error_PixelCountShort;
    }
    if (nStatus1 & (ESP32_REG_MASK(DSI_HOST_INT_ST1, GEN_CMD_WR_ERR)
                  | ESP32_REG_MASK(DSI_HOST_INT_ST1, GEN_PLD_WR_ERR)
                  | ESP32_REG_MASK(DSI_HOST_INT_ST1, DPI_PLD_WR_ERR))) {
        nErrors |= kLTDeviceMipiDsi_Error_TxFifoOverflow;
    }
    if (nStatus1 & (ESP32_REG_MASK(DSI_HOST_INT_ST1, GEN_PLD_SEND_ERR)
                  | ESP32_REG_MASK(DSI_HOST_INT_ST1, DPI_BUFF_PLD_UNDER))) {
        nErrors |= kLTDeviceMipiDsi_Error_TxFifoUnderflow;
    }
    if (nStatus1 & ESP32_REG_MASK(DSI_HOST_INT_ST1, GEN_PLD_RECEV_ERR)) {
        nErrors |= kLTDeviceMipiDsi_Error_RxFifoOverflow;
    }
    if (nStatus1 & ESP32_REG_MASK(DSI_HOST_INT_ST1, GEN_PLD_RD_ERR)) {
        nErrors |= kLTDeviceMipiDsi_Error_RxFifoUnderflow;
    }

    return nErrors;
}

/*
 * The DSI interrupt, shared by the host's error line and the bridge's underrun
 * line - both blocks are read whichever one woke it.  Returns zero: only the
 * tick line advances the clock.
 */
static u32 LT_ISR_SAFE Esp32p4DriverMipiDsi_Isr(void) {
    /* Both host status registers clear on read, so each is read exactly once
       and everything downstream works from the copies. */
    u32 nStatus0 = ESP32_REG(DSI_HOST_INT_ST0);
    u32 nStatus1 = ESP32_REG(DSI_HOST_INT_ST1);

    u32 nBridgeStatus = ESP32_REG(DSI_BRG_INT_ST);
    if (nBridgeStatus) ESP32_REG(DSI_BRG_INT_CLR) = nBridgeStatus;

    u32 nErrors = Esp32p4DriverMipiDsi_TranslateErrors(nStatus0, nStatus1);

    /* The bridge's FIFO running dry is the same fault as the host's, seen from
       the other side of it. */
    if (nBridgeStatus & ESP32_REG_MASK(DSI_BRG_INT, UNDERRUN)) {
        nErrors |= kLTDeviceMipiDsi_Error_TxFifoUnderflow;
    }

    Esp32p4DriverMipiDsi *dsi = s_driverMipiDsi;
    if (nErrors && dsi && dsi->pErrorProc) {
        (*dsi->pErrorProc)((LTDeviceMipiDsi_Error)nErrors, dsi->pErrorClientData);
    }

    return 0;
}

/*
 * Wait for room in the command FIFO.  A header written while it is full is
 * silently dropped, so every header write is preceded by this.
 */
static bool Esp32p4DriverMipiDsi_WaitCommandFifo(void) {
    for (u32 i = 0; i < kFifoSpinLimit; i++) {
        if (! (ESP32_REG(DSI_HOST_CMD_PKT_STATUS)
               & ESP32_REG_MASK(DSI_HOST_CMD_PKT_STATUS, GEN_CMD_FULL))) {
            return true;
        }
    }
    LTLOG_YELLOWALERT("fifo.cmd.timeout", NULL);
    return false;
}

/* The same, for the payload FIFO. */
static bool Esp32p4DriverMipiDsi_WaitPayloadFifo(void) {
    for (u32 i = 0; i < kFifoSpinLimit; i++) {
        if (! (ESP32_REG(DSI_HOST_CMD_PKT_STATUS)
               & ESP32_REG_MASK(DSI_HOST_CMD_PKT_STATUS, GEN_PLD_W_FULL))) {
            return true;
        }
    }
    LTLOG_YELLOWALERT("fifo.pld.timeout", NULL);
    return false;
}

/*
 * Write a packet header.  Writing this register is what starts a transmission,
 * so it is always the last thing written for a packet.
 */
static bool Esp32p4DriverMipiDsi_WriteHeader(Esp32_MipiDsiDataType dataType, u32 nVirtualChannel,
                                             u8 nWordCountLow, u8 nWordCountHigh) {
    if (! Esp32p4DriverMipiDsi_WaitCommandFifo()) return false;

    ESP32_REG(DSI_HOST_GEN_HDR) =
          ((u32)dataType        << ESP32_REG_SHIFT(DSI_HOST_GEN_HDR, DT))
        | (nVirtualChannel      << ESP32_REG_SHIFT(DSI_HOST_GEN_HDR, VC))
        | ((u32)nWordCountLow   << ESP32_REG_SHIFT(DSI_HOST_GEN_HDR, WC_LSBYTE))
        | ((u32)nWordCountHigh  << ESP32_REG_SHIFT(DSI_HOST_GEN_HDR, WC_MSBYTE));
    return true;
}

/*
 * Push a payload into the FIFO four bytes at a time.  The FIFO is word wide and
 * the host takes the byte count from the header, so a payload that is not a
 * multiple of four is written as a whole word with the unused bytes left zero.
 */
static bool Esp32p4DriverMipiDsi_WritePayload(const u8 *pPayload, u32 nBytes) {
    while (nBytes) {
        u32 nWord = 0;
        u32 nChunk = (nBytes < 4) ? nBytes : 4;
        for (u32 i = 0; i < nChunk; i++) nWord |= (u32)pPayload[i] << (8 * i);

        if (! Esp32p4DriverMipiDsi_WaitPayloadFifo()) return false;
        ESP32_REG(DSI_HOST_GEN_PLD_DATA) = nWord;

        pPayload += nChunk;
        nBytes   -= nChunk;
    }
    return true;
}

/* Choose between the low power and high speed command paths.  The host has one
   bit per packet type and they all move together. */
static void Esp32p4DriverMipiDsi_SetCommandSpeed(bool bHighSpeed) {
    u32 nConfig = ESP32_REG(DSI_HOST_CMD_MODE_CFG);
    if (bHighSpeed) nConfig &= ~ESP32_REG_MASK(DSI_HOST_CMD_MODE_CFG, ALL_LP);
    else            nConfig |=  ESP32_REG_MASK(DSI_HOST_CMD_MODE_CFG, ALL_LP);
    ESP32_REG(DSI_HOST_CMD_MODE_CFG) = nConfig;
}

/*
 * Point the GDMA channel at the framebuffer and arm it.
 *
 * The descriptor is rebuilt rather than merely re-armed: the hardware clears
 * the valid bit as it consumes it, and rebuilding is cheaper than tracking
 * which fields survived.
 */
/*
 * Push the framebuffer out of the caches before the DMA reads it.
 *
 * The CPU writes pixels through a writeback cache, so until the dirty lines are
 * flushed the only copy of the frame is in the cache and the DMA - which goes
 * straight to PSRAM - fetches whatever was in memory before, usually zeroes.
 * The panel scans that out as black.
 *
 * Rounded outwards to a cache line at both ends: the caller's buffer need not
 * start or end on one, and the ROM call works in whole lines regardless, so
 * rounding is what keeps the partial lines at the edges from being skipped.
 */
static void Esp32p4DriverMipiDsi_FlushFrameBuffer(const void *pPixels, u32 nBytes) {
    if (! pPixels || ! nBytes) return;

    u32 nStart   = (u32)(LT_SIZE)pPixels;
    u32 nEnd     = nStart + nBytes;
    u32 nAligned = nStart & ~(u32)(kCacheLineBytes - 1);
    nEnd         = (nEnd + kCacheLineBytes - 1) & ~(u32)(kCacheLineBytes - 1);

    Cache_WriteBack_Addr(CACHE_MAP_L1_DCACHE | CACHE_MAP_L2_CACHE, nAligned, nEnd - nAligned);
}

static void Esp32p4DriverMipiDsi_ArmPixelDma(Esp32p4DriverMipiDsi *dsi) {
    /* Sixty-four bit transfers, so the item count is the byte count over eight,
       and BLOCK_TS is written one less than the count. */
    u32 nItems = dsi->nFrameBufferBytes / 8;

    lt_memset(&s_pixelDescriptor, 0, sizeof(s_pixelDescriptor));

    s_pixelDescriptor.nSourceAddressLow      = (u32)(LT_SIZE)dsi->pFrameBuffer;
    s_pixelDescriptor.nDestinationAddressLow = kEsp32_Gdma_MipiDsiBridgeMemory;
    s_pixelDescriptor.nBlockTransferSize     = nItems - 1;

    /*
     * SINC and DINC are inverted - a set bit holds the address still - so the
     * source increments by being left clear and the bridge's fixed sink is
     * selected by setting DINC.  SMS picks the memory master for the source;
     * DMS is left clear, which is the master the DSI bridge is on.
     */
    s_pixelDescriptor.nControlLow =
          ESP32_REG_MASK(GDMA_CH_CTL0, SMS)
        | ESP32_REG_MASK(GDMA_CH_CTL0, DINC)
        | (kEsp32_GdmaTransferWidth_64  << ESP32_REG_SHIFT(GDMA_CH_CTL0, SRC_TR_WIDTH))
        | (kEsp32_GdmaTransferWidth_64  << ESP32_REG_SHIFT(GDMA_CH_CTL0, DST_TR_WIDTH))
        | (kEsp32_GdmaBurstSize_512     << ESP32_REG_SHIFT(GDMA_CH_CTL0, SRC_MSIZE))
        | (kEsp32_GdmaBurstSize_256     << ESP32_REG_SHIFT(GDMA_CH_CTL0, DST_MSIZE));

    s_pixelDescriptor.nControlHigh =
          ESP32_REG_MASK(GDMA_CH_CTL1, ARLEN_EN)
        | (kPixelDmaBurstLength << ESP32_REG_SHIFT(GDMA_CH_CTL1, ARLEN))
        | ESP32_REG_MASK(GDMA_CH_CTL1, AWLEN_EN)
        | (kPixelDmaBurstLength << ESP32_REG_SHIFT(GDMA_CH_CTL1, AWLEN))
        | ESP32_REG_MASK(GDMA_CH_CTL1, LLI_LAST)
        | ESP32_REG_MASK(GDMA_CH_CTL1, LLI_VALID);

    /* Both ends walk a link list of exactly one item. */
    ESP32_GDMA_CH_REG(kPixelDmaChannel, CFG0) =
          (kEsp32_GdmaBlockTransfer_LinkedList << ESP32_REG_SHIFT(GDMA_CH_CFG0, SRC_MULTBLK_TYPE))
        | (kEsp32_GdmaBlockTransfer_LinkedList << ESP32_REG_SHIFT(GDMA_CH_CFG0, DST_MULTBLK_TYPE));

    /*
     * HS_SEL is inverted too: left clear on the destination it asks for
     * hardware handshaking, which is what lets the bridge pace the transfer by
     * raising its request line as FIFO space opens up.  Set on the source
     * because memory has no handshake to offer.
     */
    ESP32_GDMA_CH_REG(kPixelDmaChannel, CFG1) =
          (kEsp32_GdmaFlow_MemToPeri_Dmac << ESP32_REG_SHIFT(GDMA_CH_CFG1, TT_FC))
        | ESP32_REG_MASK(GDMA_CH_CFG1, HS_SEL_SRC)
        | (kEsp32_GdmaHandshake_MipiDsi << ESP32_REG_SHIFT(GDMA_CH_CFG1, DST_PER))
        | (kPixelDmaOutstandingLimit << ESP32_REG_SHIFT(GDMA_CH_CFG1, SRC_OSR_LMT))
        | (kPixelDmaOutstandingLimit << ESP32_REG_SHIFT(GDMA_CH_CFG1, DST_OSR_LMT));

    /* The link list pointer holds the descriptor address over sixty-four, which
       is why the descriptor has to be aligned that way. */
    ESP32_GDMA_CH_REG(kPixelDmaChannel, LLP0) =
        ((u32)(LT_SIZE)&s_pixelDescriptor >> 6) << kEsp32_RegisterGDMA_CH_LLP0_LOC_S;
    ESP32_GDMA_CH_REG(kPixelDmaChannel, LLP1) = 0;

    ESP32_REG(GDMA_CHEN0) = ESP32_GDMA_CHEN_ENABLE(kPixelDmaChannel);
}

/*________________________________________
  Esp32p4DriverMipiDsi API functions    */

static bool Esp32p4DriverMipiDsi_Configure(Esp32p4DriverMipiDsi *dsi,
                                           const LTDeviceMipiDsi_Config *pConfig) {
    if (! pConfig) return false;

    /* Two data lanes is all the part brings out, whatever the interface allows. */
    if (! pConfig->nDataLanes || pConfig->nDataLanes > kEsp32_MipiDsi_MaxDataLanes) {
        LTLOG_YELLOWALERT("configure.lanes", "asked for %lu lanes, the part has %lu",
                          LT_Pu32(pConfig->nDataLanes), LT_Pu32(kEsp32_MipiDsi_MaxDataLanes));
        return false;
    }

    /* The bridge can read YUV from memory but the host cannot carry it to the
       panel, so there is no YUV path through this hardware. */
    if (! Esp32p4DriverMipiDsi_BitsPerPixel(pConfig->pixelFormat)) {
        LTLOG_YELLOWALERT("configure.format", "pixel format %lu is not RGB",
                          LT_Pu32((u32)pConfig->pixelFormat));
        return false;
    }

    /*
     * Command mode would have the host's DCS write path carrying the pixels
     * rather than the bridge, which is a second pixel path this driver does not
     * implement.
     */
    if (kLTDeviceMipiDsi_Mode_Command == pConfig->mode) {
        LTLOG_YELLOWALERT("configure.commandmode", "command mode is not implemented");
        return false;
    }
    if (kLTDeviceMipiDsi_Mode_Unset == pConfig->mode) return false;

    if (! pConfig->timing.nFrameRateHz
     || ! pConfig->timing.nHorizontalActive || ! pConfig->timing.nVerticalActive) {
        return false;
    }

    u32 nLaneRateMbps = pConfig->nLaneRateMbps
                      ? pConfig->nLaneRateMbps
                      : Esp32p4DriverMipiDsi_DeriveLaneRate(pConfig);

    if (nLaneRateMbps < kEsp32_MipiDsi_MinLaneRateMbps
     || nLaneRateMbps > kEsp32_MipiDsi_MaxLaneRateMbps) {
        LTLOG_YELLOWALERT("configure.lanerate", "%lu Mbps is outside %lu..%lu",
                          LT_Pu32(nLaneRateMbps), LT_Pu32(kEsp32_MipiDsi_MinLaneRateMbps),
                          LT_Pu32(kEsp32_MipiDsi_MaxLaneRateMbps));
        return false;
    }

    /*
     * The DPI clock comes from a whole divider off a fixed source, so the pixel
     * rate the panel asked for is rounded to what the divider can make, and the
     * host's horizontal timing is later scaled by the rate that results rather
     * than the one requested.
     */
    u32 nPixelClockMHz = Esp32p4DriverMipiDsi_DerivePixelClockMHz(&pConfig->timing);
    u32 nDivider = kDpiSourceClockMHz / nPixelClockMHz;
    if (nDivider < 2 || nDivider > kEsp32_MipiDsi_MaxDpiClockDivider) {
        LTLOG_YELLOWALERT("configure.dpiclock", "%lu MHz needs divider %lu",
                          LT_Pu32(nPixelClockMHz), LT_Pu32(nDivider));
        return false;
    }

    dsi->config        = *pConfig;
    dsi->nLaneRateMbps = nLaneRateMbps;
    dsi->nDpiClockMHz  = kDpiSourceClockMHz / nDivider;
    dsi->bConfigured   = true;

    DLOG("configure", "lanes=%lu rate=%lu dpi=%lu", LT_Pu32(pConfig->nDataLanes),
         LT_Pu32(dsi->nLaneRateMbps), LT_Pu32(dsi->nDpiClockMHz));
    return true;
}

/*
 * Bring up the D-PHY's analog supply, LDO channel 3.
 *
 * The PHY's digital section runs off the core supply and answers its registers
 * without this, which is why a dead rail reads as a PLL that will not lock
 * rather than as a missing peripheral.  The channel is idle out of reset.
 *
 * The output is Vref * (1 + 0.25 * MUL), with Vref set by DREF.  The eFuse
 * carries signed corrections to that ideal - K scales it, VOS offsets it and C
 * trims the multiplier step - rather than a DREF/MUL pair, so the pair is
 * searched for with the corrections applied.  Everything is scaled by 1000 to
 * keep it in integers.
 */
static void Esp32p4DriverMipiDsi_StartPhyLdo(void) {
    s32 nK1000   = 1000;
    s32 nVos1000 = 0;
    s32 nC1000   = 1000;

    u32 nMacSys2 = ESP32_REG(EFUSE_RD_MAC_SYS_2);
    u32 nBlockVersion = ((nMacSys2 & ESP32_REG_MASK(EFUSE, BLK_VERSION_MAJOR)) >> ESP32_REG_SHIFT(EFUSE, BLK_VERSION_MAJOR))
                      * 100
                      + ((nMacSys2 & ESP32_REG_MASK(EFUSE, BLK_VERSION_MINOR)) >> ESP32_REG_SHIFT(EFUSE, BLK_VERSION_MINOR));

    if (nBlockVersion >= 100) {
        u32 nMacSys3 = ESP32_REG(EFUSE_RD_MAC_SYS_3);
        u32 nFuseK   = (nMacSys3 & ESP32_REG_MASK(EFUSE, LDO_VO3_K))   >> ESP32_REG_SHIFT(EFUSE, LDO_VO3_K);
        u32 nFuseVos = (nMacSys3 & ESP32_REG_MASK(EFUSE, LDO_VO3_VOS)) >> ESP32_REG_SHIFT(EFUSE, LDO_VO3_VOS);
        u32 nFuseC   = (nMacSys3 & ESP32_REG_MASK(EFUSE, LDO_VO3_C))   >> ESP32_REG_SHIFT(EFUSE, LDO_VO3_C);

        /* Sign-magnitude, and the magnitudes are offsets from a nominal. */
        if (nFuseK)   nK1000   = (nFuseK   & 0x80) ? 975 - (s32)(nFuseK   & 0x7f) : 975 + (s32)nFuseK;
        if (nFuseVos) nVos1000 = (nFuseVos & 0x20) ? -3  - (s32)(nFuseVos & 0x1f) : (s32)nFuseVos - 3;
        if (nFuseC)   nC1000   = (nFuseC   & 0x20) ? 990 - (s32)(nFuseC   & 0x1f) : 990 + (s32)nFuseC;
    }

    u8  nDRef = kPhyLdoUncalibratedDRef;
    u8  nMul  = kPhyLdoUncalibratedMul;
    s32 nBestError = 0;
    bool bFound = false;

    for (u32 nDRefTry = 0; nDRefTry < 16; nDRefTry++) {
        /* Vref in twentieths of a volt: 0.5V plus 0.05V a step to 9, then
           1V plus 0.1V a step. */
        s32 nVref20 = (nDRefTry < 9) ? (s32)(10 + nDRefTry) : (s32)(20 + (nDRefTry - 9) * 2);
        for (u32 nMulTry = 0; nMulTry < 8; nMulTry++) {
            s32 nVout = (nVref20 * nK1000 + 20 * nVos1000) * (4000 + (s32)nMulTry * nC1000);
            s32 nError = (s32)(kPhyLdoTargetMilliVolts * 80000) - nVout;
            if (nError < 0) nError = -nError;
            if (! bFound || nError < nBestError) {
                bFound     = true;
                nBestError = nError;
                nDRef      = (u8)nDRefTry;
                nMul       = (u8)nMulTry;
            }
        }
    }

    /* Regulate rather than pass the 3.3V input rail through, then set the
       target, then take the channel off the eFuse's control and onto TIEH. */
    ESP32_REG(PMU_EXT_LDO_CHAN3) &= ~(u32)ESP32_REG_MASK(PMU_EXT_LDO_CHAN3, TIEH);

    u32 nAnalog = ESP32_REG(PMU_EXT_LDO_CHAN3_ANA);
    nAnalog &= ~(ESP32_REG_MASK(PMU_EXT_LDO_CHAN3_ANA, DREF) | ESP32_REG_MASK(PMU_EXT_LDO_CHAN3_ANA, MUL));
    nAnalog |= ((u32)nDRef << ESP32_REG_SHIFT(PMU_EXT_LDO_CHAN3_ANA, DREF))
             | ((u32)nMul  << ESP32_REG_SHIFT(PMU_EXT_LDO_CHAN3_ANA, MUL));
    ESP32_REG(PMU_EXT_LDO_CHAN3_ANA) = nAnalog;

    ESP32_REG(PMU_EXT_LDO_CHAN3) |= ESP32_REG_MASK(PMU_EXT_LDO_CHAN3, FORCE_TIEH_SEL);
    ESP32_REG(PMU_EXT_LDO_CHAN3) &= ~(u32)ESP32_REG_MASK(PMU_EXT_LDO_CHAN3, TIEH_SEL);

    ESP32_REG(PMU_EXT_LDO_CHAN3_ANA) |= ESP32_REG_MASK(PMU_EXT_LDO_CHAN3_ANA, EN_VDET);
    ESP32_REG(PMU_EXT_LDO_CHAN3)     |= ESP32_REG_MASK(PMU_EXT_LDO_CHAN3, XPD);

    DLOG("ldo", "dref=%lu mul=%lu", LT_Pu32(nDRef), LT_Pu32(nMul));

    /* Let the rail reach its target before the PHY is brought out of reset. */
    esp_rom_delay_us(kPhyLdoSettleMicroseconds);
}

static bool Esp32p4DriverMipiDsi_PowerOn(Esp32p4DriverMipiDsi *dsi) {
    if (! dsi->bConfigured) return false;
    if (dsi->bPoweredOn) return true;

    /* Power the PHY's analog section before any of its clocks or resets. */
    Esp32p4DriverMipiDsi_StartPhyLdo();

    /* The DSI gate releases the bridge's reset; the host has none of its own. */
    Esp32_ClockEnablePeripheralClock(kEsp32_Clock_MIPI_DSI);

    /* The GDMA needs its CPU clock as well as the system clock the gate
       descriptor carries. */
    Esp32_ClockEnablePeripheralClock(kEsp32_Clock_GDMA);
    ESP32_REG(HP_CLKRST_SOC_CLK_CTRL0) |= ESP32_REG_MASK(HP_CLKRST_SOC_CLK_CTRL0, GDMA_CPU_CLK_EN);

    u32 nPeriClock02 = ESP32_REG(HP_CLKRST_PERI_CLK_CTRL02);
    nPeriClock02 &= ~ESP32_REG_MASK(HP_CLKRST_PERI_CLK_CTRL02, DSI_DPHY_CLK_SRC_SEL);
    nPeriClock02 |= (u32)kEsp32_RegisterHP_CLKRST_DSI_DPHY_CLK_SRC_PLL_F20M_V
                    << ESP32_REG_SHIFT(HP_CLKRST_PERI_CLK_CTRL02, DSI_DPHY_CLK_SRC_SEL);
    ESP32_REG(HP_CLKRST_PERI_CLK_CTRL02) = nPeriClock02;

    /* The D-PHY's reference and configuration clocks, and the DPI pixel clock;
       the divider field is written one less than the divider. */
    u32 nDivider = kDpiSourceClockMHz / dsi->nDpiClockMHz;
    u32 nPeriClock03 = ESP32_REG(HP_CLKRST_PERI_CLK_CTRL03);
    nPeriClock03 &= ~(ESP32_REG_MASK(HP_CLKRST_PERI_CLK_CTRL03, DSI_DPICLK_SRC_SEL)
                    | ESP32_REG_MASK(HP_CLKRST_PERI_CLK_CTRL03, DSI_DPICLK_DIV_NUM));
    nPeriClock03 |= ESP32_REG_MASK(HP_CLKRST_PERI_CLK_CTRL03, DSI_DPHY_CFG_CLK_EN)
                  | ESP32_REG_MASK(HP_CLKRST_PERI_CLK_CTRL03, DSI_DPHY_PLL_REFCLK_EN)
                  | ESP32_REG_MASK(HP_CLKRST_PERI_CLK_CTRL03, DSI_DPICLK_EN)
                  | ((u32)kEsp32_RegisterHP_CLKRST_DSI_DPICLK_SRC_PLL_F160M_V
                     << ESP32_REG_SHIFT(HP_CLKRST_PERI_CLK_CTRL03, DSI_DPICLK_SRC_SEL))
                  | ((nDivider - 1) << ESP32_REG_SHIFT(HP_CLKRST_PERI_CLK_CTRL03, DSI_DPICLK_DIV_NUM));
    ESP32_REG(HP_CLKRST_PERI_CLK_CTRL03) = nPeriClock03;

    /* The bridge gates the host's register clock, so it comes up first. */
    ESP32_REG(DSI_BRG_CLK_EN)    = ESP32_REG_MASK(DSI_BRG_CLK, EN);
    ESP32_REG(DSI_BRG_HOST_CTRL) = ESP32_REG_MASK(DSI_BRG_HOST_CTRL, CFG_REF_CLK_EN);

    /* N_LANES is written one less than the lane count. */
    ESP32_REG(DSI_HOST_PHY_IF_CFG) =
          ((dsi->config.nDataLanes - 1) << ESP32_REG_SHIFT(DSI_HOST_PHY_IF_CFG, N_LANES))
        | (kStopWaitTime << ESP32_REG_SHIFT(DSI_HOST_PHY_IF_CFG, STOP_WAIT_TIME));

    ESP32_REG(DSI_HOST_PWR_UP) = ESP32_REG_MASK(DSI_HOST_PWR_UP, SHUTDOWNZ);

    /* The test interface is only live once the PHY is powered, so the PLL is
       configured after the reset release rather than before it. */
    ESP32_REG(DSI_HOST_PHY_RSTZ) = ESP32_REG_MASK(DSI_HOST_PHY_RSTZ, SHUTDOWNZ);
    ESP32_REG(DSI_HOST_PHY_RSTZ) = 0;
    ESP32_REG(DSI_HOST_PHY_RSTZ) = ESP32_REG_MASK(DSI_HOST_PHY_RSTZ, SHUTDOWNZ)
                                 | ESP32_REG_MASK(DSI_HOST_PHY_RSTZ, RSTZ);
    ESP32_REG(DSI_HOST_PHY_RSTZ) |= ESP32_REG_MASK(DSI_HOST_PHY_RSTZ, ENABLECLK)
                                  | ESP32_REG_MASK(DSI_HOST_PHY_RSTZ, FORCEPLL);

    u32 nActualRateMbps = Esp32p4DriverMipiDsi_ConfigurePll(dsi->nLaneRateMbps);
    if (! nActualRateMbps) {
        LTLOG_REDALERT("poweron.pll", "no divider pair for %lu Mbps", LT_Pu32(dsi->nLaneRateMbps));
        Esp32_ClockDisablePeripheralClock(kEsp32_Clock_MIPI_DSI);
        return false;
    }
    dsi->nLaneRateMbps = nActualRateMbps;
    DLOG("poweron.pll", "rev=%lu ref=%lu MHz rate=%lu Mbps",
         LT_Pu32(Esp32p4DriverMipiDsi_WaferRevision()),
         LT_Pu32(Esp32p4DriverMipiDsi_PhyReferenceClockMHz()),
         LT_Pu32(nActualRateMbps));

    bool bLocked = false;
    for (u32 i = 0; i < kPhyReadySpinLimit; i++) {
        if (ESP32_REG(DSI_HOST_PHY_STATUS) & ESP32_REG_MASK(DSI_HOST_PHY_STATUS, LOCK)) {
            bLocked = true;
            break;
        }
    }
    if (! bLocked) {
        LTLOG_REDALERT("poweron.nolock", "rev=%lu ref=%lu MHz rate=%lu Mbps phy_status=0x%08lx",
                       LT_Pu32(Esp32p4DriverMipiDsi_WaferRevision()),
                       LT_Pu32(Esp32p4DriverMipiDsi_PhyReferenceClockMHz()),
                       LT_Pu32(dsi->nLaneRateMbps),
                       LT_Pu32(ESP32_REG(DSI_HOST_PHY_STATUS)));
        Esp32_ClockDisablePeripheralClock(kEsp32_Clock_MIPI_DSI);
        return false;
    }

    u32 nStopMask = Esp32p4DriverMipiDsi_StopStateMask(dsi->config.nDataLanes);
    bool bStopped = false;
    for (u32 i = 0; i < kPhyReadySpinLimit; i++) {
        if ((ESP32_REG(DSI_HOST_PHY_STATUS) & nStopMask) == nStopMask) {
            bStopped = true;
            break;
        }
    }
    if (! bStopped) {
        LTLOG_REDALERT("poweron.nostopstate", "phy_status=0x%08lx",
                       LT_Pu32(ESP32_REG(DSI_HOST_PHY_STATUS)));
        Esp32_ClockDisablePeripheralClock(kEsp32_Clock_MIPI_DSI);
        return false;
    }

    /* Both escape and timeout clocks divide down from the lane byte clock. */
    u32 nLaneByteRate = dsi->nLaneRateMbps / 8;
    u32 nEscapeDivider = nLaneByteRate / kEscapeClockMHz;
    u32 nTimeoutDivider = nLaneByteRate / kTimeoutClockMHz;
    if (nEscapeDivider < kMinimumClockDivider) nEscapeDivider = kMinimumClockDivider;
    if (nTimeoutDivider < kMinimumClockDivider) nTimeoutDivider = kMinimumClockDivider;
    ESP32_REG(DSI_HOST_CLKMGR_CFG) =
          (nEscapeDivider  << ESP32_REG_SHIFT(DSI_HOST_CLKMGR_CFG, TX_ESC_CLK_DIVISION))
        | (nTimeoutDivider << ESP32_REG_SHIFT(DSI_HOST_CLKMGR_CFG, TO_CLK_DIVISION));

    ESP32_REG(DSI_HOST_PHY_TMR_CFG) =
          (kDataLp2HsTime << ESP32_REG_SHIFT(DSI_HOST_PHY_TMR_CFG, LP2HS_TIME))
        | (kDataHs2LpTime << ESP32_REG_SHIFT(DSI_HOST_PHY_TMR_CFG, HS2LP_TIME));
    ESP32_REG(DSI_HOST_PHY_TMR_LPCLK_CFG) =
          (kClockLp2HsTime << ESP32_REG_SHIFT(DSI_HOST_PHY_TMR_LPCLK_CFG, CLKLP2HS_TIME))
        | (kClockHs2LpTime << ESP32_REG_SHIFT(DSI_HOST_PHY_TMR_LPCLK_CFG, CLKHS2LP_TIME));
    ESP32_REG(DSI_HOST_PHY_TMR_RD_CFG) =
        kMaxReadTime << ESP32_REG_SHIFT(DSI_HOST_PHY_TMR_RD_CFG, MAX_RD_TIME);

    /* Check what comes back.  EoTp framing is the panel's business, so it
       follows the configuration; EoTp in low power is left off because it costs
       escape mode time for no benefit. */
    u32 nPacketHandling = ESP32_REG_MASK(DSI_HOST_PCKHDL_CFG, ECC_RX_EN)
                        | ESP32_REG_MASK(DSI_HOST_PCKHDL_CFG, CRC_RX_EN);
    if (dsi->config.bEnableEotPacket) {
        nPacketHandling |= ESP32_REG_MASK(DSI_HOST_PCKHDL_CFG, EOTP_TX_EN)
                         | ESP32_REG_MASK(DSI_HOST_PCKHDL_CFG, EOTP_RX_EN);
    }
    ESP32_REG(DSI_HOST_PCKHDL_CFG) = nPacketHandling;

    /* No transfer deadlines: a timeout here would abort a transfer the panel
       was merely slow to answer, and the error path reports what went wrong
       either way. */
    ESP32_REG(DSI_HOST_TO_CNT_CFG)   = 0;
    ESP32_REG(DSI_HOST_HS_RD_TO_CNT) = 0;
    ESP32_REG(DSI_HOST_LP_RD_TO_CNT) = 0;
    ESP32_REG(DSI_HOST_HS_WR_TO_CNT) = 0;
    ESP32_REG(DSI_HOST_LP_WR_TO_CNT) = 0;
    ESP32_REG(DSI_HOST_BTA_TO_CNT)   = 0;

    /* Start in command mode with the clock lane in LP: the panel's init
       sequence has to go out before there is anything to scan out. */
    ESP32_REG(DSI_HOST_MODE_CFG)   = ESP32_REG_MASK(DSI_HOST_MODE_CFG, CMD_VIDEO_MODE);
    ESP32_REG(DSI_HOST_LPCLK_CTRL) = 0;
    Esp32p4DriverMipiDsi_SetCommandSpeed(false);

    Esp32p4DriverMipiDsi_SetColorCoding(dsi->config.pixelFormat);
    ESP32_REG(DSI_HOST_DPI_VCID) = dsi->config.nVirtualChannel;
    ESP32_REG(DSI_HOST_GEN_VCID) = dsi->config.nVirtualChannel
                                   << ESP32_REG_SHIFT(DSI_HOST_GEN_VCID, RX);
    /* Active high throughout: these DPI signals never leave the chip, the host
       encoding them into DSI packets rather than driving pins. */
    ESP32_REG(DSI_HOST_DPI_CFG_POL) = 0;

    Esp32p4DriverMipiDsi_SetTiming(dsi);

    /* Errors are reported, nothing else is; a set mask bit turns a source off. */
    ESP32_REG(DSI_HOST_INT_MSK0) = 0;
    ESP32_REG(DSI_HOST_INT_MSK1) = 0;
    ESP32_REG(DSI_BRG_INT_ENA)   = ESP32_REG_MASK(DSI_BRG_INT, UNDERRUN);

    /* Both DSI sources share one handler and one CPU line - the handler reads
       both blocks' status whichever of them woke it. */
    Esp32_AttachInterrupt(kEsp32_ExternalIrq_DSIHost, kEsp32_IrqNumber_MipiDsi,
                          kEsp32_IrqType_Level, kEsp32_IrqPriority_MipiDsi,
                          &Esp32p4DriverMipiDsi_Isr);
    Esp32_AttachInterrupt(kEsp32_ExternalIrq_DSIBridge, kEsp32_IrqNumber_MipiDsi,
                          kEsp32_IrqType_Level, kEsp32_IrqPriority_MipiDsi,
                          &Esp32p4DriverMipiDsi_Isr);

    dsi->bPoweredOn = true;
    DLOG("poweron", "rate=%lu Mbps", LT_Pu32(dsi->nLaneRateMbps));
    return true;
}

static void Esp32p4DriverMipiDsi_PowerOff(Esp32p4DriverMipiDsi *dsi) {
    if (! dsi->bPoweredOn) return;

    Esp32p4DriverMipiDsi_StopVideo(dsi);

    Esp32_DetachInterrupt(kEsp32_ExternalIrq_DSIBridge, kEsp32_IrqNumber_MipiDsi);
    Esp32_DetachInterrupt(kEsp32_ExternalIrq_DSIHost, kEsp32_IrqNumber_MipiDsi);

    ESP32_REG(DSI_HOST_PHY_RSTZ) = 0;
    ESP32_REG(DSI_HOST_PWR_UP)   = 0;

    ESP32_REG(HP_CLKRST_PERI_CLK_CTRL03) &=
        ~(ESP32_REG_MASK(HP_CLKRST_PERI_CLK_CTRL03, DSI_DPHY_CFG_CLK_EN)
        | ESP32_REG_MASK(HP_CLKRST_PERI_CLK_CTRL03, DSI_DPHY_PLL_REFCLK_EN)
        | ESP32_REG_MASK(HP_CLKRST_PERI_CLK_CTRL03, DSI_DPICLK_EN));

    /* The bridge and the host gate together, so this takes both down. */
    Esp32_ClockDisablePeripheralClock(kEsp32_Clock_MIPI_DSI);

    dsi->bPoweredOn = false;
}

static bool Esp32p4DriverMipiDsi_SetFrameBuffer(Esp32p4DriverMipiDsi *dsi,
                                                const void *pPixels, u32 nBytes) {
    if (! pPixels) {
        dsi->pFrameBuffer      = NULL;
        dsi->nFrameBufferBytes = 0;
        return true;
    }

    /*
     * The DMA moves sixty-four bits at a time and the bridge counts the frame
     * in sixty-four bit words, so a framebuffer that is not a whole number of
     * them would leave the two disagreeing about where the frame ended.
     */
    if (! nBytes || (nBytes % 8)) {
        LTLOG_YELLOWALERT("setframebuffer.align", "%lu bytes is not a multiple of eight",
                          LT_Pu32(nBytes));
        return false;
    }

    u32 nBitsPerPixel = Esp32p4DriverMipiDsi_BitsPerPixel(dsi->config.pixelFormat);
    u64 nNeededBits   = (u64)dsi->config.timing.nHorizontalActive
                      * dsi->config.timing.nVerticalActive * nBitsPerPixel;
    if ((u64)nBytes * 8 < nNeededBits) {
        LTLOG_YELLOWALERT("setframebuffer.small", "%lu bytes for a %lux%lu frame",
                          LT_Pu32(nBytes), LT_Pu32(dsi->config.timing.nHorizontalActive),
                          LT_Pu32(dsi->config.timing.nVerticalActive));
        return false;
    }

    dsi->pFrameBuffer      = pPixels;
    dsi->nFrameBufferBytes = nBytes;
    return true;
}

static bool Esp32p4DriverMipiDsi_StartVideo(Esp32p4DriverMipiDsi *dsi) {
    if (! dsi->bPoweredOn || ! dsi->pFrameBuffer) return false;
    if (dsi->bVideoActive) return true;

    u32 nBurstType;
    switch (dsi->config.mode) {
        case kLTDeviceMipiDsi_Mode_VideoSyncEvent:
            nBurstType = ESP32_REG_VAL(DSI_HOST_VID_MODE_CFG, NON_BURST_SYNC_EVENTS);
            break;
        case kLTDeviceMipiDsi_Mode_VideoBurst:
            nBurstType = ESP32_REG_VAL(DSI_HOST_VID_MODE_CFG, BURST);
            break;
        default:
            nBurstType = ESP32_REG_VAL(DSI_HOST_VID_MODE_CFG, NON_BURST_SYNC_PULSES);
            break;
    }

    /*
     * Drop to low power through every blanking interval long enough to pay for
     * the transition, and keep the low power command path open so a panel
     * command can go out between frames without stopping the stream.
     */
    ESP32_REG(DSI_HOST_VID_MODE_CFG) =
          (nBurstType << ESP32_REG_SHIFT(DSI_HOST_VID_MODE_CFG, VID_MODE_TYPE))
        | ESP32_REG_MASK(DSI_HOST_VID_MODE_CFG, LP_VSA_EN)
        | ESP32_REG_MASK(DSI_HOST_VID_MODE_CFG, LP_VBP_EN)
        | ESP32_REG_MASK(DSI_HOST_VID_MODE_CFG, LP_VFP_EN)
        | ESP32_REG_MASK(DSI_HOST_VID_MODE_CFG, LP_VACT_EN)
        | ESP32_REG_MASK(DSI_HOST_VID_MODE_CFG, LP_HBP_EN)
        | ESP32_REG_MASK(DSI_HOST_VID_MODE_CFG, LP_HFP_EN)
        | ESP32_REG_MASK(DSI_HOST_VID_MODE_CFG, LP_CMD_EN);

    /* One chunk per line and no null packets: the link was configured from this
       geometry, so it has the bandwidth for a whole line in one go. */
    ESP32_REG(DSI_HOST_VID_PKT_SIZE)   = dsi->config.timing.nHorizontalActive;
    ESP32_REG(DSI_HOST_VID_NUM_CHUNKS) = 0;
    ESP32_REG(DSI_HOST_VID_NULL_SIZE)  = 0;

    Esp32p4DriverMipiDsi_SetClockLaneContinuous(dsi->config.bContinuousClock);

    /* The bridge counts the frame in sixty-four bit words, and the SET bit is
       the write trigger rather than part of the count. */
    ESP32_REG(DSI_BRG_RAW_NUM_CFG) =
          ((dsi->nFrameBufferBytes / 8) << ESP32_REG_SHIFT(DSI_BRG_RAW_NUM_CFG, TOTAL))
        | ESP32_REG_MASK(DSI_BRG_RAW_NUM_CFG, TOTAL_SET);

    /* On a FIFO stall, discard the rest of the line rather than let the frame
       desynchronise behind it. */
    ESP32_REG(DSI_BRG_DPI_MISC_CONFIG) =
        dsi->config.timing.nHorizontalActive
        << ESP32_REG_SHIFT(DSI_BRG_DPI_MISC_CONFIG, FIFO_UNDERRUN_DISCARD_VCNT);

    /* CONTROLLER clear leaves the DMA pacing itself off the bridge's requests,
       which is what lets one descriptor cover a whole frame. */
    ESP32_REG(DSI_BRG_DMA_FLOW_CTRL) = 1u << ESP32_REG_SHIFT(DSI_BRG_DMA_FLOW_CTRL, MULTIBLK_NUM);
    ESP32_REG(DSI_BRG_DMA_FRAME_INTERVAL) = 0;
    ESP32_REG(DSI_BRG_DMA_REQ_CFG) =
        kBridgeBurstLength << ESP32_REG_SHIFT(DSI_BRG_DMA_REQ_CFG, BURST_LEN);
    ESP32_REG(DSI_BRG_RAW_BUF_ALMOST_EMPTY_THRD) = kBridgeFifoWords - kBridgeBurstLength;

    ESP32_REG(DSI_BRG_EN) = ESP32_REG_MASK(DSI_BRG, EN);

    /* The GDMA's global enable.  Its channel events go nowhere: a frame
       completing is not something anyone waits for. */
    ESP32_REG(GDMA_CFG0) = ESP32_REG_MASK(GDMA_CFG0, DMAC_EN);
    Esp32p4DriverMipiDsi_FlushFrameBuffer(dsi->pFrameBuffer, dsi->nFrameBufferBytes);
    Esp32p4DriverMipiDsi_ArmPixelDma(dsi);

    ESP32_REG(DSI_HOST_MODE_CFG) = 0;   /* video mode */
    ESP32_REG(DSI_BRG_DPI_MISC_CONFIG) |= ESP32_REG_MASK(DSI_BRG_DPI_MISC_CONFIG, DPI_EN);
    ESP32_REG(DSI_BRG_DPI_CONFIG_UPDATE) = ESP32_REG_MASK(DSI_BRG_DPI_CONFIG, UPDATE);

    dsi->bVideoActive = true;
    return true;
}

static void Esp32p4DriverMipiDsi_StopVideo(Esp32p4DriverMipiDsi *dsi) {
    if (! dsi->bVideoActive) return;

    ESP32_REG(DSI_BRG_DPI_MISC_CONFIG) &= ~ESP32_REG_MASK(DSI_BRG_DPI_MISC_CONFIG, DPI_EN);
    ESP32_REG(DSI_BRG_DPI_CONFIG_UPDATE) = ESP32_REG_MASK(DSI_BRG_DPI_CONFIG, UPDATE);
    ESP32_REG(DSI_BRG_EN) = 0;

    /* Abort rather than disable: the channel is mid-frame, and a clean stop
       would wait for a frame boundary the bridge is no longer producing. */
    ESP32_REG(GDMA_CHEN1) = ESP32_GDMA_CHEN_ABORT(kPixelDmaChannel);
    ESP32_REG(GDMA_CHEN0) = ESP32_GDMA_CHEN_DISABLE(kPixelDmaChannel);

    /* Back to command mode so the panel can still be addressed with the stream
       stopped, which is what a shutdown sequence needs. */
    ESP32_REG(DSI_HOST_MODE_CFG) = ESP32_REG_MASK(DSI_HOST_MODE_CFG, CMD_VIDEO_MODE);
    Esp32p4DriverMipiDsi_SetClockLaneContinuous(false);

    dsi->bVideoActive = false;
}

/*
 * Command mode is not implemented, and Configure rejects it - so this is only
 * reachable if the Device level called out of contract.
 */
static bool Esp32p4DriverMipiDsi_WriteFrame(Esp32p4DriverMipiDsi *dsi,
                                            const void *pPixels, u32 nBytes) {
    LT_UNUSED(dsi);
    LT_UNUSED(pPixels);
    LT_UNUSED(nBytes);
    LTLOG_YELLOWALERT("writeframe.unsupported", NULL);
    return false;
}

static bool Esp32p4DriverMipiDsi_SendPacket(Esp32p4DriverMipiDsi *dsi,
                                            const LTDriverMipiDsi_Packet *pPacket) {
    if (! dsi->bPoweredOn || ! pPacket) return false;
    if (pPacket->nPayloadBytes && ! pPacket->pPayload) return false;

    Esp32p4DriverMipiDsi_SetCommandSpeed(pPacket->bHighSpeed);

    const u8 *pPayload = pPacket->pPayload;
    u32 nBytes = pPacket->nPayloadBytes;

    switch (pPacket->packetType) {
        case kLTDriverMipiDsi_PacketType_DcsShort: {
            /* A DCS short packet is the command byte and at most one parameter,
               both carried in the header's word count field. */
            if (! nBytes || nBytes > 2) return false;
            Esp32_MipiDsiDataType dcsType = (1 == nBytes)
                ? kEsp32_MipiDsiDataType_DcsShortWrite0
                : kEsp32_MipiDsiDataType_DcsShortWrite1;
            return Esp32p4DriverMipiDsi_WriteHeader(dcsType, pPacket->nVirtualChannel,
                                                    pPayload[0], (2 == nBytes) ? pPayload[1] : 0);
        }

        case kLTDriverMipiDsi_PacketType_GenericShort: {
            /* Zero, one or two payload bytes, each with its own data type. */
            if (nBytes > 2) return false;
            Esp32_MipiDsiDataType genType = (0 == nBytes) ? kEsp32_MipiDsiDataType_GenericShortWrite0
                                          : (1 == nBytes) ? kEsp32_MipiDsiDataType_GenericShortWrite1
                                                          : kEsp32_MipiDsiDataType_GenericShortWrite2;
            return Esp32p4DriverMipiDsi_WriteHeader(genType, pPacket->nVirtualChannel,
                                                    nBytes ? pPayload[0] : 0,
                                                    (2 == nBytes) ? pPayload[1] : 0);
        }

        case kLTDriverMipiDsi_PacketType_DcsLong:
        case kLTDriverMipiDsi_PacketType_GenericLong: {
            if (! nBytes) return false;
            /* The payload goes in first: writing the header starts the
               transmission, and the host sends whatever is in the FIFO. */
            if (! Esp32p4DriverMipiDsi_WritePayload(pPayload, nBytes)) return false;
            Esp32_MipiDsiDataType longType =
                (kLTDriverMipiDsi_PacketType_DcsLong == pPacket->packetType)
                    ? kEsp32_MipiDsiDataType_DcsLongWrite
                    : kEsp32_MipiDsiDataType_GenericLongWrite;
            return Esp32p4DriverMipiDsi_WriteHeader(longType, pPacket->nVirtualChannel,
                                                    (u8)(nBytes & 0xff), (u8)(nBytes >> 8));
        }

        default: break;
    }

    return false;
}

static s32 Esp32p4DriverMipiDsi_ReceivePacket(Esp32p4DriverMipiDsi *dsi,
                                              const LTDriverMipiDsi_Packet *pRequest,
                                              u8 *pReadBuffer, u32 nReadBufferBytes) {
    if (! dsi->bPoweredOn || ! pRequest || ! pReadBuffer || ! nReadBufferBytes) return -1;
    if (pRequest->nPayloadBytes && ! pRequest->pPayload) return -1;
    if (pRequest->nPayloadBytes > 2) return -1;

    /*
     * A read needs the panel to drive the bus back, which only happens in low
     * power escape mode and only while the host is not sending video.  Both are
     * restored on the way out.
     */
    bool bWasVideo = (0 == (ESP32_REG(DSI_HOST_MODE_CFG)
                            & ESP32_REG_MASK(DSI_HOST_MODE_CFG, CMD_VIDEO_MODE)));
    ESP32_REG(DSI_HOST_MODE_CFG) = ESP32_REG_MASK(DSI_HOST_MODE_CFG, CMD_VIDEO_MODE);
    Esp32p4DriverMipiDsi_SetCommandSpeed(false);

    ESP32_REG(DSI_HOST_PCKHDL_CFG) |= ESP32_REG_MASK(DSI_HOST_PCKHDL_CFG, BTA_EN);
    ESP32_REG(DSI_HOST_GEN_VCID) = pRequest->nVirtualChannel
                                   << ESP32_REG_SHIFT(DSI_HOST_GEN_VCID, RX);

    s32 nResult = -1;
    do {
        /* The panel has to be told how much it may send back before being asked
           for anything, or it answers with its own default length. */
        if (! Esp32p4DriverMipiDsi_WriteHeader(kEsp32_MipiDsiDataType_SetMaximumReturnPacket,
                                               pRequest->nVirtualChannel,
                                               (u8)(nReadBufferBytes & 0xff),
                                               (u8)(nReadBufferBytes >> 8))) break;

        const u8 *pPayload = pRequest->pPayload;
        u32 nBytes = pRequest->nPayloadBytes;
        Esp32_MipiDsiDataType dataType;

        if (kLTDriverMipiDsi_PacketType_DcsShort == pRequest->packetType
         || kLTDriverMipiDsi_PacketType_DcsLong  == pRequest->packetType) {
            /* DCS has one read type, and it carries exactly the command byte. */
            if (1 != nBytes) break;
            dataType = kEsp32_MipiDsiDataType_DcsRead0;
        } else {
            dataType = (0 == nBytes) ? kEsp32_MipiDsiDataType_GenericRead0
                     : (1 == nBytes) ? kEsp32_MipiDsiDataType_GenericRead1
                                     : kEsp32_MipiDsiDataType_GenericRead2;
        }

        if (! Esp32p4DriverMipiDsi_WriteHeader(dataType, pRequest->nVirtualChannel,
                                               nBytes ? pPayload[0] : 0,
                                               (2 == nBytes) ? pPayload[1] : 0)) break;

        bool bComplete = false;
        for (u32 i = 0; i < kFifoSpinLimit; i++) {
            if (! (ESP32_REG(DSI_HOST_CMD_PKT_STATUS)
                   & ESP32_REG_MASK(DSI_HOST_CMD_PKT_STATUS, GEN_RD_CMD_BUSY))) {
                bComplete = true;
                break;
            }
        }
        if (! bComplete) {
            LTLOG_YELLOWALERT("read.timeout", NULL);
            break;
        }

        /*
         * GEN_RD_CMD_BUSY clears when the request has gone out, not when the
         * answer has come back, so the payload FIFO has to be waited on
         * separately.  Draining on the first signal alone finds it empty and
         * reports a zero length read.
         */
        bool bAnswered = false;
        for (u32 i = 0; i < kFifoSpinLimit; i++) {
            if (! (ESP32_REG(DSI_HOST_CMD_PKT_STATUS)
                   & ESP32_REG_MASK(DSI_HOST_CMD_PKT_STATUS, GEN_PLD_R_EMPTY))) {
                bAnswered = true;
                break;
            }
        }
        if (! bAnswered) {
            LTLOG_YELLOWALERT("read.noresponse", NULL);
            break;
        }

        /*
         * Drain the whole response even where it overruns the buffer: words
         * left behind would come back as the head of the next read.
         */
        u32 nRead = 0;
        while (! (ESP32_REG(DSI_HOST_CMD_PKT_STATUS)
                  & ESP32_REG_MASK(DSI_HOST_CMD_PKT_STATUS, GEN_PLD_R_EMPTY))) {
            u32 nWord = ESP32_REG(DSI_HOST_GEN_PLD_DATA);
            for (u32 i = 0; i < 4; i++) {
                if (nRead < nReadBufferBytes) pReadBuffer[nRead++] = (u8)(nWord >> (8 * i));
            }
        }
        nResult = (s32)nRead;
    } while (false);

    ESP32_REG(DSI_HOST_PCKHDL_CFG) &= ~ESP32_REG_MASK(DSI_HOST_PCKHDL_CFG, BTA_EN);
    if (bWasVideo) ESP32_REG(DSI_HOST_MODE_CFG) = 0;

    return nResult;
}

static bool Esp32p4DriverMipiDsi_SetUltraLowPowerState(Esp32p4DriverMipiDsi *dsi, bool bEnter) {
    if (! dsi->bPoweredOn) return false;

    /*
     * Only the data lanes sleep.  The clock lane has its own ULPS request, but
     * using it stops the PLL - and the wakeup that would bring the lanes back
     * is itself clocked, so the exit request goes out with nothing to carry it
     * and the lanes stay asleep for good.  FORCEPLL does not hold the PLL up
     * through it either.  Leaving the clock lane running keeps the byte and
     * escape clocks alive across the sleep, which is what the data lanes need
     * to wake; dropping the clock lane to low power between frames is
     * AUTO_CLKLANE_CTRL's job, not this one's.
     *
     * The request line is held rather than pulsed: a lane stays in ultra low
     * power for as long as TXREQULPSLAN is asserted, so entry leaves it
     * standing and exit asserts TXEXITULPSLAN alongside it.  Both come down
     * together once the lanes are awake.
     */
    u32 nRequest = ESP32_REG_MASK(DSI_HOST_PHY_ULPS_CTRL, TXREQULPSLAN);
    if (! bEnter) nRequest |= ESP32_REG_MASK(DSI_HOST_PHY_ULPS_CTRL, TXEXITULPSLAN);
    ESP32_REG(DSI_HOST_PHY_ULPS_CTRL) = nRequest;

    /* Inverted: a lane is in ultra low power when its ULPSACTIVENOT bit reads zero. */
    u32 nActiveNotMask = 0, nStopMask = 0;
    if (dsi->config.nDataLanes >= 1) {
        nActiveNotMask |= ESP32_REG_MASK(DSI_HOST_PHY_STATUS, ULPSACTIVENOT0);
        nStopMask      |= ESP32_REG_MASK(DSI_HOST_PHY_STATUS, STOPSTATE0);
    }
    if (dsi->config.nDataLanes >= 2) {
        nActiveNotMask |= ESP32_REG_MASK(DSI_HOST_PHY_STATUS, ULPSACTIVENOT1);
        nStopMask      |= ESP32_REG_MASK(DSI_HOST_PHY_STATUS, STOPSTATE1);
    }

    /* Leaving ULPS takes the PHY at least a millisecond of wakeup signalling,
       which is long enough to sleep through rather than spin on. */
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    bool bReached = false;
    for (u32 i = 0; ! bReached && i < kUlpsSettleAttempts; i++) {
        u32 nStatus = ESP32_REG(DSI_HOST_PHY_STATUS) & nActiveNotMask;
        bReached = bEnter ? (0 == nStatus) : (nActiveNotMask == nStatus);
        if (! bReached) iThread->Sleep(LTTime_Milliseconds(kUlpsSettleMs));
    }

    if (! bReached) {
        LTLOG_YELLOWALERT("ulps.timeout", "enter=%lu status=0x%08lx", LT_Pu32(bEnter ? 1 : 0),
                          LT_Pu32(ESP32_REG(DSI_HOST_PHY_STATUS)));
        ESP32_REG(DSI_HOST_PHY_ULPS_CTRL) = 0;
        return false;
    }

    if (bEnter) return true;

    /*
     * ULPSACTIVENOT returns as soon as the wakeup starts, but the lane then
     * drives mark-1 for the rest of the wakeup time before it reaches stop
     * state.  Hold both bits across that, drop them together, and wait out the
     * return to stop, so the lanes are driveable by the time this returns.
     */
    iThread->Sleep(LTTime_Milliseconds(kUlpsWakeupMs));
    ESP32_REG(DSI_HOST_PHY_ULPS_CTRL) = 0;

    for (u32 i = 0; i < kUlpsSettleAttempts; i++) {
        if (nStopMask == (ESP32_REG(DSI_HOST_PHY_STATUS) & nStopMask)) return true;
        iThread->Sleep(LTTime_Milliseconds(kUlpsSettleMs));
    }

    LTLOG_YELLOWALERT("ulps.nostop", "status=0x%08lx", LT_Pu32(ESP32_REG(DSI_HOST_PHY_STATUS)));
    return false;
}

/*
 * The PHY reports stop state and ULPS per lane but has no bit for "this lane is
 * transmitting", so anything that is neither stopped nor in ULPS is reported as
 * transferring, at whatever speed the host is currently set to.
 */
static LTDeviceMipiDsi_LaneState
Esp32p4DriverMipiDsi_LaneStateFrom(Esp32p4DriverMipiDsi *dsi, u32 nStatus,
                                   u32 nStopMask, u32 nActiveNotMask) {
    if (! dsi->bPoweredOn) return kLTDeviceMipiDsi_LaneState_Off;
    if (! (nStatus & nActiveNotMask)) return kLTDeviceMipiDsi_LaneState_UltraLowPower;
    if (nStatus & nStopMask) return kLTDeviceMipiDsi_LaneState_Stop;

    bool bHighSpeed = dsi->bVideoActive
                   || (0 == (ESP32_REG(DSI_HOST_CMD_MODE_CFG)
                             & ESP32_REG_MASK(DSI_HOST_CMD_MODE_CFG, ALL_LP)));
    return bHighSpeed ? kLTDeviceMipiDsi_LaneState_HighSpeed
                      : kLTDeviceMipiDsi_LaneState_LowPower;
}

static LTDeviceMipiDsi_LaneState Esp32p4DriverMipiDsi_GetClockLaneState(Esp32p4DriverMipiDsi *dsi) {
    return Esp32p4DriverMipiDsi_LaneStateFrom(dsi, ESP32_REG(DSI_HOST_PHY_STATUS),
                                              ESP32_REG_MASK(DSI_HOST_PHY_STATUS, STOPSTATECLK),
                                              ESP32_REG_MASK(DSI_HOST_PHY_STATUS, ULPSACTIVENOTCLK));
}

static LTDeviceMipiDsi_LaneState Esp32p4DriverMipiDsi_GetDataLaneState(Esp32p4DriverMipiDsi *dsi,
                                                                      u32 nLane) {
    if (nLane >= dsi->config.nDataLanes) return kLTDeviceMipiDsi_LaneState_Off;

    u32 nStopMask      = (0 == nLane) ? ESP32_REG_MASK(DSI_HOST_PHY_STATUS, STOPSTATE0)
                                      : ESP32_REG_MASK(DSI_HOST_PHY_STATUS, STOPSTATE1);
    u32 nActiveNotMask = (0 == nLane) ? ESP32_REG_MASK(DSI_HOST_PHY_STATUS, ULPSACTIVENOT0)
                                      : ESP32_REG_MASK(DSI_HOST_PHY_STATUS, ULPSACTIVENOT1);

    return Esp32p4DriverMipiDsi_LaneStateFrom(dsi, ESP32_REG(DSI_HOST_PHY_STATUS),
                                              nStopMask, nActiveNotMask);
}

static void Esp32p4DriverMipiDsi_SetErrorProc(Esp32p4DriverMipiDsi *dsi,
                                              LTDriverMipiDsi_ErrorProc *pErrorProc,
                                              void *pErrorClientData) {
    /* The pair has to change together or the ISR could call a new procedure
       with the old client data. */
    LT_SIZE nMask = LT_GetCore()->Disable();
    dsi->pErrorProc       = pErrorProc;
    dsi->pErrorClientData = pErrorClientData;
    LT_GetCore()->Enable(nMask);
}

/*______________________________________
  Esp32p4DriverMipiDsi register dump  */

typedef struct Esp32p4DriverMipiDsi_RegisterName {
    const char *pName;
    u32         nAddress;
} Esp32p4DriverMipiDsi_RegisterName;

/* The clock tree feeding the link.  Always readable: HP_CLKRST and LP_CLKRST
   are never gated. */
static const Esp32p4DriverMipiDsi_RegisterName s_clockRegisters[] = {
    { "LP_CLKRST_HP_CLK_CTRL",  kEsp32_RegisterLP_CLKRST_HP_CLK_CTRL  },
    { "HP_CLKRST_ROOT_CLK_CTRL0", kEsp32_RegisterHP_CLKRST_ROOT_CLK_CTRL0 },
    { "HP_CLKRST_ROOT_CLK_CTRL1", kEsp32_RegisterHP_CLKRST_ROOT_CLK_CTRL1 },
    { "HP_CLKRST_ROOT_CLK_CTRL2", kEsp32_RegisterHP_CLKRST_ROOT_CLK_CTRL2 },
    { "HP_CLKRST_REF_CLK_CTRL1",  kEsp32_RegisterHP_CLKRST_REF_CLK_CTRL1  },
    { "HP_CLKRST_REF_CLK_CTRL2",  kEsp32_RegisterHP_CLKRST_REF_CLK_CTRL2  },
    { "HP_CLKRST_SOC_CLK_CTRL0",  kEsp32_RegisterHP_CLKRST_SOC_CLK_CTRL0  },
    { "HP_CLKRST_SOC_CLK_CTRL1",  kEsp32_RegisterHP_CLKRST_SOC_CLK_CTRL1  },
    { "HP_CLKRST_PERI_CLK_CTRL00", kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL00 },
    { "HP_CLKRST_PERI_CLK_CTRL02", kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL02 },
    { "HP_CLKRST_PERI_CLK_CTRL03", kEsp32_RegisterHP_CLKRST_PERI_CLK_CTRL03 },
    { "HP_CLKRST_ANA_PLL_CTRL0",   kEsp32_RegisterHP_CLKRST_ANA_PLL_CTRL0   },
    { "HP_CLKRST_HP_RST_EN0",      kEsp32_RegisterHP_CLKRST_HP_RST_EN0      },
    { "HP_CLKRST_CPU_SRC_FREQ0",   kEsp32_RegisterHP_CLKRST_CPU_SRC_FREQ0   },
    { "PMU_EXT_LDO_CHAN3",         kEsp32_RegisterPMU_EXT_LDO_CHAN3         },
    { "PMU_EXT_LDO_CHAN3_ANA",     kEsp32_RegisterPMU_EXT_LDO_CHAN3_ANA     },
};

/* The host controller and the bridge.  Only readable once the DSI module clock
   is ungated, so the dump skips them until it is. */
static const Esp32p4DriverMipiDsi_RegisterName s_dsiRegisters[] = {
    { "DSI_HOST_PWR_UP",            kEsp32_RegisterDSI_HOST_PWR_UP            },
    { "DSI_HOST_CLKMGR_CFG",        kEsp32_RegisterDSI_HOST_CLKMGR_CFG        },
    { "DSI_HOST_MODE_CFG",          kEsp32_RegisterDSI_HOST_MODE_CFG          },
    { "DSI_HOST_LPCLK_CTRL",        kEsp32_RegisterDSI_HOST_LPCLK_CTRL        },
    { "DSI_HOST_PHY_TMR_CFG",       kEsp32_RegisterDSI_HOST_PHY_TMR_CFG       },
    { "DSI_HOST_PHY_TMR_LPCLK_CFG", kEsp32_RegisterDSI_HOST_PHY_TMR_LPCLK_CFG },
    { "DSI_HOST_PHY_RSTZ",          kEsp32_RegisterDSI_HOST_PHY_RSTZ          },
    { "DSI_HOST_PHY_IF_CFG",        kEsp32_RegisterDSI_HOST_PHY_IF_CFG        },
    { "DSI_HOST_PHY_STATUS",        kEsp32_RegisterDSI_HOST_PHY_STATUS        },
    { "DSI_HOST_INT_ST0",           kEsp32_RegisterDSI_HOST_INT_ST0           },
    { "DSI_HOST_INT_ST1",           kEsp32_RegisterDSI_HOST_INT_ST1           },
    { "DSI_BRG_CLK_EN",             kEsp32_RegisterDSI_BRG_CLK_EN             },
    { "DSI_BRG_EN",                 kEsp32_RegisterDSI_BRG_EN                 },
    { "DSI_BRG_FIFO_FLOW_STATUS",   kEsp32_RegisterDSI_BRG_FIFO_FLOW_STATUS   },
    { "DSI_BRG_HOST_CTRL",          kEsp32_RegisterDSI_BRG_HOST_CTRL          },
    { "DSI_BRG_INT_ST",             kEsp32_RegisterDSI_BRG_INT_ST             },
};

#define kClockRegisterCount     (sizeof(s_clockRegisters) / sizeof(s_clockRegisters[0]))
#define kDsiRegisterCount       (sizeof(s_dsiRegisters) / sizeof(s_dsiRegisters[0]))

static void Esp32p4DriverMipiDsi_DumpRegisterTable(const Esp32p4DriverMipiDsi_RegisterName *pTable,
                                                   u32 nEntries,
                                                   LTDeviceMipiDsi_RegisterProc *pRegisterProc,
                                                   void *pClientData) {
    for (u32 nIndex = 0; nIndex < nEntries; ++nIndex) {
        u32 nValue = *(volatile u32 *)pTable[nIndex].nAddress;
        pRegisterProc(pTable[nIndex].pName, pTable[nIndex].nAddress, nValue, pClientData);
    }
}

static void Esp32p4DriverMipiDsi_DumpRegisters(Esp32p4DriverMipiDsi *dsi,
                                               LTDeviceMipiDsi_RegisterProc *pRegisterProc,
                                               void *pClientData) {
    LT_UNUSED(dsi);
    Esp32p4DriverMipiDsi_DumpRegisterTable(s_clockRegisters, kClockRegisterCount,
                                           pRegisterProc, pClientData);

    if (ESP32_REG(HP_CLKRST_SOC_CLK_CTRL1) & ESP32_REG_MASK(HP_CLKRST_SOC_CLK_CTRL1, DSI_SYS_CLK_EN)) {
        Esp32p4DriverMipiDsi_DumpRegisterTable(s_dsiRegisters, kDsiRegisterCount,
                                               pRegisterProc, pClientData);
    }
}

/*__________________________________________
  Esp32p4DriverMipiDsi constructors       */

static bool Esp32p4DriverMipiDsi_ConstructObject(Esp32p4DriverMipiDsi *dsi) {
    /* One DSI host, and an ISR that reaches its instance through a static - so
       a second object would have nowhere to be called back from. */
    if (s_driverMipiDsi) return false;
    s_driverMipiDsi = dsi;
    return true;
}

static void Esp32p4DriverMipiDsi_DestructObject(Esp32p4DriverMipiDsi *dsi) {
    Esp32p4DriverMipiDsi_PowerOff(dsi);
    if (s_driverMipiDsi == dsi) s_driverMipiDsi = NULL;
}

/*________________________________
  LTDriverMipiDsi api binding   */
define_LTObjectImplPublic(LTDriverMipiDsi, Esp32p4DriverMipiDsi,
    Configure,
    PowerOn,
    PowerOff,
    SetFrameBuffer,
    StartVideo,
    StopVideo,
    WriteFrame,
    SendPacket,
    ReceivePacket,
    SetUltraLowPowerState,
    GetClockLaneState,
    GetDataLaneState,
    SetErrorProc,
    DumpRegisters);

/*____________________
  LTLibrary binding */
define_LTObjectLibrary(1, NULL, NULL);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  30-Sep-26   dwoodward   created
 *  01-Oct-26   dwoodward   read the wafer revision and pick the D-PHY PLL
 *                          reference to match; best-delta PLL divider search
 *  01-Oct-26   dwoodward   bring up the VO3 LDO, the D-PHY's analog supply,
 *                          before touching the PHY
 *  01-Oct-26   dwoodward   wait for the read response rather than the request;
 *                          floor the escape and timeout clock dividers
 *  01-Oct-26   dwoodward   hold the ULPS request for the duration of ULPS, and
 *                          wait out the wakeup back to stop state on exit
 *  01-Oct-26   dwoodward   sleep only the data lanes in ULPS; a clock lane in
 *                          ULPS stops the PLL the wakeup needs to run
 *  01-Oct-26   dwoodward   write the framebuffer back out of the caches before
 *                          the DMA reads it, or the panel scans out black
 */
