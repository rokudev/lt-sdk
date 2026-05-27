/******************************************************************************
 * lt/source/core/LTCoreImpl.c   -   implementation of LTCore library interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************
 ******************************************************************************
 * CAUTION: Any functions here called by LTLoggerImpl.c MUST NOT log or assert.
 ******************************************************************************
 ******************************************************************************/

#include "LTCoreImpl.h"
#include "LTHandle.h"
#include "LTEventImpl.h"
#include "LTThreadImpl.h"
#include "LTLibraryManager.h"
#include "LTLoggerImpl.h"
#include "LTStdlibImpl.h"
#include "LTResourceTreeImpl.h"
#include "LTConsoleConnector.h"
#include <lt/core/LTArray.h>
#include <lt/core/bsp/LTCoreBSP.h>
#include <lt/core/bsp/LTHostAPI.h>
#include <lt/core/LTCrashdump.h>

/****************************
 * file LTCoreImpl #defines */
#define LTCOREIMPL_LT_PREFIX                    "LT"
#define LTCOREIMPL_LTDRIVER_PREFIX              LTCOREIMPL_LT_PREFIX "Driver"
#define LTCOREIMPL_LT_STRLEN                    (sizeof(LTCOREIMPL_LT_PREFIX) - 1)
#define LTCOREIMPL_LTDRIVER_STRLEN              (sizeof(LTCOREIMPL_LTDRIVER_PREFIX) - 1)

#define LTCOREIMPL_CORE_THREADNAME              "Core"
#define LTCOREIMPL_CORE_INITIAL_PRIORITY        (kLTThread_PrivatePriorityHighest)
#define LTCOREIMPL_CORE_FINAL_PRIORITY          (kLTThread_PriorityLowest + 1)
#define LTCOREIMPL_CORE_STACKSIZE               (2 * LTThreadImpl_GetDefaultStackSize())

#define LTCOREIMPL_GENESIS_THREADNAME           "Genesis"
#define LTCOREIMPL_GENESIS_PRIORITY             (kLTThread_PriorityLowest)
#define LTCOREIMPL_GENESIS_DEFAULT_STACKSIZE    (LTThreadImpl_GetDefaultStackSize())

DEFINE_LTLOG_SECTION("ltcore");

#if     DISABLE_LT_CALLSITE
#define DISABLE_UNUSED_LT_CALLSITE
#else
#define DISABLE_UNUSED_LT_CALLSITE LT_UNUSED(callsite)
#endif


#if LTCORE_LOG_SLEEP_MODE_ACTIVITY
  static char s_sleepModeTimeBuffer[24];
  #define LTLOG_SLEEPMODE             LTLOG_STOMP
#else
  #define LTLOG_SLEEPMODE             LTLOG_LOGNULL
#endif /* #if LTCORE_LOG_SLEEP_MODE_ACTIVITY */

/*****************************************
 * file LTCoreImpl forward declarations */
static void LTCoreImpl_InitializeExportedInterfaces(void);
static bool LTCoreImpl_OpenDependentLibraries(void);
static void LTCoreImpl_CloseDependentLibraries(void);
static void LTCoreImpl_CoreThreadProc(const char * pGenesisLibraryName); /* parameter should be void *, see comment at function definition */
static void LTCoreImpl_InvokeGenesisRunFunctionAndTerminateGenesisThread(void * pClientData);
static void LTCoreImpl_FaultHandler(const char * pABI, u32 * pInterruptStack, LT_SIZE nInterruptStackSize) LT_ISR_SAFE;
static void LTCoreImpl_LTShutdownUninitialize(void);
static bool LTCoreImpl_IsDeveloperBuild(void) LT_ISR_SAFE;
static void LTCoreImpl_TerminateLT(int nExitCode);
static void LTCoreImpl_AddObjectRef(LTObject *object);
static void LTCoreImpl_RemoveObjectRef(LTObject *object);
static void LTCoreImpl_OnLTKEnterIdle(void);
static void LTCoreImpl_OnLTKIdleTick(void);
static void LTCoreImpl_OnLTKExitIdle(void);
static void LTCoreImpl_EngageSleepModeTaskProc(void * pClientData);
static void LTCoreImpl_EngagePreSleepModeTaskProc(void * pClientData);
static void LTCoreImpl_SleepActionEventDispatchCompleteProc(LTEvent hEvent, LTArgs * pEventArgs);

/******************************************
 * file LTCoreImpl static const variables */
static const LTCore                             s_LTCore;
static const LTArgsDescriptor                   s_sleepActionEventArgs = {2, {kLTArgType_u32, kLTArgType_s64} };
static const LTCoreBSP_LTCoreCallbacks          s_bspLTCoreCallbacks = {

        .InsideInterruptContext                 = &LTKInsideInterruptContext,
        .InterruptsAreDisabled                  = &LTKInterruptsAreDisabled,
        .Disable                                = &LTKDisableInterrupts,
        .Enable                                 = &LTKEnableInterrupts,
        .SetInterruptVector                     = &LTKSetInterruptVector,
        .SetInterruptPriority                   = &LTKSetInterruptPriority,

        .ProcessISRConsoleInputChars            = &LTCoreImpl_ProcessISRConsoleInputChars,

        .LTCoreLogFunction                      = &LTLoggerImpl_Log,
        .TerminateLT                            = &LTCoreImpl_TerminateLT,
        .IsDeveloperBuild                       = &LTCoreImpl_IsDeveloperBuild

    #ifndef LT_NO_DYNAMIC_LOADER
        ,
        .ReportLibraryLoaderFunctionFailure     = &LTLibraryManager_ReportLibraryLoaderFunctionFailure
    #endif
};

/************************************
 * file LTCoreImpl static variables */
static LTAtomic             s_nRunCount = { 0 };
static int                  s_nRetVal = -1;
static LTCoreImpl           s_coreImpl;
static LTCore              *s_pCore = NULL;
static const LTCoreBSP     *s_pBSP = NULL;
static ILTThread           *iThread = NULL;
static LTCore_CrashdumpWriteCallback *s_pCrashdumpWriteCallback = NULL;
static LTTraceStream      **IMPL_LTTRACE_STREAMS_PTR(LTCore);
static LTCore_LibraryHookFunction *s_pLibraryHook = NULL;
static LTAssociativeArray  *s_mocks = NULL;
static LTAtomic             s_mockGuard = { 0 };

#if LTCORE_ELIDE_ALL_LTCORE_HEAPTRACKING
    typedef struct LTCore_HeapBlock {
    } LTCore_HeapBlock;
    LT_STATIC_ASSERT_SIZE_32_64(LTCore_HeapBlock, 0, 0)
#else
#if LTCORE_TRACE_HEAP_ALLOCATIONS
    typedef struct LTCore_HeapBlock {
        struct LTCore_HeapBlock * pPrev;       //  4  or  8
        struct LTCore_HeapBlock * pNext;       //  4  or  8
        LT_SIZE                   nBytes;      //  4  or  8
        LTThread                  hThread;     //  4  or  4
        char                      tag;         //  1  or  1
        u8                        pad[3];      //  3  or  3
        LTCallSite                callsite;    // 12  or 24
    } LTCore_HeapBlock;                        // __     __
    LT_STATIC_ASSERT_SIZE_32_64(LTCore_HeapBlock, 32,    56)

    static LTCore_HeapBlock * s_pHeapBlockList = NULL;
    static u32                s_nNumBlocksAllocated = 0;
    static char               s_HeapTag = '\0';
    static LTCore_HeapBlock * s_pNextEnumeratedHeapBlock = NULL;
    static LTAtomic           s_heapBlockEnumerationInProgress = { 0 };
#else
    typedef struct LTCore_HeapBlock {
        u32                       nBytes;      //  4  or  4
        LTThread                  hThread;     //  4  or  4
    } LTCore_HeapBlock;                        //  _      _
    LT_STATIC_ASSERT_SIZE_32_64(LTCore_HeapBlock,  8,     8)
#endif
#endif

/*_____________________________
  static kernel time jumpers */
static s64 ZeroFrequencyFunction(void) { return 0; }
static s64 (*s_getHighFrequencyCounterFunction)(void)    = &ZeroFrequencyFunction;
static s64 (*s_getHighFrequencyResolutionFunction)(void) = &ZeroFrequencyFunction;

/********************************************
 * LTCore library private utility functions */
LTLibrary * LTCoreImpl_GetLibrary(void) {
    return (LTLibrary *)s_pCore;
}

LT_TEXT_RAM_CRITICAL(1)
LTCoreImpl * LTCoreImpl_GetLTCoreImpl(void) {
    return &s_coreImpl;
}

const LTCoreBSP * LTCoreImpl_GetLTCoreBSP(void) {
    return s_pBSP;
}

LTInterfaceType LTCoreImpl_GetObjectInterfaceType(void) {
    return kLTInterfaceType_LTObjectApi;
}

void LTCoreImpl_AddRef(LTObject *object) {
    LTCoreImpl_AddObjectRef(object);
}

void LTCoreImpl_RemoveRef(LTObject *object) {
    LTCoreImpl_RemoveObjectRef(object);
}

/************************************
 * BSP distribution and retraction */
static void LTCoreImpl_DistributeBSP(void) {
    LTLibraryManager_AcceptBSP(s_pBSP);
    LTThreadImpl_AcceptBSP(s_pBSP);
}

static void LTCoreImpl_RecallBSP(void) {
    LTThreadImpl_RelinquishBSP();
    LTLibraryManager_RelinquishBSP();
}

/************************************************************************
 * LTCore Library Functions exported for host OS runtime dynamic loader */
#if !defined(LT_NO_DYNAMIC_LOADER)
    LT_LIBRARY_EXPORT_DECL int LTCoreExported_LTRun(int argc, const char ** argv)   { return LT_Run(argc, argv); }
    LT_LIBRARY_EXPORT_DECL const LTCore * LTCoreExported_LTGetCore(void)            { return s_pCore; }
#endif

/******************************************
 * LTCoreImpl Primitive static functions */
static void LTCoreImpl_PrimitiveZeroMem(u8 * pMem, LT_SIZE nBytes) {
    while (nBytes--) *pMem++ = 0;
}

static void LTCoreImpl_PrimitiveParseDeviceTagName(char * pTagString) {
    /* The device tag name is a simple mechanism for pulling a device identifier
       out of argv[0] and using it as a replacement string for driver libraries.
       so for example if LTDeviceMic opens LTDriverMic and the device tag is Elk
       then the LTCore internal OpenLibrary will remap LTDriverMic to ElkDriverMic.
       The device tag is ultimately intended to be a prefix on a registry key where
       library mappings for devices are encoded.

       Make the take the deviceTagKeyname by using the portion of argv[0] to the right of
       the last forward or backslash character in the string and including only alpha numeric
       characters forward from there, so "C:\bin\ltrun.exe" would yield "ltrun" and "Elk"
       would yield Elk */
    const char * pStart = pTagString;
    while (*pStart) pStart++; /* advance to null terminator */
    do { /* move backwards to the right of the first slash encountered */
        pStart--;
        if (*pStart == '/' || *pStart == '\\') { pStart++; break; }
    } while (pStart != pTagString);
    pTagString = (char *)(s_coreImpl.deviceTagName); /* reuse pTagString to keep stack down */
    while (LTStdlibImpl_isalnum(*pStart)) {
        *pTagString++ = *pStart++;
        if (++s_coreImpl.nDeviceTagNameLen == (kLTCoreImpl_MaxDeviceTagNameBufferSize-1)) break;
    }
    *pTagString = 0;
    if (0 == s_coreImpl.nDeviceTagNameLen) {
        s_coreImpl.nDeviceTagNameLen = 2;
        s_coreImpl.deviceTagName[0] = 'l';
        s_coreImpl.deviceTagName[1] = 't';
        s_coreImpl.deviceTagName[2] =  0;
    }
}

static void LTCoreImpl_SleepActionEventDispatchProc(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    LTCore_SleepAction action = (LTCore_SleepAction)LTArgs_u32At(0, args);
    LTTime newSoftwareSleepWakeupTime = ((LTCore_SleepActionEventProc *)proc)(action, LTTime_Nanoseconds(LTArgs_s64At(1, args)), pClientData);
    if ((action == kLTCore_SleepAction_GoingToSleep) && (! LTTime_IsZero(newSoftwareSleepWakeupTime))) {
        LT_SIZE nMask = LTKDisableInterrupts();
        if ((LTTime_IsZero(s_coreImpl.softwareSleepWakeupTime))
            || LTTime_IsLessThan(newSoftwareSleepWakeupTime, s_coreImpl.softwareSleepWakeupTime))
            s_coreImpl.softwareSleepWakeupTime = newSoftwareSleepWakeupTime;
        LTKEnableInterrupts(nMask);
    }
}

/*_________________________________
  Serial console receive buffers */
enum { kLTCoreSerialConsoleReceiveBufferSizeBytes = 256 };
static                          LTAtomic s_rxBufIndex;  /* Index into interrupt buffer of next received character */
static LT_ALIGNED(sizeof (LT_SIZE)) char s_serialConsoleInterruptBuffer[kLTCoreSerialConsoleReceiveBufferSizeBytes];
static LT_ALIGNED(sizeof (LT_SIZE)) char s_serialConsoleReceiveBuffer[kLTCoreSerialConsoleReceiveBufferSizeBytes];

/*_________________________________________________
  Serial console buffer transfer helper function */
static u32 TransferSerialInterruptBufferChars(void) {
    u32 nTransferIndex = 0;
    u32 nDestIndex = LTAtomic_Load(&s_rxBufIndex);
    while (nDestIndex > nTransferIndex) {
        u32 nBytes = nDestIndex - nTransferIndex;
        LTStdlibImpl_memcpy(s_serialConsoleReceiveBuffer + nTransferIndex, s_serialConsoleInterruptBuffer + nTransferIndex, nBytes);
        nTransferIndex += nBytes;
        if (LTAtomic_CompareAndExchange(&s_rxBufIndex, nDestIndex, 0)) break;
        nDestIndex = LTAtomic_Load(&s_rxBufIndex);
    }
    return nTransferIndex;
}

/*____________________________________
  Tserial-console-receive task proc */
static void LTCoreImpl_ProcessConsoleInputCharsTaskProc(void * pClientData) {
    LTCore_ConsoleCharactersReceivedProc *pCharsReceivedProc = s_coreImpl.pConsoleCharactersReceivedProc;
    if (pCharsReceivedProc && ((u32)((LT_SIZE)pClientData) == LTAtomic_Load(&s_coreImpl.nAtomicConsoleCounter))) {
        u32 nChars; void *pReceivedProcClientData = s_coreImpl.pConsoleCharactersReceivedProcClientData;
        while (0 != (nChars = TransferSerialInterruptBufferChars())) pCharsReceivedProc(s_serialConsoleReceiveBuffer, nChars, pReceivedProcClientData);
    }
}

/*______________________________________________
  ISR-context serial-console-receive function */
void LTCoreImpl_ProcessISRConsoleInputChars(const char * pChars, u32 nChars) LT_ISR_SAFE {
    if (s_coreImpl.hThreadToReceiveConsoleCharacters) { /* only process chars if someone cares */
        if (pChars && nChars) {
            /* there are chars, add them to the buffer, dropping if buffer full */
            u32 nRoom = kLTCoreSerialConsoleReceiveBufferSizeBytes - LTAtomic_Load(&s_rxBufIndex);
            if (nChars > nRoom) nChars = nRoom;
            while (nChars--) s_serialConsoleInterruptBuffer[LTAtomic_FetchAdd(&s_rxBufIndex, 1)] = *pChars++;
        }
        else if (LTAtomic_Load(&s_rxBufIndex)) {
            /* NULL or 0 chars sent from ISR, we're done for this round, if there are chars, queue to handle */
            LTThreadImpl_QueueTaskProcIfRequired(s_coreImpl.hThreadToReceiveConsoleCharacters, LTCoreImpl_ProcessConsoleInputCharsTaskProc, NULL, (void *)((LT_SIZE)LTAtomic_Load(&s_coreImpl.nAtomicConsoleCounter)));
        }
    }
}

/****************************************************
 * LT.h public functions: LT_GetCore() and LT_Run() */
LT_TEXT_RAM_CRITICAL(1)
LTCore * LT_ISR_SAFE LT_GetCore(void) {
    if (LTAtomic_CompareAndExchange(&s_nRunCount, 0, 1)) {
        static const char * _argv[] = { "lthost", "Linux" }; /* DRW : temporary for tools build */
        LT_Run(2, _argv);
    }
    return s_pCore;
}

