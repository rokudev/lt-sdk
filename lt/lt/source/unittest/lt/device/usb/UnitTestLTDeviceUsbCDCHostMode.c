/*******************************************************************************
 * LTDeviceUsbCDCHostMode Unit Test
 *
 * This is a test for USB CDC with LT operating in USB host mode.
 *
 * This test is a copy-paste from UnitTestLTDeviceUsbCDC and quickly made in order
 * to test host-mode CDC functionality. It is very much a hacky work-in-progress.
 * It can be considered for removal later or merged with the original
 * UnitTestLTDeviceUsbCDC.
 *
 * This test assumes an Alta unit that has a USB LTConsole enabled is connected
 * to the host running the test.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/usb/host/LTDeviceUsbHost.h>
#include <lt/device/usb/LTDeviceUsbCDC.h>
#include <lt/core/LTCore.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <tilt/JiltEngine.h>

/*******************************************************************************
 * Static Variables
 ******************************************************************************/
static struct Statics {
    LTUtilityByteOps    *byteOps;
    Tilt                *tilt;
    const char          *currentTest;

    LTDeviceUsbHost               *usbHost;
    LTDeviceUsbCDC                *usbCDC;
    u32                            fifoSize;

    u8                  *testData;
    u32                  testDataSize;
    u32                  testDataWritten;
    u8                  *testBuffer;
    u32                  testBufferRead;

    u32                  maxWriteSize;
    u32                  maxReadSize;
} S;

static JiltEngine *s_engine;
static u8          s_tmpReadBuf[1024];

/*******************************************************************************
 * Callback Functions
 ******************************************************************************/

static void Cleanup(void) {
    return;
    if (S.testData) {
        TILT_INFO(S.tilt, "cleaning up CDC device");
        S.usbCDC->API->Stop(S.usbCDC);

        lt_free(S.testData);
        S.testData = NULL;
    }
    if (S.testBuffer) {
        lt_free(S.testBuffer);
        S.testBuffer = NULL;
    }
}

static void Finish(void) {
    Cleanup();
    s_engine->API->SignalTestCompletion(s_engine, S.currentTest);
}

static void TimeoutCleanup(Tilt *tilt, void *clientData) {
    LT_UNUSED(tilt);
    LT_UNUSED(clientData);
    Cleanup();
}

static void WriteHandler(void *clientData) {
    LT_UNUSED(clientData);

    // Write ready indicates CDC up and ready to be used in individual tests
    if (S.testData == NULL) s_engine->API->SignalTestCompletion(s_engine, NULL);

    // Write whatever test data is set, ONCE
    if (S.testDataWritten == 0) {
        S.testDataWritten = S.usbCDC->API->Write(S.usbCDC, S.testData, S.testDataSize);
    }
}

static void ReadHandler(void *clientData) {
    LT_UNUSED(clientData);
    // Print everything out as soon as its available
    s32 bytesRead = S.usbCDC->API->Read(S.usbCDC, s_tmpReadBuf, 1024);
    if (bytesRead >= 0) {
        s_tmpReadBuf[bytesRead] = '\0';
        TILT_INFO(S.tilt, "----- %lu BYTES READ ----", LT_Pu32(bytesRead));
        TILT_INFO(S.tilt, (char *)s_tmpReadBuf);
    } else {
        TILT_INFO(S.tilt, "failed to read data");
    }
}

static void ErrorHandler(void *clientData) {
    LT_UNUSED(clientData);
    if (!S.testData) {
        // stray callback after cleanup
        return;
    }
    TILT_REPORT_FAILURE(S.tilt, "USB error");
    Finish();
}

static void EndCurrentTestProc(void *clientData) {
    LT_UNUSED(clientData);
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    thread->API->KillTimer(thread, EndCurrentTestProc, NULL);
    s_engine->API->SignalTestCompletion(s_engine, S.currentTest);
}

/*******************************************************************************
 * Test Functions
 ******************************************************************************/

static void TestPs(Tilt *tilt) {
    S.currentTest = "TestPs";
    S.testData = (u8*)lt_strdup("ps\n");
    S.testDataSize = lt_strlen((char*)S.testData);

    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(60), TimeoutCleanup, (void*)tilt);

    WriteHandler(NULL);

    // Set test to end automatically after 10 seconds (assumes success)
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    thread->API->SetTimer(thread, LTTime_Seconds(10), EndCurrentTestProc, NULL, NULL);
}

static void TestOtherWrite(Tilt *tilt) {
    S.currentTest  = "TestOtherWrite";
    S.testData     = (u8 *)lt_strdup("hellohellohellohellohellohellohellohellohellohello\n");
    S.testDataSize = lt_strlen((char*)S.testData);

    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(60), TimeoutCleanup, (void*)tilt);

    WriteHandler(NULL);

    // Set test to end automatically after 10 seconds (assumes success)
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    thread->API->SetTimer(thread, LTTime_Seconds(10), EndCurrentTestProc, NULL, NULL);

}

static void BeforeAllTests(Tilt *tilt) {
    S = (struct Statics) {};
    S.tilt = tilt;
    S.byteOps = lt_openlibrary(LTUtilityByteOps);
    TILT_EXPECT_TRUE(tilt,S.byteOps != NULL, "Cannot open LTUtilityByteOps");

    S.usbCDC = lt_createobject_typed(LTDeviceUsbCDC, LTDeviceUsbCDCHostMode);

    TILT_ASSERT_TRUE(tilt, S.usbCDC != NULL, "Cannot create CDC device");

    TILT_ASSERT_TRUE(tilt, S.usbCDC->API->Init(S.usbCDC, "LTMailbox", LT_GetCore()->GetCurrentThreadObject(), ReadHandler,
                                                  WriteHandler, ErrorHandler, NULL),
                                                  "Cannot init CDC device");

    // Start USB CDC before all tests
    S.usbCDC->API->Start(S.usbCDC);

    // Defer until CDC is up
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(60), TimeoutCleanup, (void*)tilt);
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);

    if (S.usbCDC) {
        lt_destroyobject(S.usbCDC);
        S.usbCDC = NULL;
    }

    if (S.byteOps) {
        lt_closelibrary(S.byteOps);
    }
}

static void BeforeTest(Tilt *tilt) {
    LT_UNUSED(tilt);
    S.testDataSize = 0;
    S.testDataWritten = 0;
    S.testBufferRead = 0;

    S.maxWriteSize = 0;
    S.maxReadSize = 0;
}

static void AfterTest(Tilt *tilt) {
    LT_UNUSED(tilt);
    Cleanup();
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
    .BeforeTest     = BeforeTest,
    .AfterTest      = AfterTest
};

static const TiltEngineTest s_tests[] = {
    {TestPs,         "TestPs",         "Write ps to LTConsole and read back result",   0},
    {TestOtherWrite, "TestOtherWrite", "Write data to LTConsole and read back result", 0},
};

static int UnitTestLTDeviceUsbCDCHostModeImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceUsbCDCHostModeImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceUsbCDCHostModeImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceUsbCDCHostMode, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceUsbCDCHostMode, UnitTestLTDeviceUsbCDCHostModeImpl_Run, 1536) LTLIBRARY_DEFINITION;
