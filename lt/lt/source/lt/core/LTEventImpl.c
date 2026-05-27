/******************************************************************************
 * <lt/core/LTEventImpl.c>                      LTEvent - asynchronous notifier
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTEventImpl.h"
#include "LTHandle.h"
#include "LTCoreImpl.h"
#include "LTThreadImpl.h"

/*________________________
 / LTEventImpl #defines */
#define LTEVENT_LTARGS_POOL_LIMIT                   32      /* limit of per-event notifications in flight at once                                    */
#define LTEVENT_EVENTDATA_POOL_LIMIT                48      /* limit of simultaneous system wide event notifications in flight at once               */
#define LTEVENT_EVENTDISPATCHCLIENTDATA_POOL_LIMIT  32      /* limit of threads system wide being notified of an event (any event) at the same time  */

DEFINE_LTLOG_SECTION("LTEvent");
declare_LTTRACE_STREAM(event);

/*____________________________________
 / LTEventImpl Forward Declarations */
 static const ILTEvent s_ILTEvent;

/*________________________
 / LTEventImpl Typedefs */
typedef struct EventReceiverRecord {
    LTThread                            hReceiverThread;
    void *                              pReceiverEventProc;
    LTThread_ClientDataReleaseProc *    pReceiverClientDataReleaseProc;
    void *                              pReceiverClientData;
    union {
        struct EventReceiverRecord *        pNext;
        LTEvent                             hEvent; /* added in union with pNext for async immediate notification */
    };
} EventReceiverRecord;

LT_STATIC_ASSERT_SIZE_32_64(EventReceiverRecord, 20, 40)

typedef struct LTArgsPoolNode {
    LTArgs * pArgs;
    struct LTArgsPoolNode *pNext;
} LTArgsPoolNode;
LT_STATIC_ASSERT_SIZE_32_64(LTArgsPoolNode, 8, 16)

typedef struct LTArgsPool {
    LTArgsPoolNode *pHeadNodes;
    LTArgsPoolNode *pHeadBlanks;
    u32            nodeCount;
    u32            blankCount;
} LTArgsPool;
LT_STATIC_ASSERT_SIZE_32_64(LTArgsPool, 16, 24)

typedef struct LTEventImpl {
    LTArgsDescriptor *                      pEventArgsDescriptor;
    LTEvent_DispatchProc *                  pEventDispatchProc;
    LTEvent_DispatchCompleteProc *          pDispatchCompleteProc;
    LTEvent_NotifyImmediateEventStateProc * pNotifyImmediateEventStateProc;
    void *                                  pNotifyImmediateEventStateClientData;
    EventReceiverRecord *                   pReceiverListHead;
    LTMutex *                               mutex;               /* protects pReceiverListHead  and argsPool */
    LTArgsPool                              argsPool;
    u32                                     reserved;
} LTEventImpl;
LT_STATIC_ASSERT_SIZE_32_64(LTEventImpl, 48, 88)

typedef struct EventData {
    struct EventData               *pNext;
    LTEvent_DispatchProc           *pDispatchProc;         /*  4 or  8 */
    LTEvent_DispatchCompleteProc   *pDispatchCompleteProc; /*  8 or 16 */
    LTArgs                         *pArgs;                 /* 12 or 24 */
    LTThread                        hNotifyingThread;      /* 16 or 28*/
    LTEvent                         hEvent;                /* 20 or 32 */
    LTAtomic                        nDispatchCount;        /* 24 or 36 */
    u32                             nTotalDispatchees;     /* 28 or 40 */
} EventData;
LT_STATIC_ASSERT_SIZE_32_64(EventData, 32, 48)

typedef struct EventDispatchClientData {
    struct EventDispatchClientData  *pNext;
    void                            *pReceiverEventProc;
    void                            *pReceiverClientData;
    EventData                       *pEventData;
} EventDispatchClientData;

/*________________________________
 / LTEventImpl static variables */
static EventData                *s_EventDataPoolHead = NULL;
static EventDispatchClientData  *s_EventDispatchClientDataPoolHead = NULL;
static u32                       s_nEventDataPoolCount = 0;
static u32                       s_nEventDispatchClientDataPoolCount = 0;
static LTMutex                  *s_poolMutex = NULL;

/*__________________________________________
 / LTEventImpl ILTEvent private functions */
