/******************************************************************************
 * platforms/esp32/source/esp32/driver/video/Esp32DriverVideoDVP.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * ESP32 DVP (Digital Video Port) driver implementing the LTDeviceVideo /
 * ILTVideo interface.
 *
 * Architecture
 * ────────────
 * This is the master video driver for the ESP32-CAM family.  It sits at the
 * top of the stack and owns the full frame lifecycle:
 *
 *   LTDeviceVideo  →  Esp32DriverVideoDVP  →  LTDeviceImageSensor
 *                                          →  I²S0 / DMA hardware
 *
 * The driver opens LTDeviceImageSensor in LibInit to configure the sensor
 * (pixel format, frame size, power).  Frame capture is handled entirely here:
 * the I²S0 peripheral runs in 8-bit DVP camera mode, writing frames to a
 * two-buffer DMA ring.  When a frame completes the ISR fires and — from the
 * ISR context — raises an LTEvent that dispatches to all registered
 * OnVideoEvent callbacks on the subscriber's own thread.
 *
 * Frame delivery model
 * ────────────────────
 * LTDeviceVideo_VideoData is allocated per-frame (malloc'd in the ISR shim),
 * holds a pointer into the DMA buffer, and is reference-counted.  The client
 * MUST call ReleaseVideoData() after processing.
 *
 * When a slot is claimed (refCount raised to 1) it is removed from the DMA
 * descriptor ring so DMA cannot overwrite the client-held buffer.  If both
 * slots are claimed simultaneously, DMA stalls on a nullDesc sentinel and the
 * VSYNC GPIO ISR is re-armed (vsyncArmed=2).  When the client calls
 * ReleaseVideoData the slot is re-armed in place and marked pendingRearm; the
 * VSYNC ISR restarts DMA at the next frame boundary so the first byte captured
 * into the newly-free buffer is always the SOI of a fresh frame.
 *
 * Supported channels
 * ──────────────────
 *   kLTDeviceVideo_Channel_ImageHD — JPEG / RGB565 / YUV422 still frames
 *   kLTDeviceVideo_Channel_H264HD  — not supported (returns false)
 *   All SD / ISP / MdData channels  — not supported (returns false)
 *
 * DVP signal routing (AI Thinker ESP32-CAM defaults; override via config)
 * ────────────────────────────────────────────────────────────────────────
 *   PCLK=22  VSYNC=25  HREF=23
 *   D0=5  D1=18  D2=19  D3=21  D4=36  D5=39  D6=34  D7=35
 *****************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/video/LTDeviceVideo.h>
#include <lt/device/imagesensor/LTDeviceImageSensor.h>
#include <lt/device/config/LTDeviceConfig.h>

#include "esp32/Esp32_Registers.h"
#include "esp32/Esp32_Clock.h"
#include "esp32/Esp32_GPIO.h"
#include "esp32/Esp32_Irq.h"


DEFINE_LTLOG_SECTION("esp32.drv.video.dvp");

/* =========================================================================
 * I²S0 register file  (base 0x3FF4F000, ESP32 TRM §12)
 * ========================================================================= */

#define I2S0_BASE           0x3FF4F000UL
#define I2S0(off)           (*(volatile u32 *)(I2S0_BASE + (off)))

/* I2S0 register offsets (i2s_struct.h field layout starting at base+0x08)
 * reserved_0=+0x00, reserved_4=+0x04, conf=+0x08, int_raw=+0x0C,
 * int_st=+0x10, int_ena=+0x14, int_clr=+0x18, timing=+0x1C,
 * fifo_conf=+0x20, rx_eof_num=+0x24, conf_single_data=+0x28,
 * conf_chan=+0x2C, out_link=+0x30, in_link=+0x34,
 * out_eof_des_addr=+0x38, in_eof_des_addr=+0x3C, out_eof_bfr_des_addr=+0x40,
 * ahb_test=+0x44, in_link_dscr=+0x48..0x50, out_link_dscr=+0x54..0x5C,
 * lc_conf=+0x60, ...
 * conf2=+0xA8, clkm_conf=+0xAC, sample_rate_conf=+0xB0 */
#define I2S_CONF_OFF         0x08
#define I2S_INT_RAW_OFF      0x0C
#define I2S_INT_ENA_OFF      0x14
#define I2S_INT_CLR_OFF      0x18
#define I2S_TIMING_OFF       0x1C
#define I2S_FIFO_CONF_OFF    0x20
#define I2S_RX_EOF_NUM_OFF   0x24
#define I2S_CONF_CHAN_OFF    0x2C
#define I2S_IN_LINK_OFF      0x34
#define I2S_IN_EOF_ADDR_OFF  0x3C
#define I2S_LC_CONF_OFF      0x60
#define I2S_CONF2_OFF        0xA8
#define I2S_CLKM_CONF_OFF    0xAC
#define I2S_SAMPLE_RATE_OFF  0xB0

/* CONF bits */
#define I2S_RX_RESET_M      (1u <<  1)
#define I2S_RX_FIFO_RESET_M (1u <<  3)
#define I2S_RX_START_M      (1u <<  5)
#define I2S_RX_SLAVE_MOD_M  (1u <<  7)
/* CONF2 bits */
#define I2S_LCD_EN_M        (1u <<  5)
#define I2S_CAMERA_EN_M     (1u <<  0)
/* FIFO_CONF bits: dscr_en[12], rx_fifo_mod[18:16], rx_fifo_mod_force_en[20] */
#define I2S_DSCR_EN_M                (1u << 12)
#define I2S_RX_FIFO_MOD_FORCE_EN_M  (1u << 20)
/* Sampling mode 3 = SM_0A00_0B00 (one byte per 32-bit DMA word) */
#define I2S_RX_FIFO_MOD_VAL         (3u << 16)
/* LC_CONF bits */
#define I2S_LC_IN_RST_M         (1u <<  0)
#define I2S_LC_AHBM_FIFO_RST_M (1u <<  2)
#define I2S_LC_AHBM_RST_M       (1u <<  3)
/* TIMING bits */
#define I2S_RX_DSYNC_SW_M   (1u << 21)
/* IN_LINK bits */
#define I2S_INLINK_ADDR_M   0x000FFFFFu
#define I2S_INLINK_STOP_M   (1u << 28)
#define I2S_INLINK_START_M  (1u << 29)
/* INT_RAW / INT_ENA bit */
#define I2S_IN_SUC_EOF_M    (1u <<  9)

