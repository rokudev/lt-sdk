/*******************************************************************************
 *
 * Implementation of Tone playback command from LT Media Shell
 * -----------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include "LTShellMediaPlayTone.h"
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/system/shell/LTSystemShell.h>

enum {
    kAvailableFrameWaitTimeMs = 20,
};

typedef struct {
    LTShell hShell;
} PlayToneProcContext_t;

static struct Statics {
   LTThread hPlayToneThread;
   PlayToneProcContext_t playbackCtx;
} S;

static u32 ToneGenerator(u8 *pData, u32 index, LT_SIZE nDataLen) {
    /* 1KHz */
    const s16 sinWave[] = {     0,  12539,  23170,  30273,  32767,  30273,  23170,  12539, 
                                0, -12539, -23170, -30273, -32767, -30273, -23170, -12539 };
    
    // /* 500 Hz*/
    // const s16 sinWave[] = {     0,   6393,  12539,  18204,  23170,  27245,  30273,  32137, 
    //                         32767,  32137,  30273,  27245,  23170,  18204,  12539,   6393, 
    //                             0,  -6393, -12539, -18204, -23170, -27245, -30273, -32137,
    //                        -32767, -32137, -30273, -27245, -23170, -18204, -12539,  -6393};

    for (LT_SIZE i = 0; i < nDataLen/2; i++) {
        ((s16*)pData)[i] = (sinWave[index]);
        index = (index + 1) % (sizeof(sinWave) / sizeof(sinWave[0]));
    }
    return index;
}

static void TonePlaybackProc(void *pClientData) {
    ILTThread *iThread;
    LTShell hShell;
    ILTShell *SHL_iShell;
    
    PlayToneProcContext_t *pCtx;
    pCtx = (PlayToneProcContext_t *)pClientData;
    if (!pCtx) {
        return;
    }

    hShell = pCtx->hShell;
    iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    SHL_iShell = lt_gethandleinterface(ILTShell, hShell);

    if (!iThread->IsTerminatePending(iThread->GetCurrentThread())) {
        LTDeviceMedia *deviceMedia = lt_openlibrary(LTDeviceMedia);
        LTMediaFormat fmt;

        fmt.nKind = kLTMediaKind_Audio;
        fmt.nEncoding = kLTMediaEncoding_PCM;
        fmt.params.pcm.nSampleRate = 16000;
        fmt.params.pcm.nBitsPerSample = 16;
        fmt.params.pcm.nChannels = 1;

        if (deviceMedia) {
            LTMediaSink sink = deviceMedia->OpenSink(&fmt);
            if (sink) {
                ILTMediaSink *iMediaSink = lt_gethandleinterface(ILTMediaSink, sink);
                u32 maxPacketSize = iMediaSink->GetMaxFrameByteSize(sink);
                iMediaSink->Start(sink);

                while (!iThread->IsTerminatePending(iThread->GetCurrentThread())) {
                    u32 toneGenIdx = 0;

                    if (iMediaSink->GetAvailableFrameBuffers(sink) == 0) {
                        iThread->Sleep(LTTime_Milliseconds(kAvailableFrameWaitTimeMs));
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
                    toneGenIdx = ToneGenerator(packet->pData, toneGenIdx, maxPacketSize);
                    packet->nDataLen = maxPacketSize;
                    iMediaSink->HandleMediaData(sink, packet);
                }
                iMediaSink->Stop(sink, iThread->IsTerminatePending(iThread->GetCurrentThread()));
                lt_destroyhandle(sink);
            }
            lt_closelibrary(deviceMedia);
        }
    }

    // cleanup
    SHL_iShell->Print(hShell, "Tone playback complete\n");
}

void StopTone(void) {
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    if (S.hPlayToneThread) {
        iThread->Terminate(S.hPlayToneThread);
        iThread->WaitUntilFinished(S.hPlayToneThread, LTTime_Infinite());
        lt_destroyhandle(S.hPlayToneThread);
        S.hPlayToneThread = 0;
    }
}

bool PlayTone(LTShell hShell) {
    ILTShell *SHL_iShell = lt_gethandleinterface(ILTShell, hShell);
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    StopTone();

    S.playbackCtx.hShell = hShell;

    S.hPlayToneThread = LT_GetCore()->CreateThread("toneplayback");
    if (!S.hPlayToneThread) {
        SHL_iShell->Print(hShell, "Failed to create playback thread\n");
        return false;
    }
    iThread->Start(S.hPlayToneThread, NULL, NULL);
    iThread->QueueTaskProc(S.hPlayToneThread, TonePlaybackProc, NULL, (void *)&S.playbackCtx);
    return true;
}
