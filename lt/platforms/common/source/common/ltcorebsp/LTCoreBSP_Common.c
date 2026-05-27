/******************************************************************************
 * platforms/common/source/common/ltcorebsp/LTCoreBSPCommon.c
 *                                          - LTCoreBSP for "Common" platform
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
#include <strings.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <libgen.h>

#include <lt/core/bsp/LTCore.h>
#include <lt/core/bsp/LTCoreBSP.h>

/*___________
  #defines */
#define LTCOREBSP_REAL_TIME_SCHED_POLICY                SCHED_RR
#define LTCOREBSP_NON_REAL_TIME_SCHED_POLICY            SCHED_OTHER
#define LTCOREBSP_DISABLE_PTHREAD_PRIORITY              (33)
#define LTCOREBSP_ISR_PTHREAD_PRIORITY                  (32)
#define LTCOREBSP_MIN_PTHREAD_STACK_SIZE                (16*1024)   /* man page says 16K minimum stack size, empirically verified, 20K required to use printf */
#define LTCOREBSP_STACK_PAGE_SIZE                       (512)       /* man page says stack size must be page size multiple but doesn't say page size; epirically determined to be 512 */
#define LTCOREBSP_ROUND_UP_TO_PAGE_MULTIPLE(nStackSize) ((nStackSize + (LTCOREBSP_STACK_PAGE_SIZE-1)) & ~(LTCOREBSP_STACK_PAGE_SIZE-1))
#define PRINT_STACKTISTICS                              0
#define LTCOREBSP_START_KERNELTIME_AT_ZERO              1
#define LTCOREBSP_MEM_SEAL_ALLOCATED                    0x600dBeef
#define LTCOREBSP_MEM_SEAL_FREED                        0xBadDeed5
#define LTCOREBSP_SYS_RAM_SIZE                          (64* 1024) /* 64K is the LT minimum RAM requirement so that's what we simulate */

/*_______________________
  forward declarations */
static const LTCoreBSP s_bsp;
static bool LTCoreBSP_ThreadInitializeAndRun(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pThreadProc)(void * pClientData), void * pClientData);
static void InitPseudoKeyInputISR(void);
static void ExitPseudoKeyInputISR(void);

/*___________________
  static variables */
static const LTCoreBSP_LTCoreCallbacks *    s_pCoreCallbacks = NULL;
static LTAtomic                             s_nLTCoreBSPInitialized = 0;
static pthread_mutex_t  s_mutex;
static clockid_t        s_clockID = CLOCK_MONOTONIC_RAW;
static s64              s_nHighFrequencyCounterResolution = 0;
static s64              s_nHighFrequencyCounterInitial = 0;
static pthread_key_t    s_keyThreadLocal = (pthread_key_t)0;
static int              s_nRealtimePriorityMin = 0;
static pthread_mutex_t  s_disableMutex;
static pthread_t        s_disableThread = (pthread_t)0;
static int              s_nDisableThreadPriorityOld = 0;
static int              s_nDisableCount = 0;

static pthread_t        s_threadKeyInput = (pthread_t)0;
static struct termios   s_termiosNormal;
static struct termios   s_termiosRaw;

static LTAtomic_LT_SIZE s_nFreeSystemRAM        = LTCOREBSP_SYS_RAM_SIZE;
static LTAtomic_LT_SIZE s_nFreeRAMLowWatermark  = LTCOREBSP_SYS_RAM_SIZE;

static void ConsoleStomp(const char * pString);

/*_____________________
  BSP initialization */
const LTCoreBSP *
LTCoreBSP_Initialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks) {

    if (s_nLTCoreBSPInitialized) return NULL; /* don't let anyone come in here except LTCore the first time */
    s_nLTCoreBSPInitialized = 1; /* don't need a test and set, LTCore calls this before any threads are running */

    // remember pCallbacks
    s_pCoreCallbacks = pCallbacks;

    /* init s_mutex */
    pthread_mutexattr_t mutexAttrs = {{ PTHREAD_MUTEX_FAST_NP }};
    pthread_mutex_init(&s_mutex, &mutexAttrs);

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
    if (0 > (s_nRealtimePriorityMin = sched_get_priority_min(LTCOREBSP_REAL_TIME_SCHED_POLICY))) s_nRealtimePriorityMin = 1;

    /* init Disable/Enable mutex */
    pthread_mutexattr_t disableMutexAttrs = {{ PTHREAD_MUTEX_RECURSIVE_NP }};
    pthread_mutex_init(&s_disableMutex, &disableMutexAttrs);

    /* init pseudo key input (emulated UART) ISR */
    InitPseudoKeyInputISR();

    return &s_bsp;
}

void
LTCoreBSP_Finalize(const LTCoreBSP * pBSP) {
    if ((0 == s_nLTCoreBSPInitialized) || (pBSP != &s_bsp)) return; /* don't let anyone except LTCore in here */

    /* exit pseudo key input (emulated UART) ISR */
    ExitPseudoKeyInputISR();

    s_nRealtimePriorityMin = 0;
    pthread_key_delete(s_keyThreadLocal);
    s_keyThreadLocal = (pthread_key_t)0;
    s_nHighFrequencyCounterInitial = 0;
    s_nHighFrequencyCounterResolution = 0;
    s_clockID = CLOCK_MONOTONIC_RAW;
    pthread_mutex_destroy(&s_mutex);
    pthread_mutex_destroy(&s_disableMutex);

    s_pCoreCallbacks = NULL;
    s_nLTCoreBSPInitialized = 0;
}

/*________________________________________________________________
  LTCoreBSP global functions (statically linked only with LTCore) \________________________________________________
  Excluded from s_bsp struct to enable direct placement into LTCore and LTStdlib interfaces to avoid extra thunk */
bool LT_ISR_SAFE
LTCoreBSP_InsideInterruptContext(void) {
    return false;
}

