/******************************************************************************
 * Slider Unit Test
 *
 * Test LTDeviceSlider and <platform>DriverSlider.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/device/slider/LTDeviceSlider.h>

#include <tilt/TiltImpl.c>

//DEFINE_LTLOG_SECTION("slidertest")

#define TEST_TIME_LIMIT 120
#define TIMER_INTERVAL    2
#define EVENT_COUNTER     8
#define TIMER_COUNTER     8

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceSlider, 1) LTLIBRARY_EMPTY_INTERFACE

static LTSystemShell             *s_pShell          = NULL;
static ILTShell                  *s_iShell          = NULL;
static ILTThread                 *s_iThread         = NULL;

static LTDeviceSlider            *s_pSlider          = NULL;
static ILTSliderDeviceUnit       *s_iDeviceUnit      = NULL;
static bool                       s_bShortTest       = false;
static bool                       s_bAutomatedTest   = false;
static LTHandle                   s_hSlider          = 0;
static LTHandle                   s_hSlider2         = 0;

static LTShell                    s_hShell           = 0;
static u32                        s_nResult          = 20; /* not complete */

static u32                        s_nIntrptCounter   = 0;
static u32                        s_nTimerCounter    = 0;
static TiltImplReportingCallbacks s_noTiltCallbacks;

static void TestSlider_Print(const char *pMessage, ...) {
    lt_va_list args;
    lt_va_start(args, pMessage);
    if (s_hShell) {
        s_iShell->VPrint(s_hShell, pMessage, args);
    }
    else {
        LT_GetCore()->ConsolePrintV(pMessage, args);
    }
    lt_va_end(args);
}

/********************************************************************************
 * The output functions that replace Tilt functions for the use cases when TiltEngine is not running.
 * Both functions redirect the output to TestSlider_Print, which then detects if a shell or a console
 * is available for output.
 */
static void TiltReplMessage(const char * pFileName, u32 lineNumber, TiltMessageLevel level, const char * pMessage, ...) {
    LT_UNUSED(pFileName);
    LT_UNUSED(lineNumber);
    LT_UNUSED(level);

    lt_va_list args;
    lt_va_start(args, pMessage);
    char message[64];
    lt_vsnprintf(message, sizeof(message), pMessage, args);
    TestSlider_Print("%s\n", message);
    lt_va_end(args);
}

static void TiltReplFailure(TiltResult type, const char * pFileName, u32 lineNumber, const char * pMessage, ...) {
    LT_UNUSED(pFileName);
    LT_UNUSED(lineNumber);
    LT_UNUSED(type);
    lt_va_list args;
    lt_va_start(args, pMessage);
    char message[64];
    lt_vsnprintf(message, sizeof(message), pMessage, args);
    TestSlider_Print("Test Fail: %s\n", message);
    lt_va_end(args);
}

/******************************************************************************
 * Output usage hints
 */
static void DisplayLongUsage(int const argc, char const **argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    TestSlider_Print("*********************************************************************\n");
    TestSlider_Print("* Slider unit test initially runs in the event-driven mode.         *\n");
    TestSlider_Print("* It expects %d correct swipes and reports state after each swipe.   *\n", EVENT_COUNTER);
    TestSlider_Print("* Then, it switches to periodic updates for %d seconds.             *\n", TIMER_COUNTER * TIMER_INTERVAL);
    TestSlider_Print("* It reports updates once a second with or without swipes between   *\n");
    TestSlider_Print("* reports.                                                          *\n");
    TestSlider_Print("* The test terminates after %d s.                                  *\n", TEST_TIME_LIMIT);
    TestSlider_Print("*********************************************************************\n");
}

static void DisplayShortUsage(int const argc, char const **argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    TestSlider_Print("* Test runs for 2 minutes. Try to press, swipe and hold, and observe value changes. *\n");
}

static void DisplayUsage(int const argc, char const **argv) {
    LT_UNUSED(argc);
    char const * pInvocation = "UnitTestLTDeviceSlider";
    if (argc >= 2) pInvocation = argv[1];
    TestSlider_Print("usage: %s [-h | -s | -l | -a]\n", pInvocation);
    TestSlider_Print("          -h: help\n");
    TestSlider_Print("          -s: simple test that receives interrupt events\n");
    TestSlider_Print("          -l: test that exercises all APIs\n");
    TestSlider_Print("          -a: tests APIs that don't require manual inputs\n");
}