static LTArgs * LTEventImpl_PoolGetOrMallocateArgsInternal(LTArgsPool *pool, LTArgsDescriptor * pDescriptor) {
    LTArgs *pArgs = NULL;
    if (pool->pHeadNodes) {
        LTArgsPoolNode *node = pool->pHeadNodes;
        pool->pHeadNodes = pool->pHeadNodes->pNext;
        node->pNext = pool->pHeadBlanks;
        pool->pHeadBlanks = node;
        pArgs = node->pArgs;
        node->pArgs = NULL;
        pool->nodeCount--;
        pool->blankCount++;
    }
    else pArgs = LTCoreImpl_CreateArgsFromDescriptor(pDescriptor);
    return pArgs;
}

static void LTEventImpl_PoolReturnOrDestroyArgsInternal(LTArgsPool *pool, LTArgs *pArgs, LTEvent hEvent) {
    LTArgsPoolNode *node = NULL;
    if (pool->nodeCount < LTEVENT_LTARGS_POOL_LIMIT) {
        if (pool->pHeadBlanks) {
            node = pool->pHeadBlanks;
            pool->pHeadBlanks = node->pNext;
            pool->blankCount--;
        }
        else node = core_malloc(sizeof(*node));
        if (node) {
            LTCoreImpl_TrimArgs(pArgs);
            node->pArgs = pArgs;
            pArgs = NULL;
            node->pNext = pool->pHeadNodes;
            pool->pHeadNodes = node;
            pool->nodeCount++;
        }
    }
    else {
        node = pool->pHeadBlanks;
        while (node) {
            pool->pHeadBlanks = node->pNext;
            core_free(node);
            node = pool->pHeadBlanks;
            pool->blankCount--;
        }
        LT_ASSERT(pool->blankCount == 0);
        char name[kLTThread_MaxNameBuff];
        LTThreadImpl_GetName(LTThreadImpl_GetCurrentThread(), name);
        LTLOG("args.pool", "Args pool reached %d limit for event 0x%X th %s", LTEVENT_LTARGS_POOL_LIMIT, (int)hEvent, name);
    }
    if (pArgs) LTCoreImpl_DestroyArgs(pArgs);
}

static void LTEventImpl_DestroyArgsPoolContents(LTArgsPool *pool) {
    LTArgsPoolNode *node = pool->pHeadNodes;
    while (node) {
        pool->pHeadNodes = node->pNext;
        LTCoreImpl_DestroyArgs(node->pArgs);
        core_free(node);
        pool->nodeCount--;
        node = pool->pHeadNodes;
    }
    node = pool->pHeadBlanks;
    while (node) {
        pool->pHeadBlanks = node->pNext;
        core_free(node);
        pool->blankCount--;
        node = pool->pHeadBlanks;
    }
    LT_ASSERT(pool->nodeCount == 0);
    LT_ASSERT(pool->blankCount == 0);
}

static EventData * LTEventImpl_PoolGetOrMallocateEventData(void) {
    EventData * retVal = NULL;
    s_poolMutex->API->Lock(s_poolMutex);
    if (s_EventDataPoolHead) {
        retVal = s_EventDataPoolHead;
        s_EventDataPoolHead = s_EventDataPoolHead->pNext;
        retVal->pNext = NULL;
        s_nEventDataPoolCount--;
    }
    s_poolMutex->API->Unlock(s_poolMutex);
    if (NULL == retVal) {
        retVal = core_malloc(sizeof(*retVal));
        if (retVal) retVal->pNext = NULL;
    }
    return retVal;
}

static void LTEventImpl_PoolReturnOrFreeEventData(EventData *pEventData) {
    s_poolMutex->API->Lock(s_poolMutex);
    if (s_nEventDataPoolCount < LTEVENT_EVENTDATA_POOL_LIMIT) {
        pEventData->pNext = s_EventDataPoolHead;
        s_EventDataPoolHead = pEventData;
        s_nEventDataPoolCount++;
        pEventData = NULL;
    }
    else {
        char name[kLTThread_MaxNameBuff];
        LTThreadImpl_GetName(LTThreadImpl_GetCurrentThread(), name);
        LTLOG("eventdata.pool", "EventData pool reached %d limit th %s", LTEVENT_EVENTDATA_POOL_LIMIT, name);
    }
    s_poolMutex->API->Unlock(s_poolMutex);
    if (pEventData) core_free(pEventData);
}

