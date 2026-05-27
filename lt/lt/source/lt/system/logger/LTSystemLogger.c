/******************************************************************************
 * LTSystemLogger.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTSystemLogger_Private.h"

#ifdef LT_DEBUG
    #define LTSL_DEFAULT_LOGLEVEL (kLTCore_LogFlags_LogTypeDebugLog)
#else
    #define LTSL_DEFAULT_LOGLEVEL (kLTCore_LogFlags_LogTypeLog)
#endif

static bool Private_Write_String_IsRodata(const char *s) {
    return (s >= S.rodata.start) && (s < S.rodata.end);
}

static bool Private_Write_String(LTMessagePack_Obj *w, const char *s) {
    if (!s) {
        return S.i.mp->PutString(w, "", 1);
    } else {
        if (Private_Write_String_IsRodata(s)) {
            return S.i.mp->PutIntU64(w, (LT_PTRDIFF)(s - S.rodata.start));
        } else {
            return S.i.mp->PutCString(w, s);
        }
    }
}

static bool Private_Parse_String(LTMessagePack_Value v, const char **str) {
    if (v.type == LTMessagePack_Type_Integer) {
        *str = S.rodata.start + v.integer;
    } else if (v.type == LTMessagePack_Type_String) {
        if (v.size == 0) {
            return false;
        }
        if (v.data[v.size - 1] != '\0') {
            return false;
        }
        *str = (const char *)v.data;
    } else {
        return false;
    }
    return true;
}

static bool Private_Read_String(LTMessagePack_Obj *r, const char **str) {
    LTMessagePack_Value v;
    S.i.mp->GetValue(r, &v);
    return Private_Parse_String(v, str);
}

static bool Private_Read_OffsetString(LTMessagePack_Obj *r, const char **begin, const char **end, LT_PTRDIFF *offset) {
    LTMessagePack_Value value;
    switch (S.i.mp->GetValue(r, &value)) {
        case LTMessagePack_Type_Integer:
            *offset = value.integer;
            *begin  = S.rodata.start + *offset;
            *end    = *begin + lt_strlen((char *)*begin);
            break;
        case LTMessagePack_Type_String:
            *offset = LT_LOG_INVALID_FORMAT_OFFSET;
            *begin  = (const char *)value.data;
            *end    = *begin + value.size;
            break;
        default:
            *offset = LT_LOG_INVALID_FORMAT_OFFSET;
            *begin  = NULL;
            *end    = NULL;
            return false;
    }

    return true;
}

static void Private_VPrint(const char *section, const char *tag, u32 flags, const char *fmt, lt_va_list args) {
    bool bInsideInterruptContext = LT_GetCore()->InsideInterruptContext();
    if (!bInsideInterruptContext) {
        if (S.i.thread->IsTerminatePending(S.consumer.thread)) {
            LT_GetCore()->SetLogHookFunction(NULL);
            LT_GetCore()->SetTraceHookFunction(NULL);
            return;
        }
    }
    if (flags & kLTCore_LogFlags_Flush) {
        LTSystemLoggerImpl_Flush();
    } else {
        if (!fmt) {
            fmt = "";
        }
        u32 level = flags & kLTCore_LogFlags_LogTypeMask;
        if (level && level < S.config.minLevel) {
            return;
        }
        bool success = true;

        LTTime now = LT_GetCore()->GetKernelTime();

        if (!bInsideInterruptContext) {
            if (flags & kLTCore_LogFlags_DumpThreadName) level |= MSG_FLAG_DUMP_THREADNAME;
            if (flags & kLTCore_LogFlags_DumpMemstats) level |= MSG_FLAG_DUMP_MEMSTATS;
        }

        LT_SIZE size = LTLOG_CONFIG_MIN_ALLOCATION_SIZE;
        if (!Private_Write_String_IsRodata(fmt)) {
            size += lt_strlen(fmt);
        }
        LTLogBufferSpan *payload = Private_Payload_Alloc(size, true);
        if (!payload) {
            return;
        }
        LTString newSection = NULL;
        if (S.sectionPrefix) {
            // "prefix.section\0"
            newSection = ltstring_create(S.sectionPrefix);
            if (newSection) {
                ltstring_append(&newSection, section);
                section = newSection;
            }
        }
        LTMessagePack_Obj writer;
        S.i.mp->Init(&writer, payload->data, payload->size);
        writer.allocator  = Private_Payload_Realloc;
        success          &= LTMessagePackPut(S.i.mp, &writer, (u8)0);
        success          &= LTMessagePackPut(S.i.mp, &writer, now.nNanoseconds);
        success          &= LTMessagePackPut(S.i.mp, &writer, level);
        success          &= Private_Write_String(&writer, section);
        success          &= Private_Write_String(&writer, tag);
        success          &= Private_Write_String(&writer, fmt);
        success          &= Private_Format_Write(&writer, fmt, args);
        if (level & MSG_FLAG_DUMP_THREADNAME) {
            LTThread thisThread  = S.i.thread->GetCurrentThread();
            success             &= LTMessagePackPut(S.i.mp, &writer, S.i.thread->GetPriority(thisThread));
            char thread_name[kLTThread_MaxNameBuff];
            S.i.thread->GetName(thisThread, thread_name);
            success &= LTMessagePackPut(S.i.mp, &writer, thread_name);
        }
        if (level & MSG_FLAG_DUMP_MEMSTATS) {
            u64 memstat  = LT_GetCore()->SnapshotMemstat();
            success     &= LTMessagePackPut(S.i.mp, &writer, memstat);
        }

        payload = Private_Payload_Finish(&writer);
        if (!success) {
            payload->used = 0;
        }
        Private_Payload_Publish(payload, flags);

        if (newSection) ltstring_destroy(newSection);
    }
}

static bool Private_Init(void) {
    LTSystemLoggerImpl_EnableConsole(true);
    LTSystemLoggerImpl_SetFilterLevel(LTSL_DEFAULT_LOGLEVEL);

    LT_GetCore()->SetLogHookFunction(Private_VPrint);
    LT_GetCore()->SetTraceHookFunction(Private_Trace);
    return true;
}

static void Private_Cleanup(void) {
    LT_GetCore()->SetLogHookFunction(NULL);
    LT_GetCore()->SetTraceHookFunction(NULL);
    LTLogConsumer_Obj *obj = S.consumer.dummy.next;
    S.consumer.dummy.next  = NULL;
    while (obj) {
        LTLogConsumer_Obj *next = obj->next;
        if (obj->releaseProc) {
            obj->releaseProc(kLTThread_ReleaseReason_Because, obj->context);
        }
        /* release the handle reservation we made in AddConsumer */
        LT_GetCore()->ReleaseHandlePrivateData(obj->handle, obj);
        LT_GetCore()->DestroyHandle(obj->handle);
        obj = next;
    }
}

