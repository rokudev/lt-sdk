/*******************************************************************************
 * tilt/TiltSampleTest.c                              (using TILT 2.0 Framework)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <tilt/JiltEngine.h>

/* Here are some guidelines for writing unit tests using the TILT Engine:
 *
 *  1. Create the object(s) under test in the BeforeAllTests() hook, the BeforeTest()
 *       hook or individual TestProcs. Do NOT use LibInit() or Run() for this.
 *  2. Similarly, destroy object(s) under test in the AfterAllTests() hook, the
 *       AfterTest() hook or individual TestProcs. Do NOT use LibFini() or Run() for this.
 *  3. Do NOT use LT Logging facilities in unit tests, instead use the provided TILT
 *       mechanisms, e.g.: TILT_DEBUG(), TILT_INFO(), etc.
 *  4. The TiltEngine TestList and TestHook tables should be declared _static const_
 *       to avoid use of RAM which might be limited on certain platforms.
 *  5. Constant data tables (e.g.: test vectors) should also be declared static const for
 *       the same reason.
 *  6. If a large working buffer is required, consider allocating it from the heap instead
 *       of declaring it directly in the file scope.
 *  7. The TiltEngine should NOT be used to implement code examples, rather it should
 *       be used only for unit testing.
 *  8. Take time to line up your code vertically so that it is easier to read. Test tables
 *       should look like tables you want to read, not a smear on the page.
 *  9. Take as much pride in writing unit tests as you would with product code.
 */

typedef struct Dummy Dummy;

static JiltEngine  *s_engine;
static Dummy       *s_dummy;
static u32          s_stopwatch;
static u32          s_prng;

/*____________________
  Dummy Mock Object */

/* Note: This dummy mock is a simple example and has no default implementation.
 *       Also, this mock is self-contained in this unittest but in many cases
 *       it is better for mocks to be generally available for shared use. */

typedef_LTObject(Dummy, 1) {
    s32 (* Function)(s32 x, s32 y);
} LTOBJECT_API;

typedef_LTObjectImpl(Dummy, Mock) {
} LTOBJECT_API;

static s32 Mock_Function(s32 x, s32 y) {
    return x + y;
}

static bool Mock_ConstructObject(Mock *mock) {
    LT_UNUSED(mock);
    return true;
}

static void Mock_DestructObject(Mock *mock) {
    LT_UNUSED(mock);
}

define_LTObjectImplPublic(Dummy, Mock,
    Function);

/*_____________
  Unit Tests */

static void SuccessTest(Tilt *tilt) {
    TILT_REPORT(tilt, "Ah, the sweet smell of success!");
    TILT_WARNING(tilt, "... but this is not a very good test");
    TILT_DEBUG(tilt, "... and here's some %s output", "debug");
}

static void FailureTest(Tilt *tilt) {
    TILT_EXPECT_TRUE(tilt, false, "I'm not %s yet!", "dead");
    TILT_EXPECT_TRUE(tilt, true, "Should never see this because first parameter is true");
    TILT_EXPECT_TRUE(tilt, false, NULL);
    TILT_EXPECT_FALSE(tilt, true, "True is not false!");
    TILT_ASSERT_TRUE(tilt, false, "This cannot continue, time to leave");
    TILT_EXPECT_TRUE(tilt, false, "Should never see this because ASSERT should return");
}

static void QuantityTest(Tilt *tilt) {
    TILT_REPORT_INTEGER(tilt, "SomeInteger", 1234);
    TILT_REPORT_FLOAT(tilt, "SomeFloat", 3.14);
    TILT_REPORT_INTEGER_WITH_UNITS(tilt, "SomeIntegerWithUnits", 1234, "mph");
    TILT_REPORT_FLOAT_WITH_UNITS(tilt, "SomeFloatWithUnits", 3.14, "parsecs");
    const u8 array[] = { 0xba, 0x5e, 0xba, 0x11 };
    TILT_REPORT_BINARY(tilt, "SomeBuffer", array, sizeof(array));
}

static void PrngTest(Tilt *tilt) {
    enum { kNumBytes = 1024, kMaxRandomNumber = 16 };
    u8 *buffer = lt_malloc(kNumBytes);
    TILT_ASSERT_TRUE(tilt, buffer, "memory allocation failure");
    tilt->API->PrngGenerateRandomBytes(s_prng, buffer, kNumBytes);
    /* Check the first few words against canon (streamVector 201, seedPosition 3001) */
    u32 expected[4] = { 0x71c375a4, 0x49aaead0, 0x59b3c871, 0xf72edb80 };
    for (u32 ix = 0; ix < sizeof(expected)/sizeof(u32); ix++) {
        TILT_EXPECT_TRUE(tilt, expected[ix] == ((u32 *)buffer)[ix], "value mismatch");
    }
    /* Generate a few random numbers in the interval [0, kMaxRandomNumber] */
    for (u32 ix = 0; ix < 32; ix++) {
        u32 value = tilt->API->PrngGenerateRandomNumber(s_prng, kMaxRandomNumber);
        TILT_EXPECT_TRUE(tilt, value <= kMaxRandomNumber, "value out of range");
    }
    lt_free(buffer);
}

