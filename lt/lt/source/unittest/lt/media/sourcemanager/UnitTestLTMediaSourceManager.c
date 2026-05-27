/*******************************************************************************
 * source/unittest/lt/media/sourcemanager/UnitTestLTMediaSourceManager.c
 *    __  __      _ __ ______          __
 *   / / / /___  (_) //_  __/__  _____/ /_
 *  / / / / __ \/ / __// / / _ \/ ___/ __/
 * / /_/ / / / / / /_ / / /  __(__  ) /_/
 * \____/_/ /_/_/\__//_/  \___/____/\__/
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include <lt/core/LTCore.h>

#include <lt/device/media/LTDeviceMedia.h>
#include <lt/media/sourcemanager/LTMediaSourceManager.h>

#include <tilt/JiltEngine.h>

enum {
    kTestBlockSize = 256,
    kMaxSources    = 16,
};

typedef struct {
    LTMediaSourceManager               *srcMgr;
    LTThread                            tiltThread;
    LTMediaSource                       hSource1;
    u32                                 source1Dispatched;
    u32                                 source1Received;
    LTMediaSource                       hSource2;
    u32                                 source2Dispatched;
    u32                                 source2Received;
    bool                                done;
} DispatchTestContext;

/*******************************************************************************
 * Static Variables
 ******************************************************************************/
static struct {
    LTCore               *pCore;
    ILTThread            *iThread;
    LTMediaSourceManager *srcMgr;
    LTThread              testThread;
    Tilt                 *tilt;
} S;

static JiltEngine *s_engine;

/*******************************************************************************
 * Helper to wait for source manager cleanup
 ******************************************************************************/
static void WaitForSourceCleanup(LTMediaSourceManager *srcMgr) {
    // Simply poll activeSourcesCount in a loop instead of using a timer
    // This avoids race conditions with the timer firing after srcMgr is destroyed
    u32 maxPolls = 110; // 1100ms worth at 10ms intervals
    for (u32 i = 0; i < maxPolls; i++) {
        u32 activeCount = srcMgr->API->GetActiveSourceCount(srcMgr);
        if (activeCount == 0) {
            break;
        }
        S.iThread->Sleep(LTTime_Milliseconds(10));
    }
}

/*******************************************************************************
 * Test Functions
 ******************************************************************************/
static void TestCreateDestroy(Tilt *tilt) {
    S.srcMgr = lt_createobject(LTMediaSourceManager);
    TILT_ASSERT_TRUE(tilt, S.srcMgr != NULL, "Cannot create source manager");
    lt_destroyobject(S.srcMgr);
    S.srcMgr = NULL;
}

