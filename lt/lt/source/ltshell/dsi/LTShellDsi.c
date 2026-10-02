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

static void FreeFrameBuffer(void) {
    if (s_pFrameBuffer == NULL) return;
    if (s_pDsi) s_pDsi->API->SetFrameBuffer(s_pDsi, NULL, 0);
    lt_free(s_pFrameBuffer);
    s_pFrameBuffer      = NULL;
    s_nFrameBufferBytes = 0;
}

/* Prefers the heap region named "psram" because a frame of any real panel is
   larger than the on-chip SRAM a small part has.  A platform without that
   region falls through to the unrestricted allocator. */
static bool AllocFrameBuffer(u32 nBytes) {
    FreeFrameBuffer();
    if (nBytes == 0) return false;
    LTMemoryRegion region = LT_GetCore()->GetNamedMemoryRegion("psram");
    s_pFrameBuffer = (u8 *)lt_malloc_from_region(region, nBytes);
    if (s_pFrameBuffer == NULL) return false;
    lt_memset(s_pFrameBuffer, 0, nBytes);
    s_nFrameBufferBytes = nBytes;
    return true;
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

static int DsiConfig(LTShell hShell, int argc, const char ** argv) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    u32 nValue = 0;

    if (argc < 3) { DsiShow(hShell); return 0; }

    if (0 == lt_strcmp(argv[2], "reset")) {
        SetDefaultConfig(&s_config);
        iShell->PutString(hShell, "working config reset to the default panel\n");
        return 0;
    }

    if (argc < 4) { ShellHelpDsi(hShell, argc, argv); return -1; }

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
        FreeFrameBuffer();
        return 0;
    }

    if (0 == lt_strcmp(argv[2], "alloc")) {
        u32 nBytes = FrameBufferBytes(&s_config);
        if (argc > 3 && ! ParseU32(argv[3], &nBytes)) { iShell->Print(hShell, "not a number: %s\n", argv[3]); return -1; }
        if (! AllocFrameBuffer(nBytes)) { iShell->Print(hShell, "cannot allocate %u bytes\n", (unsigned)nBytes); return -1; }
        iShell->Print(hShell, "%u bytes at %lx\n", (unsigned)s_nFrameBufferBytes, (unsigned long)(LT_SIZE)s_pFrameBuffer);
        if (! s_pDsi->API->SetFrameBuffer(s_pDsi, s_pFrameBuffer, s_nFrameBufferBytes)) {
            iShell->PutString(hShell, "the device would not take it\n");
            return -1;
        }
        return 0;
    }

    if (s_pFrameBuffer == NULL) { iShell->PutString(hShell, "no framebuffer; 'dsi fb alloc' first\n"); return -1; }

    /* Only the two formats whose pixels are a whole number of bytes are filled
       here; the packed ones would need the bit position of each pixel. */
    u32 nBitsPerPixel = BitsPerPixel(s_config.pixelFormat);
    if (nBitsPerPixel != 16 && nBitsPerPixel != 24) {
        iShell->Print(hShell, "cannot fill a %s framebuffer\n", FormatName(s_config.pixelFormat));
        return -1;
    }
    u32 nBytesPerPixel = nBitsPerPixel / 8;
    u32 nPixels        = s_nFrameBufferBytes / nBytesPerPixel;

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

    if (0 == lt_strcmp(argv[2], "bars")) {
        /* Eight vertical bars, so a panel that scans out at all shows whether
           the line length and the pixel format agree with what it expected. */
        static const u32 s_barColours16[8] = { 0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f, 0x0000 };
        static const u32 s_barColours24[8] = { 0xffffff, 0x00ffff, 0xffff00, 0x00ff00, 0xff00ff, 0x0000ff, 0xff0000, 0x000000 };
        u32 nWidth = s_config.timing.nHorizontalActive;
        if (nWidth == 0) { iShell->PutString(hShell, "the working config has no width\n"); return -1; }
        for (u32 i = 0; i < nPixels; ++i) {
            u32 nBar    = ((i % nWidth) * 8) / nWidth;
            u32 nColour = (nBytesPerPixel == 2) ? s_barColours16[nBar] : s_barColours24[nBar];
            u8 * pPixel = &s_pFrameBuffer[i * nBytesPerPixel];
            pPixel[0] = (u8)(nColour & 0xff);
            pPixel[1] = (u8)((nColour >> 8) & 0xff);
            if (nBytesPerPixel == 3) pPixel[2] = (u8)((nColour >> 16) & 0xff);
        }
        iShell->Print(hShell, "colour bars over %u pixels\n", (unsigned)nPixels);
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

    /* PowerOff and StopVideo return nothing; the test is that they are safe to
       call against a device that was never brought up. */
    s_pDsi->API->StopVideo(s_pDsi);
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
    CHECK(! s_pDsi->API->SetFrameBuffer(s_pDsi, s_pFrameBuffer, s_nFrameBufferBytes), "SetFrameBuffer mid-stream is refused");
    CHECK(! s_pDsi->API->EnterUltraLowPowerState(s_pDsi), "ULPS mid-stream is refused");
    CHECK(! s_pDsi->API->WriteFrame(s_pDsi, s_pFrameBuffer, s_nFrameBufferBytes), "WriteFrame in a video mode is refused");
    CHECK(! s_pDsi->API->Configure(s_pDsi, &s_config), "Configure mid-stream is refused");

    /* Long enough for several frames at any plausible refresh rate, so the
       error interrupts have something to report if the link cannot keep up. */
    pThread->API->Sleep(LTTime_Milliseconds(200));
    NOTE("clock lane %s, data lane 0 %s while streaming",
         LaneStateName(s_pDsi->API->GetClockLaneState(s_pDsi)),
         LaneStateName(s_pDsi->API->GetDataLaneState(s_pDsi, 0)));

    s_pDsi->API->StopVideo(s_pDsi);
    CHECK(! s_pDsi->API->IsVideoActive(s_pDsi), "StopVideo ended the stream");
    s_pDsi->API->StopVideo(s_pDsi);
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
    iShell->PutString(hShell, "       dsi config mode <syncpulse|syncevent|burst|command>\n");
    iShell->PutString(hShell, "       dsi config format <rgb565|rgb666p|rgb666l|rgb888|yuv422>\n");
    iShell->PutString(hShell, "       dsi config lanes <n>\n");
    iShell->PutString(hShell, "       dsi config vc <n>\n");
    iShell->PutString(hShell, "       dsi config rate <mbps>        - 0 to derive it from the timing\n");
    iShell->PutString(hShell, "       dsi config fps <hz>\n");
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
    iShell->PutString(hShell, "       dsi fb bars                   - eight vertical colour bars\n");
    iShell->PutString(hShell, "       dsi video <start|stop>\n");
    iShell->PutString(hShell, "       dsi lanes                     - the clock and data lane states\n");
    iShell->PutString(hShell, "       dsi hs <on|off>               - send panel commands in high speed\n");
    iShell->PutString(hShell, "       dsi ulps <enter|exit>\n");
    iShell->PutString(hShell, "       dsi errors [clear]\n");
    iShell->PutString(hShell, "\n");
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
        if (0 == lt_strcmp(argv[2], "stop")) { s_pDsi->API->StopVideo(s_pDsi); return 0; }
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
