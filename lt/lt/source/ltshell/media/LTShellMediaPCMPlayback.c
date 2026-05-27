/*******************************************************************************
 *
 * Implementation of PCM Playback command from LT Media Shell
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

enum {
    kPCMPlaybackWaitTimeMs = 20,
};

typedef struct {
    LTShell hShell;
    LTFile *pFile;
    LTMediaFormat fmt;
} PCMPlaybackProcContext_t;

static struct Statics {
   LTThread hPlaybackThread;
   PCMPlaybackProcContext_t playbackCtx;
} S;

static void PCMPlaybackProc(void *pClientData) {
    ILTThread *iThread;
    LTShell hShell;
    ILTShell *SHL_iShell;
    
    PCMPlaybackProcContext_t *pCtx;
    pCtx = (PCMPlaybackProcContext_t *)pClientData;
    if (!pCtx || !pCtx->pFile || !pCtx->pFile->API) {
        return;
    }

    hShell = pCtx->hShell;
    iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    SHL_iShell = lt_gethandleinterface(ILTShell, hShell);

    if (!iThread->IsTerminatePending(iThread->GetCurrentThread())) {
        LTDeviceMedia *deviceMedia = lt_openlibrary(LTDeviceMedia);

        if (deviceMedia) {
            LTMediaSink sink = deviceMedia->OpenSink(&S.playbackCtx.fmt);
            if (sink) {
                ILTMediaSink *iMediaSink = lt_gethandleinterface(ILTMediaSink, sink);
                u32 maxPacketSize = iMediaSink->GetMaxFrameByteSize(sink);
                iMediaSink->Start(sink);

                while (!iThread->IsTerminatePending(iThread->GetCurrentThread())) {
                    u32 readRes;

                    if (iMediaSink->GetAvailableFrameBuffers(sink) == 0) {
                        iThread->Sleep(LTTime_Milliseconds(kPCMPlaybackWaitTimeMs));
                        continue;
                    }
                    LTMediaData *packet = lt_malloc(sizeof(LTMediaData) + maxPacketSize);
                    if (!packet) {
                        SHL_iShell->Print(hShell, "Failed to allocate packet\n");
                        break;
                    }

                    packet->pData = (u8 *)(packet + 1);
                    packet->nTimestamp = 0;
                    packet->bEndOfFrame = 0;
                    readRes = pCtx->pFile->API->Read(pCtx->pFile, maxPacketSize, packet->pData);

                    if (!readRes) {
                        //EOF
                        lt_free(packet);
                        break;
                    }
                    packet->nDataLen = readRes;

                    if (readRes != maxPacketSize) {
                        packet->bEndOfFrame = 1;
                    }
                    iMediaSink->HandleMediaData(sink, packet);
                }
                iMediaSink->Stop(sink, iThread->IsTerminatePending(iThread->GetCurrentThread()));
                lt_destroyhandle(sink);
            }
            lt_closelibrary(deviceMedia);
        }
    }

    // cleanup
    SHL_iShell->Print(hShell, "Playback complete\n");
    lt_destroyobject(pCtx->pFile);
    pCtx->pFile = NULL;
}

void StopPCMPlayback(void) {
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    if (S.hPlaybackThread) {
        iThread->Terminate(S.hPlaybackThread);
        iThread->WaitUntilFinished(S.hPlaybackThread, LTTime_Infinite());
        lt_destroyhandle(S.hPlaybackThread);
        S.hPlaybackThread = 0;
    }
}

bool PlayPCMFile(LTShell hShell, LTFile *pFile, LTMediaFormat fmt) {
    ILTShell *SHL_iShell = lt_gethandleinterface(ILTShell, hShell);
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    StopPCMPlayback();

    pFile->API->SeekToPosition(pFile, 0);
    S.playbackCtx.hShell = hShell;
    S.playbackCtx.pFile = pFile;
    S.playbackCtx.fmt = fmt;


    S.hPlaybackThread = LT_GetCore()->CreateThread("pcmplayback");
    if (!S.hPlaybackThread) {
        SHL_iShell->Print(hShell, "Failed to create playback thread\n");
        return false;
    }
    iThread->Start(S.hPlaybackThread, NULL, NULL);
    iThread->QueueTaskProc(S.hPlaybackThread, PCMPlaybackProc, NULL, (void *)&S.playbackCtx);
    return true;
}
