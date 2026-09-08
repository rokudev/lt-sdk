/******************************************************************************
 * freertos.c                                      ESP32-S3 WPA supplicant port
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The FreeRTOS entry points on LT primitives.  esp_supplicant/src/esp_wpa2.c is
 * the one file in this component that talks to FreeRTOS, and the declarations
 * in freertos/ are exactly what it uses.
 *
 * A tick here is portTICK_PERIOD_MS milliseconds, as it is in IDF.  That is a
 * different unit from the tick the Wi-Fi blobs deal in, which the OS adapter's
 * _task_ms_to_tick makes one millisecond - the two never meet.
 */

#include <lt/core/LTCore.h>
#include <lt/core/LTCountingSemaphore.h>
#include <lt/core/LTMutex.h>
#include <lt/core/LTThread.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* Retry interval for the two operations LT has no timed wait for: taking a
 * mutex with a deadline, and sending to a queue that is full. */

#define WPA_POLL_INTERVAL_MS    2

/* A queue is a bounded ring under a mutex, with the count of queued items
 * mirrored into a counting semaphore so the receiver can block on it.  Only
 * wpa2_task receives, which satisfies LTCountingSemaphore's single waiter
 * rule. */

typedef struct WpaQueue {
    LTMutex             *pLock;
    LTCountingSemaphore *pFilled;
    u32                  nCapacity;
    u32                  nItemSize;
    u32                  nCount;
    u32                  nHead;
    u32                  nTail;
    u8                   pItems[];
} WpaQueue;

static LTTime
TicksToTimeout(TickType_t xTicksToWait) {
    if (xTicksToWait == portMAX_DELAY) return LTTime_Infinite();
    return LTTime_Milliseconds((s64)xTicksToWait * portTICK_PERIOD_MS);
}

/* Absolute kernel time the wait expires at, for the poll loops below. */
static LTTime
TicksToDeadline(TickType_t xTicksToWait) {
    if (xTicksToWait == portMAX_DELAY) return LTTime_Infinite();
    return LTTime_Add(LT_GetCore()->GetKernelTime(), TicksToTimeout(xTicksToWait));
}

static bool
DeadlinePassed(LTTime deadline) {
    return !LTTime_IsLessThan(LT_GetCore()->GetKernelTime(), deadline);
}

static void
PollDelay(void) {
    lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Milliseconds(WPA_POLL_INTERVAL_MS));
}

/****************************************************************************
 * Queues
 ****************************************************************************/

QueueHandle_t
xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize) {
    WpaQueue *pQueue;

    if (uxQueueLength == 0 || uxItemSize == 0) return NULL;

    pQueue = lt_malloc(sizeof(*pQueue) + (uxQueueLength * uxItemSize));
    if (pQueue == NULL) return NULL;

    lt_memset(pQueue, 0, sizeof(*pQueue));
    pQueue->nCapacity = uxQueueLength;
    pQueue->nItemSize = uxItemSize;
    pQueue->pLock     = lt_createobject(LTMutex);
    pQueue->pFilled   = lt_createobject(LTCountingSemaphore);

    if (pQueue->pLock == NULL || pQueue->pFilled == NULL) {
        vQueueDelete(pQueue);
        return NULL;
    }

    pQueue->pFilled->API->Init(pQueue->pFilled, uxQueueLength, 0);
    return pQueue;
}

void
vQueueDelete(QueueHandle_t xQueue) {
    WpaQueue *pQueue = xQueue;

    if (pQueue == NULL) return;

    if (pQueue->pFilled) lt_destroyobject(pQueue->pFilled);
    if (pQueue->pLock)   lt_destroyobject(pQueue->pLock);
    lt_free(pQueue);
}

BaseType_t
xQueueSend(QueueHandle_t xQueue, const void *pItemToQueue, TickType_t xTicksToWait) {
    WpaQueue *pQueue   = xQueue;
    LTTime    deadline = TicksToDeadline(xTicksToWait);

    if (pQueue == NULL || pItemToQueue == NULL) return pdFAIL;

    for (;;) {
        bool bQueued = false;

        pQueue->pLock->API->Lock(pQueue->pLock);
        if (pQueue->nCount < pQueue->nCapacity) {
            lt_memcpy(&pQueue->pItems[pQueue->nTail * pQueue->nItemSize], pItemToQueue, pQueue->nItemSize);
            pQueue->nTail = (pQueue->nTail + 1) % pQueue->nCapacity;
            pQueue->nCount++;
            bQueued = true;
        }
        pQueue->pLock->API->Unlock(pQueue->pLock);

        if (bQueued) {
            pQueue->pFilled->API->Signal(pQueue->pFilled);
            return pdPASS;
        }

        if (DeadlinePassed(deadline)) return pdFAIL;
        PollDelay();
    }
}

