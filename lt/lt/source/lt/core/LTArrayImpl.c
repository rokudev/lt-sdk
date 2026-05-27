/******************************************************************************
 * lt/source/lt/core/LTArrayImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTArray.h>

/** Array Type */
enum {
    kArrayElementType_Pointer,
    kArrayElementType_Struct
};

/*
 * LTArrayImpl
 */

/*_______________________
  LTArray private data */
typedef_LTObjectImpl(LTArray, LTArrayImpl) {
    u8     *data;
    u32     count;
    u32     capacity;
    u16     blockSize;
    u16     initialNumElements;
    u16     maxIncrementalElements;
    u8      type;
    u8      isSorted;
} LTOBJECT_API;

/*____________________________
  LTArray utility functions */

static bool
AdjustCapacityAsRequired(LTArrayImpl *array) {
    if (array->count == array->capacity) {
        if (array->capacity < array->maxIncrementalElements) {
            if (array->capacity == 0) {
                array->capacity = array->initialNumElements;
            } else {
                array->capacity *= 2;
            }
        } else {
            array->capacity += array->maxIncrementalElements;
        }
        u8 *newData = lt_realloc(array->data, array->capacity * array->blockSize);
        if (newData) {
            array->data = newData;
        } else {
            array->capacity = array->count;
            return false;
        }
    }
    return true;
}

static void
SetValueAtIndex(LTArrayImpl *array, u32 index, const void *value) {
    u8 *valueToSet = array->data + index * array->blockSize;
    if (array->type == kArrayElementType_Pointer) {
        *((const void **)valueToSet) = value;
    } else {
        if (value) {
            lt_memcpy(valueToSet, value, array->blockSize);
        } else {
            lt_memset(valueToSet, 0, array->blockSize);
        }
    }
}

/*________________________
  LTArray API functions */

static bool LTArrayImpl_SetCount(LTArrayImpl *array, u32 newElementCount);

static void
LTArrayImpl_InitAsStructArray(LTArrayImpl *array, u16 elementSize) {
    if (!array) return;
    /* Changing an existing array's element size requires its capacity to be reset */
    if (array->data) LTArrayImpl_SetCount(array, 0);
    array->blockSize = elementSize;
    array->type      = kArrayElementType_Struct;
}

static void
LTArrayImpl_TuneAllocation(LTArrayImpl *array, u16 initialNumElements, u16 maxIncrementalElements) {
    if (!array) return;
    if (initialNumElements == 0) initialNumElements = kLTArray_InitialNumElements;
    if (maxIncrementalElements == 0) maxIncrementalElements = kLTArray_MaxIncrementalElements;
    if (maxIncrementalElements > kLTArray_MaxIncrementalElementsUpperBound)
        maxIncrementalElements = kLTArray_MaxIncrementalElementsUpperBound;
    array->initialNumElements     = initialNumElements;
    array->maxIncrementalElements = maxIncrementalElements;
}

static u16
LTArrayImpl_GetElementSize(LTArrayImpl *array) {
    if (!array) return 0;
    return array->blockSize;
}

static u32
LTArrayImpl_GetCount(LTArrayImpl *array) {
    if (!array) return 0;
    return array->count;
}

static bool
LTArrayImpl_SetCount(LTArrayImpl *array, u32 newElementCount) {
    if (!array) return false;
    u8 *newData = lt_realloc(array->data, newElementCount * array->blockSize);
    if (!newData && newElementCount) return false;
    array->data = newData;
    if (newElementCount > array->count) {
        /* zero new elements */
        lt_memset(array->data + array->count * array->blockSize, 0, (newElementCount - array->count) * array->blockSize);
        array->isSorted = 0;
    }
    array->count    = newElementCount;
    array->capacity = newElementCount;
    if (newElementCount <= 1) array->isSorted = 1;
    return true;
}

static void
LTArrayImpl_Trim(LTArrayImpl *array) {
    if (!array) return;
    array->data     = lt_realloc(array->data, array->count * array->blockSize);
    array->capacity = array->count;
}

static void *
LTArrayImpl_GetStorage(LTArrayImpl *array) {
    if (!array || array->count == 0) return NULL;
    return array->data;
}

