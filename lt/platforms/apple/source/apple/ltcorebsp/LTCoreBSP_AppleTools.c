/******************************************************************************
 * platforms/apple/source/apple/ltcorebsp/LTCoreBSP_AppleTools.c
 *                               - minimal LTCoreBSP for Apple build tools
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stddef.h>
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <libgen.h>
#include <time.h>

#include <lt/core/LTStdlib.h>
#include <lt/core/LTCore.h>
#include <lt/core/bsp/LTHostAPI.h>
#include <lt/core/bsp/LTCoreBSP.h>

/*___________
  #defines */
#define LTCOREBSP_MIN_PTHREAD_STACK_SIZE                (40 * 1024)             /* macOS needs >=40K for dlsym to work */
#define LTCOREBSP_STACK_PAGE_SIZE                       (4096)                  /* from getpagesize() on macOS */
#define LTCOREBSP_ROUND_UP_TO_PAGE_MULTIPLE(nStackSize) ((nStackSize + (LTCOREBSP_STACK_PAGE_SIZE-1)) & ~(LTCOREBSP_STACK_PAGE_SIZE-1))
#define LTCOREBSP_ISR_PRIORITY                          (31)
#define LTCOREBSP_MAX_PRIORITY                          (LTCOREBSP_ISR_PRIORITY)
#define ENABLE_KEY_INPUT                                0

DEFINE_BSP_LTLOG_SECTION("appletools.bsp");

/*_______________________
  forward declarations */
static const LTCoreBSP s_bsp;
static bool  LTHostAPI_ThreadInitializeAndRun(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pThreadProc)(void * pClientData), void * pClientData);
struct       AppleThreadInstanceData;
static bool  LTCoreBSP_ReleaseCPU(struct AppleThreadInstanceData *pThread, bool abortIfHighestPriority);
static void  LTCoreBSP_TakeCPU(struct AppleThreadInstanceData *pThread);

/*___________________
  static variables */
static const LTCoreBSP_LTCoreCallbacks *    s_pCoreCallbacks = NULL;
static LTAtomic                             s_LTCoreBSPInitialized = { 0 };
static pthread_mutex_t                      s_mutex;
static clockid_t                            s_clockID = _CLOCK_MONOTONIC_RAW;
static s64                                  s_nHighFrequencyCounterResolution = 0;
static s64                                  s_nHighFrequencyCounterInitial = 0;
static pthread_key_t                        s_keyThreadLocal = (pthread_key_t)0;
static pthread_mutex_t                      s_disableMutex;
static pthread_t                            s_disableThread = (pthread_t)0;
static int                                  s_nDisableCount = 0;
static LTAtomic                             s_InsideInterruptContext = { 0 };

static int                                  s_numPriorityReady[LTCOREBSP_MAX_PRIORITY+1];
static struct AppleThreadInstanceData*      s_pCurrentThread = NULL;
static int                                  s_nMaxReadyPriority = -1;
static pthread_cond_t                       s_condition;

static const char kEAGAINMessage[] = "system thread resource limit reached\n";
static const char kEINVALMessage[] = "invalid attributes specified\n";

#define debug_printf(args...)

/*______________________________
  unbuffered console putchars */
static void LT_ISR_SAFE LTCoreBSP_PutCharsToConsole(const char * pChars, u32 nChars) {
    while (nChars > 0) {
        ssize_t n = write(STDOUT_FILENO, pChars, nChars);
        if (n <= 0) break;
        pChars += n;
        nChars -= n;
    }
}

static void LTCoreBSP_Yield(void) {
    struct AppleThreadInstanceData* pThread = pthread_getspecific(s_keyThreadLocal);
    if (!pThread) return;
    if (!LTCoreBSP_ReleaseCPU(pThread, true)) return;
    LTCoreBSP_TakeCPU(pThread);
}

static void OnProcessExit(void) {
    if (s_pCoreCallbacks) s_pCoreCallbacks->TerminateLT(0);
}

/*_____________________
  BSP initialization */
