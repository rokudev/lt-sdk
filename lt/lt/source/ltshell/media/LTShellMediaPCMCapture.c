/*******************************************************************************
 *
 * Implementation of PCM Capture command from LT Media Shell
 * -----------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include "LTShellMediaPCM.h"
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/system/fs/LTFile.h>
#include <lt/system/shell/LTSystemShell.h>

typedef struct {
    LTShell hShell;
    LTFile *pFile;
    LTDeviceMedia *deviceMedia;
    LTMediaSource hSource;
} PCMRecordingContext_t;

static struct Statics {
    PCMRecordingContext_t recordingCtx;
} S;

static void HandleMediaData(LTMediaEvent hEvent, void *eventData, void *pClientData) {
    PCMRecordingContext_t *pCtx = (PCMRecordingContext_t *)pClientData;
    ILTShell *iShell;
    LTMediaData *pMediaData = (LTMediaData *)eventData;

    if (hEvent == kLTMediaEvent_Data) {
        if (!pCtx || !pCtx->pFile) {
            return;
        }
        iShell = lt_gethandleinterface(ILTShell, pCtx->hShell);
        do {
            u32 res = pCtx->pFile->API->Write(pCtx->pFile, pMediaData->nDataLen, pMediaData->pData);
            if (res != pMediaData->nDataLen) {
                iShell->Print(pCtx->hShell, "Failed to write data\n");
                break;
            }
            return;
        } while (0);

        // Error
        StopPCMCapture();
    }
}

void StopPCMCapture(void) {
    if (S.recordingCtx.hSource) {
        ILTMediaSource *iSource = lt_gethandleinterface(ILTMediaSource, S.recordingCtx.hSource);
        iSource->Stop(S.recordingCtx.hSource);
        lt_destroyhandle(S.recordingCtx.hSource);
        S.recordingCtx.hSource = 0;
    }
    lt_closelibrary(S.recordingCtx.deviceMedia);
    S.recordingCtx.deviceMedia = NULL;

    if (S.recordingCtx.pFile) {
        lt_destroyobject(S.recordingCtx.pFile);
        S.recordingCtx.pFile = NULL;
    }
}

bool StartPCMCapture(LTShell hShell, LTFile *pFile, LTMediaFormat fmt) {
    ILTMediaSource *iSource;
    ILTShell *SHL_iShell = lt_gethandleinterface(ILTShell, hShell);
    if (S.recordingCtx.pFile) {
        SHL_iShell->Print(hShell, "Capture already in progress\n");
        return false;
    }

    S.recordingCtx.hShell = hShell;

    S.recordingCtx.pFile = pFile;

    S.recordingCtx.deviceMedia = lt_openlibrary(LTDeviceMedia);
    if (!S.recordingCtx.deviceMedia) {
        SHL_iShell->Print(hShell, "Failed to open device media\n");
        StopPCMCapture();
        return false;
    }
    S.recordingCtx.hSource = S.recordingCtx.deviceMedia->OpenSource(&fmt);
    if (!S.recordingCtx.hSource) {
        SHL_iShell->Print(hShell, "Failed to open audio source\n");
        StopPCMCapture();
        return false;
    }

    iSource = lt_gethandleinterface(ILTMediaSource, S.recordingCtx.hSource);
    iSource->OnMediaEvent(S.recordingCtx.hSource, HandleMediaData, &S.recordingCtx);
    iSource->Start(S.recordingCtx.hSource);

    SHL_iShell->Print(hShell, "PCM Capture started\n");
    return true;
}

