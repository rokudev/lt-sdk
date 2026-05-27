/******************************************************************************
 * lt/source/lt/ltk/LTKAllocator.c                         LTK Memory Allocator
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTKernel.h"

// Align values up or down to nearest boundary (NOTE: mask = alignment - 1)
#define LTK_ALIGN32(val, mask)         (((u32)(val) + (mask)) & ~((u32)(mask)))
#define LTK_ALIGN_PTR(val, mask)       (((LT_SIZE)(val) + (mask)) & ~((LT_SIZE)(mask)))
#define LTK_ALIGN_PTR_DOWN(val, mask)  (((LT_SIZE)(val)) & ~((LT_SIZE)(mask)))

enum {
    kChunkAlignment  = 8,              /**< Alignment of memory returned from allocator */
    kCanaryValue     = 0xe711dead,
    kMinPayloadSize  = sizeof(LTKList)
};

typedef struct {
    LTKMutex mutex;
    LTKList freeList;
    u32 nBytesTotal;
    u32 nBytesFree;
    u32 nMinBytesFree;
    u16 nAlignMask;
    u16 nMinBlockSize;
} LTKHeap;

typedef struct {
    u32 nCanary;
#if LT_ARCHITECTURE_BITS == 64
    u32 pad;
#endif
    u32 nSizePrev;
    u32 nSize;
} HeapLink;

typedef struct {
    HeapLink link;
    union { // <--  Alignment guaranteed here
        LTKList freeLink;
        u8 payload[0];
    };
} HeapBlock;

LT_STATIC_ASSERT_SIZE_32_64(HeapLink, 12, 12)
LT_STATIC_ASSERT_SIZE_32_64(HeapBlock, 20, 32)

// Heap descriptor
static LTKHeap s_heap;

/* Region tracking for region-aware allocation.
 * Up to LTK_MAX_HEAP_REGIONS regions can be registered (in array order from the BSP).
 * LTKAlloc walks the unified free list and skips blocks falling inside an exclusive
 * region; LTKAllocFromRegion(region, ...) restricts the walk to a single region (region != 0). */
typedef struct {
    u8   *pStart;
    u8   *pEnd;          /**< One past last valid byte */
    bool  bExclusive;    /**< Skipped by default LTKAlloc walk */
} LTKHeapRegionInfo;
static LTKHeapRegionInfo s_regions[LTK_MAX_HEAP_REGIONS];
static u32               s_nRegions = 0;

/* Returns 0..s_nRegions-1 if p is inside a registered region, else LTK_MAX_HEAP_REGIONS. */
static u32 LTKHeap_FindRegion(const void *p) {
    for (u32 i = 0; i < s_nRegions; i++) {
        if ((const u8 *)p >= s_regions[i].pStart && (const u8 *)p < s_regions[i].pEnd) return i;
    }
    return LTK_MAX_HEAP_REGIONS;
}

void LTKHeapInitialize(void) {
    // Alignment must be a power of 2, and at a minimum should be the pointer
    //   size. Smallest block must fit a HeapLink, the free-list link and
    //   satisfy the alignment requirements of payload.
    u32 nAlignment = (kChunkAlignment > sizeof(void *)) ? kChunkAlignment : sizeof(void *);
    s_heap.nAlignMask = nAlignment - 1;
    LT_ASSERT((nAlignment & s_heap.nAlignMask) == 0);
    s_heap.nMinBlockSize = LTK_ALIGN32(kMinPayloadSize + sizeof(HeapLink), s_heap.nAlignMask);
    // Total size
    s_heap.nBytesTotal = 0;
    // Free counters
    s_heap.nBytesFree    = 0;
    s_heap.nMinBytesFree = 0;
    // Initialize free-list (explicit links)
    LTKList_Init(&s_heap.freeList);
    LTKMutexInitialize(&s_heap.mutex);
}

/* Sort block into free-list in increasing order of size */
static void AddBlockToFreeList(LTKList_Node * pNodeToAdd, u32 nSize) {
    LTKList_Node * pNode = s_heap.freeList.pNext;
    for (; pNode != &s_heap.freeList; pNode = pNode->pNext) {
        HeapBlock * pBlock = LT_CONTAINER_OF(pNode, HeapBlock, freeLink);
        if (pBlock->link.nSize >= nSize) break;
    }
    LTKList_InsertBefore(pNode, pNodeToAdd);
}

