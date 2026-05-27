/******************************************************************************
 * platforms/linux/source/linux/ltcorebsp/LTCoreBSP_Linux_Cloud.c
 *                                          - LTCoreBSP for Linux
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#define _GNU_SOURCE
#include <dlfcn.h>          // for dlopen, dlsym, dlclose
#include <stdio.h>          // for putchar
#include <stddef.h>         // for ptrdiff_t
#include <pthread.h>
#include <stdlib.h>
#include <asm/errno.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <libgen.h>

#include <lt/core/LTStdlib.h>
#include <lt/core/LTCore.h>
#include <lt/core/bsp/LTHostAPI.h>
#include <lt/core/bsp/LTCoreBSP.h>

/*___________
  #defines */
#define LTCOREBSP_MIN_PTHREAD_STACK_SIZE                (PTHREAD_STACK_MIN)     /* man page says 16K minimum stack size, empirically verified, 20K required to use printf */
#define LTCOREBSP_STACK_PAGE_SIZE                       (512)                   /* man page says stack size must be page size multiple but doesn't say page size; epirically determined to be 512 */
#define LTCOREBSP_ROUND_UP_TO_PAGE_MULTIPLE(nStackSize) ((nStackSize + (LTCOREBSP_STACK_PAGE_SIZE-1)) & ~(LTCOREBSP_STACK_PAGE_SIZE-1))
#define LTCOREBSP_ISR_PRIORITY                          (31)
#define LTCOREBSP_MAX_PRIORITY                          (LTCOREBSP_ISR_PRIORITY)
#define ENABLE_KEY_INPUT                                0

DEFINE_BSP_LTLOG_SECTION("linuxtools.bsp");

/*_______________________
  forward declarations */
static const LTCoreBSP s_bsp;
static bool  LTHostAPI_ThreadInitializeAndRun(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pThreadProc)(void * pClientData), void * pClientData);
static bool  LTHostAPI_ThreadSetPriority(void * pThread, u8 nPriority);
struct       LinuxThreadInstanceData;
static bool  LTCoreBSP_ReleaseCPU(struct LinuxThreadInstanceData *pThread, bool abortIfHighestPriority);
static void  LTCoreBSP_TakeCPU(struct LinuxThreadInstanceData *pThread);
#if ENABLE_KEY_INPUT
static void  InitPseudoKeyInputISR(void);
static void  ExitPseudoKeyInputISR(void);
#endif

/*___________________
  static variables */
static const LTCoreBSP_LTCoreCallbacks *    s_pCoreCallbacks = NULL;
static LTAtomic                             s_LTCoreBSPInitialized = { 0 };
static pthread_mutex_t                      s_mutex;
static clockid_t                            s_clockID = CLOCK_MONOTONIC_RAW;
static s64                                  s_nHighFrequencyCounterResolution = 0;
static s64                                  s_nHighFrequencyCounterInitial = 0;
static pthread_key_t                        s_keyThreadLocal = (pthread_key_t)0;
static pthread_mutex_t                      s_disableMutex;
static pthread_t                            s_disableThread = (pthread_t)0;
static int                                  s_nDisableCount = 0;
static LTAtomic                             s_InsideInterruptContext = { 0 };

static int                                  s_numPriorityReady[LTCOREBSP_MAX_PRIORITY+1];
static struct LinuxThreadInstanceData*      s_pCurrentThread = NULL;
static int                                  s_nMaxReadyPriority = -1;
static pthread_cond_t                       s_condition;

#if ENABLE_KEY_INPUT
static pthread_t                            s_threadKeyInput = (pthread_t)0;
static struct termios                       s_termiosNormal;
static struct termios                       s_termiosRaw;
#endif

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

/*_____________________________________________________________________________________________________________
  unbuffered console putstring - use only during bsp initialize; otherwise use BSP_LTLOG and BSP_LTLOG_DEBUG */
static void LT_ISR_SAFE LTCoreBSP_ConsolePutString(const char * pString) {
    LTCoreBSP_PutCharsToConsole(pString, strlen(pString));
}

