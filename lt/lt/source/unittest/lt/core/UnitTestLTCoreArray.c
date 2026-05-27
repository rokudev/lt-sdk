/*******************************************************************************
 * unittest/lt/core/UnitTestLTCoreArray.c
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

typedef struct {
    u64 tmp1;
    u32 tmp2;
} TestData;

/*******************************************************************************
 * Static Variables
 ******************************************************************************/

static const char *s_errorCreate      = "failed to create";
static const char *s_errorAdd         = "failed to add value";
static const char *s_errorNotFound    = "cannot find value";
static const char *s_errorBadValue    = "value is not correct";
static const char *s_errorGetStorage  = "error getting storage";
static const char *s_errorBadCount    = "incorrect element count";
static const char *s_errorBadSize     = "incorrect element size";
static const char *s_errorBadReturn   = "bad return value";
static const char *s_errorExists      = "value shouldn't exist";

static LTArray    *s_array  = NULL;
static LTArray    *s_arrayQ = NULL;

/*******************************************************************************
 * Tests
 ******************************************************************************/

static void TestPointerArray(Tilt *tilt) {
    enum { kNumElms = 5 };
    static const LT_SIZE  s_ptr[kNumElms] = { 137, 111, 24000, 4200, 1330 };
    LT_SIZE ptrTemp;

    /* Test Append, Size, Count, Trim and Get */
    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == 0, s_errorBadCount);
    TILT_EXPECT_TRUE(tilt, s_array->API->GetElementSize(s_array) == sizeof(LT_SIZE), s_errorBadSize);

    for (u32 i = 0; i < kNumElms; i++)
        TILT_EXPECT_TRUE(tilt, s_array->API->Append(s_array, (void *)s_ptr[i]) == (s32)i, s_errorAdd);

    s_array->API->Trim(s_array);

    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == kNumElms, s_errorBadCount);

    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, i, (void *)&ptrTemp) == (void *)s_ptr[i], s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, ptrTemp == s_ptr[i], s_errorBadValue);
    }

    /* Get (out of range) */
    ptrTemp = 888;
    TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, kNumElms, (void *)&ptrTemp) == NULL, s_errorExists);
    TILT_EXPECT_TRUE(tilt, ptrTemp == 888, s_errorBadValue);

    /* Test Set, Remove and Insert */
    s_array->API->Set(s_array, 0, (void *)333);
    s_array->API->Set(s_array, kNumElms - 1, (void *)777);
    TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, 0, NULL) == (void *)333, s_errorBadValue);
    TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, kNumElms - 1, NULL) == (void *)777, s_errorBadValue);

    for (s32 i = kNumElms - 1; i >= 0; i--) {
        s_array->API->Remove(s_array, 0);
        TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == (u32)i, s_errorBadCount);
    }

    for (s32 i = kNumElms - 1; i >= 0;  i--) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Insert(s_array, 0, (void *)s_ptr[i]) == 0, s_errorAdd);
    }
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, 0, (void *)&ptrTemp) == (void *)s_ptr[i], s_errorBadValue);
        s_array->API->Remove(s_array, 0);
    }
    for (s32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Insert(s_array, i, (void *)s_ptr[i]) == i, s_errorAdd);
    }
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, 0, (void *)&ptrTemp) == (void *)s_ptr[i], s_errorBadValue);
        s_array->API->Remove(s_array, 0);
    }
}