static void TestCreateSource(Tilt *tilt) {
    bool res;
    LTMediaSource hSource1;
    LTMediaSource hSource2;
    ILTMediaSource *iSource;
    u32 activeSources;
    u32 mediaEncoding;

    S.srcMgr = lt_createobject(LTMediaSourceManager);
    TILT_ASSERT_TRUE(tilt, S.srcMgr != NULL, "Cannot create source manager");

    hSource1 = S.srcMgr->API->CreateSource(S.srcMgr, 0, 1);
    TILT_ASSERT_TRUE(tilt, hSource1 != LTHANDLE_INVALID, "Cannot create source 1");

    hSource2 = S.srcMgr->API->CreateSource(S.srcMgr, 2, 3);
    TILT_ASSERT_TRUE(tilt, hSource2 != LTHANDLE_INVALID, "Cannot create source 2");

    iSource = lt_gethandleinterface(ILTMediaSource, hSource1);
    TILT_ASSERT_TRUE(tilt, iSource != NULL, "Cannot get interface for source 1");

    iSource = lt_gethandleinterface(ILTMediaSource, hSource2);
    TILT_ASSERT_TRUE(tilt, iSource != NULL, "Cannot get interface for source 2");

    activeSources = S.srcMgr->API->GetActiveSourceCount(S.srcMgr);
    TILT_EXPECT_TRUE(tilt, activeSources == 0, "Expected 0 active sources, got %d", activeSources);

    res = S.srcMgr->API->FormatRequested(S.srcMgr, 1);
    TILT_EXPECT_TRUE(tilt, res == false, "Expected format 1 not requested");

    res = S.srcMgr->API->FormatRequested(S.srcMgr, 3);
    TILT_EXPECT_TRUE(tilt, res == false, "Expected format 3 not requested");

    // Start first source
    iSource = lt_gethandleinterface(ILTMediaSource, hSource1);

    mediaEncoding = iSource->GetMediaEncoding(hSource1);
    TILT_EXPECT_TRUE(tilt, mediaEncoding == 0, "Expected media encoding 0, got %d", mediaEncoding);

    iSource->Start(hSource1);
    activeSources = S.srcMgr->API->GetActiveSourceCount(S.srcMgr);
    TILT_EXPECT_TRUE(tilt, activeSources == 1, "Expected 1 active source, got %d", activeSources);

    mediaEncoding = iSource->GetMediaEncoding(hSource1);
    TILT_EXPECT_TRUE(tilt, mediaEncoding == 0, "Expected media encoding 0, got %d", mediaEncoding);

    res = S.srcMgr->API->FormatRequested(S.srcMgr, 1);
    TILT_EXPECT_TRUE(tilt, res == true, "Expected format 1 requested");

    res = S.srcMgr->API->FormatRequested(S.srcMgr, 3);
    TILT_EXPECT_TRUE(tilt, res == false, "Expected format 3 not requested");

    // Start second source
    iSource = lt_gethandleinterface(ILTMediaSource, hSource2);

    mediaEncoding = iSource->GetMediaEncoding(hSource2);
    TILT_EXPECT_TRUE(tilt, mediaEncoding == 2, "Expected media encoding 2, got %d", mediaEncoding);

    iSource->Start(hSource2);
    activeSources = S.srcMgr->API->GetActiveSourceCount(S.srcMgr);
    TILT_EXPECT_TRUE(tilt, activeSources == 2, "Expected 2 active sources, got %d", activeSources);

    mediaEncoding = iSource->GetMediaEncoding(hSource2);
    TILT_EXPECT_TRUE(tilt, mediaEncoding == 2, "Expected media encoding 2, got %d", mediaEncoding);

    res = S.srcMgr->API->FormatRequested(S.srcMgr, 1);
    TILT_EXPECT_TRUE(tilt, res == true, "Expected format 1 requested");

    res = S.srcMgr->API->FormatRequested(S.srcMgr, 3);
    TILT_EXPECT_TRUE(tilt, res == true, "Expected format 3 requested");

    // Stop first source
    iSource = lt_gethandleinterface(ILTMediaSource, hSource1);
    iSource->Stop(hSource1);
    activeSources = S.srcMgr->API->GetActiveSourceCount(S.srcMgr);
    TILT_EXPECT_TRUE(tilt, activeSources == 1, "Expected 1 active source, got %d", activeSources);

    res = S.srcMgr->API->FormatRequested(S.srcMgr, 1);
    TILT_EXPECT_TRUE(tilt, res == false, "Expected format 1 not requested");

    res = S.srcMgr->API->FormatRequested(S.srcMgr, 3);
    TILT_EXPECT_TRUE(tilt, res == true, "Expected format 3 requested");

    // Stop second source
    iSource = lt_gethandleinterface(ILTMediaSource, hSource2);
    iSource->Stop(hSource2);
    activeSources = S.srcMgr->API->GetActiveSourceCount(S.srcMgr);
    TILT_EXPECT_TRUE(tilt, activeSources == 0, "Expected 0 active sources, got %d", activeSources);

    res = S.srcMgr->API->FormatRequested(S.srcMgr, 1);
    TILT_EXPECT_TRUE(tilt, res == false, "Expected format 1 not requested");

    res = S.srcMgr->API->FormatRequested(S.srcMgr, 3);
    TILT_EXPECT_TRUE(tilt, res == false, "Expected format 3 not requested");

    // Clean up
    lt_destroyhandle(hSource1);
    lt_destroyhandle(hSource2);
    lt_destroyobject(S.srcMgr);
    S.srcMgr = NULL;
}