LT_SIZE LT_ISR_SAFE
LTCoreBSP_DisableInterrupts(void) {
    pthread_mutex_lock(&s_disableMutex);
#ifdef LT_DEBUG
    if (((pthread_t)0 == s_disableThread && s_nDisableCount) ||
        (s_disableThread != (pthread_t)0 &&  s_disableThread != pthread_self()) ||
        (s_disableThread != (pthread_t)0 && (0 == s_nDisableCount))) {
        ConsoleStomp("[ltcorebsp] *** ERROR Inconsistent Disable State\n");
        pthread_mutex_unlock(&s_disableMutex);
        return 0;
    }
#endif
    if ((pthread_t)0 == s_disableThread) {
        s_disableThread = pthread_self();
        int nPolicy = LTCOREBSP_NON_REAL_TIME_SCHED_POLICY;
        struct sched_param param;

        if (0 == pthread_getschedparam(pthread_self(), &nPolicy, &param)) {
            s_nDisableThreadPriorityOld = (nPolicy == LTCOREBSP_REAL_TIME_SCHED_POLICY) ? param.sched_priority : 0;
        }
        else s_nDisableThreadPriorityOld = 0;

        // make this thread higher priority than anything else
        nPolicy = LTCOREBSP_REAL_TIME_SCHED_POLICY;
        param.sched_priority = LTCOREBSP_DISABLE_PTHREAD_PRIORITY;
        pthread_setschedparam(pthread_self(), nPolicy, &param);
    }
    /* return from this function with the mutex locked; we unlock it in Enable() */
    return (1 == ++s_nDisableCount) ? (LT_SIZE)s_nDisableThreadPriorityOld : 0;
}

void LT_ISR_SAFE
LTCoreBSP_EnableInterrupts(LT_SIZE nMask) {
    /* lock and unlock to make sure I'm the thread that did the Disable() */
    pthread_mutex_lock(&s_disableMutex);
    if (pthread_self() != s_disableThread)
    {
        ConsoleStomp("[ltcorebsp] *** LTCoreBSP_EnableInterrupts called by thread that did not Disable\n");
        pthread_mutex_unlock(&s_disableMutex);
        return;
    }

    /* I have the mutex locked at least once from doing the Disable() call */
    if (0 == --s_nDisableCount) {
        struct sched_param param;
        int nPolicy = s_nDisableThreadPriorityOld ? LTCOREBSP_REAL_TIME_SCHED_POLICY : LTCOREBSP_NON_REAL_TIME_SCHED_POLICY;
    #ifdef LT_DEBUG
        if ((int)nMask != s_nDisableThreadPriorityOld) {
            ConsoleStomp("[ltcorebsp] *** LTCoreBSP_EnableInterrupts: nMask doesn't match original priority\n");
        }
    #endif
        nMask = (LT_SIZE)s_nDisableThreadPriorityOld; /* always use our truth */
        s_disableThread = (pthread_t)0;
        s_nDisableThreadPriorityOld = 0;
        param.sched_priority = (int)nMask;
        if (0 != pthread_setschedparam(pthread_self(), nPolicy, &param)) {
            ConsoleStomp("[ltcorebsp] *** LTCoreBSP_EnableInterrupts: failed to restore thread priority\n");
        }
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
    default:                            /* Error getting the lock.  Shouldn't ever happen:          */
                    ConsoleStomp("[ltcorebsp] *** LTCoreBSP_InterruptsAreDisabled: failed to determine interrupt status\n");
                    return false;
    }
    disabled = (pthread_t)0 != s_disableThread && 0 != s_nDisableCount;
    pthread_mutex_unlock(&s_disableMutex);
    return disabled;
}

static void LT_ISR_SAFE
LTCoreBSP_EnableConsoleReceiveInterrupt(bool bEnable) {
    if (bEnable) /*  enable UART receive-character-available interrupt */;
    else         /* disable UART receive-character-available interrupt */;
}

/*_____________________
  putchar to console */
static void LT_ISR_SAFE
LTCoreBSP_ConsoleCramChar(char c) { putchar((int)c); }

/*____________________________________________________________________________
  ConsoleStomp function - only call this to print during BSP initialization */
static void ConsoleStomp(const char * pString) {
    /* The BSP should not be using the UART.  In the future LTCore will
       provide the BSP with ConsolePrint and ConsoleLog functions so it can
       call back into LTCore to print coherently.   For now we will just
       ConsoleStomp sparingly.  Don't add calls to printf or vsnprintf or anything
       like that in this file, please. */
    while (*pString) LTCoreBSP_ConsoleCramChar(*pString++);
}

void
LTCoreBSP_DebugBreak(void) {
    /* break into debugger if debugger available, otherwise raise exception */
}

/* _______________________________________________
 * static functions placed into struct LTCoreBSP */
/*_________________________
  high frequency counter */
static s64 LT_ISR_SAFE
LTCoreBSP_GetHighFrequencyCounterNanoseconds(void) {
    struct timespec ts; clock_gettime(s_clockID, &ts);
#if LTCOREBSP_START_KERNELTIME_AT_ZERO
    return (LTCoreBSP_SecondsToNanoseconds(ts.tv_sec) + (s64)ts.tv_nsec) - s_nHighFrequencyCounterInitial;
#else
    return LTCoreBSP_SecondsToNanoseconds(ts.tv_sec) + (s64)ts.tv_nsec;
#endif
}

static s64 LT_ISR_SAFE
LTCoreBSP_GetHighFrequencyCounterNanosecondResolution(void) {
    return s_nHighFrequencyCounterResolution;
}

/*____________
  debugging */
static bool LT_ISR_SAFE
LTCoreBSP_DebugAssertFailed(const char * pFile, int nLine, const char * pTest) {
    LT_UNUSED(pFile); LT_UNUSED(nLine); LT_UNUSED(pTest);
    #if 1
        return true;   /* DRW 07-Feb-23 : always do asserts, even in release mode now */
    #else
        #ifdef LT_DEBUG
            /* return true to trap to debugger on assert - may be used to implement abort/continue prompt */
            return true;
        #else
            return false;
        #endif
    #endif
}

