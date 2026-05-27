/******************************************************************************
 * lt/source/core/LTHandle.c   -   implementation of LTHandles
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include "LTHandle.h"
#include "LTCoreImpl.h"
#include "LTStdlibImpl.h"
#include "LTThreadImpl.h"
#include "LTLoggerImpl.h"
#include "LTConsoleConnector.h"

/* ______________________________________________________
   Three Atomic CompareAndExchange strategies were tried
   to optimize for lowest stack usage. In the course of
   running experiements, I uncovered what I believe is abort
   bug in the way interrupts are simulated in LTCoreBSP_Linux.c
   that manifests in a stage 3 unittest failure (likely a
   segfault). I've seen segfaults under similar circumstances
   on the x86 build as well.

   To reproduce the unit test failure, set the
   three #defines below as follows:

     #define USE_INTRINSIC_COMPAREANDEXCHANGE           0
     #define USE_ENABLE_DISABLE_COMPAREANDEXCHANGE      1
     #define USE_MUTEX_PROTECTED_ATOMIC_CALLS           0
=========================================================
_________________________________________________________   */
#define USE_INTRINSIC_COMPAREANDEXCHANGE                1
#define USE_ENABLE_DISABLE_COMPAREANDEXCHANGE           0
#define USE_MUTEX_PROTECTED_ATOMIC_CALLS                0
/*_____________________________________________________ */

#if USE_INTRINSIC_COMPAREANDEXCHANGE
/*  USE_INTRINSIC_COMPAREANDEXCHANGE - just define the other
    two to zero so all LTAtomic calls are left unmolested    */

    #undef  USE_ENABLE_DISABLE_COMPAREANDEXCHANGE
    #define USE_ENABLE_DISABLE_COMPAREANDEXCHANGE       0
    #undef  USE_MUTEX_PROTECTED_ATOMIC_CALLS
    #define USE_MUTEX_PROTECTED_ATOMIC_CALLS            0

    /* set initialization to no-op */
    #define LTHANDLE_INIT_EXPERIMENTAL_ATOMICS
    #define LTHANDLE_FINALIZE_EXPERIMENTAL_ATOMICS
#endif

#if USE_ENABLE_DISABLE_COMPAREANDEXCHANGE
/*  USE_ENABLE_DISABLE_COMPAREANDEXCHANGE - first ensure
    the other #defines are zero and can't interfere; the frist
    one is; let's ensure the other one is too. */
    #undef  USE_MUTEX_PROTECTED_ATOMIC_CALLS
    #define USE_MUTEX_PROTECTED_ATOMIC_CALLS            0

    /*CompareAndExchange function that disables interrupts for atomicity */
    LT_INLINE bool LTHandle_EnableDisableCompareAndExchange(LTAtomic *atomic, u32 oldValue, u32 newValue) {
        LT_SIZE nMask = LT_GetCore()->Disable();
        oldValue = (oldValue == LTAtomic_Load(atomic)) ? LTAtomic_Store(atomic, newValue), 1 : 0;
        LT_GetCore()->Enable(nMask);
        return oldValue ? true : false;
    }

    /* set initialization to no-op and
       #define LTAtomic_CompareAndExchange to redirect to LTHandle_EnableDisableCompareAndExchange */
    #define LTHANDLE_INIT_EXPERIMENTAL_ATOMICS
    #define LTHANDLE_FINALIZE_EXPERIMENTAL_ATOMICS
    #define LTAtomic_CompareAndExchange LTHandle_EnableDisableCompareAndExchange
#endif

#if USE_MUTEX_PROTECTED_ATOMIC_CALLS
    /* wrap all of LTLAtomic calls made in this file with functions that lock and unlock a mutex around the LTAtomic invocation
       because we don't want another thread coming in after the mutex protected CompareAndExchange has determined it will write,
       and sneak in a write before CompareAndExchange writes. */
    static void    *s_mutex = NULL;
    static u32      LTHandle_Load(LTAtomic *atomic)                  { LTKMutexLock(s_mutex); u32 value = LTAtomic_Load(atomic); LTKMutexUnlock(s_mutex); return value; }
    static void     LTHandle_Store(LTAtomic *atomic, u32 value)      { LTKMutexLock(s_mutex);  LTAtomic_Store(atomic, value);    LTKMutexUnlock(s_mutex); }
    static u32      LTHandle_FetchAdd(LTAtomic *atomic, u32 operand) { LTKMutexLock(s_mutex); u32 value = LTAtomic_Load(atomic); LTAtomic_Store(atomic, value + operand);  LTKMutexUnlock(s_mutex); return value; }
    static u32      LTHandle_FetchAnd(LTAtomic *atomic, u32 operand) { LTKMutexLock(s_mutex); u32 value = LTAtomic_Load(atomic); LTAtomic_Store(atomic, value & operand);  LTKMutexUnlock(s_mutex); return value; }
    static bool     LTHandle_CompareAndExchange(LTAtomic *atomic, u32 oldValue, u32 newValue) { LTKMutexLock(s_mutex);  oldValue = (oldValue == LTAtomic_Load(atomic)) ? LTAtomic_Store(atomic, newValue), 1 : 0; LTKMutexUnlock(s_mutex); return oldValue ? true : false; }

    /* setup initialization and #define the LTAtomic functions used in this file to the mutex protected versions here. */
    #define         LTHANDLE_INIT_EXPERIMENTAL_ATOMICS      { if (NULL == (s_mutex = core_malloc(LTKMutexInstanceSize()))) return false; LTKMutexInitialize(s_mutex); }
    #define         LTHANDLE_FINALIZE_EXPERIMENTAL_ATOMICS  { if (s_mutex) { LTKMutexFinalize(s_mutex); s_mutex = NULL; } }

    #define         LTAtomic_Load LTHandle_Load
    #define         LTAtomic_Store LTHandle_Store
    #define         LTAtomic_FetchAdd LTHandle_FetchAdd
    #define         LTAtomic_FetchAnd LTHandle_FetchAnd
    #define         LTAtomic_CompareAndExchange LTHandle_CompareAndExchange
