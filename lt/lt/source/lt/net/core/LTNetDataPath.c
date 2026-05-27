/************************************************************************
 * LTNetDataPath implementation
 *
 * This file implements the LTNet Data Path APIs
 *
 * platforms/si91x/source/si91x/driver/wireless/common/LTNetDataPath.c
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTNetDataPath.h>
#include <lt/net/core/LTNetQueue.h>
DEFINE_LTLOG_SECTION("lt.net.dp");
#if defined(DEBUG_ASSERT)
#define DEBUG_ASSERT_LTNETDP(x) LT_ASSERT(x)
#else
#define DEBUG_ASSERT_LTNETDP(x)
#endif
#if defined(DEBUG)
#define PLOG(...) if(S.log_en) LTLOG_STOMP(__VA_ARGS__)
#else
#define PLOG(...)
#endif

typedef struct {
    u32                          prio;
    LTNetDataPathReceiveCallback rx_cb;
    void                         *rx_cb_ctx;
    LTList_Node                  node;
} LTNetDataPathRxCbNode;

typedef struct {
    LTMutex* mutex;
    LTList   cb_list;
} LTNetDataPathRxCbList;

static struct Statics {
    LTCore                                  *core;
    LTNetBuffer                             *netBuffer;
    ILTThread                               *iThread;
    ILTEvent                                *iEvent;
    LTThread                                busThread;
    LTThread                                eventRxThread;
    LTThread                                respRxThread;
    LTNetQueue                              *netQueue[kLTNetDataPathQueueType_Max];
    LTNetDataPathTransmitCallback           send_tx_cb;
    LTNetDataPathIsOkayToTransmitCallback   is_okay_to_transmit_cb;
    void                                    *send_tx_cb_ctx;
    LTNetDataPathRxCbList                   rxCbList[kLTNetDataPathQueueType_Max];
    bool                                    log_en;
    LTNetDataPathHwStatsLogCallback         hw_stats_log_cb;
    LTEvent                                 hwres_event;
} S;

typedef struct {
    u32 queueSize;
    LTNetDataPathQueueType queueType;
} QueueMetaData;

static inline bool IS_BIT_VALID(u32 mask, u32 bit) {
    return mask&bit;
}

static u32 get_queue_size(int queueType) {
    switch (queueType) {
        case kLTNetDataPathQueueType_CommonCmdQueue:
            return LTNETDP_QUEUE_COMMONCMD_SIZE;
        case kLTNetDataPathQueueType_WlanCmdQueue:
            return LTNETDP_QUEUE_WLANCMD_SIZE;
        case kLTNetDataPathQueueType_NetworkCmdQueue:
            return LTNETDP_QUEUE_NETWORKCMD_SIZE;
        case kLTNetDataPathQueueType_SocketCmdQueue:
            return LTNETDP_QUEUE_SOCKETCMD_SIZE;
        case kLTNetDataPathQueueType_BleCmdQueue:
            return LTNETDP_QUEUE_BLECMD_SIZE;
        case kLTNetDataPathQueueType_WlanTxDataQueue:
            return LTNETDP_QUEUE_WLANTXDATA_SIZE;
        case kLTNetDataPathQueueType_SocketTxDataQueue:
            return LTNETDP_QUEUE_SOCKETTXDATA_SIZE;
        case kLTNetDataPathQueueType_BleTxDataQueue:
            return LTNETDP_QUEUE_BLETXDATA_SIZE;
        case kLTNetDataPathQueueType_CommonRespQueue:
            return LTNETDP_QUEUE_COMMONRESP_SIZE;
        case kLTNetDataPathQueueType_WlanRespQueue:
            return LTNETDP_QUEUE_WLANRESP_SIZE;
        case kLTNetDataPathQueueType_NetworkRespQueue:
            return LTNETDP_QUEUE_NETWORKRESP_SIZE;
        case kLTNetDataPathQueueType_SocketRespQueue:
            return LTNETDP_QUEUE_SOCKETRESP_SIZE;
        case kLTNetDataPathQueueType_BleRespQueue:
            return LTNETDP_QUEUE_BLERESP_SIZE;
        case kLTNetDataPathQueueType_WlanEventQueue:
            return LTNETDP_QUEUE_WLANEVENT_SIZE;
        case kLTNetDataPathQueueType_NetworkEventQueue:
            return LTNETDP_QUEUE_NETWORKEVENT_SIZE;
        case kLTNetDataPathQueueType_SocketEventQueue:
            return LTNETDP_QUEUE_SOCKETEVENT_SIZE;
        case kLTNetDataPathQueueType_BleEventQueue:
            return LTNETDP_QUEUE_BLEEVENT_SIZE;
        case kLTNetDataPathQueueType_RxDataQueue:
            return LTNETDP_QUEUE_RXDATA_SIZE;
        default:
            return 0;
    }
}

/*********************************************************
 * Library API interface Implementation
 *********************************************************/

static bool LTNetDataPathImpl_Receive(LTNet_buff *buff, bool notify) {
    LTNetDataPathQueueType queueType = kLTNetDataPathQueueType_RxDataQueue;
    if (queueType >= kLTNetDataPathQueueType_Max || S.netQueue[queueType] == NULL) {
        LTLOG_REDALERT("invalid.args", "LTNetDataPathImpl_Receive: Invalid arguments");
        DEBUG_ASSERT_LTNETDP(0);
        return false;
    }
    if (buff) {
        if(!S.netQueue[queueType]->API->Push(S.netQueue[queueType], buff)) {
            return false;
        }
    }
    if(notify) {
        S.netQueue[queueType]->API->Notify(S.netQueue[queueType]);
    }
    return true;
}

