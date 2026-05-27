/*******************************************************************************
 * source/unittest/lt/net/backoff/UnitTestLTNetBackoff.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/net/backoff/LTNetBackoff.h>

#include <tilt/JiltEngine.h>

#define COUNTOF(x)   (sizeof(x)/sizeof(x[0]))

#define NUM_SAMPLES  50000
#define NUM_BINS     10

typedef struct {
    const char *testName;
    // Delays will be interpreted in milliseconds
    u32         minDelay;
    u32         maxInitialDelay;
    u32         s_maxDelay;
    u32         maxAttempts;
} TestBackoffParams;

static JiltEngine   *s_engine;
static LTNetBackoff *s_backoff;
static LTTime        s_minDelay;
static LTTime        s_maxInitialDelay;
static LTTime        s_maxDelay;
static u32           s_maxAttempts;

/*___________________
  Helper functions */

static void AssertTenBinUniformity(Tilt *tilt, u32 counts[NUM_BINS], double expectedCountsPerBin) {
    // Calculated the expected count per bin
    // const double expectedCountsPerBin = NUM_SAMPLES / NUM_BINS;
    double chiSquare = 0.0;
    for (u32 i = 0; i < NUM_BINS; i++) {
        // Chi-Square: Sum of squares of differences between observed and expected counts
        double diff = counts[i] - expectedCountsPerBin;
        TILT_INFO(tilt, "bin: %lu, count: %lu, diff: %f", LT_Pu32(i), LT_Pu32(counts[i]), diff);
        chiSquare += (diff * diff) / expectedCountsPerBin;
    }
    TILT_INFO(tilt, "Chi-square value: %f", chiSquare);

    // This value is taken from a chi-square distribution table.
    // 9 degrees of freedom (10 bins - 1), 0.005 significance (0.5% risk that the distribution is not uniform when it actually is)
    const double criticalValue = 23.589;
    // If chiSquare <= critical value, then the distribution is likely uniform (*fail* to reject null hypothesis that it is uniform)
    if (chiSquare <= criticalValue) {
        TILT_INFO(tilt, "Chi-square value (%f) below critical value, distribution is likely uniform", chiSquare);
    } else {
        // Warn but not fail since it is possible for distribution to be close to but not quite uniform
        TILT_WARNING(tilt, "Chi-square value (%f) is greater than critical value, distribution may not be uniform!", chiSquare);
        // Only fail if critical value is exceeded by a very large margin
        TILT_ASSERT_TRUE(tilt, chiSquare <= criticalValue * 2, "Chi-square value (%f) is greater than twice the critical value, distribution is not uniform", chiSquare);
    }
}

/*______________________
  Test implementation */

static void TestRandomBetweenUniformity(Tilt *tilt) {
    u32 counts[NUM_BINS] = {0};  // initialize all counts with 0

    // Generate and track the number of samples per bin
    for (u32 i = 0; i < NUM_SAMPLES; i++) {
        LTTime generatedNumber = s_backoff->API->GetRandomBackoff(s_backoff);
        TILT_ASSERT_TRUE(tilt, LTTime_IsLessThanOrEqual(generatedNumber, s_maxDelay), "Generated number (%lu) is not within expected range (%lu - %lu)",
                         LT_Pu32(LTTime_GetMilliseconds(generatedNumber)), LT_Pu32(LTTime_GetMilliseconds(s_maxInitialDelay)), LT_Pu32(LTTime_GetMilliseconds(s_maxDelay)));
        u32 binOfGeneratedNumber = ((u32)LTTime_GetMilliseconds(generatedNumber)) % NUM_BINS;
        ++counts[binOfGeneratedNumber];
    }

    AssertTenBinUniformity(tilt, counts, NUM_SAMPLES / NUM_BINS);
}

