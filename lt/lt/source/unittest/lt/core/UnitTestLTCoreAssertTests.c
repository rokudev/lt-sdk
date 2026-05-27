/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreAssertTests.c>
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

/* Assert test.  Fail an assert.
 * This test is not included in the suite of automatic tests as it crashes
 * the system. */

void TestAssert(Tilt * tilt) {
    LT_ASSERT(0);
    TILT_REPORT_FAILURE(tilt, "return from assert (should not)");
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Aug-19   constantine created
 *  23-Dec-19   constantine changed name from LTTestNoFault to LTUnitTestCore
 *  17-Jul-20   constantine converted to C
 *  19-Oct-21   constantine moved to UnitTestLTCore
 *  02-Aug-22   constantine reworked for TILT
 */