/*_______________________
  allocation functions */
static bool
ReserveSystemRAMBytes(LT_SIZE nNumBytes, LT_SIZE * pNewFreeSystemRamToSet) {
    // make sure there are nNumBytes still available in s_nFreeSystemRAM and if so, atomically reduce s_nFreeSystemRAM
    LT_SIZE nBytesFree;
    do {
        nBytesFree = s_nFreeSystemRAM;
        if (nNumBytes > nBytesFree) return false;
    } while (false == LTAtomic_TestAndSet_LT_SIZE(&s_nFreeSystemRAM, nBytesFree, nBytesFree - nNumBytes));
    *pNewFreeSystemRamToSet = nBytesFree - nNumBytes;
    return true;
}

static void
UpdateFreeRAMLowWatermark(LT_SIZE nNewPotentialLow) {
    LT_SIZE nLowWatermarkBytes;
    do {
        nLowWatermarkBytes = s_nFreeRAMLowWatermark;
        if (nLowWatermarkBytes <= nNewPotentialLow) break; // free watermark is lower already
    } while (false == LTAtomic_TestAndSet_LT_SIZE(&s_nFreeRAMLowWatermark, nLowWatermarkBytes, nNewPotentialLow));
}

/* malloc, realloc, and free */
static void *
LTCoreBSP_malloc(LT_SIZE nBytes) {
    void * pMem = NULL; LT_SIZE nNewFreeSystemRam = 0;
    if (nBytes && ReserveSystemRAMBytes(nBytes, &nNewFreeSystemRam)) {
        // allocate the bytes plus 8 extra for seal and size
        if (NULL != (pMem = malloc(nBytes + 8))) {
            // alloc succeeded - update ram low watermark, mark pMem valid, stash size, and advance pMem past header
            UpdateFreeRAMLowWatermark(nNewFreeSystemRam);
            *((u32 *)pMem) = LTCOREBSP_MEM_SEAL_ALLOCATED;
            *(((u32 *)pMem) + 1) = (u32)nBytes;
            pMem = (void *)(((LT_SIZE)pMem) + 8);
        }
        else {
            // allocation failed - give the reserved bytes back to 'the system'
            (void)LTAtomic_PostIncrement_LT_SIZE(&s_nFreeSystemRAM, nBytes);
        }
    }
    return pMem;
}

static void
LTCoreBSP_free(void * pMem) {
    if (pMem && LTAtomic_TestAndSet_LT_SIZE((LTAtomic_LT_SIZE *)(((LT_SIZE)pMem) - 8), LTCOREBSP_MEM_SEAL_ALLOCATED, LTCOREBSP_MEM_SEAL_FREED)) {
        (void)LTAtomic_PostIncrement_LT_SIZE(&s_nFreeSystemRAM, (LT_SIZE)(*(((u32 *)pMem) - 1))); /* give bytes back to 'system ram' */
        free((void *)(((LT_SIZE)pMem) - 8));
    }
}

static void *
LTCoreBSP_realloc(void * pMem, LT_SIZE nBytes) {
    if (NULL == pMem) return LTCoreBSP_malloc(nBytes);
    if (0 == nBytes) { LTCoreBSP_free(pMem); return NULL; }
    if (*(((u32 *)pMem) - 2) != LTCOREBSP_MEM_SEAL_ALLOCATED) return NULL;
    LT_SIZE nExistingBytes = (LT_SIZE)(*(((u32 *)pMem) - 1));
    if (nBytes > nExistingBytes) {
        /* increasing size, try to reserve the increasal amount */
        if (ReserveSystemRAMBytes(nBytes - nExistingBytes, &nExistingBytes)) { /* pass in &nExistingBytes to take the new free system ram amount */
            if (NULL != (pMem = realloc((void *)(((LT_SIZE)pMem) - 8), nBytes + 8))) {
                // realloc succeeded - update ram low watermark, stash new alloc'd size, and advance pMem past header
                UpdateFreeRAMLowWatermark(nExistingBytes);
                *(((u32 *)pMem) + 1) = (u32)nBytes;
                pMem = (void *)(((LT_SIZE)pMem) + 8);
                return pMem;
            }
            else {
                // allocation failed - give the reserved bytes back to 'the system'
                (void)LTAtomic_PostIncrement_LT_SIZE(&s_nFreeSystemRAM, nBytes - nExistingBytes);
                return NULL;
            }
        }
        else return NULL; /* couldn't reserve the ram for the increase */
    }
    else if (nBytes < nExistingBytes) {
        /* we are decreasing in size */
        if (NULL != (pMem = realloc((void *)(((LT_SIZE)pMem) - 8), nBytes + 8))) {
            /* realloc succeeded - update stashed size, advance pMem past our header, and return reduced amount of bytes back to 'system ram' */
            *(((u32 *)pMem) + 1) = (u32)nBytes;
            pMem = (void *)(((LT_SIZE)pMem) + 8);
            (void)LTAtomic_PostIncrement_LT_SIZE(&s_nFreeSystemRAM, nExistingBytes - nBytes);
        }
        return pMem;
    }
    else return pMem; /* staying the same size, success */
}

/*________________
  ram functions */
static LT_SIZE
LTCoreBSP_GetTotalSystemRAM(void) {
    return (LT_SIZE)LTCOREBSP_SYS_RAM_SIZE;
}

static LT_SIZE
LTCoreBSP_GetAvailableSystemRAM(void) {
    return (LT_SIZE)s_nFreeSystemRAM;
}

static LT_SIZE
LTCoreBSP_GetSystemRAMLowWatermark(void) {
    return (LT_SIZE)s_nFreeRAMLowWatermark;
}