static void SliderTimerCallback(LTDeviceUnit unit, s32 state, void * data) {
    LT_UNUSED(unit);
    LT_UNUSED(data);
    TestSlider_Print("%d new state %ld\n", TIMER_COUNTER - s_nTimerCounter, state);

    s_nTimerCounter++;
    /* After TIMER_COUNTER callbacks, this callback is unregistered, one more
     * test is performed for a handle that's not created by LTDeviceSlider and
     * then the thread running this test terminates, which will terminate the
     * test too.
     */
    if (s_nTimerCounter == TIMER_COUNTER) {
        s_iDeviceUnit->UnregisterFromUpdates(unit, SliderTimerCallback);
        /* unit + 4 is an invalid handle, or if it is valid, it is not created
         * by LTDeviceSlider because only one handle is created for this test.
         */
        if (s_iDeviceUnit->RegisterForUpdates(unit + 4, LTTime_Zero(), SliderTimerCallback, NULL)) {
            TestSlider_Print("test fail - wrong handle\n");
            s_nResult = 51;
            return;
        }

        s_iThread->Terminate(s_iThread->GetCurrentThread());
        return;
    }
}

static void SliderIntCallback(LTDeviceUnit unit, s32 value, void * data) {
    LT_UNUSED(unit);
    LT_UNUSED(data);
    TestSlider_Print("event counter: %d new value %ld\n", EVENT_COUNTER - s_nIntrptCounter, value);

    s_nIntrptCounter++;
    /* After EVENT_COUNTER interrupts, switch to periodic updates */
    if (s_nIntrptCounter == EVENT_COUNTER) {
        TestSlider_Print("registering periodic callback\n");
        s_iDeviceUnit->RegisterForUpdates(unit, LTTime_Seconds(TIMER_INTERVAL),
                                          SliderTimerCallback, NULL);
    }
}

static void SimpleCallback(LTDeviceUnit unit, s32 value, void * pData) {
    LT_UNUSED(unit);
    LT_UNUSED(pData);
    TestSlider_Print("new value %d\n", LT_Ps32(value));
}

/******************************************************************************
 * A short test that installs interrupt handler and displays events and values.
 *
 */
static void ShortTest(const TiltImplReportingCallbacks * trc) {
    LT_UNUSED(trc);
    DisplayShortUsage(0, NULL);
    s_hSlider = s_pSlider->CreateDeviceUnitHandle(0);
    s_iDeviceUnit = lt_gethandleinterface(ILTSliderDeviceUnit, s_hSlider);

    s_iDeviceUnit->Initialize(s_hSlider, -10, 10, 0, 0);
    s_iDeviceUnit->RegisterForUpdates(s_hSlider, LTTime_Zero(), SimpleCallback, NULL);
    s_nResult = 0;
    return;
}

/******************************************************************************
 * A test that doesn't require manual inputs, appropriate for automated testing.
 *
 */
