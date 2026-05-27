/*******************************************************************************
 * <tilt/TiltEngine.c>
 *    ______ ____ __   ______
 *   /_  __//  _// /  /_  __/       TILT: the Test
 *    / /   / / / /    / /                Infrastructure
 *   / /  _/ / / /___ / /                 for LT
 *  /_/  /___//_____//_/
 *
 *  Command-line interface for TILT.
 *      Enables invocation of Unit Tests made with TILT, and provides
 *      test-result output in human-readable and JSON formats, through
 *      standard TILT callbacks.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#define TILT_VALGRIND 0
#if defined(__GNUC__) && defined(LT_DEBUG)
#if __has_include(<valgrind/memcheck.h>)
#include <valgrind/memcheck.h>
#undef TILT_VALGRIND
#define TILT_VALGRIND 1
#endif
#endif
#if !TILT_VALGRIND
#define VALGRIND_DO_QUICK_LEAK_CHECK
#define VALGRIND_DO_CHANGED_LEAK_CHECK
#endif
#include <lt/LT.h>
#include <lt/core/LTArray.h>
#include <tilt/Tilt.h>
#include <tilt/TiltEngine.h>
#include <lt/system/settings/LTSystemSettings.h>

enum { kTiltEngineDefaultTimeout = 30 };

/* Command-line options */
static const char *  s_pTestLibraryName  = NULL;
static LTLibrary *   s_pTestLibrary      = NULL;
static bool          s_bQuiet            = false;
static bool          s_bVerbose          = false;
static bool          s_bAutomation       = false; ///< Skip tests that shouldn't run in automation
static u32           s_failureLimit      = 999999;
static u32           s_failureCount      = 0;
static bool          s_bEmitText         = true;  ///< Emit human-readable output
static bool          s_bEmitJson         = false; ///< Emit JSON output
static bool          s_bPrettyJson       = false; ///< Make JSON output human-friendly
static LTTime        s_timeout;
                                                  ///< Timeout for entire test run
static bool          s_bMemCheck         = true;  ///< Check heap and available RAM every test?
static bool          s_bRtos             = true;  ///< Is current platform a real RTOS?
static const char ** s_ppIncludeTests    = NULL;  ///< List of specific tests to include
static u32           s_includeTestCount  = 0;     ///< Number of tests in s_ppIncludeTests
static const char ** s_ppExcludeTests    = NULL;  ///< List of specific tests to exclude
static u32           s_excludeTestCount  = 0;     ///< Number of tests in s_ppExcludeTests
static u32           s_testThreadStackSize = 4096; ///< Stack size for test thread
static bool          s_prependTime       = false;  ///< Prepend kernel time to (non-JSON) output

static u32           s_jsonIndentLevel    = 0;    ///< Current indentation level for JSON output
static u32           s_jsonLastPrintLevel = 0;    ///< Indentation level of last PrintJson call

/* Current execution state */
typedef enum {
    kTiltEngineExecutionState_None,
    kTiltEngineExecutionState_InBeginTesting,
    kTiltEngineExecutionState_RunningTest,
    kTiltEngineExecutionState_InEndTesting,
} TiltEngineExecutionState;
static TiltEngineExecutionState s_executionState = kTiltEngineExecutionState_None;

/* State controlling test execution */
static u16 *         s_testsToRun           = NULL;  ///< List of tests to run, in order (by index)
static u32           s_testsToRunCount      = 0;     ///< Number of entries in s_testsToRun
static s32           s_testsToRunIndex      = -1;    ///< Index of currently-running test in s_testsToRun
static bool          s_bDeferCompletion     = false; ///< If true, test is asynchronously running
static ILTThread *   s_pIThread             = NULL;  ///< LTThread library interface

/* Values from test library */
static ITilt *             s_pUnitTest     = NULL;
static TiltTestSpecifier * s_pAllTests     = NULL;
static u32                 s_allTestsCount = 0;

/* Results of test run */
static TiltResult *  s_pResults            = NULL;
static u32           s_runningTest         = kInvalidTestIndex;
static TiltResult    s_resultOutsideOfTest = kTilt_Result_None;
static u32           s_currentTestFailures = 0;

/* List of open libraries before the test library is loaded */
static LTSystemSettings       * s_pSystemSettings     = NULL;
static LTArray                * s_pOpenLibraries      = NULL;
static u32                      s_leakedLibraryCount  = 0;

static int s_threadResult = kTiltEngine_InternalError;  ///< Exit code reported by test thread

/* Support for test properties (such as endpoint address for networking tests) */
enum { kTilt_Property_Name_Length  = 32 };
enum { kTilt_Property_Value_Length = 32 };
typedef struct PropertyCell {
    char name[kTilt_Property_Name_Length];
    char value[kTilt_Property_Value_Length];
    struct PropertyCell * next;
} PropertyCell;
PropertyCell * s_testProperties = NULL;


static const char * kTilt_Color_Default    = "\x1b[39m";   ///< (terminal settings)
static const char * kTilt_Color_Yellow     = "\x1b[33m";
static const char * kTilt_Color_Red        = "\x1b[31m";
static const char * kTilt_Color_Green      = "\x1b[32m";

static void ReportFailureCallback(TiltResult type, const char * pMessage);


/**
 * Snapshot of current memory usage.
 */
typedef struct {
    u32 heapSize;       ///< Current heap size (in bytes)
    u32 availableRAM;   ///< Current total available RAM (in bytes)
} MemoryUsage;

static MemoryUsage s_lastMemoryUsage;   ///< Memory info right after previous test ended

typedef struct {
    LTHandle handle;
    const char *interfaceName;
    const char *libraryName;
} LTHandleName;

typedef struct {
    MemoryUsage startMemoryUsage;
    MemoryUsage endMemoryUsage;
    //LTCore_HeapAllocSnapshot * heapAllocsBeforeRun;
    //LT_SIZE heapAllocsBeforeRunCount;
    //bool comparingHeapAllocs;
    LTHandleName * openHandlesBeforeRun;
    u32 openHandlesBeforeRunCount;
} MemoryCheckStruct;

static MemoryCheckStruct s_MemCheck;

/* Forward declarations of functions */
static void CompareOpenLibraries(void);
static void ContinueTestThread(void * pClientData);
static void GetMemoryUsage(MemoryUsage * usage);
static void PrintText(const char * pColor, const char * pFormatString, ...);
static void RecordOpenLibraries(void);
static void RunNextTest(LTThread hThread);
static void MessageCallback(TiltMessageLevel level, const char * pMessage);