/*___________________________________________________________________________________________________________
  mutex - this will move into core once we have our own kernel - tbd how we run that on top of a hosted os */
static LT_SIZE
LTCoreBSP_MutexInstanceSize(void) {
    return sizeof(pthread_mutex_t);
}

static void
LTCoreBSP_MutexInitialize(void * pMutex) {
    pthread_mutex_t * pMutex = (pthread_mutex_t *)pMutex;
    pthread_mutexattr_t mutexAttrs = {{ PTHREAD_MUTEX_RECURSIVE_NP }};
    if (0 != pthread_mutex_init(pMutex, &mutexAttrs)) {
        /* LTCore_RedAlert(LTCORE_BSP_OS_LOGSECTION_LINUX, "mutex.initfailure"); */
    }
}

static void
LTCoreBSP_MutexFinalize(void * pMutex) {
    pthread_mutex_t * pMutex = (pthread_mutex_t *)pMutex;
    pthread_mutex_destroy(pMutex);
}

static void
LTCoreBSP_MutexLock(void * pMutex) {
    pthread_mutex_lock((pthread_mutex_t *)pMutex);
}

static void
 LTCoreBSP_MutexUnlock(void * pMutex) {
    pthread_mutex_unlock((pthread_mutex_t *)pMutex);
}

static bool
 LTCoreBSP_MutexTryLock(void * pMutex) {
    return (0 == pthread_mutex_trylock((pthread_mutex_t *)pMutex)) ? true : false;
}

/*___________________________________________________________________________________________________________
  monitor - this will move into core once we have our own kernel - tbd how we run that on top of a hosted os */
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
LTCoreBSP_MonitorInstanceSize(void) {
    return sizeof(struct MonitorData);
}

static void
LTCoreBSP_MonitorInitialize(void * pMonitor) {
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
LTCoreBSP_MonitorFinalize(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData))
    {
        pthread_cond_destroy(&pData->m_condition);
        pthread_mutex_destroy(&pData->m_mutex);
        pData->m_nFlags = 0;
    }
}

static void
LTCoreBSP_MonitorEnter(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) {
        switch (pthread_mutex_lock(&pData->m_mutex)) {
            case 0:         return;
            case EDEADLK:   /* s_logger.YellowAlert("enter.illegal.nest"); */              return;
            default:        /* s_logger.YellowAlert("enter.unknown.pthread.returncode"); */ return;
        }
    }
    // s_logger.YellowAlert("enter.invalidmonitor");
}

static void
LTCoreBSP_MonitorExit(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) {
        switch (pthread_mutex_unlock(&pData->m_mutex)) {
            case 0:         return;
            case EPERM:     /* s_logger.YellowAlert("exit.noenter"); */ return;
            default:        /* s_logger.YellowAlert("exit.unknown.pthread.returncode"); */ return;
        }
    }
    // s_logger.YellowAlert("exit.invalidmonitor");
}

static void LT_ISR_SAFE
LTCoreBSP_MonitorNotify(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) pthread_cond_signal(&pData->m_condition);

    #ifdef LT_DEBUG
    else { /* s_logger.YellowAlert("notify.invalidmonitor"); */ }
    #endif
}

static bool
LTCoreBSP_MonitorWait(void * pMonitor, s64 nTimeoutNanoseconds) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) {
        if (LTCoreBSP_NanosecondsIsInfinite(nTimeoutNanoseconds)) {
            switch (pthread_cond_wait(&pData->m_condition, &pData->m_mutex)) {
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

                switch (pthread_cond_timedwait(&pData->m_condition, &pData->m_mutex, &timeSpec)) {
                    case 0:         return true;
                    case ETIMEDOUT: return false;
                    case EPERM:     /* s_logger.YellowAlert("timedwait.noenter"); */  return false;
                    default:        /* s_logger.YellowAlert("timedwait.unknown.pthread.returncode"); */ return false;
                }
            }
            else {
                /* s_logger.YellowAlert("timedwait.clock_gettime.failure"); */
                return false;
            }
        }
    }
    /* s_logger.YellowAlert("wait.invalidmonitor"); */
    return false;
}

/*___________________________________________________________________________________________________________
  thread - this will move into core once we have our own kernel - tbd how we run that on top of a hosted os */
struct LinuxThreadInstanceData {
    pthread_t m_thread;
    void (*   m_pThreadProc)(void * pClientData);
    void  *   m_pClientData;
    bool      m_bCreated;
};

static int LTCoreBSP_LTPriorityToLinuxPThreadPriority(u8 nLTPriority) {
    return nLTPriority ? s_nRealtimePriorityMin + (nLTPriority - 1) : 0;
}

static void * LTCoreBSP_OSThread_LinuxThreadProc(void * pClientData) {
    pthread_mutex_lock(&s_mutex);
        /* synchronize thread execution with creation by blocking on this mutex while we finish configuring the pthread
           in thread creation.  Done like this because pthreads can't be created suspended like win32 threads */
    pthread_mutex_unlock(&s_mutex);

    pthread_setspecific(s_keyThreadLocal, pClientData);
    #if PRINT_STACKTISTICS
        PrintStackTistics("ThreadProc", &pClientData);
    #endif
    ((struct LinuxThreadInstanceData *)pClientData)->m_pThreadProc(((struct LinuxThreadInstanceData *)pClientData)->m_pClientData);
    pthread_setspecific(s_keyThreadLocal, NULL);
    return 0;
}

static LT_SIZE
LTCoreBSP_ThreadInstanceSize(void) {
    return sizeof(struct LinuxThreadInstanceData);
}

