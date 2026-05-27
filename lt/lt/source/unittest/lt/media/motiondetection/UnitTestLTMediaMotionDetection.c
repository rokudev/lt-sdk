/*******************************************************************************
 * source/unittest/lt/media/motiondetection/UnitTestLTMediaMotionDetection.c
 *
 * LTMediaMotionDetection Unit Test
 *
 * Test that:
 * 1. MotionDetectionEngine can start and stop successfully
 * 2. MotionDetectionEngine can be re-started and stopped successfully
 * 3. Motion frame can be detected correctly (boolean test).
 * 4. Multiple Motion frames can be detected correctly (boolean test).
 * 5. Single Motion ROI can be correctly calculated for a range of inputs and regions.
 * 6. Multi Motion ROIs can be correctly calculated for a range of inputs and regions.
 * 7. SetMotionZones logic (the scaling up logic from the BitGrid to the full resolution motion zone bitmap) works correctly
 * 8. Motion event can be correctly detected and stopped.
 * 9. Frame selection and snapshot events work as expected.
 *
 * Test note for successful result:
 *
 * 1. A longer TiltEngine timeout is required for this test in order to complete
 *    all the scans and joins. See TILT_LIBRARY_TIMEOUT setting below.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/media/motiondetection/LTMediaMotionDetection.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/product/config/LTProductConfig.h>
#define TILT_LIBRARY_TIMEOUT 180
#include <tilt/TiltImpl.c>
#include <lt/core/LTCore.h>

/* Enable the debug variant to get extra debug logging during development. Do not enable in committed code. */
// #define TILT_MSG_DEBUG(...) TILT_MESSAGE(__VA_ARGS__)
#define TILT_MSG_DEBUG(...)

#define BYTES_PER_WORD  sizeof(LTMediaMotionDetection_MotionZoneBitmapWord) // number of bytes in a single motion zone bitmap word
#define BITMAP_WORD_LEN (BYTES_PER_WORD * 8) // number of bits in a single motion zone bitmap word

static LTCore *s_core                   = NULL;
static LTDeviceMedia *s_deviceMedia     = NULL;
static LTMediaMotionDetection *s_motion = NULL;
static ILTEvent *s_IEvent               = NULL;
static ILTThread *s_IThread             = NULL;
static LTThread s_testThread;
static LTTime lastMotionEventTime;
static ILTThread *s_IZoneTestThread     = NULL;
static LTThread s_zoneTestThread;

typedef enum {
    kLTTestMotionFlag_None              = 0,
    kLTTestMotionFlag_ProcDone          = 1 << 0,  /**< Motion library MotionProc has finished processing */
    kLTTestMotionFlag_Started           = 1 << 1,
    kLTTestMotionFlag_Stopped           = 1 << 2,
    kLTTestMotionFlag_Metadata          = 1 << 3,
    kLTTestMotionFlag_SnapshotTriggered = 1 << 4,  /**< Snapshot triggered by motion library */
    kLTTestMotionFlag_SnapshotDelivered = 1 << 5,  /**< Best snapshot delivered from motion library */
    kLTTestMotionFlag_Unknown           = 1 << 6,  /**< Unknown event was received */
    kLTTestMotionFlag_Duplicate         = 1 << 7,  /**< Duplicate of an existing event was received */
} TestMotionEventFlags;

typedef enum {
    kLTTestZoneFlag_None              = 0,
    kLTTestZoneFlag_ProcDone          = 1 << 0,  /**< ZoneStateProc has finished processing */
    kLTTestZoneFlag_SetMotionZone     = 1 << 1,
    kLTTestZoneFlag_EnableMotionZone  = 1 << 2,
    kLTTestZoneFlag_Unknown           = 1 << 3,  /**< Unknown event was received */
    kLTTestZoneFlag_Duplicate         = 1 << 4,  /**< Duplicate of an existing event was received */
} TestZoneStateEventFlags;

static LTMediaMotionDetectionAlgorithm *s_algorithm = NULL; /* Unit test reference to LTMediaMotionDetection's algorithm object. */
u32 s_differenceThreshold          = 0;
u32 MinMotionFramesForEvent      = 0; /**< Minimum frames with motion detected before a motion event starts */
u32 MinFramesUntilEventEnd       = 0; /**< Minimum frames with no motion before current motion event stops */
u32 MaxFramesUntilFirstSnapshot  = 0; /**< Maximum frames until first snapshot notification */
u32 MaxFramesForBestSnapshot     = 0; /**< Maximum frames until latest snapshot is notified */
u32 MinFramesMotionEventDuration = 0; /**< Minimum event frames before a started motion event can be stopped */
u8 SnapshotsUploaded             = 0; /**< Number of snapshots uploaded in the current motion event */
u8 MaxSnapshotsPerMotionEvent    = 0; /**< Maximum snapshots that can be notified within a single motion event */
u32 DownsamplingScaleFactor      = 1;
u32 FramesOfMotion               = 0;
u32 FramesNoMotion               = 0;
u32 FramesSinceMotionStart       = 0;
LTMediaMotionState MotionState   = kLTMediaMotionState_Stopped;
LTAtomic ReceivedMotionEvents;
LTAtomic ReceivedZoneStateEvents;
LTMediaMotionDetection_MotionZoneBitmap *pMotionZonesBitmap = NULL;
LTMediaData *s_baseMotionOutput = NULL;
bool s_sourcesLeftOpen          = false;
u32 s_motionProcDelays          = 0;
u32 s_maxMotionProcDelays       = 1;
u32 s_motionProcSleepTime       = 20;

// Non-production values are used here to speed up testing:
// Changing these values may lead to unittest failures!!!
float s_heatmapCap = 50.0;
float s_heatmapThreshold = 30.0;
float s_heatmapGain = 1.0;
float s_heatmapDrop = 0.5;

/* Constants defined by product configuration. */
/* 'BitGrid' refers to the user-specified zone configuration in the Smart Home app, which is scaled up to the
 * native shape of motion data for internal motion zone calculations. */
static u8 BitGridWidth;
static u8 BitGridHeight;
static u32 nBitmapBytes;
LT_SIZE bitGridSize;
static u32 HorizontalMoxels;
static u32 VerticalMoxels;

/*******************************************************************************
 * LTDeviceMedia library mocks
 ******************************************************************************/

typedef struct {
    bool motionNotSnapshot;
    bool started;
    LTEvent mediaEvent;
} UnitTestMediaSourceContext;

static const LTArgsDescriptor s_MediaDataEventArgs = {1, {kLTArgType_pointer}};
static LTEvent s_hMotionEvent;
static LTEvent s_hSnapshotEvent;

/* The motion library is only expected to open at most one instance of each source */
static LTMediaSource s_hMotionMediaSource = 0;
static LTMediaSource s_hSnapshotMediaSource = 0;
static bool s_bTestFailedInHook = false;
static LTMediaMotionDetection_Regions *s_pLastMotionBounds;

static void OnZoneStateEvent(LTMediaZoneState ZoneState, void *clientData) {
    LT_UNUSED(clientData);
    TestZoneStateEventFlags eventType = kLTTestZoneFlag_Unknown;
    switch (ZoneState) {
        case kLTMediaMotionState_EnableMotionZone:
            eventType = kLTTestZoneFlag_EnableMotionZone;
            break;
        case kLTMediaMotionState_SetMotionZone:
            eventType = kLTTestZoneFlag_SetMotionZone;
            break;
        default:
            /* Nothing to do in the other states. */
            break;
    }
    if (LTAtomic_Load(&ReceivedZoneStateEvents) & eventType) {
        LTAtomic_FetchOr(&ReceivedZoneStateEvents, kLTTestZoneFlag_Duplicate);
    } else {
        LTAtomic_FetchOr(&ReceivedZoneStateEvents, eventType);
    }
}

static LTMediaEncoding UnitTestHook_GetMediaEncoding(LTMediaSource hSource) {
    LT_UNUSED(hSource);
    LTLOG_YELLOWALERT("hook.GetMediaEncoding", "Unsupported function called by unit under test");
    return (LTMediaEncoding){0};
}

static void UnitTestHook_OnModeChangeEvent(LTMediaSource hSource, LTMediaSource_OnModeChangeEventProc *pCallback, void *pClientData) {
    LT_UNUSED(hSource);
    LT_UNUSED(pCallback);
    LT_UNUSED(pClientData);

    // We know the motion library uses this API but it isn't part of the unit tests so lightly log it
    LTLOG_DEBUG("hook.OnModeChangeEvent", "Motion library registered Mode Change Event");
}

static void UnitTestHook_NoModeChangeEvent(LTMediaSource hSource, LTMediaSource_OnModeChangeEventProc *pCallback) {
    LT_UNUSED(hSource);
    LT_UNUSED(pCallback);

    // We know the motion library uses this API but it isn't part of the unit tests so lightly log it
    LTLOG_DEBUG("hook.NoModeChangeEvent", "Motion library un-registered Mode Change Event");
}

static void UnitTestHook_SetTargetBitrate(LTMediaSource hSource, u32 nTargetBitrate) {
    LT_UNUSED(hSource);
    LT_UNUSED(nTargetBitrate);
    LTLOG_YELLOWALERT("hook.SetTargetBitrate", "Unsupported function called by unit under test");
}

static bool UnitTestHook_SetAeComp(LTMediaSource hSource, int comp) {
    LT_UNUSED(hSource);
    LT_UNUSED(comp);
    LTLOG_YELLOWALERT("hook.SetAeComp", "Unsupported function called by unit under test");
    return false;
}

static void UnitTestHook_OnMediaEvent(LTMediaSource hSource, LTMediaSource_OnMediaEventProc *pCallback, void *pClientData) {
    if (hSource == s_hMotionMediaSource || hSource == s_hSnapshotMediaSource) {
        UnitTestMediaSourceContext *source = s_core->ReserveHandlePrivateData(hSource);
        if (!source) return;
        s_IEvent->RegisterForEvent(source->mediaEvent, pCallback, NULL, pClientData, false);
        s_core->ReleaseHandlePrivateData(hSource, source);
    } else {
        LTLOG_YELLOWALERT("hook.OnMediaEvent", "Library used unexpected handle: 0x%08lx", LT_PLT_HANDLE(hSource));
        s_bTestFailedInHook = true;
    }
}

static void UnitTestHook_NoMediaEvent(LTMediaSource hSource, LTMediaSource_OnMediaEventProc *pCallback) {
    if (hSource == s_hMotionMediaSource || hSource == s_hSnapshotMediaSource) {
        UnitTestMediaSourceContext *source = s_core->ReserveHandlePrivateData(hSource);
        s_IEvent->UnregisterFromEvent(source->mediaEvent, pCallback);
        s_core->ReleaseHandlePrivateData(hSource, source);
    } else {
        LTLOG_YELLOWALERT("hook.NoMediaEvent", "Library used unexpected handle: 0x%08lx", LT_PLT_HANDLE(hSource));
        s_bTestFailedInHook = true;
    }
}

static void UnitTestHook_Start(LTMediaSource hSource) {
    if (hSource == s_hMotionMediaSource) {
        UnitTestMediaSourceContext *source = s_core->ReserveHandlePrivateData(hSource);
        if (!source || source->started) {
            LTLOG_YELLOWALERT("hook.Start", "Library restarted media source without stopping it");
            s_bTestFailedInHook = true;
        } else {
            source->started = true;
        }
        s_core->ReleaseHandlePrivateData(hSource, source);
    } else {
        LTLOG_YELLOWALERT("hook.Start", "Library used unexpected handle: 0x%08lx", LT_PLT_HANDLE(hSource));
        s_bTestFailedInHook = true;
    }
}

static void UnitTestHook_Stop(LTMediaSource hSource) {
    if (hSource == s_hMotionMediaSource || hSource == s_hSnapshotMediaSource) {
        UnitTestMediaSourceContext *source = s_core->ReserveHandlePrivateData(hSource);
        if (!source || !source->started) {
            LTLOG_YELLOWALERT("hook.Stop", "Library stopped media source without starting it");
            s_bTestFailedInHook = true;
        } else {
            source->started = false;
        }
        s_core->ReleaseHandlePrivateData(hSource, source);
    } else {
        LTLOG_YELLOWALERT("hook.Stop", "Library used unexpected handle: 0x%08lx", LT_PLT_HANDLE(hSource));
        s_bTestFailedInHook = true;
    }
}

static void NotifySnapshotFromTestThread(void *pClientData) {
    // Update event flags to indicate that snapshot was requested during this frame
    LTAtomic_FetchOr(&ReceivedMotionEvents, kLTTestMotionFlag_SnapshotTriggered);

    // The motion proc will get a chance to process the snapshot in the next frame
    s_IEvent->NotifyEvent(VOIDPTR_TO_LTHANDLE(pClientData), NULL);
}

static void UnitTestHook_Trigger(LTMediaSource hSource) {
    if (hSource == s_hSnapshotMediaSource) {
        UnitTestMediaSourceContext *source = s_core->ReserveHandlePrivateData(hSource);
        if (!source || !source->started) {
            LTLOG_YELLOWALERT("hook.Trigger", "Library triggered media source without starting it");
            s_bTestFailedInHook = true;
        } else {
            // Currently running in motion thread context via Trigger() call.
            // Hand off event generation to the test thread to decouple from motion thread.
            s_IThread->QueueTaskProcIfRequired(s_testThread, NotifySnapshotFromTestThread, NULL, LTHANDLE_TO_VOIDPTR(source->mediaEvent));
        }
        s_core->ReleaseHandlePrivateData(hSource, source);
    } else {
        LTLOG_YELLOWALERT("hook.Trigger", "Library used unexpected handle: 0x%08lx", LT_PLT_HANDLE(hSource));
        s_bTestFailedInHook = true;
    }
}

static void UnitTestHook_Destroy(LTMediaSource hSource) {
    /* Media sources are auto-stopped on destroy */
    UnitTestMediaSourceContext *source = s_core->ReserveHandlePrivateData(hSource);
    if (source->started) {
        LTLOG_YELLOWALERT("hook.Destroy", "Handle destroyed before stopping media source");
        UnitTestHook_Stop(hSource);
    }
    s_core->ReleaseHandlePrivateData(hSource, source);

    if (hSource == s_hMotionMediaSource) s_hMotionMediaSource = 0;
    else if (hSource == s_hSnapshotMediaSource) s_hSnapshotMediaSource = 0;
    else {
        LTLOG_YELLOWALERT("hook.Destroy", "Library used unexpected handle: 0x%08lx", LT_PLT_HANDLE(hSource));
        s_bTestFailedInHook = true;
    }
}

define_LTLIBRARY_INTERFACE(ILTMediaSource, UnitTestHook_Destroy) {
    .GetMediaEncoding  = &UnitTestHook_GetMediaEncoding,
    .OnMediaEvent      = &UnitTestHook_OnMediaEvent,
    .NoMediaEvent      = &UnitTestHook_NoMediaEvent,
    .OnModeChangeEvent = &UnitTestHook_OnModeChangeEvent,
    .NoModeChangeEvent = &UnitTestHook_NoModeChangeEvent,
    .Start             = &UnitTestHook_Start,
    .Stop              = &UnitTestHook_Stop,
    .Trigger           = &UnitTestHook_Trigger,
    .SetTargetBitrate  = &UnitTestHook_SetTargetBitrate,
    .SetAeComp         = &UnitTestHook_SetAeComp,
} LTLIBRARY_DEFINITION;

static bool UnitTestHook_DeviceMediaSetProperty(const char *name, const void *value) {
    if (lt_strcmp(name, "motion.bounds") == 0) {
        /* Save a copy of the last ROI data received from the library */
        if (s_pLastMotionBounds) {
            lt_free(s_pLastMotionBounds);
            s_pLastMotionBounds = NULL;
        }

        if (value) s_pLastMotionBounds = LTMediaMotionDetection_CloneRegionList((const LTMediaMotionDetection_Regions *)value);
        return true;
    }

    // Pass through other properties
    return s_deviceMedia->SetProperty(name, value);
}

static LTMediaSource UnitTestHook_DeviceMediaOpenSource(LTMediaFormat *pFormat) {
    bool motionNotSnapshot = true;

    if (pFormat->nKind == kLTMediaKind_Motion && pFormat->nEncoding == kLTMediaEncoding_Motion) {
        if (s_hMotionMediaSource != 0) {
            LTLOG_YELLOWALERT("hook.open.motion", "Motion media source already opened");
            return 0;
        }

    } else if (pFormat->nEncoding == kLTMediaEncoding_Jpeg) {
        if (s_hSnapshotMediaSource != 0) {
            LTLOG_YELLOWALERT("hook.open.snapshot", "Snapshot media source already opened");
            return 0;
        }
        motionNotSnapshot = false;

    } else {
        // The library is not expected to open any other sources, so log and pass through
        LTLOG_YELLOWALERT("hook.open.source", "Unexpected media format");
        return s_deviceMedia->OpenSource(pFormat);
    }

    // Create a handle for the selected media source
    LTMediaSource mediaHandle = s_core->CreateHandle((LTInterface *)&s_ILTMediaSource, sizeof(UnitTestMediaSourceContext));
    if (!mediaHandle) return 0;

    UnitTestMediaSourceContext *source = s_core->ReserveHandlePrivateData(mediaHandle);
    lt_memset(source, 0, sizeof(UnitTestMediaSourceContext));
    source->motionNotSnapshot = motionNotSnapshot;
    source->started = !motionNotSnapshot;
    if (motionNotSnapshot) {
        source->mediaEvent = s_hMotionEvent;
        s_hMotionMediaSource = mediaHandle;
    } else {
        source->mediaEvent = s_hSnapshotEvent;
        s_hSnapshotMediaSource = mediaHandle;
    }
    s_core->ReleaseHandlePrivateData(mediaHandle, source);

    return mediaHandle;
}

static LTLibrary * UnitTestHook_LtDeviceMedia(LTCore_LibraryHookSite site, LTLibrary *pLibrary, const char *pLibraryName) {
    LT_UNUSED(pLibraryName);
    if (site == kLTCore_LibraryHookSite_AfterOpen) {
        // Opening LTDeviceMedia
        LTDeviceMedia * hookedLib = lt_malloc(sizeof(LTDeviceMedia));
        lt_memcpy(hookedLib, pLibrary, sizeof(LTDeviceMedia));

        // Remap API calls that the test needs to intercept
        hookedLib->SetProperty = UnitTestHook_DeviceMediaSetProperty;
        hookedLib->OpenSource = UnitTestHook_DeviceMediaOpenSource;

        // Library types are const
        return (LTLibrary *)hookedLib;

    }
    // Not hooking close or Substitute for LTDeviceMedia; nothing else to do here
    return pLibrary;
}

