/*******************************************************************************
 * LTNetHttpsQueue.c - Implementation of HTTPS queue
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/httpclient/LTNetHttpClient.h>
#include <lt/net/httpsqueue/LTNetHttpsQueue.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

DEFINE_LTLOG_SECTION("net.https.queue");
#define PLOG(...) LTLOG(__VA_ARGS__)
#define P(...)

static struct {
    LTCore       *core;
    ILTEvent     *iEvent;
    u16           queueCount;
} S;

typedef_LTObjectImpl(LTNetHttpsQueue, LTNetHttpsQueueImpl) {
    u32           queueId;
    LTOThread    *thread;
    LTMutex      *mutex;
    LTEvent       hQueueEvent;          // This event handler is always re-created on a new sending request.
    // LTSocket      hSocket;              // TODO the persistent socket of the queue
    LTList        requests;             // queue of requests
    bool          bIsRunning;
    // Stat data
    u32           newRequest;           // accumulated count of new requests
    u32           doneRequest;          // accumulated count of done requests
    u32           completeRequest;      // accumulated count of successfully completed requests
} LTOBJECT_API;

typedef struct {
    LTList_Node   node;
    LTHttpRequest hRequest;
    u32           toRead;
    u32           alreadyRead;
    u8            retryCount;           // starts counting from 1

    u32           expectedStatusCode;   // 0 means no expected status code
    u8            maxRetry;
    u8            secondsTimeout;
    bool          bCloseOnComplete;     // close connection on completion
    bool          bSlot;                // Queue slot; request is handled externally
    bool          bWakeOnTimeout;       // If request timeout will wake the device from sleep mode

    // Save callback data here so we can register them on sending and retries.
    LTOThread                *registerThread;
    LTNetHttpsQueueEventProc *queueEventProc;
    void                     *queueEventClientData;

    // Received http data and passed to application on kLTNetHttpsQueueEvent_RequestComplete.
    // Released in DispatchHttpsQueueRequestCompleteProc on success.
    // Released in FreeResponseBuffer on errors.
    char         *response;
} LTNetHttpsRequestQueueEntry;

/** HTTPS queue functions */
static void LogQueue(bool alert, const char *tag, LTNetHttpsRequestQueueEntry *entry, LTNetHttpsQueueImpl *queue) {
    char name[kLTThread_MaxNameBuff];
    entry->registerThread->API->GetName(entry->registerThread, name);
    if (!alert)
        LTLOG(tag,
              "%lu: Req:0x%lx(%s) Retry:%lu/%lu TOut:%lu Queue:(%lu/%lu/%lu)",
              LT_Pu32(queue->queueId),
              LT_PLT_HANDLE(entry->hRequest),
              name,
              LT_Pu32(entry->retryCount),
              LT_Pu32(entry->maxRetry),
              LT_Pu32(entry->secondsTimeout),
              LT_Pu32(queue->newRequest),
              LT_Pu32(queue->doneRequest),
              LT_Pu32(queue->completeRequest));
    else
        LTLOG_YELLOWALERT(tag,
              "%lu: Req:0x%lx(%s) Retry:%lu/%lu TOut:%lu Queue:(%lu/%lu/%lu)",
              LT_Pu32(queue->queueId),
              LT_PLT_HANDLE(entry->hRequest),
              name,
              LT_Pu32(entry->retryCount),
              LT_Pu32(entry->maxRetry),
              LT_Pu32(entry->secondsTimeout),
              LT_Pu32(queue->newRequest),
              LT_Pu32(queue->doneRequest),
              LT_Pu32(queue->completeRequest));
}
#define LOGQUEUE(tag, entry, queue) LogQueue(false, tag, entry, queue)
#define LOGQUEUE_YELLOWALERT(tag, entry, queue) LogQueue(true, tag, entry, queue)

#define LT_NET_HTTPS_QUEUE_MAX_BUF (2048)

static void HttpsQueueRequestTimeoutProc(void *clientData);
static void SendHttpsQueueHeadTaskProc(void *clientData);
static void AdvanceQueue(LTNetHttpsQueueImpl *queue);