static void MemCheckStart(void) {
    LTHandle * openHandles;
    lt_memset(&s_MemCheck, 0, sizeof(MemoryCheckStruct));

    // Collect the list of open libraries (for comparison after tests run)
    RecordOpenLibraries();

    // Collect the list of open handles (for comparison after tests run)
    s_MemCheck.openHandlesBeforeRunCount = LT_GetCore()->GetHandlesByInterface(NULL, 0, NULL);
    openHandles      = lt_malloc(s_MemCheck.openHandlesBeforeRunCount * sizeof(LTHandle));
    LT_ASSERT(openHandles != NULL);
    s_MemCheck.openHandlesBeforeRunCount = LT_GetCore()->GetHandlesByInterface(openHandles, s_MemCheck.openHandlesBeforeRunCount, NULL);

    s_MemCheck.openHandlesBeforeRun = lt_malloc(sizeof(LTHandleName) * s_MemCheck.openHandlesBeforeRunCount);
    LT_ASSERT(s_MemCheck.openHandlesBeforeRun != NULL);
    for (LT_SIZE ii = 0; ii < s_MemCheck.openHandlesBeforeRunCount; ii++) {
        LTHandle handle = openHandles[ii];
        s_MemCheck.openHandlesBeforeRun[ii].handle = handle;
        if (handle) {

            s_MemCheck.openHandlesBeforeRun[ii].interfaceName = LT_GetCore()->GetHandleInterfaceName(handle);
            if (s_MemCheck.openHandlesBeforeRun[ii].interfaceName == NULL) {
                s_MemCheck.openHandlesBeforeRun[ii].interfaceName = "(none)";
            }
            LTLibrary *library = LT_GetCore()->GetHandleLibrary(handle);
            if (library) {
                s_MemCheck.openHandlesBeforeRun[ii].libraryName = library->GetLibraryExtrinsicName();
            } else {
                s_MemCheck.openHandlesBeforeRun[ii].libraryName = "(none)";
            }
        }
    }
    lt_free(openHandles);

    // Record heap size and available RAM (before loading test library!)
    VALGRIND_DO_QUICK_LEAK_CHECK;
    GetMemoryUsage(&s_MemCheck.startMemoryUsage);

#if 0
    /* DRW : heap info changed to enumeration api, not snapshot api, and the old method only worked in debug mode
             with special flags set in LTCoreDebugHelpers.h, so it wasn't run anyway.  Disabling to avoid compiler error
             to expedite merge to main for leak tracking support enhancements. */
    // Record list of heap allocations
    if (!LT_GetCore()->SnapshotSystemRAM(0, &s_MemCheck.heapAllocsBeforeRun, &s_MemCheck.heapAllocsBeforeRunCount)) {
        MessageCallback(kTilt_Message_Level_Debug, "(Heap allocation snapshot failed - no heap alloc checks at end)");
        s_MemCheck.comparingHeapAllocs = false;
    }
#endif
}

static void MemCheckEnd(void) {
    // Compare heap allocations after unloading test library with those before
#if 0
    /* DRW : heap info changed to enumeration api, not snapshot api, and the old method only worked in debug mode
             with special flags set in LTCoreDebugHelpers.h, so it wasn't run anyway.  Disabling to avoid compiler error
             to expedite merge to main for leak tracking support enhancements. */
    if (s_MemCheck.comparingHeapAllocs) {
        LTCore_HeapAllocSnapshot * heapAllocsAfterRun = NULL;
        LT_SIZE heapAllocsAfterRunCount = 0;
        LT_GetCore()->SnapshotSystemRAM(0, &heapAllocsAfterRun, &heapAllocsAfterRunCount);

        // Note: There will always be one additional heap allocation, which is the
        // heapAllocsAfterRun array that was created by the second SnapshotSystemRAM
        // call!  We compensate in the following test and in the "special check" below.

        if (heapAllocsAfterRunCount - 1 != s_MemCheck.heapAllocsBeforeRunCount) {
            PrintText(kTilt_Color_Red, "\nHeap alloc count mismatch: %lu before run, %lu after\n",
                    LT_PLT_SIZE(s_MemCheck.heapAllocsBeforeRunCount), LT_PLT_SIZE(heapAllocsAfterRunCount-1));
            if (heapAllocsAfterRunCount - 1 > s_MemCheck.heapAllocsBeforeRunCount) {
                for (LT_SIZE ii = 0; ii < heapAllocsAfterRunCount; ++ii) {
                    void * matchPtr = heapAllocsAfterRun[ii].pAddr;
                    bool ptrFound = false;
                    for (LT_SIZE jj = 0; jj < s_MemCheck.heapAllocsBeforeRunCount; ++jj) {
                        if (s_MemCheck.heapAllocsBeforeRun[jj].pAddr == matchPtr) {
                            ptrFound = true;
                            break;
                        }
                    }

                    // Special check: if the additional allocation is just the heapAllocsAfterRun
                    // array, ignore it; this is expected.
                    if (!ptrFound && matchPtr == heapAllocsAfterRun) {
                        ptrFound = true;
                    }

                    if (!ptrFound) {
                        LTCore_HeapAllocSnapshot * const snapshot = &heapAllocsAfterRun[ii];
                        const char * fileName = snapshot->callsite.file;
                        if (fileName == NULL) {
                            fileName = "(none)";
                        }
                        PrintText(kTilt_Color_Red, "  Pointer %p, size %lu, file \"%s\", line %lu\n",
                                matchPtr, LT_Pu32(snapshot->nBytes), fileName, LT_PLT_SIZE(snapshot->callsite.line));
                    }
                }
            }
        }
        lt_free(heapAllocsAfterRun);
        heapAllocsAfterRun = NULL;
        lt_free(s_MemCheck.heapAllocsBeforeRun);
        s_MemCheck.heapAllocsBeforeRun = NULL;
        s_MemCheck.comparingHeapAllocs = false;
    }
#endif

    // Compare available RAM after unloading test library with that before

    char workString[128];

    VALGRIND_DO_CHANGED_LEAK_CHECK;
    GetMemoryUsage(&s_MemCheck.endMemoryUsage);
    if (s_MemCheck.endMemoryUsage.availableRAM < s_MemCheck.startMemoryUsage.availableRAM) {
        lt_snprintf(workString, sizeof(workString), "%lu bytes potentially leaked during test run (available RAM went from %lu.%02lu to %lu.%02lu kb)\n",
                    LT_Pu32(s_MemCheck.startMemoryUsage.availableRAM - s_MemCheck.endMemoryUsage.availableRAM),
                    LT_Pu32(s_MemCheck.startMemoryUsage.availableRAM >> 10), LT_Pu32(((s_MemCheck.startMemoryUsage.availableRAM & 0x3FF) * 100) >> 10),
                    LT_Pu32(s_MemCheck.endMemoryUsage.availableRAM   >> 10), LT_Pu32(((s_MemCheck.endMemoryUsage.availableRAM   & 0x3FF) * 100) >> 10));
        if (s_bEmitJson) {
            MessageCallback(kTilt_Message_Level_Warning, workString);
        } else {
            PrintText(kTilt_Color_Red, workString);
        }
    } else {
        lt_snprintf(workString, sizeof(workString), "Available RAM (%lu) did not decrease. Well done!",
                    LT_Pu32(s_MemCheck.endMemoryUsage.availableRAM));
        MessageCallback(kTilt_Message_Level_Info, workString);
    }

    // Match opened handles with those before test
    u32 openHandlesAfterRunCount    = LT_GetCore()->GetHandlesByInterface(NULL, 0, NULL);
    LTHandle * openHandlesAfterRun  = lt_malloc(openHandlesAfterRunCount * sizeof(LTHandle));
    LT_ASSERT(openHandlesAfterRun != NULL);
    u32 newCount                    = LT_GetCore()->GetHandlesByInterface(openHandlesAfterRun, openHandlesAfterRunCount, NULL);
    if (newCount < openHandlesAfterRunCount) openHandlesAfterRunCount = newCount;

    if (openHandlesAfterRunCount != s_MemCheck.openHandlesBeforeRunCount) {
        PrintText(kTilt_Color_Red, "\nOpen handle mismatch: %lu before run, %lu after\n",
                    LT_PLT_SIZE(s_MemCheck.openHandlesBeforeRunCount), LT_PLT_SIZE(openHandlesAfterRunCount));
    }

    // Compare for differences
    for (LT_SIZE ii = 0; ii < openHandlesAfterRunCount; ii++) {
        LTHandle matchHandle = openHandlesAfterRun[ii];
        for (LT_SIZE jj = 0; jj < s_MemCheck.openHandlesBeforeRunCount; jj++) {
            if (s_MemCheck.openHandlesBeforeRun[jj].handle == matchHandle) {
                openHandlesAfterRun[ii] = 0;
                s_MemCheck.openHandlesBeforeRun[jj].handle = 0;
                break;
            }
        }
    }

    for (LT_SIZE ii = 0; ii < openHandlesAfterRunCount; ii++) {
        LTHandle handle = openHandlesAfterRun[ii];
        if (openHandlesAfterRun[ii]) {
            const char * interfaceName = LT_GetCore()->GetHandleInterfaceName(handle);
            if (interfaceName == NULL) {
                interfaceName = "(none)";
            }
            LTLibrary *library = LT_GetCore()->GetHandleLibrary(handle);
            const char * libraryName = "(none)";
            if (library) {
                libraryName = library->GetLibraryExtrinsicName();
            }
            PrintText(kTilt_Color_Red, "  Left open Handle %lu, interface \"%s\", library \"%s\"\n",
                        LT_Pu32(handle), interfaceName, libraryName);

        }
    }

    for (LT_SIZE ii = 0; ii < s_MemCheck.openHandlesBeforeRunCount; ii++) {
        LTHandle handle = s_MemCheck.openHandlesBeforeRun[ii].handle;
        if (handle) {
            PrintText(kTilt_Color_Red, "  Closed Handle  %lu, interface \"%s\", library \"%s\"\n",
                        LT_Pu32(handle), s_MemCheck.openHandlesBeforeRun[ii].interfaceName, s_MemCheck.openHandlesBeforeRun[ii].libraryName);
        }
    }

    lt_free(openHandlesAfterRun);
    openHandlesAfterRun = NULL;
    lt_free(s_MemCheck.openHandlesBeforeRun);
    s_MemCheck.openHandlesBeforeRun = NULL;

    // Compare list of open libraries with those before the test library was loaded

    PrintText(kTilt_Color_Default, "\nLeaked libraries:\n");
    CompareOpenLibraries();
    if (s_leakedLibraryCount == 0) {
        PrintText(kTilt_Color_Green, "  NONE\n");
    }
    if (s_pOpenLibraries) {
        LTArray_RemoveAndFreeAll(s_pOpenLibraries);
        lt_destroyobject(s_pOpenLibraries);
        s_pOpenLibraries = NULL;
    }

    lt_memset(&s_MemCheck, 0, sizeof(MemoryCheckStruct));
}