/******************************************************************************
 * @name Library Interface
 * @{
 */

static void LTSystemLoggerImpl_ConfigureMemory(const void *rodata_start, const void *rodata_end) {
    S.rodata.start = rodata_start;
    S.rodata.end   = rodata_end;
}

static void LTSystemLoggerImpl_Flush(void) {
    if (LT_GetCore()->InsideInterruptContext()) return;
    LTThread currentThread = S.i.thread->GetCurrentThread();
    if (currentThread == S.consumer.thread) {
        static int safeToRecurse = 1;
        if (safeToRecurse) {
            safeToRecurse = 0;
            Private_Consumer_Process(NULL);
            safeToRecurse = 1;
        }
        return;
    }
#if LTLOG_CONFIG_ALLOW_PRIORITY_BOOST
    // establish we are the ones flushing so we don't stomp with thread priorities on other flushers
    // we should always succeed on the first try, but just in case...
    int  retryCount     = 0;
    bool bWeAreFlushing = LTAtomic_CompareAndExchange(&S.buf.flush, 0, 1);
    while ((!bWeAreFlushing) && (retryCount++ < 100)) {
        S.i.thread->Sleep(LTTime_Milliseconds(10));
        bWeAreFlushing = LTAtomic_CompareAndExchange(&S.buf.flush, 0, 1);
    }
    if (bWeAreFlushing) {
        u8 currentThreadPriority  = S.i.thread->GetPriority(currentThread);
        u8 consumerThreadPriority = S.i.thread->GetPriority(S.consumer.thread);
        S.i.thread->QueueTaskProcIfRequired(S.consumer.thread, Private_Consumer_Process, NULL, NULL);
        // if the consumer thread already isn't higher than us, set it to be so
        // when control returns to this thread after the call to SetPriority, logger is flushed
        if (consumerThreadPriority <= currentThreadPriority) {
            S.i.thread->SetPriority(S.consumer.thread, currentThreadPriority + 1);
        }
    }
#else
    LTAtomic_Store(&S.buf.flush, 1);
    S.i.thread->QueueTaskProcIfRequired(S.consumer.thread, Private_Consumer_Process, NULL, NULL);
    for (int i = 0; i < 100; ++i) {
        if (Private_Msg_Empty() || !LTAtomic_Load(&S.buf.flush)) break;
        S.i.thread->Sleep(LTTime_Milliseconds(10));
    }
#endif
}