static EventDispatchClientData * LTEventImpl_PoolGetOrMallocateEventDispatchClientData(void) {
    EventDispatchClientData * retVal = NULL;
    s_poolMutex->API->Lock(s_poolMutex);
    if (s_EventDispatchClientDataPoolHead) {
        retVal = s_EventDispatchClientDataPoolHead;
        s_EventDispatchClientDataPoolHead = s_EventDispatchClientDataPoolHead->pNext;
        retVal->pNext = NULL;
        s_nEventDispatchClientDataPoolCount--;
    }
    s_poolMutex->API->Unlock(s_poolMutex);
    if (NULL == retVal) {
        retVal = core_malloc(sizeof(*retVal));
        if (retVal) retVal->pNext = NULL;
    }
    return retVal;
}
static void LTEventImpl_PoolReturnOrFreeEventDispatchClientData(EventDispatchClientData *pClientData) {
    s_poolMutex->API->Lock(s_poolMutex);
    if (s_nEventDispatchClientDataPoolCount < LTEVENT_EVENTDISPATCHCLIENTDATA_POOL_LIMIT) {
        pClientData->pNext = s_EventDispatchClientDataPoolHead;
        s_EventDispatchClientDataPoolHead = pClientData;
        s_nEventDispatchClientDataPoolCount++;
        pClientData = NULL;
    }
    else {
        LTLOG("clientdata.pool", "DispatchClientData pool reached %d limit", LTEVENT_EVENTDISPATCHCLIENTDATA_POOL_LIMIT);
    }
    s_poolMutex->API->Unlock(s_poolMutex);
    if (pClientData) core_free(pClientData);
}

static void
LTEventImpl_InvokeEventDispatchProc(void * pClientData) {
    EventDispatchClientData * pCD = (EventDispatchClientData *)pClientData;
    LTThreadImpl_SetUnregisterCurrentEventFlag(false);
    LTTRACE_NUMERIC(event, 1, LTTRACE_PTR(pCD->pEventData->pDispatchProc));
    pCD->pEventData->pDispatchProc(
        pCD->pEventData->hEvent,
        pCD->pReceiverEventProc,
        pCD->pEventData->pArgs,
        pCD->pReceiverClientData);
    LTTRACE_NUMERIC(event, 0, LTTRACE_PTR(pCD->pEventData->pDispatchProc));
    if (LTThreadImpl_IsUnregisterCurrentEventFlagSet()) {
        LTEventImpl_UnregisterFromEvent(pCD->pEventData->hEvent, pCD->pReceiverEventProc);
        LTThreadImpl_SetUnregisterCurrentEventFlag(false);
    }
}

static void
LTEventImpl_NotifyEventStateImmediate(void * pClientData) {
   /* This is running in the context of the receiver thread for the initial immediate asynchronous notification;
       add the EventReceiver record to the receiver list and make the function call for the immediate event state dispatch */
    EventReceiverRecord * pRecord = (EventReceiverRecord *)pClientData;
    LTEvent hEvent = pRecord->hEvent;
    LTEventImpl * pEventImpl = (LTEventImpl *)LTHandle_ReservePrivateData(hEvent);

    if (pEventImpl) {
        /* insert the receiver record into the receiver list and do the immediate notify */
        pEventImpl->mutex->API->Lock(pEventImpl->mutex);
        pRecord->pNext = pEventImpl->pReceiverListHead;
        pEventImpl->pReceiverListHead = pRecord;
        pEventImpl->mutex->API->Unlock(pEventImpl->mutex);
        (*pEventImpl->pNotifyImmediateEventStateProc)(hEvent, pEventImpl->pNotifyImmediateEventStateClientData, pRecord->pReceiverEventProc, pRecord->pReceiverClientData);
        LTHandle_ReleasePrivateData(hEvent, pEventImpl);
    }
    else {
        // event was destroyed while this was in flight, free pRecord
        core_free(pRecord);
    }
}