static void TestPointerArrayQ(Tilt *tilt) {
    enum { kNumElms = 5 };
    static const LT_SIZE  s_ptr[kNumElms] = { 137, 111, 24000, 4200, 1330 };
    LT_SIZE ptrTemp;

    /* Test Append, Size, Count, Trim and Get */
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->GetCount(s_arrayQ) == 0, s_errorBadCount);
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->GetElementSize(s_arrayQ) == sizeof(LT_SIZE), s_errorBadSize);

    for (u32 i = 0; i < kNumElms; i++)
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Append(s_arrayQ, (void *)s_ptr[i]) == (s32)i, s_errorAdd);

    s_arrayQ->API->Trim(s_arrayQ);

    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Get(s_arrayQ, i, (void *)&ptrTemp) == (void *)s_ptr[i], s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, ptrTemp == s_ptr[i], s_errorBadValue);
    }
    for (s32 i = kNumElms - 1; i >= 0; i--) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Get(s_arrayQ, i, (void *)&ptrTemp) == (void *)s_ptr[i], s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, ptrTemp == s_ptr[i], s_errorBadValue);
    }

    /* Get (out of range) */
    ptrTemp = 888;
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Get(s_arrayQ, kNumElms, (void *)&ptrTemp) == NULL, s_errorExists);
    TILT_EXPECT_TRUE(tilt, ptrTemp == 888, s_errorBadValue);

    /* Test Set, Remove and Insert */
    s_arrayQ->API->Set(s_arrayQ, 0, (void *)333);
    s_arrayQ->API->Set(s_arrayQ, kNumElms - 1, (void *)777);
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Get(s_arrayQ, 0, NULL) == (void *)333, s_errorBadValue);
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Get(s_arrayQ, kNumElms - 1, NULL) == (void *)777, s_errorBadValue);

    for (s32 i = kNumElms - 1; i >= 0; i--) {
        s_arrayQ->API->Remove(s_arrayQ, 0);
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->GetCount(s_arrayQ) == (u32)i, s_errorBadCount);
    }

    for (s32 i = kNumElms - 1; i >= 0; i--) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Insert(s_arrayQ, 0, (void *)s_ptr[i]) == 0, s_errorAdd);
    }
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Get(s_arrayQ, 0, (void *)&ptrTemp) == (void *)s_ptr[i], s_errorBadValue);
        s_arrayQ->API->Remove(s_arrayQ, 0);
    }
    for (s32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Insert(s_arrayQ, i, (void *)s_ptr[i]) == i, s_errorAdd);
    }
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Get(s_arrayQ, 0, (void *)&ptrTemp) == (void *)s_ptr[i], s_errorBadValue);
        s_arrayQ->API->Remove(s_arrayQ, 0);
    }
    for (s32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Insert(s_arrayQ, i, (void *)s_ptr[i]) == i, s_errorAdd);
    }
    for (s32 i = kNumElms - 1; i >= 0; i--) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Get(s_arrayQ, i, (void *)&ptrTemp) == (void *)s_ptr[i], s_errorBadValue);
        s_arrayQ->API->Remove(s_arrayQ, i);
    }

    /* Random-access test */
    {
        enum { kNumElms = 12 };
        for (s32 i = kNumElms - 1; i >= 0; i--) {
            s_arrayQ->API->Append(s_arrayQ, (void *)33);
        }
        LT_SIZE temp;
        s_arrayQ->API->Get(s_arrayQ, 4, &temp);
        TILT_EXPECT_TRUE(tilt, temp == 33, s_errorBadValue);

        s_arrayQ->API->SetCount(s_arrayQ, 0);
        for (LT_SSIZE i = kNumElms - 1; i >= 0; i--) {
            s_arrayQ->API->Insert(s_arrayQ, 0, (void *)i);
        }
        const u32 test[] = { 3, 7, 5, 5, 9, 1, 11, 0, 4, 3, 6 };
        for (u32 i = 0; i < sizeof(test)/sizeof(test[0]); i++) {
            s_arrayQ->API->Get(s_arrayQ, test[i], &temp);
            TILT_EXPECT_TRUE(tilt, temp == test[i], s_errorBadValue);
        }
        s_arrayQ->API->SetCount(s_arrayQ, 0);
    }

    /* LTString Array FIFO */
    {
        enum { kNumElms = 5 };
        static const char *s_str[kNumElms] = { "one", "two", "three", "four", "five" };

        for (u32 i = 0; i < kNumElms; i++) {
            LTString elm = ltstring_create(s_str[i]);
            s_arrayQ->API->Append(s_arrayQ, elm);
        }

        for (u32 i = 0; i < kNumElms; i++) {
            LTString str = s_arrayQ->API->Get(s_arrayQ, 0, NULL);
            ltstring_destroy(str);
            s_arrayQ->API->Remove(s_arrayQ, 0);

            for (s32 j = s_arrayQ->API->GetCount(s_arrayQ) - 1; j >= 0; j--) {
                LTString str2 = s_arrayQ->API->Get(s_arrayQ, j, NULL);
                TILT_EXPECT_TRUE(tilt, lt_strcmp(str2, s_str[i + j + 1]) == 0, s_errorBadValue);
            }
        }
    }
}

