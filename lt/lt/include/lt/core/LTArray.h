/******************************************************************************
 * <lt/core/LTArray.h>                                         LT Dynamic Array
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltcore_array LTArray and LTAssociativeArray
 * @ingroup ltcore
 * @{
 *
 * @brief A set of array utilities.
 */

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTARRAY_H
#define ROKU_LT_INCLUDE_LT_CORE_LTARRAY_H

#include <lt/core/LTCore.h>

LT_EXTERN_C_BEGIN

/*____________
 * LT Array */

/* LTArrays are dynamic arrays that hold elements of arbitrary (but uniform) size.
 * LTArrays are indexed using an integer value and
 *   1. do not provide any mutex: clients must handle all required locking externally.
 *   2. support both get-by-copy and get-by-reference.
 *   3. can store pointers that reference objects of non-uniform size.
 *   4. guarantee that the first element data is aligned to a 64-bit boundary. If element sizes are a multiple of
 *        32- or 64-bits then all subsequent elements will also have that respective alignment. Alignment does not
 *        normally matter when using get-by-copy operations but should be considered when using get-by-reference.
 *
 * There are two types of LTArrays, the Default implementation and the alternative List implementation.
 *   1. The Default implementation stores arrays just like C-Arrays in contiguous memory thus optimizing total
 *      memory usage. The Default implementation supports binomial search and sort algorithms.
 *
 *   2. The List implementation stores array elements in linked lists, thus optimizing the speed of insert and
 *      delete operations at the expense of memory overhead. Note that the List implementation only supports
 *      linear search and sort operations. The following creates an array using the List implementation:
 *          LTArray *array = lt_createobject_typed(LTArray, List);
 */

enum {
    kLTArray_InitialNumElements                = 2,
    kLTArray_MaxIncrementalElements            = 8,
    kLTArray_MaxIncrementalElementsUpperBound  = 1024,
   /* When the first element is added to a LTArray or LTAssociativeArray, the array will allocate enough
    * memory to store kLTArray_InitialNumElements elements. Thereafter it will double in allocation size
    * each time a reallocation is needed up to a maximum increase amount of kLTArray_MaxIncrementalElements
    * elements. These numbers may be adjusted for the array with the TuneAllocation() function.
    * kLTArray_MaxIncrementalElementsUpperBound is the high limit on the nMaxIncrementalElements value passed
    * into TuneAllocation(). */
};

typedef int (LTArray_CompareFunction)(const void *element1, const void *element2, void *clientData);
/**< Compare function callback used for sorts and searches.
 * @note For Pointer arrays element1 and element2 must be dereferenced to obtain the actual pointer data.
 *
 * @param element1 pointer to the left-hand element for sorts (or the search term for finds) to compare with.
 * @param element2 pointer to the right-hand element to compare with.
 * @param clientData pointer to client data passed from client API call and passed down to this callback.
 * @returns -1 if lhs < rhs
 * @returns  0 if lhs == rhs -OR- matched search term
 * @returns  1 if lhs > rhs
 */

/* Client data values to use with built-in and/or application-specific compare functions */
typedef enum {
    /* For alphabetic comparisons */
    kLTArrayCompare_Ascending            = 0,  /**<  Ascending sort order */
    kLTArrayCompare_Descending           = 1,  /**< Descending sort order */
    kLTArrayCompare_IgnoreCaseAscending  = 2,  /**<  Ascending sort order, ignoring case */
    kLTArrayCompare_IgnoreCaseDescending = 3,  /**< Descending sort order, ignoring case */

    /* for numeric comparisons */
    kLTArrayCompare_UnsignedAscending    = 0,  /**<  Ascending sort order, interpret pointer as unsigned 32-bit integer */
    kLTArrayCompare_UnsignedDescending   = 1,  /**< Descending sort order, interpret pointer as unsigned 32-bit integer */
    kLTArrayCompare_SignedAscending      = 2,  /**<  Ascending sort order, interpret pointer as signed 32-bit integer */
    kLTArrayCompare_SignedDescending     = 3,  /**< Descending sort order, interpret pointer as signed 32-bit integer */
} LTArrayCompareType;

