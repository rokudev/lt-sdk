/******************************************************************************
 * LTSystemLogger.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_LOGGER_LTSYSTEMLOGGER_H
#define ROKU_LT_INCLUDE_LT_LOGGER_LTSYSTEMLOGGER_H

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/utility/messagepack/LTUtilityMessagePack.h>
#include <lt/utility/messagepack/LTUtilityMessagePack_Helpers.h>

LT_EXTERN_C_BEGIN

#define LT_LOG_INVALID_FORMAT_OFFSET LT_SSIZE_MAX

/**
 * @defgroup ltsystem_logger LTSystemLogger
 * @ingroup ltsystem
 * @{
 *
 * @brief High speed deferred logging
 */

/* Forward Declarations */

typedef struct LTLogMessage LTLogMessage;

typedef struct LTLogConsumerWorkingData LTLogConsumerWorkingData;

typedef struct LTLogRecord LTLogRecord;

typedef const struct ILTLogConsumerApi ILTLogConsumer;

typedef LTHandle LTLogConsumer;

/******************************************************************************
 * @name Type Definitions
 * @{
 */

typedef enum {
    LTLog_Error_None = 0,     ///< No error set
    LTLog_Error_BadFormat,    ///< Format string is malformed
    LTLog_Error_BadEncoding,  ///< Bad encoding encountered
} LTLog_Error;

typedef bool (*LTLogRecordEncoder)(const LTLogRecord *record,
                                   LTUtilityMessagePack *mp,
                                   LTMessagePack_Obj *writer);
/**< Callback for encoding a record data structure
 *
 * @param record The record that is being encoded
 * @param mp MsgPack interface
 * @param writer MsgPack writer state
 */

typedef void (*LTLogCallback)(const ILTLogConsumer *api,
                              LTLogConsumer consumer,
                              const LTLogMessage *msg);
/**< Callback interface for consuming log messages
 *
 * @param api Consumer api, passed for convenience
 * @param consumer Consumer being processed
 * @param msg Message that should be processed by the consumer
 */

typedef struct LTLogConfig {
    u32 minLevel;
    /**< Log level below which all logs are dropped */
    u32 isConsoleEnabled;
    /**< Flag indicating whether the default console logger is enabled */
} LTLogConfig;

typedef union LTLogBufferIndex {
    struct {
        u16 offset;
        /**< Offset of the payload within the payload buffer */
        u16 toConsole   : 1;
        /**< Print log to the console */
        u16 toServer    : 1;
        /**< Send log to the server */
        u16 isForwarded : 1;
        /**< Log has been forwarded from another device */
        u16 isReady     : 1;
        /**< Message contains a valid payload offset */
        u16 isFinished  : 1;
        /**< Message has been consumed but not released */
        u16 reserved    : 11;
    };
    LTAtomic atomicValue;
    /**< Union alias for accessing message in a single atomic operation */
    u32 value;
    /**< Union alias for non-atomic access */
} LTLogBufferIndex;
/**< Identifier for a single encoded message */

LT_STATIC_ASSERT(sizeof(LTLogBufferIndex) == 4, "Header must be 32 bits");

typedef struct LTLogMessage {
    LTCore_LogFlags level;
    /**< Log level the message was recorded at */
    u32 recordType;
    /**< Type of record encoded, or 0 for a print */
    LTTime timestamp;
    /**< Timestamp when the message was recorded */
    LTLogBufferIndex *index;
    /**< Buffer index where the message is stored */
    struct {
        const char *section;
        /**< Section identifier of the print */
        const char *tag;
        /**< Tag identifier of the print */
        const char *format;
        /**< Format string of the message */
        const char *formatEnd;
        /**< End of the format string of the message */
        LT_PTRDIFF formatOffset;
        /**< Offset of format string
         *
         * If this is 0, the format string was dynamically generated and no
         * offset is available.
         */
        u64 memstat;
        /**< if non-zero, contains memstat snapshot retrieved at log time using
         * LTCore->SnapshotMemstat() */
        LTMessagePack_Obj args;
        /**< Copy of the messagepack reader positioned at the args */
        const char *threadName;
        /**< Optional name of the thread from which the log was sent */
        u8 threadPriority;
        /**< Optional priority of the thread from which the log was sent */
    } printPayload;
    /**< Payload for messages from LTSystemLogger::Print */
} LTLogMessage;

/** Record definition header */
typedef struct LTLogRecord {
    u32 type;
    /**< Unique identifier for this record layout */
    LTAtomic cycles;
    /**< Atomic flag to prevent simultaneous access */
    LTTime period;
    /**< Publication period for this records
     *
     * A period of 0 causes the record to be immediately published a single time on calling @ref
     * LTSystemLogger::AddRecord.
     */
    LTLogRecordEncoder encoder;
    /**< Encoder registered for this record */
    u32 payloadSize;
    /**< Size of the record's payload
     *
     * The payload must immediately follow the header in memory.  This is most easily achieved using
     * the LTLOG_RECORD macro to declare a record of a specific type.
     */
    void *payload;
    /**< Address of the record payload */
} LTLogRecord;