static void MockTest(Tilt *tilt) {
    /* Normally the mock is used by the object under test but for this simple example we use it directly. */
    TILT_EXPECT_TRUE(tilt, s_dummy->API->Function(100, 200) == 300, "I can't add today");
}

static void CannotRunTest(Tilt *tilt) {
    TILT_REPORT_CANNOT_RUN(tilt, "I just don't want to run today");
}

static void CannotRunInBeforeTestHookTest(Tilt *tilt) {
    TILT_WARNING(tilt, "Should not see this message!");
}

static void FailInBeforeTestHookTest(Tilt *tilt) {
    TILT_WARNING(tilt, "Should not see this message!");
}

static void FailInAfterTestHookTest(Tilt *tilt) {
    TILT_INFO(tilt, "Running test %s", tilt->API->GetTestName());
}

static void DontRunInAutomationTest(Tilt *tilt) {
    TILT_INFO(tilt, "This test should not run in automation mode!");
}

/** The timer proc for SimpleAsyncTest. Its job is to signal that the test is complete
 * It also signals test failure, to ensure that failures signalled in these asynchronous
 * test functions are properly recorded by TILT.  */
static void SimpleAsyncTestTimerProc(void *pClientData) {
    Tilt *tilt = (Tilt *)pClientData;
    TILT_DEBUG(tilt, "Timer proc called");
    LTTime elapsed = tilt->API->StopwatchStop(s_stopwatch);
    TILT_REPORT_ELAPSED_TIME(tilt, "AsyncTestTimerProc pause duration", elapsed, 0);
    TILT_REPORT_FAILURE(tilt, "Forcing test failure in SimpleAsyncTest");
    s_engine->API->SignalTestCompletion(s_engine, "SimpleAsync");
}

/* TestCleanupProc for SimpleAsyncTest (ensures Timer is killed) */
static void SimpleAsyncTestCleanupProc(Tilt *tilt, void *clientData) {
    LT_UNUSED(clientData);
    TILT_INFO(tilt, "SimpleAsyncTestReleaseProc called");
    /* Cleanup is run just before the AfterTest() hook (if any). Ensure timer is killed here. */
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    thread->API->KillTimer(thread, SimpleAsyncTestTimerProc, tilt);
    TILT_DEBUG(tilt, "Timer killed");
}

static void SimpleAsyncTest(Tilt *tilt) {
    tilt->API->StopwatchStart(s_stopwatch);
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(2), SimpleAsyncTestCleanupProc, NULL);
    /* Arrange for SimpleAsyncTestTimerProc to be called 1 second from now */
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    /* The one second delay guarantees that the TimerProc gets invoked within the 2 second timeout,
     *  Try setting it to 4 seconds and see what happens.*/
    thread->API->SetTimer(thread, LTTime_Seconds(1), SimpleAsyncTestTimerProc, NULL, (void *)tilt);
}

/*_____________
  Test Hooks */

/** The timer proc for the BeforeAllTests and AfterAllTests hooks.
 *  Its job is to signal that the hooks are complete
 *  (and cancel its own timer to prevent future timer events).  */
static void AsyncAllTimerProc(void * pClientData) {
    Tilt *tilt = (Tilt *)pClientData;
    TILT_DEBUG(tilt, "Timer proc called");
    s_engine->API->SignalTestCompletion(s_engine, NULL);
    TILT_DEBUG(tilt, "Async test complete signalled");

    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    thread->API->KillTimer(thread, AsyncAllTimerProc, tilt);
    TILT_DEBUG(tilt, "Timer killed");
}

/* These are test hook functions, which are called by the test framework
 * at certain points of execution. */

