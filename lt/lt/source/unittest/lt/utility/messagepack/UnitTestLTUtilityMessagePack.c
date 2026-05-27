/*******************************************************************************
 *
 *  UnitTestLTUtilityMessagePack.c
 *    __  __      _ __ ______          __  __  ________  ____  _ ___ __
 *   / / / /___  (_) //_  __/__  _____/ /_/ / /_  __/ / / / /_(_) (_) /___  __
 *  / / / / __ \/ / __// / / _ \/ ___/ __/ /   / / / / / / __/ / / / __/ / / /
 * / /_/ / / / / / /_ / / /  __(__  ) /_/ /___/ / / /_/ / /_/ / / / /_/ /_/ /
 * \____/_/ /_/_/\__//_/  \___/____/\__/_____/_/  \____/\__/_/_/_/\__/\__, /
 *                                                                   /____/
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/utility/messagepack/LTUtilityMessagePack.h>
#include <lt/utility/messagepack/LTUtilityMessagePack_Helpers.h>
#include <tilt/JiltEngine.h>

/*******************************************************************************
 * Static Variables
 ******************************************************************************/

static JiltEngine           *s_engine;
static LTUtilityMessagePack *s_mpLib;

/*******************************************************************************
 * Debug Functions
 ******************************************************************************/

#define P LT_GetCore()->ConsolePrint
#define WATCH(e) //P("=================== %s %s\n", e->typeLabel, e->valueLabel);
#define COUNTOF(a) (sizeof(a)/sizeof(a[0]))

#if 0
static void DumpCode(LTMessagePack_Obj *mp) {
    P("size: %ld (head %p next %p end %p)\n", LT_Pu32(s_mpLib->GetPosition(mp)), mp->head, mp->next, mp->end);
    int i = 0;
    for (u8 *cp = mp->head; cp < mp->next; cp++, i++) {
        P("0x%02x, ", *cp);
        if ((i+1) % 10 == 0) P("\n");
        if (i > 1000) break; // safety break
    }
    if (i % 10 != 0) P("\n");
}
#endif

/*******************************************************************************
 * Basic Init/Free Tests
 ******************************************************************************/

static void TestInitFree(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Value value;

    // Should not crash:
    TILT_EXPECT_FALSE(tilt, s_mpLib->Init(NULL, 0, 0), "NullPtr.init");
    TILT_EXPECT_FALSE(tilt, s_mpLib->PutNil(NULL), "NullPtr.putnil");
    s_mpLib->Free(NULL);
    TILT_EXPECT_FALSE(tilt, s_mpLib->GetValue(NULL, &value), "NullPtr.getvalue");
    TILT_EXPECT_FALSE(tilt, s_mpLib->GetPosition(NULL), "NullPtr.getposition");
    s_mpLib->SetPosition(NULL, 123); // no-op

    // Attempt free of allocated memory:
    s_mpLib->Init(&mp, NULL, 10);
    s_mpLib->Free(&mp); // valid
    // Attempt free twice:
    s_mpLib->Free(&mp); // no-op
    // All functions disabled after free:
    TILT_EXPECT_FALSE(tilt, s_mpLib->GetValue(&mp, &value), "Freed.getvalue");
    TILT_EXPECT_FALSE(tilt, s_mpLib->GetPosition(&mp), "Freed.getposition");
    s_mpLib->PutNil(&mp); // no-op
    s_mpLib->SetPosition(&mp, 123); // no-op

    // Attempt free of non-allocated memory
    u8 data[] = {0xc0};
    s_mpLib->Init(&mp, data, 1);
    s_mpLib->Free(&mp); // no-op
}

/*******************************************************************************
 * Decoder Tests
 ******************************************************************************/

static void TestDecodeNil(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;
    u8 data[] = {0xc0};

    s_mpLib->Init(&mp, data, 1);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Nil, "IsNil");

    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Error, "Nil.AtEnd");
}

static void TestDecodeBool(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;
    u8 *data;

    data = (u8[]){0xc2};
    s_mpLib->Init(&mp, data, 1);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Boolean, "IsBool.false");
    TILT_EXPECT_TRUE(tilt, !value.boolean, "Bool.false");

    data = (u8[]){0xc3};
    s_mpLib->Init(&mp, data, 1);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Boolean, "IsBool.true");
    TILT_EXPECT_TRUE(tilt, value.boolean, "Bool.true");

    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Error, "Bool.AtEnd");
}

typedef struct Decode {
    char *typeLabel;
    char *valueLabel;
    u8   *data;
    u8    size;
    s64   value;
} Decode;

