/******************************************************************************
 * <tilt/BitsEngine.h>
 *    ____ ___ __^__ ____
 *   < __ )_ _|_   _/ ___>             BITS: Better
 *   |  _ \| |  | | \___ \                   Integrity
 *   < |_) | |  | |  ___) >                  Through Stress
 *   |____/_ _| |_| |____/
 *          v
 *  The BitsEngine is the default test executive for LT Stress Testing.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_TILT_BITSENGINE_H
#define LT_INCLUDE_TILT_BITSENGINE_H

/*
 * The BitsEngine is a test executive built on the TILT Framework, designed
 * specifically for stress testing. It enables the creation of multiple stressor
 * threads alongside a main thread that coordinates the testing process. Testing
 * progresses through a series of synchronization points, where all test threads
 * align to before advancing to the next stage. The process operates as follows:
 *   (where MT = Main Thread and TT = Test Thread)
 *
 * Starting at syncPoint 0:
 *  MT:     Invoke the BeforeAllTests() hook, then
 *  MT:     Invoke the Synchronization Hook for syncPoint 0; then, in parallel:
 *  TT 0:   Run the testProc for test thread 0   (for syncPoint 0)
 *  TT 1:   Run the testProc for test thread 1   (for syncPoint 0)
 *              ...
 *  TT N-1: Run the testProc for test thread N-1 (for syncPoint 0)
 *
 * After all test threads synchronize to syncPoint 1 (via calls to Synchronize()),
 *  MT:     Invoke the Synchronization Hook for syncPoint 1; then, in parallel:
 *  TT 0:   Run the testProc for test thread 0   (for syncPoint 1)
 *  TT 1:   Run the testProc for test thread 1   (for syncPoint 1)
 *
 *   ... Continue through the remaining synchronization points as above ...
 *
 *  MT: Once all test threads have synchronized to the final synchronization point,
 *      the main thread invokes FinishTesting() within its Synchronization Hook.
 *  MT: Afterward, the Synchronization Hook returns, the AfterAllTests() hook
 *      is executed, and the test bench terminates.
 */

#include <lt/core/LTThread.h>
#include <lt/core/LTArray.h>
#include <tilt/Jilt.h>
LT_EXTERN_C_BEGIN

enum {
    /* Values 0 through 7 reserved by Tilt Framework (See Tilt.h) */
    kBitsEngineStatus_BadOptions = 8    /**< Bad command-line options */
};

typedef struct BitsEngine_TestThreadDesc BitsEngine_TestThreadDesc;

typedef void (BitsEngine_TestProc)(Tilt *engine, BitsEngine_TestThreadDesc *desc, u32 syncPoint);
/**< Thread test proc.
 *
 * @param[in] engine pointer to Tilt test framework.
 * @param[in] desc pointer to test thread descriptor (including private client data).
 * @param[in] syncPoint run tests for this synchronization point.  */

/** BitsEngine test thread descriptor */
typedef struct BitsEngine_TestThreadDesc {
    LTOThread           *thread;       /**< Test thread object */
    LTThread_InitProc   *initProc;     /**< Test thread Init proc */
    LTThread_ExitProc   *exitProc;     /**< Test thread Exit proc */
    BitsEngine_TestProc *testProc;     /**< BitsEngine Test thread proc */
    void                *clientData;   /**< Set private data for the thread */
    const u32            threadNum;    /**< Test thread number [0, N-1] */
    const u32            syncPoint;    /**< Current synchronization point for thread */
} BitsEngine_TestThreadDesc;

/** Hook functions that are provided by test code, and called from the
 *  BitsEngine main thread at various points. All of these are optional; if any
 *  hook function pointer is set to NULL, that function will simply not be called. */
