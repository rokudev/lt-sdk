/*
 * Copyright (c) 2021 Roku, Inc. All rights reserved.
 * This software and any compilation or derivative thereof is, and shall
 * remain, the proprietary information of Roku, Inc. and is highly confidential
 * in nature.
 */
/*
 * POOL_1532 is used for TCP TX-buffer packets waiting to be ACK'ed
 */

#ifndef LT_TCP_MEM_POOLS_256
#define TCP_MEM_POOLS_256       8
#else
#define TCP_MEM_POOLS_256      LT_TCP_MEM_POOLS_256
#endif

#ifndef LT_TCP_MEM_POOLS_512
#define TCP_MEM_POOLS_512       2
#else
#define TCP_MEM_POOLS_512      LT_TCP_MEM_POOLS_512
#endif

#ifndef LT_TCP_MEM_POOLS_1540
#define TCP_MEM_POOLS_1540      5
#else
#define TCP_MEM_POOLS_1540      LT_TCP_MEM_POOLS_1540
#endif
LWIP_MALLOC_MEMPOOL_START
LWIP_MALLOC_MEMPOOL(TCP_MEM_POOLS_256, 256)
LWIP_MALLOC_MEMPOOL(TCP_MEM_POOLS_512, 512)
LWIP_MALLOC_MEMPOOL(TCP_MEM_POOLS_1540, 1540)
LWIP_MALLOC_MEMPOOL_END

