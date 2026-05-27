/******************************************************************************
 *
 * The Ring Queue is a conveyor belt type data structure that has no concept
 * of use level. The full size of the queue is always available for use.
 * Multiple producers can modify any location of the queue. A single consumer
 * consumes data from the head. The consumed area is zeroed filled.
 *
 * First use case is for audio mixer buffer. Queue size not limited to power of
 * 2 to allow the queue length to be an integer multiple of low-level audio hardware
 * buffer sizes.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTMediaPipelineRingQueue.h"

/*  ______________________________________________
 *  Object private data members */
typedef_LTObjectImpl(LTMediaPipelineRingQueue, LTMediaPipelineRingQueueImpl) {
    u8 * data;
    u32 elementSize;
    u32 elementCount;
    u32 head;
} LTOBJECT_API;

bool LTMediaPipelineRingQueueImpl_Init(LTMediaPipelineRingQueueImpl * queue, u32 elementSize, u32 elementCount) {
    if (!queue ||
        !elementSize ||
        !elementCount) return false;

    queue->elementCount = elementCount;

    // Allocate the data memory
    queue->data = lt_malloc(elementSize * queue->elementCount);
    if (!queue->data) return false;
    lt_memset(queue->data, 0, elementSize * queue->elementCount);
    queue->elementSize = elementSize;
    queue->head = 0;
    return true;
}

void LTMediaPipelineRingQueueImpl_Destroy(LTMediaPipelineRingQueueImpl * queue) {
    if (!queue) return;
    if (queue->data) {
        lt_free(queue->data);
        queue->data = NULL;
    }
}

void* LTMediaPipelineRingQueueImpl_GetElementFromHead(LTMediaPipelineRingQueueImpl * queue, u32 elementIndex) {
    u32 index;
    if (!queue || !queue->data) return NULL;
    index = (queue->head + elementIndex) % queue->elementCount;
    return queue->data + (index * queue->elementSize);
}

LT_SIZE LTMediaPipelineRingQueueImpl_GetContiguousElements(LTMediaPipelineRingQueueImpl * queue, u32 elementsFromHead) {
    u32 index;
    if (!queue || !queue->data) return 0;
    index = (queue->head + elementsFromHead) % queue->elementCount;
    return queue->elementCount - index;
}

void LTMediaPipelineRingQueueImpl_AdvanceHead(LTMediaPipelineRingQueueImpl * queue, u32 elementCount) {
    // Fill the advance area with zeroes, take care not to overflow the buffer
    LT_SIZE advanceAreaSize;
    if (!queue || !queue->data) return;
    advanceAreaSize = queue->elementSize * elementCount;
    if (elementCount >= queue->elementCount) {
        // Advance area is larger than the buffer, fill the entire buffer with zeroes
        lt_memset(queue->data, 0, queue->elementSize * queue->elementCount);
        return;
    }

    if (queue->head + elementCount > queue->elementCount) {
        // Advance area wraps around the end of the buffer
        LT_SIZE firstPartSize = queue->elementSize * (queue->elementCount - queue->head);
        LT_SIZE secondPartSize = advanceAreaSize - firstPartSize;
        lt_memset(queue->data + (queue->head * queue->elementSize), 0, firstPartSize);
        lt_memset(queue->data, 0, secondPartSize);
        queue->head = queue->head + elementCount - queue->elementCount;
    } else {
        // Advance area does not wrap around the end of the buffer
        lt_memset(queue->data + (queue->head * queue->elementSize), 0, advanceAreaSize);
        queue->head = queue->head + elementCount;
    }
}

/*  _________________________________
 *  Object constructor and destructor
 */
static void LTMediaPipelineRingQueueImpl_DestructObject(LTMediaPipelineRingQueueImpl *queue) {
    LTMediaPipelineRingQueueImpl_Destroy(queue);
}

static bool LTMediaPipelineRingQueueImpl_ConstructObject(LTMediaPipelineRingQueueImpl *queue) {
    queue->data = NULL;
    queue->elementCount = 0;
    queue->elementSize = 0;
    queue->head = 0;
    return true;
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPrivate(LTMediaPipelineRingQueue, LTMediaPipelineRingQueueImpl,
    Init,
    Destroy,
    GetElementFromHead,
    GetContiguousElements,
    AdvanceHead,
);