static void LTCoreBSP_Yield(void) {
    struct LinuxThreadInstanceData* pThread =  pthread_getspecific(s_keyThreadLocal);
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

    if (LTAtomic_Load(&s_LTCoreBSPInitialized)) return NULL; /* don't let anyone come in here except LTCore the first time */
    LTAtomic_Store(&s_LTCoreBSPInitialized, 1); /* don't need CompareAndExchange, LTCore calls this before any threads are running */

    /* change our cpu affinity to always run on core 0; all created pthreads will inherit this affinity and run on core 0
            This is a short term solution; after sweeping all cases for interrupt disable and making them not rely
            on interrupt disable for disabling task switching and mutual exclusion with other threads and sweeping
            to eradicate assumptions about using thread priorities for thread sequencing, we take this out */
    cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(0, &cpuset);
    if (0 != pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset)) LTCoreBSP_ConsolePutString("[linuxtools.bsp.init] pthread_setaqffinity_np failed\\n");

    // initialize bsp log
    BSP_LTLOG_INITIALIZE(pCallbacks->LTCoreLogFunction);

    // remember pCallbacks
    s_pCoreCallbacks = pCallbacks;
    atexit(&OnProcessExit);

    /* init s_mutex */
    pthread_mutexattr_t mutexAttrs = {{ PTHREAD_MUTEX_FAST_NP }};
    pthread_mutex_init(&s_mutex, &mutexAttrs);

    pthread_condattr_t condAttrs;
    pthread_cond_init(&s_condition, &condAttrs);

    /* init LTCoreBSP_OS_GetHighFrequencyCounterNanoseconds() */
    /* figure out what clock we have available from CLOCK_MONOTONIC, CLOCK_MONOTONIC_RAW, and CLOCK_BOOTTIME */
    struct timespec ts;
    bool bGotTime = false;

    if (0 == clock_getres(CLOCK_MONOTONIC_RAW, &ts)) {
        s_nHighFrequencyCounterResolution = LTCoreBSP_SecondsToNanoseconds(ts.tv_sec) + (s64)ts.tv_nsec;
        if (0 == clock_gettime(CLOCK_MONOTONIC_RAW, &ts)) {
            s_nHighFrequencyCounterInitial = LTCoreBSP_SecondsToNanoseconds(ts.tv_sec) + (s64)ts.tv_nsec;
            s_clockID = CLOCK_MONOTONIC_RAW;
            bGotTime = true;
        } // else s_logger.LogDebug("gettime.fail.monotonicraw");
    } // else s_logger.LogDebug("getres.fail.monotonicraw");
    if (! bGotTime) {
        if (0 == clock_getres(CLOCK_MONOTONIC, &ts)) {
            s_nHighFrequencyCounterResolution = LTCoreBSP_SecondsToNanoseconds(ts.tv_sec) + (s64)ts.tv_nsec;
            if (0 == clock_gettime(CLOCK_MONOTONIC, &ts)) {
                s_nHighFrequencyCounterInitial = LTCoreBSP_SecondsToNanoseconds(ts.tv_sec) + (s64)ts.tv_nsec;
                s_clockID = CLOCK_MONOTONIC;
                bGotTime = true;
            } // else s_logger.LogDebug("gettime.fail.monotonic");
        } // else s_logger.LogDebug("getres.fail.monotonic");
    }
    if (! bGotTime) {
        if (0 == clock_getres(CLOCK_BOOTTIME, &ts)) {
            s_nHighFrequencyCounterResolution = LTCoreBSP_SecondsToNanoseconds(ts.tv_sec) + (s64)ts.tv_nsec;
            if (0 == clock_gettime(CLOCK_BOOTTIME, &ts)) {
                s_nHighFrequencyCounterInitial = LTCoreBSP_SecondsToNanoseconds(ts.tv_sec) + (s64)ts.tv_nsec;
                s_clockID = CLOCK_BOOTTIME;
                bGotTime = true;
            } // else s_logger.LogDebug("gettime.fail.boottime");
        } // else s_logger.LogDebug("getres.fail.boottime");
    }
    // if (! bGotTime) s_logger.YellowAlert("kerneltime.init.fail");

    /* init threads */
    pthread_key_create(&s_keyThreadLocal, NULL);

    /* init Disable/Enable mutex */
    pthread_mutexattr_t disableMutexAttrs = {{ PTHREAD_MUTEX_RECURSIVE_NP }};
    pthread_mutex_init(&s_disableMutex, &disableMutexAttrs);

