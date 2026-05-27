/*******************************************************************************
 * source/lt/media/sourcemanager/LTMediaSourceManager.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/
#include <lt/media/sourcemanager/LTMediaSourceManager.h>
#include <lt/device/media/LTDeviceMedia.h>

DEFINE_LTLOG_SECTION("sourcemanager");

#define ENABLE_PRINTMETRICS 0

// Debug logging control (set to 1 to enable verbose boot logs)
#define SOURCEMANAGER_DO_DLOG 0
#if SOURCEMANAGER_DO_DLOG
#define DLOG LTLOG
#else
#define DLOG LTLOG_LOGNULL
#endif

/*________________________________________________
  static constants */

enum {
    kLTMediaSourceManager_MaxSources = 16,
};

/*  ______________________________________________
 *  Object private data members */
typedef_LTObjectImpl(LTMediaSourceManager, LTMediaSourceManagerImpl) {
    LTCore              *pCore;
    ILTEvent            *pEvent;
    LTMutex             *srcMgrMutex;
    LTHandle             managedSources[kLTMediaSourceManager_MaxSources];
    u32                  managedSourcesCount;
    u32                  activeSourcesCount;
    u32                  eventsInProgress;
    LTMediaSourceManager_CaptureRequestCallback pCaptureRequestCallback;
    void                *pCaptureRequestClientData;
} LTOBJECT_API;

typedef struct {
    LTMediaSourceManagerImpl   *srcMgr;
    LTHandle                    hHandle;
    LTEvent                     hEvent;
    LTMediaData                *pMediaData;
    LT_SIZE                     allocatedMediaDataSize;
    u32                         formatEncoding;
    u32                         formatId;
    bool                        started;
    bool                        pendingDestroy;
    bool                        eventInFlight;  // Per source, only allow one event with data to be in flight at a time to prevent events building up over time from a slow consumer
} LTMediaSourceContext;

static const LTArgsDescriptor s_SourceMediaDataEventArgs =
    {2, {kLTArgType_lthandle, kLTArgType_pointer}};

static ILTMediaSource s_ILTMediaSource;

/*_________________________________
  Library private implementation */

#if ENABLE_PRINTMETRICS
static void PrintMetrics(LTMediaSourceManagerImpl *srcMgr) {
    srcMgr->srcMgrMutex->API->Lock(srcMgr->srcMgrMutex);
    LTLOG("metrics", "Managed sources: %d, Active sources: %d, Events in progress: %d",
        srcMgr->managedSourcesCount, srcMgr->activeSourcesCount, srcMgr->eventsInProgress);
    srcMgr->srcMgrMutex->API->Unlock(srcMgr->srcMgrMutex);
}
#else
#define PrintMetrics(x)
#endif

static void CleanupSource(LTMediaSourceManagerImpl *srcMgr, LTMediaSource hSource, LTMediaSourceContext *pSource) {
    for (u32 i = 0; i < srcMgr->managedSourcesCount; i++) {
        if (srcMgr->managedSources[i] == hSource) {
            lt_destroyhandle(pSource->hEvent);
            lt_free(pSource->pMediaData);
            srcMgr->managedSourcesCount--;
            for (u32 j = i; j < srcMgr->managedSourcesCount; j++) {
                srcMgr->managedSources[j] = srcMgr->managedSources[j + 1];
            }
            break;
        }
    }
}

static void DispatchData(LTEvent hEvent, void * pEventProc, LTArgs * pEventArgs, void * pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTMediaSource_OnMediaEventProc * pCallback = (LTMediaSource_OnMediaEventProc *)pEventProc;
    LTMediaSourceContext *pSource = LTArgs_pointerAt(1, pEventArgs);

    if (pSource) {
        if (pSource->started) {
            pCallback(kLTMediaEvent_Data, pSource->pMediaData, pEventProcClientData);
        }
    } else {
        LTLOG("dispatch.err", "Invalid source");
    }
}