static int StructCompareFunction(const void *element1, const void *element2, void *userData) {
    if (userData != s_array && userData != s_arrayQ) return 0;
    u32 lhs = ((TestData *)element1)->tmp2;
    u32 rhs = ((TestData *)element2)->tmp2;
    if (lhs < rhs) return -1;
    else if (lhs > rhs) return 1;
    else return 0;
}

static void TestStructArray(Tilt *tilt) {
    enum { kNumElms = 5 };
    static const TestData s_str[kNumElms]          = { { 100, 2 }, { 42, 70 }, { 67, 81 }, { 77, 44 }, { 22, 55 } };
    static const u8       s_sortedStrIdx[kNumElms] = {        0,          3,          4,          1,          2 };
    TestData strTemp;

    s_array->API->InitAsStructArray(s_array, sizeof(TestData));

    /* Test Count and Size */
    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == 0, s_errorBadCount);
    TILT_EXPECT_TRUE(tilt, s_array->API->GetElementSize(s_array) == sizeof(TestData), s_errorBadSize);

    /* Test Append, Trim, Sort, InsertSorted and Find */
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Append(s_array, &s_str[i]) == (s32)i, s_errorAdd);
    }

    s_array->API->Trim(s_array);

    s_array->API->Sort(s_array, StructCompareFunction, s_array);
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Find(s_array, StructCompareFunction, &s_str[i], s_array) == (s32)s_sortedStrIdx[i], s_errorAdd);
    }

    s_array->API->SetCount(s_array, 0);
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->InsertSorted(s_array, StructCompareFunction, &s_str[i], s_array) >= 0, s_errorAdd);
    }
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Find(s_array, StructCompareFunction, &s_str[i], s_array) == (s32)s_sortedStrIdx[i], s_errorAdd);
    }

    /* Test Set and Get */
    for (u32 i = 0; i < kNumElms; i++) {
        s_array->API->Set(s_array, i, &s_str[i]);
    }
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, i, &strTemp) != NULL, s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, strTemp.tmp1 == s_str[i].tmp1, s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, strTemp.tmp2 == s_str[i].tmp2, s_errorBadValue);
    }
    for (u32 i = 0; i < kNumElms; i++) {
        TestData *str = s_array->API->Get(s_array, i, NULL);
        TILT_EXPECT_TRUE(tilt, str->tmp1 == s_str[i].tmp1, s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, str->tmp2 == s_str[i].tmp2, s_errorBadValue);
    }

    /* Test GetStorage */
    TILT_EXPECT_TRUE(tilt, s_array->API->GetStorage(s_array) == s_array->API->Get(s_array, 0, NULL), s_errorGetStorage);

    /* Test Insert and Remove */
    s_array->API->SetCount(s_array, 0);
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Insert(s_array, s_array->API->GetCount(s_array), &s_str[i]) == (s32)i, s_errorAdd);
    }
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, 0, &strTemp) != NULL, s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, strTemp.tmp1 == s_str[i].tmp1, s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, strTemp.tmp2 == s_str[i].tmp2, s_errorBadValue);
        s_array->API->Remove(s_array, 0);
    }
    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == 0, s_errorBadCount);

    /* Adding back an element after previously having removed them all */
    TILT_EXPECT_TRUE(tilt, s_array->API->Append(s_array, &s_str[0]) == 0, s_errorAdd);

    /* Append zeroed data */
    s32 idx = s_array->API->Append(s_array, NULL);
    TILT_EXPECT_TRUE(tilt, idx >= 0, s_errorBadReturn);
    TestData *data = s_array->API->Get(s_array, idx, NULL);
    TILT_EXPECT_TRUE(tilt, data->tmp1 == 0, s_errorBadValue);
    TILT_EXPECT_TRUE(tilt, data->tmp2 == 0, s_errorBadValue);

    /* Insert zeroed data */
    idx = s_array->API->Insert(s_array, idx, NULL);
    TILT_EXPECT_TRUE(tilt, idx >= 0, s_errorBadReturn);
    data = s_array->API->Get(s_array, idx, NULL);
    TILT_EXPECT_TRUE(tilt, data->tmp1 == 0, s_errorBadValue);
    TILT_EXPECT_TRUE(tilt, data->tmp2 == 0, s_errorBadValue);

    /* Set zeroed data */
    s_array->API->Set(s_array, 0, NULL);
    data = s_array->API->Get(s_array, 0, NULL);
    TILT_EXPECT_TRUE(tilt, data->tmp1 == 0, s_errorBadValue);
    TILT_EXPECT_TRUE(tilt, data->tmp2 == 0, s_errorBadValue);

    /* Test Re-InitAsStructArray on non-empty array */
    s_array->API->InitAsStructArray(s_array, sizeof(u32));
    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == 0, s_errorBadCount);
    u32 temp = 33;
    TILT_EXPECT_TRUE(tilt, s_array->API->Append(s_array, &temp) == 0, s_errorAdd);
    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == 1, s_errorBadCount);
}