static Decode IntegerDecode[] = {
    { "IsInteger.fixed", "Sf8.0",  (u8[]){0x00},                         1,  0LL },
    { "IsInteger.fixed", "Sf8.1",  (u8[]){0x01},                         1,  1LL },
    { "IsInteger.fixed", "Sf8-1",  (u8[]){0xff},                         1, -1LL },

    { "IsInteger.8bit",  "S08.0",  (u8[]){0xd0, 0x00},                   2,  0LL },
    { "IsInteger.8bit",  "S08.1",  (u8[]){0xd0, 0x01},                   2,  1LL },
    { "IsInteger.8bit",  "S08.-1", (u8[]){0xd0, 0xff},                   2, -1LL },

    { "IsInteger.16bit", "S16.0",  (u8[]){0xd1, 0x00, 0x00},             3,  0LL },
    { "IsInteger.16bit", "S16.1",  (u8[]){0xd1, 0x00, 0x01},             3,  1LL },
    { "IsInteger.16bit", "S16.-1", (u8[]){0xd1, 0xff, 0xff},             3, -1LL },

    { "IsInteger.32bit", "S32.0",  (u8[]){0xd2, 0x00, 0x00, 0x00, 0x00}, 5,  0LL },
    { "IsInteger.32bit", "S32.1",  (u8[]){0xd2, 0x00, 0x00, 0x00, 0x01}, 5,  1LL },
    { "IsInteger.32bit", "S32.80", (u8[]){0xd2, 0x80, 0x00, 0x00, 0x00}, 5,  0xffffffff80000000LL },
    { "IsInteger.32bit", "S32.-1", (u8[]){0xd2, 0xff, 0xff, 0xff, 0xff}, 5, -1LL },

    { "IsInteger.64bit", "S64.0",  (u8[]){0xd3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9,  0LL },
    { "IsInteger.64bit", "S64.1",  (u8[]){0xd3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 9,  1LL },
    { "IsInteger.64bit", "S64.80", (u8[]){0xd3, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9,  0x8000000000000000LL },
    { "IsInteger.64bit", "S64.-1", (u8[]){0xd3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 9, -1LL },

    { "IsInteger.8bit",  "U08.0",  (u8[]){0xcc, 0x00},                   2,  0ULL },
    { "IsInteger.8bit",  "U08.1",  (u8[]){0xcc, 0x01},                   2,  1ULL },
    { "IsInteger.8bit",  "U08.ff", (u8[]){0xcc, 0xff},                   2,  0xffULL },

    { "IsInteger.16bit", "U16.0",  (u8[]){0xcd, 0x00, 0x00},             3,  0ULL },
    { "IsInteger.16bit", "U16.1",  (u8[]){0xcd, 0x00, 0x01},             3,  1ULL },
    { "IsInteger.16bit", "U16.80", (u8[]){0xcd, 0x80, 0x00},             3,  0x0000000000008000ULL },
    { "IsInteger.16bit", "U16.ff", (u8[]){0xcd, 0xff, 0xff},             3,  0x000000000000ffffULL },

    { "IsInteger.32bit", "U32.0",  (u8[]){0xce, 0x00, 0x00, 0x00, 0x00}, 5,  0ULL },
    { "IsInteger.32bit", "U32.1",  (u8[]){0xce, 0x00, 0x00, 0x00, 0x01}, 5,  1ULL },
    { "IsInteger.32bit", "U32.80", (u8[]){0xce, 0x80, 0x00, 0x00, 0x00}, 5,  0x0000000080000000ULL },
    { "IsInteger.32bit", "U32.ff", (u8[]){0xce, 0xff, 0xff, 0xff, 0xff}, 5,  0x00000000ffffffffULL },

    { "IsInteger.64bit", "U64.0",  (u8[]){0xcf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9,  0ULL },
    { "IsInteger.64bit", "U64.1",  (u8[]){0xcf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 9,  1ULL },
    { "IsInteger.64bit", "U64.0f", (u8[]){0xcf, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff}, 9,  0x00000000ffffffffULL },
    { "IsInteger.64bit", "U64.80", (u8[]){0xcf, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9,  0x8000000000000000ULL },
    { "IsInteger.64bit", "U64.ff", (u8[]){0xcf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 9,  0xffffffffffffffffULL },
};

static void TestDecodeInteger(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;

    for (u8 n = 0; n < COUNTOF(IntegerDecode); n++) {
        Decode *dec = &IntegerDecode[n];
        WATCH(dec);
        s_mpLib->Init(&mp, dec->data, dec->size);
        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, dec->typeLabel);
        TILT_EXPECT_TRUE(tilt, (u64)value.integer == (u64)dec->value, dec->valueLabel);

        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Error, "Integer.AtEnd");
    }
}

static Decode FloatDecode[] = {
    { "IsFloat.32", "Float.0.5",  (u8[]){0xca, 0x3f, 0x00, 0x00, 0x00},                         5,  1 },
    { "IsFloat.32", "Float.-0.5", (u8[]){0xca, 0xbf, 0x00, 0x00, 0x00},                         5, -1 },
    { "IsFloat.64", "Float.0.5",  (u8[]){0xcb, 0x3f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9,  2 },
    { "IsFloat.64", "Float.-0.5", (u8[]){0xcb, 0xbf, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9, -2 },
};

static void TestDecodeFloat(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;

    for (u8 n = 0; n < COUNTOF(FloatDecode); n++) {
        Decode *dec = &FloatDecode[n];
        WATCH(dec);
        s_mpLib->Init(&mp, dec->data, dec->size);
        result = s_mpLib->GetValue(&mp, &value);
        switch (dec->value) {
            case 1:
                TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Float32, dec->typeLabel);
                TILT_EXPECT_TRUE(tilt, value.float32 == 0.5, dec->valueLabel);
                break;
            case -1:
                TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Float32, dec->typeLabel);
                TILT_EXPECT_TRUE(tilt, value.float32 == -0.5, dec->valueLabel);
                break;
            case 2:
                TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Float64, dec->typeLabel);
                TILT_EXPECT_TRUE(tilt, value.float64 == 0.5, dec->valueLabel);
                break;
            case -2:
                TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Float64, dec->typeLabel);
                TILT_EXPECT_TRUE(tilt, value.float64 == -0.5, dec->valueLabel);
                break;
        }

        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Error, "Float.AtEnd");
    }
}

static Decode StringDecode[] = {
    { "IsString.fixed", "String.a",   (u8[]){0xa2, 0x41, 0x00},                         3,  0 },
    { "IsString.8bit",  "String.a8",  (u8[]){0xd9, 0x02, 0x41, 0x00},                   4,  0 },
    { "IsString.16bit", "String.a16", (u8[]){0xda, 0x00, 0x02, 0x41, 0x00},             5,  0 },
    { "IsString.32bit", "String.a32", (u8[]){0xdb, 0x00, 0x00, 0x00, 0x02, 0x41, 0x00}, 7,  0 },
};

static void TestDecodeString(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;

    for (u8 n = 0; n < COUNTOF(StringDecode); n++) {
        Decode *dec = &StringDecode[n];
        WATCH(dec);
        s_mpLib->Init(&mp, dec->data, dec->size);
        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_String, dec->typeLabel);
        TILT_EXPECT_TRUE(tilt, value.data[0] == 'A', dec->valueLabel);

        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Error, "String.AtEnd");
    }
}

static Decode BinaryDecode[] = {
    { "IsBinary.8bit",  "Binary.8",  (u8[]){0xc4, 0x01, 0x11},                   3,  0 },
    { "IsBinary.16bit", "Binary.16", (u8[]){0xc5, 0x00, 0x01, 0x11},             4,  0 },
    { "IsBinary.32bit", "Binary.32", (u8[]){0xc6, 0x00, 0x00, 0x00, 0x01, 0x11}, 6,  0 },
};

static void TestDecodeBinary(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;

    for (u8 n = 0; n < COUNTOF(BinaryDecode); n++) {
        Decode *dec = &BinaryDecode[n];
        WATCH(dec);
        s_mpLib->Init(&mp, dec->data, dec->size);
        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Binary, dec->typeLabel);
        TILT_EXPECT_TRUE(tilt, value.data[0] == 0x11, dec->valueLabel);

        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Error, "Binary.AtEnd");
    }
}

static Decode ArrayDecode[] = {
    { "IsArray.8bit",  "Array.8",  (u8[]){0x91, 0x22},                         2,  0 },
    { "IsArray.16bit", "Array.16", (u8[]){0xdc, 0x00, 0x01, 0x22},             4,  0 },
    { "IsArray.32bit", "Array.32", (u8[]){0xdd, 0x00, 0x00, 0x00, 0x01, 0x22}, 6,  0 },
};

static void TestDecodeArray(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;

    for (u8 n = 0; n < COUNTOF(ArrayDecode); n++) {
        Decode *dec = &ArrayDecode[n];
        WATCH(dec);
        s_mpLib->Init(&mp, dec->data, dec->size);
        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Array, dec->typeLabel);
        TILT_EXPECT_TRUE(tilt, value.size == 1, dec->valueLabel);

        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "Array.element");
        TILT_EXPECT_TRUE(tilt, value.integer == 0x22, "Array.at.0");

        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Error, "Array.AtEnd");
    }
}

static Decode MapDecode[] = {
    { "IsMap.8bit",  "Map.8",  (u8[]){0x81, 0x22, 0x46},                         3,  0 },
    { "IsMap.16bit", "Map.16", (u8[]){0xde, 0x00, 0x01, 0x22, 0x46},             5,  0 },
    { "IsMap.32bit", "Map.32", (u8[]){0xdf, 0x00, 0x00, 0x00, 0x01, 0x22, 0x46}, 7,  0 },
};

static void TestDecodeMap(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;

    for (u8 n = 0; n < COUNTOF(MapDecode); n++) {
        Decode *dec = &MapDecode[n];
        WATCH(dec);
        s_mpLib->Init(&mp, dec->data, dec->size);
        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Map, dec->typeLabel);
        TILT_EXPECT_TRUE(tilt, value.size == 1, dec->valueLabel);

        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "Map.key0.type");
        TILT_EXPECT_TRUE(tilt, value.integer == 0x22, "Map.key0.value");

        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "Map.value0.type");
        TILT_EXPECT_TRUE(tilt, value.integer == 0x46, "Map.value0.value");

        result = s_mpLib->GetValue(&mp, &value);
        TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Error, "Map.AtEnd");
    }
}

static u8 ExtendedData[] = {
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
};

static u8 ExtendedDecode[] = {
    0xc7, 0x00, 0x00, 0xd4, 0x01, 0x11, 0xd5, 0x02, 0x11, 0x12,
    0xc7, 0x03, 0x03, 0x11, 0x12, 0x13, 0xd6, 0x04, 0x11, 0x12,
    0x13, 0x14, 0xc7, 0x05, 0x05, 0x11, 0x12, 0x13, 0x14, 0x15,
    0xc7, 0x06, 0x06, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0xc7,
    0x07, 0x07, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0xd7,
    0x08, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0xc7,
    0x09, 0x09, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0xc7, 0x0a, 0x0a, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1a, 0xc7, 0x0b, 0x0b, 0x11, 0x12, 0x13,
    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x21, 0xc7, 0x0c,
    0x0c, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
    0x1a, 0x21, 0x22, 0xc7, 0x0d, 0x0d, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x21, 0x22, 0x23, 0xc7,
    0x0e, 0x0e, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x21, 0x22, 0x23, 0x24, 0xc7, 0x0f, 0x0f, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x21,
    0x22, 0x23, 0x24, 0x25, 0xd8, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x21, 0x22, 0x23, 0x24,
    0x25, 0x26, 0xc7, 0x11, 0x11, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x21, 0x22, 0x23, 0x24, 0x25,
    0x26, 0x27,
};

