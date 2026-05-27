/******************************************************************************
 * lt/source/core/LTAtomicMemoryImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include "LTCoreImpl.h"
#include "LTStdlibImpl.h"
#include "LTMemory.h"

/* -------------------------------------------------------- */
/* -------------------------------------------------------- */
/* PRIVATE structures */
/* -------------------------------------------------------- */
/* -------------------------------------------------------- */
DEFINE_LTLOG_SECTION("ltcore.mem");

/********************************************
 * LTAtomicMemoryImpl Library Private structs */
typedef struct LTFixedMemoryPoolConfig {
    u32              nBits;
    u32              nElements;
} LTFixedMemoryPoolConfig;


typedef struct LTBitmaskMemPool {
    void *                              pPool;

    /* Atomic Bitfield manager for lock-free allocation */
    LTAtomicBitfield                    sABF;

    /* pool size for address comparisons */
    LT_SIZE                             nLowAddr;
    LT_SIZE                             nBytes;
    /* Element (sub-buffer) size in bits */
    u32                                 nBits;
#ifdef LT_DEBUG
    LTAtomic                            nDbgAlloc;
    LTAtomic                            nDbgAllocFail;
    LTAtomic                            nDbgAllocBytes;

    LTAtomic                            nDbgFree;
    LTAtomic                            nDbgBorrowed;
#endif
} LTBitmaskMemPool;

/* given a pointer into a larger buffer, what "element"
 * is it, we're effectively dividing by the struct size
 */
#define POOLADDR_TO_POS(pool,pBuf) \
    (((LT_SIZE)pBuf - (LT_SIZE)pool->pPool) >> pool->nBits)

/* get a pointer to a buffer given the position */
#define POS_TO_POOLADDR(pool,pos) \
    ((char *)pool->pPool + ((pos) << pool->nBits))

#define MIN_FIXED_NBITS     3   /* implies 8 byte minimum */
#ifdef MEM_SELF_TEST
    #define MAX_FIXED_NBITS     (19)   /*  */
#else
    #define MAX_FIXED_NBITS     (LT_ARCHITECTURE_BITS - 3)   /*  */
#endif
#define NUM_BITFIELD_ARRAYS     (MAX_FIXED_NBITS-MIN_FIXED_NBITS+1)

/* Align all buffers to 16 byte boundaries */
#define POOL_BUF_ALIGN       ((LT_SIZE)(1<<4))
#define POOL_BUF_ALIGN_MASK  (POOL_BUF_ALIGN-1)

static const LTFixedMemoryPoolConfig g_memorycfg[NUM_BITFIELD_ARRAYS] = {
                 /* _________   __________   _________    _____       ____________       ____________
                    SLAB BITS   SLAB BYTES   NUM SLABS    BYTES       32-BIT WORDS       64-BIT WORDS */
    {  3, 366 }, /*    2 ^  3 =          8 *       366 =   2928 / 4 =          732 / 2 =          366 */
    {  4, 273 }, /*    2 ^  4 =         16 *       273 =   4368 / 4 =         1092 / 2 =          546 */
    {  5, 182 }, /*    2 ^  5 =         32 *       182 =   5824 / 4 =         1456 / 2 =          728 */
    {  6,  91 }, /*    2 ^  6 =         64 *        91 =   5824 / 4 =         1456 / 2 =          728 */
    {  7,  46 }, /*    2 ^  7 =        128 *        46 =   5888 / 4 =         1472 / 2 =          736 */
    {  8,  23 }, /*    2 ^  8 =        256 *        23 =   5888 / 4 =         1472 / 2 =          736 */
    {  9,  12 }, /*    2 ^  9 =        512 *        12 =   6144 / 4 =         1536 / 2 =          768 */
    { 10,   6 }, /*    2 ^ 10 =       1024 *         6 =   6144 / 4 =         1536 / 2 =          768 */
    { 11,   3 }, /*    2 ^ 11 =       2048 *         3 =   6144 / 4 =         1536 / 2 =          768
                     ___________________________________  _____       ____________       ____________
                     TOTAL TOTAL TOTAL TOTAL TOTAL TOTAL  BYTES       32-BIT WORDS       64-BIT WORDS
                                                          49152              12288               6144
                                                            48K                                       */
};