/* GPIO matrix signal numbers for I²S0 RX camera mode (from soc/gpio_sig_map.h) */
#define SIG_I2S0I_DATA_IN0  140  /* I2S0I_DATA_IN0_IDX — D0–D7 → 140–147 */
#define SIG_I2S0I_WS_IN      28  /* I2S0I_WS_IN_IDX    — PCLK → WS in camera mode */
#define SIG_I2S0I_V_SYNC    191  /* I2S0I_V_SYNC_IDX   — VSYNC */
#define SIG_I2S0I_H_SYNC    190  /* I2S0I_H_SYNC_IDX   — HREF */
#define SIG_I2S0I_H_ENABLE  192  /* I2S0I_H_ENABLE_IDX — HREF (data enable) */

#define GPIO_FUNC_IN_SEL(s) (*(volatile u32 *)(0x3FF44130u + (u32)(s) * 4u))
#define GPIO_IN_USE_MATRIX  (1u << 7)

/* I²S0 external IRQ — ETS_I2S0_INTR_SOURCE=32 (from soc/soc.h) */
enum {
    kEsp32_ExternalIrq_I2S0   = 32,
    kEsp32_IrqNumber_I2S0     = 13,   /* level-1 Xtensa slot, unused by existing BSP */
    kEsp32_IrqPriority_I2S0   = 1,
    kEsp32_CPU0                = 0,
};


/* =========================================================================
 * DMA descriptor
 * ========================================================================= */

typedef struct DmaDesc {
    volatile u32             ctrl;
    volatile u8             *buf;
    volatile struct DmaDesc *next;
} DmaDesc;

/* A descriptor with OWNER_M=0 causes the DMA engine to halt when it tries to
 * load it.  Every slot's last descriptor points here when its slot is not in
 * rotation, so a ring of N free slots ends cleanly instead of wrapping. */
#define DMA_NULLDESC_INITIALIZER { 0u, NULL, NULL }

#define DMA_SIZE_M   0xFFFu
#define DMA_LEN_S    12
#define DMA_OWNER_M  (1u << 31)

/* =========================================================================
 * Pin configuration
 * ========================================================================= */

typedef struct { u8 pclk, vsync, href, d[8]; } DvpPins;

static const DvpPins kDefaultPins = {
    .pclk = 22, .vsync = 25, .href = 23,
    .d    = { 5, 18, 19, 21, 36, 39, 34, 35 },
};

/* =========================================================================
 * VideoData reference-counted wrapper
 *
 * Allocated once per frame in the ISR shim.  The DMA buffer it points into
 * is not freed — it is returned to the ring when the refcount hits zero.
 * ========================================================================= */

typedef struct {
    LTDeviceVideo_VideoData  pub;        /* must be first — clients cast to this */
    LTAtomic                 refCount;
    int                      bufIdx;     /* which ring buffer (0 or 1) */
} VideoDataWrapper;

/* =========================================================================
 * Driver state
 * ========================================================================= */

enum {
    kNumDmaDesc      = 2,      /* number of frame slots (double buffer) */
    kMaxDescBytes    = 4092,   /* max bytes per DMA descriptor (≤4095, 4-byte aligned) */
    /* JPEG DMA budget per slot.  In SM_0A00_0B00 mode 4 DMA bytes = 1 camera byte,
     * so kJpegDmaMaxBytes/4 is the camera-byte capacity per slot.
     *
     * With VSYNC synchronisation DMA always starts at the beginning of a frame,
     * so each slot needs to hold exactly one compressed JPEG.  Empirically:
     * ~6 KB for QVGA (320×240), ~3 KB for QQVGA (160×120).  32 KB (8 KB camera
     * bytes) provides comfortable headroom for any supported frame size.
     * 2 slots × 32 KB = 64 KB total. */
    kJpegDmaMaxBytes = (32768-4096)
};

typedef struct {
    /* --- sensor --- */
    LTDeviceImageSensor     *libSensor;
    LTDeviceUnit             hSensorUnit;
    ILTDriverImageSensor    *iSensor;

    /* --- channel state --- */
    bool                     enabled;                  /* Enable() called */
    bool                     streaming[kLTDeviceVideo_Channel_Count]; /* Start() called */

    /* --- LTEvent for frame delivery --- */
    LTEvent                  hFrameEvent;
    ILTEvent                *iEvent;

    /* --- DMA ring --- */
    /* Each frame slot has nDescsPerSlot chained descriptors (because each descriptor
     * can hold at most kMaxDescBytes).  Free slots are chained together; the last
     * free slot's tail points to nullDesc so DMA halts cleanly when no free slot
     * follows. */
    DmaDesc                 *descs[kNumDmaDesc];  /* malloc'd arrays, nDescsPerSlot each */
    u32                      nDescsPerSlot;
    u8                      *buf[kNumDmaDesc];
    u32                      bufSize;    /* total DMA bytes per slot (4 × raw camera bytes) */
    DmaDesc                  nullDesc;  /* OWNER=0 sentinel; DMA stalls here when ring is empty */
    bool                     dmaRunning;
    bool                     dmaStalled; /* true when all slots are held and DMA has stopped */
    LTAtomic                 pendingRearm[kNumDmaDesc]; /* 1 = slot released, needs re-arm on next VSYNC */

    /* --- per-slot frame wrappers (allocated with DMA buffers, not in ISR) --- */
    VideoDataWrapper         wrappers[kNumDmaDesc];
    u32                      sequence;

    /* --- misc --- */
    DvpPins                  pins;
    /* vsyncArmed states:
     *   0 = GPIO ISR not active
     *   1 = waiting for first VSYNC after DMA start (initial alignment)
     *   2 = waiting for VSYNC to restart stalled DMA at frame boundary */
    LTAtomic                 vsyncArmed;
} DvpDriver;

static DvpDriver *s_drv;

/* forward declarations */
static void DvpImpl_ReleaseVideoData(LTDeviceVideo_Channel, LTDeviceVideo_VideoData *);

/* =========================================================================
 * Event args: {pointer → VideoDataWrapper, u32 → channel, u32 → event}
 * ========================================================================= */

static const LTArgsDescriptor s_frameEventArgs = {
    .nNumArgs = 3,
    .argTypes = { kLTArgType_pointer, kLTArgType_u32, kLTArgType_u32 }
};

static void DispatchFrameEvent(LTEvent hEvent, void *eventProc,
                               LTArgs *args, void *eventProcClientData) {
    LT_UNUSED(hEvent);
    LTDeviceVideo_EventProc *cb = eventProc;
    VideoDataWrapper *wd = LTArgs_pointerAt(0, args);
    LTDeviceVideo_Channel ch = (LTDeviceVideo_Channel)LTArgs_u32At(1, args);
    LTDeviceVideo_Event   ev = (LTDeviceVideo_Event)LTArgs_u32At(2, args);
    /* wd is NULL for drop/fail events — pass NULL videoData to callback */
    cb(ch, ev, wd ? &wd->pub : NULL, eventProcClientData);
}