static void TestCreateMax(Tilt *tilt) {
    LTMediaSource hSources[kMaxSources + 1];

    S.srcMgr = lt_createobject(LTMediaSourceManager);
    TILT_ASSERT_TRUE(tilt, S.srcMgr != NULL, "Cannot create source manager");

    for (int i = 0; i < kMaxSources; i++) {
        hSources[i] = S.srcMgr->API->CreateSource(S.srcMgr, 0, i);
        TILT_ASSERT_TRUE(tilt, hSources[i] != LTHANDLE_INVALID, "Cannot create source %d", i);
    }

    hSources[kMaxSources] = S.srcMgr->API->CreateSource(S.srcMgr, 0, kMaxSources);
    TILT_EXPECT_TRUE(tilt, hSources[kMaxSources] == LTHANDLE_INVALID, "Expected invalid source handle");

    for (int i = 0; i < kMaxSources; i++) {
        lt_destroyhandle(hSources[i]);
    }

    // Again
    for (int i = 0; i < kMaxSources; i++) {
        hSources[i] = S.srcMgr->API->CreateSource(S.srcMgr, 0, i);
        TILT_ASSERT_TRUE(tilt, hSources[i] != LTHANDLE_INVALID, "Cannot create source %d", i);
    }

    hSources[kMaxSources] = S.srcMgr->API->CreateSource(S.srcMgr, 0, kMaxSources);
    TILT_EXPECT_TRUE(tilt, hSources[kMaxSources] == LTHANDLE_INVALID, "Expected invalid source handle");

    for (int i = 0; i < kMaxSources; i++) {
        lt_destroyhandle(hSources[i]);
    }

    // Wait for all sources to be fully destroyed before cleaning up the source manager
    WaitForSourceCleanup(S.srcMgr);

    // Clean up
    lt_destroyobject(S.srcMgr);
    S.srcMgr = NULL;
}

static void TestDispatchProducerThread_TestDone(void *pClientData) {
    DispatchTestContext *pCtx = (DispatchTestContext *)pClientData;
    Tilt *tilt = S.tilt;
    S.iThread->KillTimer(S.iThread->GetCurrentThread(), TestDispatchProducerThread_TestDone, pClientData);

    if (pCtx->source1Received != pCtx->source1Dispatched) {
        TILT_REPORT_FAILURE(tilt, "Source 1 received %d, expected %d", pCtx->source1Received, pCtx->source1Dispatched);
    }

    if (pCtx->source2Received != pCtx->source2Dispatched) {
        TILT_REPORT_FAILURE(tilt, "Source 2 received %d, expected %d", pCtx->source2Received, pCtx->source2Dispatched);
    }

    pCtx->srcMgr->API->SetCaptureRequestCallback(pCtx->srcMgr, NULL, NULL);
    lt_destroyobject(pCtx->srcMgr);
    s_engine->API->SignalTestCompletion(s_engine, tilt->API->GetTestName());
    lt_free(pCtx);
}

static void TestDispatchConsumerThread_HandleData(LTMediaSource hSource, LTMediaData *pData, DispatchTestContext *pCtx) {
    u8 formatId;

    if (!pData) {
        TILT_REPORT_FAILURE(S.tilt, "No data");
        return;
    }

    if (hSource == pCtx->hSource1) {
        formatId = 1;
    } else if (hSource == pCtx->hSource2) {
        formatId = 3;
    } else {
        TILT_REPORT_FAILURE(S.tilt, "Invalid source handle");
        return;
    }

    // Validate data
    for (u32 i = 0; i < pData->nDataLen; i += 2) {
        if (pData->pData[i] != formatId || pData->pData[i + 1] != pData->nDataLen / kTestBlockSize) {
            TILT_REPORT_FAILURE(S.tilt, "Invalid data at index %d = %d %d, expected %d %d",
                i, pData->pData[i], pData->pData[i + 1],
                formatId, pData->nDataLen / kTestBlockSize);
            break;
        }
    }

    if (hSource == pCtx->hSource1) {
        pCtx->source1Received += pData->nDataLen;
        if (pCtx->source1Received >= kTestBlockSize * 6) {
            // Done. Stop source 1
            ILTMediaSource *iSource = lt_gethandleinterface(ILTMediaSource, hSource);
            iSource->Stop(pCtx->hSource1);
            lt_destroyhandle(pCtx->hSource1);
            pCtx->hSource1 = LTHANDLE_INVALID;
        }
    } else {
        pCtx->source2Received += pData->nDataLen;
        if (pCtx->source2Received >= kTestBlockSize * 6) {
            // Done. Stop source 2
            ILTMediaSource *iSource = lt_gethandleinterface(ILTMediaSource, hSource);
            iSource->Stop(pCtx->hSource2);
            lt_destroyhandle(pCtx->hSource2);
            pCtx->hSource2 = LTHANDLE_INVALID;
        }
    }
    if (pCtx->hSource1 == LTHANDLE_INVALID && pCtx->hSource2 == LTHANDLE_INVALID) {
        // Done
        pCtx->done = true;
        // Allow some time for the source instance to be destroyed, which is gated on this event completing.
        S.iThread->SetTimer(pCtx->tiltThread, LTTime_Milliseconds(500), TestDispatchProducerThread_TestDone, NULL, pCtx);
    }
}

