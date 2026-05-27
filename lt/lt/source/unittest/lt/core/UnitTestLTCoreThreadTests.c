/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreThreadTests.c>
 *    __  __      _ __ ______          __  __  ____________
 *   / / / /___  (_) //_  __/__  _____/ /_/ / /_  __/ ____/___  ________
 *  / / / / __ \/ / __// / / _ \/ ___/ __/ /   / / / /   / __ \/ ___/ _ \
 * / /_/ / / / / / /_ / / /  __(__  ) /_/ /___/ / / /___/ /_/ / /  /  __/
 * \____/_/ /_/_/\__//_/  \___/____/\__/_____/_/  \____/\____/_/   \___/
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <tilt/JiltEngine.h>
#include "UnitTestLTCoreHelpers.h"

typedef void (UnitTestLTCoreThreadTests_NonStartedThreadProc)(LTThread hT);

static LTThread         s_hThread = 0;
static ILTThread      * s_iThread = NULL;
static Tilt           * s_tilt = NULL;

/*******************************************************************************
 * Test thread name. */

static char * s_threadName = "LTThreadTest";

static void threadNameTest(LTThread hT) { LT_UNUSED(hT);
    char threadName[kLTThread_MaxNameLen];
    s_iThread->GetName(s_hThread, threadName);
    if (!strequ(s_threadName, threadName)) {
        TILT_REPORT_FAILURE(s_tilt, "thread.name: value='%s', expected='%s'", threadName, s_threadName);
    }
}

/*******************************************************************************
 * Test priority-related methods. */

static void threadPriorityTest(LTThread hT) {
    /* Default priority is kLTThread_PriorityDefault.
     * Default priority is non-realtime.
     * Allowable priority is 0-30.
     * 1. Check default priority (s/b 0).
     * 2. Set priority to something else.
     * 3. Check priority (s/b that).
     * 4. Set priority to lowest.
     * 5. Check priority (s/b 0).
     * PRECONDITION:
     *   This function assumes that the thread's priority has not been touched
     *   since the thread was created. */
    u8 priority = s_iThread->GetPriority(hT);
    TILT_EXPECT_TRUE(s_tilt, priority == 0, "thread.priority.actual: priority=%d", priority);
    priority = 6;
    s_iThread->SetPriority(hT, priority);
    u8 newPriority = s_iThread->GetPriority(hT);
    TILT_EXPECT_TRUE(s_tilt, newPriority == priority, "thread.priority.set.value: priority=%d", newPriority);
    s_iThread->SetPriority(hT, kLTThread_PriorityLowest);
    newPriority = s_iThread->GetPriority(hT);
    TILT_EXPECT_TRUE(s_tilt, newPriority == 0, "thread.priority.nrt.value: priority=%d", newPriority);
}

/*******************************************************************************
 * Test stack-size-related methods. */
static void threadStackSizeTest(LTThread hT) {
    /* 1. Perform a sanity check on the default stack size.
     * 2. Get the thread's stack size and check against default.
     * 3. Decrease the stack size.
     * 4. Check the new stack size.
     * PRECONDITION:
     *   This function assumes that the thread's priority has not been touched
     *   since the thread was created. */
    static u32 const maxDefaultStackSize = 2 * 1024 * 1024;
    u32 defaultStackSize = s_iThread->GetDefaultStackSize();
    TILT_DEBUG(s_tilt, "thread.stack.default: %lu", defaultStackSize);
    if (defaultStackSize > maxDefaultStackSize) {   /* 1. */
        TILT_REPORT_FAILURE(s_tilt, "thread.stack.default.sanity: max=%lu", maxDefaultStackSize);
    }
    u32 stackSize = s_iThread->GetStackSize(hT);
    TILT_DEBUG(s_tilt, "thread.stack.default.actual: %lu",stackSize);
    if (stackSize != defaultStackSize) {    /* 2. */
        TILT_REPORT_FAILURE(s_tilt, "thread.stack.default: actual=%lu", stackSize);
    }
    stackSize -= 32;
    s_iThread->SetStackSize(hT, stackSize);   /* 3. */
    u32 newStackSize = s_iThread->GetStackSize(hT);
    if (newStackSize != stackSize) {        /* 4. */
        TILT_REPORT_FAILURE(s_tilt, "thread.stack.size: value=%lu", newStackSize);
    }
}

