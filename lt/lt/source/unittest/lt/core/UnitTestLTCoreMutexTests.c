/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreMutexTests.c>
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


static int const kTestThreadIterations           = 4;
static u16 const kTestThreadInterval_ms          = 1000;
static u16 const kTestThreadIntervalTolerance_ms = 100;

static LTThread                           s_hThread = 0;
static ILTThread                        * s_iThread = NULL;
static LTMutex                          * s_mutex   = NULL;
static Tilt                             * s_tilt    = NULL;

/*******************************************************************************
 * Mutex test thread.  Acquire the LTMutex, delay, release. */
static void LTMutexTestThread(void * pClientData) {
    LT_UNUSED(pClientData);
    char threadName[kLTThread_MaxNameLen];
    LTThread hThread = s_iThread->GetCurrentThread();
    s_iThread->GetName(hThread, threadName);
    TILT_DEBUG(s_tilt, "mutex.thread.begin: %s", threadName);
    /* Precondition: the mutex has already been acquired. */
    LTTime testThreadIterationInterval = LTTime_Divide(LTTime_Milliseconds(kTestThreadInterval_ms), kTestThreadIterations);
    for (s32 i = 0; i < kTestThreadIterations;) {
        ++i;    /* base the iteration number at 1 */
        /* The mutex is acquired by this thread upon the first iteration,
         * and locks recursively kTestThreadIterations in all: */
        s_mutex->API->Lock(s_mutex);   /* (A) */
        TILT_DEBUG(s_tilt, "mutex.thread.lock.thread: %s", threadName);
        TILT_REPORT_INTEGER(s_tilt, "mutex.thread.lock.n", i);
    }
    for (s32 i = kTestThreadIterations; i; --i) {
        s_iThread->Sleep(testThreadIterationInterval);
        TILT_DEBUG(s_tilt, "mutex.thread.unlock.thread: %s", threadName);
        TILT_REPORT_INTEGER(s_tilt, "mutex.thread.unlock.n", i);
        /* Unwind all kTestThreadIterations acquisitions of the mutex.
         * The mutex is released upon the last iteration: */
        s_mutex->API->Unlock(s_mutex); /* (B) */
    }
    TILT_DEBUG(s_tilt, "mutex.thread.end: %s", threadName);
}

/*******************************************************************************
 * Main LTMutex *test. Allow the test thread to grab the LTMutex, then attempt to
 * grab (TryLock()), which should fail, then block on the LTMutex.  The total
 * duration should be close() to the delay in the test thread.
 * Finally, attempt Trylock() again after acquisition and release. */
static void testLTMutex(void) {
    /* Precondition: the mutex has already been acquired (by TestLTMutex()'s call to TryLock()). */
    /* As soon as we unlock the mutex the test thread will acquire it because it is higher priority then us
       and we won't be able to TryLock it */
    LTTime begin = LT_GetCore()->GetKernelTime();
    s_mutex->API->Unlock(s_mutex); /* (A) */
    TILT_EXPECT_TRUE(s_tilt, !s_mutex->API->TryLock(s_mutex),"mutex.trylock");
    s_mutex->API->Lock(s_mutex);  /* (B) */
    LTTime end = LT_GetCore()->GetKernelTime();
    LTTime elapsed = LTTime_Subtract(end, begin);
    enum { kStrBufSize = 80 };
    char tyme[kStrBufSize];
    TILT_DEBUG(s_tilt, "mutex.lock.begin: %s", timeWithUnits(tyme, kStrBufSize, &begin));
    TILT_DEBUG(s_tilt, "mutex.lock.end: %s", timeWithUnits(tyme, kStrBufSize, &end));
    TILT_DEBUG(s_tilt, "mutex.lock.elapsed: %s", timeWithUnits(tyme, kStrBufSize, &elapsed));
    if (!LTTime_close(elapsed, LTTime_Milliseconds(kTestThreadInterval_ms), LTTime_Milliseconds(kTestThreadIntervalTolerance_ms))) {
        TILT_REPORT_FAILURE(s_tilt, "mutex.acquire.timing: elapsed=%lld, expected=%ld, tolerance=%ld",
                            LTTime_GetMilliseconds(elapsed), LT_Pu32(kTestThreadInterval_ms), LT_Pu32(kTestThreadIntervalTolerance_ms));
    }

    LT_GetCore()->FormatCanonicalTimeString(elapsed, tyme, kStrBufSize, false);
    TILT_REPORT(s_tilt, "mutex.lock.1 in %s seconds", tyme);
    TILT_REPORT(s_tilt, "mutex.unlock.1");
    s_mutex->API->Unlock(s_mutex);
    TILT_REPORT(s_tilt, "mutex.trylock.1");
    TILT_EXPECT_TRUE(s_tilt, s_mutex->API->TryLock(s_mutex), "mutex.trylock");
    TILT_REPORT(s_tilt, "mutex.trylock.2");
    TILT_EXPECT_TRUE(s_tilt, s_mutex->API->TryLock(s_mutex), "mutex.trylock.recursive");
    TILT_REPORT(s_tilt, "mutex.unlock.2");
    s_mutex->API->Unlock(s_mutex); /* release the second recursive lock */
    TILT_REPORT(s_tilt, "mutex.unlock.1");
    s_mutex->API->Unlock(s_mutex); /* release the first recursive lock  */
}

