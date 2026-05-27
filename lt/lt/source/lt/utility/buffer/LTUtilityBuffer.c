/******************************************************************************
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/utility/buffer/LTBuffer.h>

#define CONFIG_DEFAULT_SIZE   32
#define ABSOLUTE_MAX_CAPACITY (LT_U32_MAX / 2)

/* ************************************************************************** */
/*                                LTBufferImpl                                */
/* ************************************************************************** */

typedef_LTObjectImpl(LTBuffer, LTBufferImpl) {
    u32          head;
    u32          tail;
    LTBufferSpan span;
    u32          increment;
    u32          maxCapacity;
}
LTOBJECT_API;

static const LTBufferApi s_LTBufferFrozenApi;

static void LTBufferImpl_DestructObject(LTBufferImpl *buf) {
    lt_free(buf->span.data);
}

static bool LTBufferImpl_ConstructObject(LTBufferImpl *buf) {
    buf->maxCapacity = ABSOLUTE_MAX_CAPACITY;
    buf->increment   = CONFIG_DEFAULT_SIZE;
    buf->span.size   = 0;
    buf->span.data   = NULL;

    return true;
}

static void LTBufferImpl_Reset(LTBufferImpl *buf) {
    buf->head = 0;
    buf->tail = 0;
}

static void LTBufferImpl_SetMaxCapacity(LTBufferImpl *buf, u32 capacity) {
    if (capacity > ABSOLUTE_MAX_CAPACITY) {
        capacity = ABSOLUTE_MAX_CAPACITY;
    }
    buf->maxCapacity = capacity;
}

static u32 LTBufferImpl_GetMaxCapacity(LTBufferImpl *buf) {
    return buf->maxCapacity;
}

static u32 LTBufferImpl_GetSize(LTBufferImpl *buf) {
    return buf->head - buf->tail;
}

static u32 LTBufferImpl_GetCapacity(LTBufferImpl *buf) {
    return buf->maxCapacity - LTBufferImpl_GetSize(buf);
}

static u32 Private_GetAllocatedCapacity(LTBufferImpl *buf) {
    return (buf->tail + buf->span.size) - buf->head;
}

static u32 Private_GetContiguousCapacity(LTBufferImpl *buf, u32 *contiguousPart) {
    u32 allocatedCapacity = Private_GetAllocatedCapacity(buf);
    u32 contiguous        = 0;

    if (allocatedCapacity > 0) {
        // Calculate the contiguous space available at the end of the buffer
        contiguous = buf->span.size - (buf->head % buf->span.size);
    }

    if (contiguousPart != NULL) {
        *contiguousPart = contiguous;
    }

    return allocatedCapacity;
}

static u32 Private_GetContiguousSize(LTBufferImpl *buf, u32 *contiguousPart) {
    u32 size       = LTBufferImpl_GetSize(buf);
    u32 contiguous = 0;

    if (size > 0) {
        // Calculate the contiguous data available from the tail position
        u32 tail_pos = buf->tail % buf->span.size;
        contiguous   = LT_MIN(size, buf->span.size - tail_pos);
    }

    if (contiguousPart != NULL) {
        *contiguousPart = contiguous;
    }

    return size;
}

static u8 *Private_GetDataAt(LTBufferImpl *buf, u32 offset) {
    return buf->span.data + ((offset) % buf->span.size);
}

/** @private
 * This implementation reallocates the entire buffer and copies the data contiguously into the start of the newly
 * allocated memory.
 */
static u32 LTBufferImpl_Reserve(LTBufferImpl *buf, u32 requested) {
    // Always change incremental size, even if nothing happens.
    buf->increment = requested;

    // Early exit if we already have the requested capacity.
    u32 capacity = Private_GetAllocatedCapacity(buf);
    if (capacity >= requested) {
        return capacity;
    }

    LTBufferSpan span  = buf->span;
    span.size         += requested;
    if (span.size > buf->maxCapacity) {
        return capacity;
    }
    // Allocate a buffer to accommodate the new size and copy the existing data into it.
    span.data         = lt_malloc(span.size);
    LTBufferSpan used = buf->API->Read((LTBuffer *)buf, span);
    // Replace existing buffer with new buffer.
    lt_free(buf->span.data);
    buf->span = span;
    buf->head = used.size;
    buf->tail = 0;

    // Return value is the new available capacity.
    return span.size - used.size;
}

