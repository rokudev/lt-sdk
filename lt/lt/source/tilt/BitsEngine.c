
/******************************************************************************
 * BitsEngine.c
 *    ____ ___ __^__ ____
 *   < __ )_ _|_   _/ ___>             BITS: Better
 *   |  _ \| |  | | \___ \                   Integrity
 *   < |_) | |  | |  ___) >                  Through Stress
 *   |____/_ _| |_| |____/
 *          v
 *  BITS framework for stress testing
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <tilt/BitsEngine.h>

/*_____________
  BitsEngine */

enum {
    kBitsEngineDefaultStackSize   = 1536
};

/* Test thread descriptor (private) */
typedef struct {
    LTOThread           *thread;
    LTThread_InitProc   *initProc;
    LTThread_ExitProc   *exitProc;
    BitsEngine_TestProc *testProc;
    void                *clientData;
    u32                  threadNum;  /* NB: not const like API */
    u32                  syncPoint;  /* NB: not const like API */
} TestThreadDesc;

LT_STATIC_ASSERT_SIZE_32_64(TestThreadDesc,            28, 48)
LT_STATIC_ASSERT_SIZE_32_64(BitsEngine_TestThreadDesc, 28, 48)

typedef struct {
    LTTime       timeout;
    u32          stackSize;
    bool         quiet;
    bool         verbose;
} Options;

typedef u8 BitsEngineState;
enum BitsEngineState {
    kBitsEngineState_BeforeAllTests,
    kBitsEngineState_InTests,
    kBitsEngineState_AfterAllTests,
    kBitsEngineState_CompletedTests
};

/*__________________________
  BitsEngine Private Data */
typedef_LTObjectImpl(BitsEngine, BitsEngineImpl) {
    LTTime                     syncTimeout;
    LTMutex                   *mutex;
    Tilt                      *tilt;
    const BitsEngineTestHooks *hooks;
    LTOThread                 *thread;      /* Main test thread */
    LTArray                   *threads;     /* Array of test threads */
    BitsEngineState            state;
    u32                        syncPoint;   /* The current synchronization point of test suite */
    u32                        syncCount;   /* Number of thread syncs to expect at this syncPoint */
    u32                        availRAMbefore;
    u32                        availRAMafter;
    Options                    options;
} LTOBJECT_API;

/*____________________________
  BitsEngine Implementation */

static TestThreadDesc *FindTestThreadDesc(BitsEngineImpl *engine) {
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    u32 numThreads = engine->threads->API->GetCount(engine->threads);
    for (u32 ix = 0; ix < numThreads; ix++) {
        TestThreadDesc *desc = engine->threads->API->Get(engine->threads, ix, NULL);
        if (desc->thread == thread) return desc;
    }
    return NULL;
}

/* BitsEngine test suite test proc */
static void BitsEngineTestProc(void *clientData) {
    BitsEngineImpl *engine = (BitsEngineImpl *)clientData;
    TestThreadDesc *desc   = FindTestThreadDesc(engine);
    if (desc) {
        if (desc->testProc) desc->testProc(engine->tilt, (BitsEngine_TestThreadDesc *)desc, desc->syncPoint);
    } else {
        TILT_ABORT_TESTING(engine->tilt, "Cannot find test thread");
    }
}

static void BitsEngineImpl_FinishTesting(BitsEngineImpl *engine);

static void SyncTimeoutProc(void *clientData) {
    BitsEngineImpl *engine = (BitsEngineImpl *)clientData;
    TILT_ABORT_TESTING(engine->tilt, "Synchronization timeout");
    BitsEngineImpl_FinishTesting(engine);
}

static void OverallTimeoutProc(void *clientData) {
    BitsEngineImpl *engine = (BitsEngineImpl *)clientData;
    TILT_ABORT_TESTING(engine->tilt, "Tests timed out");
    BitsEngineImpl_FinishTesting(engine);
}

