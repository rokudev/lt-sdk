/******************************************************************************
 * <lt/media/dsp/LTIntegerFFT.h>          LT Integer FFT
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltmedia_dsp_integerfft LTIntegerFFT
 * @ingroup ltmedia_dsp
 * @{
 *
 * @brief A set of integer fft utilities.
 *
 */

#ifndef ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTINTEGERFFT_H
#define ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTINTEGERFFT_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

enum {
    kLTIntegerFFT_FixedArraySize                      = 256,
    /** The IntegerFFT is a fixed size for algorithmic performance
     * It will only analyze kLTIntegerFFT_FixedArraySize elements (256)
     * For example, for 16kHz audio, this is roughly 16 milliseconds worth
     * of samples which is considered the upper limit of delay for audio/light synchronization
     */
    kLTIntegerFFT_FixedLogSize                        = 14,
    /** The Logarithmic (Power output) of the IntegerFFT contains 14 frequency bands
     * The output of PerformFFT()/ReadLogarithmicOutput() will analyze 
     * power in the signal every half-octave.  This is fine grained enough
     * to provide a reliable spectrum for 16kHz audio from 60Hz to 8kHz
     */
};

typedef struct sLTIntegerFFT LTIntegerFFT;

struct sLTIntegerFFT_Band {
    u16  lo_hz;
    u16  hi_hz;
    u16  power;
    u16  n;
};

typedef struct {
    struct sLTIntegerFFT_Band band[kLTIntegerFFT_FixedLogSize];
} LTIntegerFFT_LogOutput;

/*********************
 * LTIntegerFFT Interface *
 *********************/
/* The generic LTIntegerFFT is designed to analyze PCM Audio Samples.
 * NOTE: There is NO INVERSE INT FFT provided as the results of the
 * integer FFT is intended to be used for signal analysis only,
 * not for signal processing.
 */
TYPEDEF_LTLIBRARY_INTERFACE(ILTIntegerFFT, 1);

struct ILTIntegerFFTApi {

    INHERIT_INTERFACE_BASE

    void      (*WriteInput)(LTIntegerFFT * pFft, const s16 * pSamples, LT_SIZE nSamples );
        /** Write kLTIntegerFFT_FixedArraySize samples into the FFT Analyzer 
         * @param[pFft]     FFT Analyzer
         * @param[pSamples] array if signed 16 bit samples to analyzer
         * @param[nSamples] number of samples to analyze, MUST BE 256 (kLTIntegerFFT_FixedArraySize)
         */

    void      (*PerformFFT)(LTIntegerFFT * pFft);
        /** Perform the FFT on the samples 
         * @param[pFft]     FFT Analyzer
         */

    void      (*ReadLogarithmicOutput)(LTIntegerFFT * pFft, LTIntegerFFT_LogOutput *pPower );
        /** Read the logarithmic Power output of the FFT, broken into bands
         * @param[pFft]     FFT Analyzer
         * @param[pPower]   array of frequency bands and their power level (logarithmic)
         */

    void      (*DestroyIntegerFFT)(LTIntegerFFT * pFft);
        /** Destroys the given fft 
         * @param[pFft]     FFT Analyzer
         */
};

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTINTEGERFFT_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  31-May-23   diocletian  created
 */