#if ENABLE_KEY_INPUT
    /* init pseudo key input (emulated UART) ISR */
    InitPseudoKeyInputISR();
#endif

    return &s_bsp;
}

void
LTCoreBSP_Finalize(const LTCoreBSP * pBSP) {
    if ((! LTAtomic_Load(&s_LTCoreBSPInitialized)) || (pBSP != &s_bsp)) return; /* don't let anyone except LTCore in here */

#if ENABLE_KEY_INPUT
    /* exit pseudo key input (emulated UART) ISR */
    ExitPseudoKeyInputISR();
#endif

    pthread_key_delete(s_keyThreadLocal);
    s_keyThreadLocal = (pthread_key_t)0;
    s_nHighFrequencyCounterInitial = 0;
    s_nHighFrequencyCounterResolution = 0;
    s_clockID = CLOCK_MONOTONIC_RAW;
    pthread_mutex_destroy(&s_mutex);
    pthread_mutex_destroy(&s_disableMutex);

    s_pCoreCallbacks = NULL;
    LTAtomic_Store(&s_LTCoreBSPInitialized, 0);
}

/*________________________________________________________________
  LTCoreBSP global functions (statically linked only with LTCore) \________________________________________________
  Excluded from s_bsp struct to enable direct placement into LTCore and LTStdlib interfaces to avoid extra thunk */
bool LT_ISR_SAFE
LTCoreBSP_InsideInterruptContext(void) {
    return LTAtomic_Load(&s_InsideInterruptContext);
}

LT_SIZE LT_ISR_SAFE
LTCoreBSP_DisableInterrupts(void) {
    pthread_mutex_lock(&s_disableMutex);
    if ((pthread_t)0 == s_disableThread) s_disableThread = pthread_self();
    /* return from this function with the mutex locked; we unlock it in Enable() */
    return ++s_nDisableCount;
}

void LT_ISR_SAFE
LTCoreBSP_EnableInterrupts(LT_SIZE nMask) {
    /* lock and unlock to make sure I'm the thread that did the Disable() */
    pthread_mutex_lock(&s_disableMutex);
    if (pthread_self() != s_disableThread) {
        BSP_LTLOG_STOMP("enable", "called by thread that did not Disable");
        pthread_mutex_unlock(&s_disableMutex);
        return;
    }

    if ((int)nMask != s_nDisableCount) {
        BSP_LTLOG_STOMP("enable", "inconsistent disable order");
    }

    /* I have the mutex locked at least once from doing the Disable() call */
    if (0 == --s_nDisableCount) {
        s_disableThread = (pthread_t)0;
    }

    /* unlock once for this function and once for this enable's disable */
    pthread_mutex_unlock(&s_disableMutex);
    pthread_mutex_unlock(&s_disableMutex);
}

