/******************************************************************************
 * LTSystemLogger_Buffer.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTSystemLogger_Private.h"

/******************************************************************************
 * @name Buffer Convenience Macros
 * @{
 */

#if LTLOG_CONFIG_MAGIC_ASSERTIONS
    #define PAYLOAD_MAGIC                 0b1100101011011
    #define PAYLOAD_ASSERT_MAGIC(payload) LT_ASSERT(payload->unused == PAYLOAD_MAGIC)
#else
    #define PAYLOAD_ASSERT_MAGIC(payload)
#endif

/** @} */

/******************************************************************************
 * @name Message Buffer
 * @{
 */

#if !LTLOG_CONFIG_ALLOW_PRIORITY_BOOST
static bool Private_Msg_Empty(void) {
    return LTRingGetSize(&S.buf.msg) == 0;
}
#endif

static void Private_Msg_Store(LTLogBufferSpan *payload, LTLogBufferIndex msg) {
    msg.offset              = LTRingCalculateIndex(&S.buf.payload, payload);
    msg.isReady             = true;
    LTLogBufferIndex *index = LTRingReserve(&S.buf.msg, 1);
    LT_ASSERT(index);
    LTAtomic_Store(&index->atomicValue, msg.value);
}

static LTLogBufferIndex *Private_Msg_Consume(void) {
    LTLogBufferIndex *index;
    u32               offset = 0;
    while ((index = LTRingPeek(&S.buf.msg, offset++))) {
        LTLogBufferIndex value = (LTLogBufferIndex){.value = LTAtomic_Load(&index->atomicValue)};
        if (!value.isReady) {
            return NULL;
        }
        if (value.isFinished) {
            continue;
        }
        if (value.offset != (S.buf.payloadOffset & S.buf.payload.mask)) {
            continue;
        }
        break;
    }

    return index;
}

static void Private_Msg_Release(LTLogBufferIndex *index) {
    LTLogBufferSpan *span  = Private_Payload_Get(*index);
    S.buf.payloadOffset   += span->size + sizeof(LTLogBufferSpan);
    index->isFinished      = true;
    // Messages must be released in ORDER OF PAYLOAD.  That means out-of-order messages must sometimes wait until the
    // next in-order message is released, allowing all payloads to be released consecutively.  The following logic
    // assumes that all consumed messages have been sorted by payload order.
    while ((index = LTRingPeek(&S.buf.msg, 0)) && index->isFinished) {
        LTLogBufferSpan *span = Private_Payload_Get(*index);
        LTAtomic_Store(&index->atomicValue, 0);
        LTRingConsume(&S.buf.msg, 1);
        LTRingConsume(&S.buf.payload, span->size + sizeof(LTLogBufferSpan));
    }
}

/**
 * @internal
 * @todo Add some logic to check whether the acquired buffer space is live or
 * dead. Everything between buffer_tail and buffer_head is live data and should
 * not be written to.
 */