static void
LTCoreBSP_ThreadInitializeAndStartScheduler(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pInitialThreadProc)(void * pClientData), void * pClientData) {
    LT_UNUSED(pName);
    /* This function should do three things:
    1. perform any instance initialization of pThread.
    2. start the scheduler, running the initial thread as specified by the function parameters.
    3. block until the thread exits, then UNINITIALIZE THE INSTANCE DATA,  STOP THE SCHEDULER and return */
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)pThread;

    /* no need to start the scheduler on windows, just run the thread */
    if (LTCoreBSP_ThreadInitializeAndRun(pThread, nPriority, nStackSize, pName, pInitialThreadProc, pClientData)) {
        /* and block until it finishes */
        pthread_join(pInstanceData->m_thread, NULL);
    }
    pInstanceData->m_thread = 0;
    pInstanceData->m_pThreadProc = 0;
    pInstanceData->m_pClientData = NULL;
    pInstanceData->m_bCreated = false;
}

static void
LTCoreBSP_ThreadStopScheduler(void) {
    /* nothing to do */
}

static bool
LTCoreBSP_ThreadInitializeAndRun(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pThreadProc)(void * pClientData), void * pClientData) {
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)pThread;
    /* called to spawn threads after the scheduler has been started.  This function should do three things:
        1. perform any instance initialization of pThread.
        2. run the initial thread as specified by the function parms.
        3. return from this function immediately after spawning the thread. */
    bool bAttrsInitialized = false;
    pthread_attr_t attrs;
    struct sched_param schedParam;
    schedParam.sched_priority = LTCoreBSP_LTPriorityToLinuxPThreadPriority(nPriority);
    int nPolicy = nPriority ? LTCOREBSP_REAL_TIME_SCHED_POLICY : LTCOREBSP_NON_REAL_TIME_SCHED_POLICY;

    pInstanceData->m_thread       = 0;
    pInstanceData->m_pThreadProc  = pThreadProc;
    pInstanceData->m_pClientData  = pClientData;
    pInstanceData->m_bCreated     = false;

    /* pthreads won't create without a 16K minimum stack, and stack sizes have to be a multiple of the page size */
    nStackSize = (nStackSize < LTCOREBSP_MIN_PTHREAD_STACK_SIZE) ? LTCOREBSP_MIN_PTHREAD_STACK_SIZE : LTCOREBSP_ROUND_UP_TO_PAGE_MULTIPLE(nStackSize);

    do
    {
        if (0 != pthread_attr_init(&attrs)) break;
        bAttrsInitialized = true;
        if (0 != pthread_attr_setstacksize(&attrs, (size_t)nStackSize)) break;
        if (0 != pthread_attr_setinheritsched(&attrs, PTHREAD_EXPLICIT_SCHED)) break;
        if (0 != pthread_attr_setschedpolicy(&attrs, nPolicy)) break;
        if (0 != pthread_attr_setschedparam(&attrs, &schedParam)) break;

        if (pName && *pName) {
            /* going to set the thread name, someone elaborate */
            unsigned i; char name[16]; // must limit to 16 or pthread_setname_np will fail
            for (i = 0; i < (sizeof(name)-1); i++) if (pName[i]) name[i] = pName[i]; else break;
            name[i] = 0;
            pthread_mutex_lock(&s_mutex); /* to make the spawned thread block until we can set its name */
            if (0 != (nPolicy = pthread_create(&pInstanceData->m_thread, &attrs, &LTCoreBSP_OSThread_LinuxThreadProc, pInstanceData))) {
                /* note that nPolicy is reused to hold the return code for error printing below*/
                pInstanceData->m_thread = 0;
                pthread_mutex_unlock(&s_mutex);
                break;
            }
            /* setting a pthread's name has to be done after it's created but we don't want the thread to run until
               we do that, hence the overloaded use of s_mutex.  */
            pthread_setname_np(pInstanceData->m_thread, name);
            pthread_mutex_unlock(&s_mutex);
        }
        else {
            if (0 != (nPolicy = pthread_create(&pInstanceData->m_thread, &attrs, &LTCoreBSP_OSThread_LinuxThreadProc, pInstanceData))) {
                pInstanceData->m_thread = 0;
                break;
            }
        }
        pInstanceData->m_bCreated = true;
    }
    while (false);

    if (bAttrsInitialized) pthread_attr_destroy(&attrs);

    if (! pInstanceData->m_bCreated) {
        pInstanceData->m_thread = 0;
        pInstanceData->m_pThreadProc = NULL;
        pInstanceData->m_pClientData = NULL;
        ConsoleStomp("[ltcorebsp.os.linux.thread.");
        ConsoleStomp(pName);
        ConsoleStomp("] creation failed: ");
        switch (nPolicy) {
            case EAGAIN: pName = "system thread resource limit reached"; break;
            case EINVAL: pName = "invalid attributes specified"; break;
            case EPERM:  pName = "inadequate privileges\n ** add -->  \"username - rtprio unlimited\"  to /etc/security/limits.conf  and logout/in"; break;
            default:     pName = ""; break;
        }
        ConsoleStomp(pName);
        ConsoleStomp("\n");
    }

    return pInstanceData->m_bCreated;
}

static void
LTCoreBSP_ThreadGetStackUsage(void * pThread, u32 * pStackSizeToSet, u32 * pCurrentStackUsageToSet, u32 * pMaxStackUsageToSet) {
    LT_UNUSED(pThread);
    *pStackSizeToSet = *pCurrentStackUsageToSet = *pMaxStackUsageToSet = 0;
}

static bool
LTCoreBSP_ThreadSetPriority(void * pThread, u8 nPriority) {
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)pThread;
    if (pInstanceData->m_bCreated) {
        int nPriorityPThread = LTCoreBSP_LTPriorityToLinuxPThreadPriority(nPriority);
        int nPolicy;
        struct sched_param param;

        // don't set anything, just return true if the thread already has the parameters we want
        if (0 == pthread_getschedparam(pInstanceData->m_thread, &nPolicy, &param)) {
            if (nPolicy == LTCOREBSP_NON_REAL_TIME_SCHED_POLICY) { if (nPriority == 0) return true; }
            else if ((nPolicy == LTCOREBSP_REAL_TIME_SCHED_POLICY) && (param.sched_priority == nPriorityPThread)) return true;
        }

        // set the parameters
        nPolicy = nPriority ? LTCOREBSP_REAL_TIME_SCHED_POLICY : LTCOREBSP_NON_REAL_TIME_SCHED_POLICY;
        param.sched_priority = nPriorityPThread;
        if (0 == pthread_setschedparam(pInstanceData->m_thread, nPolicy, &param)) return true;
    }
    return false;
}