static void *
LTArrayImpl_Get(LTArrayImpl *array, u32 index, void *valueToSet) {
    if (!array || index >= array->count) return NULL;
    u8 *value = array->data + index * array->blockSize;
    if (array->type == kArrayElementType_Pointer) {
        value = (void *)*((LT_SIZE *)value);
        if (valueToSet) *((void **)valueToSet) = value;
    } else if (valueToSet) {
        lt_memcpy(valueToSet, value, array->blockSize);
    }
    return value;
}

static void
LTArrayImpl_Set(LTArrayImpl *array, u32 index, const void *value) {
    if (!array || index >= array->count) return;
    SetValueAtIndex(array, index, value);
    if (array->count > 1) array->isSorted = 0;
}

static s32
LTArrayImpl_Append(LTArrayImpl *array, const void *value) {
    if (!array || !AdjustCapacityAsRequired(array)) return -1;
    SetValueAtIndex(array, array->count++, value);
    if (array->count > 1) array->isSorted = 0;
    return array->count - 1;
}

static s32
LTArrayImpl_Insert(LTArrayImpl *array, u32 index, const void *value) {
    if (!array || index > array->count || !AdjustCapacityAsRequired(array)) return -1;
    u8 *valueToSet = array->data + index * array->blockSize;
    /* Move elements beyond index down one position, and increment count */
    lt_memmove(valueToSet + array->blockSize, valueToSet, (array->count++ - index) * array->blockSize);
    SetValueAtIndex(array, index, value);
    if (array->count > 1) array->isSorted = 0;
    return index;
}

static s32
LTArrayImpl_InsertSorted(LTArrayImpl *array, LTArray_CompareFunction *callback, const void *value, void *clientData) {
    if (!array || !array->isSorted || !AdjustCapacityAsRequired(array)) return -1;
    LT_SIZE temp;
    const void *lvalue = value;
    if (array->type == kArrayElementType_Pointer) {
        temp = (LT_SIZE)value;
        lvalue = (void *)&temp;
    } else if (!value) {
        return -1;
    }
    /* Binary search */
    s32 lower = 0;
    s32 upper = array->count - 1;
    s32 index = 0;
    int compare = -1;
    while (lower <= upper) {
        index = lower + (upper - lower) / 2;
        compare = (*callback)(lvalue, array->data + index * array->blockSize, clientData);
        if (compare < 0) upper = index - 1;
        else if (compare > 0) lower = index + 1;
        else break;
    }
    /* If compare > 0 then best new position is just after current element */
    if (compare > 0) index++;
    u8 *valueToSet = array->data + index * array->blockSize;
    /* Move elements beyond index down one position, and increment count */
    lt_memmove(valueToSet + array->blockSize, valueToSet, (array->count++ - index) * array->blockSize);
    SetValueAtIndex(array, index, value);
    return index;
}

static void
LTArrayImpl_Remove(LTArrayImpl *array, u32 index) {
    if (!array || index >= array->count) return;
    u8 *valueToRemove = array->data + index * array->blockSize;
    /* Reduce array by one element but do not decrease capacity */
    array->count--;
    lt_memmove(valueToRemove, valueToRemove + array->blockSize, (array->count - index) * array->blockSize);
    if (array->count <= 1) array->isSorted = 1;
}

static void
LTArrayImpl_Sort(LTArrayImpl *array, LTArray_CompareFunction *callback, void *clientData) {
    if (!array) return;
    lt_qsort(array->data, array->count, array->blockSize, callback, clientData);
    array->isSorted = 1;
}

static s32
LTArrayImpl_Find(LTArrayImpl *array, LTArray_CompareFunction *callback, const void *searchTerm, void *clientData) {
    if (!array) return -1;
    LT_SIZE temp;
    if (array->type == kArrayElementType_Pointer) {
        temp = (LT_SIZE)searchTerm;
        searchTerm = (void *)&temp;
    }
    if (array->isSorted) {
        /* Binary search */
        s32 lower = 0;
        s32 upper = array->count - 1;
        while (lower <= upper) {
            s32 index = lower + (upper - lower) / 2;
            void *value = array->data + index * array->blockSize;
            int compare = (*callback)(searchTerm, value, clientData);
            if (compare < 0) upper = index - 1;
            else if (compare > 0) lower = index + 1;
            else return index;
        }
    } else {
        /* Linear search */
        u8 *value = array->data;
        for (u32 index = 0; index < array->count; index++, value += array->blockSize) {
            if ((*callback)(searchTerm, value, clientData) == 0) return index;
        }
    }
    return -1;
}