static void AutoTest(const TiltImplReportingCallbacks * trc) {
    u32 nNumDeviceUnits = s_pSlider->GetNumDeviceUnits();
    TILT_MESSAGE(trc, "LTDeviceSlider reports %lu Device Unit%s\n", LT_Pu32(nNumDeviceUnits), nNumDeviceUnits == 1 ? "" : "s");
    TILT_ASSERT_TRUE(trc, nNumDeviceUnits > 0, "LTDeviceSlider provides no Device Units");

    s_hSlider = s_pSlider->CreateDeviceUnitHandle(0);
    s_iDeviceUnit = lt_gethandleinterface(ILTSliderDeviceUnit, s_hSlider);

    s_hSlider2 = s_pSlider->CreateDeviceUnitHandle(0);

    s_nResult = 50;
    TILT_ASSERT_FALSE(trc, s_hSlider2 != s_hSlider, "Slider handles must be equal  %lx != %lx",
        LT_PLT_HANDLE(s_hSlider), LT_PLT_HANDLE(s_hSlider2));

    TILT_DEBUG(trc, "Create Device - PASS");
    s_nResult = 51;
    TILT_ASSERT_FALSE(trc, s_iDeviceUnit->Initialize(s_hSlider, 20, 10, 0, 0),
        "Initialize() should fail - lower bound is higher than upper");

    s_nResult = 52;
    TILT_ASSERT_FALSE(trc, s_iDeviceUnit->Initialize(s_hSlider, 10, 10, 0, 0),
        "Initialize() should fail - lower and upper bound are same");

    s_nResult = 53;
    TILT_ASSERT_FALSE(trc, s_iDeviceUnit->Initialize(s_hSlider, -10, 10, 40, 0),
        "Initialize() should fail - initial state is out of bounds");

    s_nResult = 54;
    TILT_ASSERT_FALSE(trc, s_iDeviceUnit->Initialize(s_hSlider, LT_S32_MIN, LT_S32_MAX, 0, 0),
        "Initialize() should fail - reserved value used");

    s_nResult = 55;
    TILT_ASSERT_TRUE(trc, s_iDeviceUnit->Initialize(s_hSlider, -10, 10, 0, 0),
        "unexpected initialization error");
    TILT_DEBUG(trc, "Initialize - PASS");

    s_nResult = 56;
    TILT_ASSERT_TRUE(trc, s_iDeviceUnit->SetValue(s_hSlider, 4), "SetValue() should not fail - new state is correct");
    TILT_DEBUG(trc, "SetValue - PASS");

    s_nResult = 57;
    TILT_ASSERT_TRUE(trc, s_iDeviceUnit->GetValue(s_hSlider) == 4, "GetValue should return 4");
    TILT_DEBUG(trc, "GetValue - PASS");
    s_nResult = 0;
}

/******************************************************************************
 *  The test first initializes the device with some intentionally invalid values,
 *  and then at the end a callback function is registered, that will get invoked
 *  on every value change.
 */
static void RunSliderTest(const TiltImplReportingCallbacks * trc) {
    LT_UNUSED(trc);
    u32 nNumDeviceUnits = s_pSlider->GetNumDeviceUnits();
    TestSlider_Print("LTDeviceSlider reports %lu Device Unit%s\n", LT_Pu32(nNumDeviceUnits), nNumDeviceUnits == 1 ? "" : "s");
    if (nNumDeviceUnits == 0) {
        TestSlider_Print("LTDeviceSlider provides no Device Units\n");
        s_nResult = 21;
    }
    else {
        s_hSlider = s_pSlider->CreateDeviceUnitHandle(0);
        s_iDeviceUnit = lt_gethandleinterface(ILTSliderDeviceUnit, s_hSlider);

        /* Try to acquire the same device unit twice. CreateDeviceUnitHandle() should return
         * the same handle as for the first call.
         *
         * DRW - no it should not - two libraries or threads or whatever can
         *       call CreateDeviceUnitHandle(0); and they should get back
         *       *different* handles that refer to the same device unit.
         *       That way when one destroys their handle, the handle memory
         *       is reclaimed but the other handle still works.
         *       That's why it's called CreateDeviceUnitHandle and not
         *       GetDeviceUnitHandle.
         *
         */
        s_hSlider2 = s_pSlider->CreateDeviceUnitHandle(0);
        if (s_hSlider2 != s_hSlider) {
            TestSlider_Print("handles must be equal %lx != %lx\n", LT_PLT_HANDLE(s_hSlider), LT_PLT_HANDLE(s_hSlider2));
            s_nResult = 22;
            return;
        }

        if (s_iDeviceUnit->Initialize(s_hSlider, 20, 10, 0, 0)) {
            TestSlider_Print("should fail - lower bound is higher than upper\n");
            s_nResult = 23;
            return;
        };
        if (s_iDeviceUnit->Initialize(s_hSlider, 10, 10, 0, 0)) {
            TestSlider_Print("should fail - lower and upper bound are same\n");
            s_nResult = 24;
            return;
        };
        if (s_iDeviceUnit->Initialize(s_hSlider, -10, 10, 40, 0)) {
            TestSlider_Print("should fail - initial state is out of bounds\n");
            s_nResult = 25;
            return;
        };
        if (s_iDeviceUnit->Initialize(s_hSlider, LT_S32_MIN, LT_S32_MAX, 0, 0)) {
            TestSlider_Print("should fail - reserved value used\n");
            s_nResult = 26;
            return;
        };

        if (!(s_iDeviceUnit->Initialize(s_hSlider, -10, 10, 0, 0))) {
            TestSlider_Print("unexpected initialization error\n");
            s_nResult = 27;
            return;
        };

        if (s_iDeviceUnit->RegisterForUpdates(s_hSlider + 4, LTTime_Zero(), SliderIntCallback, NULL)) {
            TestSlider_Print("should fail - wrong handle\n");
            s_nResult = 30;
            return;
        }

        if (s_iDeviceUnit->SetValue(s_hSlider, 45)) {
            TestSlider_Print("should fail - new state out of bounds\n");
            s_nResult = 40;
            return;
        }
        if (s_iDeviceUnit->GetValue(s_hSlider + 4) != LT_S32_MIN) {
            TestSlider_Print("should fail - wrong handle\n");
            s_nResult = 41;
            return;
        }
        DisplayLongUsage(0, NULL);
        s_iDeviceUnit->RegisterForUpdates(s_hSlider, LTTime_Zero(), SliderIntCallback, NULL);
    }
    return;
}

