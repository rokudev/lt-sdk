/******************************************************************************
 * LTSystemLogger_Record.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTSystemLogger_Private.h"

static LTLogBufferSpan *Private_Record_Encode(LTLogRecord *record, LTTime now) {
    // Payload must hold the entire record + flags + timestamp + type.
    LTLogBufferSpan *payload = Private_Payload_Alloc(record->payloadSize + 1 + 9 + 1, true);
    if (payload) {
        LTMessagePack_Obj writer;
        S.i.mp->Init(&writer, payload->data, payload->size);
        writer.allocator = Private_Payload_Realloc;
        LTMessagePackPut(S.i.mp, &writer, record->type);
        LTMessagePackPut(S.i.mp, &writer, now.nNanoseconds);
        record->encoder(record, S.i.mp, &writer);
        payload = Private_Payload_Finish(&writer);
    }

    return payload;
}

static void Private_Record_Schedule(LTLogRecord *record, LTTime period) {
    if (period.nNanoseconds > 0) {
        S.i.thread->SetTimer(S.consumer.thread, period, Private_Record_Process, NULL, record);
    }
}

static void Private_Record_Process(void *context) {
    LTTime now          = LT_GetCore()->GetKernelTime();
    LTLogRecord *record = (LTLogRecord *)context;

    u32 old_cycles           = LTAtomic_Load(&record->cycles);
    LTLogBufferSpan *payload = Private_Record_Encode(record, now);
    u32 new_cycles           = LTAtomic_Load(&record->cycles);
    // There are two ways the encoding could be invalid:
    //
    // 1. The cycles count changed during the encoding process.  This means another thread completed
    // an update.  We could probably just retry if that's the case.
    //
    // 2. The lock flag is set.  This means another thread is currently updating the record and we
    // have to try again later.
    if (!payload) {
        Private_Record_Schedule(record, LTLOG_CONFIG_RECORD_RETRY_PERIOD_LONG);
        return;
    }
    if (old_cycles != new_cycles || (new_cycles & LTLOG_RECORD_LOCK_FLAG)) {
        payload->used = 0;
        Private_Record_Schedule(record, LTLOG_CONFIG_RECORD_RETRY_PERIOD);
    } else {
        Private_Record_Schedule(record, record->period);
    }
    Private_Payload_Publish(payload, kLTCore_LogFlags_LogToServer);
}
