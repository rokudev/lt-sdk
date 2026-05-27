/******************************************************************************
 * tilt/Tilt.c
 *    ______ ____ __   ______
 *   /_  __//  _// /  /_  __/       TILT 2.0: the Test
 *    / /   / / / /    / /                    Infrastructure
 *   / /  _/ / / /___ / /                     for LT
 *  /_/  /___//_____//_/
 *
 *  TILT Framework for the Unit Testing, Performance Testing and Stress Testing
 *     of LT Objects.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTArray.h>
#include <tilt/JiltEngine.h>

/* Test Suite Abort (internal) */
#define TILT_INTERNAL_ABORT(...)   (TiltImpl_AbortTestSuite(__FILE__, __LINE__, __VA_ARGS__))

enum {
    kTiltMaxResourceID                   = 1000,
    kTiltDefaultFailureLimit             = 999999,

    kTiltEngineDefaultTestTimeoutSeconds = 30,
    kTiltEngineDefaultStackSize          = 3072,
    kTiltEngineStatus_ListTests          = -1
};

/*_________________
  Tilt Framework */

/* A test record */
typedef struct {
    LTString       name;
    TiltTestStatus status;
} TestRecord;

/* PRNG (PCG, Permuted Congruential Generator) */
typedef struct {
    u64     state;
    u64     increment;
} PRNG;

typedef struct {
    LTTime  accumulated;
    LTTime  start;
} Stopwatch;

/*____________________
  Tilt Private Data */
typedef_LTObjectImpl(Tilt, TiltImpl) {
    /* I am a singleton so nothing goes here (for now) */
} LTOBJECT_API;

static struct {
    LTMutex            *mutex;
    LTString            testSuiteName;
    LTAssociativeArray *properties;
    LTArray            *tests;
    LTArray            *PRNGs;
    LTArray            *stopwatches;
    TestRecord         *currentTest;
    void               *clientData;
    u32                 failureLimit;
    u32                 failureCount;
    u16                 numTests;
    u16                 numTestsFailed;
    u16                 numTestsCannotRun;
    u16                 numTestsSkipped;
    bool                reportLevelEnabled[4];
    bool                aborted;
    bool                useTimestamps;
} s;

/*________________________________
  Tilt Framework Implementation */

static void TiltImpl_AbortTestSuite(const char *file, u32 line, const char *fmt, ...);
static void TiltImpl_Report(TiltReportLevel level, const char *fmt, ...);

static bool TiltTryLock(void) {
    if (!s.mutex->API->TryLock(s.mutex)) {
        TILT_INTERNAL_ABORT("Only one client allowed at a time");
        return false;
    }
    return true;
}

static void PrintPrefix(const char *file, u32 line, const char *type) {
    LT_GetCore()->FlushConsoleOutput();
    const char *f = lt_strrchr(file, '/');
    if (s.useTimestamps) {
        char buf[24];
        LT_GetCore()->FormatCanonicalTimeString(LT_GetCore()->GetKernelTime(), buf, sizeof(buf), true);
        LT_GetCore()->ConsolePrint("%s !!! %s: %s, line %lu: ", buf, type, f ? (f + 1) : file, LT_Pu32(line));
    } else {
        LT_GetCore()->ConsolePrint("  !!! %s: %s, line %lu: ", type, f ? (f + 1) : file, LT_Pu32(line));
    }
}

static void TiltImpl_SetTestSuiteName(const char *testSuiteName) {
    if (!TiltTryLock()) return;
    ltstring_set(&s.testSuiteName, testSuiteName);
    s.mutex->API->Unlock(s.mutex);
}

static const char *TiltImpl_GetTestSuiteName(void) {
    return s.testSuiteName;
}

static void TiltImpl_SetProperty(const char *name, const char *value) {
    s.mutex->API->Lock(s.mutex);
    if (!s.properties) {
        s.properties = lt_createobject(LTAssociativeArray);
    }
    if (s.properties) {
        LTString value_ = LTCStringKeyedArray_Get(s.properties, name, NULL);
        ltstring_set(&value_, value);
        LTCStringKeyedArray_Set(s.properties, name, value_);
    }
    s.mutex->API->Unlock(s.mutex);
}

static const char *TiltImpl_GetProperty(const char *name, const char *defaultValue) {
    s.mutex->API->Lock(s.mutex);
    LTString value = NULL;
    if (s.properties) value = LTCStringKeyedArray_Get(s.properties, name, NULL);
    s.mutex->API->Unlock(s.mutex);
    return value ? value : defaultValue;
}

static void TiltImpl_EnableReportLevel(TiltReportLevel level) {
    s.reportLevelEnabled[level] = true;
}

static void TiltImpl_DisableReportLevel(TiltReportLevel level) {
    s.reportLevelEnabled[level] = false;
}

static void TiltImpl_SetFailureLimit(u32 failureLimit) {
    s.failureLimit = failureLimit;
}

static bool TiltImpl_MockObject(const char *objectName, const char *specialization, const char *mockSpecialization) {
    if (!objectName || !specialization || !mockSpecialization) return false;
    TiltImpl_Report(kTiltReportLevel_Info, "mocking \"%s:%s\" to the \"%s:%s\" specialization",
                                                objectName, specialization, objectName, mockSpecialization);
    bool status = LT_GetCore()->MockObject(objectName, specialization, mockSpecialization);
    if (!status) TILT_INTERNAL_ABORT("Mocking Failed");
    return status;
}

