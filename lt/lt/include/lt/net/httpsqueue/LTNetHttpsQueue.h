/*******************************************************************************
 * include/lt/net/httpsqueue/LTNetHttpsQueue.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/net/httpclient/LTNetHttpClient.h>

#ifndef LT_INCLUDE_LT_NET_HTTPSQUEUE_LTNETHTTPSQUEUE_H
#define LT_INCLUDE_LT_NET_HTTPSQUEUE_LTNETHTTPSQUEUE_H

LT_EXTERN_C_BEGIN

typedef enum {
    /* Unused */
    kLTNetHttpsQueueEvent_None = 0,        /* This event is not notified to clients. */
    /* Request is now in flight */
    kLTNetHttpsQueueEvent_RequestStart,    /* Request has been sent out by the queue and is now in-flight */
    /* Request has ended */
    kLTNetHttpsQueueEvent_RequestComplete, /* Request has completed successfully with the expected status code. Request is removed from the queue. */
    kLTNetHttpsQueueEvent_Error,           /* Request has completed with an unexpected status code, maximum number of retries, or network-related failure. Request is removed from the queue. */
} LTNetHttpsQueueEvent;

typedef struct {
    u8   maxRetry;         /* The maximum number of times the request should be retried after a failure/timeout */
    u8   secondsTimeout;   /* The time a request should take before a retry is forcibly triggered  */
    bool bCloseOnComplete; /* True if the "Connection: Close" header should be added to the request */
    bool bWakeOnTimeout;   /* True if the timeout should wake up a sleeping system, in case the request response is critical */
} LTNetHttpsQueueRequest_Config;

typedef void LTNetHttpsQueueEventProc(LTHttpRequest hRequest, LTNetHttpsQueueEvent event, const char *response, void *clientData);
/**< Event handler for receiving status updates on the current, in-flight request in the queue
 * The order of events is illustrated below
 * 1. Client adds LTHttpRequest to the queue via QueueHttpsRequest(). Client waits for request to make its way through the queue.
 * 2. kLTNetHttpsQueueEvent_RequestStart is sent out when LTNetHttpRequest queued by the client opens a socket and begins sending out the HTTP request
 *      a. If the request is a request "slot", this event indicates the queue is paused and client can start their custom request
 * 3. kLTNetHttpsQueueEvent_RequestComplete or kLTNetHttpsQueueEvent_Error is sent out when the HTTP request is completed and the LTNetHttpRequest can now be safely destroyed by the client
 *
 * @param hRequest Request originally added to the queue that is now in-flight
 * @param event Status of the in-flight request
 * @param response When event is RequestComplete or Error, contains the server response for the LTHttpRequest. Otherwise NULL
 * @param clientData Client data registered with the event handler
 */

typedef_LTObject(LTNetHttpsQueue, 1) {

    bool (*Start)(LTNetHttpsQueue *queue, u32 stackSize);
        /**< Start the HTTPS queue thread.
         *
         *   @param queue      this queue object
         *   @param stackSize  the stack size of the queue thread
         *
         *   @return true on success, else false.
         *
         *   @note  Creating a queue does not start the thread of the queue.
         */

    void (*Stop)(LTNetHttpsQueue *queue);
        /**< Stop the HTTPS queue thread. */

    bool (*QueueHttpsRequest)(LTNetHttpsQueue *queue, LTHttpRequest hRequest, LTNetHttpsQueueEventProc *eventProc, void *clientData, u32 expectedStatusCode);
        /**< Queue an HTTP request with preconfigured retry and timeout values (2 retries, 6 second timeout).
         *
         *   @param queue               this queue object
         *   @param hRequest            the request to be sent
         *   @param eventProc           the event callback for LTNetHttpsQueueEvent
         *   @param clientData          the client data pointer, to be passed back to callback function
         *   @param expectedStatusCode  the expected status code
         *
         *   @return true if the request was queued successfully, else false.
         *
         *   @note  If expectedStatusCode is not 0, queue notifies kLTNetHttpsQueueEvent_RequestComplete when receiving the expectedStatusCode, otherwise, kLTNetHttpsQueueEvent_Error.
         *          If expectedStatusCode is 0, queue notifies kLTNetHttpsQueueEvent_RequestComplete when receiving any status code.
         */

    bool (*QueueHttpsRequestCustom)(LTNetHttpsQueue *queue, LTHttpRequest hRequest, LTNetHttpsQueueEventProc *eventProc, void *clientData, u32 expectedStatusCode, LTNetHttpsQueueRequest_Config customConfig);
        /**< Queue an HTTP request with custom retry and timeout values.
         *
         *   @param queue               this queue object
         *   @param hRequest            the request to be sent
         *   @param eventProc           the event callback for LTNetHttpsQueueEvent
         *   @param clientData          the client data pointer, to be passed back to callback function
         *   @param expectedStatusCode  the expected status code
         *   @param customConfig        extra configuration for the request in the queue
         *
         *   @return true if the request was queued successfully, else false
         *
         *   @note  If expectedStatusCode is not 0, queue notifies kLTNetHttpsQueueEvent_RequestComplete when receiving the expectedStatusCode, otherwise, kLTNetHttpsQueueEvent_Error.
         *          If expectedStatusCode is 0, queue notifies kLTNetHttpsQueueEvent_RequestComplete when receiving any status code.
         */

    bool (*QueueHttpsRequestSlot)(LTNetHttpsQueue *queue, LTHttpRequest hRequest, LTNetHttpsQueueEventProc *eventProc, void *clientData);
        /**< Queue a "slot" in the queue to reserve a time slice in the queue for a custom http request handler outside of
         * the queue thread's context. The queue will be paused upon sending out a RequestStart event and will stay paused
         * until NotifySlotComplete() is called by the client.
         *
         *   @note If the hRequest provided to this API is destroyed before the queue reaches the slot, the slot will be skipped
         *   @see NotifySlotComplete
         *
         *   @param queue Queue object
         *   @param hRequest Request handle to be managed outside of the queue
         *   @param eventProc Event callback to receive events in caller's thread context
         *   @param clientData Data to pass alongside event callback
         *
         *   @return true if slot was queued successfully, else false
         */

    void (*NotifySlotComplete)(LTNetHttpsQueue *queue, LTHttpRequest hRequest);
        /**< Notify the queue that the request slot is now complete. The queue thread will resume control of the queue and
         * move on to the next request in the queue. This API has no effect if hRequest does not match the current ongoing request.
         *   @see QueueHttpsRequestSlot
         *   @param queue Queue object
         *   @prama hRequest Request handle that was managed outside of the queue
         */

    void (*NoQueueHttpsRequest)(LTNetHttpsQueue *queue, LTNetHttpsQueueEventProc *eventProc);
       /**< Unregister a previously registered queue request event callback.
        *
        *   @param queue      this queue object
        *   @param eventProc  the request event callback function
        */

    void (*GetQueueCounts)(LTNetHttpsQueue *queue, u32 *newRequest, u32 *doneRequest, u32 *completeRequest);
        /**< Get queue stat data.
         *
         *   @param queue        this queue object
         *   @param newRequest   accumulated count of new requests (added to queue)
         *   @param doneRequest  accumulated count of done requests (removed from queue)
         *   @param doneRequest  accumulated count of successfully completed requests (removed from queue)
         */

} LTOBJECT_API;

LT_EXTERN_C_END
#endif // LT_INCLUDE_LT_NET_HTTPSQUEUE_LTNETHTTPSQUEUE_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  12-Dec-24   gallienus   created
 */