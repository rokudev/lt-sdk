/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreMathTests.c>
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

#include <lt/core/LTCore.h>
#include <tilt/JiltEngine.h>

/* clang-format off */

/*******************************************************************************
 * Math tests
 * A test for a given math function has a table of tests to perform, involving
 * various inputs including edge cases and expected outputs.
 */

/* Comparing floating point values is hard. This comparison logic
   was adapted from: https://bitbashing.io/comparing-floats.html */
static s32 UlpsDistance(const float a, const float b) {
    s32 ia, ib;
    lt_memcpy(&ia, &a, sizeof(float));
    lt_memcpy(&ib, &b, sizeof(float));
    /* Don't compare differently-signed floats. */
    if ((ia < 0) != (ib < 0)) return LT_S32_MAX;
    /* Return the absolute value of the distance in ULPs. */
    s32 distance = ia - ib;
    if (distance < 0) distance = -distance;
    return distance;
}

static bool NearlyEqual(float a, float b, float fixedEpsilon, int ulpsEpsilon) {
    /* Save work if the floats are equal. Also handles +0 == -0 */
    if (a == b) return true;
    /* Return if either or both values are NaNs. If both
       are NaN, they are considered equal here. */
    if (lt_isnan(a) || lt_isnan(b)) return lt_isnan(a) && lt_isnan(b);
    /* One's infinite and they're not equal. */
    if (lt_isinf(a) || lt_isinf(b)) return false;
    /* Handle the near-zero case. */
    const float difference = (a > b) ? (a - b) : (b - a);
    if (difference <= fixedEpsilon) return true;
    /* Finally resort to comparing ULPs */
    return UlpsDistance(a, b) <= ulpsEpsilon;
}

void Testsqrtf(Tilt * tilt) {
    struct sqrtfTestElement {
        float input;
        float expectedValue;
    };

    static struct sqrtfTestElement const tests[] = {
        {          0.0f,                   0.0f },
        {          1.0f,                   1.0f },
        {          4.0f,                   2.0f },
        {          2.0f,             1.4142135f },
        {          0.1f,             0.3162277f },
        { 2335215616.0f,         48324.0687029f }, // caused infinite loop in the wild
        {       1.0e38f,                1.0e19f },
        {   LT_INFINITY,            LT_INFINITY },
        {  -LT_INFINITY,                 LT_NAN },
        {        LT_NAN,                 LT_NAN },
        {         -1.0f,                 LT_NAN },
        {      -1.0e38f,                 LT_NAN },
    };

    struct sqrtfTestElement const * pTest = tests;
    for (u32 n = 1; n <= sizeof tests / sizeof *pTest; ++n, ++pTest) {
        float value = lt_sqrtf(pTest->input);
        if (!NearlyEqual(value, pTest->expectedValue, 0.0001, 1)) {
            TILT_REPORT_FAILURE(tilt, "sqrtf.result.a: n=%lu expected=%f result=%f", n, pTest->expectedValue, value);
        }
    }
}

static void Test_LT_CLIP(Tilt * tilt, int var, int min, int max, int expected) {
    int result = LT_CLIP(var, min, max);
    TILT_EXPECT_TRUE(tilt, result == expected, "LT_CLIP(%d, %d, %d), expected=%d, result=%d", var, min, max, expected, result);
}

void TestLT_CLIP(Tilt * tilt) {
    Test_LT_CLIP(tilt, 0, 0, 5, 0);
    Test_LT_CLIP(tilt, 0, 0, 5, 0);
    Test_LT_CLIP(tilt, 1, 0, 5, 1);
    Test_LT_CLIP(tilt, 4, 0, 5, 4);
    Test_LT_CLIP(tilt, 5, 0, 5, 5);
    Test_LT_CLIP(tilt, 0, 5, 0, 0);
    Test_LT_CLIP(tilt, 1, 5, 0, 1);
    Test_LT_CLIP(tilt, 4, 5, 0, 4);
    Test_LT_CLIP(tilt, 5, 5, 0, 5);
    Test_LT_CLIP(tilt, 0, 1, 5, 1);
    Test_LT_CLIP(tilt, 1, 1, 5, 1);
    Test_LT_CLIP(tilt, 2, 1, 5, 2);
    Test_LT_CLIP(tilt, 4, 1, 5, 4);
    Test_LT_CLIP(tilt, 5, 1, 5, 5);
    Test_LT_CLIP(tilt, 6, 1, 5, 5);
    Test_LT_CLIP(tilt, 0, 5, 1, 1);
    Test_LT_CLIP(tilt, 1, 5, 1, 1);
    Test_LT_CLIP(tilt, 2, 5, 1, 2);
    Test_LT_CLIP(tilt, 4, 5, 1, 4);
    Test_LT_CLIP(tilt, 5, 5, 1, 5);
    Test_LT_CLIP(tilt, 6, 5, 1, 5);

    Test_LT_CLIP(tilt, -1, 0, 5, 0);
    Test_LT_CLIP(tilt, -1, 1, 5, 1);
    Test_LT_CLIP(tilt, -1, 5, 0, 0);
    Test_LT_CLIP(tilt, -1, 5, 1, 1);

    Test_LT_CLIP(tilt,  0, -1, 5,  0);
    Test_LT_CLIP(tilt,  1, -1, 5,  1);
    Test_LT_CLIP(tilt, -1, -1, 5, -1);
    Test_LT_CLIP(tilt, -2, -1, 5, -1);

    Test_LT_CLIP(tilt,  0,  5, -1,  0);
    Test_LT_CLIP(tilt,  1,  5, -1,  1);
    Test_LT_CLIP(tilt, -1,  5, -1, -1);
    Test_LT_CLIP(tilt, -2,  5, -1, -1);
}