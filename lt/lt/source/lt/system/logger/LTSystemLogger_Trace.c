/******************************************************************************
 * LTSystemLogger_Trace.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTSystemLogger_Private.h"

#if LTLIBRARY_INCLUDE_LTTRACE

#define PRIVATE_COMMON_SIZE (sizeof(u64) + sizeof(u8))
#define PRIVATE_HEADER_SIZE (3 + PRIVATE_COMMON_SIZE)

static LTLogBufferSpan *Private_Trace_Header(LTTraceStream *stream, u64 time, LT_SIZE payloadSize, u8 type) {
    u32              packetSize = payloadSize + PRIVATE_HEADER_SIZE;
    LTLogBufferSpan *payload    = Private_Payload_Alloc(packetSize, false);
    if (!payload) {
        return NULL;
    }
    payload->used = packetSize;
    u8 *data      = payload->data;
    *data++       = 0xc7;
    *data++       = payloadSize + PRIVATE_COMMON_SIZE;
    *data++       = type;
    *data++       = LTBitfieldGet(stream->state, kLTTrace_State_IdMask, kLTTrace_State_IdShift);
    time          = LT_LE64(time);
    lt_memcpy(data, &time, sizeof(time));
    return payload;
}

static void Private_Trace_DoWriteString(LTTraceStream *stream, u8 type, u64 time, u32 id, const char *str) {
    if (!str) return;
    u32 len = lt_strlen(str);
    if (len > 0x1F) return;
    u8               size    = sizeof(id) + len;
    LTLogBufferSpan *payload = Private_Trace_Header(stream, time, size, type);
    if (!payload) return;
    u8 *data = payload->data + PRIVATE_HEADER_SIZE;
    id       = LT_LE32(id);
    lt_memcpy(data, &id, sizeof(id));
    data += sizeof(id);
    lt_memcpy(data, str, len);
    Private_Msg_Store(payload, (LTLogBufferIndex){.toConsole = 1});
}

static void Private_Trace_WriteString(LTTraceStream *stream, u64 time, lt_va_list args) {
    Private_Trace_DoWriteString(stream, 18, time, lt_va_arg(args, u32), lt_va_arg(args, const char *));
}

static void Private_Trace_WriteNumbers(LTTraceStream *stream, u64 time, lt_va_list args) {
    u32              count   = lt_va_arg(args, u32);
    u32              size    = count * sizeof(u32);
    LTLogBufferSpan *payload = Private_Trace_Header(stream, time, size, 17);
    if (!payload) return;
    u8 *data = payload->data + PRIVATE_HEADER_SIZE;
    while (count--) {
        u32 value = LT_LE32(lt_va_arg(args, u32));
        lt_memcpy(data, &value, sizeof(u32));
        data += sizeof(u32);
    }
    Private_Msg_Store(payload, (LTLogBufferIndex){.toConsole = 1});
}

static void Private_Trace(LTTraceStream *stream, LTTracePayloadType type, lt_va_list args) {
    LTTime now = LT_GetCore()->GetKernelTime();
    switch (type) {
        case kLTTrace_Type_Descriptor:
            // Descriptors can be sent at any time, so we need to check if the client has requested descriptors.
            if (LTBitfieldGet(LTTRACE_STREAM(desc).state, kLTTrace_State_EnabledMask, kLTTrace_State_EnabledShift)) {
                Private_Trace_DoWriteString(&LTTRACE_STREAM(desc),
                                            19,
                                            now.nNanoseconds,
                                            LTBitfieldGet(stream->state, kLTTrace_State_IdMask, kLTTrace_State_IdShift),
                                            stream->name);
            }
            break;
        case kLTTrace_Type_String:
            Private_Trace_WriteString(stream, now.nNanoseconds, args);
            break;
        case kLTTrace_Type_Numbers:
            Private_Trace_WriteNumbers(stream, now.nNanoseconds, args);
            break;
    }
}

#else

static void Private_Trace(LTTraceStream *stream, LTTracePayloadType type, lt_va_list args) {
    LT_UNUSED(stream);
    LT_UNUSED(type);
    LT_UNUSED(args);
}

#endif
