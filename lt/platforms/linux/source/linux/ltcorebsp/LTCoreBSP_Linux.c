/******************************************************************************
 * platforms/linux/source/linux/ltcorebsp/LTCoreBSP_Linux.c
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
#include <stdio.h>          // for getchar
#include <stddef.h>         // for ptrdiff_t
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <libgen.h>
#include <poll.h>
#include <time.h>
#include <sys/prctl.h>
#include <sys/sysinfo.h>
#include <malloc.h>

#include <lt/core/LTStdlib.h>
#include <lt/core/LTCore.h>
#include <lt/core/bsp/LTHostAPI.h>
#include <lt/core/bsp/LTCoreBSP.h>

#include <lt/core/LTThread.h> // for kLTThread_NumberOfPriorities

/*_______________________________
  uClibc compatibility defines */
#ifdef __UCLIBC__

/* Define clock identifiers missing from uClibc headers */
#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW		4
#endif
#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME			7
#endif

#endif

#if defined(__GLIBC__) && defined (__GLIBC_MINOR__) && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 33
typedef struct mallinfo2 LTMallInfo;

LT_INLINE LTMallInfo LTGetMallInfo(void) {
    return mallinfo2();
}
#else
typedef struct mallinfo LTMallInfo;

LT_INLINE LTMallInfo LTGetMallInfo(void) {
    return mallinfo();
}
#endif

/*___________
  #defines */
#define LTCOREBSP_DEFAULT_CLOCK                         CLOCK_MONOTONIC_RAW
#define LTCOREBSP_REAL_TIME_SCHED_POLICY                SCHED_RR
#define LTCOREBSP_NON_REAL_TIME_SCHED_POLICY            SCHED_OTHER
#define LTCOREBSP_ISR_PTHREAD_PRIORITY                  (s_nRealtimePriorityMin + kLTThread_NumberOfPriorities + 1)
#define LTCOREBSP_DISABLE_BARRIER_PTHREAD_PRIORITY      (LTCOREBSP_ISR_PTHREAD_PRIORITY + 1)
#define LTCOREBSP_DISABLE_PTHREAD_PRIORITY              (LTCOREBSP_DISABLE_BARRIER_PTHREAD_PRIORITY + 1)
#if __SIZEOF_SIZE_T__ == 8
#define LTCOREBSP_MIN_PTHREAD_STACK_SIZE                (PTHREAD_STACK_MIN  * 2)
#else
#define LTCOREBSP_MIN_PTHREAD_STACK_SIZE                (PTHREAD_STACK_MIN)
#endif
#define LTCOREBSP_STACK_PAGE_SIZE                       (512)       /* man page says stack size must be page size multiple but doesn't say page size; empirically determined to be 512 */
#define LTCOREBSP_ROUND_UP_TO_PAGE_MULTIPLE(nStackSize) ((nStackSize + (LTCOREBSP_STACK_PAGE_SIZE-1)) & ~(LTCOREBSP_STACK_PAGE_SIZE-1))
#define PRINT_STACKTISTICS                              0
#define LTCOREBSP_START_KERNELTIME_AT_ZERO              1
#define LTCOREBSP_MEM_SEAL_ALLOCATED                    0x600dBeef
#define LTCOREBSP_MEM_SEAL_FREED                        0xBadDeed5

#define LTCOREBSP_PTHREAD_CREATE_ERRORMESSAGE_EAGAIN    "system thread resource limit reached"
#define LTCOREBSP_PTHREAD_CREATE_ERRORMESSAGE_EINVAL    "invalid attributes specified"
#define LTCOREBSP_PTHREAD_CREATE_ERRORMESSAGE_EPERM     "inadequate privileges\n ** add -->  \"username - rtprio unlimited\"  to /etc/security/limits.conf  and logout/in"

DEFINE_BSP_LTLOG_SECTION("linux.bsp");

/*_______________________
  forward declarations */
static const LTCoreBSP s_bsp;
static LT_SIZE LTCoreBSP_GetAvailableSystemRAM_Internal(const LTMallInfo *minfo);
static void LTHostAPI_ThreadSleep(s64 nNanoseconds);
static bool LTHostAPI_ThreadInitializeAndRun(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pThreadProc)(void * pClientData), void * pClientData);
static void InitPseudoKeyInputISR(void);
static void ExitPseudoKeyInputISR(void);
static void InitInterruptDisableEmulation(void);
static void ExitInterruptDisableEmulation(void);
static void * LTHostAPI_ThreadGetCurrentThreadInstanceData(void);
static void UpdateThreadCurrentAndMaxStackUsage(void);

/*___________________
  static variables */
static const LTCoreBSP_LTCoreCallbacks *    s_pCoreCallbacks = NULL;
static LTAtomic                             s_LTCoreBSPInitialized = { 0 };
static clockid_t        s_clockID = LTCOREBSP_DEFAULT_CLOCK;
static s64              s_nHighFrequencyCounterResolution = 0;
static s64              s_nHighFrequencyCounterInitial = 0;
static pthread_key_t    s_keyThreadLocal = (pthread_key_t)0;
static int              s_nRealtimePriorityMin = 0;
static pthread_mutex_t  s_disableMutex;
static pthread_cond_t   s_disableCondition;
static pthread_t        s_disableThread = (pthread_t)0;
static pthread_t        s_disableBarrierThread = (pthread_t)0;
static int              s_nDisableThreadPriorityOld = 0;
static LTAtomic         s_disableCount = { 0 };
static int              s_keyInputPipe[2] = {0, 0};

static pthread_t        s_threadKeyInput = (pthread_t)0;
static struct termios   s_termiosNormal;
static struct termios   s_termiosRaw;

static LTAtomic         s_nMaxUOrdPages  = { 0 };
static LTAtomic         s_nFreeRAMLowWatermark  = { LT_U32_MAX };

#ifndef LT_NO_DYNAMIC_LOADER
    static LTLibrary_LTObjectMapEntry * s_pLTLibraryObjectMap;
    static char * s_pLTLibraryObjectMapData = NULL;

    static void InitLTLibraryObjectMap(void);
    static void FreeLTLibraryObjectMap(void);
#endif

/*_________________________________
  uClibc compatibility functions */
#ifdef __UCLIBC__

/* The function below is not implemented by uClibc. */

#if PRINT_STACKTISTICS
static int pthread_getname_np(pthread_t thread, char *name, size_t len)
{
    int written = snprintf(name, len, "%p", (void *)thread);
    if (written < 0)
        return EIO;

    if ((size_t)written >= len)
        return ERANGE;

    return 0;
}
#endif

#endif

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
/*___________________________________________________________________________________________________
  unbuffered console putstring - use only during bsp initialize; otherwise use BSP_LTLOG... macros */
static void LT_ISR_SAFE LTCoreBSP_ConsolePutString(const char * pString) {
    LTCoreBSP_PutCharsToConsole(pString, strlen(pString));
}

static bool
LTCoreBSP_InitMutexCommon(pthread_mutex_t * pMutex, int kind, bool bWithPriorityInheritance, bool bRobust) {
    // don't log from this function; is called from init
    bool bMutexInitialized = false, bAttrsInitialized = false;
    pthread_mutexattr_t mutexAttrs;
    do
    {
        if (false == (bAttrsInitialized = (0 == pthread_mutexattr_init(&mutexAttrs)))) break;
        if (0 != pthread_mutexattr_settype(&mutexAttrs, kind)) break;
        if (bWithPriorityInheritance && (0 != pthread_mutexattr_setprotocol(&mutexAttrs, PTHREAD_PRIO_INHERIT))) break;
        if (0 != pthread_mutexattr_setpshared(&mutexAttrs, PTHREAD_PROCESS_PRIVATE)) break;
        if (bRobust && (0 != pthread_mutexattr_setrobust(&mutexAttrs, PTHREAD_MUTEX_ROBUST))) break;
        bMutexInitialized = (0 == pthread_mutex_init(pMutex, &mutexAttrs));
    } while (false);
    if (bAttrsInitialized) pthread_mutexattr_destroy(&mutexAttrs);
    return bMutexInitialized;
}

