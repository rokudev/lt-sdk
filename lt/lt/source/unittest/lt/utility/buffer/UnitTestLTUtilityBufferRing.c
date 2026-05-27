/******************************************************************************
 * UnitTestLTUtilityBufferRing.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/utility/buffer/LTRing.h>
#include <tilt/JiltEngine.h>

static JiltEngine *s_engine;

#define BUFFER_COUNT 4
#define USE_ATOMIC_BUFFER   1
#if USE_ATOMIC_BUFFER
    #define BUFFER_TYPE LTRingBufferAtomic
    #define StoreHead(rb, n) LTAtomic_Store(&((rb)->head), n)
    #define StoreTail(rb, n) LTAtomic_Store(&((rb)->tail), n)
#else
    #define BUFFER_TYPE LTRingBuffer
    #define StoreHead(rb, n) (rb)->head = n
    #define StoreTail(rb, n) (rb)->tail = n
#endif
    
/*******************************************************************************
 * Tests
 *******************************************************************************/

static void TestBasic(Tilt *tilt) {
    u16 backing[BUFFER_COUNT];
    BUFFER_TYPE rb;

    LTRingInit(&rb, backing, BUFFER_COUNT);
    TILT_ASSERT_TRUE(tilt, LTRingGetCapacity(&rb) == BUFFER_COUNT, "empty capacity");
    TILT_ASSERT_TRUE(tilt, LTRingGetSize(&rb) == 0, "empty size");
    TILT_ASSERT_TRUE(tilt,
                     LTRingGetContiguousCapacity(&rb) == BUFFER_COUNT,
                     "empty contiguous capacity");
    TILT_ASSERT_TRUE(tilt, LTRingGetContiguousSize(&rb) == 0, "empty contiguous size");

    StoreHead(&rb, 3);
    StoreTail(&rb, 3);
    TILT_ASSERT_TRUE(tilt, LTRingGetCapacity(&rb) == BUFFER_COUNT, "empty capacity");
    TILT_ASSERT_TRUE(tilt, LTRingGetSize(&rb) == 0, "empty size");
    TILT_ASSERT_TRUE(tilt, LTRingGetContiguousCapacity(&rb) == 1, "empty contiguous capacity");
    TILT_ASSERT_TRUE(tilt, LTRingGetContiguousSize(&rb) == 0, "empty contiguous size");

    StoreHead(&rb, 4);
    StoreTail(&rb, 4);
    TILT_ASSERT_TRUE(tilt, LTRingGetCapacity(&rb) == BUFFER_COUNT, "empty capacity");
    TILT_ASSERT_TRUE(tilt, LTRingGetSize(&rb) == 0, "empty size");
    TILT_ASSERT_TRUE(tilt,
                     LTRingGetContiguousCapacity(&rb) == BUFFER_COUNT,
                     "empty contiguous capacity");
    TILT_ASSERT_TRUE(tilt, LTRingGetContiguousSize(&rb) == 0, "empty contiguous size");

    StoreHead(&rb, 4);
    StoreTail(&rb, 0);
    TILT_ASSERT_TRUE(tilt, LTRingGetCapacity(&rb) == 0, "full capacity");
    TILT_ASSERT_TRUE(tilt, LTRingGetSize(&rb) == BUFFER_COUNT, "full size");
    TILT_ASSERT_TRUE(tilt, LTRingGetContiguousCapacity(&rb) == 0, "empty contiguous capacity");
    TILT_ASSERT_TRUE(tilt, LTRingGetContiguousSize(&rb) == BUFFER_COUNT, "empty contiguous size");

    StoreHead(&rb, 9);
    StoreTail(&rb, 5);
    TILT_ASSERT_TRUE(tilt, LTRingGetCapacity(&rb) == 0, "full capacity");
    TILT_ASSERT_TRUE(tilt, LTRingGetSize(&rb) == BUFFER_COUNT, "full size");
    TILT_ASSERT_TRUE(tilt, LTRingGetContiguousCapacity(&rb) == 0, "empty contiguous capacity");
    TILT_ASSERT_TRUE(tilt, LTRingGetContiguousSize(&rb) == 3, "empty contiguous size");

    /* integer wraparound reflects long-running fifos reaching int wraparound */
    StoreHead(&rb, 1);
    StoreTail(&rb, (u32) - (BUFFER_COUNT - 2));
     TILT_ASSERT_TRUE(tilt,
                     LTRingGetCapacity(&rb) == (BUFFER_COUNT - 3),
                     "one element can be written");
    TILT_ASSERT_TRUE(tilt, LTRingGetSize(&rb) == 3, "three elements available");
    TILT_ASSERT_TRUE(tilt,
                     LTRingGetContiguousCapacity(&rb) == (BUFFER_COUNT - 3),
                     "should be able to write one element");
    TILT_ASSERT_TRUE(tilt, LTRingGetContiguousSize(&rb) == 2, "empty contiguous size");
}

static void TestRW(Tilt *tilt) {
    u16 backing[BUFFER_COUNT];
    BUFFER_TYPE rb;
    LTRingInit(&rb, backing, BUFFER_COUNT);

    TILT_ASSERT_TRUE(tilt, LTRingPeek(&rb, 0) == NULL, "can't peek empty buffer");
    TILT_ASSERT_FALSE(tilt,
                      LTRingReserve(&rb, BUFFER_COUNT + 1),
                      "can't submit more elements than space");
    TILT_ASSERT_TRUE(tilt, LTRingReserve(&rb, BUFFER_COUNT), "submit entire buffer");
    TILT_ASSERT_TRUE(tilt, LTRingGetCapacity(&rb) == 0, "buffer is now full");
    TILT_ASSERT_TRUE(tilt, LTRingPeek(&rb, 0) == &backing[0], "ready to consume first element");
    TILT_ASSERT_FALSE(tilt,
                      LTRingConsume(&rb, BUFFER_COUNT + 1),
                      "can't submit more elements than submitted");
    TILT_ASSERT_TRUE(tilt, LTRingConsume(&rb, BUFFER_COUNT), "consume everything submitted");
    TILT_ASSERT_TRUE(tilt, LTRingGetSize(&rb) == 0, "buffer is now empty again");
}

/*******************************************************************************
 * Tilt
 ******************************************************************************/

static const TiltEngineTest s_tests[] = {
    { TestBasic, "Basic", "Basic operations",      0 },
    { TestRW,    "RW",    "Read/Write operations", 0 },
};

/*******************************************************************************
 * Library
 ******************************************************************************/

static int UnitTestLTUtilityBufferRingImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), NULL);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTUtilityBufferRingImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTUtilityBufferRingImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityBufferRing, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityBufferRing, UnitTestLTUtilityBufferRingImpl_Run, 1536) LTLIBRARY_DEFINITION;