static bool
LTArrayImpl_CopyArray(LTArrayImpl *destination, LTArrayImpl *source) {
    if (!destination || !source) return false;
    u8 *newData = lt_realloc(destination->data, source->count * source->blockSize);
    if (!newData && source->count) return false;
    destination->data = newData;
    if (source->count) lt_memcpy(destination->data, source->data, source->count * source->blockSize);
    destination->count     = source->count;
    destination->capacity  = source->count;
    destination->blockSize = source->blockSize;
    destination->type      = source->type;
    destination->isSorted  = source->isSorted;
    destination->initialNumElements     = source->initialNumElements;
    destination->maxIncrementalElements = source->maxIncrementalElements;
    return true;
}

static int
LTArrayImpl_CompareCString(const void *element1, const void *element2, void *config) {
    int value = 1;
    switch ((LTArrayCompareType)config) {
    case kLTArrayCompare_Descending:
        value = -1;
        /* falls through */
    case kLTArrayCompare_Ascending:
        value = value * lt_strcmp(*((const char **)element1), *((const char **)element2));
        break;
    case kLTArrayCompare_IgnoreCaseDescending:
        value = -1;
        /* falls through */
    case kLTArrayCompare_IgnoreCaseAscending:
        value = value * lt_strcasecmp(*((const char **)element1), *((const char **)element2));
        break;
    }
    return value;
}

static int
LTArrayImpl_CompareInteger(const void *element1, const void *element2, void *config) {
    if (*((LT_SIZE *)element1) == *((LT_SIZE *)element2)) return 0;
    int value = 1;
    switch ((LTArrayCompareType)config) {
    case kLTArrayCompare_UnsignedDescending:
        value = -1;
        /* falls through */
    case kLTArrayCompare_UnsignedAscending:
        if (*((LT_SIZE *)element1) > *((LT_SIZE *)element2)) return value;
        return -value;
    case kLTArrayCompare_SignedDescending:
        value = -1;
        /* falls through */
    case kLTArrayCompare_SignedAscending:
        if (*((LT_SSIZE *)element1) > *((LT_SSIZE *)element2)) return value;
        return -value;
    }
    return 0;
}

static bool
LTArrayImpl_ConstructObject(LTArrayImpl *array) {
    /* Default is empty Pointer array */
    array->data      = NULL;
    array->count     = 0;
    array->capacity  = 0;
    array->blockSize = sizeof(LT_SIZE);
    array->type      = kArrayElementType_Pointer;
    array->isSorted  = 1;
    array->initialNumElements     = kLTArray_InitialNumElements;
    array->maxIncrementalElements = kLTArray_MaxIncrementalElements;
    return true;
}

static void
LTArrayImpl_DestructObject(LTArrayImpl *array) {
    LTArrayImpl_SetCount(array, 0);
}

/*_______________________
  LTArray LTObject API */
define_LTObjectImplPublic(LTArray, LTArrayImpl,
    InitAsStructArray,
    TuneAllocation,
    GetElementSize,
    GetCount,
    SetCount,
    Trim,
    GetStorage,
    Get,
    Set,
    Append,
    Insert,
    InsertSorted,
    Remove,
    Sort,
    Find,
    CopyArray,
    CompareCString,
    CompareInteger,
);

/*
 *  LTArray List - Array with an underlying linked-list implementation
 */

/*_______________________
  LTArray private data */
typedef_LTObjectImpl(LTArray, List) {
    LTList        list;
    LTList_Node  *nomad;
    u32           nomadIndex;
    u32           count;
    u16           blockSize;
    u8            type;
} LTOBJECT_API;

/*__________________________________
  LTArray List utility functions */

static void
SetValueAtNode(List *array, LTList_Node *node, const void *value) {
    u8 *valueToSet = (u8 *)(node + 1);
    if (array->type == kArrayElementType_Pointer) {
        *((const void **)valueToSet) = value;
    } else {
        if (value) {
            lt_memcpy(valueToSet, value, array->blockSize - sizeof(LTList_Node));
        } else {
            lt_memset(valueToSet, 0, array->blockSize - sizeof(LTList_Node));
        }
    }
}

