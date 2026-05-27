/*******************************************************************************
 *
 * LTIPCBufferManager: LT Library for managing bulk data buffers between on-device SoCs
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/
#include <lt/LT.h>
#include <lt/core/LTArray.h>
#include <lt/product/config/LTProductConfig.h>
#include <lt/device/sdio/LTDeviceSdioPort.h>
#include <lt/system/ipc/buffermanager/LTIPCBufferManager.h>

DEFINE_LTLOG_SECTION("ipc.buf")
#define P(...)


/*******************************************************************************
 * Static Variables & Types
 ******************************************************************************/

 // General constants
enum {
    kIpcTransportBlockSize = kLTDeviceSdioPort_BlockSize * 4,
    ///< The maximum raw packet size that can be sent over the IPC transport.
    //
    // This includes a control header used by the LTIPCBufferManager IPC protocol,
    // therefore the total user data that can be sent in an IPC transaction is slightly
    // less. The value has had minimal tuning - it is believed to give better throughput
    // than *1, but *8 did not result in any further throughput gains when running the
    // LTShellSDIO buffer test.
    //
    // This parameter is currently also used to set the local cache buffer capacity
    // when accessing a remote source buffer.
};
static const LTTime kTransportSyncRetryTime = LTTimeInitializer_Milliseconds(250); // Initiator time to wait before retrying a sync operation - might need tuned down to speed up boot
static const LTTime kOperationPendingTimeout = LTTimeInitializer_Seconds(2);

// The top bit of a buffer ID indicates whether it is pinned on the initiator side (set) or the responder side (clear)
#define BUFFER_ID_SRC_MASK  0x80000000
#define BUFFER_ID_MASK      0x7FFFFFFF

#define IND_CMD_FLAG 0x00   // Indication command flag, no response expected
#define REQ_CMD_FLAG 0x80   // Request command flag, requires a response from the peer
#define RSP_CMD_FLAG 0x40   // Response command flag, indicates a response to the preceding request
#define CMD_MASK     0x3F   // Mask for the command portion of the header
#define TYPE_MASK    0xC0   // Mask for the type portion of the header

typedef_LTENUM_SIZED(IpcTransportCmd, u8) {
    // These values are used to index into the command handler array
    // below, so keep them tightly packed.
    IpcTransportCmd_Sync            = 0,
    IpcTransportCmd_CreateBuffer    = 1,
    IpcTransportCmd_DestroyBuffer   = 2,
    IpcTransportCmd_GetBufferState  = 3,
    IpcTransportCmd_Push            = 4,
    IpcTransportCmd_Pull            = 5,
    IpcTransportCmd_MoreData        = 6,
    IpcTransportCmd_DataConsumed    = 7,
    IpcTransportCmd_BufferLoss      = 8,
};

typedef_LTENUM_SIZED(IpcTransportStatus, u8) {
    IpcTransportStatus_Success      = 0,
    IpcTransportStatus_Pending      = 1,
    IpcTransportStatus_Failed       = 2,
    IpcTransportStatus_OOM          = 3,
    IpcTransportStatus_NoBuffer     = 4,
    IpcTransportStatus_NotSource    = 5,
};

typedef struct {
    // Used by CreateBufferReq and GetBufferStateRsp
    u32 capacity;
} BufferParams;

typedef struct {
    LTBufferID bufferID;
    u16 size;
    IpcTransportCmd cmd;
    IpcTransportStatus status;
} IpcTransportHeader;

// Bitmask of flags for the LTAtomic completion in the Transport Operation below
#define TXP_OP_COMPLETION_PENDING   0x00
#define TXP_OP_COMPLETION_COMPLETED 0x01
#define TXP_OP_COMPLETION_AUTOFREE  0x80    /**< Automatically free the Op when it has been executed, nothing is waiting for completion */
typedef struct {
    LTList_Node node;
    LTAtomic complete;
    IpcTransportHeader hdr;
    u8 *payloadSource;
} TransportOperation;

// IPC transport state
typedef enum {
    kIpcTransport_Uninitialised,    /**< The IPC transport is not available */
    kIpcTransport_InitiatorPending, /**< This end is the transport initiator and is trying to initialise */
    kIpcTransport_InitiatorIdle,    /**< The transport is initialised, as initiator */
    kIpcTransport_ResponderWaiting, /**< This end is the transport responder and is waiting for the peer */
    kIpcTransport_ResponderIdle,    /**< The transport is initialised, as responder */
} kIpcTransportState;

typedef_LTObjectImpl(LTBuffer, LTBufferIPC) {
    LTBufferID bufferID;
    LTEvent statusEvent;            /**< Buffer-specific event to signal status changes (more data available, data consumed, or source buffer loss) */
    bool isSource;                  /**< Indicates the source buffer is owned by this subsystem */
    bool descriptorLocked;          /**< Indicates the associated buffer descriptor is locked during a StartRead operation (preventing concurrent accesses) */
} LTOBJECT_API;

// Mapping between buffer ID and underlying buffer storage
typedef struct {
    LTBufferID bufferID;
    u32 sourceCapacity;             /**< Capacity of source buffer (whether here or remote) */  // TBD if I need this here, ideally not
    LTBufferIPC *ipcBuf;            /**< IPC-aware buffer object, may be NULL */
    LTBuffer *rawBuf;               /**< Local raw buffer storage (never exposed outside this library) */
    LTMutex *rawBufMutex;           /**< Mutex to protect access to the raw buffer. DO NOT LOCK DIRECTLY - CALL LockDescriptor() */
    bool isSource;                  /**< True if this buffer is the source of data */
} BufferDescriptor;

/* Buffer event args for MoreData and BufferLoss events - these currently have the same args list */
static const LTArgsDescriptor s_BufferEventArgs = {2, {kLTArgType_u32, kLTArgType_pointer}};

/* Static references to LT libraries and interfaces, opened for long periods of time */
static struct Statics {
    LTCore *core;
    LTDeviceSdioPort *sdioPort;
    LTOThread *sdioReadThread;
    LTOThread *sdioWriteThread;
    kIpcTransportState ipcTransportState;

    // Modified only by the write thread
    LTList pendingOps;
    TransportOperation *curReq;

    // Modified by multiple threads, with mutex protection
    LTMutex *knownBuffersMutex;
    LTArray *knownBuffers;
    LTBufferID lastUsedBufferID;
} S;

/* Temporary buffers used to build the transport header and (optional) payload */
static u8 rxBuffer[kIpcTransportBlockSize];
static u8 txBuffer[kIpcTransportBlockSize];

// Forward decls for command handlers to populate the table below
static void cmdHandlerSync(IpcTransportHeader *hdr);
static void cmdHandlerCreateBuffer(IpcTransportHeader *hdr);
static void cmdHandlerDestroyBuffer(IpcTransportHeader *hdr);
static void cmdHandlerGetBufferState(IpcTransportHeader *hdr);
static void cmdHandlerPush(IpcTransportHeader *hdr);
static void cmdHandlerPull(IpcTransportHeader *hdr);
static void cmdHandlerMoreData(IpcTransportHeader *hdr);
static void cmdHandlerDataConsumed(IpcTransportHeader *hdr);
static void cmdHandlerBufferLoss(IpcTransportHeader *hdr);

typedef void (*IpcTransportCmdHandler)(IpcTransportHeader *hdr);
static const IpcTransportCmdHandler s_cmdHandlers[] = {
    [IpcTransportCmd_Sync]           = cmdHandlerSync,
    [IpcTransportCmd_CreateBuffer]   = cmdHandlerCreateBuffer,
    [IpcTransportCmd_DestroyBuffer]  = cmdHandlerDestroyBuffer,
    [IpcTransportCmd_GetBufferState] = cmdHandlerGetBufferState,
    [IpcTransportCmd_Push]           = cmdHandlerPush,
    [IpcTransportCmd_Pull]           = cmdHandlerPull,
    [IpcTransportCmd_MoreData]       = cmdHandlerMoreData,
    [IpcTransportCmd_DataConsumed]   = cmdHandlerDataConsumed,
    [IpcTransportCmd_BufferLoss]     = cmdHandlerBufferLoss,
};


/*******************************************************************************
 * Buffer Management
 ******************************************************************************/

 // Callback to compare buffer IDs in the knownBuffers array, for inserting and finding
static int CompareBufferIDs(const void *element1, const void *element2, void *clientData) {
    LT_UNUSED(clientData);
    BufferDescriptor *entry1 = (BufferDescriptor *)element1;
    BufferDescriptor *entry2 = (BufferDescriptor *)element2;

    if (entry1->bufferID < entry2->bufferID) {
        return -1;
    } else if (entry1->bufferID > entry2->bufferID) {
        return 1;
    } else {
        return 0;
    }
}

// Get the next free buffer ID, for a buffer owned by this side of the IPC transport
static LTBufferID GetNextFreeBufferID(void) {
    // Calculate the next free buffer ID, searching the knownBuffers array to make sure it is unused.
    // This is fairly lazy, but buffer IDs are allocated across a wide range and are very unlikely
    // to persist for long periods of time.
    LTBufferID newBufferID = S.lastUsedBufferID;
    do {
        newBufferID++;
        newBufferID &= BUFFER_ID_MASK;
        if (newBufferID == LT_BUFFER_ID_INVALID) newBufferID++;
        if (S.ipcTransportState == kIpcTransport_InitiatorIdle) {
            newBufferID |= BUFFER_ID_SRC_MASK;
        }

        BufferDescriptor searchTerm = { .bufferID = newBufferID };
        if (S.knownBuffers->API->Find(S.knownBuffers, CompareBufferIDs, &searchTerm, NULL) < 0) break; // buffer ID is not currently allocated
    } while(true);

    S.lastUsedBufferID = newBufferID;
    return newBufferID;
}