static u32 LTKHeapAddRegionInternal(u8 * pHeapRegion, u32 nSizeInBytes, bool bExclusive) {
    HeapLink * pLink = (HeapLink *) LTK_ALIGN_PTR(pHeapRegion + sizeof(HeapLink), s_heap.nAlignMask);
    HeapBlock * pBot = (HeapBlock *) (pLink - 1);
    pLink            = (HeapLink *) LTK_ALIGN_PTR_DOWN(pHeapRegion + nSizeInBytes, s_heap.nAlignMask);
    HeapBlock * pTop = (HeapBlock *) (pLink - 1);
    /* Initialize implicit links */
    pBot->link.nCanary   = kCanaryValue;
    pBot->link.nSizePrev = 0x1;
    pBot->link.nSize     = (u8 *) pTop - (u8 *) pBot;
    pTop->link.nCanary   = kCanaryValue;
    pTop->link.nSizePrev = pBot->link.nSize;
    pTop->link.nSize     = 0x1;

    u32 slot = LTK_MAX_HEAP_REGIONS;  /* sentinel: table full */
    LTKMutexLock(&s_heap.mutex);
    if (s_nRegions < LTK_MAX_HEAP_REGIONS) {
        slot = s_nRegions;
        s_regions[slot].pStart     = pHeapRegion;
        s_regions[slot].pEnd       = pHeapRegion + nSizeInBytes;
        s_regions[slot].bExclusive = bExclusive;
        s_nRegions++;
    }
    /* Add the block to the unified free list, except when the slot table is full and
     * the caller asked for an exclusive region. Doing so in that case would silently
     * leak the exclusivity contract: LTKHeap_FindRegion would return the sentinel for
     * these addresses and LTKAlloc_Internal would treat them as general-purpose memory. */
    if (slot < LTK_MAX_HEAP_REGIONS || !bExclusive) {
        s_heap.nBytesTotal   += nSizeInBytes;
        s_heap.nBytesFree    += pBot->link.nSize;
        s_heap.nMinBytesFree += pBot->link.nSize;
        AddBlockToFreeList(&pBot->freeLink, pBot->link.nSize);
    }
    LTKMutexUnlock(&s_heap.mutex);
    return slot;
}

void LTKHeapAddRegion(u8 * pHeapRegion, u32 nSizeInBytes) {
    (void)LTKHeapAddRegionInternal(pHeapRegion, nSizeInBytes, false);
}

u32 LTKHeapAddRegionEx(u8 * pHeapRegion, u32 nSizeInBytes, bool bExclusive) {
    return LTKHeapAddRegionInternal(pHeapRegion, nSizeInBytes, bExclusive);
}

bool LTKHeap_IsExclusiveByPtr(const void * p) {
    u32 r = LTKHeap_FindRegion(p);
    return (r < LTK_MAX_HEAP_REGIONS) && s_regions[r].bExclusive;
}

static void * LTKAlloc_Internal(LT_SIZE nSize, bool wantRegion, u32 targetRegion) {
    if (nSize < kMinPayloadSize) nSize = kMinPayloadSize;
    nSize = LTK_ALIGN32(nSize + sizeof(HeapLink), s_heap.nAlignMask);
    LTKMutexLock(&s_heap.mutex);
    HeapBlock * pBlock;
    {
        // Search sorted list: max(min_size, size) + link_size needs to fit
        LTKList_Node * pNode = s_heap.freeList.pNext;
        for (; pNode != &s_heap.freeList; pNode = pNode->pNext) {
            pBlock = LT_CONTAINER_OF(pNode, HeapBlock, freeLink);
            LT_ASSERT(pBlock->link.nCanary == kCanaryValue);
            if (pBlock->link.nSize >= nSize) {
                u32 r = LTKHeap_FindRegion(pBlock);
                if (wantRegion) {
                    if (r == targetRegion) break;
                } else if (r >= LTK_MAX_HEAP_REGIONS || !s_regions[r].bExclusive) {
                    break;
                }
            }
        }
        if (pNode == &s_heap.freeList) {
            LTKMutexUnlock(&s_heap.mutex);
            return NULL;
        }
        // Remove chosen block from free-list
        LTKList_Remove(pNode);
    }
    HeapBlock * pNextBlock = (HeapBlock *) ((u8 *) pBlock + pBlock->link.nSize);
    // Split the block if there is enough room left for a block of minimum size
    if (pBlock->link.nSize >= nSize + s_heap.nMinBlockSize) {
        u32 nNextBlockSize = pBlock->link.nSize - nSize;
        pNextBlock->link.nSizePrev = nNextBlockSize;
        // The new next block
        pNextBlock = (HeapBlock *) ((u8 *) pBlock + nSize);
        pNextBlock->link.nCanary = kCanaryValue;
        pNextBlock->link.nSize = nNextBlockSize;
        // Set size and mark allocation bit
        pNextBlock->link.nSizePrev = nSize + 1;
        pBlock->link.nSize = pNextBlock->link.nSizePrev;
        AddBlockToFreeList(&pNextBlock->freeLink, nNextBlockSize);
        s_heap.nBytesFree -= nSize;
    } else {
        // Use existing block
        s_heap.nBytesFree -= pNextBlock->link.nSizePrev;
        // Mark allocation bit
        pNextBlock->link.nSizePrev += 1;
        pBlock->link.nSize = pNextBlock->link.nSizePrev;
    }
    if (s_heap.nBytesFree < s_heap.nMinBytesFree)
        s_heap.nMinBytesFree = s_heap.nBytesFree;
    LTKMutexUnlock(&s_heap.mutex);
    return (void *) ((u8 *) pBlock + sizeof(HeapLink));
}