BaseType_t
xQueueReceive(QueueHandle_t xQueue, void *pBuffer, TickType_t xTicksToWait) {
    WpaQueue *pQueue = xQueue;

    if (pQueue == NULL || pBuffer == NULL) return pdFAIL;

    if (!pQueue->pFilled->API->Wait(pQueue->pFilled, TicksToTimeout(xTicksToWait))) return pdFAIL;

    pQueue->pLock->API->Lock(pQueue->pLock);
    lt_memcpy(pBuffer, &pQueue->pItems[pQueue->nHead * pQueue->nItemSize], pQueue->nItemSize);
    pQueue->nHead = (pQueue->nHead + 1) % pQueue->nCapacity;
    pQueue->nCount--;
    pQueue->pLock->API->Unlock(pQueue->pLock);

    return pdPASS;
}

/****************************************************************************
 * Semaphores
 *
 * The recursive mutex is an LTMutex, which nests, and the counting semaphore
 * an LTCountingSemaphore.  The supplicant keeps the two apart - it only ever
 * calls the Recursive entry points on the first and the plain ones on the
 * second - so the handles need no tag; vSemaphoreDelete takes either because
 * lt_destroyobject does.
 ****************************************************************************/

SemaphoreHandle_t
xSemaphoreCreateRecursiveMutex(void) {
    return lt_createobject(LTMutex);
}

SemaphoreHandle_t
xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount) {
    LTCountingSemaphore *pSem = lt_createobject(LTCountingSemaphore);

    if (pSem) pSem->API->Init(pSem, uxMaxCount, uxInitialCount);
    return pSem;
}

BaseType_t
xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex, TickType_t xTicksToWait) {
    LTMutex *pMutex   = xMutex;
    LTTime   deadline = TicksToDeadline(xTicksToWait);

    if (pMutex == NULL) return pdFAIL;

    if (xTicksToWait == portMAX_DELAY) {
        pMutex->API->Lock(pMutex);
        return pdPASS;
    }

    /* LTMutex has no timed lock, so poll TryLock until the deadline. */

    for (;;) {
        if (pMutex->API->TryLock(pMutex)) return pdPASS;
        if (DeadlinePassed(deadline))     return pdFAIL;
        PollDelay();
    }
}

BaseType_t
xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex) {
    LTMutex *pMutex = xMutex;

    if (pMutex == NULL) return pdFAIL;
    pMutex->API->Unlock(pMutex);
    return pdPASS;
}

BaseType_t
xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait) {
    LTCountingSemaphore *pSem = xSemaphore;

    if (pSem == NULL) return pdFAIL;
    return pSem->API->Wait(pSem, TicksToTimeout(xTicksToWait)) ? pdPASS : pdFAIL;
}

BaseType_t
xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
    LTCountingSemaphore *pSem = xSemaphore;

    if (pSem == NULL) return pdFAIL;
    pSem->API->Signal(pSem);
    return pdPASS;
}

void
vSemaphoreDelete(SemaphoreHandle_t xSemaphore) {
    if (xSemaphore) lt_destroyobject((LTObject *)xSemaphore);
}

/****************************************************************************
 * Tasks
 *
 * A task is an LTThread running one queued task proc.  The handle handed back
 * is the LTThread handle, which is what GetCurrentThread returns, so
 * xTaskGetCurrentTaskHandle compares equal inside the task as the supplicant
 * expects.  IDF's xTaskCreate takes the stack depth in bytes, unlike upstream
 * FreeRTOS.
 ****************************************************************************/

BaseType_t
xTaskCreate(TaskFunction_t pTaskCode, const char *pName, uint32_t nStackDepth,
            void *pParameters, UBaseType_t uxPriority, TaskHandle_t *pCreatedTask) {
    LTCore    *pCore   = LT_GetCore();
    ILTThread *pThread = lt_getlibraryinterface(ILTThread, pCore);
    LTThread   hThread;

    if (pTaskCode == NULL || pName == NULL) return pdFAIL;

    hThread = pCore->CreateThread(pName);
    if (!hThread) return pdFAIL;

    if (uxPriority > kLTThread_PriorityHighest) uxPriority = kLTThread_PriorityHighest;
    pThread->SetPriority(hThread, (u8)uxPriority);
    if (nStackDepth) pThread->SetStackSize(hThread, nStackDepth);
    pThread->Start(hThread, NULL, NULL);

    /* Published before the proc is queued: the task can run immediately, and
     * wpa2_post compares the caller's handle against it. */

    if (pCreatedTask) *pCreatedTask = (TaskHandle_t)hThread;

    if (!pThread->QueueTaskProc(hThread, (LTThread_TaskProc *)pTaskCode, NULL, pParameters)) {
        if (pCreatedTask) *pCreatedTask = NULL;
        pThread->Terminate(hThread);
        return pdFAIL;
    }

    return pdPASS;
}

void
vTaskDelete(TaskHandle_t xTaskToDelete) {
    ILTThread *pThread = lt_getlibraryinterface(ILTThread, LT_GetCore());

    pThread->Terminate(xTaskToDelete ? VOIDPTR_TO_LTHANDLE(xTaskToDelete) : pThread->GetCurrentThread());
}

TaskHandle_t
xTaskGetCurrentTaskHandle(void) {
    return (TaskHandle_t)lt_getlibraryinterface(ILTThread, LT_GetCore())->GetCurrentThread();
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created
 */