typedef struct LTBitmaskMemManager {
    LTBitmaskMemPool              pool[NUM_BITFIELD_ARRAYS];
    void (*UserDebug)(const char *txt, struct LTBitmaskMemPool *pool, void *pBuf);
} LTBitmaskMemManager;

static LTBitmaskMemManager g_mem;

static void LTBitmaskMemPool_Free(
    LTBitmaskMemPool *            pool,
    void *                        pMem)
{
    LT_SIZE         pos;

    pos = POOLADDR_TO_POS(pool,pMem);

    LTAtomicBitfield_FreeOne(&pool->sABF,pos);

#ifdef LT_DEBUG
    LTAtomic_FetchAdd(&pool->nDbgFree,1);
#endif
}

static void *LTBitmaskMemPool_Alloc(
    LTBitmaskMemPool *            pool)
{
    char *          retval = NULL;
    LT_SIZE         pos;

    if (LTAtomicBitfield_AllocOne(&pool->sABF,&pos)) {
        retval = POS_TO_POOLADDR(pool,pos);

#ifdef LT_DEBUG
        LTAtomic_FetchAdd(&pool->nDbgAlloc,1);
#endif
    } else {
#ifdef LT_DEBUG
        LTAtomic_FetchAdd(&pool->nDbgAllocFail,1);
#endif
    }

    return (void *)retval;
}

static void *LTBitmaskMemManagerImpl_Alloc(LT_SIZE size)
{
    void *      pMem = NULL;
    LT_SIZE     mask;
    u32         poolNum;
    int         bBorrowed = 0;

    mask = 0x1 << MIN_FIXED_NBITS;
    for (poolNum = 0; poolNum < NUM_BITFIELD_ARRAYS; poolNum++) {
        if (mask >= size) {
            /* attempt alloc */
            LTBitmaskMemPool *            pool = &g_mem.pool[poolNum];

            if (NULL != pool->pPool) {
                pMem = LTBitmaskMemPool_Alloc(pool);

                if (NULL != pMem) {
#ifdef LT_DEBUG
                    LTAtomic_FetchAdd(&pool->nDbgAllocBytes,size);
#endif
                    if (bBorrowed) {
                        /* debug stats */
#ifdef LT_DEBUG
                        LTAtomic_FetchAdd(&pool->nDbgBorrowed,1);
#endif
                    }
                    break;
                }
            }
            /* note, if a pool is empty, we will borrow the next
             * larger size chunk!!!
             */
            /* TODO: emit debug that this pool size has been exhausted
             */
            bBorrowed = 1;
        }
        mask <<= 1;
    }
    return pMem;
}

/* returns true if it was freed, false if the buffer was
 * not contained in one of the pools
 */
bool LTBitmaskMemManagerImpl_Free(void *pMem)
{
    u32         poolNum;

    for (poolNum = 0; poolNum < NUM_BITFIELD_ARRAYS; poolNum++) {
        LTBitmaskMemPool *    pool = &g_mem.pool[poolNum];

        /* is the address within this pool?
         * note the subtraction results in unsigned
         * value so the offset will appear large unsigned
         * if it is negative.
         */
        if (((LT_SIZE)pMem - pool->nLowAddr) < pool->nBytes) {
            LTBitmaskMemPool_Free(pool,pMem);
        }
    }
    if (poolNum >= NUM_BITFIELD_ARRAYS) {
        /* TODO: emit bug as this chunk of memory is not managed */
        return false;
    }
    return true;
}

/* returns the size of the pool that contains the address pMem,
 * false if there is no such pool.
 */
static LT_SIZE LTBitmaskMemManagerImpl_GetBufferSize(void *pMem)
{
    LT_SIZE     nBytes = 0;
    u32         poolNum;

    for (poolNum = 0; poolNum < NUM_BITFIELD_ARRAYS; poolNum++) {
        LTBitmaskMemPool *    pool = &g_mem.pool[poolNum];

        /* is the address within this pool?
         * note the subtraction results in unsigned
         * value so the offset will appear large unsigned
         * if it is negative.
         */
        if (((LT_SIZE)pMem - pool->nLowAddr) < pool->nBytes) {
            nBytes = (LT_SIZE)1 << pool->nBits;
        }
    }

    return nBytes;
}