static LTAtomic s_nBusyCounter    = { 0 };
//#define BUSY_COUNTER_MAX            (10000000)
#define BUSY_COUNTER_MAX            (100000)

static bool BusyHighPriorityThread(void) {
    while (LTAtomic_Load(&s_nBusyCounter) < BUSY_COUNTER_MAX) LTAtomic_FetchAdd(&s_nBusyCounter, 1);
    TILT_DEBUG(s_tilt, "thread.busy.done: %lu", LT_Pu32(LTAtomic_Load(&s_nBusyCounter)));
    return false; /* return false from this thread init will auto terminate the thread without me having to do it manually, yay! */
}

// Spawns a higher-priority busy thread that should get, and maintain, execution time until done.
static void threadBusyHighPriorityThread(LTThread hT) {
    // Be sure we're a real-time thread.
    LTThread hSelfT = s_iThread->GetCurrentThread();
    if (!hSelfT) {
        TILT_REPORT_FAILURE(s_tilt, "self.thread.pointer: fn='%s'", __FUNCTION__);
        return;
    }
    s_iThread->SetPriority(hSelfT, kLTThread_PriorityHighest - 1);
    s_iThread->SetStackSize(hT, 1024);
    s_iThread->SetPriority(hT, kLTThread_PriorityHighest);
    LT_GetCore()->FlushConsoleOutput(); /* starting the highest priority thread that will starve logger output thread; flush first */
    s_iThread->Start(hT, &BusyHighPriorityThread, NULL);
    // At this point we should only get execution-time when the busy task is terminated.
    // note this only works because we force all threads to run on the same processor core; in the future spa
    // we'll run threads across all cores and be core coherent in the hizzy
    TILT_ASSERT_TRUE(s_tilt, LTAtomic_Load(&s_nBusyCounter) == BUSY_COUNTER_MAX, "high priority should run to end but only ran to %u", LT_Pu32(LTAtomic_Load(&s_nBusyCounter)));
    s_iThread->WaitUntilFinished(hT, LTTime_Infinite());
    s_iThread->SetPriority(hSelfT, kLTThread_PriorityDefault);
}

/**
 * Helper function for running thread tests.
 * It creates and destroys the thread under test, and manages static variables.
 */
static void ExecuteThreadTest(Tilt * tilt, UnitTestLTCoreThreadTests_NonStartedThreadProc * pTest) {
    s_tilt = tilt;
    s_hThread = LT_GetCore()->CreateThread(s_threadName);
    if (s_hThread) {
        s_iThread = lt_gethandleinterface(ILTThread, s_hThread);
        TILT_DEBUG(s_tilt, "thread.created: %s", s_threadName);
        pTest(s_hThread);
        s_iThread->Destroy(s_hThread);
    } else {
        TILT_REPORT_FAILURE(s_tilt, "thread.creation");
    }

    s_hThread = 0;
    s_iThread = NULL;
    s_tilt    = NULL;
}

/*******************************************************************************
 * Create a LTThread and test. */

void TestLTThread(Tilt * tilt) {
    UnitTestLTCoreThreadTests_NonStartedThreadProc* pTests[] = {
        threadNameTest,
        threadPriorityTest,
        threadStackSizeTest,
        NULL,
    };

    for (u32 i = 0; pTests[i]; i++) {
        ExecuteThreadTest(tilt, pTests[i]);
    }
}

/*************************************************************************************
 * Ensure that a higher-priority thread is not preempted by a lower-priority thread. */

void TestThreadPriority(Tilt * tilt) {
    LTAtomic_Store(&s_nBusyCounter, 0);
    ExecuteThreadTest(tilt, threadBusyHighPriorityThread);
}

/*************************************************************************************
 * Test Wait Until Finished API and associated timeout. */

static char * s_pTimeString;
static const char * TimeString1(LTTime time) { LT_GetCore()->FormatCanonicalTimeString(time, s_pTimeString,      24, true); return s_pTimeString; }
static const char * TimeString2(LTTime time) { LT_GetCore()->FormatCanonicalTimeString(time, s_pTimeString + 24, 24, true); return s_pTimeString + 24; }

