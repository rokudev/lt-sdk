/*******************************************************************************
 *
 * LTUtilityMessagePack_Helpers.h - LT MessagePack Helper Functions
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

/**
 * @ingroup ltutility_messagepack
 * @{
 */

#ifndef ROKU_LT_INCLUDE_LT_MESSAGEPACK_LTUTILITYMESSAGEPACK_HELPERS_H
#define ROKU_LT_INCLUDE_LT_MESSAGEPACK_LTUTILITYMESSAGEPACK_HELPERS_H

#include <lt/utility/messagepack/LTUtilityMessagePack.h>

/******************************************************************************
 * @name Generic Put Helpers
 * @{
 */

// clang-format off
#define LTMessagePackPut_Method(mp, value, ...) _Generic((value),\
    bool: mp->PutBoolean,\
    u8: mp->PutIntU32,\
    u16: mp->PutIntU32,\
    u32: mp->PutIntU32,\
    u64: mp->PutIntU64,\
    s8: mp->PutIntS32,\
    s16: mp->PutIntS32,\
    s32: mp->PutIntS32,\
    s64: mp->PutIntS64,\
    float: mp->PutFloat32,\
    double: mp->PutFloat64,\
    char*: mp->PutCString,\
    const char*: mp->PutCString,\
    void*: mp->PutBinary,\
    const void*: mp->PutBinary,\
    u8*: mp->PutBinary,\
    const u8*: mp->PutBinary\
)
// clang-format on

#define LTMessagePackPut(mp, obj, ...) LTMessagePackPut_Method(mp, __VA_ARGS__)(obj, __VA_ARGS__)

/** @} */

/******************************************************************************
 * @name Generic Get Helpers
 * @{
 */

LT_INLINE bool LTMessagePackGetBoolean(LTUtilityMessagePack *mp,
                                       LTMessagePack_Obj *obj,
                                       bool *value)
{
    LTMessagePack_Value v;
    if (mp->GetValue(obj, &v) != LTMessagePack_Type_Boolean) {
        return false;
    }
    *value = v.boolean;
    return true;
}

LT_INLINE bool LTMessagePackGetUInt8(LTUtilityMessagePack *mp,
                                     LTMessagePack_Obj *obj,
                                     u8 *value)
{
    u64 tmp;
    s8 size = mp->GetInteger(obj, &tmp);
    if (size <= 0) {
        return false;
    }
    if (size > 2) {
        return false;
    }
    *value = tmp;
    return true;
}

LT_INLINE bool LTMessagePackGetInt8(LTUtilityMessagePack *mp,
                                    LTMessagePack_Obj *obj,
                                    s8 *value)
{
    u64 tmp;
    s8 size = mp->GetInteger(obj, &tmp);
    if (size == 0) {
        return false;
    }
    if (size > 2) {
        return false;
    }
    if (size > 0 && tmp > LT_S8_MAX) {
        return false;
    }
    if (size < -2) {
        return false;
    }
    *value = tmp;
    return true;
}

LT_INLINE bool LTMessagePackGetUInt16(LTUtilityMessagePack *mp,
                                      LTMessagePack_Obj *obj,
                                      u16 *value)
{
    u64 tmp;
    s8 size = mp->GetInteger(obj, &tmp);
    if (size <= 0) {
        return false;
    }
    if (size > 3) {
        return false;
    }
    *value = tmp;
    return true;
}

LT_INLINE bool LTMessagePackGetInt16(LTUtilityMessagePack *mp,
                                     LTMessagePack_Obj *obj,
                                     s16 *value)
{
    u64 tmp;
    s8 size = mp->GetInteger(obj, &tmp);
    if (size == 0) {
        return false;
    }
    if (size > 3) {
        return false;
    }
    if (size > 0 && tmp > LT_S16_MAX) {
        return false;
    }
    if (size < -3) {
        return false;
    }
    *value = tmp;
    return true;
}

LT_INLINE bool LTMessagePackGetUInt32(LTUtilityMessagePack *mp,
                                      LTMessagePack_Obj *obj,
                                      u32 *value)
{
    u64 tmp;
    s8 size = mp->GetInteger(obj, &tmp);
    if (size <= 0) {
        return false;
    }
    if (size > 5) {
        return false;
    }
    *value = tmp;
    return true;
}

LT_INLINE bool LTMessagePackGetInt32(LTUtilityMessagePack *mp,
                                     LTMessagePack_Obj *obj,
                                     s32 *value)
{
    u64 tmp;
    s8 size = mp->GetInteger(obj, &tmp);
    if (size == 0) {
        return false;
    }
    if (size > 5) {
        return false;
    }
    if (size > 0 && tmp > LT_S32_MAX) {
        return false;
    }
    if (size < -5) {
        return false;
    }
    *value = tmp;
    return true;
}

