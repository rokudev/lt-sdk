/*******************************************************************************
 * lt/source/ltshell/dsi/LTShellDsi.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 *******************************************************************************
 * LT Library registering a "dsi" shell command that exercises LTDeviceMipiDsi.
 *
 * The subcommands are thin calls into the device api, built around a working
 * configuration the shell keeps and only hands to Configure when told to, so a
 * panel can be described a field at a time and then brought up.
 *
 * "dsi selftest" is the useful one with no panel attached: it walks a fresh
 * device object through the whole interface and checks every documented
 * contract - the rejections an unconfigured or powered-off device owes its
 * caller, the configuration validation, the lane states after power on, and the
 * argument checking on the packet calls.  Nothing it does needs a display to
 * answer, so a failure is the device, the driver, or the link, not the panel.
 *
 * Results a panel would have to answer for - whether a DCS write is acked,
 * whether the lanes reach ULPS - are reported as notes rather than scored, so
 * a bare board does not read as a pile of failures.
 *******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/mipidsi/LTDeviceMipiDsi.h>
#include <lt/device/gpio/LTDeviceGpio.h>
#include <lt/system/shell/LTSystemShell.h>

DEFINE_LTLOG_SECTION("ltshell.dsi");

/*_________________________
_/ #forward declarations */
static int  ShellCommandDsi(LTShell hShell, int argc, const char ** argv);
static void ShellHelpDsi(LTShell hShell, int argc, const char ** argv);

/*____________________
_/ static constants */
static const LTSystemShell_CommandDesc s_dsiShellCommands[] = {
    { "dsi", ShellCommandDsi, "exercise the LTDeviceMipiDsi driver", ShellHelpDsi },
};

/* The default panel description: a 480x480 RGB565 video mode panel at 60Hz over
   two lanes.  Chosen because it is a common DSI part and because the link rate
   and pixel clock it implies sit inside what a host controller can be expected
   to make, so "dsi selftest" has something to configure on a bare board. */
#define kDefaultHorizontalActive        480
#define kDefaultHorizontalSyncWidth     10
#define kDefaultHorizontalBackPorch     20
#define kDefaultHorizontalFrontPorch    20
#define kDefaultVerticalActive          480
#define kDefaultVerticalSyncWidth       4
#define kDefaultVerticalBackPorch       12
#define kDefaultVerticalFrontPorch      20
#define kDefaultFrameRateHz             60
#define kDefaultDataLanes               2

#define kMaxPacketBytes                 16      /* as many payload bytes as the shell will assemble */
#define kMaxReadBytes                   16

/*____________________
_/ static variables */
static LTSystemShell    * s_pLTSystemShell  = NULL;
static LTDeviceMipiDsi  * s_pDsi            = NULL;

/* The configuration being assembled, and a scratch copy for the probes that
   need a deliberately broken one.  Both are static rather than automatic
   because a CommandProc has 320 bytes of stack and these are over a hundred
   bytes each. */
static LTDeviceMipiDsi_Config   s_config;
static LTDeviceMipiDsi_Config   s_scratch;

static u8               * s_pFrameBuffer    = NULL;
static u32                s_nFrameBufferBytes = 0;
static u8               * s_pAnimationFrameBuffer = NULL;
/* What was allocated, which is s_nMarginBytes before the address the driver is given: the
   logical image starts at the allocation and the driver's pointer is "offset" pixels into it. */
static u8               * s_pFrameBufferAlloc = NULL;
static u8               * s_pAnimationAlloc   = NULL;
static u32                s_nMarginBytes      = 0;

/* The geometry the framebuffer was allocated for.  "dsi config" edits the working configuration
   at any time but only "dsi fb alloc" sizes the allocation, so every drawing command works from
   this copy, taken at allocation, and never from the working configuration. */
static struct {
    u32                             nStride, nWidth, nHeight, nOffset, nBitsPerPixel;
    LTDeviceMipiDsi_PixelFormat     format;
} s_geom;

/* The logical view of the framebuffer, see FbView below.  The stride is always the active
   width of the config (the driver reads the frame as one linear run of that many pixels per
   line); the visible width and the offset are set with "dsi config view".  A width of 0 means
   the whole line and an offset of kAutoOffset means stride - width. */
#define kAutoOffset 0xffffffffu
static u32                s_viewWidth         = 0;
static u32                s_viewOffset        = kAutoOffset;

static u8                 s_packetBuffer[kMaxPacketBytes];
static u8                 s_readBuffer[kMaxReadBytes];

/* Written by the error event proc, which runs on a device level thread rather
   than the shell's. */
static volatile u32       s_nErrorEvents     = 0;
static volatile u32       s_nErrorEventMask  = 0;

/*____________________
_/ helpers */
static const char * ModeName(LTDeviceMipiDsi_Mode mode) {
    switch (mode) {
        case kLTDeviceMipiDsi_Mode_VideoSyncPulse: return "syncpulse";
        case kLTDeviceMipiDsi_Mode_VideoSyncEvent: return "syncevent";
        case kLTDeviceMipiDsi_Mode_VideoBurst:     return "burst";
        case kLTDeviceMipiDsi_Mode_Command:        return "command";
        default:                                   return "unset";
    }
}

static const char * FormatName(LTDeviceMipiDsi_PixelFormat format) {
    switch (format) {
        case kLTDeviceMipiDsi_PixelFormat_RGB565:       return "rgb565";
        case kLTDeviceMipiDsi_PixelFormat_RGB666Packed: return "rgb666p";
        case kLTDeviceMipiDsi_PixelFormat_RGB666Loose:  return "rgb666l";
        case kLTDeviceMipiDsi_PixelFormat_RGB888:       return "rgb888";
        case kLTDeviceMipiDsi_PixelFormat_YUV422_8Bit:  return "yuv422";
        default:                                        return "unset";
    }
}

static const char * LaneStateName(LTDeviceMipiDsi_LaneState state) {
    switch (state) {
        case kLTDeviceMipiDsi_LaneState_Off:           return "off";
        case kLTDeviceMipiDsi_LaneState_Stop:          return "stop";
        case kLTDeviceMipiDsi_LaneState_LowPower:      return "lp";
        case kLTDeviceMipiDsi_LaneState_HighSpeed:     return "hs";
        case kLTDeviceMipiDsi_LaneState_UltraLowPower: return "ulps";
        default:                                       return "?";
    }
}

static LTDeviceMipiDsi_Mode ParseMode(const char * pArg) {
    if (pArg == NULL)                            return kLTDeviceMipiDsi_Mode_Unset;
    if (0 == lt_strcmp(pArg, "syncpulse"))       return kLTDeviceMipiDsi_Mode_VideoSyncPulse;
    if (0 == lt_strcmp(pArg, "syncevent"))       return kLTDeviceMipiDsi_Mode_VideoSyncEvent;
    if (0 == lt_strcmp(pArg, "burst"))           return kLTDeviceMipiDsi_Mode_VideoBurst;
    if (0 == lt_strcmp(pArg, "command"))         return kLTDeviceMipiDsi_Mode_Command;
    return kLTDeviceMipiDsi_Mode_Unset;
}

static LTDeviceMipiDsi_PixelFormat ParseFormat(const char * pArg) {
    if (pArg == NULL)                            return kLTDeviceMipiDsi_PixelFormat_Unset;
    if (0 == lt_strcmp(pArg, "rgb565"))          return kLTDeviceMipiDsi_PixelFormat_RGB565;
    if (0 == lt_strcmp(pArg, "rgb666p"))         return kLTDeviceMipiDsi_PixelFormat_RGB666Packed;
    if (0 == lt_strcmp(pArg, "rgb666l"))         return kLTDeviceMipiDsi_PixelFormat_RGB666Loose;
    if (0 == lt_strcmp(pArg, "rgb888"))          return kLTDeviceMipiDsi_PixelFormat_RGB888;
    if (0 == lt_strcmp(pArg, "yuv422"))          return kLTDeviceMipiDsi_PixelFormat_YUV422_8Bit;
    return kLTDeviceMipiDsi_PixelFormat_Unset;
}

static bool ParseU32(const char * pArg, u32 * pValueToSet) {
    if (pArg == NULL) return false;
    char * pEnd = NULL;
    u32 nValue = lt_strtou32(pArg, &pEnd, 0);
    if (pEnd == NULL || *pEnd != '\0') return false;
    *pValueToSet = nValue;
    return true;
}

static u32 BitsPerPixel(LTDeviceMipiDsi_PixelFormat format) {
    switch (format) {
        case kLTDeviceMipiDsi_PixelFormat_RGB565:       return 16;
        case kLTDeviceMipiDsi_PixelFormat_RGB666Packed: return 18;
        case kLTDeviceMipiDsi_PixelFormat_RGB666Loose:  return 24;
        case kLTDeviceMipiDsi_PixelFormat_RGB888:       return 24;
        case kLTDeviceMipiDsi_PixelFormat_YUV422_8Bit:  return 16;
        default:                                        return 0;
    }
}

/* What a frame of the configured geometry occupies, rounded up to a whole
   number of 64 bit words - a scanout engine that counts the frame in words
   wants the framebuffer to end on one. */
static u32 FrameBufferBytes(const LTDeviceMipiDsi_Config * pConfig) {
    u32 nBitsPerPixel = BitsPerPixel(pConfig->pixelFormat);
    u32 nPixels       = pConfig->timing.nHorizontalActive * pConfig->timing.nVerticalActive;
    u32 nBytes        = ((nPixels * nBitsPerPixel) + 7u) / 8u;
    return (nBytes + 7u) & ~7u;
}

static void SetDefaultConfig(LTDeviceMipiDsi_Config * pConfig) {
    lt_memset(pConfig, 0, sizeof(*pConfig));
    pConfig->mode             = kLTDeviceMipiDsi_Mode_VideoSyncPulse;
    pConfig->pixelFormat      = kLTDeviceMipiDsi_PixelFormat_RGB565;
    pConfig->nDataLanes       = kDefaultDataLanes;
    pConfig->nVirtualChannel  = 0;
    pConfig->nLaneRateMbps    = 0;      /* let the driver derive it from the timing */
    pConfig->bContinuousClock = true;
    pConfig->bEnableEotPacket = false;

    pConfig->timing.nHorizontalActive     = kDefaultHorizontalActive;
    pConfig->timing.nHorizontalSyncWidth  = kDefaultHorizontalSyncWidth;
    pConfig->timing.nHorizontalBackPorch  = kDefaultHorizontalBackPorch;
    pConfig->timing.nHorizontalFrontPorch = kDefaultHorizontalFrontPorch;
    pConfig->timing.nVerticalActive       = kDefaultVerticalActive;
    pConfig->timing.nVerticalSyncWidth    = kDefaultVerticalSyncWidth;
    pConfig->timing.nVerticalBackPorch    = kDefaultVerticalBackPorch;
    pConfig->timing.nVerticalFrontPorch   = kDefaultVerticalFrontPorch;
    pConfig->timing.nFrameRateHz          = kDefaultFrameRateHz;
}

static void PrintErrors(LTShell hShell, LTDeviceMipiDsi_Error errors) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    if (errors == kLTDeviceMipiDsi_Error_None) {
        iShell->PutString(hShell, "none");
        return;
    }
    for (u32 nBit = 0; nBit < 32; ++nBit) {
        u32 flag = 1u << nBit;
        if (errors & flag) iShell->Print(hShell, "%s ", s_pDsi->API->ErrorToString((LTDeviceMipiDsi_Error)flag));
    }
}

static void DsiErrorEventProc(LTDeviceMipiDsi_Error errors, void * pClientData) {
    LT_UNUSED(pClientData);
    ++s_nErrorEvents;
    s_nErrorEventMask |= (u32)errors;
}

/* Returns false, and leaves the buffer allocated, if the stream could not be confirmed
   stopped. */
static bool FreeFrameBuffer(void) {
    if (s_pFrameBuffer == NULL) return true;
    if (s_pDsi && s_pDsi->API->IsVideoActive(s_pDsi) && ! s_pDsi->API->StopVideo(s_pDsi)) return false;
    if (s_pDsi) s_pDsi->API->SetFrameBuffer(s_pDsi, NULL, 0);
    lt_free(s_pAnimationAlloc);
    lt_free(s_pFrameBufferAlloc);
    s_pAnimationFrameBuffer = NULL;
    s_pAnimationAlloc   = NULL;
    s_pFrameBuffer      = NULL;
    s_pFrameBufferAlloc = NULL;
    s_nFrameBufferBytes = 0;
    s_nMarginBytes      = 0;
    lt_memset(&s_geom, 0, sizeof s_geom);
    return true;
}

/* The logical framebuffer: pixel (x, y) is at pDriver + ((y * stride + x) - offset) * bytes per
   pixel, where pDriver is the address given to the driver.  That is a width x height image with
   a line stride of "stride" pixels whose first pixel is "offset" pixels before the driver's
   address - so a panel whose visible window starts "offset" pixels before the first pixel of
   each line shows the image at its own (0,0).  The offset pixels before pDriver are allocated
   as a margin but are never sent. */
typedef struct {
    u8 * pDriver;
    u32  nStride, nWidth, nHeight, nOffset, nBytesPerPixel;
} FbView;

static u32 ViewWidth(void) {
    u32 nStride = s_config.timing.nHorizontalActive;
    return (s_viewWidth && s_viewWidth <= nStride) ? s_viewWidth : nStride;
}

static u32 ViewOffset(void) {
    u32 nStride = s_config.timing.nHorizontalActive;
    return (s_viewOffset == kAutoOffset) ? (nStride - ViewWidth()) : s_viewOffset;
}

static FbView MakeView(u8 * pDriver) {
    FbView v;
    v.pDriver        = pDriver;
    v.nStride        = s_geom.nStride;
    v.nWidth         = s_geom.nWidth;
    v.nHeight        = s_geom.nHeight;
    v.nOffset        = s_geom.nOffset;
    v.nBytesPerPixel = s_geom.nBitsPerPixel / 8;
    return v;
}