static bool TiltImpl_UnmockObject(const char *objectName, const char *specialization) {
    if (!objectName || !specialization) return false;
    TiltImpl_Report(kTiltReportLevel_Info, "unmocking to \"%s:%s\"", objectName, specialization);
    bool status = LT_GetCore()->UnmockObject(objectName, specialization);
    if (!status) TILT_INTERNAL_ABORT("Unmocking Failed");
    return status;
}

static void TiltImpl_EnableTimestamps(bool enable) {
    s.useTimestamps = enable;
}

static void TiltImpl_SetClientData(void *clientData) {
    s.clientData = clientData;
}

static void *TiltImpl_GetClientData(void) {
    return s.clientData;
}

static void CreateTest(const char *testName, bool skipped) {
    if (!TiltTryLock()) return;
    if (!s.tests) {
        s.tests = lt_createobject(LTArray);
        if (!s.tests) {
            TILT_INTERNAL_ABORT("Allocation error");
        }
        s.tests->API->InitAsStructArray(s.tests, sizeof(TestRecord));
    }
    /* Add test to list */
    TestRecord test = {
        .name   = ltstring_create(testName),
        .status = skipped ? kTiltTestStatus_Skipped : kTiltTestStatus_Ok
    };
    if (!test.name) {
        TILT_INTERNAL_ABORT("Allocation error");
        s.mutex->API->Unlock(s.mutex);
        return;
    }
    if (s.tests->API->Append(s.tests, &test) < 0) {
        TILT_INTERNAL_ABORT("Allocation error");
        ltstring_destroy(test.name);
        s.mutex->API->Unlock(s.mutex);
        return;
    }
    s.currentTest = s.tests->API->Get(s.tests, s.numTests++, NULL);
    if (skipped) s.numTestsSkipped++;
    s.mutex->API->Unlock(s.mutex);
}

static void TiltImpl_StartTest(const char *testName) {
    LT_GetCore()->FlushConsoleOutput();
    CreateTest(testName, false);
}

static void TiltImpl_FinishTest(void) {
    LT_GetCore()->FlushConsoleOutput();
    s.currentTest = NULL;
}

static void TiltImpl_SkipTest(const char *testName) {
    CreateTest(testName, true);
}

static const char *TiltImpl_GetTestName(void) {
    s.mutex->API->Lock(s.mutex);
    const char *name = NULL;
    if (s.currentTest) name = s.currentTest->name;
    s.mutex->API->Unlock(s.mutex);
    return name;
}

static TiltTestStatus TiltImpl_GetTestStatus(void) {
    return s.currentTest->status;
}

static TiltTestStatus TiltImpl_GetOverallTestStatus(void) {
    TiltTestStatus status = kTiltTestStatus_Ok;
    if (s.aborted) status = kTiltTestStatus_Aborted;
    else if (s.numTestsFailed > 0) status = kTiltTestStatus_Failed;
    return status;
}

static bool DestroyPropertyProc(LTAssociativeArray *array, const void *name, u16 keySize, void *value, void *clientData) {
    LT_UNUSED(array); LT_UNUSED(name); LT_UNUSED(keySize); LT_UNUSED(clientData);
    ltstring_destroy(value);
    return true;
}

static void TiltImpl_ClearTestSuite(void) {
    if (!TiltTryLock()) return;
    if (s.tests) {
        /* Clear out test records */
        u32 numTests = s.tests->API->GetCount(s.tests);
        for (u32 ix = 0; ix < numTests; ix++) {
            TestRecord *test = s.tests->API->Get(s.tests, ix, NULL);
            ltstring_destroy(test->name);
        }
        lt_destroyobject(s.tests);
        s.tests = NULL;
    }
    lt_destroyobject(s.PRNGs);
    s.PRNGs = NULL;
    lt_destroyobject(s.stopwatches);
    s.stopwatches = NULL;
    if (s.properties) {
        s.properties->API->Enumerate(s.properties, DestroyPropertyProc, NULL);
        lt_destroyobject(s.properties);
        s.properties = NULL;
    }
    ltstring_destroy(s.testSuiteName);
    s.testSuiteName = NULL;
    s.mutex->API->Unlock(s.mutex);
}

static const u64 s_pcgMultiplier = LT_CONSTU64(6364136223846793005);

static u32 TiltImpl_PrngCreate(u64 streamVector, u64 seedPosition) {
    /* Seed and initialize PRNG */
    PRNG prng = { .state = 0, .increment = (streamVector << 1) | 1 };
    prng.state  = prng.state * s_pcgMultiplier + prng.increment;
    prng.state += seedPosition;
    prng.state  = prng.state * s_pcgMultiplier + prng.increment;
    /* Add to PRNG array */
    s.mutex->API->Lock(s.mutex);
    if (!s.PRNGs) {
        s.PRNGs = lt_createobject(LTArray);
        LT_ASSERT(s.PRNGs);
        s.PRNGs->API->InitAsStructArray(s.PRNGs, sizeof(PRNG));
    }
    u32 id = s.PRNGs->API->Append(s.PRNGs, &prng);
    LT_ASSERT(id <= kTiltMaxResourceID);
    s.mutex->API->Unlock(s.mutex);
    return id;
}

