/******************************************************************************
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "UnitTestLTSystemLogger.h"

#define TEST_FUZZ_THREAD_COUNT 32

static bool Test_Fuzz_Init(void) {
    for (int i = 0; i < 10; ++i) {
        for (int i = 0; i < 100; ++i) {
            s_if.log->Print("test", "fuzz", kLTCore_LogFlags_LogTypeLog | (kLTCore_LogFlags_LogToConsole), "test fuzz");
        }
        s_if.thread->Sleep(LTTime_Milliseconds(10));
    }
    s_if.thread->Terminate(s_if.thread->GetCurrentThread());

    return true;
}

static void Test_Fuzz(Tilt *tilt) {
    LT_UNUSED(tilt);

    LTThread threads[TEST_FUZZ_THREAD_COUNT];
    for (int i = 0; i < TEST_FUZZ_THREAD_COUNT; ++i) {
        threads[i] = LT_GetCore()->CreateThread("Fuzz");
    }

    for (int i = 0; i < TEST_FUZZ_THREAD_COUNT; ++i) {
        s_if.thread->Start(threads[i], Test_Fuzz_Init, NULL);
    }

    for (int i = 0; i < TEST_FUZZ_THREAD_COUNT; ++i) {
        s_if.thread->WaitUntilFinished(threads[i], LTTime_Seconds(60));
    }
}