static u8 ExtendedDecode16[] = {
    0xc8, 0x00, 0x02, 0x20, 0x11, 0x12
};

static u8 ExtendedDecode32[] = {
    0xc9, 0x00, 0x00, 0x00, 0x03, 0x21, 0x11, 0x12, 0x13
};

static void TestDecodeExtended(Tilt *tilt) {
    LTMessagePack_Obj mp;
    s8  type;
    u8 *data;
    u32 length;

    s_mpLib->Init(&mp, ExtendedDecode, sizeof(ExtendedDecode));
    for (u8 n = 0; n < 18; n++) {
        type = s_mpLib->GetExtended(&mp, &data, &length);
        TILT_EXPECT_TRUE(tilt, type == n, "Decode.extend.type");
        TILT_EXPECT_TRUE(tilt, length == n, "Decode.extend.length");
        if (length) {
            // The data pointer is still uninitialized in the first iteration, so avoid this check.
            TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(data, ExtendedData, length), "Decode.extend.compare %u", n);
        }
    }

    s_mpLib->Init(&mp, ExtendedDecode16, sizeof(ExtendedDecode16));
    type = s_mpLib->GetExtended(&mp, &data, &length);
    TILT_EXPECT_TRUE(tilt, type    == 32, "Decode.extend16.type");
    TILT_EXPECT_TRUE(tilt, length  == 2, "Decode.extend16.length");
    TILT_EXPECT_TRUE(tilt, data[0] == 0x11, "Decode.extend16.data");

    s_mpLib->Init(&mp, ExtendedDecode32, sizeof(ExtendedDecode32));
    type = s_mpLib->GetExtended(&mp, &data, &length);
    TILT_EXPECT_TRUE(tilt, type    == 33, "Decode.extend32.type");
    TILT_EXPECT_TRUE(tilt, length  == 3, "Decode.extend32.length");
    TILT_EXPECT_TRUE(tilt, data[0] == 0x11, "Decode.extend32.data");
}

// MultiDecode is shared also with TestEncodeMultiple() below
static u8 MultiDecode[] = {
    0xa8, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x00, 0xcd,
    0x04, 0xd2, 0x81, 0xa5, 0x77, 0x69, 0x66, 0x69, 0x00, 0x83,
    0xa5, 0x73, 0x73, 0x69, 0x64, 0x00, 0xa6, 0x6d, 0x79, 0x2d,
    0x61, 0x70, 0x00, 0xa5, 0x70, 0x61, 0x73, 0x73, 0x00, 0xac,
    0x6d, 0x79, 0x2d, 0x70, 0x61, 0x73, 0x73, 0x77, 0x6f, 0x72,
    0x64, 0x00, 0xa8, 0x63, 0x68, 0x61, 0x6e, 0x6e, 0x65, 0x6c,
    0x00, 0x06,
};

static void TestDecodeMultiple(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;
    u8 *str;
    u32 len;
    u64 num;

    s_mpLib->Init(&mp, MultiDecode, sizeof(MultiDecode));
    result = s_mpLib->GetString(&mp, &str, &len);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_String, "decode.multi.type.1");
    TILT_EXPECT_TRUE(tilt, lt_strncmp((char*)str, "example", len) == 0, "decode.multi.val.1");
    result = s_mpLib->GetInteger(&mp, &num);
    TILT_EXPECT_TRUE(tilt, result, "decode.multi.type.2");
    TILT_EXPECT_TRUE(tilt, num == 1234, "decode.multi.val.2");
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Map, "decode.multi.type.3");
    TILT_EXPECT_TRUE(tilt, value.size == 1, "decode.multi.val.3");
    result = s_mpLib->GetString(&mp, &str, &len);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_String, "decode.multi.type.4");
    TILT_EXPECT_TRUE(tilt, lt_strncmp((char*)str, "wifi", len) == 0, "decode.multi.val.4");
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Map, "decode.multi.type.5");
    TILT_EXPECT_TRUE(tilt, value.size == 3, "decode.multi.val.5");
    result = s_mpLib->GetString(&mp, &str, &len);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_String, "decode.multi.type.6");
    TILT_EXPECT_TRUE(tilt, lt_strncmp((char*)str, "ssid", len) == 0, "decode.multi.val.6");
    result = s_mpLib->GetString(&mp, &str, &len);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_String, "decode.multi.type.7");
    TILT_EXPECT_TRUE(tilt, lt_strncmp((char*)str, "my-ap", len) == 0, "decode.multi.val.7");
    result = s_mpLib->GetString(&mp, &str, &len);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_String, "decode.multi.type.8");
    TILT_EXPECT_TRUE(tilt, lt_strncmp((char*)str, "pass", len) == 0, "decode.multi.val.8");
    result = s_mpLib->GetString(&mp, &str, &len);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_String, "decode.multi.type.9");
    TILT_EXPECT_TRUE(tilt, lt_strncmp((char*)str, "my-password", len) == 0, "decode.multi.val.9");
    result = s_mpLib->GetString(&mp, &str, &len);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_String, "decode.multi.type.10");
    TILT_EXPECT_TRUE(tilt, lt_strncmp((char*)str, "channel", len) == 0, "decode.multi.val.10");
    result = s_mpLib->GetInteger(&mp, &num);
    TILT_EXPECT_TRUE(tilt, result, "decode.multi.type.11");
    TILT_EXPECT_TRUE(tilt, num == 6, "decode.multi.val.11");
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Error, "decode.multi.type.end");
}

/*******************************************************************************
 * Encoder Tests
 ******************************************************************************/
static void TestEncodeNil(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;
    u8 data[4];

    s_mpLib->Init(&mp, data, sizeof(data));
    s_mpLib->PutNil(&mp);
    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == 1, "Encode.nil.pos");

    s_mpLib->SetPosition(&mp, 0);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Nil, "Encode.nil.type");
    TILT_EXPECT_TRUE(tilt, value.type == LTMessagePack_Type_Nil, "Encode.nil.val.type");
}

static void TestEncodeBoolean(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;
    u8 data[4];

    s_mpLib->Init(&mp, data, sizeof(data));
    s_mpLib->PutBoolean(&mp, false);
    s_mpLib->PutBoolean(&mp, true);
    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == 2, "Encode.bool.pos");

    s_mpLib->SetPosition(&mp, 0);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Boolean, "Encode.bool.type");
    TILT_EXPECT_TRUE(tilt, !value.boolean, "Encode.bool.false");

    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Boolean, "Encode.bool.type");
    TILT_EXPECT_TRUE(tilt, value.boolean, "Encode.bool.true");
}

static u8 IntegerEncode[] = {
    0x00, 0x01, 0xff, 0xcc, 0x80, 0xd0, 0x80, 0xcd, 0x01, 0x00,
    0xd1, 0xff, 0x00, 0xcd, 0x10, 0x00, 0xcd, 0x80, 0x00, 0xd1,
    0xf0, 0x00, 0xd1, 0x80, 0x00, 0xce, 0x10, 0x00, 0x00, 0x00,
    0xd2, 0x80, 0x00, 0x00, 0x00, 0xd2, 0xf0, 0x00, 0x00, 0x00,
    0xd2, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x00, 0x01,
    0xcc, 0x80, 0xcc, 0xff, 0xcd, 0x01, 0x00, 0xcd, 0x08, 0x00,
    0xcd, 0x10, 0x00, 0xcd, 0x80, 0x00, 0xcd, 0xff, 0xff, 0xce,
    0x10, 0x00, 0x00, 0x00, 0xce, 0x80, 0x00, 0x00, 0x00, 0xce,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0xcf, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xcf, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff,
};