const LTCoreBSP *
LTCoreBSP_Initialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks) {

    if (LTAtomic_Load(&s_LTCoreBSPInitialized)) return NULL;
    LTAtomic_Store(&s_LTCoreBSPInitialized, 1);

    /* macOS does not support pthread_setaffinity_np; skip CPU affinity */

    BSP_LTLOG_INITIALIZE(pCallbacks->LTCoreLogFunction);

    s_pCoreCallbacks = pCallbacks;
    atexit(&OnProcessExit);

    /* init s_mutex */
    pthread_mutexattr_t mutexAttrs;
    pthread_mutexattr_init(&mutexAttrs);
    pthread_mutexattr_settype(&mutexAttrs, PTHREAD_MUTEX_DEFAULT);
    pthread_mutex_init(&s_mutex, &mutexAttrs);
    pthread_mutexattr_destroy(&mutexAttrs);

    pthread_cond_init(&s_condition, NULL);

    /* init LTCoreBSP_GetHighFrequencyCounterNanoseconds() */
    struct timespec ts1, ts2;
    s_clockID = _CLOCK_MONOTONIC_RAW;
    if ((0 == clock_getres(s_clockID, &ts1)) && (0 == clock_gettime(s_clockID, &ts2))) {
        s_nHighFrequencyCounterResolution = LTCoreBSP_SecondsToNanoseconds(ts1.tv_sec) + (s64)ts1.tv_nsec;
        s_nHighFrequencyCounterInitial = LTCoreBSP_SecondsToNanoseconds(ts2.tv_sec) + (s64)ts2.tv_nsec;
    }
    else {
        s_clockID = CLOCK_MONOTONIC;
        if ((0 == clock_getres(s_clockID, &ts1)) && (0 == clock_gettime(s_clockID, &ts2))) {
            s_nHighFrequencyCounterResolution = LTCoreBSP_SecondsToNanoseconds(ts1.tv_sec) + (s64)ts1.tv_nsec;
            s_nHighFrequencyCounterInitial = LTCoreBSP_SecondsToNanoseconds(ts2.tv_sec) + (s64)ts2.tv_nsec;
        }
    }

    /* init threads */
    pthread_key_create(&s_keyThreadLocal, NULL);

    /* init Disable/Enable mutex */
    pthread_mutexattr_t disableMutexAttrs;
    pthread_mutexattr_init(&disableMutexAttrs);
    pthread_mutexattr_settype(&disableMutexAttrs, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_disableMutex, &disableMutexAttrs);
    pthread_mutexattr_destroy(&disableMutexAttrs);

    return &s_bsp;
}

void
LTCoreBSP_Finalize(const LTCoreBSP * pBSP) {
    if ((! LTAtomic_Load(&s_LTCoreBSPInitialized)) || (pBSP != &s_bsp)) return;

    pthread_key_delete(s_keyThreadLocal);
    s_keyThreadLocal = (pthread_key_t)0;
    s_nHighFrequencyCounterInitial = 0;
    s_nHighFrequencyCounterResolution = 0;
    s_clockID = _CLOCK_MONOTONIC_RAW;
    pthread_mutex_destroy(&s_mutex);
    pthread_mutex_destroy(&s_disableMutex);

    s_pCoreCallbacks = NULL;
    LTAtomic_Store(&s_LTCoreBSPInitialized, 0);
}

/*________________________________________________________________
  LTCoreBSP global functions (statically linked only with LTCore) */
bool LT_ISR_SAFE
LTCoreBSP_InsideInterruptContext(void) {
    return LTAtomic_Load(&s_InsideInterruptContext);
}

LT_SIZE LT_ISR_SAFE
LTCoreBSP_DisableInterrupts(void) {
    pthread_mutex_lock(&s_disableMutex);
    if ((pthread_t)0 == s_disableThread) s_disableThread = pthread_self();
    return ++s_nDisableCount;
}

void LT_ISR_SAFE
LTCoreBSP_EnableInterrupts(LT_SIZE nMask) {
    pthread_mutex_lock(&s_disableMutex);
    if (!pthread_equal(pthread_self(), s_disableThread)) {
        BSP_LTLOG_STOMP("enable", "called by thread that did not Disable");
        pthread_mutex_unlock(&s_disableMutex);
        return;
    }

    if ((int)nMask != s_nDisableCount) {
        BSP_LTLOG_STOMP("enable", "inconsistent disable order");
    }

    if (0 == --s_nDisableCount) {
        s_disableThread = (pthread_t)0;
    }

    pthread_mutex_unlock(&s_disableMutex);
    pthread_mutex_unlock(&s_disableMutex);
}