static u8 * ViewPixel(const FbView * v, u32 x, u32 y) {
    return v->pDriver + (((s64)y * v->nStride + x) - (s64)v->nOffset) * v->nBytesPerPixel;
}

static void ViewPut16(const FbView * v, u32 x, u32 y, u16 nColour) {
    u8 * p = ViewPixel(v, x, y);
    p[0] = (u8)nColour;
    p[1] = (u8)(nColour >> 8);
}

/* Prefers the heap region named "psram" because a frame of any real panel is
   larger than the on-chip SRAM a small part has.  A platform without that
   region falls through to the unrestricted allocator.  The margin in front of the driver's
   address is the view's offset. */
static u8 * AllocWithMargin(u32 nBytes, u32 nMarginBytes, u8 ** ppAlloc) {
    LTMemoryRegion region = LT_GetCore()->GetNamedMemoryRegion("psram");
    u8 * pAlloc = (u8 *)lt_malloc_from_region(region, nMarginBytes + nBytes);
    if (pAlloc == NULL) return NULL;
    lt_memset(pAlloc, 0, nMarginBytes + nBytes);
    *ppAlloc = pAlloc;
    return pAlloc + nMarginBytes;
}

static bool AllocFrameBuffer(u32 nBytes) {
    if (! FreeFrameBuffer()) return false;
    if (nBytes == 0) return false;
    u32 nBytesPerPixel = BitsPerPixel(s_config.pixelFormat) / 8;
    u64 nMarginWide = (u64)ViewOffset() * (nBytesPerPixel ? nBytesPerPixel : 2);
    if (nMarginWide + nBytes > 0xffffffffu) return false;
    u32 nMargin = (u32)nMarginWide;
    s_pFrameBuffer = AllocWithMargin(nBytes, nMargin, &s_pFrameBufferAlloc);
    if (s_pFrameBuffer == NULL) return false;
    s_nFrameBufferBytes = nBytes;
    s_nMarginBytes      = nMargin;
    s_geom.nStride       = s_config.timing.nHorizontalActive;
    s_geom.nWidth        = ViewWidth();
    s_geom.nHeight       = s_config.timing.nVerticalActive;
    s_geom.nOffset       = ViewOffset();
    s_geom.nBitsPerPixel = BitsPerPixel(s_config.pixelFormat);
    s_geom.format        = s_config.pixelFormat;
    return true;
}

/* The second (ping-pong) buffer, with the same margin; returns the driver's address. */
static u8 * AnimationBuffer(void) {
    if (s_pAnimationFrameBuffer == NULL) {
        s_pAnimationFrameBuffer = AllocWithMargin(s_nFrameBufferBytes, s_nMarginBytes, &s_pAnimationAlloc);
    }
    return s_pAnimationFrameBuffer;
}

/*____________________
_/ subcommands */
static void DsiShow(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);

    bool bHasApplied = s_pDsi->API->GetConfig(s_pDsi, &s_scratch);

    iShell->Print(hShell, "working config (not yet applied unless noted)\n");
    iShell->Print(hShell, "  mode %s  format %s  lanes %u  vc %u  rate %u Mbps%s\n",
                  ModeName(s_config.mode), FormatName(s_config.pixelFormat),
                  (unsigned)s_config.nDataLanes, (unsigned)s_config.nVirtualChannel,
                  (unsigned)s_config.nLaneRateMbps,
                  s_config.nLaneRateMbps ? "" : " (derived)");
    iShell->Print(hShell, "  clock %s  eotp %s\n",
                  s_config.bContinuousClock ? "continuous" : "gated",
                  s_config.bEnableEotPacket ? "on" : "off");
    iShell->Print(hShell, "  h %u act %u sync %u bp %u fp\n",
                  (unsigned)s_config.timing.nHorizontalActive,
                  (unsigned)s_config.timing.nHorizontalSyncWidth,
                  (unsigned)s_config.timing.nHorizontalBackPorch,
                  (unsigned)s_config.timing.nHorizontalFrontPorch);
    iShell->Print(hShell, "  view: width %u of stride %u, offset %u%s\n",
                  (unsigned)ViewWidth(), (unsigned)s_config.timing.nHorizontalActive, (unsigned)ViewOffset(),
                  s_viewOffset == kAutoOffset ? " (stride - width)" : "");
    iShell->Print(hShell, "  v %u act %u sync %u bp %u fp  %u Hz\n",
                  (unsigned)s_config.timing.nVerticalActive,
                  (unsigned)s_config.timing.nVerticalSyncWidth,
                  (unsigned)s_config.timing.nVerticalBackPorch,
                  (unsigned)s_config.timing.nVerticalFrontPorch,
                  (unsigned)s_config.timing.nFrameRateHz);
    iShell->Print(hShell, "  a frame is %u bytes\n", (unsigned)FrameBufferBytes(&s_config));

    iShell->Print(hShell, "device\n");
    iShell->Print(hShell, "  applied %s  powered %s  video %s  commands %s\n",
                  bHasApplied ? "yes" : "no",
                  s_pDsi->API->IsPoweredOn(s_pDsi)   ? "yes" : "no",
                  s_pDsi->API->IsVideoActive(s_pDsi) ? "active" : "idle",
                  s_pDsi->API->GetCommandsUseHighSpeed(s_pDsi) ? "hs" : "lp");
    if (bHasApplied) {
        iShell->Print(hShell, "  applied: mode %s format %s lanes %u vc %u\n",
                      ModeName(s_scratch.mode), FormatName(s_scratch.pixelFormat),
                      (unsigned)s_scratch.nDataLanes, (unsigned)s_scratch.nVirtualChannel);
    }
    iShell->Print(hShell, "  framebuffer %lx %u bytes\n",
                  (unsigned long)(LT_SIZE)s_pFrameBuffer, (unsigned)s_nFrameBufferBytes);

    iShell->Print(hShell, "  clock lane %s, data lanes", LaneStateName(s_pDsi->API->GetClockLaneState(s_pDsi)));
    for (u32 nLane = 0; nLane < s_config.nDataLanes; ++nLane) {
        iShell->Print(hShell, " %s", LaneStateName(s_pDsi->API->GetDataLaneState(s_pDsi, nLane)));
    }
    iShell->PutString(hShell, "\n");

    iShell->Print(hShell, "  %u error events, mask ", (unsigned)s_nErrorEvents);
    PrintErrors(hShell, (LTDeviceMipiDsi_Error)s_nErrorEventMask);
    iShell->PutString(hShell, "\n  accumulated ");
    PrintErrors(hShell, s_pDsi->API->GetErrors(s_pDsi, false));
    iShell->PutString(hShell, "\n");
}

static void DsiRegisterProc(const char * pName, u32 nAddress, u32 nValue, void * pClientData) {
    LTShell hShell = (LTShell)(LT_SIZE)pClientData;
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    iShell->Print(hShell, "  %-26s %08lx  %08lx\n", pName, (unsigned long)nAddress, (unsigned long)nValue);
}

static void DsiRegisters(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    iShell->PutString(hShell, "  register                   address   value\n");
    s_pDsi->API->DumpRegisters(s_pDsi, DsiRegisterProc, (void *)(LT_SIZE)hShell);
}

/*____________________
_/ the Waveshare ESP32-P4-WIFI6-Touch-LCD-5 panel */
/*
 * That board carries a Himax HX8394 720x1280 panel on two lanes, which answers a DCS
 * read of 0x04 with 83 94 0f.  Everything below is the board's own: the geometry and
 * link rate are the Waveshare BSP's (esp32_p4_wifi6_touch_lcd_5: HX8394_720_1280 timing,
 * 700Mbps per lane, RGB565), and the register table is the esp_lcd_hx8394 driver's
 * default init, command for command and in the same order.  Source:
 *
 *   https://github.com/waveshareteam/Waveshare-ESP32-components
 *       bsp/esp32_p4_wifi6_touch_lcd_5, display/lcd/esp_lcd_hx8394
 *
 * The shell cannot type this table in: a DCS write is capped at kMaxPacketBytes and
 * the gamma registers are 58 bytes, so it is built in.  What the shell does not do is
 * the board's own wiring - the backlight is GPIO 26 and the panel reset is GPIO 27,
 * active high, and both are driven with the gpio command.
 */
static const u8 s_hx8394Params0[] = { 0xff, 0x83, 0x94 };
static const u8 s_hx8394Params1[] = { 0x48, 0x0a, 0x6a, 0x09, 0x33, 0x54, 0x71, 0x71, 0x2e, 0x45 };
static const u8 s_hx8394Params2[] = { 0x61, 0x03, 0x68, 0x6b, 0xb2, 0xc0 };
static const u8 s_hx8394Params3[] = { 0x00, 0x80, 0x64, 0x0c, 0x06, 0x2f };
static const u8 s_hx8394Params4[] = { 0x1c, 0x78, 0x1c, 0x78, 0x1c, 0x78, 0x01, 0x0c, 0x86, 0x75, 0x00, 0x3f, 0x1c, 0x78, 0x1c, 0x78, 0x1c, 0x78, 0x01, 0x0c, 0x86 };
static const u8 s_hx8394Params5[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x32, 0x10, 0x05, 0x00, 0x05, 0x32, 0x13, 0xc1, 0x00, 0x01, 0x32, 0x10, 0x08, 0x00, 0x00, 0x37, 0x03, 0x07, 0x07, 0x37, 0x05, 0x05, 0x37, 0x0c, 0x40 };
static const u8 s_hx8394Params6[] = { 0x18, 0x18, 0x18, 0x18, 0x22, 0x23, 0x20, 0x21, 0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x19, 0x19, 0x19, 0x19 };
static const u8 s_hx8394Params7[] = { 0x18, 0x18, 0x19, 0x19, 0x21, 0x20, 0x23, 0x22, 0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x19, 0x19, 0x18, 0x18 };
static const u8 s_hx8394Params8[] = { 0x07, 0x08, 0x09, 0x0d, 0x10, 0x14, 0x16, 0x13, 0x24, 0x36, 0x48, 0x4a, 0x58, 0x6f, 0x76, 0x80, 0x97, 0xa5, 0xa8, 0xb5, 0xc6, 0x62, 0x63, 0x68, 0x6f, 0x72, 0x78, 0x7f, 0x7f, 0x00, 0x02, 0x08, 0x0d, 0x0c, 0x0e, 0x0f, 0x10, 0x24, 0x36, 0x48, 0x4a, 0x58, 0x6f, 0x78, 0x82, 0x99, 0xa4, 0xa0, 0xb1, 0xc0, 0x5e, 0x5e, 0x64, 0x6b, 0x6c, 0x73, 0x7f, 0x7f };
static const u8 s_hx8394Params9[] = { 0x0b };
static const u8 s_hx8394Params10[] = { 0x1f, 0x73 };
static const u8 s_hx8394Params11[] = { 0x6b, 0x6b };
static const u8 s_hx8394Params12[] = { 0x02 };
static const u8 s_hx8394Params13[] = { 0x01 };
static const u8 s_hx8394Params14[] = { 0x00 };
static const u8 s_hx8394Params15[] = { 0x00 };
static const u8 s_hx8394Params16[] = { 0x40, 0x81, 0x50, 0x00, 0x1a, 0xfc, 0x01 };
static const u8 s_hx8394Params17[] = { 0x50 };
static const u8 s_hx8394Params19[] = { 0x00, 0x80, 0x64, 0x0c, 0x06, 0x2f, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x18 };

static const struct { u8 nCommand; u8 nParams; u16 nDelayMs; const u8 * pParams; } s_hx8394Init[] = {
    { 0xb9,  3,   0, s_hx8394Params0 },
    { 0xb1, 10,   0, s_hx8394Params1 },
    { 0xba,  6,   0, s_hx8394Params2 },
    { 0xb2,  6,   0, s_hx8394Params3 },
    { 0xb4, 21,   0, s_hx8394Params4 },
    { 0xd3, 33,   0, s_hx8394Params5 },
    { 0xd5, 44,   0, s_hx8394Params6 },
    { 0xd6, 44,   0, s_hx8394Params7 },
    { 0xe0, 58,   0, s_hx8394Params8 },
    { 0xcc,  1,   0, s_hx8394Params9 },
    { 0xc0,  2,   0, s_hx8394Params10 },
    { 0xb6,  2,   0, s_hx8394Params11 },
    { 0xd4,  1,   0, s_hx8394Params12 },
    { 0xbd,  1,   0, s_hx8394Params13 },
    { 0xb1,  1,   0, s_hx8394Params14 },
    { 0xbd,  1,   0, s_hx8394Params15 },
    { 0xbf,  7,   0, s_hx8394Params16 },
    { 0x3a,  1,   0, s_hx8394Params17 },
    { 0x11,  0, 200, NULL },
    { 0xb2, 12,   0, s_hx8394Params19 },
    { 0x29,  0,  80, NULL },
};

/* The driver's own steps ahead of the table: sleep out, then the colour order, the
   pixel format and the lane count, each with the values the BSP gives it */
static bool DsiPanelHx8394(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    LTOThread * pThread = LT_GetCore()->GetCurrentThreadObject();
    const u8 nLaneCommand = (s_config.nDataLanes == 1) ? 0x60 : 0x61;
    const u8 nMadctl      = 0x00;      /* RGB order */
    const u8 nColmod      = 0x55;      /* RGB565 */

    if (! s_pDsi->API->DcsWrite(s_pDsi, 0x11, NULL, 0)) goto fail;
    pThread->API->Sleep(LTTime_Milliseconds(120));
    if (! s_pDsi->API->DcsWrite(s_pDsi, 0x36, &nMadctl, 1))      goto fail;
    if (! s_pDsi->API->DcsWrite(s_pDsi, 0x3a, &nColmod, 1))      goto fail;
    if (! s_pDsi->API->DcsWrite(s_pDsi, 0xba, &nLaneCommand, 1)) goto fail;

    for (u32 i = 0; i < sizeof(s_hx8394Init) / sizeof(s_hx8394Init[0]); ++i) {
        if (! s_pDsi->API->DcsWrite(s_pDsi, s_hx8394Init[i].nCommand, s_hx8394Init[i].pParams, s_hx8394Init[i].nParams)) {
            iShell->Print(hShell, "DcsWrite of 0x%02x failed (table entry %u)\n", (unsigned)s_hx8394Init[i].nCommand, (unsigned)i);
            return false;
        }
        if (s_hx8394Init[i].nDelayMs) pThread->API->Sleep(LTTime_Milliseconds(s_hx8394Init[i].nDelayMs));
    }
    return true;

fail:
    iShell->PutString(hShell, "DcsWrite failed ahead of the table\n");
    return false;
}

