/******************************************************************************
 * platforms/apple/source/apple/ltcorebsp/AppleLTCoreBSP.m
 *                               - common LTCoreBSP for Apple Operating Systems
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2021-2022, Roku, Inc.  All rights reserved.
 ******************************************************************************/


#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <dlfcn.h>          // for dlopen, dlsym, dlclose
#include <stdio.h>          // for fprintf(stderr,
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <strings.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <strings.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <libgen.h>
#include <sys/types.h>
#include <dirent.h>

#include <mach/mach_time.h>
#include <mach/thread_act.h>
#include <mach/mach_error.h>
#include <mach-o/dyld.h>

#include <lt/core/bsp/LTCoreBSP.h>
#include <lt/core/LTCore.h>
#undef va_list
#include <apple/Apple_LTLibraryPaths.h>


/*_______________________
  #defines */
#define LTCOREBSP_CONSOLE_INPUT_THREAD_PRIORITY             (32)
#define LTCOREBSP_REAL_TIME_SCHED_POLICY                    SCHED_RR
#define LTCOREBSP_NON_REAL_TIME_SCHED_POLICY                SCHED_OTHER
#define NOTYET_LTCOREBSP_OSTHREAD_MIN_PTHREAD_STACK_SIZE   (PTHREAD_STACK_MIN)
// MacOS defines PTHREAD_STACK_MIN, which is set to 8KB, but crashes in dlsym unless >=40K
#define LTCOREBSP_OSTHREAD_MIN_PTHREAD_STACK_SIZE           (40 * 1024) /* Determined by trial and error */
#define LTCOREBSP_OSTHREAD_STACK_PAGE_SIZE                  (4096)      /* From getpagesize() */
#define LTCOREBSP_ROUND_UP_TO_PAGE_MULTIPLE(nStackSize)     ((nStackSize + (LTCOREBSP_OSTHREAD_STACK_PAGE_SIZE-1)) & ~(LTCOREBSP_OSTHREAD_STACK_PAGE_SIZE-1))
#define LTCOREBSP_START_KERNELTIME_AT_ZERO                  1
#define LTCOREBSP_SET_MACH_REALTIME_THREAD_POLICY           0

#define LTCOREBSP_LOG_TAG_PREAMBLE                          "[ltcorebsp." LT_APPLE_PLATFORM_NAME "."
#define LTCOREBSP_LOG_TAG_POSTAMBLE                         "] "
#define LTCOREBSP_LOG(tag, msg)                             ConsoleStomp(LTCOREBSP_LOG_TAG_PREAMBLE tag LTCOREBSP_LOG_TAG_POSTAMBLE msg)

#define LTCOREBSP_LOG_PRIO  0

#ifdef LT_APPLE_PLATFORM_MACOS
  #if (! defined(LTCOREBSP_TOOLS_BUILD))
    #define LTCOREBSP_HORK_TERMINAL
  #endif
#endif

/*_______________________
  forward declarations */
static const LTCoreBSP s_bsp;
static bool LTCoreBSP_ThreadInitializeAndRun(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pThreadProc)(void * pClientData), void * pClientData);

/*___________________
  static variables */
static const LTCoreBSP_LTCoreCallbacks *    s_pCoreCallbacks = NULL;
static LTAtomic                             s_nLTCoreBSPInitialized = {0};
static pthread_mutex_t                      s_mutex;
static pthread_mutex_t                      s_disableMutex;
static pthread_key_t                        s_keyThreadLocal = (pthread_key_t)0;
static int                                  s_nRealtimePriorityMin = 0;
static int                                  s_nRealtimePriorityMax = 0;
static clockid_t                            s_clockID = CLOCK_MONOTONIC_RAW;
static s64                                  s_nHighFrequencyCounterResolution = 0;
static s64                                  s_nHighFrequencyCounterInitial = 0;

#ifdef LTCOREBSP_HORK_TERMINAL
static pthread_t                            s_threadKeyInput = (pthread_t)0;
static struct termios                       s_termiosNormal;
static struct termios                       s_termiosRaw;
#endif

#if LTCOREBSP_SET_MACH_REALTIME_THREAD_POLICY
static double                               s_clock2abs = 0.;
static thread_time_constraint_policy_data_t s_RealtimeThreadPolicy;
#endif

static const char kEAGAINMessage[] = "system thread resource limit reached\n";
static const char kEINVALMessage[] = "invalid attributes specified\n";
static const char kEPERMMessage2[] = "inadequate privileges\n ** add -->  \"username - rtprio unlimited\"  to /etc/security/limits.conf  and logout/in";

static void LT_ISR_SAFE
LTCoreBSP_PutCharsToConsole(const char * pChars, u32 numChars) {
#ifdef LT_APPLE_PLATFORM_IOS
    for (; numChars; --numChars, ++pChars) putchar(*pChars);
#else
    ssize_t n = write(STDOUT_FILENO, pChars, numChars); LT_UNUSED(n);  /* write to bypass buffering since we are raw */
#endif
}

/*____________________________________________________________________________
  ConsoleStomp function - only call this to print during BSP initialization */
static void ConsoleStomp(const char * pString) {
    const char * pEnd = pString;
    for (; *pEnd; ++pEnd);
    u32 numChars = pEnd - pString;
    LTCoreBSP_PutCharsToConsole(pString, numChars);
}

#ifdef LTCOREBSP_HORK_TERMINAL

/*______________________________________________________
  Signal handler to restore termio settings on ctrl-c */
