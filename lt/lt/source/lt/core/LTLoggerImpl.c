/******************************************************************************
 * lt/source/core/LTLoggerImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/******************************************************************************
 ******************************************************************************
 * ATTENTION
 *
 *   For reasons of efficiency, LTLoggerImpl is not re-entrant.
 *   All functions (static and non-static) in this file must only call:
 *       i. static functions in this file
 *      ii. other LTCore or LTCoreBSP functions external to this file that are *statically bound*
 *          (contained within) the LTCore library, as required.
 *
 * CAUTION
 *      This means that any external facilities called by functions in this file must not
 *      use anything that calls back into this file.  Namely no LTLogger, no LT_ASSERT,
 *           and none of the following LTCore functions: ConsolePutCharsToUART, ConsolePutString,
 *           ConsolePrint, and AssertFailed.
 *
 * PENALTY
 *      If you work on LTCore it is your duty to understand what external functions are called by
 *      the functions herein and ensure the entire call paths of those external functions don't
 *      cause LTLoggerImpl re-entrancy.  The penalty is an almost certain hang or crash.
 */

#include "LTLoggerImpl.h"
#include "LTCoreImpl.h"
#include "LTThreadImpl.h"
#include "LTHandle.h"
#include "LTStdlibImpl.h"
#include "LTConsoleConnector.h"

DEFINE_LTLOG_SECTION("ltcore.log");

/**************************************************
 * file LTLoggerImpl.c private #defines and enums */

#define ROKU_LT_COPYRIGHT      " Roku LT OS. Copyright 2026, Roku, Inc. All rights reserved.\n"
#define ROKU_LT_VERSION_LEADER "   LTCore v. "

enum { kSizeThreadBuffer            = 720,
       kSizeInterruptBuf            = 244
};

/*****************************************
 * file LTLoggerImpl.c private types */
enum {
    kLTLoggerImpl_InitFlags_HostedMode                  = 1 << 0,
    kLTLoggerImpl_InitFlags_BufferInitialized           = 1 << 1
};

/*******************
 * stub callbacks */
static void DefaultTraceHook(LTTraceStream *stream, LTTracePayloadType type, lt_va_list args);

/*****************************************
 * file LTLoggerImpl.c static variables */
static const LTCoreBSP *        s_pBSP                  = NULL;
static char *                   s_pThreadFormatBuffer   = NULL;
static LTCore_LogHookFunction * s_pLogHookFunction      = NULL;
static LTCore_TraceHookFunction s_pTraceHookFunction    = &DefaultTraceHook;
static LTMutex                 *s_mutex                 = NULL;
static LTMutex                 *s_traceMutex            = NULL;
static LTTraceStream            s_traceDummy            = {.next = NULL};
static u8                       s_traceNextId           = 2;
static lt_va_list               s_nullArgs;
static LTAtomic                 s_initFlags;
static LTAtomic                 s_insideLogHookFunction;
static char                     s_interruptFormatBuf[kSizeInterruptBuf];

/*******************************
 * LTLoggerImpl inline helpers */
LT_INLINE bool LTLoggerImpl_InHostedMode(void)         { return LTAtomic_Load(&s_initFlags) & kLTLoggerImpl_InitFlags_HostedMode; }
LT_INLINE bool LTLoggerImpl_IsBufferInitialized(void)  { return LTAtomic_Load(&s_initFlags) & kLTLoggerImpl_InitFlags_BufferInitialized; }

 /*****************************************************************
  * LTCore public interface functions implemented by LTLoggerImpl *
  *****************************************************************/
LT_PRINTF_FORMAT_FUNCTION(1) void
LTLoggerImpl_ConsolePrint(const char *pFormatString, ...) LT_ISR_SAFE {
    lt_va_list args;
    lt_va_start(args, pFormatString);
    LTLoggerImpl_LogV(NULL, NULL, kLTCore_LogFlags_LogTypeRaw | kLTCore_LogFlags_LogToConsole, pFormatString, args);
    lt_va_end(args);
}

void
LTLoggerImpl_ConsolePrintV(const char * pFormatString, lt_va_list args) LT_ISR_SAFE {
    LTLoggerImpl_LogV(NULL, NULL, kLTCore_LogFlags_LogTypeRaw | kLTCore_LogFlags_LogToConsole, pFormatString, args);
}

