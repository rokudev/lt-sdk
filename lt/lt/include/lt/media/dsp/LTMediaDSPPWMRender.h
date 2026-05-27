/******************************************************************************
 * <lt/media/dsp/LTMediaDSPPWMRender.h>    LT PWM Waveform Render
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltmedia_dsp_pwm_render LTMediaDSPPWMRender
 * @ingroup ltmedia_dsp
 * @{
 *
 * @brief A set of utilities to render a PWM Waveform
 *
 */

#ifndef ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTMEDIADSPPWMRENDER_H
#define ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTMEDIADSPPWMRENDER_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

typedef struct sLTMediaDSPPWMRender LTMediaDSPPWMRender;

typedef struct sLTPWMRenderElement {
    u16 hi_ticks_val;   /* LSB is zero or one */
    u16 lo_ticks_val;   /* LSB is zero or one */
} LTPWMRenderElement;

/*********************
 * LTMediaDSPPWMRender Interface *
 *********************/
TYPEDEF_LTLIBRARY_INTERFACE(ILTMediaDSPPWMRender, 1);

struct ILTMediaDSPPWMRenderApi {

    INHERIT_INTERFACE_BASE

    void      (*SetBitstreamTickCounts)(LTMediaDSPPWMRender *pPWM, u16 period_ticks, u16 zero_ticks, u16 one_ticks );
        /** Initialise the period (in clock ticks) and width of the zero and one bits
         * @param[pPWM]     PWM Renderer
         * @param[period_ticks]  number of clock ticks to represent a zero or one
         * @param[zero_ticks]    width of a zero bit, in clock ticks (remainder is zero)
         * @param[period_ticks]  width of a one bit, in clock ticks (remainder is zero)
         */

    void      (*RenderElements)(LTMediaDSPPWMRender *pPWM, 
                    LTPWMRenderElement *pElems,
                    const u8           *pBits,
                    LT_SIZE             bitOffset,
                    LT_SIZE             nBits );
        /** Direct Conversion of bits from a bitstream to RenderElements.
         * This method has no memory requirements and converts based on bit
         * offset and length directly from the source bits.
         * @param[pPWM]      PWM Renderer
         * @param[pElems]    array of LTPWMRenderElement to write to
         * @param[pBits]     array of bytes to convert LWPMRenderElements, LSB first.
         * @param[bitOffset] bit position of the first bit of pBits to convert to LWPMRenderElements
         * @param[nBits]     number of bits/LTPWMRenderElements to render
         */

    void      (*DestroyPWMRender)(LTMediaDSPPWMRender *pPWM);
        /** Destroys the given renderer
         * @param[pPWM]     PWM Renderer
         */
};

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTMEDIADSPPWMRENDER_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  31-May-23   diocletian  created
 */