static u32 Private_ReserveAtLeast(LTBufferImpl *buf, u32 requested) {
    return LTBufferImpl_Reserve(buf, LT_MAX(requested, buf->increment));
}

static LTBufferSpan LTBufferImpl_Write(LTBufferImpl *buf, LTBufferSpan src) {
    src.size     = LT_MIN(src.size, Private_ReserveAtLeast(buf, src.size));
    u32 position = buf->head % buf->span.size;
    u32 copy     = LT_MIN(src.size, buf->span.size - position);

    // Copy the first part of the data
    lt_memcpy(buf->span.data + position, src.data, copy);

    // Copy any remaining data that wraps around to the beginning of the buffer
    if (copy < src.size) {
        lt_memcpy(buf->span.data, src.data + copy, src.size - copy);
    }

    buf->head += src.size;
    return src;
}

/** @private
 * This implementation guarantees contiguous spans by providing a temporary buffer in the case where a write would span
 * across the end of the underlying ring buffer.
 */
static LTBufferAccess LTBufferImpl_StartWrite(LTBufferImpl *buf, u32 capacity) {
    LTBufferAccess access;
    if (capacity == 0) {
        // Get the largest contiguous span available.
        u32 contiguous = Private_GetContiguousCapacity(buf, NULL);
        access.span    = (LTBufferSpan){Private_GetDataAt(buf, buf->head), contiguous};
        access.priv    = NULL;
    } else {
        access.span.size = LT_MIN(capacity, Private_ReserveAtLeast(buf, capacity));
        u32 contiguous   = Private_GetContiguousCapacity(buf, NULL);
        if (access.span.size <= contiguous) {
            // Access size can fit in a contiguous span, so just directly return a pointer.
            access.span.data = Private_GetDataAt(buf, buf->head);
            access.priv      = NULL;
        } else {
            // Access size spans a discontinuity, so return a temporary buffer to write into.
            access.span.data = lt_malloc(access.span.size);
            access.priv      = access.span.data;
        }
    }
    return access;
}

static void LTBufferImpl_FinishWrite(LTBufferImpl *buf, LTBufferAccess access) {
    if (access.priv) {
        LT_ASSERT(access.priv == access.span.data);
        bool success = LTBufferImpl_Write(buf, access.span).size == access.span.size;
        LT_ASSERT(success);
        LT_UNUSED(success);
        lt_free((void *)access.priv);
    } else {
        LT_ASSERT(access.span.data == Private_GetDataAt(buf, buf->head));
        buf->head += access.span.size;
    }
}

static u32 LTBufferImpl_Fill(LTBufferImpl *buf, u32 size) {
    size                  = LT_MIN(size, Private_ReserveAtLeast(buf, size));
    LTBufferAccess access = LTBufferImpl_StartWrite(buf, size);
    lt_memset(access.span.data, 0, access.span.size);
    LTBufferImpl_FinishWrite(buf, access);

    return access.span.size;
}

static LTBufferSpan LTBufferImpl_Read(LTBufferImpl *buf, LTBufferSpan dst) {
    u32 size = LTBufferImpl_GetSize(buf);
    dst.size = LT_MIN(size, dst.size);

    if (dst.size > 0) {
        u32 position = buf->tail % buf->span.size;
        u32 copy     = LT_MIN(dst.size, buf->span.size - position);

        // Copy the first contiguous portion of the data
        lt_memcpy(dst.data, buf->span.data + position, copy);

        // Copy any remaining data that wraps around from the beginning of the buffer
        if (copy < dst.size) {
            lt_memcpy(dst.data + copy, buf->span.data, dst.size - copy);
        }

        // Update the tail pointer to reflect the read data
        buf->tail += dst.size;
    }

    return dst;
}