void
LTLoggerImpl_ConsolePutChars(const char *pSrc, u32 nChars) LT_ISR_SAFE {
    LTLoggerImpl_Log(NULL, NULL, kLTCore_LogFlags_LogTypeRaw | kLTCore_LogFlags_LogToConsole, "%.*s", (int)nChars, pSrc);
}

void
LTLoggerImpl_ConsolePutString(const char * pString) LT_ISR_SAFE {
    LTLoggerImpl_Log(NULL, NULL, kLTCore_LogFlags_LogTypeRaw | kLTCore_LogFlags_LogToConsole, "%s", pString);
}

LT_PRINTF_FORMAT_FUNCTION(1) void
LTLoggerImpl_ConsoleStomp(const char * pFormatString, ...) LT_ISR_SAFE {
    lt_va_list args;
    lt_va_start(args, pFormatString);
    LTLoggerImpl_LogV(NULL, NULL, kLTCore_LogFlags_LogTypeRaw | kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_ConsoleStomp, pFormatString, args);
    lt_va_end(args);
}

void
LTLoggerImpl_ConsoleStompV(const char * pFormatString, lt_va_list args) LT_ISR_SAFE {
    LTLoggerImpl_LogV(NULL, NULL, kLTCore_LogFlags_LogTypeRaw | kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_ConsoleStomp, pFormatString, args);
}

void
LTLoggerImpl_ConsoleStompString(const char * pString) LT_ISR_SAFE {
    if (pString && s_pBSP) {
        bool bDisable = ((! LTKInsideInterruptContext()) && (s_pLogHookFunction != NULL));
        LT_SIZE nMask = bDisable ? LTKDisableInterrupts() : 0;
        LTConsoleConnector_ConsoleStompChars(pString, lt_strlen(pString));
        if (bDisable) LTKEnableInterrupts(nMask);
    }
}

void
LTLoggerImpl_Flush(void) {
    LTCore_LogHookFunction *pHookFunction = s_pLogHookFunction;
    if (pHookFunction) (*pHookFunction)(NULL, NULL, kLTCore_LogFlags_Flush, NULL, s_nullArgs);
}

// output colors
// helpful link for VT100 codes: http://domoticx.com/terminal-codes-ansivt100/
#define LTLOG_COLOR_VERBOSE "\x1b[32m"
#define LTLOG_COLOR_DEBUG "\x1b[36m"
#define LTLOG_COLOR_ASSERT "\x1b[35m"
#define LTLOG_COLOR_YELLOWALERT "\x1b[33m"
#define LTLOG_COLOR_REDALERT "\x1b[31m"
#define LTLOG_COLOR_DEFAULT "\x1b[39m"
#define LTLOG_COLORCOMMAND_NUMCHARS 5

static const char * s_logColors[8] = {
    /* kLTCore_LogFlags_LogTypeRaw             = 0,   */      LTLOG_COLOR_DEFAULT,
    /* kLTCore_LogFlags_LogTypeVerbose         = 1,   */      LTLOG_COLOR_VERBOSE,
    /* kLTCore_LogFlags_LogTypeDebugLog        = 2,   */      LTLOG_COLOR_DEBUG,
    /* kLTCore_LogFlags_LogTypeLog             = 3,   */      LTLOG_COLOR_DEFAULT,
    /* kLTCore_LogFlags_LogTypeYellowAlert     = 4,   */      LTLOG_COLOR_YELLOWALERT,
    /* kLTCore_LogFlags_LogTypeRedAlert        = 5,   */      LTLOG_COLOR_REDALERT,
    /* kLTCore_LogFlags_LogTypeAssert          = 6,   */      LTLOG_COLOR_ASSERT,

    /* kLTCore_LogFlags_LogTypeMask            = 0x7, */      LTLOG_COLOR_DEFAULT /* so we don't crash if there is a 7 in the log flags */
};