static void TiltImpl_PrngGenerateRandomBytes(u32 id, u8 *bufferToFill, u32 numBytes) {
    s.mutex->API->Lock(s.mutex);
    PRNG *prng = s.PRNGs->API->Get(s.PRNGs, id, NULL);
    if (!prng) {
        s.mutex->API->Unlock(s.mutex);
        return;
    }
    u32 result;
    u8 *data = (u8 *)&result;
    do {
        u64 stateOld   = prng->state;
        prng->state    = stateOld * s_pcgMultiplier + prng->increment;
        u32 xorShifted = (u32)(((stateOld >> 18u) ^ stateOld) >> 27u);
        s32 rotation   = stateOld >> 59u;
        result         = (xorShifted >> rotation) | (xorShifted << ((-rotation) & 31));
        if (numBytes > 3) {
            *bufferToFill++ = data[0]; *bufferToFill++ = data[1];
            *bufferToFill++ = data[2]; *bufferToFill++ = data[3];
            numBytes -= 4;
        }
    } while (numBytes > 3);
    while (numBytes--) *bufferToFill++ = *data++;
    s.mutex->API->Unlock(s.mutex);
}

static u32 TiltImpl_PrngGenerateRandomNumber(u32 id, u32 maxVal) {
    u32 modulus = maxVal + 1;
    /* Clever trick of the RNG masters to avoid bias */
    u32 threshold = -modulus % modulus;
    u32 value;
    while (1) {
        TiltImpl_PrngGenerateRandomBytes(id, (u8 *)&value, sizeof(value));
        if (value >= threshold) return value % modulus;
    }
}

static u32 TiltImpl_StopwatchCreate(void) {
    Stopwatch stopwatch = { .start = LT_GetCore()->GetKernelTime(), .accumulated = LTTime_Zero() };
    s.mutex->API->Lock(s.mutex);
    if (!s.stopwatches) {
        s.stopwatches = lt_createobject(LTArray);
        LT_ASSERT(s.stopwatches);
        s.stopwatches->API->InitAsStructArray(s.stopwatches, sizeof(Stopwatch));
    }
    u32 id = s.stopwatches->API->Append(s.stopwatches, &stopwatch);
    LT_ASSERT(id <= kTiltMaxResourceID);
    s.mutex->API->Unlock(s.mutex);
    return id;
}

static void TiltImpl_StopwatchStart(u32 id) {
    Stopwatch *stopwatch = s.stopwatches->API->Get(s.stopwatches, id, NULL);
    stopwatch->start = LT_GetCore()->GetKernelTime();
}

static LTTime TiltImpl_StopwatchStop(u32 id) {
    LTTime now = LT_GetCore()->GetKernelTime();
    Stopwatch *stopwatch = s.stopwatches->API->Get(s.stopwatches, id, NULL);
    stopwatch->accumulated = LTTime_Add(stopwatch->accumulated, LTTime_Subtract(now, stopwatch->start));
    return stopwatch->accumulated;
}

static LTTime TiltImpl_StopwatchRead(u32 id) {
    LTTime now = LT_GetCore()->GetKernelTime();
    Stopwatch *stopwatch = s.stopwatches->API->Get(s.stopwatches, id, NULL);
    LTTime elapsed = LTTime_Add(stopwatch->accumulated, LTTime_Subtract(now, stopwatch->start));
    return elapsed;
}

static void TiltImpl_StopwatchReset(u32 id) {
    Stopwatch *stopwatch = s.stopwatches->API->Get(s.stopwatches, id, NULL);
    stopwatch->accumulated = LTTime_Zero();
}

static void TiltImpl_ReportTestSuiteStarted(void) {
    if (!TiltTryLock()) return;
    LT_GetCore()->ConsolePrint("\n### Running %s ###\n\n", s.testSuiteName);
    s.mutex->API->Unlock(s.mutex);
}

static void TiltImpl_ReportTestSuiteSummary(void) {
    if (!TiltTryLock()) return;
    LT_GetCore()->ConsolePutString("\n### Summary ###\n\n");
    u32 numTests = s.tests ? s.tests->API->GetCount(s.tests) : 0;
    if (numTests) {
        u32 numTestsSucceeded = numTests - s.numTestsFailed - s.numTestsCannotRun - s.numTestsSkipped;
        LT_GetCore()->ConsolePrint("Total tests:         %lu\n", LT_Pu32(numTests));
        LT_GetCore()->ConsolePrint("Succeeded tests:     %lu\n", LT_Pu32(numTestsSucceeded));
        LT_GetCore()->ConsolePrint("Failed tests:        %lu\n", LT_Pu32(s.numTestsFailed));
        LT_GetCore()->ConsolePrint("Unable to run tests: %lu\n", LT_Pu32(s.numTestsCannotRun));
        LT_GetCore()->ConsolePrint("Skipped tests:       %lu\n", LT_Pu32(s.numTestsSkipped));
        if (s.numTestsFailed) {
            /* Report test failures */
            LT_GetCore()->ConsolePutString("\n### Failed tests ###\n\n");
            for (u32 ix = 0; ix < numTests; ix++) {
                TestRecord *test = s.tests->API->Get(s.tests, ix, NULL);
                if (test->status == kTiltTestStatus_Failed) LT_GetCore()->ConsolePrint("%s\n", test->name);
            }
        }
        if (s.numTestsSkipped) {
            /* Report skipped tests */
            LT_GetCore()->ConsolePutString("\n### Skipped tests ###\n\n");
            for (u32 ix = 0; ix < numTests; ix++) {
                TestRecord *test = s.tests->API->Get(s.tests, ix, NULL);
                if (test->status == kTiltTestStatus_Skipped) LT_GetCore()->ConsolePrint("%s\n", test->name);
            }
        }
    } else {
        LT_GetCore()->ConsolePrint("NOTE: No tests were run\n");
    }
    if (s.aborted) LT_GetCore()->ConsolePutString("\nNOTE: Test suite aborted abnormally\n");
    else if (s.numTestsFailed == 0) LT_GetCore()->ConsolePutString("\n### PASSED ###\n");
    LT_GetCore()->ConsolePutString("\n");
    s.mutex->API->Unlock(s.mutex);
}


