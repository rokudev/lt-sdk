/*******************************************************************************
 * BitsSampleTest.c                                   (using TILT 2.0 Framework)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <tilt/BitsEngine.h>

/*
 * The guidelines given in TiltSampleTest.c for unit tests also apply to stress
 *  tests, therefore please refer to TiltSampleTest.c for more information.
 *
 *  Additional BitsEngine Test Guidelines:
 *   1. TestProcs must call Synchronize() right before returning, or at the end of
 *      their testProc chains (e.g.: when timers are scheduled for the test thread).
 *   2. Synchronize() must be invoked with a value greater than the current syncPoint;
 *      otherwise, synchronization will fail, potentially causing tests to hang or
 *      time out.
 *   3. The main thread is intended for configuring test cases within its thread
 *      context. The Tilt test framework supports only one test case at a time, even
 *      in multi-threaded scenarios like stress testing. (See Tilt.h for details on
 *      multi-threaded testing with Tilt.) Therefore, it is recommended to use the
 *      main thread for sequencing test cases.
 *   4. TestProcs are asynchronous and execute in parallel, with no guaranteed start
 *      order at a synchronization point—unless different thread priorities are used.
 *      However, using different thread priorities may have other unintended
 *      consequences such as test thread starvation if higher-priority threads fail
 *      to sleep or block adequately.
 *   5. To guarantee a proper testProc launch test threads should avoid using the
 *      highest thread priority.
 */

/*
 *  In this example we create several test threads to help stress the allocator.
 *   The first test case runs memory allocations for a period of time using multiple
 *   threads. The second test case runs a series of randomized allocs, reallocs and
 *   frees using one thread.
 */

enum {
    kNumTestThreads = 4,
};

/** Stress Tests */
enum {
    kSyncPoint_Allocs = 0,
    kSyncPoint_RandomAllocs
};

static BitsEngine  *s_engine;
static LTArray     *s_threads;
static u32          s_stopwatch;
static u32          s_prng;

enum { kMaxBlocks = 256, kBlockThresh = 192 };
static u8 *s_blocks[kMaxBlocks];
static u32 s_sizes[kMaxBlocks];

static void *Alloc(u32 size, u8 val) {
    void *block = lt_malloc(size);
    if (block && size) {
        lt_memset(block, val, size);
    }
    return block;
}

/* Multi-threaded allocation test to stress allocator */
static u32 AllocsTest(Tilt *tilt, LTOThread *thread) {
    enum { kMax = 2, kBlockSize = 0x1010, kBlockData = 0x55 };
    void *block[kMax];
    /* Run stressor loop for a minimum period of time */
    u32 nIter = 0;
    do {
        nIter++;
        for (u32 nBlock = 0; nBlock < kMax; nBlock++) {
            block[nBlock] = Alloc(kBlockSize, kBlockData);
            if ((nIter & 511) == 255) thread->API->Yield();
        }
        if ((nIter & 511) == 511) thread->API->Yield();
        for (u32 nBlock = 0; nBlock < kMax; nBlock++) {
            if ((nIter & 511) == 127) thread->API->Yield();
            TILT_EXPECT_TRUE(tilt, block[nBlock], "Block allocation failed");
            lt_free(block[nBlock]);
        }
    } while (LTTime_IsLessThan(tilt->API->StopwatchRead(s_stopwatch), LTTime_Seconds(10)));
    return nIter;
}

static void TestFree(Tilt *tilt, u8 *block, u32 size) {
    TILT_ASSERT_TRUE(tilt, block, "block is null");
    for (u32 nIdx = 0; nIdx < size; nIdx++) {
        TILT_EXPECT_TRUE(tilt, block[nIdx] == (size & 0xff), "bad block data");
    }
    lt_free(block);
}

static void *TestRealloc(Tilt *tilt, u8 *block, u32 size, u32 oldSize) {
    /* NOTE: not for testing NULL input case ... */
    for (u32 nIdx = 0; nIdx < oldSize; nIdx++) {
        TILT_EXPECT_TRUE(tilt, block[nIdx] == (oldSize & 0xff), "bad block data");
    }
    u8 *newBlock = (u8 *)lt_realloc(block, size);
    if (newBlock) {
        /* Check realloc copy */
        for (u32 nIdx = 0; nIdx < ((size < oldSize) ? size : oldSize); nIdx++) {
            TILT_EXPECT_TRUE(tilt, block[nIdx] == (oldSize & 0xff), "bad block data");
        }
        lt_memset(newBlock, size & 0xff, size);
    }
    return (void *)newBlock;
}

