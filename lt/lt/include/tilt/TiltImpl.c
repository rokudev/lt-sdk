/*******************************************************************************
 * <tilt/TiltImpl.c>
 *    ______ ____ __   ______
 *   /_  __//  _// /  /_  __/       TILT: the Test
 *    / /   / / / /    / /                Infrastructure
 *   / /  _/ / / /___ / /                 for LT
 *  /_/  /___//_____//_/
 *
 *   Interface implementation which, accompanied by functions to exercise the
 *   LT Library under test, constitutes a Unit Test LT Library.
 *
 *  This file is meant to be #included into exactly one source file per
 *  test library.  Notice that all functions are static, to avoid multiple
 *  definition problems on platforms that assemble LT libraries as static
 *  (.a) libraries.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTArray.h>
#include <tilt/TiltImpl.h>

DEFINE_LTLOG_SECTION("lt.test.framework");

/** Name of thread-specific client data used to support cleanupProc in DeferCompletion */
static const char kDeferredCompletionVar[] = "TiltDC";

static TiltCallbacks const         * s_pCallbacks           = NULL;
static const TiltImplTestSpecifier * s_pInternalTestList    = NULL;
static TiltTestSpecifier           * s_pTestList            = NULL;
static u32                           s_testCount            = 0;
static u32                           s_runningTestIndex     = kInvalidTestIndex;
static TiltImplTestHooks           * s_pTestHooks           = NULL;
static TiltResult                    s_currentResult        = kTilt_Result_None;
static bool                          s_bDeferCompletion     = false;

/* Working strings (for printf formatting) on the heap */
enum { kWorkStringSize = 256 };
static char                        * s_workString1       = NULL;
static char                        * s_workString2       = NULL;

/* Current execution state */
typedef enum {
    kTiltImplExecutionState_None,
    kTiltImplExecutionState_InBeforeAllTests,
    kTiltImplExecutionState_InBeforeTest,
    kTiltImplExecutionState_RunningTest,
    kTiltImplExecutionState_InAfterTest,
    kTiltImplExecutionState_InAfterAllTests,
} TiltImplExecutionState;
static TiltImplExecutionState        s_executionState  = kTiltImplExecutionState_None;

static ILTThread                   * s_pIThread        = NULL;  ///< LTThread library interface
static LTThread                      s_deferralThread  = 0;     ///< Thread on which DeferCompletion() was last called

typedef struct {
    const char * pLibraryName;
    LTCore_LibraryHookFunction *pLibraryHookFunction;
} TiltHookedLibrary;

typedef struct {
    TiltHookedLibrary *pHookEntry;
    LTLibrary *originalLib;
    LTLibrary *hookedLib;
} TiltHookedLibInstance;

static LTArray *s_pTiltHookedLibraries = NULL;
static LTArray *s_pTiltHookedLibraryInstances = NULL;

/* Forward declarations for callback functions */
static void TiltReportFailure(TiltResult type, const char * pFileName, u32 lineNumber,
                              const char * pMessage, ...);
static void TiltMessage(const char * pFileName, u32 lineNumber, TiltMessageLevel level,
                        const char * pMessage, ...);
static void TiltReportInteger(const char * pName, s64 value, const char * pUnits);
static void TiltReportFloat(const char * pName, double value, const char * pUnits);
static void TiltReportBuffer(const char * pName, const u8 *buffer, u32 length);
static const char * TiltGetTestName(void);
static void TiltDeferCompletion(LTTime timeout, LTThread_ClientDataReleaseProc * cleanupProc, void * cleanupData);
static void TiltSignalCompletion(const char * pTestName);
static const char * TiltGetProperty(const char * pPropertyName, const char * pDefaultValue);
static bool TiltHookLibrary(const char * pLibraryName, LTCore_LibraryHookFunction *pLibraryHookFunction);

static TiltImplReportingCallbacks const s_reportingCallbacks = {
    .ReportFailure       = TiltReportFailure,
    .Message             = TiltMessage,
    .ReportInteger       = TiltReportInteger,
    .ReportFloat         = TiltReportFloat,
    .ReportBuffer        = TiltReportBuffer,
    .GetTestName         = TiltGetTestName,
    .DeferCompletion     = TiltDeferCompletion,
    .SignalCompletion    = TiltSignalCompletion,
    .GetProperty         = TiltGetProperty,
    .HookLibrary         = TiltHookLibrary,
};

