/*******************************************************************************
 * unittest/lt/core/UnitTestLTCoreAssociativeArray.c
 *  LTAssociativeArray unit tests
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTArray.h>
#include <tilt/JiltEngine.h>

static JiltEngine *s_engine;

/*******************************************************************************
 * Static Variables
 ******************************************************************************/

static const char *s_errorCreate      = "failed to create";
static const char *s_errorSet         = "failed to set value";
static const char *s_errorNotFound    = "cannot find value";
static const char *s_errorBadValue    = "value is not correct";
static const char *s_errorBadCount    = "incorrect element count";
static const char *s_errorBadSize     = "incorrect element size";
static const char *s_errorBadReturn   = "bad return value";
static const char *s_errorExists      = "value shouldn't exist";
static const char *s_errorArray       = "bad array object";

static LTAssociativeArray *s_array = NULL;

/*******************************************************************************
 * Test Vectors
 ******************************************************************************/

typedef struct {
   u64 tmp1;
   u32 tmp2;
} TestData;

enum { kNumKeys = 5 };
static const char    *s_key[kNumKeys] = { "zero", "one", "this_is_a_large_key", "this_is_an_even_larger_key", "four" };
static const LT_SIZE s_ptr[kNumKeys]  = { 137, 111, 1330, 4200, 24000 };
static const TestData s_str[kNumKeys] = { { 100, 2 }, { 42, 70 }, { 67, 81 }, { 77, 44 }, { 22, 55 } };
static const char    *s_badKey        = "abc";

/*******************************************************************************
 * Tests
 ******************************************************************************/

static bool PointerArrayCallback(LTAssociativeArray *array, const void *key, u16 keySize, void *value, void *clientData) {
    Tilt *tilt = (Tilt *)clientData;
    TILT_EXPECT_TRUE(tilt, array == s_array, s_errorArray);
    bool found = false;
    for (u32 i = 0; i < kNumKeys; i++) {
        if (lt_strlen(s_key[i]) == keySize && lt_memcmp(key, s_key[i], keySize) == 0) {
            TILT_EXPECT_TRUE(tilt, (void *)value == (void *)(s_ptr[i]), s_errorBadValue);
            found = true;
            break;
        }
    }
    TILT_EXPECT_TRUE(tilt, found, s_errorNotFound);
    return true;
}

static void TestPointerArray(Tilt *tilt) {
    LT_SIZE ptrTemp;

    /* Test Set, Trim, Get, Count and Size */
    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == 0, s_errorBadCount);
    TILT_EXPECT_TRUE(tilt, s_array->API->GetElementSize(s_array) == sizeof(LT_SIZE), s_errorBadSize);

    for (u32 i = 0; i < kNumKeys; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Set(s_array, s_key[i], lt_strlen(s_key[i]), (void *)s_ptr[i]), s_errorSet);
    }

    s_array->API->Trim(s_array);

    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == kNumKeys, s_errorBadCount);

    for (u32 i = 0; i < kNumKeys; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, s_key[i], lt_strlen(s_key[i]), NULL) == (void *)(s_ptr[i]), s_errorNotFound);
        TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, s_key[i], lt_strlen(s_key[i]), &ptrTemp), s_errorNotFound);
        TILT_EXPECT_TRUE(tilt, ptrTemp == s_ptr[i], s_errorBadValue);
    }

    /* Test Enumerate */
    TILT_EXPECT_TRUE(tilt, s_array->API->Enumerate(s_array, PointerArrayCallback, (void *)tilt), s_errorBadReturn);

    /* Test Exists and missing key */
    ptrTemp = 500;
    TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, s_badKey, lt_strlen(s_badKey), &ptrTemp) == NULL, s_errorExists);
    TILT_EXPECT_TRUE(tilt, ptrTemp == 500, s_errorExists);
    TILT_EXPECT_FALSE(tilt, s_array->API->Exists(s_array, s_badKey, lt_strlen(s_badKey)), s_errorExists);

    for (u32 i = 0; i < kNumKeys; i++)
        TILT_EXPECT_TRUE(tilt, s_array->API->Exists(s_array, s_key[i], lt_strlen(s_key[i])), s_errorNotFound);

    /* Test Remove, Exists and Count */
    for (u32 i = 0; i < kNumKeys; i++) {
        s_array->API->Remove(s_array, s_key[i], lt_strlen(s_key[i]));
        TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == (kNumKeys - i - 1), s_errorBadCount);
        TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, s_key[i], lt_strlen(s_key[i]), NULL) == NULL, s_errorExists);
        for (u32 j = i + 1; j < kNumKeys; j++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, s_key[j], lt_strlen(s_key[j]), NULL), s_errorNotFound);
            TILT_EXPECT_TRUE(tilt, s_array->API->Exists(s_array, s_key[j], lt_strlen(s_key[j])), s_errorNotFound);
        }
    }
}