static int LTBitmaskMemManagerImpl_InitializeBitmask(void *pBuf, LT_SIZE nElements, u32 nSizeBits)
{
    int         err = 1;
    if ((nSizeBits >= MIN_FIXED_NBITS) &&
         (nSizeBits <= MAX_FIXED_NBITS)) {
        LTBitmaskMemPool *    pool = &g_mem.pool[nSizeBits - MIN_FIXED_NBITS];

        if (NULL == pool->pPool) {
            LTAtomicBitfield_Init(&pool->sABF, pBuf, nElements, false);
            err = 0;
        } else {
            /* TODO: log error, already initialized */
        }
    } else {
        /* TODO: log error, insane size */
    }
    return err;
}

static int LTBitmaskMemManagerImpl_InitializeNbitsPool(void *pBuf, LT_SIZE nElements, u32 nSizeBits)
{
    int         err = 1;
    if ((nSizeBits >= MIN_FIXED_NBITS) &&
         (nSizeBits <= MAX_FIXED_NBITS)) {
        LTBitmaskMemPool *    pool = &g_mem.pool[nSizeBits - MIN_FIXED_NBITS];

        if (NULL == pool->pPool) {
            pool->pPool = pBuf;
            pool->nLowAddr = (LT_SIZE)pBuf;
            pool->nBits = nSizeBits;
            pool->nBytes = nElements * (1 << nSizeBits);
            err = 0;
        } else {
            /* TODO: log error, already initialized */
        }
    } else {
        /* TODO: log error, insane size */
    }
    return err;
}

static LT_SIZE LTBitmaskMemManagerImpl_GetPoolSizes(const LTFixedMemoryPoolConfig *cfg, u32 nConfigs)
{
    LT_SIZE     nBytes = 0;
    u32         i;

    /* ensure every chunk is aligned to pool buffer size */
    for (i = 0; i < nConfigs; i++) {
        nBytes += LTAtomicBitfield_CalculateBitfieldSize(cfg[i].nElements);
        nBytes = (nBytes + POOL_BUF_ALIGN_MASK) & ~POOL_BUF_ALIGN_MASK;
    }
    for (i = 0; i < nConfigs; i++) {
        nBytes += cfg[i].nElements * (1 << cfg[i].nBits);
        nBytes = (nBytes + POOL_BUF_ALIGN_MASK) & ~POOL_BUF_ALIGN_MASK;
    }

    return nBytes;
}

static void LTBitmaskMemManagerImpl_InitializeAllPools(const LTFixedMemoryPoolConfig *cfg, u32 nConfigs, void *buf)
{
    u32         i;

    /* initialize Bitmasks first, keeping them all in a separate
     * area to avoid memory corruption from wandering code in
     * the general memory pool
     */
    for (i = 0; i < nConfigs; i++) {
        LT_SIZE     nPoolBytes;
        /* calculate the size for the bitmask */
        nPoolBytes = LTAtomicBitfield_CalculateBitfieldSize(cfg[i].nElements);
        /* align to byte boundary */
        nPoolBytes = (nPoolBytes + POOL_BUF_ALIGN_MASK) & ~POOL_BUF_ALIGN_MASK;
        if (nPoolBytes) {
            LTBitmaskMemManagerImpl_InitializeBitmask(
                buf, cfg[i].nElements, cfg[i].nBits);
        }
        buf = ((char *)buf) + nPoolBytes;
    }
    /* Now set up the memory pools for use by Alloc()/Free()
     */
    for (i = 0; i < nConfigs; i++) {
        LT_SIZE     nPoolBytes;
        nPoolBytes = cfg[i].nElements * (1 << cfg[i].nBits);
        /* align to byte boundary */
        nPoolBytes = (nPoolBytes + POOL_BUF_ALIGN_MASK) & ~POOL_BUF_ALIGN_MASK;
        if (nPoolBytes) {
            LTBitmaskMemManagerImpl_InitializeNbitsPool(
                buf, cfg[i].nElements, cfg[i].nBits);
        }
        buf = ((char *)buf) + nPoolBytes;
    }
}

