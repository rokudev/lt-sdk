/*******************************************************************************
 * lt/source/lt/device/watchdog/ThreadWatchdogManager.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * Implementation of "THREAD WATCHDOG MANAGER"
 *   US Patent Application# 63/614,235
 *
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include "ThreadWatchdogManager.h"

/* _________________________________
 * ThreadWatchdogManager #defines */
DEFINE_LTLOG_SECTION("thread.watchdog");
#define LTWATCHDOGMGR_THREAD_SPECIFIC_CLIENT_DATA_ID    "LTWDMGR"
#define LTWATCHDOGMGR_THREAD_MINIMUM_RESPONSE_FIDELITY  LTTime_Milliseconds(100)

/* ______________________________________
 * ThreadWatchdogManager private types */
typedef_LTENUM_SIZED(ThreadWatchdogManager_WatchFlags, LT_SIZE) {     /* Flags used by the thread watchdog manager to assess thread fidelity */
    kThreadWatchdogManager_WatchFlags_AlwaysSet           = (1 << 0), /* this flag always set so flags as clientData can't appear to be a NULL pointer */
    kThreadWatchdogManager_WatchFlags_FidelityAffirmed    = (1 << 1),
    kThreadWatchdogManager_WatchFlags_TerminationAllowed  = (1 << 2)
};

typedef enum ThreadWatchdogManager_RebootReason {
    kThreadWatchdogManager_RebootReason_FidelityBreached = 0,
    kThreadWatchdogManager_RebootReason_TerminationDisallowed,
} ThreadWatchdogManager_RebootReason;

/* _________________________________________________
 * ThreadWatchdogManager private static variables */
static LTOThread                          * s_watcherThread = NULL;
static ThreadWatchdogManager_HardBootProc * s_pHardBootProc = NULL;

/* _____________________________________________
 * ThreadWatchdogManager forward declarations */
static void ThreadWatchdogManager_ThreadFidelityTimerProc(void *pClientData);

/* _________________________________________________
 * ThreadWatchdogManager private static functions */
static LT_NOINLINE void ThreadWatchdogManager_ThreadBreachReboot(ThreadWatchdogManager_RebootReason reason, LTThread hThread) {
    char threadName[kLTThread_MaxNameBuff];
    lt_getlibraryinterface(ILTThread, LT_GetCore())->GetName(hThread, threadName);
    switch (reason) {
        case kThreadWatchdogManager_RebootReason_FidelityBreached:
            LTLOG_REDALERT("fidelity.breach", "%s unresponsive, rebooting.", threadName);
            break;
        case kThreadWatchdogManager_RebootReason_TerminationDisallowed:
            LTLOG_REDALERT("termination.breach", "%s unexpectedly terminated, rebooting.", threadName);
            break;
        default:
            LT_ASSERT(0);
            break;
    }
    LT_GetCore()->FlushConsoleOutput();
    /* the flush should have worked but on some platforms (e.g. esp32) the hardware has a fifo after our flush; give those some time to get our characters out */
    LTTime busyWaitStopTime = LTTime_Add(LT_GetCore()->GetKernelTime(), LTTime_Milliseconds(20));
    while (LTTime_IsLessThan(LT_GetCore()->GetKernelTime(), busyWaitStopTime)) { }
    (*s_pHardBootProc)();
}

static void ThreadWatchdogManager_WatchFlagsThreadSpecificClientDataReleaseProc(LTThread_ReleaseReason releaseReason, void *pClientData) {
    if (releaseReason == kLTThread_ReleaseReason_ThreadSpecificPurge) {
        /* watched thread is going away */
        LTThread hThread = lt_getlibraryinterface(ILTThread, LT_GetCore())->GetCurrentThread();
        if (((LT_SIZE)pClientData) & kThreadWatchdogManager_WatchFlags_TerminationAllowed) {
            // thread is going away and termination is allowed, kill the timer for this thread
            s_watcherThread->API->KillTimer(s_watcherThread, &ThreadWatchdogManager_ThreadFidelityTimerProc, LTHANDLE_TO_VOIDPTR(hThread));
        }
        else {
            /* termination not allowed; reboot the system */
            ThreadWatchdogManager_ThreadBreachReboot(kThreadWatchdogManager_RebootReason_TerminationDisallowed, hThread);
        }
    }
}

static void ThreadWatchdogManager_SetThreadWatchFlags(LTThread hThread, LT_SIZE flags) {
    lt_getlibraryinterface(ILTThread, LT_GetCore())->SetThreadSpecificClientData(hThread, LTWATCHDOGMGR_THREAD_SPECIFIC_CLIENT_DATA_ID, (flags && (0 == (flags & kThreadWatchdogManager_WatchFlags_TerminationAllowed))) ? &ThreadWatchdogManager_WatchFlagsThreadSpecificClientDataReleaseProc : NULL, (void *)flags);
}

static LT_SIZE ThreadWatchdogManager_GetThreadWatchFlags(LTThread hThread) {
    return (LT_SIZE)lt_getlibraryinterface(ILTThread, LT_GetCore())->GetThreadSpecificClientData(hThread, LTWATCHDOGMGR_THREAD_SPECIFIC_CLIENT_DATA_ID);
}