/**
 * Cheesy implementation of basename()
 */
static const char * basename(const char * pPath)
{
    const char * lastSlash = lt_strrchr(pPath, '/');
    if (lastSlash != NULL) {
        return lastSlash + 1;
    }

    return pPath;
}

/**
 * Default implementation of ITilt::GetLibrarySettings.
 *
 * A test library can provide its own default timeout by #defining
 * TILT_LIBRARY_TIMEOUT to an integer value before including this file.
 */
static void GetLibrarySettings(TiltTestLibrarySettings * pSettings)
{
#ifdef TILT_LIBRARY_TIMEOUT
    pSettings->defaultTimeout = TILT_LIBRARY_TIMEOUT;
#else
    LT_UNUSED(pSettings);
#endif
}

/**
 * Default implementation of ITilt::GetTestList.
 */
static bool GetTestList(TiltTestSpecifier ** ppTestList, u32 * pTestCount)
{
    if (ppTestList == NULL || pTestCount == NULL) {
        LTLOG_YELLOWALERT("ltut.gtlargs", "Illegal null argument to GetTestList()");
        return false;
    }
    if (s_pTestList == NULL) {
        LTLOG_YELLOWALERT("ltut.gtlstate", "InitializeTilt() has not been called");
        return false;
    }

    *ppTestList = s_pTestList;
    *pTestCount = s_testCount;

    return true;
}

/**
 * Default implementation of ITilt::BeginTesting.
 */
static bool BeginTesting(const TiltCallbacks * pCallbacks)
{
    if (s_pTestList == NULL) {
        LTLOG_YELLOWALERT("ltut.btstate", "InitializeTilt() has not been called");
        return false;
    }
    if (s_pCallbacks != NULL) {
        s_pCallbacks->ReportFailure(kTilt_Result_Error, "BeginTesting() has already been called");
        return false;
    }
    if (pCallbacks == NULL) {
        LTLOG_YELLOWALERT("ltut.btargs", "Illegal null argument to BeginTesting()");
        return false;
    }
    if (pCallbacks->Message == NULL         || pCallbacks->ReportFailure == NULL ||
        pCallbacks->ReportInteger == NULL   || pCallbacks->ReportFloatingPoint == NULL ||
        pCallbacks->ReportBuffer == NULL    ||
        pCallbacks->DeferCompletion == NULL || pCallbacks->SignalCompletion == NULL) {
        LTLOG_YELLOWALERT("ltut.btcbargs", "Illegal null callback to BeginTesting()");
        return false;
    }

    s_pCallbacks = pCallbacks;

    // Run the BeforeAllTests hook function, if it exists
    s_currentResult = kTilt_Result_None;
    if (s_pTestHooks != NULL && s_pTestHooks->BeforeAllTests != NULL) {
        s_executionState = kTiltImplExecutionState_InBeforeAllTests;
        s_pTestHooks->BeforeAllTests(&s_reportingCallbacks);
        if (!s_bDeferCompletion) {
            s_executionState = kTiltImplExecutionState_None;
        }
    }

    if (s_currentResult != kTilt_Result_None) {
        // BeforeAllTests() signalled an error; abort the test run
        return false;
    }

    return true;
}

/**
 * Perform all necessary processing when a test ends.
 */
static void CompleteTest(void)
{
    // Run the AfterTest hook function, if it exists

    if (s_pTestHooks != NULL && s_pTestHooks->AfterTest != NULL) {
        s_executionState = kTiltImplExecutionState_InAfterTest;
        s_pTestHooks->AfterTest(&s_reportingCallbacks);
        if (!s_bDeferCompletion) {
            s_executionState = kTiltImplExecutionState_None;
        }
    }

    s_runningTestIndex = kInvalidTestIndex;
}

/**
 * Default implementation of ITilt::RunTest.
 */