#endif
/*
    ************************************************
    ************************************************
    *  END  LTHandle.c TEMPORARY EXPERIMENTAL CODE *
    ************************************************
    ************************************************
    ************************************************
*/


/* ______________________
   LTHandle.c #defines */

#define LTHANDLE_STOMP_CREATE_DESTROY 0

#if LTHANDLE_STOMP_CREATE_DESTROY
    #include "LTLoggerImpl.h"
    #define LTHANDLE_STOMP(pTag, pFormat, ...) LTLoggerImpl_Log("lthandle", pTag, kLTCore_LogFlags_LogTypeLog | (kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_ConsoleStomp), pFormat, ##__VA_ARGS__)
#endif

#define MAX_NUM_RECORD_POOLS                                            (32)
     /* MAX_NUM_RECORD_POOLS can be any even number between 2 and 254;
        each pool holds 32 handles, so 32 record pools = 32*32 = 1024 max handles */

#define   MAKE_RECORDSTATE(reservationCount, flags, cycleCount)         ((((reservationCount) << 16) & 0xFFFF0000) | (((flags) << 8) & 0x0000FF00) | ((cycleCount) & 0xFF))
#define UPDATE_RECORDSTATE_RESERVATIONCOUNT(state, reservationCount)    (((state) & 0x0000FFFF) | (((reservationCount) << 16) & 0xFFFF0000))
#define UPDATE_RECORDSTATE_FLAGS(state, flags)                          (((state) & 0xFFFF00FF) | (((flags) << 8) & 0x0000FF00))
#define UPDATE_RECORDSTATE_CYCLECOUNT(state, cycleCount)                (((state) & 0xFFFFFF00) | ((cycleCount) & 0xFF))
#define    GET_RECORDSTATE_RESERVATIONCOUNT(state)                      (((state) >> 16) & 0xFFFF)
#define    GET_RECORDSTATE_FLAGS(state)                                 (((state) >>  8) & 0xFF)
#define    GET_RECORDSTATE_CYCLECOUNT(state)                            ((state) & 0xFF)

#define MAKE_HANDLE(poolIndex, recordIndex, cycleCount)                 ((((poolIndex) << 16) & 0x00FF0000) | (((recordIndex) << 8) & 0x0000FF00) | ((cycleCount) & 0xFF))
#define  GET_HANDLE_POOLINDEX(handle)                                   (((handle) >> 16) & 0xFF)
#define  GET_HANDLE_RECORDINDEX(handle)                                 (((handle) >>  8) & 0xFF)
#define  GET_HANDLE_CYCLECOUNT(handle)                                  ((handle) & 0xFF)

#define HANDLE_INCREMENT_PRIME_NUMBER                                   1

DEFINE_LTLOG_SECTION                                                    ("lthandle");

/* ______________
   LTHandle Types
        ___________________________
        enum LTHandleRecordFlags */
typedef enum LTHandleRecordFlags {
    kLTHandleRecordFlags_InUse                              = (1 << 0),
    kLTHandleRecordFlags_DestroyPending                     = (1 << 1),
    kLTHandleRecordFlags_DestroyScheduled                   = (1 << 2),
    kLTHandleRecordFlags_InDestroy                          = (1 << 3),
    kLTHandleRecordFlags_ReservationLimitReported           = (1 << 4),
    kLTHandleRecordFlags_GetHandleRecordDeprecatedReported  = (1 << 5),
    kLTHandleRecordFlags_DestroyFlagsMask                   = kLTHandleRecordFlags_DestroyPending | kLTHandleRecordFlags_DestroyScheduled | kLTHandleRecordFlags_InDestroy
} LTHandleFlags;

/* _____________________________
        struct LTHandleRecord */
typedef struct LTHandleRecord {
    LTAtomic                state;     // 16 bits reservation count:8 bits flags:8 bits cycleCount
    LTThread                hThreadDestroying;
    LTInterface *           pInterface;
    void *                  privateData;
} LTHandleRecord;
LT_STATIC_ASSERT_SIZE_32_64(LTHandleRecord, 16, 24) /* 16 or 24 bytes */

/* ______________________________
        struct LTHandleRecordPool */
typedef struct LTHandleRecordPool {
    LTAtomic                recordsInUseBitfield;
    LTHandleRecord          handleRecords[32];
    u32                     reserved;
} LTHandleRecordPool;

/* ___________________________
        struct LTHandlePool */
typedef struct LTHandlePool {
    LTAtomic                indexOfHighestRecordPoolInUse; /* only incremented when new pool is fully initialized */
    LTHandleRecordPool     *handleRecordPools[MAX_NUM_RECORD_POOLS];
    LTAtomic                lastHandleAllocated;  /* units of poolIndex * 32 + recordIndex */
} LTHandlePool;

/* __________________________________
   LTHandle.c static variables */
static LTHandlePool         s_handlePool;
static ILTThread           *s_iThread;

/* _____________________________________
   LTHandle private utility functions */
static bool LTHandle_FilterByInterface(LTInterface *pInterface, void *filter) {
    return (pInterface == (LTInterface *)filter);
}

static bool LTHandle_FilterByInterfaceName(LTInterface *pInterface, void *filter) {
    return (0 == LTStdlibImpl_strcmp(pInterface->GetInterfaceName(), (const char *)filter));
}

