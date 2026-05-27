/************************************************************************
 * LTNet Queue implementation
 * 
 * This file implements the Net Queue API.
 * 
 * platforms/si91x/source/si91x/driver/wireless/include/LTNetQueue.h
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTNETQUEUE_H
#define ROKU_LT_INCLUDE_LT_CORE_LTNETQUEUE_H
#include <lt/LTObject.h>
#include <lt/core/LTThread.h>
LT_EXTERN_C_BEGIN

typedef enum {
    kLTNetQueueEvent_ExceedWatermark = 0,
    kLTNetQueueEvent_FallsUnderWatermark,
} LTNetQueueEvent;

typedef void (*LTNetQueueEventCallback)(LTNetQueueEvent event, u32 curr_size, u32 watermark, s64 time, void *pReceiverClientData);
typedef LTThread_ClientDataReleaseProc LTNetQueueEventClientDataReleaseProc;
/***********************
 * LTNetQueue Object
***********************/
typedef_LTObject(LTNetQueue, 1) {
    /**
     * @brief Create the Singleton instance of the LTNetQueue
     * @param queueSize - size of the queue
     * @return true if successful, false otherwise
     */
    bool    (* Create)(LTNetQueue *queue, u32 queueSize);

    /**
     * @brief Destroy the Singleton instance of the LTNetQueue
     */
    void    (* Destroy)(LTNetQueue *queue);
    
    /**
     * @brief Push the data to the front of the queue
     * @param data - data to be enqueued
     * @return true if successful, false otherwise
     */
    bool    (* Push) (LTNetQueue *queue, void *data);
    
    /**
     * @brief Pop the data from the back of the queue
     * @param queue - queue from which data to be dequeued
     * @return data if successful, NULL otherwise
     */
    void*   (* Pop) (LTNetQueue *queue);

    /**
     * @brief Notify all the listeners of the queue
     * @param queue - queue to be notified
     * @return void
     */
    void    (* Notify) (LTNetQueue *queue);

    /**
     * @brief Register the notification for the queue
     * @param queue - queue to be registered
     * @param eventCallback - callback function
     * @param relaseCb - callback function to release the client data
     * @param pReceiverClientData - client data
     * @param notify_immidiate - notify immediately
     * @return void
     */
    void    (* RegisterNotification) (LTNetQueue *queue, LTThread_TaskProc eventCallback, LTThread_ClientDataReleaseProc* relaseCb, void * pReceiverClientData, bool notify_immidiate);

    /**
     * @brief Unregister the notification for the queue
     * @param queue - queue to be unregistered
     * @param eventCallback - callback function
     * @return true if successful, false otherwise
     */
    bool    (* UnRegisterNotification) (LTNetQueue *queue, LTThread_TaskProc eventCallback);

    /**
     * @brief Get the size of the queue
     * @param queue - queue to get the size
     * @return size of the queue
     */
    u32 (*Size) (LTNetQueue *queue);

    /**
     * @brief Set the watermark for the queue
     * @param queue - queue to set the watermark
     * @param watermark - watermark value
     * @return true if successful, false otherwise
     */
    bool   (* SetWatermark) (LTNetQueue *queue, u32 watermark);

    /**
     * @brief Get the watermark for the queue
     * @param queue - queue to get the watermark
     * @return watermark value
     */
    u32   (* GetWatermark) (LTNetQueue *queue);

    /**
     * @brief Set the event callback for the queue
     * @param queue - queue to set the event callback
     * @param eventCallback - event callback function
     * @param relCallback - callback function to release the client data
     * @param pReceiverClientData - client data
     * @param bNotifyEventStateImmediately - notify event state immediately
     * @return true if successful, false otherwise
     */
    bool  (* RegisterEventCallback) (LTNetQueue *queue, LTNetQueueEventCallback eventCallback, LTNetQueueEventClientDataReleaseProc relCallback, void *pReceiverClientData, bool bNotifyEventStateImmediately);

    /**
     * @brief Unregister the event callback for the queue
     * @param queue - queue to unregister the event callback
     * @param eventCallback - event callback function
     * @return true if successful, false otherwise
     */
    bool  (* UnRegisterEventCallback) (LTNetQueue *queue, LTNetQueueEventCallback eventCallback);

    /**
     * @brief Check if the queue is full
     * @param queue - queue to check
     * @return true if full, false otherwise
     */
    bool  (* IsFull) (LTNetQueue *queue);

    /**
     * @brief Peek the data from the back of the queue
     * @param queue - queue to be peeked
     * @return data if successful, NULL otherwise
     */
    void* (* Peek) (LTNetQueue *queue);
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_LTNETQUEUE_H */

/************************************************************************
 *  LOG
 ************************************************************************
 *  27-Aug-24   galba       created
 ************************************************************************/