/* Replace the working config with the HX8394 panel's.  The Waveshare BSP asks for a
   58MHz pixel clock, which over 800x1318 total pixels is 55Hz, but 55 reports a DSI
   transmit FIFO overflow here while 40 is clean in every video mode, with the PSRAM
   at 200MHz - so 40 it is.  The frame comes out of PSRAM, and with the PSRAM at its
   old 20MHz not even 20 was clean. */
static void SetHx8394Config(LTDeviceMipiDsi_Config * pConfig) {
    lt_memset(pConfig, 0, sizeof(*pConfig));
    pConfig->mode             = kLTDeviceMipiDsi_Mode_VideoBurst;
    pConfig->pixelFormat      = kLTDeviceMipiDsi_PixelFormat_RGB565;
    pConfig->nDataLanes       = 2;
    pConfig->nVirtualChannel  = 0;
    pConfig->nLaneRateMbps    = 700;
    pConfig->bContinuousClock = true;
    pConfig->bEnableEotPacket = false;

    pConfig->timing.nHorizontalActive     = 720;
    pConfig->timing.nHorizontalSyncWidth  = 20;
    pConfig->timing.nHorizontalBackPorch  = 20;
    pConfig->timing.nHorizontalFrontPorch = 40;
    pConfig->timing.nVerticalActive       = 1280;
    pConfig->timing.nVerticalSyncWidth    = 4;
    pConfig->timing.nVerticalBackPorch    = 10;
    pConfig->timing.nVerticalFrontPorch   = 24;
    pConfig->timing.nFrameRateHz          = 40;
}

/*____________________
_/ the Startek KD020HVOIN002-C002B module on the Waveshare ESP32-P4-Nano */
/*
 * A 2.06 inch AMOLED, 410x502, driven by a Chipone CO5300.  The module's connector
 * (spec, pin assignment) brings out ONE data lane and the clock, so this is a
 * one-lane link; the CO5300 allows 500Mbps on one lane.  The reset (active low) and
 * the FMARK output are wired to GPIO 32 and 33 by the board, and the shell drives
 * the reset with the gpio command - there is no backlight to switch.
 *
 * The init order and values were found to light a CO5300 on this board (a 466x466
 * module, by another session): software reset, RGB565, a few vendor page registers
 * (FEh pages 20h and 80h), C2h = 0Bh for video mode, TE on, brightness, the address
 * window, sleep out, and display on a second later.  The vendor page writes are not
 * in the datasheet.  Only the address window is this module's own: 410 columns
 * starting at column 22 (0x16..0x1af) and 502 rows.  The column start is the offset
 * this panel size is known to need, but has not been confirmed on this module.
 */
static const u8 s_co5300P0[]  = { 0x00 };
static const u8 s_co5300P1[]  = { 0x55 };
static const u8 s_co5300P2[]  = { 0x20 };
static const u8 s_co5300P3[]  = { 0x5a };
static const u8 s_co5300P4[]  = { 0x59 };
static const u8 s_co5300P5[]  = { 0x80 };
static const u8 s_co5300P6[]  = { 0x0b };
static const u8 s_co5300P7[]  = { 0xff };
static const u8 s_co5300P8[]  = { 0xab };
static const u8 s_co5300P24[] = { 0x77 };

static const struct { u8 nCommand; u8 nParams; u16 nDelayMs; const u8 * pParams; } s_co5300Init[] = {
    { 0x01,  0,  100, NULL },                          /* software reset */
    { 0x36,  1,    0, s_co5300P0 },                    /* RGB order, no flip */
    { 0x3a,  1,    0, s_co5300P1 },                    /* 16 bits per pixel */
    { 0xfe,  1,    0, s_co5300P2 },
    { 0xf4,  1,    0, s_co5300P3 },
    { 0xf5,  1,    0, s_co5300P4 },
    { 0xfe,  1,    0, s_co5300P5 },
    { 0x03,  1,    0, s_co5300P0 },
    { 0xfe,  1,    0, s_co5300P0 },                    /* back to the user page */
    { 0xc2,  1,    0, s_co5300P6 },                    /* video mode */
    { 0x35,  1,    0, s_co5300P0 },                    /* TE on */
    { 0x53,  1,    0, s_co5300P2 },                    /* brightness control on */
    { 0x51,  1,    0, s_co5300P7 },
    { 0x63,  1,    0, s_co5300P8 },                    /* HBM brightness */
    { 0x11,  0,  100, NULL },                          /* sleep out */
    { 0x29,  0,   80, NULL },                          /* display on */
};

static bool DsiPanelCo5300(LTShell hShell, bool b24Bit) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    LTOThread * pThread = LT_GetCore()->GetCurrentThreadObject();

    for (u32 i = 0; i < sizeof(s_co5300Init) / sizeof(s_co5300Init[0]); ++i) {
        const u8 * pParams = s_co5300Init[i].pParams;
        if (b24Bit && s_co5300Init[i].nCommand == 0x3a) pParams = s_co5300P24;   /* 24 bits per pixel */
        if (! s_pDsi->API->DcsWrite(s_pDsi, s_co5300Init[i].nCommand, pParams, s_co5300Init[i].nParams)) {
            iShell->Print(hShell, "DcsWrite of 0x%02x failed (table entry %u)\n", (unsigned)s_co5300Init[i].nCommand, (unsigned)i);
            return false;
        }
        if (s_co5300Init[i].nDelayMs) pThread->API->Sleep(LTTime_Milliseconds(s_co5300Init[i].nDelayMs));
    }
    return true;
}

static void SetCo5300Config(LTDeviceMipiDsi_Config * pConfig) {
    lt_memset(pConfig, 0, sizeof(*pConfig));
    pConfig->mode             = kLTDeviceMipiDsi_Mode_VideoSyncPulse;
    pConfig->pixelFormat      = kLTDeviceMipiDsi_PixelFormat_RGB565;
    pConfig->nDataLanes       = 1;
    pConfig->nVirtualChannel  = 0;
    pConfig->nLaneRateMbps    = 490;
    pConfig->bContinuousClock = false;
    pConfig->bEnableEotPacket = true;

    /* The panel takes two host pixels for each of its own, so its 410 columns are 820 host
       pixels wide.  It shows an 820 pixel window onto each line, and (camera measured) the
       window starts about 52 pixels BEFORE the first pixel of the line: the last 52 pixels
       of the previous line show at the left edge and the line's own pixels 0..767 follow.
       That offset does not depend on the line width (it stayed at 48..56 for strides from
       864 to 932), so the line stride is chosen so that stride - width equals it: 872 - 820
       = 52, and the framebuffer view (width 820, offset stride - width) then has its (0,0)
       exactly at the left edge of the display.  Strides much above that only add pixels
       the panel never shows; the preset used 932 before this was understood.
       The porches are short on purpose: the driver rounds the pixel clock to
       160MHz / whole number, and the frame must stay on the 26.67MHz step (divider 6,
       between 24.6 and 29.1MHz); a long line pushes it onto the 32MHz step, which
       overcommits a 490Mbps lane and is unstable.  At 520 lines and 50fps this line
       (956 clocks) is 24.9MHz and rounds to the 26.67MHz step. */
    pConfig->timing.nHorizontalActive     = 872;
    pConfig->timing.nHorizontalSyncWidth  = 4;
    pConfig->timing.nHorizontalBackPorch  = 40;
    pConfig->timing.nHorizontalFrontPorch = 40;
    /* The panel's video path shows 480 lines, not the 502 of the glass: with 502 active
       lines the 22 lines it cannot take show as a cyan strip at the top and bottom (split
       by the back porch), and the picture is rotated vertically.  With 480 active lines and
       a back porch of 18 the picture fills the whole display.  Measured with a camera. */
    pConfig->timing.nVerticalActive       = 480;
    pConfig->timing.nVerticalSyncWidth    = 4;
    pConfig->timing.nVerticalBackPorch    = 18;
    pConfig->timing.nVerticalFrontPorch   = 18;
    pConfig->timing.nFrameRateHz          = 50;
}

/* The CO5300 datasheet (page 8) caps the bit rate by pixel format: 500Mbps with
   24-bit pixels, 360 with 18-bit and only 320 with 16-bit.  The RGB565 preset above
   runs the lane at 490Mbps, past that cap, which may be why its picture is unstable.
   This variant sends 24-bit pixels (use "dsi panel co5300 888" to match), which allows
   the full rate.  The 2:1 host-pixel factor of the RGB565 preset may not apply, so the
   line is the panel's own 410 pixels.  TRIED, DOES NOT WORK: at 410 the left half is
   black and the right half is one-pixel bars; at 820 and 30fps the left half is green
   with black lines and the bars are wrong.  Kept only as a starting point. */
static void SetCo5300Rgb888Config(LTDeviceMipiDsi_Config * pConfig) {
    SetCo5300Config(pConfig);
    pConfig->pixelFormat                   = kLTDeviceMipiDsi_PixelFormat_RGB888;
    pConfig->timing.nHorizontalActive      = 410;
    pConfig->timing.nHorizontalBackPorch   = 50;
    pConfig->timing.nHorizontalFrontPorch  = 60;
    pConfig->timing.nFrameRateHz           = 60;
}


/* ---- dsi te: where the panel's frame pulses fall in the host's frame --------------------
   The CO5300 drives its tearing-effect output (FMARK, GPIO 33) once per panel frame once
   0x35 has been sent.  This watches that pad and, at each rising edge, reads the DMA's
   source address, which says how far through the framebuffer the host has read.  The
   same fraction of the frame at every edge means the two frames run in step; the value
   itself says how far apart they are.  Diagnostic only; the register dump adds a few
   tens of microseconds between the edge and the sample. */
typedef struct { u32 nSourceAddress; bool bFound; } TeSample;

static void TeRegisterProc(const char * pName, u32 nAddress, u32 nValue, void * pClientData) {
    LT_UNUSED(nAddress);
    TeSample * pSample = (TeSample *)pClientData;
    if (0 == lt_strcmp(pName, "GDMA_CH0_SAR0")) { pSample->nSourceAddress = nValue; pSample->bFound = true; }
}

static LTDeviceGpio * s_pTeGpio = NULL;

/* The edge samples, kept out of the shell's 320 byte callback stack.  Only one "dsi te" may
   use them at a time. */
enum { kTeMaxEdges = 24 };
static struct {
    s64  nRiseNs[kTeMaxEdges];
    s64  nFallNs[kTeMaxEdges];
    u32  nSar[kTeMaxEdges];
    bool bBusy;
} s_te;

static int DsiTe(LTShell hShell, int argc, const char ** argv) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    enum { kPad = 33, kMaxEdges = kTeMaxEdges };
    u32 nMillis = 400;
    if (argc > 2 && ! ParseU32(argv[2], &nMillis)) { iShell->PutString(hShell, "usage: dsi te [milliseconds]\n"); return -1; }
    if (s_pFrameBuffer == NULL || ! s_pDsi->API->IsVideoActive(s_pDsi)) {
        iShell->PutString(hShell, "video must be running with a framebuffer\n");
        return -1;
    }
    if (s_pTeGpio == NULL) s_pTeGpio = lt_createdeviceobject(LTDeviceGpio);
    if (s_pTeGpio == NULL) { iShell->PutString(hShell, "no LTDeviceGpio on this platform\n"); return -1; }
    s_pTeGpio->API->SetGpioModeFromIndex(s_pTeGpio, kPad, kLTDeviceGpio_ModeType_Input);

    if (s_te.bBusy) { iShell->PutString(hShell, "another dsi te is running\n"); return -1; }
    s_te.bBusy = true;

    LTCore * pCore = LT_GetCore();
    s64 * nRiseNs = s_te.nRiseNs, * nFallNs = s_te.nFallNs;
    u32 * nSar    = s_te.nSar;
    u32 nRises = 0, nFalls = 0;
    bool bPrev = s_pTeGpio->API->GetInputValue(s_pTeGpio, kPad);
    bool bStartLevel = bPrev;
    s64 nStart = pCore->GetKernelTime().nNanoseconds;
    s64 nEnd   = nStart + (s64)nMillis * 1000000;
    while (pCore->GetKernelTime().nNanoseconds < nEnd) {
        bool bNow = s_pTeGpio->API->GetInputValue(s_pTeGpio, kPad);
        if (bNow == bPrev) continue;
        s64 nWhen = pCore->GetKernelTime().nNanoseconds;
        if (bNow && nRises < kMaxEdges) {
            TeSample sample = { 0, false };
            s_pDsi->API->DumpRegisters(s_pDsi, TeRegisterProc, &sample);
            nRiseNs[nRises] = nWhen;
            nSar[nRises]    = sample.bFound ? sample.nSourceAddress : 0;
            ++nRises;
        } else if (! bNow && nFalls < kMaxEdges) {
            nFallNs[nFalls++] = nWhen;
        }
        bPrev = bNow;
    }
    iShell->Print(hShell, "TE pad %u: start level %u, %u rising and %u falling edges in %u ms\n",
                  (unsigned)kPad, (unsigned)bStartLevel, (unsigned)nRises, (unsigned)nFalls, (unsigned)nMillis);
    if (nRises < 2) {
        iShell->PutString(hShell, "no TE pulses seen; is the TE output on (0x35) and the pad wired?\n");
        s_te.bBusy = false;
        return 0;
    }
    u32 nBase = (u32)(LT_SIZE)s_pFrameBuffer;
    u64 nPeriodSum = 0, nPhaseSum = 0;
    for (u32 i = 0; i < nRises; ++i) {
        u32 nOffset = (nSar[i] - nBase) % s_nFrameBufferBytes;
        u32 nPhasePermille = (u32)(((u64)nOffset * 1000) / s_nFrameBufferBytes);
        s64 nPeriodUs = (i > 0) ? (nRiseNs[i] - nRiseNs[i - 1]) / 1000 : 0;
        s64 nWidthUs = -1;
        for (u32 j = 0; j < nFalls; ++j) if (nFallNs[j] > nRiseNs[i]) { nWidthUs = (nFallNs[j] - nRiseNs[i]) / 1000; break; }
        iShell->Print(hShell, "  edge %2u  period %6d us  width %6d us  host read position %3u.%u%% of frame\n",
                      (unsigned)i, (int)nPeriodUs, (int)nWidthUs, (unsigned)(nPhasePermille / 10), (unsigned)(nPhasePermille % 10));
        if (i > 0) nPeriodSum += (u64)nPeriodUs;
        nPhaseSum += nPhasePermille;
    }
    iShell->Print(hShell, "mean period %u us, mean host position %u.%u%%\n",
                  (unsigned)(nPeriodSum / (nRises - 1)), (unsigned)(nPhaseSum / nRises / 10), (unsigned)((nPhaseSum / nRises) % 10));
    s_te.bBusy = false;
    return 0;
}