/* =========================================================================
 * Helper: route a GPIO to an I²S0 input signal via the GPIO matrix
 * ========================================================================= */

static void RouteInput(u8 gpio, u8 signal) {
    GPIO_FUNC_IN_SEL(signal) = gpio | GPIO_IN_USE_MATRIX;
    Esp32GPIO_ConfigPin(gpio, kEsp32GPIO_Direction_Input,
                        kEsp32GPIO_PullNone, kEsp32GPIO_Function_GPIO);
}

/* =========================================================================
 * DMA buffer management
 * ========================================================================= */

static bool AllocDma(u32 sz) {
    /* sz is total DMA bytes per slot (4 bytes per camera pixel byte in SM_0A00_0B00).
     * We need multiple descriptors per slot since each descriptor holds ≤ kMaxDescBytes. */
    u32 nDescs = (sz + kMaxDescBytes - 1) / kMaxDescBytes;
    if (!nDescs) nDescs = 1;

    /* Sentinel: OWNER=0 causes the DMA engine to halt when it fetches this desc. */
    DmaDesc nd = DMA_NULLDESC_INITIALIZER;
    s_drv->nullDesc = nd;

    for (int i = 0; i < kNumDmaDesc; ++i) {
        lt_free(s_drv->buf[i]);
        lt_free(s_drv->descs[i]);
        s_drv->buf[i]   = NULL;
        s_drv->descs[i] = NULL;

        s_drv->buf[i] = lt_malloc(sz);
        if (!s_drv->buf[i]) {
            LTLOG("alloc.dma", "failed to alloc %lu bytes", LT_Pu32(sz));
            return false;
        }
        s_drv->descs[i] = lt_malloc(nDescs * sizeof(DmaDesc));
        if (!s_drv->descs[i]) {
            LTLOG("alloc.dma.desc", "failed to alloc %lu descs", LT_Pu32(nDescs));
            return false;
        }
        /* Pre-populate wrapper fields that never change between frames */
        s_drv->wrappers[i].pub.address = s_drv->buf[i];
        s_drv->wrappers[i].pub.type    = kLTDeviceVideo_Frame_Jpeg;
        s_drv->wrappers[i].bufIdx      = i;
        LTAtomic_Store(&s_drv->wrappers[i].refCount, 0);
        LTAtomic_Store(&s_drv->pendingRearm[i], 0);
    }
    s_drv->bufSize       = sz;
    s_drv->nDescsPerSlot = nDescs;
    s_drv->dmaStalled    = false;
    return true;
}

static void FreeDma(void) {
    for (int i = 0; i < kNumDmaDesc; ++i) {
        lt_free(s_drv->buf[i]);
        lt_free(s_drv->descs[i]);
        s_drv->buf[i]   = NULL;
        s_drv->descs[i] = NULL;
        lt_memset(&s_drv->wrappers[i], 0, sizeof(VideoDataWrapper));
        LTAtomic_Store(&s_drv->pendingRearm[i], 0);
    }
    s_drv->bufSize       = 0;
    s_drv->nDescsPerSlot = 0;
    s_drv->dmaStalled    = false;
}

/* Arm all descriptors for slot idx in place: fill ctrl/buf fields and set the
 * tail's next to nullDesc.  Does NOT stitch this slot into the ring — call
 * RebuildRingLinks() for that.  Safe to call from any context (no DMA writes
 * to these fields while DMA is stalled or before the slot is linked in). */
static void ArmDescInPlace(int idx) {
    u32 remaining = s_drv->bufSize;
    u8 *base = s_drv->buf[idx];
    u32 n = s_drv->nDescsPerSlot;
    s_drv->descs[idx][n-1].next = &s_drv->nullDesc;
    for (u32 d = 0; d < n; ++d) {
        u32 chunk = remaining < kMaxDescBytes ? remaining : kMaxDescBytes;
        s_drv->descs[idx][d].ctrl = (chunk & DMA_SIZE_M) | ((chunk & DMA_SIZE_M) << DMA_LEN_S) | DMA_OWNER_M;
        s_drv->descs[idx][d].buf  = base;
        s_drv->descs[idx][d].next = (d + 1 < n)
            ? &s_drv->descs[idx][d + 1]
            : &s_drv->nullDesc;   /* tail → nullDesc until ring is linked */
        base      += chunk;
        remaining -= chunk;
    }
}

/* Chain all ready-to-fill (refCount==0 AND pendingRearm==0) slots into a list;
 * the last slot's tail points to nullDesc so DMA halts cleanly at the end.
 *
 * A slot with pendingRearm==1 is excluded even though refCount==0 — it has
 * been released by the client but is waiting for a VSYNC boundary before DMA
 * is allowed to write into it.
 *
 * Returns the first descriptor in the list, or NULL if no slot is ready.
 * Must be called from ISR context (or with DMA stopped). */
static DmaDesc * LT_ISR_SAFE RebuildRingLinks(void) {
    int free[kNumDmaDesc];
    int nFree = 0;
    for (int i = 0; i < kNumDmaDesc; ++i) {
        if (LTAtomic_Load(&s_drv->wrappers[i].refCount) == 0 &&
            LTAtomic_Load(&s_drv->pendingRearm[i]) == 0)
            free[nFree++] = i;
    }
    if (nFree == 0) return NULL;

    u32 n = s_drv->nDescsPerSlot;
    for (int f = 0; f < nFree - 1; ++f) {
        s_drv->descs[free[f]][n - 1].next = &s_drv->descs[free[f + 1]][0];
    }
    s_drv->descs[free[nFree - 1]][n - 1].next = &s_drv->nullDesc;

    return &s_drv->descs[free[0]][0];
}

static void BuildRing(void) {
    for (int i = 0; i < kNumDmaDesc; ++i) ArmDescInPlace(i);
    RebuildRingLinks();
}

/* =========================================================================
 * Frame byte size: derive from sensor's current config
 * ========================================================================= */