static void DispatchComplete(LTEvent hEvent, LTArgs * pEventArgs) {
    LT_UNUSED(hEvent);
    LTMediaSource hSource = LTArgs_lthandleAt(0, pEventArgs);
    LTMediaSourceContext *pSource = LTArgs_pointerAt(1, pEventArgs);
    LTMediaSourceManagerImpl *pSrcMgr = pSource->srcMgr;
    bool requestCapture = false;

    pSrcMgr->srcMgrMutex->API->Lock(pSrcMgr->srcMgrMutex);
    LT_ASSERT(pSrcMgr->eventsInProgress > 0);
    pSrcMgr->eventsInProgress--;
    pSource->eventInFlight = false;

    if (pSrcMgr->eventsInProgress == 0 && pSrcMgr->activeSourcesCount > 0) {
        requestCapture = true;
    }
    pSrcMgr->srcMgrMutex->API->Unlock(pSrcMgr->srcMgrMutex);

    // Release the handle private data
    pSrcMgr->pCore->ReleaseHandlePrivateData(hSource, pSource);

    // Avoid holding the lock while calling the callback
    if (requestCapture) {
        if (pSrcMgr->pCaptureRequestCallback) {
            pSrcMgr->pCaptureRequestCallback(pSrcMgr->pCaptureRequestClientData);
        }
    }
}

/****************************************************/
/*          Source Manager implementation           */
/****************************************************/
static bool LTMediaSourceManagerImpl_SetCaptureRequestCallback(LTMediaSourceManagerImpl *srcMgr,
                                                               LTMediaSourceManager_CaptureRequestCallback pfnCallback,
                                                               void *pClientData) {
    if (!srcMgr) {
        LTLOG("setcallback.invargs", "Invalid parameters");
        return false;
    }
    srcMgr->pCaptureRequestCallback = pfnCallback;
    srcMgr->pCaptureRequestClientData = pClientData;
    return true;
}

static bool LTMediaSourceManagerImpl_FormatRequested(LTMediaSourceManagerImpl *srcMgr, u32 formatId) {
    bool requested = false;
    if (!srcMgr) {
        LTLOG("format.invargs", "Invalid parameters");
        return false;
    }

    srcMgr->srcMgrMutex->API->Lock(srcMgr->srcMgrMutex);
    for (u32 i = 0; i < srcMgr->managedSourcesCount && requested == false; i++) {
        LTMediaSourceContext *pContext = srcMgr->pCore->ReserveHandlePrivateData(srcMgr->managedSources[i]);
        if (pContext) {
            if (!pContext->pendingDestroy && pContext->started && pContext->formatId == formatId) {
                requested = true;
            }
            srcMgr->pCore->ReleaseHandlePrivateData(srcMgr->managedSources[i], pContext);
        }
    }
    srcMgr->srcMgrMutex->API->Unlock(srcMgr->srcMgrMutex);
    return requested;
}

static u32 LTMediaSourceManagerImpl_GetActiveSourceCount(LTMediaSourceManagerImpl *srcMgr) {
    u32 count = 0;
    if (srcMgr) {
        srcMgr->srcMgrMutex->API->Lock(srcMgr->srcMgrMutex);
        count = srcMgr->activeSourcesCount;
        srcMgr->srcMgrMutex->API->Unlock(srcMgr->srcMgrMutex);
    } else {
        LTLOG("getcount.invargs", "Invalid parameters");
    }
    return count;
}


static LTHandle LTMediaSourceManagerImpl_CreateSource(LTMediaSourceManagerImpl *srcMgr, u32 formatEncoding, u32 formatId) {
    LTMediaSource hSource = LTHANDLE_INVALID;
    LTMediaSourceContext * pSource = NULL;
    if (!srcMgr) {
        LTLOG("create.invargs", "Invalid parameters");
        return LTHANDLE_INVALID;
    }

    if (srcMgr->managedSourcesCount >= kLTMediaSourceManager_MaxSources) {
        LTLOG("create.maxsources", "Maximum number of sources reached");
        return LTHANDLE_INVALID;
    }

    hSource = srcMgr->pCore->CreateHandle((LTInterface *)&s_ILTMediaSource, sizeof(LTMediaSourceContext));
    pSource = srcMgr->pCore->ReserveHandlePrivateData(hSource);
    if (!pSource) {
        LTLOG("create.oom", "Failed to allocate memory for source context");
        srcMgr->pCore->DestroyHandle(hSource);
        return LTHANDLE_INVALID;
    }

    lt_memset(pSource, 0, sizeof(LTMediaSourceContext));
    pSource->hHandle        = hSource;
    pSource->hEvent         = srcMgr->pCore->CreateEvent(&s_SourceMediaDataEventArgs, DispatchData, DispatchComplete, NULL, NULL);
    pSource->formatEncoding = formatEncoding;
    pSource->formatId       = formatId;
    pSource->srcMgr         = srcMgr;
    pSource->pMediaData     = NULL;
    pSource->pendingDestroy = false;
    pSource->eventInFlight  = false;
    DLOG("create.fmt", "Created source with format %d", pSource->formatId);

    // Add to managed sources
    srcMgr->srcMgrMutex->API->Lock(srcMgr->srcMgrMutex);
    srcMgr->managedSources[srcMgr->managedSourcesCount] = hSource;
    srcMgr->managedSourcesCount++;
    srcMgr->srcMgrMutex->API->Unlock(srcMgr->srcMgrMutex);
    srcMgr->pCore->ReleaseHandlePrivateData(hSource, pSource);
    PrintMetrics(srcMgr);
    return hSource;
}