static LTBufferSpan LTBufferImpl_Peek(LTBufferImpl *buf, LTBufferSpan dst, u32 offset) {
    u32 size = LTBufferImpl_GetSize(buf);

    // Check if offset is beyond available data
    if (offset >= size) {
        dst.size = 0;
        return dst;
    }

    // Adjust available size based on offset
    size     -= offset;
    dst.size  = LT_MIN(size, dst.size);

    if (dst.size > 0) {
        u32 position = (buf->tail + offset) % buf->span.size;
        u32 copy     = LT_MIN(dst.size, buf->span.size - position);

        // Copy the first contiguous portion of the data
        lt_memcpy(dst.data, buf->span.data + position, copy);

        // Copy any remaining data that wraps around from the beginning of the buffer
        if (copy < dst.size) {
            lt_memcpy(dst.data + copy, buf->span.data, dst.size - copy);
        }
    }

    return dst;
}

static LTBufferAccess LTBufferImpl_StartRead(LTBufferImpl *buf, u32 capacity) {
    LTBufferAccess access;
    u32            contiguous = Private_GetContiguousSize(buf, NULL);
    if (capacity == 0) {
        access.span = (LTBufferSpan){Private_GetDataAt(buf, buf->tail), contiguous};
        access.priv = NULL;
    } else {
        access.span.size = LT_MIN(capacity, LTBufferImpl_GetSize(buf));
        if (access.span.size <= contiguous) {
            access.span.data = Private_GetDataAt(buf, buf->tail);
            access.priv      = NULL;
        } else {
            access.span.data = lt_malloc(access.span.size);
            access.priv      = access.span.data;
            LTBufferImpl_Peek(buf, access.span, 0);
        }
    }
    return access;
}

static void LTBufferImpl_FinishRead(LTBufferImpl *buf, LTBufferAccess access) {
    if (access.priv) {
        lt_free((void *)access.priv);
    }
    buf->tail += access.span.size;
}

static u32 LTBufferImpl_Skip(LTBufferImpl *buf, u32 size) {
    size       = LT_MIN(size, LTBufferImpl_GetSize(buf));
    buf->tail += size;
    return size;
}

static LTBuffer *LTBufferImpl_Extract(LTBufferImpl *buf, u32 size, bool replace) {
    if (replace) {
        size       = LT_MIN(size, LTBufferImpl_GetSize(buf));
        buf->head -= size;
        return (LTBuffer *)buf;
    } else {
        LTBufferImpl *extracted = (LTBufferImpl *)lt_createobject(LTBuffer);
        if (size) {
            LTBufferAccess data = LTBufferImpl_StartWrite(extracted, size);
            data.span           = LTBufferImpl_Read(buf, data.span);
            LTBufferImpl_FinishWrite(extracted, data);
        }
        return (LTBuffer *)extracted;
    }
}

static LTBuffer *LTBufferImpl_Chain(LTBufferImpl *before, LTBuffer *after) {
    LTBufferAccess access;
    while ((access = after->API->StartRead(after, 0)).span.size) {
        LTBufferImpl_Write(before, access.span);
        after->API->FinishRead(after, access);
    }
    lt_destroyobject(after);
    return (LTBuffer *)before;
}

static LTBuffer *LTBufferImpl_Freeze(LTBufferImpl *buf) {
    if (buf->tail != 0) {
        LTBufferSpan span;
        span.size = LTBufferImpl_GetSize(buf);
        span.data = lt_malloc(span.size);
        LTBufferImpl_Read(buf, span);
        lt_free(buf->span.data);
        buf->span = span;
        buf->tail = 0;
    }
    buf->API = &s_LTBufferFrozenApi;
    return (LTBuffer *)buf;
}

static LTBuffer *LTBufferImpl_Unfreeze(LTBufferImpl *buf) {
    return (LTBuffer *)buf;
}

define_LTObjectImplPublic(LTBuffer,
                          LTBufferImpl,
                          Reset,
                          SetMaxCapacity,
                          GetMaxCapacity,
                          GetCapacity,
                          GetSize,
                          Reserve,
                          Write,
                          StartWrite,
                          FinishWrite,
                          Fill,
                          Read,
                          StartRead,
                          FinishRead,
                          Peek,
                          Skip,
                          Extract,
                          Chain,
                          Freeze,
                          Unfreeze);

/* ************************************************************************** */
/*                               LTBufferFrozen                               */
/* ************************************************************************** */

static void LTBufferFrozen_Reset(LTBufferImpl *buf) {
    buf->tail = 0;
}

static void LTBufferFrozen_SetMaxCapacity(LTBufferImpl *buf, u32 capacity) {
    LT_UNUSED(buf);
    LT_UNUSED(capacity);
}