static void
LTEventImpl_NotifyEventStateComplete(LTThread_ReleaseReason releaseReason, void * pClientData) {
    if (releaseReason != kLTThread_ReleaseReason_TaskProcComplete) {
        // LTEventImpl_NotifyEventStateImmediate() did not run; either task queue full or thread terminated, etc.
        // need to free the record
        core_free(pClientData);
    }
}

static void
LTEventImpl_ExecuteFinalizeAndFreeEventData(LTThread_ReleaseReason releaseReason, void * pClientData) {
    /* this is the TaskClientDataReleaseProc for the FinalizeAndFree operation */
    LT_UNUSED(releaseReason);
    EventData * pEventData = (EventData *)pClientData;
    LTArgs *pArgs = pEventData->pArgs;
    LTEvent hEvent = pEventData->hEvent;
    pEventData->pArgs = NULL;
    if (pEventData->pDispatchCompleteProc) pEventData->pDispatchCompleteProc(hEvent, pArgs); /* let the notifier of the event reclaim any pointed to values in the args that need reclamation */
    LTEventImpl *pEventImpl = LTHandle_ReservePrivateData(hEvent);
    if (pEventImpl) {
        pEventImpl->mutex->API->Lock(pEventImpl->mutex);
        LTEventImpl_PoolReturnOrDestroyArgsInternal(&pEventImpl->argsPool, pArgs, hEvent);
        pEventImpl->mutex->API->Unlock(pEventImpl->mutex);
        pArgs = NULL;
        LTHandle_ReleasePrivateData(hEvent, pEventImpl);
    }
    if (pArgs) LTCoreImpl_DestroyArgs(pArgs); /* destroy the args structure */
    LTEventImpl_PoolReturnOrFreeEventData(pEventData); /* free the event data */
}

static void LTEventImpl_FinalizeAndFreeEventData(EventData * pEventData) {
    if (NULL == pEventData->pDispatchCompleteProc || 0 == pEventData->hNotifyingThread) {
        /* DRW 21-Dec-20 : optimization
           If an event has a dispatch complete proc we want it to execute on the sending thread, where we also free the event args and the EventData struct
           used by this implementation. - But if the event doesn't have a dispatch complete proc, no need to queue a task proc to the event sender to
           merely free the args and struct EventData - just do it on this thread - we're already here.
           DRW 22-Oct-22 : call the complete proc on this thread if it wasn't an LT thread that called Notify (e.g. notified from ISR) */
        LTEventImpl_ExecuteFinalizeAndFreeEventData(kLTThread_ReleaseReason_Because, pEventData);
    }
    else {
        LTThreadImpl_QueueTaskProc(pEventData->hNotifyingThread, NULL, &LTEventImpl_ExecuteFinalizeAndFreeEventData, pEventData);
    }
}

static void
LTEventImpl_OnEventDispatchCompleteClientDataRelease(LTThread_ReleaseReason releaseReason, void * pClientData) {
    LT_UNUSED(releaseReason);
    EventDispatchClientData * pCD = (EventDispatchClientData *)pClientData;
    if ((pCD->pEventData->nTotalDispatchees - 1) == LTAtomic_FetchAdd(&pCD->pEventData->nDispatchCount, 1)) {
        // the last EventReceiverProc just finished executing for this specific notification cycle, finalize the notification's event data
        LTEventImpl_FinalizeAndFreeEventData(pCD->pEventData);
    }
    // free my client data
    LTEventImpl_PoolReturnOrFreeEventDispatchClientData(pCD); /* free the event data */
}


static void LTEventImpl_DisposeStaleReceiverRecords(void *pClientData) {
    EventReceiverRecord * pCurr = (EventReceiverRecord *)pClientData;
    while (pCurr) {
        EventReceiverRecord * pNext = pCurr->pNext;
        if (pCurr->pReceiverClientDataReleaseProc && pCurr->pReceiverClientData) pCurr->pReceiverClientDataReleaseProc(kLTThread_ReleaseReason_EventPurged, pCurr->pReceiverClientData);
        core_free(pCurr);
        pCurr = pNext;
    }
}

