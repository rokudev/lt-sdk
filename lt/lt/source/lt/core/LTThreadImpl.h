/******************************************************************************
 * lt/source/core/LTThreadImpl.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_CORE_LTTHREADIMPL_H
#define ROKU_LT_SOURCE_LT_CORE_LTTHREADIMPL_H

#include <lt/core/LTCore.h>
#include "LTCoreDebugHelpers.h"

/*************************
 * forward declarations */
struct LTCoreBSP;

/**************************
 * LTThread Priority enum */
enum {
    kLTThread_ClientPriorityHighest             = kLTThread_PriorityHighest,  /* From API */
    kLTThread_PrivatePriorityHighest            = 31,  /* LTCore reserves the highest priority */
    kLTThread_PrivateNumberOfPriorities         = 32   /* Total number of client and private priorities */
};

/***********************
 * LTThread Flags enum */
enum {
    kLTThread_FlagsCanPostFromISR               = (1 << 0),
    kLTThread_FlagsIsLTCoreSystemThread         = (1 << 1),
    kLTThread_FlagsUnregisterCurrentEvent       = (1 << 2),
    kLTThread_FlagsProhibitLogging              = (1 << 3),
    kLTThread_FlagsSystemThreadIsTimeCritical   = (1 << 4),
    kLTThread_FlagsHasSofwareWakeupTimer        = (1 << 5),
    kLTThread_FlagsThreadInitComplete           = (1 << 6),
    kLTThread_FlagsThreadInitResult             = (1 << 7),
    kLTThread_ThreadNumberShift                 = 8, /* up to 8 bits of flags, then 24 high bits of threadID (for debugging only) */
    kLTThread_ThreadFlagsMask                   = 0xFF
};

typedef_LTObjectImpl(LTOThread, LTOThreadImpl) {
    LTThread  hThread;
} LTOBJECT_API;
LT_STATIC_ASSERT_SIZE_32_64(LTOThreadImpl, 16, 24)


 typedef struct LTThreadImpl {
    /* for any given struct we would like to order elements largest to smallest and try to always assemble all 32 bit values and all pointers in pairs of 2
       so that two together make 64 even bits and in the case of pointers or LT_SIZE on 64 bit systems 2 together will make 128 bits - we want to always land
       on a multiple of 64 bits irrespective of 32 or 64 bit architecture, e.g. { void *, u32, void *, u32 } would align on a 32 bit system but introduce
       32 bits of padding after each u32 on a 64 bit system.  Changing the struct layout to { void *, void *, u32, u32 } would introduce zero padding
       on 32 and 64 bit systems.  Ideally the total structure size would be a multiple of 64 bits since malloc on most systems, not knowing if you intend
       to dereference a 64-bit quantity at the start of the allocation, will align on 64 bit boundaries irrespective of 32 or 64 bit architecture.
      */
    LTOThreadImpl                            threadObject;                  // size 16 or 24 total  16 or  24
    struct LTThreadImpl                     *pPrev;                         // size  4 or  8 total  20 or  32
    struct LTThreadImpl                     *pNext;                         // size  4 or  8 total  24 or  40
    struct ThreadSpecificClientDataRecord   *pThreadSpecificClientDataHead; // size  4 or  8 total  28 or  48
    LTThread_InitProc                       *pInitProc;                     // size  4 or  8 total  32 or  56
    LTThread_ExitProc                       *pExitProc;                     // size  4 or  8 total  36 or  64
    struct TimerRecord                      *pFirstTimerRecord;             // size  4 or  8 total  40 or  72
    struct ISRTaskProcRecord                *pFirstISRTaskProcRecord;       // size  4 or  8 total  44 or  80
    struct TaskProcRecord                   *pTaskProcQueueHead;            // size  4 or  8 total  48 or  88
    struct TaskProcRecord                   *pTaskProcQueueTail;            // size  4 or  8 total  52 or  96
    void                                    *pMonitor;                      // size  4 or  8 total  56 or 104
    LTThread_TimerProc                      *pTimerProcInProgress;          // size  4 or  8 total  60 or 112
    void                                    *pTimerClientDataInProgress;    // size  4 or  8 total  64 or 120
    LTOThread                               *pThreadObject;                 // size  4 or  8 total  68 or 128
    LTHandle                                 hThread;                       // size  4       total  72 or 132
    LTAtomic                                 nThreadState;                  // size  4       total  76 or 136
    LTAtomic                                 nThreadFlags;                  // size  4       total  80 or 140
    LTAtomic                                 nStackSize;                    // size  4       total  84 or 144
    LTAtomic                                 nCurrHeapUsage;                // size  4       total  88 or 148
    LTAtomic                                 nMaxHeapUsage;                 // size  4       total  92 or 152
    LTAtomic                                 nPriority;                     // size  4       total  96 or 156
    u32                                      nTaskProcRecordsQueued;        // size  4       total 100 or 160
    char                                     name[kLTThread_MaxNameBuff];   // size 20       total 120 or 180
} LTThreadImpl;                                                             //         grand total 120 or 184 (with end pad)