void * LTKReAlloc(void * _pBlock, LT_SIZE nNewSize) {
    if (!_pBlock) return LTKAlloc(nNewSize);
    if (!nNewSize) {
        LTKFree(_pBlock);
        return NULL;
    }
    LTKMutexLock(&s_heap.mutex);
    HeapBlock * pBlock = (HeapBlock *) ((u8 *) _pBlock - sizeof(HeapLink));
    // Check for canary value and potential double-free
    LT_ASSERT(pBlock->link.nCanary == kCanaryValue);
    LT_ASSERT(pBlock->link.nSize & 0x1);
    if (nNewSize < kMinPayloadSize) nNewSize = kMinPayloadSize;
    nNewSize = LTK_ALIGN32(nNewSize + sizeof(HeapLink), s_heap.nAlignMask);
    u32 nAvail = pBlock->link.nSize - 1;
    HeapBlock * pNextBlock = (HeapBlock *) ((u8 *) pBlock + nAvail);
    if (!(pNextBlock->link.nSize & 0x1)) {
        if (nAvail + pNextBlock->link.nSize >= nNewSize) {
            nAvail += pNextBlock->link.nSize;
            // Merge with next
            LTKList_Remove(&pNextBlock->freeLink);
            s_heap.nBytesFree -= pNextBlock->link.nSize;
            pBlock->link.nSize += pNextBlock->link.nSize;
            // The new next block
            pNextBlock = (HeapBlock *) ((u8 *) pBlock + pBlock->link.nSize - 1);
            pNextBlock->link.nSizePrev = pBlock->link.nSize;
        }
    }
    if (nAvail >= nNewSize + s_heap.nMinBlockSize) {
        // Split
        u32 nNextBlockSize = pBlock->link.nSize - nNewSize - 1;
        s_heap.nBytesFree += nNextBlockSize;
        pNextBlock->link.nSizePrev = nNextBlockSize;
        // The new next block
        pNextBlock = (HeapBlock *) ((u8 *) pBlock + nNewSize);
        pNextBlock->link.nCanary = kCanaryValue;
        pNextBlock->link.nSize = nNextBlockSize;
        // Set size and mark allocation bit
        pNextBlock->link.nSizePrev = nNewSize + 1;
        pBlock->link.nSize = pNextBlock->link.nSizePrev;
        AddBlockToFreeList(&pNextBlock->freeLink, nNextBlockSize);
    } else if (nAvail < nNewSize) {
        // Move. LTKAlloc is region-blind and skips exclusive regions, so if the source
        // block lives in an exclusive (e.g. DMA-only) region we must not silently move
        // it into a non-exclusive region. Detect and fail in that case; per realloc
        // semantics the caller still owns the original block.
        u32 srcRegion = LTKHeap_FindRegion(_pBlock);
        if (srcRegion < LTK_MAX_HEAP_REGIONS && s_regions[srcRegion].bExclusive) {
            _pBlock = NULL;
        } else {
            u8 * pNewBlock = LTKAlloc(nNewSize);
            if (pNewBlock) {
                u32 nOldSize = pBlock->link.nSize - sizeof(HeapLink) - 1;
                u32 nCopySize = nNewSize < nOldSize ? nNewSize : nOldSize;
                for (u32 nIdx = 0; nIdx < nCopySize; nIdx++)
                    pNewBlock[nIdx] = ((u8 *) _pBlock)[nIdx];
                // Only free block if successful
                LTKFree(_pBlock);
            }
            _pBlock = pNewBlock;
        }
    }
    LTKMutexUnlock(&s_heap.mutex);
    return _pBlock;
}

void * LTKAlloc(LT_SIZE nSize) {
    return LTKAlloc_Internal(nSize, false, 0);
}

