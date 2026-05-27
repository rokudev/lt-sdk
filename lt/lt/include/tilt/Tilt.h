/******************************************************************************
 * <tilt/Tilt.h>
 *    ______ ____ __   ______
 *   /_  __//  _// /  /_  __/       TILT: the Test
 *    / /   / / / /    / /                Infrastructure
 *   / /  _/ / / /___ / /                 for LT
 *  /_/  /___//_____//_/
 *
 *  Interface definition, supporting values and other information for
 *  Unit Test LT Libraries made with TILT.
 *
 *  All top-level source files which implement Unit Test LT Libraries shall
 *  include this file.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef LT_INCLUDE_TILT_TILT_H
#define LT_INCLUDE_TILT_TILT_H

/**
 * @file Tilt.h
 *
 * TILT: Test Infrastructure for LT
 * This file defines the ITilt interface, which is a
 * minimal interface for discovering and running tests.
 */


#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

/* Reliably invalid test index */
enum { kInvalidTestIndex = 999999 };

/*--- Flags for the testFlags field in UnitTestSpecifier ---*/

typedef u8 TiltTestFlags;
enum  TiltTestFlags {
    kTiltTestFlag_NoAutomation  = 0x01,  ///< Do not run this test in automation.
    kTiltTestFlag_NeedsRtos     = 0x02,  ///< This test requires a real RTOS (for thread scheduling, etc.)
    kTiltTestFlag_SectionHeader = 0x04,  ///< This "test" is simply a header that's printed during execution.
};

/**
 * The description of one test in this library.
 */
typedef struct {
    const char *     pTestName;        ///< Test name (must be unique within this library)
    const char *     pTestDescription; ///< Test description (optional; may be null)
    TiltTestFlags    testFlags;        ///< Metadata associated with this test
} TiltTestSpecifier;

/**
 * Settings provided by each test library.
 */
typedef struct {
    u32 defaultTimeout;     ///< Default timeout for this library, in seconds.
} TiltTestLibrarySettings;

/**
 * Possible test results.
 *
 * We want to distinguish a "real test failure" from other types of failures
 * that can happen, because the former implies that the code under test is
 * incorrect, while the others imply that something is wrong with the test
 * framework or its client.
 */
typedef u8 TiltResult;
enum TiltResult {
    kTilt_Result_None,       ///< No result type specified
    kTilt_Result_Success,    ///< Test succeeded
    kTilt_Result_Failure,    ///< Test detected an error in code under test
    kTilt_Result_CannotRun,  ///< Test detected a configuration failure that prevents test from running
    kTilt_Result_Error,      ///< Test framework failed somehow
};

/**
 * Message levels.
 * Every Message() call contains a message level.
 */
typedef u8 TiltMessageLevel;
enum TiltMessageLevel {
    kTilt_Message_Level_Debug,      ///< Debug output; normally not shown
    kTilt_Message_Level_Info,       ///< Standard message, not shown in quiet mode
    kTilt_Message_Level_Warning,    ///< Warning message; will normally be shown
    kTilt_Message_Level_Report      ///< Report; will be shown in quiet mode
};

/**************************************************************************************
 * TILT Test Callbacks
 *
 * In Tilt, the initiator of a test (typically but not necessarily TiltEngine)
 * initializes TiltImpl, then instructs TiltImpl to run individual tests.
 *
 * The functions that actually perform the tests communicate results and other
 * information back to the initiator through TiltImpl; the test functions call
 * intermediary callbacks in TestImpl, which assemble the message and then pass
 * it to the initiator through the callbacks below.  The test functions are made
 * aware of the intermediary callbacks in TestImpl through a counterpart structure
 * defined in TestImpl.h.
 */


/**
 * Structure of pointers to the callbacks:
 */