static void BeforeAllTests(Tilt *tilt) {
    TILT_INFO(tilt, "BeforeAllTests called");
    /* ... Mock object(s) here as required ... */
    {
        /* This uses the Dummy Mock object in place of a default implementation */
        tilt_mockobject(tilt, Dummy, Mock);
    }
    /* ... Create object(s) under test here ... */
    {
        /* Normally the mocked object would be created by the object under test, but for this example we create it directly */
        s_dummy = lt_createobject(Dummy);
        TILT_ASSERT_TRUE(tilt, s_dummy, "Cannot create Dummy mock object");

        /* Create a stopwatch for timing test execution */
        s_stopwatch = tilt->API->StopwatchCreate();
        /* Create a PRNG perhaps for stress testing, and yes, you can seed from a TRNG instead of using a fixed seed */
        s_prng = tilt->API->PrngCreate(201, 3001);
        /* Exercise GetProperty callback */
        const char * xyzzyValue = tilt->API->GetProperty("xyzzy", "xyzzyDefault");
        TILT_INFO(tilt, "Property xyzzy has value '%s'", xyzzyValue ? xyzzyValue : "<NULL>");
    }
    /* Exercise deferred completion in BeforeAllTests() */
    {
        s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(2), NULL, NULL);
        /* Arrange for AsyncBeforeAllTimerProc to be called 1 second from now */
        LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
        thread->API->SetTimer(thread, LTTime_Seconds(1), AsyncAllTimerProc, NULL, (void *)tilt);
    }
}

static void AfterAllTests(Tilt *tilt) {
    TILT_INFO(tilt, "AfterAllTests called");
    /* Exercise deferred completion in AfterAllTests() */
    {
        s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(2), NULL, NULL);
        /* Arrange for AsyncBeforeAllTimerProc to be called 1 second from now */
        LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
        thread->API->SetTimer(thread, LTTime_Seconds(1), AsyncAllTimerProc, NULL, (void *)tilt);
    }
    /* ... Destroy object(s) under test here ... */
    {
        lt_destroyobject(s_dummy);
    }
    /* Pretend we failed here... */
    TILT_REPORT_FAILURE(tilt, "Failed AfterAllTestsHook but this in of itself won't fail the test suite");
}

static void BeforeTest(Tilt *tilt) {
    TILT_DEBUG(tilt, "BeforeTest called");
    if (lt_strcmp(tilt->API->GetTestName(), "FailBeforeHook") == 0) {
        TILT_EXPECT_TRUE(tilt, false, "BeforeHook failed");
    }
    if (lt_strcmp(tilt->API->GetTestName(), "CannotRunInBeforeHook") == 0) {
        TILT_REPORT_CANNOT_RUN(tilt, "BeforeHook cannot run");
    }
}

static void AfterTest(Tilt *tilt) {
    TILT_DEBUG(tilt, "AfterTest called");
    if (lt_strcmp(tilt->API->GetTestName(), "FailAfterHook") == 0) {
        TILT_EXPECT_TRUE(tilt, false, "AfterHook failed");
    }
}

/*_______________________
  Test Hook Descriptor */

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
    .BeforeTest     = BeforeTest,
    .AfterTest      = AfterTest
};

/*____________
  Test List */

static const TiltEngineTest s_tests[] = {
  TILT_TEST_SECTION("Basic tests"),
    { SuccessTest,                   "SuccessAlways",         "Always succeeds",                           0 },
    { FailureTest,                   "FailureAlways",         "Never succeeds",                            0 },
    { QuantityTest,                  "Quantities",            "Reports quantities",                        0 },
    { PrngTest,                      "PRNG",                  "Exercises PRNG",                            0 },
    { MockTest,                      "Mock",                  "Exercises Dummy Mock",                      0 },
    { CannotRunTest,                 "CannotRun",             "Can never run",                             0 },

  TILT_TEST_SECTION("Hook function tests"),
    { FailInBeforeTestHookTest,      "FailBeforeHook",        "Will fail in the BeforeTest hook function", 0 },
    { FailInAfterTestHookTest,       "FailAfterHook",         "Will fail in the AfterTest hook function",  0 },
    { CannotRunInBeforeTestHookTest, "CannotRunInBeforeHook", "Reports cannot-run in BeforeTest",          0 },

  TILT_TEST_SECTION("Async support tests"),
    { SimpleAsyncTest,               "SimpleAsync",           "Uses asynchronous support",                 0 },

  TILT_TEST_SECTION("Test flag tests"),
    { DontRunInAutomationTest,       "DontRunInAutomation",   "Should not run in automation",              kTiltEngineFlag_NoAutomation },
};

/*_________________
  Test Executive */

static int TiltSampleTestImpl_Run(int argc, const char **argv) {
    /* Configure testing */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

/*__________________
  Library Binding */

static bool TiltSampleTestImpl_LibInit(void) {
    /* NOTE: Please do not create the object(s) under test here,
     *   rather one should use the BeforeAllTests hook, the BeforeTest hook and/or individual TestProcs for this purpose */
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void TiltSampleTestImpl_LibFini(void) {
    /* NOTE: Please do not destroy the object(s) under test here,
     *   rather one should use the AfterAllTests hook, the AfterTest hook and/or individual TestProcs for this purpose */
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(TiltSampleTest, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(TiltSampleTest, TiltSampleTestImpl_Run, 1536) LTLIBRARY_DEFINITION;