typedef_LTObject(LTArray, 1) {

    void             (* InitAsStructArray)  (LTArray *array, u16 elementSize);
    /**< Initialize this LTArray as a Structure array that holds struct data of a given fixed size.
     *
     *  This call is used to initialize an array as a Structure array instead of the default Pointer array.
     *  As the name suggests, a struct array is intended to hold C struct data of the given element size.
     *  This function is usually invoked on newly created LTArray objects but may also be used to repurpose
     *  an existing array. When repurposed, all existing array elements are automatically deallocated.
     *
     *  If this call is NOT used, then the default Pointer array element type will instead be used, where array
     *  elements are void pointers of LT_SIZE. 32-bit integers may also be stored in a Pointer array if they are
     *  cast to/from void pointers and LT_SIZE.
     *
     *  @param array pointer to array instance.
     *  @param elementSize the size of a struct array element in bytes.
     */

    void             (* TuneAllocation)     (LTArray *array, u16 initialNumElements, u16 maxIncrementalElements);
    /**< Tune the allocation performance of this array.
     *
     *  This call sets the initial capacity and max incremental capacity for the array. The use of initial and
     *  incremental allocations helps decrease the number of memory reallocation calls invoked by LTArray. The
     *  maximum increment prevents array capacity from getting too large. If a particular tuning value is set
     *  to zero than the default will instead be used.
     *
     *  @param array pointer to array instance.
     *  @param initialNumElements initial number of elements allocated when the first element is set in array,
     *         if zero then the default value will be used.
     *  @param maxIncrementalElements please see kLTArray_MaxIncrementalElements,
     *         if zero then the default value will be used.
     */

    u16              (* GetElementSize)     (LTArray *array);
    /**< Obtain the size in bytes of elements in the array.
     *
     *  @param array pointer to array instance.
     *  @returns size of array element in bytes.
     *  @returns sizeof(LT_SIZE) for pointer arrays.
     */

    u32              (* GetCount)           (LTArray *array);
    /**< Obtain the current number of elements stored in the array.
     *  If the element count is zero, then the array is empty.
     *
     *  @param array pointer to array instance.
     *  @returns number of elements currently in the array.
     */

    bool             (* SetCount)           (LTArray *array, u32 newElementCount);
    /**< Truncate or extend array and trim the array capacity.
     *  New elements of extended arrays will be filled with zero-byte data.
     *  @note After this call capacity == count and the array is said to be "trimmed."
     *
     *  @param array pointer to array instance.
     *  @param newElementCount number of elements to resize array count and capacity to.
     *  @returns true if newElementCount <= previous element count (automatic success).
     *  @returns true if able to increase element count of array.
     *  @returns false on failure to increase element count.
     */

    void             (* Trim)               (LTArray *array);
    /**< Frees memory by reducing the underlying capacity to the current element count.
     *  After this is called the array capacity will equal the array count.
     *
     *  @param array pointer to array instance.
     */

    void *           (* GetStorage)         (LTArray *array);
    /**< Obtain pointer to the local storage for the array.
     *  Changes to the stored data that alter the array sorting order may result in unpredictable
     *  behavior. In these situations invoke Sort() to rearrange the array.
     *
     *  @param array pointer to array instance.
     *  @returns pointer to the local storage, NULL if array is empty. Pointer is valid until the
     *           the next operation that alters the array.
     *  @returns NULL for List array implementations.
     */

    void *           (* Get)                (LTArray *array, u32 index, void *valueToSet);
    /**< Obtain array element data from given index by copy or reference.
     *
     *  @param array pointer to array instance.
     *  @param index element index to obtain the copy or reference from.
     *  @param valueToSet where to copy struct data for a Struct array or where to store pointer for a Pointer
     *                    array. If NULL or index is out of range, no data will be copied.
     *  @returns pointer that is stored at the given index of a Pointer array. This pointer can be cast to LT_SIZE to
     *           obtain an integer if integers are stored in the Pointer array.
     *  @returns pointer to the internal element at the given index of a Struct array (get-by-reference).
                 Pointer is valid until the next operation that alters the array.
     *  @returns NULL if index is out of range, or if the stored pointer is itself NULL!
     */

    void             (* Set)                (LTArray *array, u32 index, const void *value);
    /**< Replace the value of the array at the given index.
     *
     *  @param array pointer to array instance.
     *  @param index element index where to store the pointer or struct data.
     *  @param value pointer to store into a Pointer array -OR- pointer to the struct to copy into a Struct array.
     *               With a Pointer array if the value is NULL a NULL will be stored in the array at the given index.
     *               With a Struct array if the value is NULL a zeroed struct will be stored in the array at the given index.
     */

    s32              (* Append)             (LTArray *array, const void *value);
    /**< Append new element to the end of an array.
     *
     *  @param array pointer to array instance.
     *  @param value pointer to store into a Pointer array -OR- pointer to the struct to copy into a Struct array.
     *               With a Pointer array if the value is NULL a NULL will be stored in the array at the given index.
     *               With a Struct array if the value is NULL a zeroed struct will be stored in the array at the given index.
     *  @returns index of the newly appended element (now the last element in the array).
     *  @returns -1 if element could not be appended due to allocation error.
     */

    s32              (* Insert)             (LTArray *array, u32 index, const void *value);
    /**< Insert value at array index.
     *
     *  @param array pointer to array instance.
     *  @param index element index where to insert the new value. Pre-existing elements at index and beyond are moved down one
     *               index in the array. If index is equal to the current element count, the new value is appended to the array.
     *  @param value pointer to store into a Pointer array -OR- pointer to the struct to copy into a Struct array.
     *               With a Pointer array if the value is NULL a NULL will be stored in the array at the given index.
     *               With a Struct array if the value is NULL a zeroed struct will be stored in the array at the given index.
     *  @returns index of the newly inserted element.
     *  @returns -1 if element could not be added due to allocation error.
     */

    s32              (* InsertSorted)       (LTArray *array, LTArray_CompareFunction *callback, const void *value, void *clientData);
    /**< Insert new array element into its sorted position.
     *  @note The interpretation of "sorted" is application-specific as it depends on the compare function and the client data.
     *  @note The use of InsertSorted() requires that array elements added prior to this call are already sorted.
     *  @see Sort
     *
     *  @param array pointer to array instance.
     *  @param callback pointer to callback function with sorting compare function.
     *  @param value pointer to store into a Pointer array -OR- pointer to the struct to copy into a Struct array.
     *               With a Pointer array if the value is NULL a NULL will be stored in the array at the given index.
     *               NB: With a Struct array if the value is NULL NO ACTION will be performed.
     *  @param clientData pointer to client data that is handed down to the callback.
     *  @returns index of the newly inserted element.
     *  @returns -1 if element could not be added due to allocation error.
     */

    void             (* Remove)             (LTArray *array, u32 index);
    /**< Remove an element at the given index.
     *
     *  @param array pointer to array instance.
     *  @param index element index which is to be removed. Pre-existing elements at index and beyond are moved up one index in the array.
     */

    void             (* Sort)               (LTArray *array, LTArray_CompareFunction *callback, void *clientData);
    /**< Rearrange array elements into sorted order.
     *  @note The interpretation of "sorted" is application-specific as it depends on the compare function and the client data.
     *  @note Arrays with less than 2 elements are "sorted." When an array element is added or changed with SetCount, Set,
     *        Insert or Append such that the resultant array has 2 or more elements, the array becomes "unsorted." A subsequent
     *        call to Sort will use quicksort to reorder the elements such that it is "sorted" again. Sorted arrays allow
     *        for faster searching using the Find function.
     *  @note If the array uses the List implementation then linear search and sort will always be used.
     *  @see Find
     *
     *  @param array pointer to array instance.
     *  @param callback pointer to callback function for performing sorting.
     *  @param clientData pointer to client data that is handed down to the callback.
     */

    s32              (* Find)               (LTArray *array, LTArray_CompareFunction *callback, const void *searchTerm, void *clientData);
    /**< Returns index of array element that matches the given search term.
     *  @note The interpretation of "match" is application-specific as it depends on the compare function and the client data.
     *  @note If the array is sorted, Find will perform a binary search for the data; if unsorted the search will be linear,
     *        i.e.: O(N) for you nerds out there. Linear searches are slow (for you non-nerds). However, if the array uses
     *        the List implementation then linear search and sort will be used instead.
     *  @note Find may NOT work if the find compare function is not consistent with the sorting compare function used when
     *        Sort() or InsertSorted() were originally called.
     *  @see Sort
     *
     *  @param array pointer to array instance.
     *  @param callback pointer to callback function for performing find.
     *  @param searchTerm pointer to the application-specific search term for the find operation.
     *  @param clientData pointer to client data that is handed down to the callback.
     *  @returns index of array element found.
     *  @returns -1 if element could not be found.
     */

    bool             (* CopyArray)          (LTArray *destination, LTArray *source);
    /**< Copies contents of source array to destination array.
     *  @note If the destination array is not empty, its contents will first be deallocated.
     *  @note Copying arrays of pointers or arrays of structs that contain pointers will copy the pointers, not the objects
     *        they point to.
     *  @note The destination array capacity will be trimmed after the copy.
     *
     *  @param destination pointer to destination array for copy.
     *  @param source pointer to source array for copy.
     *  @returns true if copy is successful, false on allocation error.
     */

    /* Built-in compare functions.
     *   IMPORTANT: These two compare functions are the only ones that should be implemented here,
     *                 DO NOT ADD OTHER COMPARE FUNCTIONS.
     */
    int              (* CompareCString)     (const void *element1, const void *element2, void *config);
    /**< Built-in LTArray_CompareFunction callback for alphabetic comparisons of C strings.
     *  @note Array must be Pointer array where stored pointers point to null-terminated C strings.
     *  Example usage:
     *  <pre>array->API->Sort(array, array->API->CompareCString, (void *)kLTArrayCompare_IgnoreCaseDescending);</pre>
     */

    int              (* CompareInteger)     (const void *element1, const void *element2, void *config);
    /**< Built-in LTArray_CompareFunction callback for numeric comparisons of 32-bit integers or pointers.
     *  @note Array must be Pointer array storing integer or pointer data. Integer data may be signed or unsigned.
     *  Example usage:
     *  <pre>array->API->Sort(array, array->API->CompareInteger, (void *)kLTArrayCompare_SignedAscending);</pre>
     */

} LTOBJECT_API;