/**
 * Set the color of the output using VT100 codes
 */
static void SetTextColor(const char * pColor) {
    // set the color
    static const char * pLastColor = NULL;
    if (pColor != pLastColor) {
        LT_GetCore()->ConsoleStompString(pColor);
        pLastColor = pColor;
    }
}

/**
 * Print information that's not part of test results.
 * Do not use it to report errors, or any other information that might be
 * written as part of JSON output!
 */
static void PrintOther(const char * pColor, const char * pFormatString, ...)
{
    LT_GetCore()->FlushConsoleOutput();

    lt_va_list args;
    lt_va_start(args, pFormatString);
    SetTextColor(pColor);
    LT_GetCore()->ConsoleStompV(pFormatString, args);
    SetTextColor(kTilt_Color_Default);
    lt_va_end(args);
}

/**
 * Print an error message (as human-readable text or JSON, depending on settings).
 * Note that this will also register an error in Tilt execution.
 */
static void ReportTiltEngineError(const char * pFormatString, ...)
{
    char workString[64];
    lt_va_list args;
    lt_va_start(args, pFormatString);
    lt_vsnprintf(workString, sizeof(workString), pFormatString, args);
    lt_va_end(args);

    ReportFailureCallback(kTilt_Result_Error, workString);
}

/**
 * Print human-readable output.
 * This function will do nothing if s_emitText is false.
 */
static void PrintText(const char * pColor, const char * pFormatString, ...)
{
    if (!s_bEmitText) {
        return;
    }
    LT_GetCore()->FlushConsoleOutput();

    if (s_prependTime) {
        char timeString[24];
        LT_GetCore()->FormatCanonicalTimeString(LT_GetCore()->GetKernelTime(), timeString, sizeof(timeString), true);
        LT_GetCore()->ConsoleStompString(timeString);
        LT_GetCore()->ConsoleStompChar(' ');
    }

    lt_va_list args;
    lt_va_start(args, pFormatString);
    SetTextColor(pColor);
    LT_GetCore()->ConsoleStompV(pFormatString, args);
    SetTextColor(kTilt_Color_Default);
    lt_va_end(args);
}

/**
 * Print JSON output.
 * This function will do nothing if s_emitJson is false.
 */
static void PrintJson(const char * pFormatString, ...)
{
    if (!s_bEmitJson) {
        return;
    }

    // Finish up previous line
    if (s_jsonLastPrintLevel == s_jsonIndentLevel) {
        LT_GetCore()->ConsoleStompString(",");
    }

    // If we're making nice, indented output, advance to the next line
    if (s_bPrettyJson) {
        LT_GetCore()->ConsoleStompString("\n");

        // Indent current line
        for (u32 ii = 0; ii < s_jsonIndentLevel; ++ii) {
            LT_GetCore()->ConsoleStompString("  ");
        }
    }
    s_jsonLastPrintLevel = s_jsonIndentLevel;

    // Print without newline, that will be done on the next call
    lt_va_list args;
    lt_va_start(args, pFormatString);
    LT_GetCore()->ConsoleStompV(pFormatString, args);
    lt_va_end(args);
}

/**
 * Open a new JSON block.
 * This function will do nothing if s_emitJson is false.
 * Otherwise, it will emit @c pOpenString and then increase the indentation level.
 */
static void OpenJsonBlock(const char * pOpenString)
{
    PrintJson(pOpenString);
    s_jsonIndentLevel += 1;
    s_jsonLastPrintLevel -= 1;  // Prevent a wayward comma in empty block
}

/**
 * Close a JSON block.
 * This function will do nothing if s_emitJson is false.
 * Otherwise, it will decrease the indentation level and then emit @c pCloseString.
 */
static void CloseJsonBlock(const char * pCloseString)
{
    if (s_jsonIndentLevel == 0) {
        ReportTiltEngineError("Trying to close nonexistent JSON block!");
        return;
    }
    s_jsonIndentLevel -= 1;
    PrintJson(pCloseString);
    if (s_jsonIndentLevel == 0 && s_bEmitJson) {
        // Newline after final closing brace
        LT_GetCore()->ConsoleStompString("\n");
    }
}

/**
 * Collect statistics on memory usage (both heap and available RAM).
 *
 * @param[out] usage Memory usage returned by this call.
 */
static void GetMemoryUsage(MemoryUsage * usage)
{
    LTThread_Snapshot threadSnapshot;
    threadSnapshot.nStructureSize = sizeof(threadSnapshot);
    s_pIThread->GetSnapshot(s_pIThread->GetCurrentThread(), &threadSnapshot);
    usage->heapSize = threadSnapshot.nHeapCurrent;
    usage->availableRAM = LT_GetCore()->GetAvailableSystemRAM();
}

/**
 * Callback function for SnapshotOpenLibraries to record open libraries at beginning of run.
 */
static void OpenLibraryBeginCallback(LTCore_LibrarySnapshot * pSnapshot, void * clientData)
{
    LT_UNUSED(clientData);
    s_pOpenLibraries->API->Append(s_pOpenLibraries, lt_strdup(pSnapshot->name));
}