static void OnProcessExit(void) {
    if (s_pCoreCallbacks) s_pCoreCallbacks->TerminateLT(0);
}

/*_____________________
  BSP initialization */
const LTCoreBSP *
LTCoreBSP_Initialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks) {

    // do not BSP_LTLOG from this function or from any it calls, BSP_LTLOG non-functional until after this function returns

    if (LTAtomic_Load(&s_LTCoreBSPInitialized)) return NULL; /* don't let anyone come in here except LTCore the first time */
    LTAtomic_Store(&s_LTCoreBSPInitialized, 1); /* don't need CompareAndExchange, LTCore calls this before any threads are running */

    /* change our cpu affinity to always run on core 0; all created pthreads will inherit this affinity and run on core 0
            This is a short term solution; after sweeping all cases for interrupt disable and making them not rely
            on interrupt disable for disabling task switching and mutual exclusion with other threads and sweeping
            to eradicate assumptions about using thread priorities for thread sequencing, we take this out */
    cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(0, &cpuset);
    if (0 != pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset)) LTCoreBSP_ConsolePutString("[linux.bsp.init] pthread_setaqffinity_np failed\\n");

    // initialize bsp log
    BSP_LTLOG_INITIALIZE(pCallbacks->LTCoreLogFunction);

    // remember pCallbacks
    s_pCoreCallbacks = pCallbacks;
    atexit(&OnProcessExit);

    signal(SIGPIPE, SIG_IGN);

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

    /* init interrupt Disable/Enable emulation */
    InitInterruptDisableEmulation();

    /* init pseudo key input (emulated UART) ISR */
    InitPseudoKeyInputISR();

    #ifndef LT_NO_DYNAMIC_LOADER
        InitLTLibraryObjectMap();
    #endif

    return &s_bsp;
}
void
LTCoreBSP_Finalize(const LTCoreBSP * pBSP) {
    if ((! LTAtomic_Load(&s_LTCoreBSPInitialized)) || (pBSP != &s_bsp)) return; /* don't let anyone except LTCore in here */

    #ifndef LT_NO_DYNAMIC_LOADER
        FreeLTLibraryObjectMap();
    #endif

    /* exit pseudo key input (emulated UART) ISR */
    ExitPseudoKeyInputISR();

    /* Exit our interrupt disable emulation */
    ExitInterruptDisableEmulation();

    s_nRealtimePriorityMin = 0;
    pthread_key_delete(s_keyThreadLocal);
    s_keyThreadLocal = (pthread_key_t)0;
    s_nHighFrequencyCounterInitial = 0;
    s_nHighFrequencyCounterResolution = 0;
    s_clockID = LTCOREBSP_DEFAULT_CLOCK;

    s_pCoreCallbacks = NULL;

    LTAtomic_Store(&s_LTCoreBSPInitialized, 0);
}

/*_________________________________
  LTCoreBSP emulated ISR Context */
static pthread_t s_threadISRContext = (pthread_t)0;

static void EnterISRContext(void) {
    s_threadISRContext = pthread_self();
}

static void ExitISRContext(void) {
    if (pthread_self() == s_threadISRContext) s_threadISRContext = (pthread_t)0;
}

/*________________________________________________________________
  LTCoreBSP global functions (statically linked only with LTCore) \________________________________________________
  Excluded from s_bsp struct to enable direct placement into LTCore and LTStdlib interfaces to avoid extra thunk */
bool LT_ISR_SAFE
LTCoreBSP_InsideInterruptContext(void) {
    return s_threadISRContext == pthread_self();
}

LT_SIZE LT_ISR_SAFE
LTCoreBSP_DisableInterrupts(void) {
    LT_SIZE retVal = 0;
    pthread_mutex_lock(&s_disableMutex);
    while ((pthread_t)0 != s_disableThread && pthread_self() != s_disableThread) pthread_cond_wait(&s_disableCondition, &s_disableMutex);

    if (((pthread_t)0 == s_disableThread && LTAtomic_Load(&s_disableCount)) ||
        (s_disableThread != (pthread_t)0 &&  s_disableThread != pthread_self()) ||
        (s_disableThread != (pthread_t)0 && (0 == LTAtomic_Load(&s_disableCount)))) {
        BSP_LTLOG_YELLOWALERT("disable", "inconsistent disable state");
        pthread_mutex_unlock(&s_disableMutex);
        return 0;
    }

    LTAtomic_FetchAdd(&s_disableCount, 1);
    if ((pthread_t)0 == s_disableThread) {
        s_disableThread = pthread_self();
        int nPolicy = LTCOREBSP_NON_REAL_TIME_SCHED_POLICY;
        struct sched_param param;

        if (0 == pthread_getschedparam(pthread_self(), &nPolicy, &param)) {
            s_nDisableThreadPriorityOld = (nPolicy == LTCOREBSP_REAL_TIME_SCHED_POLICY) ? param.sched_priority : 0;
        }
        else s_nDisableThreadPriorityOld = 0;
        retVal = (LT_SIZE)s_nDisableThreadPriorityOld;

        // make this thread higher priority than anything else
        nPolicy = LTCOREBSP_REAL_TIME_SCHED_POLICY;
        param.sched_priority = LTCOREBSP_DISABLE_PTHREAD_PRIORITY;
        pthread_setschedparam(pthread_self(), nPolicy, &param);
        /* signal the barrier thread to busy-loop so if this thread, having 'disabled interrupts' blocks
           for any reason (as is happening on write calls to stdio), the barrier thread will steal the cpu
           and prevent all lower threads from running.  ISSUE - does this work on multi-core ?*/
        pthread_cond_signal(&s_disableCondition);
    }
    pthread_mutex_unlock(&s_disableMutex);

    return retVal;
}

void LT_ISR_SAFE
LTCoreBSP_EnableInterrupts(LT_SIZE nMask) {
    /* lock and unlock to make sure I'm the thread that did the Disable() */
    pthread_mutex_lock(&s_disableMutex);
    if (pthread_self() != s_disableThread)
    {
        pthread_mutex_unlock(&s_disableMutex);
        BSP_LTLOG_YELLOWALERT("enable.wrongthread", "thread calling enable did not disable");
        return;
    }

    if (1 == LTAtomic_FetchSubtract(&s_disableCount, 1)) {
        struct sched_param param;
        int nPolicy = s_nDisableThreadPriorityOld ? LTCOREBSP_REAL_TIME_SCHED_POLICY : LTCOREBSP_NON_REAL_TIME_SCHED_POLICY;
        if ((int)nMask != s_nDisableThreadPriorityOld) {
            BSP_LTLOG_YELLOWALERT("enable.wrongmask", "nMask doesn't match original priority");
        }
        nMask = (LT_SIZE)s_nDisableThreadPriorityOld; /* always use our truth */
        s_disableThread = (pthread_t)0;
        s_nDisableThreadPriorityOld = 0;
        param.sched_priority = (int)nMask;
        /* signal the barrier thread to stop busy-looping and put our priority back where it was */
        pthread_cond_signal(&s_disableCondition);
        if (0 != pthread_setschedparam(pthread_self(), nPolicy, &param)) {
            BSP_LTLOG_YELLOWALERT("enable.priority", "failed to restore originalthread priority");
        }
    }
    pthread_mutex_unlock(&s_disableMutex);
}

bool LT_ISR_SAFE
LTCoreBSP_InterruptsAreDisabled(void) {
    pthread_mutex_lock(&s_disableMutex);
    bool disabled = (pthread_t)0 != s_disableThread && LTAtomic_Load(&s_disableCount);
    pthread_mutex_unlock(&s_disableMutex);
    return disabled;
}