static void TestEncodeInteger(Tilt *tilt) {
    LTMessagePack_Obj mp;
    u8 data[sizeof(IntegerEncode) + 16];

    s_mpLib->Init(&mp, data, sizeof(data));
    s_mpLib->PutIntS32(&mp,  0);
    s_mpLib->PutIntS32(&mp,  1);
    s_mpLib->PutIntS32(&mp, -1);

    s_mpLib->PutIntS32(&mp,  0x80);
    s_mpLib->PutIntS32(&mp, -0x80);

    s_mpLib->PutIntS32(&mp,  0x100);
    s_mpLib->PutIntS32(&mp, -0x100);

    s_mpLib->PutIntS32(&mp,  0x1000);
    s_mpLib->PutIntS32(&mp,  0x8000);
    s_mpLib->PutIntS32(&mp, -0x1000);
    s_mpLib->PutIntS32(&mp, -0x8000);

    s_mpLib->PutIntS32(&mp,  0x10000000);
    s_mpLib->PutIntS32(&mp,  0x80000000);
    s_mpLib->PutIntS32(&mp, -0x10000000);
    s_mpLib->PutIntS32(&mp, -0x80000000);

    s_mpLib->PutIntS64(&mp,  0);
    s_mpLib->PutIntS64(&mp,  1);
    s_mpLib->PutIntS64(&mp, -1);

    s_mpLib->PutIntU32(&mp,  0x0);
    s_mpLib->PutIntU32(&mp,  0x1);
    s_mpLib->PutIntU32(&mp,  0x80);
    s_mpLib->PutIntU32(&mp,  0xff);

    s_mpLib->PutIntU32(&mp,  0x100);
    s_mpLib->PutIntU32(&mp,  0x800);

    s_mpLib->PutIntU32(&mp,  0x1000);
    s_mpLib->PutIntU32(&mp,  0x8000);
    s_mpLib->PutIntU32(&mp,  0xffff);

    s_mpLib->PutIntU32(&mp,  0x10000000);
    s_mpLib->PutIntU32(&mp,  0x80000000);
    s_mpLib->PutIntU32(&mp,  0xffffffff);

    s_mpLib->PutIntU64(&mp,  0);
    s_mpLib->PutIntU64(&mp,  1);
    s_mpLib->PutIntU64(&mp,  0x8000000000000000);
    s_mpLib->PutIntU64(&mp,  0xffffffffffffffff);

    //DumpCode(&mp);
    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == sizeof(IntegerEncode), "Encode.integer.size");
    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(data, IntegerEncode, sizeof(IntegerEncode)), "Encode.integer.compare");
}

static u8 UnsignedEncode[] = {
    0x00, 0x01, 0xcc, 0x80, 0xcc, 0xff, 0xcd, 0x10, 0x00, 0xcd,
    0x80, 0x00, 0xcd, 0xff, 0xff, 0xce, 0x10, 0x00, 0x00, 0x00,
    0xce, 0x80, 0x00, 0x00, 0x00, 0xce, 0xff, 0xff, 0xff, 0xff,
};

static void TestEncodeUnsigned(Tilt *tilt) {
    LTMessagePack_Obj mp;
    u8 data[64];

    s_mpLib->Init(&mp, data, sizeof(data));
    s_mpLib->PutIntU32(&mp,  0);
    s_mpLib->PutIntU32(&mp,  1);
    s_mpLib->PutIntU32(&mp,  0x80);
    s_mpLib->PutIntU32(&mp,  0xff);
    s_mpLib->PutIntU32(&mp,  0x1000);
    s_mpLib->PutIntU32(&mp,  0x8000);
    s_mpLib->PutIntU32(&mp,  0xffff);
    s_mpLib->PutIntU32(&mp,  0x10000000);
    s_mpLib->PutIntU32(&mp,  0x80000000);
    s_mpLib->PutIntU32(&mp,  0xffffffff);

    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == sizeof(UnsignedEncode), "Encode.unsigned.size");
    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(data, UnsignedEncode, sizeof(UnsignedEncode)), "Encode.unsigned.compare");
}

static u8 FloatEncode[] = {
    0xca, 0x00, 0x00, 0x00, 0x00, 0xca, 0x3f, 0x00, 0x00, 0x00,
    0xca, 0xbf, 0x00, 0x00, 0x00, 0xcb, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xcb, 0x3f, 0xe0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xcb, 0xbf, 0xe0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
};

static void TestEncodeFloat(Tilt *tilt) {
    LTMessagePack_Obj mp;
    u8 data[64];

    s_mpLib->Init(&mp, data, sizeof(data));
    s_mpLib->PutFloat32(&mp,  0.0);
    s_mpLib->PutFloat32(&mp,  0.5);
    s_mpLib->PutFloat32(&mp, -0.5);
    s_mpLib->PutFloat64(&mp,  0.0);
    s_mpLib->PutFloat64(&mp,  0.5);
    s_mpLib->PutFloat64(&mp, -0.5);

    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == sizeof(FloatEncode), "Encode.float.size");
    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(data, FloatEncode, sizeof(FloatEncode)), "Encode.float.compare");
}

static u8 StringEncode[] = {
    0xa1, 0x00, 0xa2, 0x41, 0x00, 0xa2, 0x42, 0x43, 0xa4, 0x62,
    0x63, 0x64, 0x00, 0xb5, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x00, 0xd9, 0x22, 0x30, 0x31, 0x32,
    0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x31, 0x32,
    0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x31, 0x32,
    0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x31, 0x32,
    0x00,
};

static void TestEncodeString(Tilt *tilt) {
    LTMessagePack_Obj mp;
    u8 data[80];

    s_mpLib->Init(&mp, data, sizeof(data));
    s_mpLib->PutCString(&mp, "");
    s_mpLib->PutCString(&mp, "A");
    TILT_EXPECT_TRUE(tilt, 6 == s_mpLib->PutString(&mp, "BCD", 2), "");
    TILT_EXPECT_TRUE(tilt, 9 == s_mpLib->PutString(&mp, "bcd", 4), "");
    s_mpLib->PutCString(&mp, "01234567890123456789");
    s_mpLib->PutCString(&mp, "012345678901234567890123456789012");
    // PutString of other sizes not practical here

    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == sizeof(StringEncode), "Encode.string.size");
    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(data, StringEncode, sizeof(StringEncode)), "Encode.string.compare");
}

static u8 BinaryEncode[] = {
    0xc4, 0x00, 0xc4, 0x01, 0x00, 0xc4, 0x01, 0x41, 0xc4, 0x03,
    0x42, 0x43, 0x44, 0xc4, 0x04, 0x62, 0x63, 0x64, 0x00, 0xc4,
    0x14, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0xc4, 0x21, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x30, 0x31, 0x32,
};

static void TestEncodeBinary(Tilt *tilt) {
    LTMessagePack_Obj mp;
    u8 data[80];

    s_mpLib->Init(&mp, data, sizeof(data));
    TILT_EXPECT_TRUE(tilt, 2  == s_mpLib->PutBinary(&mp, (u8*)"", 0),                                   "PutBinary.str.empty");
    TILT_EXPECT_TRUE(tilt, 4  == s_mpLib->PutBinary(&mp, (u8*)"", 1),                                   "PutBinary.str.term");
    TILT_EXPECT_TRUE(tilt, 7  == s_mpLib->PutBinary(&mp, (u8*)"A", 1),                                  "PutBinary.A");
    TILT_EXPECT_TRUE(tilt, 10 == s_mpLib->PutBinary(&mp, (u8*)"BCD", 3),                                "PutBinary.BCD");
    TILT_EXPECT_TRUE(tilt, 15 == s_mpLib->PutBinary(&mp, (u8*)"bcd", 4),                                "PutBinary.bcd.term");
    TILT_EXPECT_TRUE(tilt, 21 == s_mpLib->PutBinary(&mp, (u8*)"01234567890123456789", 20),              "PutBinary.20");
    TILT_EXPECT_TRUE(tilt, 43 == s_mpLib->PutBinary(&mp, (u8*)"012345678901234567890123456789012", 33), "PutBinary.33");
    // PutBinary of other sizes not practical here

    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == sizeof(BinaryEncode), "Encode.binary.size");
    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(data, BinaryEncode, sizeof(BinaryEncode)), "Encode.binary.compare");
}

static u8 ArrayEncode[] = {
    0x90, 0x91, 0xcd, 0x12, 0x34, 0xdc, 0x00, 0x14, 0x00, 0x01,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
};