static void TiltImpl_ReportTestStarted(void) {
    if (!TiltTryLock()) return;
    if (s.useTimestamps) {
        char buf[24];
        LT_GetCore()->FormatCanonicalTimeString(LT_GetCore()->GetKernelTime(), buf, sizeof(buf), true);
        LT_GetCore()->ConsolePrint("%s Running test %s...\n", buf, s.currentTest->name);
    } else {
        LT_GetCore()->ConsolePrint("Running test %s...\n", s.currentTest->name);
    }
    LT_GetCore()->FlushConsoleOutput();
    s.mutex->API->Unlock(s.mutex);
}

static void TiltImpl_ReportTestResult(void) {
    static const char *s_statusString[] = { "success", "failure", "cannot-run", "skipped" };
    if (!TiltTryLock()) return;
    LT_GetCore()->FlushConsoleOutput();
    bool finished = (s.currentTest->status == kTiltTestStatus_Ok) || (s.currentTest->status == kTiltTestStatus_Failed);
    if (s.useTimestamps) {
        char buf[24];
        LT_GetCore()->FormatCanonicalTimeString(LT_GetCore()->GetKernelTime(), buf, sizeof(buf), true);
        LT_GetCore()->ConsolePrint("%s %s %s: %s\n", buf,
                                    finished ? "Finished test" : "Test", s.currentTest->name,
                                    s_statusString[s.currentTest->status]);
    } else {
        LT_GetCore()->ConsolePrint("%s %s: %s\n",
                                    finished ? "Finished test" : "Test", s.currentTest->name,
                                    s_statusString[s.currentTest->status]);
    }
    s.mutex->API->Unlock(s.mutex);
}

static void TiltImpl_ReportTestBanner(const char *text) {
    LT_GetCore()->ConsolePrint("\n--- %s ---\n\n", text);
}

static void TiltImpl_AbortTestSuite(const char *file, u32 line, const char *fmt, ...) {
    s.mutex->API->Lock(s.mutex);
    PrintPrefix(file, line, "failure (abort)");
    if (fmt) {
        lt_va_list args;
        lt_va_start(args, fmt);
        LT_GetCore()->ConsolePrintV(fmt, args);
        lt_va_end(args);
        LT_GetCore()->ConsolePutString("\n");
    } else {
        LT_GetCore()->ConsolePutString("(no message)\n");
    }
    if (s.currentTest && !s.currentTest->status) {
        s.currentTest->status = kTiltTestStatus_Failed;
        s.numTestsFailed++;
    }
    s.aborted = true;
    s.mutex->API->Unlock(s.mutex);
}

static void TiltImpl_Report(TiltReportLevel level, const char *fmt, ...) {
    static const char *levelString[] = { "debug", "info", "warning", "message" };
    if (!s.reportLevelEnabled[level]) return;
    s.mutex->API->Lock(s.mutex);
    LT_GetCore()->FlushConsoleOutput();
    if (s.useTimestamps) {
        char buf[24];
        LT_GetCore()->FormatCanonicalTimeString(LT_GetCore()->GetKernelTime(), buf, sizeof(buf), true);
        LT_GetCore()->ConsolePrint("%s %s ", buf, levelString[level]);
    } else {
        LT_GetCore()->ConsolePrint("  %s ", levelString[level]);
    }
    if (fmt) {
        lt_va_list args;
        lt_va_start(args, fmt);
        LT_GetCore()->ConsolePrintV(fmt, args);
        lt_va_end(args);
        LT_GetCore()->ConsolePutString("\n");
    } else {
        LT_GetCore()->ConsolePutString("(no message)\n");
    }
    s.mutex->API->Unlock(s.mutex);
}

static void TiltImpl_ReportFailure(const char *file, u32 line, const char *fmt, ...) {
    s.mutex->API->Lock(s.mutex);
    ++s.failureCount;
    if (s.failureCount < s.failureLimit) {
        PrintPrefix(file, line, "failure");
        if (fmt) {
            lt_va_list args;
            lt_va_start(args, fmt);
            LT_GetCore()->ConsolePrintV(fmt, args);
            lt_va_end(args);
            LT_GetCore()->ConsolePutString("\n");
        } else {
            LT_GetCore()->ConsolePutString("(no message)\n");
        }
    } else if (s.failureCount == s.failureLimit) {
        LT_GetCore()->ConsolePutString("NOTE: Failure limit reached, squelching further failure output\n");
    }
    if (!s.currentTest) {
        // No current test (e.g.: BeforeAllTests hook or AfterAllTests hook )
        TILT_INTERNAL_ABORT("Failed while not running a test");
    } else if (!s.currentTest->status) {
        s.currentTest->status = kTiltTestStatus_Failed;
        s.numTestsFailed++;
    }
    s.mutex->API->Unlock(s.mutex);
}

static void TiltImpl_ReportCannotRun(const char *file, u32 line, const char *fmt, ...) {
    s.mutex->API->Lock(s.mutex);
    PrintPrefix(file, line, "cannot-run");
    if (fmt) {
        lt_va_list args;
        lt_va_start(args, fmt);
        LT_GetCore()->ConsolePrintV(fmt, args);
        lt_va_end(args);
        LT_GetCore()->ConsolePutString("\n");
    } else {
        LT_GetCore()->ConsolePutString("(no message)\n");
    }
    if (s.currentTest) {
        if (!s.currentTest->status) {
            s.currentTest->status = kTiltTestStatus_CannotRun;
            s.numTestsCannotRun++;
        }
    } else {
        // No current test
        TILT_INTERNAL_ABORT("Failed while not running a test");
    }
    s.mutex->API->Unlock(s.mutex);
}