typedef struct {
    void (* ReportFailure)(TiltResult type, const char * pMessage);
       /**<
         * Callback used by a test to signal a failure.
         *
         * @param[in] type Failure type; used to distinguish failure in the code under test
         * from other sorts of errors.
         * @param[in] pMessage Optional message providing more information about this error.
         */

    void (* Message)(TiltMessageLevel level, const char * pMessage);
       /**<
         * Callback used by a test to emit a message.
         *
         * @param[in] level Message level, which controls how messages are displayed
         * and in what circumstances they are shown at all.
         * @param[in] pMessage Message to be printed as part of verbose test output.
         */

    void (* ReportInteger)(const char * pName, s64 value, const char * pUnits);
       /**<
         * Callback used by a test to report an integer quantity.
         *
         * @param[in] pName Name of the quantity being reported.
         * @param[in] value Value of the quantity being reported.
         * @param[in] pUnits The units being used for the quantity.
         * (May be null.)
         */

    void (* ReportFloatingPoint)(const char * pName, double value, const char * pUnits);
       /**<
         * Callback used by a test to report a floating-point quantity.
         *
         * @param[in] pName Name of the quantity being reported.
         * @param[in] value Value of the quantity being reported.
         * @param[in] pUnits The units being used for the quantity.
         * (May be null.)
         */

    void (* ReportBuffer)(const char * pName, const u8 *buffer, u32 length);
       /**<
         * Callback used by a test to report a buffer quantity.
         *
         * @param[in] pName Name of the quantity being reported.
         * @param[in] buffer Value of the quantity being reported.
         * @param[in] length The length of buffer.
         * (May be null.)
         */

    void (* DeferCompletion)(LTTime timeout);
      /**<
       * Callback used by a test to indicate that it won't be done until
       * SignalCompletion() is called.
       *
       * @param[in] timeout Timeout for this test.
       * If the test has not called SignalCompletion() within this period,
       * TILT will terminate the entire test run.
       */

    void (* SignalCompletion)(const char * pTestName);
      /**<
       * Callback used by a test to indicate that it's finally done.
       * DeferCompletion() must have been called previously by the test.
       */

    const char * (* GetProperty)(const char * pPropertyName, const char * pDefaultValue);
      /**<
       * Callback used by a test to retrieve a test property.
       *
       * @param[in] pPropertyName Name of the test property.
       * @param[in] pDefaultValue Default value used if this property is not found.
       * @returns the value of the property, or pDefaultValue if the property is not found.
       */
} TiltCallbacks;

/******************************************************************************
 * Unit Test Library Interface
 *
 * This interface is implemented by all standard LT unit test libraries.
 * It provides a complete and minimal set of operations for running
 * test functions.  The TiltEngine library uses this interface to
 * provide a standardized method of executing unit tests and reporting
 * their results.
 */
typedef_LTLIBRARY_INTERFACE(ITilt, 1) {

    void (* GetLibrarySettings)(TiltTestLibrarySettings * pSettings);
      /**<
       * Return settings values for this test library.
       *
       * @param[out] pSettings Location for settings to be written.
       * Reasonable defaults will be provided for all settings,
       * so the library can overwrite only the settings it cares to.
       */

    bool (* GetTestList)(TiltTestSpecifier ** ppTestList, u32 * pTestCount);
       /**<
         * Return the list of tests in this test library.
         *
         * @param[out] ppTestList Pointer to the array of test specifiers.
         * This array will remain valid and unchanged while the library is loaded.
         * @param[out] pTestCount Number of elements in the @c pTestList array.
         * @returns false if @c pTestList or @c pTestCount are null.
         */

    bool (* BeginTesting)(const TiltCallbacks * pCallbacks);
       /**<
         * Prepare to run one or more tests.
         *
         * @param[in] pCallbacks Functions for use by test library.
         * This memory must remain valid and unchanged until EndTesting has returned.
         * @returns false if @c pCallbacks is null, or if BeginTesting has
         * already been called.
         */

    bool (* RunTest)(u32 nIndex);
       /**<
         * Run a single test.
         *
         * @param[in] nIndex The index of the test to run (relative to
         * @c ppTestList that was returned by GetTestList).
         * @returns false if <tt>index >= testCount</tt> (where @c testCount
         * was returned by GetTestList), or if BeginTesting has not been called.
         */

    bool (* EndTesting)(void);
       /**<
         * End this test run.
         * This method can be used by the test library to clean up any resources
         * allocated in BeginTesting.  After is has been called, BeginTesting
         * can be called to begin a new round of testing.
         *
         * @returns false if BeginTesting has not been called.
         */

} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif // LT_INCLUDE_TILT_TILT_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  27-Jun-22   pertinax    created
 *  22-May-24   augustus    added kTilt_Message_Level_Report
 */