static void LTNetDataPathImpl_SetSendTxCallback(LTNetDataPathTransmitCallback callback, void *client_context) {
    S.send_tx_cb = callback;
    S.send_tx_cb_ctx = client_context;
}

static void LTNetDataPathImpl_SetIsOkayToTransmitCallback(LTNetDataPathIsOkayToTransmitCallback callback) {
    S.is_okay_to_transmit_cb = callback;
}

static bool LTNetDataPathImpl_GetStatistics(LTNetDataPathStats *stats) {
    if (!stats) {
        LTLOG_REDALERT("invalid.args", "LTNetDataPathImpl_GetStatistics: Invalid arguments");
        DEBUG_ASSERT_LTNETDP(0);
        return false;
    }
    for (int i = 0; i < kLTNetDataPathQueueType_Max; i++) {
        if (S.netQueue[i] != NULL) {
            stats->stats[i].size = S.netQueue[i]->API->Size(S.netQueue[i]);
        }
    }
    S.netBuffer->GetMemStats(&stats->mem_pool_stats);
    if(S.hw_stats_log_cb) S.hw_stats_log_cb();
    return true;
}

static void LTNetDataPathImpl_LogStatistics(const char* tag) {
    LTNetDataPathStats qstats;
    char qlog_tag[32];
    char memlog_tag[32];
    if (tag) {
        lt_snprintf(qlog_tag, sizeof(qlog_tag), "%s.qstats", tag);
        lt_snprintf(memlog_tag, sizeof(memlog_tag), "%s.memstats", tag);
    } else {
        lt_strncpyTerm(qlog_tag, "qstats", sizeof(qlog_tag));
        lt_strncpyTerm(memlog_tag, "memstats", sizeof(memlog_tag));
    }
    if (!LTNetDataPathImpl_GetStatistics(&qstats)) {
        LTLOG_YELLOWALERT("stats.fail", "LTNetDataPathImpl_LogStatistics: Failed to get statistics");
        return;
    }
        for (int i = 0 ; i < kLTNetDataPathQueueType_Max; i++) {
            LTLOG(qlog_tag, "Queue %s(%ld):       Current Size=%ld\n", LTNetDataPathQueueTypeToString(i),LT_Pu32(i), LT_Pu32(qstats.stats[i].size));
        }
        for (u32 i = 0; i < qstats.mem_pool_stats.num_pool; i++) {
            LTLOG(memlog_tag, "%s:  block_count=%ld, block_size=%ld, free_count=%ld, num_alloc=%lld, num_free=%lld\n", qstats.mem_pool_stats.pool_stats[i].name,
                            LT_Pu32(qstats.mem_pool_stats.pool_stats[i].block_count), LT_Pu32(qstats.mem_pool_stats.pool_stats[i].block_size),
                            LT_Pu32(qstats.mem_pool_stats.pool_stats[i].free_count),
                            LT_Pu64(qstats.mem_pool_stats.pool_stats[i].num_alloc), LT_Pu64(qstats.mem_pool_stats.pool_stats[i].num_free));
        }
}

static int LTNetDataPathImpl_Transmit(LTNetDataPathQueueType queueType, LTNet_buff *buff, bool notify) {
    s32 ret = -1;
    if (queueType >= kLTNetDataPathQueueType_Max || S.netQueue[queueType] == NULL) {
        LTLOG_REDALERT("invalid.args", "LTNetDataPathImpl_Transmit: Invalid arguments");
        DEBUG_ASSERT_LTNETDP(0);
        return ret;
    }
    if ((TXQUALIFY_QUEUE_MASK & BIT(queueType)) == 0x0) {
        LTLOG_YELLOWALERT("invalid.queue", "LTNetDataPathImpl_Transmit: Invalid queue type %s", LTNetDataPathQueueTypeToString(queueType));
        return ret;
    }
    if(buff) {
        if(!S.netQueue[queueType]->API->Push(S.netQueue[queueType], buff)) {
            PLOG("queue.full", "LTNetDataPathImpl_Transmit: Queue %s is full", LTNetDataPathQueueTypeToString(queueType));
            if(!notify) S.netQueue[queueType]->API->Notify(S.netQueue[queueType]);
            ret = -1;
        } else {
            ret = 0;
        }
    }
    if (notify) S.netQueue[queueType]->API->Notify(S.netQueue[queueType]);
    return ret;
}

static bool LTNetDataPathImpl_SubscribeToQueue(LTNetDataPathQueueType queueType, LTNetDataPathReceiveCallback callback, void *context) {
    if (queueType >= kLTNetDataPathQueueType_Max || !IS_BIT_VALID(RXQUALIFY_QUEUE_MASK, BIT(queueType)) || S.netQueue[queueType] == NULL) {
        LTLOG_REDALERT("invalid.args", "LTNetDataPathImpl_SubscribeToQueue: Invalid arguments");
        DEBUG_ASSERT_LTNETDP(0);
        return false;
    }
    LTNetDataPathRxCbNode *node = (LTNetDataPathRxCbNode*)lt_malloc(sizeof(LTNetDataPathRxCbNode));
    if (node == NULL) {
        LTLOG_REDALERT("malloc.fail", "LTNetDataPathImpl_SubscribeToQueue: malloc failed");
        DEBUG_ASSERT_LTNETDP(0);
        return false;
    }
    node->rx_cb = callback;
    node->rx_cb_ctx = context;
    //Add to the list based on the thread priority, higher priority first
    node->prio = S.iThread->GetPriority(S.iThread->GetCurrentThread());
    S.rxCbList[queueType].mutex->API->Lock(S.rxCbList[queueType].mutex);
    if (LTList_IsEmpty(&S.rxCbList[queueType].cb_list)) {
        LTList_AddTail(&S.rxCbList[queueType].cb_list, &node->node);
    } else {
        bool inserted = false;
        LTList_ForEach(pNode, &S.rxCbList[queueType].cb_list) {
            LTNetDataPathRxCbNode *pNodeData = LT_CONTAINER_OF(pNode, LTNetDataPathRxCbNode, node);
            if (pNodeData->prio < node->prio) {
                LTList_InsertBefore(pNode, &node->node);
                inserted = true;
                break;
            }
        } LTList_EndForEach;
        if (!inserted) {
            LTList_AddTail(&S.rxCbList[queueType].cb_list, &node->node);
        }
    }
    S.rxCbList[queueType].mutex->API->Unlock(S.rxCbList[queueType].mutex);
    return true;
}