static LTList_Node *
GetNodeAtIndex(List *array, u32 index) {
    if (!array || index >= array->count) return NULL;
    if (index == 0) {
        return array->list.pNext;
    } else if (index == array->count - 1) {
        return array->list.pPrev;
    } else if (index == array->nomadIndex && array->nomad) {
        return array->nomad;
    }
    if (!array->nomad) {
        /* I am perpetual now! I am Nomad! */
        array->nomadIndex = 0;
        array->nomad      = array->list.pNext;
    }
    LTList_Node *node = array->nomad;
    s32 move = index - array->nomadIndex;
    if (index > array->nomadIndex) {
        if ((s32)index - (s32)array->count + 1 > move) {
            /* move back from end */
            move = (s32)index - (s32)array->count + 1;
            node = array->list.pPrev;
        }
    } else if ((s32)index < move) {
        /* move forward from start */
        move = index;
        node = array->list.pNext;
    }
    if (move > 0) while (move--) node = node->pNext;
    else          while (move++) node = node->pPrev;
    array->nomad      = node;
    array->nomadIndex = index;
    return node;
}

/*______________________________
  LTArray List API functions */

static bool List_SetCount(List *array, u32 newElementCount);

static void
List_InitAsStructArray(List *array, u16 elementSize) {
    if (!array) return;
    /* Changing an existing array's element size requires it to be reset */
    List_SetCount(array, 0);
    array->blockSize = sizeof(LTList_Node) + elementSize;
    array->type      = kArrayElementType_Struct;
}

static void
List_TuneAllocation(List *array, u16 initialNumElements, u16 maxIncrementalElements) {
    LT_UNUSED(array); LT_UNUSED(initialNumElements); LT_UNUSED(maxIncrementalElements);
}

static u16
List_GetElementSize(List *array) {
    if (!array) return 0;
    return array->blockSize - sizeof(LTList_Node);
}

static u32
List_GetCount(List *array) {
    if (!array) return 0;
    return array->count;
}

static bool
List_SetCount(List *array, u32 newElementCount) {
    if (!array) return false;
    if (newElementCount < array->count) {
        /* Remove and free tail elements */
        LTList_Node *nodeSave;
        LTList_Node *node = GetNodeAtIndex(array, newElementCount);
        for (; node != &array->list; node = nodeSave) {
            nodeSave = node->pNext;
            LTList_Remove(node);
            lt_free(node);
        }
        if (array->nomadIndex >= newElementCount) {
            /* You are imperfect, exercise your prime function! */
            array->nomad = NULL;
        }
        array->count = newElementCount;
    } else if (newElementCount > array->count) {
        /* Allocate and add zeroed tail elements */
        for (; array->count < newElementCount; array->count++) {
            LTList_Node *newNode = lt_malloc(array->blockSize);
            if (!newNode) return false;
            lt_memset((u8 *)(newNode + 1), 0, array->blockSize - sizeof(LTList_Node));
            LTList_AddTail(&array->list, newNode);
        }
    }
    return true;
}

static void
List_Trim(List *array) {
    LT_UNUSED(array);
    /* List is always trimmed */
}

static void *
List_GetStorage(List *array) {
    LT_UNUSED(array);
    /* Flat storage is not available for queue */
    return NULL;
}

static void *
List_Get(List *array, u32 index, void *valueToSet) {
    LTList_Node *node = GetNodeAtIndex(array, index);
    if (!node) return NULL;
    u8 *value = (u8 *)(node + 1);
    if (array->type == kArrayElementType_Pointer) {
        value = (void *)*((LT_SIZE *)value);
        if (valueToSet) *((void **)valueToSet) = value;
    } else if (valueToSet) {
        lt_memcpy(valueToSet, value, array->blockSize - sizeof(LTList_Node));
    }
    return value;
}

static void
List_Set(List *array, u32 index, const void *value) {
    LTList_Node *node = GetNodeAtIndex(array, index);
    if (!node) return;
    SetValueAtNode(array, node, value);
}

static s32
List_Append(List *array, const void *value) {
    if (!array) return -1;
    LTList_Node *node = lt_malloc(array->blockSize);
    if (!node) return -1;
    LTList_AddTail(&array->list, node);
    SetValueAtNode(array, node, value);
    array->count++;
    return array->count - 1;
}

