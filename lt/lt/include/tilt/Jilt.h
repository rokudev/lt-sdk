/*******************************************************************************
 * <tilt/Tilt.h>
 *    ______ ____ __   ______
 *   /_  __//  _// /  /_  __/       TILT 2.0: the Test
 *    / /   / / / /    / /                    Infrastructure
 *   / /  _/ / / /___ / /                     for LT
 *  /_/  /___//_____//_/
 *
 *  TILT Framework for the Unit Testing, Stress Testing and Performance Testing
 *     of LT Objects.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_TILT_TILT_H
#define LT_INCLUDE_TILT_TILT_H

/* TILT 2.0 Framework
 *
 *  The Tilt Framework is an infrastructure for the unit-testing, stress testing and
 *  performance testing of LT objects.
 *
 *  The Tilt framework can be used either directly or with a test executive. The
 *  TiltEngine is provided as a default executive (see TiltEngine.h). The TiltEngine
 *  facilitates looping through a series of user-defined unit tests. With the
 *  TiltEngine the user does not have to create their own thread as the engine takes
 *  care of that automatically.
 *
 *  @note The Tilt Framework and by extension TiltEngine are NOT intended for code
 *        examples. Code examples should be self-contained and live in their own
 *        libraries.
 *
 * Multi-threaded testing
 *  The Tilt framework generally supports multiple test threads; however, only one test
 *  case can run at a time. Therefore, it is recommended to sequence test cases from a
 *  single thread context. Using the TiltEngine helps ensure this requirement is met.
 *
 *  If the error "Only one client allowed at a time" occurs during testing, it indicates
 *  a violation of the Tilt API. This error may arise if two test benches are running
 *  simultaneously or if parallel test threads attempt to use the Tilt API while a master
 *  thread is sequencing test cases. To avoid this issue, test threads must remain
 *  quiescent while test case sequencing is in progress.
 */

#include <lt/core/LTCore.h>
LT_EXTERN_C_BEGIN

/** This adornment signifies that the specified API method can be invoked in parallel
 *  with other test threads using APIs marked with the same adornment. For more
 *  information see Multi-threaded testing (above). */
#define TILT_MULTIPROC_SAFE

/*____________________________________
 * Helper macros for Tilt TestProcs */

/* Report failures or if test cannot run */
#define TILT_REPORT_FAILURE(tilt, ...) \
    ((tilt)->API->ReportFailure(__FILE__, __LINE__, __VA_ARGS__))
#define TILT_REPORT_CANNOT_RUN(tilt, ...) \
    ((tilt)->API->ReportCannotRun(__FILE__, __LINE__, __VA_ARGS__))

/* Test Checking */
#define TILT_EXPECT_TRUE(tilt, cond, ...) \
    do { if (!(cond)) { TILT_REPORT_FAILURE((tilt), __VA_ARGS__); } } while (0)
#define TILT_EXPECT_FALSE(tilt, cond, ...) \
    TILT_EXPECT_TRUE((tilt), !(cond), __VA_ARGS__)

/* Test Asserts (NB: These immediately return from a TestProc upon failure!) */
#define TILT_ASSERT_TRUE(tilt, cond, ...) \
    do { if (!(cond)) { TILT_REPORT_FAILURE((tilt), __VA_ARGS__); return; } } while (0)
#define TILT_ASSERT_FALSE(tilt, cond, ...) \
    TILT_ASSERT_TRUE((tilt), !(cond), __VA_ARGS__)

/* Test Messages */
#define TILT_DEBUG(tilt, ...) \
    ((tilt)->API->Report(kTiltReportLevel_Debug, __VA_ARGS__))
#define TILT_INFO(tilt, ...) \
    ((tilt)->API->Report(kTiltReportLevel_Info, __VA_ARGS__))
#define TILT_WARNING(tilt, ...) \
    ((tilt)->API->Report(kTiltReportLevel_Warning, __VA_ARGS__))
#define TILT_REPORT(tilt, ...) \
    ((tilt)->API->Report(kTiltReportLevel_Report, __VA_ARGS__))

/* Test Value Reporting */
#define TILT_REPORT_INTEGER(tilt, name, value) \
    ((tilt)->API->ReportInteger((name), (value), NULL))
#define TILT_REPORT_INTEGER_WITH_UNITS(tilt, name, value, units) \
    ((tilt)->API->ReportInteger((name), (value), (units)))
#define TILT_REPORT_FLOAT(tilt, name, value) \
    ((tilt)->API->ReportFloat((name), (value), NULL))