static void DispatchMotionDataEventCB(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    LT_UNUSED(hEvent);

    LTMediaData *pClientPacket = LTArgs_pointerAt(0, pEventArgs);
    if (!pClientPacket) {
        LTLOG_REDALERT("dispatch.motion.cb.noclientpacket", "No client packet in DispatchMotionDataEventCB!");
        return;
    }

    ((LTMediaSource_OnMediaEventProc *)pEventProc)(kLTMediaEvent_Data, pClientPacket, pEventProcClientData);
    lastMotionEventTime = LT_GetCore()->GetKernelTime();

    LTAtomic_FetchOr(&ReceivedMotionEvents, kLTTestMotionFlag_ProcDone);
    LTLOG_DEBUG("motion.data.procdone", "Motion Data Event proc done");
}

static void DispatchJpegDataEventCB(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    LT_UNUSED(hEvent);
    LT_UNUSED(pEventArgs);

    // The contents of the snapshot are not relevant, and can be zero sized
    LTMediaData clientPacket = {0};
    ((LTMediaSource_OnMediaEventProc *)pEventProc)(kLTMediaEvent_Data, &clientPacket, pEventProcClientData);
}

static bool WaitForStartDetectionEngine(const TiltImplReportingCallbacks *trc, bool bTakeSnapshot) {
    // Helper function to start the engine and wait for the internal pause to finish
    bool engineStarted = s_motion->StartDetectionEngine(bTakeSnapshot);
    if (engineStarted) {
        TILT_MESSAGE(trc, "WaitForStartDetectionEngine. Sleeping to allow motion engine to unpause after startup.");
        s_IThread->Sleep(LTTime_Seconds(3));
    } else {
        TILT_REPORT_FAILURE(trc, "Error: Motion Detection Engine failed to start!");
    }
    return engineStarted;
}

/*******************************************************************************
 * Unit test cases
 ******************************************************************************/

/* ===============================================================================================
    Test that MotionDetectionEngine can (re-)start and stop successfully
=================================================================================================*/
static void MotionStartStopTest(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "%s - Start. =====================", trc->GetTestName());
    bool bStarted = WaitForStartDetectionEngine(trc, true);
    TILT_ASSERT_TRUE(trc, bStarted, "Error: Motion Detection Engine is not running!");

    s_motion->StopDetectionEngine();
    bStarted = s_motion->IsDetectionEngineRunning();
    TILT_ASSERT_FALSE(trc, bStarted, "Error: Motion Detection Engine failed to stop!");
    TILT_MESSAGE(trc, "%s - Finish. =====================", trc->GetTestName());
}

/* ===============================================================================================
    Test that Motion frame can be detected correctly for single motion ROI (boolean test).
=================================================================================================*/
static void CheckFrameForMotionTest(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "%s - Start. =====================", trc->GetTestName());
    TILT_ASSERT_TRUE(trc, WaitForStartDetectionEngine(trc, true), "Error: Motion Detection Engine is not running!");

    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);

    int test_case;

    /* Set motion algorithm to use single ROI detection for this test. */
    s_algorithm->API->SetROICalculationStrategy(s_algorithm, kLTMediaMotionROICalculationStrategy_SingleROI);

    // Test 1 ------------------------------------------------------------
    test_case = 1;
    bool isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_FALSE(trc, isMotion, "Error: %s case %d - No motion expected!", trc->GetTestName(), test_case);

    // Test 2 ------------------------------------------------------------
    test_case = 2;
    s_baseMotionOutput->pData[0] = s_differenceThreshold;
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_TRUE(trc, isMotion, "Error: %s case %d - Motion expected!", trc->GetTestName(), test_case);

    // Test 3 ------------------------------------------------------------
    test_case = 3;
    s_baseMotionOutput->pData[0] = s_differenceThreshold - 1;
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_FALSE(trc, isMotion, "Error: %s case %d - No motion expected!", trc->GetTestName(), test_case);

    // Test 4 ------------------------------------------------------------
    test_case = 4;
    s_baseMotionOutput->pData[s_baseMotionOutput->nDataLen - 1] = s_differenceThreshold;
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_TRUE(trc, isMotion, "Error: %s case %d - Motion expected!", trc->GetTestName(), test_case);

    // Test 5 ------------------------------------------------------------
    test_case = 5;
    s_baseMotionOutput->pData[0] = s_differenceThreshold;
    s_baseMotionOutput->pData[s_baseMotionOutput->nDataLen - 1] = s_differenceThreshold;
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_TRUE(trc, isMotion, "Error: %s case %d - Motion expected!", trc->GetTestName(), test_case);

    s_motion->StopDetectionEngine();
    TILT_MESSAGE(trc, "%s - Finish. =====================", trc->GetTestName());
}

/* ===============================================================================================
    Test that Motion frame can be detected correctly for Multiple Motion ROIs (boolean test).
=================================================================================================*/
static void MultipleMotionTest(const TiltImplReportingCallbacks *trc, bool UseMaxPooling, u32 scalar);

static void CheckFrameForMultipleMotionTest(const TiltImplReportingCallbacks *trc) {
    bool UseMaxPooling = true;
    u32 scalar = 1;
    MultipleMotionTest(trc, UseMaxPooling, scalar);

    UseMaxPooling = false;
    scalar = DownsamplingScaleFactor * DownsamplingScaleFactor;
    TILT_MESSAGE(trc, "DownsamplingScaleFactor: %d, scalar: %d; s_differenceThreshold: %d ", DownsamplingScaleFactor, scalar, s_differenceThreshold);
    MultipleMotionTest(trc, UseMaxPooling, scalar);

    s_algorithm->API->SetDownsamplingStrategy(s_algorithm, kLTMediaMotionDownsamplingStrategy_MaxPooling);
}

static void MultipleMotionTest(const TiltImplReportingCallbacks *trc, bool bUseMaxPooling,  u32 scalar) {
    TILT_MESSAGE(trc, "%s (%s pooling) - Start. =====================", trc->GetTestName(), bUseMaxPooling ? "Max" : "Avg");

    /* Set motion algorithm downsampling strategy - only max pooling and average pooling are supported. */
    LTMediaMotionDownsamplingStrategy downsamplingStrategy = bUseMaxPooling ?
                                                             kLTMediaMotionDownsamplingStrategy_MaxPooling :
                                                             kLTMediaMotionDownsamplingStrategy_AveragePooling;
    s_algorithm->API->SetDownsamplingStrategy(s_algorithm, downsamplingStrategy);

    /* Set motion algorithm to use multi ROI detection for this test. */
    s_algorithm->API->SetROICalculationStrategy(s_algorithm, kLTMediaMotionROICalculationStrategy_MultipleROIs);
    TILT_ASSERT_TRUE(trc, WaitForStartDetectionEngine(trc, true), "Error: Motion Detection Engine is not running!");

    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);

    int test_case;

    // Test 1 ------------------------------------------------------------
    test_case = 1;
    bool isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_FALSE(trc, isMotion, "Error: %s case %d - No motion expected!", trc->GetTestName(), test_case);

    // Test 2 ------------------------------------------------------------
    test_case = 2;
    s_baseMotionOutput->pData[0] = scalar * s_differenceThreshold;
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_FALSE(trc, isMotion, "Error: %s case %d - No motion expected!!", trc->GetTestName(), test_case);

    // Test 3 ------------------------------------------------------------
    test_case = 3;
    s_baseMotionOutput->pData[0] = scalar * (s_differenceThreshold - 1);
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_FALSE(trc, isMotion, "Error: %s case %d - No motion expected!", trc->GetTestName(), test_case);

    // Test 4 ------------------------------------------------------------
    test_case = 4;
    s_baseMotionOutput->pData[s_baseMotionOutput->nDataLen - 1] = scalar * s_differenceThreshold;
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_FALSE(trc, isMotion, "Error: %s case %d -  No motion expected!!", trc->GetTestName(), test_case);

    // Test 5 ------------------------------------------------------------
    test_case = 5;
    s_baseMotionOutput->pData[0] = scalar * s_differenceThreshold;
    s_baseMotionOutput->pData[s_baseMotionOutput->nDataLen - 1] = scalar * s_differenceThreshold;
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_FALSE(trc, isMotion, "Error: %s case %d -  No motion expected!!", trc->GetTestName(), test_case);

    // Test 6 ------------------------------------------------------------
    // Test that will generate a single area at the difference threshold for a positive case
    test_case = 6;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i <  HorizontalMoxels * 2; i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold;
    }
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_TRUE(trc, isMotion, "Error: %s case %d - Motion expected!!", trc->GetTestName(), test_case);

    // Test 7 ------------------------------------------------------------
    // Test that a single row at the difference threshold will result in motion if max pooling is used
    test_case = 7;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i < HorizontalMoxels; i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold;
    }
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_TRUE(trc, isMotion, "Error: %s case %d - Motion expected!!", trc->GetTestName(), test_case);

    // Test 8 ------------------------------------------------------------
    // Test that a single row at 2 *difference threshold will result in motion.
    test_case = 8;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i <  HorizontalMoxels; i++) {
        s_baseMotionOutput->pData[i] = 2 * scalar * s_differenceThreshold;
    }
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_TRUE(trc, isMotion, "Error: %s case %d - Motion expected!!", trc->GetTestName(), test_case);

    // Test 9 ------------------------------------------------------------
    // Test that will generate a second line of motion at the difference threshold for a positive case
    test_case = 9;
    for (u32 i = 0; i < HorizontalMoxels * 2; i++) {
        s_baseMotionOutput->pData[i + HorizontalMoxels * 2] = scalar * s_differenceThreshold;
    }
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_TRUE(trc, isMotion, "Error: %s case %d - Motion expected!!", trc->GetTestName(), test_case);

    // Test 10 ------------------------------------------------------------
    // Test that will generate a single area equal to 3 rows at the difference threshold for a positive case
    test_case = 10;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);  // Resetting pData to 0
    for (u32 i = 1; i < (HorizontalMoxels * 3); i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold;
    }
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_TRUE(trc, isMotion, "Error: %s case %d - Motion expected!!", trc->GetTestName(), test_case);

    // Test 11 ------------------------------------------------------------
    // Test that will generate 2 areas of diiferent size at the difference threshold for a positive case
    test_case = 11;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i < HorizontalMoxels; i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold;
    }
    for (u32 i = 0; i < (HorizontalMoxels * 3); i++) {
        s_baseMotionOutput->pData[i + HorizontalMoxels * 2] = scalar * s_differenceThreshold;
    }
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_TRUE(trc, isMotion, "Error: %s case %d - Motion expected!!", trc->GetTestName(), test_case);

    // Test 12 ------------------------------------------------------------
    // Test that will generate 2 areas of diiferent size at one of the regions will be below the difference threshold for a positive case
    test_case = 12;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 1; i < HorizontalMoxels * 3; i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold / 2;
    }
    for (u32 i = 0; i < HorizontalMoxels * 2; i++) {
        s_baseMotionOutput->pData[i + HorizontalMoxels * 6] = scalar * s_differenceThreshold;
    }
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_TRUE(trc, isMotion, "Error: %s case %d - Motion expected!!", trc->GetTestName(), test_case);

    // Test 13 ------------------------------------------------------------
    // Test that will generate 2 areas of diiferent size; one of the regions will be below the difference threshold for a positive case
    // Almost identical to Test case 10, but a different row-offset.
    test_case = 13;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 1; i < HorizontalMoxels * 3; i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold / 2;
    }
    for (u32 i = 0; i < HorizontalMoxels * 2; i++) {
        s_baseMotionOutput->pData[i + HorizontalMoxels * 5] = scalar * s_differenceThreshold;
    }
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_TRUE(trc, isMotion, "Error: %s case %d - Motion expected!!", trc->GetTestName(), test_case);

    // Test 14 ------------------------------------------------------------
    // Test that will generate 2 areas of diiferent size; one of the regions will be below the difference threshold for a positive case
    test_case = 14;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i < HorizontalMoxels * 2; i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold;
    }
    for (u32 i = 0; i < (HorizontalMoxels * 3); i++) {
        s_baseMotionOutput->pData[i + HorizontalMoxels * 4] = scalar * s_differenceThreshold / 2;
    }
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_TRUE(trc, isMotion, "Error: %s case %d - Motion expected!!", trc->GetTestName(), test_case);

    s_motion->StopDetectionEngine();
    TILT_MESSAGE(trc, "%s - Finish. =====================", trc->GetTestName());
}

static void TILT_MESSAGE_ROI(const TiltImplReportingCallbacks *trc, LTMediaMotionDetection_Region ROI, int test_case, char tag[]) {
    LT_UNUSED(trc); // unused unless debug logging is enabled, so pacify the compiler
    LT_UNUSED(ROI);
    LT_UNUSED(test_case);
    LT_UNUSED(tag);
    TILT_MSG_DEBUG(
        trc,
        "Case %d. %s ROI x1: %lu, y1: %lu, x2: %lu, y2: %lu\r\n",
        test_case,
        tag, // tag in {"Actual", "Expected}
        LT_Pu32(ROI.x1),
        LT_Pu32(ROI.y1),
        LT_Pu32(ROI.x2),
        LT_Pu32(ROI.y2)
    );
}

static bool ROIComparison(LTMediaMotionDetection_Region expectedROI, LTMediaMotionDetection_Region actualROI) {
    return (expectedROI.x1 == actualROI.x1) &&
           (expectedROI.y1 == actualROI.y1) &&
           (expectedROI.x2 == actualROI.x2) &&
           (expectedROI.y2 == actualROI.y2);
}

static char * motionDetection_timeWithAbbreviatedUnits(char *buf, int bufsize, LTTime const * t, bool bAbbreviatedUnits) {
    u32 nSeconds      = (u32)LTTime_GetSeconds(*t);
    u32 nMicroseconds = (u32)LTTime_GetMicroseconds(LTTime_FractionalSeconds(*t));
    u32 nNanoseconds  = (u32)LTTime_GetNanoseconds(LTTime_FractionalMicroseconds(*t));
    const char * pSeconds      = bAbbreviatedUnits ? "s"  : ((1 == nSeconds)      ? "second"      : "seconds");
    const char * pMicroseconds = bAbbreviatedUnits ? "us" : ((1 == nMicroseconds) ? "microsecond" : "microseconds");
    const char * pNanoseconds  = bAbbreviatedUnits ? "ns" : ((1 == nNanoseconds)  ? "nanosecond"  : "nanoseconds");
    if (nSeconds && nNanoseconds) lt_snprintf(buf, bufsize, "%ld %s, %06ld %s, %03ld %s", nSeconds, pSeconds, nMicroseconds, pMicroseconds, nNanoseconds, pNanoseconds);
    else if (nSeconds)            lt_snprintf(buf, bufsize, "%ld %s, %06ld %s", nSeconds, pSeconds, nMicroseconds, pMicroseconds);
    else                          lt_snprintf(buf, bufsize, "%ld %s, %03ld %s", nMicroseconds, pMicroseconds, nNanoseconds, pNanoseconds);
    return buf;     /* return a pointer to the string */
}

/* Copied from lt/lt/source/unittest/lt/core/UnitTestLTCoreHelpers.h
*/
static char * motionDetection_timeWithUnits(char *buf, int bufsize, LTTime const * t) {
    return motionDetection_timeWithAbbreviatedUnits(buf, bufsize, t, false);
}

static void ROITest(const TiltImplReportingCallbacks *trc, bool isMultiRegion, u32 region_idx, u32 x1, u32 y1, u32 x2, u32 y2, int test_case) {
    LT_UNUSED(isMultiRegion);

    enum { strbufSize = 80 };
    char strbuf[strbufSize];

    LTTime before = LT_GetCore()->GetKernelTime();
    bool isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_TRUE(trc, isMotion, "Error: %s case %d - Motion expected!", trc->GetTestName(), test_case);
    LTTime after = LT_GetCore()->GetKernelTime();
    LTTime elapsed = LTTime_Subtract(after, before);
    char * processTimeStr = motionDetection_timeWithUnits(strbuf, strbufSize, &elapsed);
    TILT_MSG_DEBUG(trc, "Test case: %d. Multiple-motionROIs process time: %s", test_case, processTimeStr);
    (void) processTimeStr;

    LTMediaMotionDetection_Regions *motionROIs = s_algorithm->API->GetMotionROIs(s_algorithm);
    LTMediaMotionDetection_Region actualROI = motionROIs->region[region_idx];
    lt_free(motionROIs);
    LTMediaMotionDetection_Region expectedROI = {x1, y1, x2, y2};

    bool ROIs_agreed = ROIComparison(expectedROI, actualROI);

    if (!ROIs_agreed) {
        TILT_MESSAGE(trc, "Actual and expected ROI disagree for case %d", test_case);
        TILT_MESSAGE_ROI(trc, actualROI, test_case, "Actual");
        TILT_MESSAGE_ROI(trc, expectedROI, test_case, "Expected");
    }
    TILT_ASSERT_TRUE(trc, ROIs_agreed, "Error: Case %d - motionROIs incorrect!!", test_case);
}

