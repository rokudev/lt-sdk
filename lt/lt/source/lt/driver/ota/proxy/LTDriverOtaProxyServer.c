/*******************************************************************************
 * source/lt/drover/ota/proxy/LTDriverOtaProxyServer.c - LTDriverOta proxy server
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/
#include <lt/LT.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/device/ota/LTDeviceOta.h>
#include <lt/device/otabundle/LTDeviceOtaBundle.h>
#include <lt/product/config/LTProductConfig.h>
#include <lt/device/sdio/LTDeviceSdioPort.h>
#include <lt/utility/messagepack/LTUtilityMessagePack.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>
#include "LTDriverOtaProxy.h"

DEFINE_LTLOG_SECTION("ota.prx.srv");

#define OTA_PROXY_SERVER_RX_THREAD_STACKSIZE     1024
#define OTA_PROXY_SERVER_TX_THREAD_STACKSIZE     1024

static LTSystemSettings     *s_settings;
static LTDeviceSdioPort     *s_SdioPort;
static LTOThread            *s_txThread;
static LTOThread            *s_rxThread;
static LTUtilityMessagePack *s_mpack;
static LTDriverLibrary      *s_driverlib;
static LTHandle              s_hOta;
static ILTOta               *s_iOta;
static char                 *s_newVersion;
static u32                   s_imageSize;

static void SendResponseProc(void *clientData) {
    LTBuffer *response = clientData;
    LTBufferAccess access = response->API->StartRead(response, 0);
    s_SdioPort->API->Write(s_SdioPort, access.span.data, kLTDeviceSdioPort_BlockSize);
    response->API->FinishRead(response, access);
    lt_destroyobject(response);
}

static void SendResponse(LTBuffer *response) {
    s_txThread->API->QueueTaskProc(s_txThread, SendResponseProc, NULL, response);
}

static void Handle_GetVersion(LTBuffer *packet) {
    LT_UNUSED(packet);

    if (!s_iOta->IsValidated()) {
        /* Validate OTA partition (SDIO works, device should be recoverable) */
        LTLOG("validate", "Marking OTA partition valid");
        s_iOta->MarkValidated();
    }

    bool bVersionMatched = false;
    const char *curVersion = LT_GetCore()->GetSoftwareVersion();
    LTString expectedVersion = ltstring_create("");
    if (s_settings->GetStringValue("ota/expectedVersion", &expectedVersion)) {
        if (lt_strcmp(curVersion, expectedVersion) == 0) {
            bVersionMatched = true;
            s_settings->DeleteSetting("ota/expectedVersion");
        } else {
            LTLOG_YELLOWALERT("vers.mismatch", "Version mismatch after OTA update - expected %s, got %s", expectedVersion, curVersion);
        }
    }
    LTLOG("cur.ver", "%s %u", curVersion, bVersionMatched);
    LTBuffer *response = lt_createobject(LTBuffer);
    response->API->Reserve(response, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(response, kLTOtaProxy_ResponseType_GetVersion);
    s_mpack->WriteCString(response, curVersion, false);
    s_mpack->WriteBoolean(response, bVersionMatched);
    SendResponse(response);
}

