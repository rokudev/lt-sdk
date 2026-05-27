/******************************************************************************
 * UnitTestLTSystemLogger_Corners.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "UnitTestLTSystemLogger.h"

static void Test_Corners_EmptyFormatCallback(const ILTLogConsumer *api,
                                             LTLogConsumer consumer,
                                             const LTLogMessage *message) {
    s_context.log_size = sizeof(s_context.log_buffer);
    LTLog_Error err    = api->Print(consumer, message, false, false, s_context.log_buffer, &s_context.log_size);

    TILT_ASSERT_TRUE(s_if.tilt, err == LTLog_Error_None, "Empty format made an error");
    TILT_ASSERT_TRUE(s_if.tilt, s_context.log_size == 0, "Empty format should make an empty log");
}

static void Test_Corners_EmptyFormat(void) {
    LTLogConsumer consumer = s_if.log->AddConsumer(Test_Corners_EmptyFormatCallback,
                                                   NULL,
                                                   NULL,
                                                   NULL);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-zero-length"
    // There are some way zero length formats could sneak in, so we should make sure it doesn't
    // cause a problem.
    s_if.log->Print("", "", kLTCore_LogFlags_LogTypeLog, "");
    s_if.log->Flush();
#pragma GCC diagnostic pop

    s_if.log->Print("", "", kLTCore_LogFlags_LogTypeLog, "%s", "");
    s_if.log->Flush();

    s_if.log->RemoveConsumer(consumer);
}

static void Test_Corners(Tilt *tilt) {
    s_if.tilt = tilt;

    Test_Corners_EmptyFormat();
}
