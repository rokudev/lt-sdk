/*******************************************************************************
 *
 * LTUtilityMessagePack.h - LT MessagePack Library
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/utility/messagepack/LTUtilityMessagePack.h>

// #define P LT_GetCore()->ConsolePrint

/*******************************************************************************
 * Numeric Conversions
 *******************************************************************************/

// Integer byte sequence conversions:
// Note: Compiler may support better way to do these but must work for non-aligned bytes, BE, and LE hosts.
#define Set16(p, v)                                                                                                    \
    *p++ = (v) >> 8;                                                                                                   \
    *p++ = v;
#define Set32(p, v)                                                                                                    \
    *p++ = (v) >> 24;                                                                                                  \
    *p++ = (v) >> 16;                                                                                                  \
    *p++ = (v) >> 8;                                                                                                   \
    *p++ = v;
#define Set64(p, v)                                                                                                    \
    *p++ = (v) >> 56;                                                                                                  \
    *p++ = (v) >> 48;                                                                                                  \
    *p++ = (v) >> 40;                                                                                                  \
    *p++ = (v) >> 32;                                                                                                  \
    *p++ = (v) >> 24;                                                                                                  \
    *p++ = (v) >> 16;                                                                                                  \
    *p++ = (v) >> 8;                                                                                                   \
    *p++ = v;
#define Get16(p) (p[0] << 8) | p[1]
#define Get32(p) (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]
#define Get64(p)                                                                                                       \
    (p[0] << 56) | (p[1] << 48) | (p[2] << 40) | (p[3] << 32) | (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7]

typedef union FloatConvert32 {
    float f;
    u32   u;
} FloatConvert32;
typedef union FloatConvert64 {
    double f;
    u64    u;
} FloatConvert64;

/*******************************************************************************
 * Helper Functions
 *******************************************************************************/

static u8 *DoEnsureAvailable(LTMessagePack_Obj *mp, u8 *bp, u32 need) {
    if (bp + need > mp->end) {
        if (!mp->allocator) return NULL;
        mp->next = bp;
        bp       = mp->allocator(mp, need);
        if (!bp) return NULL;
        if (bp + need > mp->end) return NULL;
    }
    return bp;
}

#define EnsureAvailable(need)                                                                                          \
    bp = DoEnsureAvailable(mp, bp, need);                                                                              \
    if (!bp) return false

/*******************************************************************************
 * Encoder Functions
 *******************************************************************************/

static bool PutByte(LTMessagePack_Obj *mp, u8 code) {
    if (!mp || !mp->next) return false;
    u8 *bp = mp->next;
    EnsureAvailable(sizeof(u8));
    *bp++    = code;
    mp->next = bp;
    return true;
}

static bool LTMessagePack_PutNil(LTMessagePack_Obj *mp) {
    return PutByte(mp, 0xc0);
}

static bool LTMessagePack_PutBoolean(LTMessagePack_Obj *mp, bool isTrue) {
    return PutByte(mp, isTrue ? 0xc3 : 0xc2);
}

static bool LTMessagePack_PutIntU32(LTMessagePack_Obj *mp, u32 integer) {
    if (!mp || !mp->next) return false;
    u8 *bp = mp->next;
    do {
        if (integer < 0x80) {
            EnsureAvailable(sizeof(u8));
            *bp++ = (u8)integer;
            break;
        }
        if (integer <= LT_U8_MAX) {
            EnsureAvailable(sizeof(u8) + sizeof(u8));
            *bp++ = 0xcc;
            *bp++ = (u8)integer;
            break;
        }
        if (integer <= LT_U16_MAX) {
            EnsureAvailable(sizeof(u8) + sizeof(u16));
            *bp++ = 0xcd;
            Set16(bp, integer);
            break;
        }
        EnsureAvailable(sizeof(u8) + sizeof(u32));
        *bp++ = 0xce;
        Set32(bp, integer);
        break;
    } while (false);
    mp->next = bp;
    return true;
}

static bool LTMessagePack_PutIntU64(LTMessagePack_Obj *mp, u64 integer) {
    if (integer <= LT_U32_MAX) {
        return LTMessagePack_PutIntU32(mp, integer);
    }
    if (!mp || !mp->next) return false;
    u8 *bp = mp->next;
    EnsureAvailable(sizeof(u8) + sizeof(u64));
    *bp++ = 0xcf;
    Set64(bp, integer);
    mp->next = bp;
    return true;
}

static bool LTMessagePack_PutIntS32(LTMessagePack_Obj *mp, s32 integer) {
    // Positive values get stored unsigned for an extra bit of representation
    // Decoders don't care if encoding is unsigned when less than 0x80000000
    if (integer >= 0x80) {
        return LTMessagePack_PutIntU32(mp, (u32)integer);
    }
    if (!mp || !mp->next) return false;
    u8 *bp = mp->next;
    do {
        if ((s64)integer >= -0x20) {
            EnsureAvailable(sizeof(s8));
            *bp++ = integer;
            break;
        }
        if ((s64)integer >= LT_S8_MIN) {
            EnsureAvailable(sizeof(s8) + sizeof(u8));
            *bp++ = 0xd0;
            *bp++ = integer;
            break;
        }
        if ((s64)integer >= LT_S16_MIN) {
            EnsureAvailable(sizeof(u8) + sizeof(s16));
            *bp++ = 0xd1;
            Set16(bp, integer);
            break;
        }
        EnsureAvailable(sizeof(u8) + sizeof(s32));
        *bp++ = 0xd2;
        Set32(bp, integer);
        break;
    } while (false);
    mp->next = bp;
    return true;
}

static bool LTMessagePack_PutIntS64(LTMessagePack_Obj *mp, s64 integer) {
    if (integer <= LT_S32_MAX && integer >= LT_S32_MIN) {
        return LTMessagePack_PutIntS32(mp, (s32)integer);
    }
    if (!mp || !mp->next) return false;
    u8 *bp = mp->next;
    EnsureAvailable(sizeof(u8) + sizeof(s64));
    *bp++ = 0xd3;
    Set64(bp, integer);
    mp->next = bp;
    return true;
}

static bool LTMessagePack_PutFloat32(LTMessagePack_Obj *mp, float value) {
    if (!mp || !mp->next) return false;
    u8 *bp = mp->next;
    EnsureAvailable(sizeof(u8) + sizeof(float));
    *bp++                  = 0xca;
    FloatConvert32 convert = {.f = value};
    Set32(bp, convert.u);
    mp->next = bp;
    return true;
}

static bool LTMessagePack_PutFloat64(LTMessagePack_Obj *mp, double value) {
    if (!mp || !mp->next) return false;
    u8 *bp = mp->next;
    EnsureAvailable(sizeof(u8) + sizeof(double));
    *bp++                  = 0xcb;
    FloatConvert64 convert = {.f = value};
    Set64(bp, convert.u);
    mp->next = bp;
    return true;
}

static u32 PutData(LTMessagePack_Obj *mp, const u8 *data, u32 size, bool isBinary) {
    if (!mp || !mp->next) return 0;
    u8 *bp = mp->next;
    do {
        if (size <= LT_U8_MAX) {
            bp = DoEnsureAvailable(mp, bp, size + sizeof(u8) + sizeof(u8));
            if (!bp) return 0;
            *bp++ = isBinary ? 0xc4 : 0xd9;
            *bp++ = (u8)size;
            break;
        }
        if (size <= LT_U16_MAX) {
            bp = DoEnsureAvailable(mp, bp, size + sizeof(u8) + sizeof(u16));
            if (!bp) return 0;
            *bp++ = isBinary ? 0xc5 : 0xda;
            Set16(bp, size);
            break;
        }
        bp = DoEnsureAvailable(mp, bp, size + sizeof(u8) + sizeof(u32));
        if (!bp) return 0;
        *bp++ = isBinary ? 0xc6 : 0xdb;
        Set32(bp, size);
        break;
    } while (false);
    u8 *dp = bp;
    if (data) lt_memcpy(bp, data, size);
    bp       += size;
    mp->next  = bp;
    return dp - mp->head;
}

