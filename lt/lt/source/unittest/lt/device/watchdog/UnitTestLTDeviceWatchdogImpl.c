/*******************************************************************************
 * Watchdog Unit Test
 *
 * Tests LTDeviceWatchdog and <platform>DriverWatchdog.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>
#include <tilt/JiltEngine.h>

static JiltEngine       * s_engine;
static ILTThread        * s_iThread           = NULL;
static LTDeviceWatchdog * s_pWatchdog         = NULL;

/* True to disable the watchdog as the final test, in which case, the test will
 * end without the device rebooting.  Default is to leave the watchdog enabled,
 * which will cause the device to reboot. */
static bool               s_bTestFinalDisable = false;

/* Formatted status-print helper: */
char const * EnabledToString(bool b) { return b ? "enabled" : "disabled"; }

/* Test and report on the actual and supposed enable/disable state of the watchdog.
 * Return true if the watchdog state is what it's supposed to be: */
static bool IsWatchdogEnabledCorrectly(Tilt * tilt, bool bShouldBeEnabled) {
    bool const bEnabled = s_pWatchdog->IsEnabled();
    TILT_DEBUG(tilt, "watchdog.enabled.value %s", EnabledToString(bEnabled));
    TILT_EXPECT_TRUE(tilt, bEnabled == bShouldBeEnabled, "watchdog.enabled.actual");
    return (bEnabled == bShouldBeEnabled);
}

static void TestWatchdog(Tilt * tilt) {
    /* Get the initial state of the watchdog.  This is useful to see if there is prior-running * code (initialization?) which enables or disables: */
    TILT_DEBUG(tilt, "initial.watchdog.enabled.value %s", EnabledToString(s_pWatchdog->IsEnabled()));
    TILT_DEBUG(tilt, "initial.watchdog.disable");
    TILT_ASSERT_TRUE(tilt, s_pWatchdog->DisableTimer(), "Cannot disable timer");
    TILT_DEBUG(tilt, "timeout.set.watchdog: 1 second");
    TILT_ASSERT_TRUE(tilt, s_pWatchdog->SetTimeout(LTTime_Seconds(1)), "Cannot set timer");
    TILT_DEBUG(tilt, "delay.long: 2 seconds");
    s_iThread->Sleep(LTTime_Seconds(2));
    TILT_DEBUG(tilt, "watchdog.enable1");
    TILT_ASSERT_TRUE(tilt, s_pWatchdog->EnableTimer(), "Cannot enable timer");
    if (!IsWatchdogEnabledCorrectly(tilt, true)) return;
    TILT_DEBUG(tilt, "watchdog.disable1");
    TILT_ASSERT_TRUE(tilt, s_pWatchdog->DisableTimer(), "Cannot disable timer #2");
    if (!IsWatchdogEnabledCorrectly(tilt, false)) return;
    TILT_DEBUG(tilt, "watchdog.enable2");
    TILT_ASSERT_TRUE(tilt, s_pWatchdog->EnableTimer(), "Cannot enable timer #2");
    if (!IsWatchdogEnabledCorrectly(tilt, true)) return;

    for (u16 i = 5; i; --i) {
        TILT_DEBUG(tilt, "refresh.watchdog.iter %d", i);
        s_iThread->Sleep(LTTime_Milliseconds(200));
        s_pWatchdog->ResetTimer();
    }

    if (s_bTestFinalDisable) {
        TILT_DEBUG(tilt, "watchdog.disable2");
        s_pWatchdog->DisableTimer();
        if (!IsWatchdogEnabledCorrectly(tilt, false)) return;
    }

    TILT_DEBUG(tilt, "delay.long.2: 2 seconds");
    s_iThread->Sleep(LTTime_Seconds(2));

    if (s_bTestFinalDisable) {
        TILT_DEBUG(tilt, "watchdog.noreboot.success");
    } else {
        /* should never reach this point - the watchdog should reboot the device during the Sleep above */
        TILT_REPORT_FAILURE(tilt, "watchdog.reboot.fail");
        return;
    }

    /* Report current Boot Reason and perform sanity checks */
    const char * pBootReasonString = NULL;
    LTBootReason bootReason = s_pWatchdog->GetBootReason(&pBootReasonString);
    TILT_ASSERT_TRUE(tilt, pBootReasonString, "Null boot reason");
    TILT_ASSERT_TRUE(tilt, bootReason < kLTBootReason_NumReasons, "Boot reason out of range");
    TILT_INFO(tilt, "Boot Reason %d: %s", bootReason, pBootReasonString);
}

static void RunWatchdogTestFinalReset(Tilt * tilt) {
    s_bTestFinalDisable = false;
    s_pWatchdog = (LTDeviceWatchdog *)LT_GetCore()->OpenLibrary("LTDeviceWatchdog");
    if (!s_pWatchdog) {
        TILT_REPORT_CANNOT_RUN(tilt, "LTDeviceWatchdog.missing");
    } else {
        TestWatchdog(tilt);
        s_pWatchdog->DisableTimer();    /* the test may have left it enabled */
        LT_GetCore()->CloseLibrary((LTLibrary *)s_pWatchdog);
    }
}

static void RunWatchdogTest(Tilt * tilt) {
    s_bTestFinalDisable = true;
    s_pWatchdog = (LTDeviceWatchdog *)LT_GetCore()->OpenLibrary("LTDeviceWatchdog");
    if (!s_pWatchdog) {
        TILT_REPORT_CANNOT_RUN(tilt, "LTDeviceWatchdog.missing");
    } else {
        TestWatchdog(tilt);
        s_pWatchdog->DisableTimer();    /* the test may have left it enabled */
        LT_GetCore()->CloseLibrary((LTLibrary *)s_pWatchdog);
    }
}

static void RunWatchdogTestCrash(Tilt * tilt) {
    s_pWatchdog = (LTDeviceWatchdog *)LT_GetCore()->OpenLibrary("LTDeviceWatchdog");
    if (!s_pWatchdog) {
        TILT_REPORT_CANNOT_RUN(tilt, "LTDeviceWatchdog.missing");
    } else {
        volatile u32 *pNull = NULL;
        s_pWatchdog->SetTimeout(LTTime_Seconds(120));
        s_pWatchdog->EnableTimer();
        *pNull = 0xdeadbabe;
        /* Not reached */
        TILT_REPORT_FAILURE(tilt, "watchdog.crash.fail");
        s_pWatchdog->DisableTimer();
        LT_GetCore()->CloseLibrary((LTLibrary *)s_pWatchdog);
    }
}

static void BeforeAllTests(Tilt * tilt) {
    LT_UNUSED(tilt);
    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
};

/* clang-format off */

static const TiltEngineTest s_tests[] = {
    { RunWatchdogTest,            "Watchdog-no-reset", "LTDeviceWatchdog test no reset", 0                            },
    { RunWatchdogTestFinalReset,  "Watchdog",          "LTDeviceWatchdog test",          kTiltEngineFlag_NoAutomation },
    { RunWatchdogTestCrash,       "Watchdog-crash",    "LTDeviceWatchdog crash system",  kTiltEngineFlag_NoAutomation },
};

/* clang-format on */

static int UnitTestLTDeviceWatchdogImplImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceWatchdogImplImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceWatchdogImplImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceWatchdogImpl, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceWatchdogImpl, UnitTestLTDeviceWatchdogImplImpl_Run, 1536) LTLIBRARY_DEFINITION;