static void LTSystemLoggerImpl_GetInternalBuffer(const u8 **ppBuffer, LT_SIZE *pSize) {
    *ppBuffer = (const u8 *)&S.buf;
    *pSize    = sizeof(LTLogBuffer);
}

static void LTSystemLoggerImpl_Write(const u8 *data, u32 size, LTLogBufferIndex flags, bool publish) {
    if (data) {
        LTLogBufferSpan *payload = Private_Payload_Alloc(size, true);
        if (!payload) {
            return;
        }
        lt_memcpy(payload->data, data, size);
        payload->used = size;
        Private_Msg_Store(payload, flags);
    }
    if (publish) {
        S.i.thread->QueueTaskProcIfRequired(S.consumer.thread, Private_Consumer_Process, NULL, NULL);
    }
}

static void LTSystemLoggerImpl_VPrint(const char *section,
                                      const char *tag,
                                      u32         flags,
                                      const char *fmt,
                                      lt_va_list  args) {
    Private_VPrint(section, tag, flags, fmt, args);
}

static void LTSystemLoggerImpl_Print(const char *section, const char *tag, u32 flags, const char *fmt, ...) {
    lt_va_list args;

    lt_va_start(args, fmt);
    Private_VPrint(section, tag, flags, fmt, args);
    lt_va_end(args);
}

static void LTSystemLoggerImpl_Trace(LTTraceStream *stream, LTTracePayloadType type, ...) {
    lt_va_list args;

    lt_va_start(args, type);
    Private_Trace(stream, type, args);
    lt_va_end(args);
}

static bool LTSystemLoggerImpl_EncodeRecord(LTLogRecord *record) {
    LTLogBufferSpan *span = Private_Record_Encode(record, LT_GetCore()->GetKernelTime());
    if (span != NULL) {
        Private_Payload_Publish(span, kLTCore_LogFlags_LogToServer);
    }
    return span != NULL;
}

static void LTSystemLoggerImpl_AddRecord(LTLogRecord *record, LTTime period) {
    if (period.nNanoseconds == 0) {
        LTSystemLoggerImpl_EncodeRecord(record);
    } else {
        bool locked = LTLogRecordLock(record);
        LT_ASSERT(locked);
        record->period = period;
        LTLogRecordUnlock(record);

        Private_Record_Schedule(record, record->period);
    }
}

static void LTSystemLoggerImpl_RemoveRecord(LTLogRecord *record) {
    S.i.thread->KillTimer(S.consumer.thread, Private_Record_Process, record);
}