static const LTArgsDescriptor s_requestEventArgs = {3, {kLTArgType_lthandle, kLTArgType_u32, kLTArgType_pointer}};

// in app thread
static void DispatchHttpsQueueRequestEventProc(LTEvent hEvent, void *eventProc, LTArgs *eventArgs, void *clientData) {
    LT_UNUSED(hEvent);
    LTNetHttpsQueueEventProc *Callback = (LTNetHttpsQueueEventProc *)eventProc;
    LTHttpRequest hRequest = LTArgs_lthandleAt(0, eventArgs);
    LTNetHttpsQueueEvent event = LTArgs_u32At(1, eventArgs);
    char *response = LTArgs_pointerAt(2, eventArgs);
    P("disp.rsp", "evt %lu %p", LT_Pu32(event), response);
    Callback(hRequest, event, response, clientData);
}

// in queue thread
static void DispatchHttpsQueueRequestCompleteProc(LTEvent hEvent, LTArgs *eventArgs) {
    LT_UNUSED(hEvent);
    LTHttpRequest hRequest = LTArgs_lthandleAt(0, eventArgs);
    if (!hRequest) return;
    char *response = LTArgs_pointerAt(2, eventArgs);
    P("free.rsp", "%p", response);
    // response is not attached to any entry here. Its previously assocatiated entry is already freed.
    lt_free(response);
}

// This is only called on error paths to free response. On happy path, response is freed in DispatchHttpsQueueRequestCompleteProc.
static void FreeResponseBuffer(LTNetHttpsRequestQueueEntry *entry) {
    lt_free(entry->response);
    entry->response = NULL;
    entry->toRead = 0;
    entry->alreadyRead = 0;
}