static IpcTransportStatus ReserveBufferID(BufferDescriptor *pBufDesc) {
    IpcTransportStatus status = IpcTransportStatus_Failed;

    if (pBufDesc) {
        S.knownBuffersMutex->API->Lock(S.knownBuffersMutex);
        BufferDescriptor *array = S.knownBuffers->API->GetStorage(S.knownBuffers);
        s32 index = S.knownBuffers->API->Find(S.knownBuffers, CompareBufferIDs, pBufDesc, NULL);
        if (index < 0) {
            status = IpcTransportStatus_NoBuffer;
        } else if (array[index].ipcBuf == NULL) {
            status = IpcTransportStatus_Success;
            array[index].ipcBuf = pBufDesc->ipcBuf;
            *pBufDesc = array[index];
        }
        S.knownBuffersMutex->API->Unlock(S.knownBuffersMutex);
    }

    return status;
}

static IpcTransportStatus ReleaseBufferID(LTBufferIPC *pIpcBuf) {
    IpcTransportStatus status = IpcTransportStatus_NoBuffer;
    BufferDescriptor bufDesc = { .bufferID = pIpcBuf->bufferID};

    S.knownBuffersMutex->API->Lock(S.knownBuffersMutex);
    BufferDescriptor *array = S.knownBuffers->API->GetStorage(S.knownBuffers);
    s32 index = S.knownBuffers->API->Find(S.knownBuffers, CompareBufferIDs, &bufDesc, NULL);
    if (index >= 0 && array[index].ipcBuf == pIpcBuf) {
        status = IpcTransportStatus_Success;
        array[index].ipcBuf = NULL;
    }
    S.knownBuffersMutex->API->Unlock(S.knownBuffersMutex);

    return status;
}

static bool GetOrLockOrDeleteDescriptor(BufferDescriptor *pBufDesc, bool lock, bool delete) {
    // Search for the ID in the knownBuffers array, return the BufferDescriptor, and (optionally) lock or delete it.
    // Don't try to lock and delete at the same time - delete will take precedence
    if (!pBufDesc) return false;

    bool retry = false;
    s32 index;
    do {
        S.knownBuffersMutex->API->Lock(S.knownBuffersMutex);
        index = S.knownBuffers->API->Find(S.knownBuffers, CompareBufferIDs, pBufDesc, NULL);
        if (index >= 0) {
            S.knownBuffers->API->Get(S.knownBuffers, index, pBufDesc);
            if (lock || delete) {
                retry = !pBufDesc->rawBufMutex->API->TryLock(pBufDesc->rawBufMutex);
                if (delete && !retry) {
                    S.knownBuffers->API->Remove(S.knownBuffers, index);
                    pBufDesc->rawBufMutex->API->Unlock(pBufDesc->rawBufMutex);
                }
            }
        }
        S.knownBuffersMutex->API->Unlock(S.knownBuffersMutex);

        if (retry) {
            // The ID couldn't be locked - yield then try again
            S.sdioWriteThread->API->Yield();
        }
    } while (retry);
    return (index >= 0);
}

static bool GetDescriptor(BufferDescriptor *pBufDesc) {
    return GetOrLockOrDeleteDescriptor(pBufDesc, false, false);
}

static bool LockDescriptor(BufferDescriptor *pBufDesc) {
    return GetOrLockOrDeleteDescriptor(pBufDesc, true, false);
}

static bool DeleteDescriptor(BufferDescriptor *pBufDesc) {
    bool success = GetOrLockOrDeleteDescriptor(pBufDesc, true, true);
    if (success) {
        lt_destroyobject(pBufDesc->rawBuf);
        pBufDesc->rawBuf = NULL;
        lt_destroyobject(pBufDesc->rawBufMutex);
        pBufDesc->rawBufMutex = NULL;
    }
    return success;
}

static bool CreateRemoteBufferRecord(BufferDescriptor *pBufDesc) {
    if (!pBufDesc) return false;

    // Create a local raw buffer acting as a read cache for the remote buffer
    pBufDesc->rawBufMutex = lt_createobject(LTMutex);
    if (!pBufDesc->rawBufMutex) return false;
    pBufDesc->rawBuf = lt_createobject(LTBuffer);
    if (!pBufDesc->rawBuf) {
        lt_destroyobject(pBufDesc->rawBufMutex);
        pBufDesc->rawBufMutex = NULL;
        return false;
    }
    pBufDesc->rawBuf->API->SetMaxCapacity(pBufDesc->rawBuf, kIpcTransportBlockSize);
    pBufDesc->rawBuf->API->Reserve(pBufDesc->rawBuf, kIpcTransportBlockSize);   // force realloc to correct size (default is 32 bytes)

    // Insert the new buffer descriptor into the knownBuffers array
    S.knownBuffersMutex->API->Lock(S.knownBuffersMutex);
    do {
        s32 index = S.knownBuffers->API->InsertSorted(S.knownBuffers, CompareBufferIDs, pBufDesc, NULL);
        if (index < 0) {
            lt_destroyobject(pBufDesc->rawBuf);
            pBufDesc->rawBuf = NULL;
            lt_destroyobject(pBufDesc->rawBufMutex);
            pBufDesc->rawBufMutex = NULL;
            break;
        }

        LTLOG_DEBUG("create.remote", "Created remote buffer record for: 0x%08lx", LT_Pu32(pBufDesc->bufferID));
    } while(false);

    S.knownBuffersMutex->API->Unlock(S.knownBuffersMutex);
    return (pBufDesc->rawBuf != NULL);
}

static LTBufferID CreateLocalBufferDescriptor(u32 capacity) {
    LTBufferID newBufferID = LT_BUFFER_ID_INVALID;

    BufferDescriptor bufDesc = {
        .bufferID = GetNextFreeBufferID(),
        .sourceCapacity = capacity,
        .ipcBuf = NULL,
        .rawBuf = lt_createobject(LTBuffer),
        .rawBufMutex = lt_createobject(LTMutex),
        .isSource = true,
    };
    if (!bufDesc.rawBuf || !bufDesc.rawBufMutex) {
        lt_destroyobject(bufDesc.rawBuf);
        bufDesc.rawBuf = NULL;
        lt_destroyobject(bufDesc.rawBufMutex);
        bufDesc.rawBufMutex = NULL;
        return newBufferID;
    }
    bufDesc.rawBuf->API->SetMaxCapacity(bufDesc.rawBuf, capacity);
    bufDesc.rawBuf->API->Reserve(bufDesc.rawBuf, capacity);   // force realloc to correct size (default is 32 bytes)

    S.knownBuffersMutex->API->Lock(S.knownBuffersMutex);
    do {
        s32 index = S.knownBuffers->API->InsertSorted(S.knownBuffers, CompareBufferIDs, &bufDesc, NULL);
        if (index < 0) {
            lt_destroyobject(bufDesc.rawBuf);
            bufDesc.rawBuf = NULL;
            lt_destroyobject(bufDesc.rawBufMutex);
            bufDesc.rawBufMutex = NULL;
            break;
        }
        newBufferID = bufDesc.bufferID;
        LTLOG("create.local", "Created local buffer ID 0x%08lx with size: %lu", LT_Pu32(newBufferID), LT_Pu32(capacity));
    } while(false);

    S.knownBuffersMutex->API->Unlock(S.knownBuffersMutex);

    return newBufferID;
}


/*******************************************************************************
 * Threads to handle SDIO read & write operations
 ******************************************************************************/
static void OperationRspTimeoutProc(void *pClientData);

static bool TriggerWrite(void) {
    IpcTransportHeader *hdr = (IpcTransportHeader *)txBuffer;

    // The SDIO transport requires a byte count to send, but that count must be a multiple of the block size.
    // Round up to SDIO block size (not transport block size), so that data is not truncated
    u32 size = sizeof(IpcTransportHeader) + hdr->size;
    size += (kLTDeviceSdioPort_BlockSize - 1);
    size &= ~(kLTDeviceSdioPort_BlockSize - 1);

    // We think SDIO should be robust, but recent concerns around CRC errors and how much recovery
    // the SDIO transport will do suggests we could retry a couple of times before giving up.
    for (u8 fails = 0; fails < 3; fails++) {
        if (S.sdioPort->API->Write(S.sdioPort, txBuffer, size)) {
            return true;
        }
    }

    return false;
}

static IpcTransportStatus ExecuteOperation(TransportOperation *op) {
    IpcTransportStatus status = IpcTransportStatus_Success;
    op->hdr.status = IpcTransportStatus_Success;

    // Build the TX buffer for the operation
    lt_memcpy(txBuffer, &op->hdr, sizeof(IpcTransportHeader));

    // Both payloadSource and size have to be non-zero to copy. This allows
    // commands to reuse the size field for other purposes (with limits of the
    // transport block size)
    if (op->payloadSource && op->hdr.size) {
        lt_memcpy(txBuffer + sizeof(IpcTransportHeader), op->payloadSource, op->hdr.size);
    }
    bool isReqCmd = op->hdr.cmd & REQ_CMD_FLAG;
    if (isReqCmd) op->hdr.status = IpcTransportStatus_Pending;

    if (TriggerWrite()) {
        if (isReqCmd) {
            status = IpcTransportStatus_Pending;
            S.sdioWriteThread->API->SetTimer(S.sdioWriteThread, kOperationPendingTimeout, OperationRspTimeoutProc, NULL, S.curReq);
        }
    } else {
        op->hdr.status = IpcTransportStatus_Failed;
        status = IpcTransportStatus_Failed;
    }
    return status;
}

