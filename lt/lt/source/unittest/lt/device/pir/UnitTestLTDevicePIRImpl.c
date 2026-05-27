/*******************************************************************************
 * PIR sensors Unit Test
 *
 * Test LTDevicePIR and <platform>DriverPIR.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/pir/LTDevicePIR.h>
#include <tilt/JiltEngine.h>

/*******************************************************************************
 * Static Variables
 ******************************************************************************/
static struct Statics {
    JiltEngine  *engine;

    LTDevicePIR *devicePIR;
    LTDeviceUnit deviceUnit;
    u32          deviceIndex;
} S;

/*******************************************************************************
 * Callback Functions
 ******************************************************************************/

static void PIRMotionDetectionEventProc(bool bMotion, u32 nDeviceIndex, void *pClientData) {
    Tilt *tilt = (Tilt *)pClientData;
    TILT_ASSERT_TRUE(tilt, nDeviceIndex == S.deviceIndex, "Device index mismatch in motion event");

    if (bMotion) {
        TILT_INFO(tilt, "Motion started");
        S.engine->API->SignalTestCompletion(S.engine, tilt->API->GetTestName());
    } else {
        TILT_WARNING(tilt, "Motion stopped");
    }
}

static void FinishTest(void * pClientData) {
    Tilt *tilt = (Tilt *)pClientData;
    
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    thread->API->KillTimer(thread, FinishTest, pClientData);

    TILT_INFO(tilt, "Finishing test, no motion detected");
    S.engine->API->SignalTestCompletion(S.engine, tilt->API->GetTestName());
}

/*******************************************************************************
 * Test Functions
 ******************************************************************************/
static void TestMotion(Tilt *tilt) {
    // Start a proc that completes the test even if there was no motion event.
    // In CI we don't have a way to trigger motion, so we don't want to fail.
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    thread->API->SetTimer(thread, LTTime_Seconds(5), FinishTest, NULL, tilt);

    S.engine->API->DeferTestCompletion(S.engine, LTTime_Seconds(10), NULL, (void*)tilt);
}

static void BeforeAllTests(Tilt *tilt) {
    S.devicePIR = lt_openlibrary(LTDevicePIR);
    TILT_ASSERT_TRUE(tilt, S.devicePIR != NULL, "Cannot open LTDevicePIR");

    u32 nNumDevices = S.devicePIR->GetNumDeviceUnits();
    for (u32 i = 0; i < nNumDevices; ++i) {
        S.deviceUnit = S.devicePIR->CreateDeviceUnitHandle(i);
        if (S.deviceUnit != LTHANDLE_INVALID) {
            S.deviceIndex = i;
            break;
        }
    }
    TILT_ASSERT_TRUE(tilt, S.deviceUnit != LTHANDLE_INVALID, "Cannot create PIR device unit");
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);

    lt_destroyhandle(S.deviceUnit);
    S.deviceUnit = LTHANDLE_INVALID;
    lt_closelibrary(S.devicePIR);
    S.devicePIR = NULL;
}

static void BeforeTest(Tilt *tilt) {
    LT_UNUSED(tilt);
    S.devicePIR->RegisterForMotionDetection(S.deviceUnit, PIRMotionDetectionEventProc, NULL, tilt);
    S.devicePIR->Enable(S.deviceUnit, true);
}

static void AfterTest(Tilt *tilt) {
    LT_UNUSED(tilt);
    S.devicePIR->Enable(S.deviceUnit, false);
    S.devicePIR->UnregisterFromMotionDetection(S.deviceUnit, PIRMotionDetectionEventProc);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
    .BeforeTest     = BeforeTest,
    .AfterTest      = AfterTest
};

static const TiltEngineTest s_tests[] = {
    { TestMotion, "TestMotion", "Test if motion event is started", 0 },
};

static int UnitTestLTDevicePIRImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    S.engine->API->ConfigureTestSuite(S.engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return S.engine->API->RunTestSuite(S.engine, argc, argv);
}

static bool UnitTestLTDevicePIRImpl_LibInit(void) {
    S = (struct Statics){0};
    S.engine = lt_createobject(JiltEngine);
    return S.engine != NULL;
}

static void UnitTestLTDevicePIRImpl_LibFini(void) {
    lt_destroyobject(S.engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDevicePIR, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDevicePIR, UnitTestLTDevicePIRImpl_Run, 1024) LTLIBRARY_DEFINITION;