static u32 LTHandle_GetCreatedHandles(LTHandle *handlesArrayToFill, u32 nArrayCount, bool (*FilterFunction)(LTInterface * pInterface, void *filter), void *filter) {
    u32 nTotalCount = 0;
    if (NULL == handlesArrayToFill) nArrayCount = 0;
    u32 nHighestRecordPoolIndex = LTAtomic_Load(&s_handlePool.indexOfHighestRecordPoolInUse);
    for (u32 i = 0; i <= nHighestRecordPoolIndex; i++) {
        LTHandleRecordPool *recordPool = s_handlePool.handleRecordPools[i];
        for (u32 j = 0; j < 32; j++) {
            LTHandleRecord *record = &recordPool->handleRecords[j];
            u32 recordState = LTAtomic_Load(&record->state);
            u32 cycleCount = GET_RECORDSTATE_CYCLECOUNT(recordState);
            if (FilterFunction) {
                if (GET_RECORDSTATE_FLAGS(recordState) & kLTHandleRecordFlags_InUse) {
                    if (filter) {
                        LTInterface *pInterface = record->pInterface;
                        recordState = LTAtomic_Load(&record->state);
                        if (pInterface && (GET_RECORDSTATE_FLAGS(recordState) & kLTHandleRecordFlags_InUse) && (cycleCount == GET_RECORDSTATE_CYCLECOUNT(recordState))) {
                            if (! (*FilterFunction)(pInterface, filter)) continue;
                        }
                        else continue;
                    }
                    nTotalCount++;
                    if (nArrayCount) {
                        *handlesArrayToFill++ = MAKE_HANDLE(i, j, cycleCount);
                        nArrayCount--;
                    }
                }
            }
            else {
                /* special case for "all" */
                nTotalCount++;
                if (nArrayCount) {
                    *handlesArrayToFill++ = MAKE_HANDLE(i, j, cycleCount);
                    nArrayCount--;
                }
            }
        }
    }
    return nTotalCount;
}

static void LTHandle_ExecuteScheduledDestroy(void *pClientData) {
    LTHandle_DestroyHandle(VOIDPTR_TO_LTHANDLE(pClientData));
}

static void LTHandle_TerminateWaitAndDestroyThread(void *pClientData) {
    LTThread hThread = VOIDPTR_TO_LTHANDLE(pClientData);
    LTThreadImpl_Terminate(hThread);
    LTThreadImpl_WaitUntilFinished(hThread, LTTime_Infinite());
    LTHandle_DestroyHandle(hThread);
}

LT_INLINE u32 SetFlagsWithDestroyFlag(u32 nFlags, u32 nDestroyFlag) {
    nFlags &= ~kLTHandleRecordFlags_DestroyFlagsMask;
    nFlags |= nDestroyFlag;
    return nFlags;
}

LT_INLINE LTHandleRecord * LTHandle_GetHandleRecord(LTHandle handle) {
    if (handle == 0) return NULL;
    return ((GET_HANDLE_RECORDINDEX(handle) < 32) && (GET_HANDLE_POOLINDEX(handle) <= LTAtomic_Load(&s_handlePool.indexOfHighestRecordPoolInUse)))
           ? &s_handlePool.handleRecordPools[GET_HANDLE_POOLINDEX(handle)]->handleRecords[GET_HANDLE_RECORDINDEX(handle)]
           : NULL;
}

LT_INLINE bool LTHandle_IsRecordStateActiveWithMatchingCycleCount(u32 recordState, LTHandle handle) {
    return ((GET_RECORDSTATE_CYCLECOUNT(recordState) == GET_HANDLE_CYCLECOUNT(handle)) && (GET_RECORDSTATE_FLAGS(recordState) & kLTHandleRecordFlags_InUse));
}

LT_INLINE bool LTHandle_InDestroyByAnotherThread(u32 recordState, LTHandleRecord *record) {
    /* this function returns true if we must prevent reading private data because the handle is InDestroy by another thread */
    if (0 == (GET_RECORDSTATE_FLAGS(recordState) & kLTHandleRecordFlags_InDestroy)) return false; /* not in destroy, allowed to read private data */
    if (LTKInsideInterruptContext()) return true; /* thread is destroying, isr can't get private data */
    bool bWrongThread = LTThreadImpl_GetCurrentThread() != record->hThreadDestroying; /* check to see if we are the destroying thread or not */
    /* check the destroy flag of the state again; we only trust the thread read if we are still in destroy */
    if (0 == (GET_RECORDSTATE_FLAGS(LTAtomic_Load(&record->state)) & kLTHandleRecordFlags_InDestroy)) return true; /* destroy completed by other thread; don't read private data */
    return bWrongThread;
}

static const char * LTHandle_RecordStateFlagsToStateString(u32 flags) {
    switch (flags & kLTHandleRecordFlags_DestroyFlagsMask) {
        case kLTHandleRecordFlags_DestroyPending:   return "DestroyPending";
        case kLTHandleRecordFlags_DestroyScheduled: return "DestroyScheduled";
        case kLTHandleRecordFlags_InDestroy:        return "InDestroy";
        default:                                    return "healthy";
    }
}

/* _______________________________________
   LTHandle functions internal to LTCore*/
bool LTHandle_Init(void) {
    LTHANDLE_INIT_EXPERIMENTAL_ATOMICS;
    s_iThread = LTCoreImpl_GetILTThread();
    LTStdlibImpl_memset(&s_handlePool, 0, sizeof(s_handlePool));
    if (NULL != (s_handlePool.handleRecordPools[0] = core_malloc(sizeof(LTHandleRecordPool)))) {
        LTStdlibImpl_memset(s_handlePool.handleRecordPools[0], 0, sizeof(LTHandleRecordPool));
        /* make handle 0 always invalid */
        LTAtomic_Store(&s_handlePool.handleRecordPools[0]->handleRecords[0].state, MAKE_RECORDSTATE(0, 0, 0x55));
        LTAtomic_Store(&s_handlePool.handleRecordPools[0]->recordsInUseBitfield, 1); /* handles 0 is in use */
        return true;
    }
    LTHandle_Fini();
    return false;
}