static int DsiConfig(LTShell hShell, int argc, const char ** argv) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    u32 nValue = 0;

    if (argc < 3) { DsiShow(hShell); return 0; }

    if (0 == lt_strcmp(argv[2], "reset")) {
        SetDefaultConfig(&s_config);
        s_viewWidth = 0; s_viewOffset = kAutoOffset;
        iShell->PutString(hShell, "working config reset to the default panel\n");
        return 0;
    }

    if (argc < 4) { ShellHelpDsi(hShell, argc, argv); return -1; }

    if (0 == lt_strcmp(argv[2], "preset")) {
        if (0 == lt_strcmp(argv[3], "hx8394")) {
            SetHx8394Config(&s_config);
            s_viewWidth = 0; s_viewOffset = kAutoOffset;
            iShell->PutString(hShell, "working config set to the Waveshare ESP32-P4-WIFI6-Touch-LCD-5 panel (HX8394, 720x1280, 2 lanes, 700Mbps)\n");
            return 0;
        }
        if (0 == lt_strcmp(argv[3], "co5300")) {
            SetCo5300Config(&s_config);
            /* The panel shows an 820 pixel window onto each line (see SetCo5300Config); the offset
               is stride - width = 52, which is where the window starts. */
            s_viewWidth = 820; s_viewOffset = kAutoOffset;
            iShell->PutString(hShell, "working config set to the Startek KD020HVOIN002-C002B module (CO5300, 410x502 panel in a 872x480 host frame, view 820 wide, 1 lane, 490Mbps)\n");
            return 0;
        }
        if (0 == lt_strcmp(argv[3], "co5300rgb888")) {
            SetCo5300Rgb888Config(&s_config);
            s_viewWidth = 0; s_viewOffset = kAutoOffset;
            iShell->PutString(hShell, "working config set to the Startek module with 24-bit pixels (410x502, 1 lane, 490Mbps); use dsi panel co5300 888\n");
            return 0;
        }
        iShell->Print(hShell, "no such preset: %s\n", argv[3]);
        return -1;
    }

    if (0 == lt_strcmp(argv[2], "view")) {
        /* dsi config view <width> [<offset>|auto]: the logical framebuffer.  The line stride is
           the active width of the config; width is how much of each line the panel shows;
           offset is how many pixels before the first pixel of a line the panel's window
           starts (auto: stride - width).  Takes effect at the next "dsi fb alloc". */
        u32 nWidth = 0, nOffset = 0;
        if (! ParseU32(argv[3], &nWidth)) { iShell->Print(hShell, "not a number: %s\n", argv[3]); return -1; }
        if (nWidth == 0 || nWidth > s_config.timing.nHorizontalActive) {
            iShell->Print(hShell, "width must be 1..%u (the line stride)\n", (unsigned)s_config.timing.nHorizontalActive);
            return -1;
        }
        if (argc > 4 && 0 != lt_strcmp(argv[4], "auto")) {
            if (! ParseU32(argv[4], &nOffset)) { iShell->Print(hShell, "not a number: %s\n", argv[4]); return -1; }
            s_viewOffset = nOffset;
        } else {
            s_viewOffset = kAutoOffset;
        }
        s_viewWidth = nWidth;
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "mode")) {
        LTDeviceMipiDsi_Mode mode = ParseMode(argv[3]);
        if (mode == kLTDeviceMipiDsi_Mode_Unset) { iShell->Print(hShell, "no such mode: %s\n", argv[3]); return -1; }
        s_config.mode = mode;
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "format")) {
        LTDeviceMipiDsi_PixelFormat format = ParseFormat(argv[3]);
        if (format == kLTDeviceMipiDsi_PixelFormat_Unset) { iShell->Print(hShell, "no such format: %s\n", argv[3]); return -1; }
        s_config.pixelFormat = format;
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "clock")) {
        s_config.bContinuousClock = (0 == lt_strcmp(argv[3], "continuous"));
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "eotp")) {
        s_config.bEnableEotPacket = (0 == lt_strcmp(argv[3], "on"));
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "h") || 0 == lt_strcmp(argv[2], "v")) {
        if (argc < 7) { ShellHelpDsi(hShell, argc, argv); return -1; }
        u32 nTiming[4];
        for (int i = 0; i < 4; ++i) {
            if (! ParseU32(argv[3 + i], &nTiming[i])) { iShell->Print(hShell, "not a number: %s\n", argv[3 + i]); return -1; }
        }
        if (argv[2][0] == 'h') {
            s_config.timing.nHorizontalActive     = nTiming[0];
            s_config.timing.nHorizontalSyncWidth  = nTiming[1];
            s_config.timing.nHorizontalBackPorch  = nTiming[2];
            s_config.timing.nHorizontalFrontPorch = nTiming[3];
        } else {
            s_config.timing.nVerticalActive       = nTiming[0];
            s_config.timing.nVerticalSyncWidth    = nTiming[1];
            s_config.timing.nVerticalBackPorch    = nTiming[2];
            s_config.timing.nVerticalFrontPorch   = nTiming[3];
        }
        return 0;
    }

    if (! ParseU32(argv[3], &nValue)) { iShell->Print(hShell, "not a number: %s\n", argv[3]); return -1; }

    if (0 == lt_strcmp(argv[2], "lanes")) { s_config.nDataLanes      = nValue; return 0; }
    if (0 == lt_strcmp(argv[2], "vc"))    { s_config.nVirtualChannel = nValue; return 0; }
    if (0 == lt_strcmp(argv[2], "rate"))  { s_config.nLaneRateMbps   = nValue; return 0; }
    if (0 == lt_strcmp(argv[2], "fps"))   { s_config.timing.nFrameRateHz = nValue; return 0; }

    ShellHelpDsi(hShell, argc, argv);
    return -1;
}