static u32 LTMessagePack_PutString(LTMessagePack_Obj *mp, const char *data, u32 size) {
    if (size < 0x20) {  // Special string case
        if (!mp || !mp->next) return 0;
        if (!DoEnsureAvailable(mp, mp->next, size + sizeof(u8))) return 0;
        *mp->next++ = 0xa0 | size;
        u8 *dp      = mp->next;
        lt_memcpy(mp->next, data, size);
        mp->next += size;
        return dp - mp->head;
    }
    return PutData(mp, (u8 *)data, size, false);
}

static bool LTMessagePack_PutCString(LTMessagePack_Obj *mp, const char *data) {
    u32 size = lt_strlen(data) + 1;  // always includes terminator
    return LTMessagePack_PutString(mp, data, size);
}

static u32 LTMessagePack_PutBinary(LTMessagePack_Obj *mp, const u8 *data, u32 size) {
    return PutData(mp, data, size, true);
}

static bool PutArrayMap(LTMessagePack_Obj *mp, u32 size, bool isMap) {
    if (!mp || !mp->next) return false;
    u8 *bp = mp->next;
    if (size < 0x10) {
        EnsureAvailable(sizeof(u8));
        *bp++ = (isMap ? 0x80 : 0x90) | (u8)size;
    } else if (size <= LT_U16_MAX) {
        EnsureAvailable(sizeof(u8) + sizeof(u16));
        *bp++ = isMap ? 0xde : 0xdc;
        Set16(bp, size);
    } else {
        EnsureAvailable(sizeof(u8) + sizeof(u32));
        *bp++ = isMap ? 0xdf : 0xdd;
        Set32(bp, size);
    }
    mp->next = bp;
    return true;
}

static bool LTMessagePack_PutArray(LTMessagePack_Obj *mp, u32 size) {
    return PutArrayMap(mp, size, false);
}

static bool LTMessagePack_PutMap(LTMessagePack_Obj *mp, u32 size) {
    return PutArrayMap(mp, size, true);
}

static bool LTMessagePack_PutExtended(LTMessagePack_Obj *mp, s8 type, const u8 *data, u32 size) {
    if (!mp || !mp->next) return false;
    u8 *bp = mp->next;
    do {
        EnsureAvailable(sizeof(u8));
        *bp = 0;
        switch (size) {  // check if it fits in fixed-ext type
            case 1:
                *bp = 0xd4;
                break;
            case 2:
                *bp = 0xd5;
                break;
            case 4:
                *bp = 0xd6;
                break;
            case 8:
                *bp = 0xd7;
                break;
            case 16:
                *bp = 0xd8;
                break;
            default:
                break;
        }
        if (*bp) {
            EnsureAvailable(size + (2 * sizeof(u8)));
            bp++;
            *bp++ = type;
            break;
        }
        if (size <= LT_U8_MAX) {
            EnsureAvailable(size + (3 * sizeof(u8)));
            *bp++ = 0xc7;
            *bp++ = size;
            *bp++ = (u8)type;
            break;
        }
        if (size <= LT_U16_MAX) {
            EnsureAvailable(size + sizeof(u8) + sizeof(u16) + sizeof(u8));
            *bp++ = 0xc8;
            Set16(bp, size);
            *bp++ = (u8)type;
            break;
        }
        EnsureAvailable(size + sizeof(u8) + sizeof(u32) + sizeof(u8));
        *bp++ = 0xc9;
        Set32(bp, size);
        *bp++ = (u8)type;
        break;
    } while (false);

    lt_memcpy(bp, data, size);
    bp       += size;
    mp->next  = bp;
    return true;
}

static bool LTMessagePack_PutMessagePack(LTMessagePack_Obj *mp, const LTMessagePack_Obj *source) {
    u8 *bp = mp->next;
    EnsureAvailable(source->next - source->head);
    for (const u8 *src = source->head; src < source->next; ++src, ++bp) *bp = *src;
    mp->next = bp;
    return true;
}

/*******************************************************************************
 * Decoder Functions
 *******************************************************************************/

// Enum of MessagePack types used for switch statements
typedef enum MPTypeCase {
    MPT_Error,
    MPT_Nil,
    MPT_False,
    MPT_True,
    MPT_FixSInt,
    MPT_IntS8,
    MPT_IntS16,
    MPT_IntS32,
    MPT_IntS64,
    MPT_FixUInt,
    MPT_IntU8,
    MPT_IntU16,
    MPT_IntU32,
    MPT_IntU64,
    MPT_Float32,
    MPT_Float64,
    MPT_FixStr,
    MPT_Str8,
    MPT_Str16,
    MPT_Str32,
    MPT_Bin8,
    MPT_Bin16,
    MPT_Bin32,
    MPT_FixMap,
    MPT_Map16,
    MPT_Map32,
    MPT_FixArray,
    MPT_Array16,
    MPT_Array32,
    MPT_FExt1,
    MPT_FExt2,
    MPT_FExt4,
    MPT_FExt8,
    MPT_FExt16,
    MPT_Ext8,
    MPT_Ext16,
    MPT_Ext32,
} MPTypeCase;

// Map of MessagePack encodings from C0 to DF for switch cases
static const MPTypeCase MPTCaseMap[0x20] = {MPT_Nil,    // Starts at C0
                                            MPT_Error,  // C1 never used
                                            MPT_False,  MPT_True,   MPT_Bin8,    MPT_Bin16,   MPT_Bin32,  MPT_Ext8,
                                            MPT_Ext16,  MPT_Ext32,  MPT_Float32, MPT_Float64, MPT_IntU8,  MPT_IntU16,
                                            MPT_IntU32, MPT_IntU64, MPT_IntS8,   MPT_IntS16,  MPT_IntS32, MPT_IntS64,
                                            MPT_FExt1,  MPT_FExt2,  MPT_FExt4,   MPT_FExt8,   MPT_FExt16, MPT_Str8,
                                            MPT_Str16,  MPT_Str32,  MPT_Array16, MPT_Array32, MPT_Map16,  MPT_Map32};

// For an encoded byte, return its MP case:
static MPTypeCase WhatCase(u8 byte) {
    if (byte < 0x80) return MPT_FixUInt;
    if (byte < 0x90) return MPT_FixMap;
    if (byte < 0xa0) return MPT_FixArray;
    if (byte < 0xc0) return MPT_FixStr;
    if (byte >= 0xe0) return MPT_FixSInt;
    return MPTCaseMap[byte - 0xc0];  // 0xc0 - 0xdf
}