typedef struct {

    void (* BeforeAllTests)(Tilt *tilt);
    /**< This function is invoked in the main thread before the test suite is run.
     * If it signals a failure via TILT reporting methods, the entire test suite
     * will be skipped.
     *
     * @param[in] tilt pointer to the test framework. */

    void (* AfterAllTests)(Tilt *tilt);
    /**< This function is invoked after all tests have completed in the main thread.
     * It will be called even if BeforeAllTests() signalled a failure.
     * @note Signalling a failure inside this function has no effect,
     *       other than to produce an error message.
     *
     * @param[in] tilt pointer to the test framework. */

    void (* Synchronization)(Tilt *tilt, u32 syncPoint);
    /**< This function is invoked in the main thread before each synchronization point.
     * If it signals a failure via TILT reporting methods, the test will not run.
     *
     * @param[in] tilt pointer to the test framework.
     * @param[in] syncPoint the new synchronization point to run to. */

} BitsEngineTestHooks;

/** BitsEngine test executive object */
typedef_LTObject(BitsEngine, 1) {

    /******* Test executive control *******/

    int         (* RunTestSuite)(BitsEngine *engine, int argc, const char **argv,
                                    const BitsEngineTestHooks *hooks);
    /**< Start main test thread and run stress tests.
     *
     * @param[in,out] engine The test engine instance.
     * @param[in]     argc Command-line option count.
     * @param[in]     argv Command-line option vector.
     * @param[in]     hooks Pointer to test hook structure containing the procs to
     *                      be executed in the main thread.
     * @return        test status. */

    void         (* SetTestSuiteName)(BitsEngine *engine, const char *testSuiteName);
    /**< Override the default test suite name.
     * @note The default test suite name is the first argument passed to RunTestSuite().
     *
     * @param[in,out] engine The test engine instance.
     * @param[in]     testSuiteName test suite name. */

    void         (* SetTestSuiteTimeout)(BitsEngine *engine, LTTime timeout);
    /**< Set the overall test suite timeout.
     * @note The default test suite timeout for stress testing is infinite.
     *
     * @param[in,out] engine The test engine instance.
     * @param[in]     timeout test suite timeout. */

    void         (* SetMainThreadStackSize)(BitsEngine *engine, const u32 stackSize);
    /**< Set main thread stack size (overrides the default).
     *
     * @param[in,out] engine The test engine instance.
     * @param[in]     stackSize main thread stack size in bytes. */

    Tilt *       (* GetTestFramework)(BitsEngine *engine);
    /**< Get pointer to the test framework.
     *
     * @param[in,out] engine The test engine instance.
     * @return        pointer to the Tilt Framework object. */

    /******* Test thread management *******/

    LTArray *    (* CreateTestThreads)(BitsEngine *engine, u32 numThreads, u32 stackSize);
    /**< Create test threads.
     * @note Only one set of test threads can be active at once. If DestroyTestThreads() is
     *       called then CreateTestThreads() can be invoked again.
     *
     * @param[in,out] engine The test engine instance.
     * @param[in]     numThreads Number of test threads to create.
     * @param[in]     stackSize Stack size of test threads in bytes.
     * @return        array of test thread descriptors (BitsEngineTestThreadDesc).  */

    void         (* StartTestThreads)(BitsEngine *engine);
    /**< Start the test threads.
     *
     * @param[in,out] engine The test engine instance.  */

    void         (* DestroyTestThreads)(BitsEngine *engine);
    /**< Terminate test threads.
     *
     * @param[in,out] engine The test engine instance.  */

    void         (* Synchronize)(BitsEngine *engine, u32 nextSyncPoint);
    /**< Announce that thread is finished up to a given synchronization point
     *
     * @param[in,out] engine The test engine instance.
     * @param[in]     nextSyncPoint the next synchronization point the test thread is ready to advance to.  */
 
    void         (* SetSynchronizationTimeout)(BitsEngine *engine, LTTime timeout);
    /**< Set the synchronization timeout.
     * @note If this is not called, the default timeout is infinite.
     * @note This timeout can be changed between synchronization points.
     *
     * @param[in,out] engine The test engine instance.
     * @param[in]     timeout synchronization point timeout. */

    void         (* FinishTesting)(BitsEngine *engine);
    /**< Indicate testing is finished.
     * The main test thread invokes this at the final synchronization point to terminate the test.
     *
     * @param[in,out] engine The test engine instance. */

} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* LT_INCLUDE_TILT_BITSENGINE_H */