static int DsiFrameBuffer(LTShell hShell, int argc, const char ** argv) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);

    if (argc < 3) { ShellHelpDsi(hShell, argc, argv); return -1; }

    if (0 == lt_strcmp(argv[2], "free")) {
        if (! FreeFrameBuffer()) {
            iShell->PutString(hShell, "video did not stop; the framebuffer was kept\n");
            return -1;
        }
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "alloc")) {
        u32 nBytes = FrameBufferBytes(&s_config);
        if (argc > 3 && ! ParseU32(argv[3], &nBytes)) { iShell->Print(hShell, "not a number: %s\n", argv[3]); return -1; }
        if (! AllocFrameBuffer(nBytes)) { iShell->Print(hShell, "cannot allocate %u bytes\n", (unsigned)nBytes); return -1; }
        iShell->Print(hShell, "%u bytes at %lx (view %u of stride %u, %u pixels of margin in front)\n",
                      (unsigned)s_nFrameBufferBytes, (unsigned long)(LT_SIZE)s_pFrameBuffer,
                      (unsigned)ViewWidth(), (unsigned)s_config.timing.nHorizontalActive, (unsigned)ViewOffset());
        if (! s_pDsi->API->SetFrameBuffer(s_pDsi, s_pFrameBuffer, s_nFrameBufferBytes)) {
            iShell->PutString(hShell, "the device would not take it\n");
            FreeFrameBuffer();
            return -1;
        }
        return 0;
    }

    if (s_pFrameBuffer == NULL) { iShell->PutString(hShell, "no framebuffer; 'dsi fb alloc' first\n"); return -1; }

    /* Only the two formats whose pixels are a whole number of bytes are filled
       here; the packed ones would need the bit position of each pixel. */
    u32 nBitsPerPixel = s_geom.nBitsPerPixel;
    if (nBitsPerPixel != 16 && nBitsPerPixel != 24) {
        iShell->Print(hShell, "cannot fill a %s framebuffer\n", FormatName(s_geom.format));
        return -1;
    }
    u32 nBytesPerPixel = nBitsPerPixel / 8;
    u32 nPixels        = s_nFrameBufferBytes / nBytesPerPixel;

    /* Everything but the plain pixel loops addresses the buffer by the frame's geometry, so
       refuse those when an allocation of a chosen size does not hold the whole frame. */
    if (   0 != lt_strcmp(argv[2], "fill") && 0 != lt_strcmp(argv[2], "fillchunked")
        && 0 != lt_strcmp(argv[2], "touch") && 0 != lt_strcmp(argv[2], "reswap")
        && (u64)s_geom.nStride * s_geom.nHeight * nBytesPerPixel > s_nFrameBufferBytes) {
        iShell->PutString(hShell, "the framebuffer is smaller than the frame it was allocated for\n");
        return -1;
    }

    if (0 == lt_strcmp(argv[2], "fill")) {
        u32 nColour = 0;
        if (argc < 4 || ! ParseU32(argv[3], &nColour)) { iShell->PutString(hShell, "usage: dsi fb fill <colour>\n"); return -1; }
        for (u32 i = 0; i < nPixels; ++i) {
            u8 * pPixel = &s_pFrameBuffer[i * nBytesPerPixel];
            pPixel[0] = (u8)(nColour & 0xff);
            pPixel[1] = (u8)((nColour >> 8) & 0xff);
            if (nBytesPerPixel == 3) pPixel[2] = (u8)((nColour >> 16) & 0xff);
        }
        iShell->Print(hShell, "filled %u pixels\n", (unsigned)nPixels);
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "fillchunked")) {
        /* Like 'fill', but the CPU write burst is split into chunks with a sleep between them, to limit contention with the GDMA reads. */
        u32 nColour = 0, nChunkPixels = 0, nSleepMs = 0;
        if (argc < 6
         || ! ParseU32(argv[3], &nColour)
         || ! ParseU32(argv[4], &nChunkPixels) || ! nChunkPixels
         || ! ParseU32(argv[5], &nSleepMs)) {
            iShell->PutString(hShell, "usage: dsi fb fillchunked <colour> <chunk pixels> <sleep ms>\n");
            return -1;
        }
        LTOThread *pThread = LT_GetCore()->GetCurrentThreadObject();
        u32 nSinceYield = 0;
        for (u32 i = 0; i < nPixels; ++i) {
            u8 * pPixel = &s_pFrameBuffer[i * nBytesPerPixel];
            pPixel[0] = (u8)(nColour & 0xff);
            pPixel[1] = (u8)((nColour >> 8) & 0xff);
            if (nBytesPerPixel == 3) pPixel[2] = (u8)((nColour >> 16) & 0xff);
            if (++nSinceYield >= nChunkPixels) {
                nSinceYield = 0;
                pThread->API->Sleep(LTTime_Milliseconds(nSleepMs));
            }
        }
        iShell->Print(hShell, "filled %u pixels in chunks of %u, %u ms apart\n",
                      (unsigned)nPixels, (unsigned)nChunkPixels, (unsigned)nSleepMs);
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "touch")) {
        /* Repeatedly overwrites the first <pixels> pixels of the live buffer with no sleep between repeats and no SetFrameBuffer call. */
        u32 nTouchPixels = 0, nRepeats = 0;
        if (argc < 5 || ! ParseU32(argv[3], &nTouchPixels) || ! nTouchPixels
         || ! ParseU32(argv[4], &nRepeats) || ! nRepeats
         || nTouchPixels > nPixels) {
            iShell->PutString(hShell, "usage: dsi fb touch <pixels> <repeats>\n");
            return -1;
        }
        for (u32 r = 0; r < nRepeats; ++r) {
            u32 nColour = (r & 1) ? 0xf800 : 0x001f;
            for (u32 i = 0; i < nTouchPixels; ++i) {
                u8 * pPixel = &s_pFrameBuffer[i * nBytesPerPixel];
                pPixel[0] = (u8)(nColour & 0xff);
                pPixel[1] = (u8)((nColour >> 8) & 0xff);
                if (nBytesPerPixel == 3) pPixel[2] = (u8)((nColour >> 16) & 0xff);
            }
        }
        iShell->Print(hShell, "touched %u pixels, %u times\n",
                      (unsigned)nTouchPixels, (unsigned)nRepeats);
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "swaploop2")) {
        /* Per iteration, writes a <rows> x <cols> block at <row> into the current ping-pong buffer, calls SetFrameBuffer, then sleeps <delay ms>; runs natively on the device so the pacing matches 'animate'. */
        u32 nHeight = s_geom.nHeight;
        u32 nWidth  = s_geom.nStride;
        if (! s_pDsi->API->IsVideoActive(s_pDsi)) {
            iShell->PutString(hShell, "video is not active\n");
            return -1;
        }
        u32 nRow = 0, nRows = 0, nCols = 0, nRepeats = 0, nDelayMs = 0;
        if (argc < 8 || nWidth == 0 || nHeight == 0
         || ! ParseU32(argv[3], &nRow)
         || ! ParseU32(argv[4], &nRows) || ! nRows || nRow + nRows > nHeight
         || ! ParseU32(argv[5], &nCols) || ! nCols || nCols > nWidth
         || ! ParseU32(argv[6], &nRepeats) || ! nRepeats
         || ! ParseU32(argv[7], &nDelayMs)) {
            iShell->PutString(hShell, "usage: dsi fb swaploop2 <row> <rows> <cols> <repeats> <delay ms>\n");
            return -1;
        }
        if (! AnimationBuffer()) {
            iShell->PutString(hShell, "cannot allocate the second framebuffer\n");
            return -1;
        }
        LTOThread *pThread = LT_GetCore()->GetCurrentThreadObject();
        for (u32 r = 0; r < nRepeats; ++r) {
            u8 *pDrawBuffer = (r & 1) ? s_pAnimationFrameBuffer : s_pFrameBuffer;
            u32 nColour = (r & 1) ? 0xf800 : 0x001f;
            for (u32 y = nRow; y < nRow + nRows; ++y) {
                for (u32 x = 0; x < nCols; ++x) {
                    u32 i = y * nWidth + x;
                    u8 * pPixel = &pDrawBuffer[i * nBytesPerPixel];
                    pPixel[0] = (u8)(nColour & 0xff);
                    pPixel[1] = (u8)((nColour >> 8) & 0xff);
                    if (nBytesPerPixel == 3) pPixel[2] = (u8)((nColour >> 16) & 0xff);
                }
            }
            u32 nWaits = 0;
            while (! s_pDsi->API->SetFrameBuffer(s_pDsi, pDrawBuffer, s_nFrameBufferBytes)
                   && nWaits++ < 100) {
                pThread->API->Sleep(LTTime_Milliseconds(1));
            }
            if (nWaits > 100) {
                iShell->PutString(hShell, "framebuffer swap timed out\n");
                return -1;
            }
            pThread->API->Sleep(LTTime_Milliseconds(nDelayMs));
        }
        iShell->Print(hShell, "swaplooped row=%u rows=%u cols=%u, %u times, %u ms apart\n",
                      (unsigned)nRow, (unsigned)nRows, (unsigned)nCols,
                      (unsigned)nRepeats, (unsigned)nDelayMs);
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "dumprow")) {
        /* Diagnostic only: print the colour (rgb565) at <cols> consecutive
           x positions starting at <x>, for <rows> consecutive y positions
           starting at <y> - one line per row, so a boundary that should be
           a clean vertical edge can be checked for being crisp in the
           stored framebuffer memory itself (as opposed to a tearing/timing
           artifact that only shows up on the physical panel). */
        u32 nWidth  = s_geom.nStride;
        u32 nHeight = s_geom.nHeight;
        u32 x0 = 0, y0 = 0, nCols = 0, nRows = 0;
        if (argc < 7 || nWidth == 0
         || ! ParseU32(argv[3], &x0) || ! ParseU32(argv[4], &y0)
         || ! ParseU32(argv[5], &nCols) || ! nCols
         || ! ParseU32(argv[6], &nRows) || ! nRows) {
            iShell->PutString(hShell, "usage: dsi fb dumprow <x> <y> <cols> <rows>\n");
            return -1;
        }
        /* The whole rectangle must lie inside the frame; compared by subtraction so no sum can wrap. */
        if (x0 >= nWidth || nCols > nWidth - x0 || y0 >= nHeight || nRows > nHeight - y0) {
            iShell->Print(hShell, "the rectangle must lie inside the %ux%u frame\n", (unsigned)nWidth, (unsigned)nHeight);
            return -1;
        }
        for (u32 r = 0; r < nRows; ++r) {
            iShell->Print(hShell, "y=%u:", (unsigned)(y0 + r));
            for (u32 c = 0; c < nCols; ++c) {
                u32 i = (y0 + r) * nWidth + (x0 + c);
                u8 * pPixel = &s_pFrameBuffer[i * nBytesPerPixel];
                u16 nColour = pPixel[0] | (pPixel[1] << 8);
                iShell->Print(hShell, " %04x", (unsigned)nColour);
            }
            iShell->PutString(hShell, "\n");
        }
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "bars")) {
        /* Eight vertical bars across the visible width of the view (see "dsi config view"),
           so a panel that scans out at all shows whether the line length, the pixel
           format and the view agree with what it expected. */
        static const u32 s_barColours16[8] = { 0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f, 0x0000 };
        static const u32 s_barColours24[8] = { 0xffffff, 0x00ffff, 0xffff00, 0x00ff00, 0xff00ff, 0x0000ff, 0xff0000, 0x000000 };
        FbView v = MakeView(s_pFrameBuffer);
        if (v.nWidth == 0) { iShell->PutString(hShell, "the working config has no width\n"); return -1; }
        lt_memset(s_pFrameBufferAlloc, 0, s_nMarginBytes + s_nFrameBufferBytes);
        for (u32 y = 0; y < v.nHeight; ++y) {
            for (u32 x = 0; x < v.nWidth; ++x) {
                u32 nBar    = (x * 8) / v.nWidth;
                u32 nColour = (nBytesPerPixel == 2) ? s_barColours16[nBar] : s_barColours24[nBar];
                u8 * pPixel = ViewPixel(&v, x, y);
                pPixel[0] = (u8)(nColour & 0xff);
                pPixel[1] = (u8)((nColour >> 8) & 0xff);
                if (nBytesPerPixel == 3) pPixel[2] = (u8)((nColour >> 16) & 0xff);
            }
        }
        iShell->Print(hShell, "colour bars over %ux%u (stride %u, offset %u)\n",
                      (unsigned)v.nWidth, (unsigned)v.nHeight, (unsigned)v.nStride, (unsigned)v.nOffset);
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "grid")) {
        if (nBytesPerPixel != 2) {
            iShell->PutString(hShell, "the grid currently requires rgb565\n");
            return -1;
        }
        FbView v = MakeView(s_pFrameBuffer);
        lt_memset(s_pFrameBufferAlloc, 0, s_nMarginBytes + s_nFrameBufferBytes);
        for (u32 y = 0; y < v.nHeight; ++y) {
            for (u32 x = 0; x < v.nWidth; ++x) {
                u16 nColour = (x < v.nWidth / 2) ? 0x4000 : 0x0010;
                if ((x % 32) == 0) nColour = 0xffff;
                if ((y % 32) == 0) nColour = 0x8410;
                if (x + 1 >= v.nWidth / 2 && x <= v.nWidth / 2 + 1) nColour = 0xffe0;
                ViewPut16(&v, x, y, nColour);
            }
        }
        iShell->Print(hShell, "32-pixel grid over %ux%u (stride %u, offset %u)\n",
                      (unsigned)v.nWidth, (unsigned)v.nHeight, (unsigned)v.nStride, (unsigned)v.nOffset);
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "stripes") || 0 == lt_strcmp(argv[2], "hstripes") || 0 == lt_strcmp(argv[2], "grid2")) {
        /* Test patterns with features thick enough for a low resolution camera, drawn
           through the view:
           stripes <n>  - vertical white/black stripes, each n pixels wide
           hstripes <n> - horizontal white/black stripes, each n lines tall
           grid2        - 8 pixel wide white lines every 64 pixels, 4 line tall grey lines
                          every 32 lines, a yellow line down the middle, and a dark red left
                          half, so the scale and position of the picture can be measured. */
        if (nBytesPerPixel != 2) { iShell->PutString(hShell, "these patterns require rgb565\n"); return -1; }
        FbView v = MakeView(s_pFrameBuffer);
        u32 nSize   = 8;
        bool bGrid  = (0 == lt_strcmp(argv[2], "grid2"));
        bool bHoriz = (0 == lt_strcmp(argv[2], "hstripes"));
        if (! bGrid && (argc < 4 || ! ParseU32(argv[3], &nSize) || nSize == 0)) {
            iShell->Print(hShell, "usage: dsi fb %s <size>\n", argv[2]);
            return -1;
        }
        lt_memset(s_pFrameBufferAlloc, 0, s_nMarginBytes + s_nFrameBufferBytes);
        for (u32 y = 0; y < v.nHeight; ++y) {
            for (u32 x = 0; x < v.nWidth; ++x) {
                u16 nColour;
                if (bGrid) {
                    nColour = (x < v.nWidth / 2) ? 0x4000 : 0x0010;
                    if ((x % 64) < 8) nColour = 0xffff;
                    if ((y % 32) < 4) nColour = 0x8410;
                    if (x + 2 >= v.nWidth / 2 && x <= v.nWidth / 2 + 2) nColour = 0xffe0;
                } else {
                    u32 nIndex = bHoriz ? (y / nSize) : (x / nSize);
                    nColour = (nIndex & 1u) ? 0x0000 : 0xffff;
                }
                ViewPut16(&v, x, y, nColour);
            }
        }
        iShell->Print(hShell, "%s over %ux%u\n", argv[2], (unsigned)v.nWidth, (unsigned)v.nHeight);
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "reswap")) {
        /* Diagnostic only: re-submits the buffer already on screen through
           SetFrameBuffer unchanged, so a mid-stream swap can be exercised -
           descriptor rebuild, cache flush, frame-boundary rearm and all -
           without the pixel content or the physical address moving.  Useful
           for telling a swap-bookkeeping cost apart from an address-change
           cost (eg. a PSRAM bank/row switch) when the two are otherwise
           always bundled together, as they are in 'dsi fb animate'. */
        if (! s_pDsi->API->IsVideoActive(s_pDsi)) {
            iShell->PutString(hShell, "video is not active; nothing to reswap\n");
            return -1;
        }
        LTOThread *pThread = LT_GetCore()->GetCurrentThreadObject();
        u32 nWaits = 0;
        while (! s_pDsi->API->SetFrameBuffer(s_pDsi, s_pFrameBuffer, s_nFrameBufferBytes)
               && nWaits++ < 100) {
            pThread->API->Sleep(LTTime_Milliseconds(1));
        }
        if (nWaits > 100) {
            iShell->PutString(hShell, "framebuffer swap timed out\n");
            return -1;
        }
        iShell->Print(hShell, "reswapped the same %u byte buffer at %lx (diagnostic only)\n",
                      (unsigned)s_nFrameBufferBytes, (unsigned long)(LT_SIZE)s_pFrameBuffer);
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "animate")) {
        if (nBytesPerPixel != 2) {
            iShell->PutString(hShell, "the animation currently requires rgb565\n");
            return -1;
        }

        u32 nFrames = 300;
        if (argc > 3 && (! ParseU32(argv[3], &nFrames) || ! nFrames)) {
            iShell->PutString(hShell, "usage: dsi fb animate [frames]\n");
            return -1;
        }

        FbView view0 = MakeView(s_pFrameBuffer);
        u32 nWidth  = view0.nWidth;
        u32 nHeight = view0.nHeight;
        if (! AnimationBuffer()) {
            iShell->PutString(hShell, "cannot allocate the second framebuffer\n");
            return -1;
        }
        FbView view1 = MakeView(s_pAnimationFrameBuffer);
        u32 nSquare = ((nWidth < nHeight) ? nWidth : nHeight) / 8;
        u32 nTravel = nWidth - nSquare;
        u32 nPeriod = nTravel ? nTravel * 2 : 1;
        u32 nTop    = (nHeight - nSquare) / 2;
        static const u16 s_background[8] = {
            0xf800, 0xfd20, 0xffe0, 0x07e0, 0x07ff, 0x001f, 0x801f, 0xf81f
        };
        LTOThread *pThread = LT_GetCore()->GetCurrentThreadObject();

        /* Per (ping-pong) buffer dirty-rectangle tracking - see below. */
        u32  nLastLeft[2]    = { 0, 0 };
        bool bInitialized[2] = { false, false };

        iShell->Print(hShell, "animating %u frames\n", (unsigned)nFrames);
        for (u32 nFrame = 0; nFrame < nFrames; ++nFrame) {
            u32 nBuf = nFrame & 1;
            u8 *pDrawBuffer = nBuf ? s_pAnimationFrameBuffer : s_pFrameBuffer;
            const FbView * pView = nBuf ? &view1 : &view0;
            u32 nPhase = (nFrame * 8) % nPeriod;
            u32 nLeft  = (nPhase <= nTravel) ? nPhase : nPeriod - nPhase;

            if (! bInitialized[nBuf]) {
                /* First touch of this buffer this invocation: draw the whole
                   frame, same as the original unconditional loop below. */
                for (u32 y = 0; y < nHeight; ++y) {
                    for (u32 x = 0; x < nWidth; ++x) {
                        u16 nColour = s_background[(x * 8) / nWidth];
                        if (x >= nLeft && x < nLeft + nSquare
                         && y >= nTop && y < nTop + nSquare) {
                            nColour = 0xffff;
                        }
                        ViewPut16(pView, x, y, nColour);
                    }
                }
                bInitialized[nBuf] = true;
            } else {
                /* Dirty-rectangle update.  The background bars never change and the square
                   only occupies rows [nTop, nTop + nSquare), so only the edge it vacates and
                   the edge it enters differ from what this buffer already holds.  Writing
                   less keeps CPU stores from starving the DMA's PSRAM reads. */
                u32 nPrevLeft = nLastLeft[nBuf];
                u32 nOldEnd   = nPrevLeft + nSquare;
                u32 nNewEnd   = nLeft + nSquare;
                u32 vLo, vHi, eLo, eHi;
                if (nLeft > nPrevLeft) {
                    /* Moved right: vacated the left edge, entered the right. */
                    vLo = nPrevLeft;
                    vHi = (nLeft < nOldEnd) ? nLeft : nOldEnd;
                    eLo = (nOldEnd > nLeft) ? nOldEnd : nLeft;
                    eHi = nNewEnd;
                } else {
                    /* Moved left: vacated the right edge, entered the left. */
                    vLo = (nNewEnd > nPrevLeft) ? nNewEnd : nPrevLeft;
                    vHi = nOldEnd;
                    eLo = nLeft;
                    eHi = (nPrevLeft < nNewEnd) ? nPrevLeft : nNewEnd;
                }
                for (u32 y = nTop; y < nTop + nSquare; ++y) {
                    for (u32 x = vLo; x < vHi; ++x) {
                        ViewPut16(pView, x, y, s_background[(x * 8) / nWidth]);
                    }
                    for (u32 x = eLo; x < eHi; ++x) {
                        ViewPut16(pView, x, y, 0xffff);
                    }
                }
            }
            nLastLeft[nBuf] = nLeft;

            if (s_pDsi->API->IsVideoActive(s_pDsi)) {
                u32 nWaits = 0;
                while (! s_pDsi->API->SetFrameBuffer(
                           s_pDsi, pDrawBuffer, s_nFrameBufferBytes)
                       && nWaits++ < 100) {
                    pThread->API->Sleep(LTTime_Milliseconds(1));
                }
                if (nWaits > 100) {
                    iShell->PutString(hShell, "framebuffer swap timed out\n");
                    return -1;
                }
            } else if (! s_pDsi->API->StartVideo(s_pDsi)) {
                iShell->PutString(hShell, "video start failed during animation\n");
                return -1;
            }
            /* fixed frame delay */
            pThread->API->Sleep(LTTime_Milliseconds(40));
        }
        iShell->PutString(hShell, "animation complete\n");
        return 0;
    }

    ShellHelpDsi(hShell, argc, argv);
    return -1;
}