// in queue thread
static void OnHttpsRequestEventForQueue(LTHttpRequest hRequest, LTHttpRequestEvent event, void *clientData) {
    LTNetHttpsQueueImpl *queue = (LTNetHttpsQueueImpl *)clientData;
    queue->mutex->API->Lock(queue->mutex);
    if (LTList_IsEmpty(&queue->requests)) {
        queue->mutex->API->Unlock(queue->mutex);
        P("evt.empty", "evt %lu req %lx", LT_Pu32(event), LT_Pu32(hRequest));
        return;  // Queue is empty. An earlier timeout already advanced queue.
    }

    LTNetHttpsRequestQueueEntry *head = LT_CONTAINER_OF(queue->requests.pNext, LTNetHttpsRequestQueueEntry, node);
    queue->mutex->API->Unlock(queue->mutex);

    /* Verify that the request being serviced is still the current request. If the response
     * arrived after the timeout was triggered and hRequest's socket was destroyed, we would be
     * passing to the Http client a request with an invalid socket handle. */
    if (head->hRequest != hRequest) {
        P("evt.unmatch.req", "evt %lu req %lx", LT_Pu32(event), LT_Pu32(hRequest));
        return;
    }

    void *request = lt_reservehandleprivatedata(hRequest);
    if (!request) {
        P("evt.invalid.req", "evt %lu req %lx", LT_Pu32(event), LT_Pu32(hRequest));
        AdvanceQueue(queue);
        return;  // Invalid request
    }

    ILTHttpRequest *iRequest = lt_gethandleinterface(ILTHttpRequest, hRequest);
    if (event == LTHttpRequestEvent_HeadComplete) {
        head->toRead = iRequest->GetResponseContentLength(hRequest);
        head->alreadyRead = 0;
        P("evt.rsp", "Head: Status Code: %lu", LT_Pu32(iRequest->GetResponseStatusCode(hRequest)));
        P("evt.rsp", "Head: Content-Length: %lu", LT_Pu32(head->toRead));
        if (head->toRead) { // body data exists
            // OR check will execute the malloc if toRead < maxBufferSize
            // A new buffer is allocated every request
            if ((head->toRead + 1 > LT_NET_HTTPS_QUEUE_MAX_BUF) || !(head->response = lt_malloc(head->toRead + 1))) {
                FreeResponseBuffer(head);
                LTLOG_YELLOWALERT("read.max", "body len %lu over max len %d or OOM. Wait for timeout (%us)", LT_Pu32(head->toRead), LT_NET_HTTPS_QUEUE_MAX_BUF, head->secondsTimeout);
            }
        }

    } else if (event == LTHttpRequestEvent_BodyDataAvailable) {
        while (iRequest->BodyDataAvailable(hRequest) > 0 && head->toRead) {
            u32 readLen = iRequest->ReadBodyData(hRequest, head->response + head->alreadyRead, head->toRead);
            if (readLen == 0) break;
            if (readLen > head->toRead) {
                FreeResponseBuffer(head);
                LTLOG_YELLOWALERT("read.err", "fatal read too much body data. Wait for timeout (%us)", head->secondsTimeout);
                break;
            }
            head->alreadyRead += readLen;
            head->toRead      -= readLen;
        }

    } else if (event == LTHttpRequestEvent_RequestComplete) {
        P("req.complete", "req %lx", LT_Pu32(hRequest));
        iRequest->NoRequestEvent(hRequest, OnHttpsRequestEventForQueue);
        if (head->alreadyRead && head->response) head->response[head->alreadyRead] = '\0'; // force a terminator here as insurance
        u32 statusCode = iRequest->GetResponseStatusCode(hRequest);
        // The entry will be freed by AdvanceQueue, but response is not.
        // So, keep the response. The response will be freed later in DispatchHttpsQueueRequestCompleteProc.
        char *response = head->response;
        head->response = NULL;
        if (head->expectedStatusCode == 0 || head->expectedStatusCode == statusCode) {
            // Request successfully completed. Move to next in queue.
            AdvanceQueue(queue);
            ++queue->completeRequest;
            S.iEvent->NotifyEvent(queue->hQueueEvent, hRequest, kLTNetHttpsQueueEvent_RequestComplete, response);

        } else if (head->retryCount >= head->maxRetry - 1) {
            // Max retry reached, so advance queue, and let client to decide what to do with the unexpected status code.
            PLOG("stcode.noretry", "Unexpected status code %lu/%lu, no more retry", LT_Pu32(statusCode), LT_Pu32(head->expectedStatusCode));
            AdvanceQueue(queue);
            ++queue->completeRequest;
            S.iEvent->NotifyEvent(queue->hQueueEvent, hRequest, kLTNetHttpsQueueEvent_RequestComplete, response);

        } else {
            // Request completed but with unexpected status code. It's possible transient service problem, so retry as failed request.
            PLOG("stcode.retry", "Unexpected status code %lu/%lu, wait for timeout (%us)", LT_Pu32(statusCode), LT_Pu32(head->expectedStatusCode), head->secondsTimeout);
            PLOG("stcode.error", "%s", response);
            lt_free(response);  // Free response because it is not notified to app. Avoid memory leak.
        }

    } else if (event == LTHttpRequestEvent_Disconnected) {
        /* RequestComplete event has already fired when this event triggers, so no need to manage queue.
         * The http event sequence is
         * 1. LTHttpRequestEvent_HeadComplete
         * 2. LTHttpRequestEvent_BodyDataAvailable
         * 3. LTHttpRequestEvent_RequestComplete. This event will AdvanceQueue.
         * 4. LTHttpRequestEvent_Disconnected
         */
        // s_pEvent->NotifyEvent(pRequest->hQueueEvent, hRequest,LTHttpQueueRequestEvent_ServerDisconnect, head->response);

    } else if (event == LTHttpRequestEvent_DisconnectPending) {
        /* This event may occur right before or after LTHttpRequestEvent_RequestComplete
         * So, just skip, wait until RequestComplete or Error, or timeout.
         * It is usually triggered by request->AddHeader(hRequest, "Connection", kLTArgType_charstar, "close");
         * So, server closes connection upon completing request. Server may be configured to close by default.
         * The http event sequence is
         * 1. LTHttpRequestEvent_HeadComplete
         * 2. LTHttpRequestEvent_BodyDataAvailable
         * 3. LTHttpRequestEvent_DisconnectPending
         * 4. LTHttpRequestEvent_RequestComplete. This event will AdvanceQueue.
         *    Or, LTHttpRequestEvent_Error and timeout. Both will AdvanceQueue.
         */
        // P("http.disc.pending", "server is pending disconnect, queue will skip this request");
        // s_pEvent->NotifyEvent(pRequest->hQueueEvent, hRequest,LTHttpQueueRequestEvent_DisconnectPending, head->response);

    } else if (event == LTHttpRequestEvent_Error) {
        PLOG("http.err", "error processing request, wait for timeout (%us)", head->secondsTimeout);
    }

    lt_releasehandleprivatedata(hRequest, request);
}