/*_______________________________________
 / LTEventImpl ILTEvent init functions */
 void LTEventImpl_Init(void) {
     s_poolMutex = lt_createobject(LTMutex);
 }

 void LTEventImpl_Fini(void) {
     EventData * pData = s_EventDataPoolHead;
     while (pData) {
         s_EventDataPoolHead = pData->pNext;
         core_free(pData);
         s_nEventDataPoolCount--;
         pData = s_EventDataPoolHead;
     }
     EventDispatchClientData * pClientData = s_EventDispatchClientDataPoolHead;
     while (pClientData) {
         s_EventDispatchClientDataPoolHead = pClientData->pNext;
         core_free(pClientData);
         s_nEventDispatchClientDataPoolCount--;
         pClientData = s_EventDispatchClientDataPoolHead;
     }
     LT_ASSERT(s_nEventDataPoolCount == 0);
     LT_ASSERT(s_nEventDispatchClientDataPoolCount == 0);
     if (s_poolMutex) {
         lt_destroyobject(s_poolMutex);
         s_poolMutex = NULL;
     }
 }

/*___________________________________________
 / LTEventImpl LTCore public api functions */
LTEvent
LTEventImpl_CreateEvent(const LTArgsDescriptor * pEventArgsDescriptor, LTEvent_DispatchProc * pEventDispatchProc, LTEvent_DispatchCompleteProc * pEventDispatchCompleteProc, LTEvent_NotifyImmediateEventStateProc * pNotifyImmediateEventStateProc, void *pNotifyImmediateEventStateClientData) { LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (NULL == pEventDispatchProc || NULL == pEventArgsDescriptor || 0 == pEventArgsDescriptor->nNumArgs) return 0;
    int nLen;
    LTEvent hEvent = LTHandle_CreateHandle((LTInterface *)&s_ILTEvent, sizeof(LTEventImpl));
    LTEventImpl * pEventImpl = LTHandle_ReservePrivateData(hEvent);

    if (pEventImpl) {
        pEventImpl->pEventArgsDescriptor = NULL;
        pEventImpl->pEventDispatchProc = pEventDispatchProc;
        pEventImpl->pDispatchCompleteProc = pEventDispatchCompleteProc;
        pEventImpl->pNotifyImmediateEventStateProc = pNotifyImmediateEventStateProc;
        pEventImpl->pNotifyImmediateEventStateClientData = pNotifyImmediateEventStateClientData;
        pEventImpl->pReceiverListHead = NULL;
        lt_memset(&pEventImpl->argsPool, 0, sizeof(pEventImpl->argsPool));
        pEventImpl->reserved = 0;
        do {
            if (0 == (pEventImpl->mutex = lt_createobject(LTMutex))) break;
            nLen = sizeof(LT_SIZE) + (sizeof(LTArgType) * pEventArgsDescriptor->nNumArgs);
            if (NULL == (pEventImpl->pEventArgsDescriptor = (LTArgsDescriptor *)core_malloc(nLen))) break;
            lt_memcpy(pEventImpl->pEventArgsDescriptor, pEventArgsDescriptor, nLen);
            LTHandle_ReleasePrivateData(hEvent, pEventImpl);
            return hEvent;
        }
        while (false);
        LTHandle_ReleasePrivateData(hEvent, pEventImpl);
        LTHandle_DestroyHandle(hEvent);
    }
    return 0;
}

/*_____________________________________________
 / LTEventImpl ILTEvent public api functions */