static void TestStructArrayQ(Tilt *tilt) {
    enum { kNumElms = 5 };
    static const TestData s_str[kNumElms]          = { { 100, 2 }, { 42, 70 }, { 67, 81 }, { 77, 44 }, { 22, 55 } };
    static const u8       s_sortedStrIdx[kNumElms] = {        0,          3,          4,          1,          2 };
    TestData strTemp;

    s_arrayQ->API->InitAsStructArray(s_arrayQ, sizeof(TestData));

    /* Test Count and Size */
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->GetCount(s_arrayQ) == 0, s_errorBadCount);
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->GetElementSize(s_arrayQ) == sizeof(TestData), s_errorBadSize);

    /* Test Append, Trim, Sort, InsertSorted and Find */
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Append(s_arrayQ, &s_str[i]) == (s32)i, s_errorAdd);
    }

    s_arrayQ->API->Trim(s_arrayQ);

    s_arrayQ->API->Sort(s_arrayQ, StructCompareFunction, s_arrayQ);
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Find(s_arrayQ, StructCompareFunction, &s_str[i], s_arrayQ) == (s32)s_sortedStrIdx[i], s_errorAdd);
    }

    s_arrayQ->API->SetCount(s_arrayQ, 0);
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->InsertSorted(s_arrayQ, StructCompareFunction, &s_str[i], s_arrayQ) >= 0, s_errorAdd);
    }
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Find(s_arrayQ, StructCompareFunction, &s_str[i], s_arrayQ) == (s32)s_sortedStrIdx[i], s_errorAdd);
    }

    /* Test Set and Get */
    for (u32 i = 0; i < kNumElms; i++) {
        s_arrayQ->API->Set(s_arrayQ, i, &s_str[i]);
    }

    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Get(s_arrayQ, i, &strTemp) != NULL, s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, strTemp.tmp1 == s_str[i].tmp1, s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, strTemp.tmp2 == s_str[i].tmp2, s_errorBadValue);
    }
    for (u32 i = 0; i < kNumElms; i++) {
        TestData *str = s_arrayQ->API->Get(s_arrayQ, i, NULL);
        TILT_EXPECT_TRUE(tilt, str->tmp1 == s_str[i].tmp1, s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, str->tmp2 == s_str[i].tmp2, s_errorBadValue);
    }

    /* Test GetStorage */
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->GetStorage(s_arrayQ) == NULL, s_errorGetStorage);

    /* Test Insert and Remove */
    s_arrayQ->API->SetCount(s_arrayQ, 0);
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Insert(s_arrayQ, s_arrayQ->API->GetCount(s_arrayQ), &s_str[i]) == (s32)i, s_errorAdd);
    }
    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Get(s_arrayQ, 0, &strTemp) != NULL, s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, strTemp.tmp1 == s_str[i].tmp1, s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, strTemp.tmp2 == s_str[i].tmp2, s_errorBadValue);
        s_arrayQ->API->Remove(s_arrayQ, 0);
    }
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->GetCount(s_arrayQ) == 0, s_errorBadCount);

    /* Adding back an element after previously having removed them all */
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Append(s_arrayQ, &s_str[0]) == 0, s_errorAdd);

    /* Append zeroed data */
    s32 idx = s_arrayQ->API->Append(s_arrayQ, NULL);
    TILT_EXPECT_TRUE(tilt, idx >= 0, s_errorBadReturn);
    TestData *data = s_arrayQ->API->Get(s_arrayQ, idx, NULL);
    TILT_EXPECT_TRUE(tilt, data->tmp1 == 0, s_errorBadValue);
    TILT_EXPECT_TRUE(tilt, data->tmp2 == 0, s_errorBadValue);

    /* Insert zeroed data */
    idx = s_arrayQ->API->Insert(s_arrayQ, idx, NULL);
    TILT_EXPECT_TRUE(tilt, idx >= 0, s_errorBadReturn);
    data = s_arrayQ->API->Get(s_arrayQ, idx, NULL);
    TILT_EXPECT_TRUE(tilt, data->tmp1 == 0, s_errorBadValue);
    TILT_EXPECT_TRUE(tilt, data->tmp2 == 0, s_errorBadValue);

    /* Set zeroed data */
    s_arrayQ->API->Set(s_arrayQ, 0, NULL);
    data = s_arrayQ->API->Get(s_arrayQ, 0, NULL);
    TILT_EXPECT_TRUE(tilt, data->tmp1 == 0, s_errorBadValue);
    TILT_EXPECT_TRUE(tilt, data->tmp2 == 0, s_errorBadValue);

    /* Test Re-InitAsStructArray on non-empty array */
    s_arrayQ->API->InitAsStructArray(s_arrayQ, sizeof(u32));
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->GetCount(s_arrayQ) == 0, s_errorBadCount);
    u32 temp = 33;
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->Append(s_arrayQ, &temp) == 0, s_errorAdd);
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->GetCount(s_arrayQ) == 1, s_errorBadCount);
}