static u32 CalcFrameBytes(void) {

    LTImageSensorFrameResolution res = {0, 0};
    if (!s_drv->iSensor->GetAttribute(s_drv->hSensorUnit,
                                       kLTImageSensorAttribute_FrameResolution, &res)) return 0;
    if (!res.width || !res.height) return 0;
    LTImageSensorPixelFormat fmt = kLTImageSensorPixelFormat_JPEG;
    s_drv->iSensor->GetAttribute(s_drv->hSensorUnit,
                                  kLTImageSensorAttribute_PixelFormat, &fmt);
    u32 px = (u32)res.width * (u32)res.height;
    //LTLOG("frame.alloc", "%lu x %lu, %lu pixels, %lu bytes", LT_Pu32(res.width), LT_Pu32(res.height), LT_Pu32(px), LT_Pu32(px/4));

    /* In SM_0A00_0B00 mode the I²S0 DMA stores one camera byte per 32-bit word.
     * For JPEG the sensor emits a compressed stream of variable length; cap the
     * buffer at kJpegDmaMaxBytes so memory use is independent of resolution.
     * For raw formats each pixel produces a fixed number of camera bytes. */
    switch (fmt) {
        case kLTImageSensorPixelFormat_JPEG:
            return kJpegDmaMaxBytes;
        case kLTImageSensorPixelFormat_Grayscale:
            return px * 4u;
        default: /* RGB565, YUV422: 2 bytes per pixel */
            return px * 8u;
    }
}

/* =========================================================================
 * I²S0 hardware
 * ========================================================================= */

static void I2s0ClockEnable(bool en) {
    volatile u32 *clk = (volatile u32 *)kEsp32_RegisterDPORT_PERIP_CLK_EN;
    volatile u32 *rst = (volatile u32 *)kEsp32_RegisterDPORT_PERIP_RST_EN;
    if (en) { *clk |=  kEsp32_Clock_I2S0; *rst &= ~kEsp32_Clock_I2S0; }
    else    { *clk &= ~kEsp32_Clock_I2S0; *rst |=  kEsp32_Clock_I2S0; }
}

static void I2s0Reset(void) {
    /* Reset RX path in CONF */
    I2S0(I2S_CONF_OFF) |=  I2S_RX_RESET_M | I2S_RX_FIFO_RESET_M;
    I2S0(I2S_CONF_OFF) &= ~(I2S_RX_RESET_M | I2S_RX_FIFO_RESET_M);
    /* Reset DMA engine via LC_CONF */
    I2S0(I2S_LC_CONF_OFF) |=  I2S_LC_IN_RST_M | I2S_LC_AHBM_FIFO_RST_M | I2S_LC_AHBM_RST_M;
    I2S0(I2S_LC_CONF_OFF) &= ~(I2S_LC_IN_RST_M | I2S_LC_AHBM_FIFO_RST_M | I2S_LC_AHBM_RST_M);
}

static void I2s0Configure(void) {
    /* Match Espressif esp32-camera ll_cam_config exactly */
    I2S0(I2S_CONF_OFF) = I2S_RX_SLAVE_MOD_M;   /* slave, no msb_right, no mono */

    I2S0(I2S_CONF2_OFF) = I2S_LCD_EN_M | I2S_CAMERA_EN_M;

    /* Clock: CLK_EN=BIT(20), CLKM_DIV_NUM=2 (bits[7:0]) */
    I2S0(I2S_CLKM_CONF_OFF) = (1u << 20) | 2u;

    /* FIFO: dscr_en, rx_fifo_mod=SM_0A00_0B00(3), rx_fifo_mod_force_en */
    I2S0(I2S_FIFO_CONF_OFF) = I2S_DSCR_EN_M | I2S_RX_FIFO_MOD_VAL | I2S_RX_FIFO_MOD_FORCE_EN_M;

    /* Channel mode: rx_chan_mod=1 (bits[4:3]) */
    I2S0(I2S_CONF_CHAN_OFF) = (1u << 3);

    /* Sample rate: rx_bits_mod=0 (default 16; 0 means use fifo_mod) */
    I2S0(I2S_SAMPLE_RATE_OFF) = 0u;

    /* Timing: rx_dsync_sw=1 (bit 21) */
    I2S0(I2S_TIMING_OFF) = I2S_RX_DSYNC_SW_M;

    /* GPIO matrix: data pins D0-D7, PCLK→WS, VSYNC, HREF */
    for (int i = 0; i < 8; ++i)
        RouteInput(s_drv->pins.d[i], (u8)(SIG_I2S0I_DATA_IN0 + i));
    RouteInput(s_drv->pins.pclk,  SIG_I2S0I_WS_IN);
    RouteInput(s_drv->pins.vsync, SIG_I2S0I_V_SYNC);
    RouteInput(s_drv->pins.href,  SIG_I2S0I_H_SYNC);
    /* H_ENABLE: map to internal signal 0x38 (high = always enabled) per Espressif */
    GPIO_FUNC_IN_SEL(SIG_I2S0I_H_ENABLE) = 0x38u | GPIO_IN_USE_MATRIX;

}

static void I2s0StartDma(void) {
    I2S0(I2S_CONF_OFF) &= ~I2S_RX_START_M;

    I2S0(I2S_INT_CLR_OFF) = 0xFFFFFFFFu;
    I2S0(I2S_INT_ENA_OFF) = I2S_IN_SUC_EOF_M;

    I2S0(I2S_CONF_OFF) |=  I2S_RX_RESET_M | I2S_RX_FIFO_RESET_M;
    I2S0(I2S_CONF_OFF) &= ~(I2S_RX_RESET_M | I2S_RX_FIFO_RESET_M);
    I2S0(I2S_LC_CONF_OFF) |=  I2S_LC_IN_RST_M | I2S_LC_AHBM_FIFO_RST_M | I2S_LC_AHBM_RST_M;
    I2S0(I2S_LC_CONF_OFF) &= ~(I2S_LC_IN_RST_M | I2S_LC_AHBM_FIFO_RST_M | I2S_LC_AHBM_RST_M);

    I2S0(I2S_RX_EOF_NUM_OFF) = s_drv->bufSize / 4u;

    u32 addr = (u32)&s_drv->descs[0][0];
    I2S0(I2S_IN_LINK_OFF) = (addr & I2S_INLINK_ADDR_M) | I2S_INLINK_START_M;

    I2S0(I2S_CONF_OFF) |= I2S_RX_START_M;
}

static void I2s0StopDma(void) {
    I2S0(I2S_CONF_OFF) &= ~I2S_RX_START_M;
    I2S0(I2S_INT_ENA_OFF)  = 0;
    I2S0(I2S_IN_LINK_OFF) |= I2S_INLINK_STOP_M;
    I2S0(I2S_INT_CLR_OFF)  = 0xFFFFFFFFu;
}

/* =========================================================================
 * ISR notify path
 *
 * The ISR fills the wrapper and calls NotifyEventFromISR, which queues
 * FrameNotifyProc onto a system proxy thread.  FrameNotifyProc then calls
 * NotifyEvent — safely on a thread, not in interrupt context.
 *
 * FrameDropNotifyProc is the same pattern for the drop case.
 * ========================================================================= */