int LT_Run(int argc, const char ** argv) {
    /* This function's job is to initialize and run LT OS; it should use no stack variables.
       First guard against unlikely re-entry */
    if (LTAtomic_CompareAndExchange(&s_nRunCount, 0, 2)) s_nRetVal = 0;      /* LT_Run() call from client, start as OS */
    else if (LTAtomic_CompareAndExchange(&s_nRunCount, 1, 2)) s_nRetVal = 1; /* LT_Run() triggered from LT_GetCore(), start hosted */
    else return -1;

    /* 1. no facilities yet, do a primitive zeroing of my struct, read device tag and reset s_nRetVal */
    LTCoreImpl_PrimitiveZeroMem((u8 *)&s_coreImpl, (LT_SIZE)sizeof(s_coreImpl));
    if (argc && argv[0] && *argv[0]) {
        if (0 == LTStdlibImpl_strcmp("lthost", argv[0])) {
            s_coreImpl.bHostedMode = true;
            if ((argc > 1) && argv[1] && *argv[1]) LTCoreImpl_PrimitiveParseDeviceTagName((char*)argv[1]);
        }
        else LTCoreImpl_PrimitiveParseDeviceTagName((char*)argv[0]);
    }
    if (s_nRetVal) s_coreImpl.bHostedMode = true; /* force hosted mode if LT_Run() triggered from LT_GetCore() */
    s_nRetVal = -1;

    /* 2. - Init the serial console and BSP (no access to LTCore yet) and read the UTC offset and default local time zone */
    /* Initialize serial-console buffers: */
    LTAtomic_Store(&s_rxBufIndex, 0);
    if (NULL == (s_pBSP = LTCoreBSP_Initialize(&s_bspLTCoreCallbacks))) return -1;
    if (s_pBSP->GetHighFrequencyCounterNanoseconds && s_pBSP->GetHighFrequencyCounterNanosecondResolution) {
        /* wire up kernel time from the bsp for great justice of timestamping copyright message and pre-LTCore LogStomp logging */
        s_getHighFrequencyCounterFunction = s_pBSP->GetHighFrequencyCounterNanoseconds;
        s_getHighFrequencyResolutionFunction = s_pBSP->GetHighFrequencyCounterNanosecondResolution;
    }
    #ifndef LT_NO_DYNAMIC_LOADER
        {
            extern LTLibrary_LTObjectMapEntry * g_pStaticallyBoundLTObjectMapEntries;
            if (s_pBSP->hostAPI && s_pBSP->hostAPI->GetLTLibraryObjectMap) {
                g_pStaticallyBoundLTObjectMapEntries = s_pBSP->hostAPI->GetLTLibraryObjectMap();
            }
        }
    #endif

    LTLoggerImpl_EnablePreLTCoreStompLogging(s_pBSP, s_coreImpl.bHostedMode); /* after this Logging with LogStomp works */
    LTKSetIdleHooks(&LTCoreImpl_OnLTKEnterIdle, &LTCoreImpl_OnLTKIdleTick, &LTCoreImpl_OnLTKExitIdle);
    LTKInitialize(s_pBSP, s_bspLTCoreCallbacks.LTCoreLogFunction, LTCoreImpl_FaultHandler);
    if (! s_coreImpl.bHostedMode) LTLoggerImpl_PrintCopyrightMessage();
    if (s_getHighFrequencyCounterFunction == &ZeroFrequencyFunction) {
        /* bsp didn't provide high frequency time counter functions; use LTK for greater justice */
        s_getHighFrequencyCounterFunction = &LTKGetHighFrequencyCounterNanoseconds;
        s_getHighFrequencyResolutionFunction = &LTKGetHighFrequencyCounterNanosecondResolution;
    }
    LTCoreImpl_DistributeBSP();

    /* 1. Set s_pCore so that LT_GetCore() will work, it won't work fully but LTHandle now has a startup dependency on it.) */
    s_pCore = (LTCore *)&s_LTCore;

    /* Initialize the LTHandle implementation */
    LTHandle_Init();

    // initialize UTC time bases
    s_coreImpl.timebaseUTC            = (LTTimeBase){ .primaryClockTime = LTTime_Zero(), .secondaryClockTime = LTTime_Zero() };
    s_coreImpl.timebaseApproximateUTC = (LTTimeBase){ .primaryClockTime = LTTime_Zero(), .secondaryClockTime = s_pCore->GetBuildTime() };
   // s_coreImpl.timebaseUTC.secondaryClockTime.nNanoseconds = 0;
    //s_coreImpl.timebaseApproximateUTC.secondaryClockTime = s_pCore->GetBuildTime();

    /* 3. stash argc/argv, init our exported interfaces, and cache ILTThread */
    s_coreImpl.argc = argc;
    s_coreImpl.argv = argv;
    LTCoreImpl_InitializeExportedInterfaces(); /* Have to call these manually because LTCore doesn't use LTLIBRARY_DEFINE_EXPORT_BINDING */
    LTCoreImpl_OpenDependentLibraries();
    iThread = LTCoreImpl_GetILTThread();

    /* 4. We are now going to start the scheduler so that the rest of LT initialization happens in
          an LTThread context. We start the scheduler with an initial thread called Core.
          Create the LTThread handle manually without calling LTThreadImpl_Create because we can
          only use BSP facilities at this stage of init. */
    s_coreImpl.hThreadCore = LTThreadImpl_CreateCoreThread(LTCOREIMPL_CORE_THREADNAME, LTCOREIMPL_CORE_STACKSIZE,
                                                           LTCOREIMPL_CORE_INITIAL_PRIORITY);

    /* 5. Start the scheduler using the manually created Core thread handle for the initial thread.
          Do this by callling ThreadInitializeAndStartScheduler() which will only return
          when the Core thread ends and the scheduler is stopped. The Core thread proc implemented below,
          LTCoreImpl_CoreThreadProc, blocks until all other threads in system have finished so if and when the function
          ThreadInitializeAndStartScheduler() returns, all threads have exited and the scheduler has been
          stopped.   */
    void *privateData = LTHandle_ReservePrivateData(s_coreImpl.hThreadCore);
    LTThreadImpl *pImpl = LTThreadImpl_PrivateDataToThreadImpl(privateData);
    LTKThreadInitializeAndStartScheduler(
        privateData,
        LTCOREIMPL_CORE_INITIAL_PRIORITY,
        LTAtomic_Load(&pImpl->nStackSize),
        LTCOREIMPL_CORE_THREADNAME,
        (void (*)(void *))&LTCoreImpl_CoreThreadProc,
        NULL);
    LTHandle_ReleasePrivateData(s_coreImpl.hThreadCore, privateData);

    if (s_coreImpl.bHostedMode) return 0; /* we don't shut down in hosted mode */

    /* If control reaches this point, there are no threads running in the system any longer, the Core thread's BSP instance data
       has been uninitialized and the scheduler has been stopped.  We can no longer run LT operations, only BSP functions again. */
    argc = s_nRetVal;

    LTCoreImpl_CloseDependentLibraries();
    LTCoreImpl_LTShutdownUninitialize(); /* Finalize everything for shutdown */

    return argc;
}

const char * LTCoreImpl_NumToStaticString(u32 num) {
    static char buff[24]; static char * pBuff;
    pBuff = &buff[23]; *pBuff-- = 0; *pBuff = '0';
    if (num) { while (num) { *pBuff-- = (num % 10) + '0'; num /= 10; } pBuff++; }
    return pBuff;
}

static void LTCoreImpl_ReportMaxAllocatedBytesCeiling(void) {
    LTLoggerImpl_ConsoleStompString("ltcore: ");
    LTLoggerImpl_ConsoleStompString(LTCoreImpl_NumToStaticString(LTKGetLTKAllocationHighWatermark()));
    LTLoggerImpl_ConsoleStompString(" bytes heap high-watermark\n");
}

static void LTCoreImpl_ReportAllocLeakage(void) {
    s32 bytesLeaked = (s32)LTKGetLTKCurrentAllocationCount();
    bool  bNegative = (bytesLeaked < 0), bSingular = (bytesLeaked == 1);
    if   (bNegative)   bytesLeaked = 0 - bytesLeaked;
    LTLoggerImpl_ConsoleStompString("ltcore: ");
    LTLoggerImpl_ConsoleStompString(LTCoreImpl_NumToStaticString((u32)bytesLeaked));
    LTLoggerImpl_ConsoleStompString(bSingular ? " byte " : " bytes ");
    LTLoggerImpl_ConsoleStompString(bNegative ? "over-freed!\n" : "leaked\n");
}

static void LTCoreImpl_LTShutdownUninitialize(void) {
    /* We're shutting down, reverse the initialization.

          Free the Core thread handle private data manually since we created the Core thread manually.
          We don't want to call LTHandle_DestroyHandle() because we're not running in an LTThread so LTHandle_DestroyHandle()
          will try to queue the destroy back to the Core thread, which we don't want.
          We do this directly because we don't want to use LTHandle_DestroyHandle() because that would call
          the no longer functioning LT_GetCore() and LTThreadImpl's OnDestroyHandle()
          NOTE: even though we called CreateHandle, we can't call DestroyHandle because it calls the interface's DestroyHandle which
                calls LT_GetCore() and the threads OnDestroyHandle, but (a) LT_GetCore() is shut down now, and (b) LTThreadImpl didn't
                set up the thread handle, we did, so we will not call DestroyHandle, but simply reset the handle record seal and free the handle record */


    /* clear argc/argv */
    s_coreImpl.argc = 0;
    s_coreImpl.argv = NULL;

    /* Clear the UTC offset and device name */
    s_coreImpl.timebaseUTC.primaryClockTime.nNanoseconds = 0;
    s_coreImpl.timebaseUTC.secondaryClockTime.nNanoseconds = 0;
    s_coreImpl.nDeviceTagNameLen = 0;
    s_coreImpl.deviceTagName[0] = 0;

    /* destroy the Core thread handle */
    if (s_coreImpl.hThreadCore) {
        LTHandle_DeleteHandleWithoutDestroy(s_coreImpl.hThreadCore);
        s_coreImpl.hThreadCore = 0;
    }

    if (s_coreImpl.bHostedMode) {
        /* in hosted mode, just uninitialize LTHandle subsystem without reports */
        LTHandle_Fini();
    }
    else {
        /* in non hosted mode, do the reports and uninitialization of LTHandles as follows:
             report max allocated bytes, report leaked handles, uninitialize LTHandle subsystem, and report leaked bytes */
        LTCoreImpl_ReportMaxAllocatedBytesCeiling();
        LTHandle_DumpLeakedHandles();
        LTHandle_Fini();
        LTCoreImpl_ReportAllocLeakage();
    }

    /* Recall and Finalize the BSP */
    LTCoreImpl_RecallBSP();
    LTLoggerImpl_DisablePreLTCoreStompLogging();
    #ifndef LT_NO_DYNAMIC_LOADER
    {
        extern LTLibrary_LTObjectMapEntry * g_pStaticallyBoundLTObjectMapEntries;
        g_pStaticallyBoundLTObjectMapEntries = NULL;
    }
    #endif
    LTCoreBSP_Finalize(s_pBSP);
    s_pBSP = NULL;

    /* reset s_nRetVal and reset s_nRunCount so that LT can be run again */
    s_nRetVal = 0;
    LTAtomic_Store(&s_nRunCount, 0);
}

/* LTThreadImpl.c is not built for hosted architectures */
void LT_WEAK LTThreadImpl_ThreadMain(void * clientData) {
    LT_UNUSED(clientData);
}

/*********************************
 * LTCoreImpl.c static functions */
static void LTCoreImpl_CoreThreadProc(const char * pGenesisLibraryName) {
    /* Note: the function parameter here should be void * pClientData not const char * pGenesisLibraryName
             but the type has been changed because the pClientData argument is not used (NULL) and
             by fiddling with the type a free temporary stack variable is obtained for use, saving having
             to allocate a local variable on the stack by reusing one already there.  Is it important to
             save 4 bytes on the stack?  Yes.  Er maybe.

       We are now in the Core thread, at this point we:
             a. Initialize the rest of LT.
             b. Open the Genesis Library and start the Genesis thread which invokes the Genesis Run() function.
             c. Call LTThreadImpl_ThreadMain(). Now we are a full-fledged LT thread with a message loop.
             d. When LTThreadImpl_ThreadMain() returns, exit LT.
     */

    /* Set s_pCore so that LT_GetCore() will work, check for hosted mode, if so init LTLibraryManager, LTThreads and LTEvnt and bail,
       otherwise initialize LTLoggerImpl so ConsolePrint and log functions will work (direct to console, no ring buffer yet) */
    s_pCore = (LTCore *)&s_LTCore;
    if (s_coreImpl.bHostedMode) {
        LTLibraryManager_Init();
        LTThreadImpl_Init();
        LTEventImpl_Init();
        /* hosted mode has no sleep so don't init sleep event. */
        /* hosted mode has no LTDeviceConfig.json (it could, but doesn't now) so don't create s_coreImpl.pDeviceKonfig */
        return;
    }

    /* a1. Initialize the logger */
    LTLoggerImpl_Init();
    LTLoggerImpl_TraceAddStreams(IMPL_LTTRACE_STREAMS_PTR(LTCore));

    /* a2. Initialize the library manager and get the genesis library name */
    LTLibraryManager_Init();
    pGenesisLibraryName = LTLibraryManager_GetGenesisLibraryName();

    /* b1. Proceed if a genesis library name was specified */
    if (pGenesisLibraryName && *pGenesisLibraryName) {
        /* b2. Proceed to initialize threading LTEvent */
        LTThreadImpl_Init();
        LTEventImpl_Init();
        s_coreImpl.hSleepModeEvent = LTEventImpl_CreateEvent(&s_sleepActionEventArgs, &LTCoreImpl_SleepActionEventDispatchProc, &LTCoreImpl_SleepActionEventDispatchCompleteProc, NULL, NULL);
        s_coreImpl.pDeviceKonfig = lt_createobject(LTDeviceKonfig);

        if (s_coreImpl.pDeviceKonfig) {
            LTLibrary * pGenesisLibrary = LTLibraryManager_OpenLibrary(pGenesisLibraryName);
            /* Call BSP post-initialization hook if provided */
            if (s_pBSP->PostInitialize) {
                s_pBSP->PostInitialize();
            }
            /*  b3. Open the Genesis library and make sure it has a run function */
            if (pGenesisLibrary) {
                if (pGenesisLibrary->Run) {
                    /* b4. Get the required stack size for the run function, create the genesis thread with that stack size and run it */
                    u32 nStackSize = pGenesisLibrary->GetRunFunctionStacksizeRequirement();
                    if (0 == nStackSize) nStackSize = LTCOREIMPL_GENESIS_DEFAULT_STACKSIZE;
                    LTHandle hThreadGenesis = LTThreadImpl_CreateThread(LTCOREIMPL_GENESIS_THREADNAME);
                    s_coreImpl.hThreadGenesis = hThreadGenesis;
                    LTThreadImpl_SetPriority(hThreadGenesis, LTCOREIMPL_GENESIS_PRIORITY);
                    LTThreadImpl_SetStackSize(hThreadGenesis, nStackSize);
                    LTThreadImpl_Start(hThreadGenesis, NULL, NULL);
                    LTThreadImpl_QueueTaskProc(hThreadGenesis, &LTCoreImpl_InvokeGenesisRunFunctionAndTerminateGenesisThread, NULL, (void *)pGenesisLibrary);
                }
                else {
                    LTLoggerImpl_ConsolePrint("%s: %s has no Run() function\n", s_coreImpl.deviceTagName, pGenesisLibraryName);
                }
            }
            else {
                LTLoggerImpl_ConsolePrint("%s: failed to open genesis library %s\n", s_coreImpl.deviceTagName, pGenesisLibraryName);
            }
            s_coreImpl.pGenesisLibrary = pGenesisLibrary;
        }
        else {
            LTLoggerImpl_ConsolePrint("%s: failed to create LTDeviceKonfig object\n", s_coreImpl.deviceTagName);
        }
    }
    else {
        LTLoggerImpl_ConsolePrint("usage: ltrun <library> [library_options]\n");
    }

    /* c. Lower thread priority and call LTThreadImpl_ThreadMain() to become a full-fledged LT thread with a message loop. */
    LTThreadImpl_SetPriority(s_coreImpl.hThreadCore, LTCOREIMPL_CORE_FINAL_PRIORITY);
    LTThreadImpl_ThreadMain(LTHANDLE_TO_VOIDPTR(&s_coreImpl.hThreadCore));

    /* d. LT Shutdown */
    {
        LTAtomic_CompareAndExchange(&s_nRunCount, 2, 3); // 2 is running, 3 is shutting down

        LTLoggerImpl_Flush();
        LTLoggerImpl_SetLogHookFunction(NULL);
        LTLoggerImpl_SetTraceHookFunction(NULL);
        LTLibraryManager_CloseLibrary(s_coreImpl.pGenesisLibrary);
        s_coreImpl.pGenesisLibrary = NULL;

        /* In case closing the genesis library didn't terminate all threads */
        LTThreadImpl_TerminateAllNonSystemThreads();

        /* delete the sleep event handle */
        LTHandle_DestroyHandle(s_coreImpl.hSleepModeEvent);
        s_coreImpl.hSleepModeEvent = 0;

        /* destroy the LTDeviceKonfig object */
        lt_destroyobject(s_coreImpl.pDeviceKonfig);
        s_coreImpl.pDeviceKonfig = NULL;

        /* Clean up after the Core thread */
        LTHandle_DeleteHandleWithoutDestroy(s_coreImpl.hThreadCore);
        s_coreImpl.hThreadCore = 0;

        /* Finalize threads before events so all of the threads can destroy their events */
        LTThreadImpl_Fini();
        LTEventImpl_Fini();

        /* Uninitialize the library manager, logger and LTLoggerImpl (console print no longer works) */
        LTLibraryManager_Fini();
        LTLoggerImpl_TraceRemoveStreams(IMPL_LTTRACE_STREAMS_PTR(LTCore));

        LT_GetCore()->ConsoleStomp("LT terminated\n");
        LTLoggerImpl_Fini();

        /* Nullify s_pCore; LT_GetCore() will no longer work */
        s_pCore = NULL;
    }
}

static void
LTCoreImpl_InvokeGenesisRunFunctionAndTerminateGenesisThread(void * pClientData) {
    LTLibrary * pGenesisLibrary = (LTLibrary *)pClientData;
    s_nRetVal = pGenesisLibrary->Run(s_coreImpl.argc, s_coreImpl.argv);
    lt_destroyhandle(LTThreadImpl_GetCurrentThread());
}

#if 0
static LTObjectApi *
LTCoreImpl_GetLibraryObjectApi(LTLibrary * pLibrary, const char *objectName, const char *specialization) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (pLibrary && objectName && *objectName && specialization && *specialization) {
        const LTInterface ** pInterfaces = pLibrary->GetInterfaces();
        if (pInterfaces) {
            LTObjectApi *api;
            while (NULL != (api = (LTObjectApi *)*pInterfaces++)) {
                if ((kLTInterfaceType_LTObjectApi == api->GetObjectInterfaceType()) &&
                    (0 == LTStdlibImpl_strcmp(api->GetObjectImplName(), specialization)) &&
                    (0 == LTStdlibImpl_strcmp(api->GetObjectApiName(), objectName))) return api;
            }
        }
    }
    return NULL;
}
#endif