void LTHandle_Fini(void) {
    for (u32 i = 0; i < MAX_NUM_RECORD_POOLS; i++) if (s_handlePool.handleRecordPools[i]) core_free(s_handlePool.handleRecordPools[i]); else break;
    LTStdlibImpl_memset(&s_handlePool, 0, sizeof(s_handlePool));
    s_iThread = NULL;
    LTHANDLE_FINALIZE_EXPERIMENTAL_ATOMICS;
}

void * LTHandle_ReserveInterfaceCheckedPrivateData(LTHandle handle, LTInterface *pInterface) {
    void *privateData = LTHandle_ReservePrivateData(handle);
    if (privateData && LTHandle_GetHandleRecord(handle)->pInterface != pInterface) {
        LTHandle_ReleasePrivateData(handle, privateData);
        privateData = NULL;
    }
    return privateData;
}

bool LTHandle_FORCRASHDUMPONLY_EnumerateHandlesForInterface(LTInterface *pInterface, bool (*handleEnumProc)(LTHandle handle, LTInterface *pInterface, void *privateData, void *clientData), void *clientData) {
    bool bRetVal = true;
    u32 nHighestRecordPoolIndex = LTAtomic_Load(&s_handlePool.indexOfHighestRecordPoolInUse);
    for (u32 i = 0; i <= nHighestRecordPoolIndex; i++) {
        LTHandleRecordPool *recordPool = s_handlePool.handleRecordPools[i];
        for (u32 j = 0; j < 32; j++) {
            LTHandleRecord *record = &recordPool->handleRecords[j];
            u32 recordState = LTAtomic_Load(&record->state);
            u32 nFlags = GET_RECORDSTATE_FLAGS(recordState);
            if ((nFlags & kLTHandleRecordFlags_InUse) && (0 == (nFlags & kLTHandleRecordFlags_InDestroy))) {
                /* we have a record that is in use and not in destroy.  That means it's private data is valid
                   but we won't 'reserve' it because that's relatively expensive and this function only runs
                   from the fault handler so no one can change any state of this table anyway */
                if ((record->pInterface == pInterface) || (NULL == pInterface)) {
                    if (! handleEnumProc(MAKE_HANDLE(i, j, GET_RECORDSTATE_CYCLECOUNT(recordState)), record->pInterface, record->privateData, clientData)) {
                        bRetVal = false;
                        break;
                    }
                }
            }
        }
        if (! bRetVal) break;
    }
    return bRetVal;
}

static const char * NumToStaticHexString6(u32 num) {
    static char string[7];
    char *pChar = &string[5];
    int nDigits = 6;
    string[6] = 0;
    while (nDigits--) {
        char ch = (char)(num & 0xF);
        if (ch > 9) ch = 'A' + (ch-10); else ch += '0';
        *pChar-- = ch;
        num >>=4;
    }
    return string;
}

void LTHandle_DumpLeakedHandles(void) {
    u32 i, j, nCount = 0, nHighestRecordPoolIndex = LTAtomic_Load(&s_handlePool.indexOfHighestRecordPoolInUse);
    LTHandleRecordPool *recordPool; LTHandleRecord *record;
    const char *pString = NULL; int numSpaces = 0;
    for (i = 0; i <= nHighestRecordPoolIndex; i++) { recordPool = s_handlePool.handleRecordPools[i];
        for (j = 0; j < 32; j++) if (GET_RECORDSTATE_FLAGS(LTAtomic_Load(&recordPool->handleRecords[j].state)) & kLTHandleRecordFlags_InUse) nCount++;
    }
    if (nCount) {
        LTLoggerImpl_ConsoleStompString("ltcore: leftover handles\n");
        LTLoggerImpl_ConsoleStompString("______  ____________  _____             ____\n");
        LTLoggerImpl_ConsoleStompString("handle  reservations  state             type\n");

        for (i = 0; i <= nHighestRecordPoolIndex; i++) { recordPool = s_handlePool.handleRecordPools[i];
            for (u32 j = 0; j < 32; j++) { record = &recordPool->handleRecords[j];
                u32 recordState = LTAtomic_Load(&record->state);
                u32 nFlags = GET_RECORDSTATE_FLAGS(recordState);
                if (nFlags & kLTHandleRecordFlags_InUse) {
                    pString = NumToStaticHexString6(MAKE_HANDLE(i, j, GET_RECORDSTATE_CYCLECOUNT(recordState)));
                    numSpaces = 8 - lt_strlen(pString); if (numSpaces < 2) numSpaces = 2;
                    LTLoggerImpl_ConsoleStompString(pString); while (numSpaces --) LTConsoleConnector_ConsoleStompChar(' ');
                    pString = LTCoreImpl_NumToStaticString(GET_RECORDSTATE_RESERVATIONCOUNT(recordState));
                    numSpaces = 12 - lt_strlen(pString); if (numSpaces < 2) numSpaces = 2; while (numSpaces--) LTConsoleConnector_ConsoleStompChar(' ');
                    LTLoggerImpl_ConsoleStompString(pString); numSpaces = 2; while (numSpaces--) LTConsoleConnector_ConsoleStompChar(' ');
                    pString = LTHandle_RecordStateFlagsToStateString(nFlags);
                    numSpaces = 18 - lt_strlen(pString); if (numSpaces < 2) numSpaces = 2;
                    LTLoggerImpl_ConsoleStompString(pString); while (numSpaces --) LTConsoleConnector_ConsoleStompChar(' ');
                    pString = record->pInterface ? record->pInterface->GetInterfaceName() : "???";
                    LTLoggerImpl_ConsoleStompString(pString);
                    if (record->pInterface == (LTInterface *)s_iThread) {
                        char name[kLTThread_MaxNameBuff];
                        LTThreadImpl_GetName(MAKE_HANDLE(i, j, GET_RECORDSTATE_CYCLECOUNT(recordState)), name);
                        LTLoggerImpl_ConsoleStompString("  [");
                        LTLoggerImpl_ConsoleStompString(name);
                        LTConsoleConnector_ConsoleStompChar(']');
                    }
                    LTConsoleConnector_ConsoleStompChar('\n');
                }
            }
        }
    }
    pString = LTCoreImpl_NumToStaticString(nCount);
    LTLoggerImpl_ConsoleStompString("ltcore: ");
    LTLoggerImpl_ConsoleStompString(pString);
    LTLoggerImpl_ConsoleStompString(" leftover handle");
    LTLoggerImpl_ConsoleStompString((nCount == 1) ? "\n" : "s\n");
}

