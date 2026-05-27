/*******************************************************************************
 * lt/source/example/audioplayback/ExampleAudioPlayback.c
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
static LTMediaSink    s_hSink      = 0;
static LTDeviceMedia *s_media      = NULL;

enum {
    // This is the time to sleep when the buffer is full, the exact value is
    // not critical
    kBufferFullWaitTimeMs = 16,
};

#define TEST_OUTPUT_SAMPLE_FREQUENCY 8000

#define AUD_SIN_MAX (float)(1<<12)
#if (8000 == TEST_OUTPUT_SAMPLE_FREQUENCY)
static const s16 sin_8steps[8] = {
    (s16)(AUD_SIN_MAX * 0.000000),  (s16)(AUD_SIN_MAX * 0.707107),
    (s16)(AUD_SIN_MAX * 1.000000),  (s16)(AUD_SIN_MAX * 0.707107),
    (s16)(AUD_SIN_MAX * 0.000000),  (s16)(AUD_SIN_MAX * -0.707107),
    (s16)(AUD_SIN_MAX * -1.000000), (s16)(AUD_SIN_MAX * -0.707107)
};
#else
static const s16 sin_16steps[16] = {
    (s16)(AUD_SIN_MAX * 0.000000),  (s16)(AUD_SIN_MAX * 0.382683),
    (s16)(AUD_SIN_MAX * 0.707107),  (s16)(AUD_SIN_MAX * 0.923880),
    (s16)(AUD_SIN_MAX * 1.000000),  (s16)(AUD_SIN_MAX * 0.923880),
    (s16)(AUD_SIN_MAX * 0.707107),  (s16)(AUD_SIN_MAX * 0.382683),
    (s16)(AUD_SIN_MAX * 0.000000),  (s16)(AUD_SIN_MAX * -0.382683),
    (s16)(AUD_SIN_MAX * -0.707107), (s16)(AUD_SIN_MAX * -0.923880),
    (s16)(AUD_SIN_MAX * -1.000000), (s16)(AUD_SIN_MAX * -0.923880),
    (s16)(AUD_SIN_MAX * -0.707107), (s16)(AUD_SIN_MAX * -0.382683)
};
#endif



/*******************************************************************************
 * Event Dispatch Procs
 ******************************************************************************/
static void ShutdownExampleAudioPlayback(void) {
    if (s_mainThread) lt_destroyhandle(s_mainThread);
    s_mainThread = 0;
    s_thread = NULL;
    if (s_media) lt_closelibrary(s_media);
    s_media = NULL;
    s_core = NULL;
}

static void PlayTask(void *pClientData) {
    LT_UNUSED(pClientData);

    do {
        // Check for free buffer
        ILTMediaSink *pSink = lt_gethandleinterface(ILTMediaSink, s_hSink);
        u32 fbs = pSink->GetAvailableFrameBuffers(s_hSink);
        //LT_GetCore()->ConsolePrint("Frame Buffers = %d x %d\n", fbs, pSink->GetMaxFrameByteSize(s_hSink) );
        if (fbs) {
            LT_SIZE sinkFrameSize = pSink->GetMaxFrameByteSize(s_hSink);
            LTMediaData *databuf = lt_malloc(sizeof(LTMediaData) + sinkFrameSize);
            s16 *pPcm;
            LT_SIZE i,nSamples;
            if (!databuf) {
                lt_consoleprint("Cannot allocate buffer");
                break;
            }

            databuf->pData = (u8 *)(databuf + 1);
            databuf->nDataLen = sinkFrameSize;
            databuf->nTimestamp = 0;
            databuf->bEndOfFrame = 0;

            /* write the sin() values */
            pPcm = (s16 *)databuf->pData;
            nSamples = sinkFrameSize >> 1;
            for ( i = 0; i < nSamples; i++ ) {
              #if (8000 == TEST_OUTPUT_SAMPLE_FREQUENCY)
                pPcm[i] = sin_8steps[ i & 0x7 ];
              #else
                pPcm[i] = sin_16steps[ i & 0xf ];
              #endif
            }

            /* send to output */
            pSink->HandleMediaData(s_hSink, databuf);
        }
        return;
    } while(0);

    ShutdownExampleAudioPlayback();
}

static bool OnThreadStart(void) {
    // Only support 16kHz, 16-bit, mono PCM
    LTMediaFormat format = {
        .nKind = kLTMediaKind_Audio,
        .nEncoding = kLTMediaEncoding_PCM,
        .params.pcm.nSampleRate = TEST_OUTPUT_SAMPLE_FREQUENCY,
        .params.pcm.nBitsPerSample = 16,
        .params.pcm.nChannels = 1
    };
    // Confirm the media library supports the format
    if (!s_media->SupportsFormat(&format)) {
        lt_consoleprint("Audio play format not supported");
        return false;
    }

    s_hSink = s_media->OpenSink(&format);
    ILTMediaSink *pSink = lt_gethandleinterface(ILTMediaSink, s_hSink);
    if (!pSink) {
        lt_consoleprint("Audio play open sink failed");
        return false;
    }
    pSink->Start(s_hSink);

    s_thread->SetTimer(s_thread->GetCurrentThread(),
                        LTTime_Milliseconds(kBufferFullWaitTimeMs),
                        PlayTask,
                        NULL,
                        NULL);

    return true;
}

static void OnThreadExit(void) {
    ILTMediaSink *pSink = lt_gethandleinterface(ILTMediaSink, s_hSink);
    if (pSink) {
        pSink->Stop(s_hSink, true);
        lt_destroyhandle(s_hSink);
    }
}

static bool ExampleAudioPlaybackImpl_LibInit(void) {
    s_core   = LT_GetCore();
    s_thread = lt_getlibraryinterface(ILTThread, s_core);

    do {
        s_media = lt_openlibrary(LTDeviceMedia);
        if (!s_media) break;

        return true;
    } while (0);

    ShutdownExampleAudioPlayback();
    return false;
}

static void ExampleAudioPlaybackImpl_LibFini(void) {
    ShutdownExampleAudioPlayback();
}

static int ExampleAudioPlayback_Run(int argc, const char **argv) {
    if (argc < 2) {
        LT_GetCore()->ConsolePrint(
            "Usage: %s sin <nsec>\n"
            "  sin:  generate a sine wave\n"
            "  nsec:  number of seconds to play\n",
            argv[0]
        );
        return 1;
    }

    s_mainThread = s_core->CreateThread("AudioPlayTest");
    if (!s_mainThread) return 0;
    s_thread = lt_getlibraryinterface(ILTThread, s_core);
    s_thread->SetStackSize(s_mainThread, 2048);
    s_thread->Start(s_mainThread, OnThreadStart, OnThreadExit);
    s_thread->WaitUntilFinished(s_mainThread, LTTime_Infinite());
    lt_destroyhandle(s_mainThread);
    s_mainThread = 0;
    return 0;
}

typedef_LTLIBRARY_ROOT_INTERFACE(ExampleAudioPlayback, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(ExampleAudioPlayback, ExampleAudioPlayback_Run, 8192) LTLIBRARY_DEFINITION;