bool LT_ISR_SAFE
LTCoreBSP_InterruptsAreDisabled(void) {
    switch (pthread_mutex_trylock(&s_disableMutex)) {
        case 0:         break;              /* Acquired the lock.  Check for interrupts disabled below. */
        case EBUSY:     return true;        /* Lock already required.  Interrupts disabled.             */
        default:        BSP_LTLOG_STOMP("query.disablestate", "failed to determine interrupt status");
                        return false;       /* Error getting the lock.  Shouldn't ever happen:          */
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
static LT_SIZE LTHostAPI_GetTotalSystemRAM(void) { return (LT_SIZE)0; }
static LT_SIZE LTHostAPI_GetAvailableSystemRAM(void) { return (LT_SIZE)0; }
static LT_SIZE LTHostAPI_GetSystemRAMLowWatermark(void) { return (LT_SIZE)0; }

static void LTCoreBSP_EnterHostOS(void) {
    struct LinuxThreadInstanceData* pThread =  pthread_getspecific(s_keyThreadLocal);
    if (pThread) LTCoreBSP_ReleaseCPU(pThread, false);
}

static void LTCoreBSP_LeaveHostOS(void) {
    struct LinuxThreadInstanceData* pThread =  pthread_getspecific(s_keyThreadLocal);
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
    bool bAttrsInitialized = false;
    pthread_mutexattr_t mutexAttrs;
    do
    {
        if (false == (bAttrsInitialized = (0 == pthread_mutexattr_init(&mutexAttrs)))) break;
        if (0 != pthread_mutexattr_settype(&mutexAttrs, PTHREAD_MUTEX_RECURSIVE_NP)) break;
        //if (0 != pthread_mutexattr_setprotocol(&mutexAttrs, PTHREAD_PRIO_INHERIT)) break;
        //if (0 != pthread_mutexattr_setpshared(&mutexAttrs, PTHREAD_PROCESS_PRIVATE)) break;
        //if (0 != pthread_mutexattr_setrobust(&mutexAttrs, PTHREAD_MUTEX_ROBUST)) break;
        if (0 != pthread_mutex_init(pMutex, &mutexAttrs)) {
            BSP_LTLOG_STOMP("mutex.init", "failed");
        }

    } while (false);
    if (bAttrsInitialized) pthread_mutexattr_destroy(&mutexAttrs);
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
LT_INLINE bool MonitorIsSignalled(struct MonitorData * pData)    { return (pData->m_nFlags & MONITOR_FLAG_SIGNALLED); }
LT_INLINE void MonitorSetValid(struct MonitorData * pData)       { pData->m_nFlags |= MONITOR_FLAG_VALID; }
LT_INLINE void MonitorSetSignalled(struct MonitorData * pData)   { pData->m_nFlags |= MONITOR_FLAG_SIGNALLED; }
LT_INLINE void MonitorClearSignalled(struct MonitorData * pData) { pData->m_nFlags &= ~MONITOR_FLAG_SIGNALLED; }

static LT_SIZE
LTHostAPI_MonitorInstanceSize(void) {
    return sizeof(struct MonitorData);
}

static void
LTHostAPI_MonitorInitialize(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;

    pthread_mutexattr_t mutexAttrs = { { PTHREAD_MUTEX_ERRORCHECK_NP } };
    pthread_condattr_t condAttrs;

    bool bMutexInitialized = false;
    bool bCondAttrsInited = false;
    pData->m_nFlags = 0;

    do
    {
        if (false == (bMutexInitialized = (0 == pthread_mutex_init(&pData->m_mutex, &mutexAttrs)))) break;
        if (false == (bCondAttrsInited  = (0 == pthread_condattr_init(&condAttrs)))) break;
        if (0 != pthread_condattr_setclock(&condAttrs, CLOCK_MONOTONIC)) break;
        if (0 != pthread_cond_init(&pData->m_condition, &condAttrs)) break;
        MonitorSetValid(pData);
    } while (false);

    if (bCondAttrsInited) pthread_condattr_destroy(&condAttrs);
    if (false == MonitorIsValid(pData)) {
        if (bMutexInitialized) pthread_mutex_destroy(&pData->m_mutex);
        // s_logger.YellowAlert("createfailure");
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
            case EDEADLK:   /* s_logger.YellowAlert("enter.illegal.nest"); */              return;
            default:        /* s_logger.YellowAlert("enter.unknown.pthread.returncode"); */ return;
        }
    }
    // s_logger.YellowAlert("enter.invalidmonitor");
}

static void
LTHostAPI_MonitorExit(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) {
        int ret = pthread_mutex_unlock(&pData->m_mutex);
        LTCoreBSP_Yield();
        switch (ret) {
            case 0:         return;
            case EPERM:     /* s_logger.YellowAlert("exit.noenter"); */ return;
            default:        /* s_logger.YellowAlert("exit.unknown.pthread.returncode"); */ return;
        }
    }
    // s_logger.YellowAlert("exit.invalidmonitor");
}

static void LT_ISR_SAFE
LTHostAPI_MonitorNotify(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) pthread_cond_signal(&pData->m_condition);

    #ifdef LT_DEBUG
    else { /* s_logger.YellowAlert("notify.invalidmonitor"); */ }
    #endif
}