void
LTCoreBSP_DebugBreak(void) {
    BSP_LTLOG_STOMP("break", "triggering trap to force a core dump");
    __builtin_trap();
}

#ifdef __mips__
/*
 * abort(3) will normally call raise(SIGABRT) which will call
 * kill(getpid(), SIGABRT) resulting in a core dump.
 *
 * For the MIPS Linux on T31 SoC based systems the resulting core dump
 * will have an back trace that GDB cannot properly follow. This makes
 * it very hard to figure out which part of LT caused heap corruption
 * that uClibc reports by calling abort(3).
 *
 * The abort(3) implementation below therefore triggers a CPU trap
 * which will result in a more useful core dump.
 */
void abort(void) {
    __builtin_trap();
}
#endif

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
        return true; /* DRW 07-Feb-23 : always invoke debugger, even in release mode now */
    #else
        #ifdef LT_DEBUG
            return true;
        #endif
        return false; /* we shall not invoke debugger in release mode */
    #endif
}

/*_______________________
  allocation functions */
static void
UpdateFreeRAMLowWatermark(bool force) {
    LTMallInfo minfo = LTGetMallInfo();

    if (!force) {
        // Determine amount of memory allocated by the C library in 4K blocks
        // Using 4K blocks should avoid overflowing the 32 bit atomic type
        // and be a good enough approximation of the system's page size.
        u32 nPotentialMaxUOrdPages = (minfo.uordblks + 4095) / 4096;

        // Is this a new maximum?
        u32 nMaxUOrdPages;
        do {
            nMaxUOrdPages = LTAtomic_Load(&s_nMaxUOrdPages);
            if (nPotentialMaxUOrdPages <= nMaxUOrdPages) return;
        } while (false == LTAtomic_CompareAndExchange(&s_nMaxUOrdPages,
                                                      nMaxUOrdPages,
                                                      nPotentialMaxUOrdPages));
    }

    // The C library's memory consumption is at a new high.
    // Check whether the overall system memory consumption is at a new high.
    LT_SIZE nNewPotentialLow = LTCoreBSP_GetAvailableSystemRAM_Internal(&minfo);
    LT_SIZE nLowWatermarkBytes;
    do {
        nLowWatermarkBytes = LTAtomic_Load(&s_nFreeRAMLowWatermark);;
        if (nLowWatermarkBytes <= nNewPotentialLow) break; // free watermark is lower already
    } while (false == LTAtomic_CompareAndExchange(&s_nFreeRAMLowWatermark, (u32)nLowWatermarkBytes, (u32)nNewPotentialLow));
}

/* malloc, realloc, and free */
static void * LTHostAPI_malloc(LT_SIZE nBytes) {
    void * pMem = malloc(nBytes);
    UpdateFreeRAMLowWatermark(nBytes && !pMem ? true : false);
    UpdateThreadCurrentAndMaxStackUsage();
    return pMem;
}

static void LTHostAPI_free(void * pMem) {
    UpdateThreadCurrentAndMaxStackUsage();
    free(pMem);
}

static void * LTHostAPI_realloc(void * pMem, LT_SIZE nBytes) {
    void * pNewMem = realloc(pMem, nBytes);
    UpdateFreeRAMLowWatermark(nBytes && !pMem ? true : false);
    return pNewMem;
}

/*________________
  ram functions */
static LT_SIZE
LTCoreBSP_GetTotalSystemRAM(void) {
    struct sysinfo sinfo;
    int ret = sysinfo(&sinfo);
    if (ret != 0) {
        BSP_LTLOG_YELLOWALERT("totalram", "sysinfo failed");
        return 0;
    }
#if LT_ARCHITECTURE_BITS == 64
    return (LT_SIZE)((LT_SIZE)sinfo.totalhigh << 32) | sinfo.totalram;
#else
    return (LT_SIZE)sinfo.totalram;
#endif
}

static LT_SIZE
LTCoreBSP_GetAvailableSystemRAM_Internal(const LTMallInfo *minfo) {
    struct sysinfo sinfo;
    int ret = sysinfo(&sinfo);
    if (ret != 0) {
        BSP_LTLOG_YELLOWALERT("availram", "sysinfo failed");
        return 0;
    }
#if LT_ARCHITECTURE_BITS == 64
    LT_SIZE sysFree = ((LT_SIZE)sinfo.freehigh << 32) | sinfo.freeram;
#else
    LT_SIZE sysFree = sinfo.freeram;
#endif

    LT_SIZE procFree = minfo->fordblks;

    // open /proc/meminfo to read cached memory in kB
    FILE *file;
    file = fopen("/proc/meminfo", "r");
    if (!file) {
        BSP_LTLOG_YELLOWALERT("availram", "meminfo read failed");
        return 0;
    }
    char line[40];
    unsigned long int   cached          = 0;
    bool                readCached      = false;
    unsigned long int   reclaimable     = 0;
    bool                readReclaimable = false;
    unsigned long int   shmem           = 0;
    bool                readShmem       = false;
    while (fgets(line, sizeof(line), file) && !(readCached && readReclaimable && readShmem)) {
        if (!readCached && sscanf(line, "Cached: %lu %*s\n", &cached) == 1)
            readCached = true;
        if (!readReclaimable && sscanf(line, "SReclaimable: %lu %*s\n", &reclaimable) == 1)
            readReclaimable = true;
        if (!readShmem && sscanf(line, "Shmem: %lu %*s\n", &shmem) == 1) // tmpfs uses shmem
            readShmem = true;
    }
    fclose(file);
    LT_SIZE cachedBytes = cached * 1024;
    cachedBytes -= shmem * 1024;         // shmem is included in cached, but it is not reclaimable memory
    cachedBytes += reclaimable * 1024;   // slab cache
    cachedBytes += sinfo.bufferram;      // buffers

    // consider buffer cache memory as part of free memory
    return sysFree + procFree + cachedBytes;
}

static LT_SIZE
LTCoreBSP_GetAvailableSystemRAM(void) {
    LTMallInfo minfo = LTGetMallInfo();
    return LTCoreBSP_GetAvailableSystemRAM_Internal(&minfo);
}

static LT_SIZE
LTCoreBSP_GetSystemRAMLowWatermark(void) {
    return (LT_SIZE)LTAtomic_Load(&s_nFreeRAMLowWatermark);
}

/*________
  mutex */
static LT_SIZE
LTHostAPI_MutexInstanceSize(void) {
    return sizeof(pthread_mutex_t);
}

static void
LTHostAPI_MutexInitialize(void * pMutex) {
    if (! LTCoreBSP_InitMutexCommon(pMutex, PTHREAD_MUTEX_RECURSIVE_NP, true, true)) {
        BSP_LTLOG_YELLOWALERT("mutexinit", "InitMutexCommon failed");
    }
}

static void
LTHostAPI_MutexFinalize(void * pMutex) {
    pthread_mutex_destroy((pthread_mutex_t *)pMutex);
}

static void
LTHostAPI_MutexLock(void * pMutex) {
    UpdateThreadCurrentAndMaxStackUsage();
    switch(pthread_mutex_lock((pthread_mutex_t *)pMutex)) {
        case 0:
            break;
        case EOWNERDEAD:
            pthread_mutex_consistent((pthread_mutex_t *)pMutex);
            BSP_LTLOG("mutexlock", "EOWNERDEAD, non-fatal");
            break;
        case EAGAIN:
            BSP_LTLOG_YELLOWALERT("mutexlock", "EAGAIN, mutex recursion limit reached");
            break;
        case EINVAL:
            BSP_LTLOG_YELLOWALERT("mutexlock", "EIVAL, mutex protect ceiling error");
            LT_ASSERT(0);
            break;
        case ENOTRECOVERABLE:
            BSP_LTLOG_YELLOWALERT("mutexlock", "ENOTRECOVERABLE");
            break;
        case EDEADLK:
            BSP_LTLOG_YELLOWALERT("mutexlock", "EDEADLK, recursing on non-recursive mutex");
            break;
        default:
            BSP_LTLOG_YELLOWALERT("mutexlock", "unknown error");
            break;
    }

}

