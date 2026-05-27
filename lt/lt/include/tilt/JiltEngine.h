/*******************************************************************************
 * <tilt/TiltEngine.h>
 *    ______ ____ __   ______
 *   /_  __//  _// /  /_  __/       TILT 2.0 : the Test
 *    / /   / / / /    / /                     Infrastructure
 *   / /  _/ / / /___ / /                      for LT
 *  /_/  /___//_____//_/
 *
 *  The TiltEngine is the default test executive for LT Unit Testing.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_TILT_TILTENGINE_H
#define LT_INCLUDE_TILT_TILTENGINE_H

/* TiltEngine Test Executive
 *   The Default test executive for the TILT Framework, intended for unit testing. */

#include <tilt/Jilt.h>
LT_EXTERN_C_BEGIN

/* Test Section title (use this as row of TiltEngineTest table) */
#define TILT_TEST_SECTION(title) { NULL, (title), NULL, 0 }

enum {
    /* Values 0 through 7 reserved by Tilt Framework (See Tilt.h) */
    kTiltEngineStatus_BadOptions = 8    /**< Bad command-line options */
};

/** TiltEngine test flags */
typedef u8 TiltEngineFlags;
enum TiltEngineFlags {
    kTiltEngineFlag_NoAutomation  = 0x01,  /**< Do not run this test in automation. */
};

/** Callback for deferred test timeout */
typedef void (TiltEngineCleanupProc)(Tilt *tilt, void *clientData);

/** A single entry in the test table. The test table defines a suite of tests and the default
 *  order in which to run them.  */
typedef struct {
    TiltTestProc      testProc;     /**< The function that implements this test */
    const char       *name;         /**< Test name (must be unique within this instance) */
    const char       *description;  /**< Test description (optional; may be null) */
    TiltEngineFlags   flags;        /**< Metadata associated with this test */
} TiltEngineTest;

/** Hook functions that are provided by test code, and called from the
 * TiltEngine at various points. All of these are optional; if any hook
 * function pointer is set to NULL, that function will simply not be called. */
typedef struct {

    void (* BeforeAllTests)(Tilt *tilt);
    /**< This function is invoked before the test suite is run.
     * If it signals a failure, the entire test suite will be skipped.
     *
     * @param[in] tilt pointer to test framework. */

    void (* AfterAllTests)(Tilt *tilt);
    /**< This function is invoked after all tests have completed.
     * It will be called even if BeforeAllTests() signalled a failure.
     * @note Signalling a failure inside this function has no effect,
     *       other than to produce an error message.
     *
     * @param[in] tilt pointer to test framework. */

    void (* BeforeTest)(Tilt *tilt);
    /**< This function is invoked before every test.
     * If it signals a failure, the test will not run.
     *
     * @param[in] tilt pointer to test framework. */

    void (* AfterTest)(Tilt *tilt);
    /**< This function is invoked after every test.
     * It will be called even if BeforeTest() signalled a failure.
     *
     * @param[in] tilt pointer to test framework. */

} TiltEngineTestHooks;

/** TiltEngine test executive object */
typedef_LTObject(JiltEngine, 1) {

    /******* Test executive control *******/

    void (* ConfigureTestSuite)(JiltEngine *engine, const TiltEngineTest testSuite[],
                                   u32 testCount, const TiltEngineTestHooks *hooks);
    /**< Configure tests. Can be invoked again as needed to configure new test suites.

     * @param[in,out] engine The test engine instance.
     * @param[in]     testSuite The list of tests comprising the test suite.
     * @param[in]     testCount Number of tests in the list.
     * @param[in]     hooks Pointer to the test hooks. */

    int (* RunTestSuite)(JiltEngine *engine, int argc, const char **argv);
    /**< Run tests. Can be invoked again as needed for subsequent test suites.
     *
     * @param[in,out] engine The test engine instance.
     * @param[in]     argc Command-line option count.
     * @param[in]     argv Command-line option vector.
     * @return        test status. */

    void (* SetTestSuiteName)(JiltEngine *engine, const char *testSuiteName);
    /**< Override the default test suite name.
     * @note The default test suite name is the first argument passed to RunTestSuite().
     *
     * @param[in,out] engine The test engine instance.
     * @param[in]     testSuiteName test suite name. */

    void (* SetTestSuiteTimeout)(JiltEngine *engine, LTTime timeout);
    /**< Set the overall test suite timeout (overrides the default).
     *
     * @param[in,out] engine The test engine instance.
     * @param[in]     timeout test suite timeout. */

    void (* SetTestThreadStackSize)(JiltEngine *engine, const u32 stackSize);
    /**< Set test thread stack size (overrides the default).
     *
     * @param[in,out] engine The test engine instance.
     * @param[in]     stackSize test thread stack size in bytes. */

    Tilt * (* GetTestFramework)(JiltEngine *engine);
    /**< Get pointer to the test framework.
     *
     * @return pointer to the Tilt Framework object. */

    /******* Asynchronous test support *******/

    void (* DeferTestCompletion)(JiltEngine *engine, LTTime timeout, TiltEngineCleanupProc *cleanupProc, void *clientData);
    /**< Indicate that the currently-running test or test hook will not be finished when its proc returns.
     *   The test must later call SignalTestCompletion() to indicate completion.
     *
     * @note This function must be called before the corresponding call to SignalTestCompletion().
     *       Only one test may be in the deferred state at any one time.
     *
     * @param[in,out] engine The test engine instance.
     * @param[in]     timeout Timeout for this test. If the test has not called SignalTestCompletion()
     *                within this period, the entire test run will be terminated, and the remaining
     *                tests will be skipped.
     * @param[in]     cleanupProc Optional cleanup proc to be called when completion is signalled
     *                (or if timeout occurs). Set to NULL if this functionality is not needed.
     *                @c cleanupProc will be called as part of the SignalTestCompletion() sequence,
     *                right before the AfterTest() hook (if any) is called.
     * @param[in]     clientData Client data that will be passed to the optional cleanupProc.  */

    void (* SignalTestCompletion)(JiltEngine *engine, const char *testName);
    /**< Signal that test has completed
     *
     * @note This function must be called before the corresponding call to SignalTestCompletion().
     *       Only one test may be in the deferred state at any one time.
     *
     * @param[in,out] engine The test engine instance.
     * @param[in]     testName Name of test signalling completion, use NULL outside of testing. */

} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* LT_INCLUDE_TILT_TILTENGINE_H */