static void LT_ISR_SAFE FrameNotifyProc(void *pClientData) {
    VideoDataWrapper *wd = (VideoDataWrapper *)pClientData;
    //static int badFrames = 0;
    /* Compact: in I²S SM_0A00_0B00 camera mode the data byte from the sensor
     * lands at byte offset 2 of each 32-bit DMA word (bits[23:16] in little-endian
     * memory — confirmed by analysis of captured DMA buffers).
     * Reading from w*4+2 is always ahead of the write pointer at w, so safe. */
    u32 totalWords = wd->pub.length;   /* set by VSYNC ISR to words captured */
    u8 *raw = wd->pub.address;
    for (u32 w = 0; w < totalWords; ++w) {
        raw[w] = raw[w * 4u + 2u];
    }

    /* DMA is VSYNC-aligned: the frame always starts at raw[0] with SOI.
     * Just find the EOI to determine the actual JPEG length. */
    u32 jpegLen = 0;
    for (u32 i = 0; i + 1 < totalWords; ++i) {
        if (raw[i] == 0xFF && raw[i + 1] == 0xD9) { jpegLen = i + 2; break; }
    }

    if (jpegLen > 0 && raw[0] == 0xFF && raw[1] == 0xD8) {
        wd->pub.length = jpegLen;
        s_drv->iEvent->NotifyEvent(s_drv->hFrameEvent, &wd->pub, kLTDeviceVideo_Channel_ImageHD, kLTDeviceVideo_Event_FrameReady);
    } else {
        /* Incomplete or non-JPEG frame — release via the normal path so the
         * VSYNC-aligned re-arm logic runs correctly. */
        //LTLOG("frame.notify.bogus.jpg", "bad jpg frame %p, index %d, total %d", (void *)&wd->pub, wd->bufIdx, ++badFrames);
        DvpImpl_ReleaseVideoData(kLTDeviceVideo_Channel_ImageHD, &wd->pub);
    }
}
#if 0
static void LT_ISR_SAFE FrameDropNotifyProc(void *pClientData) {
    LT_UNUSED(pClientData);
    s_drv->iEvent->NotifyEvent(s_drv->hFrameEvent,
        (void *)NULL,
        (u32)kLTDeviceVideo_Channel_ImageHD,
        (u32)kLTDeviceVideo_Event_FrameDrop);
}
#endif
/* =========================================================================
 * VSYNC rising-edge GPIO ISR — fires at the start of each new frame.
 *
 * On the first rising VSYNC after DMA is started, the DMA ring has been
 * running freely since I2s0StartDma().  We reset the I²S0 RX path here so
 * the next byte captured is guaranteed to be the very first byte of the
 * new frame — eliminating the mid-frame alignment problem that previously
 * required the warmupDrops hack.  After one sync we detach ourselves so
 * the I²S0 EOF ISR alone drives subsequent frames.
 * ========================================================================= */

static void LT_ISR_SAFE VsyncRisingIsr(u8 nPin, bool bPinHigh, void *pClientData) {
    LT_UNUSED(nPin); LT_UNUSED(bPinHigh); LT_UNUSED(pClientData);
    if (!s_drv || !s_drv->dmaRunning) return;

    u32 state = LTAtomic_Load(&s_drv->vsyncArmed);
    if (state == 0) return;

    if (state == 1) {
        /* Initial alignment: first VSYNC after DMA start.  After this we
         * detach the ISR — it will be re-attached only if DMA stalls. */
        LTAtomic_Store(&s_drv->vsyncArmed, 0);
        Esp32GPIO_DetachISR(s_drv->pins.vsync);

        I2S0(I2S_CONF_OFF) &= ~I2S_RX_START_M;
        I2S0(I2S_CONF_OFF)    |=  I2S_RX_RESET_M | I2S_RX_FIFO_RESET_M;
        I2S0(I2S_CONF_OFF)    &= ~(I2S_RX_RESET_M | I2S_RX_FIFO_RESET_M);
        I2S0(I2S_LC_CONF_OFF) |=  I2S_LC_IN_RST_M | I2S_LC_AHBM_FIFO_RST_M | I2S_LC_AHBM_RST_M;
        I2S0(I2S_LC_CONF_OFF) &= ~(I2S_LC_IN_RST_M | I2S_LC_AHBM_FIFO_RST_M | I2S_LC_AHBM_RST_M);

        LTAtomic_Store(&s_drv->wrappers[0].refCount, 0);
        LTAtomic_Store(&s_drv->wrappers[1].refCount, 0);
        for (int i = 0; i < kNumDmaDesc; ++i) ArmDescInPlace(i);
        DmaDesc *head = RebuildRingLinks();

        u32 addr = (u32)head;
        I2S0(I2S_IN_LINK_OFF) = (addr & I2S_INLINK_ADDR_M) | I2S_INLINK_START_M;
        I2S0(I2S_CONF_OFF)    |= I2S_RX_START_M;
        return;
    }

    /* state == 2: DMA stalled because all slots were held by the client.
     * Re-arm whichever slots have been released (pendingRearm set), rebuild
     * the ring, and restart DMA at this frame boundary. */
    LTAtomic_Store(&s_drv->vsyncArmed, 0);
    Esp32GPIO_DetachISR(s_drv->pins.vsync);

    bool anyFree = false;
    for (int i = 0; i < kNumDmaDesc; ++i) {
        if (LTAtomic_CompareAndExchange(&s_drv->pendingRearm[i], 1, 0)) {
            /* Release the hold we kept so DMA wouldn't overwrite the buffer */
            LTAtomic_Store(&s_drv->wrappers[i].refCount, 0);
            ArmDescInPlace(i);
            anyFree = true;
        }
    }
    if (!anyFree) return;   /* shouldn't happen, but be safe */

    DmaDesc *head = RebuildRingLinks();
    if (!head) return;      /* still no free slot somehow */

    I2S0(I2S_CONF_OFF) &= ~I2S_RX_START_M;
    I2S0(I2S_CONF_OFF)    |=  I2S_RX_RESET_M | I2S_RX_FIFO_RESET_M;
    I2S0(I2S_CONF_OFF)    &= ~(I2S_RX_RESET_M | I2S_RX_FIFO_RESET_M);
    I2S0(I2S_LC_CONF_OFF) |=  I2S_LC_IN_RST_M | I2S_LC_AHBM_FIFO_RST_M | I2S_LC_AHBM_RST_M;
    I2S0(I2S_LC_CONF_OFF) &= ~(I2S_LC_IN_RST_M | I2S_LC_AHBM_FIFO_RST_M | I2S_LC_AHBM_RST_M);

    u32 addr = (u32)head;
    I2S0(I2S_IN_LINK_OFF) = (addr & I2S_INLINK_ADDR_M) | I2S_INLINK_START_M;
    I2S0(I2S_CONF_OFF)    |= I2S_RX_START_M;
    s_drv->dmaStalled = false;
}