static void
LTHostAPI_MutexUnlock(void * pMutex) {
    pthread_mutex_unlock((pthread_mutex_t *)pMutex);
}

static bool
LTHostAPI_MutexTryLock(void * pMutex) {
    UpdateThreadCurrentAndMaxStackUsage();
    return (0 == pthread_mutex_trylock((pthread_mutex_t *)pMutex)) ? true : false;
}

/*__________
  monitor */
struct MonitorData {
    pthread_mutex_t m_mutex;
    pthread_cond_t  m_condition;
    LTAtomic        m_nFlags;
};
#define MONITOR_FLAG_VALID              (1 << 0)
#define MONITOR_FLAG_SIGNALLED          (1 << 1)
LT_INLINE void MonitorSetValid(struct MonitorData * pData)                 {         LTAtomic_FetchOr(&pData->m_nFlags,            MONITOR_FLAG_VALID);     }
LT_INLINE bool MonitorIsValid(struct MonitorData * pData)                  { return (LTAtomic_Load(&pData->m_nFlags) &             MONITOR_FLAG_VALID);     }
LT_INLINE void MonitorSetSignalled(struct MonitorData * pData)             {         LTAtomic_FetchOr(&pData->m_nFlags,            MONITOR_FLAG_SIGNALLED); }
LT_INLINE void MonitorClearSignalled(struct MonitorData * pData)           {         LTAtomic_FetchAnd(&pData->m_nFlags,          ~MONITOR_FLAG_SIGNALLED);  }
LT_INLINE bool MonitorIsSignalledCheckAndReset(struct MonitorData * pData) { return  LTAtomic_CompareAndExchange(&pData->m_nFlags, MONITOR_FLAG_SIGNALLED | MONITOR_FLAG_VALID, MONITOR_FLAG_VALID); }

static LT_SIZE
LTHostAPI_MonitorInstanceSize(void) {
    return sizeof(struct MonitorData);
}

static void
LTHostAPI_MonitorInitialize(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;

    pthread_condattr_t condAttrs;

    bool bMutexInitialized = false;
    bool bCondAttrsInited = false;
    LTAtomic_Store(&pData->m_nFlags, 0);

    do
    {
        if (false == (bMutexInitialized = LTCoreBSP_InitMutexCommon(&pData->m_mutex, PTHREAD_MUTEX_ERRORCHECK_NP, true, true))) break;
        if (false == (bCondAttrsInited  = (0 == pthread_condattr_init(&condAttrs)))) break;
        if (0 != pthread_condattr_setclock(&condAttrs, CLOCK_MONOTONIC)) break;
        if (0 != pthread_cond_init(&pData->m_condition, &condAttrs)) break;
        MonitorSetValid(pData);
    } while (false);

    if (bCondAttrsInited) pthread_condattr_destroy(&condAttrs);
    if (false == MonitorIsValid(pData)) {
        if (bMutexInitialized) pthread_mutex_destroy(&pData->m_mutex);
        BSP_LTLOG_YELLOWALERT("monitorinit", "failure");
    }
}

static void
LTHostAPI_MonitorFinalize(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData))
    {
        pthread_cond_destroy(&pData->m_condition);
        pthread_mutex_destroy(&pData->m_mutex);
        LTAtomic_Store(&pData->m_nFlags, 0);
    }
}

static void
LTHostAPI_MonitorEnter(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) {
        UpdateThreadCurrentAndMaxStackUsage();
        switch (pthread_mutex_lock(&pData->m_mutex)) {
            case 0:
                return;
            case EDEADLK:
                BSP_LTLOG_YELLOWALERT("monitorenter", "EDEADLK, illegal Nest");
                return;
            default:
                BSP_LTLOG_YELLOWALERT("monitorenter", "unknown error");
                return;
        }
    }
    BSP_LTLOG_YELLOWALERT("monitorenter", "invalid monitor");
}

static void
LTHostAPI_MonitorExit(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) {
        switch (pthread_mutex_unlock(&pData->m_mutex)) {
            case 0:         return;
            case EPERM:     BSP_LTLOG_YELLOWALERT("monitorexit", "EPERM, illegal Nest"); return;
            default:        BSP_LTLOG_YELLOWALERT("monitorexit", "unknown error"); return;
        }
    }
    BSP_LTLOG_YELLOWALERT("monitorexit", "invalid monitor");
}

static void LT_ISR_SAFE
LTHostAPI_MonitorNotify(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) {
        if (LTCoreBSP_InsideInterruptContext()) MonitorSetSignalled(pData);
        if (0 != pthread_cond_signal(&pData->m_condition)) {
            BSP_LTLOG_YELLOWALERT("monitornotify", "pthread_cond_signal error");
        }
    }
    else {
        BSP_LTLOG_YELLOWALERT("monitornotify", "invalid monitor");
    }
}

static bool ConvertRelativeNanosecondsToOverflowProtectedAbsoluteTimespec(clockid_t clockID, s64 nTimeoutNanoseconds, struct timespec * pTimeSpec) {

    if (nTimeoutNanoseconds == LTTIME_INFINITE) return false;
    if ((nTimeoutNanoseconds < 0) || (nTimeoutNanoseconds > LTTIME_INFINITE)) nTimeoutNanoseconds = 0;
    if (nTimeoutNanoseconds < 1) { clock_gettime(clockID, pTimeSpec); return true; }

    // convert nTimeoutNanoseconds to seconds and nanoseconds
    s64 nSeconds = LTCoreBSP_NanosecondsToSeconds(nTimeoutNanoseconds);
    nTimeoutNanoseconds -= LTCoreBSP_SecondsToNanoseconds(nSeconds);
    clock_gettime(clockID, pTimeSpec);
    pTimeSpec->tv_sec += nSeconds;
    pTimeSpec->tv_nsec += nTimeoutNanoseconds;
    while (pTimeSpec->tv_nsec >= 1000000000) { pTimeSpec->tv_nsec -= 1000000000; pTimeSpec->tv_sec++; }
    return true;
}

static bool
LTHostAPI_MonitorWait(void * pMonitor, s64 nTimeoutNanoseconds) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (! MonitorIsValid(pData)) { BSP_LTLOG_YELLOWALERT("monitorwait", "invalid monitor"); return false; }
    if (MonitorIsSignalledCheckAndReset(pData)) return true;
    UpdateThreadCurrentAndMaxStackUsage();

    bool bRetVal = false;
    struct timespec timeSpec;

    if (ConvertRelativeNanosecondsToOverflowProtectedAbsoluteTimespec(CLOCK_MONOTONIC, nTimeoutNanoseconds, &timeSpec)) {
        int retVal =  pthread_cond_timedwait(&pData->m_condition, &pData->m_mutex, &timeSpec);
        switch (retVal) {
            case 0:
                bRetVal = true;
                break;
            case ETIMEDOUT:
                break;
            case EPERM:
                BSP_LTLOG_YELLOWALERT("monitortimedwait", "EPERM, monitor not entered");
                break;
            case EOWNERDEAD:
                bRetVal = true;
                pthread_mutex_consistent((pthread_mutex_t *)&pData->m_mutex);
                BSP_LTLOG("monitortimedwait", "EOWNERDEAD, non-fatal");
                break;
            case EINVAL:
                BSP_LTLOG_YELLOWALERT("monitortimedwait", "EINVAL, bad timeout: seconds(%lld), nanoseconds(%lld), nTimeoutNanoseconds(%lld).  WaWa, WHAT?", LT_Ps64(timeSpec.tv_sec), LT_Ps64(timeSpec.tv_nsec), LT_Ps64(nTimeoutNanoseconds));
                break;
            default:
                BSP_LTLOG_YELLOWALERT("monitortimedwait", "other error %d", retVal);
                break;
        }
    }
    else {
        switch (pthread_cond_wait(&pData->m_condition, &pData->m_mutex)) {
            case 0:
                bRetVal = true;
                break;
            case EPERM:
                BSP_LTLOG_YELLOWALERT("monitorwait", "EPERM, monitor not entered");
                break;
            case EOWNERDEAD:
                BSP_LTLOG("monitorwait", "EOWNERDEAD, non-fatal");
                pthread_mutex_consistent((pthread_mutex_t *)&pData->m_mutex);
                bRetVal = true;
                break;
            default:
                BSP_LTLOG_YELLOWALERT("monitorwait", "other error");
                break;
        }
    }
    if (bRetVal) MonitorClearSignalled(pData);
    return bRetVal;
}