static bool RunTest(u32 nIndex)
{
    UnitTestProc testProc = NULL;

    if (s_pCallbacks == NULL) {
         LTLOG_YELLOWALERT("ltut.rtstate", "BeginTesting() has not been called before RunTest()");
        return false;
    }
    if (nIndex >= s_testCount) {
        s_pCallbacks->ReportFailure(kTilt_Result_Error, "Test index is out of range");
        return false;
    }
    testProc = s_pInternalTestList[nIndex].testProc;
    if (testProc == NULL) {
        s_pCallbacks->ReportFailure(kTilt_Result_Error, "Test function is null");
        return false;
    }

    s_runningTestIndex = nIndex;
    s_currentResult = kTilt_Result_None;
    s_bDeferCompletion = false;

    // Run the BeforeTest hook function, if it exists

    if (s_pTestHooks != NULL && s_pTestHooks->BeforeTest != NULL) {
        s_pTestHooks->BeforeTest(&s_reportingCallbacks);
    }

    if (s_currentResult != kTilt_Result_None) {
        // BeforeTest() signalled an error; don't run the test
    } else {
        // Run test normally
        s_executionState = kTiltImplExecutionState_RunningTest;
        testProc(&s_reportingCallbacks);
    }

    if (!s_bDeferCompletion) {
        CompleteTest();
    }

    return true;
}

/**
 * Default implementation of ITilt::EndTesting.
 */
static bool EndTesting(void)
{
    if (s_pCallbacks == NULL) {
         LTLOG_YELLOWALERT("ltut.etstate", "BeginTesting() has not been called before EndTesting()");
        return false;
    }

    // Run the AfterAllTests hook function, if it exists

    if (s_pTestHooks != NULL && s_pTestHooks->AfterAllTests != NULL) {
        s_executionState = kTiltImplExecutionState_InAfterAllTests;
        s_pTestHooks->AfterAllTests(&s_reportingCallbacks);
        if (!s_bDeferCompletion) {
            s_pCallbacks = NULL;
            s_executionState = kTiltImplExecutionState_None;
        }
    } else {
        s_pCallbacks = NULL;
    }

    return true;
}


/*******************************************************************************
 * Standard implementations of TILT callback functions.
 *******************************************************************************/

/**
 * Standard implementation of TiltImplReportingCallbacks.ReportFailure.
 * Do not call directly.
 */
static void TiltReportFailure(TiltResult type, const char * pFileName, u32 lineNumber,
                              const char * pMessage, ...)
{
    if (s_pCallbacks == NULL) {
         LTLOG_YELLOWALERT("ltut.rfstate", "TiltReportFailure() has no callbacks");
         return;
    }

    if (pMessage != NULL) {
        lt_va_list args;
        lt_va_start(args, pMessage);
        lt_vsnprintf(s_workString1, kWorkStringSize, pMessage, args);
        lt_va_end(args);
    } else {
        lt_strncpyTerm(s_workString1, "(no message)", kWorkStringSize);
    }

    lt_snprintf(s_workString2, kWorkStringSize, "%s, line %d: %s",
                basename(pFileName), lineNumber, s_workString1);
    s_pCallbacks->ReportFailure(type, s_workString2);
    s_currentResult = type;
}

/**
 * Standard implementation of TiltImplReportingCallbacks.Message.
 * Do not call directly.
 */
static void TiltMessage(const char * pFileName, u32 lineNumber, TiltMessageLevel level,
                        const char * pMessage, ...)
{
    if (s_pCallbacks == NULL) {
         LTLOG_YELLOWALERT("ltut.msgstate", "TiltMessage() has no callbacks");
         return;
    }

    if (pMessage != NULL) {
        lt_va_list args;
        lt_va_start(args, pMessage);
        lt_vsnprintf(s_workString1, kWorkStringSize, pMessage, args);
        lt_va_end(args);
    } else {
        lt_strncpyTerm(s_workString1, "(no message)", kWorkStringSize);
    }

    if (pFileName) {
        lt_snprintf(s_workString2, kWorkStringSize, "%s, line %d: %s", basename(pFileName), lineNumber, s_workString1);
        s_pCallbacks->Message(level, s_workString2);
    }
    else {
        s_pCallbacks->Message(level, s_workString1);
    }
}