static bool
LTHostAPI_MonitorWait(void * pMonitor, s64 nTimeoutNanoseconds) {
    debug_printf("[ltcorebsp.os.linux.thread] monitor wait\n");
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) {
        LTCoreBSP_EnterHostOS();
        if (LTCoreBSP_NanosecondsIsInfinite(nTimeoutNanoseconds)) {
            int ret = pthread_cond_wait(&pData->m_condition, &pData->m_mutex);
            LTCoreBSP_LeaveHostOS();
            switch (ret) {
                case 0:         return true;
                case EPERM:     /* s_logger.YellowAlert("wait.noenter"); */ return false;
                default:        /* s_logger.YellowAlert("wait.unknown.pthread.returncode"); */ return false;
            }
        }
        else {
            // timed wait
            struct timespec timeSpec;
            if (0 == clock_gettime(CLOCK_MONOTONIC, &timeSpec))
            {
                // make the timeout absolute from now
                nTimeoutNanoseconds += LTCoreBSP_SecondsToNanoseconds(timeSpec.tv_sec);
                nTimeoutNanoseconds += timeSpec.tv_nsec;

                // convert absolute time to seconds and nanoseconds for struct timespec
                timeSpec.tv_sec = LTCoreBSP_NanosecondsToSeconds(nTimeoutNanoseconds);
                nTimeoutNanoseconds -= LTCoreBSP_SecondsToNanoseconds(timeSpec.tv_sec);
                timeSpec.tv_nsec = nTimeoutNanoseconds;

                int ret = pthread_cond_timedwait(&pData->m_condition, &pData->m_mutex, &timeSpec);
                LTCoreBSP_LeaveHostOS();
                switch (ret) {
                    case 0:         return true;
                    case ETIMEDOUT: return false;
                    case EPERM:     /* s_logger.YellowAlert("timedwait.noenter"); */  return false;
                    default:        /* s_logger.YellowAlert("timedwait.unknown.pthread.returncode"); */ return false;
                }
            }
            else {
                /* s_logger.YellowAlert("timedwait.clock_gettime.failure"); */
                LTCoreBSP_LeaveHostOS();
                return false;
            }
        }
    }
    /* s_logger.YellowAlert("wait.invalidmonitor"); */
    return false;
}

/*_________
  thread */