static void InitiateTransportProc(void *clientData) {
    LT_UNUSED(clientData);

    // This proc is either triggered directly on boot, or from a timer. Stop the timer on each sync attempt
    S.sdioWriteThread->API->KillTimer(S.sdioWriteThread, InitiateTransportProc, NULL);

    if (S.ipcTransportState == kIpcTransport_InitiatorIdle) {
        // The transport is already initialised
        return;
    }

    // Send a sync command to the peer - this can directly use the txBuffer until sync is done
    IpcTransportHeader *hdr = (IpcTransportHeader *)txBuffer;
    hdr->bufferID = 0x12345678;
    hdr->size = 0;
    if (S.ipcTransportState == kIpcTransport_InitiatorPending) {
        hdr->cmd = IpcTransportCmd_Sync | REQ_CMD_FLAG;
    } else {
        hdr->cmd = IpcTransportCmd_Sync | RSP_CMD_FLAG;
    }
    hdr->status = 0;
    if (TriggerWrite()) {
        // Don't update initiator state until the response is received
        if (S.ipcTransportState == kIpcTransport_ResponderWaiting) {
            S.ipcTransportState = kIpcTransport_ResponderIdle;
            P("init", "Transport ready");
        } else if (S.ipcTransportState == kIpcTransport_InitiatorPending) {
            // Restart the sync timer in case the other side fails to respond
            S.sdioWriteThread->API->SetTimer(S.sdioWriteThread, kTransportSyncRetryTime, InitiateTransportProc, NULL, NULL);
        }
    }
}

static void ProcessOperationProc(void *clientData) {
    TransportOperation *op = clientData;

    if (sizeof(IpcTransportHeader) + op->hdr.size > kIpcTransportBlockSize) {
        LTLOG_YELLOWALERT("process.fail.size", "Command size exceeds block size: %lu", LT_Pu32(op->hdr.size));
        op->hdr.status = IpcTransportStatus_Failed;
        LTAtomic_FetchOr(&op->complete, TXP_OP_COMPLETION_COMPLETED);
        return;
    }

    // IND and RSP can be executed straight away, but REQs are queued such
    // that there is only and REQ pending a RSP at any time
    if (!(op->hdr.cmd & REQ_CMD_FLAG) || !S.curReq) {
        if (op->hdr.cmd & REQ_CMD_FLAG) S.curReq = op;
        bool isReqCmd = op->hdr.cmd & REQ_CMD_FLAG;
        IpcTransportStatus status = ExecuteOperation(op);

        if (status != IpcTransportStatus_Pending) {
            if (isReqCmd && status == IpcTransportStatus_Failed) S.curReq = NULL;
            LTAtomic_FetchOr(&op->complete, TXP_OP_COMPLETION_COMPLETED);
        }
    } else {
        LTList_AddTail(&S.pendingOps, &op->node);
    }
}

static void ProcessOperationReleaseProc(LTThread_ReleaseReason releaseReason, void *pClientData) {
    TransportOperation *op = (TransportOperation*)pClientData;

    if (releaseReason != kLTThread_ReleaseReason_TaskProcComplete) {
        // The operation was not processed, so we need to release the mutex here instead
        op->hdr.status = IpcTransportStatus_Failed;
        LTAtomic_FetchOr(&op->complete, TXP_OP_COMPLETION_COMPLETED);
    }

    u32 completion = LTAtomic_Load(&op->complete);
    if (completion & TXP_OP_COMPLETION_AUTOFREE) {
        lt_free(op);
    }
}

static void OperationRspProc(void *clientData) {
    LT_UNUSED(clientData);
    // Nothing to do here - it's all handled in the release proc below.
}

static void OperationRspReleaseProc(LTThread_ReleaseReason releaseReason, void *pClientData) {
    // A Response to our previous Request has been received. The Request is complete.
    S.sdioWriteThread->API->KillTimer(S.sdioWriteThread, OperationRspTimeoutProc, pClientData);
    LT_UNUSED(releaseReason);

    TransportOperation *op = (TransportOperation*)pClientData;
    if (S.curReq != op) return;
    S.curReq = NULL;

    // Check if there are requests queued
    while (!S.curReq && !LTList_IsEmpty(&S.pendingOps)) {
        LTList_Node *head = S.pendingOps.pNext;
        LTList_Remove(head);
        ProcessOperationProc(LT_CONTAINER_OF(head, TransportOperation, node));
    }

    LTAtomic_FetchOr(&op->complete, TXP_OP_COMPLETION_COMPLETED);
}

static void OperationRspTimeoutProc(void *pClientData) {
    LTLOG_YELLOWALERT("op.rsp.timeout", NULL);
    TransportOperation *op = (TransportOperation *)pClientData;
    if (op) {
        S.curReq->hdr.status = IpcTransportStatus_Failed;
        LTAtomic_FetchOr(&op->complete, TXP_OP_COMPLETION_COMPLETED);
    }
    OperationRspReleaseProc(kLTThread_ReleaseReason_TaskProcComplete, op);
}

static void SendIndRsp(IpcTransportHeader *hdr, u8 *payload) {
    if (sizeof(IpcTransportHeader) + hdr->size > kIpcTransportBlockSize) {
        LTLOG_YELLOWALERT("indrsp.fail.size", "Command size exceeds block size: %lu", LT_Pu32(hdr->size));
        return;
    }

    u32 allocSize = sizeof(TransportOperation);
    if (payload) allocSize += hdr->size;
    TransportOperation *op = lt_malloc(allocSize);
    if (!op) {
        LTLOG_YELLOWALERT("indrsp.fail.alloc", "Failed to allocate memory for operation");
        return;
    }
    lt_memset(op, 0, sizeof(TransportOperation));
    op->hdr = *hdr;
    if (payload && hdr->size) {
        u8 *payloadSource = ((u8 *)op) + sizeof(TransportOperation);
        op->payloadSource = payloadSource;
        lt_memcpy(payloadSource, payload, hdr->size);
    }
    // op is freed when ProcessOperationReleaseProc is called
    LTAtomic_FetchOr(&op->complete, TXP_OP_COMPLETION_AUTOFREE);
    S.sdioWriteThread->API->QueueTaskProc(S.sdioWriteThread, ProcessOperationProc, ProcessOperationReleaseProc, op);
}

static void RequestOperation(TransportOperation *op) {
    // This function is called in the context of the user's thread. It will
    // block until the operation is complete. We use a per-operation mutex
    // to block.
    //
    // The operation is passed through the write queue, which will action operations
    // in the order they are received. The mutex will unlock when the operation completes.
    // This is either in the read thread (if a response was required) or in the write thread
    // if the operation was a fire-and-forget indication.
    //
    LTAtomic_Store(&op->complete, TXP_OP_COMPLETION_PENDING);
    S.sdioWriteThread->API->QueueTaskProc(S.sdioWriteThread, ProcessOperationProc, ProcessOperationReleaseProc, op);

    // The mutex is locked when the write is prepared, so the following Lock will then
    // block until the write is complete. At most, the thread will be blocked for kOperationPendingTimeout seconds.
    while (!(LTAtomic_Load(&op->complete) & TXP_OP_COMPLETION_COMPLETED)) {
        S.sdioWriteThread->API->Yield();    // yields the current thread to allow the write thread to run
    }
}

static void cmdHandlerSync(IpcTransportHeader *hdr) {
    // Out-of-sequence sync is not considered a problem here
    hdr->cmd = IpcTransportCmd_Sync | RSP_CMD_FLAG;
    SendIndRsp(hdr, NULL);
}

static void cmdHandlerCreateBuffer(IpcTransportHeader *hdr) {
    // Peer is requesting to create a source buffer on this side
    BufferParams *params = (BufferParams *)(rxBuffer + sizeof(IpcTransportHeader));
    hdr->bufferID = CreateLocalBufferDescriptor(params->capacity);
    if (hdr->bufferID != LT_BUFFER_ID_INVALID) {
        hdr->status = IpcTransportStatus_Success;
    } else {
        hdr->status = IpcTransportStatus_OOM;
    }
    hdr->cmd = IpcTransportCmd_CreateBuffer | RSP_CMD_FLAG;
    SendIndRsp(hdr, NULL);
}

static void cmdHandlerDestroyBuffer(IpcTransportHeader *hdr) {
    BufferDescriptor bufDesc = { .bufferID = hdr->bufferID };
    if (DeleteDescriptor(&bufDesc)) {
        if (bufDesc.ipcBuf) {
            ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, bufDesc.ipcBuf->statusEvent);
            iEvent->NotifyEvent(bufDesc.ipcBuf->statusEvent, LTIPCBufferManagerStatus_BufferLoss, bufDesc.ipcBuf);
        }

        hdr->status = IpcTransportStatus_Success;
    } else {
        hdr->status = IpcTransportStatus_NoBuffer;
    }

    // This command can be an indication or a request, so only send a response if needed
    if (hdr->cmd & REQ_CMD_FLAG) {
        hdr->cmd = IpcTransportCmd_DestroyBuffer | RSP_CMD_FLAG;
        SendIndRsp(hdr, NULL);
    }
}