static void TestDispatchConsumerThread_Source1MediaEventHandler(LTMediaEvent event, void *eventData, void *pClientData) {
    DispatchTestContext *pCtx = (DispatchTestContext *)pClientData;
    if (!pCtx) return;
    if (event != kLTMediaEvent_Data) {
        TILT_REPORT_FAILURE(S.tilt, "Invalid event");
        return;
    }
    TestDispatchConsumerThread_HandleData(pCtx->hSource1, (LTMediaData *)eventData, (DispatchTestContext *)pClientData);
}

static void TestDispatchConsumerThread_Source2MediaEventHandler(LTMediaEvent event, void *eventData, void *pClientData) {
    DispatchTestContext *pCtx = (DispatchTestContext *)pClientData;
    if (!pCtx) return;
    if (event != kLTMediaEvent_Data) {
        TILT_REPORT_FAILURE(S.tilt, "Invalid event");
        return;
    }
    TestDispatchConsumerThread_HandleData(pCtx->hSource2, (LTMediaData *)eventData, (DispatchTestContext *)pClientData);
}

static void TestDispatchConsumerThread_StartTest(void *pClientData) {
    DispatchTestContext *pCtx = (DispatchTestContext *)pClientData;
    pCtx->hSource1 = pCtx->srcMgr->API->CreateSource(pCtx->srcMgr, 0, 1);
    pCtx->hSource2 = pCtx->srcMgr->API->CreateSource(pCtx->srcMgr, 2, 3);

    if (pCtx->hSource1 == LTHANDLE_INVALID || pCtx->hSource2 == LTHANDLE_INVALID) {
        Tilt *tilt = S.tilt;
        TILT_REPORT_FAILURE(tilt, "Cannot create sources");
        lt_destroyhandle(pCtx->hSource1);
        lt_destroyhandle(pCtx->hSource2);
        lt_destroyobject(pCtx->srcMgr);

        lt_free(pCtx);
        s_engine->API->SignalTestCompletion(s_engine, tilt->API->GetTestName());
        return;
    }

    ILTMediaSource *iSource1 = lt_gethandleinterface(ILTMediaSource, pCtx->hSource1);
    ILTMediaSource *iSource2 = lt_gethandleinterface(ILTMediaSource, pCtx->hSource2);

    // Register for media events
    iSource1->OnMediaEvent(pCtx->hSource1, TestDispatchConsumerThread_Source1MediaEventHandler, pCtx);
    iSource2->OnMediaEvent(pCtx->hSource2, TestDispatchConsumerThread_Source2MediaEventHandler, pCtx);
    iSource1->Start(pCtx->hSource1);
    iSource2->Start(pCtx->hSource2);
}

