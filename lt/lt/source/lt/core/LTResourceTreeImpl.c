/******************************************************************************
 * lt/source/lt/core/LTResourceTreeImpl.c
 *
 * Tree format created by tiberius, originally for an "LTRegistry"
 * All mentions of "registry" have since been translated to "resource tree"
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include "LTResourceTreeImpl.h"
#include "LTCoreImpl.h"

DEFINE_LTLOG_SECTION("ltcore.rscrc.tree");

#define USE_DLOG    0
#if USE_DLOG
  #define DLOG LTLOG
#else
  #define DLOG LTLOG_LOGNULL
#endif

/*************
 * Constants *
 *************/
enum {
    kPathSeparator   = '/',    /**< Path element separator character */
};

/* Match path name component with entry name */
static bool
LTResourceTreeImpl_MatchEntryName(const u8 * pResourceTree, u32 offset, u32 nameSize, const u8 ** ppPath) {
    if (nameSize < 2) return 0; /* size 1 == len 0, bail */
    nameSize--; /* turn size into length, easier to grok */
    pResourceTree += offset; /* advance pResourceTree to start of name so we can reuse offset to save stack space */

    DLOG("MatchEntryName", "comparing key \"%s\" against entry \"%s\" (len %d)\n", (const char *)(*ppPath), (const char *)pResourceTree, (int)nameSize);
    /* Read and compare name prefix */
    for (offset = 0; offset < nameSize; offset++) {
        if ((*ppPath)[offset] != pResourceTree[offset]) return false;
    }
    /* Finalize match */
    if ((*ppPath)[offset] == '\0') {
        *ppPath += offset;         /* Matched leaf, move position to \0 */
        return true;
    }
    if ((*ppPath)[offset] == kPathSeparator) {
        *ppPath += (offset + 1);   /* Matched internal node, move position to next path component */
        return true;
    }
    return false; /* name was a subset of current path component, no match */
}

u32 LTResourceTreeImpl_FindResourceOffset(const LTResourceTree *resourceTree, u32 offset, const char *resourceKey) {
    ResourceTreeEntry *entry;
    DLOG("FindResourceOffset", "tree(0x%x), offset(%d), key(\"%s\")", (int)((LT_SIZE)((void *)resourceTree)), (int)offset, resourceKey);
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (NULL == resourceTree || NULL == resourceKey) return 0;
    if (*resourceKey == kPathSeparator) resourceKey++;
    if (0 == *resourceKey) return 0;
    if (kResourceTree_HeaderStampARB0 != Read32(resourceTree, 0)) return 0;               /* make sure resourceTree is an ARBOLATED_/\/\_TREE!*/
    if (offset + 7 >= Read32(resourceTree, 4)) return 0;                                  /* make sure we not trying to read past the end (7 bytes is the smallest entry) */
    if (offset == 0) offset = sizeof(ResourceTreeHeader);
    else {
        // specified an offset to start search from, if it's an object, advance offset to first child
        entry = (ResourceTreeEntry *)(((const u8*)resourceTree) + offset);
        if ((entry->type == kResourceTreeEntryType_Object) || (entry->type == kResourceTreeEntryType_Array)) {
            if (Read16(resourceTree, offset + sizeof(ResourceTreeEntry)) != kResourceTree_NoSuchEntry) {
                offset = offset + sizeof(ResourceTreeEntry) + Read16(resourceTree, offset + sizeof(ResourceTreeEntry));
            }
            else return 0; /* has no children */
        }
    }
    while (1) {
        entry = (ResourceTreeEntry *)(((const u8*)resourceTree) + offset);
        if (LTResourceTreeImpl_MatchEntryName(resourceTree,
                                              offset + sizeof(ResourceTreeEntry) + ((entry->type == kResourceTreeEntryType_Object) || (entry->type == kResourceTreeEntryType_Array) ? 2 : 0),
                                              entry->nNameSize,
                                              (const u8 **)&resourceKey)) {
            DLOG("MatchEntryName", "returns true");
            if (*resourceKey == 0) return offset; // found it                                                                                                                                                                                                                                                                                                                                                                                                                                                                        ; // found it
            if ((entry->type == kResourceTreeEntryType_Object) || (entry->type == kResourceTreeEntryType_Array)) {
                // advance to first child
                if (Read16(resourceTree, offset + sizeof(ResourceTreeEntry)) != kResourceTree_NoSuchEntry) {
                    offset = offset + sizeof(ResourceTreeEntry) + Read16(resourceTree, offset + sizeof(ResourceTreeEntry));
                    continue;
                }
                return 0; /* internal node with no child (0xfffff in child field), not found*/
            }
            return 0; /* at the end of the trail with more to look for, not found*/
        }
        else {
            DLOG("MatchEntryName", "returns false");
            if (Read16(entry, 0) != kResourceTree_NoSuchEntry) {
                offset += Read16(entry, 0); /* advance to next entry */
                continue;
            }
            return 0; /* no more entries */
        }
    }
}

