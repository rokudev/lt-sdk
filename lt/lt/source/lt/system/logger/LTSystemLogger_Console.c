/******************************************************************************
 * LTSystemLogger_Console.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTSystemLogger_Private.h"

typedef struct {
    char *buffer;
    LT_SIZE avail;
} StringSpan;

static void StringCopy(StringSpan *to, const char *from) {
    LT_SIZE copied  = lt_strncpyTerm(to->buffer, from, to->avail);
    to->buffer     += copied;
    to->avail      -= copied;
}

static void StringPrint(StringSpan *to, const char *format, ...) {
    lt_va_list args;
    lt_va_start(args, format);
    int printed = lt_vsnprintf(to->buffer, to->avail, format, args);
    lt_va_end(args);

    if (printed > 0) {
        if ((LT_SIZE)printed > to->avail) {
            printed = (int)to->avail;
        }
        to->buffer += printed;
        to->avail  -= printed;
    }
}

static void StringAdvance(StringSpan *to, int amount) {
    to->buffer += amount;
    to->avail  -= amount;
}

static bool Private_Console_SetTextColor(StringSpan *span, u32 nLogFlags) {
    const char *pColor        = NULL;
    enum LTCore_LogFlags logLevel = (nLogFlags & kLTCore_LogFlags_LogTypeMask);
    switch (logLevel) {
        case kLTCore_LogFlags_LogTypeVerbose:
            pColor = LTLOG_COLOR_VERBOSE;
            break;
        case kLTCore_LogFlags_LogTypeDebugLog:
            pColor = LTLOG_COLOR_DEBUG;
            break;
        case kLTCore_LogFlags_LogTypeAssert:
            pColor = LTLOG_COLOR_ASSERT;
            break;
        case kLTCore_LogFlags_LogTypeYellowAlert:
            pColor = LTLOG_COLOR_YELLOWALERT;
            break;
        case kLTCore_LogFlags_LogTypeRedAlert:
            pColor = LTLOG_COLOR_REDALERT;
            break;
        default:
            return false;
    }
    StringCopy(span, pColor);
    return true;
}

#define LTLOG_PREAMBLE_ISR         "[I]"
#define LTLOG_PREAMBLE_VERBOSE     "[V]"
#define LTLOG_PREAMBLE_DEBUG       "[D]"
#define LTLOG_PREAMBLE_YELLOWALERT "[W]"
#define LTLOG_PREAMBLE_REDALERT    "[E]"
#define LTLOG_PREAMBLE_ASSERT      "[A]"
#define LTLOG_PREAMBLE_SERVER      "[S]"
#define LTLOG_PREAMBLE_OFFSET      ""

static void Private_Console_OutputPreamble(StringSpan *span, const LTLogMessage *msg) {
    StringAdvance(span,
                  LT_GetCore()->FormatCanonicalTimeString(msg->timestamp,
                                                          span->buffer,
                                                          span->avail,
                                                          true));
    if (msg->level & kLTCore_LogFlags_LogFromISR) {
        StringCopy(span, LTLOG_PREAMBLE_ISR);
    }
    switch (msg->level & kLTCore_LogFlags_LogTypeMask) {
        case kLTCore_LogFlags_LogTypeVerbose:
            StringCopy(span, LTLOG_PREAMBLE_VERBOSE);
            break;
        case kLTCore_LogFlags_LogTypeDebugLog:
            StringCopy(span, LTLOG_PREAMBLE_DEBUG);
            break;
        case kLTCore_LogFlags_LogTypeYellowAlert:
            StringCopy(span, LTLOG_PREAMBLE_YELLOWALERT);
            break;
        case kLTCore_LogFlags_LogTypeRedAlert:
            StringCopy(span, LTLOG_PREAMBLE_REDALERT);
            break;
        case kLTCore_LogFlags_LogTypeAssert:
            StringCopy(span, LTLOG_PREAMBLE_ASSERT);
            break;
        default:
            if (msg->index->toServer) {
                StringCopy(span, LTLOG_PREAMBLE_SERVER);
            } else {
                StringCopy(span, LTLOG_PREAMBLE_OFFSET);
            }
    }
}

static LTLog_Error Private_Consumer_PrintFullLog(LTLogConsumer consumer,
                                                 const LTLogMessage *msg,
                                                 bool bSetColor,
                                                 char *buffer,
                                                 LT_SIZE *size) {
    LTCore *core    = LT_GetCore();
    LT_SIZE availSize = *size;
    StringSpan span = {.buffer = buffer, .avail = availSize};
    bool resetColor = false;

    if (((msg->level & kLTCore_LogFlags_LogTypeMask) != kLTCore_LogFlags_LogTypeRaw)) {
        if (bSetColor) resetColor = Private_Console_SetTextColor(&span, msg->level);
        Private_Console_OutputPreamble(&span, msg);

        if (msg->printPayload.threadName)
            StringPrint(&span,
                        "[P%d:%-19s]",
                        msg->printPayload.threadPriority,
                        msg->printPayload.threadName);
        if (msg->printPayload.memstat)
            StringAdvance(&span,
                        core->FormatCanonicalMemstatString(msg->printPayload.memstat,
                                                           span.buffer,
                                                           span.avail,
                                                           true));

        StringPrint(&span,
                    "[%s%s%s] ",
                    msg->printPayload.section,
                    msg->printPayload.tag ? "." : "",
                    msg->printPayload.tag ? msg->printPayload.tag : "");
    }

    LT_SIZE count = span.avail;
    LTLog_Error result = Private_Consumer_Print(consumer, msg, false, false, span.buffer, &count);
    if (result != LTLog_Error_None) {
        core->ConsoleStompString("\n" LTLOG_COLOR_REDALERT "!!! logging: read failure" LTLOG_COLOR_DEFAULT "\n");
        return result;
    }
    StringAdvance(&span, count);

    if (((msg->level & kLTCore_LogFlags_LogTypeMask) != kLTCore_LogFlags_LogTypeRaw)) {
        // Remove trailing newline if any. We will re-add it after the epilogue.
        char last = *(span.buffer - 1);
        if (last == '\n' || last == '\r') {
            StringAdvance(&span, -1);
        }

        // 1 for the newline, 1 for the terminator, plus the color reset if needed
        LT_SIZE epilogueLen = 2;
        if (span.avail < epilogueLen) {
            // We will definitely truncate, which adds color that we need to reset.
            // Set this now so the epilogue length calculation will be correct for truncation.
            resetColor = true;
        }
        if (resetColor) {
            epilogueLen += LTLOG_COLORCOMMAND_NUMCHARS;
        }

        if (span.avail < epilogueLen) {
            static const char truncationMsg[] = " " LTLOG_COLOR_YELLOWALERT "...";
            s32 truncationLen       = sizeof(truncationMsg);
            s32 extraLen  = (truncationLen + epilogueLen) - span.avail;
            StringAdvance(&span, -extraLen);
            StringCopy(&span, truncationMsg);
        }

        if (resetColor) {
            StringCopy(&span, LTLOG_COLOR_DEFAULT);
        }
        StringCopy(&span, "\n");
    } else {
        static const char truncationMsg[] = " ...";
        s32 truncationLen        = sizeof(truncationMsg) + 1;
        if (span.avail == 0) {
            lt_memcpy(span.buffer - truncationLen, truncationMsg, truncationLen);
        }
    }
    *size = availSize- span.avail;
    return LTLog_Error_None;
}

static void Private_Console_Consumer(const ILTLogConsumer *api,
                                     LTLogConsumer consumer,
                                     const LTLogMessage *msg) {
    if (msg->recordType) {
        if (msg->recordType == LT_U32_MAX) {
            const u8 *data;
            LT_SIZE   size = api->GetData(consumer, msg, &data);
            LT_GetCore()->ConsoleStompChars((const char *)data, size);
        }
    }
    if (!msg->index->toConsole) {
        return;
    }
    if (msg->printPayload.format == NULL) {
        return;
    }
#ifndef LT_DEBUG
    switch (msg->level & kLTCore_LogFlags_LogTypeMask) {
        case kLTCore_LogFlags_LogTypeVerbose:
        case kLTCore_LogFlags_LogTypeDebugLog:
            return;
    }
#endif

    LT_UNUSED(api);
    LT_SIZE bufSize = sizeof(S.buf.format);
    Private_Consumer_PrintFullLog(consumer, msg, true, S.buf.format, &bufSize);
    LT_GetCore()->ConsoleStompString(S.buf.format);
}