static void TestBackoffRange(Tilt *tilt) {
    // EXAMPLE: maxInitial = 1000; maxDelay = 16000;
    //          Delay progression: 1000 -> 2000 -> 4000 -> 8000 -> 16000
    TILT_ASSERT_TRUE(tilt, s_maxAttempts <= 5, "Too many max attempts configured");

    // countsPerAttempt[s_maxAttempts][NUM_BINS]
    u32 *countsPerAttempt = lt_malloc(sizeof(*countsPerAttempt) * s_maxAttempts * NUM_BINS);
    TILT_ASSERT_TRUE(tilt, countsPerAttempt != NULL, "Could not allocate memory for test array");
    // Init all values to 0
    lt_memset(countsPerAttempt, 0, sizeof(*countsPerAttempt) * s_maxAttempts * NUM_BINS);

    // Gather enough samples to make sure every attempt is uniformly distributed in the correct range
    for (u32 sample = 0; sample < NUM_SAMPLES; sample++) {
        // Make sure every s_backoff is within range
        LTTime prevMaxDelay = s_maxInitialDelay;
        for (u32 i = 0; i < s_maxAttempts; i++) {
            // Ensure starting max is correct
            if (i == 0) {
                LTTime firstMaxDelay = s_backoff->API->GetNextAttemptMaxDelay(s_backoff);
                TILT_ASSERT_TRUE(tilt, LTTime_IsEqual(firstMaxDelay, prevMaxDelay), "Before first s_backoff, first max delay (%lu) is not equal to initial max delay (%lu)",
                                 LT_Pu32(LTTime_GetMilliseconds(firstMaxDelay)), LT_Pu32(LTTime_GetMilliseconds(prevMaxDelay)));
            }

            // Get the next s_backoff
            LTTime delay = s_backoff->API->GetNextBackoff(s_backoff);
            LTTime nextAttemptMaxDelay = s_backoff->API->GetNextAttemptMaxDelay(s_backoff);

            // Ensure max delay is scaling exponentially until the max
            TILT_ASSERT_TRUE(tilt, LTTime_IsEqual(nextAttemptMaxDelay, LTTime_IsEqual(s_maxDelay, prevMaxDelay) ? s_maxDelay : LTTime_Add(prevMaxDelay, prevMaxDelay)),
                             "(Attempt %lu) Current max delay (%lu) is not double the previous max delay (%lu)",
                             LT_Pu32(i), LT_Pu32(LTTime_GetMilliseconds(nextAttemptMaxDelay)), LT_Pu32(LTTime_GetMilliseconds(prevMaxDelay)));
            prevMaxDelay = nextAttemptMaxDelay;

            // Ensure generated delay is within range
            TILT_ASSERT_TRUE(tilt, LTTime_IsLessThanOrEqual(delay, nextAttemptMaxDelay), "(Attempt %lu) Delay (%lu) is greater than current max delay (%lu)",
                             LT_Pu32(i), LT_Pu32(LTTime_GetMilliseconds(delay)), LT_Pu32(LTTime_GetMilliseconds(nextAttemptMaxDelay)));

            // Count the distributions for every attempt to ensure every attempt uniformly spans 0 to nextAttemptMaxDelay
            u32 binOfGeneratedNumber = ((u32)LTTime_GetMilliseconds(delay)) % NUM_BINS;
            ++countsPerAttempt[i*NUM_BINS + binOfGeneratedNumber];
        }

        // Out of attempts, should return LTTime_Infinite
        LTTime outOfAttempts = s_backoff->API->GetNextBackoff(s_backoff);
        TILT_ASSERT_TRUE(tilt, LTTime_IsInfinite(outOfAttempts), "Out of attempts, expected LTTime_Infinite, got %lu", LT_Pu32(LTTime_GetMilliseconds(outOfAttempts)));

        // Reset for next sample
        s_backoff->API->Reset(s_backoff);
    }

    // For every attempt, ensure there was a uniform distribution of s_backoff values in that range
    for (u32 i = 0; i < s_maxAttempts; i++) {
        AssertTenBinUniformity(tilt, &countsPerAttempt[i*NUM_BINS], NUM_SAMPLES / NUM_BINS);
    }
    
    lt_free(countsPerAttempt);
}

static void TestBackoffSetRandom(Tilt *tilt) {
    u32 counts[NUM_BINS] = {0};
    // nextAttemptDelay should be a random value between 0 and s_maxDelay
    for (u32 sample = 0; sample < NUM_SAMPLES; sample++) {
        LTTime nextAttemptMaxDelay = s_backoff->API->GetNextAttemptMaxDelay(s_backoff);
        TILT_ASSERT_TRUE(tilt, LTTime_IsLessThanOrEqual(nextAttemptMaxDelay, s_maxDelay), "Next attempt max delay (%lu) is greater than max delay (%lu)",
                         LT_Pu32(LTTime_GetMilliseconds(nextAttemptMaxDelay)), LT_Pu32(LTTime_GetMilliseconds(s_maxDelay)));

        // Organize into bin to check uniformity afterwards
        u32 binOfGeneratedNumber = ((u32)LTTime_GetMilliseconds(nextAttemptMaxDelay)) % NUM_BINS;
        ++counts[binOfGeneratedNumber];

        // Reset and generate again        
        s_backoff->API->Reset(s_backoff);
    }

    // Make sure random value was uniformly distributed
    AssertTenBinUniformity(tilt, counts, NUM_SAMPLES / NUM_BINS);
}

