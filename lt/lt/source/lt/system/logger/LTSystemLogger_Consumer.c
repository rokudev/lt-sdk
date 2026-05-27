/******************************************************************************
 * LTSystemLogger_Consumer.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTSystemLogger_Private.h"

static bool Private_Consumer_ProcessWorkingData(LTMessagePack_Obj *reader, LTLogMessage *msg) {
    LTLogBufferSpan *span = Private_Payload_Get(*msg->index);
    if (span->used == 0) {
        return false;
    }
    if (span->data[0] == 0xc7) {
        msg->recordType = LT_U32_MAX;
        return true;
    }
    S.i.mp->Init(reader, span->data, span->used);

    bool success  = true;
    success      &= LTMessagePackGet(S.i.mp, reader, &msg->recordType);
    success      &= LTMessagePackGet(S.i.mp, reader, &msg->timestamp.nNanoseconds);
    if (!msg->recordType) {
        LTMessagePack_Value args;
        success                &= LTMessagePackGet(S.i.mp, reader, &msg->level);
        success                &= Private_Read_String(reader, &msg->printPayload.section);
        success                &= Private_Read_String(reader, &msg->printPayload.tag);
        success                &= Private_Read_OffsetString(reader,
                                             &msg->printPayload.format,
                                             &msg->printPayload.formatEnd,
                                             &msg->printPayload.formatOffset);
        msg->printPayload.args  = *reader;
        success                &= S.i.mp->GetValue(reader, &args) == LTMessagePack_Type_Array;
        success                &= S.i.mp->SkipContainer(reader, &args);
        if (msg->level & MSG_FLAG_DUMP_THREADNAME) {
            success &= LTMessagePackGet(S.i.mp, reader, &msg->printPayload.threadPriority);
            success &= LTMessagePackGet(S.i.mp, reader, &msg->printPayload.threadName);
        } else {
            msg->printPayload.threadName = NULL;
        }
        if (msg->level & MSG_FLAG_DUMP_MEMSTATS) {
            success &= LTMessagePackGet(S.i.mp, reader, &msg->printPayload.memstat);
        } else {
            msg->printPayload.memstat = 0;
        }
    }

    return success;
}

static void Private_Consumer_Call_Setup(void *pClientData) {
    LTLogConsumer_Obj *consumer = pClientData;
    if (consumer->setupProc) consumer->setupProc(consumer->context);
}
static void Private_Consumer_Dummy(void *pClientData) {
    LT_UNUSED(pClientData);
}

static void Private_Consumer_Add(LTThread_ReleaseReason releaseReason, void *pClientData) {
    /* we do the add as a clientdata release proc, because while queueing asynchronously,
       we returned the handle already to the client.
       we have to get it into this list, even if the QueueTaskProc actually failed, so that it can
       be reclaimed since the only way we have to reclaim it is in remove below. It would be better
       to use an event for this - it handles the multiple registrants and all thse sticky cases */
    LT_UNUSED(releaseReason);
    LTLogConsumer_Obj *consumer = pClientData;
    consumer->next              = S.consumer.dummy.next;
    S.consumer.dummy.next       = consumer;
}

static void Private_Consumer_Remove(LTThread_ReleaseReason releaseReason, void *pClientData) {
    /* we do remove as a clientdata release proc because we always want it to be called */
    LT_UNUSED(releaseReason);
    LTLogConsumer_Obj *consumer = pClientData;
    LTLogConsumer_Obj *current  = &S.consumer.dummy;
    for (LTLogConsumer_Obj *next = current->next; next != NULL; next = next->next) {
        if (next == consumer) {
            current->next = next->next;
            if (next->releaseProc) {
                next->releaseProc(kLTThread_ReleaseReason_Because, next->context);
            }
            /* release the handle reservation we made in AddConsumer */
            LT_GetCore()->ReleaseHandlePrivateData(next->handle, next);
            LT_GetCore()->DestroyHandle(next->handle);
            next = NULL;
            break;
        }
        current = next;
    }
}