bool LT_ISR_SAFE
LTCoreBSP_InterruptsAreDisabled(void) {
    if (0 != pthread_mutex_trylock(&s_disableMutex)) {
        return true;  /* Lock already held: interrupts disabled */
    }
    bool disabled = (pthread_t)0 != s_disableThread && 0 != s_nDisableCount;
    pthread_mutex_unlock(&s_disableMutex);
    return disabled;
}

void
LTCoreBSP_DebugBreak(void) {
    BSP_LTLOG_STOMP("break", "triggering trap to force a core dump");
    __builtin_trap();
}

/* _______________________________________________
 * static functions placed into struct LTCoreBSP */
/*_________________________
  high frequency counter */
static s64 LTCoreBSP_GetHighFrequencyCounterNanoseconds(void) LT_ISR_SAFE {
    struct timespec ts; clock_gettime(s_clockID, &ts);
    return (LTCoreBSP_SecondsToNanoseconds(ts.tv_sec) + (s64)ts.tv_nsec) - s_nHighFrequencyCounterInitial;
}

static s64 LT_ISR_SAFE LTCoreBSP_GetHighFrequencyCounterNanosecondResolution(void) {
    return s_nHighFrequencyCounterResolution;
}

/*____________
  debugging */
static bool LTCoreBSP_DebugAssertFailed(const char * pFile, int nLine, const char * pTest) LT_ISR_SAFE {
    LT_UNUSED(pFile); LT_UNUSED(nLine); LT_UNUSED(pTest);
    return true;
}

/* malloc, realloc, and free */
static void * LTHostAPI_malloc(LT_SIZE nBytes)                  { return malloc(nBytes); }
static void * LTHostAPI_realloc(void * pMem, LT_SIZE nBytes)    { return realloc(pMem, nBytes); }
static void   LTHostAPI_free(void * pMem)                       { free(pMem); }

/*________________
  ram functions */
static LT_SIZE LTHostAPI_GetTotalSystemRAM(void)        { return (LT_SIZE)0; }
static LT_SIZE LTHostAPI_GetAvailableSystemRAM(void)    { return (LT_SIZE)0; }
static LT_SIZE LTHostAPI_GetSystemRAMLowWatermark(void) { return (LT_SIZE)0; }

static void LTCoreBSP_EnterHostOS(void) {
    struct AppleThreadInstanceData* pThread = pthread_getspecific(s_keyThreadLocal);
    if (pThread) LTCoreBSP_ReleaseCPU(pThread, false);
}

static void LTCoreBSP_LeaveHostOS(void) {
    struct AppleThreadInstanceData* pThread = pthread_getspecific(s_keyThreadLocal);
    if (pThread) LTCoreBSP_TakeCPU(pThread);
}

#define BSP_BLOCKING_CALL(exp) ({          \
    typeof (exp) _rc;                      \
    do {                                   \
        LTCoreBSP_EnterHostOS();           \
        _rc = (exp);                       \
        LTCoreBSP_LeaveHostOS();           \
    } while (0);                           \
    _rc; })


static int LTCoreBSP_YieldUntilLock(pthread_mutex_t *mutex) {
    if (pthread_mutex_trylock(mutex) == 0) return 0;
    LTCoreBSP_EnterHostOS();
    int ret = pthread_mutex_lock(mutex);
    LTCoreBSP_LeaveHostOS();
    return ret;
}

/*________
  mutex */
static LT_SIZE
LTHostAPI_MutexInstanceSize(void) {
    return sizeof(pthread_mutex_t);
}

static void
LTHostAPI_MutexInitialize(void * pMutex) {
    pthread_mutexattr_t mutexAttrs;
    if (0 != pthread_mutexattr_init(&mutexAttrs)) return;
    pthread_mutexattr_settype(&mutexAttrs, PTHREAD_MUTEX_RECURSIVE);
    if (0 != pthread_mutex_init(pMutex, &mutexAttrs)) {
        BSP_LTLOG_STOMP("mutex.init", "failed");
    }
    pthread_mutexattr_destroy(&mutexAttrs);
}

static void
LTHostAPI_MutexFinalize(void * pMutex) {
    pthread_mutex_destroy((pthread_mutex_t *)pMutex);
}