static void TiltImpl_ReportInteger(const char *name, s64 value, const char *units) {
    LT_GetCore()->ConsolePrint("  quantity: %s = %lld %s\n", name, LT_Ps64(value), units ? units : "");
}

static void TiltImpl_ReportFloat(const char *name, double value, const char *units) {
    LT_GetCore()->ConsolePrint("  quantity: %s = %e %s\n", name, value, units ? units : "");
}

static void TiltImpl_ReportBinary(const char *name, const u8 *buffer, u32 sizeInBytes) {
    s.mutex->API->Lock(s.mutex);
    LT_GetCore()->ConsolePrint("  quantity: %s = ", name);
    while (sizeInBytes--) {
        const char str[] = "0123456789abcdef";
        LT_GetCore()->ConsolePutChars(&str[*buffer >> 4],  1);
        LT_GetCore()->ConsolePutChars(&str[*buffer & 0xf], 1);
        buffer++;
    }
    LT_GetCore()->ConsolePutString("\n");
    s.mutex->API->Unlock(s.mutex);
}

static void TiltImpl_ReportElapsedTime(const char *name, LTTime time, u32 numIterations) {
    char buf[24];
    if (numIterations) {
        time = LTTime_Divide(time, numIterations);
        LT_GetCore()->FormatCanonicalTimeString(time, buf, sizeof(buf), false);
        LT_GetCore()->ConsolePrint("  elapsed time: %s = %s seconds per iteration (%lu iterations)\n",
                                      name, buf, LT_Pu32(numIterations));
    } else {
        LT_GetCore()->FormatCanonicalTimeString(time, buf, sizeof(buf), false);
        LT_GetCore()->ConsolePrint("  elapsed time: %s = %s seconds\n", name, buf);
    }
}

/*_______________________________________________
  TiltEngine Object Constructor and Destructor */
static bool TiltImpl_ConstructObject(TiltImpl *tilt) {
    LT_UNUSED(tilt);
    return true;
}

static void TiltImpl_DestructObject(TiltImpl *tilt) {
    LT_UNUSED(tilt); /* I am a singleton so nothing goes here (for now) */
}

/*______________________________
  Tilt LTObjectApi definition */
define_LTObjectImplPublic(Tilt, TiltImpl,
    SetTestSuiteName,
    GetTestSuiteName,
    SetProperty,
    GetProperty,
    EnableReportLevel,
    DisableReportLevel,
    SetFailureLimit,
    MockObject,
    UnmockObject,
    EnableTimestamps,
    SetClientData,
    GetClientData,
    StartTest,
    FinishTest,
    SkipTest,
    GetTestName,
    GetTestStatus,
    AbortTestSuite,
    GetOverallTestStatus,
    ClearTestSuite,
    PrngCreate,
    PrngGenerateRandomBytes,
    PrngGenerateRandomNumber,
    StopwatchCreate,
    StopwatchStart,
    StopwatchStop,
    StopwatchRead,
    StopwatchReset,
    ReportTestSuiteStarted,
    ReportTestSuiteSummary,
    ReportTestStarted,
    ReportTestResult,
    ReportTestBanner,
    Report,
    ReportFailure,
    ReportCannotRun,
    ReportInteger,
    ReportFloat,
    ReportBinary,
    ReportElapsedTime);

/*_____________
  TiltEngine */

typedef struct {
    LTTime       timeout;
    const char **testList;
    u32          testListCount;
    u32          stackSize;
    bool         quiet;
    bool         verbose;
    bool         automation;
    bool         isExcludeList;
} Options;

typedef u8 TiltEngineState;
enum TiltEngineState {
    kTiltEngineState_BeforeAllTests,
    kTiltEngineState_BeforeTest,
    kTiltEngineState_InTest,
    kTiltEngineState_AfterTest,
    kTiltEngineState_AfterAllTests,
    kTiltEngineState_CompletedTests
};

/*__________________________
  TiltEngine Private Data */
typedef_LTObjectImpl(JiltEngine, JiltEngineImpl) {
    Tilt                      *tilt;
    LTOThread                 *thread;
    const TiltEngineTestHooks *hooks;
    const TiltEngineTest      *tests;
    TiltEngineCleanupProc     *cleanupProc;
    void                      *cleanupData;
    Options                    options;
    u32                        availRAMbefore;
    u32                        availRAMafter;
    TiltEngineState            state;
    u16                        numTests;
    u16                        testNum;
    bool                       isDeferred;
} LTOBJECT_API;

/*____________________________
  TiltEngine Implementation */

static void JiltEngineImpl_ConfigureTestSuite(JiltEngineImpl *engine, const TiltEngineTest tests[],
                                                 u32 numTests, const TiltEngineTestHooks *hooks) {
    engine->tests    = tests;
    engine->numTests = numTests;
    engine->hooks    = hooks;
}

static void JiltEngineImpl_SetTestSuiteName(JiltEngineImpl *engine, char *testSuiteName) {
    engine->tilt->API->SetTestSuiteName(testSuiteName);
}

static void JiltEngineImpl_SetTestSuiteTimeout(JiltEngineImpl *engine, LTTime timeout) {
    engine->options.timeout = timeout;
}

