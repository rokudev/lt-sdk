/*******************************************************************************
 * <tilt/TiltImpl.h>
 *    ______ ____ __   ______
 *   /_  __//  _// /  /_  __/       TILT: the Test
 *    / /   / / / /    / /                Infrastructure
 *   / /  _/ / / /___ / /                 for LT
 *  /_/  /___//_____//_/
 *
 *  Declares functions, used by test functions of TILT Unit Test LT Libraries,
 *  for accessing test lists, initializing TILT, and communicating results.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_SOURCE_TILT_IMPL_H
#define LT_SOURCE_TILT_IMPL_H

#include <lt/LT.h>
#include <tilt/Tilt.h>

/**
 * Reporting callbacks used by actual test procs, to communicate results
 * and other information to TestImpl, which assembles messages and passes them
 * along to the test initiator (typically TiltEngine) through a complementary
 * set of callbacks defined in Tilt.h.
 *
 */
typedef struct {
    void (* ReportFailure)(TiltResult type, const char * pFileName, u32 lineNumber,
                           const char * pMessage, ...);
        /**<
         * Report a test failure.
         *
         * @param[in] type Classification of the failure.
         * kTilt_Result_Failure is used to report a "plain old" test failure.
         * @param[in] pFileName The source file in which the call was made.
         * @param[in] lineNumber The line number on which the call was made.
         * @param[in] pMessage Optional message to report along with the failure.
         */

    void (* Message)(const char * pFileName, u32 lineNumber, TiltMessageLevel level,
                     const char * pMessage, ...);
        /**<
         * Post a message to the test output.
         *
         * @param[in] pFileName The source file in which the call was made.
         * @param[in] lineNumber The line number on which the call was made.
         * @param[in] level Message level, indicating the importance or severity
         * of the message.
         * @param[in] pMessage The message to report.
         * pMessage supports printf-style formatting of any remaining arguments.
         */

    void (* ReportInteger)(const char * pName, s64 value, const char * pUnits);
        /**<
         * Report an integer quantity.
         *
         * @param[in] pName The name of the quantity being reported.
         * @param[in] value The value of the quantity being reported.
         * @param[in] pUnits The units being used for the quantity.
         * (May be null.)
         */

    void (* ReportFloat)(const char * pName, double value, const char * pUnits);
        /**<
         * Report a floating-point quantity.
         *
         * @param[in] pName The name of the quantity being reported.
         * @param[in] value The value of the quantity being reported.
         * @param[in] pUnits The units being used for the quantity.
         * (May be null.)
         */

    void (* ReportBuffer)(const char * pName, const u8 *buffer, u32 length);
        /**<
         * Report a buffer quantity.
         *
         * @param[in] pName The name of the quantity being reported.
         * @param[in] buffer The buffer being reported.
         * @param[in] length The length of the buffer.
         * (May be null.)
         */

    const char * (* GetTestName)(void);
        /**<
         * Return the name of the currently-running test.
         */

    void (* DeferCompletion)(LTTime timeout, LTThread_ClientDataReleaseProc * cleanupProc, void * cleanupData);
        /**<
         * Indicate that the currently-running test or test hook will not be finished when
         * its proc returns.  The test must later call SignalCompletion() to indicate completion.
         *
         * This currently relies on the fact that it's called before SignalCompletion().
         * Thus, any calls that start something that calls SignalCompletion() must be
         * executed after a call to DeferCompletion().
         *
         * @param[in] timeout Timeout for this test.
         * If the test has not called SignalCompletion() within this period,
         * TILT will terminate the entire test run,
         * and therefore all remaining tests will be skipped.
         * @param[in] cleanupProc Optional cleanup proc to be called when completion is signalled
         * (or if timeout occurs).  Set to NULL if this functionality is not needed.
         * @c cleanupProc will be called as part of the SignalCompletion() sequence,
         * right before the AfterTest() hook (if any) is called.
         * @param[in] cleanupData Client data that will be passed to cleanupProc.
         * This cannot be NULL if cleanupProc is not NULL.
         */

    void (* SignalCompletion)(const char * pTestName);
        /**<
         * Indicate that an asynchronous test (one that previously called
         * DeferCompletion()) is now complete.
         *
         * The pTestName passed here must match the test name where DeferCompletion() was called;
         * otherwise, a "finishing out of sequence" error message will be displayed.
         *
         * @param[in] pTestName The name of the currently-running test
         * (provided for sanity checks).
         */

    const char * (* GetProperty)(const char * pPropertyName, const char * pDefaultValue);
        /**<
         * Get a test property.
         *
         * @param[in] pPropertyName Name of the test property.
         * @param[in] pDefaultValue Default value used if this property is not found.
         * @returns the value of the property, or pDefaultValue if the property is not found.
         */

    bool (* HookLibrary)(const char * pLibraryName, LTCore_LibraryHookFunction *pLibraryHookFunction);
        /**<
         * Enable LT library hook for the given library name.
         *
         * @param[in] pLibraryName The LT library name to intercept.
         * @param[in] pDefaultValue The unit test callback function to handle opening and closing the library.
         * @returns true if the hook was enabled, otherwise false.
         */
} TiltImplReportingCallbacks;