static void EndSliderTest(void) {
    if (LT_GetCore()->IsHandleValid(s_hSlider)) {
        LT_GetCore()->DestroyHandle(s_hSlider);
    }
    s_hSlider = 0;

    if (LT_GetCore()->IsHandleValid(s_hSlider2)) {
        LT_GetCore()->DestroyHandle(s_hSlider2);
    }
    s_hSlider2 = 0;
    s_iDeviceUnit = NULL;
}

/******************************************************************************
 * Gather command-line options.
 *  -s -> short test
 *  -l -> test that exercises majority of APIs
 */
static inline int GatherOptions(int const argc, char const ** argv) {
    LT_UNUSED(argc);
    int nResult = 0;

    if (argc == 3) {
        if (lt_strcmp(argv[2], "-s") == 0) {
            s_bShortTest = true;
        }
        else if (lt_strcmp(argv[2], "-a") == 0) {
            s_bAutomatedTest = true;
        }
        else if (lt_strcmp(argv[2], "-h") == 0) {
            DisplayUsage(argc, argv);
        }
        else if (lt_strcmp(argv[2], "-l") != 0) {
            DisplayUsage(argc, argv);
            nResult = 10;
        }
    }
    else if (argc > 3) {
        DisplayUsage(argc, argv);
        nResult = 11;
    }
    return nResult;
}

static void TestSlider_Auto(void *pData) {
    LT_UNUSED(pData);
    s_noTiltTestName = "Automatic Test";
    AutoTest(&s_noTiltCallbacks);
}

static void TestSlider_Full(void *pData) {
    LT_UNUSED(pData);
    s_noTiltTestName = "Full Test";
    RunSliderTest(&s_noTiltCallbacks);
}

static void TestSlider_Short(void *pData) {
    LT_UNUSED(pData);
    s_noTiltTestName = "Short Test";
    ShortTest(&s_noTiltCallbacks);
}

static void TestSlider_List(void * pData);
static int TestSlider_RunCommand(LTShell hShell, int argc, const char * argv[]);

typedef struct {
    const char        *m_pCommand;
    LTThread_TaskProc *m_pCommandProc;
    const char        *m_pDescription;
    s64                m_nWaitingTime;
} UnitTestSlider_CommandDesc;

static const UnitTestSlider_CommandDesc s_TestSliderCommands[] = {
    { "help",            TestSlider_List,  "displays available tests", 0 },
    { "automatic",       TestSlider_Auto,  "tests APIs that don't require manual inputs", 0 },
    { "event-detection", TestSlider_Short, "simple test that displays detected events, requires manual inputs", TEST_TIME_LIMIT },
    { "full-test",       TestSlider_Full,  "test that exercises all APIs and requires manual inputs", TEST_TIME_LIMIT },
    { NULL,              NULL,             "", 0 }
};

static void TestSlider_Help(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    /* This LTShell Help proc is invoked when the console input is
     * "help test-slider", while "test-slider help" calls TestSlider_RunCommand
     * that recognizes 'help' as argv[1] amd then runs TestSlider_List.
     */
    (void)TestSlider_List(LTHANDLE_TO_VOIDPTR(hShell));
}

