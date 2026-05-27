/******************************************************************************
 * <lt/utility/buffer/LTRing.h>                                  LT Ring Buffer
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_UTILITY_BUFFER_LTRING_H
#define ROKU_LT_INCLUDE_LT_UTILITY_BUFFER_LTRING_H

/**
 * @defgroup ltutility_buffer_ring LTRing
 * @ingroup ltutility_buffer
 * @{
 *
 * @brief High performance power-of-two ring buffer.
 *
 * This ring buffer data structure wraps a raw array with a FIFO interface and is the simplest and
 * potentially fastest collection available in LT.  The algorithm is designed around the simplifying
 * requirement that the size of all ring buffers must be a power of two, allowing the use of bitwise
 * operations instead of modulus.
 *
 * This data structure provides:
 * - O(1) insertion at the head
 * - O(1) deletion at the tail
 * - O(1) random access
 *
 * It does not support insertion or deletion anywhere other than head and tail respectively.
 */

/******************************************************************************
 * Types
 ******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>

typedef struct {
    u32 head;         ///< Write index into data
    u32 tail;         ///< Read index into data
    u32 mask;         ///< Number of elements in the buffer, less 1
    u32 elementSize;  ///< Size of an individual element
    u8 *data;         ///< Backing data buffer
} LTRingBuffer;

/**< Simple ring buffer data structure
 *
 * The ring buffer and corresponding functions are meant to be used with power-of-two sizes.  The
 * indices correspond to the element index, not the byte index, so all data should be retrieved as
 * index * elementSize.
 */

typedef struct {
    LTAtomic head;         ///< @see LTRingBuffer::head
    LTAtomic tail;         ///< @see LTRingBuffer::tail
    u32      mask;         ///< @see LTRingBuffer::mask
    u32      elementSize;  ///< @see LTRingBuffer::elementSize
    u8      *data;         ///< @see LTRingBuffer::data
} LTRingBufferAtomic;

/**< Atomic version of @ref LTRingBuffer */

/******************************************************************************
 * Operations
 ******************************************************************************/
// clang-format off
#ifndef __cplusplus
#define LTRingGetHead(rb) _Generic((rb), \
              LTRingBuffer*: (rb)->head, \
        LTRingBufferAtomic*: LTAtomic_Load(&(rb)->head))
/**< Get the head index of the ring buffer */

#define LTRingGetTail(rb) _Generic((rb), \
              LTRingBuffer*: (rb)->tail, \
        LTRingBufferAtomic*: LTAtomic_Load(&(rb)->tail))
/**< Get the tail index of the ring buffer */

#define LTRingInit(rb, _data, _size) _Generic((rb), \
              LTRingBuffer*: LTRingImpl_Init, \
        LTRingBufferAtomic*: LTRingImpl_InitAtomic)((rb), (u8 *)(_data), sizeof((_data)[0]), _size)
/**< Initialize a ring buffer with a backing store
 *
 * @param rb LTRingBuffer structure
 * @param _data Pointer to backing store for the ring buffer
 * @param _size Number of elements in _data; must be a power of 2
 *
 * The element size will be derived from the type of the backing store.
 */

#define LTRingGetSize(rb) _Generic((rb), \
              LTRingBuffer*: LTRingImpl_GetSize, \
        LTRingBufferAtomic*: LTRingImpl_GetSizeAtomic)((rb))
/**< Get the number of elements used in the ring buffer */

#define LTRingGetCapacity(rb) _Generic((rb), \
              LTRingBuffer*: LTRingImpl_GetCapacity, \
        LTRingBufferAtomic*: LTRingImpl_GetCapacityAtomic)((rb))
/**< Get the number of elements the buffer has room to store from a head index */

#define LTRingGetContiguousSize(rb) _Generic((rb), \
              LTRingBuffer*: LTRingImpl_GetContiguousSize,\
        LTRingBufferAtomic*: LTRingImpl_GetContiguousSizeAtomic)((rb))
/**< Get the number of elements that can be read from the buffer without wrapping */

#define LTRingGetContiguousCapacity(rb) _Generic((rb), \
              LTRingBuffer*: LTRingImpl_GetContiguousCapacity, \
        LTRingBufferAtomic*: LTRingImpl_GetContiguousCapacityAtomic)((rb))
/**< Get the number of elements that could be written to the buffer without wrapping */

#define LTRingPeek(rb, offset) _Generic((rb), \
              LTRingBuffer*: LTRingImpl_Peek, \
        LTRingBufferAtomic*: LTRingImpl_PeekAtomic)((rb), offset)
/**< Get a pointer to the available element at an offset
 *
 * @param rb Ring buffer
 * @param offset Offset of element within the buffer
 * @returns NULL if buffer doesn't have an element available at offset, otherwise a pointer to that element
 */

#define LTRingReserve(rb, count) (void*)_Generic((rb), \
              LTRingBuffer*: LTRingImpl_Reserve, \
        LTRingBufferAtomic*: LTRingImpl_ReserveAtomic)(rb, count, NULL)