/**
 * Standard implementation of TiltImplReportingCallbacks.ReportInteger.
 * Do not call directly.
 */
static void TiltReportInteger(const char * pName, s64 value, const char * pUnits)
{
    if (s_pCallbacks == NULL) {
         LTLOG_YELLOWALERT("ltut.ristate", "TiltReportInteger() has no callbacks");
         return;
   }

    s_pCallbacks->ReportInteger(pName, value, pUnits);
}

/**
 * Standard implementation of TiltImplReportingCallbacks.ReportFloat.
 * Do not call directly.
 */
static void TiltReportFloat(const char * pName, double value, const char * pUnits)
{
    if (s_pCallbacks == NULL) {
         LTLOG_YELLOWALERT("ltut.rfstate", "TiltReportFloat() has no callbacks");
         return;
    }

    s_pCallbacks->ReportFloatingPoint(pName, value, pUnits);
}

/**
 * Standard implementation of TiltImplReportingCallbacks.ReportBuffer.
 * Do not call directly.
 */
static void TiltReportBuffer(const char * pName, const u8 *buffer, u32 length)
{
    if (s_pCallbacks == NULL) {
         LTLOG_YELLOWALERT("ltut.rbstate", "TiltReportBuffer() has no callbacks");
         return;
    }

    s_pCallbacks->ReportBuffer(pName, buffer, length);
}

/**
 * Standard implementation of TiltImplReportingCallbacks.GetTestName.
 * Do not call directly.
 */
static const char * TiltGetTestName(void)
{
    if (s_pCallbacks == NULL) {
         LTLOG_YELLOWALERT("ltut.gtnstate", "TiltGetTestName() has no callbacks");
         return NULL;
    }
    if (s_runningTestIndex == kInvalidTestIndex) {
        s_pCallbacks->Message(kTilt_Message_Level_Warning, "GetTestName() called with no running test");
        return NULL;
    }

    return s_pTestList[s_runningTestIndex].pTestName;
}

/**
 * Standard implementation of TiltImplReportingCallbacks.DeferCompletion.
 * Do not call directly.
 */
static void TiltDeferCompletion(LTTime timeout, LTThread_ClientDataReleaseProc * cleanupProc, void * cleanupData)
{
    if (s_pCallbacks == NULL) {
         LTLOG_YELLOWALERT("ltut.tdcstate", "TiltDeferCompletion() has no callbacks");
         return;
    }

    if (cleanupProc != NULL && cleanupData == NULL) {
        LTLOG_YELLOWALERT("ltut.tdc", "TiltDeferCompletion() requires data if there's a proc; ignoring proc");
    } else {
        s_pIThread->SetThreadSpecificClientData(s_pIThread->GetCurrentThread(), kDeferredCompletionVar, cleanupProc, cleanupData);
    }

    s_pCallbacks->DeferCompletion(timeout);
    s_deferralThread = s_pIThread->GetCurrentThread();
    s_bDeferCompletion = true;
}


/**
 * Code to process SignalCompletion() on the test thread
 * (as opposed to whatever thread might have called TiltSignalCompletion()).
 */
static void SignalCompletionProc(void * pClientData)
{
    const char * pTestName = (const char *)pClientData;
    bool clearCallbacks = false;

    // Clear the deferred execution variable, which will trigger any ClientDataReleaseProc
    // attached to it.

    s_pIThread->SetThreadSpecificClientData(s_pIThread->GetCurrentThread(), kDeferredCompletionVar, NULL, NULL);

    switch (s_executionState) {
        case kTiltImplExecutionState_InBeforeAllTests:
            /* If a failure was reported during deferred processing, that failure will
             * have already been reported to the client, which should have recognized that
             * the failure happened in BeginTesting() and therefore cancelled the run.
             * Nothing to do here.
             */
            break;
        case kTiltImplExecutionState_RunningTest:
            CompleteTest();
            break;
        case kTiltImplExecutionState_InAfterAllTests:
            /* We probably don't care about failures reported during deferred processing in
             * AfterAllTests(); the client can choose to fail the entire test run if it wishes.
             * Nothing to do here except indicate that the callbacks need to be cleaned up..
             */
            clearCallbacks = true;
            break;
        default:
            TiltReportFailure(kTilt_Result_Error, __FILE__, __LINE__, "SignalCompletion() received in unexpected state %d", s_executionState);
            break;
    }

    s_pCallbacks->SignalCompletion(pTestName);

    s_executionState = kTiltImplExecutionState_None;
    s_deferralThread = 0;
    if (clearCallbacks) {
        s_pCallbacks = NULL;
    }
}

