/******************************************************************************
 * lt/source/lt/core/LTResourceTreeImpl.h
 *
 * Tree format created by tiberius
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include <lt/LTTypes.h>

/**************************
 * ResourceTree Constants *
 **************************/
enum {
    kResourceTree_HeaderStampARB0 = 0x30425241,  /* 'A' 'R' 'B' '0' : ARB for Arbolated, version 0 */
    kResourceTree_NoSuchEntry     = 0xFFFF  /* byte offset field for next sibling and first child set to this to indicate no next sibling or no next child */
};

/***********************
 * ResourceTree Header *
 ***********************/
typedef struct ResourceTreeHeader {
    u32 nResourceTreeIDStamp;
    u32 nTotalTreeSize;
} ResourceTreeHeader;

/*************************************
 * ResourceTree Internal Entry Types *
 ************************************/
typedef u8 ResourceTreeEntryType;
enum ResourceTreeEntryType {
    kResourceTreeEntryType_Array        = 'a',   /* Array (container) nodes     */
    kResourceTreeEntryType_Object       = 'o',   /* Object (container) nodes    */
    kResourceTreeEntryType_Binary       = 'b',   /* Binary blobs                */
    kResourceTreeEntryType_String       = 's',   /* Strings                     */
    /* Integer types */
    kResourceTreeEntryType_Integer8     = 0x01,  /* Compressed 8-bit integer    */
    kResourceTreeEntryType_Integer16    = 0x02,  /* Compressed 16-bit integer   */
    kResourceTreeEntryType_Integer32    = 0x04,  /* Compressed 32-bit integer   */
    kResourceTreeEntryType_Integer64    = 0x08,  /* Signed 64-bit integer       */
    /* INteger size and negation masks */
    kResourceTreeEntryType_IntegerMask  = 0x0F,  /* Mask for integer size       */
    kResourceTreeEntryType_NegationMask = 0x10   /* Mask for integer negation   */
};

/**********************
 * ResourceTree Entry *
 **********************/
typedef struct ResourceTreeEntry {
    /* FIXED PART */
    u16                     nNextEntryOffset;   /* offset from this ResourceTreeEntry to the next sibling;
                                                   value is kResourceTree_NoSuchEntry if no next sibling            */
    u8                      type;               /* Type of entry                                                    */
    u8                      nNameSize;          /* Size of name field in bytes, including null terminator           */

    /* VARIABLE PART
    u16                     nFirstChildOffset;  / * ABSENT UNLESS nType == kResourceTreeEntryType_Object or kResourceTreeEntryType_Array * /
    u8                      name[nNameSize];    / * ALWAYS PRESENT, entry key name, includes null terminator        * /

    u8                      nValue8;            / * ONLY PRESENT WHEN  IsInt8() == true,   8-bit int storage        * /
    u16                     nValue16;           / * ONLY PRESENT WHEN IsInt16() == true,  16-bit integer storage    * /
    u32                     nValue32;           / * ONLY PRESENT WHEN IsInt32() == true,  32-bit integer storage    * /
    u64                     nValue64;           / * ONLY PRESENT WHEN IsInt64() == true,  64-bit integer storage    * /

    u16                     nValueSize;         / * ONLY PRESENT WHEN nType == kResourceTreeEntryType_Binary        * /
                                                / *                or nType == kResourceTreeEntryType_String        * /
                                                / *                            size includes null-term if string    * /
    u8                      value[nValueSize];  / * ONLY PRESENT WHEN nType == kResourceTreeEntryType_Binary        * /
                                                / *                or nType == kResourceTreeEntryType_String        * /
                                                / *                            size includes null-term if string    * /*/
} ResourceTreeEntry;

/***************************
 * Inline helper functions *
 **************************/
LT_INLINE bool IsTypeInIntegerTypeRange(u8 nType)                             { return (nType <= (kResourceTreeEntryType_Integer64 + kResourceTreeEntryType_NegationMask)); }
LT_INLINE bool IsInt8(u8 nType)                                               { return (IsTypeInIntegerTypeRange(nType) && (1 == (nType & kResourceTreeEntryType_IntegerMask))); }
LT_INLINE bool IsInt16(u8 nType)                                              { return (IsTypeInIntegerTypeRange(nType) && (2 == (nType & kResourceTreeEntryType_IntegerMask))); }
LT_INLINE bool IsInt32(u8 nType)                                              { return (IsTypeInIntegerTypeRange(nType) && (4 == (nType & kResourceTreeEntryType_IntegerMask))); }
LT_INLINE bool IsInt64(u8 nType)                                              { return (IsTypeInIntegerTypeRange(nType) && (8 == (nType & kResourceTreeEntryType_IntegerMask))); }
LT_INLINE void ReadBytes(const u8 *tree, u32 offset, u32 numBytes, u8 *value) { tree += offset;  while (numBytes--) *value++ = *tree++; }
LT_INLINE  u64 Read64(const LTResourceTree *tree, u32 offset)                 { u64 retVal; ReadBytes((u8*)tree, offset, 8, (u8 *)&retVal); return retVal; }
LT_INLINE  u32 Read32(const LTResourceTree *tree, u32 offset)                 { u32 retVal; ReadBytes((u8*)tree, offset, 4, (u8 *)&retVal); return retVal; }
LT_INLINE  u16 Read16(const LTResourceTree *tree, u32 offset)                 { u16 retVal; ReadBytes((u8*)tree, offset, 2, (u8 *)&retVal); return retVal; }
LT_INLINE   u8  Read8(const LTResourceTree *tree, u32 offset)                 { return *(((u8 *)tree) + offset); }

/*************************
 * Function declarations *
 *************************/
bool        LTResourceTreeImpl_ReadResourceValue(const LTResourceTree *tree, u32 offset, const char *key, LTResourceValue *valueToSet);
u32         LTResourceTreeImpl_FindResourceOffset(const LTResourceTree *tree, u32 offset, const char *key);
u32         LTResourceTreeImpl_CountResourceChildren(const LTResourceTree *tree, u32 offset, const char *key);
const char* LTResourceTreeImpl_ResourceTypeToString(const LTResourceValueType type);
u32         LTResourceTreeImpl_ResourceValueToString(const LTResourceValue *resourceValue, char *buffer, u32 bufferSize);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Sep-22   aurelian    created
 *  26-Jan-24   nerva       added CountResourceChildren, ResourceTypeToString, ResourceValueToString
 */