static void TestCopyArray(Tilt *tilt) {
    enum { kNumElms = 13 };

    for (LT_SIZE i = 0; i < kNumElms; i++) {
        s_array->API->Insert(s_array, 0, (void *)i);
    }

    LTArray *array = lt_createobject(LTArray);
    TILT_ASSERT_TRUE(tilt, array, s_errorCreate);
    TILT_ASSERT_TRUE(tilt, array->API->CopyArray(array, s_array), s_errorAdd);

    TILT_EXPECT_TRUE(tilt, array->API->GetCount(array) == kNumElms, s_errorBadCount);
    TILT_EXPECT_TRUE(tilt, array->API->GetElementSize(array) == sizeof(LT_SIZE), s_errorBadCount);

    array->API->Sort(array, array->API->CompareInteger, (void *)kLTArrayCompare_UnsignedAscending);
    for (LT_SIZE i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, array->API->Get(array, i, NULL) == (void *)i, s_errorBadValue);
    }

    lt_destroyobject(array);
}

static void TestCopyArrayQ(Tilt *tilt) {
    enum { kNumElms = 17 };

    for (LT_SIZE i = 0; i < kNumElms; i++) {
        s_arrayQ->API->Insert(s_arrayQ, 0, (void *)i);
    }

    LTArray *arrayQ = lt_createobject(LTArray, List);
    TILT_ASSERT_TRUE(tilt, arrayQ, s_errorCreate);
    TILT_ASSERT_TRUE(tilt, arrayQ->API->CopyArray(arrayQ, s_arrayQ), s_errorAdd);

    TILT_EXPECT_TRUE(tilt, arrayQ->API->GetCount(arrayQ) == kNumElms, s_errorBadCount);
    TILT_EXPECT_TRUE(tilt, arrayQ->API->GetElementSize(arrayQ) == sizeof(LT_SIZE), s_errorBadCount);

    arrayQ->API->Sort(arrayQ, arrayQ->API->CompareInteger, (void *)kLTArrayCompare_UnsignedAscending);
    for (LT_SIZE i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, arrayQ->API->Get(arrayQ, i, NULL) == (void *)i, s_errorBadValue);
    }

    lt_destroyobject(arrayQ);
}