/* =======================================================================================================
    Test that Single-Motion ROI can be correctly calculated for a range of inputs and regions.
========================================================================================================*/
static void CheckFrameForMotionROITest(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "%s - Start. =====================", trc->GetTestName());

    int test_case;
    u32 x1, y1, x2, y2;
    bool isMultiRegion = false;
    u32 region_idx = 0;

    /* Set motion algorithm to use single ROI detection for this test. */
    s_algorithm->API->SetROICalculationStrategy(s_algorithm, kLTMediaMotionROICalculationStrategy_SingleROI);

    TILT_ASSERT_TRUE(trc, WaitForStartDetectionEngine(trc, true), "Error: Motion Detection Engine is not running!");

    // Test 1 ------------------------------------------------------------
    test_case = 1;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    s_baseMotionOutput->pData[0] = s_differenceThreshold;
    x1 = 0;
    y1 = 0;
    x2 = 0;
    y2 = 0;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 2 ------------------------------------------------------------
    test_case = 2;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    s_baseMotionOutput->pData[0] = s_differenceThreshold;
    s_baseMotionOutput->pData[s_baseMotionOutput->nDataLen - 1] = s_differenceThreshold;
    x1 = 0;
    y1 = 0;
    x2 = HorizontalMoxels - 1;
    y2 = VerticalMoxels - 1;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 3 ------------------------------------------------------------
    test_case = 3;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 1; i < HorizontalMoxels; i++) {
        s_baseMotionOutput->pData[i] = s_differenceThreshold;
    }
    x1 = 1;
    y1 = 0;
    x2 = HorizontalMoxels - 1;
    y2 = 0;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 4 ------------------------------------------------------------
    test_case = 4;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 1; i < s_baseMotionOutput->nDataLen; i += HorizontalMoxels) {
        s_baseMotionOutput->pData[i] = s_differenceThreshold;
    }
    x1 = 1;
    y1 = 0;
    x2 = 1;
    y2 = VerticalMoxels - 1;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 5 ------------------------------------------------------------
    test_case = 5;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 1; i < s_baseMotionOutput->nDataLen; i += HorizontalMoxels + 1) {
        s_baseMotionOutput->pData[i] = s_differenceThreshold;
    }
    x1 = 1;
    y1 = 0;
    x2 = VerticalMoxels;
    y2 = VerticalMoxels - 1;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 6 ------------------------------------------------------------
    test_case = 6;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 1; i < 5; i++) {                                      // col index
        for (u32 j = 1; j < 4; j++) {                                  // row index
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = s_differenceThreshold;
        }
    }
    x1 = 1;
    y1 = 1;
    x2 = 4;
    y2 = 3;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 7 ------------------------------------------------------------
    test_case = 7;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = VerticalMoxels - 4; i < VerticalMoxels - 1; i++) {
        for (u32 j = 1; j < 5; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = s_differenceThreshold;
        }
    }
    x1 = VerticalMoxels - 4;
    y1 = 1;
    x2 = VerticalMoxels - 2;
    y2 = 4;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 8 ------------------------------------------------------------
    test_case = 8;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 1; i < 5; i++) {
        for (u32 j = 1; j < 4; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = s_differenceThreshold;
        }
    }
    for (u32 i = VerticalMoxels - 4; i < VerticalMoxels - 1; i++) {
        for (u32 j = 2; j < 5; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = s_differenceThreshold;
        }
    }
    x1 = 1;
    y1 = 1;
    x2 = VerticalMoxels - 2;
    y2 = 4;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 9 ------------------------------------------------------------
    test_case = 9;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 1; i < 5; i++) {
        for (u32 j = 1; j < 4; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = s_differenceThreshold;
        }
    }
    for (u32 i = 2; i < 6; i++) {
        for (u32 j = 2; j < 6; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = s_differenceThreshold;
        }
    }
    x1 = 1;
    y1 = 1;
    x2 = 5;
    y2 = 5;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 10 ------------------------------------------------------------
    test_case = 10;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    u32 min_x = HorizontalMoxels;
    u32 min_y = VerticalMoxels;
    u32 max_x = 0;
    u32 max_y = 0;
    bool isMotion = false;
    bool trigger_motion = false;

    for (u32 i = 0; i < HorizontalMoxels; i++) {        // col index, x
        for (u32 j = 0; j < VerticalMoxels; j++) {      // row index, y
            trigger_motion = ((i == 10) && (j == 10))   // 1st motion
                          || ((i == 20) && (j == 20))   // 2nd motion
                          || ((i == 30) && (j == 15))   // 3rd motion
                          || ((i == 40) && (j == 5))    // 4th motion
                          || ((i == 50) && (j == 25));  // 5th motion
            if (trigger_motion) {
                isMotion = true;
                s_baseMotionOutput->pData[i + j * HorizontalMoxels] = s_differenceThreshold;
                if (i < min_x) min_x = i;
                if (i > max_x) max_x = i;
                if (j < min_y) min_y = j;
                if (j > max_y) max_y = j;
            }
        }
    }

    if (isMotion) {
        x1 = min_x;
        y1 = min_y;
        x2 = max_x;
        y2 = max_y;
        ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);
    } else {
        TILT_MSG_DEBUG(trc, "Case %d. No motion.", test_case);
    }

    s_motion->StopDetectionEngine();
    TILT_MESSAGE(trc, "%s - Finish. =====================", trc->GetTestName());
}

/* Reference code from UnitTestLTCoreKernelTimeTests.c */
static void MultipleMotionProcessTimeTest(const TiltImplReportingCallbacks *trc, u32 N, int test_case) {
    /* Get the average multiple motion processing time over N iterations and test if it is within the specified time limit. */
    LTTime TimeLimit = LTTime_Milliseconds(5);

    enum { strbufSize = 80 };
    char strbuf[strbufSize];

    LTTime before, after, elapsed, totalElapsed, minElapsed;

    /* Set motion algorithm to use multi ROI detection for this test. */
    s_algorithm->API->SetROICalculationStrategy(s_algorithm, kLTMediaMotionROICalculationStrategy_MultipleROIs);

    for (u32 i = 0; i < N; i++) {
        before = LT_GetCore()->GetKernelTime();
        s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
        after = LT_GetCore()->GetKernelTime();
        elapsed = LTTime_Subtract(after, before);
        totalElapsed = (i > 0) ? LTTime_Add(totalElapsed, elapsed) : elapsed;
        if (i == 0 || LTTime_IsLessThan(elapsed, minElapsed)) {
            minElapsed = elapsed;
        }
        char * processTimeStr = motionDetection_timeWithUnits(strbuf, strbufSize, &elapsed);
        TILT_MSG_DEBUG(trc, "Test case: %d iter %d. Multi-motion ROI processing process time: %s", test_case, LT_Pu32(i), processTimeStr);
        (void) processTimeStr;
    }

    bool test = LTTime_IsLessThan(minElapsed, TimeLimit);
    char * minProcessTimeStr = motionDetection_timeWithUnits(strbuf, strbufSize, &minElapsed);
    char * timeLimitStr = motionDetection_timeWithUnits(strbuf, strbufSize, &TimeLimit);
    TILT_ASSERT_TRUE(trc, test, "Test case: %d. Error: Min Multi-motion ROI processing time %s >= %s!", test_case, minProcessTimeStr, timeLimitStr);
}

static LTMediaMotionDetection_MotionZoneBitGrid* CreateBitGridFromRectangle(u32 x0, u32 y0, u32 x1, u32 y1) {
    /*  Arguments:
        (x0, y0) is the top left hand corner of rectangle,
        (x1, y1) is the bottom right hand corner of rectangle.
    */
    LTMediaMotionDetection_MotionZoneBitGrid* pBitGrid = lt_malloc(bitGridSize);
    if (!pBitGrid) {
        LTLOG_REDALERT("rect.bitgrid.oom", "Could not allocate memory for motion zone bitGrid!");
        return NULL;
    }
    pBitGrid->width = BitGridWidth;
    pBitGrid->height = BitGridHeight;
    pBitGrid->bitmapSize = nBitmapBytes;
    lt_memset(pBitGrid->bitmap, 0, pBitGrid->bitmapSize);

    // set BitGrid from supplied rectangle coordinates
    for (u32 y = y0; y < y1; y++) {
        for (u32 x = x0; x < x1; x++) {
            u32 i = y * BitGridWidth + x;
            u32 wordIndex = i / 8;
            u32 bitIndex = i % 8;
            // The BitGrid has its most significant bit in each byte correspond
            // to the leftmost grid cell in the corresponding block of 8 cells
            pBitGrid->bitmap[wordIndex] |= (1 << (7 - bitIndex));
        }
    }
    return pBitGrid;
}

static void ResetMotionZonesToOnes(void) {
    LTMediaMotionDetection_MotionZoneBitGrid *pBitGrid = lt_malloc(bitGridSize);
    if (!pBitGrid) {
        LTLOG_REDALERT("ones.bitgrid.oom", "Could not allocate memory for motion zone bitGrid!");
        return;
    }
    pBitGrid->width = BitGridWidth;
    pBitGrid->height = BitGridHeight;
    pBitGrid->bitmapSize = nBitmapBytes;

    /* Setting to -1 should set all bits to 1 in each word, after a conversion from signed to unsigned. */
    lt_memset(pBitGrid->bitmap, -1, pBitGrid->bitmapSize);
    s_motion->SetMotionZones(pBitGrid);
    lt_free(pBitGrid);
}

/* =================================================================================================
    Test that Multi-Motion ROIs can be correctly calculated for a range of inputs and regions.
================================================================================================= */
static void MultipleMotionROITest(const TiltImplReportingCallbacks *trc, bool bUseMaxPooling, u32 scalar, bool bUseBFS);

static void CheckFrameForMultipleMotionROITest(const TiltImplReportingCallbacks *trc) {
    /* Test average pooling downsampling without BFS. */
    u32 scalar = DownsamplingScaleFactor * DownsamplingScaleFactor;
    MultipleMotionROITest(trc, false, scalar, false);

    /* TODO: Average pooling and BFS together are not supported in MultipleMotionROITest, so we skip that here. */

    /* Test max pooling downsampling without BFS. */
    scalar = 1;
    MultipleMotionROITest(trc, true, scalar, false);

    /* Test max pooling downsampling with BFS. */
    MultipleMotionROITest(trc, true, scalar, true);

    /* Reset to max pooling for other tests. */
    s_algorithm->API->SetDownsamplingStrategy(s_algorithm, kLTMediaMotionDownsamplingStrategy_MaxPooling);
    /* Reset to not using BFS for other tests. */
    s_algorithm->API->EnableBFS(s_algorithm, false);
}

