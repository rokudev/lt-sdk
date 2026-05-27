/*******************************************************************************
 * source/unittest/lt/utility/tokenizer/UnitTestLTUtilityTokenizer.c
 *    __  __      _ __ ______          __  __  ________  ____  _ ___ __
 *   / / / /___  (_) //_  __/__  _____/ /_/ / /_  __/ / / / /_(_) (_) /___  __
 *  / / / / __ \/ / __// / / _ \/ ___/ __/ /   / / / / / / __/ / / / __/ / / /
 * / /_/ / / / / / /_ / / /  __(__  ) /_/ /___/ / / /_/ / /_/ / / / /_/ /_/ /
 * \____/_/ /_/_/\__//_/  \___/____/\__/_____/_/  \____/\__/_/_/_/\__/\__, /
 *                                                                   /____/
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTArray.h>
#include <lt/utility/tokenizer/LTUtilityTokenizer.h>
#include <tilt/JiltEngine.h>

/*_____________________________________________
 / UnitTestLTUtilityTokenizer static variables */

static LTUtilityTokenizer * s_pUtilityTokenizer = NULL;
static LTArray            * s_pTokenArray       = NULL;
static JiltEngine         * s_engine;

/*____________________________________________________
 / UnitTestLTUtilityTokenizer static helper functions */

static bool ProcessToken(LTTokenizer * pTok, const char * pToken, char matchedSentinel, void * pClientData) {
    LT_UNUSED(pTok);
    LT_UNUSED(matchedSentinel);
    LT_UNUSED(pClientData);
    s_pTokenArray->API->Append(s_pTokenArray, lt_strdup(pToken));
    return lt_strcmp("stop", pToken) != 0;
}

static void ClearTokens(void) {
    for (u32 i = 0; i < s_pTokenArray->API->GetCount(s_pTokenArray); ++i) {
        lt_free((char *)s_pTokenArray->API->Get(s_pTokenArray, i, NULL));
    }
    s_pTokenArray->API->SetCount(s_pTokenArray, 0);
}

static bool VerifyTokens(Tilt *tilt, const char * name, u32 numTokens, ...) {
    if (numTokens != s_pTokenArray->API->GetCount(s_pTokenArray)) {
        return false;
    }
    bool result = true;
    lt_va_list args;
    lt_va_start(args, numTokens);
    for (u32 i = 0; i < numTokens; ++i) {
        char * pToken = lt_va_arg(args, char *);
        if (0 != lt_strcmp(pToken, s_pTokenArray->API->Get(s_pTokenArray, i, NULL))) {
            TILT_DEBUG(tilt, "%s.expected: %s", name, pToken);
            TILT_DEBUG(tilt, "%s.actual: %s", name, s_pTokenArray->API->Get(s_pTokenArray, i, NULL));
            result = false;
            break;
        }
    }
    lt_va_end(args);
    ClearTokens();
    return result;
}

static bool PushRawString(LTTokenizer * pTok, const char * pInputString, s32 expectedBytesConsumed, bool bMoreDataToCome) {
    if (expectedBytesConsumed < 0) expectedBytesConsumed = lt_strlen(pInputString);
    u32 bytesConsumed = s_pUtilityTokenizer->ProcessData(pTok, (u8*)pInputString, lt_strlen(pInputString), bMoreDataToCome);
    return (bytesConsumed == (u32)expectedBytesConsumed);
}

/*____________________________________________________________
 / UnitTestLTUtilityTokenizer() tests */

static void CleanUpTest(LTTokenizer * pTok) {
    ClearTokens();
    s_pUtilityTokenizer->DestroyTokenizer(pTok);
}

static void TestMultipleTokens(Tilt *tilt) {
    LTTokenizer *pTok = s_pUtilityTokenizer->CreateTokenizer(" ", ProcessToken, NULL);
    TILT_ASSERT_TRUE(tilt, pTok, "NULL tokenizer");

    if (!PushRawString(pTok, "this is a bunch of tokens", -1, false)) {
        TILT_REPORT_FAILURE(tilt, "PushRawString failed");
    } else if (!VerifyTokens(tilt, "MultipleTokens", 6, "this", "is", "a", "bunch", "of", "tokens")) {
        TILT_REPORT_FAILURE(tilt, "VerifyTokens failed");
    }

    CleanUpTest(pTok);
}