static void TestFindsAndSorts(Tilt *tilt) {
    /* Ascending/Descending alphabetical */
    {
        enum { kNumElms = 4 };
        static const char *s_strings[kNumElms] = { "quick", "brown", "fox", "jumped" };
        static const u8    s_sortIdx[kNumElms] = {       3,       0,     1,       2  };

        /* Test InsertSorted, Find and Sort */
        for (u32 i = 0; i < kNumElms; i++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->InsertSorted(s_array, s_array->API->CompareCString, s_strings[i],
                                                                (void *)kLTArrayCompare_Ascending) >= 0, s_errorAdd);
        }
        for (u32 i = 0; i < kNumElms; i++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->Find(s_array, s_array->API->CompareCString, s_strings[i],
                                                        (void *)kLTArrayCompare_Ascending) == s_sortIdx[i], s_errorNotFound);
        }

        s_array->API->Sort(s_array, s_array->API->CompareCString, (void *)kLTArrayCompare_Descending);
        for (u32 i = 0; i < kNumElms; i++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->Find(s_array, s_array->API->CompareCString, s_strings[i],
                                                        (void *)kLTArrayCompare_Descending) == kNumElms - s_sortIdx[i] - 1, s_errorNotFound);
        }

        TILT_EXPECT_TRUE(tilt, s_array->API->Find(s_array, s_array->API->CompareCString, "over",
                                                    (void *)kLTArrayCompare_Descending) == - 1, s_errorExists);
    }

    /* Ascending/Descending alphabetical (ignore case) */
    TILT_EXPECT_TRUE(tilt, s_array->API->SetCount(s_array, 0), s_errorBadReturn);
    {
        enum { kNumElms = 5 };
        static const char *s_strings[kNumElms] = { "Fox", "Quick", "brown", "jumped",  "ABC" };
        static const u8    s_sortIdx[kNumElms] = {     2,       4,       1,        3,      0  };

        /* Test InsertSorted, Find and Sort */
        for (u32 i = 0; i < kNumElms; i++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->InsertSorted(s_array, s_array->API->CompareCString, s_strings[i],
                                                                (void *)kLTArrayCompare_IgnoreCaseAscending) >= 0, s_errorAdd);
        }
        for (u32 i = 0; i < kNumElms; i++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->Find(s_array, s_array->API->CompareCString, s_strings[i],
                                                        (void *)kLTArrayCompare_IgnoreCaseAscending) == s_sortIdx[i], s_errorNotFound);
        }

        s_array->API->Sort(s_array, s_array->API->CompareCString, (void *)kLTArrayCompare_IgnoreCaseDescending);
        for (u32 i = 0; i < kNumElms; i++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->Find(s_array, s_array->API->CompareCString, s_strings[i],
                                                        (void *)kLTArrayCompare_IgnoreCaseDescending) == kNumElms - s_sortIdx[i] - 1, s_errorNotFound);
        }
    }

    /* Ascending/Descending unsigned integer */
    TILT_EXPECT_TRUE(tilt, s_array->API->SetCount(s_array, 0), s_errorBadReturn);
    {
        enum { kNumElms = 5 };
        static const LT_SIZE s_ints[kNumElms] = { 3, 0, 1, 2, 4 };

        /* Test InsertSorted, Find and Sort */
        for (u32 i = 0; i < kNumElms; i++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->InsertSorted(s_array, s_array->API->CompareInteger, (void *)s_ints[i],
                                                                (void *)kLTArrayCompare_UnsignedAscending) >= 0, s_errorAdd);
        }
        for (u32 i = 0; i < kNumElms; i++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->Find(s_array, s_array->API->CompareInteger, (void *)s_ints[i],
                                                        (void *)kLTArrayCompare_UnsignedAscending) == (s32)s_ints[i], s_errorNotFound);
        }

        s_array->API->Sort(s_array, s_array->API->CompareInteger, (void *)kLTArrayCompare_UnsignedDescending);
        for (u32 i = 0; i < kNumElms; i++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->Find(s_array, s_array->API->CompareInteger, (void *)s_ints[i],
                                                        (void *)kLTArrayCompare_UnsignedDescending) == kNumElms - (s32)s_ints[i] - 1, s_errorNotFound);
        }
    }

    /* Ascending/Descending signed integer */
    TILT_EXPECT_TRUE(tilt, s_array->API->SetCount(s_array, 0), s_errorBadReturn);
    {
        enum { kNumElms = 7 };
        static const LT_SSIZE s_ints[kNumElms] = { 0, 3, 2, -3, -1, -2, 1 };

        /* Test InsertSorted, Find and Sort */
        for (u32 i = 0; i < kNumElms; i++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->InsertSorted(s_array, s_array->API->CompareInteger, (void *)s_ints[i],
                                                                (void *)kLTArrayCompare_SignedAscending) >= 0, s_errorAdd);
        }
        for (u32 i = 0; i < kNumElms; i++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->Find(s_array, s_array->API->CompareInteger, (void *)s_ints[i],
                                                        (void *)kLTArrayCompare_SignedAscending) == s_ints[i] + 3, s_errorNotFound);
        }

        s_array->API->Sort(s_array, s_array->API->CompareInteger, (void *)kLTArrayCompare_SignedDescending);
        for (u32 i = 0; i < kNumElms; i++) {
            TILT_EXPECT_TRUE(tilt, s_array->API->Find(s_array, s_array->API->CompareInteger, (void *)s_ints[i],
                                                        (void *)kLTArrayCompare_SignedDescending) == 3 - s_ints[i], s_errorNotFound);
        }
    }
}