/* BitsEngine test suite state machine */
static void BitsEngineMainTaskProc(void *clientData) {
    BitsEngineImpl            *engine = (BitsEngineImpl *)clientData;
    Tilt                      *tilt   = engine->tilt;
    const BitsEngineTestHooks *hooks  = engine->hooks;

    if (engine->state == kBitsEngineState_BeforeAllTests) {
        if (hooks->BeforeAllTests) hooks->BeforeAllTests(tilt);
        if (tilt->API->GetOverallTestStatus() == kTiltTestStatus_Aborted) {
            engine->state = kBitsEngineState_AfterAllTests;
        } else {
            engine->state = kBitsEngineState_InTests;
        }
    }

    if (engine->state == kBitsEngineState_InTests) {
        if (tilt->API->GetOverallTestStatus() == kTiltTestStatus_Aborted) {
            engine->state = kBitsEngineState_AfterAllTests;
        } else {
            if (hooks->Synchronization) hooks->Synchronization(tilt, engine->syncPoint);
            if (engine->state == kBitsEngineState_InTests) {
                u32 numThreads = engine->threads->API->GetCount(engine->threads);
                /* Determine number of unique thread syncs required to enter next syncPoint */
                engine->syncCount = numThreads;
                for (u32 ix = 0; ix < numThreads; ix++) {
                    TestThreadDesc *desc = engine->threads->API->Get(engine->threads, ix, NULL);
                    if (desc->syncPoint > engine->syncPoint) engine->syncCount--;
                }
                if (engine->syncCount == 0) {
                    /* For the odd case where every thread skipped the syncPoint. */
                    engine->syncPoint++;
                    engine->thread->API->QueueTaskProc(engine->thread, BitsEngineMainTaskProc, NULL, engine);
                } else {
                    /* To guarantee proper testProc launch temporarily set main thread priority to highest */
                    u8 prio = engine->thread->API->GetPriority(engine->thread);
                    engine->thread->API->SetPriority(engine->thread, kLTThread_PriorityHighest);
                    for (u32 ix = 0; ix < numThreads; ix++) {
                        /* Kick off testProcs for next syncPoint */
                        TestThreadDesc *desc = engine->threads->API->Get(engine->threads, ix, NULL);
                        if (desc->syncPoint == engine->syncPoint) {
                            desc->thread->API->QueueTaskProc(desc->thread, BitsEngineTestProc, NULL, engine);
                        }
                    }
                    /* Set synchronization timeout */
                    if (! LTTime_IsEqual(engine->syncTimeout, LTTime_Infinite())) {
                        engine->thread->API->SetTimer(engine->thread, engine->syncTimeout, SyncTimeoutProc, NULL, engine);
                    }
                    /* Revert thread priority */
                    engine->thread->API->SetPriority(engine->thread, prio);
                }
            }
        }
    }

    if (engine->state == kBitsEngineState_AfterAllTests) {
        if (hooks->AfterAllTests) hooks->AfterAllTests(tilt);
        engine->state = kBitsEngineState_CompletedTests;
        engine->thread->API->Terminate(engine->thread);
    }
}

static void DisplayUsage(const char **argv) {
    LT_GetCore()->ConsolePrint    ("usage: ltrun %s [<option> ...]\n", argv[1]);
    LT_GetCore()->ConsolePutString("where <option> is one of:\n\n");
    LT_GetCore()->ConsolePutString("  --quiet              Produce minimal output (statistics and list of failed tests)\n");
    LT_GetCore()->ConsolePutString("  --verbose            Produce maximal output, including all messages from tests\n");
    LT_GetCore()->ConsolePutString("  --failure-limit <n>  Abort after <n> failures have been reported\n");
    LT_GetCore()->ConsolePrint    ("  --timeout <n>        Timeout (in seconds) for entire test run (default infinite)\n");
    LT_GetCore()->ConsolePutString("  --stacksize <n>      Stack size (in bytes) for main test thread\n");
    LT_GetCore()->ConsolePutString("  --timestamp          Prepend output with kernel timestamp\n");
    LT_GetCore()->ConsolePutString("  <property>=<value>   Set test property to a value\n");
}

static int ParseArguments(BitsEngineImpl *engine, int argc, const char **argv) {
    if (argc < 2) return kBitsEngineStatus_BadOptions;
    if (lt_strcmp(engine->tilt->API->GetTestSuiteName(), "") == 0) {
        /* Obtain default test suite name from command line (if not already set) */
        engine->tilt->API->SetTestSuiteName(argv[1]);
    }
    for (int argIndex = 2; argIndex < argc; argIndex++) {
        const char *arg = argv[argIndex];
        if (!lt_strcmp(arg, "--quiet")) {
            engine->options.quiet = true;
            engine->tilt->API->DisableReportLevel(kTiltReportLevel_Info);
        } else if (!lt_strcmp(arg, "--verbose")) {
            engine->options.verbose = true;
            engine->tilt->API->EnableReportLevel(kTiltReportLevel_Debug);
        } else if (!lt_strcmp(arg, "--failure-limit")) {
            if (argIndex == argc - 1) return kBitsEngineStatus_BadOptions;
            engine->tilt->API->SetFailureLimit(lt_strtou32(argv[++argIndex], NULL, 10));
        } else if (!lt_strcmp(arg, "--timeout")) {
            if (argIndex == argc - 1) return kBitsEngineStatus_BadOptions;
            engine->options.timeout = LTTime_Seconds(lt_strtou32(argv[++argIndex], NULL, 10));
        } else if (!lt_strcmp(arg, "--stacksize")) {
            if (argIndex == argc - 1) return kBitsEngineStatus_BadOptions;
            engine->options.stackSize = lt_strtou32(argv[++argIndex], NULL, 10);
        } else if (!lt_strcmp(arg, "--timestamp")) {
            engine->tilt->API->EnableTimestamps(true);
        } else {
            const char *equalSign = lt_strchr(arg, '=');
            if (equalSign) {
                LTString property = ltstring_createsubstring(arg, 0, equalSign - arg);
                engine->tilt->API->SetProperty(property, equalSign + 1);
                ltstring_destroy(property);
            } else {
                return kBitsEngineStatus_BadOptions;
            }
        }
    }
    return kTiltTestStatus_Ok;
}

