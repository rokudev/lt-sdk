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
#include <stdio.h>          // for getchar
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
#include <sys/sysinfo.h>
#include <malloc.h>

#include <lt/core/LTStdlib.h>

#include <lt/core/bsp/LTHostAPI.h>
#include <lt/core/bsp/LTCoreBSP.h>

/*___________
  #defines */
#define LTCOREBSP_MIN_PTHREAD_STACK_SIZE                (PTHREAD_STACK_MIN)   /* man page says 16K minimum stack size, empirically verified, 20K required to use printf */
#define LTCOREBSP_STACK_PAGE_SIZE                       (512)       /* man page says stack size must be page size multiple but doesn't say page size; epirically determined to be 512 */
#define LTCOREBSP_ROUND_UP_TO_PAGE_MULTIPLE(nStackSize) ((nStackSize + (LTCOREBSP_STACK_PAGE_SIZE-1)) & ~(LTCOREBSP_STACK_PAGE_SIZE-1))
#define PRINT_STACKTISTICS                              0
#define LTCOREBSP_START_KERNELTIME_AT_ZERO              1
#define LTCOREBSP_MEM_SEAL_ALLOCATED                    0x600dBeef
#define LTCOREBSP_MEM_SEAL_FREED                        0xBadDeed5
#define LTCOREBSP_SYS_RAM_SIZE                          (256 * 1024) /* 64K is the LT minimum RAM requirement so that's what we simulate */
#define LTCOREBSP_ISR_PRIORITY                          (31)
#define LTCOREBSP_MAX_PRIORITY                          (LTCOREBSP_ISR_PRIORITY)

DEFINE_BSP_LTLOG_SECTION("linux_cloud.bsp");

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

/*_______________________
  forward declarations */
static const LTCoreBSP s_bsp;
static LT_SIZE LTHostAPI_GetAvailableSystemRAM_Internal(const LTMallInfo *minfo);
static bool LTHostAPI_ThreadInitializeAndRun(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pThreadProc)(void * pClientData), void * pClientData);
static bool LTHostAPI_ThreadSetPriority(void * pThread, u8 nPriority);
static void InitPseudoKeyInputISR(void);
static void ExitPseudoKeyInputISR(void);
struct LinuxThreadInstanceData;
static bool LTCoreBSP_ReleaseCPU(struct LinuxThreadInstanceData *pThread, bool abortIfHighestPriority);
static void LTCoreBSP_TakeCPU(struct LinuxThreadInstanceData *pThread);

/*___________________
  static variables */
static const LTCoreBSP_LTCoreCallbacks *    s_pCoreCallbacks = NULL;
static LTAtomic                             s_LTCoreBSPInitialized = { 0 };
static pthread_mutex_t  s_mutex;
static clockid_t        s_clockID = CLOCK_MONOTONIC_RAW;
static s64              s_nHighFrequencyCounterResolution = 0;
static s64              s_nHighFrequencyCounterInitial = 0;
static pthread_key_t    s_keyThreadLocal = (pthread_key_t)0;
static int              s_nRealtimePriorityMin = 0;
static pthread_mutex_t  s_disableMutex;
static pthread_t        s_disableThread = (pthread_t)0;
static int              s_disableCount = 0;
static LTAtomic         s_insideInterruptContext = { 0 };

static int                              s_numPriorityReady[LTCOREBSP_MAX_PRIORITY+1];
static struct LinuxThreadInstanceData*  s_pCurrentThread = NULL;
static int                              s_nMaxReadyPriority = -1;
static pthread_cond_t                   s_condition;

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

static const char kEAGAINMessage[] = "system thread resource limit reached\n";
static const char kEINVALMessage[] = "invalid attributes specified\n";

#define debug_printf(args...)

/*______________________________
  unbuffered console putchars */
static void LT_ISR_SAFE LTCoreBSP_PutCharsToConsole(const char * pChars, u32 nChars) {
    ssize_t n = write(STDOUT_FILENO, pChars, nChars); LT_UNUSED(n);
}
/*_____________________________________________________________________________________________________________
  unbuffered console putstring - use only during bsp initialize; otherwise use BSP_LTLOG and BSP_LTLOG_DEBUG */
static void LT_ISR_SAFE LTCoreBSP_ConsolePutString(const char * pString) {
    LTCoreBSP_PutCharsToConsole(pString, strlen(pString));
}

/*____________________________________________________________________________
  ConsoleStomp function - only call this to print during BSP initialization */
static void ConsoleStomp(const char * pString) {
    /* The BSP should not be using the UART.  In the future LTCore will
       provide the BSP with ConsolePrint and ConsoleLog functions so it can
       call back into LTCore to print coherently.   For now we will just
       ConsoleStomp sparingly.  Don't add calls to printf or vsnprintf or anything
       like that in this file, please. */
    const char * p = pString; while (*p) p++; ssize_t n = (ssize_t)(p-pString);
    n = write(STDOUT_FILENO, pString, n);
}

