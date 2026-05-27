/****************************************************************************
 * platforms/si91x/source/si91x/driver/wireless/include/LTNetBuffer.h
 * Modified by Roku, Inc. Please see changelog below for more information.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ***************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTNETBUFFER_H
#define ROKU_LT_INCLUDE_LT_CORE_LTNETBUFFER_H

#include <lt/LTTypes.h>
#include <lt/core/LTTime.h>
LT_EXTERN_C_BEGIN

/* LTNet_buff may look like this::
 *
 *                                --------------------
 *                                | LTNet_buff       |
 *                                --------------------
 *                                |  + next          |
 *                                |  + prev          |
 *                                |  + len(data)     |
 *     ,---------------------------  + head          |
 *    /          ,-----------------  + data          |
 *   /          /      ,-----------  + tail          |
 *  |          |      |            , + end           |
 *  |          |      |           |
 *  v          v      v           v
 *   ------------------------------
 *  | headroom | data |  tailroom |
 *   ------------------------------
 * */

#define unlikely(x) (x)
struct LTNet_buff_; 

typedef bool (*LTNetBufferCompletionCb)(struct LTNet_buff_ *net_buff, void* context);
typedef struct LTNet_buff_ {
    struct LTNet_buff_ *next;
    struct LTNet_buff_ *prev;
    u32 len;
    u8* head;
    u8* data;
    u32 tail;
    u32 end;
    LTTime alloc_ts;
    LTNetBufferCompletionCb compl_cb;
    void *compl_cb_ctx;
    LTAtomic users;
} LTNet_buff;

typedef struct {
    char name[32];
    u32 block_size;
    u32 block_count;
    u32 free_count;
    u64 num_alloc;
    u64 num_free;
    u32 alloc_rate_max;
    u16  window_msec;
    u32 zero_buffers;
} LTNetBufferMemPoolStats;

typedef struct {
    u32 num_pool;
    LTNetBufferMemPoolStats pool_stats[5];
} LTNetBufferMemStats;

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTNetBuffer, 1);

struct LTNetBufferApi {
    INHERIT_LIBRARY_BASE
    /**
     * @brief Alloc will allocate the LTNetBuffer object with the given size.
     * @param size Size of the buffer to be allocated.
     * @return LTNet_buff* Pointer to the allocated buffer.
     */
    LTNet_buff* (*Alloc)(u32 size);

    /**
     * @brief Free will free the LTNetBuffer object.
     * @param net_buff Pointer to the buffer to be freed.
     */
    void (*Free)(LTNet_buff *net_buff);

    /**
     * @brief To prepend data to the beginning of the current 
     *        data in the LTNet_buff by moving the data pointer 
     *        backward (toward the head pointer), reducing
     *        the headroom and making space for new data.
     * @param net_buff Pointer to the buffer.
     * @param len Length of the data to be pushed.
     * @return void* Pointer to the data.
     */
    void* (*Push)(LTNet_buff *net_buff, u32 len);

    /**
     * @brief To remove data from the beginning of the current
     *        data in the sk_buff by moving the data pointer
     *        forward (toward the tail pointer), increasing
     *        the headroom and effectively discarding the
     *        specified number of bytes.
     * @param net_buff Pointer to the buffer.
     * @param len Length of the data to be pulled.
     * @return void* Pointer to the data.
     */
    void* (*Pull)(LTNet_buff *net_buff, u32 len);

    /**
     * @brief	Increase the headroom of an empty &LTNet_buff by reducing the tail
     * 	        room. This is only allowed for an empty buffer.
     * @param net_buff Pointer to the buffer.
     * @param len Length of the data to be reserved.
     * @return void
     */
    void (*Reserve)(LTNet_buff *net_buff, u32 len);

    /**
     * @brief Makes another reference to the LTNet_buff. Returns the pointer to the same buffer.
     * @param net_buff Pointer to the buffer.
     * @return LTNet_buff* Pointer to the buffer.
     */
    LTNet_buff* (*Get)(LTNet_buff* net_buff);

    /**
     * @brief Appends data to the buffer by advancing the tail pointer 
     *        forward by the specified length, provided there's enough space..
     * @param net_buff Pointer to the buffer.
     * @return void
     */
    void* (*Put)(LTNet_buff *net_buff, u32 len);

    /**
     * @brief To remove data from the end of the 
     *        data in the sk_buff by moving the tail pointer
     *        backwards (toward the data pointer), increasing
     *        the tailroom and effectively discarding the
     *        specified number of bytes.
     * @param net_buff Pointer to the buffer.
     * @return void* Pointer to the data.
     */
    void* (*Trim)(LTNet_buff *net_buff, u32 len);

    /**
     * @brief Get the tailroom of the buffer.
     * @param net_buff Pointer to the buffer.
     * @return u32 Tailroom of the buffer.
     */
    u32 (*GetTailroom)(LTNet_buff *net_buff);

    /**
     * @brief Get the headroom of the buffer.
     * @param net_buff Pointer to the buffer.
     * @return u32 Headroom of the buffer.
     */
    u32 (*GetHeadroom)(LTNet_buff *net_buff);

    /**
     * @brief Insert a new node after the given node.
     * @param node Pointer to the node after which the new node is to be inserted.
     * @param new_node Pointer to the new node to be inserted.
     * @return bool True if the new node is inserted successfully, false otherwise.
     */
    bool (*InsertAfter)(LTNet_buff *node, LTNet_buff *new_node);

    /**
     * @brief Insert a new node before the given node.
     * @param node Pointer to the node before which the new node is to be inserted.
     * @param new_node Pointer to the new node to be inserted.
     * @return bool True if the new node is inserted successfully, false otherwise.
     */
    bool (*InsertBefore)(LTNet_buff *node, LTNet_buff *new_node);

    /**
     * @brief Delete the given node.
     * @param node Pointer to the node to be deleted.
     * @return void
     */
    void (*DeleteNode)(LTNet_buff *node);

    /**
     * @brief Perfroems a deep copy of the given LTNet_buff.
     * @param net_buff Pointer to the buffer to be copied.
     * @return LTNet_buff* Pointer to the copied buffer.
     */
    LTNet_buff* (*Copy)(LTNet_buff *net_buff);

    /**
     * @brief Set the completion callback for the given LTNet_buff.
     * @param net_buff Pointer to the buffer.
     * @param cb Callback function to be set.
     * @param context Data to be passed to the callback function.
     * @param data Data to be passed to the callback function.
     * @return void
     */
    void (*SetBufferCompletionCb)(LTNet_buff *net_buff, LTNetBufferCompletionCb cb, void *context);

    /**
     * @brief Get the memory statistics of the LTNetBuffer.
     * @param stats Pointer to the LTNetBufferMemStats structure to be filled.
     * @return void
     */
    void (*GetMemStats)(LTNetBufferMemStats *stats);
};

LT_EXTERN_C_END
#endif  // #define ROKU_LT_INCLUDE_LT_CORE_LTNETBUFFER_H

/************************************************************************************
 * LOG 
 *************************************************************************************
 *  25-Aug-24   galba       created
 */