static void MultipleMotionROITest(const TiltImplReportingCallbacks *trc, bool bUseMaxPooling, u32 scalar, bool bUseBFS) {
    TILT_MESSAGE(trc, "%s (%s pooling) - Start. =====================", trc->GetTestName(), bUseMaxPooling ? "Max" : "Avg");

    int test_case, sub_test_case;
    u32 x1, y1, x2, y2, region_idx;
    u32 N = 20; // Used in average processing time test - the average processing time over N iterations.
    bool isMultiRegion = true;

    /* Set motion algorithm downsampling strategy - only max pooling and average pooling are supported. */
    LTMediaMotionDownsamplingStrategy downsamplingStrategy = bUseMaxPooling ?
                                                             kLTMediaMotionDownsamplingStrategy_MaxPooling :
                                                             kLTMediaMotionDownsamplingStrategy_AveragePooling;
    s_algorithm->API->SetDownsamplingStrategy(s_algorithm, downsamplingStrategy);

    /* Set motion algorithm to use multi ROI detection for this test. */
    s_algorithm->API->SetROICalculationStrategy(s_algorithm, kLTMediaMotionROICalculationStrategy_MultipleROIs);

    /* Set whether we use BFS, disabling temporal smoothing for this test. */
    s_algorithm->API->SetTemporalSmoothingFrames(s_algorithm, 1);
    s_algorithm->API->EnableBFS(s_algorithm, bUseBFS);
    if (bUseBFS) TILT_MESSAGE(trc, "Using BFS for motion zone propagation with no temporal smoothing.");

    TILT_ASSERT_TRUE(trc, WaitForStartDetectionEngine(trc, true), "Error: Motion Detection Engine is not running!");

    // Tests that measure largest zone of motion ===================================
    region_idx = 0;

    // Test 1 ------------------------------------------------------------
    // Set first 2 rows to s_differenceThreshold
    test_case = 1;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i < HorizontalMoxels * 2; i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold;
    }
    x1 = 0;
    y1 = 0;
    x2 = HorizontalMoxels - 1;
    y2 = 1;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 2 ------------------------------------------------------------
    // Set first 4 rows to s_differenceThreshold
    test_case = 2;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i < (HorizontalMoxels * 4); i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold;
    }
    x1 = 0;
    y1 = 0;
    x2 = HorizontalMoxels - 1;
    y2 = 3;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 3 ------------------------------------------------------------
    // Set first 2 rows (from col 1) to s_differenceThreshold
    test_case = 3;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 1; i < HorizontalMoxels * 2; i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold;
    }
    x1 = 0;
    y1 = 0;
    x2 = HorizontalMoxels - 1;
    y2 = 1;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 4 ------------------------------------------------------------
    // Set columns 3 to 6 to s_differenceThreshold
    test_case = 4;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 2; i < 6; i++) {
        for (u32 j = 0; j < VerticalMoxels; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = scalar * s_differenceThreshold;
        }
    }
    x1 = 2;
    y1 = 0;
    x2 = 5;
    y2 = VerticalMoxels - 1;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Tests that measures zone propagation for the largest zone ===================================

    // Test 5 ------------------------------------------------------------
    // Set first row (idx 0), and then rows 4 to 7 (idx 3 to 6), to s_differenceThreshold * 2
    test_case = 5;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i < HorizontalMoxels; i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold * 2;
    }
    for (u32 i = 0; i < (HorizontalMoxels * 4); i++) {
        s_baseMotionOutput->pData[i + 3 * HorizontalMoxels] = scalar * s_differenceThreshold * 2;
    }
    if (!bUseBFS) {
        if (DownsamplingScaleFactor > 1) {
            x1 = 0;
            y1 = 0;
            x2 = HorizontalMoxels - 1;
            y2 = 7;
        } else {
            x1 = 0;
            y1 = 3;
            x2 = HorizontalMoxels - 1;
            y2 = 6;
        }
    } else {
        x1 = 0;
        y1 = 3;
        y1 = 0;
        x2 = HorizontalMoxels - 1;
        y2 = 6;
        y2 = (DownsamplingScaleFactor == 1) ? 6 : 7;
    }
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 6 ------------------------------------------------------------
    // Set first row (idx 0), and then rows 16 to 18 (idx 15 to 17), to DifferenceThreshold * 2
    test_case = 6;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i < HorizontalMoxels; i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold * 2;
    }
    for (u32 i = 0; i < (HorizontalMoxels * 3); i++) {
        s_baseMotionOutput->pData[i + HorizontalMoxels * 15] = scalar * s_differenceThreshold * 2;
    }
    x1 = 0;
    y1 = (DownsamplingScaleFactor == 1) ? 15 : 14;
    x2 = HorizontalMoxels - 1;
    y2 = 17;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // L-shape tests ===============================================================

    // Test 7 ------------------------------------------------------------
    // Edge Case testing with L-shape: across and then down
    test_case = 7;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i < 10; i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold;
        s_baseMotionOutput->pData[i + HorizontalMoxels] = scalar * s_differenceThreshold;
    }
    for (u32 i = 8; i < 10; i++) {
        for (u32 j = 0; j < 20; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = scalar * s_differenceThreshold;
        }
    }
    x1 = 0;
    y1 = 0;
    x2 = 9;
    y2 = 19;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 8 ------------------------------------------------------------
    // Edge Case testing with L-shape: down and then across
    test_case = 8;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i < 2; i++) {
        for (u32 j = 0; j < 22; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = scalar * s_differenceThreshold;
        }
    }
    for (u32 i = 0; i < 30; i++) {
        for (u32 j = 20; j < 22; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = scalar * s_differenceThreshold;
        }
    }
    x1 = 0;
    y1 = 0;
    x2 = 29; // col
    y2 = 21; // row
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 9 ------------------------------------------------------------
    // Edge Case testing with L-shape: across (row 9 & 10) then up
    test_case = 9;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i < 10; i++) {
        s_baseMotionOutput->pData[i + 8 * HorizontalMoxels] = scalar * s_differenceThreshold;
        s_baseMotionOutput->pData[i + 9 * HorizontalMoxels] = scalar * s_differenceThreshold;
    }
    for (u32 j = 0; j < 10; j++) {
        s_baseMotionOutput->pData[8 + j * HorizontalMoxels] = scalar * s_differenceThreshold;
        s_baseMotionOutput->pData[9 + j * HorizontalMoxels] = scalar * s_differenceThreshold;
    }
    x1 = 0;
    y1 = 0;
    x2 = 9;
    y2 = 9;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 10 ------------------------------------------------------------
    // Edge Case testing with L-shape: up (at col 7 & 8), then across
    test_case = 10;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 j = 0; j < 8; j++) {
        s_baseMotionOutput->pData[6 + j * HorizontalMoxels] = scalar * s_differenceThreshold;
        s_baseMotionOutput->pData[7 + j * HorizontalMoxels] = scalar * s_differenceThreshold;
    }
    for (u32 i = 0; i < 8; i++) {
        s_baseMotionOutput->pData[i + 8] = scalar * s_differenceThreshold;
        s_baseMotionOutput->pData[i + 8 + HorizontalMoxels] = scalar * s_differenceThreshold;
    }
    x1 = 6;
    y1 = 0;
    x2 = 15;
    y2 = 7;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 11 ------------------------------------------------------------
    // Edge Case testing with symmetrical U-shape: down then across then up
    test_case = 11;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    static const u8 testMotionOutput_11[6][8] = {
        {1, 1, 0, 0, 0, 0, 1, 1},
        {1, 1, 0, 0, 0, 0, 1, 1},
        {1, 1, 0, 0, 0, 0, 1, 1},
        {1, 1, 0, 0, 0, 0, 1, 1},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 1, 1},
    };
    for (u32 y = 0; y < 6; y++) {
        for (u32 x = 0; x < 8; x++) {
            u32 current_index = y * HorizontalMoxels + x;
            s_baseMotionOutput->pData[current_index] = testMotionOutput_11[y][x] * scalar *s_differenceThreshold;
        }
    }
    x1 = 0;
    y1 = 0;
    x2 = 7;
    y2 = 5;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 12 ------------------------------------------------------------
    // Edge Case testing with compound U-shape: down then across then up across and back down
    test_case = 12;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    static const u8 testMotionOutput_12[6][10] = {
        {1, 1, 0, 0, 1, 1, 1, 1, 1, 1},
        {1, 1, 0, 0, 1, 1, 1, 1, 1, 1},
        {1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
        {1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
        {1, 1, 1, 1, 1, 1, 0, 0, 1, 1},
        {1, 1, 1, 1, 1, 1, 0, 0, 1, 1},
    };
    for (u32 y = 0; y < 6; y++) {
        for (u32 x = 0; x < 10; x++) {
            u32 current_index = y * HorizontalMoxels + x;
            s_baseMotionOutput->pData[current_index] = testMotionOutput_12[y][x] * scalar * s_differenceThreshold;
        }
    }
    x1 = 0;
    y1 = 0;
    x2 = 9; // ncol - 1
    y2 = 5; // nrow - 1
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 13 ------------------------------------------------------------
    // Edge Case testing with compound U-shape: up then across then down across and back up
    test_case = 13;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    static const u8 testMotionOutput_13[6][10] = {
        {1, 1, 1, 1, 1, 1, 0, 0, 1, 1},
        {1, 1, 1, 1, 1, 1, 0, 0, 1, 1},
        {1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
        {1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
        {1, 1, 0, 0, 1, 1, 1, 1, 1, 1},
        {1, 1, 0, 0, 1, 1, 1, 1, 1, 1},
    };
    u32 x_offset_13 = 2;
    u32 y_offset_13 = 2;
    for (u32 y = 0; y < 6; y++) {
        for (u32 x = 0; x < 10; x++) {
            u32 current_index = (y + y_offset_13) * HorizontalMoxels + (x + x_offset_13);
            s_baseMotionOutput->pData[current_index] = testMotionOutput_13[y][x] * scalar * s_differenceThreshold;
        }
    }
    x1 = x_offset_13;
    y1 = y_offset_13;
    x2 = x_offset_13 + 9; // ncol - 1
    y2 = y_offset_13 + 5; // nrow - 1
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 14 ------------------------------------------------------------
    // Edge Case testing with S-shape: left then down then right then down then left
    test_case = 14;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    static const u8 testMotionOutput_14[10][6] = {
        {1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1},
        {1, 1, 0, 0, 0, 0},
        {1, 1, 0, 0, 0, 0},
        {1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1},
        {0, 0, 0, 0, 1, 1},
        {0, 0, 0, 0, 1, 1},
        {1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1},
    };
    u32 x_offset_14 = 4;
    u32 y_offset_14 = 4;
    for (u32 y = 0; y < 10; y++) {
        for (u32 x = 0; x < 6; x++) {
            u32 current_index = (y + y_offset_14) * HorizontalMoxels + (x + x_offset_14);
            s_baseMotionOutput->pData[current_index] = testMotionOutput_14[y][x] * scalar * s_differenceThreshold;
        }
    }
    x1 = x_offset_14;
    y1 = y_offset_14;
    x2 = x_offset_14 + 5; // ncol - 1
    y2 = y_offset_14 + 9; // nrow - 1
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 15 ------------------------------------------------------------
    // Edge Case testing with backwards S-shape: right then down then left then down then right
    test_case = 15;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    static const u8 testMotionOutput_15[10][6] = {
        {1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1},
        {0, 0, 0, 0, 1, 1},
        {0, 0, 0, 0, 1, 1},
        {1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1},
        {1, 1, 0, 0, 0, 0},
        {1, 1, 0, 0, 0, 0},
        {1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1},
    };
    u32 x_offset_15 = 2;
    u32 y_offset_15 = 2;
    for (u32 y = 0; y < 10; y++) {
        for (u32 x = 0; x < 6; x++) {
            u32 current_index = (y + y_offset_15) * HorizontalMoxels + (x + x_offset_15);
            s_baseMotionOutput->pData[current_index] = testMotionOutput_15[y][x] * scalar * s_differenceThreshold;
        }
    }
    x1 = x_offset_15;
    y1 = y_offset_15;
    x2 = x_offset_15 + 5; // ncol - 1
    y2 = y_offset_15 + 9; // nrow - 1
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 16 ------------------------------------------------------------
    // Edge Case testing - asymmetric version of test 13
    test_case = 16;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    static const u8 testMotionOutput_16[7][10] = {
        {1, 1, 1, 1, 1, 1, 0, 0, 1, 1},
        {1, 1, 1, 1, 1, 1, 0, 0, 1, 1},
        {1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
        {1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
        {1, 1, 0, 0, 1, 1, 1, 1, 1, 1},
        {1, 1, 0, 0, 1, 1, 1, 1, 1, 1},
        {1, 1, 0, 0, 0, 0, 0, 0, 1, 1},
    };
    u32 x_offset_16 = HorizontalMoxels - 10;
    u32 y_offset_16 = VerticalMoxels - 7;
    for (u32 y = 0; y < 7; y++) {
        for (u32 x = 0; x < 10; x++) {
            u32 current_index = (y + y_offset_16) * HorizontalMoxels + (x + x_offset_16);
            s_baseMotionOutput->pData[current_index] = testMotionOutput_16[y][x] * scalar * s_differenceThreshold;
        }
    }
    x1 = x_offset_16;
    y1 = y_offset_16;
    x2 = x_offset_16 + 9; // ncol - 1
    y2 = y_offset_16 + 6; // nrow - 1
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 17 ------------------------------------------------------------
    // When there are 2 islands, test that the bigger one is selected at region_idx=0
    test_case = 17;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 1; i < 5; i++) {
        for (u32 j = 1; j < 10; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = scalar * s_differenceThreshold;
        }
    }
    for (u32 i = HorizontalMoxels - 4; i < HorizontalMoxels - 1; i++) {
        for (u32 j = 2; j < 5; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = scalar * s_differenceThreshold;
        }
    }
    x1 = (DownsamplingScaleFactor == 1) ? 1: 0;
    y1 = (DownsamplingScaleFactor == 1) ? 1: 0;
    x2 = (DownsamplingScaleFactor == 1) ? 4: 5;
    y2 = 9;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 18 ------------------------------------------------------------
    // When there are 2 islands, test that the bigger one is selected at region_idx=0
    test_case = 18;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 1; i < 5; i++) {
        for (u32 j = 1; j < 4; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = scalar * s_differenceThreshold;
        }
    }
    for (u32 i = 10; i < 20; i++) {
        for (u32 j = 10; j < 20; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = scalar * s_differenceThreshold;
        }
    }
    x1 = 10;
    y1 = 10;
    x2 = 19;
    y2 = 19;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Tests that measure second ROI for multiple motion ===================================
    region_idx = 1;

    // Test 19 ------------------------------------------------------------
    // 2 regions: bigger region row 16 to 18 (idx 15 to 17), smaller region row 1 only
    test_case = 19;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i < HorizontalMoxels; i++) {
        s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold;
    }
    for (u32 i = 0; i < (HorizontalMoxels * 3); i++) {
        s_baseMotionOutput->pData[i + HorizontalMoxels * 15] = scalar * s_differenceThreshold;
    }
    x1 = 0;
    y1 = 0;
    x2 = HorizontalMoxels - 1;
    y2 = (DownsamplingScaleFactor == 1) ? 0 : 1;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 20 ------------------------------------------------------------
    // 2 regions: bigger region (x1, y1, x2, y2) = (1, 1, 5, 10), smaller region (x1, y1, x2, y2) = (20, 15, 25, 19)
    test_case = 20;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 1; i < 6; i++) {
        for (u32 j = 1; j < 10; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = scalar * s_differenceThreshold;
        }
    }
    for (u32 i = 20; i < 26; i++) {
        for (u32 j = 15; j < 20; j++) {
            s_baseMotionOutput->pData[i + j * HorizontalMoxels] = scalar * s_differenceThreshold;
        }
    }
    x1 = 20;
    y1 = (DownsamplingScaleFactor == 1) ? 15 : 14;
    x2 = 25;
    y2 = 19;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    //----------------------------------------------------------------------
    static const u8 testMotionOutput_A[5][6] = {
        {1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 0},
        {1, 1, 1, 1, 0, 1},
    };

    static const u8 testMotionOutput_B1[6][6] = {
        {1, 0, 1, 0, 1, 0},
        {0, 1, 0, 1, 0, 1},
        {1, 0, 1, 0, 1, 0},
        {0, 1, 0, 1, 0, 1},
        {1, 0, 1, 0, 1, 0},
        {0, 1, 0, 1, 0, 1},
    };

    static const u8 testMotionOutput_B2[6][6] = {
        {0, 1, 0, 1, 0, 1},
        {1, 0, 1, 0, 1, 0},
        {0, 1, 0, 1, 0, 1},
        {1, 0, 1, 0, 1, 0},
        {0, 1, 0, 1, 0, 1},
        {1, 0, 1, 0, 1, 0},
    };

    static const u8 testMotionOutput_C[4][6] = {
        {1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1},
        {0, 0, 1, 1, 1, 1},
        {1, 0, 1, 1, 1, 1},
    };

    static const u8 testMotionOutput_D[8][8] = {
        {1, 1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 1, 1},
    };

    // Test 21 ------------------------------------------------------------
    test_case = 21;
    sub_test_case = test_case * 100 + 1;

    for (u32 x_offset = 0; x_offset < 3; x_offset += 2) {
        for (u32 y_offset = 0; y_offset < 3; y_offset += 2) {
            lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
            for (u32 y = 0; y < 4; y++) {
                for (u32 x = 0; x < 6; x++) {
                    u32 current_index = (y + y_offset) * HorizontalMoxels + (x + x_offset);
                    s_baseMotionOutput->pData[current_index] = testMotionOutput_A[y][x] * scalar * s_differenceThreshold;
                }
            }
            region_idx = 0;
            x1 = x_offset;
            y1 = y_offset;
            x2 = x_offset + 5;  //ncol - 1
            y2 = y_offset + 3;  //nrow - 1
            ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, sub_test_case++);
        }
    }

    // Test 22 ------------------------------------------------------------
    test_case = 22;
    sub_test_case = test_case * 100 + 1;

    for (u32 x_offset = 0; x_offset < 3; x_offset += 2) {
        for (u32 y_offset = 2; y_offset < 6; y_offset += 2) {
            lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
            for (u32 y = 0; y < 6; y++) {
                for (u32 x = 0; x < 6; x++) {
                    u32 current_index = (y + y_offset) * HorizontalMoxels + (x + x_offset);
                    s_baseMotionOutput->pData[current_index] = testMotionOutput_B1[y][x] * scalar * s_differenceThreshold * 2;
                }
            }
            region_idx = 0;
            x1 = x_offset;
            y1 = y_offset;
            x2 = x_offset + 5;  //ncol - 1
            y2 = y_offset + 5;  //nrow - 1
            ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, sub_test_case++);
        }
    }

    // Test 23 ------------------------------------------------------------
    test_case = 23;
    sub_test_case = test_case * 100 + 1;

    for (u32 x_offset = 0; x_offset < 3; x_offset += 2) {
        for (u32 y_offset = 2; y_offset < 6; y_offset += 2) {
            lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
            for (u32 y = 0; y < 6; y++) {
                for (u32 x = 0; x < 6; x++) {
                    u32 current_index = (y + y_offset) * HorizontalMoxels + (x + x_offset);
                    s_baseMotionOutput->pData[current_index] = testMotionOutput_B2[y][x] * scalar * s_differenceThreshold * 2;
                }
            }
            region_idx = 0;
            x1 = x_offset;
            y1 = y_offset;
            x2 = x_offset + 5;  //ncol - 1
            y2 = y_offset + 5;  //nrow - 1
            ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, sub_test_case++);
        }
    }

    // Test 24 ------------------------------------------------------------
    test_case = 24;
    sub_test_case = test_case * 100 + 1;

    for (u32 x_offset = 0; x_offset < 3; x_offset += 2) {
        for (u32 y_offset = 2; y_offset < 6; y_offset += 2) {
            lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
            for (u32 y = 0; y < 4; y++) {
                for (u32 x = 0; x < 6; x++) {
                    u32 current_index = (y + y_offset) * HorizontalMoxels + (x + x_offset);
                    s_baseMotionOutput->pData[current_index] = testMotionOutput_C[y][x] * scalar * s_differenceThreshold;
                }
            }
            region_idx = 0;
            x1 = x_offset;
            y1 = y_offset;
            x2 = x_offset + 5;  //ncol - 1
            y2 = y_offset + 3;  //nrow - 1
            ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, sub_test_case++);
        }
    }

    // Test 25 ------------------------------------------------------------
    test_case = 25;
    sub_test_case = test_case * 100 + 1;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    u32 x_offset_A = 0;
    u32 y_offset_A = 0;
    for (u32 y = 0; y < 5; y++) {
        for (u32 x = 0; x < 6; x++) {
            u32 current_index = (y + y_offset_A) * HorizontalMoxels + (x + x_offset_A);
            s_baseMotionOutput->pData[current_index] = testMotionOutput_A[y][x] * scalar * s_differenceThreshold;
        }
    }
    u32 x_offset_D = 0;
    u32 y_offset_D = VerticalMoxels - 9;
    for (u32 y = 0; y < 8; y++) {
        for (u32 x = 0; x < 8; x++) {
            u32 current_index = (y + y_offset_D) * HorizontalMoxels + (x + x_offset_D);
            s_baseMotionOutput->pData[current_index] = testMotionOutput_D[y][x] * scalar * s_differenceThreshold;
        }
    }

    u32 x_offset_C = HorizontalMoxels - 7;
    u32 y_offset_C = VerticalMoxels - 5;
    for (u32 y = 0; y < 4; y++) {
        for (u32 x = 0; x < 6; x++) {
            u32 current_index = (y + y_offset_C) * HorizontalMoxels + (x + x_offset_C);
            s_baseMotionOutput->pData[current_index] = testMotionOutput_C[y][x] * scalar * s_differenceThreshold;
        }
    }
    region_idx = 0;
    x1 = x_offset_D;
    y1 = y_offset_D;
    x2 = x_offset_D + 7; // ncol - 1
    y2 = y_offset_D + 7; // nrow - 1
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, sub_test_case++);

    region_idx = 1;
    x1 = x_offset_A;
    y1 = y_offset_A;
    x2 = x_offset_A + 5;
    y2 = (DownsamplingScaleFactor == 1) ? y_offset_A + 4 : y_offset_A + 5;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, sub_test_case);

    // Test 26 ------------------------------------------------------------
    test_case = 26;

    static const u8 testMotionOutput_E[10][10] = {
        {1, 1, 0, 0, 0, 0, 0, 0, 1, 1},
        {1, 1, 0, 0, 0, 0, 0, 0, 1, 1},
        {0, 0, 1, 1, 0, 0, 1, 1, 0, 0},
        {0, 0, 1, 1, 0, 0, 1, 1, 0, 0},
        {0, 0, 0, 0, 1, 1, 0, 1, 0, 0},
        {0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
        {0, 0, 1, 1, 0, 0, 1, 1, 0, 0},
        {0, 0, 1, 1, 1, 0, 1, 1, 0, 0},
        {1, 1, 0, 0, 0, 0, 0, 0, 1, 1},
        {1, 1, 0, 0, 0, 0, 0, 0, 1, 1},
    };

    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    u32 x_offset_E = 0;
    u32 y_offset_E = 0;
    for (u32 y = 0; y < 10; y++) {
        for (u32 x = 0; x < 10; x++) {
            u32 current_index = (y + y_offset_E) * HorizontalMoxels + (x + x_offset_E);
            s_baseMotionOutput->pData[current_index] = testMotionOutput_E[y][x] * scalar * s_differenceThreshold;
        }
    }
    region_idx = 0;
    x1 = x_offset_E;
    y1 = y_offset_E;
    x2 = x_offset_E + 9; // ncol - 1
    y2 = y_offset_E + 9; // nrow - 1
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 27 ------------------------------------------------------------
    // Test the time it takes to process a frame when all moxels are above the threshold.
    test_case = 27;
    lt_memset(s_baseMotionOutput->pData, s_differenceThreshold, s_baseMotionOutput->nDataLen);
    region_idx = 0;
    x1 = 0;
    y1 = 0;
    x2 = HorizontalMoxels - 1;
    y2 = VerticalMoxels - 1;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    MultipleMotionProcessTimeTest(trc, N, test_case);

    // Test 28 ------------------------------------------------------------
    // "Salt and pepper" processing time test for whole frame.
    test_case = 28;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 y = 0; y < VerticalMoxels; y++) {
        for (u32 x = 0; x < HorizontalMoxels; x+=2) {
            u32 i = y * HorizontalMoxels + x;
            s_baseMotionOutput->pData[i + (y % 2)] = scalar * s_differenceThreshold * 2;
        }
    }
    region_idx = 0;
    x1 = 0;
    y1 = 0;
    x2 = HorizontalMoxels - 1;
    y2 = VerticalMoxels - 1;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    MultipleMotionProcessTimeTest(trc, N, test_case);

    // Test 29 ------------------------------------------------------------
    // "Salt and pepper" pattern again, but this time moxels are too far apart for connection i.e. no motion expected. Test processing time.
    test_case = 29;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);

    u32 offset = bUseBFS ? 12 : 4;
    for (u32 y = 0; y < VerticalMoxels; y+=offset) {
        for (u32 x = 0; x < HorizontalMoxels; x+=offset) {
            u32 i = y * HorizontalMoxels + x;
            s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold * 2;
        }
    }
    bool isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_FALSE(trc, isMotion, "Error: %s case %d - No motion expected!", trc->GetTestName(), test_case);
    MultipleMotionProcessTimeTest(trc, N, test_case);

    // Test 30 ------------------------------------------------------------
    // Test diagonal lines from bottom left to top right
    test_case = 30;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 y = 0; y < VerticalMoxels; y++) {
        for (u32 x = 0; x < HorizontalMoxels; x++) {
            if ((y + x == VerticalMoxels - 1) || (y + x == VerticalMoxels - 2) || (y + x == VerticalMoxels - 3) || (y + x == VerticalMoxels)) {
                u32 current_index = y * HorizontalMoxels + x;
                s_baseMotionOutput->pData[current_index] = scalar * s_differenceThreshold;
            }
        }
    }
    region_idx = 0;
    x1 = 0;
    y1 = 0;
    x2 = VerticalMoxels;
    y2 = VerticalMoxels - 1;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 31 ------------------------------------------------------------
    // Edge Case testing
    test_case = 31;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    static const u8 testMotionOutput_31[8][8] = {
        {0, 0, 1, 1, 0, 0, 0, 0},
        {0, 0, 1, 1, 0, 0, 0, 0},
        {0, 0, 1, 1, 1, 1, 1, 1},
        {0, 0, 1, 1, 1, 1, 1, 1},
        {0, 0, 1, 1, 0, 0, 1, 1},
        {0, 0, 1, 1, 0, 0, 1, 1},
        {1, 1, 1, 1, 0, 0, 1, 1},
        {1, 1, 1, 1, 0, 0, 1, 1},
    };
    u32 x_offset_31 = 2;
    u32 y_offset_31 = 2;
    for (u32 y = 0; y < 8; y++) {
        for (u32 x = 0; x < 8; x++) {
            u32 current_index = (y + y_offset_31) * HorizontalMoxels + (x + x_offset_31);
            s_baseMotionOutput->pData[current_index] = testMotionOutput_31[y][x] * scalar * s_differenceThreshold;
        }
    }
    region_idx = 0;
    x1 = x_offset_31;
    y1 = y_offset_31;
    x2 = x_offset_31 + 7;  //ncol - 1
    y2 = y_offset_31 + 7;  //nrow - 1
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 32 ------------------------------------------------------------
    // Edge Case testing
    test_case = 32;
    sub_test_case = test_case * 100 + 1;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    static const u8 testMotionOutput_32[10][10] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 1, 1},
        {0, 0, 0, 0, 0, 0, 0, 0, 1, 1},
        {0, 0, 0, 0, 0, 0, 1, 1, 0, 0},
        {0, 0, 0, 0, 0, 0, 1, 1, 0, 0},
        {0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
        {0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
        {0, 0, 1, 1, 0, 0, 0, 0, 0, 0},
        {0, 0, 1, 1, 0, 0, 0, 0, 0, 0},
        {1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
        {1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    };
    for (u32 y = 0; y < 10; y++) {
        for (u32 x = 0; x < 10; x++) {
            u32 current_index = y * HorizontalMoxels + x;
            s_baseMotionOutput->pData[current_index] = testMotionOutput_32[y][x] * scalar * s_differenceThreshold;
        }
    }
    region_idx = 0;
    x1 = 0;
    y1 = 0;
    x2 = 9;  //ncol - 1
    y2 = 9;  //nrow - 1
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, sub_test_case++);

    for (u32 y = 0; y < 10; y++) {
        for (u32 x = 0; x < 10; x++) {
            u32 current_index = y * HorizontalMoxels + x;
            u32 x1 = 9 - x;
            s_baseMotionOutput->pData[current_index] = testMotionOutput_32[y][x1] * scalar * s_differenceThreshold;
        }
    }
    region_idx = 0;
    x1 = 0;
    y1 = 0;
    x2 = 9;  //ncol - 1
    y2 = 9;  //nrow - 1
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, sub_test_case);

    //****************************************************************************
    // Motion Zone Bitmap Test
    //****************************************************************************
    s_motion->EnableMotionZones(true);

    // Set motionzone to top left quadrant:
    LTMediaMotionDetection_MotionZoneBitGrid *pBitGrid = CreateBitGridFromRectangle(0, 0, BitGridWidth / 2, BitGridHeight / 2);
    s_motion->SetMotionZones(pBitGrid);
    lt_free(pBitGrid);
    // Sleep is needed for Anyka based cameras, as otherwise the motion zone
    // would not yet be ready in time for the next test.
    s_IThread->Sleep(LTTime_Seconds(1));

    // Test 33 ------------------------------------------------------------
    // When there is no overlap between the motion zone and the motion ROI, no motion should be detected.
    test_case = 33;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 y = VerticalMoxels / 2; y < VerticalMoxels; y++) {
        for (u32 x = HorizontalMoxels / 2; x < HorizontalMoxels; x++) {
            u32 i = y * HorizontalMoxels + x;
            s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold;
        }
    }
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_FALSE(trc, isMotion, "Error: %s case %d - No motion expected!", trc->GetTestName(), test_case);

    // Test 34 ------------------------------------------------------------
    // When there is full overlap between the motion zone and the motion ROI
    test_case = 34;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 y = 0; y < VerticalMoxels / 2; y++) {
        for (u32 x = 0; x < HorizontalMoxels / 2; x++) {
            u32 i = y * HorizontalMoxels + x;
            s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold;
        }
    }
    region_idx = 0;
    x1 = 0;
    y1 = 0;
    x2 = (BitGridWidth / 2) * (HorizontalMoxels / BitGridWidth) - 1;
    y2 = (BitGridHeight / 2) * (VerticalMoxels / BitGridHeight) - 1;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Test 35 ------------------------------------------------------------
    // When there is partial overlap between the motion zone and the motion ROI
    test_case = 35;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 y = 4; y < VerticalMoxels; y++) {
        for (u32 x = 4; x < HorizontalMoxels; x++) {
            u32 i = y * HorizontalMoxels + x;
            s_baseMotionOutput->pData[i] = scalar * s_differenceThreshold;
        }
    }
    region_idx = 0;
    x1 = 4;
    y1 = 4;
    x2 = (BitGridWidth / 2) * (HorizontalMoxels / BitGridWidth) - 1;
    y2 = (BitGridHeight / 2) * (VerticalMoxels / BitGridHeight) - 1;
    ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

    // Finish s_motionZoneBitmap tests---------------------------------------
    // Reset pBitGrid
    ResetMotionZonesToOnes();
    s_motion->EnableMotionZones(false);

    /* Additional BFS test cases. */
    if (bUseBFS && bUseMaxPooling) {  // TODO: expand to allow average pooling to be used here.
        // Test 36 ------------------------------------------------------------
        // Test BFS temporal smoothing
        test_case = 36;
        u32 temporalSmoothingFrames = 3;
        s_algorithm->API->SetTemporalSmoothingFrames(s_algorithm, temporalSmoothingFrames);
        lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
        for (u32 offset=0; offset < 5; offset+=2){
            for (u32 i = 0; i < HorizontalMoxels; i++) {
                s_baseMotionOutput->pData[i + HorizontalMoxels * offset] = scalar * s_differenceThreshold;
            }
            s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
        }
        region_idx = 0;
        x1 = 0;
        y1 = 0;
        x2 = HorizontalMoxels - 1;
        y2 = (DownsamplingScaleFactor == 1) ? 4 : 5;
        ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);

        test_case = 37;
        // Test that shows we need 360 scan for component connection (not just bottom half)
        // First wear off the effect of previous temporal smoothing frames
        for (u32 i=0; i < temporalSmoothingFrames; i++) {
            s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
        }
        lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);

        u32 x = 0;
        u32 y = 0;
        u32 idx = y * HorizontalMoxels + x;
        s_baseMotionOutput->pData[idx] = scalar * s_differenceThreshold;
        s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);

        x = 1;
        y = 1;
        idx = y * HorizontalMoxels + x;
        s_baseMotionOutput->pData[idx] = scalar * s_differenceThreshold;
        s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);

        x = 2;
        y = 2;
        idx = y * HorizontalMoxels + x;
        s_baseMotionOutput->pData[idx] = scalar * s_differenceThreshold;
        s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);

        x = 5;
        y = 1;
        idx = y * HorizontalMoxels + x;
        s_baseMotionOutput->pData[idx] = scalar * s_differenceThreshold;
        s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);

        region_idx = 0;
        x1 = 0;
        y1 = 0;
        x2 = 5;
        y2 = (DownsamplingScaleFactor == 1) ? 2 : 3;
        ROITest(trc, isMultiRegion, region_idx, x1, y1, x2, y2, test_case);
    }

    s_motion->StopDetectionEngine();
    TILT_MESSAGE(trc, "%s - Finish. =====================", trc->GetTestName());
}