bool LTResourceTreeImpl_ReadResourceValue(const LTResourceTree *resourceTree, u32 offset, const char *resourceKey, LTResourceValue *resourceValue) {
    DLOG("ReadResourceValue", "tree(0x%x), offset(%d), key(\"%s\")", (int)((LT_SIZE)((void *)resourceTree)), (int)offset, resourceKey);
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (!resourceTree) return false;                        /* check parameters */
    if (kResourceTree_HeaderStampARB0 != Read32(resourceTree, 0)) return false;               /* make sure resourceTree is an ARBOLATED_/\/\_TREE!*/

    ResourceTreeEntry *entry = (ResourceTreeEntry *)(((u8 *)resourceTree) + offset);
    if (resourceKey == kLTResourceKey_NextSibling) {
        if (Read16(&entry->nNextEntryOffset, 0) == kResourceTree_NoSuchEntry) return false;
        offset += Read16(&entry->nNextEntryOffset, 0);
    } else if (resourceKey == kLTResourceKey_FirstChild) {
        if ((entry->type != kResourceTreeEntryType_Object) && (entry->type != kResourceTreeEntryType_Array)) return false;
        u32 firstChildOffset = Read16(entry, sizeof(ResourceTreeEntry));
        if (firstChildOffset == kResourceTree_NoSuchEntry) return false;
        offset += sizeof(ResourceTreeEntry) + firstChildOffset;
    } else if (resourceKey != NULL) {
        offset = LTResourceTreeImpl_FindResourceOffset(resourceTree, offset, resourceKey);
        DLOG("FindResourceOffset", "returns %d\n", (int)offset);
        if (0 == offset) return false; /* didn't find the key */
    } /* else if (resourceKey == NULL) don't need to adjust the offset. Just return the value at the given offset.  */
    entry = (ResourceTreeEntry *)(((u8 *)resourceTree) + offset);

    switch (entry->type) {
        case kResourceTreeEntryType_String:
            if (resourceValue) {
                resourceValue->type = kLTResourceValueType_String;
                resourceValue->size = Read16(entry, sizeof(ResourceTreeEntry) + entry->nNameSize);
                resourceValue->string = (const char *)entry + sizeof(ResourceTreeEntry) + entry->nNameSize + 2;
                resourceValue->name   = (const char *)entry + sizeof(ResourceTreeEntry);
                resourceValue->offset = offset;
            }
            break;
        case kResourceTreeEntryType_Binary:
            if (resourceValue) {
                resourceValue->type = kLTResourceValueType_Binary;
                resourceValue->size = Read16(entry, sizeof(ResourceTreeEntry) + entry->nNameSize);
                resourceValue->binary = (const u8 *)entry + sizeof(ResourceTreeEntry) + entry->nNameSize + 2;
                resourceValue->name   = (const char *)entry + sizeof(ResourceTreeEntry);
                resourceValue->offset = offset;
            }
            break;
        case kResourceTreeEntryType_Array:
        case kResourceTreeEntryType_Object:
            if (resourceValue) {
                resourceValue->type = (entry->type == kResourceTreeEntryType_Array) ? kLTResourceValueType_Array : kLTResourceValueType_Object;
                resourceValue->size = 0;
                resourceValue->integer = 0;
                resourceValue->name   = (const char *)entry + sizeof(ResourceTreeEntry) + 2;
                resourceValue->offset = offset;
            }
            break;
        default:
            if (entry->type > (kResourceTreeEntryType_Integer64 + kResourceTreeEntryType_NegationMask)) return false;
            if (resourceValue) {
                resourceValue->type = kLTResourceValueType_Integer;
                resourceValue->size = entry->type & kResourceTreeEntryType_IntegerMask;
                resourceValue->name = (const char *)entry + sizeof(ResourceTreeEntry);
                resourceValue->offset = offset;
                switch (resourceValue->size) {
                    case 1: resourceValue->integer = (s64) Read8(entry, sizeof(ResourceTreeEntry) + entry->nNameSize); break;
                    case 2: resourceValue->integer = (s64)Read16(entry, sizeof(ResourceTreeEntry) + entry->nNameSize); break;
                    case 4: resourceValue->integer = (s64)Read32(entry, sizeof(ResourceTreeEntry) + entry->nNameSize); break;
                    case 8: resourceValue->integer = (s64)Read64(entry, sizeof(ResourceTreeEntry) + entry->nNameSize); break;
                   default: return false;
                }
                if (entry->type & kResourceTreeEntryType_NegationMask) resourceValue->integer = 0 - resourceValue->integer;
            }
            else switch (entry->type & kResourceTreeEntryType_IntegerMask) { case 1: case 2: case 4: case 8: break; default: return false; }
            break;
    }
    return true;
}