static void
LTHostAPI_MutexLock(void * pMutex) {
    LTCoreBSP_YieldUntilLock((pthread_mutex_t *)pMutex);
}

static void
LTHostAPI_MutexUnlock(void * pMutex) {
    pthread_mutex_unlock((pthread_mutex_t *)pMutex);
    LTCoreBSP_Yield();
}

static bool
LTHostAPI_MutexTryLock(void * pMutex) {
    return (0 == pthread_mutex_trylock((pthread_mutex_t *)pMutex)) ? true : false;
}

/*__________
  monitor */
struct MonitorData {
    pthread_mutex_t m_mutex;
    pthread_cond_t  m_condition;
    u32             m_nFlags;
};
#define MONITOR_FLAG_VALID              (1 << 0)
#define MONITOR_FLAG_SIGNALLED          (1 << 1)
LT_INLINE bool MonitorIsValid(struct MonitorData * pData)        { return (pData->m_nFlags & MONITOR_FLAG_VALID); }
LT_INLINE void MonitorSetValid(struct MonitorData * pData)       { pData->m_nFlags |= MONITOR_FLAG_VALID; }

static LT_SIZE
LTHostAPI_MonitorInstanceSize(void) {
    return sizeof(struct MonitorData);
}

static void
LTHostAPI_MonitorInitialize(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;

    pthread_mutexattr_t mutexAttrs;
    pData->m_nFlags = 0;

    bool bMutexInitialized = false;

    do
    {
        if (0 != pthread_mutexattr_init(&mutexAttrs)) break;
        pthread_mutexattr_settype(&mutexAttrs, PTHREAD_MUTEX_ERRORCHECK);
        if (false == (bMutexInitialized = (0 == pthread_mutex_init(&pData->m_mutex, &mutexAttrs)))) {
            pthread_mutexattr_destroy(&mutexAttrs);
            break;
        }
        pthread_mutexattr_destroy(&mutexAttrs);
        /* macOS does not support pthread_condattr_setclock; use default clock */
        if (0 != pthread_cond_init(&pData->m_condition, NULL)) break;
        MonitorSetValid(pData);
    } while (false);

    if (false == MonitorIsValid(pData)) {
        if (bMutexInitialized) pthread_mutex_destroy(&pData->m_mutex);
    }
}

static void
LTHostAPI_MonitorFinalize(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData))
    {
        pthread_cond_destroy(&pData->m_condition);
        pthread_mutex_destroy(&pData->m_mutex);
        pData->m_nFlags = 0;
    }
}

static void
LTHostAPI_MonitorEnter(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) {
        switch (LTCoreBSP_YieldUntilLock(&pData->m_mutex)) {
            case 0:         return;
            case EDEADLK:   return;
            default:        return;
        }
    }
}

static void
LTHostAPI_MonitorExit(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) {
        int ret = pthread_mutex_unlock(&pData->m_mutex);
        LTCoreBSP_Yield();
        (void)ret;
    }
}

static void LT_ISR_SAFE
LTHostAPI_MonitorNotify(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) pthread_cond_signal(&pData->m_condition);
}

static bool
LTHostAPI_MonitorWait(void * pMonitor, s64 nTimeoutNanoseconds) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) {
        LTCoreBSP_EnterHostOS();
        if (LTCoreBSP_NanosecondsIsInfinite(nTimeoutNanoseconds)) {
            int ret = pthread_cond_wait(&pData->m_condition, &pData->m_mutex);
            LTCoreBSP_LeaveHostOS();
            return (0 == ret);
        }
        else {
            /* macOS uses gettimeofday-based absolute time for pthread_cond_timedwait */
            struct timespec timeSpec;
            clock_gettime(CLOCK_REALTIME, &timeSpec);

            nTimeoutNanoseconds += LTCoreBSP_SecondsToNanoseconds(timeSpec.tv_sec);
            nTimeoutNanoseconds += timeSpec.tv_nsec;

            timeSpec.tv_sec = (time_t)LTCoreBSP_NanosecondsToSeconds(nTimeoutNanoseconds);
            nTimeoutNanoseconds -= LTCoreBSP_SecondsToNanoseconds(timeSpec.tv_sec);
            timeSpec.tv_nsec = (long)nTimeoutNanoseconds;

            int ret = pthread_cond_timedwait(&pData->m_condition, &pData->m_mutex, &timeSpec);
            LTCoreBSP_LeaveHostOS();
            return (0 == ret);
        }
    }
    return false;
}