LT_STATIC_ASSERT((sizeof(LTLogRecord) & 7) == 0,
                 "Record header must be multiple of 8 bytes for alignment reasons");

/** @} */

/******************************************************************************
 * @name Library Interfaces
 * @{
 */

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTSystemLogger, 1);

TYPEDEF_LTLIBRARY_INTERFACE(ILTLogConsumer, 1);

struct LTSystemLoggerApi {
    INHERIT_LIBRARY_BASE

    void (*ConfigureMemory)(const void *rodata_start, const void *rodata_end);
    /**< Configure the logger to recognize a section as .rodata
     *
     * Any string residing in this section will be encoded as an offset within
     * this section rather than a full string.
     */

    void (*Flush)(void);
    /**< Flush logs from the buffer by giving the consumer a high priority
     *
     * It should be noted that there is no synchronization with log producers, so this function
     * makes no guarantees about race conditions.
     */

    void (*Write)(const u8 *data, u32 size, LTLogBufferIndex flags, bool publish);
    /**< Write formatted data directly to the internal buffer
     *
     * @param data Pointer to the data to be written; may be null to request a publish.
     * @param size Size of the data to be written
     * @param flags Index with the appropriate flags filled; other members will be ignored.
     * @param publish If true, the message will be immediately published to the consumers.
     *
     * @warning Failure to write perfectly formatted data will result in undefined behavior.
     */

    LT_PRINTF_FORMAT_FUNCTION(4)
    void (*Print)(const char *section, const char *tag, u32 flags, const char *fmt, ...);
    /**< Store a printf-formatted log message for deferred processing */

    void (*VPrint)(const char *section,
                   const char *tag,
                   u32 flags,
                   const char *fmt,
                   lt_va_list args);
    /**< Store a printf-formatted log message for deferred processing */

    void (*Trace)(LTTraceStream *stream, LTTracePayloadType type, ...);
    /**< Low overhead tracepoint
     *
     * User is responsible for ensuring variable arguments are 32-bits in size.
     */

    bool (*EncodeRecord)(LTLogRecord *record);
    /**< Immediately encode a record */

    void (*AddRecord)(LTLogRecord *record, LTTime period);
    /**< Begin logging a record at a given interval
     *
     * If a record is submitted a second time with a different period, it will be rescheduled with
     * the new period and its timer reset.
     */

    void (*RemoveRecord)(LTLogRecord *record);
    /**< Stop publishing a record that was previously added */

    LTLogConsumer (*AddConsumer)(LTLogCallback cb,
                                 LTThread_TaskProc *setupProc,
                                 LTThread_ClientDataReleaseProc cleanup,
                                 void *context);
    /**< Register a consumer interface to receive log messages
     *
     * @param cb Callback to be executed per message
     * @param setupProc (optional) Setup procedure to be executed on the logger thread
     * @param cleanup (optional) Procedure to run after the consumer is removed
     * @param context (optional) Context data to be passted to cb and setupProc
     * @returns An LTHandle to the newly created consumer; this must be released with RemoveConsumer
     *
     * The setup procedure will be executed on the consumer thread, allowing that thread to be used
     * for further concurrent operations.  Note that unlike most LT callback functions, this
     * callback will always be called on the log consumer thread context.
     */

    void (*RemoveConsumer)(LTLogConsumer consumer);
    /**< Unregister a consumer interface so that it no longer receives log
     * messages
     *
     * @returns true if the consumer was previously registered, false otherwise
     *
     * @warning This is blocking operation.
     */

    void (*EnableConsole)(bool enable);
    /**< Configure default console consume
     *
     * @param enable Enable the console consumer, which will print logs to console
     */
    bool (*IsConsoleEnabled)(void);
    /**< Test whether the console consumer is enabled */

    void (*SetFilterLevel)(LTCore_LogFlags minLevel);
    /**< Configure log filtering
     *
     * @param minLevel Minimum LogType level which will be accepted by the logger
     *
     * Any logs below minLevel will be immediately discarded.
     */

    LTCore_LogFlags (*GetFilterLevel)(void);
    /** Get currently configured log filter level */

    void (*GetInternalBuffer)(const u8 **buffer, LT_SIZE *size);
    /**< Expose the internal buffer for backing up to flash
     *
     * @param buffer Pointer to a location where the internal buffer address is saved
     * @param size   Pointer to a location where the size of the internal buffer is saved
     *
     * @warning The internal buffer is guaranteed to be internally consistent in the context of
     * the Fault Handler, when the scheduler is stopped and interrupts are disabled. Acquiring
     * access and backing up the log at any other times is discouraged.
     */

    void (*DebugPrintStatistics)(void);
    /**< Print debugging statistics
     *
     * This may do nothing in a release build.
     */
};
/**< Global interface for high speed logging */

struct ILTLogConsumerApi {
    INHERIT_INTERFACE_BASE

    void *(*GetContext)(LTLogConsumer consumer);
    /**< Get the user-provided context pointer */

