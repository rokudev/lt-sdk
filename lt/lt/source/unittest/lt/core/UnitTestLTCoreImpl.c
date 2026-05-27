/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreImpl.c>
 *    __  __      _ __ ______          __  __  ____________
 *   / / / /___  (_) //_  __/__  _____/ /_/ / /_  __/ ____/___  ________
 *  / / / / __ \/ / __// / / _ \/ ___/ __/ /   / / / /   / __ \/ ___/ _ \
 * / /_/ / / / / / /_ / / /  __(__  ) /_/ /___/ / / /___/ /_/ / /  /  __/
 * \____/_/ /_/_/\__//_/  \___/____/\__/_____/_/  \____/\____/_/   \___/
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <tilt/JiltEngine.h>

static JiltEngine *s_engine;

/*******************************************************  Unit-test functions */
void TestAlloc(          Tilt * tilt),
     TestConsolePrint(   Tilt * tilt),
     TestKernelTime(     Tilt * tilt),
     TestSetUTCTime(     Tilt * tilt),
     TestLTMutex(        Tilt * tilt),
     TestLTThread(       Tilt * tilt),
     TestThreadPriority( Tilt * tilt),
     TestThreadWait(     Tilt * tilt),
     TestTimer(          Tilt * tilt),
     TestTimerPrecision( Tilt * tilt),
     TestLTEvent(        Tilt * tilt),
     TestReAlloc(        Tilt * tilt),
     Testmemcmp(         Tilt * tilt),
     Testmemcpy(         Tilt * tilt),
     Testmemmove(        Tilt * tilt),
     Testmemset(         Tilt * tilt),
     TestmemsetInt(      Tilt * tilt),
     Testqsort(          Tilt * tilt),
     Testbsearch(        Tilt * tilt),
     TestbsearchIndex(   Tilt * tilt),
     Testisalnum(        Tilt * tilt),
     Testisxdigit(       Tilt * tilt),
     Testisspace(        Tilt * tilt),
     Testisupper(        Tilt * tilt),
     Testislower(        Tilt * tilt),
     Testhextobyte(      Tilt * tilt),
     Teststrempty(       Tilt * tilt),
     Teststrlen(         Tilt * tilt),
     Teststrcmp(         Tilt * tilt),
     Teststrcasecmp(     Tilt * tilt),
     Teststrncmp(        Tilt * tilt),
     Teststrncasecmp(    Tilt * tilt),
     Teststrchr(         Tilt * tilt),
     Teststrrchr(        Tilt * tilt),
     Teststrstr(         Tilt * tilt),
     Teststrendswith(    Tilt * tilt),
     Teststrdup(         Tilt * tilt),
     Teststrupper_lower( Tilt * tilt),
     TeststrncpyTerm(    Tilt * tilt),
     TeststrncatTerm(    Tilt * tilt),
     Testsnprintf(       Tilt * tilt),
     Testu32toString(    Tilt * tilt),
     TestLTString(       Tilt * tilt),
     Teststrtod(         Tilt * tilt),
     Teststrtos32(       Tilt * tilt),
     Teststrtos64(       Tilt * tilt),
     Teststrtou32(       Tilt * tilt),
     Teststrtou64(       Tilt * tilt),
     Testsqrtf(          Tilt * tilt),
     TestLT_CLIP(        Tilt * tilt),
     TestReleaseClientData(Tilt * tilt);