static void LTCoreBSP_Yield(void) {
    struct LinuxThreadInstanceData* pThread =  pthread_getspecific(s_keyThreadLocal);
    if (!pThread) return;
    if (!LTCoreBSP_ReleaseCPU(pThread, true)) return;
    LTCoreBSP_TakeCPU(pThread);
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
    if (0 != pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset)) ConsoleStomp("[cloud.bsp.init] pthread_setaqffinity_np failed\\n");

    // initialize bsp log
    BSP_LTLOG_INITIALIZE(pCallbacks->LTCoreLogFunction);

    // remember pCallbacks
    s_pCoreCallbacks = pCallbacks;

    /* init s_mutex */
    pthread_mutexattr_t mutexAttrs = {{ PTHREAD_MUTEX_FAST_NP }};
    pthread_mutex_init(&s_mutex, &mutexAttrs);

    pthread_condattr_t condAttrs = {};
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

    s_nRealtimePriorityMin = 0;
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
    return LTAtomic_Load(&s_insideInterruptContext);
}

LT_SIZE LT_ISR_SAFE
LTCoreBSP_DisableInterrupts(void) {
    pthread_mutex_lock(&s_disableMutex);
#ifdef LT_DEBUG
    if (((pthread_t)0 == s_disableThread && s_disableCount) ||
        (s_disableThread != (pthread_t)0 &&  s_disableThread != pthread_self()) ||
        (s_disableThread != (pthread_t)0 && (0 == s_disableCount))) {
        ConsoleStomp("[ltcorebsp] *** ERROR Inconsistent Disable State\n");
        pthread_mutex_unlock(&s_disableMutex);
        return 0;
    }
#endif
    if ((pthread_t)0 == s_disableThread) {
        s_disableThread = pthread_self();
    }
    /* return from this function with the mutex locked; we unlock it in Enable() */
    return ++s_disableCount;
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

    if ((int)nMask != s_disableCount) {
        ConsoleStomp("[ltcorebsp] *** LTCoreBSP_EnableInterrupts: inconsistent disable order\n");
    }

    /* I have the mutex locked at least once from doing the Disable() call */
    if (0 == --s_disableCount) {
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
    default:                            /* Error getting the lock.  Shouldn't ever happen:          */
                    ConsoleStomp("[ltcorebsp] *** LTCoreBSP_InterruptsAreDisabled: failed to determine interrupt status\n");
                    return false;
    }
    bool disabled = (pthread_t)0 != s_disableThread && 0 != s_disableCount;
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
    LT_SIZE nNewPotentialLow = LTHostAPI_GetAvailableSystemRAM_Internal(&minfo);
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
    return pMem;
}