static int BitsEngineImpl_RunTestSuite(BitsEngineImpl *engine, int argc, const char **argv,
                                          const BitsEngineTestHooks *hooks) {

    /* Save available RAM *before* parsing arguments to account for test properties, etc. */
    engine->availRAMbefore = LT_GetCore()->GetAvailableSystemRAM();

    int rtn = ParseArguments(engine, argc, argv);
    switch (rtn) {
    case kTiltTestStatus_Ok:
        break;
    case kBitsEngineStatus_BadOptions:
        DisplayUsage(argv);
        return rtn;
    default:
        return rtn;
    }

    /* Start test thread */
    engine->thread = lt_createobject(LTOThread);
    if (!engine->thread) {
        TILT_ABORT_TESTING(engine->tilt, "Cannot create test thread");
        return -1;
    }

    engine->hooks = hooks;
    engine->tilt->API->ReportTestSuiteStarted();

    LTOThread *thread = engine->thread;
    thread->API->SetStackSize(thread, engine->options.stackSize);
    thread->API->Start(thread, "BITS_Main", NULL, NULL);

    /* Initialize and run test state machine */
    engine->state = kBitsEngineState_BeforeAllTests;
    thread->API->QueueTaskProc(thread, BitsEngineMainTaskProc, NULL, engine);

    if (! LTTime_IsEqual(engine->options.timeout, LTTime_Infinite())) {
        engine->thread->API->SetTimer(engine->thread, engine->options.timeout, OverallTimeoutProc, NULL, engine);
    }

    /* Wait for test thread to finish */
    thread->API->WaitUntilFinished(thread, LTTime_Infinite());
    lt_destroyobject(thread);

    engine->tilt->API->ReportTestSuiteSummary();
    engine->tilt->API->ClearTestSuite();

    engine->availRAMafter = LT_GetCore()->GetAvailableSystemRAM();
    LT_GetCore()->ConsolePrint("Available RAM before and after test (%lu - %lu = %ld potentially leaked)\n\n",
                                  LT_Pu32(engine->availRAMbefore), LT_Pu32(engine->availRAMafter),
                                  LT_Ps32(engine->availRAMbefore - engine->availRAMafter));

    return engine->tilt->API->GetOverallTestStatus();
}

static void BitsEngineImpl_SetTestSuiteName(BitsEngineImpl *engine, char *testSuiteName) {
    engine->tilt->API->SetTestSuiteName(testSuiteName);
}

static void BitsEngineImpl_SetTestSuiteTimeout(BitsEngineImpl *engine, LTTime timeout) {
    engine->options.timeout = timeout;
}

static void BitsEngineImpl_SetMainThreadStackSize(BitsEngineImpl *engine, u32 stackSize) {
    engine->options.stackSize = stackSize;
}

static Tilt *BitsEngineImpl_GetTestFramework(BitsEngineImpl *engine) {
    return engine->tilt;
}

static LTArray *BitsEngineImpl_CreateTestThreads(BitsEngineImpl *engine, u32 numThreads, u32 stackSize) {
    if (engine->threads) {
        TILT_ABORT_TESTING(engine->tilt, "Cannot create second array of test threads");
        return NULL;
    }
    LTArray *threads = lt_createobject(LTArray);
    if (!threads) return NULL;
    threads->API->InitAsStructArray(threads, sizeof(TestThreadDesc));
    /* Allocate pre-zeroed array for threads */
    if (!threads->API->SetCount(threads, numThreads)) {
        TILT_ABORT_TESTING(engine->tilt, "Cannot allocate thread array");
        return NULL;
    }
    for (u32 ix = 0; ix < numThreads; ix++) {
        TestThreadDesc *desc = threads->API->Get(threads, ix, NULL);
        desc->thread = lt_createobject(LTOThread);
        if (!desc->thread) {
            TILT_ABORT_TESTING(engine->tilt, "Cannot create test threads");
            return NULL;
        }
        desc->thread->API->SetStackSize(desc->thread, stackSize);
        desc->threadNum = ix;
    }
    engine->threads = threads;
    return threads;
}