/**
 * Standard implementation of TiltImplReportingCallbacks.SignalCompletion.
 * Do not call directly.
 */
static void TiltSignalCompletion(const char * pTestName)
{
    if (s_pCallbacks == NULL) {
         LTLOG_YELLOWALERT("ltut.tscstate", "TiltSignalCompletion() has no callbacks");
         return;
    }

    /* This method could have been called from any thread, but we want all the
     * end-of-test processing to happen on the main test thread, which is (by definition)
     * the thread that called DeferCompletion().  So schedule the work on that thread
     * and then just return.
     */
    s_pIThread->QueueTaskProc(s_deferralThread, SignalCompletionProc, NULL, (void *)pTestName);
}

/**
 * Standard implementation of TiltImplReportingCallbacks.GetProperty.
 * Do not call directly.
 */
static const char * TiltGetProperty(const char * pPropertyName, const char * pDefaultValue)
{
    if (s_pCallbacks == NULL) {
         LTLOG_YELLOWALERT("ltut.gpstate", "TiltGetProperty() has no callbacks");
         return pDefaultValue;
    }

    return s_pCallbacks->GetProperty(pPropertyName, pDefaultValue);
}

static TiltHookedLibrary * GetHookedLibrary(const char *pLibraryName) {
    for (u32 i = 0; i < s_pTiltHookedLibraries->API->GetCount(s_pTiltHookedLibraries); i++) {
        TiltHookedLibrary *pHookEntry = s_pTiltHookedLibraries->API->Get(s_pTiltHookedLibraries, i, NULL);
        if (pHookEntry->pLibraryName && !lt_strcmp(pHookEntry->pLibraryName, pLibraryName)) {
            return pHookEntry;
        }
    }

    return NULL;
}

static TiltHookedLibInstance * GetHookedLibraryInstance(LTLibrary *pLibrary) {
    for (u32 i = 0; i < s_pTiltHookedLibraryInstances->API->GetCount(s_pTiltHookedLibraryInstances); i++) {
        TiltHookedLibInstance *pHookInstance = s_pTiltHookedLibraryInstances->API->Get(s_pTiltHookedLibraryInstances, i, NULL);
        if (pHookInstance->hookedLib == pLibrary) {
            return pHookInstance;
        }
    }

    return NULL;
}

static bool CleanupHookedLibraries(void) {
    bool success = true;

    // Check that no library instances remain open
    if (s_pTiltHookedLibraryInstances) {
        LTLOG("clean.hook.libs", "%u", (u16)s_pTiltHookedLibraryInstances->API->GetCount(s_pTiltHookedLibraryInstances));
        for (u32 i = 0; i < s_pTiltHookedLibraryInstances->API->GetCount(s_pTiltHookedLibraryInstances); i++) {
            TiltHookedLibInstance *pHookInstance = s_pTiltHookedLibraryInstances->API->Get(s_pTiltHookedLibraryInstances, i, NULL);
            if (!pHookInstance->hookedLib) continue;
            LTLOG_YELLOWALERT("hook.cleanup.still.open", "Library '%s' instance still open", pHookInstance->pHookEntry->pLibraryName);
            success = false;
        }
    }

    lt_destroyobject(s_pTiltHookedLibraryInstances);
    s_pTiltHookedLibraryInstances = NULL;
    lt_destroyobject(s_pTiltHookedLibraries);
    s_pTiltHookedLibraries = NULL;

    LT_GetCore()->SetLibraryHook(NULL);
    return success;
}

