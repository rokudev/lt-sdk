/******************************************************************************
 * LTSystemLogger_Private.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LTSYSTEMLOGGER_PRIVATE_H
#define LTSYSTEMLOGGER_PRIVATE_H

#include "LTSystemLogger_Config.h"

#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTTime.h>
#include <lt/product/config/LTProductConfig.h>
#include <lt/system/logger/LTSystemLogger.h>
#include <lt/system/logger/LTSystemLogger_Records.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/utility/buffer/LTRing.h>

/**
 * @defgroup ltsystem_logger LTSystemLogger
 * @ingroup ltsystem
 * @brief High performance ISR-safe logging interface
 * @{
 */

// output colors
// helpful link for VT100 codes: http://domoticx.com/terminal-codes-ansivt100/
#define LTLOG_COLOR_VERBOSE         "\x1b[32m"
#define LTLOG_COLOR_DEBUG           "\x1b[36m"
#define LTLOG_COLOR_ASSERT          "\x1b[35m"
#define LTLOG_COLOR_YELLOWALERT     "\x1b[33m"
#define LTLOG_COLOR_REDALERT        "\x1b[31m"
#define LTLOG_COLOR_DEFAULT         "\x1b[39m"
#define LTLOG_COLORCOMMAND_NUMCHARS 5

declare_LTTRACE_STREAM(desc);

/******************************************************************************
 * @name Types
 * @{
 */

struct LTLogConsumerWorkingData {
    s64         timestamp;
    char       *buffer;
    char       *bufferEnd;
    const char *specStart;
    char        spec[32];
};

typedef struct {
    u16 size;
    /**< Size of this segment */
    u16 used;
    /**< Used size of this segment */
    u8  data[];
    /**< Flexible size payload */
} LTLogBufferSpan;

LT_STATIC_ASSERT(sizeof(LTLogBufferSpan) == 4, "Span has become too large");
LT_STATIC_ASSERT(LTLOG_CONFIG_BUFFER_SIZE <= ((1 << 14) - 1), "uelog_buffer_t::segment_next has insufficient range");

typedef struct {
    _Alignas(LTLOG_CONFIG_ALIGNMENT) LTLogBufferIndex msgBuffer[LTLOG_CONFIG_MSG_COUNT];
    /**< Buffer of fixed-size data for small messages */
    LTRingBufferAtomic msg;
    /**< Ring buffer managing @ref msgBuffer */

    _Alignas(LTLOG_CONFIG_ALIGNMENT) u8 payloadBuffer[LTLOG_CONFIG_BUFFER_SIZE];
    /**< Buffer for allocating flexible payload blocks from */
    LTRingBufferAtomic payload;
    /**< Ring buffer managing @ref payloadBuffer */
    u32                payloadOffset;

    LTAtomic flags;
    /**< Atomic flags; generally used for error handling */

    char format[LTLOG_CONFIG_FORMAT_BUFFER_SIZE];
    /**< Working buffer for string formatting */

    LTAtomic flush;
    /**< Flag storing a request for the consumer thread to flush the buffer */

#if LTLOG_CONFIG_DEBUG_ALLOCATION
    u32      payloadAlloc[LTLOG_CONFIG_MSG_COUNT];
    LTAtomic payloadAllocIndex;
#endif
} LTLogBuffer;
/**< Structure containing all shared message buffers
 *
 * The buffer sizes and allocation strategy **guarantee** that @ref Buffer will
 * always be exhausted before @ref Msg is exhausted.  Internal logic depends on
 * this assumption which should be guaranteed by static assertions in the
 * configuration file.
 *
 * # Alternative Designs
 *
 * Rather than using a separate buffer for dynamically sized data, we could
 * allocate any number of messages and just pack them sequentially.  While this
 * would work, I decided against it because:
 *
 * 1. There doesn't seem to be a straightforward way to protect against lapping.
 *
 * 2. It complicates the logic of message completion.  Currently a message can
 * be marked as complete atomically, but with this alternative design all
 * segments of a message would be individually complete, but marking the whole
 * chain as complete atomically is not possible.
 *
 * 3. It imposes significant overhead for the expected use case where the format
 * string itself will be stored in the message.
 */

typedef struct LTLogConsumer_Obj LTLogConsumer_Obj;

struct LTLogConsumer_Obj {
    LTLogConsumer handle;
    /**< My own handle */

    LTThread_TaskProc *setupProc;
    /**< User-provided thread setup proc */

    void *context;
    /**< User-provided context data */

    LTLogCallback callback;
    /**< Callback associated with this consumer */

    struct LTLogConsumer_Obj *next;
    /**< Used internally to locate the next consumer */

    LTLogConsumerWorkingData *working;
    /**< Working torage for reading message header */

    LTThread_ClientDataReleaseProc *releaseProc;
    /**< Client callback when the consumer is destroyed */
};

#define LTLOG_BUFFER_FLAG_DROPPED (1 << 0)

/** @} */

/******************************************************************************
 * @name Global Data
 * @{
 */

static struct {
    struct {
        ILTThread            *thread;
        LTUtilityMessagePack *mp;
    } i;

    struct {
        const char *start;
        const char *end;
    } rodata;

    struct {
        LTThread          thread;
        LTLogConsumer_Obj dummy;
        LTLogConsumer     console;
    } consumer;

    LTLogBuffer          buf;
    volatile LTLogConfig config;
    /**< Configuration settings
     *
     * This is not thread-safe beyond the inherent thread-safety of getting and setting u32 values
     * on active platforms.  That is, there should be no corrupt intermediate values.
     */