/* now let's see if I'm correct */
LT_STATIC_ASSERT_SIZE_32_64(LTThreadImpl, 120, 184)

/*******************************************************
 * Functions that go into the LTCore Public Interface */
LTThread                 LTThreadImpl_CreateThread(const char * pName);

/***********************************************************
 * LTCore Library Private LTThreadImpl utility functions */
void                     LTThreadImpl_Init(void);
void                     LTThreadImpl_Fini(void);
LTThread                 LTThreadImpl_CreateCoreThread(const char *pName, u32 nStackSize, u32 nPriority);
void                     LTThreadImpl_AcceptBSP(const struct LTCoreBSP * pBSP);
void                     LTThreadImpl_RelinquishBSP(void);
void                     LTThreadImpl_NotifyShutdown(void);
void                     LTThreadImpl_WaitForShutdownOrAllThreadsToFinish(void);
void                     LTThreadImpl_WaitForAllThreadsToFinish(void);
void                     LTThreadImpl_TerminateAllNonSystemThreads(void);
bool                     LTThreadImpl_ThreadHandleInActiveRunState(LTThread hThread);
LTTime                   LTThreadImpl_GetSoonestFiringThreadWakeupTimerFireTime(void);
void                     LTThreadImpl_SetThreadFlags(LTThread hThread,  u32 nFlags);
LTThread_ThreadState     LTThreadImpl_GetCurrentThreadState(void);
void                     LTThreadImpl_SetCurrentThreadState(LTThread_ThreadState threadState);
bool                     LTThreadImpl_IsUnregisterCurrentEventFlagSet(void);
void                     LTThreadImpl_SetUnregisterCurrentEventFlag(bool bSet);
bool                     LTThreadImpl_IsProhibitLoggingFlagSet(void);
void                     LTThreadImpl_SetProhibitLoggingFlag(bool bSet);
bool                     LTThreadImpl_FetchSetProhibitLoggingFlag(bool bSet);
bool                     LTThreadImpl_IsProhibitLoggingFlagSet(void);

void                     LTThreadImpl_IncreaseHeapAllocatedByteCount(LTThread hThread, u32 nBytes);
void                     LTThreadImpl_ReduceHeapAllocatedByteCount(LTThread hThread, u32 nBytes);

LT_SIZE                  LTThreadImpl_GetThreadBSPSize(void);

#ifndef LTTHREADIMPL_INTERNAL
LT_INLINE LTThreadImpl * LTThreadImpl_PrivateDataToThreadImpl(void *privateData)        { return privateData ? (LTThreadImpl *)(((u8 *)privateData) + LTThreadImpl_GetThreadBSPSize()) : NULL; }
LT_INLINE void *         LTThreadImpl_ThreadImplToPrivateData(LTThreadImpl *pImpl)      { return pImpl ? (void *)(((u8 *)pImpl) - LTThreadImpl_GetThreadBSPSize()) : NULL; }
#endif

