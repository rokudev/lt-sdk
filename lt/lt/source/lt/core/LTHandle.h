/******************************************************************************
 * lt/source/core/LTHandle.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_CORE_LTHANDLE_H
#define ROKU_LT_SOURCE_LT_CORE_LTHANDLE_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/* ________________
   Handle functions internal to the LTCore library */
bool             LTHandle_Init(void);
void             LTHandle_Fini(void);
void *           LTHandle_ReserveInterfaceCheckedPrivateData(LTHandle handle, LTInterface *pInterface);
bool             LTHandle_FORCRASHDUMPONLY_EnumerateHandlesForInterface(LTInterface *pInterface, bool (*handleEnumProc)(LTHandle h, LTInterface *pInterface, void *privateData, void *clientData), void *clientData);
void             LTHandle_DeleteHandleWithoutDestroy(LTHandle handle);
void             LTHandle_DumpLeakedHandles(void);
/* ________________
   Handle functions that are LTCore public interface functions */

LTHandle         LTHandle_CreateHandle(LTInterface * pHandleInterface, LT_SIZE nSizeInBytes);
void             LTHandle_DestroyHandle(LTHandle handle);
bool             LTHandle_IsHandleValid(LTHandle handle);
void *           LTHandle_ReservePrivateData(LTHandle handle);
void             LTHandle_ReleasePrivateData(LTHandle handle, void *privateData);
void *           LTHandle_GetPrivateData(LTHandle handle);
LTInterface *    LTHandle_GetHandleInterface(LTHandle handle);
LTInterface *    LTHandle_GetNameCheckedHandleInterface(LTHandle handle, const char * pInterfaceName);
const char *     LTHandle_GetHandleInterfaceName(LTHandle handle);
LTLibrary *      LTHandle_GetHandleLibrary(LTHandle handle);
u32              LTHandle_GetHandleReservationCount(LTHandle handle);
const char *     LTHandle_GetHandleStateString(LTHandle handle);

u32              LTHandle_GetCreatedHandlesByInterface(LTHandle *handlesArrayToFill, u32 nArrayCount, LTInterface *pInterface);
                    // returns number of handles that match pInterface; puts up to nArrayCount into *handlesArrayToFill
u32              LTHandle_GetCreatedHandlesByInterfaceName(LTHandle *handlesArrayToFill, u32 nArrayCount, const char *pInterfaceName);
                    // returns number of handles that match pInterface; puts up to nArrayCount into *handlesArrayToFill

u32              LTHandle_GetHandleCount(void);
                    // returns the count of handles in handle pool.

u32              LTHandle_GetTotalHandleBytesOverhead(void);
                    // returns the count of handles in handle pool.

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_SOURCE_LT_CORE_LTHANDLE_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  19-Sep-23   augustus    created
 */
