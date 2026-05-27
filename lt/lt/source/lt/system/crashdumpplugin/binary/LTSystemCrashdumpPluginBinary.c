/*******************************************************************************
 * lt/system/crashdumpplugin/binary/LTSystemCrashdumpBinary.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/net/httpclient/LTNetHttpClient.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/system/crashdump/LTSystemCrashdump.h>
#include <lt/system/crashdump/LTSystemCrashdumpAccessWrapper.h>
#include <lt/system/crashdump/LTSystemCrashdumpPlugin.h>

DEFINE_LTLOG_SECTION("crashdump.binary")

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTSystemCrashdumpPluginBinaryLib, (LTNetHttpClient) (LTSystemSettings) (LTSystemCrashdump));

static struct Statics {
    ILTThread *iThread;
    ILTEvent  *iEvent;
    LTAtomic   objCounter;
} S;

static void LTSystemCrashdumpPluginBinaryImpl_LibFini(void) {
    lt_memset(&S, 0, sizeof(S));
}

static bool LTSystemCrashdumpPluginBinaryImpl_LibInit(void) {
    {
        LTCore *iCore = LT_GetCore();
        S = (struct Statics) {
            .iThread = lt_getlibraryinterface(ILTThread, iCore),
            .iEvent  = lt_getlibraryinterface(ILTEvent, iCore)
        };
    }

    return true;
}

typedef_LTObjectImpl(LTSystemCrashdumpPlugin, LTSystemCrashdumpPluginBinary) {
    char           *pBuffer;
    LT_SIZE         nBufferSize;
    LT_SIZE         nTotal;
    LT_SIZE         nRemainder;
    LT_SIZE         nBuffered;
    LTString        pRequestUrl;
    LTString        pRequestTlsSpec;
    LTString        pVirtualID;
    LTEvent         hEvent;
    LTThread        hThread;
    LTHttpRequest   hRequest;
    ILTHttpRequest *iRequest;
}  LTOBJECT_API;

static const LTArgsDescriptor s_CoreUploadEventArgs = { 1, { kLTArgType_u32 } };

static bool AllocateBuffer(char **ppBuffer, LT_SIZE *pBufferSize) {
    const LT_SIZE minBufferSize = 4096;
    const LT_SIZE maxBufferSize = 65536;

    for (LT_SIZE bufferSize = maxBufferSize; bufferSize >= minBufferSize; bufferSize /= 2) {
        char *pBuffer = lt_malloc(bufferSize);
        if (pBuffer) {
            *ppBuffer = pBuffer;
            *pBufferSize = bufferSize;
            return true;
        }
    }

    *ppBuffer = NULL;
    *pBufferSize = 0;
    return false;
}

static void
DispatchCoreUploadEvent(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    if (!proc) return;

    LTSystemCrashdump_EventProc *eventCallback = (LTSystemCrashdump_EventProc *)proc;
    LTSystemCrashdump_Event uploadEvent = (LTSystemCrashdump_Event)LTArgs_u32At(0, args);
    eventCallback(uploadEvent, data);
}

static void
ResetCrashdumpPlugin(LTSystemCrashdumpPluginBinary *pPlugin) {
    lt_destroyhandle(pPlugin->hRequest);
    pPlugin->hRequest = LTHANDLE_INVALID;

    lt_free(pPlugin->pBuffer);
    pPlugin->pBuffer = NULL;

    pPlugin->nRemainder = 0;
    pPlugin->nBuffered = 0;

    ltstring_destroy(pPlugin->pVirtualID);
    pPlugin->pVirtualID = NULL;
    ltstring_destroy(pPlugin->pRequestTlsSpec);
    pPlugin->pRequestTlsSpec = NULL;
    ltstring_destroy(pPlugin->pRequestUrl);
    pPlugin->pRequestUrl = NULL;
}

static void
LTSystemCrashdumpPluginBinary_DestructObject(LTSystemCrashdumpPluginBinary *pPlugin) {
    if (LTAtomic_FetchSubtract(&S.objCounter, 1) == 1) {
        LTSystemCrashdumpPluginBinaryImpl_LibFini();
    }
    lt_destroyhandle(pPlugin->hThread);
    pPlugin->hThread= LTHANDLE_INVALID;
    ResetCrashdumpPlugin(pPlugin);
    lt_destroyhandle(pPlugin->hEvent);
}

static bool
LTSystemCrashdumpPluginBinary_ConstructObject(LTSystemCrashdumpPluginBinary *pPlugin) {
    if (LTAtomic_FetchAdd(&S.objCounter, 1) == 0) {
        if (!LTSystemCrashdumpPluginBinaryImpl_LibInit()) {
            LTAtomic_FetchSubtract(&S.objCounter, 1);
            LTSystemCrashdumpPluginBinaryImpl_LibFini();
            return false;
        }
    }

    pPlugin->pBuffer = NULL;
    pPlugin->nBufferSize = 0;
    pPlugin->nRemainder = 0;
    pPlugin->nBuffered = 0;
    pPlugin->pRequestUrl = NULL;
    pPlugin->pRequestTlsSpec = NULL;
    pPlugin->pVirtualID = NULL;
    pPlugin->hEvent = LTHANDLE_INVALID;
    pPlugin->hThread = LTHANDLE_INVALID;
    pPlugin->hRequest = LTHANDLE_INVALID;

    do {
        LTCore *iCore = LT_GetCore();
        pPlugin->hEvent = iCore->CreateEvent(&s_CoreUploadEventArgs, DispatchCoreUploadEvent, NULL, NULL, NULL);
        if (!pPlugin->hEvent) break;

        pPlugin->hThread = iCore->CreateThread("core.upload.worker");
        if (!pPlugin->hThread) break;
        S.iThread->Start(pPlugin->hThread, NULL, NULL);
        return true;
    } while (false);

    LTSystemCrashdumpPluginBinary_DestructObject(pPlugin);
    return false;
}

static bool TransferCoreData(LTSystemCrashdumpPluginBinary *pPlugin, LTHttpRequest hRequest) {
    while (pPlugin->nRemainder > 0 || pPlugin->nBuffered > 0) {
        LT_SIZE freeSpace = pPlugin->nBufferSize - pPlugin->nBuffered;
        if (pPlugin->nRemainder > 0 && freeSpace > 0) {
            LT_SIZE chunkSize = LT_MIN(pPlugin->nRemainder, freeSpace);
            if (!LT_GetLTSystemCrashdump()->GetChunk(
                &pPlugin->pBuffer[pPlugin->nBuffered], chunkSize,
                    pPlugin->nTotal - pPlugin->nRemainder)) {
                return false;
            }
            pPlugin->nBuffered += chunkSize;
            pPlugin->nRemainder -= chunkSize;
        }

        s32 outBytes = pPlugin->iRequest->WriteBodyData(hRequest,
                                                        pPlugin->pBuffer,
                                                        pPlugin->nBuffered);
        if (outBytes < 0) {
            LTLOG_SERVER("core.upload.write.fail",
                         "Failed to write core data");
            return false;
        }
        if (outBytes == 0) break;

        pPlugin->nBuffered -= outBytes;
        if (pPlugin->nBuffered > 0) {
            lt_memmove(&pPlugin->pBuffer[0], &pPlugin->pBuffer[outBytes],
                       pPlugin->nBuffered);
        }
    }

    return true;
}

static void OnRequestEvent(LTHttpRequest hRequest, LTHttpRequestEvent event,
                           void *pClientData) {
    LTSystemCrashdumpPluginBinary *pPlugin = pClientData;
    if (!pPlugin || pPlugin->hRequest != hRequest || !pPlugin->hRequest) return;

    switch (event) {
    case LTHttpRequestEvent_HeadComplete: {
        u32 nStatusCode = pPlugin->iRequest->GetResponseStatusCode(hRequest);
        LTLOG_DEBUG("event.head", "Head: Status Code: %lu", LT_Pu32(nStatusCode));
        LTSystemCrashdump_Event crashDumpEvent = kLTSystemCrashdump_Event_UploadComplete;
        if (nStatusCode != 200) {
            // HTTP server reports an unexpected status code
            LTLOG_SERVER("core.upload.status.fail", "Status: %lu",
                         LT_Pu32(nStatusCode));
            crashDumpEvent = kLTSystemCrashdump_Event_UploadFailed;
        }
        S.iEvent->NotifyEvent(pPlugin->hEvent, crashDumpEvent);
        break;
    }

    case LTHttpRequestEvent_BodyDataAvailable:
        while (pPlugin->iRequest->BodyDataAvailable(hRequest) > 0 &&
               pPlugin->iRequest->ReadBodyData(hRequest, pPlugin->pBuffer,
                                            pPlugin->nBufferSize) > 0);
        break;

    case LTHttpRequestEvent_BodyWriteReady:
        if (!TransferCoreData(pPlugin, hRequest)) {
            pPlugin->nRemainder = 0;
            pPlugin->nBuffered = 0;

            S.iEvent->NotifyEvent(pPlugin->hEvent,
                                  kLTSystemCrashdump_Event_UploadFailed);
            return;
        }
        break;

    case LTHttpRequestEvent_Error:
        S.iEvent->NotifyEvent(pPlugin->hEvent, kLTSystemCrashdump_Event_UploadFailed);
        break;

    case LTHttpRequestEvent_RequestComplete:
    case LTHttpRequestEvent_Disconnected:
    case LTHttpRequestEvent_DisconnectPending:
        pPlugin->iRequest->NoRequestEvent(pPlugin->hRequest, OnRequestEvent);
        break;

    default:
        break;
    }
}

static void StartUploadProc(void *pClientData) {
    LTSystemCrashdumpPluginBinary *pPlugin = pClientData;
    if (!pPlugin) return;

    ResetCrashdumpPlugin(pPlugin);

    LTSystemCrashdump_Event crashDumpEvent = kLTSystemCrashdump_Event_UploadFailed;
    do {
        LTSystemSettings *pSettings = LT_GetLTSystemSettings();

        pPlugin->nRemainder = pPlugin->nTotal = LT_GetLTSystemCrashdump()->GetSize();
        if (pPlugin->nTotal == 0) {
            crashDumpEvent = kLTSystemCrashdump_Event_NoDump;
            break;
        }
        const char *sVersion = LT_GetLTSystemCrashdump()->GetVersion();

        if (!AllocateBuffer(&pPlugin->pBuffer, &pPlugin->nBufferSize)) break;

        pPlugin->pRequestUrl = NULL;
        if (!pSettings->GetStringValue("crashdump/url", &pPlugin->pRequestUrl)) break;

        pPlugin->pRequestTlsSpec = NULL;
        if (!pSettings->GetStringValue("crashdump/tlsspec", &pPlugin->pRequestTlsSpec)) break;

        pPlugin->pVirtualID = NULL;
        if (!pSettings->GetStringValue("iot/vdid", &pPlugin->pVirtualID)) break;

        pPlugin->hRequest = LT_GetLTNetHttpClient()->CreateRequest(
            pPlugin->pRequestUrl,
            kLTHttpRequestMethod_POST, LTHANDLE_INVALID);
        if (!pPlugin->hRequest) break;

        pPlugin->iRequest = lt_gethandleinterface(ILTHttpRequest, pPlugin->hRequest);
        pPlugin->iRequest->SetTlsSpec(pPlugin->hRequest, pPlugin->pRequestTlsSpec);
        pPlugin->iRequest->OnRequestEvent(pPlugin->hRequest, OnRequestEvent, pClientData);
        pPlugin->iRequest->AddHeader(pPlugin->hRequest,
                                     "X-serial-number",
                                     kLTArgType_charstar,
                                     pPlugin->pVirtualID);
        pPlugin->iRequest->AddHeader(pPlugin->hRequest,
                                     "X-Roku-Reserved-Version",
                                     kLTArgType_charstar,
                                     sVersion);
        pPlugin->iRequest->AddHeader(pPlugin->hRequest,
                                     "X-Build-Number",
                                     kLTArgType_charstar,
                                     sVersion);
        pPlugin->iRequest->AddHeader(pPlugin->hRequest,
                                     "X-Channel-ID",
                                     kLTArgType_charstar, "0");
        pPlugin->iRequest->AddHeader(pPlugin->hRequest,
                                     "X-Channel-Version",
                                     kLTArgType_charstar,
                                     "0");
        pPlugin->iRequest->AddHeader(pPlugin->hRequest,
                                     "X-Encryption",
                                     kLTArgType_charstar,
                                     "unencrypted");

        LTString amoebaId = NULL;
        pSettings->GetStringValue("amoeba/identifiers", &amoebaId);
        if (amoebaId && amoebaId[0]) { // not empty amoebaId.
            pPlugin->iRequest->AddHeader(pPlugin->hRequest, "X-Roku-Reserved-Amoeba-Ids", kLTArgType_charstar, amoebaId);
        }
        ltstring_destroy(amoebaId);

        pPlugin->iRequest->SetBody(pPlugin->hRequest, "application/octet-stream",
                                   NULL, pPlugin->nRemainder);
        pPlugin->iRequest->SendRequest(pPlugin->hRequest);

        return;
    } while (false);

    ResetCrashdumpPlugin(pPlugin);

    if (crashDumpEvent == kLTSystemCrashdump_Event_UploadFailed) {
        LTLOG_SERVER("core.upload.start.fail",
                     "Failed to start core upload");
    }
    S.iEvent->NotifyEvent(pPlugin->hEvent, crashDumpEvent);
}

static void
LTSystemCrashdumpPluginBinary_StartUpload(LTSystemCrashdumpPluginBinary *pPlugin) {
    S.iThread->QueueTaskProc(pPlugin->hThread, StartUploadProc, NULL, pPlugin);
}

static void
LTSystemCrashdumpPluginBinary_OnUploadEvent(LTSystemCrashdumpPluginBinary *pPlugin,
                                            LTSystemCrashdump_EventProc *proc,
                                            LTThread_ClientDataReleaseProc *clientDataReleaseProc,
                                            void *clientData) {
    S.iEvent->RegisterForEvent(pPlugin->hEvent, proc,
                               clientDataReleaseProc, clientData, false);
}

static void
LTSystemCrashdumpPluginBinary_NoUploadEvent(LTSystemCrashdumpPluginBinary *pPlugin,
                                            LTSystemCrashdump_EventProc *proc) {
    S.iEvent->UnregisterFromEvent(pPlugin->hEvent, proc);
}

static bool
LTSystemCrashdumpPluginBinary_CrashdumpErase(LTSystemCrashdumpPluginBinary *pPlugin) {
    ResetCrashdumpPlugin(pPlugin);
    return LT_GetLTSystemCrashdump()->Erase();
}

static void   /* This plugin specialization calls into LTSystemCrashdump directly and does not use the access wrapper. */
LTSystemCrashdumpPluginBinary_SetCrashdumpAccessWrapper(LTSystemCrashdumpPluginBinary *pPlugin,
                                                        LTSystemCrashdumpAccessWrapper *pWrapper)
{ LT_UNUSED(pPlugin), LT_UNUSED(pWrapper); }

define_LTObjectImplPublic(LTSystemCrashdumpPlugin, LTSystemCrashdumpPluginBinary,
    OnUploadEvent,
    NoUploadEvent,
    StartUpload,
    CrashdumpErase,
    SetCrashdumpAccessWrapper
)

define_LTOBJECT_EXPORTLIBRARY(LTSystemCrashdumpPluginBinary, 1, LTSystemCrashdumpPluginBinary);