static void ThreadWatchdogManager_AffirmThreadFidelityTaskProc(void *pClientData) { LT_UNUSED(pClientData);
    LTThread hThread = VOIDPTR_TO_LTHANDLE(pClientData);
    LT_SIZE flags = ThreadWatchdogManager_GetThreadWatchFlags(hThread);
    if (flags) ThreadWatchdogManager_SetThreadWatchFlags(hThread, flags | kThreadWatchdogManager_WatchFlags_FidelityAffirmed);
}

static void ThreadWatchdogManager_FidelityTimerClientDataReleaseProc(LTThread_ReleaseReason releaseReason, void *pClientData) {
    if (releaseReason == kLTThread_ReleaseReason_TimerPurge) {
        /* s_watcherThread is going away (watchdog lib is closing), clear thread specific client data on watched threads */
        LTThread hThread = VOIDPTR_TO_LTHANDLE(pClientData);
        ThreadWatchdogManager_SetThreadWatchFlags(hThread, 0);
    }
}

static void ThreadWatchdogManager_ThreadFidelityTimerProc(void *pClientData) {
    LTThread hThread = VOIDPTR_TO_LTHANDLE(pClientData);
    LT_SIZE flags = ThreadWatchdogManager_GetThreadWatchFlags(hThread);
    if (flags) {
        if (flags & kThreadWatchdogManager_WatchFlags_FidelityAffirmed) {
            // reset affirmation flag and queue another affirmation
            ThreadWatchdogManager_SetThreadWatchFlags(hThread, flags & ~kThreadWatchdogManager_WatchFlags_FidelityAffirmed);
            lt_getlibraryinterface(ILTThread, LT_GetCore())->QueueTaskProc(hThread, &ThreadWatchdogManager_AffirmThreadFidelityTaskProc, NULL, pClientData);
        }
        else {
            ThreadWatchdogManager_ThreadBreachReboot(kThreadWatchdogManager_RebootReason_FidelityBreached, hThread);
        }
    }
    else {
        s_watcherThread->API->KillTimer(s_watcherThread, &ThreadWatchdogManager_ThreadFidelityTimerProc, pClientData);
    }
}

/* __________________________________________________________________
 * ThreadWatchdogManager functions for LTDeviceWatchdog public API */
void ThreadWatchdogManager_WatchThread(LTTime responseFidelity, bool bTerminationAllowed) {
    if (LTTime_IsZero(responseFidelity)) {
        if (bTerminationAllowed) { LTLOG("watchthread.params", "WatchThread requires non-zero responseFidelity and/or false bTerminationAllowed"); return; }
    }
    else if (LTTime_IsLessThan(responseFidelity, LTWATCHDOGMGR_THREAD_MINIMUM_RESPONSE_FIDELITY)) {
        char timeBuff[24]; LT_GetCore()->FormatCanonicalTimeString(LTWATCHDOGMGR_THREAD_MINIMUM_RESPONSE_FIDELITY, timeBuff, sizeof(timeBuff), false);
        LTLOG("watchthread.fidelity.toosmall", "WatchThread responseFidelity must be at least %ss", timeBuff); return;
    }

    LTThread hThread = lt_getlibraryinterface(ILTThread, LT_GetCore())->GetCurrentThread();
    if (hThread && hThread != s_watcherThread->API->GetThreadHandle(s_watcherThread) && (0 == ThreadWatchdogManager_GetThreadWatchFlags(hThread))) {
        LT_SIZE flags = kThreadWatchdogManager_WatchFlags_AlwaysSet | (bTerminationAllowed ? kThreadWatchdogManager_WatchFlags_TerminationAllowed : 0);
        ThreadWatchdogManager_SetThreadWatchFlags(hThread, flags);
        if (! LTTime_IsZero(responseFidelity)) {
            lt_getlibraryinterface(ILTThread, LT_GetCore())->QueueTaskProc(hThread, &ThreadWatchdogManager_AffirmThreadFidelityTaskProc, NULL, LTHANDLE_TO_VOIDPTR(hThread));
            s_watcherThread->API->SetTimer(s_watcherThread, responseFidelity, &ThreadWatchdogManager_ThreadFidelityTimerProc, &ThreadWatchdogManager_FidelityTimerClientDataReleaseProc, LTHANDLE_TO_VOIDPTR(hThread));
        }
    }
}

void ThreadWatchdogManager_UnwatchThread(void) {
    LTThread hThread = lt_getlibraryinterface(ILTThread, LT_GetCore())->GetCurrentThread();
    if (hThread) {
        s_watcherThread->API->KillTimer(s_watcherThread, &ThreadWatchdogManager_ThreadFidelityTimerProc, LTHANDLE_TO_VOIDPTR(hThread));
        ThreadWatchdogManager_SetThreadWatchFlags(hThread, 0);
    }
}

/* _________________________________________________
 * ThreadWatchdogManager initialization functions */
bool ThreadWatchdogManager_Initialize(ThreadWatchdogManager_HardBootProc * pHardBootProc) {
    if (NULL == (s_pHardBootProc = pHardBootProc)) return false;
    s_watcherThread = lt_createobject(LTOThread);
    s_watcherThread->API->SetPriority(s_watcherThread, kLTThread_PriorityHighest);
    s_watcherThread->API->Start(s_watcherThread, "WatchdogMgr", NULL, NULL);
    return true;
}

void ThreadWatchdogManager_Finalize(void) {
    lt_destroyobject(s_watcherThread);
    s_watcherThread = NULL;
    s_pHardBootProc = NULL;
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  22-Apr-25   augustus    created
 */