static void
LTEventImpl_RegisterThreadForEvent(LTEvent hEvent, LTThread hReceiverThread, void * pReceiverEventProc, LTThread_ClientDataReleaseProc * pReceiverClientDataReleaseProc, void * pReceiverClientData, bool bNotifyEventStateImmediately) { LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (! LTHandle_IsHandleValid(hReceiverThread)) return;
    LTEventImpl * pEventImpl = (LTEventImpl *)LTHandle_ReservePrivateData(hEvent); if (! pEventImpl) return;
    EventReceiverRecord * pRecord;
    // check to see if we are already registered
    pEventImpl->mutex->API->Lock(pEventImpl->mutex);
        pRecord = pEventImpl->pReceiverListHead;
        while (pRecord && (pRecord->hReceiverThread != hReceiverThread || pRecord->pReceiverEventProc != pReceiverEventProc)) pRecord = pRecord->pNext;
    pEventImpl->mutex->API->Unlock(pEventImpl->mutex);
    if (pRecord) {
        LTHandle_ReleasePrivateData(hEvent, pEventImpl);
        return; // already in list
    }
    // not in the list, make a record and add it
    if (NULL == (pRecord = (EventReceiverRecord *)core_malloc(sizeof(*pRecord)))) {
        LTHandle_ReleasePrivateData(hEvent, pEventImpl);
        return;
    }
    pRecord->hReceiverThread = hReceiverThread;
    pRecord->pReceiverEventProc = pReceiverEventProc;
    pRecord->pReceiverClientDataReleaseProc = pReceiverClientDataReleaseProc;
    pRecord->pReceiverClientData = pReceiverClientData;
    if (bNotifyEventStateImmediately && pEventImpl->pNotifyImmediateEventStateProc) {
        /* client wants immediate async notification of the state, we queue a task proc that does that immediate notify to his thread and
           will add in pRecord to our registration list then */
        pRecord->hEvent = hEvent;
        LTThreadImpl_QueueTaskProc(hReceiverThread, &LTEventImpl_NotifyEventStateImmediate, &LTEventImpl_NotifyEventStateComplete, pRecord);
    }
    else {
        pEventImpl->mutex->API->Lock(pEventImpl->mutex);
        pRecord->pNext = pEventImpl->pReceiverListHead;
        pEventImpl->pReceiverListHead = pRecord;
        pEventImpl->mutex->API->Unlock(pEventImpl->mutex);
    }
    LTHandle_ReleasePrivateData(hEvent, pEventImpl);
}

static bool
LTEventImpl_UnregisterThreadFromEvent(LTEvent hEvent, LTThread hReceiverThread, void * pReceiverEventProc) { LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
   if (! LTHandle_IsHandleValid(hReceiverThread)) return false; /* not an LT thread */
    LTEventImpl * pEventImpl =  (LTEventImpl *)LTHandle_ReservePrivateData(hEvent); if (! pEventImpl) return false;

    // find the record and unlink it if it exists
    EventReceiverRecord * pPrev = NULL;
    EventReceiverRecord * pCurr;
    pEventImpl->mutex->API->Lock(pEventImpl->mutex);
        pCurr = pEventImpl->pReceiverListHead;
        while (pCurr) {
            if (pCurr->hReceiverThread == hReceiverThread && pCurr->pReceiverEventProc == pReceiverEventProc) {
                if (pPrev == NULL) pEventImpl->pReceiverListHead = pCurr->pNext;
                else pPrev->pNext = pCurr->pNext;
                break;
            }
            pPrev = pCurr;
            pCurr = pCurr->pNext;
        }
    pEventImpl->mutex->API->Unlock(pEventImpl->mutex);
    if (pCurr) {
        if (pCurr->pReceiverClientDataReleaseProc && pCurr->pReceiverClientData) pCurr->pReceiverClientDataReleaseProc(kLTThread_ReleaseReason_EventUnregistered, pCurr->pReceiverClientData);
        core_free(pCurr);
        LTHandle_ReleasePrivateData(hEvent, pEventImpl);
        return true;
    }
    LTHandle_ReleasePrivateData(hEvent, pEventImpl);
    return false;
}

void
LTEventImpl_RegisterForEvent(LTEvent hEvent, void * pReceiverEventProc, LTThread_ClientDataReleaseProc * pReceiverClientDataReleaseProc, void * pReceiverClientData, bool bNotifyEventStateImmediately) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTEventImpl_RegisterThreadForEvent(hEvent, LTCoreImpl_GetILTThread()->GetCurrentThread(), pReceiverEventProc, pReceiverClientDataReleaseProc, pReceiverClientData, bNotifyEventStateImmediately);
}

bool
LTEventImpl_UnregisterFromEvent(LTEvent hEvent, void * pReceiverEventProc) { LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    return LTEventImpl_UnregisterThreadFromEvent(hEvent, LTCoreImpl_GetILTThread()->GetCurrentThread(), pReceiverEventProc);
}

void
LTEventImpl_UnregisterFromCurrentEvent(void) { LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTThreadImpl_SetUnregisterCurrentEventFlag(true);
}