static bool
LTCoreBSP_ThreadWaitUntilFinished(void * pThread, s64 nTimeoutNanoseconds) {
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)pThread;
    /* This function should block for [at least] nTimeoutNanoseconds and return true at the point when the thread
       completely finishes all execution, or false if the thread has not finished execution within the timeout period.
       By returning true, this function indicates it is now legal to subsequently call LTCoreBSP_ThreadUninitializeInstance()
       on the thread object.  */
    LT_ASSERT(pInstanceData);
    if (!pInstanceData->m_bCreated) return true;

    int error;
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
    if (error != 0) return false;

    pInstanceData->m_thread = 0;
    pInstanceData->m_bCreated = false;
    return true;
}

static void
LTCoreBSP_ThreadFinalize(void * pThread) {
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)pThread;
    /* called to uninitialize the private instance data (pThread))
       This function call will always be preceded by a call to LTCoreBSP_ThreadWaitUntilThreadFinished() that returned
       a value of true, therefore it is guaranteed the thread is no longer running when this function is called. */
    pInstanceData->m_thread = 0;
    pInstanceData->m_pThreadProc = NULL;
    pInstanceData->m_pClientData = NULL;
    pInstanceData->m_bCreated = false;
}

static void *
LTCoreBSP_ThreadGetCurrentThreadInstanceData(void) {
    return pthread_getspecific(s_keyThreadLocal);
}

static void
LTCoreBSP_ThreadSleep(s64 nNanoseconds) {
    struct timespec delay = { (time_t)LTCoreBSP_NanosecondsToSeconds(nNanoseconds), (long)(nNanoseconds - LTCoreBSP_SecondsToNanoseconds(delay.tv_sec)) };
    struct timespec remaining;
    while (0 != nanosleep(&delay, &remaining)) delay = remaining;
}

/*___________________________________________________________________________________________________________________
  Dynamic library enumeration and loading ala dlopen/dlsym/dlclose - only if platform has a runtime dynamic loader */
#ifndef LT_NO_DYNAMIC_LOADER
    static bool
    LTCoreBSP_LibraryLoad(const char * pLTLibraryName, void ** pLibraryHandleToSet) {
        /* This function returns one of the following result codes:
               LTCOREBSP_LOADLIBRARY_SUCCESS,
               LTCOREBSP_LOADLIBRARY_ERROR_LIBNOTFOUND,
               LTCOREBSP_LOADLIBRARY_ERROR_UNRESOLVEDSYMBOL,
               LTCOREBSP_LOADLIBRARY_ERROR_LIBINVALID,
               LTCOREBSP_LOADLIBRARY_ERROR_GENERIC           */

        /* LTLibraryManager guarantees pLibName is non null and between 1 and kMaxLibNameLen characters long
           It also guarantees the thread safety of s_libraryString below because this function
           is guaranteed to only ever be called from one thread at a time. */
        static char s_libraryString[128];
        /* DRW : 11-Jan-21 : can't call LT_snprintf anymore, manually do it
        LT_snprintf(s_libraryString, sizeof(s_libraryString), "lib%s.so", pLTLibraryName);
        */
        char * pDest = s_libraryString;
        const char * pSource= "lib"; while (0 != (*pDest++ = *pSource++)); pDest--;
        pSource = pLTLibraryName;    while (0 != (*pDest++ = *pSource++)); pDest--;
        pSource = ".so";             while (0 != (*pDest++ = *pSource++));
        if (NULL != (*pLibraryHandleToSet = (void *)dlopen(s_libraryString, RTLD_NOW | RTLD_LOCAL))) return LTCOREBSP_LOADLIBRARY_SUCCESS;
        else {
            const char * pError = dlerror();
            s_pCoreCallbacks->ReportLibraryLoaderFunctionFailure("dlopen", s_libraryString, pError ? pError :"failed");
        }

        return LTCOREBSP_LOADLIBRARY_ERROR_GENERIC;
    }

    static void
    LTCoreBSP_LibraryUnload(void * pLibraryHandle) {
        dlclose(pLibraryHandle);
    }

    static void *
    LTCoreBSP_LibraryLookupSymbol(void * pLibraryHandle, const char * pSymbolName) {
        void * pAddress = (void *)dlsym(pLibraryHandle, pSymbolName);
        if (NULL == pAddress) {
            const char * pError = dlerror();
            s_pCoreCallbacks->ReportLibraryLoaderFunctionFailure("dlsym", pSymbolName, pError ? pError :"failed");
        }
        return pAddress;
    }

    static bool
    LTCoreBSP_LibraryEnumerate(LTCoreBSP_LibraryEnumProc * pEnumProc, void * pClientData) {
        bool bRetVal = true;
        char * pExePathname = strdup(program_invocation_name);  /* strdup because call to dirname will mangle */
        char * pExeDir = dirname(pExePathname);
        struct dirent ** ppNameList;
        int n = scandir(pExeDir, &ppNameList, NULL, NULL);
        if (n < 0) perror("scandir");
        else {
            while (n--) {
                if (bRetVal && (0 == strncmp(ppNameList[n]->d_name, "lib", 3))) {
                    char * pName = strdup(ppNameList[n]->d_name + 3);
                    int nLen = strlen(pName);
                    if ((nLen > 3) && (0 == strncmp(pName + (nLen - 3), ".so", 3))) {
                        *(pName + ( nLen - 3)) = 0;
                        bRetVal = (*pEnumProc)(pName, pClientData);
                    }
                    free(pName);
                }
                free(ppNameList[n]);
            }
            free(ppNameList);
        }
        free(pExePathname);
        return bRetVal;
    }

