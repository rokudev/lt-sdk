/*******************************************************************************
 * LTDeviceVideo Unit Test
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/device/video/LTDeviceVideo.h>
#include <tilt/TiltImpl.c>

// DEFINE_LTLOG_SECTION("test.dev.vid");
#define P(...)    LTLOG_DEBUG(__VA_ARGS__)
#define PLOG(...) LTLOG(__VA_ARGS__)

static struct Static {
    LTCore *core;
    LTDeviceVideo *video;
    LTOThread *thread;
    LTDeviceVideo_Source source;

    // unittest variables
    bool bCapture;
    u32  frameCount;
    bool bStream;
    u8   frame[0x80000];  // 512KB
    u8   cropFrame[160 * 160 * 3 / 2];  // 38,400 bytes for 160x160 NV12
    s32  cropResult;
} S;

static void VideoProc(LTDeviceVideo_Channel channel, LTDeviceVideo_Event event, LTDeviceVideo_VideoData *videoData, void *clientData) {
    LT_UNUSED(clientData);
    if (event == kLTDeviceVideo_Event_FrameReady) {
        switch (channel) {
            case kLTDeviceVideo_Channel_ImageHD :
            case kLTDeviceVideo_Channel_ImageSD :
                S.video->NoVideoEvent(kLTDeviceVideo_Channel_ImageHD, VideoProc);
                PLOG("jpeg", "vid chn %d type %d time %d len %u", channel, videoData->type, (s32)LTTime_GetMilliseconds(videoData->time), videoData->length);
                S.bCapture = true;
                S.video->ReleaseVideoData(channel, videoData);
                break;

            case kLTDeviceVideo_Channel_H264HD :
            case kLTDeviceVideo_Channel_H264SD :
                ++S.frameCount;
                PLOG("h264", "vid %d chn %d type %d seq %u time %d len %u data %p", S.frameCount, channel, videoData->type, videoData->sequence, (s32)LTTime_GetMilliseconds(videoData->time), videoData->length, videoData->address);
                S.video->ReleaseVideoData(channel, videoData);
                if (S.frameCount > 42) {  // GOP is 40
                    S.video->NoVideoEvent(kLTDeviceVideo_Channel_H264HD, VideoProc);
                    S.bStream = true;
                } else {
                    S.video->Poll(kLTDeviceVideo_Channel_H264HD);
                }
                break;

            default:
                ;
        }

    } else if (event == kLTDeviceVideo_Event_FrameDrop) {
        LTLOG_YELLOWALERT("frame.drop", "vid chn %d", channel);

    } else if (event == kLTDeviceVideo_Event_FrameFail) {
        LTLOG_YELLOWALERT("frame.fail", "vid chn %d", channel);
    }
}

static void Stream(void *clientData) {
    LTDeviceVideo_Channel channel = *(LTDeviceVideo_Channel *)clientData;
    S.video->OnVideoEvent(channel, VideoProc, clientData);
    S.video->Poll(kLTDeviceVideo_Channel_H264HD);
}

static void TestStream(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "stream start");
    S.frameCount = 0;
    S.bStream = false;
    LTDeviceVideo_Channel channel = kLTDeviceVideo_Channel_H264HD;
    TILT_EXPECT_TRUE(trc, S.video->Start(channel), "fail to start h264 channel");
    S.thread->API->QueueTaskProc(S.thread, Stream, NULL, &channel);
    int cnt = 0;
    while (!S.bStream && cnt < 600) {
        S.thread->API->Sleep(LTTime_Milliseconds(10));
        ++cnt;
    }
    TILT_EXPECT_TRUE(trc, S.bStream, "fail to stream");
    S.video->Stop(channel);
    TILT_MESSAGE(trc, "stream done");
}

static void Capture(void *clientData) {
    LTDeviceVideo_Channel channel = *(LTDeviceVideo_Channel *)clientData;
    S.video->OnVideoEvent(channel, VideoProc, clientData);
    S.video->Capture(channel, kLTDeviceVideo_Frame_Jpeg);
}

static void TestCapture(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "capture start");
    S.bCapture = false;
    LTDeviceVideo_Channel channel = kLTDeviceVideo_Channel_ImageHD;
    TILT_EXPECT_TRUE(trc, S.video->Start(channel), "fail to start Jpeg channel");
    S.thread->API->QueueTaskProc(S.thread, Capture, NULL, &channel);
    int cnt = 0;
    while (!S.bCapture && cnt < 200) {
        S.thread->API->Sleep(LTTime_Milliseconds(10));
        ++cnt;
    }
    TILT_EXPECT_TRUE(trc, S.bCapture, "fail to capture");
    S.video->Stop(channel);
    TILT_MESSAGE(trc, "capture done");
}

static void TestCaptureCrop(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "crop capture test start");
    
    // Start the ISP channel for crop capture
    TILT_EXPECT_TRUE(trc, S.video->Start(kLTDeviceVideo_Channel_IspHD), "fail to start ISP channel");
    // Wait for ISP to stabilize
    S.thread->API->Sleep(LTTime_Milliseconds(500));
    
    // Test 1: Basic center crop
    TILT_MESSAGE(trc, "Test 1: Basic center crop 160x160");
    S.cropResult = S.video->CaptureCrop(
        kLTDeviceVideo_Channel_IspHD,
        kLTDeviceVideo_Frame_Yuv420,
        S.cropFrame,
        160, 160,  // 160x160 crop
        960, 540   // Center of 1920x1080
    );
    
    TILT_EXPECT_TRUE(trc, S.cropResult > 0, "Center crop should succeed");
    u32 expectedSize = 160 * 160 * 3 / 2;  // 38,400 bytes
    TILT_EXPECT_TRUE(trc, (u32)S.cropResult == expectedSize, 
        "Center crop size should be %u bytes (got %d)", expectedSize, S.cropResult);
    
    // Validate crop data is not all zeros
    bool hasData = false;
    for (u32 i = 0; i < expectedSize && !hasData; i++) {
        if (S.cropFrame[i] != 0) hasData = true;
    }
    TILT_EXPECT_TRUE(trc, hasData, "Crop should contain actual image data");
    
    // Test 2: Corner crops
    struct {
        u32 centerX, centerY;
        const char *description;
    } cornerTests[] = {
        {80, 80, "top-left corner"},
        {1840, 80, "top-right corner"},
        {80, 1000, "bottom-left corner"},
        {1840, 1000, "bottom-right corner"},
    };
    
    for (u32 i = 0; i < sizeof(cornerTests) / sizeof(cornerTests[0]); i++) {
        TILT_MESSAGE(trc, "Test 2.%u: Corner crop at %s", i+1, cornerTests[i].description);
        
        S.cropResult = S.video->CaptureCrop(
            kLTDeviceVideo_Channel_IspHD,
            kLTDeviceVideo_Frame_Yuv420,
            S.cropFrame,
            160, 160,
            cornerTests[i].centerX, cornerTests[i].centerY
        );
        
        TILT_EXPECT_TRUE(trc, S.cropResult > 0, 
            "Corner crop at %s should succeed", cornerTests[i].description);
        TILT_EXPECT_TRUE(trc, (u32)S.cropResult == expectedSize, 
            "Corner crop size at %s should be %u bytes (got %d)", 
            cornerTests[i].description, expectedSize, S.cropResult);
    }
    
    // Test 3: Different crop sizes
    struct {
        u32 width, height;
        const char *description;
    } sizeTests[] = {
        {128, 128, "small square 128x128"},
        {240, 80, "very wide 240x80"},
        {80, 240, "very tall 80x240"},
    };
    
    for (u32 i = 0; i < sizeof(sizeTests) / sizeof(sizeTests[0]); i++) {
        TILT_MESSAGE(trc, "Test 3.%u: Size test %s", i+1, sizeTests[i].description);
        
        u32 testExpectedSize = sizeTests[i].width * sizeTests[i].height * 3 / 2;
        if (testExpectedSize <= sizeof(S.cropFrame)) {  // Only test if buffer is large enough
            S.cropResult = S.video->CaptureCrop(
                kLTDeviceVideo_Channel_IspHD,
                kLTDeviceVideo_Frame_Yuv420,
                S.cropFrame,
                sizeTests[i].width, sizeTests[i].height,
                960, 540  // Center
            );
            
            TILT_EXPECT_TRUE(trc, S.cropResult > 0, 
                "Size test %s should succeed", sizeTests[i].description);
            TILT_EXPECT_TRUE(trc, (u32)S.cropResult == testExpectedSize, 
                "Size test %s should be %u bytes (got %d)", 
                sizeTests[i].description, testExpectedSize, S.cropResult);
        }
    }
    
    // Test 4: Error conditions
    TILT_MESSAGE(trc, "Test 4: Error condition tests");
    
    // Test null buffer
    S.cropResult = S.video->CaptureCrop(
        kLTDeviceVideo_Channel_IspHD,
        kLTDeviceVideo_Frame_Yuv420,
        NULL,  // Invalid buffer
        160, 160, 960, 540
    );
    TILT_EXPECT_TRUE(trc, S.cropResult < 0, "Null buffer should fail");
    
    // Test zero dimensions
    S.cropResult = S.video->CaptureCrop(
        kLTDeviceVideo_Channel_IspHD,
        kLTDeviceVideo_Frame_Yuv420,
        S.cropFrame,
        0, 160,  // Invalid width
        960, 540
    );
    TILT_EXPECT_TRUE(trc, S.cropResult < 0, "Zero width should fail");

    S.cropResult = S.video->CaptureCrop(
        kLTDeviceVideo_Channel_IspHD,
        kLTDeviceVideo_Frame_Yuv420,
        S.cropFrame,
        160, 0,  // Invalid height
        960, 540
    );
    TILT_EXPECT_TRUE(trc, S.cropResult < 0, "Zero height should fail");
    
    S.video->Stop(kLTDeviceVideo_Channel_IspHD);
    TILT_MESSAGE(trc, "crop capture test done");
}

static const TiltImplTestSpecifier s_tests[] = {
    { TestCapture,      "capture",     "Test capture",      0 },
    { TestStream,       "stream",      "Test stream",       0 },
    { TestCaptureCrop,  "crop",        "Test crop capture", 0 },
};

static void BeforeAllTestsHook(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "enable video source");
    TILT_EXPECT_TRUE(trc, S.video->Enable(S.source), "fail to enable video source");
}

static void AfterAllTestsHook(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "disable video source");
    S.video->Disable(S.source);
}

static TiltImplTestHooks s_testHooks = {
    .BeforeAllTests = BeforeAllTestsHook,
    .AfterAllTests  = AfterAllTestsHook,
};

static void UnitTestLTDeviceVideoImpl_LibFini(void) {
    lt_destroyobject(S.thread);
    lt_closelibrary(S.video);
    lt_memset(&S, 0, sizeof(struct Static));
    ShutdownTilt();
}

static bool UnitTestLTDeviceVideoImpl_LibInit(void) {
    lt_memset(&S, 0, sizeof(struct Static));
    S.core = LT_GetCore();
    S.source = kLTDeviceVideo_Source_0;
    // S.source = kLTDeviceVideo_Source_Test;
    do {
        if (!(S.video = lt_openlibrary(LTDeviceVideo))) break;
        if (!(S.thread = lt_createobject(LTOThread))) break;
        S.thread->API->SetStackSize(S.thread, 2048);
        S.thread->API->Start(S.thread, "videotest", NULL, NULL);
        return InitializeTilt(s_tests, sizeof(s_tests) / sizeof(s_tests[0]), &s_testHooks);
    } while (0);

    LTLOG_YELLOWALERT("error", "fail to init");
    UnitTestLTDeviceVideoImpl_LibFini();
    return false;
}

void RunSingle(void) {
    S.video->Enable(S.source);
    S.video->Start(kLTDeviceVideo_Channel_ImageHD);
    S.video->Start(kLTDeviceVideo_Channel_H264HD);
    // S.video->Start(kLTDeviceVideo_Channel_ImageSD);
    // S.video->Start(kLTDeviceVideo_Channel_H264SD);

    #if 0
    // capture image
    S.thread->API->Sleep(LTTime_Milliseconds(500));
    LTDeviceVideo_ISPTuningMode m = kLTDeviceVideo_ISPTuningMode3;
    S.video->SetParam(kLTDeviceVideo_Param_ISPTuningMode, &m);
    S.thread->API->Sleep(LTTime_Milliseconds(500));
    S.video->CaptureSingle(kLTDeviceVideo_Channel_ImageHD, kLTDeviceVideo_Frame_Jpeg, S.frame, sizeof(S.frame));
    // S.video->CaptureSingle(kLTDeviceVideo_Channel_ImageSD, kLTDeviceVideo_Frame_Jpeg, S.frame, sizeof(S.frame));
    #else
    // stream
    LTDeviceVideo_Channel channel = kLTDeviceVideo_Channel_H264HD;
    S.thread->API->QueueTaskProc(S.thread, Capture, NULL, &channel);
    int cnt = 0;
    while (!S.bStream && cnt < 5000) {
        S.thread->API->Sleep(LTTime_Milliseconds(1));
        ++cnt;
    }
    #endif

    // stop
    S.video->Stop(kLTDeviceVideo_Channel_ImageHD);
    S.video->Stop(kLTDeviceVideo_Channel_H264HD);
    // S.video->Stop(kLTDeviceVideo_Channel_ImageSD);
    // S.video->Stop(kLTDeviceVideo_Channel_H264HD);
    S.video->Disable(S.source);
}

/* ltrun UnitTestLTDeviceVideo */
static int UnitTestLTDeviceVideo_Run(int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    RunSingle();
    return 0;
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceVideo, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceVideo, UnitTestLTDeviceVideo_Run, 4096) LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(UnitTestLTDeviceVideo, (ITilt));

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  23-Feb-24   gallienus   created
 */
