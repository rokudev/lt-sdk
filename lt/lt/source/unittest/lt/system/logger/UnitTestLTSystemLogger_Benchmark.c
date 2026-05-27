/******************************************************************************
 * UnitTestLTSystemLogger_Benchmark.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "UnitTestLTSystemLogger.h"

#include <lt/core/LTProbe.h>

static bool Test_Benchmark_Thread(void) {
    LTProbe *probe;
    LTCore *core = LT_GetCore();
    probe        = LTProbe_Create("Benchmark", 4, false);
    s_if.log->EnableConsole(true);
    LTProbe_CaptureProbe(probe, "Before Logger");
    for (int j = 0; j < 1000; ++j) {
        s_if.log->Print("abcd.asdf", "asd", kLTCore_LogFlags_LogToConsole, "Hello %s", ", logger!");
        s_if.log->Flush();
    }
    LTProbe_CaptureProbe(probe, "After Logger");
    LTProbe_CaptureProbe(probe, "Before Direct");
    for (int i = 0; i < 1000; ++i) {
        core->Log("abcd.asdf", "asd", kLTCore_LogFlags_LogToConsole, "Hello %s", ", printf!");
    }
    LTProbe_CaptureProbe(probe, "After Direct");
    LTProbe_ConsolePrintReport(probe);
    LTProbe_Destroy(probe);

    s_if.thread->Terminate(s_if.thread->GetCurrentThread());

    return true;
}

static void Test_Benchmark(Tilt *tilt) {
    s_if.tilt = tilt;

#if TEST_ENABLE_BENCHMARK
    LTThread thread = LT_GetCore()->CreateThread("Benchmark");
    s_if.thread->SetStackSize(thread, 4096);
    s_if.thread->Start(thread, Test_Benchmark_Thread, NULL);
    TILT_EXPECT_TRUE(tilt,
                     s_if.thread->WaitUntilFinished(thread, LTTime_Seconds(30)),
                     "Timed out waiting for fade tests to complete");
    s_if.thread->Destroy(thread);
#else
    LT_UNUSED(Test_Benchmark_Thread);
#endif
}