/*******************************************************
 * LTCore_v1 public interface function implementation */
static const char *
LTCoreImpl_GetSoftwareVersion(void) LT_ISR_SAFE {
    return LT_SOFTWARE_VERSION;
}

static bool
LTCoreImpl_IsDeveloperBuild(void) LT_ISR_SAFE {
    // Temporarily enable serial for secured (gold) units to debug RANGER-625/RANGER-678
    // RANGER-682 tracks re-disabling
    return true;

    // enum { kDeveloperBuildVersionTypeOffset = 6, kDeveloperBuildTypeID = 'S' };
    // return *(LT_SOFTWARE_VERSION + kDeveloperBuildVersionTypeOffset) == kDeveloperBuildTypeID;
}

LTTime
LTCoreImpl_GetBuildTime(void) LT_ISR_SAFE {
    return LTTime_Seconds(LT_BUILD_TIME);
}

/*  _________________
    Time Functions */
LTTime
LTCoreImpl_GetKernelTime(void) LT_ISR_SAFE {
    return (LTTime){ (*s_getHighFrequencyCounterFunction)() } ;
}

LTTime
LTCoreImpl_GetKernelTimeResolution(void) LT_ISR_SAFE {
    return (LTTime){ (*s_getHighFrequencyResolutionFunction)() } ;
}

#if 1 // new implementation of LTCoreImpl_FormatCanonicalTimeString; saves 48 bytes stack space on risc V and ARM over lt_sprintf version

static u32 LTCoreImpl_CountDigits(u32 nNumber) {
    // fully stack optimized - 0 bytes of stack space
    u32 nDigits=1; if (nNumber < 10)  goto bail;
        nDigits++; if (nNumber < 100)  goto bail;
        nDigits++; if (nNumber < 1000)  goto bail;
        nDigits++; if (nNumber < 10000)  goto bail;
        nDigits++; if (nNumber < 100000)  goto bail;
        nDigits++; if (nNumber < 1000000)  goto bail;
        nDigits++; if (nNumber < 10000000)  goto bail;
        nDigits++; if (nNumber < 100000000)  goto bail;
        nDigits++; if (nNumber < 1000000000)  goto bail;
        nDigits++;
bail:
    return nDigits;
}

int
LTCoreImpl_FormatCanonicalTimeString(LTTime time, char * pStringBuff, u32 nBuffSize, bool bIncludeBrackets) LT_ISR_SAFE {
   // somewhat ugly but saves 48 bytes of stack space on risc V and ARM over lt_sprintf version of same
   int nCharsAdded = 0;
    if (pStringBuff && nBuffSize) {
        u32 number, digits; u32 pad = 3;
        if (time.nNanoseconds < 0) { pad = 2, time.nNanoseconds = 0 - time.nNanoseconds; }
        if (bIncludeBrackets && nBuffSize > 1) { pStringBuff[nCharsAdded++] = '['; nBuffSize--; }
        if (pad == 2 && nBuffSize > 1) { pStringBuff[nCharsAdded++] = '-'; nBuffSize--; }
        if (nBuffSize > 1) {
            number = LTTime_GetSeconds(time);
            digits = LTCoreImpl_CountDigits(number);
            if (digits < pad) {
                digits = pad - digits;
                while (nBuffSize > 1 && digits--) { pStringBuff[nCharsAdded++] = '0'; nBuffSize--; }
            }
            if (nBuffSize > 1) {
                digits = LTStdlibImpl_u32toString(number, pStringBuff + nCharsAdded, (((nBuffSize - 1) & 0xFF) | kLTStdlib_FormatFlags_LeftJustify));
                nCharsAdded += digits;
                nBuffSize -= digits;
            }
        }
        if (nBuffSize > 1) { pStringBuff[nCharsAdded++] = '.'; nBuffSize--; }
        if (nBuffSize > 1) {
            number = LTTime_GetMicroseconds(LTTime_FractionalSeconds(time));
            digits = LTCoreImpl_CountDigits(number);
            if (digits < 6) {
                digits = 6 - digits;
                while (nBuffSize > 1 && digits--) { pStringBuff[nCharsAdded++] = '0'; nBuffSize--; }
            }
            if (nBuffSize > 1) {
                digits = LTStdlibImpl_u32toString(number, pStringBuff + nCharsAdded, (((nBuffSize - 1) & 0xFF) | kLTStdlib_FormatFlags_LeftJustify));
                nCharsAdded += digits;
                nBuffSize -= digits;
            }
        }
        if (bIncludeBrackets && nBuffSize > 1) { pStringBuff[nCharsAdded++] = ']'; nBuffSize--; }
        pStringBuff[nCharsAdded] = 0;
    }
    return nCharsAdded;
}
#else // lt_snprintf version of FormatCanonicalTimeString
int
LTCoreImpl_FormatCanonicalTimeString(LTTime time, char * pStringBuff, u32 nBuffSize, bool bIncludeBrackets) LT_ISR_SAFE {
    const char * pFormatString = "%s%03lu.%06lu%s";
    if (time.nNanoseconds < 0) { pFormatString = "%s-%02lu.%06lu%s"; time.nNanoseconds = 0 - time.nNanoseconds; }
    int numChars = (pStringBuff && nBuffSize) ? LTStdlibImpl_snprintf(pStringBuff, nBuffSize, pFormatString,
        bIncludeBrackets ? "[" : "",
        LT_Pu32(LTTime_GetSeconds(time)),
        LT_Pu32(LTTime_GetMicroseconds(LTTime_FractionalSeconds(time))),
        bIncludeBrackets ? "]" : "") : 0;
    if (numChars < 0) numChars = 0;
    else if (numChars >= (int)nBuffSize) numChars = (int)nBuffSize - 1;
    return numChars;
}
#endif

LTTime
LTCoreImpl_GetClockTimeUTC(void) LT_ISR_SAFE {
    LT_SIZE nMask = LTKDisableInterrupts();
    LTTime timeUTC = (LTTime){ s_coreImpl.timebaseUTC.secondaryClockTime.nNanoseconds } ;
    if (timeUTC.nNanoseconds) timeUTC.nNanoseconds += (*s_getHighFrequencyCounterFunction)();
    LTKEnableInterrupts(nMask);
    return timeUTC;
}

void
LTCoreImpl_SetClockTimeBaseUTC(const LTTimeBase * pTimeBaseUTC) LT_ISR_SAFE {
    if (pTimeBaseUTC) {
        s64 adjustedOffset = pTimeBaseUTC->secondaryClockTime.nNanoseconds - pTimeBaseUTC->primaryClockTime.nNanoseconds;
        if (adjustedOffset <= 0 || pTimeBaseUTC->secondaryClockTime.nNanoseconds < s_pCore->GetBuildTime().nNanoseconds) return; /* invalid time utc before build time, ignore*/
        LT_SIZE nMask = LTKDisableInterrupts();
        s_coreImpl.timebaseUTC.primaryClockTime.nNanoseconds = 0;
        s_coreImpl.timebaseUTC.secondaryClockTime.nNanoseconds = adjustedOffset;
        LTKEnableInterrupts(nMask);
    }
}

void
LTCoreImpl_GetClockTimeBaseUTC(LTTimeBase * pTimeBaseUTCToSet) LT_ISR_SAFE {
    if (pTimeBaseUTCToSet) {
        LT_SIZE nMask = LTKDisableInterrupts();
        *pTimeBaseUTCToSet = s_coreImpl.timebaseUTC;
        LTKEnableInterrupts(nMask);
    }
}

LTTime
LTCoreImpl_GetClockTimeUTCKernelOffset(void) LT_ISR_SAFE {
    LT_SIZE nMask = LTKDisableInterrupts();
    LTTime timeUTCOffset = LTTime_Nanoseconds(s_coreImpl.timebaseUTC.secondaryClockTime.nNanoseconds);
    LTKEnableInterrupts(nMask);
    return timeUTCOffset;
}

LTTime
LTCoreImpl_GetApproximateClockTimeUTC(void) LT_ISR_SAFE {
    LT_SIZE nMask = LTKDisableInterrupts();
    LTTime timeUTC = (LTTime){ s_coreImpl.timebaseUTC.secondaryClockTime.nNanoseconds ? 0 : s_coreImpl.timebaseApproximateUTC.secondaryClockTime.nNanoseconds } ;
    if (timeUTC.nNanoseconds) timeUTC.nNanoseconds += (*s_getHighFrequencyCounterFunction)();
    LTKEnableInterrupts(nMask);
    return timeUTC;
}

void
LTCoreImpl_SetApproximateClockTimeBaseUTC(const LTTimeBase * pTimeBaseUTC) LT_ISR_SAFE {
    if (pTimeBaseUTC) {
        s64 adjustedOffset = pTimeBaseUTC->secondaryClockTime.nNanoseconds - pTimeBaseUTC->primaryClockTime.nNanoseconds;
        if (adjustedOffset <= 0) return; /* invalid time utc, ignore*/
        LT_SIZE nMask = LTKDisableInterrupts();
        s_coreImpl.timebaseApproximateUTC.primaryClockTime.nNanoseconds = 0;
        s_coreImpl.timebaseApproximateUTC.secondaryClockTime.nNanoseconds = adjustedOffset;
        LTKEnableInterrupts(nMask);
    }
}

/* ___________________
   Library handling */
LTLibrary *
LTCoreImpl_OpenLibrary(const char * pLibraryName) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTLibrary * pLibrary = NULL;
    if (pLibraryName && *pLibraryName) {
        // first mask out LTCore
        if (0 == LTStdlibImpl_strcmp("LTCore", pLibraryName)) return (LTLibrary *)s_pCore;

        // Call library hook before LTLibraryManager_OpenLibrary() to give clients the option to replace the OpenLibrary() call
        if (s_pLibraryHook) pLibrary = (*s_pLibraryHook)(kLTCore_LibraryHookSite_SubstituteOpen, NULL, pLibraryName);

        // Hook did not replace. Load as regular library.
        if (NULL == pLibrary) pLibrary = LTLibraryManager_OpenLibrary(pLibraryName);

        // Call library hook after LTLibraryManager_OpenLibrary(), to allow the returned library to be manipulated
        if (pLibrary && s_pLibraryHook) pLibrary = (*s_pLibraryHook)(kLTCore_LibraryHookSite_AfterOpen, pLibrary, pLibraryName);
    }
    return pLibrary;
}

void
LTCoreImpl_CloseLibrary(LTLibrary * pLibrary) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (pLibrary && s_pLibraryHook) pLibrary = (*s_pLibraryHook)(kLTCore_LibraryHookSite_BeforeClose, pLibrary, NULL);
    LTLibraryManager_CloseLibrary(pLibrary);
}

bool
LTCoreImpl_SetLibraryHook(LTCore_LibraryHookFunction *pHookFunction) {
    // Report error if caller tries to install a hook on top of another one
    if (s_pLibraryHook && pHookFunction) return false;
    s_pLibraryHook = pHookFunction;
    return true;
}

LTInterface *
LTCoreImpl_GetLibraryInterface(LTLibrary * pLibrary, const char * pInterfaceName) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (pLibrary && pInterfaceName && *pInterfaceName) {
        const LTInterface ** pInterfaces = pLibrary->GetInterfaces();
        if (pInterfaces) {
            LTInterface *  pInterface;
            while (NULL != (pInterface = (LTInterface *)*pInterfaces++)) {
                if (0 == LTStdlibImpl_strcmp(pInterface->GetInterfaceName(), pInterfaceName)) return pInterface;
            }
        }
    }
    return NULL;
}

static const char * LTCoreImpl_GetLTCoreLibraryBuildVersion(void) {
    return LTTYPES_LTLIBRARY_BUILD_VERSION;
}

#if LTCORE_TRACE_HEAP_ALLOCATIONS
static void LTCoreImpl_WireInHeapBlock(LTCore_HeapBlock * pHeapBlock) {
    pHeapBlock->pPrev = NULL;
    u32 nMask = LTKDisableInterrupts();
    s_nNumBlocksAllocated++;
    pHeapBlock->pNext = s_pHeapBlockList;
    if (s_pHeapBlockList) s_pHeapBlockList->pPrev = pHeapBlock;
    s_pHeapBlockList = pHeapBlock;
    LTKEnableInterrupts(nMask);
}

static void LTCoreImpl_WireOutHeapBlock(LTCore_HeapBlock * pHeapBlock) {
    u32 nMask = LTKDisableInterrupts();
    s_nNumBlocksAllocated--;
    if (s_pNextEnumeratedHeapBlock == pHeapBlock) s_pNextEnumeratedHeapBlock = pHeapBlock->pNext;
    if (pHeapBlock == s_pHeapBlockList) {
        if ((s_pHeapBlockList = pHeapBlock->pNext)) s_pHeapBlockList->pPrev = NULL;
    }
    else {
        pHeapBlock->pPrev->pNext = pHeapBlock->pNext;
        if (pHeapBlock->pNext) pHeapBlock->pNext->pPrev = pHeapBlock->pPrev;
    }
    LTKEnableInterrupts(nMask);
}
#endif

void *
LTCoreImpl_Alloc(LT_SIZE nBytes LT_CALLSITE_FUNCTION_PARAMETER) {
    DISABLE_UNUSED_LT_CALLSITE;

    if (0 == nBytes) {
        LTTRACE_MEM_OP(kLTTrace_MemOp_Alloc, 0);
        return NULL;
    }

    /* Guard against overflow when adding the LTCore_HeapBlock header size; a wrapped
     * allocation would succeed with a tiny buffer and the caller would then write
     * nBytes worth of data into it (heap buffer overflow). */
    if (nBytes > LT_SIZE_MAX - sizeof(LTCore_HeapBlock)) {
        LTTRACE_MEM_OP(kLTTrace_MemOp_Error, nBytes);
        return NULL;
    }

    LTCore_HeapBlock * pHeapBlock = LTKAlloc(nBytes + sizeof(LTCore_HeapBlock));
    if (! pHeapBlock) {
        LTTRACE_MEM_OP(kLTTrace_MemOp_Error, nBytes);
        LTLOG_STOMP("alloc", "failed to alloc %lu bytes", LT_PLT_SIZE(nBytes));
        #if LTCORE_BREAK_ON_HEAP_ERRORS
            LTKDebugBreak();
        #endif
        return NULL;
    }

#if !LTCORE_ELIDE_ALL_LTCORE_HEAPTRACKING
    pHeapBlock->nBytes = (u32)nBytes;
    pHeapBlock->hThread = LTThreadImpl_GetCurrentThread();
    LTThreadImpl_IncreaseHeapAllocatedByteCount(pHeapBlock->hThread, nBytes);
    LTTRACE_MEM_OP(kLTTrace_MemOp_Alloc, nBytes);
#else
    LTTRACE_MEM_OP(kLTTrace_MemOp_Alloc, 1);
#endif

    #if LTCORE_TRACE_HEAP_ALLOCATIONS
        pHeapBlock->tag = s_HeapTag;
        #if DISABLE_LT_CALLSITE
            pHeapBlock->callsite = LT_NULL_CALLSITE;
        #else
            pHeapBlock->callsite = callsite;
        #endif
        LTCoreImpl_WireInHeapBlock(pHeapBlock);
    #endif

    return pHeapBlock + 1;
}

static void *
LTCoreImpl_AllocFromRegion(LTMemoryRegion region, LT_SIZE nBytes LT_CALLSITE_FUNCTION_PARAMETER) {
    DISABLE_UNUSED_LT_CALLSITE;

    if (0 == nBytes) {
        LTTRACE_MEM_OP(kLTTrace_MemOp_Alloc, 0);
        return NULL;
    }

    /* Guard against overflow when adding the LTCore_HeapBlock header size. */
    if (nBytes > LT_SIZE_MAX - sizeof(LTCore_HeapBlock)) {
        LTTRACE_MEM_OP(kLTTrace_MemOp_Error, nBytes);
        return NULL;
    }

    LTCore_HeapBlock * pHeapBlock = LTKAllocFromRegion(region, nBytes + sizeof(LTCore_HeapBlock));
    if (! pHeapBlock) {
        LTTRACE_MEM_OP(kLTTrace_MemOp_Error, nBytes);
        LTLOG_STOMP("alloc.region", "failed to alloc %lu bytes from region %u",
                    LT_PLT_SIZE(nBytes), (unsigned)region);
        #if LTCORE_BREAK_ON_HEAP_ERRORS
            LTKDebugBreak();
        #endif
        return NULL;
    }

#if !LTCORE_ELIDE_ALL_LTCORE_HEAPTRACKING
    pHeapBlock->nBytes = (u32)nBytes;
    pHeapBlock->hThread = LTThreadImpl_GetCurrentThread();
    LTThreadImpl_IncreaseHeapAllocatedByteCount(pHeapBlock->hThread, nBytes);
    LTTRACE_MEM_OP(kLTTrace_MemOp_Alloc, nBytes);
#else
    LTTRACE_MEM_OP(kLTTrace_MemOp_Alloc, 1);
#endif

    #if LTCORE_TRACE_HEAP_ALLOCATIONS
        pHeapBlock->tag = s_HeapTag;
        #if DISABLE_LT_CALLSITE
            pHeapBlock->callsite = LT_NULL_CALLSITE;
        #else
            pHeapBlock->callsite = callsite;
        #endif
        LTCoreImpl_WireInHeapBlock(pHeapBlock);
    #endif

    return pHeapBlock + 1;
}