/*__________
  thread  */
struct LinuxThreadInstanceData {
    pthread_t m_thread;
    void (*   m_pThreadProc)(void * pClientData);
    void  *   m_pClientData;
    bool      m_bCreated;
    char      m_name[16];
    pid_t     m_tid;             // expose tid for /proc/pid/task/tid/stat
    LT_SIZE   stackSize;
    LT_SIZE   stackBottom;
    LT_SIZE   currentStackUsage;
    LT_SIZE   maxStackUsage;
};

static void UpdateThreadCurrentAndMaxStackUsage(void) {
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)LTHostAPI_ThreadGetCurrentThreadInstanceData();
    if (pInstanceData && pInstanceData->stackBottom) {
        pInstanceData->currentStackUsage = (LT_SIZE)&pInstanceData > pInstanceData->stackBottom ? (LT_SIZE)&pInstanceData - pInstanceData->stackBottom : pInstanceData->stackBottom - (LT_SIZE)&pInstanceData;
        if (pInstanceData->currentStackUsage > pInstanceData->maxStackUsage) pInstanceData->maxStackUsage = pInstanceData->currentStackUsage;
    }
}

static int LTCoreBSP_LTPriorityToLinuxPThreadPriority(u8 nLTPriority) {
    return nLTPriority ? s_nRealtimePriorityMin + (nLTPriority - 1) : 0;
}

static void SetThreadName(const char *name) {
    prctl(PR_SET_NAME, (unsigned long)name, 0, 0, 0);
}

/* implement gettid on UCLIBC and GLIBC versions 2.29 and lower */
#if defined(__UCLIBC__) || (defined(__GLIBC__) && defined (__GLIBC_MINOR__) && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 30)))

#include <sys/syscall.h>
#include <unistd.h>

static pid_t gettid(void) {
    return syscall(__NR_gettid);
}

#endif

static void * LTCoreBSP_OSThread_LinuxThreadProc(void * pClientData) {
    struct LinuxThreadInstanceData *pInstanceData = pClientData;
    pInstanceData->stackBottom = (LT_SIZE)&pInstanceData;
    pInstanceData->currentStackUsage = pInstanceData->maxStackUsage = 0;
    pInstanceData->m_tid = gettid();
    SetThreadName(pInstanceData->m_name);
    pthread_setspecific(s_keyThreadLocal, pClientData);
    #if PRINT_STACKTISTICS
        PrintStackTistics("ThreadProc", &pClientData);
    #endif
    UpdateThreadCurrentAndMaxStackUsage();
    pInstanceData->m_pThreadProc(pInstanceData->m_pClientData);
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

    /* no need to start the scheduler on linux, just run the thread */
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

static const char * ThreadCreateErrnoToString(int errNo) {
    switch (errNo) {
        case 0:      return "pthread_attr failure";
        case EAGAIN: return LTCOREBSP_PTHREAD_CREATE_ERRORMESSAGE_EAGAIN;
        case EINVAL: return LTCOREBSP_PTHREAD_CREATE_ERRORMESSAGE_EINVAL;
        case EPERM:  return LTCOREBSP_PTHREAD_CREATE_ERRORMESSAGE_EPERM;
        default:     return "?";
    }
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
    struct sched_param schedParam;
    schedParam.sched_priority = LTCoreBSP_LTPriorityToLinuxPThreadPriority(nPriority);
    int nPolicy = nPriority ? LTCOREBSP_REAL_TIME_SCHED_POLICY : LTCOREBSP_NON_REAL_TIME_SCHED_POLICY;

    pInstanceData->m_thread       = 0;
    pInstanceData->m_pThreadProc  = pThreadProc;
    pInstanceData->m_pClientData  = pClientData;
    pInstanceData->m_bCreated     = false;
    pInstanceData->stackBottom = pInstanceData->currentStackUsage = pInstanceData->maxStackUsage = 0;

    /* pthreads won't create without a 16K minimum stack, and stack sizes have to be a multiple of the page size */
    nStackSize = (nStackSize < LTCOREBSP_MIN_PTHREAD_STACK_SIZE) ? LTCOREBSP_MIN_PTHREAD_STACK_SIZE : LTCOREBSP_ROUND_UP_TO_PAGE_MULTIPLE(nStackSize);
    pInstanceData->stackSize = nStackSize;

    do
    {
        if (0 != pthread_attr_init(&attrs)) break;
        bAttrsInitialized = true;
        if (0 != pthread_attr_setstacksize(&attrs, (size_t)nStackSize)) break;
        if (0 != pthread_attr_setinheritsched(&attrs, PTHREAD_EXPLICIT_SCHED)) break;
        if (0 != pthread_attr_setschedpolicy(&attrs, nPriority ? LTCOREBSP_REAL_TIME_SCHED_POLICY : LTCOREBSP_NON_REAL_TIME_SCHED_POLICY)) break;
        if (0 != pthread_attr_setschedparam(&attrs, &schedParam)) break;

        if (NULL == pName || 0 == *pName) pName = "unnamed";
        unsigned i; // must limit to 16 or prctl will fail
        for (i = 0; i < (sizeof(pInstanceData->m_name)-1); i++) if (pName[i]) pInstanceData->m_name[i] = pName[i]; else break;
        pInstanceData->m_name[i] = 0;
        if (0 != (nPolicy = pthread_create(&pInstanceData->m_thread, &attrs, &LTCoreBSP_OSThread_LinuxThreadProc, pInstanceData))) {
            pInstanceData->m_thread = 0;
            break;
        }
        pInstanceData->m_bCreated = true;
    }
    while (false);

    if (bAttrsInitialized) pthread_attr_destroy(&attrs);

    if (! pInstanceData->m_bCreated) {
        pInstanceData->m_thread = 0;
        pInstanceData->m_pThreadProc = NULL;
        pInstanceData->m_pClientData = NULL;
        BSP_LTLOG_YELLOWALERT("threadinit", "thread \"%s\" creation failed: %s", pName, ThreadCreateErrnoToString(nPolicy));
    }

    return pInstanceData->m_bCreated;
}

static void
LTHostAPI_ThreadGetStackUsage(void * pThread, u32 * pStackSizeToSet, u32 * pCurrentStackUsageToSet, u32 * pMaxStackUsageToSet) {
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)pThread;
    if (pInstanceData == LTHostAPI_ThreadGetCurrentThreadInstanceData()) UpdateThreadCurrentAndMaxStackUsage();
    *pStackSizeToSet = pInstanceData->stackSize;
    *pCurrentStackUsageToSet = pInstanceData->currentStackUsage;
    *pMaxStackUsageToSet = pInstanceData->maxStackUsage;
}

static void *
LTHostAPI_ThreadGetCurrentThreadInstanceData(void) {
    return pthread_getspecific(s_keyThreadLocal);
}

