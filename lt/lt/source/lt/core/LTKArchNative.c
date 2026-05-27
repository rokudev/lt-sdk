/******************************************************************************
 * <lt/core/LTArchNative.c>                                        LTArchNative
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTCoreImpl.h"
#include "LTThreadImpl.h"
#include "LTHandle.h"
#include "lt/core/LTMonitor.h"

declare_LTTRACE_STREAM(mutex);

/*______________________________________
  LTMutex object private data members */
typedef_LTObjectImpl(LTMutex, LTMutexImpl) {
    LTKMutex ltkMutex;
} LTOBJECT_API;

/*________________________________
  LTMutex object constructors */
static bool LTMutexImpl_ConstructObject(LTMutexImpl *mutex) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTKMutexInitialize(&mutex->ltkMutex);
    return true;
}

static void LTMutexImpl_DestructObject(LTMutexImpl *mutex) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (mutex) {
        LTKMutexFinalize(&mutex->ltkMutex);
    }
}

/*_______________________________
  LTMutex object api functions */
static void
LTMutexImpl_Lock(LTMutexImpl *mutex) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTTRACE_NUMERIC(mutex, 0);
    if (!mutex) return;
    LTThread hThread = LTThreadImpl_GetCurrentThread();
    LTThreadImpl * pImpl = LTThreadImpl_PrivateDataToThreadImpl(LTHandle_ReservePrivateData(hThread));
    if (! pImpl) { /* if not LT Thread can't twiddle thread state; still lock mutex */
        LTKMutexLock(&mutex->ltkMutex);
        return;
    }
    /* change the thread state when it is stable with respect to our reading it (another thread may have
       called Terminate in between our reading the thread state and trying to set it).
       if ever the thread state is TerminatePending or Terminated (thanks BSP), lock the mutex and return */
    u32 threadState;
    do {
        threadState = LTAtomic_Load(&pImpl->nThreadState);
        if ((kLTThread_ThreadState_TerminatePending == threadState) || (kLTThread_ThreadState_Terminated == threadState)) {
            LTKMutexLock(&mutex->ltkMutex);
            LTHandle_ReleasePrivateData(hThread, LTThreadImpl_ThreadImplToPrivateData(pImpl));
            return;
        }
    }
    while (! LTAtomic_CompareAndExchange(&pImpl->nThreadState, threadState, kLTThread_ThreadState_MutexBlocked));

    // ok we changed threadState to kLTThread_ThreadState_MutexBlocked; lock the mutex (phew!)
    LTKMutexLock(&mutex->ltkMutex);

    // restore the thread state back but only if it didn't get changed by another thread
    // (e.g. to TerminatePending) while we were blocking
    LTAtomic_CompareAndExchange(&pImpl->nThreadState, kLTThread_ThreadState_MutexBlocked, threadState);
    LTHandle_ReleasePrivateData(hThread, LTThreadImpl_ThreadImplToPrivateData(pImpl));
}

static void
LTMutexImpl_Unlock(LTMutexImpl *mutex) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTTRACE_NUMERIC(mutex, 3);
    if (mutex) {
        LTKMutexUnlock(&mutex->ltkMutex);
    }
}

static bool
LTMutexImpl_TryLock(LTMutexImpl *mutex) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    bool bRetVal = false;
    if (mutex) {
        bRetVal = LTKMutexTryLock(&mutex->ltkMutex);
        LTTRACE_NUMERIC(mutex, bRetVal ? 1 : 2);
    }
    return bRetVal;
}

/*_________________________________
  LTMutex LTObjectApi definition */
define_LTObjectImplPublic(LTMutex, LTMutexImpl,
    Lock, TryLock, Unlock,
);

/*________________________________________
  LTMonitor object private data members */
typedef_LTObjectImpl(LTMonitor, LTMonitorImpl) {
    LTKMonitor ltkMonitor;
} LTOBJECT_API;

/*________________________________
  LTMonitor object constructors */
static bool LTMonitorImpl_ConstructObject(LTMonitorImpl *monitor) {
    LTKMonitorInitialize(&monitor->ltkMonitor);
    return true;
}

static void LTMonitorImpl_DestructObject(LTMonitorImpl *monitor) {
    LTKMonitorFinalize(&monitor->ltkMonitor);
}

/*_________________________________
  LTMonitor object api functions */
static void LTMonitorImpl_Enter(LTMonitorImpl *monitor) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    u32 priorThreadState = LTThreadImpl_IngressBlockingThreadState(kLTThread_ThreadState_MutexBlocked);
    LTKMonitorEnter(&monitor->ltkMonitor);
    LTThreadImpl_EgressBlockingThreadState(kLTThread_ThreadState_MutexBlocked, priorThreadState);
}

static void LTMonitorImpl_Exit(LTMonitorImpl *monitor) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTKMonitorExit(&monitor->ltkMonitor);
}

static bool LTMonitorImpl_Wait(LTMonitorImpl *monitor, LTTime timeout) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    u32 priorThreadState = LTThreadImpl_IngressBlockingThreadState(kLTThread_ThreadState_WaitBlocked);
    bool bRetVal = LTKMonitorWait(&monitor->ltkMonitor, timeout.nNanoseconds);
    LTThreadImpl_EgressBlockingThreadState(kLTThread_ThreadState_WaitBlocked, priorThreadState);
    return bRetVal;
}

static void LTMonitorImpl_Notify(LTMonitorImpl *monitor) LT_ISR_SAFE {
    LTKMonitorNotify(&monitor->ltkMonitor);
}

/*___________________________________
  LTMonitor LTObjectApi definition */
define_LTObjectImplPublic(LTMonitor, LTMonitorImpl,
    Enter, Exit, Wait, Notify
);