static LTLibrary * TiltCoreLibraryHook(LTCore_LibraryHookSite site, LTLibrary *pLibrary, const char *pLibraryName) {
    switch (site) {
        case kLTCore_LibraryHookSite_SubstituteOpen:
        case kLTCore_LibraryHookSite_AfterOpen:
            {
                // Library is being opened. Search list of libraries to hook to see if unit test cares about it
                TiltHookedLibrary *pHookEntry = GetHookedLibrary(pLibraryName);
                if (!pHookEntry) return pLibrary;

                // Does the unit test want to hook this library at this time?
                LTLibrary *hookedLibrary = (*pHookEntry->pLibraryHookFunction)(site, pLibrary, pLibraryName);
                // Make sure hook function returned a replaced or modified LTLibrary
                if (hookedLibrary && hookedLibrary != pLibrary) {
                    LTLOG("hook.lib", "%s hook %p orig %p", pLibraryName, hookedLibrary, pLibrary);
                    // Add a new entry to the instance array, mapping the back to the underlying library
                    TiltHookedLibInstance libInstance;
                    libInstance.pHookEntry  = pHookEntry;
                    libInstance.originalLib = pLibrary;
                    libInstance.hookedLib   = hookedLibrary;
                    s_pTiltHookedLibraryInstances->API->Append(s_pTiltHookedLibraryInstances, &libInstance);
                    pLibrary = hookedLibrary;
                }
            }
            break;
        case kLTCore_LibraryHookSite_BeforeClose:
            {
                // Library is being closed. Check to see if was previously hooked.
                TiltHookedLibInstance *pHookInstance = GetHookedLibraryInstance(pLibrary);
                if (pHookInstance) {
                    LTLOG("close.lib", "%s hook %p orig %p", pHookInstance->pHookEntry->pLibraryName, pLibrary, pHookInstance->originalLib);
                    // Notify the unit test to allow it to clean any other resources
                    LTLibrary *pHookedLibrary = (*pHookInstance->pHookEntry->pLibraryHookFunction)(site,
                                                                                                   pLibrary,
                                                                                                   NULL);
                    lt_free((void *)pHookedLibrary);  // may already be NULL, cast required to remove const-ness
                    pLibrary = pHookInstance->originalLib;
                    // Unit tests are not expected to make large number of library open/close/hook cycles so just zero
                    // the entry until the test ends
                    lt_memset(pHookInstance, 0, sizeof(TiltHookedLibInstance));
                }
            }
            break;
        default:
            LTLOG_YELLOWALERT("hook.site.err", "Invalid hook site");
            break;
    }

    return pLibrary;
}

/**
 * Standard implementation of TiltImplReportingCallbacks.HookLibrary.
 * Do not call directly.
*/
static bool TiltHookLibrary(const char * pLibraryName, LTCore_LibraryHookFunction *pLibraryHookFunction) {
    if (!pLibraryName || *pLibraryName == 0) {
        LTLOG_YELLOWALERT("hook.not.provided", "Library name not provided");
        return false;
    }

    // First-time setup of the internal arrays and general library hook in LT core, if required
    if (!s_pTiltHookedLibraries) {
        if (!LT_GetCore()->SetLibraryHook(TiltCoreLibraryHook)) {
            LTLOG_YELLOWALERT("hook.nothooked", "Failed to set LT core library hook");
            return false;
        }

        s_pTiltHookedLibraries = LTArray_CreateStructArray(sizeof(TiltHookedLibrary));
        s_pTiltHookedLibraryInstances = LTArray_CreateStructArray(sizeof(TiltHookedLibInstance));

        if (!s_pTiltHookedLibraries || !s_pTiltHookedLibraryInstances) {
            // Either or both failed to create - destroy them to be sure
            lt_destroyobject(s_pTiltHookedLibraries);
            s_pTiltHookedLibraries = NULL;
            lt_destroyobject(s_pTiltHookedLibraryInstances);
            s_pTiltHookedLibraryInstances = NULL;
            LTLOG_YELLOWALERT("hook.nothooked", "Failed to alloc hook arrays");
            LT_GetCore()->SetLibraryHook(NULL);
            return false;
        }
    }

    TiltHookedLibrary *pHookEntry = GetHookedLibrary(pLibraryName);
    if (pHookEntry) {
        // Update the test hook function (either new hook or remove existing)
        pHookEntry->pLibraryHookFunction = pLibraryHookFunction;
        if (!pLibraryHookFunction) {
            pHookEntry->pLibraryName = NULL;
        }
    } else {
        // Add an entry for the library name to list of libraries that can be hooked
        TiltHookedLibrary newHook;
        newHook.pLibraryName = pLibraryName;
        newHook.pLibraryHookFunction = pLibraryHookFunction;
        s_pTiltHookedLibraries->API->Append(s_pTiltHookedLibraries, &newHook);
    }
    return true;
}