static bool StructArrayCallback(LTAssociativeArray *array, const void *key, u16 keySize, void *value, void *clientData) {
    Tilt *tilt = (Tilt *)clientData;
    TILT_EXPECT_TRUE(tilt, array == s_array, s_errorArray);
    bool found = false;
    for (u32 i = 0; i < kNumKeys; i++) {
        if (lt_strlen(s_key[i]) == keySize && lt_memcmp(key, s_key[i], keySize) == 0) {
            TestData *data = (TestData *)value;
            TILT_EXPECT_TRUE(tilt, data->tmp1 == s_str[i].tmp1, s_errorBadValue);
            found = true;
            break;
        }
    }
    TILT_EXPECT_TRUE(tilt, found, s_errorNotFound);
    return true;
}

static void TestStructArray(Tilt *tilt) {
    TestData strTemp;

    s_array->API->InitAsStructArray(s_array, sizeof(TestData));

    /* Test Set, Trim, Get, Count and Size */
    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == 0, s_errorBadCount);
    TILT_EXPECT_TRUE(tilt, s_array->API->GetElementSize(s_array) == sizeof(TestData), s_errorBadSize);

    for (u32 i = 0; i < kNumKeys; i++)
        TILT_EXPECT_TRUE(tilt, s_array->API->Set(s_array, s_key[i], lt_strlen(s_key[i]), &s_str[i]), s_errorSet);

    s_array->API->Trim(s_array);

    for (u32 i = 0; i < kNumKeys; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, s_key[i], lt_strlen(s_key[i]), &strTemp), s_errorNotFound);
        TILT_EXPECT_TRUE(tilt, strTemp.tmp1 == s_str[i].tmp1, s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, strTemp.tmp2 == s_str[i].tmp2, s_errorBadValue);
    }

    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == kNumKeys, s_errorBadCount);

    for (u32 i = 0; i < kNumKeys; i++) {
        TestData *str = s_array->API->Get(s_array, s_key[i], lt_strlen(s_key[i]), NULL);
        TILT_EXPECT_TRUE(tilt, str, s_errorNotFound);
        if (str) {
            TILT_EXPECT_TRUE(tilt, str->tmp1 == s_str[i].tmp1, s_errorBadValue);
            TILT_EXPECT_TRUE(tilt, str->tmp2 == s_str[i].tmp2, s_errorBadValue);
        }
    }

    /* Test Enumerate */
    TILT_EXPECT_TRUE(tilt, s_array->API->Enumerate(s_array, StructArrayCallback, (void *)tilt), s_errorBadReturn);

    /* Test Exists and missing key */
    strTemp.tmp1 = 888;
    TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, s_badKey, lt_strlen(s_badKey), &strTemp) == NULL, s_errorExists);
    TILT_EXPECT_TRUE(tilt, strTemp.tmp1 == 888, s_errorExists);
    TILT_EXPECT_FALSE(tilt, s_array->API->Exists(s_array, s_badKey, lt_strlen(s_badKey)), s_errorExists);

    for (u32 i = 0; i < kNumKeys; i++)
        TILT_EXPECT_TRUE(tilt, s_array->API->Exists(s_array, s_key[i], lt_strlen(s_key[i])), s_errorNotFound);

    /* Test Remove, Exists and Count */
    for (u32 i = 0; i < kNumKeys; i++) {
        s_array->API->Remove(s_array, s_key[i], lt_strlen(s_key[i]));
        TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == (kNumKeys - i - 1), s_errorBadCount);
        TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, s_key[i], lt_strlen(s_key[i]), NULL) == NULL, s_errorExists);
        for (u32 j = i + 1; j < kNumKeys; j++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, s_key[j], lt_strlen(s_key[j]), NULL), s_errorNotFound);
            TILT_EXPECT_TRUE(tilt, s_array->API->Exists(s_array, s_key[j], lt_strlen(s_key[j])), s_errorNotFound);
        }
    }

    /* Adding back an element after previously having removed them all */
    TILT_EXPECT_TRUE(tilt, s_array->API->Set(s_array, s_key[0], lt_strlen(s_key[0]), &s_str[0]), s_errorSet);

    /* Set zeroed data */
    TILT_EXPECT_TRUE(tilt, s_array->API->Set(s_array, s_key[0], lt_strlen(s_key[0]), NULL), s_errorBadReturn);
    TestData *data = s_array->API->Get(s_array, s_key[0], lt_strlen(s_key[0]), NULL);
    TILT_EXPECT_TRUE(tilt, data->tmp1 == 0, s_errorBadValue);
    TILT_EXPECT_TRUE(tilt, data->tmp2 == 0, s_errorBadValue);

    /* Test Re-InitAsStructArray on non-empty array */
    s_array->API->InitAsStructArray(s_array, sizeof(u32));
    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == 0, s_errorBadCount);
    u32 temp = 33;
    TILT_EXPECT_TRUE(tilt, s_array->API->Set(s_array, s_key[1], lt_strlen(s_key[1]), &temp), s_errorSet);
    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == 1, s_errorBadCount);
}

