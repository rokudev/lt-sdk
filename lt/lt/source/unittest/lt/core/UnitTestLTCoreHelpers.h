/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreHelpers.h> - Helper Functions For Great
 *                                              Benefit Glorious Nation Of
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

#ifndef LT_SOURCE_UNITTEST_LT_CORE_UNITTESTLTCOREHELPERS_H
#define LT_SOURCE_UNITTEST_LT_CORE_UNITTESTLTCOREHELPERS_H

#include <lt/LTTypes.h>

bool strequ(char const *, char const *);
bool strnequ(char const *, char const *, LT_SIZE);
    /* Null-terminated and length-limited char string comparison -
     * return true if equal. */

void fill(void *p, u8 v, int n);
    /* Fill n bytes starting at p with v. */

void copy(void *d, void const * s, int n);
    /* Copy n bytes from s to d.  If the areas overlap, all bets are off. */

char * timeWithAbbreviatedUnits(char *buf, int bufsize, LTTime const * t,
                                bool bAbbreviatedUnits);
    /* Convert an LTTime to a string with real units.
     * Result is a dulcified string with abbreviated units s, us,
     * ns or non-abbreviated units.
     * Return a pointer to the string. */

char * timeWithUnits(char *buf, int bufsize, LTTime const * t);
    /* Convert an LTTime to a string with real units.
     * calls timeWithAbbreviatedUnits(buf, bufsize, t, false); to generate
     * a dulcified, non-abbreviated string
     * Return a pointer to the string. */

bool LTTime_close(LTTime value, LTTime expected, LTTime tolerance);
    /* Check an inexact duration against an expected value and a tolerance. */

#endif /* #ifndef LT_SOURCE_UNITTEST_LT_CORE_UNITTESTLTCOREHELPERS_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  16-Jul-20   constantine created
 *  07-Feb-21   constantine added strnequ()
 *  19-Oct-21   constantine moved to UnitTestLTCore
 */