static int
LTLoggerImpl_DumpThreadName(char * pBuff, int nBuffSize) {
    LTThread hThread = LTThreadImpl_GetCurrentThread();
    if ((0 == hThread) || (nBuffSize <= (kLTThread_MaxNameLen + 7))) return 0;

    int nChars = LTStdlibImpl_snprintf(pBuff, nBuffSize, "[P%d:", (int)LTThreadImpl_GetPriority(hThread));
    if (nChars < 0) return 0;
    if ((nBuffSize - nChars) <= (kLTThread_MaxNameLen + 2)) return 0;
    LTThreadImpl_GetName(hThread, pBuff + nChars);
    int len = LTStdlibImpl_strlen(pBuff + nChars);
    nChars += len;
    while (len++ < kLTThread_MaxNameLen) pBuff[nChars++] = ' ';
    pBuff[nChars++] = ']';

    return nChars;
}

static int
LTLoggerImpl_DumpThreadHandle(char * pBuff, int nBuffSize) {
    LTThread hThread = LTThreadImpl_GetCurrentThread();
    if ((0 == hThread) || (nBuffSize <= 12)) return 0;

    int nChars = LTStdlibImpl_snprintf(pBuff, nBuffSize, "[0x%lx]", LT_PLT_HANDLE(hThread));
    return (nChars < 0) ? 0 : nChars;
}

static void
LTLoggerImpl_FormatAndStompLogMessage(char * pBuff, int nBuffSize, const char *pSection, const char *pTag, u32 nLogFlags, const char *pFormatString, lt_va_list args) LT_ISR_SAFE {
    int nChars = 0, nMoreChars = 0;
    u32 nLogType = nLogFlags & kLTCore_LogFlags_LogTypeMask;
    if (kLTCore_LogFlags_LogTypeRaw != nLogType) {
        // put in "<color>[00000.00321][P0:Replevinator][12.21k/48.00k used, 32.13k max][section.tag] YELLOW ALERT: "
        if (pSection && 0 == *pSection) pSection = NULL;
        if (pTag && 0 == *pTag) pTag = NULL;
        nChars = lt_strncpyTerm(pBuff, s_logColors[nLogType], nBuffSize);
        //nChars = LTLOG_COLORCOMMAND_NUMCHARS;
        nChars += LTCoreImpl_FormatCanonicalTimeString(LTCoreImpl_GetKernelTime(), pBuff + nChars, nBuffSize - nChars, true);
        if (nLogFlags & kLTCore_LogFlags_DumpThreadName) nChars += LTLoggerImpl_DumpThreadName(pBuff + nChars, nBuffSize - nChars);
        if (nLogFlags & kLTCore_LogFlags_DumpThreadName) nChars += LTLoggerImpl_DumpThreadHandle(pBuff + nChars, nBuffSize - nChars);
        if (nLogFlags & kLTCore_LogFlags_DumpMemstats)   nChars += LTCoreImpl_FormatCanonicalMemstatString(LTCoreImpl_SnapshotMemstat(), pBuff + nChars, nBuffSize - nChars, true);
        nMoreChars = LTStdlibImpl_snprintf(pBuff + nChars, nBuffSize - nChars, "%s%s%s%s%s%s%s%s%s",
            (((nLogFlags & kLTCore_LogFlags_LogFromISR) && (0 == (nLogFlags & kLTCore_LogFlags_Reserved1))) ? "[ISR]" : ""),
            ((nLogFlags & kLTCore_LogFlags_LogToServer) ? "[SRV]" : ""),
            pSection || pTag ? "[" : "",
            pSection ? pSection : "",
            pSection && pTag ? "." : "",
            pTag ? pTag : "",
            pSection || pTag ? "]" : "",
            pSection || pTag || (nLogFlags & (((nLogFlags & kLTCore_LogFlags_LogFromISR) && (0 == (nLogFlags & kLTCore_LogFlags_Reserved1)))|kLTCore_LogFlags_LogToServer)) ? " " : "",
            ((nLogType == kLTCore_LogFlags_LogTypeYellowAlert) ? "YELLOW ALERT: " : (nLogType == kLTCore_LogFlags_LogTypeRedAlert ? "RED ALERT: " : "")));
        if (nMoreChars < 0) nMoreChars = 0;
        else if (nMoreChars >= (nBuffSize - nChars)) nMoreChars = (nBuffSize - nChars) - 1;
        s_pBSP->PutCharsToConsole(pBuff, nChars + nMoreChars);
    }

    nChars = pFormatString ? LTStdlibImpl_vsnprintf(pBuff, nBuffSize, pFormatString, args) : 0;
    if (nChars < 0) nChars = 0;
    else if (nChars >= nBuffSize) nChars = nBuffSize - 1;
    if (nChars > 0) {
        if (nLogType == kLTCore_LogFlags_LogTypeRaw) {
            s_pBSP->PutCharsToConsole(pBuff, nChars);
            nChars = nMoreChars = 0;
        }
        else {
            // <put in "formatted string <default color> \n"
            nMoreChars = ((pBuff[nChars-1] != '\r') && (pBuff[nChars-1] != '\n')) ? 1 : 0;
            if (nChars + LTLOG_COLORCOMMAND_NUMCHARS < nBuffSize) {
                nChars += lt_strncpyTerm(pBuff+nChars, LTLOG_COLOR_DEFAULT, nBuffSize - nChars);
                //nChars += LTLOG_COLORCOMMAND_NUMCHARS;
                if (nMoreChars) {
                    if (nChars + 1 < nBuffSize) {
                        pBuff[nChars++] = '\n';
                        nMoreChars = 0;
                    }
                }
                s_pBSP->PutCharsToConsole(pBuff, nChars);
                if (nMoreChars) s_pBSP->PutCharsToConsole("\n", 1);
                nChars = nMoreChars = 0;
            }
            else s_pBSP->PutCharsToConsole(pBuff, nChars);
        }
    }
    else {
        /* no format string, mark to restore the color and print a newline. */
        if (nLogType != kLTCore_LogFlags_LogTypeRaw) {
            nChars = 1; nMoreChars = 1;
        }
    }

    if (nChars) {
        // put in <default color> \n
        nChars = lt_strncpyTerm(pBuff, LTLOG_COLOR_DEFAULT, nBuffSize);
        //nChars = LTLOG_COLORCOMMAND_NUMCHARS;
        if (nMoreChars) pBuff[nChars++] = '\n';
        s_pBSP->PutCharsToConsole(pBuff, nChars);
    }
    else {
        if (nMoreChars) s_pBSP->PutCharsToConsole("\n", 1);
    }
}