static LTLogBufferSpan *Private_Payload_Alloc(LT_SIZE size, bool scheduleOnFailure) {
    // The allocated buffer must be aligned and have enough space to accommodate
    // the requested size and the span header.
    u32 requested = (size + sizeof(LTLogBufferSpan) + LTLOG_CONFIG_ALIGNMENT - 1) & ~(LTLOG_CONFIG_ALIGNMENT - 1);
    if (requested < LTLOG_CONFIG_MIN_ALLOCATION_SIZE) {
        requested = LTLOG_CONFIG_MIN_ALLOCATION_SIZE;
    }

    LTLogBufferSpan *payload;
    for (;;) {
        u32 contiguous;
        payload = LTRingReserveContiguous(&S.buf.payload, requested, &contiguous);
#if LTLOG_CONFIG_DEBUG_ALLOCATION
        if (avail < S.debug.watermark) {
            S.debug.watermark = avail;
        }
        if (++S.debug.allocationsLow == 0) {
            ++S.debug.allocationsHigh;
        }
        if ((S.debug.allocatedBytesLow += requested) < (u32)requested) {
            ++S.debug.allocatedBytesHigh;
        }
        if (avail <= requested) {
            ++S.debug.failedAllocations;
        }
#endif
        if (!payload) {
            LTAtomic_FetchOr(&S.buf.flags, LTLOG_BUFFER_FLAG_DROPPED);
            if (scheduleOnFailure) {
                S.i.thread->QueueTaskProcIfRequired(S.consumer.thread, Private_Consumer_Process, NULL, NULL);
            }
            return NULL;
        }
        payload->size = requested - sizeof(LTLogBufferSpan);
        payload->used = 0;
#if LTLOG_CONFIG_ALLOW_PRIORITY_BOOST
        if (!LT_GetCore()->InsideInterruptContext()
            && (LTRingGetCapacity(&S.buf.payload) < LTLOG_CONFIG_BUFFER_AVAIL_WATERMARK)) {
            u8 priority = S.i.thread->GetPriority(S.i.thread->GetCurrentThread()) + 1;
            if (priority > LTLOG_CONFIG_THREAD_PRIORITY_MAX) {
                priority = LTLOG_CONFIG_THREAD_PRIORITY_MAX;
            }
    #if LTLOG_CONFIG_DEBUG_ALLOCATION
            ++S.debug.boosts;
    #endif
            S.i.thread->SetPriority(S.consumer.thread, priority);
    #if LTLOG_CONFIG_ALLOW_PRIORITY_YIELD
            S.i.thread->Yield();
    #endif
        }
#endif
        if (contiguous == requested) {
            break;
        }
#if LTLOG_CONFIG_MAGIC_ASSERTIONS
        payload->unused = PAYLOAD_MAGIC;
#endif
        // Failed to allocate a contiguous block, so mark what we got as "skip"
        // and retry the allocation loop.  The message publish is guaranteed to
        // succeed because of the relationship of buffer sizes and minimum
        // allocation size.
        if (contiguous < sizeof(LTLogBufferSpan)) {
            LT_GetCore()->ConsoleStompString("\n" LTLOG_COLOR_REDALERT
                                             "!!! logging: contiguous size" LTLOG_COLOR_DEFAULT "\n");
            payload = LTRingAt(&S.buf.payload, 0);
        }
#if LTLOG_CONFIG_DEBUG_ALLOCATION
        u32 idx                 = LTAtomic_FetchAdd(&S.buf.payloadAllocIndex, 1);
        S.buf.payloadAlloc[idx] = (LTRingCalculateIndex(&S.buf.payload, payload) << 16) | requested;
#endif
        Private_Payload_Publish(payload, 0);
    }
#if LTLOG_CONFIG_MAGIC_ASSERTIONS
    payload->unused = PAYLOAD_MAGIC;
#endif
#if LTLOG_CONFIG_DEBUG_ALLOCATION
    u32 idx                       = LTAtomic_FetchAdd(&S.buf.payloadAllocIndex, 1);
    S.buf.payloadAlloc[idx & 255] = (LTRingCalculateIndex(&S.buf.payload, payload) << 16) | requested;
#endif
    return payload;
}

/** Atomically record the location of the next message payload
 *
 * The design of this method guarantees that messages will always be published
 * in contiguous order, regardless of race conditions.
 */
static void Private_Payload_Publish(LTLogBufferSpan *payload, u32 flags) {
    LTLogBufferIndex index = {.toConsole   = !!(flags & kLTCore_LogFlags_LogToConsole),
                              .toServer    = !!(flags & kLTCore_LogFlags_LogToServer)};
    Private_Msg_Store(payload, index);

#if LTLOG_CONFIG_DEBUG_IMMEDIATE_PROCESSING
    Private_Consumer_Process(NULL);
#else
    S.i.thread->QueueTaskProcIfRequired(S.consumer.thread, Private_Consumer_Process, NULL, NULL);
#endif
}

static LTLogBufferSpan *Private_Payload_Get(LTLogBufferIndex index) {
    LTLogBufferSpan *payload = LTRingAt(&S.buf.payload, index.offset);
    PAYLOAD_ASSERT_MAGIC(payload);
    return payload;
}

static LTLogBufferSpan *Private_Payload_Finish(struct LTMessagePack_Obj *obj) {
    LTLogBufferSpan *payload = (LTLogBufferSpan *)(obj->head - sizeof(LTLogBufferSpan));
    PAYLOAD_ASSERT_MAGIC(payload);
    payload->used = obj->next - obj->head;
#if LTLOG_CONFIG_DEBUG_ALLOCATION
    LT_SIZE oversize = payload->size - payload->used;
    if ((S.debug.wastedBytesLow += oversize) < oversize) {
        ++S.debug.wastedBytesHigh;
    }
#endif
    return payload;
}

static u8 *Private_Payload_Realloc(struct LTMessagePack_Obj *obj, LT_SIZE requested) {
    if (requested == 0) {
        return NULL;
    }

    LTLogBufferSpan *payload = Private_Payload_Finish(obj);
    payload->used            = 0;
    if (requested < payload->size) {
        requested = payload->size;
    }
    requested                    += payload->size;
    LTLogBufferSpan *replacement  = Private_Payload_Alloc(requested, true);
    if (!replacement) {
        return NULL;
    }
    u32 used = obj->next - obj->head;
    lt_memcpy(replacement->data, payload->data, used);
    obj->head = replacement->data;
    obj->next = replacement->data + used;
    obj->end  = replacement->data + replacement->size;
    Private_Msg_Store(payload, (LTLogBufferIndex){.value = 0});

    return obj->next;
}

/** @} */