#define TILT_REPORT_FLOAT_WITH_UNITS(tilt, name, value, units) \
    ((tilt)->API->ReportFloat((name), (value), (units)))
#define TILT_REPORT_BINARY(tilt, name, buffer, length) \
    ((tilt)->API->ReportBinary((name), (buffer), (length)))
#define TILT_REPORT_ELAPSED_TIME(tilt, name, time, iterations) \
    ((tilt)->API->ReportElapsedTime((name), (time), (iterations)))

/* Test Suite Abort -- aborts further testing (with message) */
#define TILT_ABORT_TESTING(tilt, ...) \
    ((tilt)->API->AbortTestSuite(__FILE__, __LINE__, __VA_ARGS__))

/*_____________________________________
 * Helper macros for Mocking Objects */

/* Object mocking/unmocking should be done before/after testing and
 * is NOT TILT_MULTIPROC_SAFE. */

/* Temporarily mock objects to new specializations */
#define tilt_mockobject(tilt, objectName, mockSpecialization) \
    ((tilt)->API->MockObject(#objectName, #objectName "Impl", #mockSpecialization))
#define tilt_mockobject_named(tilt, objectName, specialization, mockSpecialization) \
    ((tilt)->API->MockObject(#objectName, #specialization, #mockSpecialization))

/* Unmock objects (optional as test framework will revert all mocks when finished) */
#define tilt_unmockobject(tilt, objectName) \
    ((tilt)->API->UnmockObject(#objectName, #objectName "Impl"))
#define tilt_unmockobject_named(tilt, objectName, specialization) \
    ((tilt)->API->UnmockObject(#objectName, #specialization))

typedef struct Tilt Tilt;

/** Test Procedure */
typedef void (* TiltTestProc)(Tilt *tilt);

/** Report levels,
 * Every Report() call is given a report level. */
typedef_LTENUM_SIZED(TiltReportLevel, u8) {
    kTiltReportLevel_Debug,    /**< Debug output, normally not shown */
    kTiltReportLevel_Info,     /**< Standard message, not shown in quiet mode */
    kTiltReportLevel_Warning,  /**< Warning message, will normally be shown */
    kTiltReportLevel_Report,   /**< Report, will normally be shown */
};

/** Test Status */
typedef_LTENUM_SIZED(TiltTestStatus, u8) {
    kTiltTestStatus_Ok        = 0,  /**< Ongoing test (or test suite) hasn't failed yet */
    kTiltTestStatus_Failed    = 1,  /**< Test (or test suite) has failed */
    kTiltTestStatus_CannotRun = 2,  /**< Test cannot run */
    kTiltTestStatus_Skipped   = 3,  /**< Test has been skipped */
    kTiltTestStatus_Aborted   = 4,  /**< Test suite has been aborted */
    kTiltTestStatus_Rsvd5     = 5,  /**< Reserved for future use */
    kTiltTestStatus_Rsvd6     = 6,  /**< Reserved for future use */
    kTiltTestStatus_Rsvd7     = 7,  /**< Reserved for future use */
    /* Values 8 and beyond -> Reserved for External Use */
};

/** Tilt framework singleton object */
typedef_LTObject(Tilt, 1) {

    /****** Test Configuration ******/

    void (* SetTestSuiteName)(const char *testSuiteName);
    /**< Set the name of the test suite.
     *
     * @param[in] testSuiteName the name of the test suite. */

    const char * (* GetTestSuiteName)(void);
    /**< Get the name of the test suite.
     *
     * @return pointer to string with the name of the test suite. */

    void (* SetProperty)(const char *propertyName, const char *value);
    /**< Set a test property (text parameter).
     *
     * @param[in] propertyName Name of the test property.
     * @param[in] value value of property to set. */

    const char * (* GetProperty)(const char *propertyName, const char *defaultValue) TILT_MULTIPROC_SAFE;
    /**< Get a test property.
     *
     * @param[in] propertyName Name of the test property.
     * @param[in] defaultValue Default value used if this property is not found.
     * @returns the value of the property, or defaultValue if the property is not found. */

    void (* EnableReportLevel)(TiltReportLevel level);
    /**< Enable a Tilt report level.
     *
     * @param[in] level The level to enable. */

    void (* DisableReportLevel)(TiltReportLevel level);
    /**< Disable a Tilt report level.
     *
     * @param[in] level The level to disable. */

    void (* SetFailureLimit)(u32 failureLimit);
    /**< Set maximum number of reported failures.
     *
     * @param[in] failureLimit The maximum number of reported failures. */

    bool (* MockObject)(const char *objectName, const char *specialization, const char *mockSpecialiation);
    /**< Set a mock for an object with a temporary specialization for the purpose of testing.
     *
     * @param[in] objectName Name of object to mock.
     * @param[in] specialization Name of specialization to mock.
     * @param[in] mockSpecialization Set name of mock specialization to use in place of original specialization.
     *                               If NULL this will remove the mock specialization.
     * @return true if mock specialization was successfully installed, false on error. */

    bool (* UnmockObject)(const char *objectName, const char *specialization);
    /**< Explicitly revert an object to its default specialization.
     *
     * @note: UnmockObject() is usually not necessary since mocks are removed automatically after unit testing.
     *
     * @param[in] objectName Name of object to unmock.
     * @param[in] specialization Name of specialization to unmock.
     * @return true if mock specialization was successfully removed, false on error. */

    void (* EnableTimestamps)(bool enable);
    /**< Enable timestamping in Tilt Report APIs.
     *
     * @param[in] enable true for enable, and false for disable. Default is disable.  */

    /********* Test Control *********/

    void (* StartTest)(const char *testName);
    /**< Finish current test case (if any) and start new test case.
     *
     * @param[in] testName name of new test to start.  */

    void (* FinishTest)(void);
    /**< Indicate that current test case (or all tests) are complete. */

    void (* SkipTest)(const char *testName);
    /**< Indicate a skipped test case.
     *
     * @param[in] testName name of test that is being skipped.  */

    const char * (* GetTestName)(void) TILT_MULTIPROC_SAFE;
    /**< Get name of current test case.
     *
     * @return pointer to the name of the current test case. */

    TiltTestStatus (* GetTestStatus)(void);
    /**< Obtain status of the current test.
     *
     * @return the status of the ongoing (or finished) test. */

    void (* AbortTestSuite)(const char *file, u32 line, const char *fmt, ...) TILT_MULTIPROC_SAFE;
    /**< Abort current test suite with fatal error.
     *
     * @param[in] file the source file name where the abort occurred.
     * @param[in] line the source line number where the abort occurred.
     * @param[in] fmt the format string.
     * supports printf-style formatting of any remaining arguments. */

    TiltTestStatus (* GetOverallTestStatus)(void);
    /**< Obtain status for the test suite.
     *
     * @returns the current status of the test suite. */

    void (* ClearTestSuite)(void);
    /**< Remove mocks and free test records, test properties, stopwatches and PRNGs to prepare for
         next test or exit. */

    void (* SetClientData)(void *clientData);
    /**< Set pointer to opaque client data for use by tests.
     *
     * @param[in] clientData pointer to client data. */

    void * (* GetClientData)(void);
    /**< Obtain pointer to opaque client data for use by tests.
     *
     * @returns pointer to client data. */

    /**** Pseudo-random test data ***/

    u32 (* PrngCreate)(u64 streamVector, u64 seedPosition) TILT_MULTIPROC_SAFE;
    /**< Create a Pseudo Random Number Generator with the chosen stream and starting seed position.
     *  @note Uses PCG as detailed in https://www.pcg-random.org/pdf/hmc-cs-2014-0905.pdf.
     *
     *  There are 2^64 different 'stream vectors' or unique sequences of uniformly distributed pseudo
     *  random numbers. The seedPosition is the starting point along that streamVector. Sequences of
     *  PRNG numbers can be re-created by specifying the same streamVector and seedPosition from run to
     *  run. The stream vector can be any 64-bit number, and may be thought of as an additional 64 bits
     *  of seed when not used to reproduce a specific sequence of uniformly distributed pseudo random
     *  numbers.
     *
     *  @param[in] streamVector The chosen unique PRNG sequence.
     *  @param[in] seedPosition The chosen starting point along the stream.
     *  @returns a new PRNG ID. */

    void (* PrngGenerateRandomBytes)(u32 prngID, u8 *bytesToFill, u32 numBytes) TILT_MULTIPROC_SAFE;
    /**< Obtain random bytes from the given PRNG.
     *
     *  @param[in] prngID the PRNG ID.
     *  @param[in] bytesToFill buffer to fill with PRNG data.
     *  @param[in] numBytes size of buffer to fill. */

    u32 (* PrngGenerateRandomNumber)(u32 prngID, u32 maxVal) TILT_MULTIPROC_SAFE;
    /**< Generate a uniformly distributed PRN between 0 and maxVal, inclusive.
     *
     *  @param[in] prngID the PRNG ID.
     *  @param[in] maxVal sets the upper-limit for the generated value.
     *  @returns the pseudo random number in the requested range. */

    /*** Stopwatch (Test Timing) ****/

    u32 (* StopwatchCreate)(void) TILT_MULTIPROC_SAFE;
    /**< Create a new stopwatch for timing execution.
     *
     *  @returns the new stopwatch ID.  */

    void (* StopwatchStart)(u32 stopwatchID) TILT_MULTIPROC_SAFE;
    /**< Start (or resume) elapsed time accumulation.
     *
     * @param[in] stopwatchID ID of the stopwatch to start */

    LTTime (* StopwatchStop)(u32 stopwatchID) TILT_MULTIPROC_SAFE;
    /**< Stop elapsed time accumulation and return elapsed time
     *  NOTE: Must only be used AFTER starting stopwatch.
     *
     * @param[in] stopwatchID ID of the stopwatch to stop.
     * @returns elapsed time. */

    LTTime (* StopwatchRead)(u32 stopwatchID) TILT_MULTIPROC_SAFE;
    /**< Get elapsed time without stopping stopwatch (aka lap time).
     *  NOTE: Must only be called AFTER starting stopwatch.
     *
     * @param[in] stopwatchID ID of the stopwatch to retrieve elapsed time from.
     * @returns elapsed time. */

    void (* StopwatchReset)(u32 stopwatchID) TILT_MULTIPROC_SAFE;
    /**< Restart elapsed time to zero and stop.
     *
     * @param[in] stopwatchID ID of the stopwatch to stop. */

    /******** Test Reporting ********/

    void (* ReportTestSuiteStarted)(void);
    /**< Report the start of the test suite */

    void (* ReportTestSuiteSummary)(void);
    /**< Generate the final test report for the test suite. */

    void (* ReportTestStarted)(void);
    /**< Report the start of a test case. */

    void (* ReportTestResult)(void);
    /**< Report the test case result. */

    void (* ReportTestBanner)(const char *text);
    /**< Report a single-line test banner (test separator)
     *
     * @param[in] text Report this text string on the test banner. */

    /******** Test Reporting (Multi-Proc Safe) ********/

    void (* Report)(TiltReportLevel level, const char *fmt, ...) TILT_MULTIPROC_SAFE;
    /**< Report a formatted text string.
     *
     * @param[in] level Report level, indicating the type, importance and/or severity.
     * @param[in] fmt the format string.
     * supports printf-style formatting of any remaining arguments. */

    void (* ReportFailure)(const char *file, u32 line, const char *fmt, ...) TILT_MULTIPROC_SAFE;
    /**< Indicate a failure in current test */

    void (* ReportCannotRun)(const char *file, u32 line, const char *fmt, ...) TILT_MULTIPROC_SAFE;
    /**< Indicate an abort of current test */

    void (* ReportInteger)(const char *name, s64 value, const char *units) TILT_MULTIPROC_SAFE;
    /**< Report an integer quantity.
     *
     * @param[in] name The name of the quantity being reported.
     * @param[in] value The value of the quantity being reported.
     * @param[in] units The units being used for the quantity, ignored if null. */

    void (* ReportFloat)(const char *name, double value, const char *units) TILT_MULTIPROC_SAFE;
    /**< Report a floating-point quantity.
     *
     * @param[in] name The name of the quantity being reported.
     * @param[in] value The value of the quantity being reported.
     * @param[in] units The units being used for the quantity, ignored if null. */

    void (* ReportBinary)(const char *name, const u8 *data, u32 sizeInBytes) TILT_MULTIPROC_SAFE;
    /**< Report a binary quantity.
     *
     * @param[in] name The name of the quantity being reported.
     * @param[in] buffer The binary data being reported.
     * @param[in] sizeInBytes The length of the data. */

    void (* ReportElapsedTime)(const char *name, LTTime time, u32 numIterations) TILT_MULTIPROC_SAFE;
    /**< Report time duration (across a number of optional iterations).
     * @note If numIterations is greater than zero, then the mean iteration time
     *       will be reported (i.e.: time will be divided by numIterations).
     *
     * @param[in] name The name of the quantity being reported.
     * @param[in] time The value of time being reported.
     * @param[in] numIterations The number of iterations to report, ignored if zero. */

} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* LT_INCLUDE_TILT_TILT_H */

