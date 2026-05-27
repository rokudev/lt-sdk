/*******************************************************************************
 * LTDriverOtaProxyClient.c: OTA driver proxy client
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/otabundle/LTDeviceOtaBundle.h>
#include <lt/device/sdio/LTDeviceSdioPort.h>
#include <lt/utility/messagepack/LTUtilityMessagePack.h>
#include <lt/core/LTMonitor.h>
#include "LTDriverOtaProxy.h"

DEFINE_LTLOG_SECTION("ota.prx.clt");

#define OTA_STORAGE_PROXY_RX_THREAD_STACKSIZE 1024

#define P(...)

/** Standard LT Interfaces ****************************************************/

typedef_LTObjectImpl(LTDriverOtaStorage, LTDriverOtaStorageProxy) {
    LTDeviceSdioPort        *sdioPort;
    LTOThread               *rxThread;
    LTMonitor               *monitor;
    union {
        // For GetVersion
        struct {
            char            *versionResponse;    // Freed in GetVersion or DispatchPostGetVersionProc.
            bool             versionValidated;   // True if remote OTA is already validated.
            bool             bGetVersionSuccess; // True if get remote version over IPC.
        };
        // For InitDownload
        LTDeviceOta_Status   initResult;
        // For SaveBlock
        bool                 bSaveBlockResult;
        // For Finalize
        LTDeviceOta_Status   finalizeResult;
    };
} LTOBJECT_API;

static LTUtilityMessagePack *s_mpack = NULL;

// in swup.worker
static void SendMessage(LTDriverOtaStorageProxy *otas, LTBuffer *message) {
    u32 messageSize = message->API->GetSize(message);
    u32 finalBlockSize = messageSize % kLTDeviceSdioPort_BlockSize;
    if (finalBlockSize) message->API->Fill(message, kLTDeviceSdioPort_BlockSize - finalBlockSize);
    LTBufferAccess access = message->API->StartRead(message, 0);
    if (!otas->sdioPort->API->Write(otas->sdioPort, access.span.data, access.span.size)) {
        LTLOG_YELLOWALERT("sdio.wr.err", NULL);
    }
    message->API->FinishRead(message, access);
    lt_destroyobject(message);
}

// Prefer to use semaphore if available in future.
static void SendAndWaitResponse(LTDriverOtaStorageProxy *otas, LTBuffer *message, LTTime timeout, const char* timeoutMessage) {
    otas->monitor->API->Enter(otas->monitor);
    // message will be destroyed in SendMessage.
    SendMessage(otas, message);
    if (!otas->monitor->API->Wait(otas->monitor, timeout)) {
        LTLOG_YELLOWALERT(timeoutMessage, NULL);
    }
    otas->monitor->API->Exit(otas->monitor);
}

static void NotifyResponse(LTDriverOtaStorageProxy *otas) {
    otas->monitor->API->Enter(otas->monitor);
    otas->monitor->API->Notify(otas->monitor);
    otas->monitor->API->Exit(otas->monitor);
}

// in swup.worker
static void DispatchPostGetVersionProc(void *clientData) {
    LTDriverOtaStorage_VersionData *versionData = clientData;
    versionData->getVersionCallback(versionData->otas, versionData->version, versionData->validated);
    lt_free(versionData->version);
    lt_free(versionData);
}

// in swup.worker
static bool LTDriverOtaStorageProxy_GetVersion(LTDriverOtaStorageProxy *otas, const char *assetName, LTDriverOtaStorage_GetVersionCallback *getVersionCallback) {
    LTBuffer *message = lt_createobject(LTBuffer);
    s_mpack->WriteIntU32(message, kLTOtaProxy_RequestType_GetVersion);
    s_mpack->WriteCString(message, assetName, false);

    otas->versionResponse = NULL;
    otas->bGetVersionSuccess = false;  // Will set to true on success in Handle_GetVersionResponse
    // If timeout, will retry a few times until swup gives up.
    SendAndWaitResponse(otas, message, LTTime_Seconds(2), "ver.timeout");

    if (otas->bGetVersionSuccess) {
        LTDriverOtaStorage_VersionData *versionData = lt_malloc(sizeof(LTDriverOtaStorage_VersionData));
        if (!versionData) {
            LTLOG_YELLOWALERT("ver.cb.oom", "%s", assetName);
            lt_free(otas->versionResponse);
            return false;
        }

        *versionData = (LTDriverOtaStorage_VersionData) {
            .otas       = (LTDriverOtaStorage *)otas,
            .version    = otas->versionResponse,
            .validated  = otas->versionValidated,
            .getVersionCallback = getVersionCallback,
        };
        LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
        thread->API->QueueTaskProc(thread, DispatchPostGetVersionProc, NULL, versionData);

    } else {
        lt_free(otas->versionResponse);
    }

    return otas->bGetVersionSuccess;
}