void
 LTLoggerImpl_LogV(const char *pSection, const char *pTag, u32 nLogFlags, const char *pFormatString, lt_va_list args) LT_ISR_SAFE {
    // can't do anything until the bsp is initialized, 596F752068617665206E6F206368616E636520746F20737572766976652C206D616B6520796F75722074696D652E
    if (NULL == s_pBSP) return;
    nLogFlags &= ~kLTCore_LogFlags_Reserved1;

    // mark we're in an ISR if we are, or if in a non-LT thread (like the LTK idle thread or native os thread on hosted systems)
    if (LTKInsideInterruptContext() || (0 == LTThreadImpl_GetCurrentThread())) {
        nLogFlags |= kLTCore_LogFlags_LogFromISR;
        nLogFlags &= ~(kLTCore_LogFlags_DumpThreadName | kLTCore_LogFlags_DumpMemstats);
        if (LTAtomic_Load(&s_insideLogHookFunction)) nLogFlags |= kLTCore_LogFlags_ConsoleStomp;
    }
    else {
        // in a thread, bail if thread has prohibit logging flag set
        // and force console stomp if the log buffer isn't initialized
        if (LTThreadImpl_IsProhibitLoggingFlagSet()) return;

        #if LTLOG_AUTOLOG_PREPEND_THREAD_NAME
            nLogFlags |= kLTCore_LogFlags_DumpThreadName;
        #endif
        #if LTLOG_AUTOLOG_DUMP_RAM_STATS
            nLogFlags |= kLTCore_LogFlags_DumpMemstats;
        #endif

        if (LTKInterruptsAreDisabled()) {
            /* we're in a thread with interrupts disabled, not ideal.  Turn off kLTCore_LogFlags_DumpMemstats - can't collect when disabled.
               Also set kLTCore_LogFlags_Reserved1 to indicate in thread while disabled and to mark to perform subsequent yellow alert */
            nLogFlags &= ~kLTCore_LogFlags_DumpMemstats;
            nLogFlags |= kLTCore_LogFlags_Reserved1;
        }
        else {
            if (! LTLoggerImpl_IsBufferInitialized()) nLogFlags |= kLTCore_LogFlags_ConsoleStomp;
        }
    }

    if (LTLoggerImpl_InHostedMode()) {
        if (s_pBSP->RedirectLogV) { s_pBSP->RedirectLogV(pSection, pTag, nLogFlags, pFormatString, args); return; }
        nLogFlags |= kLTCore_LogFlags_ConsoleStomp;
    }

    LTCore_LogHookFunction *pLogHookFunction = s_pLogHookFunction;
    if (! pLogHookFunction) nLogFlags |= kLTCore_LogFlags_ConsoleStomp;

    if (nLogFlags & kLTCore_LogFlags_ConsoleStomp) {
        if (kLTCore_LogFlags_LogToConsole & nLogFlags) {
            #ifndef LT_DEBUG
                if ((nLogFlags & kLTCore_LogFlags_LogTypeMask) == kLTCore_LogFlags_LogTypeVerbose) return;
            #endif

            if ((nLogFlags & kLTCore_LogFlags_LogFromISR) || (nLogFlags & kLTCore_LogFlags_Reserved1) || (! LTLoggerImpl_IsBufferInitialized())) {
                // in ISR or in thread when disabled or no thread buffer yet, use ISR buffer
                LT_SIZE nMask = LTKDisableInterrupts();
                LTLoggerImpl_FormatAndStompLogMessage(s_interruptFormatBuf, kSizeInterruptBuf, pSection, pTag, nLogFlags, pFormatString, args);
                LTKEnableInterrupts(nMask);
            }
            else {
                /* stomp from thread with thread buffer, protect with mutex, then disable interrupts so log hook queued messages dont stomp on stomp */
                bool bProhibitLogging = LTThreadImpl_FetchSetProhibitLoggingFlag(true);
                s_mutex->API->Lock(s_mutex);
                LT_SIZE nMask = pLogHookFunction ? LTKDisableInterrupts() : 0;
                LTLoggerImpl_FormatAndStompLogMessage(s_pThreadFormatBuffer, kSizeThreadBuffer, pSection, pTag, nLogFlags, pFormatString, args);
                if (pLogHookFunction) LTKEnableInterrupts(nMask);
                s_mutex->API->Unlock(s_mutex);
                if (! bProhibitLogging) LTThreadImpl_SetProhibitLoggingFlag(false);
            }
        }
    }
    else {
        if (nLogFlags & kLTCore_LogFlags_LogFromISR) {
            if (LTAtomic_CompareAndExchange(&s_insideLogHookFunction, 0, 1)) {
                (*pLogHookFunction)(pSection, pTag, nLogFlags, pFormatString, args);
                LTAtomic_Store(&s_insideLogHookFunction, 0);
            }
        }
        else {
             bool bProhibitLogging = LTThreadImpl_FetchSetProhibitLoggingFlag(true);
             (*pLogHookFunction)(pSection, pTag, nLogFlags, pFormatString, args);
             if (! bProhibitLogging) LTThreadImpl_SetProhibitLoggingFlag(false);
        }
    }

#if 0
    if (nLogFlags & kLTCore_LogFlags_Reserved1) {
        // reserved 1 is set - yellow alert that logging from a thread with isrs disabled is not ideal
        // first make sure what we just logged wasn't already a yellow or red alert
        nLogFlags &= kLTCore_LogFlags_LogTypeMask;
        if (nLogFlags != kLTCore_LogFlags_LogTypeYellowAlert && nLogFlags != kLTCore_LogFlags_LogTypeRedAlert) {
            LTThread hThread = LTThreadImpl_GetCurrentThread();
            LTThreadImpl * pImpl = LTThreadImpl_PrivateDataToThreadImpl(LTHandle_ReservePrivateData(hThread));
            LTLOG_YELLOWALERT("when.disabled", "thread %s called LTLOG() with interrupts disabled.  No bueno.", pImpl && *pImpl->name ? pImpl->name : "???");
            LTHandle_ReleasePrivateData(hThread, LTThreadImpl_ThreadImplToPrivateData(pImpl));
        }
    }
#endif
}