void
LTEventImpl_NotifyEvent(LTEvent hEvent, ...) { LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();

    EventDispatchClientData * pClientData;
    EventData * pEventData = NULL;
    LTArgs * pArgs = NULL;
    LTEventImpl * pEventImpl = (LTEventImpl *)LTHandle_ReservePrivateData(hEvent); if (! pEventImpl) return;
    EventReceiverRecord *pPrev = NULL, *pCurr = NULL, *pStale = NULL;

    /* sweep the receiver list and prune out any that have no longer active receiver threads */
    pEventImpl->mutex->API->Lock(pEventImpl->mutex);
    pCurr = pEventImpl->pReceiverListHead;
    while (pCurr) {
        if (! LTThreadImpl_ThreadHandleInActiveRunState(pCurr->hReceiverThread)) {
            if (pPrev) pPrev->pNext = pCurr->pNext;
            else pEventImpl->pReceiverListHead = pCurr->pNext;
            pCurr->pNext = pStale;
            pStale = pCurr;
            pCurr = pPrev ? pPrev->pNext : pEventImpl->pReceiverListHead;
        }
        else {
            pPrev = pCurr;
            pCurr = pCurr->pNext;
        }
    }
    pCurr = pEventImpl->pReceiverListHead;
    /* get args and event data */
    if (pCurr || pEventImpl->pDispatchCompleteProc) {
        pArgs = LTEventImpl_PoolGetOrMallocateArgsInternal(&pEventImpl->argsPool, pEventImpl->pEventArgsDescriptor);
    }
    pEventImpl->mutex->API->Unlock(pEventImpl->mutex);

    if (pStale) LTThreadImpl_QueueTaskProc(LTCoreImpl_GetLTCoreImpl()->hThreadCore, &LTEventImpl_DisposeStaleReceiverRecords, NULL, pStale);

    if (pArgs) {
        lt_va_list vaList;
        lt_va_start(vaList, hEvent);
        LTCoreImpl_UpdateArgsFrom_lt_va_list(pArgs, vaList);
        lt_va_end(vaList);

        pEventData = LTEventImpl_PoolGetOrMallocateEventData();
    }
    else pCurr = NULL; /* need pArgs to notify */

    if (pEventData) {
        // add in the common housekeeping data
        pEventData->pDispatchProc = pEventImpl->pEventDispatchProc;
        pEventData->pDispatchCompleteProc = pEventImpl->pDispatchCompleteProc;
        pEventData->pArgs = pArgs;
        pEventData->hNotifyingThread = LTThreadImpl_GetCurrentThread();
        pEventData->hEvent = hEvent;
        LTAtomic_Store(&pEventData->nDispatchCount, 0);
        pEventData->nTotalDispatchees = 0;
    }
    else pCurr = NULL;  /* need pEventData to notify */

    if (NULL == pCurr) {
        /* no registered receivers or malloc failure preventing dispatch */
        if (pArgs && pEventImpl->pDispatchCompleteProc) {
            if (pEventData) {
                LTThreadImpl_QueueTaskProc(pEventData->hNotifyingThread, NULL, &LTEventImpl_ExecuteFinalizeAndFreeEventData, pEventData);
                pEventData = NULL;
                pArgs = NULL;
            }
            else {
                pEventImpl->pDispatchCompleteProc(hEvent, pArgs);
            }
        }
        if (pArgs) {
            pEventImpl->mutex->API->Lock(pEventImpl->mutex);
            LTEventImpl_PoolReturnOrDestroyArgsInternal(&pEventImpl->argsPool, pArgs, hEvent);
            pEventImpl->mutex->API->Unlock(pEventImpl->mutex);
        }
        if (pEventData) LTEventImpl_PoolReturnOrFreeEventData(pEventData);
        LTHandle_ReleasePrivateData(hEvent, pEventImpl);
        return;
    }

    /* lock the mutex and count the receivers as pEventData->nTotalDispatchees - this has to be done before queueing any task procs */
    pEventImpl->mutex->API->Lock(pEventImpl->mutex);
    pCurr = pEventImpl->pReceiverListHead;
    while (pCurr) { pEventData->nTotalDispatchees++; pCurr = pCurr->pNext; }

    // queue the task procs
    if (pEventData->nTotalDispatchees) {
        pCurr = pEventImpl->pReceiverListHead;
        while (pCurr) {
            pClientData = LTEventImpl_PoolGetOrMallocateEventDispatchClientData();
            if (pClientData) {
                pClientData->pReceiverEventProc = pCurr->pReceiverEventProc;
                pClientData->pReceiverClientData = pCurr->pReceiverClientData;
                pClientData->pEventData = pEventData;

                LTThreadImpl_QueueTaskProc(pCurr->hReceiverThread, &LTEventImpl_InvokeEventDispatchProc, &LTEventImpl_OnEventDispatchCompleteClientDataRelease, pClientData);
            }
            else {
                // failed to allocate a client data for this receiver, increment the 'dispatched' count and check to see if we need to free
                if ((pEventData->nTotalDispatchees - 1) == LTAtomic_FetchAdd(&pEventData->nDispatchCount, 1)) {
                    // we are the last one to complete, finalize and free the event data
                    LT_ASSERT(NULL == pCurr->pNext);
                    LTEventImpl_FinalizeAndFreeEventData(pEventData);
                }
            }
            pCurr = pCurr->pNext;
        }
    }
    else {
        // dispatching nothing
        LTEventImpl_FinalizeAndFreeEventData(pEventData);
    }
    pEventImpl->mutex->API->Unlock(pEventImpl->mutex);
    LTHandle_ReleasePrivateData(hEvent, pEventImpl);
}

