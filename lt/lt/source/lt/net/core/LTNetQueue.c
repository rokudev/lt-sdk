/*****************************************************************************************
 * LTNetQueue API implementation.
 *
 * lt/platforms/si91x/source/si91x/driver/wireless/common/LTNetQueue.c
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTSpinLock.h>
#include <lt/net/core/LTNetQueue.h>
#if defined(DEBUG_ASSERT)
#define DEBUG_ASSERT_LTNETQ(x) LT_ASSERT(x)
#else
#define DEBUG_ASSERT_LTNETQ(x)
#endif
DEFINE_LTLOG_SECTION("lt.net.queue");
//#define PP  LT_GetCore()->ConsoleStomp
#define P(...)
#define PP(...)

#if defined(DEBUG)
#define PLOG(...) LTLOG(__VA_ARGS__)
#else
#define PLOG(...)
#endif

typedef struct {
    LTThread                            tid;
    LTThread_TaskProc*                  eventCallback;
    LTThread_ClientDataReleaseProc*     relaseCb;
    void*                               pReceiverClientData;
    LTList_Node                         node;
} LTNetQueueListners;
typedef LTList LTNetQueueListnersList;


#define LTNETQUEUE_FREELISTNERS(queue) if(!LTList_IsEmpty(&queue->listners)) { \
    LTList_ForEach(node, &queue->listners) { \
        LTNetQueueListners *listner = LT_CONTAINER_OF(node, LTNetQueueListners, node); \
        lt_free(listner); \
    }LTList_EndForEach; \
}
typedef struct {
    void                    **buffer;
    u32                     head;
    u32                     tail;
    LTMutex                 *lock;
    u32                     queueSize;
    u32                     watermark;
    LTNetQueueListnersList  listners;
    LTEvent                 event;
    // If the queue is empty, release the grant; otherwise, hold it.
    // This ensures that as long as there is a packet in LTNetQueue,
    // the system will not enter sleep mode.
    // Don't allow sleep while packets are in flight, this will guarantee
    // that the application packets will be delivered or received reliably.
    u32                     disallowance_grant;
} LockedQueue;

typedef_LTObjectImpl(LTNetQueue, LTNetQueueImpl) {
    LockedQueue     *queue;
    LTCore          *core;
    ILTThread       *thread;
    ILTEvent        *iEvent;
} LTOBJECT_API;

static const LTArgsDescriptor EventArgs = {4, { kLTArgType_u32, kLTArgType_u32, kLTArgType_u32, kLTArgType_s64 }};
static void DispatchEvent(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    if (!proc) return;
    (*(LTNetQueueEventCallback)proc)(LTArgs_u32At(0, args), LTArgs_u32At(1, args), LTArgs_u32At(2, args), (LTArgs_s64At(3, args)), data);
}


static void LTNetQueueImpl_DestructObject(LTNetQueueImpl *netQueue) {
    P("LTNetQueueImpl_DestructObject: netQueue=%p netQueue->queue=%p netQueue->queue->buffer=%p\n", netQueue, (netQueue)?netQueue->queue:NULL, (netQueue)?((netQueue->queue)?netQueue->queue->buffer:NULL):NULL);
    if (netQueue->queue) {
        LTNETQUEUE_FREELISTNERS(netQueue->queue);
        if (netQueue->queue->buffer) lt_free(netQueue->queue->buffer);
        if (netQueue->queue->event != LTHANDLE_INVALID) lt_destroyhandle(netQueue->queue->event);
        lt_destroyobject(netQueue->queue->lock);
        lt_free(netQueue->queue);
        netQueue->queue = NULL;
    }
    return;
}

static bool LTNetQueueImpl_ConstructObject(LTNetQueueImpl *netQueue) {
    netQueue->queue = (LockedQueue*)lt_malloc(sizeof(LockedQueue));
    lt_memset(netQueue->queue, 0, sizeof(LockedQueue));
    netQueue->core = LT_GetCore();
    netQueue->thread = lt_getlibraryinterface(ILTThread, netQueue->core);
    netQueue->iEvent = lt_getlibraryinterface(ILTEvent, netQueue->core);
    netQueue->queue->lock = lt_createobject(LTMutex);
    if (!netQueue->core || !netQueue->thread || !netQueue->queue || !netQueue->queue->lock) {
        LTNetQueueImpl_DestructObject(netQueue);
        return false;
    }
    LTList_Init(&netQueue->queue->listners);
    netQueue->queue->event = netQueue->core->CreateEvent(&EventArgs, DispatchEvent, NULL, NULL, NULL);
    return true;
}


static u32 LTNetQueueImpl_Size(LTNetQueue *queue) {
    if (!queue) return 0;
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    u32 head = netQueue->queue->head;
    u32 tail = netQueue->queue->tail;
    u32 size = (tail >= head) ? (tail - head) : (netQueue->queue->queueSize - head + tail);
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
    return size;
}

static bool LTNetQueueImpl_SetWatermark(LTNetQueue *queue, u32 watermark) {
    if (!queue) return false;
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    netQueue->queue->watermark = watermark;
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
    return true;
}

static u32 LTNetQueueImpl_GetWatermark(LTNetQueue *queue) {
    if (!queue) return 0;
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    u32 watermark = netQueue->queue->watermark;
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
    return watermark;
}

static bool LTNetQueueImpl_Create(LTNetQueue *queue, u32 queueSize) {
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    if (!queueSize) return false;
    if (netQueue->queue->buffer) {
        DEBUG_ASSERT_LTNETQ(0);
        return false;
    }
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    netQueue->queue->buffer = (void **)lt_malloc(queueSize * sizeof(void *));
    if (!netQueue->queue->buffer) return false;
    netQueue->queue->queueSize = queueSize+1;
    netQueue->queue->watermark = queueSize;
    netQueue->queue->head = 0;
    netQueue->queue->tail = 0;
    netQueue->queue->disallowance_grant = 0;
    if (!LTList_IsEmpty(&netQueue->queue->listners)) {
        LTNETQUEUE_FREELISTNERS(netQueue->queue);
        DEBUG_ASSERT_LTNETQ(0);
        lt_free(netQueue->queue->buffer);
        netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
        return false;
    }
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
    return true;
}

static void LTNetQueueImpl_Destroy(LTNetQueue *queue) {
    if (!queue) return;
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    LTNETQUEUE_FREELISTNERS(netQueue->queue);
    if (netQueue->queue->buffer) lt_free(netQueue->queue->buffer);
    netQueue->queue->buffer = NULL;
    netQueue->queue->head = 0;
    netQueue->queue->tail = 0;
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
    return;
}

static void LTNetQueueImpl_RegisterNotification(LTNetQueue *queue, LTThread_TaskProc* eventCallback, LTThread_ClientDataReleaseProc* relaseCb, void * pReceiverClientData, bool notify_immidiate) {
    LT_UNUSED(notify_immidiate);
    if (!queue || !eventCallback) return;
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    LTNetQueueListners *listner = (LTNetQueueListners *)lt_malloc(sizeof(LTNetQueueListners));
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    //Check if already exists
    LTList_ForEach(node, &netQueue->queue->listners) {
        LTNetQueueListners *nlistner = LT_CONTAINER_OF(node, LTNetQueueListners, node);
        if (nlistner->eventCallback == eventCallback &&
            nlistner->tid ==netQueue->thread->GetCurrentThread()) {
            netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
            if(listner) lt_free(listner);
            return;
        }
    } LTList_EndForEach;

    if (listner) {
        listner->eventCallback = eventCallback;
        listner->relaseCb = relaseCb;
        listner->pReceiverClientData = pReceiverClientData;
        listner->tid = netQueue->thread->GetCurrentThread();
        LTList_AddTail(&netQueue->queue->listners, &listner->node);
    } else {
        LTLOG_REDALERT_AND_REBOOT("listener.malloc.failed", "LTNetQueueImpl_RegisterNotification: malloc failed");
        DEBUG_ASSERT_LTNETQ(0);
    }
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
}

static bool LTNetQueueImpl_UnRegisterNotification(LTNetQueue *queue, LTThread_TaskProc eventCallback) {
    if (!queue || !eventCallback) return false;
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    LTList_ForEach(node, &netQueue->queue->listners) {
        LTNetQueueListners *listner = LT_CONTAINER_OF(node, LTNetQueueListners, node);
        if (listner->eventCallback == eventCallback && listner->tid == netQueue->thread->GetCurrentThread()) {
            LTList_Remove(&listner->node);
            listner->relaseCb(kLTThread_ReleaseReason_EventUnregistered, listner->pReceiverClientData);
            lt_free(listner);
            break;
        }
    } LTList_EndForEach;
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
    return true;
}

static bool LTNetQueueImpl_Push(LTNetQueue *queue, void *data) LT_ISR_SAFE{
    if (!queue || !data) return false;
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    u32 tail = netQueue->queue->tail;
    u32 next_tail = (tail + 1) % netQueue->queue->queueSize;
    if (next_tail == netQueue->queue->head) {
        netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
        return false;
    }
    netQueue->queue->buffer[tail] = data;
    netQueue->queue->tail = next_tail;
    u32 curr_size = LTNetQueueImpl_Size(queue);
    u32 watermark = netQueue->queue->watermark;
    if (curr_size >= watermark) {
        netQueue->iEvent->NotifyEvent(netQueue->queue->event, kLTNetQueueEvent_ExceedWatermark, curr_size, watermark, netQueue->core->GetKernelTime().nNanoseconds);
    }
    if (!netQueue->queue->disallowance_grant) {
        netQueue->queue->disallowance_grant = lt_disallowsleepmode();
    }
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
    return true;
}

static void* LTNetQueueImpl_Pop(LTNetQueue *queue) LT_ISR_SAFE{
    if (!queue) return NULL;
    bool fire_notif = (LTNetQueueImpl_Size(queue) >= LTNetQueueImpl_GetWatermark(queue));
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    u32 head = netQueue->queue->head;
    if (head == netQueue->queue->tail) {
        netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
        return NULL;
    }
    void *data = netQueue->queue->buffer[head];
    netQueue->queue->head = (head + 1) % netQueue->queue->queueSize;
    u32 curr_size = LTNetQueueImpl_Size(queue);
    u32 watermark = netQueue->queue->watermark;
    if (fire_notif && (curr_size < watermark)) {
        netQueue->iEvent->NotifyEvent(netQueue->queue->event, kLTNetQueueEvent_FallsUnderWatermark, curr_size, watermark, netQueue->core->GetKernelTime().nNanoseconds);
    }
    //If queue is empty then release the disallowance grant
    if ((!curr_size) && (netQueue->queue->disallowance_grant)) {
        lt_reallowsleepmode(netQueue->queue->disallowance_grant);
        netQueue->queue->disallowance_grant = 0;
    }
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
    return data;
}

static bool LTNetQueueImpl_IsFull(LTNetQueue *queue) {
    if (!queue) return false;
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    u32 tail = netQueue->queue->tail;
    u32 next_tail = (tail + 1) % netQueue->queue->queueSize;
    bool is_full = (next_tail == netQueue->queue->head);
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
    return is_full;
}

static void* LTNetQueueImpl_Peek(LTNetQueue *queue) {
    if (!queue) return NULL;
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    u32 head = netQueue->queue->head;
    if (head == netQueue->queue->tail) {
        netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
        return NULL;
    }
    void *data = netQueue->queue->buffer[head];
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
    return data;
}

static void LTNetQueueImpl_Notify(LTNetQueue *queue) {
    if (!queue) return;
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    LTList_ForEach(node, &netQueue->queue->listners) {
        LTNetQueueListners *listner = LT_CONTAINER_OF(node, LTNetQueueListners, node);
        if (listner->eventCallback) {
            netQueue->thread->QueueTaskProcIfRequired(listner->tid, listner->eventCallback, NULL, listner->pReceiverClientData);
        }
    } LTList_EndForEach;
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
    return;
}

static bool LTNetQueueImpl_RegisterEventCallback(LTNetQueue *queue, LTNetQueueEventCallback eventCallback, LTNetQueueEventClientDataReleaseProc relCallback, void *pReceiverClientData, bool bNotifyEventStateImmediately) {
    if (!queue || !eventCallback) return false;
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    netQueue->iEvent->RegisterForEvent(netQueue->queue->event, eventCallback, relCallback, pReceiverClientData, bNotifyEventStateImmediately);
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
    return true;
}

static bool LTNetQueueImpl_UnRegisterEventCallback(LTNetQueue *queue, LTNetQueueEventCallback eventCallback) {
    if (!queue || !eventCallback) return false;
    LTNetQueueImpl *netQueue = (LTNetQueueImpl *)queue;
    netQueue->queue->lock->API->Lock(netQueue->queue->lock);
    netQueue->iEvent->UnregisterFromEvent(netQueue->queue->event, eventCallback);
    netQueue->queue->lock->API->Unlock(netQueue->queue->lock);
    return true;
}

define_LTObjectImplPublic(LTNetQueue, LTNetQueueImpl,
    Create, Destroy, Push, Pop, Notify, RegisterNotification, UnRegisterNotification, Size, SetWatermark, GetWatermark, RegisterEventCallback, UnRegisterEventCallback, IsFull, Peek
);

define_LTOBJECT_EXPORTLIBRARY(LTNetQueue, 1, LTNetQueueImpl);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  27-Aug-24   galba       created
 */