static const LTSystemShell_CommandDesc s_TestSliderCommand[] = {
    { "test-slider", TestSlider_RunCommand, "slider tests, type \"test-slider\" for options", TestSlider_Help }
};

static void TestSlider_List(void *pData) {
    LTShell hShell = VOIDPTR_TO_LTHANDLE(pData);

    s_iShell->Print(hShell, "usage: test-slider <command>\nCommands:\n");
    for (int n = 0; s_TestSliderCommands[n].m_pCommand; n++) {
        s_iShell->Print(hShell, " %-20s - %s\n", s_TestSliderCommands[n].m_pCommand,
                        s_TestSliderCommands[n].m_pDescription);
    }
}

static const char * UnitTestLTDeviceSlider_CommonInit(void) {
    static const char * msg = "Cannot open LTDeviceSlider library\n";
    s_pSlider = (LTDeviceSlider *)LT_GetCore()->OpenLibrary("LTDeviceSlider");

    if (!s_pSlider) {
        return msg;
    }

    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    return NULL;
}

static int UnitTestLTDeviceSliderImpl_Run(int argc, char const ** argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if ((s_nResult = GatherOptions(argc, argv)) != 0) return s_nResult;

    s_nIntrptCounter = 0;
    s_nTimerCounter  = 0;
    s_nResult        = 20;
    u32 timeLimit    = TEST_TIME_LIMIT;

    const char *errMsg = UnitTestLTDeviceSlider_CommonInit();
    if (errMsg) {
        TestSlider_Print(errMsg);
        return s_nResult;
    }

    LTThread hThread = LT_GetCore()->CreateThread("SliderTestThread");
    s_iThread->Start(hThread, NULL, EndSliderTest);

    GetNoTiltCallbacks(&s_noTiltCallbacks);
    s_noTiltCallbacks.Message = TiltReplMessage;
    s_noTiltCallbacks.ReportFailure = TiltReplFailure;

    if (s_bAutomatedTest) {
        s_iThread->QueueTaskProc(hThread, TestSlider_Auto, NULL, NULL);
        timeLimit = 0;
    }
    else if (!s_bShortTest) {
        s_iThread->QueueTaskProc(hThread, TestSlider_Full, NULL, NULL);
    }
    else s_iThread->QueueTaskProc(hThread, TestSlider_Short, NULL, NULL);

    /* Wait for a normal ending within TIME_LIMIT, and after that try to
     * terminate the thread at any time the scheduler gets control. In case of
     * the longer test, it will terminate itself at the last periodic callback
     * and if that happens within TIME_LIMIT we will not get into the IF block.
     * But, if there are no interrupts coming from the slider, nothing will run
     * on hThread once RunSliderTest exits, and the thread will be terminated
     * in the IF block.
     * The short test never terminates on its own, so its execution will always
     * cause this function to go through the IF block.
     */
    if (!s_iThread->WaitUntilFinished(hThread, LTTime_Seconds(timeLimit))) {
        s_iThread->Terminate(hThread);
        s_iThread->WaitUntilFinished(hThread, LTTime_Infinite());
    }

    if (hThread != 0) {
        s_iThread->Destroy(hThread);
    }

    return s_nResult;
}