void LTHandle_DeleteHandleWithoutDestroy(LTHandle handle) {
    LTHandleRecord *record = LTHandle_GetHandleRecord(handle);
    u32 oldState = LTAtomic_Load(&record->state);
    if (record && LTHandle_IsRecordStateActiveWithMatchingCycleCount(oldState, handle)) {
        LTAtomic_Store(&record->state, MAKE_RECORDSTATE(0, 0, GET_RECORDSTATE_CYCLECOUNT(oldState) + 1));
        core_free(record->privateData);
        record->hThreadDestroying = 0;
        record->pInterface = NULL;
        record->privateData = NULL;
        LTAtomic_FetchAnd(&s_handlePool.handleRecordPools[GET_HANDLE_POOLINDEX(handle)]->recordsInUseBitfield, ~(1 << GET_HANDLE_RECORDINDEX(handle)));
    }
}


/* _______________________________________________
   LTHandle (LTCore) public interface functions */
void * LTHandle_ReservePrivateData(LTHandle handle) {
    if (0 == handle) return NULL;
    void *privateData = NULL;
    LTHandleRecord *record = LTHandle_GetHandleRecord(handle);
    if (record) {
        u32 oldState = LTAtomic_Load(&record->state);
        while (LTHandle_IsRecordStateActiveWithMatchingCycleCount(oldState, handle) && (! LTHandle_InDestroyByAnotherThread(oldState, record))) {
            u32 reservationCount = GET_RECORDSTATE_RESERVATIONCOUNT(oldState);
            if (reservationCount == 65535) { /* make sure we don't wrap around the reservation count */
                while (0 == (GET_RECORDSTATE_FLAGS(oldState) & kLTHandleRecordFlags_ReservationLimitReported)) {
                    u32 newState = UPDATE_RECORDSTATE_FLAGS(oldState, GET_RECORDSTATE_FLAGS(oldState) | kLTHandleRecordFlags_ReservationLimitReported);
                    if (LTAtomic_CompareAndExchange(&record->state, oldState, newState)) {
                        LTLOG_YELLOWALERT("reserveprivatedata", "reservation count limit reached for %s handle", record->pInterface ? record->pInterface->GetInterfaceName() : "??" );
                        break;
                    }
                    oldState = LTAtomic_Load(&record->state);
                }
                break;
            }
            if (LTAtomic_CompareAndExchange(&record->state, oldState, UPDATE_RECORDSTATE_RESERVATIONCOUNT(oldState, reservationCount + 1))) {
                privateData = record->privateData;
                break;
            }
            oldState = LTAtomic_Load(&record->state);
        }
    }
    return privateData;
}

void LTHandle_ReleasePrivateData(LTHandle handle, void *privateData) {
    if (0 == handle) return;
    LTHandleRecord *record = LTHandle_GetHandleRecord(handle);
    if (record) {
        u32 oldState = LTAtomic_Load(&record->state);
        while (LTHandle_IsRecordStateActiveWithMatchingCycleCount(oldState, handle) && (privateData == record->privateData)) {
            u32 oldFlags = GET_RECORDSTATE_FLAGS(oldState);
            u32 reservationCount = GET_RECORDSTATE_RESERVATIONCOUNT(oldState);
            if (0 == reservationCount) {
                LTLOG_DEBUG("rescount.zero.release", "reservation count already zero on release for %s handle", record->pInterface ? record->pInterface->GetInterfaceName() : "??" );
                return;
            }
            reservationCount--;
            u32 newFlags;
            u32 newState = UPDATE_RECORDSTATE_RESERVATIONCOUNT(oldState, reservationCount);
            if ((0 == reservationCount) && (oldFlags & kLTHandleRecordFlags_DestroyPending)) {
                newFlags = SetFlagsWithDestroyFlag(oldFlags, kLTHandleRecordFlags_DestroyScheduled);
                newState = UPDATE_RECORDSTATE_FLAGS(newState, newFlags);
            }
            else newFlags = oldFlags;
            if (LTAtomic_CompareAndExchange(&record->state, oldState, newState)) {
                if (oldFlags != newFlags) {
                    LTThreadImpl_QueueTaskProc(LTCoreImpl_GetLTCoreImpl()->hThreadCore, &LTHandle_ExecuteScheduledDestroy, NULL, LTHANDLE_TO_VOIDPTR(handle));
                }
                break;
            }
            oldState = LTAtomic_Load(&record->state);
        }
    }
}