static s8 LTMessagePack_GetInteger(LTMessagePack_Obj *mp, u64 *integer) {
    // The return code indicates the size of the integer.
    // Negative means signed.
    // Returns zero if not a valid integer type or end of data.
    *integer = 0;
    s8 size  = 0;
    if (!mp || !mp->next) return 0;
    u8 *bp = mp->next;
    EnsureAvailable(sizeof(u8));
    u8 byte = *bp++;
    u8 code = WhatCase(byte);
    // Fast track:
    if (code == MPT_FixUInt) {
        *integer = (u64)byte;
        mp->next = bp;
        return 1;
    }
    if (code == MPT_FixSInt) {
        *integer = (s64)(s8)byte;
        mp->next = bp;
        return -1;
    }
    if (code < MPT_FixSInt || code > MPT_IntU64) return 0;
    if (code >= MPT_IntU8)
        size = code - MPT_IntU8;
    else
        size = code - MPT_IntS8;
    size = 1 << size;
    EnsureAvailable(size);
    switch (code) {
        case MPT_IntU8:
            *integer = *bp;
            break;
        case MPT_IntS8:
            *integer = (s8)*bp;
            break;
        case MPT_IntU16:
            *integer = Get16(bp);
            break;
        case MPT_IntS16:
            *integer = (s64)(s16)Get16(bp);
            break;
        case MPT_IntU32:
            *integer = (u32)Get32(bp);
            break;
        case MPT_IntS32:
            *integer = (s64)(s32)Get32(bp);
            break;
        case MPT_IntU64:
            *integer = Get64((u64)bp);
            break;
        case MPT_IntS64:
            *integer = Get64((s64)bp);
            break;
    }
    bp       += size;
    mp->next  = bp;
    return code > MPT_FixUInt ? size : -size;
}

static LTMessagePack_Type LTMessagePack_GetString(LTMessagePack_Obj *mp, u8 **string, u32 *length) {
    *string = (u8 *)"<null>";
    *length = 0;
    if (!mp || !mp->next) return LTMessagePack_Type_Error;
    u8 *bp = mp->next;
    EnsureAvailable(sizeof(u8));
    u8  byte = *bp++;
    u32 size = 0;
    u8  type = LTMessagePack_Type_String;
    u8  code = WhatCase(byte);
    switch (code) {
        case MPT_FixStr:
            size = 0x1f & byte;
            break;
        case MPT_Bin8:
            type = LTMessagePack_Type_Binary;
            // fall-thru
        case MPT_Str8:
            EnsureAvailable(sizeof(u8));
            size = *bp++;
            break;
        case MPT_Bin16:
            type = LTMessagePack_Type_Binary;
            // fall-thru
        case MPT_Str16:
            EnsureAvailable(sizeof(u16));
            size  = Get16(bp);
            bp   += sizeof(u16);
            break;
        case MPT_Bin32:
            type = LTMessagePack_Type_Binary;
            // fall-thru
        case MPT_Str32:
            EnsureAvailable(sizeof(u32));
            size  = Get32(bp);
            bp   += sizeof(u32);
            break;
        default:
            return LTMessagePack_Type_Error;  // not of the required datatype
    }
    if (size) {
        EnsureAvailable(size);
        *string  = bp;
        bp      += size;
    }
    mp->next = bp;
    *length  = size;
    return type;
}

static LTMessagePack_Type LTMessagePack_GetArray(LTMessagePack_Obj *mp, u32 *length) {
    *length = 0;
    if (!mp || !mp->next) return LTMessagePack_Type_Error;
    u8 *bp = mp->next;
    EnsureAvailable(sizeof(u8));
    u8  byte = *bp++;
    u32 size = 0;
    u8  type = LTMessagePack_Type_Array;
    switch (WhatCase(byte)) {
        case MPT_FixMap:
            type = LTMessagePack_Type_Map;
            // fall-thru
        case MPT_FixArray:
            size = 0x0f & byte;
            break;
        case MPT_Map16:
            type = LTMessagePack_Type_Map;
            // fall-thru
        case MPT_Array16:
            EnsureAvailable(sizeof(u16));
            size  = Get16(bp);
            bp   += sizeof(u16);
            break;
        case MPT_Map32:
            type = LTMessagePack_Type_Map;
            // fall-thru
        case MPT_Array32:
            EnsureAvailable(sizeof(u32));
            size  = Get32(bp);
            bp   += sizeof(u32);
            break;
        default:
            return LTMessagePack_Type_Error;  // not of the required datatype
    }
    mp->next = bp;
    *length  = size;
    return type;
}

static s8 LTMessagePack_GetExtended(LTMessagePack_Obj *mp, u8 **data, u32 *length) {
    // returns the extension type, pointer to data, and length of data
    // zero on error
    *length = 0;
    if (!mp || !mp->next) return LTMessagePack_Type_Error;
    u8 *bp = mp->next;
    EnsureAvailable(sizeof(u8));
    u8  byte = *bp++;
    u32 size = 0;
    s8  type = 0;
    u8  code = WhatCase(byte);
    switch (code) {
        case MPT_FExt1:
        case MPT_FExt2:
        case MPT_FExt4:
        case MPT_FExt8:
        case MPT_FExt16:
            EnsureAvailable(sizeof(u8));
            size = 1 << (code - MPT_FExt1);
            type = (s8)*bp++;
            break;
        case MPT_Ext8:
            EnsureAvailable(sizeof(u8) + sizeof(s8));
            size = *bp++;
            type = (s8)*bp++;
            break;
        case MPT_Ext16:
            EnsureAvailable(sizeof(u16) + sizeof(s8));
            size  = Get16(bp);
            bp   += sizeof(u16);
            type  = (s8)*bp++;
            break;
        case MPT_Ext32:
            EnsureAvailable(sizeof(u32) + sizeof(s8));
            size  = Get32(bp);
            bp   += sizeof(u32);
            type  = (s8)*bp++;
            break;
        default:
            return LTMessagePack_Type_Error;  // not of the required datatype
    }
    if (size) {
        EnsureAvailable(size);
        *data  = bp;
        bp    += size;
    }
    mp->next = bp;
    *length  = size;
    return type;
}

static LTMessagePack_Type LTMessagePack_GetValue(LTMessagePack_Obj *mp, LTMessagePack_Value *value) {
    *value = (LTMessagePack_Value){};
    u8 type;
    if (!mp || !mp->next) return LTMessagePack_Type_Error;
    u8 *bp = mp->next;
    EnsureAvailable(sizeof(u8));
    switch (WhatCase(*bp)) {
        case MPT_Nil:
            type     = LTMessagePack_Type_Nil;
            mp->next = bp + sizeof(u8);
            break;
        case MPT_False:
            value->boolean = false;
            type           = LTMessagePack_Type_Boolean;
            mp->next       = bp + sizeof(u8);
            break;
        case MPT_True:
            value->boolean = true;
            type           = LTMessagePack_Type_Boolean;
            mp->next       = bp + sizeof(u8);
            break;
        //-- Integers
        case MPT_FixUInt:
        case MPT_FixSInt:
        case MPT_IntU8:
        case MPT_IntU16:
        case MPT_IntU32:
        case MPT_IntU64:
        case MPT_IntS8:
        case MPT_IntS16:
        case MPT_IntS32:
        case MPT_IntS64:
            type = LTMessagePack_GetInteger(mp, &value->uinteger) ? LTMessagePack_Type_Integer
                                                                  : LTMessagePack_Type_Error;
            break;
        //-- Floats
        case MPT_Float32:
            bp++;
            EnsureAvailable(sizeof(float));
            value->uinteger = Get32((u32)bp);
            type            = LTMessagePack_Type_Float32;
            mp->next        = bp + sizeof(float);
            break;
        case MPT_Float64:
            bp++;
            EnsureAvailable(sizeof(double));
            value->uinteger = Get64((u64)bp);
            type            = LTMessagePack_Type_Float64;
            mp->next        = bp + sizeof(double);
            break;
        //-- Strings and binaries
        case MPT_FixStr:
        case MPT_Bin8:
        case MPT_Str8:
        case MPT_Bin16:
        case MPT_Str16:
        case MPT_Bin32:
        case MPT_Str32:
            type = LTMessagePack_GetString(mp, &value->data, &value->size);
            break;
        //-- Arrays and Maps
        case MPT_FixMap:
        case MPT_FixArray:
        case MPT_Array16:
        case MPT_Array32:
        case MPT_Map16:
        case MPT_Map32:
            type        = LTMessagePack_GetArray(mp, &value->size);
            value->data = mp->next;
            break;
        //-- Extended types
        case MPT_FExt1:
        case MPT_FExt2:
        case MPT_FExt4:
        case MPT_FExt8:
        case MPT_FExt16:
        case MPT_Ext8:
        case MPT_Ext16:
        case MPT_Ext32:
            value->extType = LTMessagePack_GetExtended(mp, &value->data, &value->size);
            type           = LTMessagePack_Type_Extended;
            break;
        default:
            type = LTMessagePack_Type_Error;
            break;  // 0xc1 is the only case here
    }
    value->type = type;
    return type;
}