/**
 * Record list of open libraries in s_openLibraries.
 */
static void RecordOpenLibraries(void)
{
    /* Allocate the open libraries array */
    s_pOpenLibraries = lt_createobject(LTArray);
    if (s_pOpenLibraries == NULL) {
        PrintText(kTilt_Color_Red, "Failed to allocate open libraries array\n");
        return;
    }
    s_pOpenLibraries->API->TuneAllocation(s_pOpenLibraries, 4, 0);

    /* Record all open library names */
    LT_GetCore()->SnapshotOpenLibraries(OpenLibraryBeginCallback, NULL);
}

/**
 * Callback function for SnapshotOpenLibraries to check open libraries at end of run.
 */
static void OpenLibraryEndCallback(LTCore_LibrarySnapshot * pSnapshot, void * clientData)
{
    LT_UNUSED(clientData);

    for (u32 i = 0; i < s_pOpenLibraries->API->GetCount(s_pOpenLibraries); ++i) {
        if (!lt_strcmp(pSnapshot->name, s_pOpenLibraries->API->Get(s_pOpenLibraries, i, NULL))) {
            // Library found in list; we're all done!
            return;
        }
    }

    /* Library was not found; report the error */
    PrintText(kTilt_Color_Red, "  %s\n", pSnapshot->name);
    ++s_leakedLibraryCount;
}

/**
 * For every open library, report an error if it's not present in s_openLibraries.
 * This can be used at the end of the run to see if any libraries "leaked"
 * (i.e. were opened and then not closed).
 */
static void CompareOpenLibraries(void)
{
    s_leakedLibraryCount = 0;
    LT_GetCore()->SnapshotOpenLibraries(OpenLibraryEndCallback, NULL);
}

static void ListAllTests(void)
{
    for (u32 ii = 0; ii < s_allTestsCount; ++ii) {
        if (s_bVerbose) {
            const char * desc = s_pAllTests[ii].pTestDescription;
            if (desc == NULL) {
                desc = "(no description)";
            }
            PrintOther(kTilt_Color_Default, "%s -- %s\n", s_pAllTests[ii].pTestName, desc);

        } else {
            PrintOther(kTilt_Color_Default, "%s\n", s_pAllTests[ii].pTestName);
        }
    }
}

/**
 * Help the user with proper invocation syntax.
 */
static void DisplayUsage(const char *pProgramName)
{
    PrintOther(kTilt_Color_Default, "usage: %s test-library [<option> ...] [-- test ...]\n", pProgramName);
    PrintOther(kTilt_Color_Default, "  or   %s test-library [<option> ...] [--exclude-tests test ...]\n\n", pProgramName);
    PrintOther(kTilt_Color_Default, "where <option> is one of:\n\n");
    PrintOther(kTilt_Color_Default, "  --list               List all tests and exit\n");
    PrintOther(kTilt_Color_Default, "  --quiet              Produce minimal output (statistics and list of failed tests)\n");
    PrintOther(kTilt_Color_Default, "  --verbose            Produce maximal output, including all messages from tests\n");
    PrintOther(kTilt_Color_Default, "  --failure-limit <n>  Exit after <n> failures have been reported\n");
    PrintOther(kTilt_Color_Default, "  --json               Produce JSON output (instead of human-readable format)\n");
    PrintOther(kTilt_Color_Default, "  --pretty             Make JSON output human-readable\n");
    PrintOther(kTilt_Color_Default, "  --automation         Skip tests that shouldn't run in automation\n");
    PrintOther(kTilt_Color_Default, "  --timeout <n>        Timeout (in seconds) for entire test run (default %u)\n",
               kTiltEngineDefaultTimeout);
    PrintOther(kTilt_Color_Default, "  --nomemcheck         Skip all memory and open library checks\n");
    PrintOther(kTilt_Color_Default, "  --stacksize <n>      Stack size for test thread (default %ld)\n", LT_Pu32(s_testThreadStackSize));
    PrintOther(kTilt_Color_Default, "  --timestamp          Prepend all non-JSON output with kernel timestamp\n");
    PrintOther(kTilt_Color_Default, "  <property>=<value>   Set test property to a value\n");
    PrintOther(kTilt_Color_Default, "                         (Name < %lu bytes; value < %lu bytes)\n",
               LT_Pu32(kTilt_Property_Name_Length), LT_Pu32(kTilt_Property_Value_Length));
}

static const char * ResultToString(TiltResult testResult)
{
    const char * result = "UNKNOWN";
    switch (testResult) {
        case kTilt_Result_Success:
            result = "success";
            break;
        case kTilt_Result_Failure:
            result = "failure";
            break;
        case kTilt_Result_Error:
            result = "error";
            break;
        case kTilt_Result_CannotRun:
            result = "cannot-run";
            break;
        case kTilt_Result_None:
            result = "did-not-run";
            break;
        default:
            break;
    }

    return result;
}

static void CompleteSingleTest(u32 testIndex)
{
    const char * const testName = s_pAllTests[testIndex].pTestName;
    const char * const resultString = ResultToString(s_pResults[testIndex]);

    s_executionState = kTiltEngineExecutionState_None;
    if (!s_bQuiet) {
        PrintText(kTilt_Color_Default, "Finished test %s: %s\n", testName, resultString);
    }
    CloseJsonBlock("]");
    PrintJson("\"result\": \"%s\", \"reported-failures\": \"%lu\"", resultString, LT_Pu32(s_currentTestFailures));
    CloseJsonBlock("}");

    if (s_bMemCheck) {
        /* Get memory usage statistics and compare to those at the end of the last test */
        VALGRIND_DO_CHANGED_LEAK_CHECK;
        MemoryUsage memoryUsage = {0, 0};
        GetMemoryUsage(&memoryUsage);
        if (memoryUsage.heapSize != s_lastMemoryUsage.heapSize) {
            PrintText(kTilt_Color_Yellow, "Heap usage changed by %ld (from %lu to %lu)\n",
                     LT_Ps32((s32)memoryUsage.heapSize - (s32)s_lastMemoryUsage.heapSize),
                     LT_Pu32(s_lastMemoryUsage.heapSize), LT_Pu32(memoryUsage.heapSize));
        } else if (s_bVerbose) {
            PrintText(kTilt_Color_Default, "Heap usage unchanged (%lu)\n", LT_Pu32(memoryUsage.heapSize));
        }
        if (memoryUsage.availableRAM != s_lastMemoryUsage.availableRAM) {
            PrintText(kTilt_Color_Yellow, "Available RAM changed by %ld (from %lu.%02lu to %lu.%02lu)\n",
                     LT_Ps32((s32)memoryUsage.availableRAM - (s32)s_lastMemoryUsage.availableRAM),
                     LT_Pu32(s_lastMemoryUsage.availableRAM >> 10), LT_Pu32(((s_lastMemoryUsage.availableRAM & 0x3ff) * 100) >> 10),
                     LT_Pu32(memoryUsage.availableRAM       >> 10), LT_Pu32(((memoryUsage.availableRAM       & 0x3ff) * 100) >> 10));
        } else if (s_bVerbose) {
            PrintText(kTilt_Color_Default, "Available RAM unchanged (%lu)\n", LT_Pu32(memoryUsage.availableRAM));
        }

        s_lastMemoryUsage = memoryUsage;
    }

    /* make sure post test prints are visible */
    LT_GetCore()->FlushConsoleOutput();

    s_runningTest = kInvalidTestIndex;
}