/*_____________________________
 * LT Array helper functions */

/** Create an array of struct object */
LT_INLINE LTArray *LTArray_CreateStructArray(u32 sizeInBytes) {
    LTArray *array = lt_createobject(LTArray);
    if (array) array->API->InitAsStructArray(array, sizeInBytes);
    return array;
}

/** RemoveAndFree() frees and removes a pointer element from a Pointer array.
  *   The stored pointer must point to dynamically allocated memory. */
LT_INLINE void LTArray_RemoveAndFree(LTArray *array, u32 index) {
    lt_free(array->API->Get(array, index, NULL));
    array->API->Remove(array, index);
}

/** RemoveAndFreeAll() frees and removes all pointers from a Pointer array.
  *   All pointers must point to dynamically allocated memory. */
LT_INLINE void LTArray_RemoveAndFreeAll(LTArray *array) {
    u32 count = array->API->GetCount(array);
    void **elm = (void **)array->API->GetStorage(array);
    while (count--) lt_free(*elm++);
    array->API->SetCount(array, 0);
}

/*_______________________
 * LTAssociative Array */

/*  LTAssociativeArrays are dynamic arrays that hold elements of arbitrary (but uniform) size.
 *  LTAssociativeArrays are indexed with (optionally) variable-length keys and
 *    1. do not provide any mutex: clients must handle all required locking externally.
 *    2. support both get-by-copy and get-by-reference.
 *    3. can store pointers that reference objects of non-uniform size.
 *    4. guarantee that the first element data is aligned to a 64-bit boundary. If element sizes are a multiple of
 *         32- or 64-bits then all subsequent elements will also have that respective alignment. Alignment does not
 *         normally matter when using get-by-copy operations but should be considered when using get-by-reference. */