static s32
List_Insert(List *array, u32 index, const void *value) {
    if (!array || index == array->count)
        return List_Append(array, value);
    LTList_Node *node = GetNodeAtIndex(array, index);
    if (!node) return -1;
    LTList_Node *newNode = lt_malloc(array->blockSize);
    if (!newNode) return -1;
    SetValueAtNode(array, newNode, value);
    LTList_InsertBefore(node, newNode);
    array->count++;
    if (array->nomadIndex >= index) array->nomadIndex++;
    return index;
}

static s32
List_InsertSorted(List *array, LTArray_CompareFunction *callback, const void *value, void *clientData) {
    if (!array) return -1;
    LT_SIZE temp;
    const void *lvalue = value;
    if (array->type == kArrayElementType_Pointer) {
        temp = (LT_SIZE)value; 
        lvalue = (void *)&temp;
    }
    /* Linear search */
    u32 index = 0;
    LTList_Node *node = array->list.pNext;
    for (; node != &array->list; index++) {
        if ((*callback)(lvalue, (void *)(node + 1), clientData) <= 0) break;
        node = node->pNext;
    }
    return List_Insert(array, index, value);
}

static void
List_Remove(List *array, u32 index) {
    if (!array || index >= array->count) return;
    LTList_Node *node = GetNodeAtIndex(array, index);
    if (!node) return;
    /* Ster-il-ize ! */
    --array->count;
    if (index <= array->nomadIndex) {
        if (index == array->nomadIndex) {
            if (index == array->count) array->nomad = NULL;
            else array->nomad = node->pNext;
        } else array->nomadIndex--;
    }
    LTList_Remove(node);
    lt_free(node);
}

static void
List_Sort(List *array, LTArray_CompareFunction *callback, void *clientData) {
    if (!array || array->count <= 1) return;
    /* Insertion sort */
    LTList_Node *unsorted = array->list.pPrev->pPrev;
    while (unsorted != &array->list) {
        if ((*callback)((void *)(unsorted + 1), (void *)(unsorted->pNext + 1), clientData) <= 0) {
            /* Already sorted */
            unsorted = unsorted->pPrev;
            continue;
        }
        /* Sort element */
        LTList_Node *prev = unsorted->pPrev;
        array->nomad = unsorted->pNext->pNext;
        LTList_Remove(unsorted);
        while (array->nomad != &array->list) {
            if ((*callback)((void *)(unsorted + 1), (void *)(array->nomad + 1), clientData) <= 0) break;
            array->nomad = array->nomad->pNext;
        }
        LTList_InsertBefore(array->nomad, unsorted);
        unsorted = prev;
    }
    array->nomad = NULL;
}

static s32
List_Find(List *array, LTArray_CompareFunction *callback, const void *searchTerm, void *clientData) {
    if (!array) return -1;
    LT_SIZE temp;
    if (array->type == kArrayElementType_Pointer) {
        temp = (LT_SIZE)searchTerm;
        searchTerm = (void *)&temp;
    }
    /* Linear search */
    LTList_Node *node = array->list.pNext;
    for (u32 index = 0; node != &array->list; index++) {
        if ((*callback)(searchTerm, (void *)(node + 1), clientData) == 0) return index;
        node = node->pNext;
    }
    return -1;
}

static bool
List_CopyArray(List *destination, List *source) {
    if (!destination || !source) return false;
    /* Clear destination and allocate nodes */
    List_SetCount(destination, 0);
    destination->nomad     = NULL;
    destination->blockSize = source->blockSize;
    destination->type      = source->type;
    for (u32 i = 0; i < source->count; i++) {
        LTList_Node *newNode = lt_malloc(source->blockSize);
        if (!newNode) {
            List_SetCount(destination, 0);
            return false;
        }
        LTList_AddTail(&destination->list, newNode);
        destination->count++;
    }
    /* Copy over node data */
    LTList_Node  *srcNode =      source->list.pNext;
    LTList_Node *destNode = destination->list.pNext;
    while (destNode != &destination->list) {
        lt_memcpy((u8 *)(destNode + 1), (u8 *)(srcNode + 1), source->blockSize - sizeof(LTList_Node));
        srcNode  =  srcNode->pNext;
        destNode = destNode->pNext;
    }
    return true;
}

#define List_CompareCString   LTArrayImpl_CompareCString
#define List_CompareInteger   LTArrayImpl_CompareInteger