static void BitsEngineImpl_StartTestThreads(BitsEngineImpl *engine) {
    LTArray *threads = engine->threads;
    if (!threads) return;
    LTString name = ltstring_create("BITS_XXX");
    if (!name) {
        TILT_ABORT_TESTING(engine->tilt, "Cannot create string");
        return;
    }
    u32 numThreads = threads->API->GetCount(threads);
    for (u32 ix = 0; ix < numThreads; ix++) {
        TestThreadDesc *desc = threads->API->Get(threads, ix, NULL);
        ltstring_format(&name, "BITS_%u", ix);
        desc->thread->API->Start(desc->thread, name, desc->initProc, desc->exitProc);
    }
    ltstring_destroy(name);
}

static void BitsEngineImpl_DestroyTestThreads(BitsEngineImpl *engine) {
    LTArray *threads = engine->threads;
    if (!threads) return;
    u32 numThreads = threads->API->GetCount(threads);
    for (u32 ix = 0; ix < numThreads; ix++) {
        TestThreadDesc *desc = threads->API->Get(threads, ix, NULL);
        if (desc->thread) {
            desc->thread->API->Terminate(desc->thread);
            desc->thread->API->WaitUntilFinished(desc->thread, LTTime_Infinite());
            lt_destroyobject(desc->thread);
        }
    }
    lt_destroyobject(engine->threads);
    engine->threads = NULL;
}

static void BitsEngineImpl_Synchronize(BitsEngineImpl *engine, u32 nextSyncPoint) {
    engine->mutex->API->Lock(engine->mutex);
    /* Find the thread */
    TestThreadDesc *desc = FindTestThreadDesc(engine);
    if (desc) {
        if (nextSyncPoint > desc->syncPoint) {
            desc->syncPoint = nextSyncPoint;
            if (--engine->syncCount == 0) {
                engine->thread->API->KillTimer(engine->thread, SyncTimeoutProc, engine);
                engine->syncPoint++;
                engine->thread->API->QueueTaskProc(engine->thread, BitsEngineMainTaskProc, NULL, engine);
            }
        }
    } else {
        TILT_ABORT_TESTING(engine->tilt, "Cannot synchronize non-test thread");
    }
    engine->mutex->API->Unlock(engine->mutex);
}

static void BitsEngineImpl_SetSynchronizationTimeout(BitsEngineImpl *engine, LTTime timeout) {
    engine->mutex->API->Lock(engine->mutex);
    engine->syncTimeout = timeout;
    engine->mutex->API->Unlock(engine->mutex);
}

static void BitsEngineImpl_FinishTesting(BitsEngineImpl *engine) {
    engine->state = kBitsEngineState_AfterAllTests;
    engine->thread->API->QueueTaskProc(engine->thread, BitsEngineMainTaskProc, NULL, engine);
}

static bool BitsEngineImpl_ConstructObject(BitsEngineImpl *engine) {
    engine->mutex = lt_createobject(LTMutex);
    if (!engine->mutex) return false;
    engine->tilt = lt_createobject(Tilt);

    /* Default options */
    lt_memset(&engine->options, 0, sizeof(Options));
    engine->options.stackSize = kBitsEngineDefaultStackSize;
    engine->options.timeout   = LTTime_Infinite();
    engine->syncTimeout       = LTTime_Infinite();
    return engine->tilt != NULL;
}

static void BitsEngineImpl_DestructObject(BitsEngineImpl *engine) {
    BitsEngineImpl_DestroyTestThreads(engine);
    lt_destroyobject(engine->tilt);
    lt_destroyobject(engine->mutex);
}

/*____________________________________
  BitsEngine LTObjectApi definition */
define_LTObjectImplPublic(BitsEngine, BitsEngineImpl,
    RunTestSuite,
    SetTestSuiteName,
    SetTestSuiteTimeout,
    SetMainThreadStackSize,
    GetTestFramework,
    CreateTestThreads,
    StartTestThreads,
    DestroyTestThreads,
    Synchronize,
    SetSynchronizationTimeout,
    FinishTesting);

/*______________________________
  Bits Root interface binding */
static void Bits_LibFini(void) {
}

static bool Bits_LibInit(void) {
    return true;
}

define_LTObjectLibrary(1, Bits_LibInit, Bits_LibFini);