static bool LTNetDataPathImpl_UnSubscribeFromQueue(LTNetDataPathQueueType queueType, LTNetDataPathReceiveCallback callback, void *context) {
    if (queueType >= kLTNetDataPathQueueType_Max || !IS_BIT_VALID(RXQUALIFY_QUEUE_MASK, BIT(queueType)) || S.netQueue[queueType] == NULL) {
        LTLOG_REDALERT("invalid.args", "LTNetDataPathImpl_UnSubscribeFromQueue: Invalid arguments");
        DEBUG_ASSERT_LTNETDP(0);
        return false;
    }
    S.rxCbList[queueType].mutex->API->Lock(S.rxCbList[queueType].mutex);
    LTList_ForEach(pNode, &S.rxCbList[queueType].cb_list) {
        LTNetDataPathRxCbNode *pNodeData = LT_CONTAINER_OF(pNode, LTNetDataPathRxCbNode, node);
        if (pNodeData->rx_cb == callback && pNodeData->rx_cb_ctx == context) {
            LTList_Remove(pNode);
            lt_free(pNodeData);
            break;
        }
    } LTList_EndForEach;
    S.rxCbList[queueType].mutex->API->Unlock(S.rxCbList[queueType].mutex);
    return true;
}

static void LTNetDataPathImpl_EnableLog(bool enable) {
    S.log_en = enable;
}

static void LTNetDataPathImpl_ForceNotify(LTNetDataPathQueueType queueType) {
    if (queueType >= kLTNetDataPathQueueType_Max || S.netQueue[queueType] == NULL) {
        LTLOG_REDALERT("invalid.args", "LTNetDataPathImpl_ForceNotify: Invalid arguments");
        DEBUG_ASSERT_LTNETDP(0);
        return;
    }
    S.netQueue[queueType]->API->Notify(S.netQueue[queueType]);
}

static void LTNetDataPathImpl_SetHardwareStatsLogCallback(LTNetDataPathHwStatsLogCallback cb) {
    S.hw_stats_log_cb = cb;
}

/*********************************************************
 * Common API Implementation
 *********************************************************/

static void LTNetDataPathHandoverDataToInterestedParty(LTNetDataPathQueueType queueType) {
    if (queueType >= kLTNetDataPathQueueType_Max || S.netQueue[queueType] == NULL) {
        LTLOG_REDALERT("invalid.args", "LTNetDataPathHandoverDataToInterestedParty: Invalid arguments");
        DEBUG_ASSERT_LTNETDP(0);
        return;
    }
    //Check if any interested party is there
    LTNetQueue *queue = S.netQueue[queueType];
    S.rxCbList[queueType].mutex->API->Lock(S.rxCbList[queueType].mutex);
    if (!LTList_IsEmpty(&S.rxCbList[queueType].cb_list)) {
        while(queue->API->Size(queue) > 0) {
            LTNet_buff *buff = (LTNet_buff*)queue->API->Pop(queue);
            bool consumed = false;
            if (!buff) {
                // No Buff but still queue size is non-zero.
                // Assert
                LTLOG_REDALERT_AND_REBOOT("no.buff", "LTNetDataPathHandoverDataToInterestedParty: No Buff in the queue");
                DEBUG_ASSERT_LTNETDP(0);
            }
            // Process the buff, keep sending packets to the interested party
            // Till someone accepts it
            LTList_ForEach(pNode, &S.rxCbList[queueType].cb_list) {
                LTNetDataPathRxCbNode *pNodeData = LT_CONTAINER_OF(pNode, LTNetDataPathRxCbNode, node);
                if (pNodeData->rx_cb) {
                    if (pNodeData->rx_cb(queueType, buff, pNodeData->rx_cb_ctx)) {
                        //Data has been consumed by the interested party
                        //Eject Out
                        PLOG("data.consumed", "LTNetDataPathHandoverDataToInterestedParty: Data consumed by the interested party");
                        consumed = true;
                        break;
                    }
                }
            } LTList_EndForEach;
            if(!consumed) {
                //No one is interested in the data, free the buffer
                PLOG("data.not.consumed", "LTNetDataPathHandoverDataToInterestedParty: Data not consumed by the interested party");
                buff->compl_cb(buff, NULL);
            }
        }
    }
    S.rxCbList[queueType].mutex->API->Unlock(S.rxCbList[queueType].mutex);
}

/*********************************************************
 * BUS Thread Implementation
 *********************************************************/