static void TestOddAlignment(Tilt *tilt) {
    enum { kOddDataSize = 3 };
    const u8 data[kOddDataSize] = { 33, 34, 35 };
    u8 temp[kOddDataSize];

    s_array->API->InitAsStructArray(s_array, kOddDataSize);
    for (u32 i = 0; i < kNumKeys; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Set(s_array, s_key[i], lt_strlen(s_key[i]), data), s_errorSet);
    }

    for (u32 i = 0; i < kNumKeys; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, s_key[i], lt_strlen(s_key[i]), temp), s_errorNotFound);
        TILT_EXPECT_TRUE(tilt, data[0] == temp[0] && data[1] == temp[1] && data[2] == temp[2], s_errorBadValue);
    }

    TILT_EXPECT_TRUE(tilt, s_array->API->GetElementSize(s_array) == kOddDataSize, s_errorBadSize);
}

/*******************************************************************************
 * Test Hooks
 ******************************************************************************/

static void BeforeTest(Tilt *tilt) {
    s_array = lt_createobject(LTAssociativeArray);
    TILT_ASSERT_TRUE(tilt, s_array, s_errorCreate);
}

static void AfterTest(Tilt *tilt) {
    s_array->API->RemoveAll(s_array, true);
    TILT_EXPECT_FALSE(tilt, s_array->API->Exists(s_array, s_key[0], lt_strlen(s_key[0])), s_errorExists);
    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == 0, s_errorBadCount);
    lt_destroyobject(s_array);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeTest     = BeforeTest,
    .AfterTest      = AfterTest,
};

static const TiltEngineTest s_tests[] = {
    { TestPointerArray,  "PointerArray", "Pointer array",       0 },
    { TestStructArray,   "StructArray",  "Struct array",        0 },
    { TestOddAlignment,  "Alignments",   "Odd data alignments", 0 },
};

/*******************************************************************************
 * Library Root Interface Binding
 ******************************************************************************/

static int UnitTestLTCoreAssociativeArrayImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTCoreAssociativeArrayImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return (s_engine != NULL);
}

static void UnitTestLTCoreAssociativeArrayImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTCoreAssociativeArray, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTCoreAssociativeArray, UnitTestLTCoreAssociativeArrayImpl_Run, 1536) LTLIBRARY_DEFINITION;