// in swup.worker
static void LTDriverOtaStorageProxy_InitDownload(LTDriverOtaStorageProxy *otas, const char *assetName, const char *newVersion, u32 imageSize, LTDriverOtaStorage_StatusCallback *initCallback) {
    LTBuffer *message = lt_createobject(LTBuffer);
    s_mpack->WriteIntU32(message, kLTOtaProxy_RequestType_InitDownload);
    s_mpack->WriteCString(message, assetName, false);
    s_mpack->WriteCString(message, newVersion, false);
    s_mpack->WriteIntU32(message, imageSize);

    otas->initResult = kLTDeviceOta_Status_Error;
    // Need 4.2 seconds in test
    // If timeout, swup will fail. But, retry will happen by next scheduled swup.
    SendAndWaitResponse(otas, message, LTTime_Seconds(12), "init.timeout");
    initCallback((LTDriverOtaStorage *)otas, otas->initResult);
}

// in swup.worker
static bool LTDriverOtaStorageProxy_SaveBlock(LTDriverOtaStorageProxy *otas, const u8 *data, u32 dataLen, u32 offsetToSave)  {
    LTBuffer *message = lt_createobject(LTBuffer);
    s_mpack->WriteIntU32(message, kLTOtaProxy_RequestType_SaveBlock);
    s_mpack->WriteIntU32(message, offsetToSave);
    s_mpack->WriteBinary(message, data, dataLen);

    otas->bSaveBlockResult = false;  // Will set to true on success in Handle_SaveBlockResponse.
    // If timeout, will retry a few times until swup gives up.
    SendAndWaitResponse(otas, message, LTTime_Seconds(2), "save.timeout");
    P("save", "%d offset %lu len %lu %02x%02x...%02x%02x", otas->bSaveBlockResult, LT_Pu32(offsetToSave), LT_Pu32(dataLen), data[0],data[1],data[dataLen-2],data[dataLen-1]);
    return otas->bSaveBlockResult;
}

// in swup.worker
static void LTDriverOtaStorageProxy_Finalize(LTDriverOtaStorageProxy *otas, u32 imageSize, const u8 imageHash[SHA256_HASH_LENGTH], LTDriverOtaStorage_StatusCallback *finalizeCallback) {
    LTBuffer *message = lt_createobject(LTBuffer);
    s_mpack->WriteIntU32(message, kLTOtaProxy_RequestType_Finalize);
    s_mpack->WriteIntU32(message, imageSize);
    s_mpack->WriteBinary(message, imageHash, SHA256_HASH_LENGTH);

    otas->finalizeResult = kLTDeviceOta_Status_Error;
    // Need 0.5 seconds in test
    // If timeout, swup will fail. But, retry will happen by next scheduled swup.
    SendAndWaitResponse(otas, message, LTTime_Seconds(5), "finalize.timeout");
    finalizeCallback((LTDriverOtaStorage *)otas, otas->finalizeResult);
}

// in rxThread
static void Handle_GetVersionResponse(LTDriverOtaStorageProxy *otas, LTBuffer *packet) {
    otas->versionResponse = (char *)s_mpack->CopyString(packet);
    s_mpack->ReadBoolean(packet, &otas->versionValidated);
    otas->bGetVersionSuccess = true;
    NotifyResponse(otas);
}

// in rxThread
static void Handle_InitDownloadResponse(LTDriverOtaStorageProxy *otas, LTBuffer *packet) {
    u32 initResult;
    s_mpack->ReadIntU32(packet, &initResult);
    otas->initResult = initResult;
    NotifyResponse(otas);
}

// in rxThread
static void Handle_SaveBlockResponse(LTDriverOtaStorageProxy *otas, LTBuffer *packet) {
    s_mpack->ReadBoolean(packet, &otas->bSaveBlockResult);
    NotifyResponse(otas);
}

// in rxThread
static void Handle_FinalizeResponse(LTDriverOtaStorageProxy *otas, LTBuffer *packet) {
    u32 finalizeResult;
    s_mpack->ReadIntU32(packet, &finalizeResult);
    otas->finalizeResult = finalizeResult;
    NotifyResponse(otas);
}

