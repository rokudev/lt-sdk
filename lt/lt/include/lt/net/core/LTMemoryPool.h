/************************************************************************
 * Memory pool implementation
 * 
 * This file implements the memory pool API.
 * 
 * platforms/si91x/source/si91x/driver/wireless/include/LTMemoryPool.h
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTMEMPOOL_H
#define ROKU_LT_INCLUDE_LT_CORE_LTMEMPOOL_H
#include <lt/LTObject.h>
LT_EXTERN_C_BEGIN
#define LT_MEM_POOL_REQUIRED_PADDING(obj_size) (((sizeof(LT_SIZE) - ((obj_size) % sizeof(LT_SIZE))) % sizeof(LT_SIZE)))
/***********************
 * LTMemoryPool Object
***********************/
typedef_LTObject(LTMemoryPool, 1) {
    bool (* Create)(LTMemoryPool *pool, u32 blockSize, u32 blockCount, void *buffer, u32 buffersize);
    void (* Destroy)(LTMemoryPool *pool);
    bool (* Alloc) (LTMemoryPool *pool, void **block);
    void (* Free) (LTMemoryPool *pool, void *block);
    void (* GetStats) (LTMemoryPool *pool, u32 *blockSize, u32 *blockCount, u32 *freeBlocks, u64 *allocCount, u64 *freeCount, u32 *allocRateMax, u16* allocRateWindowMsec, u32 *zeroBuffers);
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_LTMEMPOOL_H */

/************************************************************************
 *  LOG
 ************************************************************************
 *  22-Aug-24   galba       created
 ************************************************************************/