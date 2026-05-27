/*******************************************************************************
 * Push Button Unit Test
 *
 * Test LTDevicePushButton and <platform>DriverPushButton.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTThread.h>
#include <lt/device/pushbutton/LTDevicePushButton.h>

#include <tilt/JiltEngine.h>

static JiltEngine         * s_engine;
static LTDevicePushButton * s_pPushButton      = NULL;
static bool               * s_pButtonPressed   = NULL;
static u32                  s_nButtonUnderTest;

static void SingleButtonPressTestProc(Tilt * tilt);
static void MultiButtonPressTestProc(Tilt * tilt);
static void SingleButtonPressEventProc(u32 nIndex, void * pClientData);

static char const * ButtonName(u32 nIndex) {
    enum { kNameBufLen = 80 };
    static char nameBuf[kNameBufLen];
    nameBuf[0] = '\0';
    if (s_pPushButton)
        s_pPushButton->GetPushButtonNameFromIndex(nIndex, nameBuf, kNameBufLen);
    return nameBuf;
}

static void PromptForSingleButtonPress(Tilt * tilt, u32 nIndex) {
    TILT_INFO(tilt, "Press and release button \"%s\"", ButtonName(nIndex));
}

static void SingleButtonReleaseEventProc(u32 nIndex, void * pClientData) {
    Tilt * tilt = (Tilt *)pClientData;
    TILT_DEBUG(tilt, "Got single button release %lu", LT_Pu32(nIndex));
    if (nIndex != s_nButtonUnderTest) {
        TILT_REPORT_FAILURE(tilt, "Button release index mismatch (%lu != %lu)",
                                      LT_Pu32(nIndex), LT_Pu32(s_nButtonUnderTest));
        s_engine->API->SignalTestCompletion(s_engine, "Single Button Press");
    }
    u32 nDeviceUnits = s_pPushButton->GetNumDeviceUnits();
    s_pPushButton->UnregisterForButtonPress(nIndex, SingleButtonPressEventProc);
    s_pPushButton->UnregisterForButtonRelease(nIndex, SingleButtonReleaseEventProc);
    if (++s_nButtonUnderTest >= nDeviceUnits) {
        /* All buttons tested */
        s_engine->API->SignalTestCompletion(s_engine, "Single Button Press");
    }
    /* Go to next button */
    s_pButtonPressed[0] = false;
    s_pPushButton->RegisterForButtonPress(s_nButtonUnderTest, SingleButtonPressEventProc, NULL, (void *)tilt);
}

static void SingleButtonPressEventProc(u32 nIndex, void * pClientData) {
    Tilt * tilt = (Tilt *)pClientData;
    TILT_DEBUG(tilt, "Got single button press %lu", LT_Pu32(nIndex));
    if (nIndex != s_nButtonUnderTest) {
        TILT_REPORT_FAILURE(tilt, "Button press index mismatch (%lu != %lu)",
                                      LT_Pu32(nIndex), LT_Pu32(s_nButtonUnderTest));
        s_engine->API->SignalTestCompletion(s_engine, "Single Button Press");
    }
    if (s_pButtonPressed[0]) {
        TILT_REPORT_FAILURE(tilt, "Button %lu already pressed", LT_Pu32(nIndex));
        s_engine->API->SignalTestCompletion(s_engine, "Single Button Press");
    }
    if (!s_pPushButton->IsButtonPressed(nIndex)) {
        TILT_REPORT_FAILURE(tilt, "Button %lu press state mismatch", LT_Pu32(nIndex));
        s_engine->API->SignalTestCompletion(s_engine, "Single Button Press");
    }
    s_pButtonPressed[0] = true;
    s_pPushButton->RegisterForButtonRelease(s_nButtonUnderTest, SingleButtonReleaseEventProc, NULL, (void *)tilt);
}

static void MultiButtonPressEventProc(u32 nIndex, void * pClientData) {
    Tilt * tilt = (Tilt *)pClientData;
    TILT_DEBUG(tilt, "Got multi button press %lu", LT_Pu32(nIndex));
    TILT_ASSERT_TRUE(tilt, nIndex < s_pPushButton->GetNumDeviceUnits(), "Invalid button index %lu", LT_Pu32(nIndex));
    s_pButtonPressed[nIndex] = true;
    s_pPushButton->UnregisterForButtonPress(nIndex, MultiButtonPressEventProc);
}

static void MultiButtonReleaseEventProc(u32 nIndex, void * pClientData) {
    Tilt * tilt = (Tilt *)pClientData;
    TILT_DEBUG(tilt, "Got multi button release %lu", LT_Pu32(nIndex));
    TILT_ASSERT_TRUE(tilt, nIndex < s_pPushButton->GetNumDeviceUnits(), "Invalid button index %lu", LT_Pu32(nIndex));
    s_pPushButton->UnregisterForButtonRelease(nIndex, MultiButtonReleaseEventProc);
    TILT_ASSERT_TRUE(tilt, s_pButtonPressed[nIndex], "Button release event received before press");
    if (++s_nButtonUnderTest == s_pPushButton->GetNumDeviceUnits()) {
        /* All buttons tested */
        s_engine->API->SignalTestCompletion(s_engine, "Multiple Button Press");
    }
}