// in queue thread
static bool ResetQueueRequest(LTNetHttpsRequestQueueEntry *entry, LTNetHttpsQueueImpl *queue) {
    LTHttpRequest hRequest = entry->hRequest;
    ILTHttpRequest *iRequest = lt_gethandleinterface(ILTHttpRequest, hRequest);
    if (!iRequest) return false;

    S.iEvent->UnregisterThreadFromEvent(queue->hQueueEvent, entry->registerThread->API->GetThreadHandle(entry->registerThread), entry->queueEventProc);
    FreeResponseBuffer(entry);
    lt_destroyhandle(queue->hQueueEvent);

    if (!iRequest->ResetRequest(hRequest)) return false;
    queue->hQueueEvent = S.core->CreateEvent(&s_requestEventArgs, DispatchHttpsQueueRequestEventProc, DispatchHttpsQueueRequestCompleteProc, NULL, NULL);
    if (!queue->hQueueEvent) return false;
    S.iEvent->RegisterThreadForEvent(queue->hQueueEvent, entry->registerThread->API->GetThreadHandle(entry->registerThread), entry->queueEventProc, NULL, entry->queueEventClientData, false);
    // If return false, all data will be freed on destroy later.
    return true;
}

// in queue thread
static void AdvanceQueue(LTNetHttpsQueueImpl *queue) {
    /*****
     * Clean up and remove the current queue head
     * *****/
    P("advance", "Cleaning up and advancing queue...");
    // Kill any existing timers
    queue->thread->API->KillTimer(queue->thread, HttpsQueueRequestTimeoutProc, queue);

    // Pop the queue head (the current request)
    queue->mutex->API->Lock(queue->mutex);
    if (LTList_IsEmpty(&queue->requests)) {
        queue->mutex->API->Unlock(queue->mutex);
        return;
    }
    LTNetHttpsRequestQueueEntry *head = LT_CONTAINER_OF(queue->requests.pNext, LTNetHttpsRequestQueueEntry, node);
    LTList_Remove(&head->node);
    ++queue->doneRequest;
    // LOGQUEUE("done", head, queue);
    queue->mutex->API->Unlock(queue->mutex);

    if (!head->bSlot) {
        // Non-slot request, clean up request socket
        LTHttpRequest hRequest = head->hRequest;
        void *request = lt_reservehandleprivatedata(hRequest);
        if (request) {
            // Unregister http request events
            ILTHttpRequest *iRequest = lt_gethandleinterface(ILTHttpRequest, hRequest);
            iRequest->NoRequestEvent(hRequest, OnHttpsRequestEventForQueue);
            // Disconnect & destroy socket
            iRequest->DestroyRequestSocket(hRequest);
        }
        lt_releasehandleprivatedata(hRequest, request);
    }

    // Free entry. The response buffer is not freed here, but will be freed on DispatchHttpsQueueRequestCompleteProc.
    lt_free(head);

    // Client is responsible for destroying hRequest

    /*****
     * Move on to next request if it exists
     * *****/
    P("advance", "Checking for next request...");
    queue->mutex->API->Lock(queue->mutex);
    if (LTList_IsEmpty(&queue->requests)) {
        queue->bIsRunning = false;
        P("check", "Queue emptied!");
    } else {
        P("check", "Queue next entry");
        // Not the last entry, so send the next one!
        queue->thread->API->QueueTaskProc(queue->thread, SendHttpsQueueHeadTaskProc, NULL, queue);
    }
    queue->mutex->API->Unlock(queue->mutex);
}