static void LTHostAPI_free(void * pMem) {
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
LTHostAPI_GetTotalSystemRAM(void) {
    struct sysinfo sinfo;
    int ret = sysinfo(&sinfo);
    if (ret != 0) {
        LTCoreBSP_ConsolePutString("[linux.bsp.meminfo] sysinfo failed\n");
        return 0;
    }
#if LT_ARCHITECTURE_BITS == 64
    return (LT_SIZE)((LT_SIZE)sinfo.totalhigh << 32) | sinfo.totalram;
#else
    return (LT_SIZE)sinfo.totalram;
#endif
}

static LT_SIZE
LTHostAPI_GetAvailableSystemRAM_Internal(const LTMallInfo *minfo) {
    struct sysinfo sinfo;
    int ret = sysinfo(&sinfo);
    if (ret != 0) {
        LTCoreBSP_ConsolePutString("[linux.bsp.meminfo] sysinfo failed\n");
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
        LTCoreBSP_ConsolePutString("[linux.bsp.meminfo] /proc/meminfo read failed\n");
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
LTHostAPI_GetAvailableSystemRAM(void) {
    LTMallInfo minfo = LTGetMallInfo();
    return LTHostAPI_GetAvailableSystemRAM_Internal(&minfo);
}

static LT_SIZE
LTHostAPI_GetSystemRAMLowWatermark(void) {
    return (LT_SIZE)LTAtomic_Load(&s_nFreeRAMLowWatermark);
}

static void LTCoreBSP_EnterHostOS(void) {
    struct LinuxThreadInstanceData* pThread =  pthread_getspecific(s_keyThreadLocal);
    LTCoreBSP_ReleaseCPU(pThread, false);
}

static void LTCoreBSP_LeaveHostOS(void) {
    struct LinuxThreadInstanceData* pThread =  pthread_getspecific(s_keyThreadLocal);
    LTCoreBSP_TakeCPU(pThread);
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

/*___________________________________________________________________________________________________________
  mutex - this will move into core once we have our own kernel - tbd how we run that on top of a hosted os */
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
            ConsoleStomp("cloudbsp.mutex.init] failed to init mutex\n");
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

/*___________________________________________________________________________________________________________
  thread - this will move into core once we have our own kernel - tbd how we run that on top of a hosted os */
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

    debug_printf("[ltcorebsp.os.linux.thread] want CPU with priority %d (current %d)\n", pThread->m_nPriority, s_pCurrentThread ? s_pCurrentThread->m_nPriority : -1);

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
    debug_printf("[ltcorebsp.os.linux.thread] got CPU with priority %d\n", pThread->m_nPriority);
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

    debug_printf("[ltcorebsp.os.linux.thread] release CPU with priority %d\n", pThread->m_nPriority);
    s_pCurrentThread = NULL;
    pthread_cond_broadcast(&s_condition);
    pthread_mutex_unlock(&s_mutex);
    return true;
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
        ConsoleStomp("[ltcorebsp.os.linux.thread.");
        ConsoleStomp(pName);
        ConsoleStomp("] creation failed: ");
        switch (nErr) {
        case EAGAIN: pName = kEAGAINMessage; break;
        case EINVAL: pName = kEINVALMessage; break;
        default:     pName = "";             break;
        }
        ConsoleStomp(pName);
        ConsoleStomp("\n");
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
        delay.tv_nsec = LTCoreBSP_MillisecondsToNanoseconds(100); /* 0 is supposed to mean Yield() but Linux doesn't Yield on 0 so hack in 10ms */
    }
    LTCoreBSP_EnterHostOS();
    while (0 != nanosleep(&delay, &remaining)) delay = remaining;
    LTCoreBSP_LeaveHostOS();
}

static bool
LTHostAPI_ThreadSetPriority(void * pThread, u8 nPriority) {
    struct LinuxThreadInstanceData * pInstanceData = (struct LinuxThreadInstanceData *)pThread;
    debug_printf("[ltcorebsp.os.linux.thread] CHANGE THREAD PRIORITY %d -> %d\n", pInstanceData->m_nPriority, nPriority);
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

    // Cannot wait for uninitialized or finalized thread. LTCore shouldn't call us with NULL or 0 but definitely
    // is so let's guard against the crash until we figure out why.  @@@@ BOGUS!
    if ((NULL == pInstanceData) || (0 == pInstanceData->m_thread)) return true;

    if (LTCoreBSP_NanosecondsIsInfinite(nTimeoutNanoseconds)) {
        LTCoreBSP_EnterHostOS();

        pthread_join(pInstanceData->m_thread, NULL);
        LTCoreBSP_LeaveHostOS();
        return true;
    }
    else {
        struct timespec timeSpec;
        if (0 != clock_gettime(CLOCK_REALTIME, &timeSpec)) return false;
        nTimeoutNanoseconds += LTCoreBSP_SecondsToNanoseconds(timeSpec.tv_sec);
        nTimeoutNanoseconds += timeSpec.tv_nsec;
        timeSpec.tv_sec = (time_t)LTCoreBSP_NanosecondsToSeconds(nTimeoutNanoseconds);
        nTimeoutNanoseconds -= LTCoreBSP_SecondsToNanoseconds(timeSpec.tv_sec);
        timeSpec.tv_nsec = (long)nTimeoutNanoseconds;
        LTCoreBSP_EnterHostOS();
        //bool ret = (0 == pthread_clockjoin_np(pInstanceData->m_thread, NULL, CLOCK_MONOTONIC, &timeSpec)) ? true : false;
        bool ret = (0 == pthread_timedjoin_np(pInstanceData->m_thread, NULL, &timeSpec)) ? true : false;
        LTCoreBSP_LeaveHostOS();
        return ret;
    }
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
    LT_UNUSED(pThread);
    return 0;
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
        if (NULL == (s_pLTLibraryObjectMapData = (char *)malloc(sizeData))) goto bailure;

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
        LTAtomic_Store(&s_insideInterruptContext, 1);
        c = (ch == 127) ? 8 : (ch & 0xFF); // hack to get backspace key to work for now
        s_pCoreCallbacks->ProcessISRConsoleInputChars(&c, 1);
        s_pCoreCallbacks->ProcessISRConsoleInputChars(NULL, 0);
        LTAtomic_Store(&s_insideInterruptContext, 0);
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
        case EAGAIN: ConsoleStomp(kEAGAINMessage); break;
        case EINVAL: ConsoleStomp(kEINVALMessage); break;
        default:     ConsoleStomp("\n");           break;
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

/*____________________________
  LTHosted interface struct */
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
    .GetHighFrequencyCounterNanoseconds          = LTCoreBSP_GetHighFrequencyCounterNanoseconds,
    .GetHighFrequencyCounterNanosecondResolution = LTCoreBSP_GetHighFrequencyCounterNanosecondResolution,
    .PutCharsToConsole                           = LTCoreBSP_PutCharsToConsole,
    .DebugAssertFailed                           = LTCoreBSP_DebugAssertFailed,
    .hostAPI                                     = &s_host
};

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  12-Sep-22   galerius    created from LTCoreBSP_Linux.c
 *  12-Sep-22   galerius    implement user-mode priority scheduling
 */
