/******************************************************************************
 * lt/source/core/LTCoreImpl.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_CORE_LTCOREIMPL_H
#define ROKU_LT_SOURCE_LT_CORE_LTCOREIMPL_H

#include <lt/core/LTCore.h>
#include <lt/core/bsp/LTCoreBSP.h>
#include "LTKernel.h"
#include "LTCoreDebugHelpers.h"

#include <lt/device/config/LTDeviceKonfig.h>
    /* LTCore now depends on LTDeviceKonfig, but it's guaranteed to be built for every build target, except for
       tools builds, so we always check s_coreImpl.pDeviceKonfig is non-NULL before accessing it!     */

LT_EXTERN_C_BEGIN

#if LTCORE_ASSERT_IF_ISR_CALLS_NON_LT_ISR_SAFE_FUNCTION
#define LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT()   LT_ASSERT(! LTKInsideInterruptContext())
#else
#define LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT()
#endif

/***************************************************
 * LTCore library private implementation constants */
enum {
    kLTCoreImpl_MaxDeviceTagNameBufferSize  = (((kLTLibrary_MaxNameBufferSize >> 2) + 7) & ~7),
    kLTCoreImpl_MaxDeviceTagNameLen         = kLTCoreImpl_MaxDeviceTagNameBufferSize - 1
};

enum {
    kLTObjectFlags_AllocFree               = (1 << 0),
    kLTObjectFlags_LibraryRefCounted       = (1 << 1),
    kLTObjectFlags_PseudoObject            = (1 << 2),
    kLTObjectFlags_SingletonInConstruction = (1 << 3),
    kLTObjectFlags_SingletonInDestruction  = (1 << 4),
    kLTObjectFlags_UnitNumberShift         = 20,         /* bits 20-23 for unit   index + 1, valid values 1-15, 0 means not device or driver object, 1 means index 0... */
    kLTObjectFlags_UnitNumberMask          = (0x00F00000),
    kLTObjectFlags_DeviceNumberShift       = 24,         /* bits 24-31 for device index + 1, valid values 1-255, 0 means not device or driver object, 1 means index 0... */
    kLTObjectFlags_DeviceNumberMask        = (0xFF000000)
};

#define LTOBJECT_GETDEVICENUMBER(object)                ((object->reserved & kLTObjectFlags_DeviceNumberMask) >> kLTObjectFlags_DeviceNumberShift)
#define LTOBJECT_GETUNITNUMBER(object)                  ((object->reserved & kLTObjectFlags_UnitNumberMask) >> kLTObjectFlags_UnitNumberShift)
#define LTOBJECT_SETDEVICENUMBER(object, deviceNumber) *((u32 *)(&object->reserved)) = ((object->reserved & ~kLTObjectFlags_DeviceNumberMask) | ((deviceNumber & 0xFF) << kLTObjectFlags_DeviceNumberShift))
#define LTOBJECT_SETUNITNUMBER(object, unitNumber)     *((u32 *)(&object->reserved)) = ((object->reserved & ~kLTObjectFlags_UnitNumberMask)   | ((unitNumber & 0x0F)   << kLTObjectFlags_UnitNumberShift))

/** Get a HandleRecord given a handle ID */
typedef struct LTHandleRecord LTHandleRecord;

/******************************
 SleepMode Disallowance Records */
typedef struct LTCoreImpl_SleepModeDisallowanceGrant {
    void *pCallerAddress;
} LTCoreImpl_SleepModeDisallowanceGrant;

enum { LTCoreImpl_kMaxSleepModeDisallowanceGrants = kLTCore_MaxSleepModeModeDisallowanceGrants };

enum {
    kLTCoreImpl_SleepActionEventTimeoutMS = 1000
};

typedef_LTENUM_SIZED(LTCoreImpl_SleepActionState, u32) {
    kLTCoreImpl_SleepActionStateAwake = 0,
    kLTCoreImpl_SleepActionStateDispatchingGoingToSleep,
    kLTCoreImpl_SleepActionStateGoingToSleepDispatched,
    kLTCoreImpl_SleepActionStateAbortedDispatched,
    kLTCoreImpl_SleepActionStateEngageSleep,
    kLTCoreImpl_SleepActionStateSleepEngaged,
    kLTCoreImpl_SleepActionStateAwakenedDispatched,
    kLTCoreImpl_SleepActionStateCount                    /* used for counting number of state enums only; do not move this enum from last position, or set other enum values explicitly, except first which must be 0 */
};

/***************************************************
 * LTCore trace helpers                            */