void *
LTCoreImpl_ReAlloc(void * pMem, LT_SIZE nBytes LT_CALLSITE_FUNCTION_PARAMETER) {
    DISABLE_UNUSED_LT_CALLSITE;
    if (NULL == pMem) return LTCoreImpl_Alloc(nBytes LT_CALLSITE_FUNCTION_PARAMETER_PASSTHRU);
    if (0 == nBytes) { LTCoreImpl_Free(pMem LT_CALLSITE_FUNCTION_PARAMETER_PASSTHRU); return NULL; }

    /* Guard against overflow when adding the LTCore_HeapBlock header size. Matches
     * the guard in LTCoreImpl_Alloc / LTCoreImpl_AllocFromRegion. */
    if (nBytes > LT_SIZE_MAX - sizeof(LTCore_HeapBlock)) {
        LTTRACE_MEM_OP(kLTTrace_MemOp_Error, nBytes);
        return NULL;
    }
    LTCore_HeapBlock * pHeapBlock = (LTCore_HeapBlock *)pMem - 1;

    /* If the source block lives in an exclusive (e.g. DMA-only) region, LTKReAlloc
     * will refuse to move it out of that region (any move target found by the
     * region-blind LTKAlloc path would silently violate the DMA contract).  Detect
     * this here so we can produce a clear diagnostic instead of a generic OOM log
     * and fail fast in debug builds. */
    if (LTKHeap_IsExclusiveByPtr(pHeapBlock)) {
        LTLOG_STOMP("realloc", "refused to realloc %lu bytes from exclusive region", LT_PLT_SIZE(nBytes));
        #if LTCORE_BREAK_ON_HEAP_ERRORS
            LTKDebugBreak();
        #endif
        return NULL;
    }

#if LTCORE_ELIDE_ALL_LTCORE_HEAPTRACKING
    LTCore_HeapBlock * pHeapBlockNew = (LTCore_HeapBlock *)LTKReAlloc(pHeapBlock, nBytes + sizeof(LTCore_HeapBlock));
    if (NULL == pHeapBlockNew) {
        LTLOG_STOMP("realloc", "failed to realloc to %lu bytes", LT_PLT_SIZE(nBytes));
        //LTTRACE_NUMBERS(LTTRACE_STREAM_MEMORY, 0, changeAmount);
        #if LTCORE_BREAK_ON_HEAP_ERRORS
            LTKDebugBreak();
        #endif
        pHeapBlockNew = pHeapBlock;
        pHeapBlock = NULL;  // mark to indicate pHeapBlock left unchanged, we still need to update accounting using pHeapBlockNew
    }
#else
    // realloc might move the memory and free the old location, so update accounting and unwire now
    // and add it back after the realloc.
    LTThreadImpl_ReduceHeapAllocatedByteCount(pHeapBlock->hThread, pHeapBlock->nBytes);
    #if LTCORE_TRACE_HEAP_ALLOCATIONS
        LTCoreImpl_WireOutHeapBlock(pHeapBlock);
    #endif

    LTCore_HeapBlock * pHeapBlockNew = (LTCore_HeapBlock *)((pHeapBlock->nBytes == (u32)nBytes) ? pHeapBlock : LTKReAlloc(pHeapBlock, nBytes + sizeof(LTCore_HeapBlock)));
    if (NULL == pHeapBlockNew) {
        LTLOG_STOMP("realloc", "failed to realloc %lu into %lu bytes", LT_PLT_SIZE(pHeapBlock->nBytes), LT_PLT_SIZE(nBytes));
        #if LTCORE_BREAK_ON_HEAP_ERRORS
            LTKDebugBreak();
        #endif
        pHeapBlockNew = pHeapBlock;
        pHeapBlock = NULL;  // mark to indicate pHeapBlock left unchanged, we still need to update accounting using pHeapBlockNew
    }

    // update thread allocation
    pHeapBlockNew->hThread = LTThreadImpl_GetCurrentThread();
    if (pHeapBlock) {
        /* pHeapBlock == NULL means realloc returned NULL and left original block unchanged;
           so only update size when pHeapBlock is non-null indicating realloc worked */
        pHeapBlockNew->nBytes = nBytes;
    }
    LTThreadImpl_IncreaseHeapAllocatedByteCount(pHeapBlockNew->hThread, pHeapBlockNew->nBytes);
#endif

    #if LTCORE_TRACE_HEAP_ALLOCATIONS
        pHeapBlockNew->tag = s_HeapTag;

        #if DISABLE_LT_CALLSITE
            pHeapBlockNew->callsite = LT_NULL_CALLSITE;
        #else
            pHeapBlockNew->callsite = callsite;
        #endif
        LTCoreImpl_WireInHeapBlock(pHeapBlockNew);
    #endif

    return pHeapBlock ? pHeapBlockNew + 1 : NULL;
}

void
LTCoreImpl_Free(void * pMem LT_CALLSITE_FUNCTION_PARAMETER) {
    DISABLE_UNUSED_LT_CALLSITE;
    if (pMem) {
        LTCore_HeapBlock * pHeapBlock = (LTCore_HeapBlock *)pMem - 1;
#if !LTCORE_ELIDE_ALL_LTCORE_HEAPTRACKING
        LTThreadImpl_ReduceHeapAllocatedByteCount(pHeapBlock->hThread, pHeapBlock->nBytes);
        LTTRACE_MEM_OP(kLTTrace_MemOp_Free, pHeapBlock->nBytes);
        #if LTCORE_TRACE_HEAP_ALLOCATIONS
            LTCoreImpl_WireOutHeapBlock(pHeapBlock);
        #endif
#else
        // If we're not tracking allocation sizes, match alloc in tracking the count.
        LTTRACE_MEM_OP(kLTTrace_MemOp_Free, 1);
#endif
        LTKFree(pHeapBlock);
    }
}

LT_SIZE
LTCoreImpl_GetAllocSize(void * pMem) {
    if (!pMem) return 0;
    LTCore_HeapBlock * pHeapBlock = (LTCore_HeapBlock *)pMem - 1;
    LT_SIZE size = LTKGetActualAllocationSize(pHeapBlock);
    if (!size) return 0;
    return size - sizeof(LTCore_HeapBlock);
}

LT_SIZE
LTCoreImpl_GetTotalSystemRAM(void) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    return LTKGetTotalSystemRAM();
}

LT_SIZE
LTCoreImpl_GetAvailableSystemRAM(void) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    return LTKGetAvailableSystemRAM();
}

LT_SIZE
LTCoreImpl_GetSystemRAMLowWatermark(void) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    return LTKGetSystemRAMLowWatermark();
}

LT_SIZE
LTCoreImpl_GetLargestAvailableBlockInRAM(void) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    return LTKGetLargestAvailableBlockInRAM();
}

LT_SIZE
LTCoreImpl_GetCurrentRAMAllocationCount(void) {
    return LTKGetLTKCurrentAllocationCount();
}

LT_SIZE
LTCoreImpl_GetMaxRAMAllocationCount(void) {
    return LTKGetLTKAllocationHighWatermark();
}

void
LTCoreImpl_SetHeapTag(char tag) {
#if LTCORE_TRACE_HEAP_ALLOCATIONS
    s_HeapTag = tag;
#else
    LT_UNUSED(tag);
#endif
}

static bool
LTCoreImpl_EnumerateHeapAllocatedBlockInfo(LTThread hAllocatingThreadFilter, char tagFilter, LTCore_HeapAllocatedBlockInfoEnumCB *pCallback, void *pClientData) {
#if LTCORE_TRACE_HEAP_ALLOCATIONS
    enum { kMaxAtATime = 10 };
    bool bRetVal = false;
    if (pCallback) {
        if (LTAtomic_CompareAndExchange(&s_heapBlockEnumerationInProgress, 0, 1)) {
            LTCore_HeapAllocatedBlockInfo * pBlockInfo = core_malloc(sizeof(LTCore_HeapAllocatedBlockInfo) * kMaxAtATime);
            if (pBlockInfo) {
                bRetVal = true;
                u32 nMask = LTKDisableInterrupts();
                s_pNextEnumeratedHeapBlock = s_pHeapBlockList;
                while (bRetVal && s_pNextEnumeratedHeapBlock) {
                    u32 count = 0;
                    LTCore_HeapAllocatedBlockInfo * pCurrInfo = pBlockInfo;
                    while ((count < kMaxAtATime) && s_pNextEnumeratedHeapBlock) {
                        if ((0 == hAllocatingThreadFilter || hAllocatingThreadFilter == s_pNextEnumeratedHeapBlock->hThread) &&
                            (0 == tagFilter || tagFilter == s_pNextEnumeratedHeapBlock->tag)) {
                            pCurrInfo->pAddr   = (void *)(s_pNextEnumeratedHeapBlock + 1);
                            pCurrInfo->hThread = s_pNextEnumeratedHeapBlock->hThread;
                            pCurrInfo->nBytes  = s_pNextEnumeratedHeapBlock->nBytes;
                            pCurrInfo->callsite  = s_pNextEnumeratedHeapBlock->callsite;
                            pCurrInfo++;
                            count++;
                        }
                        s_pNextEnumeratedHeapBlock = s_pNextEnumeratedHeapBlock->pNext;
                    }
                    if (count) {
                        LTKEnableInterrupts(nMask);
                        bRetVal = (*pCallback)(pBlockInfo, count, pClientData);
                        nMask = LTKDisableInterrupts();
                    }
                }
                s_pNextEnumeratedHeapBlock = NULL;
                LTKEnableInterrupts(nMask);
                core_free(pBlockInfo);
            }
            LTAtomic_Store(&s_heapBlockEnumerationInProgress, 0);
        }
    }
    return bRetVal;
#else
    LT_UNUSED(hAllocatingThreadFilter); LT_UNUSED(tagFilter); LT_UNUSED(pCallback); LT_UNUSED(pClientData);
    return false;
#endif
}

u64
LTCoreImpl_SnapshotMemstat(void) {
    if (LTKInsideInterruptContext()) return 0;

    /* The 64 bits are encoded as followed:
     * Bits   0-7: maximum lt allocation count, fractional part  [0..99] (.00% - .99%)
     * Bits  8-15: current lt allocation count, fractional part  [0..99] (.00% - .99%)
     * Bits 16-23: total heap memory, fractional part            [0..99] (.00% - .99%)
     * Bits 24-25: maximum lt allocation count units:            00=kb, 01=mb, 10=gb, 11=tb
     * Bits 26-27: current lt allocation count units:            00=kb, 01=mb, 10=gb, 11=tb
     * Bits 28-29: total heap memory units:                      00=kb, 01=mb, 10=gb, 11=tb
     * Bits 30-31: reserved
     * Bits 32-41: maximum lt allocation count, whole part       [0..1023]
     * Bits 42-51: current lt allocation count, whole part       [0..1023]
     * Bits 52-61: total heap memory, whole part                 [0..1023]
     * Bits 62-63: reserved
     */

    // take each value down while it is >= 1mb, in other words take each down as far as kb

    u64 memstat = 0, units, value;

    // totalMem
    units = 0; value = LTCoreImpl_GetTotalSystemRAM();
    while (value >= (1024 << 10) && units < 3) { value >>= 10; units++; }
    memstat |= units << 28;                                     // or in totalMemUnits
    memstat |= (((((value % 1024) * 100) >> 10) & 0xFF) << 16); // or in totalMemFractional
    memstat |= (((value >> 10) & 1023) << 52);                  // or in totalMemWhole

    // currentUsed
    units = 0; value = LTCoreImpl_GetCurrentRAMAllocationCount();
    while (value >= (1024 << 10) && units < 3) { value >>= 10; units++; }
    memstat |= units << 26;                                     // or in usedMemUnits
    memstat |= (((((value % 1024) * 100) >> 10) & 0xFF) << 8);  // or in usedMemFractional
    memstat |= (((value >> 10) & 1023) << 42);                  // or in usedMemWhole

    // maxUsedMem
    units = 0; value = LTCoreImpl_GetMaxRAMAllocationCount();
    while (value >= (1024 << 10) && units < 3) { value >>= 10; units++; }
    memstat |= units << 24;                                     // or in maxUsedMemUnits
    memstat |= ((((value % 1024) * 100) >> 10) & 0xFF);         // or in maxUsedMemFractional
    memstat |= (((value >> 10) & 1023) << 32);                  // or in maxUsedMemWhole

    return memstat;
}

static const char * LTCoreImpl_MemstatUnitToString(u8 unit) {
    switch (unit) {
        case 1: return "mb";
        case 2: return "gb";
        case 3: return "tb";
        default: return "k";
    }
}

int
LTCoreImpl_FormatCanonicalMemstatString(u64 memstat, char * pStringBuff, u32 nBuffSize, bool bIncludeBrackets) {
    int numChars = 0;
    if (pStringBuff && nBuffSize) {
        numChars = LTStdlibImpl_snprintf(pStringBuff, nBuffSize,
            "%s%lu.%02lu%s/%lu.%02lu%s used, %lu.%02lu%s hi%s",
                bIncludeBrackets ? "[" : "",
                LT_Pu32((memstat >> 42) & 1023), LT_Pu32((memstat >>  8) & 0xFF), LTCoreImpl_MemstatUnitToString((memstat >> 26) & 3),
                LT_Pu32((memstat >> 52) & 1023), LT_Pu32((memstat >> 16) & 0xFF), LTCoreImpl_MemstatUnitToString((memstat >> 28) & 3),
                LT_Pu32((memstat >> 32) & 1023), LT_Pu32( memstat        & 0xFF), LTCoreImpl_MemstatUnitToString((memstat >> 24) & 3),
                bIncludeBrackets ? "]" : "");
        if (numChars < 0) numChars = 0;
        else if (numChars >= (int)nBuffSize) numChars = (int)nBuffSize - 1;
    }
    return numChars;
}

static LTMemoryRegion
LTCoreImpl_GetNamedMemoryRegion(const char *name) {
    return s_coreImpl.pDeviceKonfig ? s_coreImpl.pDeviceKonfig->API->GetNamedMemoryRegion(s_coreImpl.pDeviceKonfig, name) : (LTMemoryRegion)0;
}

static const char * LTCoreImpl_FindLibraryFromObjectMap(const char * pObjectName, const char * pSpecialization) {
    extern LTLibrary_LTObjectMapEntry * g_pStaticallyBoundLTObjectMapEntries;
    LTLibrary_LTObjectMapEntry * pCurrEntry = g_pStaticallyBoundLTObjectMapEntries;
    while (pCurrEntry) {
        if (0 == LTStdlibImpl_strcmp(pSpecialization, pCurrEntry->specializationName) &&
            0 == LTStdlibImpl_strcmp(pObjectName, pCurrEntry->objectApiName)) return pCurrEntry->libraryName;
        pCurrEntry = pCurrEntry->pNextEntry;
    }
    return NULL;
}

static const LTObjectApi * LTCoreImpl_FindObjectApiFromStaticallyBoundList(const char * pObjectName, const char * pSpecialization, const LTLibrary_LTObjectApiListEntry * pEntry) {
    while (pEntry) {
        if (pEntry->objectApi &&
            (0 == LTStdlibImpl_strcmp(pSpecialization, pEntry->objectApi->GetObjectImplName())) &&
            (0 == LTStdlibImpl_strcmp(pObjectName, pEntry->objectApi->GetObjectApiName()))) return pEntry->objectApi;
        pEntry = pEntry->pNextEntry;
    }
    return NULL;
}

static LTString
CreateObjectReference(const char *objectName, const char *specialization) {
    if (!objectName || !specialization) return NULL;
    /* Generate object reference name: "object:specialization" */
    LTString objectRef = ltstring_create(objectName);
    if (!objectRef) return false;
    ltstring_appendchar(&objectRef, ':');
    ltstring_append(&objectRef, specialization);
    return objectRef;
}

static LTObject *
LTCoreImpl_MacroCreateObject(const char * pObjectName, const char * pSpecialization, u32 reserved, LTLibrary_MacroCreateObjectParms *createParms LT_CALLSITE_FUNCTION_PARAMETER) {
    if (! (pObjectName && *pObjectName && pSpecialization && *pSpecialization)) return NULL;
    if (s_mocks) {
        LTString objectRef = CreateObjectReference(pObjectName, pSpecialization);
        if (objectRef) {
            LTString mockSpec = LTCStringKeyedArray_Get(s_mocks, objectRef, NULL);
            if (mockSpec) pSpecialization = mockSpec;
            ltstring_destroy(objectRef);
        }
    }
    const LTObjectApi * pObjectAPI = NULL;
    const char * pLibName = LTCoreImpl_FindLibraryFromObjectMap(pObjectName, pSpecialization);
    if (NULL == pLibName) {
        /* must be private object */
        if (NULL != (pObjectAPI = LTCoreImpl_FindObjectApiFromStaticallyBoundList(pObjectName, pSpecialization, *createParms->libraryPrivateObjectList))) {
            pLibName = pObjectAPI->GetObjectLibrary()->GetLibraryExtrinsicName();
        }
        if (NULL == pLibName) return NULL;
    }
    /* open the library before creating the object; this will load the lib if required and increase the ref count of the library */
    // LTLOG("macrocreateobject]", "%s%sopening %s for lt_createobject(%s, %s)", createParms->creatingLibrary ? createParms->creatingLibrary : "", createParms->creatingLibrary ? ": " : "", pLibName, pObjectName, pSpecialization);
    LTLibrary * pLibrary = NULL;
    if (createParms && (0 == lt_strcmp(pLibName, createParms->creatingLibrary))) {
        /* we're creating an object from our own library, only open the library if it's already open;
           this will increase the reference count of the library; if the library isn't already open,
           it means we're creating a library object from it's LibInit proc; we won't open (we can't)
           and won't increment the ref count of the lib
        */
       if (LTLibraryManager_IsLibraryOpen(pLibName)) pLibrary = LTLibraryManager_OpenLibrary(pLibName);
       if (NULL == pObjectAPI) pObjectAPI = LTCoreImpl_FindObjectApiFromStaticallyBoundList(pObjectName, pSpecialization, *createParms->libraryExportedObjectList);
    }
    else {
        LT_ASSERT(NULL == pObjectAPI);
        pLibrary = LTLibraryManager_OpenLibrary(pLibName);
        if (pLibrary) pObjectAPI = LTCoreImpl_FindObjectApiFromStaticallyBoundList(pObjectName, pSpecialization, pLibrary->GetExportedObjectList());
    }
    /* ok now can finally create the object */
    /* check for singleton */
    LTObject *object = pObjectAPI ? (LTObject *)LTCoreImpl_Alloc(pObjectAPI->GetObjectImplSize() LT_CALLSITE_FUNCTION_PARAMETER_PASSTHRU) : NULL;
    if (object) {
        LTStdlibImpl_memset(object, 0, pObjectAPI->GetObjectImplSize());
        object->API = pObjectAPI;
        *((u32 *)(&object->reserved)) = kLTObjectFlags_AllocFree | reserved;
        if (pObjectAPI->ConstructObject(object)) {
            LTAtomic_FetchAdd((LTAtomic *)&object->refCount, 1);
            /* we don't refcount the library for objects created in LibInit of the library containing the object */
            if (pLibrary) *((u32 *)(&object->reserved)) |= kLTObjectFlags_LibraryRefCounted;
        }
        else {
            core_free(object);
            object = NULL;
        }
    }
    if ((! object) && pLibrary) lt_closelibrary(pLibrary);
    return object;
}

