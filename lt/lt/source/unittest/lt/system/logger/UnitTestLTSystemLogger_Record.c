/******************************************************************************
 * UnitTestLTSystemLogger_Record.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "UnitTestLTSystemLogger.h"

static void Test_Records_Consumer(const ILTLogConsumer *api,
                                  LTLogConsumer consumer,
                                  const LTLogMessage *msg) {
    LT_UNUSED(consumer);

    Tilt *tilt = (Tilt *)api->GetContext(
        consumer);
    TILT_ASSERT_TRUE(tilt, msg->recordType == kLTLogRecordType_Events, "Record type doesn't match");
}

static void Test_Records(Tilt *tilt) {
    if (LTLogRecordLock(&s_record_events_Record)) {
        s_record_events.Count             = 1;
        s_record_events.Entries[0].When   = LT_GetCore()->GetKernelTime();
        s_record_events.Entries[0].Id     = 0xffff;
        s_record_events.Entries[0].Source = 1;
        s_record_events.Entries[0].Type   = 2;
        LTLogRecordUnlock(&s_record_events_Record);
    }

    LTLogConsumer consumer = s_if.log->AddConsumer(Test_Records_Consumer, NULL, NULL, (void *)tilt);
    s_if.log->EncodeRecord(&s_record_events_Record);
    s_if.log->Flush();
    s_if.log->RemoveConsumer(consumer);
}
