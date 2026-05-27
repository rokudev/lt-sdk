/*******************************************************************************
 * LTDeviceQrreader Unit Test
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTThread.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/device/qrreader/LTDeviceQrreader.h>
#include <tilt/TiltImpl.c>

#define P(...) LTLOG_DEBUG("", __VA_ARGS__)
#define PLOG(...) LTLOG("", __VA_ARGS__)

#define STOPONDECODE   (true)

static LTCore                *s_core = NULL;
static ILTThread           *s_thread = NULL;
static LTDeviceQrreader  *s_qrreader = NULL;
static LTThread        s_hTestThread = 0;
static char s_qrcode[256] = {};
static bool s_testDone = false;

static void OnQrreaderEvent(LTDeviceQrreaderEvent event, void *data) {
    P("%s\n", __FUNCTION__);
    LT_UNUSED(data);
    P("=== qr event 0x%04x ===", event);
    if (event == kLTDeviceQrreaderEvent_CodeReady) {
        s_testDone = true;
        u16 len = 256;
        s_qrreader->GetCode(s_qrcode, &len);
        PLOG("qr code received (%u): %s", len, s_qrcode);
        s_thread->Terminate(s_hTestThread);

    } else if (event == kLTDeviceQrreaderEvent_Fail) {
        PLOG("qr decode fail\n");
        s_testDone = false;
        s_thread->Terminate(s_hTestThread);

    }
}

static bool OnThreadStart(void) {
    s_qrreader->OnStatusChange(OnQrreaderEvent, NULL, NULL);
    s_qrreader->Start(STOPONDECODE);
    return true;
}

static void StartTest(void) {
    s_hTestThread = s_core->CreateThread("qrtest");
    s_thread->SetStackSize(s_hTestThread, 1024);
    s_thread->Start(s_hTestThread, OnThreadStart, NULL);
    s_thread->WaitUntilFinished(s_hTestThread, LTTime_Infinite());
    lt_destroyhandle(s_hTestThread);
    s_hTestThread = 0;
    s_qrreader->Stop();
}

static void TestQrreader(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "qr reader start");
    StartTest();
    TILT_EXPECT_TRUE(trc, s_testDone, "fail to qr reader");
    TILT_MESSAGE(trc, "qr reader done");
}

static const TiltImplTestSpecifier s_tests[] = {
    {TestQrreader ,      "test",     "Test Qrreader",      0},
};

static void ShutdownQrreader(void) {
    lt_closelibrary(s_qrreader);
    s_qrreader = NULL;
    s_thread = NULL;
    s_core = NULL;
    ShutdownTilt();
}

static bool ExampleDeviceQrreaderImpl_LibInit(void) {
    P("%s\n", __FUNCTION__);
    s_core   = LT_GetCore();
    s_thread = lt_getlibraryinterface(ILTThread, s_core);

    do {
        s_qrreader = lt_openlibrary(LTDeviceQrreader);
        if (!s_qrreader) break;

        return InitializeTilt(s_tests, sizeof(s_tests) / sizeof(s_tests[0]), NULL);
    } while (0);

    LTLOG_YELLOWALERT("error", "fail to init");
    ShutdownQrreader();
    return false;
}

static void ExampleDeviceQrreaderImpl_LibFini(void) {
    P("%s\n", __FUNCTION__);
    ShutdownQrreader();
}

static int ExampleDeviceQrreader_Run(int argc, const char ** argv) {
    P("%s\n", __FUNCTION__);
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    StartTest();
    return 0;
}

typedef_LTLIBRARY_ROOT_INTERFACE(ExampleDeviceQrreader, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(ExampleDeviceQrreader, ExampleDeviceQrreader_Run, 1024) LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(ExampleDeviceQrreader, (ITilt));

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  10-Jun-20   gallienus   created */