    const char      *sectionPrefix; /* Global section prefix from product config, if not NULL. */
    LTProductConfig *productConfig;
    LTSystemShell   *shell;

#if LTLOG_CONFIG_DEBUG_ALLOCATION
    struct {
        LTAtomic allocationsLow;
        LTAtomic allocationsHigh;
        LTAtomic allocatedBytesLow;
        LTAtomic allocatedBytesHigh;
        LTAtomic extensionsLow;
        LTAtomic extensionsHigh;
        LTAtomic wastedBytesLow;
        LTAtomic wastedBytesHigh;
        LTAtomic failedAllocations;
        LTAtomic watermark;
        LTAtomic boosts;
    } debug;
#endif
} S;

#define MSG_FLAG_READY ((u32)((LTLogBufferIndex){.isReady = 1}.value))

#define MSG_FLAG_DUMP_THREADNAME ((u32)0b100000)
#define MSG_FLAG_DUMP_MEMSTATS   ((u32)0b1000000)

/** @} */

/******************************************************************************
 * @name Private Function Declarations
 * @{
 */

/* LTSystemLogger_Impl.c */

static void LTSystemLoggerImpl_Flush(void);

static void LTSystemLoggerImpl_Trace(LTTraceStream *stream, LTTracePayloadType type, ...);

static void LTSystemLoggerImpl_EnableConsole(bool enable);

static void LTSystemLoggerImpl_SetFilterLevel(LTCore_LogFlags minLevel);

static bool Private_Write_String(LTMessagePack_Obj *w, const char *s);

static bool Private_Parse_String(LTMessagePack_Value v, const char **str);

static bool Private_Read_String(LTMessagePack_Obj *r, const char **str);

static bool Private_Read_OffsetString(LTMessagePack_Obj *r, const char **begin, const char **end, LT_PTRDIFF *offset);

/* LTSystemLogger_Buffer.c */

#if !LTLOG_CONFIG_ALLOW_PRIORITY_BOOST
static bool Private_Msg_Empty(void);
#endif

static void Private_Msg_Store(LTLogBufferSpan *payload, LTLogBufferIndex index);

static void Private_Payload_Publish(LTLogBufferSpan *payload, u32 flags);

static LTLogBufferIndex *Private_Msg_Consume(void);

static void Private_Msg_Release(LTLogBufferIndex *index);

static LTLogBufferSpan *Private_Payload_Alloc(LT_SIZE size, bool scheduleOnFailure);

static LTLogBufferSpan *Private_Payload_Get(LTLogBufferIndex index);

static LTLogBufferSpan *Private_Payload_Finish(struct LTMessagePack_Obj *obj);

static u8 *Private_Payload_Realloc(struct LTMessagePack_Obj *obj, LT_SIZE requested);

/* LTSystemLogger_Console.c */

static void Private_Console_Consumer(const ILTLogConsumer *api, LTLogConsumer consumer, const LTLogMessage *msg);

static LTLog_Error Private_Consumer_PrintFullLog(LTLogConsumer       consumer,
                                                 const LTLogMessage *msg,
                                                 bool                bSetColor,
                                                 char               *buffer,
                                                 LT_SIZE            *size);

/* LTSystemLogger_Consumer.c */

static void Private_Consumer_Dummy(void *pClientData);
static void Private_Consumer_Call_Setup(void *pClientData);
static void Private_Consumer_Add(LTThread_ReleaseReason releaseReason, void *pClientData);
static void Private_Consumer_Remove(LTThread_ReleaseReason releaseReason, void *pClientData);

static void Private_Consumer_Process(void *context);

static void *Private_Consumer_GetContext(LTLogConsumer consumer);

static LTLog_Error Private_Consumer_Print(LTLogConsumer       consumer,
                                          const LTLogMessage *msg,
                                          bool                bFullLog,
                                          bool                bSetColor,
                                          char               *buffer,
                                          LT_SIZE            *size);

static LTLog_Error Private_Consumer_Copy(LTLogConsumer consumer, const LTLogMessage *msg, u8 *buffer, LT_SIZE *size);

static LT_SIZE Private_Consumer_GetMessageLength(LTLogConsumer consumer, const LTLogMessage *msg);

static LT_SIZE Private_Consumer_GetData(LTLogConsumer consumer, const LTLogMessage *msg, const u8 **buffer);

/* LTSystemLogger_Format.c */

static bool Private_Format_Write(LTMessagePack_Obj *w, const char *fmt, lt_va_list args);

static LT_SIZE Private_Format_Read(LTLogConsumer_Obj *consumer, const LTLogMessage *msg);

/* LTSystemLogger_Record.c */

static LTLogBufferSpan *Private_Record_Encode(LTLogRecord *record, LTTime now);

static void Private_Record_Schedule(LTLogRecord *record, LTTime period);

static void Private_Record_Process(void *context);

/* LTSystemLogger_Shell.c */

static void Private_Shell_Init(void);

static void Private_Shell_Fini(void);

/* LTSystemLogger_Trace.c */

static void Private_Trace(LTTraceStream *stream, LTTracePayloadType type, lt_va_list args);

/** @} */

// clang-format off
define_LTLIBRARY_INTERFACE(ILTLogConsumer)
    .GetContext       = Private_Consumer_GetContext,
    .Copy             = Private_Consumer_Copy,
    .Print            = Private_Consumer_Print,
    .GetMessageLength = Private_Consumer_GetMessageLength,
    .GetData          = Private_Consumer_GetData,
LTLIBRARY_DEFINITION
// clang-format on

#endif