static void JiltEngineImpl_SetTestThreadStackSize(JiltEngineImpl *engine, u32 stackSize) {
    engine->options.stackSize = stackSize;
}

static bool IsTestSkipped(JiltEngineImpl *engine, const TiltEngineTest *test) {
    Options *options = &engine->options;
    TiltTestStatus status = engine->tilt->API->GetOverallTestStatus();
    if (status == kTiltTestStatus_Aborted) return true;
    if (options->automation && test->flags & kTiltEngineFlag_NoAutomation) return true;
    if (options->testListCount == 0) return false;
    for (u32 ix = 0; ix < options->testListCount; ix++) {
        if (!lt_strcmp(test->name, options->testList[ix])) return options->isExcludeList;
    }
    return !options->isExcludeList;
}

/* TiltEngine test suite state machine */
static void TiltEngineMainTaskProc(void *clientData) {
    JiltEngineImpl            *engine = (JiltEngineImpl *)clientData;
    Tilt                      *tilt   = engine->tilt;
    const TiltEngineTestHooks *hooks  = engine->hooks;

    /* Start state if it wasn't deferred and reset deferral state */
    bool startState = (engine->isDeferred) ? false : true;
    engine->isDeferred = false;

    if (engine->state == kTiltEngineState_BeforeAllTests) {
        if (startState) {
            /* Invoke (optional) BeforeAllTests() hook */
            if (hooks && hooks->BeforeAllTests) (hooks->BeforeAllTests)(tilt);
        }
        if (!engine->isDeferred) {
            engine->state = kTiltEngineState_BeforeTest;
            if (tilt->API->GetOverallTestStatus() == kTiltTestStatus_Aborted || engine->numTests == 0) {
                engine->state = kTiltEngineState_AfterAllTests;
            }
            startState = true;
        }
    }

    if (engine->state == kTiltEngineState_BeforeTest) {
        if (startState) {
            if (engine->tests[engine->testNum].testProc) {
                /* Skip this test ? */
                if (IsTestSkipped(engine, &engine->tests[engine->testNum])) {
                    tilt->API->SkipTest(engine->tests[engine->testNum].name);
                } else {
                    /* Start test beginning with (optional) BeforeTest hook */
                    tilt->API->StartTest(engine->tests[engine->testNum].name);
                    if (!engine->options.quiet) tilt->API->ReportTestStarted();
                    if (hooks && hooks->BeforeTest) (hooks->BeforeTest)(tilt);
                }
            } else {
                /* Print test section header (if not aborted AND user didn't just invoke specific tests) */
                if (!engine->options.quiet && tilt->API->GetOverallTestStatus() != kTiltTestStatus_Aborted) {
                    if (engine->options.testListCount == 0 || engine->options.isExcludeList)
                        tilt->API->ReportTestBanner(engine->tests[engine->testNum].name);
                }
            }
        }
        if (!engine->isDeferred) {
            if (engine->tests[engine->testNum].testProc) {
                engine->state = kTiltEngineState_InTest;
            } else if (++engine->testNum == engine->numTests) {
                engine->state = kTiltEngineState_AfterAllTests;
                startState    = true;
            } else {
                engine->state = kTiltEngineState_BeforeTest;
                /* Loop to next test */
                engine->thread->API->QueueTaskProc(engine->thread, TiltEngineMainTaskProc, NULL, clientData);
            }
            startState = true;
        }
    }

    if (engine->state == kTiltEngineState_InTest) {
        if (startState) {
            /* Invoke Main TestProc (unless failed in BeforeTest() or skipped) */
            if (tilt->API->GetTestStatus() == kTiltTestStatus_Ok) (engine->tests[engine->testNum].testProc)(tilt);
        }
        if (!engine->isDeferred) {
            engine->state = kTiltEngineState_AfterTest;
            startState    = true;
        }
    }

    if (engine->state == kTiltEngineState_AfterTest) {
        if (startState) {
            /* End test with (optional) AfterTest hook */
            if (tilt->API->GetTestStatus() != kTiltTestStatus_Skipped) {
                if (engine->cleanupProc) {
                    (engine->cleanupProc)(engine->tilt, engine->cleanupData);
                    engine->cleanupProc = NULL;
                }
                if (hooks && hooks->AfterTest) (hooks->AfterTest)(tilt);
            }
        }
        if (!engine->isDeferred) {
            TiltTestStatus status = tilt->API->GetTestStatus();
            if (!engine->options.quiet && (status == kTiltTestStatus_Ok || status == kTiltTestStatus_Failed)) {
                /* Do not report skipped or can't continue tests here */
                tilt->API->ReportTestResult();
            }
            /* Advance to next test or end */
            if (++engine->testNum == engine->numTests) {
                engine->state = kTiltEngineState_AfterAllTests;
                startState    = true;
            } else {
                engine->state = kTiltEngineState_BeforeTest;
                /* Loop to next test */
                engine->thread->API->QueueTaskProc(engine->thread, TiltEngineMainTaskProc, NULL, clientData);
            }
        }
    }

    if (engine->state == kTiltEngineState_AfterAllTests) {
        if (startState) {
            /* Indicate last test has finished before proceeding */
            tilt->API->FinishTest();
            /* Invoke (optional) AfterAllTests() hook */
            if (hooks && hooks->AfterAllTests) (hooks->AfterAllTests)(tilt);
        }
        if (!engine->isDeferred) {
            engine->state = kTiltEngineState_CompletedTests;
            /* Clean up after testing */
            if (!LT_GetCore()->UnmockObject(NULL, NULL)) {
                TILT_INTERNAL_ABORT("Unmock All Failed");
            }
            engine->thread->API->Terminate(engine->thread);
        }
    }
}