/*******************************************************************************
 * Skip and Find
 *******************************************************************************/

static void LTMessagePack_Skip(LTMessagePack_Obj *mp, u32 count) {
    for (u32 n = 0; n < count; n++) {
        // Possible to speed this up, but expands code size. Probably not worth it.
        LTMessagePack_Value value;
        if (!LTMessagePack_GetValue(mp, &value)) break;
    }
}

static bool LTMessagePack_SkipContainer(LTMessagePack_Obj *mp, const LTMessagePack_Value *container) {
    if (!mp || !mp->next || !container || !container->data) return false;
    if (container->type != LTMessagePack_Type_Array && container->type != LTMessagePack_Type_Map) return false;

    u32 remaining_items = container->size;
    if (container->type == LTMessagePack_Type_Map) remaining_items *= 2;

    LTMessagePack_Value value;
    while (remaining_items--) {
        if (!LTMessagePack_GetValue(mp, &value)) return false;
        if (value.type == LTMessagePack_Type_Array) {
            remaining_items += value.size;
        } else if (value.type == LTMessagePack_Type_Map) {
            remaining_items += (2 * value.size);
        }
    }
    return true;
}

static void LTMessagePack_SkipWithin(LTMessagePack_Obj *mp, const LTMessagePack_Value *container, u32 count) {
    if (!mp || !mp->next || !container) return;
    LTMessagePack_Obj mpEnd = *mp;
    if (!LTMessagePack_SkipContainer(&mpEnd, container)) return;  // gives us end location
    if (count > container->size)
        mp->next = mpEnd.next;
    else
        LTMessagePack_Skip(mp, container->type == LTMessagePack_Type_Map ? count * 2 : count);
}

static bool Find(LTMessagePack_Obj *mp, const LTMessagePack_Value *container, LTMessagePack_Value *target) {
    if (!mp || !mp->next || !target) return false;
    u8 *end = mp->end;
    if (container) {
        mp->next                = container->data;
        LTMessagePack_Obj mpEnd = *mp;
        if (!LTMessagePack_SkipContainer(&mpEnd, container)) return false;
        end = mpEnd.next;
    }
    LTMessagePack_Value value;
    u8                 *first = mp->next;
    u8                 *last  = mp->next;
    u8                  type;
    do {
        if (mp->next >= end) break;  // not found
        type = LTMessagePack_GetValue(mp, &value);
        if (!type) break;
        if (type == target->type) {
            if (type == LTMessagePack_Type_String || type == LTMessagePack_Type_Binary) {
                if (target->size == value.size && lt_memcmp(target->data, value.data, target->size) == 0) {
                    if (!container || container->type == LTMessagePack_Type_Array) mp->next = last;  // wind back one
                    return true;
                }
            }
            if (type == LTMessagePack_Type_Integer) {
                if (target->integer == value.integer) {
                    if (!container || container->type == LTMessagePack_Type_Array) mp->next = last;  // wind back one
                    return true;
                }
            }
        }
        if (container && container->type == LTMessagePack_Type_Map) {
            if (!LTMessagePack_GetValue(mp, &value)) break;  // skip map value
        }
        last = mp->next;
    } while (true);
    mp->next = first;  // restore position
    return false;
}

static bool LTMessagePack_FindInteger(LTMessagePack_Obj *mp, const LTMessagePack_Value *container, s64 target) {
    LTMessagePack_Value value = {.type = LTMessagePack_Type_Integer, .integer = target};
    return Find(mp, container, &value);
}

static bool LTMessagePack_FindCString(LTMessagePack_Obj *mp, const LTMessagePack_Value *container, char *target) {
    LTMessagePack_Value value = {
        .type = LTMessagePack_Type_String,
        .size = lt_strlen(target) + 1,
        .data = (u8 *)target,
    };
    return Find(mp, container, &value);
}

/*******************************************************************************
 * General Functions
 *******************************************************************************/

static u8 *LTMessagePack_DefaultRealloc(struct LTMessagePack_Obj *obj, LT_SIZE requested) {
    if (requested == 0) {
        lt_free(obj->head);
        *obj = (LTMessagePack_Obj){
            // sets unlisted fields to zero
            .head = (u8 *)0xdeadbeef,  // cause trouble if used
            .next = NULL               // primary checkstop for API
        };
        return NULL;
    } else {
        u32 len      = obj->next - obj->head;
        u32 size     = len + requested;
        u8 *new_head = lt_realloc(obj->head, size);
        if (!new_head) return false;
        obj->head = new_head;
        obj->next = new_head + len;
        obj->end  = new_head + size;
    }
    return obj->next;
}

static bool LTMessagePack_Init(LTMessagePack_Obj *mp, u8 *data, u32 dataSize) {
    if (!mp || !dataSize) return false;
    *mp           = (LTMessagePack_Obj){};  // all fields zeroed
    mp->allocator = NULL;
    if (!data) {
        if (!dataSize) return false;
        data = lt_malloc(dataSize);
        if (!data) return false;
        mp->allocator = LTMessagePack_DefaultRealloc;
    }
    mp->head = data, mp->next = data, mp->end = data + dataSize;
    return true;
}

static void LTMessagePack_Free(LTMessagePack_Obj *mp) {
    if (!mp || !mp->allocator || !mp->next) return;
    mp->allocator(mp, 0);
}

static u32 LTMessagePack_GetPosition(LTMessagePack_Obj *mp) {
    if (!mp || !mp->next) return 0;
    ;
    return (u32)(mp->next - mp->head);
}

static void LTMessagePack_SetPosition(LTMessagePack_Obj *mp, u32 position) {
    if (!mp || !mp->next) return;
    mp->next = (mp->head + position >= mp->end) ? mp->end : mp->head + position;
}

static bool Buffer_Write8(LTBuffer *buf, u8 value) {
    return buf->API->Write(buf, LTBuffer_MakeSpan(&value, sizeof(value))).size == sizeof(value);
}

static bool Buffer_Write16(LTBuffer *buf, u16 value) {
    value = LT_HTONS(value);
    return buf->API->Write(buf, LTBuffer_MakeSpan(&value, sizeof(value))).size == sizeof(value);
}

static bool Buffer_Write32(LTBuffer *buf, u32 value) {
    value = LT_HTONL(value);
    return buf->API->Write(buf, LTBuffer_MakeSpan(&value, sizeof(value))).size == sizeof(value);
}

static bool Buffer_Write64(LTBuffer *buf, u64 value) {
    value = LT_HTONLL(value);
    return buf->API->Write(buf, LTBuffer_MakeSpan(&value, sizeof(value))).size == sizeof(value);
}