LT_PRINTF_FORMAT_FUNCTION(4) void
LTLoggerImpl_Log(const char *pSection, const char *pTag, u32 nLogFlags, const char *pFormatString, ...) LT_ISR_SAFE {
    lt_va_list args;
    lt_va_start(args, pFormatString);
    LTLoggerImpl_LogV(pSection, pTag, nLogFlags, pFormatString, args);
    lt_va_end(args);
}

static void
DefaultTraceHook(LTTraceStream *stream, LTTracePayloadType type, lt_va_list args) {
    LT_UNUSED(stream);
    LT_UNUSED(type);
    LT_UNUSED(args);
}

void
LTLoggerImpl_Trace(LTTraceStream *stream, LTTracePayloadType type, ...) {
    lt_va_list args;
    lt_va_start(args, type);
    s_pTraceHookFunction(stream, type, args);
    lt_va_end(args);
}

static LTTraceStream *
LTLoggerImpl_TraceFind_Locked(u32 streamId) {
    for (LTTraceStream *stream = s_traceDummy.next; stream; stream = stream->next) {
        if (LTBitfieldGet(stream->state, kLTTrace_State_IdMask, kLTTrace_State_IdShift) == streamId) {
            return stream;
        }
    }
    return NULL;
}

void
LTLoggerImpl_TraceAddStreams(LTTraceStream **streams) {
    if (s_traceMutex && streams) {
        s_traceMutex->API->Lock(s_traceMutex);
        for(LT_SIZE i = 0; streams[i]; ++i) {
            streams[i]->next = s_traceDummy.next;
            LTBitfieldSet(streams[i]->state, s_traceNextId++, kLTTrace_State_IdMask, kLTTrace_State_IdShift);
            s_traceDummy.next = streams[i];
            LTLoggerImpl_Trace(streams[i], kLTTrace_Type_Descriptor);
        }
        s_traceMutex->API->Unlock(s_traceMutex);
    }
}