static void Private_Consumer_Process(void *context) {
    LT_UNUSED(context);

    static LTMessagePack_Obj reader         = {};
    static LTLogConsumerWorkingData working = {};
    LTLogMessage msg                        = {};

    for (;;) {
        msg.index = Private_Msg_Consume();
        if (!msg.index) {
            break;
        }

        if (Private_Consumer_ProcessWorkingData(&reader, &msg)) {
            for (LTLogConsumer_Obj *consumer = S.consumer.dummy.next; consumer != NULL; consumer = consumer->next) {
                consumer->working = &working;
                consumer->callback(&s_ILTLogConsumer, consumer->handle, &msg);
            }
        }
        Private_Msg_Release(msg.index);
    }

    // Every operation manipulating the DROPPED flag can be relaxed because the only requirements
    // are that (1) the operation is atomic, and (2) the consumer eventually sees changes from the
    // producers.
    if (LTAtomic_FetchAnd(&S.buf.flags, ~LTLOG_BUFFER_FLAG_DROPPED) & LTLOG_BUFFER_FLAG_DROPPED) {
        LT_GetCore()->ConsoleStompString("\n" LTLOG_COLOR_REDALERT
                                         "!!! logging: dropped" LTLOG_COLOR_DEFAULT "\n");
    }
    LTAtomic_Store(&S.buf.flush, 0);
#if LTLOG_CONFIG_ALLOW_PRIORITY_BOOST
    S.i.thread->SetPriority(S.consumer.thread, LTLOG_CONFIG_THREAD_PRIORITY);
#endif
}

/******************************************************************************
 * @name Consumer API
 * @{
 */

static void *Private_Consumer_GetContext(LTLogConsumer consumer) {
    LTLogConsumer_Obj *obj = LT_GetCore()->ReserveHandlePrivateData(consumer);
    void *context          = obj ? obj->context : NULL;
    if (obj) LT_GetCore()->ReleaseHandlePrivateData(consumer, obj);
    return context;
}

static LTLog_Error Private_Consumer_Print(LTLogConsumer consumer,
                                          const LTLogMessage *msg,
                                          bool bFullLog,
                                          bool bSetColor,
                                          char *buffer,
                                          LT_SIZE *size) {
    if (msg->recordType) {
        // There is no meaningful way to print records.
        return 0;
    }

    LTLogConsumer_Obj *obj = LT_GetCore()->ReserveHandlePrivateData(consumer);
    if (obj == NULL) {
        // There is no meaningful way to print without the consumer object
        return 0;
    }

    LTLog_Error result = 0;
    if (!bFullLog) {
        obj->working->buffer    = buffer;
        obj->working->bufferEnd = buffer + *size;
        result = Private_Format_Read(obj, msg);
        *size = obj->working->bufferEnd - obj->working->buffer;
    } else {
        result = Private_Consumer_PrintFullLog(consumer, msg, bSetColor, buffer, size);
    }
    
    LT_GetCore()->ReleaseHandlePrivateData(consumer, obj);
    return result;
}

static LTLog_Error Private_Consumer_Copy(LTLogConsumer consumer,
                                         const LTLogMessage *msg,
                                         u8 *buffer,
                                         LT_SIZE *size) {
    LT_UNUSED(consumer);

    LTLogBufferSpan *span   = Private_Payload_Get(*msg->index);
    LT_SIZE          count  = (span->used > *size) ? *size : span->used;
    lt_memcpy(buffer, span->data, count);
    *size = count;

    return LTLog_Error_None;
}

static LT_SIZE Private_Consumer_GetMessageLength(LTLogConsumer consumer, const LTLogMessage *msg) {
    LT_UNUSED(consumer);

    return Private_Payload_Get(*msg->index)->used;
}

static LT_SIZE Private_Consumer_GetData(LTLogConsumer consumer, const LTLogMessage *msg, const u8 **buffer) {
    LT_UNUSED(consumer);

    LTLogBufferSpan *span    = Private_Payload_Get(*msg->index);
    if (span) {
        *buffer = span->data;
        return span->used;
    } else {
        *buffer = NULL;
        return 0;
    }
}

/** @} */