static bool
LTCoreImpl_GetDeviceAndUnitIndicesFromDeviceOrDriverObject(LTObject * pDeviceOrDriverObject, u32 * pDeviceIndexToSet, u32 * pUnitIndexToSet) {
    if (pDeviceOrDriverObject && pDeviceIndexToSet && pUnitIndexToSet &&
        (0 != (*pDeviceIndexToSet = LTOBJECT_GETDEVICENUMBER(pDeviceOrDriverObject))) &&
        (0 != (*pUnitIndexToSet = LTOBJECT_GETUNITNUMBER(pDeviceOrDriverObject))))
    {
        *pDeviceIndexToSet = *pDeviceIndexToSet - 1;
        *pUnitIndexToSet = *pUnitIndexToSet - 1;
        return true;
    }
    return false;
}

static LTObject *
LTCoreImpl_MacroCreateDeviceObject(const char * pObjectName, const char * pSpecialization, const char * pDeviceUnitName, LTLibrary_MacroCreateObjectParms *createParms LT_CALLSITE_FUNCTION_PARAMETER) {
    if (! (pObjectName && *pObjectName && pSpecialization && *pSpecialization && s_coreImpl.pDeviceKonfig)) return NULL;

    u32 nDeviceIndex = 0, nUnitIndex = 0;

    if (! s_coreImpl.pDeviceKonfig->API->GetDeviceUnitIndices(
        s_coreImpl.pDeviceKonfig, pObjectName, pDeviceUnitName, &nDeviceIndex, &nUnitIndex)) return NULL;

    bool bExclusiveAccess = s_coreImpl.pDeviceKonfig->API->GetDeviceUnitExclusivity(s_coreImpl.pDeviceKonfig, nDeviceIndex, nUnitIndex);
    LT_UNUSED(bExclusiveAccess);

    // if the device has a pass-through api, change the specialization name to the name of the driver
    if (s_coreImpl.pDeviceKonfig->API->GetDevicePassThruAPI(s_coreImpl.pDeviceKonfig, nDeviceIndex)) {
        pSpecialization = s_coreImpl.pDeviceKonfig->API->GetDeviceUnitDriver(s_coreImpl.pDeviceKonfig, nDeviceIndex, nUnitIndex);
        if (NULL == pSpecialization) return NULL;
    }

    // convert the deviceIndex and unitIndex into 1 based device and unit numbers and prepare them for flags insertion
    nDeviceIndex++; nDeviceIndex <<= kLTObjectFlags_DeviceNumberShift;
    nUnitIndex++; nUnitIndex <<= kLTObjectFlags_UnitNumberShift;

    // first check to see if this device and unit combo is already created, TBD

    // now create the object
    LTObject * pObject = LTCoreImpl_MacroCreateObject(pObjectName, pSpecialization, nDeviceIndex | nUnitIndex, createParms LT_CALLSITE_FUNCTION_PARAMETER_PASSTHRU);

    // stick it in the already created table - have to modify destroyobject to remove from this table

    return pObject;
}

static LTObject *
LTCoreImpl_MacroCreateDriverObject(LTObject * pDeviceObjectInstance, LTLibrary_MacroCreateObjectParms *createParms LT_CALLSITE_FUNCTION_PARAMETER) {
    char * pDriverObjectApiName = NULL;
    LTObject * pDriverObject = NULL;

    if (! (pDeviceObjectInstance && s_coreImpl.pDeviceKonfig)) goto bail;
    u32 nDeviceNum = LTOBJECT_GETDEVICENUMBER(pDeviceObjectInstance);
    u32 nUnitNum   = LTOBJECT_GETUNITNUMBER(pDeviceObjectInstance);
    if (! (nDeviceNum && nUnitNum)) goto bail;

    const char * deviceClass = s_coreImpl.pDeviceKonfig->API->GetDeviceClassNameAt(s_coreImpl.pDeviceKonfig, nDeviceNum-1);
    const char * driverName = s_coreImpl.pDeviceKonfig->API->GetDeviceUnitDriver(s_coreImpl.pDeviceKonfig, nDeviceNum-1, nUnitNum-1);
    if (! (deviceClass && driverName && *deviceClass && *driverName)) goto bail;

    if (NULL == (pDriverObjectApiName = lt_strdup(deviceClass))) goto bail;
    if (NULL == (deviceClass = lt_strstr(pDriverObjectApiName, "Device")))  goto bail;
    lt_memcpy((char *)deviceClass, "Driver", sizeof("Driver") -1);

    nDeviceNum <<= kLTObjectFlags_DeviceNumberShift;
    nUnitNum <<= kLTObjectFlags_UnitNumberShift;

    pDriverObject = LTCoreImpl_MacroCreateObject(pDriverObjectApiName, driverName, nDeviceNum | nUnitNum, createParms LT_CALLSITE_FUNCTION_PARAMETER_PASSTHRU);

bail:
    if (pDriverObjectApiName) lt_free(pDriverObjectApiName);
    return pDriverObject;
}

static LTObject *
LTCoreImpl_MacroCreateDriverObjectForUnitTest(const char * pDriverObjectAPIName, const char * pDeviceUnitName, LTLibrary_MacroCreateObjectParms *createParms LT_CALLSITE_FUNCTION_PARAMETER) {
    if (! (pDriverObjectAPIName && *pDriverObjectAPIName && s_coreImpl.pDeviceKonfig)) return NULL;
    LT_UNUSED(pDeviceUnitName);
    LT_UNUSED(createParms);
#if DISABLE_LT_CALLSITE
#else
    LT_UNUSED(callsite);
#endif

    return NULL;
}

static void
LTCoreImpl_AddObjectRef(LTObject *object) {
    if (object) LTAtomic_FetchAdd((LTAtomic *)&object->refCount, 1);
}

static void
LTCoreImpl_RemoveObjectRef(LTObject *object) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (object) {
        if (object->reserved & kLTObjectFlags_PseudoObject) {
            LTLOG_YELLOWALERT("removeref", "RemoveObjectRef called on pseudo object %s", object->API->GetObjectImplName());
        }
        else {
            if (1 == LTAtomic_FetchSubtract((LTAtomic *)&object->refCount, 1)) {
                LTLibrary *library = object->API->GetObjectLibrary();
                if (library) {
                    object->API->DestructObject(object);
                    if (object->reserved & kLTObjectFlags_LibraryRefCounted) {
                        if (object->reserved & kLTObjectFlags_AllocFree) core_free(object);
                        LTLibraryManager_CloseLibrary(library);
                    }
                    else {
                        if (object->reserved & kLTObjectFlags_AllocFree) core_free(object);
                    }
                }
            }
        }
    }
}

static void
LTCoreImpl_DestroyObject(LTObject *object) {
    LTCoreImpl_RemoveObjectRef(object);
}

static bool
LTCoreImpl_MockObject(const char *objectName, const char *specialization, const char *mockSpecialization) {
    if (!mockSpecialization) return false;
    LTThread hThread = LTThreadImpl_GetCurrentThread();
    /* If not already reserved attempt to reserve mock mechanism for the current thread. */
    if (LTAtomic_Load(&s_mockGuard) != hThread) {
        if (!LTAtomic_CompareAndExchange(&s_mockGuard, 0, hThread)) return false;
    }
    /* Set mock specialization */
    LTString objectRef = CreateObjectReference(objectName, specialization);
    if (!objectRef) return false;
    LTString copySpec = ltstring_create(mockSpecialization);
    if (!copySpec) goto BAILURE;
    if (!s_mocks) {
        s_mocks = lt_createobject(LTAssociativeArray);
        if (!s_mocks) goto BAILURE;
    }
    lt_free(LTCStringKeyedArray_Get(s_mocks, objectRef, NULL));
    LTCStringKeyedArray_Set(s_mocks, objectRef, copySpec);
    ltstring_destroy(objectRef);
    return true;
BAILURE:
    ltstring_destroy(copySpec);
    ltstring_destroy(objectRef);
    return false;
}

static bool
RemoveAllObjectMocksProc(LTAssociativeArray *array, const void *key, const u16 keySize, void *data, void *clientData) {
    LT_UNUSED(array); LT_UNUSED(key); LT_UNUSED(keySize); LT_UNUSED(clientData);
    lt_free(data);
    return true;
}

static bool
LTCoreImpl_UnmockObject(const char *objectName, const char *specialization) {
    LTThread hThread = LTThreadImpl_GetCurrentThread();
    if (!objectName || !specialization) {
        /* If not already reserved attempt to reserve mock mechanism for the current thread. */
        if (LTAtomic_Load(&s_mockGuard) != hThread) {
            if (!LTAtomic_CompareAndExchange(&s_mockGuard, 0, hThread)) return false;
        }
        /* Completely remove mocks */
        if (s_mocks) {
            s_mocks->API->Enumerate(s_mocks, &RemoveAllObjectMocksProc, NULL);
            lt_destroyobject(s_mocks);
            s_mocks = NULL;
        }
        /* Unreserve the mock/unmock mechanism */
        LTAtomic_Store(&s_mockGuard, 0);
    }
    else {
        /* Check if current thread is allowed to use the mock/unmock mechanism */
        if (LTAtomic_Load(&s_mockGuard) != hThread) return false;
        LTString objectRef = CreateObjectReference(objectName, specialization);
        if (objectRef) {
            if (s_mocks) {
                /* Revert mock specialization */
                lt_free(LTCStringKeyedArray_Get(s_mocks, objectRef, NULL));
                LTCStringKeyedArray_Remove(s_mocks, objectRef);
            }
            ltstring_destroy(objectRef);
        }
    }
    return true;
}

LTArgs *
LTCoreImpl_CreateArgs(u32 nNumArgs) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTArgs * pArgs = NULL;
    if (nNumArgs) {
        LT_SIZE nSize = LTArgs_AllocSize(nNumArgs);
        if (NULL != (pArgs = (LTArgs *)core_malloc(nSize))) {
            LTStdlibImpl_memset(pArgs, 0, nSize);
            pArgs->nNumArgs = nNumArgs;
        }
    }
    return pArgs;
}

LTArgs *
LTCoreImpl_CreateArgsFromDescriptor(LTArgsDescriptor * pDescriptor) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTArgs * pArgs = pDescriptor ? LTCoreImpl_CreateArgs(pDescriptor->nNumArgs) : NULL;
    if (pArgs) {
        LTStdlibImpl_memcpy(pArgs->argTypes, pDescriptor->argTypes, pDescriptor->nNumArgs * sizeof(LTArgType));
    }
    return pArgs;
}

LTArgs *
LTCoreImpl_CreateArgsFrom_lt_va_list(LTArgsDescriptor * pDescriptor, lt_va_list vaList) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTArgs * pArgs = LTCoreImpl_CreateArgsFromDescriptor(pDescriptor);
    LTCoreImpl_UpdateArgsFrom_lt_va_list(pArgs, vaList);
    return pArgs;
}

void
LTCoreImpl_UpdateArgsFrom_lt_va_list(LTArgs *pArgs, lt_va_list vaList) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (pArgs) {
        for (LT_SIZE nIndex = 0; nIndex < pArgs->nNumArgs; nIndex++) {
            switch (pArgs->argTypes[nIndex]) {
                case kLTArgType_void:     break;
                case kLTArgType_pointer:  LTArgs_ArgAt(nIndex, pArgs)->lta_pointer  = lt_va_arg(vaList, void *);           break;
                case kLTArgType_ltsize:   LTArgs_ArgAt(nIndex, pArgs)->lta_ltsize   = lt_va_arg(vaList, LT_SIZE);          break;
                case kLTArgType_lthandle: LTArgs_ArgAt(nIndex, pArgs)->lta_lthandle = lt_va_arg(vaList, LTHandle);         break;
                case kLTArgType_charstar: { core_free(LTArgs_ArgAt(nIndex, pArgs)->lta_charstar);
                                            char * p = lt_va_arg(vaList, char *);
                                          LTArgs_ArgAt(nIndex, pArgs)->lta_charstar = p ? LTStdlibImpl_strdup(p comma_lt_callsite()) : NULL; } break;
                case kLTArgType_double:   LTArgs_ArgAt(nIndex, pArgs)->lta_double   = lt_va_arg(vaList, double);           break;
                case kLTArgType_u8:       LTArgs_ArgAt(nIndex, pArgs)->lta_u8  =  (u8)lt_va_arg(vaList, unsigned int);     break;
                case kLTArgType_u16:      LTArgs_ArgAt(nIndex, pArgs)->lta_u16 = (u16)lt_va_arg(vaList, unsigned int);     break;
                case kLTArgType_u32:      LTArgs_ArgAt(nIndex, pArgs)->lta_u32      = lt_va_arg(vaList, u32);              break;
                case kLTArgType_u64:      LTArgs_ArgAt(nIndex, pArgs)->lta_u64      = lt_va_arg(vaList, u64);              break;
                case kLTArgType_s8:       LTArgs_ArgAt(nIndex, pArgs)->lta_s8  =  (s8)lt_va_arg(vaList, int);              break;
                case kLTArgType_s16:      LTArgs_ArgAt(nIndex, pArgs)->lta_s16 = (s16)lt_va_arg(vaList, int);              break;
                case kLTArgType_s32:      LTArgs_ArgAt(nIndex, pArgs)->lta_s32      = lt_va_arg(vaList, s32);              break;
                case kLTArgType_s64:      LTArgs_ArgAt(nIndex, pArgs)->lta_s64      = lt_va_arg(vaList, s64);              break;
                default:                break;
            }
        }
    }
}

void
LTCoreImpl_DestroyArgs(LTArgs * pArgs) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (pArgs) {
        for (LT_SIZE nIndex = 0; nIndex < pArgs->nNumArgs; nIndex++) {
            if (kLTArgType_charstar == pArgs->argTypes[nIndex]) core_free(LTArgs_ArgAt(nIndex, pArgs)->lta_charstar);
        }
        core_free(pArgs);
    }
}

void
LTCoreImpl_TrimArgs(LTArgs * pArgs) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (pArgs) {
        for (LT_SIZE nIndex = 0; nIndex < pArgs->nNumArgs; nIndex++) {
            if (kLTArgType_charstar == pArgs->argTypes[nIndex]) {
                core_free(LTArgs_ArgAt(nIndex, pArgs)->lta_charstar);
                LTArgs_ArgAt(nIndex, pArgs)->lta_charstar = NULL;
            }
        }
    }
}

int
LTCoreImpl_GetArgc(void) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    return s_coreImpl.argc;
}

const char **
LTCoreImpl_GetArgv(void) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    return s_coreImpl.argv;
}

static void
LTCoreImpl_SetEnterSleepModeProc(LTCore_EnterSleepModeProc * pEnterSleepModeProc, void * pClientData) {
    LTThread hThread = LTThreadImpl_GetCurrentThread();
    if (hThread) {
        LT_SIZE nMask = LTKDisableInterrupts();
        s_coreImpl.hThreadEnterSleepMode = pEnterSleepModeProc ? hThread : 0;
        s_coreImpl.pEnterSleepModeProc = pEnterSleepModeProc;
        s_coreImpl.pEnterSleepModeProcClientData = pClientData && pEnterSleepModeProc ? pClientData : NULL;
        LTKEnableInterrupts(nMask);
        LTLOG_SLEEPMODE("sleepmode.setproc", pEnterSleepModeProc ? "EnterSleepModeProc set" : "EnterSleepModeProc cleared");
    }
}

static void
LTCoreImpl_SetEnterSleepModeIdleDelayAndMinimumSleepDuration(LTTime idleDelay, LTTime minimumSleepDuration) {
    LT_SIZE nMask = LTKDisableInterrupts();
    s_coreImpl.enterSleepModeIdleDelay = idleDelay;
    s_coreImpl.minimumSleepDuration = minimumSleepDuration;
    LTKEnableInterrupts(nMask);
#if LTCORE_LOG_SLEEP_MODE_ACTIVITY
    if (LTTime_IsZero(idleDelay)) { LTLOG_SLEEPMODE("sleepmode.setdelay", "sleepmode delay cleared; sleepmode disabled"); }
    else { LTLOG_SLEEPMODE("sleepmode.setdelay", "sleepmode delay set to %lums", LT_Pu32(LTTime_GetMilliseconds(idleDelay))); }
#endif
}