typedef struct LTAssociativeArray LTAssociativeArray;

typedef bool (LTAssociativeArray_EnumerateFunction)(LTAssociativeArray *array, const void *key, const u16 keySize, void *value, void *clientData);
/**< Callback to be invoked for each key-value pair during enumeration.
 *  @note The element data at *value may be modified by the callback.
 *  @note Client code cannot make assumptions about the alignment of the key.
 *  @note It is safe to use the following LTAssociativeArray API functions in the callback:
 *          GetElementSize(), GetCount(), Exists() and Get().
 *
 *  @param array pointer to array instance.
 *  @param key pointer to key used as array index.
 *  @param keySize size of key in bytes.
 *  @param value the pointer stored at the given key for a Pointer array,
 *               pointer to the struct data stored at the given key for a Struct array.
 *  @returns false to abort enumeration, true to continue.
 */

typedef_LTObject(LTAssociativeArray, 1) {

    void             (* InitAsStructArray)  (LTAssociativeArray *array, u16 elementSize);
    /**< Initialize this LTArray as a Structure array that holds struct data of a given fixed size.
     *
     *  This call is used to initialize an array as a Structure array instead of the default Pointer array.
     *  As the name suggests, a struct array is intended to hold C struct data of the given element size.
     *  This function is usually invoked on newly created LTArray objects but may also be used to repurpose
     *  an existing array. When repurposed, all existing array elements are automatically deallocated.
     *
     *  If this call is NOT used, then the default Pointer array element type will instead be used, where array
     *  elements are void pointers of LT_SIZE. 32-bit integers may also be stored in a Pointer array if they are
     *  cast to/from void pointers and LT_SIZE.
     *
     *  @param array pointer to array instance.
     *  @param elementSize the size of a struct array element in bytes.
     */

    void             (* TuneAllocation)     (LTAssociativeArray *array, u16 initialNumElements, u16 maxIncrementalElements, u16 keySizeHint);
    /**< Tune the allocation performance of this array.
     *
     *  This call sets the initial capacity, max incremental capacity and tuned key size for the array. The use
     *  of initial and incremental allocations helps decrease the number of memory reallocation calls invoked
     *  by LTArray. The maximum increment prevents array capacity from getting too large. If a particular tuning
     *  value is set to zero than the default will instead be used.
     *
     *  The key size hint helps the array optimize its internal allocation. If a key is significantly larger
     *  than the key size hint it may be allocated in the heap instead of stored in the array buffer. For
     *  example, a large hint value may cause the array to store ALL keys in the heap, no matter the size of
     *  a particular key. Setting this value is no guarantee of improved performance and certain implementations
     *  may ignore the value altogether. If the key size is zero a small default value will be used.
     *
     *  If it is known that all keys are u32 integers, for example, the client can invoke:<pre>
     *           array->API->TuneAllocation(array, kInitial, kMaxIncrement, sizeof(u32));</pre>
     *
     *  If a majority of keys are strings around 16 bytes in length, the client can invoke:<pre>
     *           array->API->TuneAllocation(array, kInitial, kMaxIncrement, 16);</pre>
     *
     *  @param array pointer to array instance.
     *  @param initialNumElements initial number of elements allocated when the first element is set in array,
     *         if zero then the default value will be used.
     *  @param maxIncrementalElements please see kLTArray_MaxIncrementalElements,
     *         if zero then the default value will be used.
     *  @param keySizeHint helps to optimize the allocation for the given key size for this array,
     *         if zero then the default value will be used.
     */

    u16              (* GetElementSize)     (LTAssociativeArray *array);
    /**< Obtain the size in bytes of elements in the array.
     *
     *  @param array pointer to array instance.
     *  @returns size of array element in bytes.
     *  @returns sizeof(LT_SIZE) for pointer arrays.
     */

    u32              (* GetCount)           (LTAssociativeArray *array);
    /**< Obtain the current number of elements stored in the array.
     *  If the element count is zero, then the array is empty.
     *
     *  @param array pointer to array instance.
     *  @returns number of elements currently in the array.
     */

    void             (* RemoveAll)          (LTAssociativeArray *array, bool trim);
    /**< Remove all entries from the array, setting element count to zero.
     *  @note This doesn't change the array capacity unless trim = true.
     *
     *  @param array pointer to array instance.
     *  @param trim true to trim remaining array resources.
     */

    void             (* Trim)               (LTAssociativeArray *array);
    /**< Frees memory by reducing the underlying capacity to the current element count.
     *  After this is called the array capacity will equal the array count.
     *
     *  @param array pointer to array instance.
     */

    bool             (* Exists)             (LTAssociativeArray *array, const void *key, u16 keySize);
    /**< Check if a value exists at the given key.
     *
     *  @param array pointer to array instance.
     *  @param key pointer to key used as array index. NULL is not a valid key.
     *  @param keySize size of key in bytes.
     *  @returns true if value exists for the given key or key is NULL.
     */

    void *           (* Get)                (LTAssociativeArray *array, const void *key, u16 keySize, void *valueToSet);
    /**< Obtain an array element value by copy or reference.
     *
     *  @param array pointer to array instance.
     *  @param key pointer to key used as array index. NULL is not a valid key.
     *  @param keySize size of key in bytes.
     *  @param valueToSet where to copy struct data for a Struct array or where to store pointer for a Pointer
     *                    array. If NULL or index is out of range, no data will be copied.
     *  @returns pointer that is stored at the given index of a Pointer array. This pointer can be cast to LT_SIZE to
     *           obtain an integer if integers are stored in the Pointer array.
     *  @returns pointer reference to the element at the given index of a Struct array (get-by-reference). Pointer is
     *           valid until the next operation that alters the array.
     *  @returns NULL if value doesn't exist for the given key, the key is NULL or if the stored pointer is itself NULL!
     */

    bool             (* Set)                (LTAssociativeArray *array, const void *key, u16 keySize, const void *value);
    /**< Add or replace an array element at the given key.
     *
     *  @param array pointer to array instance.
     *  @param key pointer to key used as array index. NULL is not a valid key.
     *  @param keySize size of key in bytes.
     *  @param value pointer to store into a Pointer array -OR- pointer to the struct to copy into a Struct array.
     *               With a Pointer array if the value is NULL a NULL will be stored in the array at the given index.
     *               With a Struct array if the value is NULL a zeroed struct will be stored in the array at the given index.
     *  @returns true on success, false on allocation error or if key is NULL.
     */

    void             (* Remove)             (LTAssociativeArray *array, const void *key, u16 keySize);
    /**< Remove the array element at the given key.
     *  This call does nothing if the key does not exist.
     *
     *  @param array pointer to array instance.
     *  @param key pointer to key used as array index. NULL is not a valid key.
     *  @param keySize size of key in bytes.
     */

    bool             (* Enumerate)          (LTAssociativeArray *array, LTAssociativeArray_EnumerateFunction *callback, void *clientData);
    /**< Invoke enumeration callback on all elements of the array.
     *  Please see LTAssociativeArray_EnumerateFunction() for more information.
     *
     *  @param array pointer to array instance.
     *  @param callback pointer to callback to invoke on each element.
     *  @param clientData pointer to client data that is handed down to the callback.
     *  @returns false if any callback returned false (aborted enumeration), true otherwise.
     */

} LTOBJECT_API;

