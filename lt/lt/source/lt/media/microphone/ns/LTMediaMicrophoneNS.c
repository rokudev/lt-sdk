/*******************************************************************************
 * lt/media/microphone/ns/LTMediaMicrophoneNS.c
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/media/microphone/LTMediaMicrophoneNS.h>
#include "noise_suppression_x.h"

typedef_LTObjectImpl(LTMediaMicrophoneNS, LTMediaMicrophoneNSImpl) {
    NsxHandle *ns;
} LTOBJECT_API;

/* implementation of LTMediaMicrophoneNS */

static void LTMediaMicrophoneNSImpl_Process(LTMediaMicrophoneNSImpl *pNs,
                           const s16 *const *inFrame,
                           int numBands,
                           s16 *const *outFrame) {
    WebRtcNsx_Process(pNs->ns, inFrame, numBands, outFrame);
}

static int LTMediaMicrophoneNSImpl_SetPolicy(LTMediaMicrophoneNSImpl *pNs, LTMediaMicrophoneNS_Mode mode) {
    return WebRtcNsx_set_policy(pNs->ns, (int)mode);
}

static s32 LTMediaMicrophoneNSImpl_Init(LTMediaMicrophoneNSImpl *pNs, u32 fs) {
    return WebRtcNsx_Init(pNs->ns, fs);
}

static void LTMediaMicrophoneNSImpl_DestructObject(LTMediaMicrophoneNSImpl *pNs) {
    if (pNs->ns) {
        WebRtcNsx_Free(pNs->ns);
        pNs->ns = NULL;
    }
}

static bool LTMediaMicrophoneNSImpl_ConstructObject(LTMediaMicrophoneNSImpl *pNs) {
    pNs->ns = WebRtcNsx_Create();
    return !!(pNs->ns);
}

/* LTMediaMicrophoneNSImpl definition */
define_LTObjectImplPublic(LTMediaMicrophoneNS, LTMediaMicrophoneNSImpl,
    Process,
    SetPolicy,
    Init
);