u32 LTResourceTreeImpl_CountResourceChildren(const LTResourceTree *tree, u32 offset, const char *key)
{
    DLOG("CountResourceChildren", "tree(0x%x), offset(%ld), key(\"%s\")", (int)((LT_SIZE)((void *)tree)), LT_Pu32(offset), key);
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();

    LTResourceValue value;
    if (!LTResourceTreeImpl_ReadResourceValue(tree, offset, key, &value)) return 0;
    u32 numChildren = 0;
    for (const char *childref = kLTResourceKey_FirstChild; LTResourceTreeImpl_ReadResourceValue(tree, value.offset, childref, &value); ++numChildren, childref = kLTResourceKey_NextSibling);
    return numChildren;
}
const char* LTResourceTreeImpl_ResourceTypeToString(const LTResourceValueType type) {
    switch (type) {
        case kLTResourceValueType_Integer: return "Integer";
        case kLTResourceValueType_String:  return "String";
        case kLTResourceValueType_Binary:  return "Binary";
        case kLTResourceValueType_Array:   return "Array";
        case kLTResourceValueType_Object:  return "Object";
        default: return "Unknown";
    }
}

static const char s_hexChars[] = "0123456789ABCDEF";

u32 LTResourceTreeImpl_ResourceValueToString(const LTResourceValue *resourceValue, char *buffer, u32 bufferSize) {
        u32 numCharsCopied = 0;
        if (resourceValue && buffer && bufferSize) {
            switch(resourceValue->type) {
                case kLTResourceValueType_Integer:
                    numCharsCopied = lt_snprintf(buffer, bufferSize, "%lld", LT_Ps64(resourceValue->integer));
                    if (numCharsCopied >= bufferSize) numCharsCopied = bufferSize - 1; /* truncated output, buffer filled up with null term */
                    break;
                case kLTResourceValueType_String:
                    numCharsCopied = lt_snprintf(buffer, bufferSize, "%s", resourceValue->string);
                    if (numCharsCopied >= bufferSize) numCharsCopied = bufferSize - 1; /* truncated output, buffer filled up with null term */
                    break;
                case kLTResourceValueType_Binary:
                    if (resourceValue->binary && resourceValue->size) {
                        const u8 *pBytes = resourceValue->binary;
                        u32 numBytes = resourceValue->size;
                        while (numBytes && bufferSize > 2) {
                            *buffer++ = s_hexChars[(*pBytes) >> 4];
                            *buffer++ = s_hexChars[(*pBytes++) & 0xF];
                            numBytes--;
                            bufferSize -= 2;
                            numCharsCopied += 2;
                        }
                    }
                    *buffer = 0;
                    break;
                default:
                    *buffer = 0;
                    break;
            }
        }
        return numCharsCopied;
}


/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Sep-22   aurelian    created
 *  28-Jan-23   augustus    analyze tree in place
 *  06-Dec-23   augustus    use Read16 to read entry->nNextEntryOffset
 *  26-Jan-24   nerva       added CountResourceChildren
 */