static void LTBitmaskMemManagerImpl_DebugPrint(void)
{
    u32         poolNum;

    for (poolNum = 0; poolNum < NUM_BITFIELD_ARRAYS; poolNum++) {
        LTBitmaskMemPool *    pool = &g_mem.pool[poolNum];

#ifdef LT_DEBUG
        LTLOG_DEBUG("pool","%2d @%p sz:%08x a%7d f%7d !%7d b%5d c%6d ",
                (int) poolNum,
                (void *)pool->nLowAddr,
                (int) pool->nBytes,
                (int) LTAtomic_Load(&pool->nDbgAlloc),
                (int) LTAtomic_Load(&pool->nDbgFree),
                (int) LTAtomic_Load(&pool->nDbgAllocFail),
                (int) LTAtomic_Load(&pool->nDbgBorrowed),
                (int) LTAtomic_Load(&pool->sABF.numDebugCollisions));
#else
        LTLOG_DEBUG("pool","%2d @%p sz:%08x ",
                (int) poolNum,
                (void *)pool->nLowAddr,
                (int) pool->nBytes);
#endif

        LTAtomicBitfield_DebugPrint(&pool->sABF);
    }
}

/*-------- LOCKING Memory Pool ------------- */
typedef struct LTLockingMemPoolChunk LTLockingMemPoolChunk;
typedef struct LTLockingMemPoolChunk {
    LTLockingMemPoolChunk *             pNext;  /* MUST BE FIRST ITEM!! */
                                                /* points to beginning of buffer (location immediately
                                                 * after this struct) if the chunk has been allocated */
    LT_SIZE                             nSize;  /* size of this chunk (which includes this struct) */
} LTLockingMemPoolChunk;

typedef struct LTLockingMemPool {
    /* whole chunk here */
    LTLockingMemPoolChunk *             pHead;
    const char *                        pMem;
    LT_SIZE                             nMemSize;
} LTLockingMemPool;

#define MAX_LOCKING_POOLS   3

static LTMutex             *g_hLockPoolsMutex;
static LTAtomic             g_nLockPools = 0;
static LTLockingMemPool     g_LockPool[MAX_LOCKING_POOLS]; /* one element per region added by AddRegion() */

static void LTLockingMemPoolPrivate_Lock(void) {
    if (0 != g_hLockPoolsMutex) {
        /* TODO: grab the mutex */
    }
}

static void LTLockingMemPoolPrivate_Unlock(void) {
    if (0 != g_hLockPoolsMutex) {
        /* TODO: release the mutex */
    }
}

bool LTLockingMemPoolImpl_AddRegion(void *pBuf, LT_SIZE nBytes)
{
    bool        result = false;
    LT_SIZE     n;

    n = LTAtomic_FetchAdd(&g_nLockPools,1);

    if (n < MAX_LOCKING_POOLS) {
        LTLockingMemPool *  pool = &g_LockPool[n];
        char *              aBuf;
        LT_SIZE             aBytes; /* aligned + padded bytes */

        /* how is the buffer aligned? */
        aBytes = (LT_SIZE)pBuf & POOL_BUF_ALIGN_MASK;

        /* need to align to next boundary */
        if (aBytes) {
            LT_SIZE     skip;

            skip = POOL_BUF_ALIGN - aBytes;

            /* move pointer forward */
            aBuf = (void *)((char *)pBuf + skip);
            /* decrease size the same amount */
            aBytes = nBytes - skip;
        } else {
            aBuf = pBuf;
            aBytes = nBytes;
        }

        /* trim off remaining buffer */
        aBytes &= ~POOL_BUF_ALIGN_MASK;

        /* mark the pool memory */
        pool->pMem = aBuf;
        pool->nMemSize = aBytes;

        /* set the head to the aligned buffer, aBuf */
        pool->pHead = (LTLockingMemPoolChunk *)aBuf;

        /* initialize the first node, which contains the entire buffer */
        pool->pHead->pNext = NULL;
        pool->pHead->nSize = aBytes;

        result = true;
    }

    return result;
}

/* clip g_nLockPools to prevent overrun of g_LockPool */
static LT_SIZE LTLockingMemPoolImpl_nPools(void) {
    LT_SIZE nPools = g_nLockPools;
    return nPools < MAX_LOCKING_POOLS ? nPools : MAX_LOCKING_POOLS;
}

