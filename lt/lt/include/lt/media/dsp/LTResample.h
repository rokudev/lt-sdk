/******************************************************************************
 * <lt/media/dsp/LTResample.h>                           LT Resampler
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

 /**
 * @defgroup ltmedia_dsp_resample LTResample
 * @ingroup ltmedia_dsp
 * @{
 *
 * @brief Resampling utilities.
 *
 */


#ifndef ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTRESAMPLE_H
#define ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTRESAMPLE_H

#include <lt/LTObject.h>

LT_EXTERN_C_BEGIN

typedef struct LTResample LTResample;

/*_________________
 / LTResample Object */
typedef_LTObject(LTResample, 1) {

    bool (*InitState)(LTResample *resampler, u32 channels, u32 in_rate, u32 out_rate, u32 quality);
    /** Init a new Speex resampler state
     * @param resampler Resampler object
     * @param channels Number of channels (1 for mono, 2 for stereo, etc.)
     * @param in_rate Input sample rate (Hz)
     * @param out_rate Output sample rate (Hz)
     * @param quality Resampler quality (0 = lowest, 10 = highest, typical: 3-5)
     * @return {true,false} on success/failure
     */

    void (*GetConfig)(LTResample *resampler, u32 *pIn_rate, u32 *pOut_rate, u32 *pChannels, u32 *pQuality);
    /** Get current configuration of the resampler
     * @param resampler Resampler object
     * @param pIn_rate Pointer to receive input sample rate
     * @param pOut_rate Pointer to receive output sample rate
     * @param pChannels Pointer to receive number of channels
     * @param pQuality Pointer to receive quality setting
     */

    void (*DestroyState)(LTResample *resampler);
    /** Destroy an existing resampler state
     * @param resampler Resampler object
     */

    bool (*Process)(LTResample *resampler, const s16 *in_buf, u32 *in_len,
                s16 *out_buf, u32 *out_len);
    /** Process/resample PCM data
     * @param resampler Resampler object
     * @param in_buf Input PCM buffer (interleaved if stereo)
     * @param in_len Input length in samples per channel (updated with samples consumed)
     * @param out_buf Output PCM buffer
     * @param out_len Output length in samples per channel (updated with samples produced)
     * @return {true,false} on success/failure
     */

    void (*ResetState)(LTResample *resampler);
    /** Reset the resampler state and flush buffers
     * @param resampler Resampler object
     */

} LTOBJECT_API;


/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTRESAMPLE_H */