/* Collects argv[nFirst..argc-1] as bytes into s_packetBuffer.  Returns -1 if
   any of them is not a number or there are more than the buffer holds. */
static int CollectBytes(LTShell hShell, int argc, const char ** argv, int nFirst) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    int nBytes = 0;
    for (int i = nFirst; i < argc; ++i) {
        u32 nValue = 0;
        if (nBytes >= kMaxPacketBytes) { iShell->Print(hShell, "at most %d bytes\n", kMaxPacketBytes); return -1; }
        if (! ParseU32(argv[i], &nValue) || nValue > 0xff) { iShell->Print(hShell, "not a byte: %s\n", argv[i]); return -1; }
        s_packetBuffer[nBytes++] = (u8)nValue;
    }
    return nBytes;
}

static void PrintReadBytes(LTShell hShell, s32 nRead) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    if (nRead < 0) { iShell->PutString(hShell, "read failed\n"); return; }
    iShell->Print(hShell, "%d bytes:", (int)nRead);
    for (s32 i = 0; i < nRead; ++i) iShell->Print(hShell, " %02x", (unsigned)s_readBuffer[i]);
    iShell->PutString(hShell, "\n");
}

/*____________________
_/ selftest */

/* Scored: the interface owes this answer whatever is on the other end of the
   link.  A failure here is the device, the driver, or the hardware. */
#define CHECK(cond, ...) do {                                        \
        bool bOk = (cond);                                           \
        if (! bOk) ++nFailures;                                      \
        iShell->Print(hShell, "  [%s] ", bOk ? "pass" : "FAIL");     \
        iShell->Print(hShell, __VA_ARGS__);                          \
        iShell->Print(hShell, "\n");                                 \
    } while (0)

/* Unscored: only a panel could say whether this was right. */
#define NOTE(...) do {                                               \
        iShell->Print(hShell, "  [note] ");                          \
        iShell->Print(hShell, __VA_ARGS__);                          \
        iShell->Print(hShell, "\n");                                 \
    } while (0)

/* Everything the interface refuses before a configuration has been applied. */
static int SelftestUnconfigured(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    int nFailures = 0;

    iShell->PutString(hShell, "unconfigured device\n");
    CHECK(! s_pDsi->API->GetConfig(s_pDsi, &s_scratch),            "GetConfig reports no configuration");
    CHECK(! s_pDsi->API->GetConfig(s_pDsi, NULL),                  "GetConfig refuses a null destination");
    CHECK(! s_pDsi->API->IsPoweredOn(s_pDsi),                      "not powered on");
    CHECK(! s_pDsi->API->IsVideoActive(s_pDsi),                    "no pixel stream");
    CHECK(! s_pDsi->API->PowerOn(s_pDsi),                          "PowerOn refused");
    CHECK(! s_pDsi->API->StartVideo(s_pDsi),                       "StartVideo refused");
    CHECK(! s_pDsi->API->DcsWrite(s_pDsi, 0x11, NULL, 0),          "DcsWrite refused");
    CHECK(s_pDsi->API->DcsRead(s_pDsi, 0x0a, s_readBuffer, sizeof s_readBuffer) < 0, "DcsRead refused");
    CHECK(! s_pDsi->API->GenericWrite(s_pDsi, s_packetBuffer, 2),  "GenericWrite refused");
    CHECK(s_pDsi->API->GenericRead(s_pDsi, s_packetBuffer, 1, s_readBuffer, sizeof s_readBuffer) < 0, "GenericRead refused");
    CHECK(! s_pDsi->API->WriteFrame(s_pDsi, s_packetBuffer, 8),    "WriteFrame refused");
    CHECK(! s_pDsi->API->EnterUltraLowPowerState(s_pDsi),          "EnterUltraLowPowerState refused");
    CHECK(! s_pDsi->API->ExitUltraLowPowerState(s_pDsi),           "ExitUltraLowPowerState refused");
    CHECK(s_pDsi->API->GetClockLaneState(s_pDsi) == kLTDeviceMipiDsi_LaneState_Off,    "clock lane is off");
    CHECK(s_pDsi->API->GetDataLaneState(s_pDsi, 0) == kLTDeviceMipiDsi_LaneState_Off,  "data lane 0 is off");

    /* PowerOff returns nothing; the test is that it and StopVideo are safe to
       call against a device that was never brought up. */
    CHECK(s_pDsi->API->StopVideo(s_pDsi),                          "StopVideo on an idle device succeeds");
    s_pDsi->API->PowerOff(s_pDsi);
    CHECK(! s_pDsi->API->IsPoweredOn(s_pDsi),                      "PowerOff on an idle device is harmless");

    return nFailures;
}

/* Every field the Device level validates, one broken config at a time. */
static int SelftestConfigValidation(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    int nFailures = 0;

    iShell->PutString(hShell, "configuration validation\n");
    CHECK(! s_pDsi->API->Configure(s_pDsi, NULL), "a null config is refused");

    s_scratch = s_config;
    s_scratch.mode = kLTDeviceMipiDsi_Mode_Unset;
    CHECK(! s_pDsi->API->Configure(s_pDsi, &s_scratch), "an unset mode is refused");

    s_scratch = s_config;
    s_scratch.pixelFormat = kLTDeviceMipiDsi_PixelFormat_Unset;
    CHECK(! s_pDsi->API->Configure(s_pDsi, &s_scratch), "an unset pixel format is refused");

    s_scratch = s_config;
    s_scratch.nDataLanes = 0;
    CHECK(! s_pDsi->API->Configure(s_pDsi, &s_scratch), "zero data lanes is refused");

    s_scratch = s_config;
    s_scratch.nDataLanes = kLTDeviceMipiDsi_MaxLanes + 1;
    CHECK(! s_pDsi->API->Configure(s_pDsi, &s_scratch), "more than %d data lanes is refused", kLTDeviceMipiDsi_MaxLanes);

    s_scratch = s_config;
    s_scratch.nVirtualChannel = kLTDeviceMipiDsi_MaxVirtualChannel + 1;
    CHECK(! s_pDsi->API->Configure(s_pDsi, &s_scratch), "virtual channel above %d is refused", kLTDeviceMipiDsi_MaxVirtualChannel);

    s_scratch = s_config;
    s_scratch.timing.nHorizontalActive = 0;
    CHECK(! s_pDsi->API->Configure(s_pDsi, &s_scratch), "a zero width is refused");

    s_scratch = s_config;
    s_scratch.timing.nVerticalActive = 0;
    CHECK(! s_pDsi->API->Configure(s_pDsi, &s_scratch), "a zero height is refused");

    s_scratch = s_config;
    s_scratch.timing.nFrameRateHz = 0;
    CHECK(! s_pDsi->API->Configure(s_pDsi, &s_scratch), "a video mode with no frame rate is refused");

    /* None of the above may have left a configuration behind. */
    CHECK(! s_pDsi->API->GetConfig(s_pDsi, &s_scratch), "a rejected config was not retained");

    return nFailures;
}

/* Applies the working config and checks it comes back unchanged.  Returns -1
   if the driver would not take it, which leaves the rest of the test moot. */
static int SelftestConfigure(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    int nFailures = 0;

    iShell->PutString(hShell, "configure\n");
    if (! s_pDsi->API->Configure(s_pDsi, &s_config)) {
        NOTE("the driver refused the working config - the rest of the test needs one it accepts");
        return -1;
    }
    CHECK(true, "the working config was accepted");
    CHECK(s_pDsi->API->GetConfig(s_pDsi, &s_scratch), "GetConfig now reports a configuration");
    CHECK(0 == lt_memcmp(&s_scratch, &s_config, sizeof s_config), "it reads back byte for byte");
    CHECK(s_pDsi->API->Configure(s_pDsi, &s_config), "reconfiguring while powered off is allowed");
    CHECK(! s_pDsi->API->IsPoweredOn(s_pDsi), "configuring did not power the link on");

    return nFailures;
}

/* The framebuffer calls that are answerable before the link is up. */
static int SelftestFrameBuffer(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    int nFailures = 0;

    iShell->PutString(hShell, "framebuffer\n");
    CHECK(! s_pDsi->API->SetFrameBuffer(s_pDsi, s_packetBuffer, 0), "a buffer of no bytes is refused");
    CHECK(s_pDsi->API->SetFrameBuffer(s_pDsi, NULL, 0), "a null buffer forgets the old one");

    u32 nBytes = FrameBufferBytes(&s_config);
    if (s_pFrameBuffer == NULL || s_nFrameBufferBytes < nBytes) {
        if (! AllocFrameBuffer(nBytes)) {
            NOTE("cannot allocate %u bytes - the pixel stream will not be tested", (unsigned)nBytes);
            return nFailures;
        }
    }
    CHECK(s_pDsi->API->SetFrameBuffer(s_pDsi, s_pFrameBuffer, s_nFrameBufferBytes),
          "a %u byte framebuffer is accepted", (unsigned)s_nFrameBufferBytes);

    /* A frame short of the configured geometry has to be refused, or the
       scanout would run off the end of it. */
    if (s_nFrameBufferBytes >= 16) {
        CHECK(! s_pDsi->API->SetFrameBuffer(s_pDsi, s_pFrameBuffer, 8), "a framebuffer too small for the geometry is refused");
        CHECK(s_pDsi->API->SetFrameBuffer(s_pDsi, s_pFrameBuffer, s_nFrameBufferBytes), "the good one is accepted again");
    }

    return nFailures;
}

/* Power on, and what the lanes and the configuration calls do once it is up. */
static int SelftestPower(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    int nFailures = 0;

    iShell->PutString(hShell, "power on\n");
    if (! s_pDsi->API->PowerOn(s_pDsi)) {
        NOTE("PowerOn failed - the PHY did not lock or the lanes never reached the stop state");
        return -1;
    }
    CHECK(true, "PowerOn brought the link up");
    CHECK(s_pDsi->API->IsPoweredOn(s_pDsi), "IsPoweredOn agrees");
    CHECK(s_pDsi->API->PowerOn(s_pDsi), "PowerOn again is a no-op that succeeds");
    CHECK(! s_pDsi->API->Configure(s_pDsi, &s_config), "Configure while powered on is refused");

    /* PowerOn only returns true once the configured lanes are in LP-11. */
    CHECK(s_pDsi->API->GetClockLaneState(s_pDsi) == kLTDeviceMipiDsi_LaneState_Stop, "the clock lane is in the stop state");
    for (u32 nLane = 0; nLane < s_config.nDataLanes; ++nLane) {
        CHECK(s_pDsi->API->GetDataLaneState(s_pDsi, nLane) == kLTDeviceMipiDsi_LaneState_Stop,
              "data lane %u is in the stop state", (unsigned)nLane);
    }
    CHECK(s_pDsi->API->GetDataLaneState(s_pDsi, s_config.nDataLanes) == kLTDeviceMipiDsi_LaneState_Off,
          "data lane %u, which is not configured, reads off", (unsigned)s_config.nDataLanes);

    return nFailures;
}

/* The command channel: the speed selector, the argument checking, and whatever
   the panel makes of some real packets. */