/* Test Procs */

static void GetterTestProc(Tilt * tilt) {
    u32 nDeviceUnits = s_pPushButton->GetNumDeviceUnits();
    TILT_ASSERT_TRUE(tilt, nDeviceUnits > 0, "No device units present?");
    TILT_INFO(tilt, "LTDevicePushButton reports existence of %lu push button%s",
                           LT_Pu32(nDeviceUnits), nDeviceUnits == 1 ? "" : "s");
    for (u32 nDevice = 0; nDevice < nDeviceUnits; ++nDevice) {
        enum { kBufLen = 80 };
        char nameBuf[kBufLen];
        TILT_ASSERT_TRUE(tilt, s_pPushButton->GetPushButtonNameFromIndex(nDevice, nameBuf, kBufLen),
                                         "Can't get button name");
        TILT_DEBUG(tilt, "LTDevicePushButton Device %lu: \"%s\"", LT_Pu32(nDevice), nameBuf);
        u32 nIndex;
        TILT_ASSERT_TRUE(tilt, s_pPushButton->GetPushButtonIndexFromName(nameBuf, &nIndex),
                                         "Unable to get push button index %lu", LT_Pu32(nIndex));
        TILT_ASSERT_TRUE(tilt, nIndex == nDevice, "Push button index does not match device number");
    }
}

static void CleanupProc(Tilt *tilt, void *clientData) {
    LT_UNUSED(clientData);
    TILT_INFO(tilt, "Cleaning up!");
    if (s_pButtonPressed) lt_free(s_pButtonPressed);
    s_pButtonPressed = NULL;
}

static void SingleButtonPressTestProc(Tilt * tilt) {
    u32 nDeviceUnits = s_pPushButton->GetNumDeviceUnits();
    TILT_ASSERT_TRUE(tilt, nDeviceUnits > 0, "No device units present?");
    bool * pTemp = (bool *)lt_realloc(s_pButtonPressed, sizeof(bool));
    TILT_ASSERT_TRUE(tilt, pTemp != NULL, "memory allocation failure");
    s_pButtonPressed    = pTemp;
    s_pButtonPressed[0] = false;
    s_nButtonUnderTest  = 0;
    PromptForSingleButtonPress(tilt, s_nButtonUnderTest);
    s_pPushButton->RegisterForButtonPress(s_nButtonUnderTest, SingleButtonPressEventProc, NULL, (void *)tilt);
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(5), CleanupProc, NULL);
}

static void MultiButtonPressTestProc(Tilt * tilt) {
    u32 nDeviceUnits = s_pPushButton->GetNumDeviceUnits();
    TILT_ASSERT_TRUE(tilt, nDeviceUnits > 0, "No device units present?");
    bool * pTemp = (bool *)lt_realloc(s_pButtonPressed, nDeviceUnits * sizeof(bool));
    TILT_ASSERT_TRUE(tilt, pTemp != NULL, "memory allocation failure");
    s_pButtonPressed   = pTemp;
    s_nButtonUnderTest = 0;
    for (u32 nDevice = 0; nDevice < nDeviceUnits; nDevice++) {
        s_pButtonPressed[nDevice] = false;
        s_pPushButton->RegisterForButtonPress(nDevice, MultiButtonPressEventProc, NULL, (void *)tilt);
        s_pPushButton->RegisterForButtonRelease(nDevice, MultiButtonReleaseEventProc, NULL, (void *)tilt);
    }
    TILT_INFO(tilt, "Press and release all buttons in any order, multipress allowed");
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(5), CleanupProc, NULL);
}

static void BeforeAllTests(Tilt *tilt) {
    s_pPushButton = (LTDevicePushButton *)LT_GetCore()->OpenLibrary("LTDevicePushButton");
    TILT_ASSERT_TRUE(tilt, s_pPushButton, "Couldn't open library under test");
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    if (s_pButtonPressed) lt_free(s_pButtonPressed);
    s_pButtonPressed = NULL;
    LT_GetCore()->CloseLibrary((LTLibrary *)s_pPushButton);
    s_pPushButton = NULL;
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests
};

static const TiltEngineTest s_tests[] = {
    { GetterTestProc,            "Test Getters",        "Basic API tests",                 0                            },
    { SingleButtonPressTestProc, "Single Button Press", "Test one button press at a time", kTiltEngineFlag_NoAutomation },
    { MultiButtonPressTestProc,  "Multi Button Press",  "Test multiple button presses",    kTiltEngineFlag_NoAutomation },
};

static int UnitTestLTDevicePushButtonImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDevicePushButtonImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDevicePushButtonImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDevicePushButton, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDevicePushButton, UnitTestLTDevicePushButtonImpl_Run, 1536) LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  10-Feb-21   constantine created
 */