u32 WaitForMotionEventProc(void) {
    /* Clear flags indicating which events this call to the motion proc generated before calling the proc */
    LTAtomic_Store(&ReceivedMotionEvents, 0);

    // Running within TILT test thread context, but presumed safe to notify motion event from here
    s_IEvent->NotifyEvent(s_hMotionEvent, s_baseMotionOutput);

    // Sleep for about a third of the total motion frame time, which should be plenty for the events to be received
    LTTime eventSleepTime = LTTime_Milliseconds(s_motionProcSleepTime);
    u32 maxSleeps = 50;
    u32 sleepCount = 0;
    while (sleepCount < maxSleeps) {
        s_IThread->Sleep(eventSleepTime);
        sleepCount++;
        if (LTAtomic_Load(&ReceivedMotionEvents) & kLTTestMotionFlag_ProcDone) { break; }
    }
    return sleepCount;
}

void WaitForMotionEventTest(const TiltImplReportingCallbacks *trc, TestMotionEventFlags expectedEvents, u32 test_case) {
    expectedEvents |= kLTTestMotionFlag_ProcDone;
    int warningTimeThreshold = 5;  // in millisec

    LTTime startTime = LT_GetCore()->GetKernelTime();

    u32 sleepCount = WaitForMotionEventProc();

    LTTime elapsed = LTTime_Subtract(lastMotionEventTime, startTime);
    if (elapsed.nNanoseconds < 0) {
        LTTime timeAfterTimeout = LT_GetCore()->GetKernelTime();
        elapsed = LTTime_Subtract(timeAfterTimeout, startTime);
    }

    /* LT-2245: Gather data about how often the test takes more than 1 sleep, as we have seen occasional unit test
     * failures with the motion proc taking longer than expected. */
    if (sleepCount > 1) {
        LTLOG_YELLOWALERT("waitformotproc.delay", "Slow motion proc performance: unit test %s case %lu took %lu sleeps.", trc->GetTestName(), LT_Pu32(test_case), LT_Pu32(sleepCount));
        s_motionProcDelays++;
    }

    enum { strbufSize = 80 };
    char strbuf[strbufSize];
    char * processTimeStr = motionDetection_timeWithUnits(strbuf, strbufSize, &elapsed);
    if (LTTime_IsGreaterThan(elapsed, LTTime_Milliseconds(warningTimeThreshold))) {
        TILT_MESSAGE(trc, "Warning! Elapsed time > %d ms. Test Case %d; Time delta: %s", warningTimeThreshold, test_case, processTimeStr);
    }
    TILT_MSG_DEBUG(trc, "Test Case %lu; Expected flags: 0x%02x. Time delta: %s", LT_Pu32(test_case), expectedEvents, processTimeStr);
    (void) processTimeStr;

    if (expectedEvents == LTAtomic_Load(&ReceivedMotionEvents)) {
        TILT_MSG_DEBUG(trc, "Success, all expected events received.");
    }

    TestMotionEventFlags extraEvents = LTAtomic_Load(&ReceivedMotionEvents) & (~expectedEvents);
    TILT_EXPECT_TRUE(trc, extraEvents == 0, "Case %lu Error: Unexpected events: 0x%02x!", LT_Pu32(test_case), extraEvents);

    TestMotionEventFlags missingExpected = expectedEvents & (~LTAtomic_Load(&ReceivedMotionEvents));
    TILT_EXPECT_TRUE(trc, missingExpected == 0, "Case %lu Error: Missing events: 0x%02x! Try increasing sleeptime", LT_Pu32(test_case), missingExpected);
}

void WaitForZoneStateEventProc(const TiltImplReportingCallbacks *trc, TestZoneStateEventFlags expectedEvents, u32 test_case) {
    /* Clear flags indicating which events this call to the zone state proc generated earlier*/
    LTAtomic_Store(&ReceivedZoneStateEvents, 0);

    // Sleep for the events to be received
    LTTime eventSleepTime = LTTime_Milliseconds(20);
    u32 maxSleeps = 50;
    u32 sleepCount = 0;
    while (sleepCount < maxSleeps) {
        s_IThread->Sleep(eventSleepTime);
        sleepCount++;
        if (LTAtomic_Load(&ReceivedZoneStateEvents) & kLTTestZoneFlag_ProcDone) {
        break; }
    }

    if (expectedEvents == LTAtomic_Load(&ReceivedZoneStateEvents)) {
        TILT_MESSAGE(trc, "Success, expected events received.");
    }

    TestMotionEventFlags extraEvents = LTAtomic_Load(&ReceivedZoneStateEvents) & (~expectedEvents);
    TILT_EXPECT_TRUE(trc, extraEvents == 0, "Case %lu Error: Unexpected events: 0x%02x!", LT_Pu32(test_case), extraEvents);

    TestMotionEventFlags missingExpected = expectedEvents & (~LTAtomic_Load(&ReceivedZoneStateEvents));
    TILT_EXPECT_TRUE(trc, missingExpected == 0, "Case %lu Error: Missing events: 0x%02x! Try increasing sleeptime", LT_Pu32(test_case), missingExpected);
}

/* =======================================================================================
    Test that Motion event can be correctly detected and stopped.
======================================================================================== */
static void SetBaseMotionOutputWithMotion(const TiltImplReportingCallbacks *trc, u32 x_start, u32 x_end, u32 y_start, u32 y_end) {
    TILT_ASSERT_TRUE(trc, x_start <= x_end, "Error: expect x_start: %lu <= x_end: %lu!", LT_Pu32(x_start), LT_Pu32(x_end));
    TILT_ASSERT_TRUE(trc, x_end < HorizontalMoxels, "Error: expect x_end: %lu < HorizontalMoxels %lu!", LT_Pu32(x_end), LT_Pu32(HorizontalMoxels));
    TILT_ASSERT_TRUE(trc, y_start <= y_end, "Error: expect y_start: %lu <= y_end %lu!", LT_Pu32(y_start), LT_Pu32(y_end));
    TILT_ASSERT_TRUE(trc, y_end < VerticalMoxels, "Error: expect y_end: %lu < VerticalMoxels %lu!", LT_Pu32(y_end), LT_Pu32(VerticalMoxels));

    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 y = y_start; y < y_end; y++) {
        for (u32 x = x_start; x < x_end; x++) {
            u32 i = y * HorizontalMoxels + x;
            s_baseMotionOutput->pData[i] = s_differenceThreshold;
        }
    }
}

static void MotionEventDetectionTest(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "%s - Start. =====================", trc->GetTestName());

    int test_case, sub_test_case;
    bool isMotion;
    TestMotionEventFlags expectedEvents;

    /* We don't care about snapshots in this test - we'll cover that elsewhere */
    TILT_ASSERT_TRUE(trc, WaitForStartDetectionEngine(trc, false), "Error: Motion Detection Engine is not running!");

    /* Set motion algorithm to use single ROI detection for this test. */
    s_algorithm->API->SetROICalculationStrategy(s_algorithm, kLTMediaMotionROICalculationStrategy_SingleROI);

    /* Test 1 ------------------------------------------------------------------
     * Check that a single frame without motion does not get detected as a motion frame or start a motion event
     */
    test_case = 1;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen); // Resetting pData to 0

    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_FALSE(trc, isMotion, "Error: (%s) CheckFrameForMotionTest case %d - No motion expected!", trc->GetTestName(), test_case);

    expectedEvents = kLTTestMotionFlag_None;
    WaitForMotionEventTest(trc, expectedEvents, test_case);

    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);

    TILT_EXPECT_TRUE(trc, FramesOfMotion == 0, "Error: FramesOfMotion = %d. Expect 0!", LT_Pu32(FramesOfMotion));
    TILT_EXPECT_TRUE(trc, FramesNoMotion > 0, "Error: FramesNoMotion = %d. Expect > 0!", LT_Pu32(FramesNoMotion));
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Stopped, "Error: Expect MotionState == kLTMediaMotionState_Stopped!");

    /* Test 2 ------------------------------------------------------------------
     * Check that having consecutive frames one frame short of the minimum required number does not start a motion event
     */
    test_case = 2;
    sub_test_case = test_case * 100 + 1;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    SetBaseMotionOutputWithMotion(trc, 0, 6, 0, 6);

    expectedEvents = kLTTestMotionFlag_None;
    for (u32 i = 0; i < MinMotionFramesForEvent - 1; i++) {
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);

    TILT_EXPECT_TRUE(trc, FramesOfMotion == MinMotionFramesForEvent - 1, "Error: FramesOfMotion = %d. Expect %d!", FramesOfMotion, MinMotionFramesForEvent - 1);
    TILT_EXPECT_TRUE(trc, FramesNoMotion == 0, "Error: FramesNoMotion = %d. Expect 0!", FramesNoMotion);
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Stopped, "Error: Expect MotionState == kLTMediaMotionState_Stopped!");

    /* Test 3 ------------------------------------------------------------------
     * Continuing from test 2, check that having the minimum required number of consecutive motion frames starts a
     * motion event, by adding one additional motion frame
     */
    test_case = 3;
    // Continuing from test 2, add 1 extra motion frame to trigger an event
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    expectedEvents = kLTTestMotionFlag_Started | kLTTestMotionFlag_Metadata;
    WaitForMotionEventTest(trc, expectedEvents, test_case);
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);
    TILT_EXPECT_TRUE(trc, FramesOfMotion == MinMotionFramesForEvent, "Error: FramesOfMotion = %d. Expect %d!", FramesOfMotion, MinMotionFramesForEvent);
    TILT_EXPECT_TRUE(trc, FramesNoMotion == 0, "Error: FramesNoMotion = %d. Expect 0!", FramesNoMotion);
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Started, "Error: Expect MotionState == kLTMediaMotionState_Started!");

    /* Test 4 ------------------------------------------------------------------
     * Continuing from test 3, check that having a single frame without motion resets FramesOfMotion but does not end
     * the motion event
     */
    test_case = 4;
    TILT_MESSAGE(trc, "%s Case %lu.", trc->GetTestName(), LT_Pu32(test_case));
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen); // Resetting pData to 0
    isMotion = s_algorithm->API->CheckFrameForMotion(s_algorithm, s_baseMotionOutput);
    TILT_ASSERT_FALSE(trc, isMotion, "Error: (%s) CheckFrameForMotionTest case %d - No motion expected!", trc->GetTestName(), test_case);
    expectedEvents = kLTTestMotionFlag_None;
    WaitForMotionEventTest(trc, expectedEvents, test_case);
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);

    TILT_EXPECT_TRUE(trc, FramesOfMotion == 0, "Error: FramesOfMotion = %d. Expect 0!", FramesOfMotion);
    TILT_EXPECT_TRUE(trc, FramesNoMotion > 0, "Error: FramesNoMotion = %d. Expect > 0!", FramesNoMotion);
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Started, "Error: Expect MotionState == kLTMediaMotionState_Started!");

    /* Test 5 ------------------------------------------------------------------
     * Continuing from test 4, check that alternating between frames with and without motion keeps the motion event
     * active
     */
    test_case = 5;
    sub_test_case = test_case * 100 + 1;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen); // Resetting pData to 0

    s32 remainingFrames = MinFramesMotionEventDuration - MinMotionFramesForEvent - FramesSinceMotionStart - 1;
    u32 framesToOscillateMotion = remainingFrames < 1 ? 1 : remainingFrames;

    for (u32 i = 0; i < framesToOscillateMotion; i++) {
        if (i % 2 == 0) {
            SetBaseMotionOutputWithMotion(trc, 0, 6, 0, 6);
            expectedEvents = kLTTestMotionFlag_Metadata;
            WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
        } else {
            lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
            expectedEvents = kLTTestMotionFlag_None;
            WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
        }
    }
    s_motion->GetMotionState(&MotionState,&FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Started, "Error: Expect MotionState == kLTMediaMotionState_Started!");

    /* Test 6 ------------------------------------------------------------------
     * Continuing from test 5, check that having the required number of frames of no motion to end a motion event fails
     * to end the event if we have not reached the required minimum frame count for the event
     */
    test_case = 6;
    sub_test_case = test_case * 100 + 1;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen); // Resetting pData to 0

    for (u32 i = 0; i < MinFramesUntilEventEnd - FramesNoMotion; i++) {
        expectedEvents = kLTTestMotionFlag_None;
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);

    TILT_EXPECT_TRUE(trc, FramesOfMotion == 0, "Error: FramesOfMotion = %d. Expect 0!", FramesOfMotion);
    TILT_EXPECT_TRUE(trc, FramesNoMotion == MinFramesUntilEventEnd, "Error: FramesNoMotion = %d. Expect %d!", FramesNoMotion, MinFramesUntilEventEnd);
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Started, "Error: Expect MotionState == kLTMediaMotionState_Started!");

    /* Test 7 ------------------------------------------------------------------
     * Continuing from test 6, check that the event does not end if the minimum required event frame number has been
     * reached but the required consecutive 'no motion' count was not reached
     */
    test_case = 7;
    sub_test_case = test_case * 100 + 1;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    SetBaseMotionOutputWithMotion(trc, 0, 6, 0, 6);

    for (u32 i = 0; i < 2; i++) {
        expectedEvents = kLTTestMotionFlag_Metadata;
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);

    TILT_EXPECT_TRUE(trc, FramesOfMotion == 2, "Error: FramesOfMotion = %d. Expect 2!", FramesOfMotion);
    TILT_EXPECT_TRUE(trc, FramesSinceMotionStart >= MinFramesMotionEventDuration, "Error: Expect FramesSinceMotionStart >= %d!", MinFramesMotionEventDuration);
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Started, "Error: Expect MotionState == kLTMediaMotionState_Started!");

    /* Test 8 ------------------------------------------------------------------
     * Continuing from test 7, check that having no motion for one frame less than the required number of 'no motion'
     * frames to end a motion event keeps the event active
     */
    test_case = 8;
    sub_test_case = test_case * 100 + 1;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen); // Resetting pData to 0

    for (u32 i = 0; i < MinMotionFramesForEvent - 1; i++) {
        expectedEvents = kLTTestMotionFlag_None;
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);

    TILT_EXPECT_TRUE(trc, FramesOfMotion == 0, "Error: FramesOfMotion = %d. Expect 0!", FramesOfMotion);
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Started, "Error: Expect MotionState == kLTMediaMotionState_Started!");

    /* Test 9 ------------------------------------------------------------------
     * Continuing from test 8, check that having the exact number of frames of no motion to end an event does end that
     * event, by adding one additional no motion frame
     */
    test_case = 9;
    sub_test_case = test_case * 100 + 1;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen); // Resetting pData to 0

    expectedEvents = kLTTestMotionFlag_Stopped;
    WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);

    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Stopped, "Error: Expect MotionState == kLTMediaMotionState_Stopped!");
    TILT_EXPECT_TRUE(trc, FramesNoMotion == MinFramesUntilEventEnd, "Error: FramesNoMotion = %d. Expect %d!", FramesNoMotion, MinFramesUntilEventEnd);

    s_motion->StopDetectionEngine();
    TILT_MESSAGE(trc, "%s - Finish. =====================", trc->GetTestName());
}

