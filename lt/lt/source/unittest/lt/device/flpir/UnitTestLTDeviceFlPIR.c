/*******************************************************************************
 * lt/source/unittest/lt/device/flpir/UnitTestLTDeviceFlPIR.c
 *
 * Floodlight PIR sensors Unit Test
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/device/flpir/LTDeviceFlPIR.h>
#include <tilt/JiltEngine.h>

/*******************************************************************************
 * static variables
 *******************************************************************************/
static JiltEngine     *s_engine;
static Tilt           *s_tilt;
static LTDeviceFlPIR  *s_DeviceFlPir;
static LTThread        s_hTestThread;
static LTCore         *s_pCore;
static ILTThread      *s_iThread;
static LTMutex        *s_mutex;
static u8              s_eventCount = 0;

static void OnTestEventProc(LTDriverFlPIREvent event, void *pEventData, void *pClientData) {
    LT_UNUSED(pClientData);
    LT_UNUSED(pEventData);
    LT_UNUSED(event);
    TILT_INFO(s_tilt, "on Motion Event");
    if (s_eventCount++ > 5) {
        s_engine->API->SignalTestCompletion(s_engine, s_tilt->API->GetTestName());
    }
}

static void DeferredCompletionCleanupHandler(Tilt *tilt, void *pClientData) {
    LT_UNUSED(tilt);
    LT_UNUSED(pClientData);
    s_iThread->KillTimer(s_hTestThread, NULL, NULL);
}

static void TestPIRSigComplete(void *pClientData) {
    LT_UNUSED(pClientData);
    s_engine->API->SignalTestCompletion(s_engine, s_tilt->API->GetTestName());
}

static void TestPIRAlgo(Tilt *tilt) {
    LTDeviceFlPIRAlgo getAlgo;
    LTDeviceFlPIRAlgo setAlgo = kLTDeviceFlPIRAlgo_Off;
    s_mutex->API->Lock(s_mutex);
    if (!s_DeviceFlPir->API->SetPIRAlgo(s_DeviceFlPir, setAlgo)) {
        TILT_REPORT_FAILURE(tilt, "Failed to SetPIRAlgo");
    }
    if (!s_DeviceFlPir->API->GetPIRAlgo(s_DeviceFlPir, &getAlgo)) {
        TILT_REPORT_FAILURE(tilt, "Failed to GetPIRAlgo");
    }
    s_mutex->API->Unlock(s_mutex);
    TILT_ASSERT_TRUE(tilt, getAlgo == setAlgo, "get PIR algo failed:%x", getAlgo);
}

static void TestPIRZone(Tilt *tilt) {
    LTDeviceFlPIRZone getZone = 0;
    LTDeviceFlPIRZone setZone = kLTDeviceFlPIRZone_Left | kLTDeviceFlPIRZone_Middle | kLTDeviceFlPIRZone_Right;
    s_mutex->API->Lock(s_mutex);
    if (!s_DeviceFlPir->API->SetPIRZone(s_DeviceFlPir, setZone)) {
        TILT_REPORT_FAILURE(tilt, "Failed to SetPIRZone");
    }
    if (!s_DeviceFlPir->API->GetPIRZone(s_DeviceFlPir, &getZone)) {
        TILT_REPORT_FAILURE(tilt, "Failed to GetPIRZone");
    }
    s_mutex->API->Unlock(s_mutex);
    TILT_ASSERT_TRUE(tilt, getZone == setZone, "get PIR Zone failed:%x", getZone);
}

static void TestPIRSensitivity(Tilt *tilt) {
    u8 getSensitivity = 0;
    u8 setSensitivity = 80;
    s_mutex->API->Lock(s_mutex);
    if (!s_DeviceFlPir->API->SetPIRSensitivity(s_DeviceFlPir, setSensitivity)) {
        TILT_REPORT_FAILURE(tilt, "Failed to SetPIRSensitivity");
    }
    if (!s_DeviceFlPir->API->GetPIRSensitivity(s_DeviceFlPir, &getSensitivity)) {
        TILT_REPORT_FAILURE(tilt, "Failed to GetPIRSensitivity");
    }
    s_mutex->API->Unlock(s_mutex);
    TILT_ASSERT_TRUE(tilt, getSensitivity == setSensitivity, "get PIR senitivity failed:%x", getSensitivity);
}

static void TestPIREvent(Tilt *tilt) {
    LT_UNUSED(tilt);
    s_iThread->SetTimer(s_hTestThread, LTTime_Milliseconds(3000), TestPIRSigComplete, NULL, NULL);
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Milliseconds(20000), DeferredCompletionCleanupHandler, (void *)1);
}

static bool FlThreadInit(void) {
    s_mutex->API->Lock(s_mutex);
    s_DeviceFlPir = lt_createobject(LTDeviceFlPIR);
    TILT_EXPECT_TRUE(s_tilt, s_DeviceFlPir != NULL, "Failed to create LTDeviceFlPIR");
    if (s_DeviceFlPir == NULL) {
        s_mutex->API->Unlock(s_mutex);
        return false;
    }
    s_DeviceFlPir->API->OnMotionEvent(s_DeviceFlPir, OnTestEventProc, NULL);
    s_mutex->API->Unlock(s_mutex);
    return true;
}

static void FlThreadDeinit(void) {
    s_mutex->API->Lock(s_mutex);
    if (s_DeviceFlPir) {
        if (!s_DeviceFlPir->API->NoMotionEvent(s_DeviceFlPir, OnTestEventProc)) {
            TILT_REPORT_FAILURE(s_tilt, "NoMotionEvent() failed");
        }
        lt_destroyobject(s_DeviceFlPir);
        s_DeviceFlPir = NULL;
    }
    s_mutex->API->Unlock(s_mutex);
}

static void BeforeAllTests(Tilt *tilt) {
    s_tilt    = tilt;
    s_pCore   = LT_GetCore();
    s_iThread = lt_getlibraryinterface(ILTThread, s_pCore);
    s_mutex   = lt_createobject(LTMutex);
    TILT_EXPECT_TRUE(tilt, s_mutex != NULL, "Cannot create mutex");
    if (!s_mutex) return;

    s_hTestThread = LT_GetCore()->CreateThread("EventDetection");
    TILT_EXPECT_TRUE(tilt, s_hTestThread != 0, "Cannot create thread");
    if (s_hTestThread == 0) return;

    s_iThread->SetPriority(s_hTestThread, kLTThread_PriorityHighest);
    s_iThread->Start(s_hTestThread, FlThreadInit, FlThreadDeinit);
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    if (s_hTestThread) {
        s_iThread->Terminate(s_hTestThread);
        s_iThread->WaitUntilFinished(s_hTestThread, LTTime_Infinite());
        s_iThread->Destroy(s_hTestThread);
        s_hTestThread = 0;
    }
    lt_destroyobject(s_mutex);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { TestPIRSensitivity, "TestPIRSensitivity", "FL PIR Sensitivity test", 0 },
    { TestPIRZone,        "TestPIRZone",        "FL PIR Zone test",        0 },
    { TestPIRAlgo,        "TestPIRAlgo",        "FL PIR Algorithm test",   0 },
    { TestPIREvent,       "TestPIREvent",       "FL PIR Event test",       0 },
};

static int UnitTestLTDeviceFlPIRImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceFlPIRImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceFlPIRImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceFlPIR, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceFlPIR, UnitTestLTDeviceFlPIRImpl_Run, 1536) LTLIBRARY_DEFINITION;
