/******************************************************************************
 * <lt/media/dsp/LTMediaDSP.h>              LT DSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
******************************************************************************/

/**
 * @defgroup ltmedia_dsp LTMediaDSP
 * @ingroup ltmedia
 * @{
 *
 * @brief LTMediaIntegerFFT.
 *
 */

#ifndef ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTMEDIADSP_H
#define ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTMEDIADSP_H

#include <lt/LTTypes.h>

#include <lt/media/dsp/LTIntegerFFT.h>
#include <lt/media/dsp/LTMediaDSPPWMRender.h>

LT_EXTERN_C_BEGIN

/******************************************************************************
 * LTMediaDSP Library Root Interface
 *****************************************************************************/
typedef_LTLIBRARY_ROOT_INTERFACE(LTMediaDSP, 1) {
    LTIntegerFFT *              (*CreateIntegerFFT)(void);
        /**< Creates an integer FFT Execution context.
         * Note: the IntegerFFT holds 256 samples (kLTIntegerFFT_FixedArraySize)
         *   @return a struct LTIntegerFFT pointer
         */

    LTMediaDSPPWMRender *     (*CreatePWMRender)(void);
        /**< Creates an LTMediaDSPPWMRender context.
         * Note: The converter is used to render bits into a PWM waveform. This
         *       is used primarily to generate the data wiggler for addressable
         *       LED arrays.
         *   @return a struct LTMediaDSPPWMRender pointer
         */

} LTLIBRARY_INTERFACE;

/** @} */

LT_EXTERN_C_END

#endif /* #ifndef ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTMEDIADSP_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  31-May-23   diocletian  created
 */