/* =======================================================================================
    Test that frame selection and snapshot events work as expected.
======================================================================================== */
static void MotionSnapshotEventTest(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "%s - Start. =====================", trc->GetTestName());

    int test_case, sub_test_case;
    TestMotionEventFlags expectedEvents;

    /* The first test cases remove the minimum required event duration to focus on other parts of the state machine
     * behaviour. */
    s_motion->SetMinFramesMotionEventDuration(0);

    /* NOTE: Since this test focuses on snapshots, to keep things simple we trigger motion using a single moxel.
     * We therefore use the single ROI algorithm. */
    s_algorithm->API->SetROICalculationStrategy(s_algorithm, kLTMediaMotionROICalculationStrategy_SingleROI);
    TILT_ASSERT_TRUE(trc, WaitForStartDetectionEngine(trc, true), "Error: Motion Detection Engine is not running!");

    /* Test 1 ------------------------------------------------------------------
     * Single motion frame from event start.
     *
     * Check that only a single snapshot is triggered.
     * Check that snapshot is released after first-snapshot expiry time.
     */
    test_case = 1;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    s_baseMotionOutput->pData[0] = s_differenceThreshold;

    // Trigger the motion event
    sub_test_case = test_case * 1000;
    for (u32 i = 0; i < MinMotionFramesForEvent; i++) {
        if (i < MinMotionFramesForEvent - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_Started | kLTTestMotionFlag_Metadata | kLTTestMotionFlag_SnapshotTriggered;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    // No more motion until the event ends
    sub_test_case = test_case * 1000 + 100;
    s_baseMotionOutput->pData[0] = s_differenceThreshold - 1;
    expectedEvents = kLTTestMotionFlag_None;
    for (u32 i = 0; i < MinFramesUntilEventEnd - 1; i++) {
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    // Final empty motion frame releases the snapshot and ends the motion event
    sub_test_case = test_case * 1000 + 200;
    expectedEvents = kLTTestMotionFlag_SnapshotDelivered | kLTTestMotionFlag_Stopped;
    WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);

    /* Test 2 ------------------------------------------------------------------
     * Decreasing weights.
     *
     * Check that only a single snapshot is triggered.
     * Check that snapshot is released after first-snapshot expiry time, despite ongoing motion.
     */
    test_case = 2;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);

    // Generate a motion event via motion in the centre of the frame
    sub_test_case = test_case * 1000;
    u32 motionPos = VerticalMoxels / 2 * HorizontalMoxels + HorizontalMoxels / 2;
    s_baseMotionOutput->pData[motionPos] = s_differenceThreshold;
    for (u32 i = 0; i < MinMotionFramesForEvent; i++) {
        if (i < MinMotionFramesForEvent - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_Started | kLTTestMotionFlag_Metadata | kLTTestMotionFlag_SnapshotTriggered;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    // Move motion outwards at each step to decrease weight, so that only one snapshot is produced
    sub_test_case = test_case * 1000 + 100;
    for (u32 i = 0; i < MaxFramesUntilFirstSnapshot; i++) {
        s_baseMotionOutput->pData[motionPos] = s_differenceThreshold - 1;
        motionPos--;
        s_baseMotionOutput->pData[motionPos] = s_differenceThreshold;
        if (i == MaxFramesUntilFirstSnapshot - 1) {
            expectedEvents = kLTTestMotionFlag_Metadata | kLTTestMotionFlag_SnapshotDelivered;
        } else {
            expectedEvents = kLTTestMotionFlag_Metadata;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    // Terminate the motion event. No further snapshots should be produced
    sub_test_case = test_case * 1000 + 200;
    expectedEvents = kLTTestMotionFlag_None;
    s_baseMotionOutput->pData[motionPos] = s_differenceThreshold - 1;
    for (u32 i = 0; i < MinFramesUntilEventEnd; i++) {
        if (i < MinFramesUntilEventEnd - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_Stopped;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    /* Test 3 ------------------------------------------------------------------
     * Slow increasing weights.
     *
     * Check that snapshots are triggered for each better weight seen.
     * Check that latest snapshot is released after first-snapshot expiry time.
     * Check that no new snapshots are triggered after the first snapshot is released.
     */
    test_case = 3;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);

    // Generate a motion event via motion starting at the centre-left of the frame
    sub_test_case = test_case * 1000;
    motionPos = VerticalMoxels / 2 * HorizontalMoxels;
    s_baseMotionOutput->pData[motionPos] = s_differenceThreshold;
    for (u32 i = 0; i < MinMotionFramesForEvent; i++) {
        if (i < MinMotionFramesForEvent - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_Started | kLTTestMotionFlag_Metadata | kLTTestMotionFlag_SnapshotTriggered;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    // Move motion inwards to increase weight, to discard and update the snapshot on each move
    // Stop motion before we get near the first-snapshot expiry time, to avoid non-deterministic snapshot triggers
    sub_test_case = test_case * 1000 + 100;
    for (u32 i = 0; i < MaxFramesUntilFirstSnapshot+10; i++) {
        s_baseMotionOutput->pData[motionPos] = s_differenceThreshold - 1;

        // Only move further in every few frames, as we don't want to reach the centre before the timeout is reached.
        // To avoid race conditions on snapshot generation, we also suppress motion around the expiry time
        if (i < MaxFramesUntilFirstSnapshot-2 || i > MaxFramesUntilFirstSnapshot) {
            if (!(i % 4)) motionPos++;
            s_baseMotionOutput->pData[motionPos] = s_differenceThreshold;
            expectedEvents = kLTTestMotionFlag_Metadata;

            // Only expect triggers until the first snapshot is released
            if (!(i % 4) && i < MaxFramesUntilFirstSnapshot) expectedEvents |= kLTTestMotionFlag_SnapshotTriggered;

        } else if (i == MaxFramesUntilFirstSnapshot - 1) {
            expectedEvents = kLTTestMotionFlag_SnapshotDelivered;
        } else {
            expectedEvents = kLTTestMotionFlag_None;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    // Terminate the motion event
    sub_test_case = test_case * 1000 + 200;
    expectedEvents = kLTTestMotionFlag_None;
    s_baseMotionOutput->pData[motionPos] = s_differenceThreshold - 1;
    for (u32 i = 0; i < MinFramesUntilEventEnd; i++) {
        if (i < MinFramesUntilEventEnd - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_Stopped;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    /* Test 4 ------------------------------------------------------------------
     * Slow increasing then decreasing weights.
     *
     * Check that snapshots are triggered for each better weight seen only (not decreasing).
     * Check that latest snapshot is released after first-snapshot expiry time.
     * Check that no new snapshots are triggered after the first snapshot is released.
     */
    test_case = 4;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);

    // Generate a motion event via motion starting at the centre-left of the frame
    sub_test_case = test_case * 1000;
    motionPos = VerticalMoxels / 2 * HorizontalMoxels;
    s_baseMotionOutput->pData[motionPos] = s_differenceThreshold;
    for (u32 i = 0; i < MinMotionFramesForEvent; i++) {
        if (i < MinMotionFramesForEvent - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_Started | kLTTestMotionFlag_Metadata | kLTTestMotionFlag_SnapshotTriggered;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    sub_test_case = test_case * 1000 + 100;
    u32 switchingFrame = MaxFramesUntilFirstSnapshot * 2 / 3;
    for (u32 i = 0; i < MaxFramesUntilFirstSnapshot + 2; i++) {
        s_baseMotionOutput->pData[motionPos] = s_differenceThreshold - 1;

        // Only move further in every few frames, as we don't want to reach the centre before the timeout is reached.
        // To avoid race conditions on snapshot generation, we also suppress motion around the expiry time
        if (i < MaxFramesUntilFirstSnapshot-2 || i > MaxFramesUntilFirstSnapshot) {
            expectedEvents = 0;

            if (!(i % 4)) {
                if (i < switchingFrame) {
                    motionPos++;
                    // Only expect triggers if increasing motion and only until the first snapshot is released
                    if (i < MaxFramesUntilFirstSnapshot) {
                        expectedEvents |= kLTTestMotionFlag_SnapshotTriggered;
                    }
                } else {
                    motionPos--;
                }
            }
            s_baseMotionOutput->pData[motionPos] = s_differenceThreshold;
            expectedEvents |= kLTTestMotionFlag_Metadata;

        } else if (i == MaxFramesUntilFirstSnapshot - 1) {
            expectedEvents = kLTTestMotionFlag_SnapshotDelivered;
        } else {
            expectedEvents = kLTTestMotionFlag_None;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    // Terminate the motion event
    sub_test_case = test_case * 1000 + 200;
    expectedEvents = kLTTestMotionFlag_None;
    s_baseMotionOutput->pData[motionPos] = s_differenceThreshold - 1;
    for (u32 i = 0; i < MinFramesUntilEventEnd; i++) {
        if (i < MinFramesUntilEventEnd - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_Stopped;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    /* Test 5 ------------------------------------------------------------------
     * Test notifications of multiple snapshots when configured for three snapshots per event,
     * with continuous inward motion.
     *
     * Check that snapshots are triggered for each better weight seen.
     * Check that a snapshot is released after first-snapshot expiry time.
     * Check that additional snapshots are triggered after the first snapshot is released.
     * Check that additional snapshots are released after the best snapshot expiry time.
     */
    test_case = 5;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen); // Resetting pData to 0

    /* From test 5 onwards, use the minimum required event duration and maximum number of snapshots. */
    s_motion->SetMotionEventMaxSnapshots(MaxSnapshotsPerMotionEvent);
    s_motion->SetMinFramesMotionEventDuration(MinFramesMotionEventDuration);

    // Generate a motion event via motion starting at the centre-left of the frame
    sub_test_case = test_case * 1000;
    motionPos = VerticalMoxels / 2 * HorizontalMoxels;
    s_baseMotionOutput->pData[motionPos] = s_differenceThreshold;
    for (u32 i = 0; i < MinMotionFramesForEvent; i++) {
        if (i < MinMotionFramesForEvent - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_Started | kLTTestMotionFlag_Metadata | kLTTestMotionFlag_SnapshotTriggered;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    // Motion in every frame, but only move inwards slowly.
    // Snapshots should be triggered on each inward step only.
    sub_test_case = test_case * 1000 + 100;
    u32 nextSnapshotExpiryFrame = MaxFramesUntilFirstSnapshot - 1;
    u32 numFramesForTest = MaxFramesUntilFirstSnapshot + 1 + ((MaxSnapshotsPerMotionEvent - 1) * (MaxFramesForBestSnapshot + 1)) + 2;
    u8 snapShotCount = 0;
    for (u32 i = 0; i < numFramesForTest; i++) {
        s_baseMotionOutput->pData[motionPos] = s_differenceThreshold - 1;
        expectedEvents = 0;

        if (i == nextSnapshotExpiryFrame) {
            expectedEvents |= kLTTestMotionFlag_SnapshotDelivered;
            snapShotCount++;

        } else if (i == nextSnapshotExpiryFrame + 1) {
            // Move inwards 1 frame after each snapshot would have been notified
            motionPos++;
            nextSnapshotExpiryFrame += MaxFramesForBestSnapshot + 1;

            if (snapShotCount < MaxSnapshotsPerMotionEvent) {
                // After the last expected snapshot, there should be no more triggers
                expectedEvents |= kLTTestMotionFlag_SnapshotTriggered;
            }
        }

        s_baseMotionOutput->pData[motionPos] = s_differenceThreshold;
        expectedEvents |= kLTTestMotionFlag_Metadata;
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, &SnapshotsUploaded);
    u32 remainingFramesUntilMinimumDuration = MinFramesMotionEventDuration - FramesSinceMotionStart > 0 ? MinFramesMotionEventDuration - FramesSinceMotionStart : 0;
    u32 remainingFramesUntilEventEnd = LT_MAX(MinFramesUntilEventEnd, remainingFramesUntilMinimumDuration);

    // Terminate the motion event
    sub_test_case = test_case * 1000 + 200;
    s_baseMotionOutput->pData[motionPos] = s_differenceThreshold - 1;
    for (u32 i = 0; i < remainingFramesUntilEventEnd; i++) {
        if (i < remainingFramesUntilEventEnd - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_Stopped;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    /* Test 6 ------------------------------------------------------------------
     * Test notifications of multiple snapshots when configured for three snapshots per event,
     * with inward motion that resets to an outer start point after each snapshot.
     *
     * Check that snapshots are triggered for each better weight seen.
     * Check that a snapshot is released after first-snapshot expiry time.
     * Check that additional snapshots are triggered even if the weight *decreases* after the previous snapshot.
     * Check that additional snapshots are released after the best snapshot expiry time.
     */
    test_case = 6;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen); // Resetting pData to 0

    // Generate a motion event via motion starting at the centre-left of the frame
    sub_test_case = test_case * 1000;
    motionPos = VerticalMoxels / 2 * HorizontalMoxels;
    s_baseMotionOutput->pData[motionPos] = s_differenceThreshold;
    for (u32 i = 0; i < MinMotionFramesForEvent; i++) {
        if (i < MinMotionFramesForEvent - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_Started | kLTTestMotionFlag_Metadata | kLTTestMotionFlag_SnapshotTriggered;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    // Motion in every frame, but only move inwards slowly.
    // Snapshots should be triggered on each inward step only.
    sub_test_case = test_case * 1000 + 100;
    nextSnapshotExpiryFrame = MaxFramesUntilFirstSnapshot - 1;
    numFramesForTest = MaxFramesUntilFirstSnapshot + 1 + ((MaxSnapshotsPerMotionEvent - 1) * (MaxFramesForBestSnapshot + 1)) + 2;
    snapShotCount = 0;
    for (u32 i = 0; i < numFramesForTest; i++) {
        s_baseMotionOutput->pData[motionPos] = s_differenceThreshold - 1;
        expectedEvents = 0;

        // Move inwards every 4 frames
        if (!(i % 4)) {
            motionPos++;
            if (snapShotCount < MaxSnapshotsPerMotionEvent) {
                // After the last expected snapshot, there should be no more triggers
                expectedEvents |= kLTTestMotionFlag_SnapshotTriggered;
            }
        }

        if (i == nextSnapshotExpiryFrame) {
            expectedEvents |= kLTTestMotionFlag_SnapshotDelivered;
            snapShotCount++;

            // Reset the motion position to the edge if a snapshot is released
            motionPos = VerticalMoxels / 2 * HorizontalMoxels;

        } else if (i == nextSnapshotExpiryFrame + 1) {
            nextSnapshotExpiryFrame += MaxFramesForBestSnapshot + 1;
            if (snapShotCount < MaxSnapshotsPerMotionEvent) {
                expectedEvents |= kLTTestMotionFlag_SnapshotTriggered;
            }
        }

        s_baseMotionOutput->pData[motionPos] = s_differenceThreshold;
        expectedEvents |= kLTTestMotionFlag_Metadata;
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, &SnapshotsUploaded);
    remainingFramesUntilMinimumDuration = MinFramesMotionEventDuration - FramesSinceMotionStart > 0 ? MinFramesMotionEventDuration - FramesSinceMotionStart : 0;
    remainingFramesUntilEventEnd = LT_MAX(MinFramesUntilEventEnd, remainingFramesUntilMinimumDuration);

    // Terminate the motion event
    sub_test_case = test_case * 1000 + 200;
    s_baseMotionOutput->pData[motionPos] = s_differenceThreshold - 1;
    for (u32 i = 0; i < remainingFramesUntilEventEnd; i++) {
        if (i < remainingFramesUntilEventEnd - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_Stopped;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    /* Test 7 ------------------------------------------------------------------
     * Single motion frame from event start with minimum event duration.
     *
     * Check that only a single snapshot is triggered when motion stops early.
     * Check that snapshot is released after first-snapshot expiry time, and event lasts exactly the minimum event
     * duration.
     */
    test_case = 7;
    TILT_MESSAGE(trc, "%s Case %d.", trc->GetTestName(), test_case);
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    s_baseMotionOutput->pData[0] = s_differenceThreshold;

    // Trigger the motion event
    sub_test_case = test_case * 1000;
    for (u32 i = 0; i < MinMotionFramesForEvent; i++) {
        if (i < MinMotionFramesForEvent - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_Started | kLTTestMotionFlag_Metadata | kLTTestMotionFlag_SnapshotTriggered;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, &SnapshotsUploaded);
    u32 remainingFramesUntilSnapshotDelivery = MaxFramesUntilFirstSnapshot - FramesSinceMotionStart;

    /* No more motion until the event ends - no more snapshots.
     * The first snapshot is released due to the MaxFramesUntilFirstSnapshot timer but the event keeps going because
     * the minimum frame count for the event still hasn't been reached */
    sub_test_case = test_case * 1000 + 100;
    s_baseMotionOutput->pData[0] = s_differenceThreshold - 1;
    for (u32 i = 0; i < remainingFramesUntilSnapshotDelivery; i++) {
        if (i < remainingFramesUntilSnapshotDelivery - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_SnapshotDelivered;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, &SnapshotsUploaded);
    TILT_ASSERT_TRUE(trc, MinFramesMotionEventDuration - FramesSinceMotionStart > 0, "Error: MinFramesMotionEventDuration is not set high enough! Current value: %lu", LT_Pu32(MinFramesMotionEventDuration));
    remainingFramesUntilEventEnd = MinFramesMotionEventDuration - FramesSinceMotionStart;

    sub_test_case = test_case * 1000 + 300;
    for (u32 i = 0; i < remainingFramesUntilEventEnd; i++) {
        if (i < remainingFramesUntilEventEnd - 1) {
            expectedEvents = kLTTestMotionFlag_None;
        } else {
            expectedEvents = kLTTestMotionFlag_Stopped;
        }
        WaitForMotionEventTest(trc, expectedEvents, sub_test_case++);
    }

    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, &SnapshotsUploaded);
    TILT_ASSERT_TRUE(trc, MinFramesMotionEventDuration == FramesSinceMotionStart, "Error: Expected event to last for %lu frames! Actual value: %lu", LT_Pu32(MinFramesMotionEventDuration), LT_Pu32(FramesSinceMotionStart));

    // Reset to one snapshot per motion event for other tests
    s_motion->SetMotionEventMaxSnapshots(1);

    s_motion->StopDetectionEngine();
    TILT_MESSAGE(trc, "%s - Finish. =====================", trc->GetTestName());
}

static bool LTTest_ZoneTestThreadInit(void) {
    s_motion->OnZoneStateEvent(OnZoneStateEvent, NULL, NULL);
    return true;
}

static void LTTest_ZoneTestThreadExit(void) {
    /* Ensure the registered events are disabled before the thread is destroyed */
    s_motion->NoZoneStateEvent(OnZoneStateEvent);
}

static void MotionZoneEventTest(const TiltImplReportingCallbacks *trc) {
    int test_case = 1;
    // start the ZoneStateEventThread for event test
    s_zoneTestThread = s_core->CreateThread("ZoneStateEventThread");
    s_IZoneTestThread = lt_gethandleinterface(ILTThread, s_zoneTestThread);
    TILT_ASSERT_TRUE(trc, s_IZoneTestThread && s_zoneTestThread, "Failed to create test thread");
    s_IZoneTestThread->SetStackSize(s_zoneTestThread, 1024);
    s_IZoneTestThread->Start(s_zoneTestThread, LTTest_ZoneTestThreadInit, LTTest_ZoneTestThreadExit);

    // Sleep to wait for thread to register for ZoneStateEvents
    s_IThread->Sleep(LTTime_Milliseconds(100));
    s_motion->EnableMotionZones(true);
    WaitForZoneStateEventProc(trc, kLTTestZoneFlag_EnableMotionZone, test_case);

    // Set motionzone to top left quadrant:
    LTMediaMotionDetection_MotionZoneBitGrid *pBitGrid = CreateBitGridFromRectangle(0, 0, BitGridWidth / 2, BitGridHeight / 2);
    s_motion->SetMotionZones(pBitGrid);
    lt_free(pBitGrid);
    WaitForZoneStateEventProc(trc, kLTTestZoneFlag_SetMotionZone, test_case++);
}

/* =======================================================================================================
    Test that motion zones are correctly set by checking the bitgrid input and the upscaled bitmap output
======================================================================================================== */
/* Reference code from UnitTestLTCoreKernelTimeTests.c
*/
static void SetMotionZoneProcessTimeTest(const TiltImplReportingCallbacks *trc, u32 N, int test_case) {
    /* Get the average multiple motion processing time over N iterations and test if it is within the specified time limit.
    */
    LTTime TimeLimit = LTTime_Milliseconds(5);

    enum { strbufSize = 80 };
    char strbuf[strbufSize];

    LTTime before, after, elapsed, totalElapsed, avgElapsed;

    for (u32 i = 0; i < N; i++) {
        before = LT_GetCore()->GetKernelTime();

        LTMediaMotionDetection_MotionZoneBitGrid *pBitGrid = CreateBitGridFromRectangle(0, 0, BitGridWidth / 2, BitGridHeight / 2);
        s_motion->SetMotionZones(pBitGrid);
        lt_free(pBitGrid);

        after = LT_GetCore()->GetKernelTime();
        elapsed = LTTime_Subtract(after, before);
        totalElapsed = (i > 0) ? LTTime_Add(totalElapsed, elapsed) : elapsed;
        char * processTimeStr = motionDetection_timeWithUnits(strbuf, strbufSize, &elapsed);
        TILT_MSG_DEBUG(trc, "Test case: %d iter %d. Set motion zones process time: %s", test_case, LT_Pu32(i), processTimeStr);
        (void) processTimeStr;
    }

    avgElapsed = LTTime_Divide(totalElapsed, N);
    char *  avgProcessTimeStr = motionDetection_timeWithUnits(strbuf, strbufSize, &avgElapsed);
    TILT_MSG_DEBUG(trc, "Test case: %d. Avg Multiple-motionROIs process time: %s", test_case, avgProcessTimeStr);

    bool test = LTTime_IsLessThan(avgElapsed, TimeLimit);
    char * timeLimitStr = motionDetection_timeWithUnits(strbuf, strbufSize, &TimeLimit);
    TILT_ASSERT_TRUE( trc, test, "Test case: %d. Error: Avg Multi-motion ROI processing time %s >= %s!", test_case, avgProcessTimeStr, timeLimitStr);
}

static void SetMotionZonesTest(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "%s - Start. =====================", trc->GetTestName());
    int test_case = 1;

    u32 y_scale = VerticalMoxels / BitGridHeight;
    u32 x_scale = HorizontalMoxels / BitGridWidth;

    // Loop though bitgrid width and height:
    for (u32 nx = 1; nx <= BitGridWidth; nx++) {
        for (u32 ny = 1; ny <= BitGridHeight; ny++) {
            bool isCorrect = true;
            LTMediaMotionDetection_MotionZoneBitGrid *pBitGrid = CreateBitGridFromRectangle(0, 0, nx, ny);
            s_motion->SetMotionZones(pBitGrid);
            lt_free(pBitGrid);
            s_motion->GetMotionZoneBitmap(&pMotionZonesBitmap);

            // Loop though upscaled bitmap width and height:
            for (u32 y = 0; y < VerticalMoxels; y++) {
                for (u32 x = 0; x < HorizontalMoxels; x++) {
                    u32 i = y * HorizontalMoxels + x;
                    u32 wordIndex = i / pMotionZonesBitmap->nBitmapWordLen;
                    u32 bitIndex = i % pMotionZonesBitmap->nBitmapWordLen;
                    bool inZone = (pMotionZonesBitmap->pData[wordIndex] & (1 << bitIndex)) != 0;

                    if (y < y_scale * ny && x < x_scale * nx) {
                        isCorrect = inZone;
                    } else {
                        isCorrect = !inZone;
                    }
                    if (!isCorrect) {break;}
                }
            }
            TILT_ASSERT_TRUE(trc, isCorrect, "Error: SetMotionZonesTest case %d - Motion zones set incorrectly!", test_case);
            test_case++;
        }
    }

    // Process time test:
    test_case = 2;
    int N = 10;
    SetMotionZoneProcessTimeTest(trc, N, test_case);

    TILT_MESSAGE(trc, "Number of tests ran: %d", test_case - 1);

    // Clean up and reset MotionZone after test
    ResetMotionZonesToOnes();

    TILT_MESSAGE(trc, "%s - Finish. =====================", trc->GetTestName());
}


/* =================================================================================================
    Test moxel heatmap works as intended.
================================================================================================= */
static void HeatmapTestInner(const TiltImplReportingCallbacks *trc, bool UseMaxPooling);

static void HeatmapTest(const TiltImplReportingCallbacks *trc) {
    /* Set motion algorithm to use multiple ROI detection for this test. */
    s_algorithm->API->SetROICalculationStrategy(s_algorithm, kLTMediaMotionROICalculationStrategy_MultipleROIs);

    s_algorithm->API->EnableHeatmap(s_algorithm, true);
    s_motion->SetMinFramesMotionEventDuration(0);

    /* Override a few of the default heatmap parameters to speed up the test significantly. */
    MotionHeatmapParameters heatmapParams;
    s_algorithm->API->GetHeatmapParameters(s_algorithm, &heatmapParams);
    heatmapParams.cap = s_heatmapCap;
    heatmapParams.threshold = s_heatmapThreshold;
    heatmapParams.gain = s_heatmapGain;
    heatmapParams.drop = s_heatmapDrop;
    s_algorithm->API->SetHeatmapParameters(s_algorithm, &heatmapParams);

    /* Test the heatmap with max pooling. */
    HeatmapTestInner(trc, true);
    /* Test the heatmap with average pooling. */
    HeatmapTestInner(trc, false);

    s_algorithm->API->SetDownsamplingStrategy(s_algorithm, kLTMediaMotionDownsamplingStrategy_MaxPooling);
    s_algorithm->API->EnableHeatmap(s_algorithm, false);
    s_motion->SetMinFramesMotionEventDuration(MinFramesMotionEventDuration);
}

static void HeatmapTestInner(const TiltImplReportingCallbacks *trc, bool bUseMaxPooling) {
    /*
    Tests to perform:
    1. After a sequence of no motion frames, heatmap starts from 0.0 (not nregative value) and not affecting subsequent tests;
    2. When started from cold, motion get triggered as normal;
    3. Continuous motion cause heatup, when heatmap > threshold, further motonframe should be suppressed (ie no motion), and event should stop.
    4. After heatup to max value, we allow heatmap to cooldown enough for motion detection to re-enable.
    */
    TILT_MESSAGE(trc, "%s %s pooling) - Start. =====================", trc->GetTestName(), bUseMaxPooling ? "Max" : "Avg");

    LTMediaMotionDownsamplingStrategy downsamplingStrategy = bUseMaxPooling ?
                                                             kLTMediaMotionDownsamplingStrategy_MaxPooling :
                                                             kLTMediaMotionDownsamplingStrategy_AveragePooling;
    s_algorithm->API->SetDownsamplingStrategy(s_algorithm, downsamplingStrategy);

    bool bTakeSnapshot = false;
    TILT_ASSERT_TRUE(trc, WaitForStartDetectionEngine(trc, bTakeSnapshot), "Error: Motion Detection Engine is not running!");

    int test_case;

    // Test 1 ------------------------------------------------------------
    // After a sequence of no motion frames, heatmap starts from 0.0 (not nregative value)
    // and not affecting subsequent tests
    test_case = 1;
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    for (u32 i = 0; i < 2 * MinMotionFramesForEvent; i++) {
        WaitForMotionEventProc();
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);
    TILT_MESSAGE(trc, "Heatmap test case 1. FramesOfMotion: %lu, FramesNoMotion: %lu", LT_Pu32(FramesOfMotion), LT_Pu32(FramesNoMotion));
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Stopped, "Error: Expect MotionState == kLTMediaMotionState_Stopped!");

    // Check that all heatmap values are 0.0
    float heatmapMax = s_algorithm->API->GetHeatmapMax(s_algorithm);
    TILT_EXPECT_TRUE(trc, heatmapMax == 0.0, "Error: Expect Heatmap values be all 0.0 at this point!");

    // Check that heatmap min is not negative
    float heatmapMin = s_algorithm->API->GetHeatmapMin(s_algorithm);
    TILT_EXPECT_TRUE(trc, heatmapMin == 0.0, "Error: Expect heatmapMin (%.1f) == 0.0!", heatmapMin);

    SetBaseMotionOutputWithMotion(trc, 0, 6, 0, 6);

    // Test 2 ------------------------------------------------------------
    // When started from cold, motion get triggerd as normal;
    test_case = 2;
    bool areParamsSensible = MinMotionFramesForEvent * s_heatmapGain < s_heatmapThreshold;
    TILT_EXPECT_TRUE(trc, areParamsSensible, "Error: Params errors. Expect MinMotionFramesForEvent * heatmapGain < heatmapThreshold!");

    for (u32 i = 0; i < MinMotionFramesForEvent; i++) {
        WaitForMotionEventProc();
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);
    TILT_MESSAGE(trc, "Heatmap test case 2. FramesOfMotion: %lu, FramesNoMotion: %lu", LT_Pu32(FramesOfMotion), LT_Pu32(FramesNoMotion));
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Started, "Error: Expect MotionState == kLTMediaMotionState_Started!");

    heatmapMax = s_algorithm->API->GetHeatmapMax(s_algorithm);
    TILT_EXPECT_TRUE(trc, (0 < heatmapMax && heatmapMax < s_heatmapThreshold), "Error: Expect 0 < heatmapMax < heatmapThreshold!");

    // Test 3 ------------------------------------------------------------
    // Continue from test 2, Contiuous motion cause heatup, when heatmap > threshold, further motionframe should be suppressed
    // (ie no motion), and event should stop.
    test_case = 3;
    u32 count = (u32)(s_heatmapThreshold + MinFramesUntilEventEnd - MinMotionFramesForEvent);  // 30 + 20 - 20 = 30
    for (u32 i = 0; i < count; i++) {
        WaitForMotionEventProc();
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);
    TILT_MESSAGE(trc, "Heatmap test case 3. FramesOfMotion: %lu, FramesNoMotion: %lu", LT_Pu32(FramesOfMotion), LT_Pu32(FramesNoMotion));
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Stopped, "Error: Expect MotionState == kLTMediaMotionState_Stopped!");

    // Test 4 ------------------------------------------------------------
    // Continue from last test, check that heatmap values are capped at heatmapCap
    test_case = 4;
    for (u32 i = 0; i < s_heatmapCap - count + 10; i++) {
        WaitForMotionEventProc();
    }
    heatmapMax = s_algorithm->API->GetHeatmapMax(s_algorithm);
    TILT_EXPECT_TRUE(trc, heatmapMax == s_heatmapCap, "Error: Expect heatmapMax (%.1f) == heatmapCap ((%.1f))!", heatmapMax, s_heatmapCap);

    // Test 5 ------------------------------------------------------------
    // After heatup to max value, we allow heatmap to cooldown enough for motion detection to re-enable.
    test_case = 5;
    // Heat up to max value s_heatmapCap
    for (u32 i = 0; i < (u32)s_heatmapCap; i++) {
        WaitForMotionEventProc();
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);
    TILT_MESSAGE(trc, "Heatmap test case 4a. FramesOfMotion: %lu, FramesNoMotion: %lu", LT_Pu32(FramesOfMotion), LT_Pu32(FramesNoMotion));
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Stopped, "Error: Expect MotionState == kLTMediaMotionState_Stopped!");

    // Now cooldown to a level that allows motionframe to be detected for MinMotionFramesForEvent times
    lt_memset(s_baseMotionOutput->pData, 0, s_baseMotionOutput->nDataLen);
    u32 total_decrement = (u32)((s_heatmapCap - s_heatmapThreshold + MinMotionFramesForEvent) / s_heatmapDrop) + 1;  // (50 - 30 + 20) / 0.5 + 1 = 81
    for (u32 i = 0; i < total_decrement; i++) {
        WaitForMotionEventProc();
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);
    TILT_MESSAGE(trc, "Heatmap test case 4b. FramesOfMotion: %lu, FramesNoMotion: %lu", LT_Pu32(FramesOfMotion), LT_Pu32(FramesNoMotion));
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Stopped, "Error: Expect MotionState == kLTMediaMotionState_Stopped!");

    // Motion detected again for MinMotionFramesForEvent frames to trigger event
    SetBaseMotionOutputWithMotion(trc, 0, 6, 0, 6);
    for (u32 i = 0; i < MinMotionFramesForEvent; i++) {
        WaitForMotionEventProc();
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);
    TILT_MESSAGE(trc, "Heatmap test case 4c. FramesOfMotion: %lu, FramesNoMotion: %lu", LT_Pu32(FramesOfMotion), LT_Pu32(FramesNoMotion));
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Started, "Error: Expect MotionState == kLTMediaMotionState_Started!");

    // Test 6 ------------------------------------------------------------
    // Continuing from last test, heat up to max value heatmapCap again on the top left corner,
    // and check for motion in bottom right corner where it should not be affected
    test_case = 6;
    SetBaseMotionOutputWithMotion(trc, 0, 6, 0, 6);  // motion in top left corner
    for (u32 i = 0; i < s_heatmapCap + MinFramesUntilEventEnd; i++) {
        WaitForMotionEventProc();
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);
    TILT_MESSAGE(trc, "Heatmap test case 5a. FramesOfMotion: %lu, FramesNoMotion: %lu", LT_Pu32(FramesOfMotion), LT_Pu32(FramesNoMotion));
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Stopped, "Error: Expect MotionState == kLTMediaMotionState_Stopped!");

    SetBaseMotionOutputWithMotion(trc, 6, 12, 6, 12);  // motion to the right and bottom of previous motion, with no overlapping
    for (u32 i = 0; i < MinMotionFramesForEvent; i++) {
        WaitForMotionEventProc();
    }
    s_motion->GetMotionState(&MotionState, &FramesOfMotion, &FramesNoMotion, NULL, NULL, &FramesSinceMotionStart, NULL);
    TILT_MESSAGE(trc, "Heatmap test case 5b. FramesOfMotion: %lu, FramesNoMotion: %lu", LT_Pu32(FramesOfMotion), LT_Pu32(FramesNoMotion));
    TILT_EXPECT_TRUE(trc, MotionState == kLTMediaMotionState_Started, "Error: Expect MotionState == kLTMediaMotionState_Started!");

    // Test 7 ------------------------------------------------------------
    // Test the time it takes to process a frame when all moxels are above the threshold when heatmap is enabled.
    test_case = 7;
    u32 sub_test_case = test_case * 100 + 1;
    lt_memset(s_baseMotionOutput->pData, s_differenceThreshold, s_baseMotionOutput->nDataLen);
    MultipleMotionProcessTimeTest(trc, 20, sub_test_case++);

    TILT_MESSAGE(trc, "Number of tests ran: %d", test_case);
    s_motion->StopDetectionEngine();

    TILT_MESSAGE(trc, "%s - Finish. =====================", trc->GetTestName());
}

static void OnMetadataEvent(LTMediaMotionDetection_Metadata *metadata, u8 *motionUuid, void *clientData) {
    LT_UNUSED(metadata);
    LT_UNUSED(motionUuid);
    LT_UNUSED(clientData);
    // lastMotionEventTime = LT_GetCore()->GetKernelTime();

    if (LTAtomic_Load(&ReceivedMotionEvents) & kLTTestMotionFlag_Metadata) {
        LTAtomic_FetchOr(&ReceivedMotionEvents, kLTTestMotionFlag_Duplicate);
    } else {
        LTAtomic_FetchOr(&ReceivedMotionEvents, kLTTestMotionFlag_Metadata);
    }
}

static void OnMotionEvent(LTMediaMotionState motionState, u8 *motionUuid, LTMediaMotionDetection_Snapshot *motionSnapshot, void *clientData) {
    LT_UNUSED(motionUuid);
    LT_UNUSED(motionSnapshot);
    LT_UNUSED(clientData);
    // lastMotionEventTime = LT_GetCore()->GetKernelTime();

    /* Convert motion state to bitmask of event type, then check it's not a duplicate of a previous event */
    TestMotionEventFlags eventType = kLTTestMotionFlag_Unknown;
    switch (motionState) {
        case kLTMediaMotionState_Started:
            eventType = kLTTestMotionFlag_Started;
            break;
        case kLTMediaMotionState_Stopped:
            eventType = kLTTestMotionFlag_Stopped;
            break;
        case kLTMediaMotionState_Snapshot:
            eventType = kLTTestMotionFlag_SnapshotDelivered;
            break;
        default:
            break;
    }

    if (LTAtomic_Load(&ReceivedMotionEvents) & eventType) {
        LTAtomic_FetchOr(&ReceivedMotionEvents, kLTTestMotionFlag_Duplicate);
    } else {
        LTAtomic_FetchOr(&ReceivedMotionEvents, eventType);
    }
}


/*******************************************************************************
 * General configuration
 ******************************************************************************/

static bool LTTest_ThreadInit(void) {
    s_motion->OnMetadataEvent(OnMetadataEvent, NULL, NULL);
    s_motion->OnMotionEvent(OnMotionEvent, NULL, NULL);
    return true;
}

static void LTTest_ThreadExit(void) {
    /* Ensure the registered events are disabled before the thread is destroyed */
    s_motion->NoMetadataEvent(OnMetadataEvent);
    s_motion->NoMotionEvent(OnMotionEvent);
}

static const TiltImplTestSpecifier s_tests[] =
{
    TILT_TEST_SECTION("Basic functionality tests"),
    { MotionStartStopTest,                  "MotionInitTest",                   "Test that the motion library can be initialised and stopped successfully.", 0 },
    { MotionStartStopTest,                  "MotionReInitTest",                 "Test that the motion library can be re-initialised and stopped successfully.", 0 },
    TILT_TEST_SECTION("Motion detection and ROI tests"),
    { CheckFrameForMotionTest,              "CheckFrameForMotionTest",          "Test motion detections are correctly triggered for single ROI detection", 0 },
    { CheckFrameForMultipleMotionTest,      "CheckFrameForMultipleMotionTest",  "Test motion detections are correctly triggered for multi ROI detection", 0 },
    { CheckFrameForMotionROITest,           "CheckFrameForMotionROITest",       "Test a single ROI is calculated correctly for a range of inputs and regions", 0 },
    { CheckFrameForMultipleMotionROITest,   "CheckFrameForMultiMotionROITest",  "Test multiple ROIs are calculated correctly for a range of inputs and regions", 0 },
    { SetMotionZonesTest,                   "SetMotionZonesTest",               "Test the scaling up logic from the BitGrid to the full resolution motion zone bitmap works correctly", 0 },
    { HeatmapTest,                          "HeatmapTest",                      "Test the heatmap mechanism", 0 },
    TILT_TEST_SECTION("Multi-frame motion event tests"),
    { MotionEventDetectionTest,             "MotionEventDetectionTest",         "Test that motion events are correctly triggered/stopped", kTiltTestFlag_NeedsRtos },
    { MotionSnapshotEventTest,              "MotionSnapshotEventTest",          "Test that frame selection and snapshot events work as expected", kTiltTestFlag_NeedsRtos },
    { MotionZoneEventTest,                  "MotionZoneEventTest",              "Test that zone enable and set custom zone events are correctly triggered" , 0},
};
#define TEST_COUNT (sizeof(s_tests) / sizeof(s_tests[0]))

static void UnitTestLTMediaMotionDetection_BeforeAllTests(const TiltImplReportingCallbacks * pTRC) {
    // Open an instance of LTDeviceMedia for the duration of the test, to stop it thrashing the HW init during testing
    s_deviceMedia = lt_openlibrary(LTDeviceMedia);
    TILT_ASSERT_TRUE(pTRC, s_deviceMedia, "Failed to open LTDeviceMedia");

    // Get the motion frame size from the hardware
    LTMediaResolution BaseMotionResolution;
    TILT_ASSERT_TRUE(pTRC,
                     s_deviceMedia->GetProperty("resolution.motion", &BaseMotionResolution),
                     "Failed to get motion properties");
    VerticalMoxels = BaseMotionResolution.height;
    HorizontalMoxels = BaseMotionResolution.width;

    // Open the motion library for the duration of the test
    s_motion = lt_openlibrary(LTMediaMotionDetection);
    TILT_ASSERT_TRUE(pTRC, s_motion, "Failed to open LTMediaMotionDetection library");

    /* Allocate the base motion output buffer to pass to the motion library under test. */
    s_baseMotionOutput = lt_malloc(sizeof(LTMediaData));
    TILT_ASSERT_TRUE(pTRC, s_baseMotionOutput != NULL, "Error: Could not allocate memory for s_baseMotionOutput!");
    s_baseMotionOutput->nDataLen = VerticalMoxels * HorizontalMoxels;
    s_baseMotionOutput->pData = lt_malloc(s_baseMotionOutput->nDataLen);
    TILT_ASSERT_TRUE(pTRC, s_baseMotionOutput->pData != NULL, "Error: Could not allocate memory for s_baseMotionOutput->pData!");

    // Enable library hooking, so we can intercept LTDeviceMedia instance used by the motion library
    TILT_ASSERT_TRUE(pTRC, pTRC->HookLibrary("LTDeviceMedia", UnitTestHook_LtDeviceMedia), "Failed to enable library hook");

    // Fetch a reference to the motion library's algorithm object, which we use to directly modify algorithm parameters
    // during testing
    s_algorithm = s_motion->GetActiveAlgorithm();
    TILT_ASSERT_TRUE(pTRC, s_algorithm, "Failed to get LTMediaMotionDetection's algorithm object");

    s_motion->GetMotionStateParameters(&MinMotionFramesForEvent, &MinFramesUntilEventEnd, &MaxFramesUntilFirstSnapshot, &MaxFramesForBestSnapshot, &MinFramesMotionEventDuration);
    LTLOG_DEBUG("motion.parameters", "MinMotionFramesForEvent: %lu, MinFramesUntilEventEnd: %lu, MaxFramesUntilFirstSnapshot: %lu, MaxFramesForBestSnapshot: %lu, MinMotionFramesEventDuration: %lu", LT_Pu32(MinMotionFramesForEvent), LT_Pu32(MinFramesUntilEventEnd), LT_Pu32(MaxFramesUntilFirstSnapshot), LT_Pu32(MaxFramesForBestSnapshot), LT_Pu32(MinFramesMotionEventDuration));

    // To make some of the pooling logic simpler for the multi motion ROI tests, the difference threshold is set
    // artificially low, but the basic premise is still used.
    s_differenceThreshold = 1;
    s_algorithm->API->SetDifferenceThreshold(s_algorithm, s_differenceThreshold);

    // Record maximum snapshots per event to use in specific multi-snapshot tests
    MaxSnapshotsPerMotionEvent = s_motion->GetMotionEventMaxSnapshots();

    // Default to 1 snapshot unless an individual test requires more
    s_motion->SetMotionEventMaxSnapshots(1);

    // Disable motion event stats logging during testing to avoid messing up timing-sensitive test cases
    s_motion->SetMotionEventStatsLogging(false);

    // Disable moxel heatmap for unit test
    s_algorithm->API->EnableHeatmap(s_algorithm, false);

    // Disable BFS for most unit tests
    s_algorithm->API->EnableBFS(s_algorithm, false);

    /* Force camera into day mode to avoid interference from day night switching in cases where the test camera is in a
     * darkened environment */
    u32 nightVisionMode = 0;
    s_deviceMedia->SetProperty("nightvision.mode", &nightVisionMode);

    // Finally, start the MotionTestThread for event management
    s_testThread = s_core->CreateThread("MotionTestThread");
    s_IThread = lt_gethandleinterface(ILTThread, s_testThread);
    TILT_ASSERT_TRUE(pTRC, s_IThread && s_testThread, "Failed to create test thread");
    s_IThread->SetStackSize(s_testThread, 1024);
    s_IThread->Start(s_testThread, LTTest_ThreadInit, LTTest_ThreadExit);
}

static void UnitTestLTMediaMotionDetection_AfterAllTests(const TiltImplReportingCallbacks * pTRC) {
    LT_UNUSED(pTRC);    // do not use this - also called from LibFini
    if (s_baseMotionOutput) {
        lt_free(s_baseMotionOutput->pData);
        lt_free(s_baseMotionOutput);
        s_baseMotionOutput = NULL;
    }
    if (s_testThread) {
        s_IThread->Terminate(s_testThread);
        s_IThread->WaitUntilFinished(s_testThread, LTTime_Infinite());
        lt_destroyhandle(s_testThread);
        s_testThread = 0;
    }

    if (s_zoneTestThread) {
        s_IThread->Terminate(s_zoneTestThread);
        s_IThread->WaitUntilFinished(s_zoneTestThread, LTTime_Infinite());
        lt_destroyhandle(s_zoneTestThread);
        s_zoneTestThread = 0;
    }
    /* Restore camera to daynight auto night vision mode */
    u32 nightVisionMode = 2;
    s_deviceMedia->SetProperty("nightvision.mode", &nightVisionMode);

    lt_closelibrary(s_motion);
    lt_closelibrary(s_deviceMedia);
    s_motion = NULL;
    s_deviceMedia = NULL;
    s_IThread = NULL;
    s_core = NULL;

    TILT_EXPECT_FALSE(pTRC, s_sourcesLeftOpen, "Failure: Media sources were not closed properly in some tests!");
}

static void UnitTestLTMediaMotionDetection_AfterTest(const TiltImplReportingCallbacks * pTRC) {
    if (s_pLastMotionBounds) {
        lt_free(s_pLastMotionBounds);
        s_pLastMotionBounds = NULL;
    }
    TILT_EXPECT_TRUE(pTRC, !s_bTestFailedInHook, "Failing due to unexpected condition in mock library hooks");
    s_bTestFailedInHook = false;
    TILT_EXPECT_TRUE(pTRC, s_motionProcDelays <= s_maxMotionProcDelays, "Error: too many delayed motion events during %s!", pTRC->GetTestName());

    /* Check if the media sources have been closed, and clean up.
     * Allow tests to continue if the sources were not closed, but mark the full run as a failure. */
    if (s_hMotionMediaSource != 0) {
        TILT_WARNING(pTRC, "Motion library didn't close motion media source");
        s_sourcesLeftOpen = true;
    }
    if (s_hSnapshotMediaSource != 0) {
        TILT_WARNING(pTRC, "Motion library didn't close snapshot media source");
        s_sourcesLeftOpen = true;
    }
    if (s_motion->IsDetectionEngineRunning()) { s_motion->StopDetectionEngine(); }
    s_motion->ResetMotionState();
}

static void UnitTestLTMediaMotionDetection_BeforeTest(const TiltImplReportingCallbacks * pTRC) {
    LT_UNUSED(pTRC);
    s_motionProcDelays = 0;
}

static TiltImplTestHooks s_TestHooks = {
    .BeforeAllTests = UnitTestLTMediaMotionDetection_BeforeAllTests,
    .AfterAllTests  = UnitTestLTMediaMotionDetection_AfterAllTests,
    .BeforeTest     = UnitTestLTMediaMotionDetection_BeforeTest,
    .AfterTest      = UnitTestLTMediaMotionDetection_AfterTest,
};

static bool UnitTestLTMediaMotionDetectionImpl_LibInit(void) {
    s_core = LT_GetCore();

    // Create dummy events to pass to the library under test
    s_IEvent = lt_getlibraryinterface(ILTEvent, s_core);
    s_hMotionEvent   = s_core->CreateEvent(&s_MediaDataEventArgs, DispatchMotionDataEventCB, NULL, NULL, NULL);
    s_hSnapshotEvent = s_core->CreateEvent(&s_MediaDataEventArgs, DispatchJpegDataEventCB, NULL, NULL, NULL);

    /* Fetch constants from product config to set key library values. */
    LTProductConfig *productConfig = NULL;
    if (!(productConfig = lt_openlibrary(LTProductConfig))) {
        LTLOG_REDALERT("lib.init.open.productconfig.fail", "Could not open LTProductConfig while initialising LTMediaMotionDetection.");
        return false;
    }
    u32 libSection = productConfig->GetLibraryConfigSection("LTMediaMotionDetection");
    BitGridWidth = libSection ? productConfig->ReadInteger(libSection, "BitGridWidth") : 0;
    if (!BitGridWidth) {
        LTLOG_REDALERT("lib.init.open.get.bitgridwidth.fail", "Could not fetch motion zone BitGridWidth from LTProductConfig.");
        return false;
    }
    BitGridHeight = libSection ? productConfig->ReadInteger(libSection, "BitGridHeight") : 0;
    if (!BitGridHeight) {
        LTLOG_REDALERT("lib.init.open.get.bitgridheight.fail", "Could not fetch motion zone BitGridHeight from LTProductConfig.");
        return false;
    }
    DownsamplingScaleFactor = libSection ? productConfig->ReadInteger(libSection, "DownsamplingScaleFactor") : 1;
    if (!DownsamplingScaleFactor) {
        LTLOG_REDALERT("lib.init.open.get.downsamplingscalefactor.fail", "Could not fetch DownsamplingScaleFactor from LTProductConfig.");
        return false;
    }
    lt_closelibrary(productConfig);
    nBitmapBytes = (BitGridWidth * BitGridHeight + 7) / 8;
    bitGridSize = sizeof(LTMediaMotionDetection_MotionZoneBitGrid) + sizeof(u8) * nBitmapBytes;

    return InitializeTilt(s_tests, TEST_COUNT, &s_TestHooks);
}

static void UnitTestLTMediaMotionDetectionImpl_LibFini(void) {
    // Check for test hard abort (e.g. timeout) resulting in extra cleanup needed
    if (s_testThread || s_motion || s_deviceMedia) UnitTestLTMediaMotionDetection_AfterAllTests(NULL);
    lt_destroyhandle(s_hMotionEvent);
    lt_destroyhandle(s_hSnapshotEvent);
    lt_destroyobject(s_algorithm); s_algorithm = NULL;
    ShutdownTilt();
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTMediaMotionDetection, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTMediaMotionDetection) LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(UnitTestLTMediaMotionDetection, (ITilt))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  11-Jul-23   honorius    created
 *  06-Dec-23   julian      CheckFrameForMotionTest added
 *  10-Jan-24   julian      CheckFrameForMotionROITest and MotionEventDetectionTest added
 *  11-Apr-24   julian      CheckFrameForMultipleMotionTest and CheckFrameForMultipleMotionROITest added
 */