LT_INLINE bool LTMessagePackGetUInt64(LTUtilityMessagePack *mp,
                                      LTMessagePack_Obj *obj,
                                      u64 *value)
{
    return mp->GetInteger(obj, value) > 0;
}

LT_INLINE bool LTMessagePackGetInt64(LTUtilityMessagePack *mp,
                                     LTMessagePack_Obj *obj,
                                     s64 *value)
{
    u64 tmp;
    s8 size = mp->GetInteger(obj, &tmp);
    if (size == 0) {
        return false;
    }
    if (size > 0 && tmp > LT_S64_MAX) {
        return false;
    }
    *(u64 *)value = tmp;
    return true;
}

LT_INLINE bool LTMessagePackGetFloat(LTUtilityMessagePack *mp,
                                     LTMessagePack_Obj *obj,
                                     float *value)
{
    LTMessagePack_Value v;
    switch (mp->GetValue(obj, &v)) {
        case LTMessagePack_Type_Float32:
            *value = v.float32;
            break;
        case LTMessagePack_Type_Float64:
            *value = v.float64;
            break;
        default:
            return false;
    }
    return true;
}

LT_INLINE bool LTMessagePackGetDouble(LTUtilityMessagePack *mp,
                                      LTMessagePack_Obj *obj,
                                      double *value)
{
    LTMessagePack_Value v;
    switch (mp->GetValue(obj, &v)) {
        case LTMessagePack_Type_Float32:
            *value = v.float32;
            break;
        case LTMessagePack_Type_Float64:
            *value = v.float64;
            break;
        default:
            return false;
    }
    return true;
}

LT_INLINE bool LTMessagePackGetCString(LTUtilityMessagePack *mp,
                                       LTMessagePack_Obj *obj,
                                       const char **value)
{
    u32 length;
    if (!mp->GetString(obj, (u8 **)value, &length)) {
        return false;
    }
    return (*value)[length - 1] == '\0';
}

LT_INLINE bool LTMessagePackGetBinary(LTUtilityMessagePack *mp,
                                      LTMessagePack_Obj *obj,
                                      const void **value,
                                      LT_SIZE *size)
{
    LTMessagePack_Value v;
    if (mp->GetValue(obj, &v) != LTMessagePack_Type_Binary) {
        return false;
    }
    *value = v.data;
    *size  = v.size;
    return true;
}

// clang-format off
#define LTMessagePackGet_Method(mp, value) _Generic((value),\
    bool*: LTMessagePackGetBoolean,\
    u8*: LTMessagePackGetUInt8,\
    u16*: LTMessagePackGetUInt16,\
    u32*: LTMessagePackGetUInt32,\
    u64*: LTMessagePackGetUInt64,\
    s8*: LTMessagePackGetInt8,\
    s16*: LTMessagePackGetInt16,\
    s32*: LTMessagePackGetInt32,\
    s64*: LTMessagePackGetInt64,\
    float*: LTMessagePackGetFloat,\
    double*: LTMessagePackGetDouble,\
    const char**: LTMessagePackGetCString,\
    const void**: LTMessagePackGetBinary\
)
#define LTMessagePackGet_Value(value) _Generic((value),\
    u8*: (u8*)value,\
    u16*: (u16*)value,\
    u32*: (u32*)value,\
    u64*: (u64*)value,\
    s8*: (u8*)value,\
    s16*: (u16*)value,\
    s32*: (u32*)value,\
    s64*: (u64*)value,\
    const char**: (u8**)value,\
    default: value\
)
#define LTMessagePackGet_Remaining(value, ...) __VA_ARGS__
#define LTMessagePackGet(mp, obj, value) \
    LTMessagePackGet_Method(mp, value)(mp, obj, value)

#define LTMessagePack_Read(mp, buffer, value) _Generic((value),\
    u8*: mp->ReadIntU8,\
    u16*: mp->ReadIntU16,\
    u32*: mp->ReadIntU32,\
    u64*: mp->ReadIntU64,\
    s8*: mp->ReadIntS8,\
    s16*: mp->ReadIntS16,\
    s32*: mp->ReadIntS32,\
    s64*: mp->ReadIntS64\
)(buffer, value)
// clang-format on

#define LTMessagePack_StartReadString(mp, buffer, access) \
    (((mp)->ReadString((buffer), &(access)->span.size)) && (*access = ((buffer)->API->StartRead((buffer), (access)->span.size))).span.size)

#endif /* #ifndef ROKU_LT_INCLUDE_LT_MESSAGEPACK_LTUTILITYMESSAGEPACK_HELPERS_H */

/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  16-Mar-23   jovian      created
 */

