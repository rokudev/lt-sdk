/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreMemoryTests.c>
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
 * Test memory compare, copy, move, and set functions. */

void Testmemcmp(Tilt * tilt) {
    static char const * const original = "The quick brown fox jumped over the lazy dog's back";
    enum { kLen = 51 };
    for (int i = 0; i < kLen; ++i) {
        char test[kLen + 1];
        for (int j = 0; j < kLen; ++j) {
            test[j] = original[j];
        }
        TILT_EXPECT_TRUE(tilt, lt_memcmp(original, test, kLen) == 0, "memcmp.equal");

        test[i] = original[i] + 5;
        TILT_EXPECT_TRUE(tilt, lt_memcmp(original, test, kLen) == -5, "memcmp.less");

        test[i] = original[i] - 5;
        TILT_EXPECT_TRUE(tilt, lt_memcmp(original, test, kLen) == 5, "memcmp.greater");
    }
}

void Testmemcpy(Tilt * tilt) {
    static char const * const source = "The frequency of the emitted wave";
    enum { kLen = 34 };
    char dest[kLen];
    void * pRet = lt_memcpy(dest, source, kLen);
    TILT_EXPECT_TRUE(tilt, strequ(source, dest), "memcpy.nz");
    TILT_EXPECT_TRUE(tilt, pRet == dest, "memcpy.pr");
    lt_memcpy(dest, source + 1, 0);
    TILT_EXPECT_TRUE(tilt, strequ(source, dest), "memcpy.z");
}

void Testmemmove(Tilt * tilt) {
    static char const * const source =   "The frequency of the emitted wave";
    static char const * const intended1 = "The frequency of The frequency of the emitted wave";
    static char const * const intended2 = "the emitted wave the emitted wave";
    enum {
        kLen      = 33,   /* not including null terminator */
        kAreaSize = 51
    };
    char area[kAreaSize];
    copy(area, source, kLen);                        /* Make a copy of the original source string */
    fill((u8 *) (area + kLen), 0, kAreaSize - kLen);   /* zero out everything after the copy */
    TILT_DEBUG(tilt, "memmove.1.before: %s", area);
    lt_memmove(area + 17, area, kLen);         /* copy the string to overlap the original */
    TILT_DEBUG(tilt, "memmove.1.after: %s", area);
    TILT_EXPECT_TRUE(tilt, strequ(area, intended1), "memmove.1");
    TILT_EXPECT_TRUE(tilt, strequ(area + 17, source), "memmove.1.source");

    copy(area, source, kLen);
    fill((u8 *) (area + kLen), 0, kAreaSize - kLen);
    TILT_DEBUG(tilt, "memmove.2.before: %s", area);
    void * pRet = lt_memmove(area, area + 17, 16);
    TILT_EXPECT_TRUE(tilt, pRet == area, "memmove.pr");
    TILT_DEBUG(tilt, "memmove.2.after: %s", area);
    TILT_EXPECT_TRUE(tilt, strequ(area, intended2), "memmove.2");

    /*
        Exhaustive test which covers:
            - Variably sized buffers (small buffers to trigger byte-by-byte copying, large buffers to trigger word-by-word rolled and unrolled copying)
            - Aligned and unaligned src/dst buffers (to trigger pre and post pointer alignment while copying)
            - Overlapping and non-overlapping buffers

        Non-overlapping, move forwards:
            Source: |<--patternSize-->|
            Dest:   |-------moveSize------->|<--patternSize-->|

        Non-overlapping, move backwards:
            Source:                         |<--patternSize-->|
            Dest:   |<--patternSize-->|<-------moveSize-------|

        Overlapping, move forwards:
            Source: |<------patternSize------>|
            Dest:   |-----moveSize---->|<------patternSize------>|

        Overlapping, move backwards:
            Source:                    |<------patternSize------>|
            Dest:   |<------patternSize------>|<----moveSize-----|
    */
    //enum { kPatternMin = 1, kPatternMax = 256, kMoveSizeMin = -127, kMoveSizeMax = 128 };
    enum { kPatternMin = 1, kPatternMax = 64, kMoveSizeMin = -31, kMoveSizeMax = 32 };
    for (u32 patternSize = kPatternMin; patternSize < kPatternMax; ++patternSize) {
        for (s32 moveSize = kMoveSizeMin; moveSize < kMoveSizeMax; ++moveSize) {
            u32 testBufSize = patternSize + lt_abs(moveSize);
            u8 *testBuf = lt_malloc(testBufSize);
            fill(testBuf, 0xff, testBufSize);

            u8 *src  = moveSize < 0 ? testBuf - moveSize : testBuf;
            u8 *dest = moveSize > 0 ? testBuf + moveSize : testBuf;
            for (u32 i = 0; i < patternSize; ++i) {
                src[i] = i;
            }

            lt_memmove(dest, src, patternSize);

            for (u32 i = 0; i < testBufSize; ++i) {
                u8 *checkByte = testBuf + i;
                if (checkByte >= dest && checkByte < dest + patternSize) {
                    // Byte is in the dest buffer. Verify that it matches the pattern.
                    u32 destOffset = checkByte - dest;
                    TILT_EXPECT_TRUE(tilt, *checkByte == destOffset, "dest.pattern.val");
                } else if (checkByte >= src && checkByte < src + patternSize) {
                    // Byte is in the source buffer but not the dest buffer. Verify that it matches the original pattern.
                    u32 srcOffset = checkByte - src;
                    TILT_EXPECT_TRUE(tilt, *checkByte == srcOffset, "src.pattern.val");
                } else {
                    // Byte is not in either the source or dest buffers. Verify that it matches the fill byte value.
                    TILT_EXPECT_TRUE(tilt, *checkByte == 0xff, "src.empty.val");
                }
            }

            lt_free(testBuf);
        }
    }
}

void Testmemset(Tilt * tilt) {
    enum { kSize = 128 };
    static u8 const testPattern = 0x4B;
    u8 area[kSize], *pa = area;
    fill(area, 0, kSize);
    void * pRet = lt_memset(area, testPattern, kSize);
    TILT_EXPECT_TRUE(tilt, pRet == area, "memset.pr");
    pa = area;
    for (int i = kSize; i; --i, ++pa) {
        TILT_EXPECT_TRUE(tilt, *pa == testPattern, "memset");
    }
}

void TestmemsetInt(Tilt * tilt) {
    enum { kSize = 128 };
    u8 area[kSize], *pa = area;
    fill(area, 0, kSize);
    lt_memset(area, -1, kSize);
    pa = area;
    for (int i = kSize; i; --i, ++pa) {
        TILT_EXPECT_TRUE(tilt, *pa == 0xff, "memset int");
    }
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Aug-19   constantine created
 *  23-Dec-19   constantine changed name from LTTestNoFault to LTUnitTestCore
 *  18-Jul-20   constantine converted to C
 *  19-Aug-20   augustus    use enum instead of static const int variables for array size constants; static const incompatible in msvc
 *  06-Dec-20   constantine reworked printf formatting
 *  19-Oct-21   constantine moved to UnitTestLTCore
 *  02-Aug-22   constantine reworked for TILT
 */