void *LTLockingMemPoolImpl_Alloc(LT_SIZE nBytes)
{
    void    *pBuf = NULL;
    LTLockingMemPoolPrivate_Lock();
    {
        LT_SIZE  n,nPools;
        LT_SIZE     aBytes; /* aligned + padded bytes */

        nPools = LTLockingMemPoolImpl_nPools();

        /* round the number of bytes up */
        aBytes = sizeof(LTLockingMemPoolChunk);
        aBytes += (nBytes + POOL_BUF_ALIGN_MASK) & ~POOL_BUF_ALIGN_MASK;

        for (n = 0; n < nPools; n++) {
            LTLockingMemPool      *  pool = &g_LockPool[n];
            LTLockingMemPoolChunk ** ppPrev = &pool->pHead;
            LTLockingMemPoolChunk *  pCur = *ppPrev;

            while (NULL != pCur) {
                if (pCur->nSize > aBytes) {
                    LTLockingMemPoolChunk   *pNext;

                    /* Take the buffer at this position */
                    /* but point over our node and link it */

                    /* this is where the next node should be */
                    pNext = (LTLockingMemPoolChunk *)((char *)pCur + aBytes);

                    /* initialize it with a new size and the Next pointer */
                    pNext->pNext = pCur->pNext;
                    pNext->nSize = pCur->nSize - aBytes;

                    /* link over the buffer we're grabbing */
                    *ppPrev = pNext;

                    pBuf = &pCur[1];

                    /* SANITY CHECK FOR FREE, point to self (indicates chunk is allocated) */
                    pCur->pNext = pBuf;
                    pCur->nSize = aBytes;
                    break;
                } else if (pCur->nSize == aBytes) {
                    /* take the entire chunk */
                    *ppPrev = pCur->pNext;

                    pBuf = &pCur[1];

                    /* SANITY CHECK FOR FREE, point to self (indicates chunk is allocated) */
                    pCur->pNext = pBuf;
                    pCur->nSize = aBytes;
                    break;
                } else {
                    /* just continue */
                }

                ppPrev = &pCur->pNext;
                pCur = pCur->pNext;
            }
            /* we've got a buffer */
            if (NULL != pBuf) {
                break;
            }
        }
    }
    LTLockingMemPoolPrivate_Unlock();
    return pBuf;
}

bool LTLockingMemPoolImpl_Free(void *pBuf)
{
    bool    result = false;

    LTLockingMemPoolPrivate_Lock();
    {
        LTLockingMemPoolChunk * pThis;

        /* Validate the node right before the alloc which indicates
         * the pointer and size of this buffer
         */

        pThis = ((LTLockingMemPoolChunk *)pBuf) - 1;

        if (pThis->pNext == pBuf) {
            LT_SIZE  n,nPools;

            nPools = LTLockingMemPoolImpl_nPools();

            for (n = 0; n < nPools; n++) {
                LTLockingMemPool      *  pool = &g_LockPool[n];

                /* is the buffer within this pool */
                if ((LT_SIZE)((char *)pBuf - pool->pMem) < pool->nMemSize) {
                    LTLockingMemPoolChunk ** ppPrev = &pool->pHead;
                    LTLockingMemPoolChunk *  pCur = *ppPrev;

                    while (NULL != pCur) {
                        /* does our buffer come before pCur? */
                        if (((char *)pCur - (char *)pThis) > 0) {
                            /* ppPrev
                             */
                            if ((char *)pCur ==
                                ((char *)pThis + pThis->nSize) ) {

                                /* Merge pCur into this buffer */

                                /* add its size */
                                pThis->nSize += pCur->nSize;
                                /* point OVER pCur by taking its link */
                                pThis->pNext = pCur->pNext;
                                /* unlink pCur from the Previous node */
                                *ppPrev = pThis;

                                result = true;
                            } else {
                                /* insert ourselves ahead of pCur Node */
                                pThis->pNext = pCur;
                                *ppPrev = pThis;
                                result = true;
                            }

                            if (result) {
                                break;
                            }
                        }
                        /* move to the next node */
                        ppPrev = &pCur->pNext;
                        pCur = pCur->pNext;
                    }

                    if (NULL == pCur) {
                        /* we got to the end of the list, but we
                         * know we're contained within this pool
                         */
                        /* Link ppPrev to point to us */
                        pThis->pNext = *ppPrev;
                        *ppPrev = pThis;
                        result = true;
                    }

                    if (result) {
                        /* check to see if the "Previous"
                         * node bumps up against us.
                         * WARNING, we need to make sure it isn't
                         * at the freelist pHead pointer.
                         */
                        if (ppPrev != &pool->pHead) {
                            LTLockingMemPoolChunk * pPrev = (LTLockingMemPoolChunk *)ppPrev;

                            if (((char *)pPrev + pPrev->nSize) == (char *)pThis) {
                                /* take our size, and unlink pThis */
                                pPrev->nSize += pThis->nSize;
                                pPrev->pNext = pThis->pNext;
                            }
                        }
                        break;
                    }
                }
            }
        } else {
            /* ERROR: Trying to free an invalid buffer! */
            /* Possible memory corruption */
        }
    }
    LTLockingMemPoolPrivate_Unlock();

    return result;
}