static u16 Buffer_Swap16(u8 *bytes) {
    return LT_NTOHS(((u16)bytes[0]) | ((u16)bytes[1]) << 8);
}

static u32 Buffer_Swap32(u8 *bytes) {
    return LT_NTOHL(((u32)bytes[0]) | ((u32)bytes[1]) << 8 | ((u32)bytes[2]) << 16 | ((u32)bytes[3]) << 24);
}

static u64 Buffer_Swap64(u8 *bytes) {
    return LT_NTOHLL(((u64)bytes[0]) | ((u64)bytes[1]) << 8 | ((u64)bytes[2]) << 16 | ((u64)bytes[3]) << 24
                     | ((u64)bytes[4]) << 32 | ((u64)bytes[5]) << 40 | ((u64)bytes[6]) << 48 | ((u64)bytes[7]) << 56);
}

static bool Buffer_WriteData(LTBuffer *buf, const u8 *data, u32 size, bool isBinary) {
    bool success;
    if (size <= LT_U8_MAX) {
        success = Buffer_Write8(buf, isBinary ? 0xc4 : 0xd9) && Buffer_Write8(buf, (u8)size);
    } else if (size <= LT_U16_MAX) {
        success = Buffer_Write8(buf, isBinary ? 0xc5 : 0xda) && Buffer_Write16(buf, size);
    } else {
        success = Buffer_Write8(buf, isBinary ? 0xc6 : 0xdb) && Buffer_Write32(buf, size);
    }
    if (!success) {
        return false;
    }
    if (data) {
        return buf->API->Write(buf, LTBuffer_MakeSpan(data, size)).size == size;
    } else {
        return true;
    }
}

static bool Buffer_WriteContainer(LTBuffer *buf, u32 size, bool isMap) {
    if (size < 0x10) {
        return Buffer_Write8(buf, (isMap ? 0x80 : 0x90) | (u8)size);
    } else if (size <= LT_U16_MAX) {
        return Buffer_Write8(buf, (isMap ? 0xde : 0xdc)) && Buffer_Write16(buf, size);
    } else {
        return Buffer_Write8(buf, (isMap ? 0xdf : 0xdd)) && Buffer_Write32(buf, size);
    }
}

static bool LTMessagePack_WriteNil(LTBuffer *buf) {
    return Buffer_Write8(buf, 0xc0);
}

static bool LTMessagePack_WriteBoolean(LTBuffer *buf, bool value) {
    return Buffer_Write8(buf, value ? 0xc3 : 0xc2);
}

static bool LTMessagePack_WriteIntU32(LTBuffer *buf, u32 integer) {
    if (integer < 0x80) {
        return Buffer_Write8(buf, (u8)integer);
    } else if (integer <= LT_U8_MAX) {
        return Buffer_Write8(buf, (u8)0xcc) && Buffer_Write8(buf, (u8)integer);
    } else if (integer <= LT_U16_MAX) {
        return Buffer_Write8(buf, (u8)0xcd) && Buffer_Write16(buf, integer);
    } else {
        return Buffer_Write8(buf, (u8)0xce) && Buffer_Write32(buf, integer);
    }
}

static bool LTMessagePack_WriteIntU64(LTBuffer *buf, u64 integer) {
    if (integer <= LT_U32_MAX) {
        return LTMessagePack_WriteIntU32(buf, integer);
    }
    return Buffer_Write8(buf, (u8)0xcf) && Buffer_Write64(buf, integer);
}

static bool LTMessagePack_WriteIntS32(LTBuffer *buf, s32 integer) {
    if (integer >= 0x80) {
        return LTMessagePack_WriteIntU32(buf, (u32)integer);
    } else if ((s64)integer >= -0x20) {
        return Buffer_Write8(buf, (u8)integer);
    } else if ((s64)integer >= LT_S8_MIN) {
        return Buffer_Write8(buf, (u8)0xd0) && Buffer_Write8(buf, (u8)integer);
    } else if ((s64)integer >= LT_S16_MIN) {
        return Buffer_Write8(buf, (u8)0xd1) && Buffer_Write16(buf, integer);
    } else {
        return Buffer_Write8(buf, (u8)0xd2) && Buffer_Write32(buf, integer);
    }
}

static bool LTMessagePack_WriteIntS64(LTBuffer *buf, s64 integer) {
    if (integer <= LT_S32_MAX && integer >= LT_S32_MIN) {
        return LTMessagePack_WriteIntS32(buf, (s32)integer);
    }
    return Buffer_Write8(buf, (u8)0xd3) && Buffer_Write64(buf, integer);
}

static bool LTMessagePack_WriteFloat32(LTBuffer *buf, float value) {
    FloatConvert32 convert = {.f = value};
    return Buffer_Write8(buf, (u8)0xca) && Buffer_Write32(buf, convert.u);
}

static bool LTMessagePack_WriteFloat64(LTBuffer *buf, double value) {
    FloatConvert64 convert = {.f = value};
    return Buffer_Write8(buf, (u8)0xcb) && Buffer_Write64(buf, convert.u);
}

static bool LTMessagePack_WriteString(LTBuffer *buf, const char *data, u32 size) {
    if (size < 0x20) {
        if (!Buffer_Write8(buf, (u8)(0xa0 | size))) return false;
        return data ? buf->API->Write(buf, LTBuffer_MakeSpan(data, size)).size == size : true;
    }
    return Buffer_WriteData(buf, (u8 *)data, size, false);
}

static bool LTMessagePack_WriteCString(LTBuffer *buf, const char *data, bool terminated) {
    return LTMessagePack_WriteString(buf, data, lt_strlen(data) + (terminated ? 1 : 0));
}

static u32 LTMessagePack_VPrintString(LTBuffer *buf, const char *format, lt_va_list args) {
    lt_va_list copy;
    lt_va_copy(copy, args);
    u32 size = lt_vsnprintf(NULL, 0, format, copy);
    lt_va_end(copy);
    LTMessagePack_WriteString(buf, NULL, size + 1);
    LTBufferAccess access = buf->API->StartWrite(buf, size + 1);
    lt_vsnprintf((char *)access.span.data, access.span.size, format, args);
    buf->API->FinishWrite(buf, access);
    return size;
}

static u32 LTMessagePack_PrintString(LTBuffer *buf, const char *format, ...) {
    lt_va_list args;
    lt_va_start(args, format);
    u32 size = LTMessagePack_VPrintString(buf, format, args);
    lt_va_end(args);
    return size;
}

static bool LTMessagePack_WriteBinary(LTBuffer *buf, const u8 *data, u32 size) {
    return Buffer_WriteData(buf, (u8 *)data, size, true);
}

static bool LTMessagePack_WriteArray(LTBuffer *buf, u32 size) {
    return Buffer_WriteContainer(buf, size, false);
}

static bool LTMessagePack_WriteMap(LTBuffer *buf, u32 size) {
    return Buffer_WriteContainer(buf, size, true);
}

static bool LTMessagePack_WriteExtended(LTBuffer *buf, s8 type, u32 size) {
    bool success = true;
    switch (size) {
        case 1:
            success = Buffer_Write8(buf, 0xd4);
            break;
        case 2:
            success = Buffer_Write8(buf, 0xd5);
            break;
        case 4:
            success = Buffer_Write8(buf, 0xd6);
            break;
        case 8:
            success = Buffer_Write8(buf, 0xd7);
            break;
        case 16:
            success = Buffer_Write8(buf, 0xd8);
            break;
        default:
            if (size <= LT_U8_MAX) {
                success = Buffer_Write8(buf, 0xc7) && Buffer_Write8(buf, size);
            } else if (size <= LT_U16_MAX) {
                success = Buffer_Write8(buf, 0xc8) && Buffer_Write16(buf, size);
            } else {
                success = Buffer_Write8(buf, 0xc9) && Buffer_Write32(buf, size);
            }
            break;
    }
    if (success) success = Buffer_Write8(buf, type);

    return success;
}