static u32 LTMediaSourceManagerImpl_DispatchMediaData(LTMediaSourceManagerImpl *srcMgr, u32 formatId, LTMediaData *data) {
    u32 sourcesDispatched = 0;
    if (!srcMgr || !data) {
        LTLOG("dispatch.invargs", "Invalid parameters");
        return 0;
    }
    srcMgr->srcMgrMutex->API->Lock(srcMgr->srcMgrMutex);
    for (u32 i = 0; i < srcMgr->managedSourcesCount; i++) {
        LTMediaSourceContext *pSource = srcMgr->pCore->ReserveHandlePrivateData(srcMgr->managedSources[i]);
        bool matched = false;
        if (!pSource) {
            continue;
        }
        do {
            if (pSource->pendingDestroy) break;
            if (!pSource->started) break;
            if (pSource->formatId != formatId) break;
            if (pSource->eventInFlight) break; // Source still needs to service the last event, ignore new data to prevent over-production of events

            if (pSource->allocatedMediaDataSize < data->nDataLen) {
                lt_free(pSource->pMediaData);
                pSource->pMediaData = lt_malloc(sizeof(LTMediaData) + data->nDataLen);
                if (!pSource->pMediaData) {
                    LTLOG_REDALERT("dispatch.oom", "Failed to allocate memory for media data");
                    pSource->allocatedMediaDataSize = 0;
                    break;
                } else {
                    pSource->pMediaData->pData = (u8 *)&(pSource->pMediaData[1]);
                    pSource->allocatedMediaDataSize = data->nDataLen;
                }
            }

            matched = true;
            lt_memcpy(pSource->pMediaData->pData, data->pData, data->nDataLen);
            pSource->pMediaData->nDataLen = data->nDataLen;
            pSource->pMediaData->nTimestamp = data->nTimestamp;
            pSource->pMediaData->meta = data->meta;

            srcMgr->eventsInProgress++;
            pSource->eventInFlight = true;
            srcMgr->pEvent->NotifyEvent(pSource->hEvent,
                                        pSource->hHandle,
                                        pSource);
            sourcesDispatched++;
        } while (0);

        if (!matched) {
            srcMgr->pCore->ReleaseHandlePrivateData(srcMgr->managedSources[i], pSource);
        } // else: pSource is released in DispatchSourceMediaDataEventComplete
    }
    srcMgr->srcMgrMutex->API->Unlock(srcMgr->srcMgrMutex);
    return sourcesDispatched;
}

/*  _________________________________
 *  Object constructor and destructor
 */
static void LTMediaSourceManagerImpl_DestructObject(LTMediaSourceManagerImpl *srcMgr) {
    // If this were not true, we would leak LTMediaSource instances
    // Orphaned LTMediaSource instances will cause a crash when they are destroyed
    LT_ASSERT(srcMgr->managedSourcesCount == 0);
    lt_destroyobject(srcMgr->srcMgrMutex);
    srcMgr->srcMgrMutex = NULL;
    srcMgr->eventsInProgress = 0;
    srcMgr->pCaptureRequestCallback = NULL;
    srcMgr->pCaptureRequestClientData = NULL;
}