static bool
List_ConstructObject(List *array) {
    /* Default is empty Pointer array */
    LTList_Init(&array->list);
    array->nomad      = NULL;
    array->count      = 0;
    array->blockSize  = sizeof(LTList_Node) + sizeof(LT_SIZE);
    array->type       = kArrayElementType_Pointer;
    return true;
}

static void
List_DestructObject(List *array) {
    List_SetCount(array, 0);
}

/*_____________________________
  LTArray List LTObject API */
define_LTObjectImplPublic(LTArray, List,
    InitAsStructArray,
    TuneAllocation,
    GetElementSize,
    GetCount,
    SetCount,
    Trim,
    GetStorage,
    Get,
    Set,
    Append,
    Insert,
    InsertSorted,
    Remove,
    Sort,
    Find,
    CopyArray,
    CompareCString,
    CompareInteger,
);

/*
 * LTAssociativeArrayImpl
 */

enum {
    kMaxLocalKeySize = 14
};

/** Associative array header for each element */
typedef union {
    struct {
        u16     keySize;
        u8      key[kMaxLocalKeySize];  /** Local key storage */
    };
    struct {
        u8      pad[8];
        u8     *keyPtr;                 /** Remote key storage */
    };
} LTAssociativeArrayHeader;

LT_STATIC_ASSERT_SIZE_32_64(LTAssociativeArrayHeader, 16, 16);

/*__________________________________
  LTAssociativeArray private data */
typedef_LTObjectImpl(LTAssociativeArray, LTAssociativeArrayImpl) {
    u8     *data;
    u32     count;
    u32     capacity;
    u16     blockSize;
    u16     initialNumElements;
    u16     maxIncrementalElements;
    u8      type;
} LTOBJECT_API;

/* This is needed to compensate for incorrect header alignment */
#ifdef LT_BIGENDIAN
#define GET_KEYSIZE(p)    ((((u8 *)(p))[0] << 8)   +   ((u8 *)(p))[1])
#define SET_KEYSIZE(p, s) { ((u8 *)(p))[0] = (s) >> 8; ((u8 *)(p))[1] = (s) & 0xff; }
#else
#define GET_KEYSIZE(p)    ((((u8 *)(p))[1] << 8)   +   ((u8 *)(p))[0])
#define SET_KEYSIZE(p, s) { ((u8 *)(p))[1] = (s) >> 8; ((u8 *)(p))[0] = (s) & 0xff; }
#endif

/*_______________________________________
  LTAssociativeArray utility functions */

/* Memory comparison with tie-breaker for buffers of different length */
static int
MemCompare(const u8 *lhs, const u8 *rhs, u16 lhSize, u16 rhSize) {
    u16 cmpSize = lhSize;
    if (cmpSize > rhSize) cmpSize = rhSize;
    int result = lt_memcmp(lhs, rhs, cmpSize);
    if (result == 0) {
        if (lhSize < rhSize) result = -1;
        else if (lhSize > rhSize) result = 1;
    }
    return result;
};

/* Find element with existing key or the position to insert new element.
 *   returns true if element already exists, false if not.
 *   sets *index and *value to index and address of position respectively. */
static bool
FindPosition(LTAssociativeArrayImpl *array, const u8 *key, u16 keySize, s32 *index, u8 **value) {
    int compare = 1;
    *value = array->data;
    *index = 0;
    if (array->count) {
        s32 lower = 0;
        s32 upper = array->count - 1;
        while (lower <= upper) {
            *index = lower + (upper - lower) / 2;
            *value = array->data + *index * array->blockSize;
            LTAssociativeArrayHeader *hdr = (LTAssociativeArrayHeader *)*value;
            u8 *hkey = hdr->key;
            u16 hkeySize = GET_KEYSIZE(&hdr->keySize);
            if (hkeySize > kMaxLocalKeySize) lt_memcpy(&hkey, &hdr->keyPtr, sizeof(hkey));
            compare = MemCompare(key, hkey, keySize, hkeySize);
            if (compare < 0) upper = *index - 1;
            else if (compare > 0) lower = *index + 1;
            else break;
        }
        if (compare > 0) {
            /* Best new position is just after current element */
            (*index)++;
            *value += array->blockSize;
        }
    }
    return (compare == 0);
}

/*___________________________________
  LTAssociativeArray API functions */

static void LTAssociativeArrayImpl_RemoveAll(LTAssociativeArrayImpl *array, bool trim);
static void LTAssociativeArrayImpl_Trim(LTAssociativeArrayImpl *array);