static bool LTMessagePack_WriteTable(LTBuffer *buf, u32 headerRows, u32 headerColumns, s32 size) {
    u32 extSize = 1;
    u32 flags   = 0;
    if (size >= 0) {
        extSize += MP_U32_SIZE(size);
    } else {
        flags |= 0b1;
    }
    if (headerRows) {
        extSize += MP_U32_SIZE(headerRows);
        flags   |= 0b10;
    }
    if (headerColumns) {
        extSize += MP_U32_SIZE(headerColumns);
        flags   |= 0b100;
    }
    return LTMessagePack_WriteExtended(buf, 16, extSize) && LTMessagePack_WriteIntU32(buf, flags)
           && (!headerRows || LTMessagePack_WriteIntU32(buf, headerRows))
           && (!headerColumns || LTMessagePack_WriteIntU32(buf, headerColumns))
           && ((size < 0) || LTMessagePack_WriteIntU32(buf, size));
}

static bool LTMessagePack_ReadNil(LTBuffer *buf) {
    u8           data[1];
    LTBufferSpan read = buf->API->Peek(buf, LTBuffer_MakeSpan(data, sizeof(data)), 0);
    if (data[0] != 0xc0 || read.size != sizeof(data)) {
        return false;
    }
    buf->API->Skip(buf, 1);
    return true;
}

static bool LTMessagePack_ReadBoolean(LTBuffer *buf, bool *value) {
    u8           data[1];
    LTBufferSpan read = buf->API->Peek(buf, LTBuffer_MakeSpan(data, sizeof(data)), 0);
    if (read.size != 1) {
        return false;
    }
    switch (data[0]) {
        case 0xc3:
            *value = true;
            break;
        case 0xc2:
            *value = false;
            break;
        default:
            return false;
    }
    buf->API->Skip(buf, 1);
    return true;
}

static u32 LTMessagePack_PeekInteger(LTBuffer *buf, u64 *value, u32 offset) {
    u8           data[9];
    u32          consumed = 0;
    LTBufferSpan read     = buf->API->Peek(buf, LTBuffer_MakeSpan(data, sizeof(data)), offset);
    if (data[0] <= 0x7f) {
        *value   = data[0];
        consumed = 1;
    } else if (data[0] >= 0xe0) {
        *(s64 *)value = (s8)data[0];
        consumed      = 1;
    } else {
        switch (data[0]) {
            case 0xcc:
                consumed = 2;
                *value   = data[1];
                break;
            case 0xd0:
                consumed      = 2;
                *(s64 *)value = (s8)data[1];
                break;
            case 0xcd:
                consumed = 3;
                *value   = Buffer_Swap16(data + 1);
                break;
            case 0xd1:
                consumed      = 3;
                *(s64 *)value = (s16)Buffer_Swap16(data + 1);
                break;
            case 0xce:
                consumed = 5;
                *value   = Buffer_Swap32(data + 1);
                break;
            case 0xd2:
                consumed      = 5;
                *(s64 *)value = (s32)Buffer_Swap32(data + 1);
                break;
            case 0xcf:
                consumed = 9;
                *value   = Buffer_Swap64(data + 1);
                break;
            case 0xd3:
                consumed      = 9;
                *(s64 *)value = (s64)Buffer_Swap64(data + 1);
                break;
        }
    }
    if (read.size < consumed) {
        *value = consumed;
        return 0;
    }

    return consumed;
}

static bool LTMessagePack_ReadInteger(LTBuffer *buf, u64 *value) {
    return buf->API->Skip(buf, LTMessagePack_PeekInteger(buf, value, 0)) > 0;
}

static bool LTMessagePack_ReadIntU8(LTBuffer *buf, u8 *value) {
    u64  tmp;
    bool result = LTMessagePack_ReadInteger(buf, &tmp);
    *value      = tmp;
    return result;
}

static bool LTMessagePack_ReadIntU16(LTBuffer *buf, u16 *value) {
    u64  tmp;
    bool result = LTMessagePack_ReadInteger(buf, &tmp);
    *value      = tmp;
    return result;
}

static bool LTMessagePack_ReadIntU32(LTBuffer *buf, u32 *value) {
    u64  tmp;
    bool result = LTMessagePack_ReadInteger(buf, &tmp);
    *value      = tmp;
    return result;
}

static bool LTMessagePack_ReadIntU64(LTBuffer *buf, u64 *value) {
    u64  tmp;
    bool result = LTMessagePack_ReadInteger(buf, &tmp);
    *value      = tmp;
    return result;
}

static bool LTMessagePack_ReadIntS8(LTBuffer *buf, s8 *value) {
    s64  tmp;
    bool result = LTMessagePack_ReadInteger(buf, (u64 *)&tmp);
    *value      = tmp;
    return result;
}

static bool LTMessagePack_ReadIntS16(LTBuffer *buf, s16 *value) {
    s64  tmp;
    bool result = LTMessagePack_ReadInteger(buf, (u64 *)&tmp);
    *value      = tmp;
    return result;
}

static bool LTMessagePack_ReadIntS32(LTBuffer *buf, s32 *value) {
    s64  tmp;
    bool result = LTMessagePack_ReadInteger(buf, (u64 *)&tmp);
    *value      = tmp;
    return result;
}

static bool LTMessagePack_ReadIntS64(LTBuffer *buf, s64 *value) {
    s64  tmp;
    bool result = LTMessagePack_ReadInteger(buf, (u64 *)&tmp);
    *value      = tmp;
    return result;
}

static u32 LTMessagePack_PeekFloat32(LTBuffer *buf, float *value, u32 offset) {
    u8 header[5];
    if (buf->API->Peek(buf, LTBuffer_MakeSpan(header, sizeof(header)), offset).size != sizeof(header)
        || header[0] != 0xca) {
        return 0;
    }
    FloatConvert32 convert;
    convert.u = Buffer_Swap32(header + 1);
    *value    = convert.f;
    return sizeof(header);
}

static bool LTMessagePack_ReadFloat32(LTBuffer *buf, float *value) {
    return buf->API->Skip(buf, LTMessagePack_PeekFloat32(buf, value, 0)) > 0;
}

static u32 LTMessagePack_PeekFloat64(LTBuffer *buf, double *value, u32 offset) {
    u8 header[9];
    if (buf->API->Peek(buf, LTBuffer_MakeSpan(header, sizeof(header)), offset).size != sizeof(header)
        || header[0] != 0xcb) {
        return false;
    }
    FloatConvert64 convert;
    convert.u = Buffer_Swap64(header + 1);
    *value    = convert.f;
    return true;
}

static bool LTMessagePack_ReadFloat64(LTBuffer *buf, double *value) {
    return buf->API->Skip(buf, LTMessagePack_PeekFloat64(buf, value, 0)) > 0;
}