static void TestBackoffMinimum(Tilt *tilt) {
    // Get backoffs. Every s_backoff should be between min and max.
    for (u32 i = 0; i < NUM_SAMPLES; i++) {
        LTTime delay = s_backoff->API->GetNextBackoff(s_backoff);
        TILT_ASSERT_TRUE(tilt, LTTime_IsGreaterThanOrEqual(delay, s_minDelay) && LTTime_IsLessThanOrEqual(delay, s_maxDelay),
                         "Backoff (%lu) is not within expected range (%lu - %lu)",
                         LT_Pu32(LTTime_GetMilliseconds(delay)), LT_Pu32(LTTime_GetMilliseconds(s_minDelay)),
                         LT_Pu32(LTTime_GetMilliseconds(s_maxDelay)));
    }
}

/*______________________________
  Library/Test Initialization */

static const TestBackoffParams s_testBackoffParams[] = {
    { "RandomBetweenUniformity", .minDelay = 0,    .maxInitialDelay = 0,    .s_maxDelay = 10000, .maxAttempts = 0                          },
    { "BackoffRange",            .minDelay = 0,    .maxInitialDelay = 1000, .s_maxDelay = 16000, .maxAttempts = 5                          },
    { "BackoffSetRandom",        .minDelay = 0,    .maxInitialDelay = 0,    .s_maxDelay = 10000, .maxAttempts = kLTNetBackoff_RetryForever },
    { "BackoffMinimum",          .minDelay = 3000, .maxInitialDelay = 5000, .s_maxDelay = 10000, .maxAttempts = kLTNetBackoff_RetryForever },
};

static TestBackoffParams GetBackoffParams(Tilt *tilt, const char *testName) {
    for (u32 i = 0; i < COUNTOF(s_testBackoffParams); i++) {
        if (lt_strcmp(s_testBackoffParams[i].testName, testName) == 0) {
            return s_testBackoffParams[i];
        }
    }
    TILT_WARNING(tilt, "Test %s not found!", testName);
    return (TestBackoffParams){0};
}

static void BeforeTest(Tilt *tilt) {
    s_backoff = lt_createobject(LTNetBackoff);
    TILT_EXPECT_TRUE(tilt, s_backoff != NULL, "Cannot create LTNetBackoff");

    TestBackoffParams params = GetBackoffParams(tilt, tilt->API->GetTestName());
    s_minDelay        = LTTime_Milliseconds(params.minDelay);
    s_maxInitialDelay = LTTime_Milliseconds(params.maxInitialDelay);
    s_maxDelay        = LTTime_Milliseconds(params.s_maxDelay);
    s_maxAttempts     = params.maxAttempts;
    
    s_backoff->API->Set(s_backoff, s_minDelay, s_maxInitialDelay, s_maxDelay, s_maxAttempts);
}

static void AfterTest(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_destroyobject(s_backoff);
    s_backoff = NULL;    

    s_minDelay        = LTTime_Zero();
    s_maxInitialDelay = LTTime_Zero();
    s_maxDelay        = LTTime_Zero();
    s_maxAttempts     = 0;
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeTest = BeforeTest,
    .AfterTest  = AfterTest,
};

static const TiltEngineTest s_tests[] = {
    { TestRandomBetweenUniformity, "RandomBetweenUniformity", "Test the uniformity of the random number generation used for jitter",                          0 },
    { TestBackoffRange,            "BackoffRange",            "Make sure backoff values are always within the correct range",                                 0 },
    { TestBackoffSetRandom,        "BackoffSetRandom",        "If maxInitialDelay == 0, nextAttemptMaxDelay should be a random value between 0 and maxDelay", 0 },
    { TestBackoffMinimum,          "BackoffMinimum",          "Make sure backoff values are always at least the minimum delay",                               0 }
};

static int UnitTestLTNetBackoffImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTNetBackoffImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTNetBackoffImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetBackoff, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetBackoff, UnitTestLTNetBackoffImpl_Run, 1536) LTLIBRARY_DEFINITION;