static LTLogConsumer LTSystemLoggerImpl_AddConsumer(LTLogCallback                   cb,
                                                    LTThread_TaskProc              *setupProc,
                                                    LTThread_ClientDataReleaseProc *releaseProc,
                                                    void                           *context) {
    LTLogConsumer consumer = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTLogConsumer, sizeof(LTLogConsumer_Obj));
    if (consumer) {
        LTLogConsumer_Obj *obj = LT_GetCore()->ReserveHandlePrivateData(consumer); /* leave it reserved until
                                                                                      Private_Consumer_Remove is called
                                                                                    */
        if (obj) {
            obj->handle      = consumer;
            obj->setupProc   = setupProc;
            obj->context     = context;
            obj->callback    = cb;
            obj->releaseProc = releaseProc;
            S.i.thread->QueueTaskProc(S.consumer.thread, Private_Consumer_Call_Setup, Private_Consumer_Add, obj);
        }
    }
    return consumer;
}

static void LTSystemLoggerImpl_RemoveConsumer(LTLogConsumer consumer) {
    // Reserve the private data to get the object and cache and release, then
    // use the obj after the release. It's ok in this special circumstance because the obj
    // is still reserved until PrivateConsumer_Remove is called
    LTLogConsumer_Obj *obj = LT_GetCore()->ReserveHandlePrivateData(consumer);
    LT_GetCore()->ReleaseHandlePrivateData(consumer, obj);
    if (obj) S.i.thread->QueueTaskProc(S.consumer.thread, Private_Consumer_Dummy, Private_Consumer_Remove, obj);
}

static void LTSystemLoggerImpl_EnableConsole(bool enable) {
    if (S.config.isConsoleEnabled == enable) {
        return;
    }
    S.config.isConsoleEnabled = enable;
    if (enable) {
        S.consumer.console = LTSystemLoggerImpl_AddConsumer(Private_Console_Consumer, NULL, NULL, NULL);
    } else {
        LTSystemLoggerImpl_RemoveConsumer(S.consumer.console);
        S.consumer.console = 0;
    }
}

static bool LTSystemLoggerImpl_IsConsoleEnabled(void) {
    return S.config.isConsoleEnabled;
}

static void LTSystemLoggerImpl_SetFilterLevel(LTCore_LogFlags minLevel) {
    S.config.minLevel = minLevel;
}

static LTCore_LogFlags LTSystemLoggerImpl_GetFilterLevel(void) {
    return S.config.minLevel;
}