static void
SIGINT_SignalHandler(int nSignal) {
    signal(nSignal, SIG_IGN);                           /* prevent recursive signal */
    tcsetattr(STDIN_FILENO, TCSANOW, &s_termiosNormal); /* restore normal terminal attributes */
    signal(nSignal, SIG_DFL);                           /* install default signal handler*/
    raise(nSignal);                                     /* return to regularly scheduled programming */
}

static void LT_ISR_SAFE LTCoreBSP_EnableConsoleReceiveInterrupt(bool bEnable) {
    if (bEnable) pthread_mutex_unlock(&s_mutex);
    else         pthread_mutex_lock(&s_mutex);
}

/*_____________________________
  New exported functions from this file - these functions are dlsym()'d by the lthost example program to simulate
  io between LTThread and non-LTThread */
LT_LIBRARY_EXPORT_DECL void LTCoreBSP_PutCharacterToConsole(char ch) {
    LTCoreBSP_EnableConsoleReceiveInterrupt(false);
    s_pCoreCallbacks->ProcessISRConsoleInputChars(&ch, 1);
    LTCoreBSP_EnableConsoleReceiveInterrupt(true);
    s_pCoreCallbacks->ProcessISRConsoleInputChars(NULL, 0);
}

/*_______________________________________________________
  Character Input Dispatch thread (simulates UART ISR) */
static void *
LTCoreBSP_ConsoleKeyInputThreadProc(void * pClientData) { LT_UNUSED(pClientData);

    pthread_setname_np("ConsoleKeyInput");

    int ch;
    while (0 != (ch = getchar()) && EOF != ch) {
        if (ch) {
            //hack to get backspace key to work for now
            if (ch == 0177) ch = 8;
            LTCoreBSP_PutCharacterToConsole((char)ch);
        }
    }
    return 0;
}

static const char kEPERMMessage1[] = "inadequate privileges\n";

static void
StartConsoleKeyInputThread(void) {
    /* save existing terminal attributes and modify a copy for 'raw' input */

    memset(&s_termiosNormal, 0, sizeof(struct termios));
    tcgetattr(STDIN_FILENO, &s_termiosNormal);
    memcpy(&s_termiosRaw, &s_termiosNormal, sizeof(struct termios));
    s_termiosRaw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    s_termiosRaw.c_lflag &= ~(ECHO | ICANON);

    /* set a SIGINT handler so that ^C will result in restored terminal attributes */
    signal(SIGINT, SIGINT_SignalHandler);

    /* set the terminal to have the raw attributes */
    tcsetattr(STDIN_FILENO, TCSANOW, &s_termiosRaw);

    /* create the getchar thread */
    pthread_attr_t attrs;
    struct sched_param schedParam;
    //schedParam.sched_priority = s_nRealtimePriorityMin + LTCOREBSP_CONSOLE_INPUT_THREAD_PRIORITY;
    schedParam.sched_priority = 0;
    if ((0 != pthread_attr_init(&attrs))                                                        ||
    /*    (0 != pthread_attr_setstacksize(&attrs, LTCOREBSP_OSTHREAD_MIN_PTHREAD_STACK_SIZE))     || */
        (0 != pthread_attr_setinheritsched(&attrs, PTHREAD_EXPLICIT_SCHED))                     ||
    /*    (0 != pthread_attr_setschedpolicy(&attrs, LTCOREBSP_REAL_TIME_SCHED_POLICY))   || */
        (0 != pthread_attr_setschedpolicy(&attrs, LTCOREBSP_NON_REAL_TIME_SCHED_POLICY))   ||
        (0 != pthread_attr_setschedparam(&attrs, &schedParam))) {
        LTCOREBSP_LOG("ckiattr", "console input thread attr init failed\n");
        return;
    }

    int nRetVal = pthread_create(&s_threadKeyInput, &attrs, &LTCoreBSP_ConsoleKeyInputThreadProc, NULL);
    pthread_attr_destroy(&attrs);

    if (0 != nRetVal) {
        s_threadKeyInput = 0;
        LTCOREBSP_LOG("ckithread", "console input thread creation failed: ");
        switch (nRetVal) {
        case EAGAIN: ConsoleStomp(kEAGAINMessage); break;
        case EINVAL: ConsoleStomp(kEINVALMessage); break;
        case EPERM:  ConsoleStomp(kEPERMMessage1); break;
        default:     ConsoleStomp("\n");           break;
        }
    }
 }

static void
StopConsoleKeyInputThread(void) {
    /* stop the thread and restore terminal attributes */
    if (s_threadKeyInput) {
        /* send 0 to stdin to cause the thread to break out of its loop */
        int ch = 0;
        int ret = ioctl(STDIN_FILENO, TIOCSTI, &ch);
        if (ret == -1) {
            fprintf(stderr, "Could not send TIOCSTI ioctl: %d\n", errno);
            abort();
        }
        pthread_join(s_threadKeyInput, NULL);
        s_threadKeyInput = 0;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &s_termiosNormal);
}

#endif /* #ifdef LTCOREBSP_HORK_TERMINAL */

/*_____________________
  BSP initialization */