static LT_SIZE LTLockingMemPoolImpl_GetBufferSize(void *pBuf)
{
    LT_SIZE  nBytes = 0;
    LT_SIZE  n,nPools;

    LTLockingMemPoolPrivate_Lock();
    {
        nPools = LTLockingMemPoolImpl_nPools();
        for (n = 0; n < nPools; n++) {
            LTLockingMemPool      *  pool = &g_LockPool[n];

            /* is the buffer within this pool */
            if ((LT_SIZE)((char *)pBuf - pool->pMem) < pool->nMemSize) {
                LTLockingMemPoolChunk * pThis;

                /* Validate the node right before the alloc which indicates
                 * the pointer and size of this buffer
                 */

                pThis = ((LTLockingMemPoolChunk *)pBuf) - 1;
                nBytes = pThis->nSize;
                break;
            }
        }
    }
    LTLockingMemPoolPrivate_Unlock();

    return nBytes;
}


/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Pulling it together, a managed API for Init/Alloc/Free */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

bool LTMemory_AddMemoryRegion(void *pBuf, LT_SIZE nBytes) {
    return LTLockingMemPoolImpl_AddRegion(pBuf, nBytes);
}

void LTMemory_SetMemoryMutex(LTMutex *mutex) {
    g_hLockPoolsMutex = mutex;
}

bool LTMemory_InitializeLockFreeMemory(void) {
    void * pLockFreeBuff = LTLockingMemPoolImpl_Alloc(LTBitmaskMemManagerImpl_GetPoolSizes(g_memorycfg, NUM_BITFIELD_ARRAYS));
    return pLockFreeBuff ? LTBitmaskMemManagerImpl_InitializeAllPools(g_memorycfg, NUM_BITFIELD_ARRAYS, pLockFreeBuf), true : false;
}

void LTMemory_MemoryDebugPrint(void) {
    LTBitmaskMemManagerImpl_DebugPrint();
}

void * LTMemoryPrivate_Alloc(LT_SIZE nBytes, const char * pFilename, int nLine) { LT_UNUSED(pFilename); LT_UNUSED(nLine);
    return LTBitmaskMemManagerImpl_Alloc(nBytes) || LTLockingMemPoolImpl_Alloc(nBytes);
}

static bool LTMemoryPrivate_Free(void *pBuf)
{
    bool    result;
    if (! (result = LTBitmaskMemManagerImpl_Free(pBuf))) {
        result = LTLockingMemPoolImpl_Free(pBuf);
    }
    return result;
}

void *LTMemoryPrivate_ReAlloc(void *pMem, LT_SIZE nBytes, const char * pFilename, int nLine) { LT_UNUSED(pFilename); LT_UNUSED(nLine);
    void * pRetval = NULL;

    if (NULL != pMem) {
        if (0 != nBytes) {
            LT_SIZE     copySize;

            if (0 == (copySize = LTBitmaskMemManagerImpl_GetBufferSize(pMem))) {
                if (0 == (copySize = LTLockingMemPoolImpl_GetBufferSize(pMem))) {
                    /* If we can't find it, then it's not one of our allocs
                     * and we won't be able to free it and validity of copying
                     * the size of the new buffer is undefined;
                     * Return NULL in this case.
                     */
                    /* copySize = nBytes; */
                    return NULL;
                }
            }

            if (copySize > nBytes) {
                copySize = nBytes;
            }

            if (NULL != (pRetval = LTMemoryPrivate_Alloc(nBytes))) {
                LTStdlibImpl_memcpy(pRetval, pMem, copySize);
                LTMemoryPrivate_Free(pMem);
            }
        } else {
            LTMemoryPrivate_Free(pMem);
        }
    } else {    /* pMem is NULL */
        if (nBytes > 0) {
            pRetval = LTMemoryPrivate_Alloc(nBytes);
        }
    }

    return pRetval;
}
