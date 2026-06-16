/******************************************************************************
 * lt/device/video/LTDeviceVideo.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/video/LTDeviceVideo.h>

DEFINE_LTLOG_SECTION("dev.vid");
#define P(...)    LTLOG_DEBUG(__VA_ARGS__)
#define PLOG(...) LTLOG(__VA_ARGS__)


static LTDriverLibrary *s_driver = NULL;
static LTDeviceUnit     s_hVideo = 0;
static ILTVideo        *s_iVideo  = NULL;

// Device APIs

static bool LTDeviceVideo_Enable(LTDeviceVideo_Source source) {
    return s_iVideo->Enable(source);
}

static void LTDeviceVideo_Disable(LTDeviceVideo_Source source) {
    s_iVideo->Disable(source);
}

static bool LTDeviceVideo_Start(LTDeviceVideo_Channel channel) {
    return s_iVideo->Start(channel);
}

static void LTDeviceVideo_Stop(LTDeviceVideo_Channel channel) {
    s_iVideo->Stop(channel);
}

static void LTDeviceVideo_Pause(void) {
    s_iVideo->Pause();
}

static void LTDeviceVideo_Resume(void) {
    s_iVideo->Resume();
}

static void LTDeviceVideo_OnVideoEvent(LTDeviceVideo_Channel channel, LTDeviceVideo_EventProc *eventProc, void *clientData) {
    s_iVideo->OnVideoEvent(channel, eventProc, clientData);
}

static void LTDeviceVideo_NoVideoEvent(LTDeviceVideo_Channel channel, LTDeviceVideo_EventProc *eventProc) {
    s_iVideo->NoVideoEvent(channel, eventProc);
}

static void LTDeviceVideo_ReleaseVideoData(LTDeviceVideo_Channel channel, LTDeviceVideo_VideoData *videoData) {
    s_iVideo->ReleaseVideoData(channel, videoData);
}

static bool LTDeviceVideo_Capture(LTDeviceVideo_Channel channel, LTDeviceVideo_FrameType type) {
    return s_iVideo->Capture(channel, type);
}

static s32 LTDeviceVideo_CaptureSingle(LTDeviceVideo_Channel channel, LTDeviceVideo_FrameType type, u8 *destBuf, u32 destMaxLen) {
    return s_iVideo->CaptureSingle(channel, type, destBuf, destMaxLen);
}

static s32 LTDeviceVideo_CaptureCrop(LTDeviceVideo_Channel channel, LTDeviceVideo_FrameType type, u8 *destBuf, u32 cropWidth, u32 cropHeight, u32 centerX, u32 centerY) {
    return s_iVideo->CaptureCrop(channel, type, destBuf, cropWidth, cropHeight, centerX, centerY);
}

static void LTDeviceVideo_RequestIdrFrame(LTDeviceVideo_Channel channel) {
    return s_iVideo->RequestIdrFrame(channel);
}

static void LTDeviceVideo_Poll(LTDeviceVideo_Channel channel) {
    return s_iVideo->Poll(channel);
}

static bool LTDeviceVideo_GetParam(LTDeviceVideo_Param param, void *value) {
    return s_iVideo->GetParam(param, value);
}

static bool LTDeviceVideo_SetParam(LTDeviceVideo_Param param, const void *value) {
    return s_iVideo->SetParam(param, value);
}

// Device init and fini

static void LTDeviceVideoImpl_LibFini(void) {
    lt_destroyhandle(s_hVideo);
    s_hVideo = 0;
    s_iVideo = NULL;
    lt_closelibrary(s_driver);
    s_driver = NULL;
}

static bool LTDeviceVideoImpl_LibInit(void) {
    do {
        s_driver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceVideo", 0);
        if (!s_driver) break;
        s_hVideo = s_driver->CreateDeviceUnitHandle(0);
        if (!s_hVideo) break;
        s_iVideo = lt_gethandleinterface(ILTVideo, s_hVideo);
        return true;
    } while (0);

    LTDeviceVideoImpl_LibFini();
    LTLOG_YELLOWALERT("init.fail", "Fail to init video");
    return false;
}

/*******************************************************************************
 * Device Unit access and conversion:
 */

static u32 LTDeviceVideoImpl_GetNumDeviceUnits(void) {
    return s_driver->GetNumDeviceUnits();
}

static LTDeviceUnit LTDeviceVideoImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    return s_driver->CreateDeviceUnitHandle(nDeviceUnitNumber);
}

static int LTDeviceVideo_Run(int argc, const char ** argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    return 0;
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceVideo, LTDeviceVideo_Run, 2048)
    .Enable           = &LTDeviceVideo_Enable,
    .Disable          = &LTDeviceVideo_Disable,
    .Start            = &LTDeviceVideo_Start,
    .Stop             = &LTDeviceVideo_Stop,
    .Pause            = &LTDeviceVideo_Pause,
    .Resume           = &LTDeviceVideo_Resume,
    .OnVideoEvent     = &LTDeviceVideo_OnVideoEvent,
    .NoVideoEvent     = &LTDeviceVideo_NoVideoEvent,
    .ReleaseVideoData = &LTDeviceVideo_ReleaseVideoData,
    .Capture          = &LTDeviceVideo_Capture,
    .CaptureSingle    = &LTDeviceVideo_CaptureSingle,
    .CaptureCrop      = &LTDeviceVideo_CaptureCrop,
    .RequestIdrFrame  = &LTDeviceVideo_RequestIdrFrame,
    .Poll             = &LTDeviceVideo_Poll,
    .GetParam         = &LTDeviceVideo_GetParam,
    .SetParam         = &LTDeviceVideo_SetParam,
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  12-Feb-24   gallienus   created
 */