/* Find nth free or allocated block in block array */
static s32 FindBlockAtIndex(u32 index, bool freeBlock) {
    for (u32 blockIndex = 0; blockIndex < kMaxBlocks; blockIndex++) {
        if (freeBlock ^ (s_blocks[blockIndex] != NULL)) {
            if (index == 0) return blockIndex;
            else index--;
        }
    }
    return -1;
}

/* Single-threaded randomized allocation test to stress allocation algorithm */
static void RandomAllocsTest(Tilt *tilt) {
    enum { kInnerLoopIterations = 128 };
    TILT_INFO(tilt, "Available RAM: %lu", LT_PLT_SIZE(LT_GetCore()->GetAvailableSystemRAM()));
    LT_SIZE leastAvailableRAM = LT_SIZE_MAX;
    /* Run stressor loop for a minimum period of time */
    do {
        u32 nAllocated = 0;
        for (u32 nInner = 0; nInner < kInnerLoopIterations; nInner++) {
            /* Alloc phase, perform a minimum of 1 up to a maximum of 16 allocs */
            LT_SIZE ram = LT_GetCore()->GetAvailableSystemRAM();
            u32 maxAllocSize = 4096;
            if (ram < 16384) maxAllocSize = 256;
            u32 nOps = tilt->API->PrngGenerateRandomNumber(s_prng, 15) + 1;
            for (u32 nOp = 0; nOp < nOps; nOp++) {
                /* Find the first free index */
                s32 blockIndex = FindBlockAtIndex(0, true);
                if (blockIndex < 0) break;
                u32 blockSize = tilt->API->PrngGenerateRandomNumber(s_prng, maxAllocSize);
                s_blocks[blockIndex] = Alloc(blockSize, blockSize & 0xff);
                if (s_blocks[blockIndex]) {
                    s_sizes[blockIndex] = blockSize;
                    nAllocated++;
                }
            }
            ram = LT_GetCore()->GetAvailableSystemRAM();
            if (ram < leastAvailableRAM) leastAvailableRAM = ram;
            /* Realloc phase, perform a minimum of 0 and maximum of 3 reallocs */
            nOps = tilt->API->PrngGenerateRandomNumber(s_prng, 3);
            for (u32 op = 0; op < nOps; op++) {
                /* Find and realloc the nth allocated block (reallocIdx) */
                u32 reallocIdx = tilt->API->PrngGenerateRandomNumber(s_prng, nAllocated - 1);
                u32 blockIndex = FindBlockAtIndex(reallocIdx, false);
                u32 blockSize = tilt->API->PrngGenerateRandomNumber(s_prng, maxAllocSize - 1) + 1;
                void *block = TestRealloc(tilt, s_blocks[blockIndex], blockSize, s_sizes[blockIndex]);
                if (block) {
                    s_blocks[blockIndex] = block;
                    s_sizes[blockIndex]  = blockSize;
                }
            }
            /* Free phase, adjust nOps to maintain manageable block count */
            if (nAllocated > kBlockThresh) {
                nOps = tilt->API->PrngGenerateRandomNumber(s_prng, 23) + 1;
            } else {
                nOps = tilt->API->PrngGenerateRandomNumber(s_prng, 7) + 1;
                if (nOps > nAllocated) nOps = nAllocated;
            }
            for (u32 nOp = 0; nOp < nOps; nOp++) {
                u32 freeIdx = tilt->API->PrngGenerateRandomNumber(s_prng, nAllocated - 1);
                u32 blockIndex = FindBlockAtIndex(freeIdx, false);
                TestFree(tilt, s_blocks[blockIndex], s_sizes[blockIndex] & 0xff);
                s_blocks[blockIndex] = NULL;
                nAllocated--;
            }
        }
        /* Free remaining blocks and reset block array */
        for (u32 blockIndex = 0; blockIndex < kMaxBlocks; blockIndex++) {
            if (s_blocks[blockIndex]) {
                TestFree(tilt, s_blocks[blockIndex], s_sizes[blockIndex] & 0xff);
                s_blocks[blockIndex] = NULL;
            }
        }
    } while (LTTime_IsLessThan(tilt->API->StopwatchRead(s_stopwatch), LTTime_Seconds(10)));
    TILT_INFO(tilt,  "Least available RAM: %lu", LT_PLT_SIZE(leastAvailableRAM));
}