static void LTSystemLoggerImpl_DebugPrintStatistics(void) {
    LT_GetCore()->ConsoleStomp("\nindex: head=%lu(%lu) tail=%lu(%lu)\n",
                               LT_Pu32(LTAtomic_Load(&S.buf.msg.head)),
                               LT_Pu32(LTAtomic_Load(&S.buf.msg.head) & S.buf.msg.mask),
                               LT_Pu32(LTAtomic_Load(&S.buf.msg.tail)),
                               LT_Pu32(LTAtomic_Load(&S.buf.msg.tail) & S.buf.msg.mask));
    LT_GetCore()->ConsoleStomp("payload: head=%lu(%lu) tail=%lu(%lu)\n",
                               LT_Pu32(LTAtomic_Load(&S.buf.payload.head)),
                               LT_Pu32(LTAtomic_Load(&S.buf.payload.head) & S.buf.payload.mask),
                               LT_Pu32(LTAtomic_Load(&S.buf.payload.tail)),
                               LT_Pu32(LTAtomic_Load(&S.buf.payload.tail) & S.buf.payload.mask));
    LTLogBufferSpan *span = LTRingPeek(&S.buf.payload, 0);
    if (span) {
        LT_GetCore()->ConsoleStomp("first payload: size=%lu used=%lu\n", LT_Pu32(span->size), LT_Pu32(span->used));
    } else {
        LT_GetCore()->ConsoleStomp("payload empty");
    }
    LT_GetCore()->ConsoleStomp("all indices...\n");
    for (u32 i = 0; i <= S.buf.msg.mask; ++i) {
        LTLogBufferIndex *index = LTRingAt(&S.buf.msg, i);
        if (!index) {
            LT_GetCore()->ConsoleStomp("\tINDEX MISMATCH AT %lu\n", LT_Pu32(i));
            break;
        }
        span = LTRingAt(&S.buf.payload, index->offset);
        LT_GetCore()->ConsoleStomp("\t%4ld: offset=%lu(%lu) ready=%lu size=%lu used=%lu\n",
                                   LT_Pu32(i),
                                   LT_Pu32(index->offset),
                                   LT_Pu32(index->offset & S.buf.payload.mask),
                                   LT_Pu32(index->isReady),
                                   LT_Pu32(span->size),
                                   LT_Pu32(span->used));
    }
#if LTLOG_CONFIG_DEBUG_ALLOCATION
    LT_GetCore()->ConsoleStomp("\nallocations: %llu\n",
                               LT_Pu64(S.debug.allocationsLow) | (LT_Pu64(S.debug.allocationsHigh) << 32));
    LT_GetCore()->ConsoleStomp("bytes:       %llu\n",
                               LT_Pu64(S.debug.allocatedBytesLow) | (LT_Pu64(S.debug.allocatedBytesHigh) << 32));
    LT_GetCore()->ConsoleStomp("extensions:  %llu\n",
                               LT_Pu64(S.debug.extensionsLow) | (LT_Pu64(S.debug.extensionsHigh) << 32));
    LT_GetCore()->ConsoleStomp("wasted:      %llu\n",
                               LT_Pu64(S.debug.wastedBytesLow) | (LT_Pu64(S.debug.wastedBytesHigh) << 32));
    LT_GetCore()->ConsoleStomp("failures:    %lu\n", LT_Pu32(S.debug.failedAllocations));
    LT_GetCore()->ConsoleStomp("watermark:   %lu\n", LT_Pu32(LTAtomic11_Exchange(&S.debug.watermark, LT_S32_MAX)));
    LT_GetCore()->ConsoleStomp("boosts:      %lu\n", LT_Pu32(S.debug.boosts));
#endif
}

static bool LTSystemLoggerImpl_LibInit(void) {
    lt_memset(&S, 0, sizeof(S));

    // Read section prefix. It is fine if fail for any reason.
    S.productConfig = lt_openlibrary(LTProductConfig);
    if (S.productConfig) {
        u32 section = S.productConfig->GetLibraryConfigSection("LTSystemLogger");
        if (section) {
            S.sectionPrefix = S.productConfig->ReadString(section, "sectionPrefix");
        }
    }

#if LTLOG_CONFIG_DEBUG_ALLOCATION
    lt_memset(&S.debug, 0, sizeof(S.debug));
    S.debug.watermark = LT_S32_MAX;
#endif

    // Initialize the internal config values
#if LTLIBRARY_INCLUDE_LTTRACE
    LTTRACE_STREAM(desc).state = 0;
    LTBitfieldSet(LTTRACE_STREAM(desc).state, 1, kLTTrace_State_IdMask, kLTTrace_State_IdShift);
#endif
    S.config.minLevel         = LTSL_DEFAULT_LOGLEVEL;
    S.config.isConsoleEnabled = 0;

    S.i.thread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    if (!S.i.thread) {
        return false;
    }
    S.i.mp = lt_openlibrary(LTUtilityMessagePack);
    if (!S.i.mp) {
        return false;
    }
    LTRingInit(&S.buf.msg, S.buf.msgBuffer, sizeof(S.buf.msgBuffer) / sizeof(S.buf.msgBuffer[0]));
    LTRingInit(&S.buf.payload, S.buf.payloadBuffer, sizeof(S.buf.payloadBuffer) / sizeof(S.buf.payloadBuffer[0]));
    S.consumer.thread = LT_GetCore()->CreateThread("Logger");
    if (!S.consumer.thread) {
        lt_closelibrary(S.i.mp);
        return false;
    }
    S.i.thread->SetStackSize(S.consumer.thread, LTLOG_CONFIG_CONSUMER_STACK_SIZE);
    S.i.thread->SetPriority(S.consumer.thread, LTLOG_CONFIG_THREAD_PRIORITY);
    S.i.thread->Start(S.consumer.thread, Private_Init, Private_Cleanup);

    // A timer to automatically run the consumer for inputs which don't schedule it explicitly.
    S.i.thread->SetTimer(S.consumer.thread, LTTime_Milliseconds(250), Private_Consumer_Process, NULL, NULL);

    Private_Shell_Init();

    return true;
}