static void cmdHandlerGetBufferState(IpcTransportHeader *hdr) {
    // TBD - check and block requests for remote-pinned buffer IDs
    BufferParams params = {0};
    BufferDescriptor bufDesc = { .bufferID = hdr->bufferID };
    if (GetDescriptor(&bufDesc)) {
        hdr->status = IpcTransportStatus_Success;
        params.capacity = bufDesc.sourceCapacity;
    } else {
        hdr->status = IpcTransportStatus_NoBuffer;
    }
    hdr->cmd = IpcTransportCmd_GetBufferState | RSP_CMD_FLAG;
    hdr->size = sizeof(BufferParams);
    SendIndRsp(hdr, (u8 *)&params);
}

static void cmdHandlerPush(IpcTransportHeader *hdr) {
    BufferDescriptor bufDesc = { .bufferID = hdr->bufferID };
    bool wasEmpty = false;
    if (LockDescriptor(&bufDesc)) {
        if (bufDesc.isSource) {
            wasEmpty = (bufDesc.rawBuf->API->GetSize(bufDesc.rawBuf) == 0);
            LTBufferSpan span = { .data = rxBuffer + sizeof(IpcTransportHeader), .size = hdr->size };
            span = bufDesc.rawBuf->API->Write(bufDesc.rawBuf, span);
            hdr->size = span.size;
            hdr->status = IpcTransportStatus_Success;
        } else {
            hdr->size = 0;
            hdr->status = IpcTransportStatus_NotSource;
        }
        bufDesc.rawBufMutex->API->Unlock(bufDesc.rawBufMutex);
    } else {
        hdr->size = 0;
        hdr->status = IpcTransportStatus_NoBuffer;
    }
    hdr->cmd = IpcTransportCmd_Push | RSP_CMD_FLAG;
    SendIndRsp(hdr, NULL);

    if (wasEmpty) {
        //LTLOG("push.moredata", "Push caused data available for ID 0x%08lx", LT_Pu32(hdr->bufferID));
        if (bufDesc.ipcBuf) {
            ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, bufDesc.ipcBuf->statusEvent);
            iEvent->NotifyEvent(bufDesc.ipcBuf->statusEvent, LTIPCBufferManagerStatus_MoreData, bufDesc.ipcBuf);
        }

        // Notify the peer that the buffer has more data available, this is a fire-and-forget operation
        IpcTransportHeader moreDataInd = {
            .bufferID = hdr->bufferID,
            .cmd = IpcTransportCmd_MoreData | IND_CMD_FLAG,
        };
        SendIndRsp(&moreDataInd, NULL);
    }
}

static void cmdHandlerPull(IpcTransportHeader *hdr) {
    BufferDescriptor bufDesc = { .bufferID = hdr->bufferID };
    u8 *payload = NULL;
    bool crossedThreshold = false;
    if (LockDescriptor(&bufDesc)) {
        if (bufDesc.isSource) {
            u32 oldSize = bufDesc.rawBuf->API->GetSize(bufDesc.rawBuf);
            u32 threshold = bufDesc.sourceCapacity / 2;

            // Reuse the SDIO RX buffer to read out of the local raw buffer
            LTBufferSpan span = { .data = rxBuffer + sizeof(IpcTransportHeader), .size = hdr->size };
            span = bufDesc.rawBuf->API->Read(bufDesc.rawBuf, span);
            hdr->size = span.size;
            hdr->status = IpcTransportStatus_Success;
            payload = span.data;
            if (hdr->size == 0) {
                LTLOG("pull.zero", "Buffer was empty: 0x%08lx", LT_Pu32(hdr->bufferID));
            }

            if (oldSize > threshold && bufDesc.rawBuf->API->GetSize(bufDesc.rawBuf) <= threshold) {
                crossedThreshold = true;
            }
        } else {
            LTLOG("pullreq.cache", "Can't pull from cache: 0x%08lx", LT_Pu32(hdr->bufferID));
            hdr->size = 0;
            hdr->status = IpcTransportStatus_NotSource;
        }
        bufDesc.rawBufMutex->API->Unlock(bufDesc.rawBufMutex);
    } else {
        LTLOG("pullreq.fail", "Can't lock: 0x%08lx", LT_Pu32(hdr->bufferID));
        hdr->size = 0;
        hdr->status = IpcTransportStatus_NoBuffer;
    }
    hdr->cmd = IpcTransportCmd_Pull | RSP_CMD_FLAG;
    SendIndRsp(hdr, payload);

    if (crossedThreshold) {
        //LTLOG("pull.dataconsumed", "Pull caused data consumed for ID 0x%08lx", LT_Pu32(hdr->bufferID));
        if (bufDesc.ipcBuf) {
            ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, bufDesc.ipcBuf->statusEvent);
            iEvent->NotifyEvent(bufDesc.ipcBuf->statusEvent, LTIPCBufferManagerStatus_DataConsumed, bufDesc.ipcBuf);
        }

        // Notify the peer that buffer data has been consumed, this is a fire-and-forget operation
        IpcTransportHeader dataConsumedInd = {
            .bufferID = hdr->bufferID,
            .cmd = IpcTransportCmd_DataConsumed | IND_CMD_FLAG,
        };
        SendIndRsp(&dataConsumedInd, NULL);
    }
}

static void cmdHandlerMoreData(IpcTransportHeader *hdr) {
    // Don't notify if the buffer ID is unknown
    BufferDescriptor bufDesc = { .bufferID = hdr->bufferID };
    if (GetDescriptor(&bufDesc) && bufDesc.ipcBuf) {
        //LTLOG("moredata", "More data available for buffer ID 0x%08lx", LT_Pu32(hdr->bufferID));
        ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, bufDesc.ipcBuf->statusEvent);
        iEvent->NotifyEvent(bufDesc.ipcBuf->statusEvent, LTIPCBufferManagerStatus_MoreData, bufDesc.ipcBuf);
    }
}

static void cmdHandlerDataConsumed(IpcTransportHeader *hdr) {
    // Don't notify if the buffer ID is unknown
    BufferDescriptor bufDesc = { .bufferID = hdr->bufferID };
    if (GetDescriptor(&bufDesc) && bufDesc.ipcBuf) {
        //LTLOG("dataconsumed", "Data consumed for buffer ID 0x%08lx", LT_Pu32(hdr->bufferID));
        ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, bufDesc.ipcBuf->statusEvent);
        iEvent->NotifyEvent(bufDesc.ipcBuf->statusEvent, LTIPCBufferManagerStatus_DataConsumed, bufDesc.ipcBuf);
    }
}

static void cmdHandlerBufferLoss(IpcTransportHeader *hdr) {
    // Don't notify if the buffer ID is unknown
    BufferDescriptor bufDesc = { .bufferID = hdr->bufferID };
    if (GetDescriptor(&bufDesc) && bufDesc.ipcBuf) {
        LTLOG("bufferloss", "Buffer loss for ID 0x%08lx", LT_Pu32(hdr->bufferID));
        ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, bufDesc.ipcBuf->statusEvent);
        iEvent->NotifyEvent(bufDesc.ipcBuf->statusEvent, LTIPCBufferManagerStatus_BufferLoss, bufDesc.ipcBuf);
    }
}

static void OnSdioRead(LTDeviceSdioPort *port, u32 nBytes, void *clientData) {
    LT_UNUSED(clientData);

    if (nBytes > kIpcTransportBlockSize) {
        LTLOG_YELLOWALERT("sdio.read.fail.size", "Received more bytes than allowed by protocol: %lu", LT_Pu32(nBytes));
        return;
    }

    if (!port->API->Read(port, rxBuffer)) {
        LTLOG_YELLOWALERT("sdio.read.fail", "Failed to read RX data");
        return;
    }

    if (S.ipcTransportState == kIpcTransport_Uninitialised) {
        LTLOG_YELLOWALERT("sdio.unini", "Failed to read RX data");
        return;
    }

    IpcTransportHeader *hdr = (IpcTransportHeader *)rxBuffer;
    LTLOG_DEBUG("rx", "Received command: 0x%02x size:%lu", hdr->cmd, LT_Pu32(hdr->size));

    // Check for sync status first
    // TBD - this needs power state notification from IOT Wakeup so we can subscribe to power state event (also needs to handle reboot & watchdog)
    // Then when Anyka powers down the sync state can be returned to Pending and other operations are not allowed.
    if (S.ipcTransportState == kIpcTransport_InitiatorPending || S.ipcTransportState == kIpcTransport_ResponderWaiting) {
        if ((hdr->cmd == (IpcTransportCmd_Sync | REQ_CMD_FLAG)) && S.ipcTransportState == kIpcTransport_ResponderWaiting) {
            // Peer initiator has started up. Immediately queue the response but don't change our state until it has been sent
            S.sdioWriteThread->API->QueueTaskProcIfRequired(S.sdioWriteThread, InitiateTransportProc, NULL, NULL);
        } else if ((hdr->cmd == (IpcTransportCmd_Sync | RSP_CMD_FLAG)) && S.ipcTransportState == kIpcTransport_InitiatorPending) {
            // IPC transport is ready on both sides
            S.ipcTransportState = kIpcTransport_InitiatorIdle;
            P("init.sdio", "Transport ready");
        } else {
            LTLOG_YELLOWALERT("sync.fail", "Unexpected command during sync");
        }

        return;
    }

    // Then check for responses
    u8 rawCmd = hdr->cmd & CMD_MASK;
    if (hdr->cmd & RSP_CMD_FLAG) {
        if (!S.curReq) {
            LTLOG_YELLOWALERT("unexpected.rsp", "Received unexpected response: 0x%02x 0x%08lx status:%d", hdr->cmd, LT_Pu32(hdr->bufferID), hdr->status);
            return;
        }

        // Copy the header and, if required, the payload to the current operation
        S.curReq->hdr = *hdr;
        if (rawCmd == IpcTransportCmd_Pull && S.curReq->payloadSource && hdr->size > 0) {
            lt_memcpy(S.curReq->payloadSource, rxBuffer + sizeof(IpcTransportHeader), hdr->size);
        }
        S.sdioWriteThread->API->QueueTaskProcIfRequired(S.sdioWriteThread, OperationRspProc, OperationRspReleaseProc, S.curReq);
        return;
    }

    // By this point, anything else is a Request or Indication
    if (rawCmd < sizeof(s_cmdHandlers) / sizeof(IpcTransportCmdHandler) && s_cmdHandlers[rawCmd] != NULL) {
        // Call the command handler for this command
        s_cmdHandlers[rawCmd](hdr);
    } else {
        LTLOG_YELLOWALERT("unknown.cmd", "Unknown command 0x%02x size:%lu", hdr->cmd, LT_Pu32(hdr->size));
        if (hdr->cmd & REQ_CMD_FLAG) {
            // Send a response to the peer to try to unblock it
            hdr->cmd = rawCmd | RSP_CMD_FLAG;
            hdr->status = IpcTransportStatus_Failed;
            SendIndRsp(hdr, NULL);
        }
    }
}