static bool LTMediaSourceManagerImpl_ConstructObject(LTMediaSourceManagerImpl *srcMgr) {
    srcMgr->pCore = LT_GetCore();
    srcMgr->pEvent = lt_getlibraryinterface(ILTEvent, srcMgr->pCore);
    srcMgr->srcMgrMutex = lt_createobject(LTMutex);
    srcMgr->managedSourcesCount = 0;
    srcMgr->activeSourcesCount = 0;
    srcMgr->eventsInProgress = 0;
    srcMgr->pCaptureRequestCallback = NULL;
    srcMgr->pCaptureRequestClientData = NULL;
    return true;
}

/*  ________________________________________________________
 *  ILTMediaSource interface implementation */
static LTMediaEncoding LTManagedSource_GetMediaEncoding(LTMediaSource hSource) {
    LTMediaSourceContext *pContext = LT_GetCore()->ReserveHandlePrivateData(hSource);
    LTMediaEncoding encoding = kLTMediaEncoding_Unknown;
    if (pContext) {
        encoding = pContext->formatEncoding ;
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSource, pContext);
    return encoding;
}

static void
LTManagedSource_Refresh(LTMediaSource hSource) {
    LT_UNUSED(hSource);
}

static void
LTManagedSource_SetTargetBitrate(LTMediaSource hSource, u32 nTargetBitrate) {
    LT_UNUSED(hSource);
    LT_UNUSED(nTargetBitrate);
}

static void
LTManagedSource_SetGopLength(LTMediaSource hSource, u32 nGopLength) {
    LT_UNUSED(hSource);
    LT_UNUSED(nGopLength);
}

static void
LTManagedSource_OnMediaEvent(LTMediaSource hSource, LTMediaSource_OnMediaEventProc * pCallback, void * pClientData) {
    LTMediaSourceContext *pContext = LT_GetCore()->ReserveHandlePrivateData(hSource);
    if (!pContext) {
        LTLOG("start.noprivdata", "Cannot reserve private data for source");
        return;
    }

    if (pContext->srcMgr) {
        pContext->srcMgr->srcMgrMutex->API->Lock(pContext->srcMgr->srcMgrMutex);
        pContext->srcMgr->pEvent->RegisterForEvent(pContext->hEvent, (void *)pCallback, NULL, pClientData, false);
        pContext->srcMgr->srcMgrMutex->API->Unlock(pContext->srcMgr->srcMgrMutex);
    } else {
        LTLOG("start.nosrcmgr", "Source manager not found");
    }

    LT_GetCore()->ReleaseHandlePrivateData(hSource, pContext);
}

static void
LTManagedSource_NoMediaEvent(LTMediaSource hSource, LTMediaSource_OnMediaEventProc * pCallback) {
    LTMediaSourceContext *pContext = LT_GetCore()->ReserveHandlePrivateData(hSource);
    if (!pContext) {
        LTLOG("start.noprivdata", "Cannot reserve private data for source");
        return;
    }

    if (pContext->srcMgr) {
        pContext->srcMgr->srcMgrMutex->API->Lock(pContext->srcMgr->srcMgrMutex);
        pContext->srcMgr->pEvent->UnregisterFromEvent(pContext->hEvent, (void *)pCallback);
        pContext->srcMgr->srcMgrMutex->API->Unlock(pContext->srcMgr->srcMgrMutex);
    } else {
        LTLOG("start.nosrcmgr", "Source manager not found");
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSource, pContext);
}

static void
LTManagedSource_OnModeChangeEvent(LTMediaSource hSource, LTMediaSource_OnModeChangeEventProc *pCallback, void *pClientData) {
    /* No mode change events available for this driver */
    LT_UNUSED(hSource);
    LT_UNUSED(pCallback);
    LT_UNUSED(pClientData);
}

static void
LTManagedSource_NoModeChangeEvent(LTMediaSource hSource, LTMediaSource_OnModeChangeEventProc *pCallback) {
    /* No mode change events available for this driver */
    LT_UNUSED(hSource);
    LT_UNUSED(pCallback);
}