// in queue thread
static void SendHttpsQueueHeadTaskProc(void *clientData) {
    LTNetHttpsQueueImpl *queue = clientData;
    // Retrieve queue head and send request
    queue->mutex->API->Lock(queue->mutex);
    LTNetHttpsRequestQueueEntry *head = LT_CONTAINER_OF(queue->requests.pNext, LTNetHttpsRequestQueueEntry, node);
    queue->mutex->API->Unlock(queue->mutex);

    LTHttpRequest hRequest = head->hRequest;
    void *request = lt_reservehandleprivatedata(hRequest);
    if (!request) { // Invalid request
        P("send.invalid.req", NULL);
        AdvanceQueue(queue);
        return;
    }

    /* FIXME: LT-3053
     * Since a new event is created for every request being sent, this implicitly means there will be only one receiver for this new event.
     * However, clients should still check the return value of hRequest against their own hRequest in the event handler to ensure
     * their event callback is indeed for their request.
     * See LT-3053 for a proper fix, where there is an event per request instead of one shared event across the queue */
    // Destroy any existing queue event handle
    lt_destroyhandle(queue->hQueueEvent);
    // Re-create the event handle
    if (!(queue->hQueueEvent = S.core->CreateEvent(&s_requestEventArgs, DispatchHttpsQueueRequestEventProc, DispatchHttpsQueueRequestCompleteProc, NULL, NULL))) {
        LOGQUEUE_YELLOWALERT("invalid.event", head, queue);
        AdvanceQueue(queue);
        goto out;
    }
    S.iEvent->RegisterThreadForEvent(queue->hQueueEvent, head->registerThread->API->GetThreadHandle(head->registerThread), head->queueEventProc, NULL, head->queueEventClientData, false);

    if (head->bSlot) {
        // Notify slot has started
        P("slot.start", NULL);
        S.iEvent->NotifyEvent(queue->hQueueEvent, hRequest, kLTNetHttpsQueueEvent_RequestStart, NULL);
        goto out;
    }

    ILTHttpRequest *iRequest = lt_gethandleinterface(ILTHttpRequest, hRequest);
    if (head->bCloseOnComplete) {
        iRequest->AddHeader(hRequest, "Connection", kLTArgType_charstar, "close");
    } else {
        iRequest->AddHeader(hRequest, "Connection", kLTArgType_charstar, "keep-alive");
    }

    iRequest->OnRequestEvent(hRequest, OnHttpsRequestEventForQueue, queue);
    // LOGQUEUE("send", head, queue);
    // Send the request!
    if (iRequest->SendRequest(hRequest)) {
        // Notify clients their http request has started
        S.iEvent->NotifyEvent(queue->hQueueEvent, hRequest, kLTNetHttpsQueueEvent_RequestStart, NULL);
    } else {
        // Fail here, log the request and queue status.
        LOGQUEUE_YELLOWALERT("send.fail", head, queue);
    }
    // Always begin timer after sending request
    // Timer will manage retries and force queue advancement
    queue->thread->API->SetTimer(queue->thread, LTTime_Seconds(head->secondsTimeout), HttpsQueueRequestTimeoutProc, NULL, queue);
    if (head->bWakeOnTimeout) queue->thread->API->SetAsWakeupTimer(queue->thread, HttpsQueueRequestTimeoutProc, queue);

out:
    lt_releasehandleprivatedata(hRequest, request);
}