const LTCoreBSP *
LTCoreBSP_Initialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks) {

    if (LTAtomic_Load(&s_nLTCoreBSPInitialized)) return NULL; /* don't let anyone come in here except LTCore the first time */
    LTAtomic_Store(&s_nLTCoreBSPInitialized, 1); /* don't need a test and set, LTCore calls this before any threads are running */

    // remember pCallbacks
    s_pCoreCallbacks = pCallbacks;

    /* init s_mutex  and s_disableMutex */
    pthread_mutexattr_t mutexAttrs; pthread_mutexattr_init(&mutexAttrs);
    pthread_mutexattr_settype(&mutexAttrs, PTHREAD_MUTEX_DEFAULT);   pthread_mutex_init(&s_mutex, &mutexAttrs);
    pthread_mutexattr_destroy(&mutexAttrs); pthread_mutexattr_init(&mutexAttrs);
    pthread_mutexattr_settype(&mutexAttrs, PTHREAD_MUTEX_RECURSIVE); pthread_mutex_init(&s_disableMutex, &mutexAttrs);
    pthread_mutexattr_destroy(&mutexAttrs);

    /* init LTCoreBSP_GetHighFrequencyCounterNanoseconds() */
    struct timespec ts1, ts2;
    s_clockID = CLOCK_MONOTONIC_RAW;
clockAgain:
    if ((0 == clock_getres(s_clockID, &ts1)) && (0 == clock_gettime(s_clockID, &ts2))) {
        s_nHighFrequencyCounterResolution = LTCoreBSP_SecondsToNanoseconds(ts1.tv_sec) + (s64)ts1.tv_nsec;
        s_nHighFrequencyCounterInitial = LTCoreBSP_SecondsToNanoseconds(ts2.tv_sec) + (s64)ts2.tv_nsec;
    }
    else if (s_clockID == CLOCK_MONOTONIC_RAW) { s_clockID = CLOCK_MONOTONIC; goto clockAgain; }

    /* init thread support */
    pthread_key_create(&s_keyThreadLocal, NULL);
    s_nRealtimePriorityMin = sched_get_priority_min(LTCOREBSP_REAL_TIME_SCHED_POLICY);
    s_nRealtimePriorityMax = sched_get_priority_max(LTCOREBSP_REAL_TIME_SCHED_POLICY);
    if (0 > s_nRealtimePriorityMin) s_nRealtimePriorityMin = 1;
#if LTCOREBSP_LOG_PRIO
    LTCOREBSP_LOG("prio.min__", "Min pthread prio is: "); printf("%d\n", s_nRealtimePriorityMin); fflush(stdout);
    LTCOREBSP_LOG("prio.max__", "Max pthread prio is: "); printf("%d\n", s_nRealtimePriorityMax); fflush(stdout);

    LTCOREBSP_LOG("prio.other", "SCHED_OTHER pthread prios are: "); printf("%d - %d\n", sched_get_priority_min(SCHED_OTHER), sched_get_priority_max(SCHED_OTHER)); fflush(stdout);
    LTCOREBSP_LOG("prio.fifo_", "SCHED_FIFO  pthread prios are: "); printf("%d - %d\n", sched_get_priority_min(SCHED_FIFO),  sched_get_priority_max(SCHED_FIFO)); fflush(stdout);
    LTCOREBSP_LOG("prio.rr___", "SCHED_RR    pthread prios are: "); printf("%d - %d\n", sched_get_priority_min(SCHED_RR),    sched_get_priority_max(SCHED_RR)); fflush(stdout);
#endif

#if LTCOREBSP_SET_MACH_REALTIME_THREAD_POLICY
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    const uint64_t NANOS_PER_MSEC = 1000000ULL;
    s_clock2abs = ((double)tb.denom / (double)tb.numer) * NANOS_PER_MSEC;

    s_RealtimeThreadPolicy.period = 0;
    s_RealtimeThreadPolicy.computation = (uint32_t)(1 * s_clock2abs); // 1 ms of work min
    s_RealtimeThreadPolicy.constraint  = (uint32_t)(2 * s_clock2abs); // 2 ms of work max
    s_RealtimeThreadPolicy.preemptible = TRUE;
#endif

#ifdef LTCOREBSP_HORK_TERMINAL
    StartConsoleKeyInputThread();
#endif

    return &s_bsp;
}

void
LTCoreBSP_Finalize(const LTCoreBSP * pBSP) {
    if ((0 == LTAtomic_Load(&s_nLTCoreBSPInitialized)) || (pBSP != &s_bsp)) return; /* don't let anyone except LTCore in here */

#ifdef LTCOREBSP_HORK_TERMINAL
    StopConsoleKeyInputThread();
#endif

#if LTCOREBSP_SET_MACH_REALTIME_THREAD_POLICY
    s_clock2abs = 0;
#endif

    s_nRealtimePriorityMin = 0;
    pthread_key_delete(s_keyThreadLocal);
    s_keyThreadLocal = (pthread_key_t)0;
    s_nHighFrequencyCounterInitial = 0;
    s_nHighFrequencyCounterResolution = 0;
    s_clockID = CLOCK_MONOTONIC_RAW;
    pthread_mutex_destroy(&s_mutex);
    pthread_mutex_destroy(&s_disableMutex);

    s_pCoreCallbacks = NULL;
    LTAtomic_Store(&s_nLTCoreBSPInitialized, 0);
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
    return 0;
}

void LT_ISR_SAFE
LTCoreBSP_EnableInterrupts(LT_SIZE nMask) {
    LT_UNUSED(nMask);
    pthread_mutex_unlock(&s_disableMutex);
}