u32                      LTThreadImpl_IngressBlockingThreadState(u32 blockingThreadState);
void                     LTThreadImpl_EgressBlockingThreadState(u32 blockingThreadState, u32 priorThreadState);

/************************************************************************************
 * ILTThread public interface functions (the ones shared directly with LTCoreImpl) */
void                     LTThreadImpl_Start(LTThread hThread, LTThread_InitProc * pInitProc, LTThread_ExitProc * pExitProc);
bool                     LTThreadImpl_StartSynchronous(LTThread hThread, LTThread_InitProc * pInitProc, LTThread_ExitProc * pExitProc);
bool LT_ISR_SAFE         LTThreadImpl_QueueTaskProc(LTThread hThread, LTThread_TaskProc * pTaskProc, LTThread_ClientDataReleaseProc * pClientDataReleseProc, void * pClientData);
bool LT_ISR_SAFE         LTThreadImpl_QueueTaskProcIfRequired(LTThread hThread, LTThread_TaskProc * pTaskProc, LTThread_ClientDataReleaseProc * pClientDataReleseProc, void * pClientData);
void                     LTThreadImpl_GetName(LTThread hThread, char nameBuffToFill[kLTThread_MaxNameBuff]);
void *                   LTThreadImpl_GetThreadSpecificClientData(LTThread hThread, const char * pID);
void                     LTThreadImpl_SetThreadSpecificClientData(LTThread hThread, const char * pID, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData);
void                     LTThreadImpl_SetStackSize(LTThread hThread, u32 nStackSize);
u8                       LTThreadImpl_SetHighestLTCoreThreadPriorityFromLTKIdleTickISRContext(LTThread hThread);
u8                       LTThreadImpl_GetPriority(LTThread hThread);
void                     LTThreadImpl_SetPriority(LTThread hThread, u8 nPriority);
void                     LTThreadImpl_Terminate(LTThread hThread);
bool                     LTThreadImpl_WaitUntilFinished(LTThread hThread, LTTime timeout);
LTThread                 LTThreadImpl_GetCurrentThread(void);
void                     LTThreadImpl_Sleep(LTTime sleepTime);
void                     LTThreadImpl_Yield(void);
u32                      LTThreadImpl_GetDefaultStackSize(void);
LTOThread *              LTOThreadImpl_GetCurrentThread(void);

#endif // #ifndef ROKU_LT_SOURCE_LT_CORE_LTTHREADIMPL_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Aug-19   augustus    created
 *  19-Aug-19   augustus    added ResetTimer() functions
 *  06-Sep-19   augustus    axed PostMessageFromInterruptHandler(); now PostMessage() handles both cases
 *  21-Oct-19   augustus    added bCanPostFromISR
 *  24-Nov-19   augustus    added PCG random number generator state variables
 *  07-Jan-20   augustus    added TerminateLT()
 *  10-Jan-20   augustus    PostTaskProcRecord() now returns false if thread no longer running
 *  24-Feb-20   augustus    added RunPreGenesisWorkerThread()
 *  09-Jun-20   augustus    re-created in C from LTThreadImplBase.h
 *  05-Aug-20   augustus    added IsTerminatePending
 *  26-Aug-20   augustus    retooled to execute queued TaskProcs instead of being message queue based
 *  11-Oct-20   augustus    Start() now takes InitProc and ExitProc
 *                          SetRealtimePriority and SetNonRealtime now return previous priority
 *  30-Dec-20   augustus    added TerminateAllNonSystemThreads
 *  05-Feb-21   augustus    added QueueTaskProcIfRequired
 *  22-Jun-21   tiberius    clean up thread priorities
 *  22-Jul-21   augustus    expose (to LTCore lib internally) GetName(), Sleep() and Yield()
 *  09-Sep-21   tiberius    simplify thread priority API
 *  20-Oct-21   augustus    redid Timers to be based on procedure and clientdata, not timer id, allow setting on arbitrary threads
 *  26-Mar-25   augustus    added StartSynchronous
 */
