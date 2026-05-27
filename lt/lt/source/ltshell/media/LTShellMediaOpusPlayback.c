/*******************************************************************************
 *
 * Implementation of Opus Playback command from LT Media Shell
 * -----------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include "LTShellMediaOpus.h"
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/system/fs/LTFile.h>
#include <lt/system/shell/LTSystemShell.h>

typedef struct {
    LTShell hShell;
    LTFile *pFile;
    OggsPage_t *pPage;
} OggsPlaybackProcContext_t;

static struct Statics {
   LTThread hPlaybackThread;
   OggsPlaybackProcContext_t oggPlaybackCtx;
} S;

enum {
    kOpusPlaybackSampleRate = 16000,
    kOpesPlaybackWaitTimeMs = 20,
};

static bool GetNextPacket(OggsPlaybackProcContext_t *pCtx, u8 **ppPacket, u32 *pPacketSize) {
    if (!pCtx || !pCtx->pPage) {
        return false;
    }
    ILTShell *SHL_iShell = lt_gethandleinterface(ILTShell, pCtx->hShell);

    // Ensure there are unread packets in the current page, read a new page if necessary,
    // skip pages with no packets
    while (pCtx->pPage->segmentIndex >= pCtx->pPage->header.pageSegments) {
        // Read a new page
        if (pCtx->pFile->API->GetPosition(pCtx->pFile) >= pCtx->pFile->API->GetSize(pCtx->pFile)) {
            SHL_iShell->Print(pCtx->hShell, "End of file\n");
            return false;
        } else {
            if (!OggSReadPageFromFile(pCtx->pFile, pCtx->pPage)) {       
                SHL_iShell->Print(pCtx->hShell, "Failed to read page\n");
                return false;
            }
        }
    }

    // There are unread packets in the current page
    return OggSGetPacketFromPage(pCtx->pPage, ppPacket, pPacketSize);
}

static void OggSPlaybackProc(void *pClientData) {
    ILTThread *iThread;
    LTShell hShell;
    ILTShell *SHL_iShell;
    
    OggsPlaybackProcContext_t *pCtx;
    pCtx = (OggsPlaybackProcContext_t *)pClientData;
    if (!pCtx || !pCtx->pFile || !pCtx->pFile->API) {
        return;
    }

    hShell = pCtx->hShell;
    iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    SHL_iShell = lt_gethandleinterface(ILTShell, hShell);

    if (!iThread->IsTerminatePending(iThread->GetCurrentThread())) {
        LTMediaFormat fmt;
        LTDeviceMedia *deviceMedia = lt_openlibrary(LTDeviceMedia);
        fmt.nKind = kLTMediaKind_Audio;
        fmt.nEncoding = kLTMediaEncoding_Opus;
        fmt.params.opus.nChannels = 1;
        fmt.params.opus.nSampleRate =  kOpusPlaybackSampleRate;

        if (deviceMedia) {
            LTMediaSink sink = deviceMedia->OpenSink(&fmt);
            if (sink) {
                ILTMediaSink *iMediaSink = lt_gethandleinterface(ILTMediaSink, sink);
                u32 maxPacketSize = iMediaSink->GetMaxFrameByteSize(sink);
                iMediaSink->Start(sink);

                while (!iThread->IsTerminatePending(iThread->GetCurrentThread())) {

                    if (iMediaSink->GetAvailableFrameBuffers(sink) == 0) {
                        iThread->Sleep(LTTime_Milliseconds(kOpesPlaybackWaitTimeMs));
                        continue;
                    }
                    u8 *pPacket;
                    LTMediaData *packet = lt_malloc(sizeof(LTMediaData) + maxPacketSize);
                    packet->pData = (u8 *)(packet + 1);
                    packet->nTimestamp = 0;
                    packet->bEndOfFrame = 0;

                    if (!GetNextPacket(pCtx, &pPacket, &packet->nDataLen)) {
                        lt_free(packet);
                        break;
                    }
                    lt_memcpy(packet->pData, pPacket, packet->nDataLen);
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
    lt_free(pCtx->pPage);
    pCtx->pFile = NULL;
    pCtx->pPage = NULL;
}

void StopOpusPlayback(void) {
    ILTThread *iThread;
    iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    if (S.hPlaybackThread) {
        iThread->Terminate(S.hPlaybackThread);
        iThread->WaitUntilFinished(S.hPlaybackThread, LTTime_Infinite());
        lt_destroyhandle(S.hPlaybackThread);
        S.hPlaybackThread = 0;
    }
}

bool PlayOpusFile(LTShell hShell, LTFile *pFile) {
    ILTShell *SHL_iShell = lt_gethandleinterface(ILTShell, hShell);
    StopOpusPlayback();
    OpusHead_t *opusHead;
    u32 opusHeadSize;

    pFile->API->SeekToPosition(pFile, 0);
    S.oggPlaybackCtx.hShell = hShell;
    S.oggPlaybackCtx.pFile = pFile;
    S.oggPlaybackCtx.pPage = lt_malloc(sizeof(OggsPage_t));
    lt_memset(S.oggPlaybackCtx.pPage, 0, sizeof(OggsPage_t));

    if (GetNextPacket(&S.oggPlaybackCtx, (u8 **)&opusHead, &opusHeadSize)) {
        // The first packet must be OpusHead for Opus OGGs files
        if (lt_memcmp(opusHead->magic, OPUSHEADMAGIC, 8) == 0) {
            SHL_iShell->Print(hShell, "OpusHead found\n");
            if (opusHead->version == 1) {
                // Supported
                ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
                SHL_iShell->Print(hShell, "Channels: %d\n", opusHead->channels);
                SHL_iShell->Print(hShell, "Sample rate: %lu\n", LT_Pu32(opusHead->inputSampleRate));
                SHL_iShell->Print(hShell, "Pre-skip: %d\n", opusHead->preSkip);
                SHL_iShell->Print(hShell, "Output gain: %lu\n", LT_Pu32(opusHead->outputGain));
                SHL_iShell->Print(hShell, "Channel mapping family: %d\n", opusHead->channelMappingFamily);
                SHL_iShell->Print(hShell, "Stream count: %d\n", opusHead->streamCount);
                SHL_iShell->Print(hShell, "Coupled count: %d\n", opusHead->coupledCount);
                SHL_iShell->Print(hShell, "\n");

                S.hPlaybackThread = LT_GetCore()->CreateThread("opusplayback");
                iThread->Start(S.hPlaybackThread, NULL, NULL);
                iThread->QueueTaskProc(S.hPlaybackThread, OggSPlaybackProc, NULL, (void *)&S.oggPlaybackCtx);
                return true;
                SHL_iShell->Print(hShell, "Unsupported Opus version\n");
            }
        }
    } else {
        SHL_iShell->Print(hShell, "Failed to read OpusHead\n");
    }

    // Cleanup on failure
    StopOpusPlayback();

    return false;
}