bool LT_ISR_SAFE
LTCoreBSP_InterruptsAreDisabled(void) {
    switch (pthread_mutex_trylock(&s_disableMutex)) {
    case 0:         break;              /* Acquired the lock.  Interrupts enabled.  Release the lock and return. */
    case EBUSY:     return true;        /* Lock already required.  Interrupts disabled.                          */
    default:                            /* Error getting the lock.  Shouldn't ever happen:                       */
                    ConsoleStomp("[ltcorebsp] *** LTCoreBSP_InterruptsAreDisabled: failed to determine interrupt status\n");
                    return false;
    }
    pthread_mutex_unlock(&s_disableMutex);
    return false;
}

void
LTCoreBSP_DebugBreak(void) {
#ifdef LT_DEBUG
    /* break into debugger */
    #if defined (__x86_64__)  /* ok to test for architecture in the BSP implementation, nowhere else */
        __asm__("int3");
    #endif
#endif
}

/* _______________________________________________
 * static functions placed into struct LTCoreBSP */
/*_________________________
  high frequency counter */
static s64 LT_ISR_SAFE
LTCoreBSP_GetHighFrequencyCounterNanoseconds(void) {
#if LTCOREBSP_START_KERNELTIME_AT_ZERO
    return clock_gettime_nsec_np(s_clockID) - s_nHighFrequencyCounterInitial;
#else
    return clock_gettime_nsec_np(s_clockID);
#endif
}

static s64 LT_ISR_SAFE
LTCoreBSP_GetHighFrequencyCounterNanosecondResolution(void) {
    return s_nHighFrequencyCounterResolution;
}

/*__________
Logging */
void LT_ISR_SAFE
LTCoreBSP_LogV(const char * pSection, const char * pTag, u32 nLogFlags, const char * pFormatString, lt_va_list args) {
    pthread_mutex_lock(&s_mutex);
    if (kLTCore_LogFlags_LogTypeRaw == (nLogFlags & kLTCore_LogFlags_LogTypeMask)) {
        /* it's a console print */
        vfprintf(stdout, pFormatString, args);
    }
    else {
        char buff[512]; buff[0] = 0; buff[sizeof(buff)-1] = 0;
        vsnprintf(buff, sizeof(buff)-1, pFormatString, args);
        /* it's a log, put a '\n' on the end if it doesn't have one */
        int nLen = strlen(buff);
        if (nLen && buff[nLen-1] != '\r' && buff[nLen-1] != '\n') {
            buff[nLen] = '\n'; buff[nLen+1] = 0;
        }
        printf("[%s.%s] %s", pSection, pTag, buff);
    }
    pthread_mutex_unlock(&s_mutex);
    fflush(stdout);
}

/*____________
  debugging */
static bool LT_ISR_SAFE
LTCoreBSP_DebugAssertFailed(const char * pFile, int nLine, const char * pTest) {
    LT_UNUSED(pFile); LT_UNUSED(nLine);  LT_UNUSED(pTest);
    #ifdef LT_DEBUG
        /* return true to trap to debugger on assert - may be used to implement abort/continue prompt */
        return true;
    #endif
    return false; /* we shall not invoke debugger in release mode */
}

/* malloc, realloc, and free */
static void *
LTCoreBSP_malloc(LT_SIZE nBytes) {
    return malloc(nBytes);
}

static void *
LTCoreBSP_realloc(void * pMem, LT_SIZE nBytes) {
    return realloc(pMem, nBytes);
}

static void
LTCoreBSP_free(void * pMem) {
    free(pMem);
}

/*________________
  ram functions */
static LT_SIZE
LTCoreBSP_GetTotalSystemRAM(void) {
    return (LT_SIZE)0; // TODO;
}

static LT_SIZE
LTCoreBSP_GetAvailableSystemRAM(void) {
    return (LT_SIZE)0; // TODO;
}

static LT_SIZE
LTCoreBSP_GetSystemRAMLowWatermark(void) {
    return (LT_SIZE)0; // TODO;
}

/*_______________
  hosted mutex */
static LT_SIZE
LTCoreBSP_MutexInstanceSize(void) {
    return sizeof(pthread_mutex_t);
}

static void
LTCoreBSP_MutexInitialize(void * pMutex) {
    pthread_mutexattr_t mutexAttrs;
    pthread_mutexattr_init(&mutexAttrs);
    pthread_mutexattr_settype(&mutexAttrs, PTHREAD_MUTEX_RECURSIVE);
    if (0 != pthread_mutex_init((pthread_mutex_t *)pMutex, &mutexAttrs)) {
        /* LTCore_RedAlert(LTCORE_BSP_OS_LOGSECTION_LINUX, "mutex.initfailure"); */
    }
    pthread_mutexattr_destroy(&mutexAttrs);
}