// in rxThread
static void SdioReadCallback(LTDeviceSdioPort *port, u32 requestedLen, void *clientData) {
    LTDriverOtaStorageProxy *otas = clientData;
    if (port != otas->sdioPort) LTLOG_YELLOWALERT("sdio.unmatch", "port %p vs otad->sdioPort %p", port, otas->sdioPort);

    LTBuffer *packet = lt_createobject(LTBuffer);
    packet->API->Reserve(packet, requestedLen);
    LTBufferAccess write = packet->API->StartWrite(packet, requestedLen);

    if (!otas->sdioPort->API->Read(otas->sdioPort, write.span.data)) {
        LTLOG_YELLOWALERT("rdcb.rd.err", "SDIO read failed");
        goto done;
    }
    packet->API->FinishWrite(packet, write);

    u32 packetType;
    if (!s_mpack->ReadIntU32(packet, &packetType)) {
        LTLOG_YELLOWALERT("rdcb.pt.err", "Failed to read packet type from message");
        goto done;
    }

    switch (packetType) {
        case kLTOtaProxy_ResponseType_GetVersion:
            Handle_GetVersionResponse(otas, packet);
            break;

        case kLTOtaProxy_ResponseType_InitDownload:
            Handle_InitDownloadResponse(otas, packet);
            break;

        case kLTOtaProxy_ResponseType_SaveBlock:
            Handle_SaveBlockResponse(otas, packet);
            break;

        case kLTOtaProxy_ResponseType_Finalize:
            Handle_FinalizeResponse(otas, packet);
            break;

        default:
            LTLOG_YELLOWALERT("pk.unhandled", NULL);
            break;
    }

done:
    lt_destroyobject(packet);
}

static bool RxThread_Init(void) {
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    LTDriverOtaStorageProxy *otas = thread->API->GetThreadSpecificClientData(thread, "otas");
    bool ret = otas->sdioPort->API->Bind(otas->sdioPort, kLTDeviceSdioPort_Ota, SdioReadCallback, otas);
    P("rx.init", "%d", ret);
    return ret;
}

static void RxThread_Exit(void) {
    P("rx.exit", NULL);
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    LTDriverOtaStorageProxy *otas = thread->API->GetThreadSpecificClientData(thread, "otas");
    if (otas && otas->sdioPort) otas->sdioPort->API->Unbind(otas->sdioPort);
}

static void LTDriverOtaStorageProxy_DestructObject(LTDriverOtaStorageProxy *otas) {
    LTLOG("destruct", NULL);
    if (otas->rxThread) {
        otas->rxThread->API->Terminate(otas->rxThread);
        otas->rxThread->API->WaitUntilFinished(otas->rxThread, LTTime_Infinite());
    }
    lt_destroyobject(otas->sdioPort);
    otas->sdioPort = NULL;
    lt_destroyobject(otas->rxThread);
    otas->rxThread = NULL;
    lt_destroyobject(otas->monitor);
    otas->monitor = NULL;
}

static bool LTDriverOtaStorageProxy_ConstructObject(LTDriverOtaStorageProxy *otas) {
    LTLOG("construct", NULL);
    const char *reason = NULL;
    do {
        if (!(otas->monitor = lt_createobject(LTMonitor))) { reason = "monitor.err"; break; }
        if (!(otas->sdioPort = lt_createobject(LTDeviceSdioPort))) { reason = "port.err"; break; }
        if (!(otas->rxThread = lt_createobject(LTOThread))) { reason = "rxthread.err"; break; }
        otas->rxThread->API->SetStackSize(otas->rxThread, OTA_STORAGE_PROXY_RX_THREAD_STACKSIZE);
        otas->rxThread->API->SetThreadSpecificClientData(otas->rxThread, "otas", NULL, otas);
        if (!otas->rxThread->API->StartSynchronous(otas->rxThread, "ota.prx.clt.rx", RxThread_Init, RxThread_Exit)) { reason = "rxThread.start.fail"; break; }
        return true;
    } while(false);

    LTLOG_YELLOWALERT("construct.fail", reason);
    LTDriverOtaStorageProxy_DestructObject(otas);
    return false;
}

static bool LibInit(void) {
    do {
        if (!(s_mpack = lt_openlibrary(LTUtilityMessagePack))) break;
        return true;
    } while(false);

    return false;
}

static void LibFini(void) {
    lt_closelibrary(s_mpack);
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/
define_LTObjectImplPublic(LTDriverOtaStorage, LTDriverOtaStorageProxy,
    GetVersion,
    InitDownload,
    SaveBlock,
    Finalize,
);

define_LTObjectLibrary(1, LibInit, LibFini);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  26-Jan-25   trajan      created
 */