u32
LTCoreImpl_DisallowSleepMode(void) {
    u32 nDisallowance = LTCoreImpl_kMaxSleepModeDisallowanceGrants;
    void * pCallerAddress = lt_returnaddress();
    if (pCallerAddress == NULL) pCallerAddress = (void *)1;
    #if LTCORE_LOG_SLEEP_MODE_ACTIVITY
        u32 nTotalDisallowances = 0;
    #endif
    LT_SIZE nMask = LTKDisableInterrupts();
    for (nDisallowance = 0; nDisallowance < LTCoreImpl_kMaxSleepModeDisallowanceGrants; nDisallowance++) {
        if (s_coreImpl.sleepModeDisallowances[nDisallowance].pCallerAddress == NULL) {
            s_coreImpl.sleepModeDisallowances[nDisallowance].pCallerAddress = pCallerAddress;
            s_coreImpl.sleepModeDisallowanceCount++;
            break;
        }
    }
    #if LTCORE_LOG_SLEEP_MODE_ACTIVITY
        nTotalDisallowances = s_coreImpl.sleepModeDisallowanceCount;
    #endif
    LTKEnableInterrupts(nMask);
    #if LTCORE_LOG_SLEEP_MODE_ACTIVITY
        if (nDisallowance < LTCoreImpl_kMaxSleepModeDisallowanceGrants) {
            LTLOG_SLEEPMODE("sleepmode.disallow", "disallowance %lu issued for 0x%lx; %lu disallowances issued", LT_Pu32(nDisallowance + 1), LT_PLT_SIZE((LT_SIZE)pCallerAddress), LT_Pu32(nTotalDisallowances));
        }
        else {
            LTLOG_SLEEPMODE("sleepmode.disallow", "max disallowances issued; no disallowance for 0x%lx", LT_PLT_SIZE((LT_SIZE)pCallerAddress));
        }
    #endif
    return nDisallowance < LTCoreImpl_kMaxSleepModeDisallowanceGrants ? nDisallowance + 1 : 0;
}

void
LTCoreImpl_ReallowSleepMode(u32 disallowanceGrant) {
#if !LTCORE_LOG_SLEEP_MODE_ACTIVITY
    if (disallowanceGrant) {
        disallowanceGrant--;
        if (disallowanceGrant < LTCoreImpl_kMaxSleepModeDisallowanceGrants) {
            LT_SIZE nMask = LTKDisableInterrupts();
            if (s_coreImpl.sleepModeDisallowances[disallowanceGrant].pCallerAddress) {
                s_coreImpl.sleepModeDisallowances[disallowanceGrant].pCallerAddress = NULL;
                s_coreImpl.sleepModeDisallowanceCount--;
            }
            LTKEnableInterrupts(nMask);
        }
    }
#else
    u32 nTotalDisallowances = 0;
    bool bInvalid = true;
    if (disallowanceGrant) {
        disallowanceGrant--;
        if (disallowanceGrant < LTCoreImpl_kMaxSleepModeDisallowanceGrants) {
            LT_SIZE nMask = LTKDisableInterrupts();
            if (s_coreImpl.sleepModeDisallowances[disallowanceGrant].pCallerAddress) {
                s_coreImpl.sleepModeDisallowances[disallowanceGrant].pCallerAddress = NULL;
                s_coreImpl.sleepModeDisallowanceCount--;
                bInvalid = false;
                nTotalDisallowances = s_coreImpl.sleepModeDisallowanceCount;
            }
            LTKEnableInterrupts(nMask);
        }
        disallowanceGrant++;
    }
    if (bInvalid) { LTLOG_SLEEPMODE("sleepmode.reallow", "reallow failed for invalid disallowance grant %lu", LT_Pu32(disallowanceGrant)); }
    else { LTLOG_SLEEPMODE("sleepmode.reallow", "disallowance grant %lu revoked (reallowed) by 0x%lx; %lu disallowances issued", LT_Pu32(disallowanceGrant), LT_PLT_SIZE((LT_SIZE)lt_returnaddress()), LT_Pu32(nTotalDisallowances)); }
#endif
}

bool
LTCoreImpl_EnumerateSleepModeDisallowanceGrants(LTCore_SleepModeDisallowanceGrantEnumProc * pEnumProc, void * pClientData) {
#if !LTCORE_LOG_SLEEP_MODE_ACTIVITY
    if (pEnumProc) {
        LTCoreImpl_SleepModeDisallowanceGrant * pGrants = lt_malloc(sizeof(s_coreImpl.sleepModeDisallowances));
        if (pGrants) {
            LT_SIZE nMask = LTKDisableInterrupts();
            lt_memcpy(pGrants, s_coreImpl.sleepModeDisallowances, sizeof(s_coreImpl.sleepModeDisallowances));
            LTKEnableInterrupts(nMask);
            for (u32 i = 0;  i < LTCoreImpl_kMaxSleepModeDisallowanceGrants; i++) {
                if (s_coreImpl.sleepModeDisallowances[i].pCallerAddress) {
                    if (! (*pEnumProc)(i+1, s_coreImpl.sleepModeDisallowances[i].pCallerAddress, pClientData)) {
                        lt_free(pGrants);
                        return false;
                    }
                }
            }
            lt_free(pGrants);
        }
    }
    return true;
#else
    bool bNotAborted = true;
    if (pEnumProc) {
        LTCoreImpl_SleepModeDisallowanceGrant * pGrants = lt_malloc(sizeof(s_coreImpl.sleepModeDisallowances));
        if (pGrants) {
            LT_SIZE nMask = LTKDisableInterrupts();
            lt_memcpy(pGrants, s_coreImpl.sleepModeDisallowances, sizeof(s_coreImpl.sleepModeDisallowances));
            LTKEnableInterrupts(nMask);
            for (u32 i = 0;  i < LTCoreImpl_kMaxSleepModeDisallowanceGrants; i++) {
                if (s_coreImpl.sleepModeDisallowances[i].pCallerAddress) {
                    LTLOG_SLEEPMODE("sleepmode.enum", "disallowance grant %2lu: issued to 0x%lx",
                        LT_Pu32(i+1), LT_PLT_SIZE((LT_SIZE)s_coreImpl.sleepModeDisallowances[i].pCallerAddress));
                }
                else {
                    LTLOG_SLEEPMODE("sleepmode.enum", "disallowance grant %2lu: not issued", LT_Pu32(i+1));
                }
                if (s_coreImpl.sleepModeDisallowances[i].pCallerAddress) {
                    if (bNotAborted) {
                        bNotAborted = (*pEnumProc)(i+1, s_coreImpl.sleepModeDisallowances[i].pCallerAddress, pClientData);
                    }
                }
            }
            lt_free(pGrants);
        }
    }
    return bNotAborted;
#endif
}

static void LTCoreImpl_OnSleepAction(LTCore_SleepActionEventProc * pEventProc, void * pClientData) {
    if (s_coreImpl.hSleepModeEvent) LTEventImpl_RegisterForEvent(s_coreImpl.hSleepModeEvent, pEventProc, NULL, pClientData, false);
}

static void LTCoreImpl_NoSleepAction(LTCore_SleepActionEventProc * pEventProc) {
    if (s_coreImpl.hSleepModeEvent) LTEventImpl_UnregisterFromEvent(s_coreImpl.hSleepModeEvent, pEventProc);
}

static void
LTCoreImpl_OnLTKEnterIdle(void) {
    /* This is running in ISR context with [nested] interrupts disabled. Be quick about things. */
    /* start the sleepmode idle countdown iff sleepmode mode is currently enabled (as determined by non-zero enterSleepModeIdleDelay
       only do this if sleep is not in progress; if sleep is in progress we'll keep the current trigger time    */
    if (! LTTime_IsZero(s_coreImpl.enterSleepModeIdleDelay)) {
        if (LTAtomic_Load(&s_coreImpl.atomicSleepState) == kLTCoreImpl_SleepActionStateAwake) {
            s_coreImpl.enterSleepModeTriggerTime = LTTime_Add(LTCoreImpl_GetKernelTime(), s_coreImpl.enterSleepModeIdleDelay);
            #if LTCORE_LOG_SLEEP_MODE_ACTIVITY
                //LTCoreImpl_FormatCanonicalTimeString(s_coreImpl.enterSleepModeTriggerTime, s_sleepModeTimeBuffer, sizeof(s_sleepModeTimeBuffer), false);
                //LTLOG_SLEEPMODE("sleepmode.enteridle", "Setting sleepmode trigger.  [kernelTime + delay(%lums)] = TRIGGER AT %s", LT_Pu32(LTTime_GetMilliseconds(s_coreImpl.enterSleepModeIdleDelay)), s_sleepModeTimeBuffer);
            #endif
        }
    }
}

static const char * LTCoreImpl_SleepActionStateToString(LTCoreImpl_SleepActionState state) {
    static const char * stateStrings[kLTCoreImpl_SleepActionStateCount] = { "Awake", "DispatchingGoingToSleep", "GoingToSleepDispatched", "AbortedDispatched", "EngageSleep", "SleepEngaged", "AwakenedDispatched" };
    return (state < kLTCoreImpl_SleepActionStateCount) ? stateStrings[state] : "???";
}


static void
LTCoreImpl_OnLTKIdleTick(void) {
    /* This is running in ISR context with [nested] interrupts disabled. Be quick about things. */
    /* The purpose of this function is to check for and take action when the trigger enter sleep mode countdown timer expires.
       When the countdown timer has expired, this function will either reset the countdown or trigger enter sleep mode.
         The countdown timer will be reset if there is no enter sleep mode thread registered or there are disallowance grants issued,
         otherwise enter sleep mode will be triggered in this function whereby:
           a. The register enter sleep mode thread will have its priority raised to the LTCore highest thread priority
           b. then the enter sleep mode task proc will be queued to the thread.
    */
    if (! LTTime_IsZero(s_coreImpl.enterSleepModeTriggerTime) && LTTime_IsGreaterThanOrEqual(LTCoreImpl_GetKernelTime(), s_coreImpl.enterSleepModeTriggerTime)) {
        /* The enter sleep mode countdown timer has expired.
             Pull the trigger if we have an enter sleep mode thread registered that is in active run state and
             there are no disallowance grants currently issued */
        if (LTThreadImpl_ThreadHandleInActiveRunState(s_coreImpl.hThreadEnterSleepMode) && s_coreImpl.sleepModeDisallowanceCount == 0) {
            if (LTAtomic_CompareAndExchange(&s_coreImpl.atomicSleepState, kLTCoreImpl_SleepActionStateAwake, kLTCoreImpl_SleepActionStateDispatchingGoingToSleep)) {
                //LTLOG_SLEEPMODE("sleepmode.idletick", "Sleep mode triggered.  Engaging GoingToSleep event dispatch!");
                s_coreImpl.enterSleepModeTriggerTime = LTTime_Zero();
                s_coreImpl.preTriggerThreadPriority = LTThreadImpl_SetHighestLTCoreThreadPriorityFromLTKIdleTickISRContext(s_coreImpl.hThreadEnterSleepMode);
                LTThreadImpl_QueueTaskProcIfRequired(s_coreImpl.hThreadEnterSleepMode, &LTCoreImpl_EngagePreSleepModeTaskProc, NULL, NULL);
            }
            else {
                //LTLOG_SLEEPMODE("sleepmode.idletick", "Sleep mode non-trigger state %s. Life continues.", LTCoreImpl_SleepActionStateToString(LTAtomic_Load(&s_coreImpl.atomicSleepState)));
            }
        }
        else {
            /* Reset the trigger */
            s_coreImpl.enterSleepModeTriggerTime = LTTime_IsZero(s_coreImpl.enterSleepModeIdleDelay) ? LTTime_Zero() : LTTime_Add(LTCoreImpl_GetKernelTime(), s_coreImpl.enterSleepModeIdleDelay);
            #if LTCORE_LOG_SLEEP_MODE_ACTIVITY
                u32 nDisallowanceCount = s_coreImpl.sleepModeDisallowanceCount;
                //LTThread hThread = s_coreImpl.hThreadEnterSleepMode;
                if (nDisallowanceCount) {
                   // LTLOG_SLEEPMODE("sleepmode.idletick", "Sleep mode triggered, but disallowed by %lu grant%s",  LT_Pu32(nDisallowanceCount), nDisallowanceCount == 1 ? "" : "s");
                }
                else {
                   // if (hThread) { LTLOG_SLEEPMODE("sleepmode.idletick", "Sleep mode triggered, but EnterSleepMode proc thread %lx %s", LT_PLT_HANDLE(hThread), LTHandle_IsHandleValid(hThread) ? "inactive" : "invalid"); }
                   // else { LTLOG_SLEEPMODE("sleepmode.idletick", "Sleep mode triggered, but no EnterSleepMode proc registered"); }
                }
            #endif
        }
    }
}

static void
LTCoreImpl_OnLTKExitIdle(void) {
    /* cancel the countdown, if we're not in process */
    if (LTAtomic_Load(&s_coreImpl.atomicSleepState) == kLTCoreImpl_SleepActionStateAwake) {
        s_coreImpl.enterSleepModeTriggerTime = LTTime_Zero();
        #if LTCORE_LOG_SLEEP_MODE_ACTIVITY
            if (! LTTime_IsZero(s_coreImpl.enterSleepModeIdleDelay)) {
                //LTLOG_SLEEPMODE("sleepmode.exitidle", "Exiting idle; sleepmode delay trigger canceled");
            }
        #endif
    }
#if LTCORE_LOG_SLEEP_MODE_ACTIVITY
    else {
        LTLOG_SLEEPMODE("sleepmode.exitidle", "sleepmode in process, trigger check continues");
    }
#endif
}

static void LTCoreImpl_SleepActionEventDispatchCompleteProc(LTEvent hEvent, LTArgs * pEventArgs) {
    LT_UNUSED(hEvent);
    LTCore_SleepAction sleepAction = LTArgs_u32At(0, pEventArgs);
    switch (sleepAction) {
        case kLTCore_SleepAction_GoingToSleep:
            if (s_coreImpl.sleepModeDisallowanceCount ||
                ((! LTTime_IsZero(s_coreImpl.softwareSleepWakeupTime)) && LTTime_IsLessThanOrEqual(s_coreImpl.softwareSleepWakeupTime, LTTime_Add(LT_GetCore()->GetKernelTime(), s_coreImpl.minimumSleepDuration))))
            {
                /* a disallowance grant was taken in a GoingToSleep sleep action event proc, or a wakeup timer will fire before the minimum sleep duration, sleep aborted! */
                if (LTAtomic_CompareAndExchange(&s_coreImpl.atomicSleepState, kLTCoreImpl_SleepActionStateGoingToSleepDispatched, kLTCoreImpl_SleepActionStateAbortedDispatched)) {
                    LTLOG_SLEEPMODE("sleepmode.eventcomplete", "Notifying kLTCore_SleepAction_SleepAborted");
                    LTEventImpl_NotifyEvent(s_coreImpl.hSleepModeEvent, kLTCore_SleepAction_SleepAborted, LT_CONSTS64(0));
                }
                else {
                    LTLOG_YELLOWALERT("sleep.evt.logic", "GoingToSleep event complete with disallowances: state %s != GoingToSleepDispatched", LTCoreImpl_SleepActionStateToString(LTAtomic_Load(&s_coreImpl.atomicSleepState)));
                }
            }
            else {
                if (LTAtomic_CompareAndExchange(&s_coreImpl.atomicSleepState, kLTCoreImpl_SleepActionStateGoingToSleepDispatched, kLTCoreImpl_SleepActionStateEngageSleep)) {
                    LTLOG_SLEEPMODE("sleepmode.eventcomplete", "GoingToSleep event complete, queuing EngageSleep");
                    LTThreadImpl_QueueTaskProcIfRequired(s_coreImpl.hThreadEnterSleepMode, &LTCoreImpl_EngageSleepModeTaskProc, NULL, NULL);
                }
                else {
                    LTLOG_YELLOWALERT("sleep.evt.logic", "GoingToSleep event complete: state %s != GoingToSleepDispatched", LTCoreImpl_SleepActionStateToString(LTAtomic_Load(&s_coreImpl.atomicSleepState)));
                }
            }
            break;
        case kLTCore_SleepAction_SleepAborted:
            if (LTAtomic_CompareAndExchange(&s_coreImpl.atomicSleepState, kLTCoreImpl_SleepActionStateAbortedDispatched, kLTCoreImpl_SleepActionStateAwake)) {
                LTThreadImpl_SetPriority(s_coreImpl.hThreadEnterSleepMode, s_coreImpl.preTriggerThreadPriority);
            }
            else {
                LTLOG_YELLOWALERT("sleep.evt.logic", "SleepAborted event complete: state %s != kLTCoreImpl_SleepActionStateAbortedDispatched", LTCoreImpl_SleepActionStateToString(LTAtomic_Load(&s_coreImpl.atomicSleepState)));
            }
            break;
        case kLTCore_SleepAction_AwakenedFromSleep:
            if (LTAtomic_CompareAndExchange(&s_coreImpl.atomicSleepState, kLTCoreImpl_SleepActionStateAwakenedDispatched, kLTCoreImpl_SleepActionStateAwake)) {
                LTThreadImpl_SetPriority(s_coreImpl.hThreadEnterSleepMode, s_coreImpl.preTriggerThreadPriority);
            }
            else {
                LTLOG_YELLOWALERT("sleep.evt.logic", "AwakenedFromSleep event complete: state %s != kLTCoreImpl_SleepActionStateAwakenedDispatched", LTCoreImpl_SleepActionStateToString(LTAtomic_Load(&s_coreImpl.atomicSleepState)));
            }
            break;
        default:
            LTLOG_YELLOWALERT("sleep.evt.logic", "SleepAction event complete: unknown sleep action %d, state %s", (int)sleepAction, LTCoreImpl_SleepActionStateToString(LTAtomic_Load(&s_coreImpl.atomicSleepState)));
            break;
    }
}

