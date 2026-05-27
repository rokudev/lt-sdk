/******************************************************************************
 * lt/source/core/LTLoggerImpl.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_CORE_LTLOGGERIMPL_H
#define ROKU_LT_SOURCE_LT_CORE_LTLOGGERIMPL_H

#include <lt/core/LTCore.h>
#include <lt/core/bsp/LTCoreBSP.h>

/**********************************************************
 * LTLoggerImpl private LTCore Library internal functions *
 **********************************************************/
void LTLoggerImpl_EnablePreLTCoreStompLogging(const LTCoreBSP *pBSP, bool bHostedMode);
void LTLoggerImpl_DisablePreLTCoreStompLogging(void);
void LTLoggerImpl_Init(void);
void LTLoggerImpl_Fini(void);
void LTLoggerImpl_SetLogHookFunction(LTCore_LogHookFunction *pLogHookFunction);
void LTLoggerImpl_SetTraceHookFunction(LTCore_TraceHookFunction pTraceHookFunction);
void LTLoggerImpl_PrintCopyrightMessage(void);

/*****************************************************************
 *LTCore Library public functions implemented by LTLoggerImpl
 *****************************************************************/
void LTLoggerImpl_ConsolePrint(const char * pFormatString, ...) LT_PRINTF_FORMAT_FUNCTION(1);   LT_ISR_SAFE
void LTLoggerImpl_ConsolePrintV(const char * pFormatString, lt_va_list args);                   LT_ISR_SAFE
void LTLoggerImpl_ConsolePutChars(const char * pSrc, u32 nChars);                               LT_ISR_SAFE
void LTLoggerImpl_ConsolePutString(const char * pString);                                       LT_ISR_SAFE

void LTLoggerImpl_ConsoleStomp(const char * pFormatString, ...) LT_PRINTF_FORMAT_FUNCTION(1);   LT_ISR_SAFE
void LTLoggerImpl_ConsoleStompV(const char * pFormatString, lt_va_list args);                   LT_ISR_SAFE
void LTLoggerImpl_ConsoleStompString(const char * pString);                                     LT_ISR_SAFE

void LTLoggerImpl_Log(const char *pSectionName, const char *pTag, u32 nLogFlags, const char *pFormatString, ... ) LT_PRINTF_FORMAT_FUNCTION(4); LT_ISR_SAFE
void LTLoggerImpl_LogV(const char * pSectionName, const char * pTag, u32 nLogFlags, const char * pFormatString, lt_va_list args);               LT_ISR_SAFE

void LTLoggerImpl_Trace(LTTraceStream *stream, LTTracePayloadType type, ...)                    LT_ISR_SAFE;
void LTLoggerImpl_TraceAddStreams(LTTraceStream **streams);
void LTLoggerImpl_TraceRemoveStreams(LTTraceStream **streams);
bool LTLoggerImpl_TraceSetStreamEnabled(u32 streamId, bool enabled);
void LTLoggerImpl_TraceKnownStreams(void);

/*****************************************************************
 * LTLogger public functions that make it to the public interface some day
 *****************************************************************/
void LTLoggerImpl_Flush(void);

#endif /* #ifndef ROKU_LT_SOURCE_LT_CORE_LTLOGGERIMPL_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  22-Jun-20   augustus    converted to C
 *  05-Aug-20   augustus    Logger is now an LTHandle not an LTObject; added AssertLog
 *  27-Aug-20   augustus    moved define_LTLIBRARY_INTERFACE into LTLogManager.c
 *  28-Sep-20   augustus    renamed LTLogManager to LTLoggerImpl
 *  17-Dec-20   augustus    added ConsolePrintV
 *  14-Feb-21   augustus    added ConsoleStomp and ConsoleStompV
 *  09-Mar-21   augustus    expose Flush internally
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 */