static void ListTests(JiltEngineImpl *engine) {
    for (u32 testNum = 0; testNum < engine->numTests; testNum++) {
        if (engine->tests[testNum].testProc) LT_GetCore()->ConsolePrint("%s\n", engine->tests[testNum].name);
    }
}

static void DisplayUsage(const char **argv) {
    LT_GetCore()->ConsolePrint    ("usage: ltrun %s [<option> ...] [-- test ...]\n", argv[1]);
    LT_GetCore()->ConsolePrint    ("  or   ltrun %s [<option> ...] [--exclude-tests test ...]\n\n", argv[1]);
    LT_GetCore()->ConsolePutString("where <option> is one of:\n\n");
    LT_GetCore()->ConsolePutString("  --list               List all tests and exit\n");
    LT_GetCore()->ConsolePutString("  --quiet              Produce minimal output (statistics and list of failed tests)\n");
    LT_GetCore()->ConsolePutString("  --verbose            Produce maximal output, including all messages from tests\n");
    LT_GetCore()->ConsolePutString("  --failure-limit <n>  Abort after <n> failures have been reported\n");
    LT_GetCore()->ConsolePutString("  --automation         Skip tests that shouldn't run in automation\n");
    LT_GetCore()->ConsolePrint    ("  --timeout <n>        Timeout (in seconds) for entire test run (default %lu sec)\n",
                                      LT_Pu32(kTiltEngineDefaultTestTimeoutSeconds));
    LT_GetCore()->ConsolePutString("  --stacksize <n>      Stack size (in bytes) for test thread\n");
    LT_GetCore()->ConsolePutString("  --timestamp          Prepend output with kernel timestamp\n");
    LT_GetCore()->ConsolePutString("  <property>=<value>   Set test property to a value\n");
}

static int ParseArguments(JiltEngineImpl *engine, int argc, const char **argv) {
    if (argc < 2) return kTiltEngineStatus_BadOptions;
    if (lt_strcmp(engine->tilt->API->GetTestSuiteName(), "") == 0) {
        /* Obtain default test suite name from command line (if not already set) */
        engine->tilt->API->SetTestSuiteName(argv[1]);
    }
    for (int argIndex = 2; argIndex < argc; argIndex++) {
        const char *arg = argv[argIndex];
        if (!lt_strcmp(arg, "--list")) {
            return kTiltEngineStatus_ListTests;
        } else if (!lt_strcmp(arg, "--quiet")) {
            engine->options.quiet = true;
            engine->tilt->API->DisableReportLevel(kTiltReportLevel_Info);
        } else if (!lt_strcmp(arg, "--verbose")) {
            engine->options.verbose = true;
            engine->tilt->API->EnableReportLevel(kTiltReportLevel_Debug);
        } else if (!lt_strcmp(arg, "--failure-limit")) {
            if (argIndex == argc - 1) return kTiltEngineStatus_BadOptions;
            engine->tilt->API->SetFailureLimit(lt_strtou32(argv[++argIndex], NULL, 10));
        } else if (!lt_strcmp(arg, "--automation")) {
            engine->options.automation = true;
        } else if (!lt_strcmp(arg, "--timeout")) {
            if (argIndex == argc - 1) return kTiltEngineStatus_BadOptions;
            engine->options.timeout = LTTime_Seconds(lt_strtou32(argv[++argIndex], NULL, 10));
        } else if (!lt_strcmp(arg, "--stacksize")) {
            if (argIndex == argc - 1) return kTiltEngineStatus_BadOptions;
            engine->options.stackSize = lt_strtou32(argv[++argIndex], NULL, 10);
        } else if (!lt_strcmp(arg, "--timestamp")) {
            engine->tilt->API->EnableTimestamps(true);
        } else if (!lt_strcmp(arg, "--") || !lt_strcmp(arg, "--include-tests")) {
            /* All remaining arguments are test names to include */
            ++argIndex;
            engine->options.testList      = argv + argIndex;
            engine->options.testListCount = argc - argIndex;
            break;
        } else if (!lt_strcmp(arg, "--exclude-tests")) {
            /* All remaining arguments are test names to exclude */
            ++argIndex;
            engine->options.testList      = argv + argIndex;
            engine->options.testListCount = argc - argIndex;
            engine->options.isExcludeList = true;
            break;
        } else {
            const char *equalSign = lt_strchr(arg, '=');
            if (equalSign) {
                LTString property = ltstring_createsubstring(arg, 0, equalSign - arg);
                engine->tilt->API->SetProperty(property, equalSign + 1);
                ltstring_destroy(property);
            } else {
                return kTiltEngineStatus_BadOptions;
            }
        }
    }
    return kTiltTestStatus_Ok;
}

