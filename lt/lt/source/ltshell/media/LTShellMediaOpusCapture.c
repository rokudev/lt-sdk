/*******************************************************************************
 *
 * Implementation of Opus Capture command from LT Media Shell
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

enum {
    kOggsCaptureSampleRate = 16000,
};

typedef struct {
    LTShell hShell;
    LTFile *pFile;
    OggsPage_t *pPage;
    LTDeviceMedia *deviceMedia;
    LTMediaSource hSource;
    u32 captureSerial;
    u32 nPageSequence;
    u32 nPageGranulePos;
    u64 nTotalGranulePos;
} OpusRecordingContext_t;

struct Statics {
    OpusRecordingContext_t opusRecordingCtx;
} S;

static void HandleMediaData(LTMediaEvent hEvent, void *eventData, void *pClientData) {
    OpusRecordingContext_t *pCtx = (OpusRecordingContext_t *)pClientData;
    ILTShell *iShell;
    LTMediaData *pMediaData = (LTMediaData *)eventData;

    if (hEvent == kLTMediaEvent_Data) {
        if (!pCtx || !pCtx->pFile || !pCtx->pPage) {
            return;
        }
        iShell = lt_gethandleinterface(ILTShell, pCtx->hShell);
        do {
            if (OggSGetAvailablePageSpace(pCtx->pPage) < pMediaData->nDataLen ||
                pCtx->nPageGranulePos > kOggsGranulePos1Second) { // 1 second per page for easy seeking
                if (!OggSWritePageToFile(pCtx->pFile, pCtx->pPage)) {
                    iShell->Print(pCtx->hShell, "Failed to write page\n");
                    break;
                }
                pCtx->nPageSequence++;
                pCtx->nTotalGranulePos += pCtx->nPageGranulePos;
                pCtx->nPageGranulePos = 0;
                OggSInitPage(S.opusRecordingCtx.pPage, pCtx->captureSerial, pCtx->nPageSequence, pCtx->nTotalGranulePos);
            }

            if (!OggSSetPacketToPage(pCtx->pPage, pMediaData->pData, pMediaData->nDataLen, pMediaData->meta.audio.nEncodedSamples)) {
                iShell->Print(pCtx->hShell, "Failed to set packet to page\n");
                break;
            }
            pCtx->nPageGranulePos += pMediaData->meta.audio.nEncodedSamples * kOggsGranulePos1Second / kOggsCaptureSampleRate;
            return;
        } while (0);

        // Error
        StopOpusCapture();
    }
}

void StopOpusCapture(void) {
    if (S.opusRecordingCtx.hSource) {
        ILTMediaSource *iSource = lt_gethandleinterface(ILTMediaSource, S.opusRecordingCtx.hSource);
        iSource->Stop(S.opusRecordingCtx.hSource);
        lt_destroyhandle(S.opusRecordingCtx.hSource);
        S.opusRecordingCtx.hSource = 0;
    }
    lt_closelibrary(S.opusRecordingCtx.deviceMedia);
    S.opusRecordingCtx.deviceMedia = NULL;

    if (S.opusRecordingCtx.pFile) {
        if (S.opusRecordingCtx.pPage->header.pageSegments) {
            S.opusRecordingCtx.pPage->header.flags |= kOpusHeaderFlagLastPage; // Last page
            OggSWritePageToFile(S.opusRecordingCtx.pFile, S.opusRecordingCtx.pPage);
        }
        lt_destroyobject(S.opusRecordingCtx.pFile);
        S.opusRecordingCtx.pFile = NULL;
    }
    lt_free(S.opusRecordingCtx.pPage);
    S.opusRecordingCtx.pPage = NULL;
}

bool StartOpusCapture(LTShell hShell, LTFile *pFile) {
    ILTMediaSource *iSource;
    ILTShell *SHL_iShell = lt_gethandleinterface(ILTShell, hShell);
    OpusHead_t opusHead;
    LTMediaFormat fmt;
    if (S.opusRecordingCtx.pFile) {
        SHL_iShell->Print(hShell, "Capture already in progress\n");
        return false;
    }

    S.opusRecordingCtx.hShell = hShell;
    S.opusRecordingCtx.pPage = lt_malloc(sizeof(OggsPage_t));
    if (!S.opusRecordingCtx.pPage) {
        SHL_iShell->Print(hShell, "Failed to allocate page\n");
        return false;
    }

    LT_ASSERT(sizeof(opusHead.magic) == sizeof(OPUSHEADMAGIC)-1);
    lt_memcpy(opusHead.magic, OPUSHEADMAGIC, sizeof(opusHead.magic));
    opusHead.version = 1;
    opusHead.channels = 1;
    opusHead.preSkip = 0;
    opusHead.inputSampleRate = kOggsCaptureSampleRate;
    opusHead.outputGain = 0;
    opusHead.channelMappingFamily = 0;
    opusHead.streamCount = 1;
    opusHead.coupledCount = 0;
    opusHead.channelMapping[0] = 0;

    S.opusRecordingCtx.captureSerial = 0x12345678;
    S.opusRecordingCtx.nPageSequence = 2; //(page 0 is the OpusHead, page 1 is OpusTags)
    S.opusRecordingCtx.nPageGranulePos = 0;
    S.opusRecordingCtx.nTotalGranulePos = 0;
    if (!OggSInitFile(pFile, S.opusRecordingCtx.captureSerial, &opusHead)) {
        SHL_iShell->Print(hShell, "Failed to initialize file\n");
        lt_free(S.opusRecordingCtx.pPage);
        S.opusRecordingCtx.pPage = NULL;
        return false;
    }
    OggSInitPage(S.opusRecordingCtx.pPage, S.opusRecordingCtx.captureSerial, S.opusRecordingCtx.nPageSequence, S.opusRecordingCtx.nTotalGranulePos);

    S.opusRecordingCtx.pFile = pFile;

    fmt.nKind = kLTMediaKind_Audio;
    fmt.nEncoding = kLTMediaEncoding_Opus;
    fmt.params.opus.nChannels = 1;
    fmt.params.opus.nSampleRate = kOggsCaptureSampleRate;
    S.opusRecordingCtx.deviceMedia = lt_openlibrary(LTDeviceMedia);
    if (!S.opusRecordingCtx.deviceMedia) {
        SHL_iShell->Print(hShell, "Failed to open device media\n");
        StopOpusCapture();
        return false;
    }
    S.opusRecordingCtx.hSource = S.opusRecordingCtx.deviceMedia->OpenSource(&fmt);
    if (!S.opusRecordingCtx.hSource) {
        SHL_iShell->Print(hShell, "Failed to open audio source\n");
        StopOpusCapture();
        return false;
    }

    iSource = lt_gethandleinterface(ILTMediaSource, S.opusRecordingCtx.hSource);
    iSource->OnMediaEvent(S.opusRecordingCtx.hSource, HandleMediaData, &S.opusRecordingCtx);
    iSource->Start(S.opusRecordingCtx.hSource);

    SHL_iShell->Print(hShell, "Capture started\n");
    return true;
}