static void
LTCoreBSP_MutexFinalize(void * pMutex) {
    pthread_mutex_destroy((pthread_mutex_t *)pMutex);
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

/*______________________
  monitor - hosted os */
struct MonitorData {
    pthread_mutex_t m_mutex;
    pthread_cond_t  m_condition;
    u32             m_nFlags;
};
#define MONITOR_FLAG_VALID              (1 << 0)
LT_INLINE bool MonitorIsValid(struct MonitorData * pData)        { return (pData->m_nFlags & MONITOR_FLAG_VALID); }

static LT_SIZE
LTCoreBSP_MonitorInstanceSize(void) {
    return sizeof(struct MonitorData);
}

static void
LTCoreBSP_MonitorInitialize(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    pthread_condattr_t condAttrs;        pthread_mutexattr_t mutexAttrs;
    pthread_mutexattr_init(&mutexAttrs); pthread_mutexattr_settype(&mutexAttrs, PTHREAD_MUTEX_ERRORCHECK);
    bool bMutexInitialized = (0 == pthread_mutex_init(&pData->m_mutex, &mutexAttrs));
    bool bCondAttrsInited = bMutexInitialized && (0 == pthread_condattr_init(&condAttrs));
    if (MONITOR_FLAG_VALID != (pData->m_nFlags = ((bCondAttrsInited && (0 == pthread_cond_init(&pData->m_condition, &condAttrs))) ? MONITOR_FLAG_VALID : 0))) {
        if (bCondAttrsInited) pthread_condattr_destroy(&condAttrs);
        if (bMutexInitialized) pthread_mutex_destroy(&pData->m_mutex);
    }
    pthread_mutexattr_destroy(&mutexAttrs);
}

static void
LTCoreBSP_MonitorFinalize(void * pMonitor) {
    struct MonitorData * pData = (struct MonitorData *)pMonitor;
    if (MonitorIsValid(pData)) {
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
            timeSpec.tv_sec = (time_t)LTCoreBSP_NanosecondsToSeconds(nTimeoutNanoseconds);
            timeSpec.tv_nsec = (long)(nTimeoutNanoseconds - LTCoreBSP_SecondsToNanoseconds(timeSpec.tv_sec));
            switch (pthread_cond_timedwait_relative_np(&pData->m_condition, &pData->m_mutex, &timeSpec)) {
                case 0:         return true;
                case ETIMEDOUT: return false;
                case EPERM:     /* s_logger.YellowAlert("timedwait.noenter"); */  return false;
                default:        /* s_logger.YellowAlert("timedwait.unknown.pthread.returncode"); */ return false;
            }
        }
    }
    /* s_logger.YellowAlert("wait.invalidmonitor"); */
    return false;
}

/*_____________________
  thread - hosted os */
struct ThreadInstanceData {
    char      m_name[16];
    pthread_t m_thread;
    void (*   m_pThreadProc)(void * pClientData);
    void  *   m_pClientData;
    bool      m_bCreated;
};

static bool LTCoreBSP_SetMachRealtimeThreadPolicy(pthread_t pThreadID, u8 nPriority) {
    LT_UNUSED(nPriority);
    #if LTCOREBSP_SET_MACH_REALTIME_THREAD_POLICY
        // Put thread into Real-Time scheduling class as suggested by TN2169.
        // https://developer.apple.com/library/archive/technotes/tn2169/_index.html
        if (nPriority) {
            struct thread_standard_policy standardPolicy = { .no_data = 0 };
            return (MACH_MSG_SUCCESS == thread_policy_set(
                        pthread_mach_thread_np(pThreadID), THREAD_STANDARD_POLICY,
                        (thread_policy_t)&standardPolicy, THREAD_STANDARD_POLICY_COUNT)
                   ) ? true : false;
        }
        else {
            return (MACH_MSG_SUCCESS == thread_policy_set(
                        pthread_mach_thread_np(pThreadID), THREAD_TIME_CONSTRAINT_POLICY,
                        (thread_policy_t)&s_RealtimeThreadPolicy, THREAD_TIME_CONSTRAINT_POLICY_COUNT)
                   ) ? true : false;
        }
    #else
        LT_UNUSED(pThreadID);
        return true;
    #endif
}

static int LTCoreBSP_LTPriorityToPThreadPriority(u8 nLTPriority) {
    return nLTPriority ? s_nRealtimePriorityMin + (nLTPriority - 1) : 0;
}

static void * LTCoreBSP_OSThread_ThreadProc(void * pClientData) {
    pthread_mutex_lock(&s_mutex);
        /* synchronize thread execution with creation by blocking on this mutex while we finish configuring the pthread
           in thread creation.  Done like this because pthreads can't be created suspended like win32 threads */
    pthread_mutex_unlock(&s_mutex);
    struct ThreadInstanceData * pCD = (struct ThreadInstanceData *)pClientData;
    if (pCD->m_name[0]) pthread_setname_np(pCD->m_name);
    pthread_setspecific(s_keyThreadLocal, pClientData);
    pCD->m_pThreadProc(pCD->m_pClientData);
    pthread_setspecific(s_keyThreadLocal, NULL);
    return 0;
}

static LT_SIZE
LTCoreBSP_ThreadInstanceSize(void) {
    return sizeof(struct ThreadInstanceData);
}