void * LTHandle_GetPrivateData(LTHandle handle) {
    if (0 == handle) return NULL;
    void *privateData = LTHandle_ReservePrivateData(handle);
    if (privateData) {
        LTHandleRecord *record = LTHandle_GetHandleRecord(handle);
        u32 oldState = LTAtomic_Load(&record->state);
        while (0 == (GET_RECORDSTATE_FLAGS(oldState) & kLTHandleRecordFlags_GetHandleRecordDeprecatedReported)) {
            u32 newState = UPDATE_RECORDSTATE_FLAGS(oldState, GET_RECORDSTATE_FLAGS(oldState) | kLTHandleRecordFlags_GetHandleRecordDeprecatedReported);
            if (LTAtomic_CompareAndExchange(&record->state, oldState, newState)) {
                char name[kLTThread_MaxNameBuff];
                LTThreadImpl_GetName(LTThreadImpl_GetCurrentThread(), name);
                LTLOG_YELLOWALERT("deprecated", "YELLOW ALERT: ERADICATE GetHandlePrivateData(%s); from %s NOW! DO IT!  ARRRGGHHHH!", record->pInterface ? record->pInterface->GetInterfaceName() : "", name);
                break;
            }
            oldState = LTAtomic_Load(&record->state);
        }
        LTHandle_ReleasePrivateData(handle, privateData);
    }
    return privateData;
}

bool LTHandle_IsHandleValid(LTHandle handle) {
    if (0 == handle) return false;
    LTHandleRecord *record = LTHandle_GetHandleRecord(handle);
    return record ? LTHandle_IsRecordStateActiveWithMatchingCycleCount(LTAtomic_Load(&record->state), handle) : false;
}

LTInterface * LTHandle_GetHandleInterface(LTHandle handle) {
    if (0 == handle) return NULL;
    LTInterface * pInterface = NULL;
    LTHandleRecord *record = LTHandle_GetHandleRecord(handle);
    if (record && LTHandle_IsRecordStateActiveWithMatchingCycleCount(LTAtomic_Load(&record->state), handle)) {
        pInterface = record->pInterface;
        if (! LTHandle_IsRecordStateActiveWithMatchingCycleCount(LTAtomic_Load(&record->state), handle)) pInterface = NULL;
    }
    return pInterface;
}

LTInterface * LTHandle_GetNameCheckedHandleInterface(LTHandle handle, const char * pInterfaceName) {
    if (0 == handle) return NULL;
    LTInterface * pInterface = LTHandle_GetHandleInterface(handle);
    if (pInterface && (0 != LTStdlibImpl_strcmp(pInterface->GetInterfaceName(), pInterfaceName))) pInterface = NULL;
    return pInterface;
}

const char * LTHandle_GetHandleInterfaceName(LTHandle handle) {
    if (0 == handle) return NULL;
    LTInterface * pInterface = LTHandle_GetHandleInterface(handle);
    return pInterface ? pInterface->GetInterfaceName() : NULL;
}

LTLibrary * LTHandle_GetHandleLibrary(LTHandle handle) {
    if (0 == handle) return NULL;
    LTInterface * pInterface = LTHandle_GetHandleInterface(handle);
    return pInterface ? pInterface->GetLibrary() : NULL;
}

u32 LTHandle_GetHandleReservationCount(LTHandle handle) {
    if (0 == handle) return 0;
    u32 count = 0;
    LTHandleRecord *record = LTHandle_GetHandleRecord(handle);
    if (record) {
        u32 state = LTAtomic_Load(&record->state);
        if (LTHandle_IsRecordStateActiveWithMatchingCycleCount(state, handle)) count = GET_RECORDSTATE_RESERVATIONCOUNT(state);
    }
    return count;
}

const char * LTHandle_GetHandleStateString(LTHandle handle) {
    if (0 == handle) return NULL;
    LTHandleRecord *record = LTHandle_GetHandleRecord(handle);
    if (record) {
        u32 state = LTAtomic_Load(&record->state);
        if (LTHandle_IsRecordStateActiveWithMatchingCycleCount(state, handle)) return LTHandle_RecordStateFlagsToStateString(GET_RECORDSTATE_FLAGS(state));
    }
    return "invalid";
}

u32 LTHandle_GetCreatedHandlesByInterface(LTHandle *handlesArrayToFill, u32 nArrayCount, LTInterface *pInterface) {
    return LTHandle_GetCreatedHandles(handlesArrayToFill, nArrayCount, &LTHandle_FilterByInterface, (void *)pInterface);
}

u32 LTHandle_GetCreatedHandlesByInterfaceName(LTHandle *handlesArrayToFill, u32 nArrayCount, const char *pInterfaceName) {
    if (pInterfaceName && 0 == lt_strcmp(pInterfaceName, "all")) return LTHandle_GetCreatedHandles(handlesArrayToFill, nArrayCount, NULL, NULL);
    return LTHandle_GetCreatedHandles(handlesArrayToFill, nArrayCount, &LTHandle_FilterByInterfaceName, (void *)pInterfaceName);
}