static bool LTNetDataPathBusProcessRxPacket(void) {
    u32 queue_notify_bitmap = 0x00;
    // Process the packet
    LTNetQueue *queue_process = S.netQueue[kLTNetDataPathQueueType_RxDataQueue];
    if (queue_process->API->Size(queue_process) <= 0) {
        // No Buff in the queue
        return false;
    }
    while (queue_process->API->Size(queue_process) > 0) {
        {
            // Check if the buffer we are going to pull can be accommodated
            // in the destination queue, if not then break
            LTNet_buff *peek_buff = (LTNet_buff *)queue_process->API->Peek(queue_process);
            LTNetDataPathPktHdr* pkt_hdr = (LTNetDataPathPktHdr*)peek_buff->data;
            if (pkt_hdr && pkt_hdr->queue_type < kLTNetDataPathQueueType_Max && S.netQueue[pkt_hdr->queue_type]) {
                if (S.netQueue[pkt_hdr->queue_type]->API->IsFull(S.netQueue[pkt_hdr->queue_type])) {
                    // Cannot process further
                    // Cause head of Line blocking
                    break;
                }
            }
        }
        LTNet_buff *buff = (LTNet_buff*)queue_process->API->Pop(queue_process);
        if (unlikely(!buff)) {
            // No Buff but still queue size is non-zero.
            // Assert
            LTLOG_REDALERT_AND_REBOOT("no.buff", "LTNetDataPathBusThreadNotify: No Buff in the queue");
            DEBUG_ASSERT_LTNETDP(0);
        }
        // Process the buff
        LTNetDataPathPktHdr* pkt_hdr = (LTNetDataPathPktHdr*)buff->data;
        S.netBuffer->Pull(buff, sizeof(LTNetDataPathPktHdr));
        PLOG(__FUNCTION__, "queue_type %s", LTNetDataPathQueueTypeToString(pkt_hdr->queue_type));
        if (RXQUALIFY_QUEUE_MASK & BIT(pkt_hdr->queue_type)) {
            LTNetQueue *queue_notify = S.netQueue[pkt_hdr->queue_type];
            if (queue_notify) {
                if (unlikely(queue_notify->API->Push(queue_notify, buff) == false)) {
                    LTLOG_YELLOWALERT("q.push.fail", "%s: Push failed for queue %s",
                        __FUNCTION__,
                        LTNetDataPathQueueTypeToString(pkt_hdr->queue_type));
                    buff->compl_cb(buff, NULL);
                    if (queue_notify_bitmap & BIT(pkt_hdr->queue_type)) {
                        //NOTE: Notifying the queue so that it can be flushed?
                        //NOTE: Not sure about this decision, we will see
                        queue_notify->API->Notify(queue_notify);
                    }
                } else {
                    // Successfully Dispacthed the packet
                    queue_notify_bitmap |= BIT(pkt_hdr->queue_type);
                }
            } else {
                LTLOG_REDALERT_AND_REBOOT("no.ltnetqueue",
                    "%s: LTNetQueue object not found for queue type %s",
                    __FUNCTION__,
                    LTNetDataPathQueueTypeToString(pkt_hdr->queue_type));
                DEBUG_ASSERT_LTNETDP(0);
            }
        } else {
            LTLOG_YELLOWALERT("invalid.queue", "%s: Invalid queue type %d", __FUNCTION__, pkt_hdr->queue_type);
            buff->compl_cb(buff, NULL);
        }
    }
    // Notify the queues
    for (int i = 0; i < kLTNetDataPathQueueType_Max; i++) {
        if (queue_notify_bitmap & BIT(i)) {
            LTNetQueue *queue_notify = S.netQueue[i];
            if (queue_notify) {
                PLOG("q.notify", "%s: Notifying queue %s", __FUNCTION__, LTNetDataPathQueueTypeToString(i));
                queue_notify->API->Notify(queue_notify);
            }
        }
    }
    return true;
}
LTAtomic res_avail;

static const LTArgsDescriptor EventArgs = {2, { kLTArgType_u32, kLTArgType_s64 }};
static void DispatchEvent(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    if (!proc) return;
    LTTime time = (LTTime) {.nNanoseconds = LTArgs_s64At(1, args)};
    (*(LTNetHWResEventCallback)proc)(LTArgs_u32At(0, args), time, data);
}
bool LTNetDataPathBusProcessTxTransmit(LTNetDataPathQueueType type) {
    LTNetQueue *queue_transmit_from = S.netQueue[type];
    if (!queue_transmit_from || queue_transmit_from->API->Size(queue_transmit_from) <= 0) {
        LTLOG_DEBUG(__FUNCTION__, "No data in the queue %s", LTNetDataPathQueueTypeToString(type));
        return false;
    }

    while (queue_transmit_from->API->Size(queue_transmit_from) > 0) {
        u32 spin_count = 0;
        while (S.is_okay_to_transmit_cb && !S.is_okay_to_transmit_cb(type)) {
            if (LTAtomic_CompareAndExchange(&res_avail, true, false)) {
                // Fire the event
                S.iEvent->NotifyEvent(S.hwres_event, kLTNetHWResEvent_Unavailable, S.core->GetKernelTime().nNanoseconds);
            }
            // Not okay to transmit, so spin till you get the green signal
            PLOG("not.okay.to.transmit", "%s: Not okay to transmit for queue %s",
                        __FUNCTION__, LTNetDataPathQueueTypeToString(type));
            if(spin_count++ > 10000) {
                // We have been spinning for a long time, so break the loop
                return false;
            }
        }
        if (LTAtomic_CompareAndExchange(&res_avail, false, true)) {
            // Fire the event
            S.iEvent->NotifyEvent(S.hwres_event, kLTNetHWResEvent_Available, S.core->GetKernelTime().nNanoseconds);
        }
        LTNet_buff *buff = (LTNet_buff*)queue_transmit_from->API->Pop(queue_transmit_from);
        if (!buff) {
            // No Buff but still queue size is non-zero.
            // Assert
            LTLOG_REDALERT_AND_REBOOT("no.buff", "LTNetDataPathBusProcessTxTransmit: No Buff in the queue");
            DEBUG_ASSERT_LTNETDP(0);
        }
        //Transmit the packet
        if (!S.send_tx_cb(buff, S.send_tx_cb_ctx, buff->compl_cb)) {
            //For now because of lack of queue discipline, we are just dropping the packets
            LTLOG_YELLOWALERT("transmit.fail", "%s: Transmit failed for queue %s",
                __FUNCTION__,
                LTNetDataPathQueueTypeToString(type));
        } else {
            // Successfully transmitted the packet
            PLOG("transmit.done", "%s: Transmitted the packet from queue %s", __FUNCTION__, LTNetDataPathQueueTypeToString(type));
        }
    }
    return true;
}