/*******************************************************************************
 * Callback functions usable when TILT is not active.
 *
 * Some tests may wish to share code between the TILT path and some other
 * method of invocation.  In the latter case, TILT will not be active and so
 * the normal test callbacks won't work.  The following functions provide a
 * very simple implementation of all callbacks.  Call GetNoTiltCallbacks() to
 * obtain the callback struct; do not call the rest of these functions directly.
 *******************************************************************************/

static const char * s_noTiltTestName = "(non-TILT)";    ///< GetTestName() return; can be changed by test library code

static void NoTiltReportFailure(TiltResult type, const char * pFileName, u32 lineNumber, const char * pMessage, ...) {
    LT_UNUSED(type);
    LT_UNUSED(lineNumber);
    LT_UNUSED(pFileName);

    char message[64];
    lt_va_list args;
    lt_va_start(args, pMessage);
    lt_vsnprintf(message, sizeof(message), pMessage, args);
    LT_GetCore()->Log(s_noTiltTestName, "ReportFailure", kLTCore_LogFlags_LogTypeRedAlert | kLTCore_LogFlags_LogToConsole, "%s\n", message);
    lt_va_end(args);
}

static void NoTiltMessage(const char * pFileName, u32 lineNumber, TiltMessageLevel level, const char * pMessage, ...) {
    LT_UNUSED(pFileName);
    LT_UNUSED(lineNumber);
    LT_UNUSED(pFileName);

    u32 logFlags = kLTCore_LogFlags_LogToConsole;
    switch (level) {
        case kTilt_Message_Level_Warning:
            logFlags |= kLTCore_LogFlags_LogTypeYellowAlert;
            break;
    }

    bool bLog = (level >= kTilt_Message_Level_Info);
#ifdef LT_DEBUG
    bLog = true;
#endif
    if (bLog) {
        char message[64];
        lt_va_list args;
        lt_va_start(args, pMessage);
        lt_vsnprintf(message, sizeof(message), pMessage, args);
        LT_GetCore()->Log(s_noTiltTestName, "Message", logFlags, "%s\n", message);
        lt_va_end(args);
    }
}

static void NoTiltReportInteger(const char * pName, s64 value, const char * pUnits) {
    LT_UNUSED(pName);
    LT_UNUSED(value);
    LT_UNUSED(pUnits);
}

static void NoTiltReportFloat(const char * pName, double value, const char * pUnits) {
    LT_UNUSED(pName);
    LT_UNUSED(value);
    LT_UNUSED(pUnits);
}

static const char * NoTiltGetTestName(void) {
    return s_noTiltTestName;
}

static void NoTiltDeferCompletion(LTTime timeout, LTThread_ClientDataReleaseProc * cleanupProc, void * cleanupData) {
    LT_UNUSED(timeout);
    LT_UNUSED(cleanupProc);
    LT_UNUSED(cleanupData);
}

static void NoTiltSignalCompletion(const char * pTestName) {
    LT_UNUSED(pTestName);
}

static const char * NoTiltGetProperty(const char * pPropertyName, const char * pDefaultValue) {
    LT_UNUSED(pPropertyName);
    return pDefaultValue;
}

static bool NoTiltHookLibrary(const char * pLibraryName, LTCore_LibraryHookFunction *pLibraryHookFunction) {
    LT_UNUSED(pLibraryName);
    LT_UNUSED(pLibraryHookFunction);
    return false;
}

/**
 * Initialize a test callback struct with functions that don't require Tilt.
 *
 * @param[out] pTrc Pointer to the callback struct to be initialized.
 */
