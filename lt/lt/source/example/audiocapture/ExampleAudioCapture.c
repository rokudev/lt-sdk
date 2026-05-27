/*******************************************************************************
 * lt/source/example/media/ExampleAudioCapture.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/device/media/LTDeviceMedia.h>

/****************************************************************************/
/* Consts                                                                   */
/****************************************************************************/

static ILTThread     *s_thread     = NULL;
static LTCore        *s_core       = NULL;
static LTThread       s_mainThread = 0;
static LTMediaSource  s_hSource    = 0;
static LTDeviceMedia *s_media      = NULL;
/*******************************************************************************
 * Event Dispatch Procs
 ******************************************************************************/

enum {
    kAudioBuffersToAnalyze = 64
};
static u32 audAnalysisBuffers = 0;
static u8 audAnalysisChars[kAudioBuffersToAnalyze+1];

static void
OnMediaEvent(LTMediaEvent event, void *eventData, void * pClientData) {
    LT_UNUSED(pClientData);
    if (event == kLTMediaEvent_Data) {
        LTMediaData *mediaData = eventData;
        s16 *pPcm,nPcmMax;
        u32  i,nSamples;
        nPcmMax = 0;
        nSamples = mediaData->nDataLen >> 1;
        pPcm = (s16 *) mediaData->pData;
        /* get absolute maximum value */
        for ( i = 0; i < nSamples; i++ ) {
            if ( pPcm[i] >= 0 ) {
                if ( nPcmMax < pPcm[i] ) {
                    nPcmMax = pPcm[i];
                }
            } else {
                if ( nPcmMax < -pPcm[i] ) {
                    nPcmMax = -pPcm[i];
                }
            }
        }

        /* write the char to the buffer */
        audAnalysisChars[audAnalysisBuffers] = 'a';
        while ( nPcmMax ) {
            audAnalysisChars[audAnalysisBuffers]++;
            nPcmMax >>= 1;
        }

        /* perform wrapping loop */
        audAnalysisBuffers = (audAnalysisBuffers+1) & (kAudioBuffersToAnalyze-1);

        /* Once we've analyzed kAudioBuffersToAnalyze, spit out a line with the results */
        if ( (kAudioBuffersToAnalyze-1) == (audAnalysisBuffers & ((kAudioBuffersToAnalyze-1))) ) {
            LT_GetCore()->ConsolePrint("Audio:[%s] [%.4x %.4x ... %.4x %.4x]\n", 
                audAnalysisChars,
                (u16)pPcm[0],(u16)pPcm[1], (u16)pPcm[nSamples-2],(u16)pPcm[nSamples-1]  );
        }
    }
}

static void OnTimeout(void *data) {
    LT_UNUSED(data);
    s_thread->KillTimer(s_mainThread, OnTimeout, NULL);
    s_thread->Terminate(s_mainThread);
}

static bool OnThreadStart(void) {
    LTMediaFormat format = {
        .nKind = kLTMediaKind_Audio,
        .nEncoding = kLTMediaEncoding_PCM,
        .params.pcm.nSampleRate = 8000,
        .params.pcm.nChannels = 1
    };
    s_hSource = s_media->OpenSource(&format);
    ILTMediaSource *pSource = lt_gethandleinterface(ILTMediaSource, s_hSource);
    if (!pSource) {
        lt_consoleprint("audio cap open source failed");
        return false;
    }
    pSource->OnMediaEvent(s_hSource, OnMediaEvent, NULL);
    pSource->Start(s_hSource);
    return true;
}

static void OnThreadExit(void) {
    ILTMediaSource *pSource = lt_gethandleinterface(ILTMediaSource, s_hSource);
    if (pSource) {
        pSource->Stop(s_hSource);
        pSource->NoMediaEvent(s_hSource, OnMediaEvent);
        lt_destroyhandle(s_hSource);
    }
}

static void ShutdownExampleAudioCapture(void) {
    if (s_mainThread) lt_destroyhandle(s_mainThread);
    s_mainThread = 0;
    s_thread = NULL;
    if (s_media) lt_closelibrary(s_media);
    s_media = NULL;
    s_core = NULL;
}

static bool ExampleAudioCaptureImpl_LibInit(void) {
    s_core   = LT_GetCore();
    s_thread = lt_getlibraryinterface(ILTThread, s_core);

    do {
        s_media = lt_openlibrary(LTDeviceMedia);
        if (!s_media) break;

        return true;
    } while (0);

    ShutdownExampleAudioCapture();
    return false;
}

static void ExampleAudioCaptureImpl_LibFini(void) {
    ShutdownExampleAudioCapture();
}

static int ExampleAudioCapture_Run(int argc, const char **argv) {
    if (argc < 2) {
        LT_GetCore()->ConsolePrint(
            "Usage: %s nSec\n"
            "  nSec:        number of seconds to capture data\n",
            argv[1]
        );
        return 1;
    }

    s_mainThread = s_core->CreateThread("AudioCapTest");
    if (!s_mainThread) return 0;
    s_thread = lt_getlibraryinterface(ILTThread, s_core);
    s_thread->SetStackSize(s_mainThread, 2048);
    s_thread->Start(s_mainThread, OnThreadStart, OnThreadExit);
    s_thread->SetTimer(s_mainThread, LTTime_Seconds(lt_strtou32(argv[2], NULL, 10)), OnTimeout, NULL, NULL);
    s_thread->WaitUntilFinished(s_mainThread, LTTime_Infinite());
    lt_destroyhandle(s_mainThread);
    s_mainThread = 0;
    return 0;
}

typedef_LTLIBRARY_ROOT_INTERFACE(ExampleAudioCapture, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(ExampleAudioCapture, ExampleAudioCapture_Run, 8192) LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  29-Feb-24   valerian    created
 */