static u32 LTMessagePack_PeekString(LTBuffer *buf, u32 *length, u32 offset) {
    u8           header[5];
    LTBufferSpan read = buf->API->Peek(buf, LTBuffer_MakeSpan(header, sizeof(header)), offset);
    u32          consumed = 0;
    if (header[0] >= 0xa0 && header[0] <= 0xbf) {
        consumed = 1;
        *length  = header[0] & 0x1f;
    } else {
        switch (header[0]) {
            case 0xd9:
                consumed = 2;
                *length  = header[1];
                break;
            case 0xda:
                consumed = 3;
                *length  = Buffer_Swap16(header + 1);
                break;
            case 0xdb:
                consumed = 5;
                *length  = Buffer_Swap32(header + 1);
                break;
        }
    }
    if (!consumed || read.size < consumed) {
        *length = consumed;
        return 0;
    }

    return consumed;
}

static bool LTMessagePack_ReadString(LTBuffer *buf, u32 *length) {
    return buf->API->Skip(buf, LTMessagePack_PeekString(buf, length, 0)) > 0;
}

static const char *LTMessagePack_CopyString(LTBuffer *buf) {
    u32 length;
    if (!LTMessagePack_ReadString(buf, &length)) {
        return NULL;
    }
    char *str   = lt_malloc(length + 1);
    str[length] = 0;
    return buf->API->Read(buf, LTBuffer_MakeSpan(str, length)).size == length ? str : NULL;
}

static void LTMessagePack_FreeString(LTBuffer *buf, const char *str) {
    LT_UNUSED(buf);
    lt_free((void *)str);
}

static u32 LTMessagePack_PeekArray(LTBuffer *buf, u32 *length, u32 offset) {
    u8           header[5];
    LTBufferSpan read     = buf->API->Peek(buf, LTBuffer_MakeSpan(header, sizeof(header)), offset);
    u32          consumed = 0;
    if ((header[0] & ~0xf) == 0x90) {
        consumed = 1;
        *length  = header[0] & 0xf;
    } else {
        switch (header[0]) {
            case 0xdc:
                consumed = 3;
                *length  = Buffer_Swap16(header + 1);
                break;
            case 0xdd:
                consumed = 5;
                *length  = Buffer_Swap32(header + 1);
                break;
        }
    }
    if (!consumed || read.size < consumed) {
        *length = consumed;
        return false;
    }

    return true;
}

static bool LTMessagePack_ReadArray(LTBuffer *buf, u32 *length) {
    return buf->API->Skip(buf, LTMessagePack_PeekArray(buf, length, 0)) > 0;
}

static u32 LTMessagePack_PeekMap(LTBuffer *buf, u32 *length, u32 offset) {
    u8           header[5];
    LTBufferSpan read = buf->API->Peek(buf, LTBuffer_MakeSpan(header, sizeof(header)), offset);
    u32          consumed = 0;
    if ((header[0] & ~0xf) == 0x80) {
        consumed = 1;
        *length  = header[0] & 0xf;
    } else {
        switch (header[0]) {
            case 0xde:
                consumed = 3;
                *length  = Buffer_Swap16(header + 1);
                break;
            case 0xdf:
                consumed = 5;
                *length  = Buffer_Swap32(header + 1);
                break;
        }
    }
    if (!consumed || read.size < consumed) {
        *length = consumed;
        return false;
    }

    return true;
}

static bool LTMessagePack_ReadMap(LTBuffer *buf, u32 *length) {
    return buf->API->Skip(buf, LTMessagePack_PeekMap(buf, length, 0)) > 0;
}

static u32 LTMessagePack_PeekExtended(LTBuffer *buf, u8 *type, u32 *length, u32 offset) {
    u8           header[6];
    LTBufferSpan read     = buf->API->Peek(buf, LTBuffer_MakeSpan(header, sizeof(header)), offset);
    u32          consumed = 0;
    switch (header[0]) {
        case 0xc7:
            consumed = 3;
            *length  = header[1];
            break;
        case 0xc8:
            consumed = 4;
            *length  = header[1] | header[2] << 8;
            *length  = LT_NTOHS(*length);
            break;
        case 0xc9:
            consumed = 6;
            *length  = header[1] | header[2] << 8 | header[3] << 16 | header[4] << 24;
            *length  = LT_NTOHL(*length);
            break;
        default:
            consumed = 2;
            switch (header[0]) {
                case 0xd4:
                    *length = 1;
                    break;
                case 0xd5:
                    *length = 2;
                    break;
                case 0xd6:
                    *length = 4;
                    break;
                case 0xd7:
                    *length = 8;
                    break;
                case 0xd8:
                    *length = 16;
                    break;
            }
            break;
    }
    if (!consumed || read.size < consumed) {
        *length = consumed;
        return 0;
    }
    *type = header[consumed - 1];

    return consumed;
}

static bool LTMessagePack_ReadExtended(LTBuffer *buf, u8 *type, u32 *length) {
    return buf->API->Skip(buf, LTMessagePack_PeekExtended(buf, type, length, 0)) > 0;
}

static u32 LTMessagePack_PeekBinary(LTBuffer *buf, u32 *length, u32 offset) {
    u8           header[6];
    LTBufferSpan read = buf->API->Peek(buf, LTBuffer_MakeSpan(header, sizeof(header)), offset);
    u32          consumed = 0;
    switch (header[0]) {
        case 0xc4:
            consumed = 2;
            *length  = header[1];
            break;
        case 0xc5:
            consumed = 3;
            *length  = Buffer_Swap16(header + 1);
            break;
        case 0xc6:
            consumed = 5;
            *length  = Buffer_Swap32(header + 1);
            break;
    }
    if (!consumed || read.size < consumed) {
        *length = consumed;
        return 0;
    }
    return consumed;
}

static bool LTMessagePack_ReadBinary(LTBuffer *buf, u32 *length) {
    return buf->API->Skip(buf, LTMessagePack_PeekBinary(buf, length, 0)) > 0;
}

static bool LTMessagePack_ReadTableHeader(LTBuffer *buf, u32 *rowHeaders, u32 *colHeaders, s32 *size) {
    u8   flags;
    bool success = LTMessagePack_ReadIntU8(buf, &flags);
    if (flags & 0b10) {
        success = success && LTMessagePack_ReadIntU32(buf, rowHeaders);
    } else {
        *rowHeaders = 0;
    }
    if (flags & 0b100) {
        success = success && LTMessagePack_ReadIntU32(buf, colHeaders);
    } else {
        *colHeaders = 0;
    }
    if (flags & 0b1) {
        *size = -1;
    } else {
        success = success && LTMessagePack_ReadIntS32(buf, size);
    }
    return success;
}

static bool LTMessagePack_ReadTable(LTBuffer *buf, u32 *rowHeaders, u32 *colHeaders, s32 *size) {
    u8   type;
    u32  length;
    bool success = LTMessagePack_ReadExtended(buf, &type, &length) && (type == 16)
                   && LTMessagePack_ReadTableHeader(buf, rowHeaders, colHeaders, size);
    return success;
}