struct LinuxThreadInstanceData {
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

static void LTCoreBSP_TakeCPU(struct LinuxThreadInstanceData *pThread) {
    pthread_mutex_lock(&s_mutex);
    if (!pThread->m_bStarted) {
        // Let spawning thread know we have now processed the initial priority evaluation.
        pThread->m_bStarted = true;
        pthread_cond_broadcast(&s_condition);
    }

    // Ignore priority for driver threads marked with priority 0xFF.
    if (pThread->m_nPriority == LT_U8_MAX) {
        pthread_mutex_unlock(&s_mutex);
        return;
    }

    // Bump highest known priority if larger.
    if (pThread->m_nPriority > s_nMaxReadyPriority) s_nMaxReadyPriority = pThread->m_nPriority;

    debug_printf("[linuxtools.bsp.init] want CPU with priority %d (current %d)\n", pThread->m_nPriority, s_pCurrentThread ? s_pCurrentThread->m_nPriority : -1);

    // Loop until we get processing time. This happens iff we
    // * there is no current processing thread
    // * have the highest priority
    // * there is no thread disabling interrupts (unless it's us).
    while (true) {
        int nThreadPriority = pThread->m_nPriority;
        if (s_pCurrentThread == NULL && nThreadPriority >= LTCoreBSP_MaxReadyPriority() &&
            (s_disableThread ? pThread->m_thread == s_disableThread : true)) break;

        // We failed to get processing time. To back to sleep but insert ourself
        // into the ready "queue" first.
        s_numPriorityReady[nThreadPriority]++;
        pthread_cond_wait(&s_condition, &s_mutex);
        s_numPriorityReady[nThreadPriority]--;
    }
    s_pCurrentThread = pThread;
    debug_printf("[linuxtools.bsp.takecpu] got CPU with priority %d\n", pThread->m_nPriority);
    pthread_mutex_unlock(&s_mutex);
}

// Returns true if the CPU was successfully released. That can only happen if we are allowed
// to abort if we are the highest priority.
static bool LTCoreBSP_ReleaseCPU(struct LinuxThreadInstanceData *pThread, bool abortIfHighestPriority) {
    pthread_mutex_lock(&s_mutex);
    if (abortIfHighestPriority && pThread->m_nPriority >= LTCoreBSP_MaxReadyPriority()) {
        pthread_mutex_unlock(&s_mutex);
        return false;
    }

    if (pThread->m_nPriority == LT_U8_MAX) {
        pthread_mutex_unlock(&s_mutex);
        return true;
    }

    debug_printf("[linuxtools.bsp.releasecpu] release CPU with priority %d\n", pThread->m_nPriority);
    s_pCurrentThread = NULL;
    pthread_cond_broadcast(&s_condition);
    pthread_mutex_unlock(&s_mutex);
    return true;
}

static void * LTCoreBSP_OSThread_LinuxThreadProc(void * pClientData) {
    pthread_mutex_lock(&s_mutex);
    pthread_mutex_unlock(&s_mutex); /* synchronize thread execution with creation */\
    pthread_setspecific(s_keyThreadLocal, pClientData);
    LTCoreBSP_TakeCPU((struct LinuxThreadInstanceData *)pClientData);
    ((struct LinuxThreadInstanceData *)pClientData)->m_pThreadProc(((struct LinuxThreadInstanceData *)pClientData)->m_pClientData);
    LTCoreBSP_ReleaseCPU((struct LinuxThreadInstanceData *)pClientData, false);
    pthread_setspecific(s_keyThreadLocal, NULL);
    return 0;
}

static LT_SIZE
LTHostAPI_ThreadInstanceSize(void) {
    return sizeof(struct LinuxThreadInstanceData);
}

static void
LTHostAPI_ThreadInitializeAndStartScheduler(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pInitialThreadProc)(void * pClientData), void * pClientData) {
    LT_UNUSED(pName);
    /* This function should do three things:
    1. perform any instance initialization of pThread.
    2. start the scheduler, running the initial thread as specified by the function parameters.
    3. block until the thread exits, then UNINITIALIZE THE INSTANCE DATA,  STOP THE SCHEDULER and return */
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)pThread;

    /* no need to start the scheduler on windows, just run the thread */
    if (LTHostAPI_ThreadInitializeAndRun(pThread, nPriority, nStackSize, pName, pInitialThreadProc, pClientData)) {
        /* and block until it finishes */
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
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)pThread;
    /* called to spawn threads after the scheduler has been started.  This function should do three things:
        1. perform any instance initialization of pThread.
        2. run the initial thread as specified by the function parms.
        3. return from this function immediately after spawning the thread. */
    bool bAttrsInitialized = false;
    pthread_attr_t attrs;
    int nErr = 0;

    pInstanceData->m_thread       = 0;
    pInstanceData->m_pThreadProc  = pThreadProc;
    pInstanceData->m_pClientData  = pClientData;
    pInstanceData->m_bCreated     = false;
    pInstanceData->m_bStarted     = false;
    pInstanceData->m_nPriority    = nPriority;

    /* pthreads won't create without a 16K minimum stack, and stack sizes have to be a multiple of the page size */
    nStackSize = (nStackSize < LTCOREBSP_MIN_PTHREAD_STACK_SIZE) ? LTCOREBSP_MIN_PTHREAD_STACK_SIZE : LTCOREBSP_ROUND_UP_TO_PAGE_MULTIPLE(nStackSize);

    do
    {
        if (0 != pthread_attr_init(&attrs)) break;
        bAttrsInitialized = true;
        if (0 != pthread_attr_setstacksize(&attrs, (size_t)nStackSize)) break;
        if (0 != pthread_attr_setinheritsched(&attrs, PTHREAD_EXPLICIT_SCHED)) break;

        if (NULL == pName || 0 == *pName) pName = "unnamed";
        /* setting thread name, somewhat elaborate */
        unsigned i; char name[16]; // must limit to 16 or pthread_setname_np will fail
        for (i = 0; i < (sizeof(name)-1); i++) if (pName[i]) name[i] = pName[i]; else break;
        name[i] = 0;
        pthread_mutex_lock(&s_mutex); /* to make the spawned thread block until we can set its name */
        if (0 != (nErr = pthread_create(&pInstanceData->m_thread, &attrs, &LTCoreBSP_OSThread_LinuxThreadProc, pInstanceData))) {
            pInstanceData->m_thread = 0;
            pthread_mutex_unlock(&s_mutex);
            break;
        }
        /* setting a pthread's name has to be done after it's created but we don't want the thread to run until
           we do that, hence the overloaded use of s_mutex.  */
        pthread_setname_np(pInstanceData->m_thread, name);
        pInstanceData->m_bCreated = true;
        pthread_mutex_unlock(&s_mutex);
    }
    while (false);

    if (bAttrsInitialized) pthread_attr_destroy(&attrs);

    if (pInstanceData->m_bCreated) {
        // Wait for the thread to be initiated.
        pthread_mutex_lock(&s_mutex);
        while (!pInstanceData->m_bStarted) {
            pthread_cond_wait(&s_condition, &s_mutex);
        }
        pthread_mutex_unlock(&s_mutex);

        // Yield to potentially new thread. If the new thread has lower priority, we'll continue if we are
        // still the highest priority.
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
    struct timespec delay = { (time_t)LTCoreBSP_NanosecondsToSeconds(nNanoseconds), (long)(nNanoseconds - LTCoreBSP_SecondsToNanoseconds(delay.tv_sec)) };
    struct timespec remaining;
    if (0 == nNanoseconds) {
        delay.tv_nsec = LTCoreBSP_MillisecondsToNanoseconds(10); /* 0 is supposed to mean Yield() but Linux doesn't Yield on 0 so hack in 10ms */
    }
    LTCoreBSP_EnterHostOS();
    while (0 != nanosleep(&delay, &remaining)) delay = remaining;
    LTCoreBSP_LeaveHostOS();
}

