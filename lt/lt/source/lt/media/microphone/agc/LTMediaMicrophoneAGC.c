/*******************************************************************************
 * lt/media/microphone/agc/LTMediaMicrophoneAGC.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/media/microphone/LTMediaMicrophoneAGC.h>
#include "gain_control.h"
#include "analog_agc.h"

typedef_LTObjectImpl(LTMediaMicrophoneAGC, LTMediaMicrophoneAGCImpl) {
    LegacyAgc *lAgc;
} LTOBJECT_API;

/* Implementation of LTMediaMicrophoneAGC */

static void LTMediaMicrophoneAGCImpl_SetMicVol(LTMediaMicrophoneAGCImpl *agc, s32 micVol) {
    WebRtcAgc_SetMicVol(agc->lAgc, micVol);
}

static int LTMediaMicrophoneAGCImpl_GetAddFarEndError(LTMediaMicrophoneAGCImpl *agc, LT_SIZE samples) {
    return WebRtcAgc_GetAddFarendError(agc->lAgc, samples);
}

static int LTMediaMicrophoneAGCImpl_AddFarEnd(LTMediaMicrophoneAGCImpl *agc, const s16 *inFar, LT_SIZE samples) {
    return WebRtcAgc_AddFarend(agc->lAgc, inFar, samples);
}

static int LTMediaMicrophoneAGCImpl_AddMic(LTMediaMicrophoneAGCImpl *agc,
                     s16 *const *inMic,
                     LT_SIZE numBands,
                     LT_SIZE samples) {
    return WebRtcAgc_AddMic(agc->lAgc, inMic, numBands, samples);
}

static int LTMediaMicrophoneAGCImpl_VirtualMic(LTMediaMicrophoneAGCImpl *agc,
                         s16 *const *inMic,
                         LT_SIZE numBands,
                         LT_SIZE samples,
                         s32 micLevelIn,
                         s32 *micLevelOut) {
    return WebRtcAgc_VirtualMic(agc->lAgc, inMic, numBands, samples, micLevelIn, micLevelOut);
}

static int LTMediaMicrophoneAGCImpl_Analyze(LTMediaMicrophoneAGCImpl *agc,
                      const s16 *const *inNear,
                      LT_SIZE numBands,
                      LT_SIZE samples,
                      s32 inMicLevel,
                      s32 *outMicLevel,
                      s16 echo,
                      uint8_t *saturationWarning,
                      s32 gains[11]) {
    return WebRtcAgc_Analyze(agc->lAgc, inNear, numBands, samples, inMicLevel, outMicLevel, echo, saturationWarning, gains);
}

static int LTMediaMicrophoneAGCImpl_Process(const LTMediaMicrophoneAGCImpl *agc,
                      const s32 gains[11],
                      const s16 *const *inNear,
                      LT_SIZE numBands,
                      s16 *const *out) {
    return WebRtcAgc_Process(agc->lAgc, gains, inNear, numBands, out);
}

static int LTMediaMicrophoneAGCImpl_SetConfig(LTMediaMicrophoneAGCImpl *agc, LTMediaMicrophoneAGC_Config config) {
    return WebRtcAgc_set_config(agc->lAgc, config);
}

static int LTMediaMicrophoneAGCImpl_GetConfig(LTMediaMicrophoneAGCImpl *agc, LTMediaMicrophoneAGC_Config *config) {
    return WebRtcAgc_get_config(agc->lAgc, config);
}

static int LTMediaMicrophoneAGCImpl_Init(LTMediaMicrophoneAGCImpl *agc,
                   s32 minLevel,
                   s32 maxLevel,
                   LTMediaMicrophoneAGC_Mode agcMode,
                   u32 fs) {
    return WebRtcAgc_Init(agc->lAgc, minLevel, maxLevel, agcMode, fs);
}

static void LTMediaMicrophoneAGCImpl_DestructObject(LTMediaMicrophoneAGCImpl *agc) {
    WebRtcAgc_Free(agc->lAgc);
    agc->lAgc = NULL;
}

static bool LTMediaMicrophoneAGCImpl_ConstructObject(LTMediaMicrophoneAGCImpl *agc) {
    agc->lAgc = WebRtcAgc_Create();
    return agc->lAgc == NULL ? false : true;
}

define_LTObjectImplPublic(LTMediaMicrophoneAGC, LTMediaMicrophoneAGCImpl,
    SetMicVol,
    GetAddFarEndError,
    AddFarEnd,
    AddMic,
    VirtualMic,
    Analyze,
    Process,
    SetConfig,
    GetConfig,
    Init
);