static u32 LTMessagePack_PeekAt(LTBuffer *buf, LTMessagePack_Value *value, u32 offset) {
    u8  header[1];
    u32 consumed = 0;
    value->size  = 0;
    if (buf->API->Peek(buf, LTBuffer_MakeSpan(header, sizeof(header)), offset).size == 0) {
        value->type = LTMessagePack_Type_Error;
    } else if (((s8)header[0]) >= -32) {
        value->type    = LTMessagePack_Type_Integer;
        value->integer = (s8)header[0];
        consumed       = 1;
    } else {
        switch (header[0] >> 5) {
            case 0b101:
                value->type = LTMessagePack_Type_String;
                value->size = header[0] & 0b11111;
                consumed    = 1;
                break;
            case 0b100:
                if (header[0] & 0b00010000) {
                    value->type = LTMessagePack_Type_Array;
                } else {
                    value->type = LTMessagePack_Type_Map;
                }
                value->size = header[0] & 0b1111;
                consumed    = 1;
                break;
            default:
                switch (header[0]) {
                    case 0xc0:
                        value->type = LTMessagePack_Type_Nil;
                        consumed    = 1;
                        break;
                    case 0xc2:
                        value->type    = LTMessagePack_Type_Boolean;
                        value->boolean = false;
                        consumed       = 1;
                        break;
                    case 0xc3:
                        value->type    = LTMessagePack_Type_Boolean;
                        value->boolean = true;
                        consumed       = 1;
                        break;
                    case 0xc4:
                    case 0xc5:
                    case 0xc6:
                        consumed    = LTMessagePack_PeekBinary(buf, &value->size, offset);
                        value->type = LTMessagePack_Type_Binary;
                        break;
                    case 0xca:
                        consumed    = LTMessagePack_PeekFloat32(buf, &value->float32, offset);
                        value->type = LTMessagePack_Type_Float32;
                        break;
                    case 0xcb:
                        consumed    = LTMessagePack_PeekFloat32(buf, &value->float32, offset);
                        value->type = LTMessagePack_Type_Float32;
                        break;
                    case 0xcc:
                    case 0xcd:
                    case 0xce:
                    case 0xcf:
                    case 0xd0:
                    case 0xd1:
                    case 0xd2:
                    case 0xd3:
                        consumed    = LTMessagePack_PeekInteger(buf, &value->uinteger, offset);
                        value->type = LTMessagePack_Type_Integer;
                        break;
                    case 0xc7:
                    case 0xc8:
                    case 0xc9:
                    case 0xd4:
                    case 0xd5:
                    case 0xd6:
                    case 0xd7:
                    case 0xd8:
                        consumed    = LTMessagePack_PeekExtended(buf, (u8 *)&value->extType, &value->size, offset);
                        value->type = LTMessagePack_Type_Extended;
                        break;
                    case 0xd9:
                    case 0xda:
                    case 0xdb:
                        consumed    = LTMessagePack_PeekString(buf, &value->size, offset);
                        value->type = LTMessagePack_Type_String;
                        break;
                    case 0xdc:
                    case 0xdd:
                        consumed    = LTMessagePack_PeekArray(buf, &value->size, offset);
                        value->type = LTMessagePack_Type_Array;
                        break;
                    case 0xde:
                    case 0xdf:
                        consumed    = LTMessagePack_PeekMap(buf, &value->size, offset);
                        value->type = LTMessagePack_Type_Map;
                        break;
                }
                break;
        }
    }
    if (!consumed) {
        value->type = LTMessagePack_Type_Error;
    }
    return consumed;
}

static bool LTMessagePack_ReadNext(LTBuffer *buf, LTMessagePack_Value *value) {
    return buf->API->Skip(buf, LTMessagePack_PeekAt(buf, value, 0)) > 0;
}

/*******************************************************************************
 * Library
 *******************************************************************************/

static bool LTUtilityMessagePackImpl_LibInit(void) {
    return true;
}

static void LTUtilityMessagePackImpl_LibFini(void) {}

// clang-format off
define_LTLIBRARY_ROOT_INTERFACE(LTUtilityMessagePack)
    .Init          = LTMessagePack_Init,
    .Free          = LTMessagePack_Free,

    .GetPosition   = LTMessagePack_GetPosition,
    .SetPosition   = LTMessagePack_SetPosition,
    .Skip          = LTMessagePack_Skip,
    .SkipWithin    = LTMessagePack_SkipWithin,
    .SkipContainer = LTMessagePack_SkipContainer,
    .FindInteger   = LTMessagePack_FindInteger,
    .FindCString   = LTMessagePack_FindCString,

    .PutNil        = LTMessagePack_PutNil,
    .PutBoolean    = LTMessagePack_PutBoolean,
    .PutIntS32     = LTMessagePack_PutIntS32,
    .PutIntS64     = LTMessagePack_PutIntS64,
    .PutIntU32     = LTMessagePack_PutIntU32,
    .PutIntU64     = LTMessagePack_PutIntU64,
    .PutFloat32    = LTMessagePack_PutFloat32,
    .PutFloat64    = LTMessagePack_PutFloat64,
    .PutString     = LTMessagePack_PutString,
    .PutCString    = LTMessagePack_PutCString,
    .PutBinary     = LTMessagePack_PutBinary,
    .PutArray      = LTMessagePack_PutArray,
    .PutMap        = LTMessagePack_PutMap,
    .PutExtended   = LTMessagePack_PutExtended,
    .PutMessagePack= LTMessagePack_PutMessagePack,

    .GetValue      = LTMessagePack_GetValue,
    .GetInteger    = LTMessagePack_GetInteger,
    .GetString     = LTMessagePack_GetString,
    .GetArray      = LTMessagePack_GetArray,
    .GetExtended   = LTMessagePack_GetExtended,

    .WriteNil      = LTMessagePack_WriteNil,
    .WriteBoolean  = LTMessagePack_WriteBoolean,
    .WriteIntU32   = LTMessagePack_WriteIntU32,
    .WriteIntU64   = LTMessagePack_WriteIntU64,
    .WriteIntS32   = LTMessagePack_WriteIntS32,
    .WriteIntS64   = LTMessagePack_WriteIntS64,
    .WriteFloat32  = LTMessagePack_WriteFloat32,
    .WriteFloat64  = LTMessagePack_WriteFloat64,
    .WriteString   = LTMessagePack_WriteString,
    .WriteCString  = LTMessagePack_WriteCString,
    .PrintString   = LTMessagePack_PrintString,
    .VPrintString  = LTMessagePack_VPrintString,
    .WriteBinary   = LTMessagePack_WriteBinary,
    .WriteArray    = LTMessagePack_WriteArray,
    .WriteMap      = LTMessagePack_WriteMap,
    .WriteExtended = LTMessagePack_WriteExtended,
    .WriteTable    = LTMessagePack_WriteTable,

    .ReadNil       = LTMessagePack_ReadNil,
    .ReadBoolean   = LTMessagePack_ReadBoolean,
    .ReadIntU8     = LTMessagePack_ReadIntU8,
    .ReadIntU16    = LTMessagePack_ReadIntU16,
    .ReadIntU32    = LTMessagePack_ReadIntU32,
    .ReadIntU64    = LTMessagePack_ReadIntU64,
    .ReadIntS8     = LTMessagePack_ReadIntS8,
    .ReadIntS16    = LTMessagePack_ReadIntS16,
    .ReadIntS32    = LTMessagePack_ReadIntS32,
    .ReadIntS64    = LTMessagePack_ReadIntS64,
    .ReadFloat32   = LTMessagePack_ReadFloat32,
    .ReadFloat64   = LTMessagePack_ReadFloat64,
    .ReadString    = LTMessagePack_ReadString,
    .ReadArray     = LTMessagePack_ReadArray,
    .ReadMap       = LTMessagePack_ReadMap,
    .ReadExtended  = LTMessagePack_ReadExtended,
    .ReadBinary    = LTMessagePack_ReadBinary,
    .ReadTable     = LTMessagePack_ReadTable,
    .ReadTableHeader = LTMessagePack_ReadTableHeader,
    .ReadNext      = LTMessagePack_ReadNext,
    .PeekAt        = LTMessagePack_PeekAt,

    .CopyString    = LTMessagePack_CopyString,
    .FreeString    = LTMessagePack_FreeString,
LTLIBRARY_DEFINITION;
// clang-format on