/* =========================================================================
 * I²S0 EOF ISR — fires on I²S0 in-DMA-EOF at Xtensa level 1
 * ========================================================================= */

static void LT_ISR_SAFE I2s0FrameIsr(void) {
    u32 status = I2S0(I2S_INT_RAW_OFF);
    I2S0(I2S_INT_CLR_OFF) = status;

    if (!(status & I2S_IN_SUC_EOF_M) || !s_drv || !s_drv->dmaRunning) return;

    /* Identify which slot completed */
    u32 eofAddr = I2S0(I2S_IN_EOF_ADDR_OFF);
    int idx = -1;
    for (int i = 0; i < kNumDmaDesc && idx < 0; ++i) {
        for (u32 d = 0; d < s_drv->nDescsPerSlot; ++d) {
            if (eofAddr == (u32)&s_drv->descs[i][d]) { idx = i; break; }
        }
    }
    if (idx < 0) return;

    VideoDataWrapper *wd = &s_drv->wrappers[idx];

    /* If the client still holds this slot (shouldn't happen because we remove
     * claimed slots from the ring, but guard defensively), drop the frame. */
    if (LTAtomic_Load(&wd->refCount) != 0) {
        LTLOG("isr.frame.drop", "dma clobbering frame %p, index %d", (void *)&wd->pub, idx);
        return;
    }

    u32 totalWords = s_drv->bufSize / 4u;
    wd->pub.length   = totalWords;
    wd->pub.time     = LT_GetCore()->GetKernelTime();
    wd->pub.sequence = s_drv->sequence++;
    LTAtomic_Store(&wd->refCount, 1);

    /* This slot is now claimed.  Rebuild the ring so it no longer appears in
     * the DMA descriptor chain — DMA will proceed only through free slots.
     * If no free slot remains the ring terminates at nullDesc and DMA stalls;
     * we set vsyncArmed=2 and re-attach the VSYNC ISR to restart at the next
     * frame boundary once the client releases a slot. */
    DmaDesc *head = RebuildRingLinks();
    if (!head) {
        s_drv->dmaStalled = true;
        LTAtomic_Store(&s_drv->vsyncArmed, 2);
        Esp32GPIO_AttachISR(s_drv->pins.vsync, kEsp32GPIO_Trigger_Rising,
                            VsyncRisingIsr, NULL);
    }

    //LTLOG("isr.frame.acquiring", "acquiring frame %p, index %d", (void *)&wd->pub, idx);
    s_drv->iEvent->NotifyEventFromISR(FrameNotifyProc, wd);
}

/* =========================================================================
 * ILTVideo implementation
 * ========================================================================= */

static bool DvpImpl_Enable(LTDeviceVideo_Source source) {
    if (source != kLTDeviceVideo_Source_0) return false;
    if (s_drv->enabled) return true;
    s_drv->libSensor->PowerOn(s_drv->hSensorUnit);
    s_drv->enabled = true;
    //LTLOG("dvp.enable", NULL);
    return true;
}

static void DvpImpl_Disable(LTDeviceVideo_Source source) {
    LT_UNUSED(source);
    if (!s_drv->enabled) return;
    s_drv->libSensor->PowerOff(s_drv->hSensorUnit);
    s_drv->enabled = false;
    //LTLOG("dvp.disable", NULL);
}

static bool DvpImpl_Start(LTDeviceVideo_Channel channel) {
    if (channel != kLTDeviceVideo_Channel_ImageHD) return false;
    if (s_drv->streaming[channel]) return true;
    if (!s_drv->enabled) { LTLOG_YELLOWALERT("dvp.start.noen", NULL); return false; }

    u32 frameBytes = CalcFrameBytes();
    if (!frameBytes) { LTLOG_YELLOWALERT("dvp.start.sz", NULL); return false; }

    if (!AllocDma(frameBytes)) { LTLOG_YELLOWALERT("dvp.start.oom", "failed to allocate %lu dma buffer bytes", LT_Pu32(frameBytes)); return false; }
    BuildRing();

    I2s0ClockEnable(true);
    I2s0Reset();
    I2s0Configure();

    Esp32MapExternalToCPUIrq(kEsp32_CPU0,
        (Esp32_ExternalIrq)kEsp32_ExternalIrq_I2S0,
        (Esp32_IrqNumber)kEsp32_IrqNumber_I2S0);
    LT_GetCore()->SetInterruptVector(kEsp32_IrqNumber_I2S0, I2s0FrameIsr,
                                     kEsp32_IrqPriority_I2S0);

    /* Start DMA, then wait for the first VSYNC rising edge before delivering
     * any frames.  VsyncRisingIsr resets the I²S0 RX path on that edge so
     * DMA is guaranteed to start at byte 0 of a new frame. */
    //LTLOG("dvp.vsync.v", "2");   /* increment this each time you rebuild to confirm new code is running */
    LTAtomic_Store(&s_drv->vsyncArmed, 1);
    s_drv->dmaRunning = true;
    s_drv->streaming[channel] = true;
    I2s0StartDma();
    Esp32GPIO_AttachISR(s_drv->pins.vsync, kEsp32GPIO_Trigger_Rising, VsyncRisingIsr, NULL);
    //LTLOG("dvp.start", "ch=%ld %lu bytes/frame", channel, LT_Pu32(frameBytes));
    return true;
}

static void DvpImpl_Stop(LTDeviceVideo_Channel channel) {
    if (channel != kLTDeviceVideo_Channel_ImageHD) return;
    if (!s_drv->streaming[channel]) return;

    /* Detach VSYNC ISR if it is active (state 1 = initial alignment,
     * state 2 = DMA stall restart) */
    u32 armed = LTAtomic_Load(&s_drv->vsyncArmed);
    if (armed != 0) {
        LTAtomic_Store(&s_drv->vsyncArmed, 0);
        Esp32GPIO_DetachISR(s_drv->pins.vsync);
    }

    s_drv->dmaRunning = false;
    s_drv->streaming[channel] = false;
    I2s0StopDma();
    LT_GetCore()->SetInterruptVector(kEsp32_IrqNumber_I2S0, NULL,
                                     kEsp32_IrqPriority_I2S0);
    I2s0ClockEnable(false);
    FreeDma();
    //LTLOG("dvp.stop", "ch=%ld", channel);
}

