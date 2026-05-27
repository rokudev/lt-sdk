/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreHelpers.c>
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

/*******************************************************************************
 * Helper functions for string compare, memory fill, and copy (please avoid
 *   overlapping regions for destination and source), plus formatted print of
 *   LTTime in the largest unit possible with the result still a whole number
 *   (as many non-significant figures removed as possible - I got tired of
 *   counting trailing zeroes of millisecond-order times expressed in
 *   nanoseconds).
 * For example, 154000000000ns printed as 154 seconds.
 * timeWithUnits() depends on LTCore::snprintf(). */

bool strnequ(char const *a, char const *b, LT_SIZE n) {
    bool equal = false;
    for (; n && (equal = *a == *b) && *a && *b; ++a, ++b, --n);
    return equal;
}

bool strequ(char const *a, char const *b) { return strnequ(a, b, LT_SIZE_MAX); }

void fill(void *pp, u8 v, int n) {
    u8 *p = pp;
    for (; n > 0; --n, ++p) *p = v;
}

void copy(void *pd, void const *ps, int n) {
    u8 *d = pd;
    const u8 *s = ps;
    for (; n > 0; --n, ++d, ++s) *d = *s;
}

char * timeWithAbbreviatedUnits(char *buf, int bufsize, LTTime const * t, bool bAbbreviatedUnits) {
    u32 nSeconds      = (u32)LTTime_GetSeconds(*t);
    u32 nMicroseconds = (u32)LTTime_GetMicroseconds(LTTime_FractionalSeconds(*t));
    u32 nNanoseconds  = (u32)LTTime_GetNanoseconds(LTTime_FractionalMicroseconds(*t));
    const char * pSeconds      = bAbbreviatedUnits ? "s"  : ((1 == nSeconds)      ? "second"      : "seconds");
    const char * pMicroseconds = bAbbreviatedUnits ? "us" : ((1 == nMicroseconds) ? "microsecond" : "microseconds");
    const char * pNanoseconds  = bAbbreviatedUnits ? "ns" : ((1 == nNanoseconds)  ? "nanosecond"  : "nanoseconds");
    if (nSeconds && nNanoseconds) lt_snprintf(buf, bufsize, "%ld %s, %06ld %s, %03ld %s", nSeconds, pSeconds, nMicroseconds, pMicroseconds, nNanoseconds, pNanoseconds);
    else if (nSeconds)            lt_snprintf(buf, bufsize, "%ld %s, %06ld %s", nSeconds, pSeconds, nMicroseconds, pMicroseconds);
    else                          lt_snprintf(buf, bufsize, "%ld %s, %03ld %s", nMicroseconds, pMicroseconds, nNanoseconds, pNanoseconds);
    return buf;     /* return a pointer to the string */
}

char * timeWithUnits(char *buf, int bufsize, LTTime const * t) {
    return timeWithAbbreviatedUnits(buf, bufsize, t, false);
}

bool LTTime_close(LTTime value, LTTime expected, LTTime tolerance) {
    LTTime lowerBound = LTTime_Subtract(expected, tolerance),
           upperBound = LTTime_Add(expected, tolerance);
    return LTTime_IsLessThanOrEqual(lowerBound, value) && LTTime_IsGreaterThanOrEqual(upperBound, value);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Aug-19   constantine created
 *  23-Dec-19   constantine changed name from LTTestNoFault to LTUnitTestCore
 *  16-Jul-20   constantine converted to C
 *  07-Feb-21   constantine added strnequ()
 *  19-Oct-21   constantine moved to UnitTestLTCore
 */