static int JiltEngineImpl_RunTestSuite(JiltEngineImpl *engine, int argc, const char **argv) {
    /* Save available RAM *before* parsing arguments to account for test properties, etc. */
    engine->availRAMbefore = LT_GetCore()->GetAvailableSystemRAM();

    int rtn = ParseArguments(engine, argc, argv);
    switch (rtn) {
    case kTiltTestStatus_Ok:
        break;
    case kTiltEngineStatus_ListTests:
        ListTests(engine);
        return kTiltTestStatus_Ok;
    case kTiltEngineStatus_BadOptions:
        DisplayUsage(argv);
        return rtn;
    default:
        return rtn;
    }

    /* Start test thread */
    engine->thread = lt_createobject(LTOThread);
    if (!engine->thread) {
        TILT_INTERNAL_ABORT("Cannot create test thread");
        return -1;
    }

    engine->tilt->API->ReportTestSuiteStarted();

    LTOThread *thread = engine->thread;
    thread->API->SetStackSize(thread, engine->options.stackSize);
    thread->API->Start(thread, "TiltEngine", NULL, NULL);

    /* Initialize and run test state machine */
    engine->testNum    = 0;
    engine->state      = kTiltEngineState_BeforeAllTests;
    engine->isDeferred = false;
    thread->API->QueueTaskProc(thread, TiltEngineMainTaskProc, NULL, engine);

    /* End test thread */
    rtn = 0;
    if (!thread->API->WaitUntilFinished(thread, engine->options.timeout)) {
        TILT_INTERNAL_ABORT("Tests timed out\n");
        thread->API->Terminate(thread);
    }
    if (engine->state != kTiltEngineState_CompletedTests) {
        /* Indicate skipped tests */
        for (engine->testNum++; engine->testNum < engine->numTests; engine->testNum++) {
            if (engine->tests[engine->testNum].testProc)
                engine->tilt->API->SkipTest(engine->tests[engine->testNum].name);
        }
    }
    lt_destroyobject(thread);

    engine->tilt->API->ReportTestSuiteSummary();
    engine->tilt->API->ClearTestSuite();

    engine->availRAMafter = LT_GetCore()->GetAvailableSystemRAM();
    LT_GetCore()->ConsolePrint("Available RAM before and after test (%lu - %lu = %ld potentially leaked)\n\n",
                                  LT_Pu32(engine->availRAMbefore), LT_Pu32(engine->availRAMafter),
                                  LT_Ps32(engine->availRAMbefore - engine->availRAMafter));

    return engine->tilt->API->GetOverallTestStatus();
}

static Tilt *JiltEngineImpl_GetTestFramework(JiltEngineImpl *engine) {
    return engine->tilt;
}

static void DeferredTestTimeoutProc(void * clientData) {
    JiltEngineImpl *engine = (JiltEngineImpl *)clientData;
    TILT_INTERNAL_ABORT("Deferred completion timeout in test %s", engine->tilt->API->GetTestName());
    if (engine->cleanupProc) {
        (engine->cleanupProc)(engine->tilt, engine->cleanupData);
        engine->cleanupProc = NULL;
    }
    engine->thread->API->QueueTaskProc(engine->thread, TiltEngineMainTaskProc, NULL, engine);
}

static void JiltEngineImpl_DeferTestCompletion(JiltEngineImpl *engine, LTTime timeout,
                                                  TiltEngineCleanupProc *cleanupProc, void *clientData) {
    LT_UNUSED(cleanupProc); LT_UNUSED(clientData);
    engine->isDeferred = true;
    engine->cleanupProc = cleanupProc;
    engine->cleanupData = clientData;
    engine->thread->API->SetTimer(engine->thread, timeout, DeferredTestTimeoutProc, NULL, engine);
}

static void JiltEngineImpl_SignalTestCompletion(JiltEngineImpl *engine, const char *testName) {
    if (testName && (lt_strcmp(testName, engine->tilt->API->GetTestName()) != 0)) {
        TILT_INTERNAL_ABORT("SignalTestCompletion() called for incorrect test *%s* != *%s*",
                                testName, engine->tilt->API->GetTestName());
        return;
    }
    engine->thread->API->KillTimer(engine->thread, DeferredTestTimeoutProc, NULL);
    engine->thread->API->QueueTaskProc(engine->thread, TiltEngineMainTaskProc, NULL, engine);
}

static bool JiltEngineImpl_ConstructObject(JiltEngineImpl *engine) {
    /* TODO: don't need to zero out certain members as LTCore does that ??? */
    engine->hooks = NULL;
    engine->tilt  = lt_createobject(Tilt);
    /* Default options */
    lt_memset(&engine->options, 0, sizeof(Options));
    engine->options.timeout   = LTTime_Seconds(kTiltEngineDefaultTestTimeoutSeconds);
    engine->options.stackSize = kTiltEngineDefaultStackSize;
    return engine->tilt != NULL;
}

static void JiltEngineImpl_DestructObject(JiltEngineImpl *engine) {
    lt_destroyobject(engine->tilt);
}

/*____________________________________
  TiltEngine LTObjectApi definition */
define_LTObjectImplPublic(JiltEngine, JiltEngineImpl,
    ConfigureTestSuite,
    RunTestSuite,
    SetTestSuiteName,
    SetTestSuiteTimeout,
    SetTestThreadStackSize,
    GetTestFramework,
    DeferTestCompletion,
    SignalTestCompletion);

/*______________________________
  Tilt Root interface binding */
static void Tilt_LibFini(void) {
    TiltImpl_ClearTestSuite();
    lt_destroyobject(s.mutex);
    s.mutex = NULL;
}

static bool Tilt_LibInit(void) {
    lt_memset(&s, 0, sizeof(s));
    s.failureLimit = kTiltDefaultFailureLimit;
    TiltImpl_EnableReportLevel(kTiltReportLevel_Info);
    TiltImpl_EnableReportLevel(kTiltReportLevel_Warning);
    TiltImpl_EnableReportLevel(kTiltReportLevel_Report);
    s.mutex = lt_createobject(LTMutex);
    if (!s.mutex) return false;
    return true;
}

define_LTObjectLibrary(1, Tilt_LibInit, Tilt_LibFini);