static void GetNoTiltCallbacks(TiltImplReportingCallbacks * pTrc)
{
    pTrc->ReportFailure       = NoTiltReportFailure;
    pTrc->Message             = NoTiltMessage;
    pTrc->ReportInteger       = NoTiltReportInteger;
    pTrc->ReportFloat         = NoTiltReportFloat;
    pTrc->GetTestName         = NoTiltGetTestName;
    pTrc->DeferCompletion     = NoTiltDeferCompletion;
    pTrc->SignalCompletion    = NoTiltSignalCompletion;
    pTrc->GetProperty         = NoTiltGetProperty;
    pTrc->HookLibrary         = NoTiltHookLibrary;
}


/**
 * Initialization function for this module.
 * A unit test library must call this function first,
 * and then never call it again.
 *
 * @param[in] testList The list of tests available in this test library.
 * The contents of this pointer must remain valid after this call.
 * @param[in] testCount The number of elements in @c testList.
 * @param[out] reportingCallbacks Functions used by test code to report failures, etc.
 * @param[in] pTestHooks An optional list of test hook functions.
 * It may be NULL, in which case no test hooks will be called.
 * If it is non-NULL, the pointer and its contents must remain valid until after
 * the test run has completed.
 */
static bool InitializeTilt(const TiltImplTestSpecifier * testList, u32 testCount, TiltImplTestHooks * pTestHooks)
{
    // Prevent libraries that don't use this call from getting "defined but not used" compilation errors
    LT_UNUSED(GetNoTiltCallbacks);

    if (s_pTestList != NULL) {
        LTLOG_YELLOWALERT("ltut.itfstate", "InitializeTilt() has already been called");
        return false;
    }
    if (testList == NULL) {
         LTLOG_YELLOWALERT("ltut.itflist", "InitializeTilt() got null testList");
         return false;
    }
    if (testCount == 0) {
         LTLOG_YELLOWALERT("ltut.itfcount", "InitializeTilt() got no tests");
         return false;
    }

    s_pTestList = lt_malloc(testCount * sizeof(s_pTestList[0]));
    if (s_pTestList == NULL) {
         LTLOG_YELLOWALERT("ltut.malloc", "InitializeTilt() failed allocation of test array");
         return false;
    }
    for (u32 ii = 0; ii < testCount; ++ii) {
        s_pTestList[ii].pTestName        = testList[ii].pTestName;
        s_pTestList[ii].pTestDescription = testList[ii].pTestDescription;
        s_pTestList[ii].testFlags        = testList[ii].testFlags;
    }

    s_workString1 = lt_malloc(kWorkStringSize);
    s_workString2 = lt_malloc(kWorkStringSize);
    if (s_workString1 == NULL || s_workString2 == NULL) {
        LTLOG_YELLOWALERT("ltut.wsmalloc", "Failed to initialize work strings");
        return false;
    }

    s_pCallbacks        = NULL;
    s_pInternalTestList = testList;
    s_testCount         = testCount;
    s_runningTestIndex  = kInvalidTestIndex;
    s_pTestHooks        = pTestHooks;
    s_currentResult     = kTilt_Result_None;
    s_bDeferCompletion  = false;
    s_pIThread          = lt_getlibraryinterface(ILTThread, LT_GetCore());
    s_deferralThread    = 0;

    return true;
}

/**
 * Shutdown function for this module.
 * Every unit test library should call this in the LibFini() function.
 */
static void ShutdownTilt(void)
{
    CleanupHookedLibraries();
    s_deferralThread    = 0;
    s_pIThread          = NULL;
    s_pTestHooks        = NULL;
    s_testCount         = 0;
    s_pInternalTestList = NULL;
    s_pCallbacks        = NULL;

    lt_free(s_workString2);
    s_workString2 = NULL;
    lt_free(s_workString1);
    s_workString1 = NULL;

    lt_free(s_pTestList);
    s_pTestList = NULL;
}

/*******************************************************************************
 * Library API Implementation Bindings
 ******************************************************************************/
define_LTLIBRARY_INTERFACE(ITilt) {
    .GetLibrarySettings = GetLibrarySettings,
    .GetTestList        = GetTestList,
    .BeginTesting       = BeginTesting,
    .RunTest            = RunTest,
    .EndTesting         = EndTesting
} LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Jun-27   pertinax    created
 */