declare_LTTRACE_STREAM(mem);
declare_LTTRACE_STREAM(proc);
declare_LTTRACE_STREAM(timer);
#define LTTRACE_MEM_OP(op, value) LTTRACE_NUMERIC(mem, ((u32)(value) << 24) | ((op) & 0xff))

enum {
    kLTTrace_MemOp_Free  = -1,
    kLTTrace_MemOp_Error = 0,
    kLTTrace_MemOp_Alloc = 1,

    kLTTrace_MemOp_Alloc_TaskProcRecord = 16,
    kLTTrace_MemOp_Alloc_TimerRecord    = 17,
    kLTTrace_MemOp_Free_TaskProcRecord  = 18,
    kLTTrace_MemOp_Free_TimerRecord     = 19,
};

/**********************************************
 * LTCore library private implementation data */
typedef struct LTCoreImpl {
    int                                     argc;
    const char **                           argv;
    LTTimeBase                              timebaseUTC;
    LTTimeBase                              timebaseApproximateUTC;
    LTThread                                hThreadCore;
    LTThread                                hThreadGenesis;
    LTLibrary *                             pGenesisLibrary;
    LTCore_ConsoleCharactersReceivedProc *  pConsoleCharactersReceivedProc;
    LTCore_ConsoleBreakReceivedProc *       pConsoleBreakReceivedProc;
    void *                                  pConsoleCharactersReceivedProcClientData;
    LTThread                                hThreadToReceiveConsoleCharacters;
    LTAtomic                                nAtomicConsoleCounter;
    /*******************************
      SleepMode Mode State Variables */
    LTTime                                  enterSleepModeIdleDelay;
    LTTime                                  minimumSleepDuration;
    LTTime                                  enterSleepModeTriggerTime;
    LTTime                                  softwareSleepWakeupTime;
    LTCore_EnterSleepModeProc *             pEnterSleepModeProc;
    void *                                  pEnterSleepModeProcClientData;
    LTAtomic                                atomicSleepState;
    LTThread                                hThreadEnterSleepMode;
    u32                                     sleepModeDisallowanceCount;
    LTCoreImpl_SleepModeDisallowanceGrant   sleepModeDisallowances[LTCoreImpl_kMaxSleepModeDisallowanceGrants];
    LTEvent                                 hSleepModeEvent;
    u8                                      preTriggerThreadPriority;
    u8                                      reserved[3]; /* padding */
    /**********************************
      Regularly Scheduled Programming */
    LTDeviceKonfig *                        pDeviceKonfig;
    u32                                     nDeviceTagNameLen;
    char                                    deviceTagName[kLTCoreImpl_MaxDeviceTagNameBufferSize];
    bool                                    bHostedMode;
} LTCoreImpl;

/*************************************************
 * LTCore library private utility functions      */
LTCoreImpl *      LTCoreImpl_GetLTCoreImpl(void);
const LTCoreBSP * LTCoreImpl_GetLTCoreBSP(void);
bool              LTCoreImpl_IsLTShuttingDown(void);
const char *      LTCoreImpl_NumToStaticString(u32 num);

/* ____________________________________
   ____________________________________
   LTCore public interface functions  /

   ________________________________________________________
   Functions dealing with kernel time and UTC clock time */
LTTime LTCoreImpl_GetKernelTime(void) LT_ISR_SAFE;
LTTime LTCoreImpl_GetKernelTimeResolution(void) LT_ISR_SAFE;
int    LTCoreImpl_FormatCanonicalTimeString(LTTime time, char * pStringBuff, u32 nBuffSize, bool bIncludeBrackets) LT_ISR_SAFE;
LTTime LTCoreImpl_GetClockTimeUTC(void) LT_ISR_SAFE;
LTTime LTCoreImpl_GetClockTimeUTCKernelOffset(void) LT_ISR_SAFE;

/*  ______________________________________
    Functions dealing with LT Libraries */
LTLibrary *         LTCoreImpl_OpenLibrary(const char * pLibraryName);
void                LTCoreImpl_CloseLibrary(LTLibrary * pLibrary);
LTInterface *       LTCoreImpl_GetLibraryInterface(LTLibrary * pLibrary, const char * pInterfaceName);

/*  ___________________________________________
    Functions dealing with memory allocation */
void *              LTCoreImpl_Alloc(LT_SIZE nBytes LT_CALLSITE_FUNCTION_PARAMETER);
void *              LTCoreImpl_ReAlloc(void * pMem, LT_SIZE nBytes LT_CALLSITE_FUNCTION_PARAMETER);
void                LTCoreImpl_Free(void * pMem LT_CALLSITE_FUNCTION_PARAMETER);
LT_SIZE             LTCoreImpl_GetTotalSystemRAM(void);
LT_SIZE             LTCoreImpl_GetAvailableSystemRAM(void);
LT_SIZE             LTCoreImpl_GetSystemRAMLowWatermark(void);
u64                 LTCoreImpl_SnapshotMemstat(void);
int                 LTCoreImpl_FormatCanonicalMemstatString(u64 memstat, char * pStringBuff, u32 nBuffSize, bool bIncludeBrackets);