/*******************************************************************************
 * Prepare a LTThread for the LTMutex *test.  If creation succeeds, run the test,
 * then destroy the LTThread.  */
void TestLTMutex(Tilt * tilt) {
    s_tilt = tilt;
    if ((s_hThread = LT_GetCore()->CreateThread("MutexTest"))) {
        s_iThread = lt_gethandleinterface(ILTThread, s_hThread);
        s_iThread->SetStackSize(s_hThread, 1024);

        /* set my thread priority to 4 and the test thread priority to 5 so we don't have to rely on sleep and yield for each thread to hope the other does what it wants */
        u8 currentPriority = s_iThread->GetPriority(s_iThread->GetCurrentThread());
        s_iThread->SetPriority(s_iThread->GetCurrentThread(), 4);
        s_iThread->SetPriority(s_hThread, 5);

        /* create the mutex and lock it with TryLock */
        s_mutex = lt_createobject(LTMutex);
        TILT_EXPECT_TRUE(s_tilt, s_mutex, "mutex.initial.create");
        TILT_EXPECT_TRUE(s_tilt, s_mutex->API->TryLock(s_mutex), "mutex.initial.acquire");

        /* start the test thread and queue a task proc to it; the task proc will run immediately and the thread
           will block trying to lock the mutex we have locked */
        s_iThread->Start(s_hThread, NULL, NULL);
        s_iThread->QueueTaskProc(s_hThread, &LTMutexTestThread, NULL, NULL);

        /* run our test */
        testLTMutex();

        /* destroy the thread and mutex and restore our thread priority */
        s_iThread->Terminate(s_hThread);
        s_iThread->WaitUntilFinished(s_hThread, LTTime_Infinite());
        s_iThread->Destroy(s_hThread);
        s_hThread = 0;

        lt_destroyobject(s_mutex);
        s_mutex = NULL;

        s_iThread->SetPriority(s_iThread->GetCurrentThread(), currentPriority);
    }
    else {
        TILT_REPORT_FAILURE(tilt, "mutex.create.thread");
    }

    s_tilt = NULL;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Aug-19   constantine created
 *  23-Dec-19   constantine changed name from LTTestNoFault to LTUnitTestCore
 *  29-Jul-20   constantine converted to C
 *  19-Aug-20   augustus    replaced LTObject with LTHandle
 *  18-Sep-20   constantine reworked for latest LTThread model (proc queue)
 *  06-Dec-20   constantine reworked printf formatting
 *  19-Oct-21   constantine moved to UnitTestLTCore
 *  02-Aug-22   constantine reworked for TILT
 *  23-May-24   augustus    rewrote to be deterministic
*/