static void RunSingleTest(void * pClientData)
{
    LT_UNUSED(pClientData);
    LTThread hThread = s_pIThread->GetCurrentThread();
    const int testIndex = s_testsToRun[s_testsToRunIndex];
    const TiltTestSpecifier * const test = &s_pAllTests[testIndex];
    const char * const testName = test->pTestName;

    if (s_failureCount >= s_failureLimit) {
        if (s_bVerbose) {
            PrintText(kTilt_Color_Yellow, "Skipping test %s (too many failures)\n", testName);
        }
        goto proceed_to_next_test;
    }

    if (s_bAutomation && (test->testFlags & kTiltTestFlag_NoAutomation)) {
        if (s_bVerbose) {
            PrintText(kTilt_Color_Default, "Skipping test %s (not suitable for automation)\n", testName);
        }
        goto proceed_to_next_test;
    }

    if (!s_bRtos && (test->testFlags & kTiltTestFlag_NeedsRtos)) {
        if (s_bVerbose) {
            PrintText(kTilt_Color_Default, "Skipping test %s (needs an RTOS platform)\n", testName);
        }
        goto proceed_to_next_test;
    }

    if (test->testFlags & kTiltTestFlag_SectionHeader) {
        if (!s_bQuiet) {
            PrintText(kTilt_Color_Default, "\n--- %s ---\n\n", testName);
        }
        goto proceed_to_next_test;
    }

    s_pResults[testIndex] = kTilt_Result_Success;
    s_runningTest = testIndex;

    if (!s_bQuiet) {
        PrintText(kTilt_Color_Default, "Running test %s...\n", testName);
    }
    OpenJsonBlock("{");
    PrintJson("\"name\": \"%s\"", testName);
    OpenJsonBlock("\"events\" : [");

    if (s_bMemCheck) {
        VALGRIND_DO_QUICK_LEAK_CHECK;
        GetMemoryUsage(&s_lastMemoryUsage);
    }

    s_currentTestFailures = 0;
    s_executionState = kTiltEngineExecutionState_RunningTest;
    LT_GetCore()->FlushConsoleOutput(); /* make sure pre-test prints are visible */
    if (s_pSystemSettings) s_pSystemSettings->Flush(NULL); /* make sure settings flushed before test start, LT-2235 */
    const bool testResult = s_pUnitTest->RunTest(testIndex);
    LT_GetCore()->FlushConsoleOutput(); /* make sure test prints are visible */
    if (s_bDeferCompletion) {
        // Nothing to do until the test signals it's done
        return;
    } else {
        if (!testResult) {
            // TODO: Is this overly paranoid?
            if (s_pResults[testIndex] == kTilt_Result_Success) {
                // Test returned false but did not report an error; mark unknown.
                s_pResults[testIndex] = kTilt_Result_Error;
            }
        }
        CompleteSingleTest(testIndex);
    }

proceed_to_next_test:

    RunNextTest(hThread);
}

/**
 * Postlude to EndTesting() call - clean up state/output and then terminate this thread.
 * This is a separate method because it might be called from SignalCompletion()
 * if something beneath EndTesting() deferred completion.
 */
static void ContinueEndTesting(void * pClientData)
{
    LT_UNUSED(pClientData);
    if (s_bMemCheck) MemCheckEnd();

    s_executionState = kTiltEngineExecutionState_None;
    CloseJsonBlock("]");

    s_pIThread->Terminate(s_pIThread->GetCurrentThread());
}

/**
 * Run a single test from the test library.
 *
 * @param[in] hThread The handle of the current thread.
 */
static void RunNextTest(LTThread hThread)
{
    if (++s_testsToRunIndex < (s32)s_testsToRunCount) {
        s_bDeferCompletion = false;
        s_pIThread->QueueTaskProc(hThread, RunSingleTest, NULL, NULL);
    } else {
        // Last test has been run; complete testing and then shut down this testing thread
        s_executionState = kTiltEngineExecutionState_InEndTesting;
        if (!s_pUnitTest->EndTesting()) {
            ReportTiltEngineError("EndTesting() failed; results should still be valid");
            s_threadResult = kTiltEngine_InternalError;
        }
        if (s_bDeferCompletion) {
            // Nothing to do until EndTesting() signals it's done
            return;
        } else {
            ContinueEndTesting(NULL);
        }
    }
}

/**
 * Callback function used by tests to report a failure, along with
 * an (optional) message.
 *
 * @param[in] type The category of failure.
 * @param[in] pMessage Message accompanying the failure.  May be NULL.
 */
static void ReportFailureCallback(TiltResult type, const char * pMessage)
{
    if (type == kTilt_Result_None || type == kTilt_Result_Success) {
        ReportTiltEngineError("Invalid type in failure callback: %d", type);
    } else {
        if (s_runningTest >= s_allTestsCount) {
            s_resultOutsideOfTest = type;
        } else {
            s_pResults[s_runningTest] = type;
        }
        PrintText(kTilt_Color_Red, "  !!! %s: %s\n", ResultToString(type), pMessage ? pMessage : "(no message)");
        PrintJson("{ \"event\": \"%s\", \"name\": \"\", \"value\": \"%s\" }",
                  ResultToString(type), pMessage ? pMessage : "");
        ++s_failureCount;
        ++s_currentTestFailures;
    }
}

/**
 * Callback function used by tests to emit a message that's not part
 * of a failure.
 */
static void MessageCallback(TiltMessageLevel level, const char * pMessage)
{
    if (pMessage != NULL) {
        const char *levelString = "info";
        if (level == kTilt_Message_Level_Debug) {
            levelString = "debug";
        } else if (level == kTilt_Message_Level_Warning) {
            levelString = "warning";
        }
        else if (level == kTilt_Message_Level_Report) {
            levelString = "report";
        }

        bool doPrint;
        if (s_bQuiet) {
            doPrint = (level >= kTilt_Message_Level_Warning);
        } else {
            doPrint = (level >= kTilt_Message_Level_Info) || s_bVerbose;
        }
        if (doPrint) {
            PrintText(level != kTilt_Message_Level_Warning ? kTilt_Color_Default : kTilt_Color_Yellow, "  %s: %s\n", level == kTilt_Message_Level_Report ? "message" : levelString, pMessage);
            PrintJson("{ \"event\": \"message\", \"name\": \"\", \"value\": \"%s\", \"level\": \"%s\" }",
                      pMessage, levelString);
        }
    }
}

/**
 * Callback function used by tests to report an integer quantity.
 */
static void ReportIntegerCallback(const char * pName, s64 value, const char * pUnits)
{
    PrintText(kTilt_Color_Default, "  quantity: %s = %lld %s\n", pName, value, pUnits ? pUnits : "");
    PrintJson("{ \"event\": \"quantity\", \"name\": \"%s\", \"value\": \"%lld\", \"units\": \"%s\" }",
               pName, value, pUnits ? pUnits : "");
}

/**
 * Callback function used by tests to report a floating-point quantity.
 */
static void ReportFloatCallback(const char * pName, double value, const char * pUnits)
{
    PrintText(kTilt_Color_Default, "  quantity: %s = %g %s\n", pName, value, pUnits ? pUnits : "");
    PrintJson("{ \"event\": \"quantity\", \"name\": \"%s\", \"value\": \"%g\", \"units\": \"%s\" }",
              pName, value, pUnits ? pUnits : "");
}

/**
 * Callback function used by tests to report a buffer quantity.
 */