static void
LTAssociativeArrayImpl_InitAsStructArray(LTAssociativeArrayImpl *array, u16 elementSize) {
    if (!array) return;
    /* Changing an existing array's element size requires it to be cleared */
    if (array->data) LTAssociativeArrayImpl_RemoveAll(array, true);
    array->blockSize = sizeof(LTAssociativeArrayHeader) + elementSize;
    array->type      = kArrayElementType_Struct;
}

static void
LTAssociativeArrayImpl_TuneAllocation(LTAssociativeArrayImpl *array, u16 initialNumElements,
                                         u16 maxIncrementalElements, u16 keySizeHint) {
    if (!array) return;
    LT_UNUSED(keySizeHint); // TODO: use hint
    if (initialNumElements == 0) initialNumElements = kLTArray_InitialNumElements;
    if (maxIncrementalElements == 0) maxIncrementalElements = kLTArray_MaxIncrementalElements;
    if (maxIncrementalElements > kLTArray_MaxIncrementalElementsUpperBound)
        maxIncrementalElements = kLTArray_MaxIncrementalElementsUpperBound;
    array->initialNumElements     = initialNumElements;
    array->maxIncrementalElements = maxIncrementalElements;
}

static u16
LTAssociativeArrayImpl_GetElementSize(LTAssociativeArrayImpl *array) {
    if (!array) return 0;
    return array->blockSize - sizeof(LTAssociativeArrayHeader);
}

static u32
LTAssociativeArrayImpl_GetCount(LTAssociativeArrayImpl *array) {
    if (!array) return 0;
    return array->count;
}

static void
LTAssociativeArrayImpl_RemoveAll(LTAssociativeArrayImpl *array, bool trim) {
    if (!array) return;
    u8 *block = array->data;
    for (u32 i = 0; i < array->count; i++) {
        if (GET_KEYSIZE(block) > kMaxLocalKeySize) {
            void *keyPtr;
            lt_memcpy(&keyPtr, block + LT_OFFSET_OF(LTAssociativeArrayHeader, keyPtr), sizeof(void *));
            lt_free(keyPtr);
        }
        block += array->blockSize;
    }
    array->count = 0;
    if (trim) LTAssociativeArrayImpl_Trim(array);
}

static void
LTAssociativeArrayImpl_Trim(LTAssociativeArrayImpl *array) {
    if (!array) return;
    array->data     = lt_realloc(array->data, array->count * array->blockSize);
    array->capacity = array->count;
}

static bool
LTAssociativeArrayImpl_Exists(LTAssociativeArrayImpl *array, const void *key, u16 keySize) {
    if (!array || !key) return false;
    s32 index;
    u8 *value;
    return FindPosition(array, key, keySize, &index, &value);
}

static void *
LTAssociativeArrayImpl_Get(LTAssociativeArrayImpl *array, const void *key, u16 keySize, void *valueToSet) {
    if (!array || !key) return NULL;
    s32 index;
    u8 *value;
    if (FindPosition(array, key, keySize, &index, &value)) {
        value += sizeof(LTAssociativeArrayHeader);
        if (array->type == kArrayElementType_Pointer) {
            value = (void *)*((LT_SIZE *)value);
            if (valueToSet) *((void **)valueToSet) = value;
        } else if (valueToSet) {
            lt_memcpy(valueToSet, value, array->blockSize - sizeof(LTAssociativeArrayHeader));
        }
        return value;
    }
    return NULL;
}