static void
LTCoreImpl_EngagePreSleepModeTaskProc(void *pClientData) { LT_UNUSED(pClientData);
    if (LTAtomic_CompareAndExchange(&s_coreImpl.atomicSleepState, kLTCoreImpl_SleepActionStateDispatchingGoingToSleep, kLTCoreImpl_SleepActionStateGoingToSleepDispatched)) {
        s_coreImpl.softwareSleepWakeupTime = LTThreadImpl_GetSoonestFiringThreadWakeupTimerFireTime();
        if ((! LTTime_IsZero(s_coreImpl.softwareSleepWakeupTime)) && LTTime_IsLessThanOrEqual(s_coreImpl.softwareSleepWakeupTime, LTTime_Add(LT_GetCore()->GetKernelTime(), s_coreImpl.minimumSleepDuration))) {
            /* wakeup timer will fire before minimumSleepDuration elapses, don't bother notifying GoingToSleep */
            LTLOG_SLEEPMODE("sleepmode.presleep.bailwake", "WakeupTimer would fire before minimum sleep duration, bailing on sleep");
            LTAtomic_Store(&s_coreImpl.atomicSleepState, kLTCoreImpl_SleepActionStateAwake);
            s_coreImpl.enterSleepModeTriggerTime = LTTime_Zero();
        }
        else {
            LTLOG_SLEEPMODE("sleepmode.presleep.goingtosleep", "Notifying Sleep Action kLTCore_SleepAction_GoingToSleep");
            LTEventImpl_NotifyEvent(s_coreImpl.hSleepModeEvent, kLTCore_SleepAction_GoingToSleep, s_coreImpl.softwareSleepWakeupTime.nNanoseconds);
        }
    }
    else {
        LTLOG_YELLOWALERT("sleepmode.presleep.stateerror", "couldn't state change to GoingToSleepDispatched, curr state is %s", LTCoreImpl_SleepActionStateToString(LTAtomic_Load(&s_coreImpl.atomicSleepState)));
    }
}

static void
LTCoreImpl_EngageSleepModeTaskProc(void *pClientData) { LT_UNUSED(pClientData);

    if (! LTAtomic_CompareAndExchange(&s_coreImpl.atomicSleepState, kLTCoreImpl_SleepActionStateEngageSleep, kLTCoreImpl_SleepActionStateSleepEngaged)) {
        LTLOG_YELLOWALERT("sleepmode.engage", "couldn't state change to SleepEngaged, curr state is %s", LTCoreImpl_SleepActionStateToString(LTAtomic_Load(&s_coreImpl.atomicSleepState)));
        return;
    }

#if LTCORE_LOG_SLEEP_MODE_ACTIVITY
    bool bLogSleepComplete = s_coreImpl.pEnterSleepModeProc ? true : false;
    if (s_coreImpl.pEnterSleepModeProc) {
        //LTLOG_SLEEPMODE("sleepmode.engage", "Entering sleep mode...");
    }
    else {
        //LTLOG_SLEEPMODE("sleepmode.engage", "Can't enter sleepmode - no EnterSleepModeProc!");
    }
#endif

    /* call the enter sleep mode proc; by the time it returns to us we will have entered and exited sleep mode with the time elapsed provided */
    LTTime timeElapsedInSleep = s_coreImpl.pEnterSleepModeProc ? (*s_coreImpl.pEnterSleepModeProc)(s_coreImpl.softwareSleepWakeupTime, s_coreImpl.pEnterSleepModeProcClientData) : LTTime_Zero();

    if (! LTTime_IsZero(timeElapsedInSleep)) {
        /* do the LTK time adjust */
#if LTCORE_LOG_SLEEP_MODE_ACTIVITY
        LTLOG_SLEEPMODE("sleepmode.engage", "Advancing kernel time by %lld ticks", LT_Ps64(timeElapsedInSleep.nNanoseconds / kLTKNanosecondsPerTick));
#endif
        LTKAdvanceNanoseconds(timeElapsedInSleep.nNanoseconds);
    }

#if LTCORE_LOG_SLEEP_MODE_ACTIVITY
    if (bLogSleepComplete) {
        if (LTTime_IsZero(timeElapsedInSleep)) {
            LTLOG_SLEEPMODE("sleepmode.engage", "Exited sleep mode, no kernel time adjustment");
        }
        else {
            LTCoreImpl_FormatCanonicalTimeString(timeElapsedInSleep, s_sleepModeTimeBuffer, sizeof(s_sleepModeTimeBuffer), false);
            LTLOG_SLEEPMODE("sleepmode.engage", "Exited sleep mode, advanced kernel time %ss", s_sleepModeTimeBuffer);
        }
    }
#endif

    if (LTAtomic_CompareAndExchange(&s_coreImpl.atomicSleepState, kLTCoreImpl_SleepActionStateSleepEngaged, kLTCoreImpl_SleepActionStateAwakenedDispatched)) {
        LTLOG_SLEEPMODE("sleepmode.engage", "Notifying kLTCore_SleepAction_AwakenedFromSleep");
        LTEventImpl_NotifyEvent(s_coreImpl.hSleepModeEvent, kLTCore_SleepAction_AwakenedFromSleep, timeElapsedInSleep.nNanoseconds);
    }
    else {
        LTLOG_YELLOWALERT("sleepmode.engage", "couldn't state change to AwakenedDispatch, curr state is %s", LTCoreImpl_SleepActionStateToString(LTAtomic_Load(&s_coreImpl.atomicSleepState)));
    }
}

#if 0
static void
LTCoreImpl_OnConsoleInputEvent(LTCore_ConsoleInputEventProc * pConsoleInputEventProc, void * pClientData) {
    if (pConsoleInputEventProc) {
        LTMutexImpl_Lock(s_mutex);
          if (0 == s_hEvent) s_hEvent = LTEventImpl_CreateEvent(&s_ConsoleInputEventProcArgsDescriptor, &LTLoggerImpl_ConsoleInputEntryEvent, &LTLoggerImpl_ConsoleInputEntryEventComplete);
          if (s_hEvent) LTEventImpl_RegisterForEvent(s_hEvent, pConsoleInputEventProc, pClientData);
        LTMutexImpl_Unlock(s_mutex);
    }
}

static void
LTCoreImpl_NoConsoleInputEvent(LTCore_ConsoleInputEventProc * pConsoleInputEventProc) {
    if (pConsoleInputEventProc) {
        LTMutexImpl_Lock(s_mutex);
        if (s_hEvent && LTEventImpl_UnregisterFromEvent(s_hEvent, pConsoleInputEventProc)) {
            LTHandle_DestroyHandle(s_hEvent);
            s_hEvent = 0;
        }
        LTMutexImpl_Unlock(s_mutex);
    }
}
#endif

static void
LTCoreImpl_SetConsoleCharactersReceivedProc(LTCore_ConsoleCharactersReceivedProc * pCharReceivedProc, LTCore_ConsoleBreakReceivedProc * pBreakReceivedProc, void * pClientData) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTThread hThreadToReceiveConsoleCharacters = 0;
    if (pCharReceivedProc) hThreadToReceiveConsoleCharacters = LTThreadImpl_GetCurrentThread();
    else pClientData = NULL;

    LT_SIZE nMask = LTKDisableInterrupts();
        s_coreImpl.hThreadToReceiveConsoleCharacters = hThreadToReceiveConsoleCharacters;
        s_coreImpl.pConsoleCharactersReceivedProc = pCharReceivedProc;
        s_coreImpl.pConsoleBreakReceivedProc = pBreakReceivedProc;
        s_coreImpl.pConsoleCharactersReceivedProcClientData = pClientData;
        (void)LTAtomic_FetchAdd(&s_coreImpl.nAtomicConsoleCounter, 1);
    LTKEnableInterrupts(nMask);
}

static void
LTCoreImpl_EnableSerialConsole(bool bEnable) {
    if (!bEnable) LTLoggerImpl_Flush();
    if (s_pBSP->EnableSerialConsole) s_pBSP->EnableSerialConsole(bEnable);
}

bool LT_ISR_SAFE
LTCoreImpl_AssertFailed(const char * pFile, int nLine, const char * pTest) {
    LTLoggerImpl_Flush();
    if (pTest && (0 == lt_strcmp(pTest, "redalert"))) {
        LTLoggerImpl_Log(s_pLTLOG_Section, "ltcore",  (kLTCore_LogFlags_LogTypeLog | kLTCore_LogFlags_LogToConsole    |  kLTCore_LogFlags_LogToServer), "red alert reboot initiated from %s(%d)\n", pFile, nLine);
    }
    else {
        LTLoggerImpl_Log(s_pLTLOG_Section,  "assert", kLTCore_LogFlags_LogTypeAssert | (kLTCore_LogFlags_ConsoleStomp | kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_LogToServer), "%s failed. This is not a syntax error in %s(%d)", pTest, pFile, nLine);
    }
    #if 1
        /* DRW 07-Feb-23 : do asserts even in release mode now */
        return s_pBSP->DebugAssertFailed(pFile, nLine, pTest);
    #else
        #ifdef LT_DEBUG
            return s_pBSP->DebugAssertFailed(pFile, nLine, pTest);
        #else
            return false;
        #endif
    #endif
}

typedef struct LTCrashdumpInfo {
    LTCrashdumpHeader    header;
    LTCrashdumpContext   context;
    u32                  nReqThreadState;
    LT_SIZE              spaceLeft;
} LTCrashdumpInfo;

static LTCrashdumpInfo s_crashDumpInfo = { 0 };

static bool LT_ISR_SAFE
LTCoreImplPrivate_CountThreadHandles(LTHandle handle, LTInterface *pInterface, void *privateData, void *clientData) {
    LT_UNUSED(handle);
    LT_UNUSED(pInterface);

    LTCrashdumpInfo     *pDumpInfo = (LTCrashdumpInfo *)clientData;
    LTKThread             *pThread = (LTKThread *)privateData;
    LTThreadImpl            *pImpl = LTThreadImpl_PrivateDataToThreadImpl(privateData);

    if ((pDumpInfo->nReqThreadState == kLTThread_ThreadState_Running && LTKGetRunningThreadPtr() == pThread) ||
        (LTKGetRunningThreadPtr() != pThread && pDumpInfo->nReqThreadState == LTAtomic_Load(&pImpl->nThreadState)))
    {
        LTCrashdumpHeader *pDumpHeader = &pDumpInfo->header;
        u16 nStackDumpSize = (pThread->pStackBottom + pThread->nStackSize) - pThread->pStack;
        LT_SIZE nContextSize = sizeof(LTCrashdumpContext) + pDumpHeader->threadInfoSize + nStackDumpSize;
        if (pDumpInfo->spaceLeft >= pDumpHeader->totalSize + nContextSize)
        {
            pDumpHeader->numContexts++;
            pDumpHeader->totalSize += nContextSize;
        }
    }
    return true; /* continue enumerating */
}

static bool LT_ISR_SAFE
LTCoreImplPrivate_CrashdumpThreadHandles(LTHandle handle, LTInterface *pInterface, void *privateData, void *clientData) {
    LT_UNUSED(handle);
    LT_UNUSED(pInterface);

    LTThreadImpl            *pImpl = LTThreadImpl_PrivateDataToThreadImpl(privateData);
    LTKThread             *pThread = (LTKThread *)privateData;
    LTCrashdumpInfo     *pDumpInfo = (LTCrashdumpInfo *)clientData;

    if ((pDumpInfo->nReqThreadState == kLTThread_ThreadState_Running && LTKGetRunningThreadPtr() == pThread) ||
        (LTKGetRunningThreadPtr() != pThread && pDumpInfo->nReqThreadState == LTAtomic_Load(&pImpl->nThreadState)))
    {
        LTCrashdumpHeader *pDumpHeader = &pDumpInfo->header;
        u16 nStackDumpSize = (pThread->pStackBottom + pThread->nStackSize) - pThread->pStack;
        if (pDumpInfo->spaceLeft >= sizeof(LTCrashdumpContext) + pDumpHeader->threadInfoSize + nStackDumpSize)
        {
            LTCrashdumpContext   *pContext = &pDumpInfo->context;
            pContext->type = kLTCrashdumpContextType_Thread;
            pContext->numData = 0;
            pContext->stackdumpSize = nStackDumpSize;
            pContext->rsvd[0] = 0;
            pContext->rsvd[1] = 0;
            if (pThread->pName) LTStdlibImpl_strncpyTerm(pContext->name, pThread->pName, kLTThread_MaxNameBuff); else pContext->name[0] = '\0';
            pContext->threadInfoAddr = (LT_SIZE)pThread;
            pContext->stackAddr = (LT_SIZE)pThread->pStack;

            s_pCrashdumpWriteCallback((u8 *)pContext, sizeof(*pContext), &pDumpInfo->spaceLeft);
            s_pCrashdumpWriteCallback((u8 *)pThread, pDumpHeader->threadInfoSize, &pDumpInfo->spaceLeft);
            s_pCrashdumpWriteCallback((u8 *)pThread->pStack, nStackDumpSize, &pDumpInfo->spaceLeft);
        }
    }
    return true; /* continue enumerating */
}

/* Order of inclusion for threads in crashdump (storage space may be limited). */
static const u32 s_threadStatesSequenceInDump[] = {
    kLTThread_ThreadState_Running,
    kLTThread_ThreadState_ReadyToRun,
    kLTThread_ThreadState_Sleeping,
    kLTThread_ThreadState_WaitBlocked,
    kLTThread_ThreadState_MutexBlocked,
    kLTThread_ThreadState_TerminatePending,
    kLTThread_ThreadState_Terminated,
    kLTThread_ThreadState_NotStarted
};

static void LT_ISR_SAFE
LTCoreImpl_FaultHandler(const char * pABI, u32 * pInterruptStack, LT_SIZE nInterruptStackSize) {
    /* Fault handler callback to be invoked by a LTK exception handler ONLY */
    if (s_pCrashdumpWriteCallback) {  /* Crash dumps are produced only if callback is registered */
        /* Initialize file header */
        s_crashDumpInfo.header.magic = kLTCrashdump_Partition_Magic;
        s_crashDumpInfo.header.version = kLTCrashdump_Version0;
        s_crashDumpInfo.header.threadInfoSize = sizeof(LTKThread);
        s_crashDumpInfo.header.totalSize = sizeof(LTCrashdumpHeader);
        LTStdlibImpl_strncpyTerm(s_crashDumpInfo.header.abi, pABI, sizeof(s_crashDumpInfo.header.abi));
        LTStdlibImpl_strncpyTerm(s_crashDumpInfo.header.swVersion, s_pCore->GetSoftwareVersion(), sizeof(s_crashDumpInfo.header.swVersion));
        /* Begin write */
        s_pCrashdumpWriteCallback(NULL, 0, &s_crashDumpInfo.spaceLeft);
        /* Calculate total dump size of all contexts starting with interrupt and then threads (in order of s_threadStatesSequenceInDump) */
        if (pInterruptStack) {
            /* Determine interrupt dump size (Context + Stack) */
            LT_SIZE nContextSize = sizeof(LTCrashdumpContext) + nInterruptStackSize;
            if (s_crashDumpInfo.spaceLeft >= s_crashDumpInfo.header.totalSize + nContextSize) {
                s_crashDumpInfo.header.numContexts = 1;
                s_crashDumpInfo.header.totalSize += nContextSize;
            }
        }
        for (unsigned i = 0; i < sizeof(s_threadStatesSequenceInDump) / sizeof(s_threadStatesSequenceInDump[0]); i++) {
            s_crashDumpInfo.nReqThreadState = s_threadStatesSequenceInDump[i];
            LTHandle_FORCRASHDUMPONLY_EnumerateHandlesForInterface((LTInterface *)iThread, &LTCoreImplPrivate_CountThreadHandles, &s_crashDumpInfo);
        }
        /* Write file header */
        s_pCrashdumpWriteCallback((u8 *)&s_crashDumpInfo.header, sizeof(s_crashDumpInfo.header), &s_crashDumpInfo.spaceLeft);
        if (pInterruptStack) {
            LTCrashdumpContext * pContext = &s_crashDumpInfo.context;
            pContext->type = kLTCrashdumpContextType_Interrupt;
            pContext->numData = 0;
            pContext->stackdumpSize = nInterruptStackSize;
            pContext->rsvd[0] = 0;
            pContext->rsvd[1] = 0;
            LTStdlibImpl_strncpyTerm(pContext->name, "Interrupt", kLTThread_MaxNameBuff);
            pContext->threadInfoAddr = (LT_SIZE)NULL;
            pContext->stackAddr = (LT_SIZE)pInterruptStack;
            /* Save context data and stack */
            s_pCrashdumpWriteCallback((u8 *)pContext, sizeof(*pContext), &s_crashDumpInfo.spaceLeft);
            s_pCrashdumpWriteCallback((u8 *)pInterruptStack, nInterruptStackSize, &s_crashDumpInfo.spaceLeft);
        }
        /* Write threads data */
        for (unsigned i = 0; i < sizeof(s_threadStatesSequenceInDump) / sizeof(s_threadStatesSequenceInDump[0]); i++) {
            s_crashDumpInfo.nReqThreadState = s_threadStatesSequenceInDump[i];
            LTHandle_FORCRASHDUMPONLY_EnumerateHandlesForInterface((LTInterface *)iThread, &LTCoreImplPrivate_CrashdumpThreadHandles, &s_crashDumpInfo);
        }
        /* End write */
        s_pCrashdumpWriteCallback(NULL, 0, &s_crashDumpInfo.spaceLeft);
    }
}