static void ReportBufferCallback(const char * pName, const u8 *buffer, u32 length)
{
    static const char *hexChars = "0123456789ABCDEF";
    u32 hexStrLen = length * 2 + 1;
    char *hexStr = lt_malloc(hexStrLen);
    if (!hexStr) return;
    for (u32 i = 0; i < length; ++i) {
        u8 byte = buffer[i];
        hexStr[i * 2]     = hexChars[byte >> 4];
        hexStr[i * 2 + 1] = hexChars[byte & 0xf];
    }
    hexStr[hexStrLen - 1] = 0;

    PrintText(kTilt_Color_Default, "  quantity: %s = %s\n", pName, hexStr);
    PrintJson("{ \"event\": \"quantity\", \"name\": \"%s\", \"value\": \"%s\" }",
              pName, hexStr);
    lt_free(hexStr);
}

/**
 * Proc called to indicate deferred test timeout.
 */
static void DeferredTestTimeoutProc(void * pClientData)
{
    LT_UNUSED(pClientData);
    const char * const testName =
        s_runningTest == kInvalidTestIndex ? "(none)" : s_pAllTests[s_runningTest].pTestName;
    char message[96];
    lt_snprintf(message, sizeof(message), "Timeout in test %s - terminating test run", testName);
    ReportFailureCallback(kTilt_Result_Failure, message);
    s_pIThread->Terminate(s_pIThread->GetCurrentThread());
}

/**
 * Callback function used by tests to indicate that they need to continue to run until
 * SignalCompletion() is called.
 */
static void DeferCompletionCallback(LTTime timeout)
{
    s_bDeferCompletion = true;
    s_pIThread->SetTimer(s_pIThread->GetCurrentThread(), timeout, DeferredTestTimeoutProc, NULL, NULL);
}

/**
 * Callback function used by asynchronously-running test (one which previously called
 * DeferCompletion()) to indicate that it's finally done.
 * This function must be called on the same thread that called DeferCompletion()
 * (which is the test thread).
 */
static void SignalCompletionCallback(const char * pTestName)
{
    const LTThread hThread = s_pIThread->GetCurrentThread();
    s_pIThread->KillTimer(hThread, DeferredTestTimeoutProc, NULL);

    if (!s_bDeferCompletion) {
        PrintText(kTilt_Color_Red, "Test signalled SignalCompletion without calling DeferCompletion\n");
    } else {
        s_bDeferCompletion = false;
        switch (s_executionState) {
            case kTiltEngineExecutionState_InBeginTesting:
                MessageCallback(kTilt_Message_Level_Debug, "Deferred processing in BeginTesting() complete");
                s_pIThread->QueueTaskProc(hThread, ContinueTestThread, NULL, NULL);
                break;
            case kTiltEngineExecutionState_InEndTesting:
                MessageCallback(kTilt_Message_Level_Debug, "Deferred processing in EndTesting() complete");
                s_pIThread->QueueTaskProc(hThread, ContinueEndTesting, NULL, NULL);
                break;
            case kTiltEngineExecutionState_RunningTest:
                if (!pTestName || lt_strcmp(pTestName, s_pAllTests[s_runningTest].pTestName)) {
                    PrintText(kTilt_Color_Red, "Test '%s' finishing out of sequence; aborting\n", pTestName);
                    s_pIThread->Terminate(hThread);
                } else {
                    const u32 testIndex = s_testsToRun[s_testsToRunIndex];
                    CompleteSingleTest(testIndex);
                    RunNextTest(hThread);
                }
                break;
            default:
                PrintText(kTilt_Color_Red, "Received SignalCompletion() in illegal state; aborting\n");
                s_pIThread->Terminate(hThread);
                break;
        }
    }
}

/**
 * Callback function used by tests to retrieve test properties.
 * Test properties are set on the command line as <propertyName>=<propertyValue>.
 * @see TiltCallbacks.GetProperty
 */
static const char * GetPropertyCallback(const char * pPropertyName, const char * pDefaultValue)
{
    for (PropertyCell * property = s_testProperties; property != NULL; property = property->next) {
        if (!lt_strcmp(property->name, pPropertyName)) {
            return property->value;
        }
    }
    return pDefaultValue;
}

/**
 * Load test library and get its list of tests.
 *
 * @returns the test library interface, or NULL if there was a failure.
 */
static ITilt * LoadTestLibrary(const char * pLibraryName)
{
    s_pTestLibraryName = pLibraryName;
    s_pTestLibrary = (LTLibrary *)LT_GetCore()->OpenLibrary(s_pTestLibraryName);
    if (s_pTestLibrary == NULL) {
        ReportTiltEngineError("Cannot load library %s", s_pTestLibraryName);
        return NULL;
    }
    ITilt * const pUnitTest = lt_getlibraryinterface(ITilt, s_pTestLibrary);
    if (pUnitTest == NULL) {
        /* Temporary hack to allow TiltEngine 2.0 to be invoked from TiltEngine 1.0 */
#if 0
        ReportTiltEngineError("Cannot get ITilt interface on library %s", s_pTestLibraryName);
#endif
        return NULL;
    }
    if (!pUnitTest->GetTestList(&s_pAllTests, &s_allTestsCount)) {
        ReportTiltEngineError("Failed to get test list on library %s", s_pTestLibraryName);
        return NULL;
    }

    return pUnitTest;
}


/**
 * Process command-line options.
 * Return the index to one past the last option.
 */
static int GatherOptions(int argc, const char ** argv)
{
    // Reset all option variables to their defaults
    s_bQuiet            = false;
    s_bVerbose          = false;
    s_bAutomation       = false;
    s_failureLimit      = 999999;
    s_failureCount      = 0;
    s_bEmitText         = true;
    s_bEmitJson         = false;
    s_bPrettyJson       = false;
    s_bMemCheck         = true;
    s_bRtos             = true;
    s_timeout           = LTTime_Zero();
    s_prependTime       = false;
    s_ppIncludeTests    = NULL;
    s_includeTestCount  = 0;
    s_ppExcludeTests    = NULL;
    s_excludeTestCount  = 0;
    s_jsonIndentLevel   = 0;
    s_jsonLastPrintLevel = 1;  // Prevent an opening comma

    int argIndex = 0;
    for (argIndex = 2; argIndex < argc; ++argIndex) {
        const char * const arg = argv[argIndex];
        if (!lt_strcmp(arg, "--list")) {
            return 0;
        }
        if (!lt_strcmp(arg, "--quiet")) {
            s_bQuiet = true;
        } else if (!lt_strcmp(arg, "--verbose")) {
            s_bVerbose = true;
        } else if (!lt_strcmp(arg, "--failure-limit")) {
            if (argIndex == argc - 1) {
                return -1;
            }
            s_failureLimit = lt_strtou32(argv[++argIndex], NULL, 10);
        } else if (!lt_strcmp(arg, "--json")) {
            s_bEmitText = false;
            s_bEmitJson = true;
        } else if (!lt_strcmp(arg, "--pretty")) {
            s_bPrettyJson = true;
        } else if (!lt_strcmp(arg, "--automation")) {
            s_bAutomation = true;
        } else if (!lt_strcmp(arg, "--timeout")) {
            if (argIndex == argc - 1) {
                return -1;
            }
            s_timeout = LTTime_Seconds(lt_strtou32(argv[++argIndex], NULL, 10));
        } else if (!lt_strcmp(arg, "--timestamp")) {
            s_prependTime = true;
        } else if (!lt_strcmp(arg, "--") || !lt_strcmp(arg, "--include-tests")) {
            // All remaining arguments are test names to include
            ++argIndex;
            s_ppIncludeTests   = argv + argIndex;
            s_includeTestCount = argc - argIndex;
            break;
        } else if (!lt_strcmp(arg, "--exclude-tests")) {
            // All remaining arguments are test names to exclude
            ++argIndex;
            s_ppExcludeTests   = argv + argIndex;
            s_excludeTestCount = argc - argIndex;
            break;
        } else if (!lt_strcmp(arg, "--nomemcheck")) {
            s_bMemCheck = false;
        } else if (!lt_strcmp(arg, "--notrtos")) {
            s_bRtos = false;
        } else if (!lt_strcmp(arg, "--stacksize")) {
            if (argIndex == argc - 1) {
                return -1;
            }
            s_testThreadStackSize = lt_strtou32(argv[++argIndex], NULL, 10);
        } else if (lt_strchr(arg, '=')) {
            const char * equalSign = lt_strchr(arg, '=');
            if (equalSign - arg >= kTilt_Property_Name_Length || equalSign == arg) {
                return -1;
            }
            const char * propertyValue = equalSign + 1;
            if (lt_strlen(propertyValue) >= kTilt_Property_Value_Length) {
                return -1;
            }
            PropertyCell * newProperty = lt_malloc(sizeof(PropertyCell));
            if (newProperty == NULL) {
                return -1;
            }
            lt_strncpyTerm(newProperty->name,  arg,           equalSign - arg + 1);
            lt_strncpyTerm(newProperty->value, propertyValue, kTilt_Property_Value_Length);
            newProperty->next = s_testProperties;
            s_testProperties = newProperty;

        } else {
            return -1;
        }
    }
    return argIndex;
}