static void TestChangeSentinel(Tilt *tilt) {
    LTTokenizer *pTok = s_pUtilityTokenizer->CreateTokenizer(" ", ProcessToken, NULL);
    TILT_ASSERT_TRUE(tilt, pTok, "NULL tokenizer");

    s_pUtilityTokenizer->SetSentinels(pTok, ";");
    if (!PushRawString(pTok, "this is one token;", -1, false)) {
        TILT_REPORT_FAILURE(tilt, "PushRawString failed");
    } else if (!VerifyTokens(tilt, "ChangeSentinel", 1, "this is one token")) {
        TILT_REPORT_FAILURE(tilt, "VerifyTokens failed");
    }

    CleanUpTest(pTok);
}

static void TestWhitespaceStripping(Tilt *tilt) {
    LTTokenizer *pTok = s_pUtilityTokenizer->CreateTokenizer(" ", ProcessToken, NULL);
    TILT_ASSERT_TRUE(tilt, pTok, "NULL tokenizer");

    s_pUtilityTokenizer->SetSentinels(pTok, ";");
    if (!PushRawString(pTok, "\r\n   token surrounded by whitespace    \r\n;", -1, false)) {
        TILT_REPORT_FAILURE(tilt, "PushRawString failed");
    } else if (!VerifyTokens(tilt, "WhitespaceStripping", 1, "token surrounded by whitespace")) {
        TILT_REPORT_FAILURE(tilt, "VerifyTokens failed");
    }

    CleanUpTest(pTok);
}

static void TestAccumulateSeveral(Tilt *tilt) {
    LTTokenizer *pTok = s_pUtilityTokenizer->CreateTokenizer(" ", ProcessToken, NULL);
    TILT_ASSERT_TRUE(tilt, pTok, "NULL tokenizer");

    s_pUtilityTokenizer->SetSentinels(pTok, "!");
    if (!PushRawString(pTok, "not a token yet... ", -1, true)) {
        TILT_REPORT_FAILURE(tilt, "First PushRawString failed");
    } else if (!PushRawString(pTok, "wait for it... ", -1, true)) {
        TILT_REPORT_FAILURE(tilt, "Second PushRawString failed");
    } else if (!PushRawString(pTok, "go!", -1, false)) {
        TILT_REPORT_FAILURE(tilt, "Third PushRawString failed");
    } else if (!VerifyTokens(tilt, "AccumulateSeveral", 1, "not a token yet... wait for it... go")) {
        TILT_REPORT_FAILURE(tilt, "VerifyTokens failed");
    }

    CleanUpTest(pTok);
}

static void TestOneTokenNoSentinels(Tilt *tilt) {
    LTTokenizer *pTok = s_pUtilityTokenizer->CreateTokenizer("~", ProcessToken, NULL);
    TILT_ASSERT_TRUE(tilt, pTok, "NULL tokenizer");

    if (!PushRawString(pTok, "This is one token with no sentinel", -1, false)) {
        TILT_REPORT_FAILURE(tilt, "PushRawString failed");
    } else if (!VerifyTokens(tilt, "TestOneTokenNoSentinels", 1, "This is one token with no sentinel")) {
        TILT_REPORT_FAILURE(tilt, "VerifyTokens failed");
    }

    CleanUpTest(pTok);
}