LTHandle
LTHandle_CreateHandle(LTInterface * pHandleInterface, LT_SIZE nSizeInBytes) {
    void *handlePrivate = (pHandleInterface && nSizeInBytes) ? core_malloc(nSizeInBytes) : NULL;
    if (! handlePrivate) return 0;

    //u32 nLast = LTAtomic_Load(&s_handlePool.lastHandleAllocated) + HANDLE_INCREMENT_PRIME_NUMBER;
    u32 nLast = LTAtomic_FetchAdd(&s_handlePool.lastHandleAllocated, HANDLE_INCREMENT_PRIME_NUMBER) + HANDLE_INCREMENT_PRIME_NUMBER;
    u32 nLastAllocated = nLast;
    u32 numPools = LTAtomic_Load(&s_handlePool.indexOfHighestRecordPoolInUse) + 1;
    while (nLast >= (numPools << 5)) nLast -= (numPools << 5);
    u32 startPoolIndex = nLast >> 5;
    u32 recordIndex = nLast & 31;
    nLast = numPools; /* nLast is now the pool index upper bound+1 for search */
    while (startPoolIndex >= numPools) startPoolIndex -= numPools;

    LTHandle handle = 0;
    LTHandleRecordPool *recordPool;
    u32 i, j;
onemoretime:
    for (i = startPoolIndex; i < nLast; i++) {
onelasttime:
        recordPool = s_handlePool.handleRecordPools[i];
        u32 inUse = LTAtomic_Load(&recordPool->recordsInUseBitfield);
        if ((recordIndex < 16) && (0xFFFF == (inUse & 0xFFFF))) recordIndex = 16;
        if ((recordIndex > 15) && (0xFFFF0000 == (inUse & 0xFFFF0000))) {
            recordIndex = 0;
            continue;
        }
        for (j = recordIndex; j < 32; j++) {
            while (0 == ((1 << j) & inUse)) {
                if (LTAtomic_CompareAndExchange(&recordPool->recordsInUseBitfield, inUse, inUse | (1 << j))) {
                    LTAtomic_CompareAndExchange(&s_handlePool.lastHandleAllocated, nLastAllocated, (i<<5) + j);
                    //LTAtomic_Store(&s_handlePool.lastHandleAllocated, (i<<5) + j);
                    LTHandleRecord *record = &recordPool->handleRecords[j];
                    record->hThreadDestroying = 0;
                    record->pInterface = pHandleInterface;
                    record->privateData = handlePrivate;
                    /* reuse nLast to get cyclecount to make the handle */
                    nLast = LTAtomic_Load(&record->state);
                    nLast = GET_RECORDSTATE_CYCLECOUNT(nLast);
                    handle = MAKE_HANDLE(i, j, nLast);
#if LTHANDLE_STOMP_CREATE_DESTROY
                    LTHANDLE_STOMP("", "ADD %02d[%02d]#%02X %s\n", ((int)handle >> 16) & 0xFF, ((int)handle >> 8) & 0xFF, (int)handle & 0xFF, pHandleInterface && pHandleInterface->GetInterfaceName() ? pHandleInterface->GetInterfaceName() : "?");
#endif
                    /* reuse nLast to make new record state with 0 reservation count, InUse (valid) flag, and same cycle count */
                    nLast = MAKE_RECORDSTATE(0, kLTHandleRecordFlags_InUse, nLast);
                    LTAtomic_Store(&record->state, nLast);
                    return handle;
                }
                inUse = LTAtomic_Load(&recordPool->recordsInUseBitfield);
            }
        }
        recordIndex = 0;
    }
    if (startPoolIndex != 0) {
        nLast = startPoolIndex;
        startPoolIndex = 0;
        goto onemoretime;
    }

    /* didn't find a free slot - see if another thread snuck in and added a new pool */
tryaddedbyanother:
    nLast = LTAtomic_Load(&s_handlePool.indexOfHighestRecordPoolInUse) + 1;
    if (nLast > numPools) {
        /* someone got in and added another pool while we were searching */
        i = numPools;
        numPools = nLast;
        startPoolIndex = 0; /* so we don't goto onemoretime */
        recordIndex = 0;
        goto onelasttime;
    }

    if (numPools < MAX_NUM_RECORD_POOLS) {
        /* all current recordPools are full and there is room for another, make a new one */
        recordPool = core_malloc(sizeof(LTHandleRecordPool));
        if (recordPool) {
            LTStdlibImpl_memset(recordPool, 0, sizeof(*recordPool));
            LTHandleRecord *record = &recordPool->handleRecords[0];
            record->pInterface = pHandleInterface;
            record->privateData = handlePrivate;
            LTAtomic_Store(&recordPool->recordsInUseBitfield, 1);
            LTAtomic_Store(&record->state, MAKE_RECORDSTATE(0, kLTHandleRecordFlags_InUse, 0));
            /* have to atomically set the index of highest record pool in use along with the pointer to the pool; disable interrupts to make this atomic */
            LT_SIZE nMask = LTKDisableInterrupts();
            if (LTAtomic_CompareAndExchange(&s_handlePool.indexOfHighestRecordPoolInUse, numPools - 1, numPools)) {
                LTAtomic_Store(&s_handlePool.lastHandleAllocated, numPools << 5);
                s_handlePool.handleRecordPools[numPools] = recordPool;
                LTKEnableInterrupts(nMask);
                return MAKE_HANDLE(numPools, 0, 0);
            }
            LTKEnableInterrupts(nMask);
            /* someone added one on us while we were making one! */
            core_free(recordPool);
            goto tryaddedbyanother;
        }
    }

    /* no more room */
    core_free(handlePrivate);
    return 0;
}