static bool ReadThreadInit(void) {
    // Read thread owns the SDIO port
    if (!(S.sdioPort = lt_createobject(LTDeviceSdioPort))) {
        LTLOG("err.sdio.create", "Failed to create LTDeviceSdioPort");
        return false;
    }
    if (!S.sdioPort->API->Bind(S.sdioPort, kLTDeviceSdioPort_BufferManager, OnSdioRead, NULL)) {
        LTLOG("err.sdio.bind", "Failed to bind SDIO port");
        return false;
    }

    if (S.ipcTransportState == kIpcTransport_InitiatorPending) {
        S.sdioWriteThread->API->QueueTaskProcIfRequired(S.sdioWriteThread, InitiateTransportProc, NULL, NULL);
    }

    return true;
}

static void ReadThreadExit(void) {
    lt_destroyobject(S.sdioPort);
    S.sdioPort = NULL;
}


/*******************************************************************************
 * LTBufferIPC - Specialisation of LTBuffer for IPC
 *
 * This object is entirely local to this file. It is never created by an external
 * entity, but is returned as an LTBuffer instance when for buffer access.
 ******************************************************************************/


static void LTBufferIPC_Reset(LTBufferIPC *buf) {
    LT_UNUSED(buf);
    LTLOG_YELLOWALERT("ipc.reset", "UNIMPLEMENTED");
    // This API call is currently unimplemented as there were no requirements to support it over IPC.
    // However there is no technical limitation to prevent adding it later if required.
}

static void LTBufferIPC_SetMaxCapacity(LTBufferIPC *buf, u32 capacity) {
    LT_UNUSED(buf);
    LT_UNUSED(capacity);
    LTLOG_YELLOWALERT("ipc.setmaxcapacity", "UNIMPLEMENTED");
    // The current usage model assumes the max capacity is set when the (local or remote)
    // source buffer is first created. Therefore there was no requirement to adjust the
    // max capacity later. That said, as long as is there enough RAM on the source buffer
    // SoC to reallocate then it could be implemented if needed.
}

static u32 LTBufferIPC_GetMaxCapacity(LTBufferIPC *buf) {
    u32 maxCapacity = 0;

    if (buf->isSource) {
        BufferDescriptor bufDesc = { .bufferID = buf->bufferID };
        if (LockDescriptor(&bufDesc)) {
            maxCapacity = bufDesc.rawBuf->API->GetMaxCapacity(bufDesc.rawBuf);
            bufDesc.rawBufMutex->API->Unlock(bufDesc.rawBufMutex);
        }
    } else {
        LTLOG("ipc.getmaxcapacity", "UNIMPLEMENTED");
        // This API call is currently unimplemented as there were no requirements to support it over IPC.
        // However there is no technical limitation to prevent adding it later if required.
    }
    return maxCapacity;
}

static u32 LTBufferIPC_GetCapacity(LTBufferIPC *buf) {
    u32 capacity = 0;

    if (buf->isSource) {
        BufferDescriptor bufDesc = { .bufferID = buf->bufferID };
        if (LockDescriptor(&bufDesc)) {
            capacity = bufDesc.rawBuf->API->GetCapacity(bufDesc.rawBuf);
            bufDesc.rawBufMutex->API->Unlock(bufDesc.rawBufMutex);
        }
    } else {
        LTLOG("ipc.getcapacity", "UNIMPLEMENTED");
        // This API call is currently unimplemented as there were no requirements to support it over IPC.
        // However there is no technical limitation to prevent adding it later if required.
    }
    return capacity;
}

static u32 LTBufferIPC_GetSize(LTBufferIPC *buf) {
    u32 size = 0;

    if (buf->isSource) {
        BufferDescriptor bufDesc = { .bufferID = buf->bufferID };
        if (LockDescriptor(&bufDesc)) {
            size = bufDesc.rawBuf->API->GetSize(bufDesc.rawBuf);
            bufDesc.rawBufMutex->API->Unlock(bufDesc.rawBufMutex);
        }
    } else {
        LTLOG("ipc.getsize", "UNIMPLEMENTED");
        // This API call is currently unimplemented as there were no requirements to support it over IPC.
        // However there is no technical limitation to prevent adding it later if required.
        // Note that the total size would be the local cache size plus peer source buffer size.
    }
    return size;
}

static u32 LTBufferIPC_Reserve(LTBufferIPC *buf, u32 requested) {
    LT_UNUSED(buf);
    LT_UNUSED(requested);
    LTLOG("ipc.reserve", "UNIMPLEMENTED");
    // The LTBuffer Reserve() implementation currently always malloc a new underlying
    // buffer, copies any existing data to the start of it, and ensures the capacity is
    // (at least) the amount reserved. Care would need to be taken with the interplay
    // with the local cache, as that data no longer exists in the source buffer.
    return 0;
}

static LTBufferSpan LTBufferIPC_Write(LTBufferIPC *buf, LTBufferSpan src) {
    // Check if the local buffer is the source - is so it's a pass-through write
    if (buf->isSource) {
        BufferDescriptor bufDesc = { .bufferID = buf->bufferID };
        if (!LockDescriptor(&bufDesc)) {
            // Failed to lock therefore buffer was probably deleted recently
            LTLOG_YELLOWALERT("write.lockfailed", "Failed to lock buffer descriptor");
            return (LTBufferSpan){0};
        }

        // Write to the local buffer then unlock the buffer
        bool wasEmpty = (bufDesc.rawBuf->API->GetSize(bufDesc.rawBuf) == 0);
        LTBufferSpan written = bufDesc.rawBuf->API->Write(bufDesc.rawBuf, src);
        bufDesc.rawBufMutex->API->Unlock(bufDesc.rawBufMutex);

        if (wasEmpty) {
            if (bufDesc.ipcBuf) {
                // Notify any local listeners that the buffer has data available
                ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, bufDesc.ipcBuf->statusEvent);
                iEvent->NotifyEvent(bufDesc.ipcBuf->statusEvent, LTIPCBufferManagerStatus_MoreData, bufDesc.ipcBuf);
            }

            // Notify the peer that the buffer has more data available, this is a fire-and-forget operation
            IpcTransportHeader hdr = {
                .bufferID = buf->bufferID,
                .cmd = IpcTransportCmd_MoreData | IND_CMD_FLAG,
            };
            SendIndRsp(&hdr, NULL);
        }
        return written;
    }

    /* Writing to a buffer on the peer. The caller can request writing more data than fits
     * in a single transport block, so may need more than one TransportOperation.
     */
    TransportOperation *op = lt_malloc(sizeof(TransportOperation));
    if (!op) {
        LTLOG_YELLOWALERT("write.oom", "Failed to alloc TransportOperation");
        return (LTBufferSpan){0};
    }
    op->hdr.bufferID = buf->bufferID;
    op->payloadSource = src.data;
    u32 remaining = src.size;
    while (remaining) {
        op->hdr.size = LT_MIN(remaining, (u32)kIpcTransportBlockSize - sizeof(IpcTransportHeader));
        op->hdr.cmd = IpcTransportCmd_Push | REQ_CMD_FLAG;
        RequestOperation(op);

        // If zero bytes could be written, or any failure occurs, then the write is concluded
        if (!op->hdr.size || op->hdr.status != IpcTransportStatus_Success) {
            break;
        }
        remaining -= op->hdr.size;
        op->payloadSource += op->hdr.size;
    }
    lt_free(op);

    src.size -= remaining;
    return src;
}

