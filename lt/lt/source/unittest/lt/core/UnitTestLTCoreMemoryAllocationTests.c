/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreMemoryAllocationTests.c>
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
 * Test Alloc() and Free().
 * Verify return value of Alloc(), and integrity of allocated memory blocks. */

void TestAlloc(Tilt * tilt) {
    /* 1. Test writability of block: allocate a block, write a
     *    test pattern, read back the test pattern. */
    u8 *m = (u8 *) lt_malloc(256);
    TILT_EXPECT_TRUE(tilt, m, "malloc.alloc.1");
    for (int i = 0; i <= 255; ++i)
        *(m + i) = i;
    u8 *n = (u8 *) lt_malloc(1024);
    TILT_EXPECT_TRUE(tilt, n, "malloc.alloc.2");
    for (int i = 0; i < 1024; ++i)
        *(n + i) = i % 255;
    for (int i = 255; i >= 0; --i)
        if (*(m + i) != i) {
            TILT_REPORT_FAILURE(tilt, "malloc.read.after.alloc.1");
            break;
        }
    for (int i = 1023; i >= 0; --i)
        if (*(n + i) != i % 255) {
            TILT_REPORT_FAILURE(tilt, "malloc.read.after.alloc.2");
            break;
        }
    lt_free(m);     /* cleanup */
    lt_free(n);
    /* 2. Allocate many small blocks, ensure none overlap. */
    enum {
        knBlocks = 32,
        kSmallBlockSize = 128
    };
    u8 *blocks[knBlocks];
    for (int i = 0; i < knBlocks; ++i)
        if (!(blocks[i] = (u8 *) lt_malloc(kSmallBlockSize))) {
            TILT_REPORT_FAILURE(tilt, "malloc.alloc.3");
            for (--i; i >= 0; --i)    /* Oops, an alloc failed.  Free all the */
                lt_free(blocks[i]);   /* successfully allocated blocks. */
        }
    for (int i = knBlocks - 1; i >= 1; --i) {  /* Check every block... */
        u8 *bi = blocks[i], *bie = bi + kSmallBlockSize;
        for (int j = i - 1; j >= 0; --j)   /* ...against every other block... */
            if (blocks[j] > bi && blocks[j] < bie) { /* ...none must overlap. */
                TILT_REPORT_FAILURE(tilt, "malloc.overlap");
            }
    }
    for (int i = knBlocks - 1; i >= 0; --i)      /* cleanup */
        lt_free(blocks[i]);
    /* 3. Allocate a big block, free, then allocate again. */
    u32 bigBlockSize = 0x00080000;
    u8 *bigBlock = 0;
    /* Assume any reasonable system has 1KB for this test: */
    for (; bigBlockSize >= 0x400; bigBlockSize /= 2)
        if ((bigBlock = (u8 *)lt_malloc(bigBlockSize)))
            break;
    TILT_EXPECT_TRUE(tilt, bigBlock, "malloc.big: bigBlockSize=0x%X", bigBlockSize);   /* block that same size again */
    lt_free(bigBlock);
    bigBlock = (u8 *)lt_malloc(bigBlockSize);  /* ought to be able to get a */
    TILT_EXPECT_TRUE(tilt, bigBlock, "malloc.big.after.free"); /* block that same size again */
    /* 4. Fill the big block with a pattern, then free it, then allocate small
     * blocks.  Look for the pattern in the small blocks.  The pattern
     * should be mostly intact. */
    static u8 const testPattern = 0xA5;
    fill(bigBlock, testPattern, bigBlockSize);
    lt_free(bigBlock);
    for (int i = knBlocks - 1; i >= 0; --i)
        blocks[i] = (u8 *) lt_malloc(kSmallBlockSize);
    for (int i = knBlocks - 1; i >= 0; --i)
        if (*(blocks[i] + kSmallBlockSize / 2) == 0xA5) {
            TILT_INFO(tilt, "success.small.inside.big");
            break;
        }
    for (int i = knBlocks - 1; i >= 0; --i)  /* cleanup */
        lt_free(blocks[i]);
}

/*******************************************************************************
 * Test ReAlloc().
 * Allocate blocks, verify correct resizing. */

void TestReAlloc(Tilt * tilt) {
    enum {
        kBlockSize = 64
    };
    #define REALLOC_TEST_PATTERN 0xB3
    static u8 const test1 = (u8)  REALLOC_TEST_PATTERN;
    static u8 const test2 = (u8) ~REALLOC_TEST_PATTERN;
    u8 *a = (u8 *) lt_malloc(kBlockSize);  /* Allocate a block, then fill it */
    TILT_EXPECT_TRUE(tilt, a, "malloc.initial");
    fill(a, test1, kBlockSize);
    /* Double the size of the block: */
    u8 *b = (u8 *) lt_realloc(a, kBlockSize * 2);
    if (!b) {
        lt_free(a); /* Realloc failed - original block is still allocated */
        TILT_REPORT_FAILURE(tilt, "malloc.realloc");
    }
    fill(b + kBlockSize, test2, kBlockSize);  /* fill second half differently */
    for (u8 *p = b + kBlockSize - 1; p >= b; --p)
        if (*p != test1) { /* first half must still have the original pattern */
            lt_free(b);
            TILT_REPORT_FAILURE(tilt, "malloc.realloc.contents.double");
        }
    u8 *c = (u8 *) lt_realloc(b, kBlockSize / 2);   /* Half the original size */
    if (!c) {
        lt_free(b);
        TILT_REPORT_FAILURE(tilt, "malloc.realloc.half");
    }
    for (u8 *p = c + kBlockSize / 2 - 1; p >= c; --p)
        if (*p != test1) {  /* the block must still have the original pattern */
            TILT_REPORT_FAILURE(tilt, "malloc.realloc.contents.half");
            break;
        }
    lt_free(c);
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