static void TestEncodeArray(Tilt *tilt) {
    LTMessagePack_Obj mp;
    u8 data[80];

    s_mpLib->Init(&mp, data, sizeof(data));
    s_mpLib->PutArray(&mp, 0);
    s_mpLib->PutArray(&mp, 1);
    s_mpLib->PutIntS32(&mp, 0x1234);
    s_mpLib->PutArray(&mp, 20);
    for (u8 n = 0; n < 20; n++) s_mpLib->PutIntS32(&mp, n);
    // PutArray of other sizes not practical here

    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == sizeof(ArrayEncode), "Encode.array.size");
    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(data, ArrayEncode, sizeof(ArrayEncode)), "Encode.array.compare");
}

static u8 MapEncode[] = {
    0x80, 0x81, 0xa4, 0x6b, 0x65, 0x79, 0x00, 0xa6, 0x76, 0x61,
    0x6c, 0x75, 0x65, 0x00, 0xde, 0x00, 0x14, 0xa5, 0x6b, 0x65,
    0x79, 0x41, 0x00, 0x00, 0xa5, 0x6b, 0x65, 0x79, 0x41, 0x00,
    0x01, 0xa5, 0x6b, 0x65, 0x79, 0x41, 0x00, 0x02, 0xa5, 0x6b,
    0x65, 0x79, 0x41, 0x00, 0x03, 0xa5, 0x6b, 0x65, 0x79, 0x41,
    0x00, 0x04, 0xa5, 0x6b, 0x65, 0x79, 0x41, 0x00, 0x05, 0xa5,
    0x6b, 0x65, 0x79, 0x41, 0x00, 0x06, 0xa5, 0x6b, 0x65, 0x79,
    0x41, 0x00, 0x07, 0xa5, 0x6b, 0x65, 0x79, 0x41, 0x00, 0x08,
    0xa5, 0x6b, 0x65, 0x79, 0x41, 0x00, 0x09, 0xa5, 0x6b, 0x65,
    0x79, 0x41, 0x00, 0x0a, 0xa5, 0x6b, 0x65, 0x79, 0x41, 0x00,
    0x0b, 0xa5, 0x6b, 0x65, 0x79, 0x41, 0x00, 0x0c, 0xa5, 0x6b,
    0x65, 0x79, 0x41, 0x00, 0x0d, 0xa5, 0x6b, 0x65, 0x79, 0x41,
    0x00, 0x0e, 0xa5, 0x6b, 0x65, 0x79, 0x41, 0x00, 0x0f, 0xa5,
    0x6b, 0x65, 0x79, 0x41, 0x00, 0x10, 0xa5, 0x6b, 0x65, 0x79,
    0x41, 0x00, 0x11, 0xa5, 0x6b, 0x65, 0x79, 0x41, 0x00, 0x12,
    0xa5, 0x6b, 0x65, 0x79, 0x41, 0x00, 0x13,
};

static void TestEncodeMap(Tilt *tilt) {
    LTMessagePack_Obj mp;
    u8 data[200];

    s_mpLib->Init(&mp, data, sizeof(data));
    s_mpLib->PutMap(&mp, 0);
    s_mpLib->PutMap(&mp, 1);
    s_mpLib->PutCString(&mp, "key");
    s_mpLib->PutCString(&mp, "value");
    s_mpLib->PutMap(&mp, 20);
    for (u8 n = 0; n < 20; n++) {
        char str[] = "keyA";
        s_mpLib->PutCString(&mp, str);
        s_mpLib->PutIntS32(&mp, n);
        str[3]++;
    }
    // PutArray of other sizes not practical here

    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == sizeof(MapEncode), "Encode.map.size");
    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(data, MapEncode, sizeof(MapEncode)), "Encode.map.compare");
}

static void TestEncodeExtended(Tilt *tilt) {
    LTMessagePack_Obj mp;

    s_mpLib->Init(&mp, NULL, 220);
    for (u8 n = 0; n < 18; n++) {
        s_mpLib->PutExtended(&mp, n, ExtendedData, n);
    }
    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == sizeof(ExtendedDecode), "Encode.extend.size");
    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(mp.head, ExtendedDecode, sizeof(ExtendedDecode)), "Encode.extend.compare");
    s_mpLib->Free(&mp);
}

static void TestEncodeMultiple(Tilt *tilt) {
    LTMessagePack_Obj mp;
    u8 data[200];

    // Just a typical case:
    s_mpLib->Init(&mp, data, sizeof(data));
    s_mpLib->PutCString(&mp, "example");
    s_mpLib->PutIntS32(&mp, 1234);
    // Indentation below shows the map nesting for {wifi: {ssid: my-ap, pass: my-password, channel: 6 }}
    s_mpLib->PutMap(&mp, 1);
        s_mpLib->PutCString(&mp, "wifi");
        s_mpLib->PutMap(&mp, 3);
            s_mpLib->PutCString(&mp, "ssid");
            s_mpLib->PutCString(&mp, "my-ap");
            s_mpLib->PutCString(&mp, "pass");
            s_mpLib->PutCString(&mp, "my-password");
            s_mpLib->PutCString(&mp, "channel");
            s_mpLib->PutIntU32(&mp, 6);

    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == sizeof(MultiDecode), "Encode.multi.size");
    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(data, MultiDecode, sizeof(MultiDecode)), "Encode.multi.compare");
}

/*******************************************************************************
 * Other Tests
 ******************************************************************************/

static void TestEncodePastEnd(Tilt *tilt) {
    LTMessagePack_Obj mp;
    bool okay;
    u8 data[4];

    s_mpLib->Init(&mp, data, 4);
    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == 0, "Encode.init.pos");

    s_mpLib->Init(&mp, data, 4);
    okay = s_mpLib->PutCString(&mp, "abc"); // len=5
    TILT_EXPECT_TRUE(tilt, !okay, "Encode.str.end");

    s_mpLib->Init(&mp, data, 4);
    okay = s_mpLib->PutCString(&mp, "abc"); // len=5
    TILT_EXPECT_TRUE(tilt, !okay, "Encode.str.end");
    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == 0, "Encode.str.pos");

    okay = s_mpLib->PutCString(&mp, "ab"); // len=4
    TILT_EXPECT_TRUE(tilt, okay, "Encode.str.fit");

    s_mpLib->Init(&mp, data, 1);
    okay = s_mpLib->PutIntU32(&mp, 0x10); // len=1
    TILT_EXPECT_TRUE(tilt, okay, "Encode.int1.fit");
    okay = s_mpLib->PutIntU32(&mp, 0x10); // len=1
    TILT_EXPECT_TRUE(tilt, !okay, "Encode.int1.end");

    s_mpLib->Init(&mp, data, 1);
    okay = s_mpLib->PutIntU32(&mp, 0x1000); // len=3
    TILT_EXPECT_TRUE(tilt, !okay, "Encode.int3.end");
    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == 0, "Encode.int3.pos");

    s_mpLib->Init(&mp, data, 4);
    okay = s_mpLib->PutIntU32(&mp, 0x10000000); // len=5
    TILT_EXPECT_TRUE(tilt, !okay, "Encode.int5.end");
    TILT_EXPECT_TRUE(tilt, s_mpLib->GetPosition(&mp) == 0, "Encode.int5.pos");

    s_mpLib->Init(&mp, data, 1);
    okay = s_mpLib->PutArray(&mp, 1); // len=1
    TILT_EXPECT_TRUE(tilt, okay, "Encode.array1.fit");

    s_mpLib->Init(&mp, data, 1);
    okay = s_mpLib->PutArray(&mp, 20); // len=3
    TILT_EXPECT_TRUE(tilt, !okay, "Encode.array20.end");

    s_mpLib->Init(&mp, data, 3);
    okay = s_mpLib->PutArray(&mp, 20); // len=3
    TILT_EXPECT_TRUE(tilt, okay, "Encode.array20.fit");

    //DumpCode(&mp);

    // More end-test cases can be added
}

static u8 SkipData[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
};