static void
LTEventImpl_NotifyEventFromISR(LTEvent_ISRThreadProxyNotifyProc *pThreadProxyNotifyProc, void *pClientData) LT_ISR_SAFE {
    LTThreadImpl_QueueTaskProc(LTCoreImpl_GetLTCoreImpl()->hThreadCore, pThreadProxyNotifyProc, NULL, pClientData);
}

/*__________________________________________
 / LTEventImpl ILTEvent private functions */
static void
LTEventImpl_OnDestroyHandle(LTHandle hEvent) {
    LTEventImpl * pImpl =LTHandle_ReservePrivateData(hEvent);
    if (pImpl) {
        EventReceiverRecord * pRecord = pImpl->pReceiverListHead;
        while (pRecord) {
            pImpl->pReceiverListHead = pRecord->pNext;
            if (pRecord->pReceiverClientDataReleaseProc && pRecord->pReceiverClientData) pRecord->pReceiverClientDataReleaseProc(kLTThread_ReleaseReason_EventDestroyed, pRecord->pReceiverClientData);
            core_free(pRecord);
            pRecord = pImpl->pReceiverListHead;
        }
        if (pImpl->pEventArgsDescriptor) {
            core_free(pImpl->pEventArgsDescriptor);
            pImpl->pEventArgsDescriptor = NULL;
        }
        if (pImpl->mutex) {
            lt_destroyobject(pImpl->mutex);
            pImpl->mutex = NULL;
        }
        LTEventImpl_DestroyArgsPoolContents(&pImpl->argsPool);
        LTHandle_ReleasePrivateData(hEvent, pImpl);
    }
}

/*____________________________________
 / LTEventImpl Interface Definition */
define_LTLIBRARY_INTERFACE(ILTEvent, LTEventImpl_OnDestroyHandle)
    .RegisterForEvent           = LTEventImpl_RegisterForEvent,
    .UnregisterFromEvent        = LTEventImpl_UnregisterFromEvent,
    .RegisterThreadForEvent     = LTEventImpl_RegisterThreadForEvent,
    .UnregisterThreadFromEvent  = LTEventImpl_UnregisterThreadFromEvent,
    .UnregisterFromCurrentEvent = LTEventImpl_UnregisterFromCurrentEvent,
    .NotifyEvent                = LTEventImpl_NotifyEvent,
    .NotifyEventFromISR         = LTEventImpl_NotifyEventFromISR,
LTLIBRARY_DEFINITION;


/******************************************************************************
 *  LOG
 ******************************************************************************
 *  24-Aug-20   augustus    created
 *  10-Sep-20   augustus    redid naming to make it clearer per Carl's suggestions;
 *                          added Unregister functions
 *  21-Dec-20   augustus    don't try to call pDispatchCompleteProc when the event
 *                          doesn't have one; optimize the case where it doesn't.
 *  15-Jan-21   augustus    added RegisterThreadForEvent and UnregisterThreadFromEvent
 *  30-Mar-21   augustus    added NotifyLastRegisteredReceiverOfEvent
 *  22-Jul-21   augustus    made Register... functions non-static for LTCore lib internal direct access
 *  24-Jul-25   augustus    added NotifyEventFromISR
 */
