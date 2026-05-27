/*******************************************************************************
 * LTNetSpeed Unit Test
 *
 * Test the Link Speed implementation LTNetSpeed.
 *
 * ltrun TiltEngine UnitTestLTNetSpeed
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/monitor/LTNetMonitor.h>
#include <lt/net/speed/LTNetSpeed.h>
#include <lt/core/LTCore.h>

#include <tilt/JiltEngine.h>

/*******************************************************************************
 * Static Data and typedefs
 *******************************************************************************/
#define RETRY 3

static struct Statics {
    LTNetMonitor    *netMonitor;
    LTNetSpeed      *netSpeed;
    const char      *doneSignal;
    u32              retry;
} S;

static JiltEngine  *s_engine;

/*******************************************************************************
 * Helpers
 *******************************************************************************/
#define SIGNAL_TEST \
    s_engine->API->SignalTestCompletion(s_engine, S.doneSignal); \
    S.doneSignal = "?"

#define WAIT_TEST(timeout) { \
    S.doneSignal = tilt->API->GetTestName(); \
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(timeout), NULL, NULL); \
}

/*******************************************************************************
 * Callback for LTNetSpeed->LinkSpeed()
 *******************************************************************************/
static void OnLinkSpeedEvent(u32 kbps, void *data) {
    Tilt *tilt = data;
    if (kbps == 0 && S.retry++ < RETRY) {
        // Try the test up to RETRY times if it fails
        TILT_WARNING(tilt, "LinkSpeedASync() returned <0> kbps - retry %lu", LT_Pu32(S.retry));
        S.netSpeed->LinkSpeed(0, 0, false, OnLinkSpeedEvent, (void*)tilt);
        return;
    }
    TILT_EXPECT_TRUE(tilt, kbps > 0, "LinkSpeedASync() returned <%lu> kbps", LT_Pu32(kbps));
    SIGNAL_TEST;
}

/**********************************************************************************
 * Test definitions
 **********************************************************************************/
static void RunNetSpeedSyncTest(Tilt *tilt) {
    u32 kbps = 0;
    S.retry = 0;
    do {
        kbps = S.netSpeed->LinkSpeed(0, 0, true, NULL, NULL) > 0;
        if (kbps == 0) {
            // Try the test up to RETRY times if it fails
            TILT_WARNING(tilt, "LinkSpeedSync() returned <0> kbps - retry %lu", LT_Pu32(S.retry + 1));
        }
    } while (kbps == 0 && S.retry++ < RETRY);
    TILT_EXPECT_TRUE(tilt, kbps > 0, "LinkSpeedSync() returned <%lu> kbps", LT_Pu32(kbps));
}

static void RunNetSpeedAsyncTest(Tilt *tilt) {
    S.retry = 0;
    S.netSpeed->LinkSpeed(0, 0, false, OnLinkSpeedEvent, (void*)tilt);
    WAIT_TEST(30);
}

/**********************************************************************************
 * Tilt Test Section
 **********************************************************************************/
static void OnNetStatus(LTNetMonitor_Status status, void *clientData) {
    LT_UNUSED(clientData);
    if (status == kLTNetMonitor_Status_NetworkUp) {
        s_engine->API->SignalTestCompletion(s_engine, NULL);
        S.netMonitor->NoStatusChange(OnNetStatus);
    }
}

static void BeforeAllTests(Tilt *tilt) {
    S = (struct Statics) {
        .doneSignal = "?",
        .netMonitor = lt_openlibrary(LTNetMonitor),
        .netSpeed = lt_openlibrary(LTNetSpeed)
    };
    TILT_EXPECT_TRUE(tilt, S.netSpeed != NULL, "Cannot open LTNetSpeed");

    S.netMonitor->OnStatusChange(OnNetStatus, NULL, (void *)tilt);

    // Don't start tests until transport comes up.
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(30), NULL, NULL);
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_closelibrary(S.netSpeed);
    lt_closelibrary(S.netMonitor);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { RunNetSpeedAsyncTest, "NetSpeedAsync", "Runs the asynchronous Net Speed Link test", 0 },
    { RunNetSpeedSyncTest,  "NetSpeedSync",  "Runs the synchronous Net Speed Link test", 0 },
};

static int UnitTestLTNetSpeedImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTNetSpeedImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTNetSpeedImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetSpeed, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetSpeed, UnitTestLTNetSpeedImpl_Run, 1536) LTLIBRARY_DEFINITION;