void
LTHandle_DestroyHandle(LTHandle handle) {
    LTHandleRecord *record = LTHandle_GetHandleRecord(handle);
    if (! record) return;
    u32 flags = 0;
    u32 oldState = LTAtomic_Load(&record->state);
    bool bThreadNotWaited = true;
    bool bInISR = LTKInsideInterruptContext();
    LTThread hThreadCurrent = bInISR ? 0 : LTThreadImpl_GetCurrentThread();
    while (LTHandle_IsRecordStateActiveWithMatchingCycleCount(oldState, handle)) {
        /* if we are destroying a thread, terminate and wait first */
        if ((record->pInterface == (LTInterface *)s_iThread) && bThreadNotWaited) {
            bThreadNotWaited = false;
            if (! bInISR) LTThreadImpl_Terminate(handle);
            if (bInISR || (hThreadCurrent == handle)) {
                /* can't wait from an isr or on self, so have the idle thread do it */
                LTThreadImpl_QueueTaskProc(LTCoreImpl_GetLTCoreImpl()->hThreadCore, &LTHandle_TerminateWaitAndDestroyThread, NULL, LTHANDLE_TO_VOIDPTR(handle));
                return;
            }
            LTThreadImpl_WaitUntilFinished(handle, LTTime_Infinite());
            oldState = LTAtomic_Load(&record->state);
            continue; /* the thread finishing will release its own private data, so reload the state and go 'round again */
        }

        flags = GET_RECORDSTATE_FLAGS(oldState);
        if (flags & kLTHandleRecordFlags_InDestroy)  return; /* already being destroyed */
        if (flags & kLTHandleRecordFlags_DestroyPending) return; /* DestroyPending will work itself out */
        if ((flags & kLTHandleRecordFlags_DestroyScheduled) && (hThreadCurrent != LTCoreImpl_GetLTCoreImpl()->hThreadCore)) {
            /* there is a destroy scheduled on the Core thread; we are not the Core thread, bail */
            return;
        }
        /* ok, we are going to pend, queue, or execute the destroy */

        // either flags & kLTHandleRecordFlags_DestroyScheduled or plain destroy
        // in either case, if the count is non-zero go to destroy pending, otherwise InDestroy
        flags &= ~kLTHandleRecordFlags_DestroyFlagsMask;
        if (GET_RECORDSTATE_RESERVATIONCOUNT(oldState)) {
            /* privateData is reserved, switch to kLTHandleRecordFlags_DestroyPending */
            flags |= kLTHandleRecordFlags_DestroyPending;
            if (LTAtomic_CompareAndExchange(&record->state, oldState, UPDATE_RECORDSTATE_FLAGS(oldState, flags))) {
                /* destroy pending set, that's all we have to do */
                return;
            }
            /* couldn't compare and exchange, try again! */
            oldState = LTAtomic_Load(&record->state);
            continue;
        }

        /* ok, zero reservation count, we can do the destroy, but only on an LTThread - we don't destroy handles from ISRs, schedule otherwise
           actually allow destroy from a non lt thread because LTCore shutdown will destroy from function main thread on Linux...
           so the second half of the if statement is commented out until I tighten this up! */
        if (bInISR) { // || ((0 == hThreadCurrent) && (handle != LTCoreImpl_GetLTCoreImpl()->hThreadCore)) { /* except don't schedule the timer thread on the timer thread! */
            /* not an LT thread, schedule on the idle thread */
            flags |= kLTHandleRecordFlags_DestroyScheduled;
            if (LTAtomic_CompareAndExchange(&record->state, oldState, UPDATE_RECORDSTATE_FLAGS(oldState, flags))) {
                 LTThreadImpl_QueueTaskProc(LTCoreImpl_GetLTCoreImpl()->hThreadCore, &LTHandle_ExecuteScheduledDestroy, NULL, LTHANDLE_TO_VOIDPTR(handle));
                 return;
             }
            /* couldn't compare and exchange, try again! */
            oldState = LTAtomic_Load(&record->state);
            continue;
        }
        /* we are going to destroy the dang handle! mark InDestroy and set the InDestroy thread atomically by disabling interrupts */
        flags |= kLTHandleRecordFlags_InDestroy;
        LT_SIZE nMask = LTKDisableInterrupts();
        if (! LTAtomic_CompareAndExchange(&record->state, oldState, UPDATE_RECORDSTATE_FLAGS(oldState, flags))) {
            /* couldn't compare and exchange, try again! */
            LTKEnableInterrupts(nMask);
            oldState = LTAtomic_Load(&record->state);
            continue;
        }
        record->hThreadDestroying = hThreadCurrent;
        LTKEnableInterrupts(nMask);
        /* If we are here we successfully marked the handle InDestroy, we are in an LTThread,
           no one has the privateData reserved, and this thread is the only thread that can reserve
           private data (so that the OnDestroyProc can access the private data) */
#if LTHANDLE_STOMP_CREATE_DESTROY
        char name[kLTThread_MaxNameBuff];
        const char * pThreadName = "";
        if (record->pInterface == (LTInterface *)s_iThread) {
            LTThreadImpl_GetName(handle, name);
            pThreadName = name;
        }
        LTHANDLE_STOMP("", "DEL %02d[%02d]#%02X %s %s\n", ((int)handle >> 16) & 0xFF, ((int)handle >> 8) & 0xFF, (int)handle & 0xFF, record->pInterface && record->pInterface->GetInterfaceName() ? record->pInterface->GetInterfaceName() : "?", pThreadName);
#endif
        if (record->pInterface->OnDestroyHandle) record->pInterface->OnDestroyHandle(handle);
        LTAtomic_Store(&record->state, MAKE_RECORDSTATE(0, 0, GET_RECORDSTATE_CYCLECOUNT(oldState) + 1));
        core_free(record->privateData);
        record->hThreadDestroying = 0;
        record->pInterface = NULL;
        record->privateData = NULL;
        LTAtomic_FetchAnd(&s_handlePool.handleRecordPools[GET_HANDLE_POOLINDEX(handle)]->recordsInUseBitfield, ~(1 << GET_HANDLE_RECORDINDEX(handle)));
    }
}

u32 LTHandle_GetHandleCount(void) {
    return LTHandle_GetCreatedHandlesByInterface(NULL, 0, NULL);
}

u32 LTHandle_GetTotalHandleBytesOverhead(void) {
    return (u32)(sizeof(LTHandlePool) + ((LTAtomic_Load(&s_handlePool.indexOfHighestRecordPoolInUse) + 1) * sizeof(LTHandleRecordPool)));
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  19-Sep-23   augustus    created
 */