static void TestSkip(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;

    s_mpLib->Init(&mp, SkipData, sizeof(SkipData));
    s_mpLib->Skip(&mp, 2);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "Skip.2.int");
    TILT_EXPECT_TRUE(tilt, value.integer == 2, "skip.2.int.2");

    s_mpLib->Skip(&mp, 0);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "Skip.0.int");
    TILT_EXPECT_TRUE(tilt, value.integer == 3, "skip.0.int.3");

    s_mpLib->Skip(&mp, 20);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Error, "Skip.end");
}

static u8 SkipArrayData[] = {
    0x93, 0x00, 0x01, 0x02, 0x03, 0x04,
};

static u8 SkipMapData[] = {
    0x82, 0x11, 0x01, 0x22, 0x02, 0x03,
};

static u8 SkipMapDataNested[] = {
    0x83, 0xA1, 0x32, 0x82, 0xA1, 0x34, 0x01, 0xA4, 0x72, 0x6F,
    0x6B, 0x75, 0x92, 0xA2, 0x6C, 0x74, 0xA2, 0x4F, 0x53, 0xA2,
    0x33, 0x35, 0x81, 0xA1, 0x37, 0x82, 0xA1, 0x31, 0xD9, 0x24,
    0x38, 0x31, 0x38, 0x65, 0x35, 0x66, 0x32, 0x34, 0x2D, 0x63,
    0x38, 0x36, 0x63, 0x2D, 0x34, 0x36, 0x37, 0x36, 0x2D, 0x38,
    0x35, 0x31, 0x65, 0x2D, 0x61, 0x32, 0x34, 0x32, 0x34, 0x32,
    0x38, 0x30, 0x39, 0x62, 0x32, 0x31, 0xA1, 0x34, 0xC3, 0xA2,
    0x38, 0x39, 0x93, 0x01, 0x02, 0x03,
    0x06
};

static void TestSkipWithin(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;
    LTMessagePack_Value container;

    s_mpLib->Init(&mp, SkipArrayData, sizeof(SkipArrayData));
    result = s_mpLib->GetValue(&mp, &container);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Array, "SkipWithin.array");

    s_mpLib->SkipWithin(&mp, &container, 2);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "SkipWithin.array.int");
    TILT_EXPECT_TRUE(tilt, value.integer == 2, "SkipWithin.array.int.2");

    s_mpLib->Init(&mp, SkipMapData, sizeof(SkipMapData));
    result = s_mpLib->GetValue(&mp, &container);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Map, "SkipWithin.map");

    s_mpLib->SkipWithin(&mp, &container, 1);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "SkipWithin.map.int");
    TILT_EXPECT_TRUE(tilt, value.integer == 0x22, "SkipWithin.map.int.22");

    s_mpLib->Init(&mp, SkipMapDataNested, sizeof(SkipMapDataNested));
    result = s_mpLib->GetValue(&mp, &container);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Map, "SkipWithin.map.nested");

    s_mpLib->SkipWithin(&mp, &container, 2);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_INFO(tilt, "%d", result);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_String, "SkipWithin.map.nested.int");
    TILT_EXPECT_TRUE(tilt, lt_strcmp((const char*)value.data, "2"), "SkipWithin.map.nested.int.2");
}

static void TestSkipContainer(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;
    LTMessagePack_Value container;

    s_mpLib->Init(&mp, SkipArrayData, sizeof(SkipArrayData));
    result = s_mpLib->GetValue(&mp, &container);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Array, "SkipCont.array");

    s_mpLib->SkipContainer(&mp, &container);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "SkipCont.array.int");
    TILT_EXPECT_TRUE(tilt, value.integer == 3, "SkipCont.array.int.3");

    s_mpLib->Init(&mp, SkipMapData, sizeof(SkipMapData));
    result = s_mpLib->GetValue(&mp, &container);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Map, "SkipCont.map");

    s_mpLib->SkipContainer(&mp, &container);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "SkipCont.map.int");
    TILT_EXPECT_TRUE(tilt, value.integer == 0x03, "SkipCont.map.int.2");

    s_mpLib->Init(&mp, SkipMapDataNested, sizeof(SkipMapDataNested));
    result = s_mpLib->GetValue(&mp, &container);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Map, "SkipCont.map.nested");

    s_mpLib->SkipContainer(&mp, &container);
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "SkipCont.map.nested.int");
    TILT_EXPECT_TRUE(tilt, value.integer == 0x06, "SkipCont.map.nested.int.2");
}

static void TestFindInteger(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;
    LTMessagePack_Value container;

    s_mpLib->Init(&mp, SkipArrayData, sizeof(SkipArrayData));
    result = s_mpLib->GetValue(&mp, &container);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Array, "FindInt.array");

    TILT_EXPECT_TRUE(tilt, s_mpLib->FindInteger(&mp, &container, 0x0), "FindInt.array.result.0");
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "FindInt.array.int.0");
    TILT_EXPECT_TRUE(tilt, value.integer == 0, "FindInt.array.int.0");

    TILT_EXPECT_TRUE(tilt, s_mpLib->FindInteger(&mp, &container, 0x1), "FindInt.array.result.1");
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "FindInt.array.int.1");
    TILT_EXPECT_TRUE(tilt, value.integer == 1, "FindInt.array.int.1");

    TILT_EXPECT_TRUE(tilt, s_mpLib->FindInteger(&mp, &container, 0x2), "FindInt.array.result.2");
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "FindInt.array.int.2");
    TILT_EXPECT_TRUE(tilt, value.integer == 2, "FindInt.array.int.2");

    TILT_EXPECT_FALSE(tilt, s_mpLib->FindInteger(&mp, &container, 0x3), "FindInt.array.fail.3");
    TILT_EXPECT_FALSE(tilt, s_mpLib->FindInteger(&mp, &container, 0x4), "FindInt.array.fail.4");

    // Find 0 again to test random access find
    TILT_EXPECT_TRUE(tilt, s_mpLib->FindInteger(&mp, &container, 0x0), "FindInt.array.result.0.again");
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "FindInt.array.int.0.again");
    TILT_EXPECT_TRUE(tilt, value.integer == 0, "FindInt.array.int.0.again");

    s_mpLib->Init(&mp, SkipMapData, sizeof(SkipMapData));
    result = s_mpLib->GetValue(&mp, &container);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Map, "FindInt.map");

    TILT_EXPECT_TRUE(tilt, s_mpLib->FindInteger(&mp, &container, 0x11), "FindInt.map.result.1");
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "FindInt.map.int.1");
    TILT_EXPECT_TRUE(tilt, value.integer == 0x01, "FindInt.map.int.1");

    TILT_EXPECT_TRUE(tilt, s_mpLib->FindInteger(&mp, &container, 0x22), "FindInt.map.result.2");
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "FindInt.map.int.2");
    TILT_EXPECT_TRUE(tilt, value.integer == 0x02, "FindInt.map.int.2");

    TILT_EXPECT_FALSE(tilt, s_mpLib->FindInteger(&mp, &container, 0x3), "FindInt.map.fail");

    // Find 1 again to test random access find
    TILT_EXPECT_TRUE(tilt, s_mpLib->FindInteger(&mp, &container, 0x11), "FindInt.map.result.1.again");
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "FindInt.map.int.1.again");
    TILT_EXPECT_TRUE(tilt, value.integer == 0x01, "FindInt.map.int.1.again");
}

static u8 FindArrayStringData[] = {
    0x93, 0x00, 0x01, 0xa4, 0x61, 0x62, 0x63, 0x00, 0x02, 0x03, 0x04,
};

static u8 FindMapStringData[] = {
    0x82, 0xa3, 0x61, 0x62, 0x00, 0x11, 0xa3, 0x63, 0x64, 0x00, 0x22, 0x03,
};