static bool LTNetDataPathBusProcessTx(void) {
    // Process the TX queue based on Priority
    LTNetDataPathBusProcessTxTransmit(kLTNetDataPathQueueType_WlanCmdQueue);
    LTNetDataPathBusProcessTxTransmit(kLTNetDataPathQueueType_BleCmdQueue);
    LTNetDataPathBusProcessTxTransmit(kLTNetDataPathQueueType_SocketCmdQueue);
    LTNetDataPathBusProcessTxTransmit(kLTNetDataPathQueueType_NetworkCmdQueue);
    LTNetDataPathBusProcessTxTransmit(kLTNetDataPathQueueType_CommonCmdQueue);
    LTNetDataPathBusProcessTxTransmit(kLTNetDataPathQueueType_SocketTxDataQueue);

    return true;
}

static void LTNetDataPathBusThreadNotify(void *queue_data) {
    QueueMetaData* queue_meta_data = (QueueMetaData*)queue_data;
    if (queue_meta_data) {
        LTNetQueue *queue = S.netQueue[queue_meta_data->queueType];
        LT_UNUSED(queue);
        PLOG(__FUNCTION__,"queue %s(%p) size %lu(%lu) ",
            LTNetDataPathQueueTypeToString(queue_meta_data->queueType),
            queue,
            LT_Pu32(queue_meta_data->queueSize),
            LT_Pu32(queue->API->Size(queue)));
    }
    // Here we can process the queue data
    // We can disregard which queue has notified us and we can fetrch data from the queue
    // based on the priority and process it.

    // First handle the kLTNetDataPathQueueType_RxDataQueue
    // That's prioritizing the RX data first.
    if (LTNetDataPathBusProcessRxPacket()) {
        LTLOG_DEBUG("rx.queue.proc.done", "LTNetDataPathBusThreadNotify: Processed the RxDataQueue");
    }

    //Now handle the TX data queues
    if (LTNetDataPathBusProcessTx()) {
        LTLOG_DEBUG("tx.queue.proc.done", "LTNetDataPathBusThreadNotify: Processed the TxDataQueues");
    }
}

static void LTNetDataPathBusThreadQueueNotifyRelCb(LTThread_ReleaseReason releaseReason, void *pClientData) {
    LT_UNUSED(releaseReason);
    PLOG("q.notify.rel.cb", "LTNetDataPathBusThreadQueueNotifyRelCb: releaseReason %lu", LT_Pu32(releaseReason));
    QueueMetaData* queue_data = (QueueMetaData*)pClientData;
    if(queue_data) lt_free(queue_data);
}

static bool LTNetDataPathBusThreadStart(void) {
    for (int i = 0; i < kLTNetDataPathQueueType_Max; i++) {
        if (BUSTHREAD_QUEUE_MASK & BIT(i)) {
            if (S.netQueue[i] != NULL) {
                QueueMetaData* meta_data = (QueueMetaData*)lt_malloc(sizeof(QueueMetaData));
                if (meta_data == NULL) {
                    LTLOG_REDALERT_AND_REBOOT("malloc.fail", "LTNetDataPathBusThreadStart: malloc failed for queue %s", LTNetDataPathQueueTypeToString(i));
                    LT_ASSERT(0);
                    return false;
                }
                meta_data->queueSize = get_queue_size(i);
                meta_data->queueType = i;
                S.netQueue[i]->API->RegisterNotification(S.netQueue[i], LTNetDataPathBusThreadNotify,
                                                        LTNetDataPathBusThreadQueueNotifyRelCb,
                                                        meta_data, false);
            } else {
                LTLOG_YELLOWALERT("no.ltnetqueue", "LTNetDataPathBusThreadStart: LTNetQueue object not found for queue type %s", LTNetDataPathQueueTypeToString(i));
                DEBUG_ASSERT_LTNETDP(0);
            }
        }
    }
    S.iThread->SetTimer(S.busThread, LTTime_Milliseconds(LTNET_DATAPATH_PERIODIC_FIRE), LTNetDataPathBusThreadNotify, NULL, NULL);
    return true;
}

static void LTNetDataPathBusThreadStop(void) {
    for (int i = 0; i < kLTNetDataPathQueueType_Max; i++) {
        if (BUSTHREAD_QUEUE_MASK & BIT(i)) {
            if (S.netQueue[i] != NULL) {
                S.netQueue[i]->API->UnRegisterNotification(S.netQueue[i], LTNetDataPathBusThreadNotify);
            }
        }
    }
    S.iThread->KillTimer(S.busThread, LTNetDataPathBusThreadNotify, NULL);
    return;
}

/*********************************************************
 * Event RX Thread Implementation
 *********************************************************/

static void LTNetDataPathEvtRxThreadNotify(void *queue_data) {
    QueueMetaData* queue_meta_data = (QueueMetaData*)queue_data;
    if (!queue_meta_data) {
        LTLOG_REDALERT("invalid.args", "LTNetDataPathRespRxThreadNotify: Invalid arguments");
        DEBUG_ASSERT_LTNETDP(0);
        return;
    }
    // Here we can process the queue data
    // We can disregard which queue has notified us and we can fetrch data from the queue
    // based on the priority and process it.
    PLOG("queue.notify.hdl", "Handling LTNetDataPathEvtRxThreadNotify: queueType %s", LTNetDataPathQueueTypeToString(queue_meta_data->queueType));
    LTNetDataPathHandoverDataToInterestedParty(queue_meta_data->queueType);
}