/*_________
  thread */
struct AppleThreadInstanceData {
    pthread_t m_thread;
    void (*   m_pThreadProc)(void * pClientData);
    void  *   m_pClientData;
    bool      m_bCreated;
    bool      m_bStarted;
    u8        m_nPriority;
};

static int LTCoreBSP_MaxReadyPriority(void) {
    while (s_nMaxReadyPriority > 0 && s_numPriorityReady[s_nMaxReadyPriority] == 0) s_nMaxReadyPriority--;
    return s_nMaxReadyPriority;
}

static void LTCoreBSP_TakeCPU(struct AppleThreadInstanceData *pThread) {
    pthread_mutex_lock(&s_mutex);
    if (!pThread->m_bStarted) {
        pThread->m_bStarted = true;
        pthread_cond_broadcast(&s_condition);
    }

    if (pThread->m_nPriority == LT_U8_MAX) {
        pthread_mutex_unlock(&s_mutex);
        return;
    }

    if (pThread->m_nPriority > s_nMaxReadyPriority) s_nMaxReadyPriority = pThread->m_nPriority;

    while (true) {
        int nThreadPriority = pThread->m_nPriority;
        if (s_pCurrentThread == NULL && nThreadPriority >= LTCoreBSP_MaxReadyPriority() &&
            (s_disableThread ? pthread_equal(pThread->m_thread, s_disableThread) : true)) break;

        s_numPriorityReady[nThreadPriority]++;
        pthread_cond_wait(&s_condition, &s_mutex);
        s_numPriorityReady[nThreadPriority]--;
    }
    s_pCurrentThread = pThread;
    pthread_mutex_unlock(&s_mutex);
}

static bool LTCoreBSP_ReleaseCPU(struct AppleThreadInstanceData *pThread, bool abortIfHighestPriority) {
    pthread_mutex_lock(&s_mutex);
    if (abortIfHighestPriority && pThread->m_nPriority >= LTCoreBSP_MaxReadyPriority()) {
        pthread_mutex_unlock(&s_mutex);
        return false;
    }

    if (pThread->m_nPriority == LT_U8_MAX) {
        pthread_mutex_unlock(&s_mutex);
        return true;
    }

    s_pCurrentThread = NULL;
    pthread_cond_broadcast(&s_condition);
    pthread_mutex_unlock(&s_mutex);
    return true;
}

static void * LTCoreBSP_OSThread_ThreadProc(void * pClientData) {
    pthread_mutex_lock(&s_mutex);
    pthread_mutex_unlock(&s_mutex); /* synchronize thread execution with creation */
    pthread_setspecific(s_keyThreadLocal, pClientData);
    LTCoreBSP_TakeCPU((struct AppleThreadInstanceData *)pClientData);
    ((struct AppleThreadInstanceData *)pClientData)->m_pThreadProc(((struct AppleThreadInstanceData *)pClientData)->m_pClientData);
    LTCoreBSP_ReleaseCPU((struct AppleThreadInstanceData *)pClientData, false);
    pthread_setspecific(s_keyThreadLocal, NULL);
    return 0;
}

static LT_SIZE
LTHostAPI_ThreadInstanceSize(void) {
    return sizeof(struct AppleThreadInstanceData);
}

static void
LTHostAPI_ThreadInitializeAndStartScheduler(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pInitialThreadProc)(void * pClientData), void * pClientData) {
    LT_UNUSED(pName);
    struct AppleThreadInstanceData * pInstanceData = (struct AppleThreadInstanceData *)pThread;

    if (LTHostAPI_ThreadInitializeAndRun(pThread, nPriority, nStackSize, pName, pInitialThreadProc, pClientData)) {
        pthread_join(pInstanceData->m_thread, NULL);
    }
    pInstanceData->m_thread = 0;
    pInstanceData->m_pThreadProc = 0;
    pInstanceData->m_pClientData = NULL;
    pInstanceData->m_bCreated = false;
}

static void
LTHostAPI_ThreadStopScheduler(void) {
    /* nothing to do */
}