// in queue thread
static void HttpsQueueRequestTimeoutProc(void *clientData) {
    // The timeout proc cannot occur if KillTimer was called in AdvanceQueue due to an HTTP event.
    P("timeout", NULL);
    LTNetHttpsQueueImpl *queue = clientData;
    queue->thread->API->KillTimer(queue->thread, HttpsQueueRequestTimeoutProc, queue);
    // Retrieve head
    queue->mutex->API->Lock(queue->mutex);
    if (LTList_IsEmpty(&queue->requests)) {
        P("tout.empty", NULL);
        queue->mutex->API->Unlock(queue->mutex);
        return;
    }
    LTNetHttpsRequestQueueEntry *head = LT_CONTAINER_OF(queue->requests.pNext, LTNetHttpsRequestQueueEntry, node);
    queue->mutex->API->Unlock(queue->mutex);

    // Slotted requests have no timeout
    if (head->bSlot) return;

    LTHttpRequest hRequest = head->hRequest;
    void *request = lt_reservehandleprivatedata(hRequest);
    if (!request) { // Invalid request
        P("tout.invalid.req", NULL);
        AdvanceQueue(queue);
        return;
    }

    ILTHttpRequest *iRequest = lt_gethandleinterface(ILTHttpRequest, hRequest);
    iRequest->NoRequestEvent(hRequest, OnHttpsRequestEventForQueue);

    ++head->retryCount;
    char name[kLTThread_MaxNameBuff];
    head->registerThread->API->GetName(head->registerThread, name);
    if (head->retryCount < head->maxRetry) {
        // Recreate the request and try again
        LOGQUEUE("timeout.retry", head, queue);
        // We keep the request handle, but reset and, recreate some data inside, in order to avoid handle reuse problems
        if (ResetQueueRequest(head, queue)) {
            // Request recreated, so now try sending again
            queue->thread->API->QueueTaskProc(queue->thread, SendHttpsQueueHeadTaskProc, NULL, queue);
            return;
        }
    }

    // If any other error or reach retry max, send error and advance the queue
    // AdvanceQueue must be before NotifyEvent to avoid thread race on destroy hRequest.
    // LOGQUEUE("retry.end", head, queue);
    AdvanceQueue(queue);
    S.iEvent->NotifyEvent(queue->hQueueEvent, hRequest, kLTNetHttpsQueueEvent_Error, NULL);
    // client should destroy hRequest upon receiving http error.

    lt_releasehandleprivatedata(hRequest, request);
}

/** Object APIs ***************************************************************/

// in app thread
static bool QueueHttpsRequestCustom(LTNetHttpsQueueImpl *queue, LTHttpRequest hRequest, LTNetHttpsQueueEventProc *eventProc, void *clientData, u32 expectedStatusCode, bool bSlot, LTNetHttpsQueueRequest_Config customConfig) {
    if (!queue->thread) return false; // no http queue
    LTNetHttpsRequestQueueEntry *newEntry = lt_malloc(sizeof(LTNetHttpsRequestQueueEntry));
    if (!newEntry) return false;
    P("req","0x%08lx", LT_Pu32(hRequest));

    lt_memset(newEntry, 0, sizeof(LTNetHttpsRequestQueueEntry));
    newEntry->maxRetry = customConfig.maxRetry;
    newEntry->secondsTimeout = customConfig.secondsTimeout;
    newEntry->hRequest = hRequest;
    newEntry->bCloseOnComplete = customConfig.bCloseOnComplete;
    newEntry->expectedStatusCode = expectedStatusCode;

    newEntry->registerThread = queue->thread->API->GetCurrentThread();
    newEntry->queueEventProc = eventProc;
    newEntry->queueEventClientData = clientData;

    newEntry->bSlot = bSlot;
    newEntry->bWakeOnTimeout = customConfig.bWakeOnTimeout;

    // Add new entry to back of the linked list queue (FIFO)
    queue->mutex->API->Lock(queue->mutex);
    LTList_AddTail(&queue->requests, &newEntry->node);
    ++queue->newRequest;
    // LOGQUEUE("add", newEntry, queue);
    // If queue has not started emptying, then start it with the first task proc!
    if (!queue->bIsRunning) {
        queue->bIsRunning = true;
        P("run","Queue started!");
        queue->thread->API->QueueTaskProc(queue->thread, SendHttpsQueueHeadTaskProc, NULL, queue);
    }
    queue->mutex->API->Unlock(queue->mutex);

    return true;
}