static int TestSlider_RunCommand(LTShell hShell, int argc, const char * argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    s_hShell = hShell;
    int cmd = 0;

    if (!s_pSlider) {
        const char *errMsg = UnitTestLTDeviceSlider_CommonInit();
        if (errMsg) {
            TestSlider_Print(errMsg);
            return -1;
        }
    }

    if (argc != 2) {
        (void)TestSlider_List(LTHANDLE_TO_VOIDPTR(hShell));
    }
    else {
        // Check the args and find the command from TestSlider commands that matches argv[1].
        // 'test-slider automatic' for example.
        for (int n = 0; s_TestSliderCommands[n].m_pCommand; n++) {
            if (lt_strcmp(s_TestSliderCommands[n].m_pCommand, argv[1]) == 0) {
                cmd = n;
                break;
            }
        }
        GetNoTiltCallbacks(&s_noTiltCallbacks);
        s_noTiltCallbacks.Message = TiltReplMessage;
        s_noTiltCallbacks.ReportFailure = TiltReplFailure;

        /* Callbacks that are reporting the slider events are queued on the thread
         * that registers them. That can't be the thread that called
         * TestSlider_RunCommand because that thread returns to the calling shell
         * before any proc can run on it.
         */
        LTThread hThread = LT_GetCore()->CreateThread("SliderTestThread");
        if (hThread != 0) {
            s_iThread->Start(hThread, NULL, EndSliderTest);
            s_iThread->QueueTaskProc(hThread, s_TestSliderCommands[cmd].m_pCommandProc, NULL, NULL);
            if (!s_iThread->WaitUntilFinished(hThread, LTTime_Seconds(s_TestSliderCommands[cmd].m_nWaitingTime))) {
                s_iThread->Terminate(hThread);
                s_iThread->WaitUntilFinished(hThread, LTTime_Infinite());
            }
            s_iThread->Destroy(hThread);
        }
    }

    s_hShell = 0;
    //s_iShell->Print(hShell, "test-slider returning %ld\n", s_nResult);
    return s_nResult;
}

static void UnitTestLTDeviceSlider_BeforeAllHook(const TiltImplReportingCallbacks * trc) {
    const char *ret = UnitTestLTDeviceSlider_CommonInit();
    if (ret) TILT_REPORT_CANNOT_RUN(trc, ret);
}

static void UnitTestLTDeviceSlider_AfterHook(const TiltImplReportingCallbacks * trc) {
    LT_UNUSED(trc);
    EndSliderTest();
}

static TiltImplTestHooks s_testHooks = {
    .BeforeAllTests = UnitTestLTDeviceSlider_BeforeAllHook,
    .AfterAllTests  = NULL,
    .BeforeTest     = NULL,
    .AfterTest      = UnitTestLTDeviceSlider_AfterHook
};

static void UnitTestLTDeviceSliderImpl_LibFini(void) {
    ShutdownTilt();

    if (s_pShell) {
        s_pShell->UnregisterCommands(s_TestSliderCommand);
        lt_closelibrary(s_pShell);
    }
    lt_closelibrary(s_pSlider);

    s_iThread       = NULL;
    s_iShell        = NULL;
    s_pShell        = NULL;
    s_pSlider       = NULL;
}

static bool UnitTestLTDeviceSliderImpl_LibInit(void) {
    static const TiltImplTestSpecifier s_tests[] = {
        { AutoTest, "Selected APIs", "Doesn't require interaction", 0 },
        { ShortTest, "Event Detection", "Displays detected events", kTiltTestFlag_NoAutomation },
        { RunSliderTest, "Full Test", "Exercises all APIs", kTiltTestFlag_NoAutomation }
    };
    InitializeTilt(s_tests, 1, &s_testHooks);

    /* At this moment we don't know if it's 'ltopen UnitTestLTDeviceSlider' or 'ltrun UnitTestLTDeviceSlider' or
     * 'ltrun TiltEngine UnitTestLTDeviceSlider' that called LibInit.
     * If it's the first, we need to register a command, so that the caller can run 'test-slider <cmd>' next.
     * There is no any other place to do that in the first cases. If we can't find LTSystemShell,
     * we return true anyway because it may not matter for the second and the third use case.
     */
    s_pShell = (LTSystemShell *)LT_GetCore()->OpenLibrary("LTSystemShell");
    if (!s_pShell) {
        LTLOG_YELLOWALERT("lib.init", "can't add commands to LTSystemShell\n");
    }

    if (s_pShell) {
        s_iShell = (ILTShell *)LT_GetCore()->GetLibraryInterface((LTLibrary *)s_pShell, "ILTShell");
        s_pShell->RegisterCommands(s_TestSliderCommand,
                               sizeof(s_TestSliderCommand) / sizeof(s_TestSliderCommand[0]));
    }
    return true;
}

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceSlider,\
    UnitTestLTDeviceSliderImpl_Run, 768) LTLIBRARY_DEFINITION

LTLIBRARY_EXPORT_INTERFACES(UnitTestLTDeviceSlider, (ITilt))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Feb-22   commodus    created
 *  07-Oct-22   commodus    converted one of the tests for Tilt
 */