static void LTNetDataPathEvtRxThreadQueueNotifyRelCb(LTThread_ReleaseReason releaseReason, void *pClientData) {
    LT_UNUSED(releaseReason);
    PLOG("q.notify.rel.cb", "LTNetDataPathEvtRxThreadQueueNotifyRelCb: releaseReason %lu", LT_Pu32(releaseReason));
    QueueMetaData* queue_data = (QueueMetaData*)pClientData;
    if(queue_data) lt_free(queue_data);
}

static bool LTNetDataPathEvtRxThreadStart(void) {
    for (int i = 0; i < kLTNetDataPathQueueType_Max; i++) {
        if (EVENTRX_QUALIFY_MASK & BIT(i)) {
            if (S.netQueue[i] != NULL) {
                QueueMetaData* meta_data = (QueueMetaData*)lt_malloc(sizeof(QueueMetaData));
                if (meta_data == NULL) {
                    LTLOG_REDALERT_AND_REBOOT("malloc.fail", "LTNetDataPathEvtRxThreadStart: malloc failed for queue %s", LTNetDataPathQueueTypeToString(i));
                    DEBUG_ASSERT_LTNETDP(0);
                    return false;
                }
                meta_data->queueSize = get_queue_size(i);
                meta_data->queueType = i;
                S.netQueue[i]->API->RegisterNotification(S.netQueue[i], LTNetDataPathEvtRxThreadNotify,
                                                        LTNetDataPathEvtRxThreadQueueNotifyRelCb,
                                                        meta_data, false);
            } else {
                LTLOG_YELLOWALERT("no.ltnetqueue", "LTNetDataPathEvtRxThreadStart: LTNetQueue object not found for queue type %s", LTNetDataPathQueueTypeToString(i));
                DEBUG_ASSERT_LTNETDP(0);
            }
        }
    }
    return true;
}

static void LTNetDataPathEvtRxThreadStop(void) {
    for (int i = 0; i < kLTNetDataPathQueueType_Max; i++) {
        if (EVENTRX_QUALIFY_MASK & BIT(i)) {
            if (S.netQueue[i] != NULL) {
                S.netQueue[i]->API->UnRegisterNotification(S.netQueue[i], LTNetDataPathEvtRxThreadNotify);
            }
        }
    }
    return;
}

/*********************************************************
 * Response RX Thread Implementation
 *********************************************************/
static void LTNetDataPathRespRxThreadNotify(void *queue_data) {
    QueueMetaData* queue_meta_data = (QueueMetaData*)queue_data;
    if (!queue_meta_data) {
        LTLOG_REDALERT("invalid.args", "LTNetDataPathRespRxThreadNotify: Invalid arguments");
        DEBUG_ASSERT_LTNETDP(0);
        return;
    }
    // Here we can process the queue data
    // We can disregard which queue has notified us and we can fetrch data from the queue
    // based on the priority and process it.
    LTNetDataPathHandoverDataToInterestedParty(queue_meta_data->queueType);
}

static void LTNetDataPathRespRxThreadQueueNotifyRelCb(LTThread_ReleaseReason releaseReason, void *pClientData) {
    LT_UNUSED(releaseReason);
    PLOG("q.notify.rel.cb", "LTNetDataPathRespRxThreadQueueNotifyRelCb: releaseReason %lu", LT_Pu32(releaseReason));
    QueueMetaData* queue_data = (QueueMetaData*)pClientData;
    if(queue_data) lt_free(queue_data);
}

static bool LTNetDataPathRespRxThreadStart(void) {
    for (int i = 0; i < kLTNetDataPathQueueType_Max; i++) {
        if (RESPRX_QUALIFY_MASK & BIT(i)) {
            if (S.netQueue[i] != NULL) {
                QueueMetaData* meta_data = (QueueMetaData*)lt_malloc(sizeof(QueueMetaData));
                if (meta_data == NULL) {
                    LTLOG_REDALERT_AND_REBOOT("malloc.fail", "LTNetDataPathRespRxThreadStart: malloc failed for queue %s", LTNetDataPathQueueTypeToString(i));
                    DEBUG_ASSERT_LTNETDP(0);
                    return false;
                }
                meta_data->queueSize = get_queue_size(i);
                meta_data->queueType = i;
                S.netQueue[i]->API->RegisterNotification(S.netQueue[i], LTNetDataPathRespRxThreadNotify,
                                                        LTNetDataPathRespRxThreadQueueNotifyRelCb,
                                                        meta_data, false);
            } else {
                LTLOG_YELLOWALERT("no.ltnetqueue", "LTNetDataPathRespRxThreadStart: LTNetQueue object not found for queue type %s", LTNetDataPathQueueTypeToString(i));
                DEBUG_ASSERT_LTNETDP(0);
            }
        }
    }
    return true;
}

static void LTNetDataPathRespRxThreadStop(void) {
    for (int i = 0; i < kLTNetDataPathQueueType_Max; i++) {
        if (RESPRX_QUALIFY_MASK & BIT(i)) {
            if (S.netQueue[i] != NULL) {
                S.netQueue[i]->API->UnRegisterNotification(S.netQueue[i], LTNetDataPathRespRxThreadNotify);
            }
        }
    }
    return;
}

static bool LTNetDataPathImpl_SetQueueWatermark(LTNetDataPathQueueType queueType, u32 watermark) {
    if (queueType >= kLTNetDataPathQueueType_Max || S.netQueue[queueType] == NULL) {
        LTLOG_REDALERT("invalid.args", "LTNetDataPathImpl_SetQueueWatermark: Invalid arguments");
        return false;
    }
    return S.netQueue[queueType]->API->SetWatermark(S.netQueue[queueType], watermark);
}

