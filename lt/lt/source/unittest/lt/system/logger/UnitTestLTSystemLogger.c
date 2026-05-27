/******************************************************************************
 * UnitTestLTSystemLogger.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "UnitTestLTSystemLogger.h"

static JiltEngine *s_engine;

static const TiltEngineTest s_tests[] = {
    { Test_Specifiers, "Specifiers", "Individual printf specifiers",        0                            },
    { Test_Rodata,     "Rodata",     "Compact .rodata string optimization", 0                            },
    { Test_Records,    "Records",    "Record encoding mechanisms",          0                            },
    { Test_Corners,    "Corners",    "Corner cases",                        0                            },
    { Test_Fuzz,       "Fuzz",       "Fuzz testing",                        kTiltEngineFlag_NoAutomation },
    { Test_Benchmark,  "Benchmark",  "Benchmarking of print performance",   kTiltEngineFlag_NoAutomation },
};

static void OnTransportEvent(LTTransport transport, LTTransport_Event event, void *data) {
    LT_UNUSED(transport); LT_UNUSED(data);
    if (event == kLTTransport_Event_Up) {
        s_engine->API->SignalTestCompletion(s_engine, NULL);
    }
}

static void BeforeAllTests(Tilt *tilt) {
    LT_UNUSED(OnTransportEvent);
    LT_UNUSED(tilt);
    s_if.thread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    s_if.log = lt_openlibrary(LTSystemLogger);
    TILT_EXPECT_TRUE(tilt, s_if.log != NULL, "Cannot open LTSystemLogger");
    if (s_if.log) s_if.log->EnableConsole(false);
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    if (s_if.log) {
        s_if.log->EnableConsole(true);
        lt_closelibrary(s_if.log);
    }
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests
};

#include "UnitTestLTSystemLogger_Benchmark.c"
#include "UnitTestLTSystemLogger_Corners.c"
#include "UnitTestLTSystemLogger_Fuzz.c"
#include "UnitTestLTSystemLogger_Print.c"
#include "UnitTestLTSystemLogger_Record.c"
#include "UnitTestLTSystemLogger_Rodata.c"

static int UnitTestLTSystemLoggerImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTSystemLoggerImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTSystemLoggerImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTSystemLogger, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTSystemLogger, UnitTestLTSystemLoggerImpl_Run, 1536) LTLIBRARY_DEFINITION;