    LTLog_Error (*Copy)(LTLogConsumer consumer, const LTLogMessage *msg, u8 *buffer, LT_SIZE *size);
    /**< Copy the message payload into a buffer
     *
     * @param[in] consumer Consumer performing the operation
     * @param[in] msg Message to be copied
     * @param[in] buffer Buffer that will be copied into
     * @param[in/out] size On input, capacity of the buffer, on output number of characters copied
     * @returns Result code, LTLog_Error_None if successful
     *
     * On failure the contents of buffer and size are undefined.
     */

    LTLog_Error (*Print)(LTLogConsumer consumer,
                         const LTLogMessage *msg,
                         bool bFullLog,
                         bool bSetColor,
                         char *buffer,
                         LT_SIZE *size);
    /**< Print a message into a character buffer
     *
     * @param[in] consumer Consumer performing the operation
     * @param[in] msg Message to be printed
     * @param[in] bFullLog   If the printed log includes preamble, section and tag.
     * @param[in] bSetColor  If set color to the printed log.
     * @param[in] buffer Buffer that will be printed into
     * @param[in/out] size On input, capacity of the buffer, on output number of characters printed
     * @return Failure code, if any
     *
     * On success the buffer will be null terminated and the size will be set to the number of valid
     * bytes in the buffer, excluding the null terminator.  On failure the contents of buffer and
     * size are undefined.  Running out of space in the buffer is not considered an error.
     */

    LT_SIZE (*GetMessageLength)(LTLogConsumer consumer, const LTLogMessage *msg);
    /**< Get the total length of all segments of a message
     *
     * @param[in] consumer Consumer performing the operation
     * @param[in] msg Message to be queried
     * @returns Number of bytes required to fully contain the message
     **/

    LT_SIZE (*GetData)(LTLogConsumer consumer, const LTLogMessage *msg, const u8 **buffer);
    /**< Get a pointer to the data in the log buffer
     *
     * @param[in] consumer Consumer message was passed to
     * @param[in] msg Message to query
     * @param[in/out] buffer Pointer that will set to the log buffer
     * @return Size of the buffer in bytes
     */

    void (*LockMessage)(LTLogConsumer consumer, const LTLogMessage *msg);
    /**< Mark a message as locked so that it won't be released until the consumer is done with it
     *
     * @param[in] consumer Consumer performing the operation
     * @param[in] msg Message to be locked
     **/

    void (*UnlockMessage)(LTLogConsumer consumer, const LTLogMessage *msg);
    /**< Unlock a message and release it if there are no remaining locks
     *
     * @param[in] consumer Consumer performing the operation
     * @param[in] msg Message to be unlocked
     **/
};

/** @} */

/******************************************************************************
 * @name Record Utility Functions
 * @{
 */

#define LTLOG_RECORD_LOCK_FLAG (1 << 31)

#define LTLOG_DEFINE_SIMPLE_RECORD(name, typearg, encoderarg)      \
    static LTLogRecord name = {.type        = typearg,             \
                               .cycles      = {0},                 \
                               .period      = {.nNanoseconds = 0}, \
                               .encoder     = encoderarg,          \
                               .payloadSize = 0,                   \
                               .payload     = NULL}

#define LTLOG_DEFINE_RECORD(name, typearg, payloadarg, encoderarg)          \
    static LTLogRecord name##_Record = {.type        = typearg,             \
                                        .cycles      = {0},                 \
                                        .period      = {.nNanoseconds = 0}, \
                                        .encoder     = encoderarg,          \
                                        .payloadSize = sizeof(*payloadarg), \
                                        .payload     = payloadarg}

LT_WARNUNUSEDRESULT
LT_INLINE bool LTLogRecordLock(LTLogRecord *record) {
    u32 cycles = LTAtomic_FetchOr(&record->cycles, LTLOG_RECORD_LOCK_FLAG);
    if (cycles & LTLOG_RECORD_LOCK_FLAG) return false;
    LTAtomic_Store(&record->cycles, (cycles + 1) | LTLOG_RECORD_LOCK_FLAG);
    return true;
}
/**< Lock a record so the consumer won't process it
 *
 * @returns true if the lock could be taken, false otherwise
 * @see LTLogRecordUnlock
 *
 * This is guaranteed non-blocking and ISR-safe.  The lock variable is owned by the producer and
 * treated as read-only by the consumer.  Only one producer may own the lock; additional attempts to
 * lock will fail with a `false` return value.  The canonical procedure for locking records is:
 *
 * ```c
 * if(LTLogRecordLock(&my_record)) {
 *     // Update the record
 *     LTLogRecordUnlock(&my_record);
 * }
 * ```
 */

LT_INLINE void LTLogRecordUnlock(LTLogRecord *record) {
    LTAtomic_FetchAnd(&record->cycles, ~LTLOG_RECORD_LOCK_FLAG);
}
/**< Unlock a previously locked record
 *
 * @see LTLogRecordLock
 */

/** @} */

/** @} */

LT_EXTERN_C_END

#endif