static LTBufferAccess LTBufferIPC_StartWrite(LTBufferIPC *buf, u32 capacity) {
    LT_UNUSED(buf);
    LT_UNUSED(capacity);
    LTLOG("ipc.startwrite", "UNIMPLEMENTED");
    // StartWrite over IPC is complicated. The semantics are supposed to allow direct
    // write access to the source buffer without necessarily needing a memcpy or
    // temporary storage. However, if the source buffer is remote the only available
    // existing storage is the local cache buffer which is only intended for Read
    // operations. As a remote source ultimately still needs a write over IPC it
    // doesn't seem useful to implement it.
    return (LTBufferAccess){0};
}

static void LTBufferIPC_FinishWrite(LTBufferIPC *buf, LTBufferAccess access) {
    LT_UNUSED(buf);
    LT_UNUSED(access);
    LTLOG("ipc.finishwrite", "UNIMPLEMENTED");
    // See above rationale in StartWrite
}

static u32 LTBufferIPC_Fill(LTBufferIPC *buf, u32 size) {
    LT_UNUSED(buf);
    LT_UNUSED(size);
    LTLOG("ipc.fill", "UNIMPLEMENTED");
    // This API call is currently unimplemented as there were no requirements to support it over IPC.
    // However there is no technical limitation to prevent adding it later if required.
    return 0;
}

static LTBufferSpan LTBufferIPC_Read(LTBufferIPC *buf, LTBufferSpan dst) {
    // Lock the local buffer for the duration of the read
    BufferDescriptor bufDesc = { .bufferID = buf->bufferID };
    if (!LockDescriptor(&bufDesc)) {
        // Failed to lock therefore buffer was probably deleted recently
        LTLOG_YELLOWALERT("read.lockfailed", "Failed to lock buffer descriptor");
        return (LTBufferSpan){0};
    }

    // Check if the local buffer is the source - is so it's a pass-through read
    if (buf->isSource) {
        u32 oldSize = bufDesc.rawBuf->API->GetSize(bufDesc.rawBuf);
        u32 threshold = bufDesc.sourceCapacity / 2;

        LTBufferSpan actuallyRead = bufDesc.rawBuf->API->Read(bufDesc.rawBuf, dst);
        bufDesc.rawBufMutex->API->Unlock(bufDesc.rawBufMutex);

        if (oldSize > threshold && bufDesc.rawBuf->API->GetSize(bufDesc.rawBuf) <= threshold) {
            if (bufDesc.ipcBuf) {
                // Notify any local listeners that the buffer has data available
                ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, bufDesc.ipcBuf->statusEvent);
                iEvent->NotifyEvent(bufDesc.ipcBuf->statusEvent, LTIPCBufferManagerStatus_DataConsumed, bufDesc.ipcBuf);
            }

            // Notify the peer that buffer data has been consumed, this is a fire-and-forget operation
            IpcTransportHeader hdr = {
                .bufferID = buf->bufferID,
                .cmd = IpcTransportCmd_DataConsumed | IND_CMD_FLAG,
            };
            SendIndRsp(&hdr, NULL);
        }

        return actuallyRead;
    }

    /* Reading from a buffer on the peer. Check and drain the local cache buffer
     * first, then attempt to read more data from the peer. The local cache is
     * refilled as much as possible on each TransportOp, to best utilise the
     * SDIO bus, even if the read is getting close to the end.
     */
    LTBufferSpan span = bufDesc.rawBuf->API->Read(bufDesc.rawBuf, dst);
    if (span.size == dst.size) {
        // Read was fully satisfied locally
        bufDesc.rawBufMutex->API->Unlock(bufDesc.rawBufMutex);
        return dst;
    }

    // Read more data from the peer (if available)
    TransportOperation *op = lt_malloc(sizeof(TransportOperation));
    if (!op) {
        LTLOG_YELLOWALERT("read.oom", "Failed to alloc TransportOperation");
        return (LTBufferSpan){0};
    }
    op->hdr.bufferID = buf->bufferID;

    LTBufferSpan dstSubspan = { .data = dst.data + span.size, .size = dst.size - span.size };
    while (dstSubspan.size) {
        /* The read buffer is guaranteed to have been drained at this point, so reset
        * it to ensure that the maximum capacity can be refilled (i.e. ring pointers
        * go back to zero).
        */
        bufDesc.rawBuf->API->Reset(bufDesc.rawBuf);

        op->hdr.size = kIpcTransportBlockSize - sizeof(IpcTransportHeader);
        op->hdr.cmd = IpcTransportCmd_Pull | REQ_CMD_FLAG;

        LTBufferAccess access = bufDesc.rawBuf->API->StartWrite(bufDesc.rawBuf, 0);
        if (access.span.size < op->hdr.size) {
            bufDesc.rawBuf->API->FinishWrite(bufDesc.rawBuf, access);
            LTLOG_YELLOWALERT("span.mismatch", "Read buffer wasn't big enough: %lu", LT_Pu32(access.span.size));
            break;
        }
        op->payloadSource = access.span.data;
        RequestOperation(op);
        access.span.size = op->hdr.size;
        bufDesc.rawBuf->API->FinishWrite(bufDesc.rawBuf, access);

        // If zero bytes could be pulled, or any failure occurs, then the read is concluded
        if (!op->hdr.size || op->hdr.status != IpcTransportStatus_Success) {
            break;
        }

        // Copy additional data from the local read buffer to the caller's buffer
        span = bufDesc.rawBuf->API->Read(bufDesc.rawBuf, dstSubspan);
        dstSubspan.size -= span.size;
        dstSubspan.data += span.size;
    }
    lt_free(op);

    bufDesc.rawBufMutex->API->Unlock(bufDesc.rawBufMutex);
    dst.size -= dstSubspan.size;
    return dst;
}

static LTBufferAccess LTBufferIPC_StartRead(LTBufferIPC *buf, u32 capacity) {
    // Local the local buffer for the duration of the read - note that this will stay locked until FinishRead is called
    BufferDescriptor bufDesc = { .bufferID = buf->bufferID };
    if (!LockDescriptor(&bufDesc)) {
        // Failed to lock therefore buffer was probably deleted recently
        LTLOG("startread.nolock", "Couldn't lock buffer: 0x%08lx", LT_Pu32(buf->bufferID));
        return (LTBufferAccess){0};
    }

    buf->descriptorLocked = true;

    // Local buffer: just pass through the full read request - this may result in a temp buffer copy
    if (buf->isSource) {
        LTLOG("startread.local", "Reading local source");
        return bufDesc.rawBuf->API->StartRead(bufDesc.rawBuf, capacity);
    }

    // Remote buffer:
    // (1) If there is *any* data in the local cache then *only* return however much is contiguously available, to avoid copies
    if (bufDesc.rawBuf->API->GetSize(bufDesc.rawBuf) > 0) {
        LTBufferAccess access = bufDesc.rawBuf->API->StartRead(bufDesc.rawBuf, 0);
        if (access.span.size > capacity) {
            // Don't let them see more than they asked for
            access.span.size = capacity;
        }
        LTLOG_DEBUG("startread.cache", "Reading from cache only: %lu", LT_Pu32(access.span.size));
        return access;
    }

    // (2) Otherwise we know the cache is empty; it can now be reset and re-populated
    // from the peer, from whatever the peer has available in the source buffer
    bufDesc.rawBuf->API->Reset(bufDesc.rawBuf);
    TransportOperation *op = lt_malloc(sizeof(TransportOperation));
    if (!op) {
        LTLOG_YELLOWALERT("startread.oom", "Failed to alloc TransportOperation");
        return (LTBufferAccess){0};
    }
    op->hdr.bufferID = buf->bufferID;
    op->hdr.size = kIpcTransportBlockSize - sizeof(IpcTransportHeader);
    op->hdr.cmd = IpcTransportCmd_Pull | REQ_CMD_FLAG;

    LTBufferAccess access = bufDesc.rawBuf->API->StartWrite(bufDesc.rawBuf, 0);
    if (access.span.size < op->hdr.size) {
        bufDesc.rawBuf->API->FinishWrite(bufDesc.rawBuf, access);
        LTLOG_YELLOWALERT("span.mismatch", "Read buffer wasn't big enough: %lu", LT_Pu32(access.span.size));
        lt_free(op);
        bufDesc.rawBufMutex->API->Unlock(bufDesc.rawBufMutex);
        buf->descriptorLocked = false;
        return (LTBufferAccess){0};
    }
    op->payloadSource = access.span.data;
    RequestOperation(op);
    access.span.size = op->hdr.size;
    bufDesc.rawBuf->API->FinishWrite(bufDesc.rawBuf, access);

    // If zero bytes could be pulled, or any failure occurs, then the read is concluded
    if (!op->hdr.size || op->hdr.status != IpcTransportStatus_Success) {
        LTLOG("startread.peer", (!op->hdr.size) ? "No data available" : "Failed to pull data");
        lt_free(op);
        bufDesc.rawBufMutex->API->Unlock(bufDesc.rawBufMutex);
        buf->descriptorLocked = false;
        return (LTBufferAccess){0};
    }
    lt_free(op);

    // (3) As with step 1, now return however much is contigously available that meets the caller's need
    access = bufDesc.rawBuf->API->StartRead(bufDesc.rawBuf, 0);
    if (access.span.size > capacity) access.span.size = capacity;
    return access;

    // Buffer will remain locked until FinishRead is called
}

