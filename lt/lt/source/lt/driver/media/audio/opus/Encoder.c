/*******************************************************************************
 * lt/source/lt/driver/media/audio/opus/Encoder.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * Linux LT Driver Library for AudioInput functions
 ******************************************************************************/
/** @file Encoder.c Implementation of Opus audio encoder
 */

#include <lt/core/LTCore.h>
#include <lt/device/media/LTDeviceMedia.h>

DEFINE_LTLOG_SECTION("opus.enc");

typedef struct {
    LTHandle             hHandle;
    LTEvent              hEvent;
} LTMediaSourceContext;

static ILTMediaSource    s_ILTMediaSource;
static ILTEvent         *s_event;

/*******************************************************************************
 * ILTDriverAudioCapture implementation                                        */

static ILTMediaSource s_ILTMediaSource;

static void
LTAudioSource_OnMediaEvent(LTMediaSource hSource, LTMediaSource_OnMediaEventProc * pCallback, void * pClientData) {
    LT_UNUSED(hSource);
    LT_UNUSED(pCallback);
    LT_UNUSED(pClientData);
}

static void
LTAudioSource_NoMediaEvent(LTMediaSource hSource, LTMediaSource_OnMediaEventProc * pCallback) {
    LT_UNUSED(hSource);
    LT_UNUSED(pCallback);
}

static void
LTAudioSource_OnModeChangeEvent(LTMediaSource hSource, LTMediaSource_OnModeChangeEventProc *pCallback, void *pClientData) {
    /* No mode change events available for this driver */
    LT_UNUSED(hSource);
    LT_UNUSED(pCallback);
    LT_UNUSED(pClientData);
}

static void
LTAudioSource_NoModeChangeEvent(LTMediaSource hSource, LTMediaSource_OnModeChangeEventProc *pCallback) {
    /* No mode change events available for this driver */
    LT_UNUSED(hSource);
    LT_UNUSED(pCallback);
}

static LTMediaEncoding
LTAudioSource_GetMediaEncoding(LTMediaSource hSource) {
    LT_UNUSED(hSource);
    return kLTMediaEncoding_Opus;
}

static void
LTAudioSource_Start(LTMediaSource hSource) {
    LT_UNUSED(hSource);
}

static void
LTAudioSource_Stop(LTMediaSource hSource) {
    LT_UNUSED(hSource);
}

static void
LTAudioSource_SetTargetBitrate(LTMediaSource hSource, u32 nTargetBitrate) {
    LT_UNUSED(hSource);
    LT_UNUSED(nTargetBitrate);
}

static void
LTAudioSource_SetGopLength(LTMediaSource hSource, u32 nGopLength) {
    LT_UNUSED(hSource);
    LT_UNUSED(nGopLength);
}

LTMediaSource
OpusEncoder_Open(LTMediaFormat * pFormat) {
    if (pFormat->nEncoding != kLTMediaEncoding_Opus) {
        LTLOG_YELLOWALERT("format.err", "Audio format unsupported %lu", LT_Pu32(pFormat->nEncoding));
        return 0;
    }
    LTMediaSource hSource = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTMediaSource, sizeof(LTMediaSourceContext));
    if (!hSource) return 0;
    LTMediaSourceContext *source = LT_GetCore()->ReserveHandlePrivateData(hSource);
    lt_memset(source, 0, sizeof(LTMediaSourceContext));
    source->hHandle = hSource;
    LT_GetCore()->ReleaseHandlePrivateData(hSource, source);
    return hSource;
}

static void
LTAudioSource_Destroy(LTMediaSource hSource) {
    LTAudioSource_Stop(hSource);
}

/*____________________________________________
/ ILTMediaSource library interface binding */
define_LTLIBRARY_INTERFACE(ILTMediaSource, LTAudioSource_Destroy) {
    .GetMediaEncoding  = &LTAudioSource_GetMediaEncoding,
    .OnMediaEvent      = &LTAudioSource_OnMediaEvent,
    .NoMediaEvent      = &LTAudioSource_NoMediaEvent,
    .OnModeChangeEvent = &LTAudioSource_OnModeChangeEvent,
    .NoModeChangeEvent = &LTAudioSource_NoModeChangeEvent,
    .Start             = &LTAudioSource_Start,
    .Stop              = &LTAudioSource_Stop,
    .SetTargetBitrate  = &LTAudioSource_SetTargetBitrate,
    .SetGopLength      = &LTAudioSource_SetGopLength,
} LTLIBRARY_DEFINITION;

bool
OpusEncoder_Init(void) {
    if (!s_event)   s_event = lt_getlibraryinterface(ILTEvent, LT_GetCore());
    return true;
}

void
OpusEncoder_Destroy(void) {
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  18-Dec-23   trajan      created
 */