/**
 * Hook functions that are provided by test code, and called from the
 * TILT framework at various points.  All of these are optional; if
 * any hook function pointer is set to NULL, that function will simply
 * not be called.
 */
typedef struct {
    void (* BeforeAllTests)(const TiltImplReportingCallbacks * pTRC);
    /**<
     * This function is called before all tests run.
     * If it signals a failure, the entire test set will be skipped.
     *
     * @param[in] pTRC Standard callbacks, usable by this function
     * for reporting errors and messages.
     */

    void (* AfterAllTests)(const TiltImplReportingCallbacks * pTRC);
    /**<
     * This function is called after all tests have completed.
     * It will be called even if BeforeAllTests() signaled a failure.
     * Signalling a failure inside this function has no effect,
     * other than to produce an error message.
     *
     * @param[in] pTRC Standard callbacks, usable by this function
     * for reporting errors and messages.
     */

    void (* BeforeTest)(const TiltImplReportingCallbacks * pTRC);
    /**<
     * This function is called before every test.
     * If it signals a failure, that test will not run.
     *
     * @param[in] pTRC Standard callbacks, usable by this function
     * for reporting errors and messages.
     */

    void (* AfterTest)(const TiltImplReportingCallbacks * pTRC);
    /**<
     * This function is called after every test.
     *
     * @param[in] pTRC Standard callbacks, usable by this function
     * for reporting errors and messages.
     */
} TiltImplTestHooks;

/**
 * The signature for all unit test functions.
 *
 * @param[in] pTRC Functions used by the test to report failures, etc.
 */
typedef void (* UnitTestProc)(const TiltImplReportingCallbacks * pTRC);

/**
 * A single entry in the unit test table.
 * This table defines all available unit tests, and the default order
 * in which to run them.
 */
typedef struct {
    UnitTestProc testProc;          ///< The function that implements this test
    const char * pTestName;         ///< Test name (must be unique within this library)
    const char * pTestDescription;  ///< Test description (optional; may be null)
    TiltTestFlags testFlags;        ///< Metadata associated with this test
} TiltImplTestSpecifier;

/** Convenience macro for adding a section header entry to the test list */
#define TILT_TEST_SECTION(_title) { NULL, (_title), NULL, kTiltTestFlag_SectionHeader }


// Convenience macros for reporting failures, messages, and quantities

#define TILT_REPORT_FAILURE(_trc, ...) \
    ((_trc)->ReportFailure(kTilt_Result_Failure,   __FILE__, __LINE__, __VA_ARGS__))
#define TILT_REPORT_CANNOT_RUN(_trc, ...) \
    ((_trc)->ReportFailure(kTilt_Result_CannotRun, __FILE__, __LINE__, __VA_ARGS__))
#define TILT_REPORT_MESSAGE(_trc, ...) \
    ((_trc)->Message(NULL, 0, kTilt_Message_Level_Report, __VA_ARGS__))
#define TILT_MESSAGE(_trc, ...) \
    ((_trc)->Message(__FILE__, __LINE__, kTilt_Message_Level_Info, __VA_ARGS__))
#define TILT_WARNING(_trc, ...) \
    ((_trc)->Message(__FILE__, __LINE__, kTilt_Message_Level_Warning, __VA_ARGS__))
#define TILT_DEBUG(_trc, ...) \
    ((_trc)->Message(__FILE__, __LINE__, kTilt_Message_Level_Debug, __VA_ARGS__))

#define TILT_EXPECT_TRUE(_trc, _cond, ...) \
    do { if (!(_cond)) { TILT_REPORT_FAILURE((_trc), __VA_ARGS__); } } while (0)
#define TILT_ASSERT_TRUE(_trc, _cond, ...) \
    do { if (!(_cond)) { TILT_REPORT_FAILURE((_trc), __VA_ARGS__); return; } } while (0)

#define TILT_EXPECT_FALSE(_trc, _cond, ...) \
    TILT_EXPECT_TRUE((_trc), !(_cond), __VA_ARGS__)
#define TILT_ASSERT_FALSE(_trc, _cond, ...) \
    TILT_ASSERT_TRUE((_trc), !(_cond), __VA_ARGS__)

#define TILT_REPORT_INTEGER(_trc, _name, _value) \
    ((_trc)->ReportInteger((_name), (_value), NULL))
#define TILT_REPORT_INTEGER_WITH_UNITS(_trc, _name, _value, _units) \
    ((_trc)->ReportInteger((_name), (_value), (_units)))
#define TILT_REPORT_FLOAT(_trc, _name, _value) \
    ((_trc)->ReportFloat((_name), (_value), NULL))
#define TILT_REPORT_FLOAT_WITH_UNITS(_trc, _name, _value, _units) \
    ((_trc)->ReportFloat((_name), (_value), (_units)))
#define TILT_REPORT_BUFFER(_trc, _name, _buffer, _length) \
    ((_trc)->ReportBuffer((_name), (_buffer), (_length)))

#endif // #LT_SOURCE_TILT_IMPL_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  27-Jun-22   pertinax    created
 *  22-May-24   augustus    added TILT_REPORT_MESSAGE
*/
