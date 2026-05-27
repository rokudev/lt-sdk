/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreKernelTimeTests.c>
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
#include "UnitTestLTCoreHelpers.h"

/*******************************************************************************
 * Test LTCore timing functions Sleep(), GetKernelTime(), and
 * GetKernelTimeResolution(). */

void TestKernelTime(Tilt * tilt) {
    LTTime chunk = LT_GetCore()->GetKernelTimeResolution();
    enum { strbufSize = 80 };
    char strbuf[strbufSize];
    TILT_DEBUG(tilt, "kerneltime.resolution: %s", timeWithUnits(strbuf, strbufSize, &chunk));
    /* Assume no system has a time resolution lower than 100Hz: */
    TILT_EXPECT_TRUE(tilt,    LTTime_IsGreaterThan(chunk, LTTime_Zero())
                           || LTTime_IsLessThanOrEqual(chunk, LTTime_Milliseconds(10)), "kerneltime.resolution");
    ILTThread * iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());

    /* Please, no sleeping with slop */
    LTThread hThread = iThread->GetCurrentThread();
    u8 oldPriority = iThread->GetPriority(hThread);
    iThread->SetPriority(hThread, kLTThread_PriorityHighest);

    LTTime const duration = LTTime_Milliseconds(200);
    LTTime before = LT_GetCore()->GetKernelTime();
    iThread->Sleep(duration);
    LTTime after = LT_GetCore()->GetKernelTime();
    LTTime elapsed = LTTime_Subtract(after, before);
    /* Should elapse no less than 1 ms before requested duration */
    LTTime negativeTol = LTTime_Milliseconds(-1);
    /* Should elapse no greater than approximately (M + N) ms after requested duration.
     * Where M is the number of runnable threads with equal priority to this test thread (assuming
     * 1ms tick) and N is the amount of time interrupts and threads with higher priority must run
     * before returning to this thread. Since the system should not be busy while running unit tests
     * we generally assume (M + N) ~ 2. To accommodate non-RT systems with lots of background threads,
     * we increase this to 5. */
    LTTime positiveTol = LTTime_Milliseconds(5);
    LTTime discrepancy = LTTime_Subtract(elapsed, duration);
    TILT_DEBUG(tilt, "kerneltime.duration: %s", timeWithUnits(strbuf, strbufSize, &duration));
    TILT_DEBUG(tilt, "kerneltime.before: %s", timeWithUnits(strbuf, strbufSize, &before));
    TILT_DEBUG(tilt, "kerneltime.after: %s", timeWithUnits(strbuf, strbufSize, &after));
    TILT_DEBUG(tilt, "kerneltime.elapsed: %s", timeWithUnits(strbuf, strbufSize, &elapsed));
    if (LTTime_IsGreaterThan(discrepancy, positiveTol) || LTTime_IsLessThan(discrepancy, negativeTol)) {
        TILT_REPORT_FAILURE(tilt, "kerneltime.discrepancy: %s", timeWithUnits(strbuf, strbufSize, &discrepancy));
    } else {
        TILT_DEBUG(tilt, "kerneltime.discrepancy: %s", timeWithUnits(strbuf, strbufSize, &discrepancy));
    }
    TILT_EXPECT_TRUE(tilt, LTTime_IsGreaterThanOrEqual(elapsed, LTTime_Zero()), "kerneltime.elapsed.negative");
    for (u32 i = 0; i < 1000; ++i) {
        LTTime before = LT_GetCore()->GetKernelTime();
        LTTime after = LT_GetCore()->GetKernelTime();
        TILT_ASSERT_TRUE(tilt, LTTime_IsLessThanOrEqual(before, after), "kerneltime.backwards: before=%lld, after=%lld",
                         LT_Ps64(before.nNanoseconds), LT_Ps64(after.nNanoseconds));
    }
    iThread->SetPriority(hThread, oldPriority);

}

void TestSetUTCTime(Tilt * tilt) {
    LTTime timeToSet = LT_GetCore()->GetBuildTime();
    LTTimeBase timebase = (LTTimeBase) {
        .primaryClockTime = LT_GetCore()->GetKernelTime(),
        .secondaryClockTime = timeToSet,
    };
    LT_GetCore()->SetClockTimeBaseUTC(&timebase);
    LT_GetCore()->SetApproximateClockTimeBaseUTC(&timebase);
    TILT_EXPECT_TRUE(tilt, LTTime_IsGreaterThan(LT_GetCore()->GetClockTimeUTC(), timeToSet), "valid.utc.set");
    TILT_EXPECT_TRUE(tilt, LTTime_IsZero(LT_GetCore()->GetApproximateClockTimeUTC()), "zero.approximate.utc");
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Aug-19   constantine created
 *  23-Dec-19   constantine changed name from LTTestNoFault to LTUnitTestCore
 *  18-Jul-20   constantine converted to C
 *  19-Aug-20   augustus    use enum instead of static const int variables for array size constants; static const incompatible in msvc
 *  09-Apr-21   augustus    1>LTUnitTestCoreKernelTimeTests.obj : error LNK2001: unresolved external symbol _SystemCoreClock
                            1>..\..\..\..\targets\win32\debug\bin\LTUnitTestCore.dll : fatal error LNK1120: 1 unresolved externals
                            1>Done building project "LTUnitTestCore.vcxproj" -- FAILED.
 *  09-Apr-21   augustus    I have no words, so I let the linker speak for itself
 *  19-Oct-21   constantine moved to UnitTestLTCore
 *  02-Aug-22   constantine reworked for TILT
 */