static void
LTHostAPI_ThreadSleep(s64 nNanoseconds) {
    struct timespec delay, remaining;
    if (nNanoseconds) {
        delay.tv_sec  = (time_t)LTCoreBSP_NanosecondsToSeconds(nNanoseconds);
        delay.tv_nsec = (long)(nNanoseconds - LTCoreBSP_SecondsToNanoseconds(delay.tv_sec));
    }
    else {
        /* 0 is supposed to mean Yield() but Linux doesn't Yield on 0 so hack in 10ms */
        delay.tv_sec  = 0;
        delay.tv_nsec = LTCoreBSP_MillisecondsToNanoseconds(10);
    }
    UpdateThreadCurrentAndMaxStackUsage();
    while (0 != nanosleep(&delay, &remaining)) delay = remaining;
}

static bool
LTHostAPI_ThreadSetPriority(void * pThread, u8 nPriority) {
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

        // set the new policy
        nPolicy = nPriority ? LTCOREBSP_REAL_TIME_SCHED_POLICY : LTCOREBSP_NON_REAL_TIME_SCHED_POLICY;
        param.sched_priority = nPriorityPThread;

        if (0 == pthread_setschedparam(pInstanceData->m_thread, nPolicy, &param)) return true;
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

    UpdateThreadCurrentAndMaxStackUsage();

    struct timespec timeSpec;
    int error = ConvertRelativeNanosecondsToOverflowProtectedAbsoluteTimespec(CLOCK_REALTIME, nTimeoutNanoseconds, &timeSpec)
        ? pthread_timedjoin_np(pInstanceData->m_thread, NULL, &timeSpec)
        : pthread_join(pInstanceData->m_thread, NULL);

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

static s64
LTHostAPI_GetThreadCpuUtilization(void *pThread) {
    if (!pThread) return -1;
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)pThread;
    pid_t tid = pInstanceData->m_tid;
    // make /proc/pid/task/tid/stat file name
    pid_t pid = getpid();
    char buf[64]; // enough to hold a full path
    lt_snprintf(buf, sizeof(buf), "/proc/%lld/task/%lld/stat", LT_Ps64(pid), LT_Ps64(tid));
    // read stat
    bool data = true; // default to true so if "bailure:" label is reached, it will know it came from fopen failure
    FILE *file = fopen(buf, "r");
    if (!file) goto bailure;
    data = false; /* now any future error will be a read error */
    s64 cpuTime = 0;
    do {
        // man -s 5 proc
        // 1    2      3 4    5    6    7 8  9       10    11 12 13 14 15 ...
        // 7478 (name) S 1248 1906 1906 0 -1 4194304 11318 0  97 0  90 57 ...
        if (fscanf(file, "%63[^)]", buf) < 0) break; // scan pid (name
        for (int i = 2; i < 14; ++i) {
            if (fscanf(file, "%63s", buf) < 0) break;
        }
        if (fscanf(file, "%63s", buf) < 0) break;
        cpuTime = lt_strtou32(buf, NULL, 10);        // utime
        if (fscanf(file, "%63s", buf) < 0) break;
        cpuTime += lt_strtou32(buf, NULL, 10);       // stime
        data = true;
    } while (0);
    fclose(file);
    if (!data) goto bailure;

    return cpuTime * (1000000000 / sysconf(_SC_CLK_TCK));  // convert to nanoseconds

 bailure:
    BSP_LTLOG("thread.cpustats", "Error %s /proc/%lld/task/%lld/stat for thread %s", data ? "opening" : "reading", LT_Ps64(pid), LT_Ps64(tid), pInstanceData->m_name);
    return -1;
}

/*___________________________________________________________________________________________________________________
  Dynamic library enumeration and loading ala dlopen/dlsym/dlclose - only if platform has a runtime dynamic loader */
#ifndef LT_NO_DYNAMIC_LOADER
    // Get the directory in which the binary executed by this process resides.
    static const char *
    LTCoreBSP_GetExecDirectory(void) {
        static char s_exeDir[PATH_MAX];

        if (s_exeDir[0])
            return s_exeDir;

        char *p_exePath = malloc(PATH_MAX);
        if (!p_exePath)
            return NULL;
        ssize_t bytes = readlink("/proc/self/exe", p_exePath, PATH_MAX);
        if (bytes < 0 || bytes >= (ssize_t)PATH_MAX) {
            free(p_exePath);
            return NULL;
        }
        p_exePath[bytes] = '\0';

        strcpy(s_exeDir, dirname(p_exePath));
        free(p_exePath);

        return s_exeDir;
    }

    // Load the given shared object from the program directory
    static void *
    LTCoreBSP_LoadSharedObject(const char *filename) {
        const char * pExeDir = LTCoreBSP_GetExecDirectory();
        if (!pExeDir)
            return NULL;

        char * pDLPath = NULL;
        if (asprintf(&pDLPath, "%s/%s", pExeDir, filename) < 0 || !pDLPath)
            return NULL;

        void *handle = dlopen(pDLPath, RTLD_NOW | RTLD_LOCAL);

        free(pDLPath);
        return handle;
    }

    static bool
    LTHostAPI_LibraryLoad(const char * pLTLibraryName, void ** pLibraryHandleToSet) {
        /* This function returns one of the following result codes:
               LTHOSTAPI_LOADLIBRARY_SUCCESS,
               LTHOSTAPI_LOADLIBRARY_ERROR_LIBNOTFOUND,
               LTHOSTAPI_LOADLIBRARY_ERROR_UNRESOLVEDSYMBOL,
               LTHOSTAPI_LOADLIBRARY_ERROR_LIBINVALID,
               LTHOSTAPI_LOADLIBRARY_ERROR_GENERIC           */

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
        if (NULL != (*pLibraryHandleToSet = LTCoreBSP_LoadSharedObject(s_libraryString))) return LTHOSTAPI_LOADLIBRARY_SUCCESS;
        else {
            const char * pError = dlerror();
            s_pCoreCallbacks->ReportLibraryLoaderFunctionFailure("dlopen", s_libraryString, pError ? pError :"failed");
        }

        return LTHOSTAPI_LOADLIBRARY_ERROR_GENERIC;
    }

    static void
    LTHostAPI_LibraryUnload(void * pLibraryHandle) {
        dlclose(pLibraryHandle);
    }

    static void *
    LTHostAPI_LibraryLookupSymbol(void * pLibraryHandle, const char * pSymbolName) {
        void * pAddress = (void *)dlsym(pLibraryHandle, pSymbolName);
        if (NULL == pAddress) {
            const char * pError = dlerror();
            s_pCoreCallbacks->ReportLibraryLoaderFunctionFailure("dlsym", pSymbolName, pError ? pError :"failed");
        }
        return pAddress;
    }

    static bool
    LTHostAPI_LibraryEnumerate(LTHostAPI_LibraryEnumProc * pEnumProc, void * pClientData) {
        const char * pExeDir = LTCoreBSP_GetExecDirectory();
        if (!pExeDir)
            return false;

        bool bRetVal = true;
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
        return bRetVal;
    }

    static bool ReadLTLibraryObjectMapEntry(LTLibrary_LTObjectMapEntry * pEntry, char ** ppChar, size_t * sizeData, int count) {
        pEntry->pNextEntry = count == 1 ? NULL : pEntry + 1;
        pEntry->specializationName = *ppChar;
        while (*sizeData && **ppChar != ':') { *ppChar = *ppChar + 1; *sizeData = *sizeData - 1; }
        if ((*sizeData == 0) || (**ppChar != ':')) return false;
        **ppChar = 0; *ppChar = *ppChar + 1; *sizeData = *sizeData - 1;
        pEntry->objectApiName = *ppChar;
        while (*sizeData && **ppChar != ':') { *ppChar = *ppChar + 1; *sizeData = *sizeData - 1; }
        if ((*sizeData == 0) || (**ppChar != ':')) return false;
        **ppChar = 0; *ppChar = *ppChar + 1; *sizeData = *sizeData - 1;
        pEntry->libraryName = *ppChar;
        while (*sizeData && **ppChar != '\n') { *ppChar = *ppChar + 1; *sizeData = *sizeData - 1; }
        if (((*sizeData == 0) && (count > 1)) || (**ppChar != '\n')) return false;
        **ppChar = 0; *ppChar = *ppChar + 1; *sizeData = *sizeData - 1;
        return true;
    }