static bool
LTHostAPI_ThreadSetPriority(void * pThread, u8 nPriority) {
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)pThread;
    debug_printf("[linuxtools.bsp.thread.setprio] CHANGE THREAD PRIORITY %d -> %d\n", pInstanceData->m_nPriority, nPriority);
    if (pInstanceData->m_bCreated) {
        // First update the priority.
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
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)pThread;
    /* This function should block for [at least] nTimeoutNanoseconds and return true at the point when the thread
       completely finishes all execution, or false if the thread has not finished execution within the timeout period.
       By returning true, this function indicates it is now legal to subsequently call LTHostAPI_ThreadUninitializeInstance()
       on the thread object.  */
    LT_ASSERT(pInstanceData);
    if (!pInstanceData->m_bCreated) return true;

    int error;
    LTCoreBSP_EnterHostOS();
    if (LTCoreBSP_NanosecondsIsInfinite(nTimeoutNanoseconds)) {
        error = pthread_join(pInstanceData->m_thread, NULL);
    }
    else {
        struct timespec timeSpec;
        if (0 != clock_gettime(CLOCK_REALTIME, &timeSpec)) return false;
        nTimeoutNanoseconds += LTCoreBSP_SecondsToNanoseconds(timeSpec.tv_sec);
        nTimeoutNanoseconds += timeSpec.tv_nsec;
        timeSpec.tv_sec = (time_t)LTCoreBSP_NanosecondsToSeconds(nTimeoutNanoseconds);
        nTimeoutNanoseconds -= LTCoreBSP_SecondsToNanoseconds(timeSpec.tv_sec);
        timeSpec.tv_nsec = (long)nTimeoutNanoseconds;
        error = pthread_timedjoin_np(pInstanceData->m_thread, NULL, &timeSpec);
    }
    LTCoreBSP_LeaveHostOS();
    if (error != 0) return false;

    pInstanceData->m_thread = 0;
    pInstanceData->m_bCreated = false;
    return true;
}

static void
LTHostAPI_ThreadFinalize(void * pThread) {
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)pThread;
    /* called to uninitialize the private instance data (pThread))
       This function call will always be preceded by a call to LTHostAPI_ThreadWaitUntilThreadFinished() that returned
       a value of true, therefore it is guaranteed the thread is no longer running when this function is called. */
    pInstanceData->m_thread = 0;
    pInstanceData->m_pThreadProc = NULL;
    pInstanceData->m_pClientData = NULL;
    pInstanceData->m_bCreated = false;
}


#if ENABLE_KEY_INPUT
/*********************
 * PseudoKeyInputISR *
 *********************/
static void
SIGINT_SignalHandler(int nSignal) {
    signal(nSignal, SIG_IGN);                           /* prevent recursive signal */
    tcsetattr(STDIN_FILENO, TCSANOW, &s_termiosNormal); /* restore normal terminal attributes */
    signal(nSignal, SIG_DFL);                           /* install default signal handler*/
    raise(nSignal);                                     /* return to regularly scheduled programming */
}