static int SelftestCommands(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    int nFailures = 0;

    iShell->PutString(hShell, "command channel\n");
    CHECK(! s_pDsi->API->GetCommandsUseHighSpeed(s_pDsi), "commands default to low power escape mode");
    s_pDsi->API->SetCommandsUseHighSpeed(s_pDsi, true);
    CHECK(s_pDsi->API->GetCommandsUseHighSpeed(s_pDsi), "selecting high speed takes");
    s_pDsi->API->SetCommandsUseHighSpeed(s_pDsi, false);
    CHECK(! s_pDsi->API->GetCommandsUseHighSpeed(s_pDsi), "selecting low power takes");

    CHECK(! s_pDsi->API->DcsWrite(s_pDsi, 0x29, NULL, 4), "DcsWrite with parameters but no pointer is refused");
    CHECK(! s_pDsi->API->GenericWrite(s_pDsi, NULL, 4), "GenericWrite with a payload but no pointer is refused");
    CHECK(s_pDsi->API->DcsRead(s_pDsi, 0x0a, NULL, 4) < 0, "DcsRead into a null buffer is refused");
    CHECK(s_pDsi->API->DcsRead(s_pDsi, 0x0a, s_readBuffer, 0) < 0, "DcsRead of no bytes is refused");
    CHECK(s_pDsi->API->GenericRead(s_pDsi, s_packetBuffer, 1, NULL, 4) < 0, "GenericRead into a null buffer is refused");
    CHECK(s_pDsi->API->GenericRead(s_pDsi, s_packetBuffer, 1, s_readBuffer, 0) < 0, "GenericRead of no bytes is refused");
    CHECK(s_pDsi->API->GenericRead(s_pDsi, s_packetBuffer, kLTDeviceMipiDsi_MaxShortParams + 1, s_readBuffer, sizeof s_readBuffer) < 0,
          "a generic read request longer than a short packet is refused");
    CHECK(s_pDsi->API->GenericRead(s_pDsi, NULL, 2, s_readBuffer, sizeof s_readBuffer) < 0,
          "GenericRead with a payload but no pointer is refused");

    /* Real traffic.  Whether any of it is acknowledged is the panel's to say,
       so these are reported rather than scored - but they do exercise the short
       and long packet paths on both the DCS and the generic side. */
    lt_memset(s_packetBuffer, 0, sizeof s_packetBuffer);
    NOTE("DCS nop, no parameters (short):   %s", s_pDsi->API->DcsWrite(s_pDsi, 0x00, NULL, 0) ? "acked" : "no ack");
    NOTE("DCS set tear off, 1 parameter:    %s", s_pDsi->API->DcsWrite(s_pDsi, 0x35, s_packetBuffer, 1) ? "acked" : "no ack");
    NOTE("DCS column address, 4 (long):     %s", s_pDsi->API->DcsWrite(s_pDsi, 0x2a, s_packetBuffer, 4) ? "acked" : "no ack");
    NOTE("generic 2 bytes (short):          %s", s_pDsi->API->GenericWrite(s_pDsi, s_packetBuffer, 2) ? "acked" : "no ack");
    NOTE("generic 8 bytes (long):           %s", s_pDsi->API->GenericWrite(s_pDsi, s_packetBuffer, 8) ? "acked" : "no ack");
    NOTE("DCS read of the power mode (0x0a) returned %d bytes",
         (int)s_pDsi->API->DcsRead(s_pDsi, 0x0a, s_readBuffer, sizeof s_readBuffer));

    return nFailures;
}

/* ULPS, which has to be entered from an idle link and left again. */
static int SelftestUlps(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    int nFailures = 0;

    iShell->PutString(hShell, "ultra low power state\n");
    if (! s_pDsi->API->EnterUltraLowPowerState(s_pDsi)) {
        NOTE("the lanes would not enter ULPS");
        return nFailures;
    }
    CHECK(true, "the lanes entered ULPS");
    for (u32 nLane = 0; nLane < s_config.nDataLanes; ++nLane) {
        CHECK(s_pDsi->API->GetDataLaneState(s_pDsi, nLane) == kLTDeviceMipiDsi_LaneState_UltraLowPower,
              "data lane %u reads ulps", (unsigned)nLane);
    }
    CHECK(s_pDsi->API->ExitUltraLowPowerState(s_pDsi), "the lanes left ULPS");
    for (u32 nLane = 0; nLane < s_config.nDataLanes; ++nLane) {
        CHECK(s_pDsi->API->GetDataLaneState(s_pDsi, nLane) == kLTDeviceMipiDsi_LaneState_Stop,
              "data lane %u is back in the stop state", (unsigned)nLane);
    }

    return nFailures;
}

/* The pixel stream, and everything the interface refuses while it is running. */
static int SelftestVideo(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    LTOThread * pThread = LT_GetCore()->GetCurrentThreadObject();
    int nFailures = 0;

    iShell->PutString(hShell, "pixel stream\n");

    CHECK(s_pDsi->API->SetFrameBuffer(s_pDsi, NULL, 0), "the framebuffer can be forgotten");
    CHECK(! s_pDsi->API->StartVideo(s_pDsi), "StartVideo with no framebuffer is refused");

    if (s_pFrameBuffer == NULL) {
        NOTE("no framebuffer was allocated - the stream itself is not tested");
        return nFailures;
    }
    CHECK(s_pDsi->API->SetFrameBuffer(s_pDsi, s_pFrameBuffer, s_nFrameBufferBytes), "the framebuffer goes back");

    if (! s_pDsi->API->StartVideo(s_pDsi)) {
        NOTE("StartVideo failed");
        return nFailures;
    }
    CHECK(true, "StartVideo began the stream");
    CHECK(s_pDsi->API->IsVideoActive(s_pDsi), "IsVideoActive agrees");
    CHECK(s_pDsi->API->StartVideo(s_pDsi), "StartVideo again is a no-op that succeeds");
    CHECK(s_pDsi->API->SetFrameBuffer(s_pDsi, s_pFrameBuffer, s_nFrameBufferBytes), "SetFrameBuffer queues a frame-boundary swap");
    CHECK(! s_pDsi->API->EnterUltraLowPowerState(s_pDsi), "ULPS mid-stream is refused");
    CHECK(! s_pDsi->API->WriteFrame(s_pDsi, s_pFrameBuffer, s_nFrameBufferBytes), "WriteFrame in a video mode is refused");
    CHECK(! s_pDsi->API->Configure(s_pDsi, &s_config), "Configure mid-stream is refused");

    /* Long enough for several frames at any plausible refresh rate, so the
       error interrupts have something to report if the link cannot keep up. */
    pThread->API->Sleep(LTTime_Milliseconds(200));
    NOTE("clock lane %s, data lane 0 %s while streaming",
         LaneStateName(s_pDsi->API->GetClockLaneState(s_pDsi)),
         LaneStateName(s_pDsi->API->GetDataLaneState(s_pDsi, 0)));

    CHECK(s_pDsi->API->StopVideo(s_pDsi), "StopVideo succeeded");
    CHECK(! s_pDsi->API->IsVideoActive(s_pDsi), "StopVideo ended the stream");
    CHECK(s_pDsi->API->StopVideo(s_pDsi), "StopVideo again succeeds");
    CHECK(! s_pDsi->API->IsVideoActive(s_pDsi), "StopVideo again is harmless");
    CHECK(s_pDsi->API->SetFrameBuffer(s_pDsi, s_pFrameBuffer, s_nFrameBufferBytes), "the framebuffer can be set again once stopped");

    return nFailures;
}

/* Error reporting: the accumulator, the event registration, and the strings. */
static int SelftestErrors(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    int nFailures = 0;

    iShell->PutString(hShell, "error reporting\n");

    iShell->PutString(hShell, "  [note] accumulated so far: ");
    PrintErrors(hShell, s_pDsi->API->GetErrors(s_pDsi, false));
    iShell->Print(hShell, " (%u events dispatched)\n", (unsigned)s_nErrorEvents);

    /* The link is powered off by now, so nothing can set a new flag between
       the clearing read and the one that checks it. */
    s_pDsi->API->GetErrors(s_pDsi, true);
    CHECK(s_pDsi->API->GetErrors(s_pDsi, false) == kLTDeviceMipiDsi_Error_None, "a clearing read empties the accumulator");

    /* Unregistering and re-registering has to be safe; a stale registration
       would have the event thread calling into a library that had gone. */
    s_pDsi->API->NoErrorEvent(s_pDsi, &DsiErrorEventProc);
    s_pDsi->API->NoErrorEvent(s_pDsi, &DsiErrorEventProc);
    s_pDsi->API->OnErrorEvent(s_pDsi, &DsiErrorEventProc, NULL, NULL);
    CHECK(true, "the error event proc unregisters and registers again");

    static const LTDeviceMipiDsi_Error s_allErrors[] = {
        kLTDeviceMipiDsi_Error_TxFifoOverflow,  kLTDeviceMipiDsi_Error_TxFifoUnderflow,
        kLTDeviceMipiDsi_Error_RxFifoOverflow,  kLTDeviceMipiDsi_Error_RxFifoUnderflow,
        kLTDeviceMipiDsi_Error_PixelCountShort, kLTDeviceMipiDsi_Error_PixelCountLong,
        kLTDeviceMipiDsi_Error_EccUncorrectable, kLTDeviceMipiDsi_Error_Checksum,
        kLTDeviceMipiDsi_Error_Timeout,         kLTDeviceMipiDsi_Error_PanelReported,
    };
    u32 nNamed = 0;
    for (u32 i = 0; i < sizeof s_allErrors / sizeof s_allErrors[0]; ++i) {
        const char * pName = s_pDsi->API->ErrorToString(s_allErrors[i]);
        if (pName && 0 != lt_strcmp(pName, "Unknown")) ++nNamed;
    }
    CHECK(nNamed == sizeof s_allErrors / sizeof s_allErrors[0], "every error flag has a name (%u of %u)",
          (unsigned)nNamed, (unsigned)(sizeof s_allErrors / sizeof s_allErrors[0]));
    CHECK(0 == lt_strcmp(s_pDsi->API->ErrorToString(kLTDeviceMipiDsi_Error_None), "None"), "no error is named None");
    CHECK(0 == lt_strcmp(s_pDsi->API->ErrorToString((LTDeviceMipiDsi_Error)(1u << 31)), "Unknown"), "an unknown flag is named Unknown");

    return nFailures;
}

/* Power off, and what the interface owes once the link is down again. */
static int SelftestPowerOff(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    int nFailures = 0;

    iShell->PutString(hShell, "power off\n");
    s_pDsi->API->PowerOff(s_pDsi);
    CHECK(! s_pDsi->API->IsPoweredOn(s_pDsi), "PowerOff brought the link down");
    CHECK(! s_pDsi->API->IsVideoActive(s_pDsi), "and stopped any pixel stream with it");
    s_pDsi->API->PowerOff(s_pDsi);
    CHECK(! s_pDsi->API->IsPoweredOn(s_pDsi), "PowerOff again is harmless");

    CHECK(s_pDsi->API->GetClockLaneState(s_pDsi) == kLTDeviceMipiDsi_LaneState_Off, "the clock lane reads off");
    CHECK(s_pDsi->API->GetDataLaneState(s_pDsi, 0) == kLTDeviceMipiDsi_LaneState_Off, "data lane 0 reads off");
    CHECK(! s_pDsi->API->DcsWrite(s_pDsi, 0x28, NULL, 0), "DcsWrite is refused again");
    CHECK(s_pDsi->API->GetConfig(s_pDsi, &s_scratch), "the configuration survives the power cycle");
    CHECK(s_pDsi->API->Configure(s_pDsi, &s_config), "and can be replaced now the link is down");

    return nFailures;
}

/*
 * Walks a fresh device object through the whole interface.  The object is
 * recreated first so that the phase checking what an unconfigured device
 * refuses has one, whatever the shell was asked to do beforehand - and so that
 * construction and destruction are themselves on the path.
 */
static int DsiSelftest(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    int nFailures = 0;

    if (s_pDsi) {
        s_pDsi->API->NoErrorEvent(s_pDsi, &DsiErrorEventProc);
        lt_destroyobject(s_pDsi);
    }
    s_pDsi = lt_createdeviceobject(LTDeviceMipiDsi);
    if (s_pDsi == NULL) {
        iShell->PutString(hShell, "cannot create an LTDeviceMipiDsi object\n");
        return -1;
    }
    s_pDsi->API->OnErrorEvent(s_pDsi, &DsiErrorEventProc, NULL, NULL);
    s_nErrorEvents    = 0;
    s_nErrorEventMask = 0;

    iShell->Print(hShell, "selftest with a %ux%u %s %s panel on %u lane%s\n",
                  (unsigned)s_config.timing.nHorizontalActive,
                  (unsigned)s_config.timing.nVerticalActive,
                  FormatName(s_config.pixelFormat), ModeName(s_config.mode),
                  (unsigned)s_config.nDataLanes,
                  (s_config.nDataLanes == 1) ? "" : "s");

    nFailures += SelftestUnconfigured(hShell);
    nFailures += SelftestConfigValidation(hShell);

    int nConfigured = SelftestConfigure(hShell);
    if (nConfigured < 0) {
        iShell->PutString(hShell, "SELFTEST INCOMPLETE - no usable configuration\n");
        return nFailures ? nFailures : -1;
    }
    nFailures += nConfigured;

    nFailures += SelftestFrameBuffer(hShell);

    int nPowered = SelftestPower(hShell);
    if (nPowered < 0) {
        iShell->PutString(hShell, "SELFTEST INCOMPLETE - the link would not power on\n");
        return nFailures ? nFailures : -1;
    }
    nFailures += nPowered;

    nFailures += SelftestCommands(hShell);
    nFailures += SelftestUlps(hShell);
    nFailures += SelftestVideo(hShell);
    nFailures += SelftestPowerOff(hShell);
    nFailures += SelftestErrors(hShell);

    iShell->Print(hShell, "%s", nFailures ? "SELFTEST FAILED: " : "selftest passed");
    if (nFailures) iShell->Print(hShell, "%d check%s", nFailures, (nFailures == 1) ? "" : "s");
    iShell->PutString(hShell, "\n");
    return nFailures;
}

#undef CHECK
#undef NOTE