/**
 * Allocate and load the s_testsToRun list.
 * This list contains the indices of all tests to run, in order.
 *
 * @return something other than kTiltEngine_Success if an error was encountered.
 */
static int GenerateTestList(void)
{
    int returnValue = kTiltEngine_Success;

    if (s_ppIncludeTests != NULL) {
        // User has specified a list of tests to run
        s_testsToRunCount = s_includeTestCount;
        s_testsToRun = lt_malloc(s_testsToRunCount * sizeof(s_testsToRun[0]));
        for (u32 i = 0; i < s_includeTestCount; ++i) {
            const char * const testName = s_ppIncludeTests[i];
            u32 testIndex = 0;
            while (testIndex < s_allTestsCount) {
                if (!lt_strcmp(testName, s_pAllTests[testIndex].pTestName)) {
                    s_testsToRun[i] = testIndex;
                    break;
                }
                ++testIndex;
            }
            if (testIndex >= s_allTestsCount) {
                ReportTiltEngineError("Invalid test name: '%s'", testName);
                returnValue = kTiltEngine_BadOptions;
            }
        }
    } else {
        // Execute all tests in default order
        s_testsToRun = lt_malloc(s_allTestsCount * sizeof(s_testsToRun[0]));
        s_testsToRunCount = 0;
        for (u32 testIndex = 0; testIndex < s_allTestsCount; ++testIndex) {
            bool bSkipTest = false;
            if (s_ppExcludeTests != NULL) {
                // User has specified a list of tests to skip
                for (u32 i = 0; i < s_excludeTestCount; ++i) {
                    const char * const testName = s_ppExcludeTests[i];
                    if (!lt_strcmp(testName, s_pAllTests[testIndex].pTestName)) {
                        bSkipTest = true;
                        break;
                    }
                }
            }

            if (!bSkipTest) {
                s_testsToRun[s_testsToRunCount++] = testIndex;
            }
        }
    }

    return returnValue;
}

static TiltCallbacks const s_testCallbacks = {
    .ReportFailure        = ReportFailureCallback,
    .Message              = MessageCallback,
    .ReportInteger        = ReportIntegerCallback,
    .ReportFloatingPoint  = ReportFloatCallback,
    .ReportBuffer         = ReportBufferCallback,
    .DeferCompletion      = DeferCompletionCallback,
    .SignalCompletion     = SignalCompletionCallback,
    .GetProperty          = GetPropertyCallback,
};

/**
 * Initialize testing state and call BeginTesting().
 */
static bool BeginTestThread(void)
{
    LTThread hThread = s_pIThread->GetCurrentThread();
    s_resultOutsideOfTest = kTilt_Result_None;
    s_executionState = kTiltEngineExecutionState_InBeginTesting;
    if (s_bMemCheck) MemCheckStart();
    if (!s_pUnitTest->BeginTesting(&s_testCallbacks)) {
        ReportTiltEngineError("Could not begin test run");
        s_threadResult = kTiltEngine_InternalError;
        s_pIThread->Terminate(hThread);
        return true; // augustus : soon get rid of above Terminate and just return false
    }

    if (s_bDeferCompletion) {
        /* A test hook called from BeginTesting() must have requested
         * deferred completion; just exit for now.  SignalCompletionCallback
         * should schedule ContinueTestThread when called.
         */
    } else {
        s_pIThread->QueueTaskProc(hThread, ContinueTestThread, NULL, NULL);
    }
    return true;
}

/**
 * Finish getting ready to run tests (after BeginTesting() was called)
 */
static void ContinueTestThread(void * pClientData)
{
    LT_UNUSED(pClientData);
    s_executionState = kTiltEngineExecutionState_None;

    if (s_resultOutsideOfTest != kTilt_Result_None) {
        // A test hook called from BeginTesting() signalled an error;
        // just leave all tests as skipped.
    } else {
        RunNextTest(s_pIThread->GetCurrentThread());
    }
}

/**
 * Initialize, run tests, and finalize.
 * Return kTiltEngine_Success if all tests could be run,
 * a kTiltEngine error code otherwise.
 */
static int RunTests(void)
{
    // Create and initialize results array

    s_pResults = lt_malloc(s_allTestsCount * sizeof(s_pResults[0]));
    if (s_pResults == NULL) {
        ReportTiltEngineError("Could not allocate results array");
        return kTiltEngine_InternalError;
    }
    for (u32 ii = 0; ii < s_allTestsCount; ++ii) {
        s_pResults[ii] = kTilt_Result_None;
    }

    // Generate list of tests to run
    const int generateResult = GenerateTestList();
    if (generateResult != kTiltEngine_Success) {
        return generateResult;
    }

    // Launch test thread and wait for it to finish

    int result = kTiltEngine_Success;

    LTThread hTestThread = LT_GetCore()->CreateThread("TiltTest");
    if (hTestThread == 0) {
        ReportTiltEngineError("Unable to create Tilt test thread");
        return kTiltEngine_InternalError;
    }
    s_pIThread->SetStackSize(hTestThread, s_testThreadStackSize);
    OpenJsonBlock("\"tests\": [");

    s_testsToRunIndex = -1;
    s_bDeferCompletion = false;
    s_threadResult = kTiltEngine_Success;
    s_pIThread->Start(hTestThread, BeginTestThread, NULL);
    if (!s_pIThread->WaitUntilFinished(hTestThread, s_timeout)) {
        s_pIThread->Terminate(hTestThread);
        s_pIThread->WaitUntilFinished(hTestThread, LTTime_Infinite());
        ReportFailureCallback(kTilt_Result_Failure, "Tests timed out");
        result = kTiltEngine_FailedTests;
    } else {
        result = s_threadResult;
    }
    s_pIThread->Destroy(hTestThread);

    return result;
}

/**
 * Print final report.
 * This includes number of tests executed/succeeded/failed,
 * and (optionally) a list of all failed tests.
 */