static void
LTCoreImpl_RegisterCrashdumpWriteCallback(LTCore_CrashdumpWriteCallback * pCallback) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    s_pCrashdumpWriteCallback = pCallback;
}

static bool
LTCoreImpl_HandleInDestroy(LTHandle handle) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    return LTHandle_IsHandleValid(handle);
}

static void
LTCoreImpl_TerminateLT(int nExitCode) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (LTAtomic_CompareAndExchange(&s_nRunCount, 2, 3)) { // 2 is running, 3 is shutting down
        s_nRetVal = nExitCode;
        if (s_coreImpl.bHostedMode) {
            LTCoreImpl_CloseDependentLibraries();
            LTThreadImpl_TerminateAllNonSystemThreads();
            LTThreadImpl_Fini(); /* Finalize threads before events so all of the threads can destroy their events */
            LTEventImpl_Fini();
            LTHandle_DeleteHandleWithoutDestroy(s_coreImpl.hThreadCore);
            s_coreImpl.hThreadCore = 0;
            LTLibraryManager_Fini();
            LTCoreImpl_LTShutdownUninitialize();
        }
        else {
            LTThreadImpl_Terminate(s_coreImpl.hThreadCore);
        }
    }
}

bool
LTCoreImpl_IsLTShuttingDown(void) {
    return (3 == LTAtomic_Load(&s_nRunCount));
}

/* ____________________________________________________________________________
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * |||||                                                                  |||||
 * |||||  ^^^^^^^             ABOVE THIS BANNER ARE              ^^^^^^^  |||||
 * |||||  ^^^^^^^  LTCore Interface Function Implementations     ^^^^^^^  |||||
 * |||||__________________________________________________________________|||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * |||||                                                                  |||||
 * |||||  vvvvvvv            BELOW THIS BANNER ARE               vvvvvvv  |||||
 * |||||  vvvvvvv           LTCore Library Bindings              vvvvvvv  |||||
 * |||||  vvvvvvv       LTCore Exported Object Bindings          vvvvvvv  |||||
 * |||||__________________________________________________________________|||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 */

/***************************
 ***************************
 * LTCore Library Bindings *
 ***************************/
/*
 * Before defining the LTCore interface with the define_LTLIBRARY_ROOT_INTERFACE macro,
 * disable the LTLIBRARY_DEFINE_EXPORT_BINDING macro that is incorporated
 * within it.  This is so LTCore doesn't define the standard LTLibrary Open/CloseLibrary()
 * export bindings because unlike all other LTLibraries, LTCore implements OpenLibrary()
 * and CloseLibrary and manually defines its own unique export bindings for ltrun. */
#undef LTLIBRARY_DEFINE_EXPORT_BINDING
#define LTLIBRARY_DEFINE_EXPORT_BINDING(cblName, libName, version)

/* Now define the LTCore interface */
define_LTLIBRARY_ROOT_INTERFACE(LTCore, 0, 0, 1)
    .GetSoftwareVersion                 = &LTCoreImpl_GetSoftwareVersion,
    .IsDeveloperBuild                   = &LTCoreImpl_IsDeveloperBuild,
    .GetBuildTime                       = &LTCoreImpl_GetBuildTime,

    .GetKernelTime                      = &LTCoreImpl_GetKernelTime,
    .GetKernelTimeResolution            = &LTCoreImpl_GetKernelTimeResolution,
    .FormatCanonicalTimeString          = &LTCoreImpl_FormatCanonicalTimeString,

    .GetClockTimeUTC                    = &LTCoreImpl_GetClockTimeUTC,
    .SetClockTimeBaseUTC                = &LTCoreImpl_SetClockTimeBaseUTC,
    .GetClockTimeBaseUTC                = &LTCoreImpl_GetClockTimeBaseUTC,
    .GetClockTimeUTCKernelOffset        = &LTCoreImpl_GetClockTimeUTCKernelOffset,
    .GetApproximateClockTimeUTC         = &LTCoreImpl_GetApproximateClockTimeUTC,
    .SetApproximateClockTimeBaseUTC     = &LTCoreImpl_SetApproximateClockTimeBaseUTC,

    .OpenLibrary                        = &LTCoreImpl_OpenLibrary,
    .CloseLibrary                       = &LTCoreImpl_CloseLibrary,
    .SetLibraryHook                     = &LTCoreImpl_SetLibraryHook,
    .GetLibraryInterface                = &LTCoreImpl_GetLibraryInterface,
    .GetLibraryBuildVersionString       = &LTLibraryManager_GetLibraryBuildVersionString,
    .GetLTCoreLibraryBuildVersion       = &LTCoreImpl_GetLTCoreLibraryBuildVersion,
    .IsLibraryOpen                      = &LTLibraryManager_IsLibraryOpen,
    .GetLibrarySnapshot                 = &LTLibraryManager_GetLibrarySnapshot,
    .SnapshotOpenLibraries              = &LTLibraryManager_SnapshotOpenLibraries,
    .EnumerateInstalledLibraries        = &LTLibraryManager_EnumerateInstalledLibraries,

    .Alloc                              = &LTCoreImpl_Alloc,
    .AllocFromRegion                    = &LTCoreImpl_AllocFromRegion,
    .ReAlloc                            = &LTCoreImpl_ReAlloc,
    .Free                               = &LTCoreImpl_Free,
    .GetAllocSize                       = &LTCoreImpl_GetAllocSize,
    .GetTotalSystemRAM                  = &LTCoreImpl_GetTotalSystemRAM,
    .GetAvailableSystemRAM              = &LTCoreImpl_GetAvailableSystemRAM,
    .GetSystemRAMLowWatermark           = &LTCoreImpl_GetSystemRAMLowWatermark,
    .GetLargestAvailableBlockInRAM      = &LTCoreImpl_GetLargestAvailableBlockInRAM,
    .GetCurrentRAMAllocationCount       = &LTCoreImpl_GetCurrentRAMAllocationCount,
    .GetMaxRAMAllocationCount           = &LTCoreImpl_GetMaxRAMAllocationCount,
    .EnumerateHeapAllocatedBlockInfo    = &LTCoreImpl_EnumerateHeapAllocatedBlockInfo,

    .SetHeapTag                         = &LTCoreImpl_SetHeapTag,

    .SnapshotMemstat                    = &LTCoreImpl_SnapshotMemstat,
    .FormatCanonicalMemstatString       = &LTCoreImpl_FormatCanonicalMemstatString,

    .GetNamedMemoryRegion               = &LTCoreImpl_GetNamedMemoryRegion,

    .CreateEvent                        = &LTEventImpl_CreateEvent,
    .CreateThread                       = &LTThreadImpl_CreateThread,
    .GetCurrentThreadObject             = &LTOThreadImpl_GetCurrentThread,

    .MacroCreateObject                  = &LTCoreImpl_MacroCreateObject,
    .MacroCreateDeviceObject            = &LTCoreImpl_MacroCreateDeviceObject,
    .MacroCreateDriverObject            = &LTCoreImpl_MacroCreateDriverObject,
    .MacroCreateDriverObjectForUnitTest = &LTCoreImpl_MacroCreateDriverObjectForUnitTest,
    .DestroyObject                      = &LTCoreImpl_DestroyObject,
    .AddObjectRef                       = &LTCoreImpl_AddObjectRef,
    .RemoveObjectRef                    = &LTCoreImpl_RemoveObjectRef,
    .GetDeviceAndUnitIndicesFromDeviceOrDriverObject = &LTCoreImpl_GetDeviceAndUnitIndicesFromDeviceOrDriverObject,
    .MockObject                         = &LTCoreImpl_MockObject,
    .UnmockObject                       = &LTCoreImpl_UnmockObject,

    .CreateHandle                       = &LTHandle_CreateHandle,
    .DestroyHandle                      = &LTHandle_DestroyHandle,
    .IsHandleValid                      = &LTHandle_IsHandleValid,
    .ReserveHandlePrivateData           = &LTHandle_ReservePrivateData,
    .ReleaseHandlePrivateData           = &LTHandle_ReleasePrivateData,
    .GetHandlePrivateData               = &LTHandle_GetPrivateData,
    .GetHandleInterface                 = &LTHandle_GetHandleInterface,
    .GetNameCheckedHandleInterface      = &LTHandle_GetNameCheckedHandleInterface,
    .GetHandleInterfaceName             = &LTHandle_GetHandleInterfaceName,
    .GetHandleLibrary                   = &LTHandle_GetHandleLibrary,
    .GetHandleReservationCount          = &LTHandle_GetHandleReservationCount,
    .GetHandleStateString               = &LTHandle_GetHandleStateString,
    .GetHandlesByInterface              = &LTHandle_GetCreatedHandlesByInterface,
    .GetHandlesByInterfaceName          = &LTHandle_GetCreatedHandlesByInterfaceName,
    .GetHandleCount                     = &LTHandle_GetHandleCount,
    .GetTotalHandleBytesOverhead        = &LTHandle_GetTotalHandleBytesOverhead,

    .CreateArgs                         = &LTCoreImpl_CreateArgs,
    .CreateArgsFromDescriptor           = &LTCoreImpl_CreateArgsFromDescriptor,
    .CreateArgsFrom_lt_va_list          = &LTCoreImpl_CreateArgsFrom_lt_va_list,
    .UpdateArgsFrom_lt_va_list          = &LTCoreImpl_UpdateArgsFrom_lt_va_list,
    .TrimArgs                           = &LTCoreImpl_TrimArgs,
    .DestroyArgs                        = &LTCoreImpl_DestroyArgs,

    .ReadResourceValue                  = &LTResourceTreeImpl_ReadResourceValue,
    .FindResourceOffset                 = &LTResourceTreeImpl_FindResourceOffset,
    .CountResourceChildren              = &LTResourceTreeImpl_CountResourceChildren,
    .ResourceTypeToString               = &LTResourceTreeImpl_ResourceTypeToString,
    .ResourceValueToString              = &LTResourceTreeImpl_ResourceValueToString,

    .InsideInterruptContext             = &LTKInsideInterruptContext,
    .InterruptsAreDisabled              = &LTKInterruptsAreDisabled,
    .Disable                            = &LTKDisableInterrupts,
    .Enable                             = &LTKEnableInterrupts,
    .SetInterruptVector                 = &LTKSetInterruptVector,
    .SetInterruptPriority               = &LTKSetInterruptPriority,

    .GetArgc                            = &LTCoreImpl_GetArgc,
    .GetArgv                            = &LTCoreImpl_GetArgv,

    .SetEnterSleepModeProc                              = &LTCoreImpl_SetEnterSleepModeProc,
    .SetEnterSleepModeIdleDelayAndMinimumSleepDuration  = &LTCoreImpl_SetEnterSleepModeIdleDelayAndMinimumSleepDuration,
    .DisallowSleepMode                                  = &LTCoreImpl_DisallowSleepMode,
    .ReallowSleepMode                                   = &LTCoreImpl_ReallowSleepMode,
    .EnumerateSleepModeDisallowanceGrants               = &LTCoreImpl_EnumerateSleepModeDisallowanceGrants,
    .OnSleepAction                                      = &LTCoreImpl_OnSleepAction,
    .NoSleepAction                                      = &LTCoreImpl_NoSleepAction,

    .Log                                = &LTLoggerImpl_Log,
    .LogV                               = &LTLoggerImpl_LogV,
    .Trace                              = &LTLoggerImpl_Trace,
    .TraceAddStreams                    = &LTLoggerImpl_TraceAddStreams,
    .TraceRemoveStreams                 = &LTLoggerImpl_TraceRemoveStreams,
    .TraceSetStreamEnabled              = &LTLoggerImpl_TraceSetStreamEnabled,
    .TraceKnownStreams                  = &LTLoggerImpl_TraceKnownStreams,
    .SetLogHookFunction                 = &LTLoggerImpl_SetLogHookFunction,
    .SetTraceHookFunction               = &LTLoggerImpl_SetTraceHookFunction,

    .ConsolePrint                       = &LTLoggerImpl_ConsolePrint,
    .ConsolePrintV                      = &LTLoggerImpl_ConsolePrintV,
    .ConsolePutChars                    = &LTLoggerImpl_ConsolePutChars,
    .ConsolePutString                   = &LTLoggerImpl_ConsolePutString,
    .ConsoleStomp                       = &LTLoggerImpl_ConsoleStomp,
    .ConsoleStompV                      = &LTLoggerImpl_ConsoleStompV,
    .ConsoleStompChar                   = &LTConsoleConnector_ConsoleStompChar,
    .ConsoleStompChars                  = &LTConsoleConnector_ConsoleStompChars,
    .ConsoleStompString                 = &LTLoggerImpl_ConsoleStompString,
    .FlushConsoleOutput                 = &LTLoggerImpl_Flush,

    .SetConsoleCharactersReceivedProc   = &LTCoreImpl_SetConsoleCharactersReceivedProc,
    .EnableSerialConsole                = &LTCoreImpl_EnableSerialConsole,

    .AssertFailed                       = &LTCoreImpl_AssertFailed,
    .DebugBreak                         = &LTKDebugBreak,

    .HandleInDestroy                    = &LTCoreImpl_HandleInDestroy,

    .GetLTStdlib                        = &LTCoreImpl_GetLTStdlib,

    .TerminateLT                        = &LTCoreImpl_TerminateLT,
    .IsLTShuttingDown                   = &LTCoreImpl_IsLTShuttingDown,
    .RegisterCrashdumpWriteCallback     = &LTCoreImpl_RegisterCrashdumpWriteCallback
};

define_LTTRACE_STREAMS((mem)(sched)(mutex)(proc)(timer)(event));

/***************************************
 ***************************************
 * Finally, export my library interfaces
 ***************************************/
LTLIBRARY_EXPORT_INTERFACES(
  LTCore,
    (ILTEvent)
    (LTMutexImpl)
    (ILTThread)
    (LTStdlib)
    (LTOThreadImpl)
    (LTMonitorImpl)
    (LTSpinLockImpl)
    (LTConsoleConnectorImpl)
)

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  11-Oct-19   augustus    created
 *  24-Nov-19   augustus    added GenRandomBytes()
 *  01-Dec-19   augustus    added private string functions for LTString
 *  19-Dec-19   augustus    added StringVFormatString
 *  20-Jan-20   augustus    added pSection and pTag to logging functions
 *  24-Feb-20   augustus    normalized boot sequence to platform independent; moved here to CoreImplBase;
 *                          new Pre-Genesis thread to init LT, query genesis lib for requested stack size,
 *                          and to create Genesis thread with requested stack size for Genesis library Run()
 *  06-Apr-20   augustus    LTCore::ConsoleLog() (used by LT_CONSOLELOG macro) sends LogFlags 0 to bypass observer notification
 *  30-Apr-20   augustus    re-created from LTCoreImplBase.cpp in C
 *  21-May-20   augustus    added SpinLockAtomic functions
 *  05-Aug-20   augustus    implemented LTHandle
 *  12-Aug-20   augustus    converted all LTObjects over to LTHandle
 *  14-Aug-20   augustus    made LTStdlib interface
 *  19-Aug-20   augustus    added LTLIBRARY_EXPORT_INTERFACES, GetNameCheckedHandleInterface
 *  26-Aug-20   augustus    added ILTEvent and InitializeExportedInterfaces
 *  27-Aug-20   augustus    moved out interface definition blocks into their proper .c files
 *  06-Sep-20   augustus    threads now queue task procs; DestroyHandle no longer recursive
 *  30-Sep-20   caligula    use updated CreateString functions
 *  11-Oct-20   augustus    LTLogManager is now LTLoggerImpl
 *  16-Oct-20   augustus    added local time, time zone, and calendar time support
 *  20-Nov-20   augustus    added GetHandleInterfaceName
 *  26-Nov-20   augustus    moved console input callback state variables into s_coreImpl and protect via atomic counter
 *  05-Dec-20   augustus    added GetSystemRAMLowWatermark
 *  17-Dec-20   augustus    added ConsolePrintV
 *  30-Dec-20   augustus    implemented TerminateLT
 *  23-Jan-21   augustus    added SnapshotOpenLibraries and GetLibrarySnapshot
 *  14-Feb-21   augustus    added ConsoleStomp and ConsoleStompV
 *  20-Mar-21   augustus    filter out ch < 32 and > 127 from ISR, except for 8,9,10,13,27
 *  22-Jul-21   augustus    got rid of spinlocks
 *  08-Sep-21   augustus    full complement of ConsoleStomp functions
 *  19-Sep-21   augustus    invoke the debugger when ctrl-c is pressed
 *  11-Dec-21   tiberius    renamed string and pointer arrays, removed U32 arrays
 *  23-Dec-21   augustus    added GetLibraryBuildVersionString
 *  18-Jan-22   tiberius    added LTList
 *  28-Feb-22   augustus    added LTCoreImpl_FormatCanonicalTimeString
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 *  07-Feb-23   augustus    do asserts even in release mode now
 *  31-Jul-23   augustus    added CreateObject, DestroyObject
 *  03-Aug-23   augustus    updated for new atomics
 *  26-Jan-24   nerva       added CountResourceChildren, ResourceTypeToString, ResourceValueToString
 *  08-Mar-24   augustus    reworked heap tracking to use LTCoreImpl_HeapChaLTCOREIMPL_LTDRIVER_PREFIXngeCallback
 *  31-Mar-24   augustus    resurrected SpinLocks, because if there ever was a day for resurrection
 *  22-May-24   augustus    added bIncludeBrackets to FormatCanonicalTimeString
 *  26-Jun-24   augustus    added low power standby mode functions
 *  10-Apr-25   augustus    renamed standby mode to sleep mode
 *  03-Apr-26   augustus    added lt_createdeviceobject support
 *  05-Apr-26   augustus    added GetLTCoreLibraryBuildVersion
 *  27-Apr-26   augustus    added GetNamedMemoryRegion
 */