static bool
LTHostAPI_ThreadInitializeAndRun(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pThreadProc)(void * pClientData), void * pClientData) {
    struct AppleThreadInstanceData * pInstanceData = (struct AppleThreadInstanceData *)pThread;
    bool bAttrsInitialized = false;
    pthread_attr_t attrs;
    int nErr = 0;

    pInstanceData->m_thread       = 0;
    pInstanceData->m_pThreadProc  = pThreadProc;
    pInstanceData->m_pClientData  = pClientData;
    pInstanceData->m_bCreated     = false;
    pInstanceData->m_bStarted     = false;
    pInstanceData->m_nPriority    = nPriority;

    nStackSize = (nStackSize < LTCOREBSP_MIN_PTHREAD_STACK_SIZE) ? LTCOREBSP_MIN_PTHREAD_STACK_SIZE : LTCOREBSP_ROUND_UP_TO_PAGE_MULTIPLE(nStackSize);

    do
    {
        if (0 != pthread_attr_init(&attrs)) break;
        bAttrsInitialized = true;
        if (0 != pthread_attr_setstacksize(&attrs, (size_t)nStackSize)) break;

        if (NULL == pName || 0 == *pName) pName = "unnamed";

        pthread_mutex_lock(&s_mutex); /* make the spawned thread block until we can set its name */
        if (0 != (nErr = pthread_create(&pInstanceData->m_thread, &attrs, &LTCoreBSP_OSThread_ThreadProc, pInstanceData))) {
            pInstanceData->m_thread = 0;
            pthread_mutex_unlock(&s_mutex);
            break;
        }
        /* macOS pthread_setname_np only sets the current thread's name; use it from the thread proc instead */
        pInstanceData->m_bCreated = true;
        pthread_mutex_unlock(&s_mutex);
    }
    while (false);

    if (bAttrsInitialized) pthread_attr_destroy(&attrs);

    if (pInstanceData->m_bCreated) {
        pthread_mutex_lock(&s_mutex);
        while (!pInstanceData->m_bStarted) {
            pthread_cond_wait(&s_condition, &s_mutex);
        }
        pthread_mutex_unlock(&s_mutex);
        LTCoreBSP_Yield();
    } else {
        pInstanceData->m_thread = 0;
        pInstanceData->m_pThreadProc = NULL;
        pInstanceData->m_pClientData = NULL;
        BSP_LTLOG("thread", "create %s failed: %s", pName, nErr == EAGAIN ? kEAGAINMessage : (nErr == EINVAL ? kEINVALMessage : "???"));
    }

    return pInstanceData->m_bCreated;
}

static void
LTHostAPI_ThreadGetStackUsage(void * pThread, u32 * pStackSizeToSet, u32 * pCurrentStackUsageToSet, u32 * pMaxStackUsageToSet) {
    LT_UNUSED(pThread);
    *pStackSizeToSet = *pCurrentStackUsageToSet = *pMaxStackUsageToSet = 0;
}

static void *
LTHostAPI_ThreadGetCurrentThreadInstanceData(void) {
    return pthread_getspecific(s_keyThreadLocal);
}

static void
LTHostAPI_ThreadSleep(s64 nNanoseconds) {
    struct timespec delay;
    delay.tv_sec = (time_t)LTCoreBSP_NanosecondsToSeconds(nNanoseconds);
    delay.tv_nsec = (long)(nNanoseconds - LTCoreBSP_SecondsToNanoseconds(delay.tv_sec));
    struct timespec remaining;
    if (0 == nNanoseconds) {
        delay.tv_nsec = (long)LTCoreBSP_MillisecondsToNanoseconds(10);
    }
    LTCoreBSP_EnterHostOS();
    while (0 != nanosleep(&delay, &remaining)) delay = remaining;
    LTCoreBSP_LeaveHostOS();
}

static bool
LTHostAPI_ThreadSetPriority(void * pThread, u8 nPriority) {
    struct AppleThreadInstanceData * pInstanceData = (struct AppleThreadInstanceData *)pThread;
    if (pInstanceData->m_bCreated) {
        pthread_mutex_lock(&s_mutex);
        pInstanceData->m_nPriority = nPriority;
        pthread_mutex_unlock(&s_mutex);
        LTCoreBSP_Yield();
        return true;
    }
    return false;
}

