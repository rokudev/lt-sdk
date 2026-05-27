/*******************************************************************************
 * lt/lt/source/lt/driver/media/audio/opus/Decoder.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * Linux LT Driver Library for AudioOutput functions
 ******************************************************************************/
/** @file AudioOutput.c Implementation of audio output for Ingenic/Linux
 */

#include <lt/core/LTCore.h>
#include <lt/device/media/LTDeviceMedia.h>

DEFINE_LTLOG_SECTION("opus.dec");

typedef struct {
    LTHandle             hHandle;
    u32                  nSampleRate;
} LTMediaSinkContext;

static ILTMediaSink     s_ILTMediaSink;
static LTCore          *s_pCore;

/*******************************************************************************
 * ILTDriverAudioOutput implementation                                        */

static void
LTAudioSink_Start(LTMediaSink hSink) {
    LT_UNUSED(hSink);
}

static void
LTAudioSink_Stop(LTMediaSink hSink, bool discardData) {
    LT_UNUSED(hSink);
    LT_UNUSED(discardData);
}

static LTMediaEncoding
LTAudioSink_GetMediaEncoding(LTMediaSink hSink) {
    LT_UNUSED(hSink);
    return kLTMediaEncoding_Opus;
}

static bool
LTAudioSink_HandleMediaData(LTMediaSink hSink, LTMediaData * pPacket) {
    LT_UNUSED(hSink);
    lt_free(pPacket);
    return true;
}

static LT_SIZE LTAudioSink_GetMaxFrameByteSize(LTMediaSink hSink) {
    LT_UNUSED(hSink);
    return 0;
}

static u32 LTAudioSink_GetAvailableFrameBuffers(LTMediaSink hSink) {
    LT_UNUSED(hSink);
    return 1;
}

LTMediaSink
OpusDecoder_Open(LTMediaFormat * pFormat) {
    if (!s_pCore)   s_pCore = LT_GetCore();

    if (pFormat->nEncoding != kLTMediaEncoding_Opus) {
        LTLOG_YELLOWALERT("fmt.unsupp", "unsupported format encoding %lu", LT_Pu32(pFormat->nEncoding));
        return 0;
    }

    LTMediaSink hSink = s_pCore->CreateHandle((LTInterface *)&s_ILTMediaSink, sizeof(LTMediaSinkContext));
    if (!hSink) {
        LTLOG_YELLOWALERT("handle.err", "Unable to allocate handle");
        return 0;
    }
    LTMediaSinkContext * pSink = s_pCore->ReserveHandlePrivateData(hSink);
    lt_memset(pSink, 0, sizeof(LTMediaSinkContext));

    pSink->hHandle      = hSink;
    pSink->nSampleRate  = pFormat->params.opus.nSampleRate;
    s_pCore->ReleaseHandlePrivateData(hSink, pSink);
    return hSink;
}

static void
LTAudioSink_Destroy(LTMediaSink hSink) {
    LTMediaSinkContext * pSink = s_pCore->ReserveHandlePrivateData(hSink);
    if (!pSink) return;
    LTAudioSink_Stop(hSink, true);
    s_pCore->ReleaseHandlePrivateData(hSink, pSink);
}

/*__________________________________________
/ ILTMediaSink library interface binding */
define_LTLIBRARY_INTERFACE(ILTMediaSink, LTAudioSink_Destroy) {
    .GetMediaEncoding           = &LTAudioSink_GetMediaEncoding,
    .HandleMediaData            = &LTAudioSink_HandleMediaData,
    .Start                      = &LTAudioSink_Start,
    .Stop                       = &LTAudioSink_Stop,
    .GetMaxFrameByteSize        = &LTAudioSink_GetMaxFrameByteSize,
    .GetAvailableFrameBuffers   = &LTAudioSink_GetAvailableFrameBuffers,
} LTLIBRARY_DEFINITION;