/**< Get a pointer to the next empty element in the buffer
 *
 * @param rb Ring buffer
 * @returns NULL if the buffer capacity is less than count, otherwise a pointer to the element at
 * rb->head
 */

#define LTRingReserveContiguous(rb, count, numContiguous) (void*)_Generic((rb), \
              LTRingBuffer*: LTRingImpl_Reserve, \
        LTRingBufferAtomic*: LTRingImpl_ReserveAtomic)(rb, count, numContiguous)
/**< Same as LTRingReserve, but returns how much of the reservation is contiguous */

#define LTRingConsume(rb, count) (void*)_Generic((rb), \
              LTRingBuffer*: LTRingImpl_Consume, \
        LTRingBufferAtomic*: LTRingImpl_ConsumeAtomic)(rb, count, NULL)
/**< Consume elements from the ring buffer after reading, returning them to the writer
 *
 * @param rb Ring buffer
 * @param count Number of elements to consume
 * @returns True if the buffer contained at least count elements, false otherwise
 */
#endif
// clang format on

#define LTRingCalculateIndex(rb, ptr) \
    ((((u8 *)(ptr)) - (rb)->data) / (rb)->elementSize)
/**< Calculate the ring buffer index of a pointer into the buffer */

#define LTRingCalculateTailOffset(rb, index) \
    ((index) - LTRingGetTail((rb))) & (rb)->mask
/**< Calculate the offset of an absolute index from the tail */

#define LTRingAt(rb, index) \
    ((void *)((rb)->data + ((index) & (rb)->mask) * (rb)->elementSize))
/**< Get a pointer to the specified element in the buffer
 *
 * @param rb Ring buffer
 * @param index Index of the element to get
 * @returns Pointer to the element at index
 */

/******************************************************************************
 * Internal Implementation
 ******************************************************************************/
#define LTRingGetSizeExplicit(rb, _head, _tail)     ((_head) - (_tail))
#define LTRingGetCapacityExplicit(rb, _head, _tail) (((_tail) + (rb)->mask + 1) - (_head))

LT_INLINE void LTRingImpl_Init(LTRingBuffer *rb, u8 * _data, u32 _elementSize, u32 _size) {
    rb->head        = 0;
    rb->tail        = 0;
    rb->mask        = (_size - 1);
    rb->elementSize = _elementSize;
    rb->data        = _data;
}

LT_INLINE void LTRingImpl_InitAtomic(LTRingBufferAtomic *rb, u8 * _data, u32 _elementSize, u32 _size) {
    LTAtomic_Store(&rb->head, 0);
    LTAtomic_Store(&rb->tail, 0);
    rb->mask        = (_size - 1);
    rb->elementSize = _elementSize;
    rb->data        = _data;
}

LT_INLINE u32 LTRingImpl_GetSize(LTRingBuffer *rb) {
    return LTRingGetSizeExplicit(rb, rb->head, rb->tail);
}

LT_INLINE u32 LTRingImpl_GetSizeAtomic(LTRingBufferAtomic *rb) {
    return LTRingGetSizeExplicit(rb, LTAtomic_Load(&rb->head), LTAtomic_Load(&rb->tail));
}

LT_INLINE u32 LTRingImpl_GetCapacity(LTRingBuffer *rb) {
    return LTRingGetCapacityExplicit(rb, rb->head, rb->tail);
}

LT_INLINE u32 LTRingImpl_GetCapacityAtomic(LTRingBufferAtomic *rb) {
    return LTRingGetCapacityExplicit(rb, LTAtomic_Load(&rb->head), LTAtomic_Load(&rb->tail));
}

LT_INLINE u32 LTRingImpl_GetContiguousSizePrivate(u32 head, u32 tail, u32 mask) {
    u32 end   = mask + 1 - (tail & mask);
    u32 count = head - tail;
    return (count < end) ? count : end;
}

LT_INLINE u32 LTRingImpl_GetContiguousSize(LTRingBuffer *rb) {
    return LTRingImpl_GetContiguousSizePrivate(rb->head, rb->tail, rb->mask);
}

LT_INLINE u32 LTRingImpl_GetContiguousSizeAtomic(LTRingBufferAtomic *rb) {
    return LTRingImpl_GetContiguousSizePrivate(LTAtomic_Load(&rb->head), LTAtomic_Load(&rb->tail), rb->mask);
}

LT_INLINE u32 LTRingImpl_GetContiguousCapacityPrivate(u32 head, u32 tail, u32 mask) {
    u32 end   = (mask + 1) - (head & mask);
    u32 count = (tail + mask + 1) - head;
    return (count < end) ? count : end;
}

LT_INLINE u32 LTRingImpl_GetContiguousCapacity(LTRingBuffer *rb) {
    return LTRingImpl_GetContiguousCapacityPrivate(rb->head, rb->tail, rb->mask);
}