static void DvpImpl_Pause(void) {
    if (s_drv->dmaRunning) {
        I2S0(I2S_CONF_OFF) &= ~I2S_RX_START_M;
    }
}

static void DvpImpl_Resume(void) {
    if (s_drv->dmaRunning) {
        I2S0(I2S_CONF_OFF) |= I2S_RX_START_M;
    }
}

static void DvpImpl_OnVideoEvent(LTDeviceVideo_Channel channel,
                                  LTDeviceVideo_EventProc *eventProc,
                                  void *clientData) {
    if (channel != kLTDeviceVideo_Channel_ImageHD) return;
    s_drv->iEvent->RegisterForEvent(s_drv->hFrameEvent, eventProc, NULL, clientData, false);
}

static void DvpImpl_NoVideoEvent(LTDeviceVideo_Channel channel,
                                  LTDeviceVideo_EventProc *eventProc) {
    if (channel != kLTDeviceVideo_Channel_ImageHD) return;
    s_drv->iEvent->UnregisterFromEvent(s_drv->hFrameEvent, eventProc);
}

static void DvpImpl_ReleaseVideoData(LTDeviceVideo_Channel channel,
                                      LTDeviceVideo_VideoData *videoData) {
    LT_UNUSED(channel);
    if (!videoData) return;
    VideoDataWrapper *wd = (VideoDataWrapper *)videoData;

    int idx = wd->bufIdx;
    if (idx < 0 || idx >= kNumDmaDesc) return;

    /* Prepare the descriptors while the slot is still marked held, so the
     * ISR's RebuildRingLinks cannot race and include a half-ready slot. */
    ArmDescInPlace(idx);

     //LTLOG("release.video.data", "releasing frame %p, index %d", (void *)&wd->pub, idx);
    /* Mark pending-rearm BEFORE clearing refCount.  RebuildRingLinks excludes
     * slots with pendingRearm==1, so even if refCount drops to 0 between the
     * two stores the ISR will not link this slot into the active ring until
     * the VSYNC boundary. */
    LTAtomic_Store(&s_drv->pendingRearm[idx], 1);
    LTAtomic_FetchSubtract(&wd->refCount, 1);

    /* If DMA stalled because all slots were held, the VSYNC ISR is already
     * armed (vsyncArmed==2).  If for some reason it isn't, arm it now. */
    if (s_drv->dmaStalled) {
        if (LTAtomic_CompareAndExchange(&s_drv->vsyncArmed, 0, 2)) {
            Esp32GPIO_AttachISR(s_drv->pins.vsync, kEsp32GPIO_Trigger_Rising,
                                VsyncRisingIsr, NULL);
        }
    }
}

static bool DvpImpl_Capture(LTDeviceVideo_Channel channel,
                             LTDeviceVideo_FrameType type) {
    LT_UNUSED(type);
    return channel == kLTDeviceVideo_Channel_ImageHD && s_drv->streaming[channel];
}

static s32 DvpImpl_CaptureSingle(LTDeviceVideo_Channel channel,
                                   LTDeviceVideo_FrameType type,
                                   u8 *destBuf, u32 destMaxLen) {
    LT_UNUSED(type);
    if (channel != kLTDeviceVideo_Channel_ImageHD) return -1;
    if (!s_drv->streaming[channel]) return -1;

    s32 result = -1;

    /* Poll the pre-allocated wrappers until the ISR fills one */
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    for (int attempts = 0; attempts < 200; ++attempts) {
        for (int i = 0; i < kNumDmaDesc; ++i) {
            VideoDataWrapper *wd = &s_drv->wrappers[i];
            /* Try to take a reference on a slot the ISR just filled */
            if (LTAtomic_FetchAdd(&wd->refCount, 1) >= 1) {
                /* Successfully claimed a frame in this slot */
                u32 len = wd->pub.length;
                if (len > 0 && len <= destMaxLen) {
                    lt_memcpy(destBuf, wd->pub.address, len);
                    result = (s32)len;
                } else {
                    result = -1;
                }
                DvpImpl_ReleaseVideoData(channel, &wd->pub);
                return result;
            }
            /* Not filled yet — undo the increment we just did */
            LTAtomic_FetchSubtract(&wd->refCount, 1);
        }
        iThread->Sleep(LTTime_Milliseconds(5));
    }
    return -1;
}

static s32 DvpImpl_CaptureCrop(LTDeviceVideo_Channel channel,
                                 LTDeviceVideo_FrameType type,
                                 u8 *destBuf, u32 cropW, u32 cropH,
                                 u32 centerX, u32 centerY) {
    /* DVP delivers full frames; crop is not supported in hardware */
    LT_UNUSED(cropW); LT_UNUSED(cropH); LT_UNUSED(centerX); LT_UNUSED(centerY);
    return DvpImpl_CaptureSingle(channel, type, destBuf, cropW * cropH * 2);
}

static void DvpImpl_RequestIdrFrame(LTDeviceVideo_Channel channel) {
    LT_UNUSED(channel);
    /* OV3660 DVP has no H.264 encoder — no-op */
}

static void DvpImpl_Poll(LTDeviceVideo_Channel channel) {
    LT_UNUSED(channel);
    /* Interrupt-driven; polling not required */
}

static bool DvpImpl_GetParam(LTDeviceVideo_Param param, void *value) {
    switch (param) {
        case kLTDeviceVideo_Param_ResolutionHD: {
            LTImageSensorFrameResolution fsz = {0, 0};
            if (!s_drv->iSensor->GetAttribute(s_drv->hSensorUnit,
                                               kLTImageSensorAttribute_FrameResolution, &fsz))
                return false;
            LTDeviceVideo_Resolution *res = value;
            res->width  = fsz.width;
            res->height = fsz.height;
            return true;
        }
        case kLTDeviceVideo_Param_Status:
            LTLOG("dvp.status", "enabled=%d streaming=%d dma=%d",
                s_drv->enabled,
                s_drv->streaming[kLTDeviceVideo_Channel_ImageHD],
                s_drv->dmaRunning);
            return true;
        default:
            return false;
    }
}

