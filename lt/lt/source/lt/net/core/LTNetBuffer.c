/****************************************************************************
 * platforms/si91x/source/si91x/driver/wireless/common/LTNetBuffer.c
 * Modified by Roku, Inc. Please see changelog below for more information.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ***************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/net/core/LTMemoryPool.h>
#include <lt/net/core/LTNetBuffer.h>

DEFINE_LTLOG_SECTION("lt.net.buf");
//#define P  LT_GetCore()->ConsoleStomp
#define P(...) 
#if defined(DEBUG_ASSERT)
#define DEBUG_ASSERT_LTNETBUF(x) LT_ASSERT(x) 
#else
#define DEBUG_ASSERT_LTNETBUF(x)
#endif
#if defined(DEBUG_NETBUF_SANTIZER)
#define SANITIZE_LTNETBUF(x) LTNetBuffer_Santizer(x, __FUNCTION__)
#else
#define SANITIZE_LTNETBUF(x)
#endif
#define THREAD_SAFE
#define THREAD_UNSAFE
#define FAST_PATH
#define SLOW_PATH
static struct Statics {
    LTCore       *core;
    LTMutex      *mutex;
    LTMemoryPool *pool_ltnet_buff;
    u8           *mem_for_ltnetbuf_pool;
    LTMemoryPool *pool_small;
    u8           *mem_for_small_pool;
    LTMemoryPool *pool_large;
    u8           *mem_for_large_pool;
    LTMemoryPool *pool_medium;
    u8           *mem_for_medium_pool;
} S;
typedef enum {
    ZERO,
    SMALL,
    MEDIUM,
    LARGE,
    LTNETBUFF,
    INVALID
} PoolType;

typedef struct {
    PoolType type;
    LTMemoryPool *pool;
} LTNetBuffPool_t;

 static inline LTNetBuffPool_t size_to_ltmempool(u32 size) {
    u32 asize = (size + LT_MEM_POOL_REQUIRED_PADDING(size));
    LTNetBuffPool_t ret_pool = {INVALID, NULL};
    if (!asize) {
        ret_pool.type = ZERO;
        ret_pool.pool = NULL;
    } else if (asize <= LTNETBUFF_SMALL_POOL_BLOCK_SIZE) {
        ret_pool.pool = S.pool_small;
        ret_pool.type = SMALL;
    } else if (asize <= LTNETBUFF_MEDIUM_POOL_BLOCK_SIZE) {
        ret_pool.pool = S.pool_medium;
        ret_pool.type = MEDIUM;
    } else if (asize <= LTNETBUFF_LARGE_POOL_BLOCK_SIZE) {
        ret_pool.pool = S.pool_large;
        ret_pool.type = LARGE;
    } else {
        P("Invalid Pool Size for block size size:%lu asize:%lu\n", LT_Pu32(size), LT_Pu32(asize));
    }
    P("size_to_ltmempool: size:%lu asize:%lu pool:%p type:%d\n", LT_Pu32(size), LT_Pu32(asize), ret_pool.pool, ret_pool.type);
    return ret_pool;
 }

static inline LTMemoryPool* pool_type_to_ltmempool(PoolType type) {
    switch (type) {
        case SMALL:
            return S.pool_small;
        case MEDIUM:
            return S.pool_medium;
        case LARGE:
            return S.pool_large;
        case LTNETBUFF:
            return S.pool_ltnet_buff;
        default:
            return NULL;
    }
}

static inline bool LTNetBuffer_Santizer(LTNet_buff *net_buff, const char* funcname) {
    LT_UNUSED(funcname);
    P("%s: Sanitizing LTNetBuffer\n", funcname);
    if (!net_buff) {
        LT_GetCore()->ConsoleStomp("%s: Sanitizing LTNetBuffer: net_buff is NULL\n", funcname);
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }
    if (net_buff->head > net_buff->data) {
        LT_GetCore()->ConsoleStomp("%s: Sanitizing LTNetBuffer: head(%p) > data(%p)\n", funcname, net_buff->head, net_buff->data);
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }
    if (net_buff->data > net_buff->head + net_buff->end) {
        LT_GetCore()->ConsoleStomp("%s: Sanitizing LTNetBuffer: data(%p) > head(%p) + end(%lu)\n", funcname, net_buff->data, net_buff->head, LT_Pu32(net_buff->end));
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }

    if (net_buff->tail > net_buff->end) {
        LT_GetCore()->ConsoleStomp("%s: Sanitizing LTNetBuffer: tail(%lu) > end(%lu)\n", funcname, LT_Pu32(net_buff->tail), LT_Pu32(net_buff->end));
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }

    if (net_buff->len > net_buff->end) {
        LT_GetCore()->ConsoleStomp("%s: Sanitizing LTNetBuffer: len(%lu) > end(%lu)\n", funcname, LT_Pu32(net_buff->len), LT_Pu32(net_buff->end));
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }

    return true;
}

static void LTNetBuffer_Free(LTNet_buff *net_buff) THREAD_SAFE SLOW_PATH {
    // Check for the NULL Pointer
    if (!net_buff) {
        return;
    }
    if (LTAtomic_FetchSubtract(&net_buff->users, 1) > 1) {
        return;
    }

    SANITIZE_LTNETBUF(net_buff);
    //Get the data header from the head
    u8* pdata = (u8*)net_buff->head;

    //Get the pool type from the data header
    LTMemoryPool* pool = pool_type_to_ltmempool(size_to_ltmempool(net_buff->end).type);

    S.mutex->API->Lock(S.mutex);
    // Free the data from the respective pool
    if (pdata && pool) {
        pool->API->Free(pool, pdata);
    }
    net_buff->head = net_buff->data = NULL;
    net_buff->tail = net_buff->end = 0;
    net_buff->len = 0;
    //Free the netbuff from the LTNetBuff pool
    S.pool_ltnet_buff->API->Free(S.pool_ltnet_buff, net_buff);
    S.mutex->API->Unlock(S.mutex);
}

static bool LTNetBufferImpl_BufferCompletion(LTNet_buff *net_buff, void *ctx) THREAD_SAFE SLOW_PATH {
    LT_UNUSED(ctx);
    if (!net_buff) {
        return false;
    }
    P("LTNetBufferImpl_BufferCompletion: net_buff:%p\n", net_buff);
    LTNetBuffer_Free(net_buff);
    return true;
}

static LTNet_buff* LTNetBuffer_Alloc(u32 size) THREAD_SAFE SLOW_PATH {
    LTNet_buff *net_buff = NULL;
    void *pdata = NULL;
    u8* data = NULL;
    S.mutex->API->Lock(S.mutex);
    //Find the pool which is suitable for the size
    LTNetBuffPool_t pooldata = size_to_ltmempool(size);
    if (pooldata.pool) {
        //Allocate the buffer from the pool
        if (!pooldata.pool->API->Alloc(pooldata.pool, (void**)&pdata)) {
            net_buff = NULL;
            goto done;
        }
        if (!pdata) {
            //This is weird, we should not get NULL here
            LTLOG_REDALERT_AND_REBOOT("alloc.fatal","LTNetBuffer_Alloc: pdata is NULL");
            DEBUG_ASSERT_LTNETBUF(0);
            goto done;
        }
        data = (u8*)pdata;
    } else {
        if (pooldata.type == INVALID) {
            LTLOG_REDALERT("alloc.fatal.invalid.pool","LTNetBuffer_Alloc: Invalid Pool Type");
            goto done;
        }
    }

    //Allocate the Meta Data from the pool
    if (!S.pool_ltnet_buff->API->Alloc(S.pool_ltnet_buff, (void**)&net_buff)) {
        LTLOG_REDALERT("alloc.fatal","LTNetBuffer_Alloc: net_buff is NULL");
        if (pdata) pooldata.pool->API->Free(pooldata.pool, pdata);
        net_buff = NULL;
        goto done;
    }

    if (!net_buff) {
        //This is weird, we should not get NULL here
        LTLOG_REDALERT("alloc.fatal","LTNetBuffer_Alloc: net_buff is NULL");
        if(pdata) pooldata.pool->API->Free(pooldata.pool, pdata);
        DEBUG_ASSERT_LTNETBUF(0);
        net_buff = NULL;
        goto done;
    }

    //Set the next, tail, head, data, tail and end
    net_buff->next = NULL;
    net_buff->prev = NULL;
    net_buff->len = 0;
    net_buff->head = data;
    net_buff->data = data;
    net_buff->tail = 0;
    net_buff->end = size;
    net_buff->compl_cb = LTNetBufferImpl_BufferCompletion;
    LTAtomic_Store(&net_buff->users, 1);
    net_buff->alloc_ts = S.core->GetKernelTime();

done:
    S.mutex->API->Unlock(S.mutex);
    if(net_buff) SANITIZE_LTNETBUF(net_buff);

    //Return the buffer
    return net_buff;
}

static void* LTNetBuffer_Push(LTNet_buff *net_buff, u32 len) THREAD_UNSAFE FAST_PATH {
    SANITIZE_LTNETBUF(net_buff);
    if (unlikely(net_buff->data - len < net_buff->head) ) {
        LTLOG_REDALERT_AND_REBOOT("push.fatal", "Fatal error, crashing");
        DEBUG_ASSERT_LTNETBUF(0);
        return NULL;
    }
    net_buff->data -= len;
    net_buff->len += len;
    return net_buff->data;
}

static void* LTNetBuffer_Pull(LTNet_buff *net_buff, u32 len) THREAD_UNSAFE FAST_PATH {
    SANITIZE_LTNETBUF(net_buff);
    if (unlikely(net_buff->len < len)) {
        LTLOG_REDALERT_AND_REBOOT("pull.fatal", "Fatal error, crashing net_buff->len=%lu len=%lu\n", LT_Pu32(net_buff->len), LT_Pu32(len));
        DEBUG_ASSERT_LTNETBUF(0);
        return NULL;
    }
    net_buff->data += len;
    net_buff->len -= len;
    return net_buff->data;
}


static void LTNetBuffer_Reserve(LTNet_buff *net_buff, u32 len) THREAD_UNSAFE FAST_PATH {
    SANITIZE_LTNETBUF(net_buff);
    if (unlikely(net_buff->data + len > net_buff->head + net_buff->end)) {
        LTLOG_REDALERT_AND_REBOOT("reserve.fatal", "%s %p Fatal error, crashing", __FUNCTION__, net_buff);
        DEBUG_ASSERT_LTNETBUF(0);
        return;
    }
    net_buff->data += len;
    net_buff->tail += len;
}

static LTNet_buff* LTNetBuffer_Get(LTNet_buff *net_buff) THREAD_SAFE FAST_PATH {
    SANITIZE_LTNETBUF(net_buff);
    LTAtomic_FetchAdd(&net_buff->users, 1);
    return net_buff;
}

static void* LTNetBuffer_Put(LTNet_buff *net_buff, u32 len) THREAD_UNSAFE FAST_PATH {
    SANITIZE_LTNETBUF(net_buff);
    void *tail_ptr = net_buff->head + net_buff->tail;
    if (unlikely(net_buff->tail + len > net_buff->end)) {
        LTLOG_REDALERT_AND_REBOOT("put.fatal", "%s %p Fatal error, crashing", __FUNCTION__, net_buff);
        DEBUG_ASSERT_LTNETBUF(0);
        return NULL;
    }
    net_buff->tail += len;
    net_buff->len += len;
    return tail_ptr;
}

static void* LTNetBuffer_Trim(LTNet_buff *net_buff, u32 len) THREAD_UNSAFE FAST_PATH {
    SANITIZE_LTNETBUF(net_buff);
    if (unlikely(net_buff->len < len)) {
        LTLOG_REDALERT_AND_REBOOT("trim.fatal", "%s %p Fatal error, crashing", __FUNCTION__, net_buff);
        DEBUG_ASSERT_LTNETBUF(0);
        return NULL;
    }
    net_buff->tail -= len;
    net_buff->len -= len;
    return net_buff->head + net_buff->tail;
}

static u32 LTNetBuffer_GetTailroom(LTNet_buff *net_buff) THREAD_UNSAFE FAST_PATH {
    SANITIZE_LTNETBUF(net_buff);
    return net_buff->end - net_buff->tail;
}

static u32 LTNetBuffer_GetHeadroom(LTNet_buff *net_buff) THREAD_UNSAFE FAST_PATH {
    SANITIZE_LTNETBUF(net_buff);
    return net_buff->data - net_buff->head;
}

static bool LTNetBuffer_InsertAfter(LTNet_buff *node, LTNet_buff *new_node) THREAD_SAFE SLOW_PATH {
    if (!node || !new_node) {
        return false;
    }
    SANITIZE_LTNETBUF(node);
    SANITIZE_LTNETBUF(new_node);
    S.mutex->API->Lock(S.mutex);
    new_node->next = node->next;
    new_node->prev = node;
    if (node->next) {
        node->next->prev = new_node;
    }
    node->next = new_node;
    S.mutex->API->Unlock(S.mutex);
    return true;
}

static bool LTNetBuffer_InsertBefore(LTNet_buff *node, LTNet_buff *new_node) THREAD_SAFE SLOW_PATH {
    if (!node || !new_node) {
        return false;
    }
    SANITIZE_LTNETBUF(node);
    SANITIZE_LTNETBUF(new_node);
    S.mutex->API->Lock(S.mutex);
    new_node->next = node;
    new_node->prev = node->prev;
    if (node->prev) {
        node->prev->next = new_node;
    }
    node->prev = new_node;
    S.mutex->API->Unlock(S.mutex);
    return true;
}

static void LTNetBuffer_DeleteNode(LTNet_buff *node) THREAD_SAFE SLOW_PATH {
    if (!node) {
        return;
    }
    SANITIZE_LTNETBUF(node);
    S.mutex->API->Lock(S.mutex);
    LTNet_buff *next = node->next;
    LTNet_buff *prev = node->prev;
    if (prev) {
        prev->next = next;
    }
    if (next) {
        next->prev = prev;
    }
    node->next = node->prev = NULL;
    S.mutex->API->Unlock(S.mutex);
    return;
}

static LTNet_buff* LTNetBuffer_Copy(LTNet_buff *net_buff) THREAD_SAFE SLOW_PATH {
    if (!net_buff) {
        return NULL;
    }
    SANITIZE_LTNETBUF(net_buff);
    S.mutex->API->Lock(S.mutex);
    // Get the size from net_buff
    // Get the head from net_buff
    // Allocate the new Data buffer from the respective pool
    // Allocate the new LTNet_buff from the LTNetBuff pool
    LTNet_buff *new_net_buff = LTNetBuffer_Alloc(net_buff->end);

    if (!new_net_buff) {
        S.mutex->API->Unlock(S.mutex);
        return NULL;
    }
    new_net_buff->len = net_buff->len;
    new_net_buff->tail = net_buff->tail;
    new_net_buff->end = net_buff->end;
    u32 headroom = net_buff->data - net_buff->head;
    new_net_buff->data = new_net_buff->head + headroom;
    // Set the data header for the new LTNetbuff
    // Now copy the headroom+data+tailroom from the old buffer to the new buffer
    lt_memcpy(new_net_buff->head, net_buff->head, new_net_buff->tail);

    S.mutex->API->Unlock(S.mutex);
    // Return the new LTNet_buff
    SANITIZE_LTNETBUF(new_net_buff);
    return new_net_buff;
}

static void LTNetBuffer_SetBufferCompletionCb(LTNet_buff *net_buff, LTNetBufferCompletionCb cb, void *context) THREAD_SAFE SLOW_PATH {
    if (!net_buff) {
        return;
    }
    SANITIZE_LTNETBUF(net_buff);
    S.mutex->API->Lock(S.mutex);
    net_buff->compl_cb = cb;
    net_buff->compl_cb_ctx = context;
    S.mutex->API->Unlock(S.mutex);
}

static void LTNetBuffer_GetMemStats(LTNetBufferMemStats *mem_pool_stats) {
    if (!mem_pool_stats) {
        return;
    }
    S.mutex->API->Lock(S.mutex);
    mem_pool_stats->num_pool = 4;
    LTNetBufferMemPoolStats *pool_stats = mem_pool_stats->pool_stats;
    S.pool_ltnet_buff->API->GetStats(S.pool_ltnet_buff, &pool_stats[0].block_size, &pool_stats[0].block_count, &pool_stats[0].free_count, 
                                     &pool_stats[0].num_alloc, &pool_stats[0].num_free, &pool_stats[0].alloc_rate_max,
                                     &pool_stats[0].window_msec, &pool_stats[0].zero_buffers);
    lt_snprintf(pool_stats[0].name, sizeof(pool_stats[0].name), "LTNetBuffPool");
    S.pool_small->API->GetStats(S.pool_small, &pool_stats[1].block_size, &pool_stats[1].block_count, &pool_stats[1].free_count, 
                                 &pool_stats[1].num_alloc, &pool_stats[1].num_free, &pool_stats[1].alloc_rate_max,
                                 &pool_stats[1].window_msec, &pool_stats[1].zero_buffers);
    lt_snprintf(pool_stats[1].name, sizeof(pool_stats[1].name), "SmallPool");
    S.pool_medium->API->GetStats(S.pool_medium, &pool_stats[2].block_size, &pool_stats[2].block_count, &pool_stats[2].free_count, 
                                  &pool_stats[2].num_alloc, &pool_stats[2].num_free, &pool_stats[2].alloc_rate_max,
                                  &pool_stats[2].window_msec, &pool_stats[2].zero_buffers);
    lt_snprintf(pool_stats[2].name, sizeof(pool_stats[2].name), "MediumPool");
    S.pool_large->API->GetStats(S.pool_large, &pool_stats[3].block_size, &pool_stats[3].block_count, &pool_stats[3].free_count, 
                                 &pool_stats[3].num_alloc, &pool_stats[3].num_free, &pool_stats[3].alloc_rate_max,
                                 &pool_stats[3].window_msec, &pool_stats[3].zero_buffers);
    lt_snprintf(pool_stats[3].name, sizeof(pool_stats[3].name), "LargePool");
    S.mutex->API->Unlock(S.mutex);
    return;
}

static bool LTNetBufferImpl_LibInit(void) {
    S = (struct Statics){};
    S.core = LT_GetCore();
    S.mutex = lt_createobject(LTMutex);
    if (!S.mutex) {
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }
    S.pool_ltnet_buff = lt_createobject(LTMemoryPool);
    if (!S.pool_ltnet_buff) {
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }
    u32 block_size = sizeof(LTNet_buff) + LT_MEM_POOL_REQUIRED_PADDING(sizeof(LTNet_buff));
    S.mem_for_ltnetbuf_pool = lt_malloc(block_size*LTNETBUFF_META_POOL_BLOCK_COUNT);
    if (!S.mem_for_ltnetbuf_pool) {
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }
    bool ret = S.pool_ltnet_buff->API->Create( S.pool_ltnet_buff, block_size, 
                                    LTNETBUFF_META_POOL_BLOCK_COUNT,
                                    S.mem_for_ltnetbuf_pool,
                                    block_size * LTNETBUFF_META_POOL_BLOCK_COUNT);
    if (!ret) {
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }

    S.pool_small = lt_createobject(LTMemoryPool);
    if (!S.pool_small) {
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }
    block_size = LTNETBUFF_SMALL_POOL_BLOCK_SIZE + LT_MEM_POOL_REQUIRED_PADDING(LTNETBUFF_SMALL_POOL_BLOCK_SIZE);
    S.mem_for_small_pool = lt_malloc(LTNETBUFF_SMALL_POOL_BLOCK_COUNT*block_size);
    if (!S.mem_for_small_pool) {
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }
    ret = S.pool_small->API->Create(S.pool_small, block_size,
                                    LTNETBUFF_SMALL_POOL_BLOCK_COUNT,
                                    S.mem_for_small_pool,
                                    LTNETBUFF_SMALL_POOL_BLOCK_COUNT*block_size);
    if (!ret) {
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }

    S.pool_large = lt_createobject(LTMemoryPool);
    if (!S.pool_large) {
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }
    block_size = LTNETBUFF_LARGE_POOL_BLOCK_SIZE + LT_MEM_POOL_REQUIRED_PADDING(LTNETBUFF_LARGE_POOL_BLOCK_SIZE);
    S.mem_for_large_pool = lt_malloc(LTNETBUFF_LARGE_POOL_BLOCK_COUNT*block_size);
    if (!S.mem_for_large_pool) {
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }
    ret = S.pool_large->API->Create(S.pool_large, block_size,
                                    LTNETBUFF_LARGE_POOL_BLOCK_COUNT,
                                    S.mem_for_large_pool,
                                    LTNETBUFF_LARGE_POOL_BLOCK_COUNT*block_size);
    if (!ret) {
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }

    S.pool_medium = lt_createobject(LTMemoryPool);
    if (!S.pool_medium) {
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }
    block_size = LTNETBUFF_MEDIUM_POOL_BLOCK_SIZE + LT_MEM_POOL_REQUIRED_PADDING(LTNETBUFF_MEDIUM_POOL_BLOCK_SIZE);
    S.mem_for_medium_pool = lt_malloc(LTNETBUFF_MEDIUM_POOL_BLOCK_COUNT*block_size);
    if (!S.mem_for_medium_pool) {
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }
    ret = S.pool_medium->API->Create(S.pool_medium, block_size,
                                    LTNETBUFF_MEDIUM_POOL_BLOCK_COUNT,
                                    S.mem_for_medium_pool,
                                    LTNETBUFF_MEDIUM_POOL_BLOCK_COUNT*block_size);
    if (!ret) {
        DEBUG_ASSERT_LTNETBUF(0);
        return false;
    }
    return true;
}

static void LTNetBufferImpl_LibFini(void) {
    if (S.pool_ltnet_buff) {
        S.pool_ltnet_buff->API->Destroy(S.pool_ltnet_buff);
        lt_free(S.mem_for_ltnetbuf_pool);
        lt_destroyobject(S.pool_ltnet_buff);
    }
    if (S.pool_small) {
        S.pool_small->API->Destroy(S.pool_small);
        lt_free(S.mem_for_small_pool);
        lt_destroyobject(S.pool_small);
    }
    if (S.pool_large) {
        S.pool_large->API->Destroy(S.pool_large);
        lt_free(S.mem_for_large_pool);
        lt_destroyobject(S.pool_large);
    }
    if (S.pool_medium) {
        S.pool_medium->API->Destroy(S.pool_medium);
        lt_free(S.mem_for_medium_pool);
        lt_destroyobject(S.pool_medium);
    }
    lt_destroyobject(S.mutex);
    S = (struct Statics){};
}

/*______________________
 / Library Interfaces */
define_LTLIBRARY_ROOT_INTERFACE(LTNetBuffer)
    .Alloc                  = &LTNetBuffer_Alloc,
    .Free                   = &LTNetBuffer_Free,
    .Push                   = &LTNetBuffer_Push,
    .Pull                   = &LTNetBuffer_Pull,
    .Reserve                = &LTNetBuffer_Reserve,
    .Get                    = &LTNetBuffer_Get,
    .Put                    = &LTNetBuffer_Put,
    .Trim                   = &LTNetBuffer_Trim,
    .GetTailroom            = &LTNetBuffer_GetTailroom,
    .GetHeadroom            = &LTNetBuffer_GetHeadroom,
    .InsertAfter            = &LTNetBuffer_InsertAfter,
    .InsertBefore           = &LTNetBuffer_InsertBefore,
    .DeleteNode             = &LTNetBuffer_DeleteNode,
    .Copy                   = &LTNetBuffer_Copy,
    .SetBufferCompletionCb  = &LTNetBuffer_SetBufferCompletionCb,
    .GetMemStats            = &LTNetBuffer_GetMemStats
LTLIBRARY_DEFINITION;

/************************************************************************************
 * LOG 
 *************************************************************************************
 *  25-Aug-24   galba       created
 */