static void LTBufferIPC_FinishRead(LTBufferIPC *buf, LTBufferAccess access) {
    if (!buf->descriptorLocked) {
        // The buffer was never locked, so there can't be a valid read pending
        return;
    }

    BufferDescriptor bufDesc = { .bufferID = buf->bufferID };
    if (!GetDescriptor(&bufDesc)) {
        // The source buffer doesn't exist anymore. This might be concerning
        // but we can't do much about it now
        LTLOG_YELLOWALERT("finishread.nobuf", "No buffer descriptor: 0x%08lx", LT_Pu32(buf->bufferID));
        buf->descriptorLocked = false;
        return;
    }

    // Whether this is the source or the cache buffer, we can just finish the read
    // and unlock the buffer
    bufDesc.rawBuf->API->FinishRead(bufDesc.rawBuf, access);
    bufDesc.rawBufMutex->API->Unlock(bufDesc.rawBufMutex);
    buf->descriptorLocked = false;
}

static LTBufferSpan LTBufferIPC_Peek(LTBufferIPC *buf, LTBufferSpan dst, u32 offset) {
    LT_UNUSED(buf);
    LT_UNUSED(dst);
    LT_UNUSED(offset);
    LTLOG("ipc.peek", "UNIMPLEMENTED");
    // Peek is complicated due to the current use of a cache buffer for reading from a
    // remote source buffer. The cache may have more or less than the Peek is trying to
    // access. The Peek may also try to access more than can fit in the cache buffer,
    // although this is broadly similar to Read limitations.
    //
    // If the cache is empty then it could be temporarily filled via a remote peek, but
    // complexity then comes from whether the peeked data can be cached longer term or
    // needs to be immediately discarded.
    return (LTBufferSpan){0};
}

static u32 LTBufferIPC_Skip(LTBufferIPC *buf, u32 size) {
    LT_UNUSED(buf);
    LT_UNUSED(size);
    LTLOG("ipc.skip", "UNIMPLEMENTED");
    // This API call is currently unimplemented as there were no requirements to support it over IPC.
    // However there is no technical limitation to prevent adding it later if required.
    // Skipping a remote-source buffer requires any cached data to be skipped first, and then if
    // any skip size is left also send a skip request to the remote side.
    return 0;
}

static LTBuffer *LTBufferIPC_Extract(LTBufferIPC *buf, u32 size, bool replace) {
    LT_UNUSED(buf);
    LT_UNUSED(size);
    LT_UNUSED(replace);
    LTLOG("ipc.extract", "UNIMPLEMENTED");
    // The primary usage for IPC-aware buffers is to pre-allocate a known buffer size and
    // use it without adjusting the max capacity. There was therefore no obvious benefit
    // to supporting the Chain() or Extract() operations.
    return NULL;
}

static LTBuffer *LTBufferIPC_Chain(LTBufferIPC *before, LTBuffer *after) {
    LT_UNUSED(before);
    LT_UNUSED(after);
    LTLOG("ipc.chain", "UNIMPLEMENTED");
    // The primary usage for IPC-aware buffers is to pre-allocate a known buffer size and
    // use it without adjusting the max capacity. There was therefore no obvious benefit
    // to supporting the Chain() or Extract() operations.
    return NULL;
}

static LTBuffer *LTBufferIPC_Freeze(LTBufferIPC *buf) {
    LT_UNUSED(buf);
    LTLOG("ipc.freeze", "UNIMPLEMENTED");
    // Freeze and Unfreeze operations had no obvious mapping to buffer usage where the data
    // is expected to stream through the buffer. Freezing a remote buffer may impact the
    // peer trying to access it concurrently, and not being aware it is frozen.
    return NULL;
}

static LTBuffer *LTBufferIPC_Unfreeze(LTBufferIPC *buf) {
    LT_UNUSED(buf);
    LTLOG("ipc.unfreeze", "UNIMPLEMENTED");
    // Freeze and Unfreeze operations had no obvious mapping to buffer usage where the data
    // is expected to stream through the buffer. Freezing a remote buffer may impact the
    // peer trying to access it concurrently, and not being aware it is frozen.
    return NULL;
}

define_LTObjectImplPublic(LTBuffer, LTBufferIPC,
    Reset,
    SetMaxCapacity,
    GetMaxCapacity,
    GetCapacity,
    GetSize,
    Reserve,
    Write,
    StartWrite,
    FinishWrite,
    Fill,
    Read,
    StartRead,
    FinishRead,
    Peek,
    Skip,
    Extract,
    Chain,
    Freeze,
    Unfreeze);


/*******************************************************************************
 * LTIPCBufferManager Object Functions
 ******************************************************************************/

typedef_LTObjectImpl(LTIPCBufferManager, LTIPCBufferManagerImpl) {
    // Effectively a singleton object with no per-instance state
} LTOBJECT_API;

static LTBufferID LTIPCBufferManagerImpl_CreateRemoteSourceBuffer(u32 capacity) {
    LTBufferID newBufferID = LT_BUFFER_ID_INVALID;
    TransportOperation *op = lt_malloc(sizeof(TransportOperation));
    BufferParams *params = lt_malloc(sizeof(BufferParams));
    if (!op || !params) {
        LTLOG("create.remote.oom", "Failed to allocate memory for remote buffer");
        lt_free(op);
        return newBufferID;
    }

    lt_memset(op, 0, sizeof(TransportOperation));
    op->hdr.cmd = IpcTransportCmd_CreateBuffer | REQ_CMD_FLAG;
    op->hdr.size = sizeof(BufferParams);
    op->payloadSource = (u8 *)params;
    params->capacity = capacity;
    LTLOG("create.remote", "Creating remote buffer: %lu", LT_Pu32(capacity));
    RequestOperation(op);

    if (op->hdr.status == IpcTransportStatus_Success) {
        newBufferID = op->hdr.bufferID;

        // Create an entry in the knownBuffers array as well.
        BufferDescriptor bufDesc = {
            .bufferID = newBufferID,
            .sourceCapacity = capacity,
            .ipcBuf = NULL,
            .rawBuf = NULL,  // allocated by CreateRemoteBufferRecord
            .isSource = false,
        };
        if (!CreateRemoteBufferRecord(&bufDesc)) {
            LTLOG_YELLOWALERT("create.remote.fail", "Failed to create remote buffer record");
            newBufferID = LT_BUFFER_ID_INVALID;
            // Also destroy the remote buffer as well
            op->hdr.cmd = IpcTransportCmd_DestroyBuffer;    // no RSP needed this time
            RequestOperation(op);
        }
    }

    lt_free(params);
    lt_free(op);
    return newBufferID;
}

static LTBufferID LTIPCBufferManagerImpl_CreateLocalSourceBuffer(u32 capacity) {
    LTBufferID newBufferID = CreateLocalBufferDescriptor(capacity);
    return newBufferID;
}

static bool LTIPCBufferManagerImpl_DestroySourceBuffer(LTBufferID bufferID) {
    bool success = false;
    bool destroyRemote = true;

    if (bufferID == LT_BUFFER_ID_INVALID) return true;

    // Check first if the buffer is known on this side
    BufferDescriptor bufDesc = { .bufferID = bufferID };
    if (DeleteDescriptor(&bufDesc)) {
        if (bufDesc.isSource) {
            destroyRemote = false;
            success = true;

            // Notify the peer of buffer loss, this is a fire-and-forget operation
            IpcTransportHeader hdr = {
                .bufferID = bufferID,
                .size = 0,
                .cmd = IpcTransportCmd_BufferLoss | IND_CMD_FLAG,
            };
            SendIndRsp(&hdr, NULL);
        }

        if (bufDesc.ipcBuf) {
            ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, bufDesc.ipcBuf->statusEvent);
            iEvent->NotifyEvent(bufDesc.ipcBuf->statusEvent, LTIPCBufferManagerStatus_BufferLoss, bufDesc.ipcBuf);
        }
    }

    if(destroyRemote) {
        // Buffer isn't local source or isn't known at all. Request peer to destroy the buffer, if it has it
        TransportOperation *op = lt_malloc(sizeof(TransportOperation));
        if (!op) {
            LTLOG_YELLOWALERT("destroy.oom", "Failed to alloc TransportOperation");
            return false;
        }
        lt_memset(op, 0, sizeof(TransportOperation));
        op->hdr.bufferID = bufferID;
        op->hdr.cmd = IpcTransportCmd_DestroyBuffer | REQ_CMD_FLAG;
        RequestOperation(op);

        if (op->hdr.status == IpcTransportStatus_Success) {
            success = true;
        }
        lt_free(op);
    }

    return success;
}

