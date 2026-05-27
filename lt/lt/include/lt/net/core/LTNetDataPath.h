/************************************************************************
 * LTNetDataPath implementation
 * 
 * This file implements the LTNet Data Path APIs
 * 
 * platforms/si91x/source/si91x/driver/wireless/include/LTNetDataPath.h
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTNETDATAPATH_H
#define ROKU_LT_INCLUDE_LT_CORE_LTNETDATAPATH_H

#include <lt/LTTypes.h>
#include <lt/net/core/LTNetBuffer.h>
#include <lt/net/core/LTNetQueue.h>
LT_EXTERN_C_BEGIN

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTNetDataPath, 1);
typedef enum {
    kLTNetHWResEvent_Available = 0,
    kLTNetHWResEvent_Unavailable,
} LTNetHWResEvent;
typedef void (*LTNetHWResEventCallback)(LTNetHWResEvent, LTTime, void*);
typedef LTThread_ClientDataReleaseProc LTNetHwResEventClientDataReleaseProc;
typedef enum {
    kLTNetDataPathQueueType_CommonCmdQueue          = 0,
    kLTNetDataPathQueueType_WlanCmdQueue            = 1,
    kLTNetDataPathQueueType_NetworkCmdQueue         = 2,
    kLTNetDataPathQueueType_SocketCmdQueue          = 3,
    kLTNetDataPathQueueType_BleCmdQueue             = 4,

    kLTNetDataPathQueueType_WlanTxDataQueue         = 5,
    kLTNetDataPathQueueType_SocketTxDataQueue       = 6,
    kLTNetDataPathQueueType_BleTxDataQueue          = 7,

    kLTNetDataPathQueueType_CommonRespQueue         = 8,
    kLTNetDataPathQueueType_WlanRespQueue           = 9,
    kLTNetDataPathQueueType_NetworkRespQueue        = 10,
    kLTNetDataPathQueueType_SocketRespQueue         = 11,
    kLTNetDataPathQueueType_BleRespQueue            = 12,

    kLTNetDataPathQueueType_WlanEventQueue          = 13,
    kLTNetDataPathQueueType_NetworkEventQueue       = 14,
    kLTNetDataPathQueueType_SocketEventQueue        = 15,
    kLTNetDataPathQueueType_BleEventQueue           = 16,

    kLTNetDataPathQueueType_RxDataQueue             = 17,

    kLTNetDataPathQueueType_Max
} LTNetDataPathQueueType;

LT_INLINE const char* LTNetDataPathQueueTypeToString(int queueType) {
    switch (queueType) {
        case kLTNetDataPathQueueType_CommonCmdQueue:        return "CommonCmdQueue";
        case kLTNetDataPathQueueType_WlanCmdQueue:          return "WlanCmdQueue";
        case kLTNetDataPathQueueType_NetworkCmdQueue:       return "NetworkCmdQueue";
        case kLTNetDataPathQueueType_SocketCmdQueue:        return "SocketCmdQueue";
        case kLTNetDataPathQueueType_BleCmdQueue:           return "BleCmdQueue";
        case kLTNetDataPathQueueType_WlanTxDataQueue:       return "WlanDataQueue";
        case kLTNetDataPathQueueType_SocketTxDataQueue:     return "SocketDataQueue";
        case kLTNetDataPathQueueType_BleTxDataQueue:        return "BleDataQueue";
        case kLTNetDataPathQueueType_CommonRespQueue:       return "CommonRespQueue";
        case kLTNetDataPathQueueType_WlanRespQueue:         return "WlanRespQueue";
        case kLTNetDataPathQueueType_NetworkRespQueue:      return "NetworkRespQueue";
        case kLTNetDataPathQueueType_SocketRespQueue:       return "SocketRespQueue";
        case kLTNetDataPathQueueType_BleRespQueue:          return "BleRespQueue";
        case kLTNetDataPathQueueType_WlanEventQueue:        return "WlanEventQueue";
        case kLTNetDataPathQueueType_NetworkEventQueue:     return "NetworkEventQueue";
        case kLTNetDataPathQueueType_SocketEventQueue:      return "SocketEventQueue";
        case kLTNetDataPathQueueType_BleEventQueue:         return "BleEventQueue";
        case kLTNetDataPathQueueType_RxDataQueue:           return "RxDataQueue";
        default: return "Unknown";
    }
}

typedef enum {
    kLTNetDataPathFrameType_Max
} LTNetDataPathFrameType;

typedef struct LTNetDataPathPktHdr_ {
    LTNetDataPathQueueType queue_type;
} __attribute__((packed)) LTNetDataPathPktHdr;

#define BIT(x) (1 << (x))

#define TXQUALIFY_QUEUE_MASK (BIT(kLTNetDataPathQueueType_CommonCmdQueue)       | \
                              BIT(kLTNetDataPathQueueType_WlanCmdQueue)         | \
                              BIT(kLTNetDataPathQueueType_NetworkCmdQueue)      | \
                              BIT(kLTNetDataPathQueueType_SocketCmdQueue)       | \
                              BIT(kLTNetDataPathQueueType_BleCmdQueue)          | \
                              BIT(kLTNetDataPathQueueType_WlanTxDataQueue)      | \
                              BIT(kLTNetDataPathQueueType_SocketTxDataQueue)    | \
                              BIT(kLTNetDataPathQueueType_BleTxDataQueue))

#define BUSTHREAD_QUEUE_MASK (TXQUALIFY_QUEUE_MASK                              | \
                              BIT(kLTNetDataPathQueueType_RxDataQueue))

                              
#define EVENTRX_QUALIFY_MASK (BIT(kLTNetDataPathQueueType_WlanEventQueue)       | \
                              BIT(kLTNetDataPathQueueType_NetworkEventQueue)    | \
                              BIT(kLTNetDataPathQueueType_SocketEventQueue)     | \
                              BIT(kLTNetDataPathQueueType_BleEventQueue))

#define RESPRX_QUALIFY_MASK (BIT(kLTNetDataPathQueueType_CommonRespQueue)       | \
                             BIT(kLTNetDataPathQueueType_WlanRespQueue)         | \
                             BIT(kLTNetDataPathQueueType_NetworkRespQueue)      | \
                             BIT(kLTNetDataPathQueueType_SocketRespQueue)       | \
                             BIT(kLTNetDataPathQueueType_BleRespQueue))

#define RXQUALIFY_QUEUE_MASK (RESPRX_QUALIFY_MASK                               | \
                              EVENTRX_QUALIFY_MASK)

#define LTNET_DATAPATH_PERIODIC_FIRE 25 //Milliseconds
typedef struct {
    u32 size;
} LTNetDataPathQueueStats;

typedef struct {
    LTNetDataPathQueueStats stats[kLTNetDataPathQueueType_Max];
    LTNetBufferMemStats mem_pool_stats;
} LTNetDataPathStats;

typedef bool (*LTNetDataPathIsOkayToTransmitCallback)(LTNetDataPathQueueType queueType);
typedef bool (*LTNetDataPathTransmitCallback)(LTNet_buff *buff, void *context, LTNetBufferCompletionCb compl_cb);
typedef bool (*LTNetDataPathReceiveCallback)(LTNetDataPathQueueType type, LTNet_buff *buff, void *context);
typedef void (*LTNetDataPathHwStatsLogCallback)(void);
struct LTNetDataPathApi {
    INHERIT_LIBRARY_BASE
    /**
     * @brief Transmit function, to send the data to the network. Asynchronous.
     * @param queueType Queue type to which the data needs to be sent.
     * @param buff Pointer to the LTNetbuffer to be sent.
     * @param notify Flag to notify the completion of the transmission.
     * @return int 0 on success, -1 on failure.
     */
    int  (*Transmit)(LTNetDataPathQueueType queueType, LTNet_buff *buff, bool notify);

    /**
     * @brief Receive function, to receive the data from the Vendor spocific module.
     * @param buff Pointer to the LTNetbuffer to be received.
     * @param notify Flag to notify the completion of the reception and notify the Bus Thread.
     * @return bool true on success, false on failure.
     */
    bool (*Receive)(LTNet_buff *buff, bool notify);

    /**
     * @brief   Subscribe to a queue to receive data from the network.
     * @param queueType Queue type to which the data needs to be subscribed.
     * @param callback Callback function to be called when data is received.
     * @param context Context to be passed to the callback function.
     * @return bool true on success, false on failure.
     */
    bool (*SubscribeToQueue)(LTNetDataPathQueueType queueType, LTNetDataPathReceiveCallback callback, void *context);

    /**
     * @brief   Unsubscribe from a queue to stop receiving data from the network.
     * @param queueType Queue type from which the data needs to be unsubscribed.
     * @param callback Callback function to be called when data is received.
     * @param context Context to be passed to the callback function.
     * @return bool true on success, false on failure.
     */
    bool (*UnSubscribeFromQueue)(LTNetDataPathQueueType queueType, LTNetDataPathReceiveCallback callback, void *context);

    /**
     * @brief   Set the callback function to be called when data is sent to HW.
     * @param callback Callback function to be called when data is sent.
     * @param client_context Context to be passed to the callback function.
     */
    void (*SetSendTxCallback)(LTNetDataPathTransmitCallback callback, void *client_context);

    /**
     * @brief Set the callback which LTNetDataPath will call to check if it is okay to transmit a packet.
     *        From queue. If this is not set then LTNetDataPath will always transmit the packet.
     * @param callback Callback function to be called to check if it is okay to transmit a packet.
     */
    void (*SetIsOkayToTransmitCallback)(LTNetDataPathIsOkayToTransmitCallback callback);

    /**
     * @brief   Get the statistics of the data path.
     * @param stats Pointer to the LTNetDataPathStats structure to be filled.
     * @return bool true on success, false on failure.
     */
    bool (*GetStatistics)(LTNetDataPathStats *stats);

    /**
     * @brief   Enable/Disable the logging of the data path.
     * @param enable Flag to enable/disable the logging.
     */
    void (*EnableLog)(bool enable);

    /**
     * @brief   Notify the data path to process the received packet.
     * @param queueType Queue type to be notified.
     */
    void (*ForceNotify)(LTNetDataPathQueueType queueType);

    /**
     * @brief Hardware stats log callback register
     * 
     */
    void (*SetHardwareStatsLogCallback)(LTNetDataPathHwStatsLogCallback cb);

    /**
     * @brief Set the watermark for the queue
     * @param queue - queue to set the watermark
     * @param watermark - watermark value
     * @return true if successful, false otherwise
     */
    bool   (*SetQueueWatermark) (LTNetDataPathQueueType queueType, u32 watermark);

    /**
     * @brief Get the watermark for the queue
     * @param queue - queue to get the watermark
     * @return watermark value
     */
    u32   (*GetQueueWatermark) (LTNetDataPathQueueType queueType);

    /**
     * @brief Set the event callback for the queue
     * @param queue - queue to set the event callback
     * @param eventCallback - event callback function
     * @param relCallback - callback function to release the client data
     * @param pReceiverClientData - client data
     * @param bNotifyEventStateImmediately - notify event state immediately
     * @return true if successful, false otherwise
     */
    bool  (*RegisterQueueEventCallback) (LTNetDataPathQueueType queueType, LTNetQueueEventCallback eventCallback, LTNetQueueEventClientDataReleaseProc relCallback, void *pReceiverClientData, bool bNotifyEventStateImmediately);

    /**
     * @brief Unregister the event callback for the queue
     * @param queue - queue to unregister the event callback
     * @param eventCallback - event callback function
     * @return true if successful, false otherwise
     */
    bool  (*UnRegisterQueueEventCallback) (LTNetDataPathQueueType queueType, LTNetQueueEventCallback eventCallback);

    bool (*RegisterHWResEventCallback)(LTNetHWResEventCallback cb, LTNetHwResEventClientDataReleaseProc relCallback, void *pReceiverClientData, bool bNotifyEventStateImmediately);
    bool (*UnRegisterHWResEventCallback)(LTNetHWResEventCallback cb);

    /**
     * @brief Log the statistics of the data path.
     */
    void (*LogStatistics)(const char* tag);
};

LT_EXTERN_C_END
#endif // ROKU_LT_INCLUDE_LT_CORE_LTNETDATAPATH_H


/************************************************************************************
 * LOG 
 *************************************************************************************
 *  01-Sep-24   galba       created
 */