void * LTKAllocFromRegion(LTMemoryRegion region, LT_SIZE nSize) {
    /* Region == 0 is a sentinel meaning "no specific region".  Fall through to the
     * unrestricted allocator so the call is equivalent to LTKAlloc().  This matches the
     * contract documented on LTCore->GetNamedMemoryRegion: a missing/unknown name returns
     * (LTMemoryRegion)0, and lt_malloc_from_region(0, ...) must behave like lt_malloc(). */
    if (region == 0) return LTKAlloc(nSize);

    /* Public LTMemoryRegion values are 1-based indices into the BSP region table.
     * Translate to the internal 0-based slot, then bounds-check. */
    u32 slot = (u32)region - 1;
    if (slot >= s_nRegions) return NULL;
    return LTKAlloc_Internal(nSize, true, slot);
}

void LTKFree(void * _pBlock) {
    if (!_pBlock) return;
    HeapBlock * pBlock = (HeapBlock *) ((u8 *) _pBlock - sizeof(HeapLink));
    LTKMutexLock(&s_heap.mutex);
    // Check for canary value and double-free
    LT_ASSERT(pBlock->link.nCanary == kCanaryValue);
    LT_ASSERT(pBlock->link.nSize & 0x1);
    // Unmark allocation bit
    pBlock->link.nSize--;
    // Check next Canary value
    HeapBlock * pNext = (HeapBlock *) ((u8 *) pBlock + pBlock->link.nSize);
    LT_ASSERT(pNext->link.nCanary == kCanaryValue);
    s_heap.nBytesFree += pBlock->link.nSize;
    // Find next and previous blocks and determine if allocated
    HeapBlock * pPrev = NULL;
    if (!(pBlock->link.nSizePrev & 0x1))
        pPrev = (HeapBlock *) ((u8 *) pBlock - pBlock->link.nSizePrev);
    s32 nSizeIncrease = 0;
    if (!(pNext->link.nSize & 0x1)) {
        if (pPrev) {
            // Combine with previous and next
            nSizeIncrease += pBlock->link.nSize + pNext->link.nSize;
            LTKList_Remove(&pPrev->freeLink);
            LTKList_Remove(&pNext->freeLink);
            pBlock = pPrev;
        } else {
            // Combine with next
            nSizeIncrease += pNext->link.nSize;
            LTKList_Remove(&pNext->freeLink);
        }
    } else if (pPrev) {
        // Combine with previous
        nSizeIncrease += pBlock->link.nSize;
        LTKList_Remove(&pPrev->freeLink);
        pBlock = pPrev;
    }
    // Adjust Links
    pBlock->link.nSize += nSizeIncrease;
    pNext = (HeapBlock *) ((u8 *) pBlock + pBlock->link.nSize);
    pNext->link.nSizePrev = pBlock->link.nSize;
    AddBlockToFreeList(&pBlock->freeLink, pBlock->link.nSize);
    LTKMutexUnlock(&s_heap.mutex);
}

LT_SIZE LTKGetActualAllocationSize(void * pBlock) {
    if (!pBlock || ((LT_SIZE)pBlock & (kChunkAlignment - 1))) return 0;
    HeapLink * pLink = (HeapLink *)((u8 *)pBlock - sizeof(HeapLink));
    if (pLink->nCanary != kCanaryValue) return 0;
    if ((pLink->nSize & 0x1) == 0) return 0;
    return pLink->nSize - sizeof(HeapLink) - 1;
}

LT_SIZE LTKGetTotalSystemRAM(void) {
    return s_heap.nBytesTotal;
}

LT_SIZE LTKGetAvailableSystemRAM(void) {
    return s_heap.nBytesFree;
}

LT_SIZE LTKGetSystemRAMLowWatermark(void) {
    return s_heap.nMinBytesFree;
}

LT_SIZE LTKGetLTKCurrentAllocationCount(void) {
    /* In the case of non arch-hosted, LTCore could calculate this itself using
       LTKGetTotalSystemRAM() - LTKGetAvailableSystemRAM()
       but on hosted platforms those functions return values from the hosted operating system
       leaving no way to determine how much memory is allocated by LT. */
    return s_heap.nBytesTotal - s_heap.nBytesFree;
}

LT_SIZE LTKGetLTKAllocationHighWatermark(void) {
    /* In the case of non arch-hosted, LTCore could calculate this itself using
       LTKGetTotalSystemRAM() - LTKGetSystemRAMLowWatermark()
       but on hosted platforms those functions return values from the hosted operating system
       leaving no way to determine the high watermark of what is allocated by LT. */
    return s_heap.nBytesTotal - s_heap.nMinBytesFree;
}

LT_SIZE LTKGetLargestAvailableBlockInRAM(void) {
    LTKMutexLock(&s_heap.mutex);
    // The last block is the largest
    HeapBlock * pBlock = LT_CONTAINER_OF(s_heap.freeList.pPrev, HeapBlock, freeLink);
    u32 nMaxSize = pBlock->link.nSize;
    LTKMutexUnlock(&s_heap.mutex);
    return nMaxSize;
}