static u32 LTNetDataPathImpl_GetQueueWatermark(LTNetDataPathQueueType queueType) {
    if (queueType >= kLTNetDataPathQueueType_Max || S.netQueue[queueType] == NULL) {
        LTLOG_REDALERT("invalid.args", "LTNetDataPathImpl_GetQueueWatermark: Invalid arguments");
        return 0;
    }
    return S.netQueue[queueType]->API->GetWatermark(S.netQueue[queueType]);
}

static bool LTNetDataPathImpl_RegisterQueueEventCallback(LTNetDataPathQueueType queueType, LTNetQueueEventCallback eventCallback,
                                                        LTNetQueueEventClientDataReleaseProc relCallback, void *pReceiverClientData, bool bNotifyEventStateImmediately) {
    if (queueType >= kLTNetDataPathQueueType_Max || S.netQueue[queueType] == NULL) {
        LTLOG_REDALERT("invalid.args", "LTNetDataPathImpl_RegisterQueueEventCallback: Invalid arguments");
        return false;
    }
    return S.netQueue[queueType]->API->RegisterEventCallback(S.netQueue[queueType], eventCallback, relCallback, pReceiverClientData, bNotifyEventStateImmediately);
}

static bool LTNetDataPathImpl_UnRegisterQueueEventCallback(LTNetDataPathQueueType queueType, LTNetQueueEventCallback eventCallback) {
    if (queueType >= kLTNetDataPathQueueType_Max || S.netQueue[queueType] == NULL) {
        LTLOG_REDALERT("invalid.args", "LTNetDataPathImpl_UnRegisterQueueEventCallback: Invalid arguments");
        return false;
    }
    return S.netQueue[queueType]->API->UnRegisterEventCallback(S.netQueue[queueType], eventCallback);
}
/*********************************************************
 * Response Library Init and Fini Implementation
 *********************************************************/

static bool LTNetDataPathImpl_LibInit(void) {
    S = (struct Statics){};
    S.core = LT_GetCore();
    S.netBuffer = lt_openlibrary(LTNetBuffer);
    if (S.netBuffer == NULL) {
        LTLOG_REDALERT_AND_REBOOT("no.ltnetbuff.lib", "LTNetDataPathImpl_LibInit: LTNetBuffer library not found");
        DEBUG_ASSERT_LTNETDP(0);
        return false;
    }
    for (int i = 0; i < kLTNetDataPathQueueType_Max; i++) {
        S.netQueue[i] = lt_createobject(LTNetQueue);
        if (S.netQueue[i] == NULL) {
            LTLOG_REDALERT_AND_REBOOT("ltnetqueue.obj.fail", "LTNetDataPathImpl_LibInit: LTNetQueue object not found");
            DEBUG_ASSERT_LTNETDP(0);
            return false;
        }
        u32 queueSize = get_queue_size(i);
        if (queueSize>0) {
            if (!S.netQueue[i]->API->Create(S.netQueue[i], queueSize)) {
                LTLOG_REDALERT_AND_REBOOT("ltnetqueue.create.fail", "LTNetDataPathImpl_LibInit: LTNetQueue create failed for queue type %s",
                    LTNetDataPathQueueTypeToString(i));
                DEBUG_ASSERT_LTNETDP(0);
                return false;
            }
            S.rxCbList[i].mutex = lt_createobject(LTMutex);
            if (S.rxCbList[i].mutex == NULL) {
                LTLOG_REDALERT_AND_REBOOT("no.ltmutex", "LTNetDataPathImpl_LibInit: LTMutex object not found");
                DEBUG_ASSERT_LTNETDP(0);
                return false;
            }
            LTList_Init(&S.rxCbList[i].cb_list);
        }
    }

    S.iThread = lt_getlibraryinterface(ILTThread, S.core);
    if (S.iThread == NULL) {
        LTLOG_REDALERT_AND_REBOOT("no.iltthread.libintf", "LTNetDataPathImpl_LibInit: ILTThread library interface not found");
        DEBUG_ASSERT_LTNETDP(0);
        return false;
    }

    //Setup the bus Thread
    S.busThread = S.core->CreateThread("busThread");
    if (!S.busThread) {
        LTLOG_REDALERT_AND_REBOOT("no.busthread", "LTNetDataPathImpl_LibInit: busThread create failed");
        DEBUG_ASSERT_LTNETDP(0);
        return false;
    }
    S.iThread->SetPriority(S.busThread, LTNETDP_BUSTHR_PRIORITY); //Can be platform specific
    S.iThread->SetStackSize(S.busThread, LTNETDP_BUSTHR_STACKSIZE); //Can be platform specific
    S.iThread->Start(S.busThread, LTNetDataPathBusThreadStart, LTNetDataPathBusThreadStop);
    S.hwres_event = S.core->CreateEvent(&EventArgs, DispatchEvent, NULL, NULL, NULL);
    S.iEvent = lt_getlibraryinterface(ILTEvent, S.core);
    LTAtomic_Store(&res_avail, true);

    //Setup the Event RX thread
    S.eventRxThread = S.core->CreateThread("eventRxThread");
    if (!S.eventRxThread) {
        LTLOG_REDALERT_AND_REBOOT("no.eventRxThread", "LTNetDataPathImpl_LibInit: eventRxThread create failed");
        DEBUG_ASSERT_LTNETDP(0);
        return false;
    }
    S.iThread->SetPriority(S.eventRxThread, LTNETDP_EVENTRXTHR_PRIORITY); //Can be platform specific
    S.iThread->SetStackSize(S.eventRxThread, LTNETDP_EVENTRXTHR_STACKSIZE); //Can be platform specific
    S.iThread->Start(S.eventRxThread, LTNetDataPathEvtRxThreadStart, LTNetDataPathEvtRxThreadStop);

    //Setup the Response RX thread
    S.respRxThread = S.core->CreateThread("respRxThread");
    if (!S.respRxThread) {
        LTLOG_REDALERT_AND_REBOOT("no.respRxThread", "LTNetDataPathImpl_LibInit: respRxThread create failed");
        DEBUG_ASSERT_LTNETDP(0);
        return false;
    }
    S.iThread->SetPriority(S.respRxThread, LTNETDP_RESPONSERXTHR_PRIORITY); //Can be platform specific
    S.iThread->SetStackSize(S.respRxThread, LTNETDP_RESPONSERXTHR_STACKSIZE); //Can be platform specific
    S.iThread->Start(S.respRxThread, LTNetDataPathRespRxThreadStart, LTNetDataPathRespRxThreadStop);

    return true;
}

