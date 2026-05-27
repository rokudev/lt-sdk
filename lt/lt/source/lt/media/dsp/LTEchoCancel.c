/*******************************************************************************
 * source/lt/media/dsp/LTEchoCancel.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/media/dsp/LTEchoCancel.h>
#include "speex/speex_echo.h"
#include "speex/speex_preprocess.h"
//DEFINE_LTLOG_SECTION("echo.cancel");

/* LTEchoCancel object stubs  */
typedef_LTObjectImpl(LTEchoCancel,LTEchoCancelImpl) {
    SpeexEchoState *echostate;
    SpeexPreprocessState *denoise;
} LTOBJECT_API;

/* pre-declaration */
static void LTEchoCancelImpl_DestroyState(LTEchoCancelImpl * echo);

/* implementation of LTEchoCancel */

static bool LTEchoCancelImpl_ConstructObject(LTEchoCancelImpl * echo)
{
    bool retval = true;
    LT_UNUSED(echo);

    return retval;
}

static void LTEchoCancelImpl_DestructObject(LTEchoCancelImpl * echo)
{
    LTEchoCancelImpl_DestroyState(echo);
}

static bool LTEchoCancelImpl_InitState(LTEchoCancelImpl * echo, u16 frame_size, u16 filter_length, u32 sample_rate)
{
    /* destroy old echo state if it exists */
    LTEchoCancelImpl_DestroyState(echo);

    do {
        /* create a new one */
        if ( NULL == (echo->echostate = speex_echo_state_init(frame_size,filter_length) )) break;
        speex_echo_ctl(echo->echostate,SPEEX_ECHO_SET_SAMPLING_RATE,&sample_rate);
        
        if ( NULL == (echo->denoise = speex_preprocess_state_init(frame_size, sample_rate) )) break;
        speex_preprocess_ctl(echo->denoise, SPEEX_PREPROCESS_SET_ECHO_STATE, echo->echostate);

        speex_echo_state_reset(echo->echostate);
        return true;
    } while(false);

    return false;
}

static void LTEchoCancelImpl_GetConfig(LTEchoCancelImpl * echo, u16 *pFrame_size, u32 *pSample_rate)
{
    LT_UNUSED(pFrame_size);
    LT_UNUSED(pSample_rate);
    if ( NULL != echo->echostate ) {
        int frame_size,sample_rate;
        speex_echo_ctl(echo->echostate,SPEEX_ECHO_GET_FRAME_SIZE,&frame_size);
        speex_echo_ctl(echo->echostate,SPEEX_ECHO_GET_SAMPLING_RATE,&sample_rate);
        *pFrame_size = (u16) frame_size;
        *pSample_rate = (u32) sample_rate;
    }
}


static void LTEchoCancelImpl_DestroyState(LTEchoCancelImpl * echo)
{
    if ( NULL != echo->echostate ) {
        speex_echo_state_destroy(echo->echostate );
        echo->echostate = NULL;
    }
}

static void LTEchoCancelImpl_Capture(LTEchoCancelImpl * echo, const s16 * rec, s16 * out)
{
    if ( NULL != echo->echostate ) {
        speex_echo_capture(echo->echostate,rec,out);
        speex_preprocess_run(echo->denoise,out);
    }
}

static void LTEchoCancelImpl_Playback(LTEchoCancelImpl * echo, const s16 * play)
{
    if ( NULL != echo->echostate ) {
        speex_echo_playback(echo->echostate,play);
    }
}

static void LTEchoCancelImpl_ResetState(LTEchoCancelImpl * echo)
{
    if ( NULL != echo->echostate ) {
        speex_echo_state_reset(echo->echostate);
    }
}

static void LTEchoCancelImpl_EnableNoiseSuppression(LTEchoCancelImpl * echo, bool enable)
{
    if ( NULL != echo->denoise ) {
        int en = enable ? 1 : 0;
        speex_preprocess_ctl(echo->denoise,SPEEX_PREPROCESS_SET_DENOISE,&en);
    }
}

/* LTEchoCancelImpl definition */
define_LTObjectImplPublic(LTEchoCancel,LTEchoCancelImpl,
    InitState,
    GetConfig,
    DestroyState,
    Capture,
    Playback,
    ResetState,
    EnableNoiseSuppression,
);
