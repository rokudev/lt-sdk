/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreCountingSemaphoreTests.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTCountingSemaphore.h>
#include <tilt/JiltEngine.h>

static u32 const kDestroyIterations = 2000;
static u16 const kSignalDelay_ms    = 100;
static u16 const kTimeout_ms        = 50;
static u16 const kTolerance_ms      = 20;

static Tilt                         * s_tilt = NULL;
static ILTThread                    * s_iThread = NULL;
static LTCountingSemaphore          * s_go = NULL;
static LTCountingSemaphore * volatile s_cur = NULL;

static LTCountingSemaphore *NewSem(u32 maxCount, u32 initCount) {
    LTCountingSemaphore *sem = lt_createobject(LTCountingSemaphore);
    if (sem) sem->API->Init(sem, maxCount, initCount);
    return sem;
}

/* The worker may still signal a semaphore whose Wait timed out; free it only
 * after the worker thread has finished. */
static LTCountingSemaphore *s_orphans[2];
static u32                  s_orphanCount = 0;

static void Orphan(LTCountingSemaphore *sem) {
    if (s_orphanCount < sizeof(s_orphans) / sizeof(s_orphans[0])) s_orphans[s_orphanCount++] = sem;
}

/*******************************************************************************
 * Count, TryWait, timeout and maxCount behaviour on a single thread. */
static void testBasics(void) {
    LTCountingSemaphore *sem = NewSem(3, 1);
    if (!sem) { TILT_REPORT_FAILURE(s_tilt, "sem.create"); return; }

    TILT_EXPECT_TRUE(s_tilt, sem->API->GetCount(sem) == 1, "sem.init.count");
    TILT_EXPECT_TRUE(s_tilt, sem->API->TryWait(sem), "sem.trywait.take");
    TILT_EXPECT_TRUE(s_tilt, !sem->API->TryWait(sem), "sem.trywait.empty");
    TILT_EXPECT_TRUE(s_tilt, !sem->API->Wait(sem, LTTime_Zero()), "sem.wait.zero");

    LTTime begin = LT_GetCore()->GetKernelTime();
    bool got = sem->API->Wait(sem, LTTime_Milliseconds(kTimeout_ms));
    s64 elapsed = LTTime_GetMilliseconds(LTTime_Subtract(LT_GetCore()->GetKernelTime(), begin));
    TILT_EXPECT_TRUE(s_tilt, !got, "sem.wait.timeout");
    TILT_EXPECT_TRUE(s_tilt, elapsed >= kTimeout_ms - kTolerance_ms,
                     "sem.wait.timeout.elapsed: %lld", LT_Ps64(elapsed));

    for (int i = 0; i < 5; ++i) sem->API->Signal(sem);
    TILT_EXPECT_TRUE(s_tilt, sem->API->GetCount(sem) == 3, "sem.signal.max");
    sem->API->SignalFromThread(sem);
    TILT_EXPECT_TRUE(s_tilt, sem->API->GetCount(sem) == 3, "sem.signalthread.max");
    for (int i = 0; i < 3; ++i) TILT_EXPECT_TRUE(s_tilt, sem->API->Wait(sem, LTTime_Zero()), "sem.wait.drain");
    TILT_EXPECT_TRUE(s_tilt, sem->API->GetCount(sem) == 0, "sem.drain.count");

    lt_destroyobject(sem);
}

/*******************************************************************************
 * Worker: signal the waiter after a delay. */
static void DelayedSignalProc(void *pClientData) {
    LTCountingSemaphore *sem = (LTCountingSemaphore *)pClientData;
    s_iThread->Sleep(LTTime_Milliseconds(kSignalDelay_ms));
    sem->API->SignalFromThread(sem);
}

/*******************************************************************************
 * Worker: signal a fresh semaphore per round; the waiter destroys it as soon
 * as its Wait returns. */
static void SignalAndForgetProc(void *pClientData) {
    LT_UNUSED(pClientData);
    for (u32 i = 0; i < kDestroyIterations; ++i) {
        if (!s_go->API->Wait(s_go, LTTime_Milliseconds(1000))) return;
        LTCountingSemaphore *sem = s_cur;
        sem->API->SignalFromThread(sem);
    }
}

/*******************************************************************************
 * A blocked Wait is woken by another thread. */
static void testBlockingWake(LTThread hThread) {
    LTCountingSemaphore *sem = NewSem(1, 0);
    if (!sem) { TILT_REPORT_FAILURE(s_tilt, "sem.create"); return; }
    if (!s_iThread->QueueTaskProc(hThread, &DelayedSignalProc, NULL, sem)) {
        TILT_REPORT_FAILURE(s_tilt, "sem.queue");
        lt_destroyobject(sem);
        return;
    }
    if (!sem->API->Wait(sem, LTTime_Milliseconds(1000))) {
        TILT_REPORT_FAILURE(s_tilt, "sem.wait.woken");
        Orphan(sem);
        return;
    }
    /* Safe even though the signaller may not have left the monitor yet. */
    lt_destroyobject(sem);
}

/*******************************************************************************
 * Smoke test: destroy each semaphore as soon as Wait returns, with another
 * thread signalling it. The signal/destroy race is timing-dependent and is
 * rarely hit on single-core targets, so a pass does not prove it is absent. */
static void testDestroyAfterWait(LTThread hThread) {
    s_go = NewSem(1, 0);
    if (!s_go) { TILT_REPORT_FAILURE(s_tilt, "sem.create"); return; }
    if (!s_iThread->QueueTaskProc(hThread, &SignalAndForgetProc, NULL, NULL)) {
        TILT_REPORT_FAILURE(s_tilt, "sem.queue");
        return;
    }

    u32 i = 0;
    for (; i < kDestroyIterations; ++i) {
        LTCountingSemaphore *sem = NewSem(1, 0);
        if (!sem) { TILT_REPORT_FAILURE(s_tilt, "sem.create: %lu", LT_Pu32(i)); break; }
        s_cur = sem;
        s_go->API->SignalFromThread(s_go);
        if (!sem->API->Wait(sem, LTTime_Milliseconds(1000))) {
            TILT_REPORT_FAILURE(s_tilt, "sem.destroy.wait: %lu", LT_Pu32(i));
            Orphan(sem);
            break;
        }
        lt_destroyobject(sem);
    }
    TILT_REPORT_INTEGER(s_tilt, "sem.destroy.rounds", i);
}

void TestLTCountingSemaphore(Tilt * tilt) {
    s_tilt = tilt;
    testBasics();

    LTThread hThread = LT_GetCore()->CreateThread("SemTest");
    if (!hThread) {
        TILT_REPORT_FAILURE(tilt, "sem.create.thread");
        s_tilt = NULL;
        return;
    }
    s_iThread = lt_gethandleinterface(ILTThread, hThread);
    s_iThread->SetStackSize(hThread, 1024);
    s_iThread->Start(hThread, NULL, NULL);

    testBlockingWake(hThread);
    testDestroyAfterWait(hThread);

    s_iThread->Terminate(hThread);
    s_iThread->WaitUntilFinished(hThread, LTTime_Infinite());
    s_iThread->Destroy(hThread);
    /* Semaphores abandoned on a timeout are safe to free once the worker is gone. */
    for (u32 i = 0; i < s_orphanCount; ++i) lt_destroyobject(s_orphans[i]);
    s_orphanCount = 0;
    if (s_go) lt_destroyobject(s_go);
    s_go = NULL;
    s_cur = NULL;
    s_tilt = NULL;
}