/* Used by other elements of LTCore */
LTArgs *            LTCoreImpl_CreateArgsFromDescriptor(LTArgsDescriptor * pDescriptor);
LTArgs *            LTCoreImpl_CreateArgsFrom_lt_va_list(LTArgsDescriptor * pDescriptor, lt_va_list vaList);
void                LTCoreImpl_UpdateArgsFrom_lt_va_list(LTArgsDescriptor * pDescriptor, lt_va_list vaList);
void                LTCoreImpl_TrimArgs(LTArgs * pArgs);
void                LTCoreImpl_DestroyArgs(LTArgs * pArgs);

bool LT_ISR_SAFE    LTCoreImpl_AssertFailed(const char * pFile, int nLine, const char * pTest);
const LTStdlib *    LTCoreImpl_GetLTStdlib(void);
void                LTCoreImpl_ProcessISRConsoleInputChars(const char * pChars, u32 nChars) LT_ISR_SAFE;

/*****************************************************************************
 * LTCoreImpl Library Private Interface Helper functions -
 * - the functions are generated by each define_LTLIBRARY_INTERFACE and the
 * - declarations by the LTLIBRARY_EXPORT_INTERFACES macro; I'll need to do
 * - something to make it a bit more obvious that what I did to get
 * - these declarations here (gcc -E then cut & paste!)
 * TODO - get rid of these - they are not necessary
 */
const ILTThread               * LTCoreImpl_GetILTThread(void);
const LTCore                  * LTCoreImpl_GetLTCore(void);

/********************************************
 * LTCoreImpl Library Private Helper macros */
#define core_malloc(nBytes)           LTCoreImpl_Alloc((nBytes) comma_lt_callsite())
#define core_realloc(pMem, nBytes)    LTCoreImpl_ReAlloc((pMem), (nBytes) comma_lt_callsite())
#define core_free(pMem)               LTCoreImpl_Free((pMem) comma_lt_callsite())

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_SOURCE_LT_CORE_LTCOREIMPL_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  05-Sep-19   augustus    created
 *  11-Oct-19   augustus    added OpenDriverLibrary and moved common functions/members here
 *  24-Nov-19   augustus    added GenRandomBytes()
 *  01-Dec-19   augustus    added private string functions for LTString
 *  19-Dec-19   augustus    added StringVFormatString
 *  07-Jan-20   augustus    added TerminateLT()
 *  20-Jan-20   augustus    AssertFailed returns bool to control DebugBreak() activation;
 *                          log functions take pSection and pTag
 *  24-Feb-20   augustus    normalized boot sequence to platform independent; moved here to CoreImplBase
 *  14-Mar-20   augustus    isr safe nested disable/enable
 *  07-May-20   augustus    re-created in C from LTCoreImplBase.h
 *  21-May-20   augustus    added SpinLockAtomic functions
 *  05-Aug-20   augustus    implemented LTHandle
 *  19-Aug-20   augustus    added GetNameCheckedHandleInterface
 *  06-Sep-20   augustus    LTHandle->LTThread; added GetLTCoreImpl
 *  16-Oct-20   augustus    added local time, time zone, and calendar time support
 *  20-Nov-20   augustus    added GetHandleInterfaceName
 *  26-Nov-20   augustus    added console character input support
 *  05-Dec-20   augustus    added GetSystemRAMLowWatermark
 *  05-Jun-21   augustus    moved TimeZone, ClockLocal, and ClockTime/CalendarTime conversion functions to LTSystemTimeZone library
 *  06-Jun-21   augustus    enabled DEBUG and RELEASE logging macros; eradicated ILTLogger and ILTMutex
 *  22-Jul-21   augustus    made handle nInDestroy LTAtomic (no more spinlock)
 *  11-Dec-21   tiberius    renamed string and pointer arrays, removed U32 arrays
 *  28-Feb-22   augustus    added LTCoreImpl_FormatCanonicalTimeString
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 *  17-May-22   augustus    replaced timeUTCOffset with timebaseUTC
 *  22-May-24   augustus    added bIncludeBrackets to FormatCanonicalTimeString
 *  10-Apr-25   augustus    renamed standby mode to sleep mode; added hSleepModeEvent
 */