static bool LTNetHttpsQueueImpl_QueueHttpsRequestCustom(LTNetHttpsQueueImpl *queue, LTHttpRequest hRequest, LTNetHttpsQueueEventProc *eventProc, void *clientData, u32 expectedStatusCode, LTNetHttpsQueueRequest_Config customConfig) {
    return QueueHttpsRequestCustom(queue, hRequest, eventProc, clientData, expectedStatusCode, false, customConfig);
}

static bool LTNetHttpsQueueImpl_QueueHttpsRequest(LTNetHttpsQueueImpl *queue, LTHttpRequest hRequest, LTNetHttpsQueueEventProc *eventProc, void *clientData, u32 expectedStatusCode) {
    return QueueHttpsRequestCustom(queue, hRequest, eventProc, clientData, expectedStatusCode, false, (LTNetHttpsQueueRequest_Config){.maxRetry = 3, .secondsTimeout = 6, .bCloseOnComplete = false});
}

static bool LTNetHttpsQueueImpl_QueueHttpsRequestSlot(LTNetHttpsQueueImpl *queue, LTHttpRequest hRequest, LTNetHttpsQueueEventProc *eventProc, void *clientData) {
    return QueueHttpsRequestCustom(queue, hRequest, eventProc, clientData, 0, true, (LTNetHttpsQueueRequest_Config){});
}

static void AdvanceQueueProc(void *clientData) {
    AdvanceQueue(clientData);
}

static void LTNetHttpsQueueImpl_NotifySlotComplete(LTNetHttpsQueueImpl *queue, LTHttpRequest hRequest) {
    queue->mutex->API->Lock(queue->mutex);
    // Push the queue forward if hRequest matches the head
    LTNetHttpsRequestQueueEntry *head = LT_CONTAINER_OF(queue->requests.pNext, LTNetHttpsRequestQueueEntry, node);
    if (head->hRequest == hRequest) queue->thread->API->QueueTaskProcIfRequired(queue->thread, AdvanceQueueProc, NULL, queue);
    queue->mutex->API->Unlock(queue->mutex);
}

static void LTNetHttpsQueueImpl_NoQueueHttpsRequest(LTNetHttpsQueueImpl *queue, LTNetHttpsQueueEventProc *eventProc) {
    S.iEvent->UnregisterFromEvent(queue->hQueueEvent, eventProc);
}

static void LTNetHttpsQueueImpl_GetQueueCounts(LTNetHttpsQueueImpl *queue, u32 *newRequest, u32 *doneRequest, u32 *completeRequest) {
    queue->mutex->API->Lock(queue->mutex);
    *newRequest  = queue->newRequest;
    *doneRequest = queue->doneRequest;
    *completeRequest = queue->completeRequest;
    queue->mutex->API->Unlock(queue->mutex);
}

static void ClearQueue(LTNetHttpsQueueImpl *queue) {
    if (queue->requests.pPrev && queue->requests.pNext) {
        while (!LTList_IsEmpty(&queue->requests)) {
            LTNetHttpsRequestQueueEntry *entry = LT_CONTAINER_OF(queue->requests.pNext, LTNetHttpsRequestQueueEntry, node);
            LTList_Remove(&entry->node);

            LTHttpRequest hRequest = entry->hRequest;
            void *request = lt_reservehandleprivatedata(hRequest);
            if (request) {
                // Unregister http request events
                ILTHttpRequest *iRequest = lt_gethandleinterface(ILTHttpRequest, hRequest);
                iRequest->NoRequestEvent(hRequest, OnHttpsRequestEventForQueue);
                // Disconnect & destroy socket
                iRequest->DestroyRequestSocket(hRequest);
            }
            lt_releasehandleprivatedata(hRequest, request);
            // Client is responsible for destroying hRequest
            lt_free(entry);
        }
    }
}