static bool
LTHostAPI_ThreadWaitUntilFinished(void * pThread, s64 nTimeoutNanoseconds) {
    struct AppleThreadInstanceData * pInstanceData = (struct AppleThreadInstanceData *)pThread;
    LT_ASSERT(pInstanceData);
    if (!pInstanceData->m_bCreated) return true;

    LTCoreBSP_EnterHostOS();
    /* macOS does not have pthread_timedjoin_np; use pthread_join for all cases */
    LT_UNUSED(nTimeoutNanoseconds);
    int error = pthread_join(pInstanceData->m_thread, NULL);
    LTCoreBSP_LeaveHostOS();
    if (error != 0) return false;

    pInstanceData->m_thread = 0;
    pInstanceData->m_bCreated = false;
    return true;
}

static void
LTHostAPI_ThreadFinalize(void * pThread) {
    struct AppleThreadInstanceData * pInstanceData = (struct AppleThreadInstanceData *)pThread;
    pInstanceData->m_thread = 0;
    pInstanceData->m_pThreadProc = NULL;
    pInstanceData->m_pClientData = NULL;
    pInstanceData->m_bCreated = false;
}

/*_____________________________
  LTHostAPI interface struct */
static const LTHostAPI s_host = {
    .malloc                   = LTHostAPI_malloc,
    .realloc                  = LTHostAPI_realloc,
    .free                     = LTHostAPI_free,

    .GetTotalSystemRAM        = LTHostAPI_GetTotalSystemRAM,
    .GetAvailableSystemRAM    = LTHostAPI_GetAvailableSystemRAM,
    .GetSystemRAMLowWatermark = LTHostAPI_GetSystemRAMLowWatermark,

    .MutexInstanceSize        = LTHostAPI_MutexInstanceSize,
    .MutexInitialize          = LTHostAPI_MutexInitialize,
    .MutexFinalize            = LTHostAPI_MutexFinalize,
    .MutexLock                = LTHostAPI_MutexLock,
    .MutexUnlock              = LTHostAPI_MutexUnlock,
    .MutexTryLock             = LTHostAPI_MutexTryLock,

    .MonitorInstanceSize      = LTHostAPI_MonitorInstanceSize,
    .MonitorInitialize        = LTHostAPI_MonitorInitialize,
    .MonitorFinalize          = LTHostAPI_MonitorFinalize,
    .MonitorEnter             = LTHostAPI_MonitorEnter,
    .MonitorExit              = LTHostAPI_MonitorExit,
    .MonitorNotify            = LTHostAPI_MonitorNotify,
    .MonitorWait              = LTHostAPI_MonitorWait,

    .ThreadInstanceSize       = LTHostAPI_ThreadInstanceSize,
    .ThreadInitializeAndStartScheduler = LTHostAPI_ThreadInitializeAndStartScheduler,
    .ThreadStopScheduler      = LTHostAPI_ThreadStopScheduler,
    .ThreadInitializeAndRun   = LTHostAPI_ThreadInitializeAndRun,
    .ThreadGetStackUsage      = LTHostAPI_ThreadGetStackUsage,
    .ThreadSetPriority        = LTHostAPI_ThreadSetPriority,
    .ThreadWaitUntilFinished  = LTHostAPI_ThreadWaitUntilFinished,
    .ThreadFinalize           = LTHostAPI_ThreadFinalize,
    .ThreadGetCurrentThreadInstanceData = LTHostAPI_ThreadGetCurrentThreadInstanceData,
    .ThreadSleep              = LTHostAPI_ThreadSleep,
};

/*_____________________________
  LTCoreBSP interface struct */
static const LTCoreBSP s_bsp = {
    .GetHighFrequencyCounterNanoseconds          = LTCoreBSP_GetHighFrequencyCounterNanoseconds,
    .GetHighFrequencyCounterNanosecondResolution = LTCoreBSP_GetHighFrequencyCounterNanosecondResolution,

    .PutCharsToConsole = LTCoreBSP_PutCharsToConsole,
    .DebugAssertFailed = LTCoreBSP_DebugAssertFailed,
    .hostAPI           = &s_host
};

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  23-Mar-26   created     from LTCoreBSP_LinuxTools.c, adapted for macOS
 */