static void LTNetDataPathImpl_LibFini(void) {
    // Order is important.
    // Stop the threads and then destroy the queue.
    // If we destroy the queue first, then the thread will try to access the queue
    if (S.busThread) {
        S.iThread->Terminate(S.busThread);
        S.iThread->WaitUntilFinished(S.busThread, LTTime_Infinite());
        lt_destroyhandle(S.busThread);
    }

    if (S.eventRxThread) {
        S.iThread->Terminate(S.eventRxThread);
        S.iThread->WaitUntilFinished(S.eventRxThread, LTTime_Infinite());
        lt_destroyhandle(S.eventRxThread);
    }

    if (S.respRxThread) {
        S.iThread->Terminate(S.respRxThread);
        S.iThread->WaitUntilFinished(S.respRxThread, LTTime_Infinite());
        lt_destroyhandle(S.respRxThread);
    }

    for (int i = 0; i < kLTNetDataPathQueueType_Max; i++) {
        if (S.netQueue[i] != NULL) {
            while (S.netQueue[i]->API->Size(S.netQueue[i]) > 0) {
                LTNet_buff *buff = (LTNet_buff*)S.netQueue[i]->API->Pop(S.netQueue[i]);
                if (buff) {
                    buff->compl_cb(buff, NULL);
                }
            }
            S.netQueue[i]->API->Destroy(S.netQueue[i]);
            lt_destroyobject(S.netQueue[i]);
            S.netQueue[i] = NULL;

            //Destroy the list
            //Take the mutex lock
            S.rxCbList[i].mutex->API->Lock(S.rxCbList[i].mutex);
            //Free the memory in the list
            LTList_ForEach(pNode, &S.rxCbList[i].cb_list) {
                LTList_Remove(pNode);
                LTNetDataPathRxCbNode *pRxCbNode = LT_CONTAINER_OF(pNode, LTNetDataPathRxCbNode, node);
                lt_free(pRxCbNode);
            } LTList_EndForEach;
            //Release the lock
            S.rxCbList[i].mutex->API->Unlock(S.rxCbList[i].mutex);
            //Destroy the lock object
            lt_destroyobject(S.rxCbList[i].mutex);
        }
    }
    if (S.netBuffer != NULL) {
        lt_closelibrary(S.netBuffer);
    }
    S = (struct Statics){};
}

bool LTNetDataPathImpl_RegisterHWResEventCallback(LTNetHWResEventCallback cb, LTNetHwResEventClientDataReleaseProc relCallback, void *pReceiverClientData, bool bNotifyEventStateImmediately) {
    S.iEvent->RegisterForEvent(S.hwres_event, cb, relCallback, pReceiverClientData, bNotifyEventStateImmediately);
    return true;
}
bool LTNetDataPathImpl_UnRegisterHWResEventCallback(LTNetHWResEventCallback cb) {
    S.iEvent->UnregisterFromEvent(S.hwres_event, cb);
    return true;
}

/*______________________
 / Library Interfaces */
define_LTLIBRARY_ROOT_INTERFACE(LTNetDataPath)
    .Transmit                       = LTNetDataPathImpl_Transmit,
    .Receive                        = LTNetDataPathImpl_Receive,
    .SetSendTxCallback              = LTNetDataPathImpl_SetSendTxCallback,
    .SetIsOkayToTransmitCallback    = LTNetDataPathImpl_SetIsOkayToTransmitCallback,
    .GetStatistics                  = LTNetDataPathImpl_GetStatistics,
    .SubscribeToQueue               = LTNetDataPathImpl_SubscribeToQueue,
    .UnSubscribeFromQueue           = LTNetDataPathImpl_UnSubscribeFromQueue,
    .EnableLog                      = LTNetDataPathImpl_EnableLog,
    .ForceNotify                    = LTNetDataPathImpl_ForceNotify,
    .SetHardwareStatsLogCallback    = LTNetDataPathImpl_SetHardwareStatsLogCallback,
    .SetQueueWatermark              = LTNetDataPathImpl_SetQueueWatermark,
    .GetQueueWatermark              = LTNetDataPathImpl_GetQueueWatermark,
    .RegisterQueueEventCallback     = LTNetDataPathImpl_RegisterQueueEventCallback,
    .UnRegisterQueueEventCallback   = LTNetDataPathImpl_UnRegisterQueueEventCallback,
    .RegisterHWResEventCallback     = LTNetDataPathImpl_RegisterHWResEventCallback,
    .UnRegisterHWResEventCallback   = LTNetDataPathImpl_UnRegisterHWResEventCallback,
    .LogStatistics                  = LTNetDataPathImpl_LogStatistics,
LTLIBRARY_DEFINITION;
/************************************************************************************
 * LOG
 *************************************************************************************
 *  01-Sep-24   galba       created
 */