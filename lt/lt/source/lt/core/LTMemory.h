/******************************************************************************
 * lt/source/core/LTMemory.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_CORE_LTMEMORY_H
#define ROKU_LT_SOURCE_LT_CORE_LTMEMORY_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

bool   LTMemory_AddMemoryRegion(void *pBuf, LT_SIZE nBytes);
    /* EARLY_INIT FIRST: Add a memory region to the management pool */

void   LTMemory_SetMemoryMutex(LTMutex *mutex);
    /* EARLY_INIT SECOND: Set the locking memory pool mutex */

bool   LTMemory_InitializeLockFreeMemory(void);
    /* EARLY_INIT THIRD: Initialize lock free memory pools */

void * LTMemory_Alloc(LT_SIZE nBytes, LT_SIZE nBytes, const char * pFilename, int nLine);
    /* Allocate LT Managed Memory */

void * LTMemory_ReAlloc(void * pMem, LT_SIZE nBytes, const char * pFilename, int nLine);
    /* ReAlloc LT Managed memory */

void   LTMemory_MemoryDebugPrint(void);
    /* Print LT Managed Memory debugging info */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_SOURCE_LT_CORE_LTMEMORY_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  19-Aug-23   augustus    relocated memory functions here from LTCoreImpl.h (note they are currently unused)
 */