static void MyTimedExitProc(void * pClientData) {
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    thread->API->KillTimer(thread, &MyTimedExitProc, pClientData);
    thread->API->Terminate(thread);
}

void TestThreadWait(Tilt * tilt) {
    enum {
        kTimedExitTimerMS   = 750,
        kWaitTimeoutMS      = 500,
        kMaxSlopMS          = 5,
    };

    /* Test Wait Until Finished WITH timeout */

    LTOThread *thread = lt_createobject(LTOThread);
    thread->API->SetPriority(thread, kLTThread_PriorityHighest);

    LTTime timeStart = LT_GetCore()->GetKernelTime();

    thread->API->Start(thread, "WaitThread", NULL, NULL);
    thread->API->SetTimer(thread, LTTime_Milliseconds(kTimedExitTimerMS), &MyTimedExitProc, NULL, NULL);
    TILT_EXPECT_FALSE(tilt, thread->API->WaitUntilFinished(thread, LTTime_Milliseconds(kWaitTimeoutMS)), "WaitTest: should return false");

    LTTime timeActual = LTTime_Subtract(LT_GetCore()->GetKernelTime(), timeStart);
    LTTime timeSlop   = LTTime_Subtract(timeActual, LTTime_Milliseconds(kWaitTimeoutMS));
    if (timeSlop.nNanoseconds < 0) timeSlop.nNanoseconds = 0 - timeSlop.nNanoseconds;

    if (NULL != (s_pTimeString = lt_malloc(48))) {
        TILT_REPORT(tilt, "WaitTest.1:   timeExpected%s,       timeActual%s", TimeString1(LTTime_Milliseconds(kWaitTimeoutMS)), TimeString2(timeActual));
    }

    TILT_EXPECT_TRUE(tilt, LTTime_IsLessThan(timeSlop, LTTime_Milliseconds(kMaxSlopMS)), "WaitTest: slop greater than allowed");
    TILT_EXPECT_TRUE(tilt, thread->API->WaitUntilFinished(thread, LTTime_Infinite()), "WaitTest: should return true");

    lt_destroyobject(thread);

    /* Test Wait Until Finished WITHOUT timeout */

    thread = lt_createobject(LTOThread);
    thread->API->SetPriority(thread, kLTThread_PriorityHighest);

    timeStart = LT_GetCore()->GetKernelTime();
    thread->API->Start(thread, "WaitThread", NULL, NULL);
    thread->API->SetTimer(thread, LTTime_Milliseconds(kTimedExitTimerMS), &MyTimedExitProc, NULL, NULL);

    TILT_EXPECT_TRUE(tilt, thread->API->WaitUntilFinished(thread, LTTime_Infinite()), "WaitTest: should return true");

    timeActual = LTTime_Subtract(LT_GetCore()->GetKernelTime(), timeStart);
    timeSlop   = LTTime_Subtract(timeActual, LTTime_Milliseconds(kTimedExitTimerMS));
    if (timeSlop.nNanoseconds < 0) timeSlop.nNanoseconds = 0 - timeSlop.nNanoseconds;

    TILT_EXPECT_TRUE(tilt, LTTime_IsLessThan(timeSlop, LTTime_Milliseconds(kMaxSlopMS)), "WaitTest: slop greater than allowed");

    if (s_pTimeString) {
        TILT_REPORT(tilt, "WaitTest.2:   timeExpected%s,       timeActual%s", TimeString1(LTTime_Milliseconds(kTimedExitTimerMS)), TimeString2(timeActual));
        lt_free(s_pTimeString); s_pTimeString = NULL;
    }

    lt_destroyobject(thread);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Aug-19   constantine created
 *  23-Dec-19   constantine changed name from LTTestNoFault to LTUnitTestCore
 *  20-Jul-20   constantine converted to C
 *  19-Sep-20   constantine reworked for latest LTThread model (proc queue)
 *  06-Dec-20   constantine reworked printf formatting
 *  19-Oct-21   constantine moved to UnitTestLTCore
 *  02-Aug-22   constantine reworked for TILT
 *  03-Jun-24   augustus    #ifdef'd out complicated timer test; replaced with simpler test, mostly deterministic
 */
