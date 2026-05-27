/******************************************************************************
 * LTMediaPipeline Ring Queue Implementation
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_MEDIA_PIPELINE_LTMEDIAPIPELINERINGQUEUE_H
#define ROKU_LT_SOURCE_LT_MEDIA_PIPELINE_LTMEDIAPIPELINERINGQUEUE_H

/**
 * @brief LT Media Pipeline Ring Queue data structure
 *
 * Ring Queue is a conveyor belt type data structure that has no concept
 * of data level. The full size of the queue is always available. 
 * Multiple producers can modify any location on the queue. A single consumer
 * consumes data from the head. The consumed area is zero filled.
 * 
 * Care must be taken to ensure thread safety. The implementation is not 
 * thread-safe.
 */

/******************************************************************************
 * Types
 ******************************************************************************/
#include <lt/core/LTCore.h>

/**< Ring Queue data structure
 */

typedef_LTObject(LTMediaPipelineRingQueue, 1) {
    bool (*Init)(LTMediaPipelineRingQueue * queue, u32 elementSize, u32 elementCount);
    /**< Initialize a Ring Queue
     *
     * @param queue         Pointer to the Ring Queue to initialize
     * @param elementSize   Size of each element in the queue
     * @param elementCount  Number of elements in the queue
     *
     * @return true if the queue was successfully initialized, false otherwise
     */

    void (*Destroy)(LTMediaPipelineRingQueue * queue);
    /**< Destroy a Ring Queue
     *  Frees all memory associated with the queue.
     *
     * @param queue         Pointer to the Ring Queue to destroy
     */

    void* (*GetElementFromHead)(LTMediaPipelineRingQueue * queue, u32 elementIndex);
    /**< Get a pointer to an element in the queue at relative position from the head
     *
     * @param queue         Pointer to the Ring Queue to get the element from
     * @param elementIndex  Index of the element to get
     *
     * @return Pointer to the element at the specified index
     */

    LT_SIZE (*GetContiguousElements)(LTMediaPipelineRingQueue * queue, u32 elementsFromHead);
    /**< Get the number of elements before the data boundary from the element specified 
     * relative to the head. 
     * 
     * This function assists in wrap-around handling.
     *
     * @param queue             Pointer to the Ring Queue to get the element from
     * @param elementsFromHead  Number of elements from the head to the boundary
     *
     * @return Number of elements to the memory boundary from the specified location 
     */

    void (*AdvanceHead)(LTMediaPipelineRingQueue * queue, u32 elementCount);
    /**< Advance the head of the queue by the specified number of elements
     * 
     * This function zero-fills the advanced area
     * 
     * @param queue         Pointer to the Ring Queue to advance
     * @param elementCount  Number of elements to advance the head by
     */

} LTOBJECT_API;

#endif