static LTBufferIPC *LTIPCBufferManagerImpl_AccessBuffer(LTBufferID bufferID) {
    if (bufferID == LT_BUFFER_ID_INVALID) {
        LTLOG("access.invalid", "Invalid buffer ID");
        return NULL;
    }

    LTBufferIPC *ipcBuf = (LTBufferIPC *)lt_createobject_typed(LTBuffer, LTBufferIPC);
    if (!ipcBuf) {
        LTLOG("access.oom", "Failed to allocate LTBufferIPC object");
        return NULL;
    }
    ipcBuf->bufferID = bufferID;

    // Attempt to reserve the buffer ID, if already known to us. This can:
    //  * succeed
    //  * fail because the buffer is already reserved
    //  * fail because the buffer is not known
    //
    // If not known then the next step is to query the peer to see if they own it instead.
    //
    BufferDescriptor bufDesc = { .bufferID = bufferID, .ipcBuf = ipcBuf };
    IpcTransportStatus status = ReserveBufferID(&bufDesc);
    if (status == IpcTransportStatus_Failed) {
        LTLOG("access.rsvd", "Buffer already reserved: 0x%08lx", LT_Pu32(bufferID));
        lt_destroyobject(ipcBuf);
        return NULL;
    }

    if (status == IpcTransportStatus_Success) {
        ipcBuf->isSource = bufDesc.isSource;
        LTLOG("access.known", "Buffer is known: 0x%08lx", LT_Pu32(bufferID));
    } else {
        // Buffer ID not already known, so query the peer
        TransportOperation *op = lt_malloc(sizeof(TransportOperation));
        if (!op) {
            LTLOG_YELLOWALERT("access.txop.oom", "Failed to alloc TransportOperation");
            lt_destroyobject(ipcBuf);
            return NULL;
        }
        lt_memset(op, 0, sizeof(TransportOperation));
        op->hdr.bufferID = bufferID;
        op->hdr.cmd = IpcTransportCmd_GetBufferState | REQ_CMD_FLAG;
        RequestOperation(op);

        if (op->hdr.status == IpcTransportStatus_Success) {
            bufDesc.bufferID = op->hdr.bufferID;
            bufDesc.sourceCapacity = op->hdr.size;
            bufDesc.ipcBuf = ipcBuf;
            bufDesc.rawBuf = NULL;  // allocated by CreateRemoteBufferRecord
            bufDesc.isSource = false;

            if (CreateRemoteBufferRecord(&bufDesc)) {
                ipcBuf->isSource = bufDesc.isSource;
                LTLOG_DEBUG("access.peer", "Buffer from peer: 0x%08lx", LT_Pu32(bufferID));
            } else {
                lt_destroyobject(ipcBuf);
                ipcBuf = NULL;
            }
        } else {
            lt_destroyobject(ipcBuf);
            ipcBuf = NULL;
        }
        lt_free(op);
    }

    if (!ipcBuf) {
        LTLOG("access.fail", "Failed to get buffer: 0x%08lx", LT_Pu32(bufferID));
    }
    return ipcBuf;
}

static void StatusChangeDispatchProc(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTIPCBufferManagerStatus bufStatus = LTArgs_u32At(0, pEventArgs);
    LTBufferIPC *ipcBuf = LTArgs_pointerAt(1, pEventArgs);
    (*(LTIPCBufferManager_StatusProc)pEventProc)(ipcBuf->bufferID, (LTBuffer *)ipcBuf, bufStatus, pEventProcClientData);
}

static void LTIPCBufferManagerImpl_OnStatusChange(LTBuffer *buffer, LTIPCBufferManager_StatusProc callback, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *clientData) {
    // Ensure the buffer object is an IPC-aware buffer
    if (lt_strcmp(buffer->API->GetObjectImplName(), "LTBufferIPC") != 0) {
        LTLOG_YELLOWALERT("status.change", "Buffer is not IPC-aware: %s", buffer->API->GetObjectImplName());
        return;
    }

    LTBufferIPC *ipcBuf = (LTBufferIPC *)buffer;
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, ipcBuf->statusEvent);
    iEvent->RegisterForEvent(ipcBuf->statusEvent, callback, pClientDataReleaseProc, clientData, false);
}

static void LTIPCBufferManagerImpl_NoStatusChange(LTBuffer *buffer, LTIPCBufferManager_StatusProc callback) {
    // Ensure the buffer object is an IPC-aware buffer (don't alert this, it's benign)
    if (lt_strcmp(buffer->API->GetObjectImplName(), "LTBufferIPC") != 0) return;

    LTBufferIPC *ipcBuf = (LTBufferIPC *)buffer;
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, ipcBuf->statusEvent);
    iEvent->UnregisterFromEvent(ipcBuf->statusEvent, callback);
}


/*******************************************************************************
 * Library & Object Standard Functions
 ******************************************************************************/

static void LTIPCBufferManager_LibFini(void) {
    // Destroy write thread before read thread, so it can't use the port owned by reader
    lt_destroyobject(S.sdioWriteThread);
    lt_destroyobject(S.sdioReadThread);

    // Users are expected to cleanup their buffers before destroying the last
    // instance of the buffer manager. It is a fatal error if they don't.
    if (S.knownBuffersMutex && S.knownBuffers) {
        S.knownBuffersMutex->API->Lock(S.knownBuffersMutex);
        u32 howMany = S.knownBuffers->API->GetCount(S.knownBuffers);
        BufferDescriptor *array = S.knownBuffers->API->GetStorage(S.knownBuffers);
        for (u32 i = 0; i < howMany; i++) {
            BufferDescriptor *desc = &array[i];
            if (desc->ipcBuf) {
                LTLOG_REDALERT("fini.leak.ipc", "Dangling IPC buffer: 0x%08lx", LT_Pu32(desc->bufferID));
            }
            if (desc->isSource) {
                LTLOG_REDALERT("fini.leak.src", "Dangling source buffer: 0x%08lx", LT_Pu32(desc->bufferID));
            }
        }
        S.knownBuffersMutex->API->Unlock(S.knownBuffersMutex);
    }
    lt_destroyobject(S.knownBuffers);
    lt_destroyobject(S.knownBuffersMutex);

    S = (struct Statics) {};
}

static bool LTIPCBufferManager_LibInit(void) {
    S = (struct Statics) {
        .core = LT_GetCore(),
    };

    const char *reason;
    do {
        LTList_Init(&S.pendingOps);

        // Determine if this side of the IPC link should try to initiate the protocol
        reason = "no config";
        LTProductConfig *productConfig = lt_openlibrary(LTProductConfig);
        if (!productConfig) break;
        u32 libSection = productConfig->GetLibraryConfigSection("LTIPCBufferManager");
        s64 initiator = libSection ? productConfig->ReadInteger(libSection, "initiator") : 0;
        if (initiator) {
            S.ipcTransportState = kIpcTransport_InitiatorPending;
            P("init", "IPC transport initiator");
        } else {
            S.ipcTransportState = kIpcTransport_ResponderWaiting;
            P("init", "IPC transport responder");
        }
        lt_closelibrary(productConfig);

        reason = "knownBuffers array failed";
        if (!(S.knownBuffersMutex = lt_createobject(LTMutex))) break;
        if (!(S.knownBuffers = LTArray_CreateStructArray(sizeof(BufferDescriptor)))) break;

        reason = "read thread failed";
        if (!(S.sdioReadThread = lt_createobject(LTOThread))) break;
        S.sdioReadThread->API->SetStackSize(S.sdioReadThread, 1024);
        S.sdioReadThread->API->Start(S.sdioReadThread, "bufMgrReader", ReadThreadInit, ReadThreadExit);

        reason = "write thread failed";
        if (!(S.sdioWriteThread = lt_createobject(LTOThread))) break;
        S.sdioWriteThread->API->SetStackSize(S.sdioWriteThread, 1024);
        S.sdioWriteThread->API->Start(S.sdioWriteThread, "bufMgrWriter", NULL, NULL);

        return true;
    } while (false);

    LTLOG_YELLOWALERT("init.fail", "init failed: %s", reason);
    LTIPCBufferManager_LibFini();
    return false;
}

static bool LTBufferIPC_ConstructObject(LTBufferIPC *ipcBuf) {
    ipcBuf->statusEvent = S.core->CreateEvent(&s_BufferEventArgs, StatusChangeDispatchProc, NULL, NULL, NULL);
    if (!ipcBuf->statusEvent) {
        LTLOG_YELLOWALERT("con.no.evt", "Failed to create status event");
        return false;
    }
    LTLOG_DEBUG("con", "Constructed LTBufferIPC");
    return true;
}

static void LTBufferIPC_DestructObject(LTBufferIPC *ipcBuf) {
    if (ipcBuf->descriptorLocked) {
        BufferDescriptor bufDesc = { .bufferID = ipcBuf->bufferID };
        if (GetDescriptor(&bufDesc)) {
            if (bufDesc.rawBufMutex) bufDesc.rawBufMutex->API->Unlock(bufDesc.rawBufMutex);
        };
    }
    lt_destroyhandle(ipcBuf->statusEvent);
    ipcBuf->statusEvent = LTHANDLE_INVALID;
    LTLOG_DEBUG("dest", "Destructed LTBufferIPC: 0x%08lx", LT_Pu32(ipcBuf->bufferID));
    ReleaseBufferID(ipcBuf);
}

static bool LTIPCBufferManagerImpl_ConstructObject(LTIPCBufferManagerImpl *bufMgr) {
    LT_UNUSED(bufMgr);
    return true;
}

static void LTIPCBufferManagerImpl_DestructObject(LTIPCBufferManagerImpl *bufMgr) {
    LT_UNUSED(bufMgr);
}


/*******************************************************************************
 * Library & Object Interfaces
 ******************************************************************************/

define_LTObjectImplPublic(LTIPCBufferManager, LTIPCBufferManagerImpl,
    CreateRemoteSourceBuffer,
    CreateLocalSourceBuffer,
    DestroySourceBuffer,
    AccessBuffer,
    OnStatusChange,
    NoStatusChange,
);

define_LTObjectLibrary(1, LTIPCBufferManager_LibInit, LTIPCBufferManager_LibFini)
