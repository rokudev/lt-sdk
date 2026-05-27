/******************************************************************************
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_UTILITY_BUFFER_LTBUFFER_H
#define ROKU_LT_INCLUDE_LT_UTILITY_BUFFER_LTBUFFER_H

#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>

#define LTBuffer_MakeSpan(ptr, size)                                                                                   \
    (LTBufferSpan) {                                                                                                   \
        (u8 *)(ptr), (size)                                                                                            \
    }

/** Identifies a contiguous block of memory */
typedef struct {
    /** Pointer to the start of the span */
    u8 *data;
    /** Number of bytes in the span */
    u32 size;
} LTBufferSpan;

/** Records a request to access memory within a LTBuffer */
typedef struct {
    /** Span that is being accessed
     *
     * This may be modified to match the spand that was actually accessed when returned to the Finish operation.
     */
    LTBufferSpan span;
    /** Private bookkeeping for the LTBuffer to track this access */
    const void  *priv;
} LTBufferAccess;

/** Generic memory-backed buffer class */
typedef_LTObject(LTBuffer, 1) {
    /** Empty all data from the buffer
     *
     * This is not specified to have any effect on actual memory allocation of the buffer.
     */
    void (*Reset)(LTBuffer *buf);

    /** Set the maximum capacity of the buffer
     *
     * This sets an upper limit to the number of bytes which may be written into the buffer before being read. Reducing
     * the capacity below its current used size has an undefined effect on the used contents.  Default capacity is
     * implementation-defined.
     */
    void (*SetMaxCapacity)(LTBuffer *buf, u32 capacity);

    /** Get the capacity set by @ref SetCapacity */
    u32 (*GetMaxCapacity)(LTBuffer *buf);

    /** Get the number of bytes that may be written to the buffer before reaching capacity */
    u32 (*GetCapacity)(LTBuffer *buf);

    /** Get the number of bytes that can be read from the buffer before becoming empty */
    u32 (*GetSize)(LTBuffer *buf);

    /** Reserve memory adequate for writing a number of bytes
     *
     * @param requested Number of additional bytes that should be reserved
     * @returns Number of additional bytes that are writable after reservation
     *
     * If the capacity is reached, the reservation may be less than requested.
     */
    u32 (*Reserve)(LTBuffer *buf, u32 requested);

    /** Write bytes into the buffer
     *
     * @param src Memory span to be written into the buffer
     * @returns Subspan of src that was written into the buffer
     */
    LTBufferSpan (*Write)(LTBuffer *buf, LTBufferSpan src);

    /** Retrieve a memory span that can be written into
     *
     * @param capacity Size of the span required for writing or 0 for largest size available without extra processing
     * @returns Writable memory span access
     * @see FinishWrite
     *
     * This function is guaranteed to always return a contiguous block of memory, but it is undefined where that memory
     * lives.  This may or may not result in extra copies.  Only a single active write may be outstanding on a buffer
     * meaning that any call to this function must always be followed by a call to FinishWrite before this may be called
     * again, even if no data was actually written.
     */
    LTBufferAccess (*StartWrite)(LTBuffer *buf, u32 capacity);

    /** Finalize writing into a span previously returned by StartWrite
     *
     * @param access Value returned by most recent call to StartWrite updated to indicate written subspan
     * @see StartWrite
     *
     * This function commits a previous write to the buffer.  The memory span is no longer valid after this call.
     */
    void (*FinishWrite)(LTBuffer *buf, LTBufferAccess access);

    /** Zero-fill the buffer
     *
     * @param size Number of bytes to fill
     * @returns Number of bytes actually filled
     */
    u32 (*Fill)(LTBuffer *buf, u32 size);

    /** Reads bytes out of the buffer
     *
     * @param dst Memory span to be read into from the buffer
     * @returns Subspan of dst that was filled
     */
    LTBufferSpan (*Read)(LTBuffer *buf, LTBufferSpan dst);

    /** Retrieve a memory span that can be read from
     *
     * @param capacity Size of the span required for reading or 0 for largest size available without extra processing
     * @returns Readable memory span access
     * @see FinishRead
     *
     * Behaves as @ref StartWrite.
     */
    LTBufferAccess (*StartRead)(LTBuffer *buf, u32 capacity);

    /** Finalize reading from a span previously returned by StartRead
     *
     * @param access Value returned by most recent call to StartRead updated to indicate read subspan
     * @see StartRead
     *
     * Behaves as @ref FinishWrite.
     */
    void (*FinishRead)(LTBuffer *buf, LTBufferAccess access);

    /** Behaves as Read but does not discard the consumed data from the buffer and can read from any location
     *
     * @param dst On input the memory to read into, on output the portion that was used
     * @param offset Offset into the buffer to start reading
     * @returns The number of bytes actually read from the buffer
     *
     * This allows the same data to be read multiple times or for a smaller amount to be read from a known offset.
     */
    LTBufferSpan (*Peek)(LTBuffer *buf, LTBufferSpan dst, u32 offset);

    /** Discard data from the buffer without reading it
     *
     * @param size Number of bytes to discard
     * @returns Number of bytes actually discarded
     */
    u32 (*Skip)(LTBuffer *buf, u32 size);

    /** Extract used bytes into a new buffer
     *
     * @param size Number of bytes to extract from the buffer or 0 for the largest span that can be extracted without
     * extra work (may be the entire buffer or nothing)
     * @param replace Replace this buffer with the extracted data rather than creating a new buffer
     * @returns A new buffer containing the extracted bytes
     *
     * This will result in two buffers containing disjoint subsets of the original buffer.  The returned buffer is
     * different from the original and contains the next size bytes, while any remaining bytes are left in the original
     * buffer.  This may be more efficient (and no worse) than performing equivalent operations manually depending on
     * the internal representation.  For example, if two buffers are chained together and then the same number of bytes
     * are extracted, it will simply unchain the buffers resulting in no copies.
     */
    LTBuffer *(*Extract)(LTBuffer *buf, u32 size, bool replace);

    /** Combine two buffers into a single buffer
     *
     * @param before The buffer that will come first in the chain
     * @param after The buffer that will come after in the chain
     * @returns A buffer which contains the contents of before followed by after
     * @warning Ownership of before and after are transferred and they must not be modified or deleted
     *
     * This operation may be more efficient (and no worse) than simply copying before and after into a new buffer.  If
     * the requirement of never modifying the input buffers is acceptable, then this function should be preferred.
     */
    LTBuffer *(*Chain)(LTBuffer *before, LTBuffer *after);

    /** Freeze a buffer making it read-only and allowing it to be read multiple times */
    LTBuffer *(*Freeze)(LTBuffer *buf);

    /** Return the buffer to its original interface */
    LTBuffer *(*Unfreeze)(LTBuffer *buf);
}

LTOBJECT_API;

#define LTBuffer_WrapValue(ptr) LTBuffer_MakeSpan((ptr), sizeof(*(ptr)))

#define LTBuffer_WriteValue(obj, ptr)                                                                                  \
    ((obj)->API->Write((obj), LTBuffer_MakeSpan((ptr), sizeof(*(ptr)))).size == sizeof(*(ptr)))

#define LTBuffer_ReadValue(obj, ptr)                                                                                   \
    ((obj)->API->Read((obj), LTBuffer_MakeSpan((ptr), sizeof(*(ptr)))).size == sizeof(*(ptr)))

#define LTBuffer_PeekValue(obj, ptr, offset)                                                                           \
    ((obj)->API->Peek((obj), LTBuffer_MakeSpan((ptr), sizeof(*(ptr))), (offset) * sizeof(*(ptr))).size                 \
     == sizeof(*(ptr)))

LT_INLINE void LTBuffer_Copy(LTBuffer *to, LTBuffer *from, u32 offset, u32 size) {
    LTBufferAccess access  = from->API->StartRead(from, offset + size);
    access.span.size      -= offset;
    access.span.data      += offset;
    to->API->Write(to, access.span);
    access.span.size = 0;
    from->API->FinishRead(from, access);
}

#endif