static void
LTCoreBSP_ThreadInitializeAndStartScheduler(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pInitialThreadProc)(void * pClientData), void * pClientData) {
    /* This function should do three things:
    1. perform any instance initialization of pThread.
    2. start the scheduler, running the initial thread as specified by the function parameters.
    3. block until the thread exits, then UNINITIALIZE THE INSTANCE DATA,  STOP THE SCHEDULER and return */
    struct ThreadInstanceData * pInstanceData = (struct ThreadInstanceData *)pThread;

    /* no need to start the scheduler, just run the thread */
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
    /* called to spawn threads after the scheduler has been started.  This function should do three things:
        1. perform any instance initialization of pThread.
        2. run the initial thread as specified by the function parms.
        3. return from this function immediately after spawning the thread. */

    struct ThreadInstanceData * pInstanceData = (struct ThreadInstanceData *)pThread;
    bool bAttrsInitialized = false;
    pthread_attr_t attrs;
    struct sched_param schedParam;
    int nPolicy = 0;
    bool bYield = false;

    memset(pInstanceData, 0, sizeof(*pInstanceData));
    pInstanceData->m_pThreadProc  = pThreadProc;
    pInstanceData->m_pClientData  = pClientData;
    if (pName) {
        while (*pName && (nPolicy < (int)(sizeof(pInstanceData->m_name)-1))) pInstanceData->m_name[nPolicy++] = *pName++;
    }

    /* if we're creating a thread of the same priority we will yield after releasing the mutex below, check for it */
    nPolicy = 0;
    if (0 == pthread_getschedparam(pthread_self(), &nPolicy, &schedParam)) {
        if (nPolicy == LTCOREBSP_NON_REAL_TIME_SCHED_POLICY) bYield = (nPriority == 0);
        else bYield = (schedParam.sched_priority == LTCoreBSP_LTPriorityToPThreadPriority(nPriority));
    }

    /* setup the new thread's priority, policy and stack size */
    schedParam.sched_priority = LTCoreBSP_LTPriorityToPThreadPriority(nPriority);
    nPolicy = nPriority ? LTCOREBSP_REAL_TIME_SCHED_POLICY : LTCOREBSP_NON_REAL_TIME_SCHED_POLICY;
    nStackSize = (nStackSize < LTCOREBSP_OSTHREAD_MIN_PTHREAD_STACK_SIZE) ? LTCOREBSP_OSTHREAD_MIN_PTHREAD_STACK_SIZE : LTCOREBSP_ROUND_UP_TO_PAGE_MULTIPLE(nStackSize);

    do
    {
        if (0 != pthread_attr_init(&attrs)) break;
        bAttrsInitialized = true;
        //if (0 != pthread_attr_setstacksize(&attrs, (size_t)nStackSize)) break;
        if (0 != pthread_attr_setinheritsched(&attrs, PTHREAD_EXPLICIT_SCHED)) break;
        if (0 != pthread_attr_setschedpolicy(&attrs, nPolicy)) break;
        if (0 != pthread_attr_setschedparam(&attrs, &schedParam)) break;

        pthread_mutex_lock(&s_mutex); /* to make the spawned thread block until we can set its name and mach thread policy */
        if (0 != (nPolicy = pthread_create(&pInstanceData->m_thread, &attrs, &LTCoreBSP_OSThread_ThreadProc, pInstanceData))) {
            pInstanceData->m_thread = 0;
            pthread_mutex_unlock(&s_mutex);
            break;
        }
        if (nPriority) LTCoreBSP_SetMachRealtimeThreadPolicy(pInstanceData->m_thread, nPriority);
        pInstanceData->m_bCreated = true;
        pthread_mutex_unlock(&s_mutex);
        if (bYield) pthread_yield_np();
    }
    while (false);

    if (bAttrsInitialized) pthread_attr_destroy(&attrs);

    if (! pInstanceData->m_bCreated) {
        pInstanceData->m_thread = 0;
        pInstanceData->m_pThreadProc = NULL;
        pInstanceData->m_pClientData = NULL;
        ConsoleStomp("[ltcorebsp.os.macos.thread.");
        ConsoleStomp(pName);
        ConsoleStomp("] creation failed: ");
        switch (nPolicy) {
        case EAGAIN: pName = kEAGAINMessage; break;
        case EINVAL: pName = kEINVALMessage; break;
        case EPERM:  pName = kEPERMMessage2; break;
        default:     pName = "";             break;
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

static void *
LTCoreBSP_ThreadGetCurrentThreadInstanceData(void) {
    return pthread_getspecific(s_keyThreadLocal);
}

static void
LTCoreBSP_ThreadSleep(s64 nNanoseconds) {
    if (0 == nNanoseconds) pthread_yield_np();
    else {
        struct timespec delay;
        delay.tv_sec = (time_t)LTCoreBSP_NanosecondsToSeconds(nNanoseconds);
        delay.tv_nsec = (long)(nNanoseconds - LTCoreBSP_SecondsToNanoseconds(delay.tv_sec));
        struct timespec remaining;
        while (0 != nanosleep(&delay, &remaining)) delay = remaining;
    }
}

static bool
LTCoreBSP_ThreadSetPriority(void * pThread, u8 nPriority) {
    struct ThreadInstanceData * pInstanceData = (struct ThreadInstanceData *)pThread;
    if (pInstanceData->m_bCreated) {
        int nPriorityPThread = LTCoreBSP_LTPriorityToPThreadPriority(nPriority);
        int nPolicy = 0;
        struct sched_param param;
        bool bYield = false;

        // don't set anything, just return true if the thread already has the parameters we want
        if (0 == pthread_getschedparam(pInstanceData->m_thread, &nPolicy, &param)) {
            if (nPolicy == LTCOREBSP_NON_REAL_TIME_SCHED_POLICY) { if (nPriority == 0) return true; }
            else if ((nPolicy == LTCOREBSP_REAL_TIME_SCHED_POLICY) && (param.sched_priority == nPriorityPThread)) return true;
        }

        // set the new policy
        nPolicy = nPriority ? LTCOREBSP_REAL_TIME_SCHED_POLICY : LTCOREBSP_NON_REAL_TIME_SCHED_POLICY;

        // DRW 09-Mar-2021 : before setting the new priority - see if we are setting priority on a different thread whose priority was less than or
        // equal to our priority and whose new priority is greater than our priority; in this case we must yield after setting the other thread's priority
        if (nPolicy == LTCOREBSP_REAL_TIME_SCHED_POLICY) { /* only matters if we're setting a real time priority */
            struct ThreadInstanceData * pCurrentThreadInstanceData = (struct ThreadInstanceData *)LTCoreBSP_ThreadGetCurrentThreadInstanceData();
            if (pCurrentThreadInstanceData && pCurrentThreadInstanceData != pInstanceData) {
                // we are setting a different thread's priority, check to see if it is higher than ours
                int nMyPolicy; struct sched_param myParam;
                if (0 == pthread_getschedparam(pCurrentThreadInstanceData->m_thread, &nMyPolicy, &myParam)) {
                    if ((nMyPolicy == LTCOREBSP_NON_REAL_TIME_SCHED_POLICY) || ((param.sched_priority <= myParam.sched_priority) && (nPriority > myParam.sched_priority))) {
                        // setting a different thread that was equal to or lower than ourself to be higher than ourself, must yield
                        bYield = true;
                    }
                }
            }
        }

        param.sched_priority = nPriorityPThread;
        if (0 == pthread_setschedparam(pInstanceData->m_thread, nPolicy, &param)) {
            LTCoreBSP_SetMachRealtimeThreadPolicy(pInstanceData->m_thread, nPriority);
            if (bYield) pthread_yield_np();
            return true;
        }
    }

    return false;
}

struct TimedWaitUntilFinishedClientData {
    int joined;
    pthread_t pthreadID;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

static void * LTCoreBSP_TimedWaitUntilFinishedThreadProc(void * pClientData) {
    struct TimedWaitUntilFinishedClientData * pCD = (struct TimedWaitUntilFinishedClientData *)pClientData;
    pthread_join(pCD->pthreadID, NULL);
    pthread_mutex_lock(&pCD->mutex);
    pCD->joined = 1;
    pthread_cond_signal(&pCD->cond);
    pthread_mutex_unlock(&pCD->mutex);
    return NULL;
}

static bool LTCoreBSP_TimedWaitUntilFinished(pthread_t threadID, s64 nTimeoutNanoseconds) {
    pthread_t waiter;
    struct TimedWaitUntilFinishedClientData clientData;

    clientData.joined = 0;
    clientData.pthreadID = threadID;
    pthread_mutex_init(&clientData.mutex, NULL);
    pthread_cond_init(&clientData.cond, NULL);

    pthread_mutex_lock(&clientData.mutex);
    if (0 != pthread_create(&waiter, 0, &LTCoreBSP_TimedWaitUntilFinishedThreadProc, &clientData)) goto bailure;

    s64 nTimeStart;
    struct timespec timeSpec;

    while (nTimeoutNanoseconds > 0) {
        nTimeStart = LTCoreBSP_GetHighFrequencyCounterNanoseconds();
        timeSpec.tv_sec = (time_t)LTCoreBSP_NanosecondsToSeconds(nTimeoutNanoseconds);
        timeSpec.tv_nsec = (long)(nTimeoutNanoseconds - LTCoreBSP_SecondsToNanoseconds(timeSpec.tv_sec));
        int nRetVal = pthread_cond_timedwait_relative_np(&clientData.cond, &clientData.mutex, &timeSpec);
        if (clientData.joined || (ETIMEDOUT == nRetVal) || (0 != nRetVal)) break;
        nTimeoutNanoseconds -= (LTCoreBSP_GetHighFrequencyCounterNanoseconds() - nTimeStart);
    }

    pthread_cancel(waiter);
    pthread_join(waiter, NULL);

bailure:
    pthread_mutex_unlock(&clientData.mutex);
    pthread_mutex_destroy(&clientData.mutex);
    pthread_cond_destroy(&clientData.cond);

    return clientData.joined ? true : false;
}

static bool
LTCoreBSP_ThreadWaitUntilFinished(void * pThread, s64 nTimeoutNanoseconds) {
    /* This function should block for [at least] nTimeoutNanoseconds and return true at the point when the thread
       completely finishes all execution, or false if the thread has not finished execution within the timeout period.
       By returning true, this function indicates it is now legal to subsequently call LTCoreBSP_ThreadUninitializeInstance()
       on the thread object.  */
    return LTCoreBSP_NanosecondsIsInfinite(nTimeoutNanoseconds) ?
        (pthread_join(((struct ThreadInstanceData *)pThread)->m_thread, NULL), true)     :
        LTCoreBSP_TimedWaitUntilFinished(((struct ThreadInstanceData *)pThread)->m_thread, nTimeoutNanoseconds);
}

static void
LTCoreBSP_ThreadFinalize(void * pThread) {
    /* called to uninitialize the private instance data.  This function call will always be preceded by a call to
       LTCoreBSP_ThreadWaitUntilThreadFinished() that returned true; the thread is guaranteed no longer running */
    struct ThreadInstanceData * pInstanceData = (struct ThreadInstanceData *)pThread;
    pInstanceData->m_thread = 0;
    pInstanceData->m_pThreadProc = NULL;
    pInstanceData->m_pClientData = NULL;
    pInstanceData->m_bCreated = false;
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

        /* LTLibraryManager guarantees pLibName is non null and between 1 and kMaxLibNameLen characters long.
           It also guarantees the thread safety this function by only calling it from one thread at a time. */
        NSString * nsLTLibraryPath = Apple_GetLTLibraryFilePath(pLTLibraryName);
        const char * pLTLibraryPath = [nsLTLibraryPath UTF8String];
        if (NULL != (*pLibraryHandleToSet = (void *)dlopen(pLTLibraryPath, RTLD_NOW | RTLD_LOCAL))) return LTCOREBSP_LOADLIBRARY_SUCCESS;
        else {
            const char * pError = dlerror();
            s_pCoreCallbacks->ReportLibraryLoaderFunctionFailure("dlopen", pLTLibraryPath, pError ? pError :"failed");
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
            s_pCoreCallbacks->ReportLibraryLoaderFunctionFailure("dlsym", pSymbolName, pError ? pError : "failed");
        }
        return pAddress;
    }

    static bool
    LTCoreBSP_LibraryEnumerate(LTCoreBSP_LibraryEnumProc * pEnumProc, void * pClientData) {

        bool bRetVal = true;

        struct dirent ** ppNameList;
        int n = scandir([Apple_GetLTLibraryDirectory() UTF8String], &ppNameList, NULL, NULL);
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

#endif /* #ifndef LT_NO_DYNAMIC_LOADER */

/*_____________________________
  LTCoreBSP interface struct */
static const LTCoreBSP s_bsp = {

    .GetHighFrequencyCounterNanoseconds = LTCoreBSP_GetHighFrequencyCounterNanoseconds,
    .GetHighFrequencyCounterNanosecondResolution = LTCoreBSP_GetHighFrequencyCounterNanosecondResolution,

    .PutCharsToConsole  = LTCoreBSP_PutCharsToConsole,

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

#ifdef NO_DYNAMIC_LOADER

/* this is the liblthost code when we're not using the dynamic loader - when statically linked.
   so far I haven't figured out how to automatically located the Open and Close library functions.
   from static libs on MacOS/iOS...  i plan to make lthost the real mode, replacing ltrun.
   For now, I'll leave this here...
*/

#define ATOMIC_SLEEP_MS                 (5)
#define ATOMIC_SLEEP_NS                 (ATOMIC_SLEEP_MS * 1000000)

static void AtomicSleep(void) {
    struct timespec remaining, delay = { 0, (long)ATOMIC_SLEEP_NS };
    while (0 != nanosleep(&delay, &remaining)) delay = remaining;
}

static LTCore * s_pCore          = NULL;
static LTAtomic s_atomicRunOnce  = 0;
static const char * s_argv[2]    = { "lthost", "iOS" };
extern LTCore * LTCoreExported_LTGetCore(void);

#include <os/log.h>

static void PerformStaticConstruction(void) {
    extern void LTLibrary_LTDeviceFlash_RegisterLibrary(void);
    extern void LTLibrary_LTShellNetDiag_RegisterLibrary(void);
    extern void LTLibrary_LTSystemSettings_RegisterLibrary(void);
    extern void LTLibrary_LTSystemShell_RegisterLibrary(void);
    extern void LTLibrary_LTUtilityMacAddress_RegisterLibrary(void);
    extern void LTLibrary_MacOSDriverFlash_RegisterLibrary(void);

    LTLibrary_LTDeviceFlash_RegisterLibrary();
    LTLibrary_LTShellNetDiag_RegisterLibrary();
    LTLibrary_LTSystemSettings_RegisterLibrary();
    LTLibrary_LTSystemShell_RegisterLibrary();
    LTLibrary_LTUtilityMacAddress_RegisterLibrary();
    LTLibrary_MacOSDriverFlash_RegisterLibrary();
}

/*****************************************************************************************
 * LT_GetCore() implementation - in hosted mode LT_GetCore() lazily instantiates LTCore */
LTCore * LT_GetCore(void) {
    while (! LTAtomic_TestAndSet(&s_atomicRunOnce, 0, 1)) AtomicSleep();
    if (NULL == s_pCore) {
        PerformStaticConstruction();
        int nResult = LT_Run(sizeof(s_argv)/sizeof(s_argv[0]), s_argv);
        os_log_debug(OS_LOG_DEFAULT, "Result of LT_Run is %d\n", nResult);
        s_pCore = LTCoreExported_LTGetCore();
        os_log_debug(OS_LOG_DEFAULT, "s_pCore is 0x%lx\n", (long)s_pCore);
    }
    s_atomicRunOnce = 0;
    return s_pCore;
}

/*****************************************************************************************
 * LT_GetStdlib() implementation - in hosted mode LT_GetStdlib() lazily instantiates LTCore */
LTStdlib * LT_GetStdlib(void) {
    return LT_GetCore()->GetLTStdlib();
}

#endif /* #ifdef NO_DYNAMIC_LOADER */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  17-Jun-20   caligula    created from LTCoreBSP_Linux.c
 *  25-Jun-20   caligula    updated with threads impl from latest LTCoreBSP_Linux.c
 *  17-Aug-20   augustus    bye bye LTObject, hello LTHandle
 *  26-Nov-20   augustus    got rid of getchar
 *  05-Dec-20   augustus    added memory info functions
 *  27-Jan-21   augustus    added LTCoreBSP_EnumerateInstalledLibraries
 *  09-Feb-21   augustus    refactored for simplified BSP; created as copy source
 *  22-Feb-21   augustus    added ThreadGetStackUsage; made all stack sizes use u32
 *  15-Jun-21   tiberius    remove handle concept from BSPs, pass in void * instead
 *  31-Oct-21   augustus    fixed WaitUntilFinished, MachThreadPolicy, Sleep, etc., and updated to be current
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 */