static void OnHttpsQueueExit(void) {
    LTOThread *thread = S.core->GetCurrentThreadObject();
    LTNetHttpsQueueImpl *queue = thread->API->GetThreadSpecificClientData(thread, "queue");
    char name[kLTThread_MaxNameBuff];
    thread->API->GetName(thread, name);
    ClearQueue(queue);
    LTLOG_YELLOWALERT("thread.exit", "queue %lu name %s", LT_Pu32(queue->queueId), name);
}

static bool LTNetHttpsQueueImpl_Start(LTNetHttpsQueueImpl *queue, u32 stackSize) {
    queue->mutex->API->Lock(queue->mutex);
    if (!queue->thread) {
        if ((queue->thread = lt_createobject(LTOThread))) {
            queue->thread->API->SetThreadSpecificClientData(queue->thread, "queue", NULL, queue);
            queue->queueId = S.queueCount;
            char name[12] = "https";
            lt_snprintf(name + 5, 7, "%d", queue->queueId);
            ++S.queueCount;
            queue->thread->API->SetStackSize(queue->thread, stackSize);
            queue->thread->API->Start(queue->thread, name, NULL, OnHttpsQueueExit);
        }
    }
    bool bRet = (queue->thread != NULL);
    queue->mutex->API->Unlock(queue->mutex);
    return bRet;
}

static void LTNetHttpsQueueImpl_Stop(LTNetHttpsQueueImpl *queue) {
    queue->mutex->API->Lock(queue->mutex);
    if (queue->thread) {
        queue->thread->API->Terminate(queue->thread);
        queue->thread->API->WaitUntilFinished(queue->thread, LTTime_Infinite());
        lt_destroyobject(queue->thread);
        queue->thread = NULL;
    }
    queue->mutex->API->Unlock(queue->mutex);
}

static void LTNetHttpsQueueImpl_DestructObject(LTNetHttpsQueueImpl *queue) {
    P("destruct", NULL);
    LTNetHttpsQueueImpl_Stop(queue);
    lt_destroyhandle(queue->hQueueEvent);
    queue->hQueueEvent = 0;
    lt_destroyobject(queue->mutex);
    queue->mutex = NULL;
}

static bool LTNetHttpsQueueImpl_ConstructObject(LTNetHttpsQueueImpl *queue) {
    P("construct", NULL);
    do {
        if (!(queue->mutex = lt_createobject(LTMutex))) break;
        queue->hQueueEvent = 0; // Create when a request is sent.
        LTList_Init(&queue->requests);
        return true;
    } while (false);
    LTLOG_YELLOWALERT("construct.fail", NULL);
    LTNetHttpsQueueImpl_DestructObject(queue);
    return false;
}

define_LTObjectImplPublic(LTNetHttpsQueue, LTNetHttpsQueueImpl,
    Start,
    Stop,
    QueueHttpsRequest,
    QueueHttpsRequestCustom,
    QueueHttpsRequestSlot,
    NotifySlotComplete,
    NoQueueHttpsRequest,
    // health
    GetQueueCounts,
);

/* Expand define_LTOBJECT_EXPORTLIBRARY(LTNetHttpsQueue, 1, LTNetHttpsQueueImpl); */
static void LTNetHttpsQueueLibImpl_LibFini(void) {
    lt_memset(&S, 0, sizeof(S));
    P("fini", NULL);
}

static bool LTNetHttpsQueueLibImpl_LibInit(void) {
    P("init", NULL);
    lt_memset(&S, 0, sizeof(S));
    S.core = LT_GetCore();
    S.iEvent= lt_getlibraryinterface(ILTEvent, S.core);
    return true;
}

typedef_LTLIBRARY_ROOT_INTERFACE(LTNetHttpsQueueLib, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTNetHttpsQueueLib, ) LTLIBRARY_DEFINITION;
LTLIBRARY_EXPORT_INTERFACES(LTNetHttpsQueueLib, (LTNetHttpsQueueImpl));

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  12-Dec-24   gallienus   created
 */