static void TestOddAlignment(Tilt *tilt) {
    enum { kNumElms = 3, kOddDataSize = 7 };
    static const u8 data[kOddDataSize] = { 33, 34, 35, 1, 2, 3, 4 };
    u8 temp[kOddDataSize];

    s_array->API->InitAsStructArray(s_array, kOddDataSize);

    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Insert(s_array, 0, data) == 0, s_errorAdd);
    }

    for (u32 i = 0; i < kNumElms; i++) {
        TILT_EXPECT_TRUE(tilt, s_array->API->Get(s_array, i, temp), s_errorNotFound);
        TILT_EXPECT_TRUE(tilt, data[0] == temp[0] && data[1] == temp[1] && data[2] == temp[2], s_errorBadValue);
        TILT_EXPECT_TRUE(tilt, data[4] == temp[4] && data[5] == temp[5] && data[6] == temp[6], s_errorBadValue);
    }

    TILT_EXPECT_TRUE(tilt, s_array->API->GetElementSize(s_array) == kOddDataSize, s_errorBadSize);
}

/*******************************************************************************
 * Test Hooks
 ******************************************************************************/

static void BeforeTest(Tilt *tilt) {
    s_array = lt_createobject(LTArray);
    TILT_ASSERT_TRUE(tilt, s_array, s_errorCreate);
    s_arrayQ = lt_createobject(LTArray, List);
    TILT_ASSERT_TRUE(tilt, s_arrayQ, s_errorCreate);
}

static void AfterTest(Tilt *tilt) {
    TILT_EXPECT_TRUE(tilt, s_array->API->SetCount(s_array, 0), s_errorBadReturn);
    TILT_EXPECT_TRUE(tilt, s_array->API->GetCount(s_array) == 0, s_errorBadCount);
    lt_destroyobject(s_array);
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->SetCount(s_arrayQ, 0), s_errorBadReturn);
    TILT_EXPECT_TRUE(tilt, s_arrayQ->API->GetCount(s_arrayQ) == 0, s_errorBadCount);
    lt_destroyobject(s_arrayQ);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeTest     = BeforeTest,
    .AfterTest      = AfterTest,
};

static const TiltEngineTest s_tests[] = {
    { TestPointerArray,     "PointerArray",  "Pointer array",        0 },
    { TestPointerArrayQ,    "PointerArrayQ", "Pointer queue array",  0 },
    { TestStructArray,      "StructArray",   "Struct array",         0 },
    { TestStructArrayQ,     "StructArrayQ",  "Struct queue array",   0 },
    { TestCopyArray,        "CopyArray",     "Copying array",        0 },
    { TestCopyArrayQ,       "CopyArrayQ",    "Copying queue array",  0 },
    { TestFindsAndSorts,    "FindsAndSorts", "Finds and Sorts",      0 },
    { TestOddAlignment,     "Alignments",    "Odd data alignments",  0 },
};

/*******************************************************************************
 * Library Root Interface Binding
 ******************************************************************************/

static int UnitTestLTCoreArrayImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTCoreArrayImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return (s_engine != NULL);
}

static void UnitTestLTCoreArrayImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTCoreArray, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTCoreArray, UnitTestLTCoreArrayImpl_Run, 1536) LTLIBRARY_DEFINITION;