/*_________________________________________
 * LT Associative Array helper functions */

/** Create an associative array of struct object */
LT_INLINE LTAssociativeArray *LTAssociativeArray_CreateStructArray(u32 sizeInBytes) {
    LTAssociativeArray *array = lt_createobject(LTAssociativeArray);
    if (array) array->API->InitAsStructArray(array, sizeInBytes);
    return array;
}

/** RemoveAndFree() removes a pointer element from a Pointer associative array and frees it.
  *   The stored pointer must point to dynamically allocated memory. */
LT_INLINE void LTAssociativeArray_RemoveAndFree(LTAssociativeArray *array, const void *key, u16 keyLen) {
    lt_free(array->API->Get(array, key, keyLen, NULL));
    array->API->Remove(array, key, keyLen);
}

/*________________________________________________________________________
 * LT Associative Array helper functions for arrays using C-String keys */

/** Exists() helper function for CString-keyed LTAssociativeArrays */
LT_INLINE bool LTCStringKeyedArray_Exists(LTAssociativeArray *array, const char *key) {
    return array->API->Exists(array, key, lt_strlen(key) + 1);
}

/** Get() helper function for CString-keyed LTAssociativeArrays */
LT_INLINE void *LTCStringKeyedArray_Get(LTAssociativeArray *array, const char *key, void *valueToSet) {
    return array->API->Get(array, key, lt_strlen(key) + 1, valueToSet);
}

/** Set() helper function for CString-keyed LTAssociativeArrays */
LT_INLINE bool LTCStringKeyedArray_Set(LTAssociativeArray *array, const char *key, const void *value) {
    return array->API->Set(array, key, lt_strlen(key) + 1, value);
}

/** Remove() helper function for CString-keyed LTAssociativeArrays */
LT_INLINE void LTCStringKeyedArray_Remove(LTAssociativeArray *array, const char *key) {
    array->API->Remove(array, key, lt_strlen(key) + 1);
}

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_LTARRAY_H */

/** @} */