static void *
LTCoreBSP_KeyInputPseudoISRThreadProc(void * pClientData) { LT_UNUSED(pClientData);
    pthread_mutex_lock(&s_mutex); /* acquire lock to block until my thread name is set */
    pthread_mutex_unlock(&s_mutex);

    struct LinuxThreadInstanceData threadData = {
        .m_thread = pthread_self(),
        .m_nPriority = LTCOREBSP_ISR_PRIORITY,
    };

    pthread_setspecific(s_keyThreadLocal, &threadData);

    LTCoreBSP_TakeCPU(&threadData);

    int ch; char c;
    while (0 != (ch = BSP_BLOCKING_CALL(getchar())) && EOF != ch) {
        LTAtomic_Store(&s_InsideInterruptContext, 1);
        c = (ch == 127) ? 8 : (ch & 0xFF); // hack to get backspace key to work for now
        s_pCoreCallbacks->ProcessISRConsoleInputChars(&c, 1);
        s_pCoreCallbacks->ProcessISRConsoleInputChars(NULL, 0);
        LTAtomic_Store(&s_InsideInterruptContext, 0);
    }

    LTCoreBSP_ReleaseCPU(&threadData, false);

    pthread_setspecific(s_keyThreadLocal, NULL);

    return 0;
}

static void
InitPseudoKeyInputISR(void) {
    /* save existing terminal attributes and modify a copy for 'raw' input */
    tcgetattr(STDIN_FILENO, &s_termiosNormal);
    lt_memcpy(&s_termiosRaw, &s_termiosNormal, sizeof(struct termios));
    s_termiosRaw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    s_termiosRaw.c_lflag &= ~(ECHO | ICANON);

    /* set a SIGINT handler so that ^C will result in restored terminal attributes */
    signal(SIGINT, SIGINT_SignalHandler);

    /* set the terminal to have the raw attributes */
    tcsetattr(STDIN_FILENO, TCSANOW, &s_termiosRaw);

    /* create the getchar thread */
    pthread_attr_t attrs;
    if ((0 != pthread_attr_init(&attrs))                                                        ||
        (0 != pthread_attr_setstacksize(&attrs, LTCOREBSP_MIN_PTHREAD_STACK_SIZE))     ||
        (0 != pthread_attr_setinheritsched(&attrs, PTHREAD_EXPLICIT_SCHED))) {
        LTCoreBSP_ConsolePutString("[linuxtools.bsp.init] failed to init thread attrs for key recv pseudo isr\n");
        return;
    }

    pthread_mutex_lock(&s_mutex); /* make the spawned thread block until we can set its name */
        int nRetVal = pthread_create(&s_threadKeyInput, &attrs, &LTCoreBSP_KeyInputPseudoISRThreadProc, NULL);
        if (0 == nRetVal) pthread_setname_np(s_threadKeyInput, "keyRcvPseudoISR");
    pthread_mutex_unlock(&s_mutex);
    pthread_attr_destroy(&attrs);

    if (0 != nRetVal) {
        LTCoreBSP_ConsolePutString("[linuxtools.bsp.init] keyRcvPseudoISR thread creation failed: ");
        switch (nRetVal) {
        case EAGAIN: LTCoreBSP_ConsolePutString(kEAGAINMessage); break;
        case EINVAL: LTCoreBSP_ConsolePutString(kEINVALMessage); break;
        default:     LTCoreBSP_ConsolePutString("\n");           break;
        }
    }
 }

 static void
 ExitPseudoKeyInputISR(void) {
    /* stop the thread and restore terminal attributes */
    if (s_threadKeyInput) {
        /* send 0 to stdin to cause the thread to break out of its loop */
        int ch = 0;
        ioctl(STDIN_FILENO, TIOCSTI, &ch);
        pthread_join(s_threadKeyInput, NULL);
        s_threadKeyInput = 0;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &s_termiosNormal);
 }

#endif /* #if ENABLE_KEY_INPUT */

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
 *  16-Feb-23   augustus    created from LTCoreBSP_Cloud.c
 */