static void LTSystemLoggerImpl_LibFini(void) {
    Private_Shell_Fini();
    LT_GetCore()->SetLogHookFunction(NULL);
    LT_GetCore()->SetTraceHookFunction(NULL);
    S.i.thread->Destroy(S.consumer.thread);
    S.consumer.thread = 0;

    lt_closelibrary(S.i.mp);
    S.i.mp = NULL;
    lt_closelibrary(S.productConfig);
}

#if LTLOG_CONFIG_DEBUG_EXECUTABLE
static int LTSystemLoggerImpl_Run(int argc, const char **argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    LT_ASSERT(argc > 2);
    u32 count = lt_strtou32(argv[2], NULL, 10);

    for (u32 j = 0; j < count; ++j) {
        LTSystemLoggerImpl_Print("abcd.asdf",
                                 "asd",
                                 kLTCore_LogFlags_LogTypeDebugLog | kLTCore_LogFlags_LogToConsole,
                                 "Hello %s",
                                 ", logger!");
    }

    return 0;
}
#endif

define_LTTRACE_STREAMS((desc));

// clang-format off
#if LTLOG_CONFIG_DEBUG_EXECUTABLE
define_LTLIBRARY_ROOT_INTERFACE(LTSystemLogger, LTSystemLoggerImpl_Run)
#else
define_LTLIBRARY_ROOT_INTERFACE(LTSystemLogger)
#endif
    .ConfigureMemory = LTSystemLoggerImpl_ConfigureMemory,
    .Flush = LTSystemLoggerImpl_Flush,
    .Write = LTSystemLoggerImpl_Write,
    .Print = LTSystemLoggerImpl_Print,
    .VPrint = LTSystemLoggerImpl_VPrint,
    .Trace = LTSystemLoggerImpl_Trace,
    .EncodeRecord = LTSystemLoggerImpl_EncodeRecord,
    .AddRecord = LTSystemLoggerImpl_AddRecord,
    .RemoveRecord = LTSystemLoggerImpl_RemoveRecord,
    .AddConsumer = LTSystemLoggerImpl_AddConsumer,
    .RemoveConsumer = LTSystemLoggerImpl_RemoveConsumer,
    .EnableConsole = LTSystemLoggerImpl_EnableConsole,
    .IsConsoleEnabled = LTSystemLoggerImpl_IsConsoleEnabled,
    .SetFilterLevel = LTSystemLoggerImpl_SetFilterLevel,
    .GetFilterLevel = LTSystemLoggerImpl_GetFilterLevel,
    .GetInternalBuffer = LTSystemLoggerImpl_GetInternalBuffer,
    .DebugPrintStatistics = LTSystemLoggerImpl_DebugPrintStatistics,
LTLIBRARY_DEFINITION;
// clang-format on

/** @} */

#include "LTSystemLogger_Buffer.c"
#include "LTSystemLogger_Console.c"
#include "LTSystemLogger_Consumer.c"
#include "LTSystemLogger_Format.c"
#include "LTSystemLogger_Record.c"
#include "LTSystemLogger_Shell.c"
#include "LTSystemLogger_Trace.c"