#endif /* #ifndef LT_NO_DYNAMIC_LOADER */

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

    int ch;
    while (0 != (ch = getchar()) && EOF != ch) {
        //hack to get backspace key to work for now
        if (ch == 0177) ch = 8;
        s_pCoreCallbacks->DispatchISRConsoleCharacterInput((char)ch);
    }

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
    struct sched_param schedParam;
    schedParam.sched_priority = LTCOREBSP_ISR_PTHREAD_PRIORITY;
    if ((0 != pthread_attr_init(&attrs))                                                        ||
        (0 != pthread_attr_setstacksize(&attrs, LTCOREBSP_MIN_PTHREAD_STACK_SIZE))     ||
        (0 != pthread_attr_setinheritsched(&attrs, PTHREAD_EXPLICIT_SCHED))                     ||
        (0 != pthread_attr_setschedpolicy(&attrs, LTCOREBSP_REAL_TIME_SCHED_POLICY))   ||
        (0 != pthread_attr_setschedparam(&attrs, &schedParam))) {
        ConsoleStomp("[ltcorebsp.os.linux.init] failed to init thread attrs for key recv pseudo isr\n");
        return;
    }

    pthread_mutex_lock(&s_mutex); /* make the spawned thread block until we can set its name */
        int nRetVal = pthread_create(&s_threadKeyInput, &attrs, &LTCoreBSP_KeyInputPseudoISRThreadProc, NULL);
        if (0 == nRetVal) pthread_setname_np(s_threadKeyInput, "keyRcvPseudoISR");
    pthread_mutex_unlock(&s_mutex);
    pthread_attr_destroy(&attrs);

    if (0 != nRetVal) {
        ConsoleStomp("[ltcorebsp.os.linux.init] keyRcvPseudoISR thread creation failed: ");
        switch (nRetVal) {
            case EAGAIN: ConsoleStomp("system thread resource limit reached\n"); break;
            case EINVAL: ConsoleStomp("invalid attributes specified\n"); break;
            case  EPERM: ConsoleStomp("inadequate privileges\n ** add -->  \"username - rtprio unlimited\"  to /etc/security/limits.conf  and logout/in\n"); break;
                default: ConsoleStomp("\n"); break;
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

/**************************************************************
 * Thread stack statistics for debugging (currently disabled) *
 **************************************************************/

#if PRINT_STACKTISTICS
typedef long long unsigned int p64;
    /* The pre-genesis thread only needs a 512 byte stack; the genesis thread gets 1K by default, but pthreads require a min of 16K amd
       if you use glibc's printf with a formatted argument you need a 20K stack.  Grr, hence the myprintf() and this PrintStackTistics() function.
       On my ubuntu 16.04, 4280/16384 stack bytes are used before my ThreadProc gets control.  */
static void PrintStackTistics(const char * pLabel, void * pCurrentStackPointer) {
    int nPolicy = 0, nPriority = 0; char name[16]; pthread_attr_t attrs; struct sched_param schedParam; void * pStackAddress; size_t nStackSizeTSize;
    p64 nStackCurrent = (p64)((LT_SIZE)pCurrentStackPointer), nStackTest = (p64)((LT_SIZE)&nPolicy), nStackBegin, nStackEnd, nStackSize, nStackUsed, nStackFree;
    const char * pThreadType = "non-rt"; name[0] = 0;

    if  (pthread_getattr_np(pthread_self(), &attrs) || pthread_attr_getschedpolicy(&attrs, &nPolicy) || pthread_attr_getschedparam(&attrs, &schedParam) ||
         pthread_attr_getstack(&attrs, &pStackAddress, &nStackSizeTSize) || pthread_getname_np(pthread_self(), name, sizeof(name))) return;
    if (LTCOREBSP_REAL_TIME_SCHED_POLICY == nPolicy) { pThreadType = "    rt"; nPriority = schedParam.sched_priority; }

    /* right justify thread name with periods */
    nPolicy = LTCoreBSP_strlen(name);
    if ((nPolicy = LTCoreBSP_strlen(name)))  {/* reuse nPolicy for strlen */
        if (nPolicy < (int)(sizeof(name)-1)) { lt_memmove(&name[(sizeof(name)-1)-nPolicy], &name[0], nPolicy); lt_memset(&name[0], '.', (sizeof(name)-1)-nPolicy); }
        else { lt_memset(&name[0], '.', (sizeof(name)-1)); } name[(sizeof(name)-1)] = 0; /* (null terming not part of else intentionally) */ }

    nStackSize = (p64)nStackSizeTSize;
    if (nStackTest > nStackCurrent) { /* stack grows up   */ nStackBegin = (p64)((LT_SIZE)pStackAddress); nStackEnd = nStackBegin + nStackSize; nStackUsed = nStackCurrent - nStackBegin; nStackFree = nStackEnd - nStackCurrent; }
                               else { /* stack grows down */ nStackEnd = (p64)((LT_SIZE)pStackAddress); nStackBegin = nStackEnd + nStackSize; nStackUsed = nStackBegin - nStackCurrent; nStackFree = nStackCurrent - nStackEnd; }

    myprintf("[ltcorebsp.os.linux.%s]%s: %s prio% 3d, %llu/%llu stack bytes free, %llu used\n", pLabel, name, pThreadType, nPriority, nStackFree, nStackSize, nStackUsed);

#if 0
    p64 nStackUsedPlusFree = nStackUsed + nStackFree, nMask = 0xFFFFFFFFFFFFFFFF, nTest = 0xF000000000000000;
    for (int i = 0; i < 11; i++) {
        if (((nStackBegin & nTest) != (nStackEnd & nTest)) || ((nStackBegin & nTest) != (nStackCurrent & nTest))) break;
        nMask >>= 4; nTest >>= 4; nStackBegin &= nMask; nStackEnd &= nMask; nStackCurrent &= nMask;
    }

    myprintf("stack: begin 0x%llx, end 0x%llx, current 0x%llx, size 0x%llx, used 0x%llx, free 0x%llx, used+free 0x%llx\n", nStackBegin, nStackEnd, nStackCurrent, nStackSize, nStackUsed, nStackFree, nStackUsedPlusFree);
    myprintf("stack: begin  %llu, end  %llu, current  %llu, size  %llu, used   %llu, free  %llu, used+free  %llu\n", nStackBegin, nStackEnd, nStackCurrent, nStackSize, nStackUsed, nStackFree, nStackUsedPlusFree);

    p64 r1 = (p64)&nStackBegin, r2 = (p64)&nStackEnd, r3 = (p64)&nStackCurrent, r4 = (p64)&nStackSize, r5 = (p64)&nStackUsed;
    nMask = 0xFFFFFFFFFFFFFFFF; nTest = 0xF000000000000000;
    for (int j = 0; j < 11; j++) {
        if (((r1 & nTest) != (r2 & nTest)) || ((r1 & nTest) != (r3 & nTest)) || ((r1 & nTest) != (r4 & nTest)) || ((r1 & nTest) != (r5 & nTest))) break;
        nMask >>= 4; nTest >>= 4; r1 &= nMask; r2 &= nMask; r3 &= nMask; r4 &= nMask; r5 &= nMask;
    }
    myprintf("rolll: r1 0x%llx, r2 0x%llx, r3 0x%llx, r4 0x%llx, r5 0x%llx\n", r1, r2, r3, r4, r5);
    myprintf("rolll: r1  %llu, r2  %llu, r3  %llu, r4  %llu, r5  %llu\n", r1, r2, r3, r4, r5);
#endif
}
#endif /* #if PRINT_STACKTISTICS */


/*_____________________________
  LTCoreBSP interface struct */
static const LTCoreBSP s_bsp = {

    .GetHighFrequencyCounterNanoseconds = LTCoreBSP_GetHighFrequencyCounterNanoseconds,
    .GetHighFrequencyCounterNanosecondResolution = LTCoreBSP_GetHighFrequencyCounterNanosecondResolution,

    .EnableConsoleReceiveInterrupt = LTCoreBSP_EnableConsoleReceiveInterrupt,
    .ConsoleCramChar = LTCoreBSP_ConsoleCramChar,

    .DebugAssertFailed = LTCoreBSP_DebugAssertFailed,

    .malloc = LTCoreBSP_malloc,
    .realloc = LTCoreBSP_realloc,
    .free = LTCoreBSP_free,

    .GetTotalSystemRAM = LTCoreBSP_GetTotalSystemRAM,
    .GetAvailableSystemRAM = LTCoreBSP_GetAvailableSystemRAM,
    .GetSystemRAMLowWatermark = LTCoreBSP_GetSystemRAMLowWatermark,

    .MutexInstanceSize = LTCoreBSP_MutexInstanceSize,
    .MutexInitialize = LTCoreBSP_MutexInitialize,
    .MutexFinalize = LTCoreBSP_MutexFinalize,
    .MutexLock = LTCoreBSP_MutexLock,
    .MutexUnlock = LTCoreBSP_MutexUnlock,
    .MutexTryLock = LTCoreBSP_MutexTryLock,

    .MonitorInstanceSize = LTCoreBSP_MonitorInstanceSize,
    .MonitorInitialize = LTCoreBSP_MonitorInitialize,
    .MonitorFinalize = LTCoreBSP_MonitorFinalize,
    .MonitorEnter = LTCoreBSP_MonitorEnter,
    .MonitorExit = LTCoreBSP_MonitorExit,
    .MonitorNotify = LTCoreBSP_MonitorNotify,
    .MonitorWait = LTCoreBSP_MonitorWait,

    .ThreadInstanceSize = LTCoreBSP_ThreadInstanceSize,
    .ThreadInitializeAndStartScheduler = LTCoreBSP_ThreadInitializeAndStartScheduler,
    .ThreadStopScheduler = LTCoreBSP_ThreadStopScheduler,
    .ThreadInitializeAndRun = LTCoreBSP_ThreadInitializeAndRun,
    .ThreadGetStackUsage = LTCoreBSP_ThreadGetStackUsage,
    .ThreadSetPriority = LTCoreBSP_ThreadSetPriority,
    .ThreadWaitUntilFinished = LTCoreBSP_ThreadWaitUntilFinished,
    .ThreadFinalize = LTCoreBSP_ThreadFinalize,
    .ThreadGetCurrentThreadInstanceData = LTCoreBSP_ThreadGetCurrentThreadInstanceData,
    .ThreadSleep = LTCoreBSP_ThreadSleep,

#ifndef LT_NO_DYNAMIC_LOADER
    .LibraryLoad = LTCoreBSP_LibraryLoad,
    .LibraryUnload = LTCoreBSP_LibraryUnload,
    .LibraryLookupSymbol = LTCoreBSP_LibraryLookupSymbol,
    .LibraryEnumerate = LTCoreBSP_LibraryEnumerate
#endif
};

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  08-May-20   augustus    created from LTCoreImplWin32*
 *  16-Aug-20   augustus    bye bye LTObject, hello LTHandle
 *  26-Nov-20   augustus    got rid of getchar
 *  05-Dec-20   augustus    added memory info functions
 *  30-Dec-20   augustus    added pseudo key input ISR thread and raw terminal fiddling
 *  27-Jan-21   augustus    added LTCoreBSP_OS_EnumerateInstalledLibraries
 *  09-Feb-21   augustus    refactored for simplified BSP
 *  22-Feb-21   augustus    added ThreadGetStackUsage; made all stack sizes use u32 *  15-Jun-21   tiberius     remove handle concept from BSPs, pass in void * instead
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 */
