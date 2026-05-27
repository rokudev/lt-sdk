/******************************************************************************
 * UnitTestLTSystemLogger_Rodata.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "UnitTestLTSystemLogger.h"

static const char Test_Rodata_Data[] =
    "this is a really long string that should not be serialized directly\0"
    "this is a really long string that %s should not be serialized directly %s\0"
    "this is a really long string that should not be serialized directly\0";
static const char *Test_Rodata_Printed =
    "this is a really long string that this is a really long string that "
    "should not be serialized directly should not be serialized directly this "
    "is a really long string that should not be serialized directly";

static void Test_Rodata_Callback(const ILTLogConsumer *api,
                                 LTLogConsumer consumer,
                                 const LTLogMessage *message) {
    Tilt *tilt = api->GetContext(consumer);
    TILT_ASSERT_TRUE(tilt,
                     message->printPayload.formatOffset != LT_LOG_INVALID_FORMAT_OFFSET,
                     "Readonly data not given an offset");
    TILT_ASSERT_TRUE(tilt,
                     message->printPayload.formatOffset + Test_Rodata_Data ==
                         Test_Rodata_Data + lt_strlen(Test_Rodata_Data) + 1,
                     "Readonly data not optimized");

    s_context.log_size = sizeof(s_context.log_buffer);
    LTLog_Error err    = api->Print(consumer, message, false, false, s_context.log_buffer, &s_context.log_size);
    TILT_ASSERT_TRUE(s_if.tilt, err == LTLog_Error_None, "Error %d printing");
    TILT_ASSERT_TRUE(tilt,
                     s_context.log_size == lt_strlen(Test_Rodata_Printed),
                     "Print didn't complete full size");
    TILT_ASSERT_TRUE(tilt,
                     lt_strcmp(s_context.log_buffer, Test_Rodata_Printed) == 0,
                     "Print didn't match expected result");
}

static void Test_Rodata(Tilt *tilt) {
    LTLogConsumer consumer = s_if.log->AddConsumer(Test_Rodata_Callback, NULL, NULL, (void *)tilt);

    const char *fmt   = Test_Rodata_Data + lt_strlen(Test_Rodata_Data) + 1;
    const char *data1 = Test_Rodata_Data;
    const char *data2 = fmt + lt_strlen(fmt) + 1;

    s_if.log->ConfigureMemory(Test_Rodata_Data, Test_Rodata_Data + sizeof(Test_Rodata_Data));
    s_if.log->Print(NULL, NULL, 0, fmt, data1, data2);
    s_if.log->Flush();

    s_if.log->RemoveConsumer(consumer);
}