/* Sample testProc for stressing the system.
 *   Multiple testProcs can execute in parallel threads for a given test case.
 *   However, ONLY ONE test case can run at a given time. The main thread should be
 *   used to sequence and configure test cases. */
static void StressorProc(Tilt *tilt, BitsEngine_TestThreadDesc *desc, u32 syncPoint) {
    switch (syncPoint) {
    case kSyncPoint_Allocs:
        u32 numIter = AllocsTest(tilt, desc->thread);
        TILT_INFO(tilt, "Stress thread %u ran for %u iterations", LT_Pu32(desc->threadNum), LT_Pu32(numIter));
        break;
    case kSyncPoint_RandomAllocs:
        /* Use one thread only (just let others immediately sync) */
        if (desc->threadNum == 0) RandomAllocsTest(tilt);
        break;
    }
    /* Done with this stressor iteration, so advance to the next syncPoint. Synchronize()
     * should be invoked at the end of testProcs just before they exit. */
    s_engine->API->Synchronize(s_engine, syncPoint + 1);
}

/* Test hooks */

/* Synchronization() is invoked in the main thread before testProcs are run for the
 * corresponding syncPoint. This callback should be used to configure and sequence
 * through test cases. */
static void Synchronization(Tilt *tilt, u32 syncPoint) {
    /* Here we use the main thread's synchronization points to sequence and report testing */
    switch (syncPoint) {
    case kSyncPoint_Allocs:
        tilt->API->StartTest("Allocs");
        tilt->API->ReportTestStarted();
        s_engine->API->SetSynchronizationTimeout(s_engine, LTTime_Seconds(60));
        break;
    case kSyncPoint_RandomAllocs:
        tilt->API->StartTest("Random Allocs");
        tilt->API->ReportTestStarted();
        break;
    default:
        /* Indicate that the entire test suite is complete */
        s_engine->API->FinishTesting(s_engine);
        break;
    }
    /* (Re-)Start test timing between test cases */
    tilt->API->StopwatchReset(s_stopwatch);
    tilt->API->StopwatchStart(s_stopwatch);
}

static void BeforeAllTests(Tilt *tilt) {
    /* Create a pool of test threads, configure and start them. */
    {
        s_threads = s_engine->API->CreateTestThreads(s_engine, kNumTestThreads, 1536);
        if (!s_threads) {
            TILT_ABORT_TESTING(tilt, "Cannot create test threads");
            return;
        }
        for (u32 ix = 0; ix < kNumTestThreads; ix++) {
            BitsEngine_TestThreadDesc *desc = s_threads->API->Get(s_threads, ix, NULL);
            /* Init and Fini test procs must be configured here (if required). */
            /* The testProc and clientData can be configured here or in Synchronization() */
            desc->testProc = StressorProc;
        }
        s_engine->API->StartTestThreads(s_engine);
    }
    /* Create a stopwatch for test timing and a pseudo-RNG */
    {
        s_stopwatch = tilt->API->StopwatchCreate();
        /* Allow test property to override default seed */
        const char *seedStr = tilt->API->GetProperty("seed", "0xba5eba11");
        u64 seed = lt_strtou64(seedStr, NULL, 16);
        s_prng = tilt->API->PrngCreate(seed, 33);
        TILT_INFO(tilt, "Using PRNG seed: 0x%llx", LT_Pu64(seed));
    }
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    s_engine->API->DestroyTestThreads(s_engine);
}

static const BitsEngineTestHooks s_hooks = {
    .BeforeAllTests  = BeforeAllTests,
    .AfterAllTests   = AfterAllTests,
    .Synchronization = Synchronization
};

/*_________________
  Test Executive */

static int BitsSampleTestImpl_Run(int argc, const char **argv) {
    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv, &s_hooks);
}

/*__________________
  Library Binding */

static bool BitsSampleTestImpl_LibInit(void) {
    /* NOTE: Please do not create the object(s) under test here,
     *   rather one should use the BeforeAllTests hook, the BeforeTest hook and/or individual TestProcs for this purpose */
    s_engine = lt_createobject(BitsEngine);
    return s_engine != NULL;
}

static void BitsSampleTestImpl_LibFini(void) {
    /* NOTE: Please do not destroy the object(s) under test here,
     *   rather one should use the AfterAllTests hook, the AfterTest hook and/or individual TestProcs for this purpose */
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(BitsSampleTest, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(BitsSampleTest, BitsSampleTestImpl_Run, 1536) LTLIBRARY_DEFINITION;