LT_INLINE u32 LTRingImpl_GetContiguousCapacityAtomic(LTRingBufferAtomic *rb) {
    return LTRingImpl_GetContiguousCapacityPrivate(LTAtomic_Load(&rb->head), LTAtomic_Load(&rb->tail), rb->mask);
}

LT_INLINE void * LTRingImpl_Peek(LTRingBuffer *rb, u32 offset) {
    return (LTRingGetSize(rb) <= offset) ? NULL : LTRingAt(rb, rb->tail + offset);
}

LT_INLINE void * LTRingImpl_PeekAtomic(LTRingBufferAtomic *rb, u32 offset) {
    return (LTRingGetSize(rb) <= offset) ? NULL : LTRingAt(rb, LTAtomic_Load(&rb->tail) + offset);
}

LT_INLINE u8 *LTRingImpl_Reserve(LTRingBuffer *rb, u32 count, u32 *numContiguous) {
    if (LTRingGetCapacity(rb) < count)  return NULL;

    u8 *data = (u8*)LTRingAt(rb, rb->head);
    if (numContiguous) {
        *numContiguous = LTRingGetContiguousCapacity(rb);
        if (*numContiguous > count) *numContiguous = count;
    }
    rb->head += count;
    return data;
}

LT_INLINE u8 *LTRingImpl_ReserveAtomic(LTRingBufferAtomic *rb, u32 count, u32 *numContiguous) {
    u32 head;
    u32 tail = LTAtomic_Load(&rb->tail);

    do {
        head = LTAtomic_Load(&rb->head);
        if (LTRingGetCapacityExplicit(rb, head, tail) < count) return NULL;
    }
    while (! LTAtomic_CompareAndExchange(&rb->head, head, head + count));

    if (numContiguous) {
        *numContiguous = LTRingImpl_GetContiguousCapacityPrivate(head, tail, rb->mask);
        if (*numContiguous > count) *numContiguous = count;
    }

    return (u8*)LTRingAt(rb, head);
}

LT_INLINE u8 *LTRingImpl_Consume(LTRingBuffer *rb, u32 count, u32 *numContiguous) {
    if (LTRingGetSize(rb) < count) return NULL;
    u8 *data = (u8*)LTRingAt(rb, rb->tail);
    if (numContiguous) {
        *numContiguous = LTRingGetContiguousSize(rb);
        if (*numContiguous > count) *numContiguous = count;
    }
    rb->tail += count;
    return data;
}

LT_INLINE u8 *LTRingImpl_ConsumeAtomic(LTRingBufferAtomic *rb, u32 count, u32 *numContiguous) {
    u32 head = LTAtomic_Load(&rb->head);
    u32 tail;

    do {
        tail = LTAtomic_Load(&rb->tail);
        if (LTRingGetSizeExplicit(rb, head, tail) < count) return NULL;
    }
    while (!LTAtomic_CompareAndExchange(&rb->tail, tail, tail + count));

    if (numContiguous) {
        *numContiguous = LTRingImpl_GetContiguousSizePrivate(head, tail, rb->mask);
        if (*numContiguous > count) *numContiguous = count;
    }

    return (u8*)LTRingAt(rb, tail);
}

LT_INLINE void LTRingImpl_Write_Copy(u8 *dst, const u8 *src, u8 *data, u32 count, u32 contiguous) {
    if(contiguous >= count) {
        lt_memcpy(dst, src, count);
    } else {
        lt_memcpy(dst, src, contiguous);
        lt_memcpy(data, src + contiguous, count - contiguous);
    }
}

LT_INLINE bool LTRingWrite(LTRingBuffer *rb, const u8 *src, u32 count) {
    u32 contiguous;
    u8 *dst = LTRingImpl_Reserve(rb, count, &contiguous);
    if(!dst) {
        return false;
    }
    LTRingImpl_Write_Copy(dst, src, rb->data, count * rb->elementSize, contiguous * rb->elementSize);
    return true;
}

LT_INLINE void LTRingImpl_Read_Copy(u8 *dst, u8 *src, u8 *data, u32 count, u32 contiguous) {
    if(contiguous >= count) {
        lt_memcpy(dst, src, count);
    } else {
        lt_memcpy(dst, src, contiguous);
        lt_memcpy(dst + contiguous, data, count - contiguous);
    }
}

LT_INLINE bool LTRingRead(LTRingBuffer *rb, u8 *dst, u32 count, bool peek) {
    u32 contiguous;
    u8 *src;
    if(!peek) {
        src = LTRingImpl_Consume(rb, count, &contiguous);
    } else {
        contiguous = LTRingImpl_GetContiguousSize(rb);
        src = LTRingPeek(rb, 0);
    }
    if(!src) {
        return false;
    }
    LTRingImpl_Read_Copy(dst, src, rb->data, count * rb->elementSize, contiguous * rb->elementSize);
    return true;
}

/** @} */

#endif