void
LTLoggerImpl_TraceRemoveStreams(LTTraceStream **streams) {
    if (s_traceMutex && streams) {
        s_traceMutex->API->Lock(s_traceMutex);
        for(LT_SIZE i = 0; streams[i]; ++i) {
            for (LTTraceStream *stream = &s_traceDummy; stream; stream = stream->next) {
                if (stream->next != streams[i]) continue;
                stream->next = stream->next->next;
                break;
            }
        }
        s_traceMutex->API->Unlock(s_traceMutex);
    }
}

bool
LTLoggerImpl_TraceSetStreamEnabled(u32 streamId, bool enabled) {
    if (s_traceMutex) {
        s_traceMutex->API->Lock(s_traceMutex);
        LTTraceStream *stream = LTLoggerImpl_TraceFind_Locked(streamId);
        if (stream) {
            LTBitfieldSet(stream->state, enabled, kLTTrace_State_EnabledMask, kLTTrace_State_EnabledShift);
        }
        s_traceMutex->API->Unlock(s_traceMutex);
        return !!stream;
    } else {
        return false;
    }
}

void
LTLoggerImpl_TraceKnownStreams(void) {
    if (s_traceMutex) {
        s_traceMutex->API->Lock(s_traceMutex);
        for (LTTraceStream *stream = s_traceDummy.next; stream; stream = stream->next) {
            LTLoggerImpl_Trace(stream, kLTTrace_Type_Descriptor);
        }
        s_traceMutex->API->Unlock(s_traceMutex);
    }
}

void
LTLoggerImpl_SetLogHookFunction(LTCore_LogHookFunction *pLogHookFunction) {
    if (pLogHookFunction) LTThreadImpl_SetProhibitLoggingFlag(true);
    s_pLogHookFunction = pLogHookFunction;
}

void
LTLoggerImpl_SetTraceHookFunction(LTCore_TraceHookFunction pTraceHookFunction) {
    s_pTraceHookFunction = pTraceHookFunction ? pTraceHookFunction : &DefaultTraceHook;
}

/*******************************
 * LTLoggerImpl initialization */
void
LTLoggerImpl_EnablePreLTCoreStompLogging(const LTCoreBSP *pBSP, bool bHostedMode) {
    s_pBSP                  = pBSP;
    s_pLogHookFunction      = NULL;
    LTConsoleConnector_Init(pBSP);
    LTAtomic_Store(&s_initFlags, bHostedMode ? kLTLoggerImpl_InitFlags_HostedMode : 0);
}

