/*******************************************************************************
 * source/lt/media/dsp/LTResample.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
DEFINE_LTLOG_SECTION("ltresample.speex");
#include <lt/media/dsp/LTResample.h>
#include "speex/speex_resampler.h"


/* LTResample object stubs */
typedef_LTObjectImpl(LTResample, LTResampleImpl) {
    SpeexResamplerState *resampler;
    u32 in_rate;
    u32 out_rate;
    u32 channels;
    u32 quality;
} LTOBJECT_API;

/* pre-declaration */
static void LTResampleImpl_DestroyState(LTResampleImpl * r);

/* implementation of LTResample */

static bool LTResampleImpl_ConstructObject(LTResampleImpl * r)
{
    bool retval = true;
    r->resampler = NULL;
    r->in_rate = 0;
    r->out_rate = 0;
    r->channels = 0;
    r->quality = 0;
    LT_UNUSED(r);
    return retval;
}

static void LTResampleImpl_DestructObject(LTResampleImpl * r)
{
    LTResampleImpl_DestroyState(r);
}

static bool LTResampleImpl_InitState(LTResampleImpl *r, u32 channels, u32 in_rate, u32 out_rate, u32 quality)
{
    int err;

    if (!r) {
        LTLOG_REDALERT("resam.init", "InitState called with NULL pointer");
        return false;
    }


    // If there’s an old resampler, destroy it safely
    LTResampleImpl_DestroyState(r);

    r->in_rate  = in_rate;
    r->out_rate = out_rate;
    r->channels = channels;
    r->quality  = quality;

    r->resampler = speex_resampler_init(channels, in_rate, out_rate, quality, &err);

    if (!r->resampler || err != RESAMPLER_ERR_SUCCESS) {
        r->resampler = NULL;
        return false;
    }

    return true;
}

static void LTResampleImpl_GetConfig(LTResampleImpl * r, u32 *pIn_rate, u32 *pOut_rate, u32 *pChannels, u32 *pQuality)
{
    if (pIn_rate)  *pIn_rate  = r->in_rate;
    if (pOut_rate) *pOut_rate = r->out_rate;
    if (pChannels) *pChannels = r->channels;
    if (pQuality)  *pQuality  = r->quality;
}

static void LTResampleImpl_DestroyState(LTResampleImpl * r)
{
    if ( NULL != r->resampler) {
        LTLOG("resam.destr", "Destroying LTResample");
        speex_resampler_destroy(r->resampler);
        r->resampler = NULL;
    }
}

/* Main processing: resample input PCM to output PCM */
static bool LTResampleImpl_Process(LTResampleImpl * r, const s16 *in_buf, spx_uint32_t *in_len,
                                   s16 *out_buf, spx_uint32_t *out_len)
{
    if (!r->resampler) return false;

    int err = speex_resampler_process_int(r->resampler, 0, in_buf, in_len, out_buf, out_len);
    return (err == RESAMPLER_ERR_SUCCESS);
}

/* Reset state (flush buffers) */
static void LTResampleImpl_ResetState(LTResampleImpl * r)
{
    if (r->resampler) {
        speex_resampler_reset_mem(r->resampler);
    }
}

/* LTResampleImpl definition */
define_LTObjectImplPublic(LTResample, LTResampleImpl,
    InitState,
    GetConfig,
    DestroyState,
    Process,
    ResetState,
);