static u32 LTBufferFrozen_GetCapacity(LTBufferImpl *buf) {
    LT_UNUSED(buf);
    return 0;
}

static u32 LTBufferFrozen_Reserve(LTBufferImpl *buf, u32 requested) {
    LT_UNUSED(buf);
    LT_UNUSED(0);
    LT_UNUSED(requested);
    return 0;
}

static LTBufferSpan LTBufferFrozen_Write(LTBufferImpl *buf, LTBufferSpan src) {
    LT_UNUSED(buf);
    LT_UNUSED(src);
    return (LTBufferSpan){0};
}

static LTBufferAccess LTBufferFrozen_StartWrite(LTBufferImpl *buf, u32 capacity) {
    LT_UNUSED(buf);
    LT_UNUSED(capacity);
    return (LTBufferAccess){0};
}

static void LTBufferFrozen_FinishWrite(LTBufferImpl *buf, LTBufferAccess access) {
    LT_UNUSED(buf);
    LT_UNUSED(access);
}

static u32 LTBufferFrozen_Fill(LTBufferImpl *buf, u32 size) {
    LT_UNUSED(buf);
    LT_UNUSED(size);
    return 0;
}

static LTBuffer *LTBufferFrozen_Freeze(LTBufferImpl *buf) {
    return (LTBuffer *)buf;
}

static LTBuffer *LTBufferFrozen_Unfreeze(LTBufferImpl *buf) {
    buf->API = &s_LTBufferImplApi;
    return (LTBuffer *)buf;
}

static const LTBufferApi s_LTBufferFrozenApi = {
    .GetObjectApiName       = LTBufferImpl_GetObjectApiName,
    .GetObjectApiVersion    = LTBufferImpl_GetObjectApiVersion,
    .GetObjectInterfaceType = LTUtilityBufferImpl_GetObjectInterfaceType,
    .GetObjectLibrary       = LTUtilityBufferImpl_GetLibrary,
    .AddRef                 = LTUtilityBufferImpl_AddRef,
    .RemoveRef              = LTUtilityBufferImpl_RemoveRef,
    .GetObjectImplName      = LTBufferImpl_GetObjectImplName,
    .GetObjectImplSize      = LTBufferImpl_GetObjectImplSize,
    .ConstructObject        = (void *)LTBufferImpl_ConstructObject,
    .DestructObject         = (void *)LTBufferImpl_DestructObject,
    .GetLTObjectExt         = LTBufferImpl_GetLTObjectExt,
    .Reset                  = (void *)LTBufferFrozen_Reset,
    .SetMaxCapacity         = (void *)LTBufferFrozen_SetMaxCapacity,
    .GetMaxCapacity         = (void *)LTBufferImpl_GetMaxCapacity,
    .GetCapacity            = (void *)LTBufferFrozen_GetCapacity,
    .GetSize                = (void *)LTBufferImpl_GetSize,
    .Reserve                = (void *)LTBufferFrozen_Reserve,
    .Write                  = (void *)LTBufferFrozen_Write,
    .StartWrite             = (void *)LTBufferFrozen_StartWrite,
    .FinishWrite            = (void *)LTBufferFrozen_FinishWrite,
    .Fill                   = (void *)LTBufferFrozen_Fill,
    .Read                   = (void *)LTBufferImpl_Read,
    .StartRead              = (void *)LTBufferImpl_StartRead,
    .FinishRead             = (void *)LTBufferImpl_FinishRead,
    .Peek                   = (void *)LTBufferImpl_Peek,
    .Skip                   = (void *)LTBufferImpl_Skip,
    .Extract                = (void *)LTBufferImpl_Extract,
    .Chain                  = (void *)LTBufferImpl_Chain,
    .Freeze                 = (void *)LTBufferFrozen_Freeze,
    .Unfreeze               = (void *)LTBufferFrozen_Unfreeze,
};

/*_________________________________________
  LTUtilityBuffer Root interface binding */
static bool LTUtilityBuffer_LibInit(void) {
    return true;
}

static void LTUtilityBuffer_LibFini(void) {}

define_LTObjectLibrary(1, LTUtilityBuffer_LibInit, LTUtilityBuffer_LibFini);