#if 0
    static void DumpLTLibraryObjectMap(void) {
        int numEntries = 0;
        LTLibrary_LTObjectMapEntry * pEntry = s_pLTLibraryObjectMap;
        printf("LTLibrary Object Map:\n");
        while (pEntry) {
            printf("%s:%s:%s\n", pEntry->specializationName, pEntry->objectApiName, pEntry->libraryName);
            pEntry = pEntry->pNextEntry;
            numEntries++;
        }
        printf ("%d entries\n", numEntries);
    }
#endif

    #define COMMAND_BASE "find %s -name \"*.so\" -exec strings {} + | grep \"^ltobject_export_string:\" | cut -f2- -d':' | sort"
    #define COMMAND_COUNT COMMAND_BASE " | wc -l -c"
    #define COMMAND_DATA  COMMAND_BASE

    static void InitLTLibraryObjectMap(void) {
        char dataString[kLTLibrary_MaxNameBufferSize * 3 + sizeof("ltobject_export_string:")];
        FILE * pFile = NULL;
        char * pCommand = NULL;
        int numEntries = 0;
        size_t sizeData = 0;
        char * pChar = NULL;
        LTLibrary_LTObjectMapEntry * pCurrEntry = NULL;
        const char * pExeDir = LTCoreBSP_GetExecDirectory();

        if (!pExeDir) goto bailure;
        if (asprintf(&pCommand, COMMAND_COUNT, pExeDir) < 0 || !pCommand) goto bailure;

        //fprintf(stderr,"count command is %s\n", pCommand);

        if (NULL == (pFile = popen(pCommand, "r"))) goto bailure;
        if (dataString != fgets(dataString, sizeof(dataString), pFile)) goto bailure;
        pChar = dataString;
        errno = 0;
        numEntries = (int)strtoul(pChar, &pChar, 10);
        if (errno != 0 || numEntries == 0) goto bailure;
        errno = 0;
        sizeData = (size_t)strtoul(pChar, NULL, 10);
        if (errno != 0 || sizeData == 0) goto bailure;
        pclose(pFile); pFile = NULL;
        free(pCommand); pCommand = NULL;

        if (NULL == (s_pLTLibraryObjectMap = (LTLibrary_LTObjectMapEntry *)malloc(numEntries * sizeof(LTLibrary_LTObjectMapEntry)))) goto bailure;
        if (NULL == (s_pLTLibraryObjectMapData = (char *)malloc(sizeData + 1))) goto bailure;
        s_pLTLibraryObjectMapData[0] = 0;

        if (asprintf(&pCommand, COMMAND_DATA, pExeDir) < 0 || !pCommand) goto bailure;

        //fprintf(stderr,"data command is %s\n", pCommand);

        if (NULL == (pFile = popen(pCommand, "r"))) goto bailure;
        int bytesRead = 0, totalBytesRead = 0;
        while (! feof(pFile)) {
            if (fgets(dataString, sizeof(dataString), pFile) != NULL) {
                bytesRead = strlen(dataString);
                if (bytesRead < 1) goto bailure;
                strcat(s_pLTLibraryObjectMapData+totalBytesRead, dataString);
                totalBytesRead += bytesRead;
            }
            else if (ferror(pFile)) break;
        }
        if (totalBytesRead != (int)sizeData) goto bailure;

        pclose(pFile); pFile = NULL;
        free(pCommand); pCommand = NULL;

        pChar = s_pLTLibraryObjectMapData;
        pCurrEntry = s_pLTLibraryObjectMap;

        //fprintf(stderr, "read map is:\n%s\n", pChar);

        while (numEntries) {
            if (! ReadLTLibraryObjectMapEntry(pCurrEntry, &pChar, &sizeData, numEntries)) goto bailure;
            pCurrEntry++;
            numEntries--;
        }

        // DumpLTLibraryObjectMap();

        goto success;
    bailure:
        FreeLTLibraryObjectMap();
    success:
        if (pFile) pclose(pFile);
        if (pCommand) free(pCommand);
    }

    static void FreeLTLibraryObjectMap(void) {
        if (s_pLTLibraryObjectMap) {
            free (s_pLTLibraryObjectMap);
            s_pLTLibraryObjectMap = NULL;
        }
        if (s_pLTLibraryObjectMapData) {
            free(s_pLTLibraryObjectMapData);
            s_pLTLibraryObjectMapData = NULL;
        }
    }

    static LTLibrary_LTObjectMapEntry * LTHostAPI_GetLTLibraryObjectMap(void) {
        return s_pLTLibraryObjectMap;
    }

#endif /* #ifndef LT_NO_DYNAMIC_LOADER */

/*********************
 * PseudoKeyInputISR *
 *********************/
static void
SignalHandler(int nSignal) {
    signal(nSignal, SIG_IGN);                           /* prevent recursive signal */
    tcsetattr(STDIN_FILENO, TCSANOW, &s_termiosNormal); /* restore normal terminal attributes */
    signal(nSignal, SIG_DFL);                           /* install default signal handler*/
    raise(nSignal);                                     /* return to regularly scheduled programming */
}

static void *
LTCoreBSP_KeyInputPseudoISRThreadProc(void * pClientData) {
    SetThreadName(pClientData);

    for (;;) {
        struct pollfd fds[2] = {
            {.fd = STDIN_FILENO,      .events = POLLIN | POLLHUP,  .revents = 0},
            {.fd = s_keyInputPipe[0], .events = POLLHUP, .revents = 0}
        };

        if (poll(fds, 2, 1000) == 0) {
            continue;
        }

        if (((fds[0].revents | fds[1].revents) & ~(POLLHUP | POLLERR)) == 0) {
            // Only exit if the only flags set require an exit.  Otherwise continue processing
            // input until exit is the only option.
            break;
        }

        if (fds[0].revents & POLLIN) {
            char c, ch;
            int result = read(STDIN_FILENO, &ch, sizeof(c));
            if (result <= 0) {
                // 0 means EoF, which should only happen if stdin is being piped in from a
                // finite source and there's no need to wait any longer.  Any other error value
                // there's probably also no way to successfully recover.
                break;
            }
            if (result > 0) {
                EnterISRContext();
                c = (ch == 127) ? 8 : (ch & 0xFF);  // hack to get backspace key to work for now
                s_pCoreCallbacks->ProcessISRConsoleInputChars(&c, 1);
                s_pCoreCallbacks->ProcessISRConsoleInputChars(NULL, 0);
                ExitISRContext();
            }
        }
    }

    return 0;
}

static void *
LTCoreBSP_DisableBarrierThreadProc(void * pClientData) { LT_UNUSED(pClientData);
    SetThreadName(pClientData);

    pthread_mutex_lock(&s_disableMutex);
    while ((pthread_t)0 != s_disableBarrierThread) {
        while ((0 == LTAtomic_Load(&s_disableCount)) && ((pthread_t)0 != s_disableBarrierThread)) pthread_cond_wait(&s_disableCondition, &s_disableMutex);
        pthread_mutex_unlock(&s_disableMutex);
        while (LTAtomic_Load(&s_disableCount)) { }
        pthread_mutex_lock(&s_disableMutex);
    }
    pthread_mutex_unlock(&s_disableMutex);
    return 0;
}