/*____________________
_/ dsi help proc */
static void ShellHelpDsi(LTShell hShell, int argc, const char ** argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    iShell->PutString(hShell, "usage: dsi show                      - the working config and the link state\n");
    iShell->PutString(hShell, "       dsi selftest                  - exercise the whole api; needs no panel\n");
    iShell->PutString(hShell, "       dsi regs                      - the clock, host and bridge registers\n");
    iShell->PutString(hShell, "\n");
    iShell->PutString(hShell, "       dsi config                    - print the working config\n");
    iShell->PutString(hShell, "       dsi config reset              - back to the default panel\n");
    iShell->PutString(hShell, "       dsi config preset hx8394      - the Waveshare ESP32-P4-WIFI6-Touch-LCD-5 panel\n");
    iShell->PutString(hShell, "       dsi config preset co5300      - the Startek KD020HVOIN002-C002B AMOLED module\n");
    iShell->PutString(hShell, "       dsi config preset co5300rgb888 - 24-bit pixels; EXPERIMENTAL, gives a broken picture\n");
    iShell->PutString(hShell, "       dsi config mode <syncpulse|syncevent|burst|command>\n");
    iShell->PutString(hShell, "       dsi config format <rgb565|rgb666p|rgb666l|rgb888|yuv422>\n");
    iShell->PutString(hShell, "       dsi config lanes <n>\n");
    iShell->PutString(hShell, "       dsi config vc <n>\n");
    iShell->PutString(hShell, "       dsi config rate <mbps>        - 0 to derive it from the timing\n");
    iShell->PutString(hShell, "       dsi config fps <hz>\n");
    iShell->PutString(hShell, "       dsi config view <width> [<offset>|auto] - the logical framebuffer: stride is the h active,\n");
    iShell->PutString(hShell, "                                     width the visible part of a line, offset (default stride-width) pixels in front\n");
    iShell->PutString(hShell, "       dsi config clock <continuous|gated>\n");
    iShell->PutString(hShell, "       dsi config eotp <on|off>\n");
    iShell->PutString(hShell, "       dsi config h <act> <sync> <bp> <fp>\n");
    iShell->PutString(hShell, "       dsi config v <act> <sync> <bp> <fp>\n");
    iShell->PutString(hShell, "       dsi apply                     - hand the working config to the device\n");
    iShell->PutString(hShell, "\n");
    iShell->PutString(hShell, "       dsi power <on|off>\n");
    iShell->PutString(hShell, "       dsi fb alloc [bytes]          - allocate a frame and hand it over\n");
    iShell->PutString(hShell, "       dsi fb free\n");
    iShell->PutString(hShell, "       dsi fb fill <colour>          - one colour, in the configured format\n");
    iShell->PutString(hShell, "       dsi fb fillchunked <colour> <chunk px> <sleep ms>\n");
    iShell->PutString(hShell, "                                     - diagnostic: fill with yields between chunks\n");
    iShell->PutString(hShell, "       dsi fb touch <pixels> <repeats>\n");
    iShell->PutString(hShell, "                                     - diagnostic: rewrite just <pixels>, back to back\n");
    iShell->PutString(hShell, "       dsi fb swaploop2 <row> <rows> <cols> <repeats> <delay ms>\n");
    iShell->PutString(hShell, "                                     - diagnostic: native write+swap loop at a chosen row band\n");
    iShell->PutString(hShell, "       dsi fb dumprow <x> <y> <cols> <rows>\n");
    iShell->PutString(hShell, "                                     - diagnostic: print rgb565 at a small region\n");
    iShell->PutString(hShell, "       dsi fb stripes|hstripes <n>   - white/black stripes n pixels wide/tall\n");
    iShell->PutString(hShell, "       dsi fb grid2                  - thick line grid for a low resolution camera\n");
    iShell->PutString(hShell, "       dsi fb bars                   - eight vertical colour bars\n");
    iShell->PutString(hShell, "       dsi fb grid                   - static 32-pixel mapping grid\n");
    iShell->PutString(hShell, "       dsi fb animate [frames]       - moving square over colour bars\n");
    iShell->PutString(hShell, "       dsi fb reswap                 - diagnostic: resubmit the same buffer mid-stream\n");
    iShell->PutString(hShell, "       dsi video <start|stop>\n");
    iShell->PutString(hShell, "       dsi lanes                     - the clock and data lane states\n");
    iShell->PutString(hShell, "       dsi hs <on|off>               - send panel commands in high speed\n");
    iShell->PutString(hShell, "       dsi ulps <enter|exit>\n");
    iShell->PutString(hShell, "       dsi errors [clear]\n");
    iShell->PutString(hShell, "\n");
    iShell->PutString(hShell, "       dsi panel hx8394              - that panel's init sequence (needs power on)\n");
    iShell->PutString(hShell, "       dsi panel co5300 [888]        - that module's init sequence (needs power on); 888 = 24-bit pixels\n");
    iShell->PutString(hShell, "       dsi te [ms]                   - panel TE pulses (GPIO 33) against the host frame position\n");
    iShell->PutString(hShell, "       dsi dcsw <cmd> [param ...]    - DCS write\n");
    iShell->PutString(hShell, "       dsi dcsr <cmd> [count]        - DCS read\n");
    iShell->PutString(hShell, "       dsi genw <byte> [byte ...]    - generic write\n");
    iShell->PutString(hShell, "       dsi genr <count> [byte ...]   - generic read\n");
    iShell->PutString(hShell, "\n");
    iShell->PutString(hShell, "Numbers take a 0x prefix for hex.  The working config is only applied\n");
    iShell->PutString(hShell, "by 'dsi apply'; 'dsi selftest' applies it to a device of its own.\n");
}

/*____________________
_/ dsi command proc */
static int ShellCommandDsi(LTShell hShell, int argc, const char ** argv) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    u32 nValue = 0;

    if (s_pDsi == NULL) {
        iShell->PutString(hShell, "no LTDeviceMipiDsi device on this platform\n");
        return -1;
    }

    if (argc < 2) { ShellHelpDsi(hShell, argc, argv); return -1; }

    if (0 == lt_strcmp(argv[1], "show"))     { DsiShow(hShell);     return 0; }
    if (0 == lt_strcmp(argv[1], "regs"))     { DsiRegisters(hShell); return 0; }
    if (0 == lt_strcmp(argv[1], "selftest")) { return DsiSelftest(hShell); }
    if (0 == lt_strcmp(argv[1], "config"))   { return DsiConfig(hShell, argc, argv); }
    if (0 == lt_strcmp(argv[1], "fb"))       { return DsiFrameBuffer(hShell, argc, argv); }

    if (0 == lt_strcmp(argv[1], "apply")) {
        if (! s_pDsi->API->Configure(s_pDsi, &s_config)) { iShell->PutString(hShell, "the config was refused\n"); return -1; }
        iShell->PutString(hShell, "configured\n");
        return 0;
    }

    if (0 == lt_strcmp(argv[1], "lanes")) {
        iShell->Print(hShell, "clock %s", LaneStateName(s_pDsi->API->GetClockLaneState(s_pDsi)));
        for (u32 nLane = 0; nLane < s_config.nDataLanes; ++nLane) {
            iShell->Print(hShell, "  data%u %s", (unsigned)nLane, LaneStateName(s_pDsi->API->GetDataLaneState(s_pDsi, nLane)));
        }
        iShell->PutString(hShell, "\n");
        return 0;
    }

    if (0 == lt_strcmp(argv[1], "errors")) {
        bool bClear = (argc > 2) && (0 == lt_strcmp(argv[2], "clear"));
        iShell->PutString(hShell, "accumulated ");
        PrintErrors(hShell, s_pDsi->API->GetErrors(s_pDsi, bClear));
        iShell->Print(hShell, "\n%u events dispatched, mask ", (unsigned)s_nErrorEvents);
        PrintErrors(hShell, (LTDeviceMipiDsi_Error)s_nErrorEventMask);
        iShell->PutString(hShell, "\n");
        if (bClear) { s_nErrorEvents = 0; s_nErrorEventMask = 0; }
        return 0;
    }

    /* "te" takes an optional argument, so it goes ahead of the guard below */
    if (0 == lt_strcmp(argv[1], "te")) {
        return DsiTe(hShell, argc, argv);
    }

    /* everything below takes an argument */
    if (argc < 3) { ShellHelpDsi(hShell, argc, argv); return -1; }

    if (0 == lt_strcmp(argv[1], "power")) {
        if (0 == lt_strcmp(argv[2], "off")) { s_pDsi->API->PowerOff(s_pDsi); return 0; }
        if (0 != lt_strcmp(argv[2], "on"))  { ShellHelpDsi(hShell, argc, argv); return -1; }
        if (! s_pDsi->API->PowerOn(s_pDsi)) { iShell->PutString(hShell, "PowerOn failed\n"); return -1; }
        iShell->PutString(hShell, "the lanes are in the stop state\n");
        return 0;
    }

    if (0 == lt_strcmp(argv[1], "video")) {
        if (0 == lt_strcmp(argv[2], "stop")) { return s_pDsi->API->StopVideo(s_pDsi) ? 0 : -1; }
        if (0 != lt_strcmp(argv[2], "start")) { ShellHelpDsi(hShell, argc, argv); return -1; }
        if (! s_pDsi->API->StartVideo(s_pDsi)) { iShell->PutString(hShell, "StartVideo failed\n"); return -1; }
        iShell->PutString(hShell, "streaming\n");
        return 0;
    }

    if (0 == lt_strcmp(argv[1], "hs")) {
        s_pDsi->API->SetCommandsUseHighSpeed(s_pDsi, 0 == lt_strcmp(argv[2], "on"));
        iShell->Print(hShell, "commands go out in %s\n",
                      s_pDsi->API->GetCommandsUseHighSpeed(s_pDsi) ? "high speed" : "low power escape mode");
        return 0;
    }

    if (0 == lt_strcmp(argv[1], "ulps")) {
        bool bEnter = (0 == lt_strcmp(argv[2], "enter"));
        bool bOK = bEnter ? s_pDsi->API->EnterUltraLowPowerState(s_pDsi)
                          : s_pDsi->API->ExitUltraLowPowerState(s_pDsi);
        if (! bOK) { iShell->Print(hShell, "the lanes would not %s ULPS\n", bEnter ? "enter" : "leave"); return -1; }
        return 0;
    }

    if (0 == lt_strcmp(argv[1], "panel")) {
        if (argc >= 3 && 0 == lt_strcmp(argv[2], "co5300")) {
            if (! DsiPanelCo5300(hShell, argc >= 4 && 0 == lt_strcmp(argv[3], "888"))) return -1;
            iShell->PutString(hShell, "CO5300 init sent\n");
            return 0;
        }
        if (argc < 3 || 0 != lt_strcmp(argv[2], "hx8394")) { ShellHelpDsi(hShell, argc, argv); return -1; }
        if (! DsiPanelHx8394(hShell)) return -1;
        iShell->PutString(hShell, "HX8394 init sent\n");
        return 0;
    }

    if (0 == lt_strcmp(argv[1], "dcsw")) {
        if (! ParseU32(argv[2], &nValue) || nValue > 0xff) { iShell->Print(hShell, "not a command byte: %s\n", argv[2]); return -1; }
        int nParams = CollectBytes(hShell, argc, argv, 3);
        if (nParams < 0) return -1;
        if (! s_pDsi->API->DcsWrite(s_pDsi, (u8)nValue, s_packetBuffer, (u32)nParams)) {
            iShell->PutString(hShell, "DcsWrite failed\n");
            return -1;
        }
        return 0;
    }

    if (0 == lt_strcmp(argv[1], "dcsr")) {
        if (! ParseU32(argv[2], &nValue) || nValue > 0xff) { iShell->Print(hShell, "not a command byte: %s\n", argv[2]); return -1; }
        u32 nCount = 1;
        if (argc > 3 && ! ParseU32(argv[3], &nCount)) { iShell->Print(hShell, "not a number: %s\n", argv[3]); return -1; }
        if (nCount > kMaxReadBytes) nCount = kMaxReadBytes;
        PrintReadBytes(hShell, s_pDsi->API->DcsRead(s_pDsi, (u8)nValue, s_readBuffer, nCount));
        return 0;
    }

    if (0 == lt_strcmp(argv[1], "genw")) {
        int nBytes = CollectBytes(hShell, argc, argv, 2);
        if (nBytes < 0) return -1;
        if (! s_pDsi->API->GenericWrite(s_pDsi, s_packetBuffer, (u32)nBytes)) {
            iShell->PutString(hShell, "GenericWrite failed\n");
            return -1;
        }
        return 0;
    }

    if (0 == lt_strcmp(argv[1], "genr")) {
        if (! ParseU32(argv[2], &nValue)) { iShell->Print(hShell, "not a number: %s\n", argv[2]); return -1; }
        if (nValue > kMaxReadBytes) nValue = kMaxReadBytes;
        int nBytes = CollectBytes(hShell, argc, argv, 3);
        if (nBytes < 0) return -1;
        PrintReadBytes(hShell, s_pDsi->API->GenericRead(s_pDsi, s_packetBuffer, (u32)nBytes, s_readBuffer, nValue));
        return 0;
    }

    ShellHelpDsi(hShell, argc, argv);
    return -1;
}

/*__________________________
_/ library initialization */
static bool LTShellDsiImpl_LibInit(void) {
    s_pLTSystemShell = lt_openlibrary(LTSystemShell);
    if (s_pLTSystemShell == NULL) return false;

    SetDefaultConfig(&s_config);

    /* the device is optional - the command reports its absence rather than
       keeping the whole library from loading */
    s_pDsi = lt_createdeviceobject(LTDeviceMipiDsi);
    if (s_pDsi == NULL) LTLOG_YELLOWALERT("no.dsi.device", "no LTDeviceMipiDsi in the device config");
    else                s_pDsi->API->OnErrorEvent(s_pDsi, &DsiErrorEventProc, NULL, NULL);

    s_pLTSystemShell->RegisterCommands(s_dsiShellCommands, sizeof s_dsiShellCommands / sizeof s_dsiShellCommands[0]);
    return true;
}

static void LTShellDsiImpl_LibFini(void) {
    if (s_pLTSystemShell) {
        s_pLTSystemShell->UnregisterCommands(s_dsiShellCommands);
        lt_closelibrary(s_pLTSystemShell);
        s_pLTSystemShell = NULL;
    }
    if (s_pDsi) {
        s_pDsi->API->NoErrorEvent(s_pDsi, &DsiErrorEventProc);
        lt_destroyobject(s_pDsi);
        s_pDsi = NULL;
    }
    if (s_pTeGpio) {
        lt_destroyobject(s_pTeGpio);
        s_pTeGpio = NULL;
    }
    FreeFrameBuffer();
}

/*______________________________
_/ LTShellDsi library binding   */
typedef_LTLIBRARY_ROOT_INTERFACE(LTShellDsi, 1) LTLIBRARY_EMPTY_INTERFACE;
 define_LTLIBRARY_ROOT_INTERFACE(LTShellDsi)    LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  01-Oct-26   dwoodward   created
 */