static void TestEarlyReturn(Tilt *tilt) {
    LTTokenizer *pTok = s_pUtilityTokenizer->CreateTokenizer(" ", ProcessToken, NULL);
    TILT_ASSERT_TRUE(tilt, pTok, "NULL tokenizer");

    s_pUtilityTokenizer->SetSentinels(pTok, " ");
    if (!PushRawString(pTok, "when I say stop ignore the rest", 16, false)) {
        TILT_REPORT_FAILURE(tilt, "PushRawString failed");
    } else if (!VerifyTokens(tilt, "EarlyReturn.2", 4, "when", "I", "say", "stop")) {
        TILT_REPORT_FAILURE(tilt, "VerifyTokens failed");
    }

    CleanUpTest(pTok);
}

static void TestRepeatedSentinels(Tilt *tilt) {
    LTTokenizer *pTok = s_pUtilityTokenizer->CreateTokenizer(" ", ProcessToken, NULL);
    TILT_ASSERT_TRUE(tilt, pTok, "NULL tokenizer");

    s_pUtilityTokenizer->SetSentinels(pTok, " ");
    if (!PushRawString(pTok, "repeated  sentinels  empty  tokens  ", -1, false)) {
        TILT_REPORT_FAILURE(tilt, "PushRawString failed");
    } else if (!VerifyTokens(tilt, "RepeatedSentinels.2", 8, "repeated", "", "sentinels", "", "empty", "", "tokens", "")) {
        TILT_REPORT_FAILURE(tilt, "VerifyTokens failed");
    }

    CleanUpTest(pTok);
}

static void TestMultipleSentinelChars(Tilt *tilt) {
    LTTokenizer *pTok = s_pUtilityTokenizer->CreateTokenizer(" ", ProcessToken, NULL);
    TILT_ASSERT_TRUE(tilt, pTok, "NULL tokenizer");

    s_pUtilityTokenizer->SetSentinels(pTok, "!@#$%^&*");
    if (!PushRawString(pTok, "any@sentinel#will$do%really^just&try*me!", -1, false)) {
        TILT_REPORT_FAILURE(tilt, "PushRawString failed");
    } else if (!VerifyTokens(tilt, "MultipleSentinelChars.2", 8, "any", "sentinel", "will", "do", "really", "just", "try", "me")) {
        TILT_REPORT_FAILURE(tilt, "VerifyTokens failed");
    }

    CleanUpTest(pTok);
}

static void BeforeAllTests(Tilt *tilt) {
    s_pTokenArray = lt_createobject(LTArray);
    TILT_EXPECT_TRUE(tilt, s_pTokenArray != NULL, "Can't create array");
    
    s_pUtilityTokenizer = lt_openlibrary(LTUtilityTokenizer);
    TILT_EXPECT_TRUE(tilt, s_pUtilityTokenizer != NULL, "Can't open library under test");
    /* AfterAllTests is always run for clean up */
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_destroyobject(s_pTokenArray);
    lt_closelibrary(s_pUtilityTokenizer);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

/*___________________________________________________
 / UnitTestLTUtilityTokenizer library initialization */

static const TiltEngineTest s_tests[] = {
    { TestMultipleTokens,        "MultipleTokens",          "Parse multiple tokens",               0 },
    { TestChangeSentinel,        "ChangeSentinel",          "Change sentinel",                     0 },
    { TestWhitespaceStripping,   "WhitespaceStripping",     "Whitespace stripping",                0 },
    { TestAccumulateSeveral,     "AccumulateSeveral",       "Accumulate token over several calls", 0 },
    { TestOneTokenNoSentinels,   "TestOneTokenNoSentinels", "Pass input to output",                0 },
    { TestEarlyReturn,           "EarlyReturn",             "Early return",                        0 },
    { TestRepeatedSentinels,     "RepeatedSentinels",       "Repeated sentinels",                  0 },
    { TestMultipleSentinelChars, "MultipleSentinelChars",   "Multiple sentinel characters",        0 },
};

/*___________________________________________________________
 / UnitTestLTUtilityTokenizer library root interface binding */

static int UnitTestLTUtilityTokenizerImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTUtilityTokenizerImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTUtilityTokenizerImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityTokenizer, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityTokenizer, UnitTestLTUtilityTokenizerImpl_Run, 1536) LTLIBRARY_DEFINITION;