void
LTLoggerImpl_DisablePreLTCoreStompLogging(void) {
    LTConsoleConnector_Fini();
    s_pBSP       = NULL;
    LTAtomic_FetchAnd(&s_initFlags, ~kLTLoggerImpl_InitFlags_HostedMode);
}

void
LTLoggerImpl_Init(void) {
    if (NULL != (s_pThreadFormatBuffer = (0 != (s_mutex = lt_createobject(LTMutex))) ? (char *)core_malloc(kSizeThreadBuffer) : NULL)) {
        *s_pThreadFormatBuffer = 0;
        LTAtomic_FetchOr(&s_initFlags, kLTLoggerImpl_InitFlags_BufferInitialized);
        LTLOG_DEBUG("init", "allocated %d.%01dk LTLogger buffer", (int)(kSizeThreadBuffer >> 10), (int)(((kSizeThreadBuffer % 1024) * 100) >> 10));
    }
    s_traceMutex = lt_createobject(LTMutex);
}

void
LTLoggerImpl_Fini(void) {
    s_pLogHookFunction = NULL;
    if (s_mutex) {
        if (s_pThreadFormatBuffer) {
            LTAtomic_FetchAnd(&s_initFlags, ~kLTLoggerImpl_InitFlags_BufferInitialized);
            core_free(s_pThreadFormatBuffer);
            s_pThreadFormatBuffer = NULL;
        }
        lt_destroyobject(s_mutex); s_mutex = NULL;
    }
    if (s_traceMutex) {
        lt_destroyobject(s_traceMutex); s_traceMutex = NULL;
    }
}

void
LTLoggerImpl_PrintCopyrightMessage(void) {
    /* s_pBSP has been set */
    LT_SIZE nMask = LTKDisableInterrupts();

    int nChars = LTCoreImpl_FormatCanonicalTimeString(LTCoreImpl_GetKernelTime(), s_interruptFormatBuf, kSizeInterruptBuf, true);
    s_pBSP->PutCharsToConsole(s_interruptFormatBuf, nChars);
    LTLoggerImpl_ConsoleStompString(ROKU_LT_COPYRIGHT);

    nChars = LTCoreImpl_FormatCanonicalTimeString(LTCoreImpl_GetKernelTime(), s_interruptFormatBuf, kSizeInterruptBuf, true);
    s_pBSP->PutCharsToConsole(s_interruptFormatBuf, nChars);
    LTLoggerImpl_ConsoleStompString(ROKU_LT_VERSION_LEADER LTTYPES_LTLIBRARY_BUILD_VERSION "\n\n");

    LTKEnableInterrupts(nMask);
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  22-Jun-20   augustus    converted to C
 *  02-Jul-20   augustus    simulate interrupt disable/enable
 *  05-Aug-20   augustus    Logger is now an LTHandle not an LTObject; added AssertLog
 *  12-Aug-20   augustus    converted all objects to handles
 *  27-Aug-20   augustus    moved define_LTLIBRARY_INTERFACE here and made functions static
 *  06-Sep-20   augustus    threads now queue TaskProc/TaskCompleteProc pairs
 *  28-Sep-20   augustus    renamed LTLogManager to LTLoggerImpl
 *  26-Oct-20   augustus    add 2K to the text ring buffer to mitigate overrun as a temp stopgap
 *  26-Oct-20   augustus    added LTLOG_PREAMBLE_INCLUDE_RAM_STATS
 *  17-Dec-20   augustus    added ConsolePrintV
 *  16-Feb-21   augustus    added ConsoleLogV
 *  09-Mar-21   augustus    re-enabled Flush and made it work with a hack for internal LTCore use
 *  04-Apr-21   augustus    added LTLOG_AUTO_PREPEND_THREAD_NAME
 *  10-Apr-21   augustus    moved debugging #defines to LTCoreDebugHelpers.h
 *  22-Jul-21   augustus    got rid of spinlocks; use mutex to protect s_pImpl->m_state.m_threadFormatBuffer
 *  22-Oct-21   augustus    changed memory log to report used/total max used instead of free/total min free
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 *  08-Aug-24   augustus    guard reentrency of log hook function from same thread and from ISRs
 */