static void TestFindString(Tilt *tilt) {
    LTMessagePack_Obj mp;
    LTMessagePack_Type  result;
    LTMessagePack_Value value;
    LTMessagePack_Value container;

    s_mpLib->Init(&mp, FindArrayStringData, sizeof(FindArrayStringData));
    result = s_mpLib->GetValue(&mp, &container);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Array, "FindStr.array");

    TILT_EXPECT_TRUE(tilt, s_mpLib->FindCString(&mp, &container, "abc"), "FindStr.array.result");
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_String, "FindStr.array.str");
    TILT_EXPECT_TRUE(tilt, lt_strcmp((char*)value.data, "abc") == 0, "FindStr.array.str.abc");

    s_mpLib->Init(&mp, FindArrayStringData, sizeof(FindArrayStringData));
    result = s_mpLib->GetValue(&mp, &container);
    TILT_EXPECT_FALSE(tilt, s_mpLib->FindCString(&mp, &container, "xyz"), "FindStr.array.fail");

    s_mpLib->Init(&mp, FindMapStringData, sizeof(FindMapStringData));
    result = s_mpLib->GetValue(&mp, &container);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Map, "FindStr.map");

    TILT_EXPECT_TRUE(tilt, s_mpLib->FindCString(&mp, &container, "cd"), "FindStr.map.result");
    result = s_mpLib->GetValue(&mp, &value);
    TILT_EXPECT_TRUE(tilt, result == LTMessagePack_Type_Integer, "FindStr.map.int");
    TILT_EXPECT_TRUE(tilt, value.integer == 0x22, "FindStr.map.int.22");

    s_mpLib->Init(&mp, FindMapStringData, sizeof(FindMapStringData));
    result = s_mpLib->GetValue(&mp, &container);
    TILT_EXPECT_FALSE(tilt, s_mpLib->FindCString(&mp, &container, "xy"), "FindStr.map.fail");
}

static void TestSizeMacros(Tilt *tilt) {
    TILT_EXPECT_TRUE(tilt, MP_NIL_SIZE == 1, "SizeMacro.nil");
    TILT_EXPECT_TRUE(tilt, MP_BOOL_SIZE == 1, "SizeMacro.bool");

    TILT_EXPECT_TRUE(tilt, MP_U32_SIZE(0)          == 1, "SizeMacro.u32.0");
    TILT_EXPECT_TRUE(tilt, MP_U32_SIZE(0x7f)       == 1, "SizeMacro.u32.7f");
    TILT_EXPECT_TRUE(tilt, MP_U32_SIZE(0x80)       == 2, "SizeMacro.u32.80");
    TILT_EXPECT_TRUE(tilt, MP_U32_SIZE(0xff)       == 2, "SizeMacro.u32.ff");
    TILT_EXPECT_TRUE(tilt, MP_U32_SIZE(0x100)      == 3, "SizeMacro.u32.100");
    TILT_EXPECT_TRUE(tilt, MP_U32_SIZE(0xffff)     == 3, "SizeMacro.u32.ffff");
    TILT_EXPECT_TRUE(tilt, MP_U32_SIZE(0x10000)    == 5, "SizeMacro.u32.10000");
    TILT_EXPECT_TRUE(tilt, MP_U32_SIZE(0xffffffff) == 5, "SizeMacro.u32.ffffffff");

    TILT_EXPECT_TRUE(tilt, MP_U64_SIZE(0)                  == 1, "SizeMacro.u64.0");
    TILT_EXPECT_TRUE(tilt, MP_U64_SIZE(0x7f)               == 1, "SizeMacro.u64.7f");
    TILT_EXPECT_TRUE(tilt, MP_U64_SIZE(0x80)               == 2, "SizeMacro.u64.80");
    TILT_EXPECT_TRUE(tilt, MP_U64_SIZE(0xff)               == 2, "SizeMacro.u64.ff");
    TILT_EXPECT_TRUE(tilt, MP_U64_SIZE(0x100)              == 3, "SizeMacro.u64.100");
    TILT_EXPECT_TRUE(tilt, MP_U64_SIZE(0xffff)             == 3, "SizeMacro.u64.ffff");
    TILT_EXPECT_TRUE(tilt, MP_U64_SIZE(0x10000)            == 5, "SizeMacro.u64.10000");
    TILT_EXPECT_TRUE(tilt, MP_U64_SIZE(0xffffffff)         == 5, "SizeMacro.u64.ffffffff");
    TILT_EXPECT_TRUE(tilt, MP_U64_SIZE(0x100000000)        == 9, "SizeMacro.u64.100000000");
    TILT_EXPECT_TRUE(tilt, MP_U64_SIZE(0xffffffffffffffff) == 9, "SizeMacro.u64.ffffffffffffffff");

    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(-0x80000000) == 5, "SizeMacro.s32.-80000000");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(-0x8001)     == 5, "SizeMacro.s32.-8001");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(-0x8000)     == 3, "SizeMacro.s32.-8000");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(-0x81)       == 3, "SizeMacro.s32.-81");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(-0x80)       == 2, "SizeMacro.s32.-80");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(-0x21)       == 2, "SizeMacro.s32.-21");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(-0x20)       == 1, "SizeMacro.s32.-20");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(0)           == 1, "SizeMacro.s32.0");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(0x7f)        == 1, "SizeMacro.s32.7f");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(0x80)        == 2, "SizeMacro.s32.80");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(0xff)        == 2, "SizeMacro.s32.ff");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(0x100)       == 3, "SizeMacro.s32.100");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(0xffff)      == 3, "SizeMacro.s32.ffff");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(0x10000)     == 5, "SizeMacro.s32.10000");
    TILT_EXPECT_TRUE(tilt, MP_S32_SIZE(0xffffffff)  == 5, "SizeMacro.s32.ffffffff");

    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(-0x100000000ll) == 9, "SizeMacro.s64.-100000000");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(-0xffffffffll)  == 5, "SizeMacro.s64.-ffffffff");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(-0x8001)        == 5, "SizeMacro.s64.-8001");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(-0x8000)        == 3, "SizeMacro.s64.-8000");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(-0x81)          == 3, "SizeMacro.s64.-81");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(-0x80)          == 2, "SizeMacro.s64.-80");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(-0x21)          == 2, "SizeMacro.s64.-21");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(-0x20)          == 1, "SizeMacro.s64.-20");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(0)              == 1, "SizeMacro.s64.0");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(0x7f)           == 1, "SizeMacro.s64.7f");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(0x80)           == 2, "SizeMacro.s64.80");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(0xff)           == 2, "SizeMacro.s64.ff");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(0x100)          == 3, "SizeMacro.s64.100");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(0xffff)         == 3, "SizeMacro.s64.ffff");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(0x10000)        == 5, "SizeMacro.s64.10000");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(0xffffffffll)   == 5, "SizeMacro.s64.ffffffff");
    TILT_EXPECT_TRUE(tilt, MP_S64_SIZE(0x100000000ll)  == 9, "SizeMacro.s64.100000000");

    TILT_EXPECT_TRUE(tilt, MP_FLOAT32_SIZE == 5, "SizeMacro.float32");
    TILT_EXPECT_TRUE(tilt, MP_FLOAT64_SIZE == 9, "SizeMacro.float64");

    TILT_EXPECT_TRUE(tilt, MP_STR_SIZE(0)       == 1,       "SizeMacro.str.0");
    TILT_EXPECT_TRUE(tilt, MP_STR_SIZE(0x1f)    == 0x20,    "SizeMacro.str.1f");
    TILT_EXPECT_TRUE(tilt, MP_STR_SIZE(0x20)    == 0x22,    "SizeMacro.str.20");
    TILT_EXPECT_TRUE(tilt, MP_STR_SIZE(0xff)    == 0x101,   "SizeMacro.str.ff");
    TILT_EXPECT_TRUE(tilt, MP_STR_SIZE(0x100)   == 0x103,   "SizeMacro.str.100");
    TILT_EXPECT_TRUE(tilt, MP_STR_SIZE(0xffff)  == 0x10002, "SizeMacro.str.ffff");
    TILT_EXPECT_TRUE(tilt, MP_STR_SIZE(0x10000) == 0x10005, "SizeMacro.str.10000");

    TILT_EXPECT_TRUE(tilt, MP_BIN_SIZE(0)       == 2,       "SizeMacro.bin.0");
    TILT_EXPECT_TRUE(tilt, MP_BIN_SIZE(0xff)    == 0x101,   "SizeMacro.bin.ff");
    TILT_EXPECT_TRUE(tilt, MP_BIN_SIZE(0x100)   == 0x103,   "SizeMacro.bin.100");
    TILT_EXPECT_TRUE(tilt, MP_BIN_SIZE(0xffff)  == 0x10002, "SizeMacro.bin.ffff");
    TILT_EXPECT_TRUE(tilt, MP_BIN_SIZE(0x10000) == 0x10005, "SizeMacro.bin.10000");

    TILT_EXPECT_TRUE(tilt, MP_ARRAY_SIZE(0)       == 1, "SizeMacro.array.0");
    TILT_EXPECT_TRUE(tilt, MP_ARRAY_SIZE(0xf)     == 1, "SizeMacro.array.f");
    TILT_EXPECT_TRUE(tilt, MP_ARRAY_SIZE(0x10)    == 3, "SizeMacro.array.10");
    TILT_EXPECT_TRUE(tilt, MP_ARRAY_SIZE(0xffff)  == 3, "SizeMacro.array.ffff");
    TILT_EXPECT_TRUE(tilt, MP_ARRAY_SIZE(0x10000) == 5, "SizeMacro.array.10000");

    TILT_EXPECT_TRUE(tilt, MP_MAP_SIZE(0)       == 1, "SizeMacro.map.0");
    TILT_EXPECT_TRUE(tilt, MP_MAP_SIZE(0xf)     == 1, "SizeMacro.map.f");
    TILT_EXPECT_TRUE(tilt, MP_MAP_SIZE(0x10)    == 3, "SizeMacro.map.10");
    TILT_EXPECT_TRUE(tilt, MP_MAP_SIZE(0xffff)  == 3, "SizeMacro.map.ffff");
    TILT_EXPECT_TRUE(tilt, MP_MAP_SIZE(0x10000) == 5, "SizeMacro.map.10000");
}