static void Handle_InitDownload(LTBuffer *packet) {
    const char *assetName  = s_mpack->CopyString(packet);
    const char *newVersion = s_mpack->CopyString(packet);
    s_mpack->ReadIntU32(packet, &s_imageSize);
    s_newVersion = lt_strdup(newVersion);

    LTDeviceOta_Status initResult = kLTDeviceOta_Status_Error;
    do {
        if (!s_iOta->CheckStorage(s_imageSize))   break;
        if (!s_iOta->PrepareStorage(s_imageSize)) break;
        initResult = kLTDeviceOta_Status_Ok;
    } while(false);

    s_mpack->FreeString(packet, assetName);
    s_mpack->FreeString(packet, newVersion);

    LTBuffer *response = lt_createobject(LTBuffer);
    response->API->Reserve(response, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(response, kLTOtaProxy_ResponseType_InitDownload);
    s_mpack->WriteIntU32(response, initResult);
    SendResponse(response);
}

static void Handle_SaveBlock(LTBuffer *packet) {
    u32 offset, dataLen;
    s_mpack->ReadIntU32(packet, &offset);
    s_mpack->ReadBinary(packet, &dataLen);
    LTBufferAccess data = packet->API->StartRead(packet, dataLen);
    bool result = s_iOta->SaveBlock(data.span.data, data.span.size, offset);
    packet->API->FinishRead(packet, data);

    LTBuffer *response = lt_createobject(LTBuffer);
    response->API->Reserve(response, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(response, kLTOtaProxy_ResponseType_SaveBlock);
    s_mpack->WriteBoolean(response, result);
    SendResponse(response);
}

static void RebootProc(void *clientData) {
    LT_UNUSED(clientData);
    LTDeviceWatchdog *watchdog = lt_openlibrary(LTDeviceWatchdog);
    watchdog->Reboot();
}

static void Handle_Finalize(LTBuffer *packet) {
    u32 imageSize;
    u32 imageHashSize;
    s_mpack->ReadIntU32(packet, &imageSize);
    s_mpack->ReadBinary(packet, &imageHashSize);
    LTBufferAccess access = packet->API->StartRead(packet, imageHashSize);

    LTDeviceOta_Status result = kLTDeviceOta_Status_NeedReboot;
    do {
        if (!s_iOta->VerifyImage(imageSize, access.span.data)) {
            LTLOG_YELLOWALERT("final.verify.err", NULL);
            result = kLTDeviceOta_Status_Error;
            break;
        }
        if (!s_iOta->ApplyUpdate(s_newVersion)) {
            LTLOG_YELLOWALERT("final.apply.err", NULL);
            result = kLTDeviceOta_Status_Error;
            break;
        }

        s_settings->SetStringValue("ota/expectedVersion", s_newVersion);
        LTOThread *thread =  LT_GetCore()->GetCurrentThreadObject();
        thread->API->SetTimer(thread, LTTime_Milliseconds(500), RebootProc, NULL, NULL);
    } while(false);
    lt_free(s_newVersion); s_newVersion = NULL;

    LTBuffer *response = lt_createobject(LTBuffer);
    response->API->Reserve(response, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(response, kLTOtaProxy_ResponseType_Finalize);
    s_mpack->WriteIntU32(response, result);
    SendResponse(response);
}

static void SdioReadCallback(LTDeviceSdioPort *bus, u32 requestedLen, void *clientData) {
    LT_UNUSED(bus); LT_UNUSED(clientData);

    LTBuffer *packet = lt_createobject(LTBuffer);
    packet->API->Reserve(packet, requestedLen);
    LTBufferAccess write = packet->API->StartWrite(packet, requestedLen);

    if (!s_SdioPort->API->Read(s_SdioPort, write.span.data)) {
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
        case kLTOtaProxy_RequestType_GetVersion:
            Handle_GetVersion(packet);
            break;

        case kLTOtaProxy_RequestType_InitDownload:
            Handle_InitDownload(packet);
            break;

        case kLTOtaProxy_RequestType_SaveBlock:
            Handle_SaveBlock(packet);
            break;

        case kLTOtaProxy_RequestType_Finalize:
            Handle_Finalize(packet);
            break;

        default:
            LTLOG_YELLOWALERT("pk.unhandled", "Unknown packet type: %lu, size: %lu", LT_Pu32(packetType), LT_Pu32(requestedLen));
            break;
    }
done:
    lt_destroyobject(packet);
}

static bool RxThread_Init(void) {
    return s_SdioPort->API->Bind(s_SdioPort, kLTDeviceSdioPort_Ota, SdioReadCallback, NULL);
}

static void RxThread_Exit(void) {
    s_SdioPort->API->Unbind(s_SdioPort);
}

static void LTDriverOtaProxyServerImpl_LibFini(void) {
    lt_closelibrary(s_driverlib);
    s_driverlib = NULL;
    if (s_rxThread) {
        s_rxThread->API->Terminate(s_rxThread);
        s_rxThread->API->WaitUntilFinished(s_rxThread, LTTime_Infinite());
    }
    if (s_txThread) {
        s_txThread->API->Terminate(s_txThread);
        s_txThread->API->WaitUntilFinished(s_txThread, LTTime_Infinite());
    }
    lt_destroyobject(s_SdioPort);
    s_SdioPort = NULL;
    lt_destroyobject(s_rxThread);
    s_rxThread = NULL;
    lt_destroyobject(s_txThread);
    s_txThread = NULL;
    lt_closelibrary(s_mpack);
    s_mpack = NULL;
    lt_closelibrary(s_settings);
    s_settings = NULL;
    lt_free(s_newVersion);
    s_newVersion = NULL;
}

static bool LTDriverOtaProxyServerImpl_LibInit(void) {
    do {
        LTProductConfig *productConfig;
        u32 configSection;
        const char *driverName;
        if (!(s_settings = lt_openlibrary(LTSystemSettings)))                                    break;
        if (!(s_mpack = lt_openlibrary(LTUtilityMessagePack)))                                   break;
        if (!(productConfig = lt_openlibrary(LTProductConfig)))                                  break;
        if (!(configSection = productConfig->GetLibraryConfigSection("LTDriverOtaProxyServer"))) break;
        if (!(driverName = productConfig->ReadString(configSection, "driver")))                  break;
        if (!(s_driverlib = (LTDriverLibrary *)LT_GetCore()->OpenLibrary(driverName)))           break;
        if (!(s_hOta = s_driverlib->CreateDeviceUnitHandle(0)))                                  break;
        if (!(s_iOta = lt_gethandleinterface(ILTOta, s_hOta)))                                   break;
        if (!s_iOta->Init())                                                                     break;
        if (!(s_SdioPort = lt_createobject(LTDeviceSdioPort)))                                   break;
        if (!(s_txThread = lt_createobject(LTOThread)))                                          break;
        s_txThread->API->SetStackSize(s_txThread, OTA_PROXY_SERVER_TX_THREAD_STACKSIZE);
        s_txThread->API->Start(s_txThread, "ota.prx.srv.tx", NULL, NULL);
        if (!(s_rxThread = lt_createobject(LTOThread)))                                          break;
        s_rxThread->API->SetStackSize(s_rxThread, OTA_PROXY_SERVER_RX_THREAD_STACKSIZE);
        s_rxThread->API->Start(s_rxThread, "ota.prx.srv.rx", RxThread_Init, RxThread_Exit);
        lt_closelibrary(productConfig);
        return true;
    } while(false);

    return false;
}

/*  _________________________
    Library Root Interface */
typedef_LTLIBRARY_ROOT_INTERFACE(LTDriverOtaProxyServer, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTDriverOtaProxyServer, ) LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  26-Jan-25   trajan      created
 */