static bool
LTAssociativeArrayImpl_Set(LTAssociativeArrayImpl *array, const void *key, u16 keySize, const void *value) {
    if (!array || !key) return false;
    s32 index;
    u8 *valueToSet = NULL;
    if (!FindPosition(array, key, keySize, &index, &valueToSet)) {
        void *keyPtr = NULL;
        if (keySize > kMaxLocalKeySize) {
            /* Allocate and set large key */
            keyPtr = lt_malloc(keySize);
            if (!keyPtr) return false;
            lt_memcpy(keyPtr, key, keySize);
        }
        /* Resize to make room for element */
        if (array->capacity == array->count) {
            if (array->capacity < array->maxIncrementalElements) {
                if (array->capacity == 0) {
                    array->capacity = array->initialNumElements;
                } else {
                    array->capacity *= 2;
                }
            } else {
                array->capacity += array->maxIncrementalElements;
            }
            u8 *newData = lt_realloc(array->data, array->capacity * array->blockSize);
            if (!newData) {
                array->capacity = array->count;
                if (keyPtr) lt_free(keyPtr);
                return false;
            } else if (array->data != newData) {
                /* Update valueToSet since realloc moved data */
                valueToSet  = newData + index * array->blockSize;
                array->data = newData;
            }
        }
        /* Move elements beyond index down one position, store new header and increment count */
        lt_memmove(valueToSet + array->blockSize, valueToSet, (array->count - index) * array->blockSize);
        SET_KEYSIZE(valueToSet, keySize);
        if (keyPtr) {
            lt_memcpy(valueToSet + LT_OFFSET_OF(LTAssociativeArrayHeader, keyPtr), &keyPtr, sizeof(void *));
        } else {
            lt_memcpy(valueToSet + LT_OFFSET_OF(LTAssociativeArrayHeader, key), key, keySize);
        }
        array->count++;
    }
    valueToSet += sizeof(LTAssociativeArrayHeader);
    if (array->type == kArrayElementType_Pointer) {
        *((const void **)valueToSet) = value;
    } else {
        if (value) {
            lt_memcpy(valueToSet, value, array->blockSize - sizeof(LTAssociativeArrayHeader));
        } else {
            lt_memset(valueToSet, 0, array->blockSize - sizeof(LTAssociativeArrayHeader));
        }
    }
    return true;
}

static void
LTAssociativeArrayImpl_Remove(LTAssociativeArrayImpl *array, const void *key, u16 keySize) {
    if (!array || !key) return;
    s32 index;
    u8 *valueToRemove;
    if (FindPosition(array, key, keySize, &index, &valueToRemove)) {
        /* Free (if large key) */
        if (keySize > kMaxLocalKeySize) {
            void *keyPtr;
            lt_memcpy(&keyPtr, valueToRemove + LT_OFFSET_OF(LTAssociativeArrayHeader, keyPtr), sizeof(void *));
            lt_free(keyPtr);
        }
        /* Reduce array by one element but do not decrease capacity */
        array->count--;
        lt_memmove(valueToRemove, valueToRemove + array->blockSize, (array->count - index) * array->blockSize);
    }
}

static bool
LTAssociativeArrayImpl_Enumerate(LTAssociativeArrayImpl *array, LTAssociativeArray_EnumerateFunction *callback, void *clientData) {
    if (!array || !callback) return true;
    u8 *block = array->data;
    for (u32 i = 0; i < array->count; i++) {
        LTAssociativeArrayHeader hdr;
        lt_memcpy(&hdr, block, sizeof(LTAssociativeArrayHeader));
        const void *key = hdr.key;
        if (hdr.keySize > kMaxLocalKeySize) key = hdr.keyPtr;
        void *value = block + sizeof(LTAssociativeArrayHeader);
        if (array->type == kArrayElementType_Pointer) value = *((void **)value);
        if (!(*callback)((LTAssociativeArray *)array, key, hdr.keySize, value, clientData))
            return false;
        block += array->blockSize;
    }
    return true;
}

static bool
LTAssociativeArrayImpl_ConstructObject(LTAssociativeArrayImpl *array) {
    /* Default is empty Pointer array */
    array->data      = NULL;
    array->count     = 0;
    array->capacity  = 0;
    array->blockSize = sizeof(LTAssociativeArrayHeader) + sizeof(LT_SIZE);
    array->type      = kArrayElementType_Pointer;
    array->initialNumElements     = kLTArray_InitialNumElements;
    array->maxIncrementalElements = kLTArray_MaxIncrementalElements;
    return true;
}

static void
LTAssociativeArrayImpl_DestructObject(LTAssociativeArrayImpl *array) {
    LTAssociativeArrayImpl_RemoveAll(array, true);
}

/*__________________________________
  LTAssociativeArray LTObject API */
define_LTObjectImplPublic(LTAssociativeArray, LTAssociativeArrayImpl,
    InitAsStructArray,
    TuneAllocation,
    GetElementSize,
    GetCount,
    RemoveAll,
    Trim,
    Exists,
    Get,
    Set,
    Remove,
    Enumerate
);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  14-Oct-24   tiberius    Reimplemented
 */