#define DO_TEST_PUT_HELPER(type, macro) \
{\
    lt_memset(data, 0, sizeof(data));\
    s_mpLib->Init(&mp, data, sizeof(data));\
    TILT_EXPECT_TRUE(tilt, LTMessagePackPut(s_mpLib, &mp, LT_##macro##_MIN), "Failed to put LT_" #macro "_MIN");\
    mp.next = data;\
    type value;\
    TILT_EXPECT_TRUE(tilt, LTMessagePackGet(s_mpLib, &mp, &value)\
                           && value == LT_##macro##_MIN, "Failed to get LT_" #macro "_MIN");\
    lt_memset(data, 0, sizeof(data));\
    mp.next = data;\
    TILT_EXPECT_TRUE(tilt, LTMessagePackPut(s_mpLib, &mp, LT_##macro##_MAX), "Failed to put LT_" #macro "_MAX");\
    mp.next = data;\
    TILT_EXPECT_TRUE(tilt, LTMessagePackGet(s_mpLib, &mp, &value)\
                           && value == LT_##macro##_MAX, "Failed to get LT_" #macro "_MAX");\
}

static void TestPutHelpers(Tilt *tilt) {
    u8 data[32];
    LTMessagePack_Obj mp;

    lt_memset(data, 0, sizeof(data));
    mp.next = data;
    {
        bool value;

        lt_memset(data, 0, sizeof(data));
        s_mpLib->Init(&mp, data, sizeof(data));
        TILT_EXPECT_TRUE(tilt, LTMessagePackPut(s_mpLib, &mp, (bool)true), "Failed to put true");
        mp.next = data;
        TILT_EXPECT_TRUE(tilt, LTMessagePackGet(s_mpLib, &mp, &value)
                               && value == true, "Failed to get true");

        lt_memset(data, 0, sizeof(data));
        mp.next = data;
        TILT_EXPECT_TRUE(tilt, LTMessagePackPut(s_mpLib, &mp, (bool)false), "Failed to put false");
        mp.next = data;
        TILT_EXPECT_TRUE(tilt, LTMessagePackGet(s_mpLib, &mp, &value)
                               && value == false, "Failed to get false");
    }
    {
        const char* input = "Hello, world!";
        const char* value;

        lt_memset(data, 0, sizeof(data));
        s_mpLib->Init(&mp, data, sizeof(data));
        TILT_EXPECT_TRUE(tilt, LTMessagePackPut(s_mpLib, &mp, input), "Failed to put string");
        mp.next = data;
        TILT_EXPECT_TRUE(tilt, LTMessagePackGet(s_mpLib, &mp, &value)
                               && lt_strcmp(value, input) == 0, "Failed to get string");
    }

    DO_TEST_PUT_HELPER(u8, U8);
    DO_TEST_PUT_HELPER(s8, S8);
    DO_TEST_PUT_HELPER(u16, U16);
    DO_TEST_PUT_HELPER(s16, S16);
    DO_TEST_PUT_HELPER(u32, U32);
    DO_TEST_PUT_HELPER(s32, S32);
    DO_TEST_PUT_HELPER(u64, U64);
    DO_TEST_PUT_HELPER(s64, S64);
#if defined(__GNUC__)
    // LT doesn't have definitions of these and I just want to get this done...
    #define LT_FLT_MIN __FLT_MIN__
    #define LT_FLT_MAX __FLT_MAX__
    DO_TEST_PUT_HELPER(float, FLT);
    #define LT_DBL_MIN __DBL_MIN__
    DO_TEST_PUT_HELPER(double, DBL);
#endif
}

#undef DO_TEST_PUT_HELPER

static void BeforeAllTests(Tilt *tilt) {
    s_mpLib = lt_openlibrary(LTUtilityMessagePack);
    TILT_EXPECT_TRUE(tilt, s_mpLib != NULL, "Cannot open LTUtilityMessagePack");
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_closelibrary(s_mpLib);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { TestInitFree,         "InitFree",          "Basic init and free",     0 },

    { TestDecodeNil,        "DecodeNil",         "...",                     0 },
    { TestDecodeBool,       "DecodeBool",        "...",                     0 },
    { TestDecodeInteger,    "DecodeInteger",     "...",                     0 },
    { TestDecodeFloat,      "DecodeFloat",       "...",                     0 },
    { TestDecodeString,     "DecodeString",      "...",                     0 },
    { TestDecodeBinary,     "DecodeBinary",      "...",                     0 },
    { TestDecodeArray,      "DecodeArray",       "...",                     0 },
    { TestDecodeMap,        "DecodeMap",         "...",                     0 },
    { TestDecodeExtended,   "DecodeExtended",    "...",                     0 },
    { TestDecodeMultiple,   "DecodeMultiple",    "Decode typical message",  0 },

    { TestEncodeNil,        "EncodeNil",         "...",                     0 },
    { TestEncodeBoolean,    "EncodeBoolean",     "...",                     0 },
    { TestEncodeInteger,    "EncodeInteger",     "...",                     0 },
    { TestEncodeUnsigned,   "EncodeUnsigned",    "...",                     0 },
    { TestEncodeFloat,      "EncodeFloat",       "...",                     0 },
    { TestEncodeString,     "EncodeString",      "...",                     0 },
    { TestEncodeBinary,     "EncodeBinary",      "...",                     0 },
    { TestEncodeArray,      "EncodeArray",       "...",                     0 },
    { TestEncodeMap,        "EncodeMap",         "...",                     0 },
    { TestEncodeExtended,   "EncodeExtended",    "...",                     0 },
    { TestEncodeMultiple,   "EncodeMultiple",    "Encode typical message",  0 },

    { TestEncodePastEnd,    "EncodePastEnd",     "Write past-end detection",0 },
    { TestSkip,             "TestSkip",          "...",                     0 },
    { TestSkipWithin,       "TestWithin",        "...",                     0 },
    { TestSkipContainer,    "TestSkipContainer", "...",                     0 },
    { TestFindInteger,      "TestFindInteger",   "...",                     0 },
    { TestFindString,       "TestFindString",    "...",                     0 },
    { TestSizeMacros,       "TestSizeMacros",    "...",                     0 },

    { TestPutHelpers,       "TestPutHelpers",    "Generic put functions",   0 }
};

static int UnitTestLTUtilityMessagePackImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTUtilityMessagePackImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTUtilityMessagePackImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityMessagePack, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityMessagePack, UnitTestLTUtilityMessagePackImpl_Run, 1536) LTLIBRARY_DEFINITION;