static void ReportResults(int result)
{
    // Calculate test counts

    u32 succeededTests = 0;
    u32 failedTests = 0;
    u32 unableToRunTests = 0;
    u32 skippedTests = 0;
    u32 totalTests = s_allTestsCount;
    for (u32 testIndex = 0; testIndex < s_allTestsCount; ++testIndex) {
        TiltResult const type = s_pResults[testIndex];
        if (s_pAllTests[testIndex].testFlags & kTiltTestFlag_SectionHeader) {
            // This "test" is really just a section header; ignore it
            --totalTests;
        } else if (type == kTilt_Result_None) {
            ++skippedTests;
        } else if (type == kTilt_Result_Failure) {
            ++failedTests;
        } else if (type != kTilt_Result_Success) {
            ++unableToRunTests;
        } else {
            ++succeededTests;
        }
    }

    // Now emit results

    PrintText(kTilt_Color_Default, "\n### Summary ###\n\n");
    PrintText(kTilt_Color_Default, "Total tests:         %d\n", totalTests);
    PrintText(kTilt_Color_Default, "Succeeded tests:     %d\n", succeededTests);
    PrintText(failedTests      > 0 ? kTilt_Color_Red    : kTilt_Color_Default, "Failed tests:        %d\n", failedTests);
    PrintText(unableToRunTests > 0 ? kTilt_Color_Yellow : kTilt_Color_Default, "Unable to run tests: %d\n", unableToRunTests);
    PrintText(skippedTests     > 0 ? kTilt_Color_Yellow : kTilt_Color_Default, "Skipped tests:       %d\n", skippedTests);
    if (!s_bQuiet) {
        if (failedTests > 0) {
            // List all failed tests, one per line
            PrintText(kTilt_Color_Red, "\n### Failed tests ###\n\n");
            for (u32 testIndex = 0; testIndex < s_allTestsCount; ++testIndex) {
                if (s_pResults[testIndex] == kTilt_Result_Failure) {
                    PrintText(kTilt_Color_Red, "%s\n", s_pAllTests[testIndex].pTestName);
                }
            }
            PrintText(kTilt_Color_Red, "\n");
        } else if (unableToRunTests > 0) {
            PrintText(kTilt_Color_Yellow, "\n### Not all tests were run ###\n\n");
        } else {
            if (result == kTiltEngine_Success) {
                PrintText(kTilt_Color_Green, "\n### PASSED ###\n\n");
            } else {
                PrintText(kTilt_Color_Red, "\n### FAILED ###\n\n");
            }
        }

        if (skippedTests > 0) {
            // List all skipped tests, one per line
            PrintText(kTilt_Color_Yellow, "### Skipped tests ###\n\n");
            for (u32 testIndex = 0; testIndex < s_allTestsCount; ++testIndex) {
                if (s_pResults[testIndex] == kTilt_Result_None &&
                    !(s_pAllTests[testIndex].testFlags & kTiltTestFlag_SectionHeader)) {
                    PrintText(kTilt_Color_Yellow, "%s\n", s_pAllTests[testIndex].pTestName);
                }
            }
            PrintText(kTilt_Color_Yellow, "\n");
        }
    }

    OpenJsonBlock("\"results\": {");
    PrintJson("\"total-tests\": %d", s_allTestsCount);
    PrintJson("\"failures\": %d", failedTests);
    PrintJson("\"unable-to-run\": %d", unableToRunTests);
    PrintJson("\"skipped\" : %d", skippedTests);
    CloseJsonBlock("}");
}

static int RunTiltCore(const char * libraryName, bool listTests)
{
    // Load the test library and obtain its list of tests.
    // (Note that library will be unloaded by the caller.)

    s_pUnitTest = LoadTestLibrary(libraryName);
    if (s_pUnitTest == NULL) {
        return kTiltEngine_LoadLibraryFailure;
    }

    if (listTests) {
        // The user specified --list on the command line; just print the tests and return.
        ListAllTests();
        return kTiltEngine_Success;
    }

    /* Initialize library settings with defaults, then call the library to
     * override whichever values it wishes.
     */
    TiltTestLibrarySettings libSettings = { .defaultTimeout = kTiltEngineDefaultTimeout };
    s_pUnitTest->GetLibrarySettings(&libSettings);
    if (LTTime_IsZero(s_timeout)) {
        // Timeout was not specified on command line; use library setting
        s_timeout = LTTime_Seconds(libSettings.defaultTimeout);
    }
    s_pSystemSettings = lt_openlibrary(LTSystemSettings);
    GetMemoryUsage(&s_lastMemoryUsage);
    const int result = RunTests();

    // End test sequence and report results

    ReportResults(result);
    if (s_pSystemSettings) {
        s_pSystemSettings->Flush(NULL); /* one final flush after last test in sequence, LT-2235 */
        lt_closelibrary(s_pSystemSettings);
        s_pSystemSettings = NULL;
    }
    return result;
}

static int TiltEngineImpl_Run(int argc, const char ** argv)
{
    /**
     * When the library is executed via ltrun, argv[0] is the name
     * of the platform, with argv[1] being the name of the library, and
     * command-line arguments beginning at argv[2].  This is counter
     * to normal practice, so here we adjust argc and argv to skip
     * over that initial platform name.
     */
    --argc;
    ++argv;

    PrintText(kTilt_Color_Default, "\n");   // Start with newline to clean up ltrun output

    const char * const programName = argv[0] ? argv[0] : "TiltEngine";
    if (argc < 2) {
        DisplayUsage(programName);
        return kTiltEngine_BadOptions;
    }

    bool listTests = false;
    int argIndex = GatherOptions(argc, argv);

    switch (argIndex) {
        case -1:
            DisplayUsage(programName);
            return kTiltEngine_BadOptions;
        case 0:
            listTests = true;
            break;
        default:
            break;
    }

    OpenJsonBlock("{");

    // Run the core testing logic
    int coreResult = RunTiltCore(argv[1], listTests);

    // Temporary hack to allow TiltEngine 2.0 to be invoked from TiltEngine 1.0
    if (coreResult == kTiltEngine_LoadLibraryFailure && s_pTestLibrary) {
        if (s_pTestLibrary->Run == NULL) {
            ReportTiltEngineError("%s has no Run() function AND no ITilt interface exists\n", argv[1]);
        } else {
            // 4k should be plenty of stack, so just run it.
            coreResult = s_pTestLibrary->Run(argc, argv);
        }
    }

    OpenJsonBlock("\"post-testing-events\": [");

    // Clean up test library and test lists

    LT_GetCore()->CloseLibrary(s_pTestLibrary);
    s_pTestLibrary = NULL;

    lt_free(s_testsToRun);
    s_testsToRun = NULL;
    s_testsToRunCount = 0;
    lt_free(s_pResults);
    s_pResults = NULL;

    while (s_testProperties != NULL) {
        PropertyCell * next = s_testProperties->next;
        lt_free(s_testProperties);
        s_testProperties = next;
    }

    CloseJsonBlock("]");
    CloseJsonBlock("}");
    s_pUnitTest = NULL;

    return coreResult != kTiltEngine_Success ? coreResult :
                         s_failureCount > 0  ? kTiltEngine_FailedTests :
                                               kTiltEngine_Success;
}

static bool TiltEngineImpl_LibInit(void)
{
    s_pIThread     = lt_getlibraryinterface(ILTThread, LT_GetCore());
    if (!s_pIThread) {
        return false;
    }
    return true;
}

static void TiltEngineImpl_LibFini(void)
{
}

/*******************************************************************************
 * Library initialization
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(TiltEngine, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(TiltEngine, TiltEngineImpl_Run, 4096) LTLIBRARY_DEFINITION;
