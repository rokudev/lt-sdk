/*******************************************************************************
 * platforms/esp32/source/esp32/driver/esp32-lt-os-adapter/Esp32_LTOSAdapterOsi.h
 *
 * The OS primitives the Espressif BLE controller blobs ask for through their
 * osi_funcs_t table, plus the ets_timer group, published as a plain
 * function-pointer table.
 *
 * The esp32 and esp32s3 controllers disagree on the shape and ordering of
 * osi_funcs_t, so each driver builds its own table.  The primitives underneath
 * are pure LT (mutex, queue, thread, heap) and identical for both, so they are
 * implemented once in Esp32_LTOSAdapter.c and reached through here rather than
 * being duplicated per chip.  Handing out pointers keeps the implementations
 * static, so the esp32 build is unchanged.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_ESP32_LT_OS_ADAPTER_ESP32_LTOSADAPTEROSI_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_ESP32_LT_OS_ADAPTER_ESP32_LTOSADAPTEROSI_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

typedef struct Esp32OSAdapterPrimitives {
    void * (*SemCreate)(u32 max, u32 init);
    void   (*SemDelete)(void *pSem);
    s32    (*SemTake)(void *pSem, u32 blockTimeTick);
    s32    (*SemGive)(void *pSem);
    s32    (*SemTakeFromIsr)(void *pSem, void *pHptw);
    s32    (*SemGiveFromIsr)(void *pSem, void *pHptw);

    void   (*InterruptDisable)(void);
    void   (*InterruptRestore)(void);
    void   (*TaskYield)(void);
    void   (*TaskYieldFromIsr)(void);

    void * (*MutexCreate)(void);
    void   (*MutexDelete)(void *pMtx);
    s32    (*MutexLock)(void *pMtx);
    s32    (*MutexUnlock)(void *pMtx);

    void * (*QueueCreate)(u32 queueLen, u32 itemSize);
    void   (*QueueDelete)(void *pQueue);
    s32    (*QueueSend)(void *pQueue, void *pItem, u32 blockTimeTick);
    s32    (*QueueSendFromIsr)(void *pQueue, void *pItem, void *pHptw);
    s32    (*QueueRecv)(void *pQueue, void *pItem, u32 blockTimeTick);
    s32    (*QueueRecvFromIsr)(void *pQueue, void *pItem, void *pHptw);

    s32    (*TaskCreatePinnedToCore)(void *entry, const char *name, u32 stackDepth,
                                     void *param, u32 prio, void *taskHandle, u32 coreID);
    void   (*TaskDelete)(void *taskHandle);
    bool   (*IsInIsr)(void);

    void * (*Malloc)(u32 size);
    void * (*MallocInternal)(LT_SIZE size);
    void   (*Free)(void *pMem);

    s32    (*ReadEfuseMac)(u8 mac[6]);
    void   (*Srand)(u32 seed);
    s32    (*Rand)(void);

    /* ets_timer_* semantics over caller-provided struct ets_timer storage.
     * The esp32 takes these seven entry points from ROM; the esp32s3 ROM does
     * not export them, so Esp32s3_LTOSAdapter.c defines them on top of these. */
    void   (*TimerArm)(void *pTimer, u32 ms, bool repeat);
    void   (*TimerArmUs)(void *pTimer, u32 us, bool repeat);
    void   (*TimerDisarm)(void *pTimer);
    void   (*TimerDone)(void *pTimer);
    void   (*TimerSetFn)(void *pTimer, void *pFunc, void *pArg);
} Esp32OSAdapterPrimitives;

const Esp32OSAdapterPrimitives *LTEsp32OSAdapter_GetPrimitives(void);

LT_EXTERN_C_END
#endif  // PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_ESP32_LT_OS_ADAPTER_ESP32_LTOSADAPTEROSI_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  27-Aug-26   claudius    created
 */