static bool DvpImpl_SetParam(LTDeviceVideo_Param param, const void *value) {
    switch (param) {
        case kLTDeviceVideo_Param_FlipVertical: {
            u8 flips[2] = { 1, 0 };
            LTImageSensorAttributeParams p = { .in = flips, .inLen = 2 };
            return s_drv->iSensor->SetAttribute(s_drv->hSensorUnit,
                kLTImageSensorAttribute_FlipVerticalHorizontal, &p);
        }
        case kLTDeviceVideo_Param_FlipHorizontal: {
            u8 flips[2] = { 0, 1 };
            LTImageSensorAttributeParams p = { .in = flips, .inLen = 2 };
            return s_drv->iSensor->SetAttribute(s_drv->hSensorUnit,
                kLTImageSensorAttribute_FlipVerticalHorizontal, &p);
        }
        case kLTDeviceVideo_Param_AutoExposure: {
            bool en = *(const bool *)value;
            return s_drv->iSensor->SetAttribute(s_drv->hSensorUnit,
                kLTImageSensorAttribute_AutoExposureControl, &en);
        }
        case kLTDeviceVideo_Param_Framerate: {
            u32 fps = *(const u32 *)value;
            return s_drv->iSensor->SetAttribute(s_drv->hSensorUnit,
                kLTImageSensorAttribute_FrameRate, &fps);
        }
        case kLTDeviceVideo_Param_Brightness: {
            int level = *(const int *)value;
            return s_drv->iSensor->SetAttribute(s_drv->hSensorUnit,
                kLTImageSensorAttribute_Brightness, &level);
        }
        default:
            return false;
    }
}

/* =========================================================================
 * Library init / fini
 * ========================================================================= */

static void LoadPinConfig(void) {
    s_drv->pins = kDefaultPins;
    LTDeviceConfig *cfg = lt_openlibrary(LTDeviceConfig);
    if (!cfg) return;
    u32 sec = cfg->GetDriverSection("LTDeviceVideo", "Esp32DriverVideoDVP");
    if (sec) {
        #define RD(key, field) { u32 _v = cfg->ReadInteger(sec, key); if (_v) (field) = (u8)_v; }
        RD("pclk",  s_drv->pins.pclk);  RD("vsync", s_drv->pins.vsync);
        RD("href",  s_drv->pins.href);
        RD("d0", s_drv->pins.d[0]); RD("d1", s_drv->pins.d[1]);
        RD("d2", s_drv->pins.d[2]); RD("d3", s_drv->pins.d[3]);
        RD("d4", s_drv->pins.d[4]); RD("d5", s_drv->pins.d[5]);
        RD("d6", s_drv->pins.d[6]); RD("d7", s_drv->pins.d[7]);
        #undef RD
    }
    lt_closelibrary(cfg);
}

static void Shutdown(void) {
    if (!s_drv) return;
    for (int ch = 0; ch < kLTDeviceVideo_Channel_Count; ++ch)
        if (s_drv->streaming[ch]) DvpImpl_Stop((LTDeviceVideo_Channel)ch);
    if (s_drv->enabled) DvpImpl_Disable(kLTDeviceVideo_Source_0);
    lt_destroyhandle(s_drv->hFrameEvent); s_drv->hFrameEvent = 0;
    lt_destroyhandle(s_drv->hSensorUnit); s_drv->hSensorUnit = 0;
    lt_closelibrary(s_drv->libSensor);    s_drv->libSensor = NULL;
    lt_free(s_drv);
    s_drv = NULL;
}

static bool Esp32DriverVideoDVPImpl_LibInit(void) {
    s_drv = lt_malloc(sizeof(DvpDriver));
    if (!s_drv) return false;
    lt_memset(s_drv, 0, sizeof(DvpDriver));

    VideoDataWrapper *wd = &s_drv->wrappers[0];
    LT_ASSERT((void *)wd == (void *)&wd->pub);

    /* Open image sensor */
    if (!(s_drv->libSensor = lt_openlibrary(LTDeviceImageSensor))) {
        LTLOG("dvp.init.sensor", NULL); goto err;
    }
    if (!(s_drv->hSensorUnit = s_drv->libSensor->CreateDeviceUnitHandle(0))) {
        LTLOG("dvp.init.unit", NULL); goto err;
    }
    if (!(s_drv->iSensor = lt_gethandleinterface(ILTDriverImageSensor, s_drv->hSensorUnit))) {
        LTLOG("dvp.init.intf", NULL); goto err;
    }

    /* Create the frame event */
    s_drv->iEvent = lt_getlibraryinterface(ILTEvent, LT_GetCore());
    if (!s_drv->iEvent) { LTLOG("dvp.init.event.intf", NULL); goto err; }

    s_drv->hFrameEvent = LT_GetCore()->CreateEvent(&s_frameEventArgs, DispatchFrameEvent, NULL, NULL, NULL);
    if (!s_drv->hFrameEvent) { LTLOG("dvp.init.event", NULL); goto err; }

    LoadPinConfig();

    //LTLOG("dvp.init", "ESP32 DVP video driver ready (pclk=%lu vsync=%lu href=%lu)",
    //    LT_Pu32(s_drv->pins.pclk), LT_Pu32(s_drv->pins.vsync), LT_Pu32(s_drv->pins.href));
    return true;

err:
    Shutdown();
    return false;
}

static void Esp32DriverVideoDVPImpl_LibFini(void) {
    Shutdown();
}

/* =========================================================================
 * define_LTDEVICE_DRIVER_IMPLEMENTATION + ILTVideo interface binding
 * ========================================================================= */

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceVideo, Esp32DriverVideoDVP);

static u32 Esp32DriverVideoDVPImpl_GetNumDeviceUnits(void) { return 1; }

static const ILTVideo s_ILTVideo;   /* defined below by define_LTLIBRARY_INTERFACE */

static LTDeviceUnit Esp32DriverVideoDVPImpl_CreateDeviceUnitHandle(u32 n) {
    LT_UNUSED(n);
    /* Single unit — the handle carries the ILTVideo interface with no private data */
    return LT_GetCore()->CreateHandle((LTInterface *)&s_ILTVideo, 1);
}

define_LTLIBRARY_INTERFACE(ILTVideo) {
    .Enable           = DvpImpl_Enable,
    .Disable          = DvpImpl_Disable,
    .Start            = DvpImpl_Start,
    .Stop             = DvpImpl_Stop,
    .Pause            = DvpImpl_Pause,
    .Resume           = DvpImpl_Resume,
    .OnVideoEvent     = DvpImpl_OnVideoEvent,
    .NoVideoEvent     = DvpImpl_NoVideoEvent,
    .ReleaseVideoData = DvpImpl_ReleaseVideoData,
    .Capture          = DvpImpl_Capture,
    .CaptureSingle    = DvpImpl_CaptureSingle,
    .CaptureCrop      = DvpImpl_CaptureCrop,
    .RequestIdrFrame  = DvpImpl_RequestIdrFrame,
    .Poll             = DvpImpl_Poll,
    .GetParam         = DvpImpl_GetParam,
    .SetParam         = DvpImpl_SetParam,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(Esp32DriverVideoDVP, (ILTVideo))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  31-May-26   created
 */