static void TestDispatchProducerThread_CaptureRequestProc(void *pClientData) {
    DispatchTestContext *pCtx = (DispatchTestContext *)pClientData;
    LTMediaData data;
    u8 blockCount = 0;

    if (pCtx->done) {
        // Too many callbacks
        TILT_REPORT_FAILURE(S.tilt, "Too many callbacks");
        return;
    }

    if (pCtx->srcMgr->API->GetActiveSourceCount(pCtx->srcMgr) == 0) {
        // Done. Stop source 1
        TILT_REPORT_FAILURE(S.tilt, "No sources active");
        return;
    }

    if (pCtx->source1Dispatched >= kTestBlockSize * 6 && pCtx->source2Dispatched >= kTestBlockSize * 6) {
        // Dispatch no more
        return;
    }

    // Test variable data length
    if (pCtx->source1Dispatched < (kTestBlockSize * 2)) {
        data.nDataLen = kTestBlockSize;
        blockCount = 1;
    } else {
        data.nDataLen = kTestBlockSize * 2;
        blockCount = 2;
    }

    data.pData = lt_malloc(data.nDataLen);
    //Create test data for format 1
    for (u32 i = 0; i < data.nDataLen; ) {
        data.pData[i++] = 1;
        data.pData[i++] = blockCount;
    }
    pCtx->source1Dispatched += data.nDataLen;
    pCtx->srcMgr->API->DispatchMediaData(pCtx->srcMgr, 1, &data);

    //Create test data for format 3
    for (u32 i = 0; i < data.nDataLen;) {
        data.pData[i++] = 3;
        data.pData[i++] = blockCount;
    }
    pCtx->source2Dispatched += data.nDataLen;
    pCtx->srcMgr->API->DispatchMediaData(pCtx->srcMgr, 3, &data);
    lt_free(data.pData);
    data.pData = NULL;
}

static void TestDispatchProducerThread_CaptureRequestCallback(void *pClientData) {
    // schedule the next capture request on the producer thread
    DispatchTestContext *pCtx = (DispatchTestContext *)pClientData;
    S.iThread->QueueTaskProcIfRequired(pCtx->tiltThread, TestDispatchProducerThread_CaptureRequestProc, NULL, pClientData);
}

static void TestDispatch(Tilt *tilt) {
    DispatchTestContext *pCtx = lt_malloc(sizeof(DispatchTestContext));
    pCtx->srcMgr = lt_createobject(LTMediaSourceManager);
    if (pCtx->srcMgr == NULL) {
        TILT_REPORT_FAILURE(tilt, false, "Cannot create source manager");
        lt_free(pCtx);
        return;
    }

    pCtx->hSource1 = LTHANDLE_INVALID;
    pCtx->hSource2 = LTHANDLE_INVALID;
    pCtx->tiltThread = S.iThread->GetCurrentThread();
    pCtx->source1Dispatched = 0;
    pCtx->source1Received = 0;
    pCtx->source2Dispatched = 0;
    pCtx->source2Received = 0;
    pCtx->done = false;
    pCtx->srcMgr->API->SetCaptureRequestCallback(pCtx->srcMgr, TestDispatchProducerThread_CaptureRequestCallback, pCtx);

    // Start consumer part of test
    S.iThread->QueueTaskProc(S.testThread, TestDispatchConsumerThread_StartTest, NULL, pCtx);

    // Wait for test to complete
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Milliseconds(2000), NULL, NULL);
}

static void BeforeAllTests(Tilt *tilt) {
    S.tilt  = tilt;
    S.pCore = LT_GetCore();
    S.iThread = lt_getlibraryinterface(ILTThread, S.pCore);

    S.testThread = S.pCore->CreateThread("TestSrcMgr");
    TILT_EXPECT_TRUE(tilt, S.testThread != 0, "Cannot create thread");
    if (S.testThread == 0) return;

    S.iThread->SetStackSize(S.testThread, 1024);
    S.iThread->Start(S.testThread, NULL, NULL);
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    if (S.testThread) {
        S.iThread->Terminate(S.testThread);
        S.iThread->WaitUntilFinished(S.testThread, LTTime_Infinite());
        lt_destroyhandle(S.testThread);
    }
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests
};

static const TiltEngineTest s_tests[] = {
    { TestCreateDestroy,    "TestCreateDestroy",    "Create destroy test",                   0 },
    { TestCreateSource,     "TestCreateSource",     "Test source creation and destruction",  0 },
    { TestCreateMax,        "TestCreateMax",        "Test max source creation",              0 },
    { TestDispatch,         "TestDispatch",         "Test normal operation",                 0 },
};

static int UnitTestLTMediaSourceManagerImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTMediaSourceManagerImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTMediaSourceManagerImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTMediaSourceManager, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTMediaSourceManager, UnitTestLTMediaSourceManagerImpl_Run, 1536) LTLIBRARY_DEFINITION;