static const TiltEngineTest s_tests[] = {
    { TestAlloc,           "Alloc",              "Memory allocation and free",                                      0 },
    { TestConsolePrint,    "ConsolePrint",       "Print a message to the LTCore console",                           0 },
    { TestKernelTime,      "KernelTime",         "GetKernelTime and time measurement",                              0 },
    { TestSetUTCTime,      "SetUTCTime",         "Set UTC time",                                                    0 },
    { TestLTMutex,         "LTMutex",            "LTMutex - lock, unlock, trylock",                                 0 },
    { TestLTThread,        "LTThread",           "LTThread",                                                        0 },
    { TestThreadPriority,  "ThreadPriority",     "LTThread preemption and priority",                                0 },
    { TestThreadWait,      "ThreadWait",         "LTThread wait until finished",                                    0 },
    { TestTimer,           "Timer",              "LTThread timer tests",                                            0 },
    { TestTimerPrecision,  "TimerPrecision",     "LTThread timer precision test",                                   0 },
    { TestLTEvent,         "LTEvent",            "LTEvent - create, register, notify",                              0 },
    { TestReAlloc,         "ReAlloc",            "Memory reallocation",                                             0 },
    { Testmemcmp,          "memcmp",             "Memory comparison",                                               0 },
    { Testmemcpy,          "memcpy",             "Memory copying",                                                  0 },
    { Testmemmove,         "memmove",            "Memory move",                                                     0 },
    { Testmemset,          "memset",             "Memory set)",                                                     0 },
    { TestmemsetInt,       "memsetInt",          "Memory set integer",                                              0 },
    { Testqsort,           "qsort",              "Data sorting",                                                    0 },
    { Testbsearch,         "bsearch",            "Sorted data searching",                                           0 },
    { TestbsearchIndex,    "bsearchIndex",       "Sorted data searching",                                           0 },
    { Testisalnum,         "isalnum",            "Check whether given character is alphanumeric",                   0 },
    { Testisxdigit,        "isxdigit",           "Check whether given character is a hex character",                0 },
    { Testisspace,         "isspace",            "Check whether given character is a space character",              0 },
    { Testisupper,         "isupper",            "Check whether given character is uppercase",                      0 },
    { Testislower,         "islower",            "Check whether given character is lowercase",                      0 },
    { Testhextobyte,       "hextobyte",          "Hex to byte conversion",                                          0 },
    { Teststrempty,        "strempty_isempty",   "Empty a string",                                                  0 },
    { Teststrlen,          "strlen",             "String length measurement",                                       0 },
    { Teststrcmp,          "strcmp",             "String comparison",                                               0 },
    { Teststrcasecmp,      "strcasecmp",         "Case-insensitive string compare",                                 0 },
    { Teststrncmp,         "strncmp",            "Fixed-length string compare",                                     0 },
    { Teststrncasecmp,     "strncasecmp",        "Fixed-length, case-insensitive string compare",                   0 },
    { Teststrchr,          "strchr",             "Character search in string",                                      0 },
    { Teststrrchr,         "strrchr",            "Reverse character search in string",                              0 },
    { Teststrstr,          "strstr",             "String search in string",                                         0 },
    { Teststrendswith,     "strendswith",        "Check string suffix",                                             0 },
    { Teststrdup,          "strdup",             "String duplication",                                              0 },
    { Teststrupper_lower,  "strupper_lower",     "Convert string to upper and/or lower case",                       0 },
    { TeststrncpyTerm,     "strncpyTerm",        "String copy with terminator",                                     0 },
    { TeststrncatTerm,     "strncatTerm",        "String concatenation with terminator",                            0 },
    { Testsnprintf,        "snprintf",           "Formatted print",                                                 0 },
    { Testu32toString,     "u32toString",        "Formatted print of u32 numbers",                                  0 },
    { TestLTString,        "LTString",           "LTString functions",                                              0 },
    { Teststrtod,          "strtod",             "String to floating-point conversion",                             0 },
    { Teststrtos32,        "strtos32",           "String to signed 32-bit integer conversion",                      0 },
    { Teststrtos64,        "strtos64",           "String to signed 64-bit integer conversion",                      0 },
    { Teststrtou32,        "strtou32",           "String to unsigned 32-bit integer conversion",                    0 },
    { Teststrtou64,        "strtou64",           "String to unsigned 64-bit integer conversion",                    0 },
    { Testsqrtf,           "sqrtf",              "Floating point square root()",                                    0 },
    { TestLT_CLIP,         "LT_CLIP",            "LT_CLIP macro",                                                   0 },
    { TestReleaseClientData,"ReleaseClientData", "Tests ClientDataReleaseProc is called for all reasons",           0 },
};

/*******************************************************  Hook functions */

static int UnitTestLTCoreImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), NULL);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTCoreImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return (s_engine != NULL);
}

static void UnitTestLTCoreImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTCore, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTCore, UnitTestLTCoreImpl_Run, 1536) LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  04-Jul-19   constantine created
 *  11-Jul-19   augustus    LTTime integer comparison operators are now private
 *  21-Jul-19   augustus    changed run() to Run() to match LTLibraryInterface;
 *                          got rid of autorun thread hack
 *  06-Aug-19   constantine broke NoFault into multiple files
 *  23-Dec-19   constantine changed name from LTTestNoFault to LTUnitTestCore
 *  16-Jul-20   constantine converted to C
 *  25-Oct-20   augustus    added TestLTTimeZone
 *  06-Dec-20   constantine reworked printf formatting
 *  07-Feb-21   constantine added --disable option
 *  19-Oct-21   constantine moved to UnitTestLTCore
 *  20-Jul-22   constantine reworked for TILT
 *  22-Dec-22   augustus    moved TimeZone tests to UnitTestLTSystemTimeZone
 *  31-Mar-24   augustus    added tests for strncatTerm, bsearch, and bsearchIndex
 *  09-Dec-24   commodus    added tests for u32toString
 */