static bool CreateInternalBSPThread(pthread_t * pThreadToSet, void * (*ThreadProc)(void *), const char * pThreadName, int nThreadPrio) {
    // no BSP_LTLOG here, called during bsp init
    int nRetVal = 0;

    pthread_attr_t attrs;
    struct sched_param schedParam;
    schedParam.sched_priority = nThreadPrio;

    if (    0 != pthread_attr_init(&attrs)) goto bailure;

    if (   (0 != pthread_attr_setstacksize(&attrs, LTCOREBSP_MIN_PTHREAD_STACK_SIZE))
        || (0 != pthread_attr_setinheritsched(&attrs, PTHREAD_EXPLICIT_SCHED))
        || (0 != pthread_attr_setschedpolicy(&attrs, LTCOREBSP_REAL_TIME_SCHED_POLICY))
        || (0 != pthread_attr_setschedparam(&attrs, &schedParam)))
    {
        pthread_attr_destroy(&attrs);
        goto bailure;
    }

    nRetVal = pthread_create(pThreadToSet, &attrs, ThreadProc, (void *)pThreadName);
    pthread_attr_destroy(&attrs);

    if (0 == nRetVal) return true;

    *pThreadToSet = (pthread_t)0;

bailure:
    LTCoreBSP_ConsolePutString("[linux.bsp.bspthread] \"");
    LTCoreBSP_ConsolePutString(pThreadName);
    LTCoreBSP_ConsolePutString("\" creation failed: ");
    LTCoreBSP_ConsolePutString(ThreadCreateErrnoToString(nRetVal));
    LTCoreBSP_ConsolePutString("\n");
    return false;
}

static void InitInterruptDisableEmulation(void) {
    // no BSP_LTLOG here, called during bsp init

    pthread_condattr_t condAttrs;
    bool bMutexInitialized = false;
    bool bCondAttrsInited = false;
    bool bSuccess = false;
    do
    {   if (false == (bMutexInitialized = LTCoreBSP_InitMutexCommon(&s_disableMutex, PTHREAD_MUTEX_ERRORCHECK_NP, false, true))) break;
        if (false == (bCondAttrsInited  = (0 == pthread_condattr_init(&condAttrs)))) break;
        if (0 != pthread_condattr_setclock(&condAttrs, CLOCK_MONOTONIC)) break;
        if (0 != pthread_cond_init(&s_disableCondition, &condAttrs)) break;
        bSuccess = true;
    } while (false);
    if (bCondAttrsInited) pthread_condattr_destroy(&condAttrs);
    if (! bSuccess) {
        if (bMutexInitialized) pthread_mutex_destroy(&s_disableMutex);
        LTCoreBSP_ConsolePutString("[linux.bsp.emulatedisable] failed to create barrier thread pthread_mutex_t and pthread_cond_t\n");
        return;
    }
    /* create the barrier thread */
    if (! CreateInternalBSPThread(&s_disableBarrierThread, LTCoreBSP_DisableBarrierThreadProc, "ISR_DisableBarrier", LTCOREBSP_DISABLE_BARRIER_PTHREAD_PRIORITY)) {
        pthread_cond_destroy(&s_disableCondition);
        pthread_mutex_destroy(&s_disableMutex);
    }
}

static void ExitInterruptDisableEmulation(void) {
    // no BSP_LTLOG here
    /* stop the thread and destroy resources */
    if ((pthread_t)0 != s_disableBarrierThread) {
        pthread_mutex_lock(&s_disableMutex);
        pthread_t temp = s_disableBarrierThread;
        LTAtomic_Store(&s_disableCount, 0);
        s_disableBarrierThread = (pthread_t)0;
        pthread_cond_signal(&s_disableCondition);
        pthread_mutex_unlock(&s_disableMutex);
        pthread_join(temp, NULL);
        pthread_cond_destroy(&s_disableCondition);
        pthread_mutex_destroy(&s_disableMutex);
    }
}


static void
InitPseudoKeyInputISR(void) {
    // no BSP_LTLOG here, called during bsp init
    /* save existing terminal attributes and modify a copy for 'raw' input */
    tcgetattr(STDIN_FILENO, &s_termiosNormal);
    lt_memcpy(&s_termiosRaw, &s_termiosNormal, sizeof(struct termios));
    s_termiosRaw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    s_termiosRaw.c_lflag &= ~(ECHO | ICANON);

    /* create a pipe we can use to exit the input thread */
    int err = pipe(s_keyInputPipe);
    if (err != 0) {
        LTCoreBSP_DebugBreak();
    }

    /* set signal handlers so that abnormal termination will result in restored terminal attributes */
    signal(SIGINT, SignalHandler);
    signal(SIGSEGV, SignalHandler);
    signal(SIGABRT, SignalHandler);
    signal(SIGBUS, SignalHandler);

    /* set the terminal to have the raw attributes */
    tcsetattr(STDIN_FILENO, TCSANOW, &s_termiosRaw);

    /* create the getchar thread */
    CreateInternalBSPThread(&s_threadKeyInput, LTCoreBSP_KeyInputPseudoISRThreadProc, "KeyRcvPseudoISR", LTCOREBSP_ISR_PTHREAD_PRIORITY);
 }

 static void
 ExitPseudoKeyInputISR(void) {
    // no BSP_LTLOG here
    /* stop the thread and restore terminal attributes */
    if (s_threadKeyInput) {
        /* close the pipe to cause the thread to break out of its loop */
        close(s_keyInputPipe[1]);
        s_keyInputPipe[1] = 0;
        pthread_join(s_threadKeyInput, NULL);
        close(s_keyInputPipe[0]);
        s_keyInputPipe[0] = 0;
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

    myprintf("[ltcorebsp.linux.%s]%s: %s prio% 3d, %llu/%llu stack bytes free, %llu used\n", pLabel, name, pThreadType, nPriority, nStackFree, nStackSize, nStackUsed);

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

/*____________________________
  LTHosted interface struct */
static const LTHostAPI s_host = {
    .malloc                   = LTHostAPI_malloc,
    .realloc                  = LTHostAPI_realloc,
    .free                     = LTHostAPI_free,

    .GetTotalSystemRAM        = LTCoreBSP_GetTotalSystemRAM,
    .GetAvailableSystemRAM    = LTCoreBSP_GetAvailableSystemRAM,
    .GetSystemRAMLowWatermark = LTCoreBSP_GetSystemRAMLowWatermark,

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

    .GetThreadCpuUtilization  = LTHostAPI_GetThreadCpuUtilization,

#ifndef LT_NO_DYNAMIC_LOADER
    .LibraryLoad              = LTHostAPI_LibraryLoad,
    .LibraryUnload            = LTHostAPI_LibraryUnload,
    .LibraryLookupSymbol      = LTHostAPI_LibraryLookupSymbol,
    .LibraryEnumerate         = LTHostAPI_LibraryEnumerate,
    .GetLTLibraryObjectMap    = LTHostAPI_GetLTLibraryObjectMap
#endif
};

/*_____________________________
  LTCoreBSP interface struct */
static const LTCoreBSP s_bsp = {
    .GetHighFrequencyCounterNanoseconds = LTCoreBSP_GetHighFrequencyCounterNanoseconds,
    .GetHighFrequencyCounterNanosecondResolution = LTCoreBSP_GetHighFrequencyCounterNanosecondResolution,
    .PutCharsToConsole = LTCoreBSP_PutCharsToConsole,
    .DebugAssertFailed = LTCoreBSP_DebugAssertFailed,
    .hostAPI = &s_host
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
 *  22-Feb-21   augustus    added ThreadGetStackUsage; made all stack sizes use u32
 *  09-Mar-21   augustus    enabled LT_ASSERT to trap to debugger in debug mode
 *  20-Mar-21   augustus    make InsideInterruptContext() return true while dispatching from KeyInputPseudoISRThreadProc()
 *  15-Jun-21   tiberius    remove handle concept from BSPs, pass in void * instead
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 *  07-Feb-23   augustus    always invoke debugger even in release mode now
 *  23-Feb-24   augustus    LT-1595: removed redundant malloc size tracking (8 byte per alloc overhead)
 *  03-Jun-24   augustus    LT-1920: approximate current & max stack usage by updating in malloc and free, and before blocking
 */