static void
LTManagedSource_Start(LTMediaSource hSource) {
    bool requestCapture = false;
    LTMediaSourceContext *pContext = LT_GetCore()->ReserveHandlePrivateData(hSource);
    if (!pContext) {
        LTLOG("start.noprivdata", "Cannot reserve private data for source");
        return;
    }

    if (pContext->srcMgr) {
        pContext->srcMgr->srcMgrMutex->API->Lock(pContext->srcMgr->srcMgrMutex);
        if (!pContext->started) {
            pContext->started = true;
            pContext->srcMgr->activeSourcesCount++;
        }
        if (pContext->srcMgr->activeSourcesCount == 1) {
            requestCapture = true;
        }
        pContext->srcMgr->srcMgrMutex->API->Unlock(pContext->srcMgr->srcMgrMutex);
    } else {
        LTLOG("start.nosrcmgr", "Source manager not found");
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSource, pContext);

    // Avoid holding the lock while calling the callback
    if (requestCapture) {
        if (pContext->srcMgr->pCaptureRequestCallback) {
            pContext->srcMgr->pCaptureRequestCallback(pContext->srcMgr->pCaptureRequestClientData);
        }
    }
    PrintMetrics(pContext->srcMgr);
}

static void
LTManagedSource_Stop(LTMediaSource hSource) {
    LTMediaSourceContext *pContext = LT_GetCore()->ReserveHandlePrivateData(hSource);
    if (!pContext) {
        LTLOG("stop.noprivdata", "Cannot reserve private data for source");
        return;
    }

    if (pContext->srcMgr) {
        pContext->srcMgr->srcMgrMutex->API->Lock(pContext->srcMgr->srcMgrMutex);
        if (pContext->started) {
            pContext->started = false;
            pContext->srcMgr->activeSourcesCount--;
        }
        pContext->srcMgr->srcMgrMutex->API->Unlock(pContext->srcMgr->srcMgrMutex);
    } else {
        LTLOG("stop.nosrcmgr", "Source manager not found");
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSource, pContext);
    PrintMetrics(pContext->srcMgr);
}

static void
LTManagedSource_Destroy(LTMediaSource hSource) {
    LTMediaSourceManagerImpl *srcMgr = NULL;
    LTMediaSourceContext *pContext = LT_GetCore()->ReserveHandlePrivateData(hSource);

    if (!pContext) {
        LTLOG("destroy.noprivdata", "Cannot reserve private data for source");
        return;
    }

    if (!pContext->srcMgr) {
        LTLOG("destroy.nosrcmgr", "Source manager not found");
        LT_GetCore()->ReleaseHandlePrivateData(hSource, pContext);
        return;
    }

    LTLOG("consumer.destroyed", "Consumer disconnected (handle=0x%x)", hSource);

    srcMgr = pContext->srcMgr;
    srcMgr->srcMgrMutex->API->Lock(srcMgr->srcMgrMutex);

    pContext->pendingDestroy = true;
    if (pContext->started) {
        pContext->started = false;
        srcMgr->activeSourcesCount--;
    }

    // Always cleanup immediately - the handle is being destroyed regardless
    CleanupSource(srcMgr, hSource, pContext);

    srcMgr->srcMgrMutex->API->Unlock(srcMgr->srcMgrMutex);
    srcMgr->pCore->ReleaseHandlePrivateData(hSource, pContext);
    PrintMetrics(srcMgr);
}

/*____________________________________________
/ ILTMediaSource library interface binding */
define_LTLIBRARY_INTERFACE(ILTMediaSource, LTManagedSource_Destroy) {
    .GetMediaEncoding   = &LTManagedSource_GetMediaEncoding,
    .OnMediaEvent       = &LTManagedSource_OnMediaEvent,
    .NoMediaEvent       = &LTManagedSource_NoMediaEvent,
    .OnModeChangeEvent  = &LTManagedSource_OnModeChangeEvent,
    .NoModeChangeEvent  = &LTManagedSource_NoModeChangeEvent,
    .Start              = &LTManagedSource_Start,
    .Stop               = &LTManagedSource_Stop,
    .Refresh            = &LTManagedSource_Refresh,
    .SetTargetBitrate   = &LTManagedSource_SetTargetBitrate,
    .SetGopLength       = &LTManagedSource_SetGopLength,
} LTLIBRARY_DEFINITION;

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTMediaSourceManager, LTMediaSourceManagerImpl,
    SetCaptureRequestCallback,
    FormatRequested,
    GetActiveSourceCount,
    CreateSource,
    DispatchMediaData
);

define_LTOBJECT_EXPORTLIBRARY(
    LTMediaSourceManager, 1,
    LTMediaSourceManagerImpl
);
