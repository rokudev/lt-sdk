/*****************************************************************************************
 * Memory Pool API implementation.
 * 
 * lt/platforms/si91x/source/si91x/driver/wireless/common/LTMemoryPool.c
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTMemoryPool.h>
#define P  LT_GetCore()->ConsoleStomp
#if defined(DEBUG_ASSERT)
#define DEBUG_ASSERT_MEMPOOL(x) LT_ASSERT(x) 
#else
#define DEBUG_ASSERT_MEMPOOL(x)
#endif
#define LT_MEM_POOL_OUT_OF_MEMORY     0xFFFFFFFF
#define LTMEM_POOL_TIME_ALLOC_MSEC    50

typedef_LTObjectImpl(LTMemoryPool, LTMemoryPoolImpl) {
    void *flatbuffer;
    u32 buffersize;
    u32 blockSize;
    u32 blockCount;
    u64 alloccnt;
    u64 freecnt;
    LTTime anchor_start_time;
    u32 anchor_start_time_count;
    u32 anchor_start_time_count_max;
    u32 zerobuffer;
    void *freeBlockAddr;
} LTOBJECT_API;

static bool LTMemoryPoolImpl_ConstructObject(LTMemoryPoolImpl *mempool) {
    mempool->flatbuffer = NULL;
    mempool->buffersize = 0;
    mempool->blockSize = 0;
    mempool->blockCount = 0;
    mempool->alloccnt = 0;
    mempool->freecnt = 0;
    mempool->freeBlockAddr = NULL;
    mempool->anchor_start_time = LT_GetCore()->GetKernelTime();
    mempool->anchor_start_time_count = 0;
    mempool->anchor_start_time_count_max = 0;
    mempool->zerobuffer = 0;
    return true;
}

static void LTMemoryPoolImpl_DestructObject(LTMemoryPoolImpl *mempool) {
    LT_UNUSED(mempool);
    DEBUG_ASSERT_MEMPOOL(mempool->flatbuffer == NULL);
    return;
}

static bool LTMemoryPoolImpl_Create(LTMemoryPool *pool, u32 blockSize, u32 blockCount, void *buffer, u32 buffersize) {
    LTMemoryPoolImpl *impl = (LTMemoryPoolImpl *)pool;
    if (!buffer || !blockSize || !blockCount || buffersize<(blockCount * (blockSize + LT_MEM_POOL_REQUIRED_PADDING(blockSize)))) {
        return false;
    }
    impl->flatbuffer = buffer;
    impl->blockSize = blockSize + LT_MEM_POOL_REQUIRED_PADDING(blockSize);
    impl->blockCount = blockCount;

    impl->freeBlockAddr = impl->flatbuffer;
    LT_SIZE blockAddr = (LT_SIZE)impl->flatbuffer;
    //Populate the list of free blocks, just leave out the last block
    for (LT_SIZE i = 0 ; i < (blockCount - 1); i++) {
        *(LT_SIZE *)blockAddr = (LT_SIZE)(blockAddr + impl->blockSize);
        blockAddr += impl->blockSize;
    }
    //Last element will indicate OOM
    *(LT_SIZE *)blockAddr = LT_MEM_POOL_OUT_OF_MEMORY;
    return true;
}

static bool LTMemoryPoolImpl_Alloc(LTMemoryPool *pool, void **block) {
    LTMemoryPoolImpl *impl = (LTMemoryPoolImpl *)pool;
    if ((LT_SIZE)impl->freeBlockAddr == LT_MEM_POOL_OUT_OF_MEMORY) {
        impl->zerobuffer++;
        return false;
    }
    if (!impl->freeBlockAddr) {
        DEBUG_ASSERT_MEMPOOL(0);
        return false;
    }
    void *blockAddr = impl->freeBlockAddr;
    impl->freeBlockAddr = (void *)(*(LT_SIZE *)blockAddr);
    *block = blockAddr;
    impl->alloccnt++;
#if defined(DEBUG_MEMPOOL_PROF)
    /* Stats Calc */
    LTTime now = LT_GetCore()->GetKernelTime();
    if (LTTime_GetMilliseconds(LTTime_Subtract(now, impl->anchor_start_time))< LTMEM_POOL_TIME_ALLOC_MSEC) {
        impl->anchor_start_time_count++;
        if (impl->anchor_start_time_count > impl->anchor_start_time_count_max) {
            impl->anchor_start_time_count_max = impl->anchor_start_time_count;
        }
    } else {
        impl->anchor_start_time = now;
        impl->anchor_start_time_count = 1;
    }
#endif
    return true;
}

static void LTMemoryPoolImpl_Free(LTMemoryPool *pool, void *block) {
    LTMemoryPoolImpl *impl = (LTMemoryPoolImpl *)pool;
    DEBUG_ASSERT_MEMPOOL(impl);
    if (!block) {
        return;
    }
    DEBUG_ASSERT_MEMPOOL((block>=impl->flatbuffer) && ((u32)block <= ((u32)impl->flatbuffer + (impl->blockSize * impl->blockCount))));
    *(LT_SIZE *)block = (LT_SIZE)impl->freeBlockAddr;
    impl->freeBlockAddr = block;
    DEBUG_ASSERT_MEMPOOL(impl->freeBlockAddr);

    impl->freecnt++;
    return;
}

static void LTMemoryPoolImpl_GetStats(LTMemoryPool *pool, u32 *blockSize, u32 *blockCount,
                                      u32 *freeBlocks, u64 *allocCount, u64 *freeCount, u32 *alloc_rate_max,
                                      u16* alloc_rate_window_msec, u32 *zeroBuffers) {
    LTMemoryPoolImpl *impl = (LTMemoryPoolImpl *)pool;
    *blockSize = impl->blockSize;
    *blockCount = impl->blockCount;
    u32 count = 0;
    void *blockAddr = impl->freeBlockAddr;
    while ((LT_SIZE)blockAddr != LT_MEM_POOL_OUT_OF_MEMORY) {
        count++;
        blockAddr = (void *)(*(LT_SIZE *)blockAddr);
    }
    *freeBlocks = count;
    *allocCount = impl->alloccnt;
    *freeCount = impl->freecnt;
    *alloc_rate_max = impl->anchor_start_time_count_max;
    *alloc_rate_window_msec = LTMEM_POOL_TIME_ALLOC_MSEC;
    *zeroBuffers = impl->zerobuffer;
    return;
}

static void LTMemoryPoolImpl_Destroy(LTMemoryPool *pool) {
    LTMemoryPoolImpl *impl = (LTMemoryPoolImpl *)pool;
    impl->flatbuffer = NULL;
    impl->buffersize = 0;
    impl->blockSize = 0;
    impl->blockCount = 0;
    impl->freeBlockAddr = NULL;
    return;
}


define_LTObjectImplPublic(LTMemoryPool, LTMemoryPoolImpl,
    Create, Destroy, Alloc, Free, GetStats
);

define_LTOBJECT_EXPORTLIBRARY(LTMemoryPool, 1, LTMemoryPoolImpl);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  23-Aug-24   galba       created
 */