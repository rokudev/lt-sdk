/*******************************************************************************
 * source/lt/media/dsp/LTMediaDSPIntegerFFT.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 * 
 * Based on code from:
 * 
 *   https://github.com/hamsternz/fft_three_ways
 * 
 *******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/media/dsp/LTMediaDSP.h>
#include "LTMediaDSPIntegerFFT.h"


#define FFT_SIZE    kLTIntegerFFT_FixedArraySize
#define DFT_SIZE      8    // When we DFT rather than FFT
#define I_SCALE       2    // Scale factor for the integer FFT data (e.g 24 bits -> 24+I_SCALE bits)
#define I_TRIG_SCALE 22    // Scale factor for the integer TRIG constants (e.g cos(0) = 1<<I_TRIG_SCALE)

struct sLTIntegerFFT {
    s64 in_r[FFT_SIZE];
    s64 in_i[FFT_SIZE];
    s64 out_r[FFT_SIZE];
    s64 out_i[FFT_SIZE];
};

static const s32 i_sines[FFT_SIZE>>1] = {
       0,   102933,   205804,   308552,   411113,   513427,   615432,   717066,   818268,   918977,  1019132,  1118674,  1217542,  1315676,  1413018,  1509509,
 1605090,  1699705,  1793296,  1885806,  1977181,  2067364,  2156303,  2243942,  2330230,  2415114,  2498543,  2580468,  2660838,  2739605,  2816722,  2892142,
 2965820,  3037712,  3107774,  3175964,  3242240,  3306564,  3368896,  3429199,  3487436,  3543572,  3597574,  3649409,  3699045,  3746454,  3791605,  3834473,
 3875031,  3913255,  3949122,  3982609,  4013698,  4042369,  4068605,  4092391,  4113711,  4132553,  4148906,  4162760,  4174107,  4182939,  4189251,  4193040,
 4194304,  4193040,  4189251,  4182939,  4174107,  4162760,  4148906,  4132553,  4113711,  4092391,  4068605,  4042369,  4013698,  3982609,  3949122,  3913255,
 3875031,  3834473,  3791605,  3746454,  3699045,  3649409,  3597574,  3543572,  3487436,  3429199,  3368896,  3306564,  3242240,  3175964,  3107774,  3037712,
 2965820,  2892142,  2816722,  2739605,  2660838,  2580468,  2498543,  2415114,  2330230,  2243942,  2156303,  2067364,  1977181,  1885806,  1793296,  1699705,
 1605090,  1509509,  1413018,  1315676,  1217542,  1118674,  1019132,   918977,   818268,   717066,   615432,   513427,   411113,   308552,   205804,   102933
};

static const unsigned char bitreverse[32] = {
   0x00, 0x10, 0x08, 0x18,
   0x04, 0x14, 0x0c, 0x1c,
   0x02, 0x12, 0x0a, 0x1a,
   0x06, 0x16, 0x0e, 0x1e,
   0x01, 0x11, 0x09, 0x19,
   0x05, 0x15, 0x0d, 0x1d,
   0x03, 0x13, 0x0b, 0x1b,
   0x07, 0x17, 0x0f, 0x1f
};

/* Logarithmic band separation for a 16KHz signal */
/* band 0         0.0->    62.50  is from 0 to 0 */
/* band 1       62.50->    125.00 is from 1 to 1 */
/* band 2      125.00->    176.78 is from 2 to 2 */
/* band 3      176.78->    250.00 is from 3 to 3 */
/* band 4      250.00->    353.55 is from 4 to 5 */
/* band 5      353.55->    500.00 is from 6 to 7 */
/* band 6      500.00->    707.11 is from 8 to 11 */
/* band 7      707.11->   1000.00 is from 12 to 15 */
/* band 8     1000.00->   1414.21 is from 16 to 22 */
/* band 9     1414.21->   2000.00 is from 23 to 31 */
/* band 10    2000.00->   2828.43 is from 32 to 45 */
/* band 11    2828.43->   4000.00 is from 46 to 63 */
/* band 12    4000.00->   5656.85 is from 64 to 90 */
/* band 13    5656.85->   8000.00 is from 91 to 127 */

struct iFFT_Band {
    u16   lo_index;
    u16   hi_index;
    u16   lo_hz;
    u16   hi_hz;
};
static const struct iFFT_Band band_bins[kLTIntegerFFT_FixedLogSize] = {
    {.lo_index =  0, .hi_index =   0, .lo_hz =    0, .hi_hz =   63 },
    {.lo_index =  1, .hi_index =   1, .lo_hz =   63, .hi_hz =  125 },
    {.lo_index =  2, .hi_index =   2, .lo_hz =  125, .hi_hz =  177 },
    {.lo_index =  3, .hi_index =   3, .lo_hz =  177, .hi_hz =  250 },
    {.lo_index =  4, .hi_index =   5, .lo_hz =  250, .hi_hz =  354 },
    {.lo_index =  6, .hi_index =   7, .lo_hz =  354, .hi_hz =  500 },
    {.lo_index =  8, .hi_index =  11, .lo_hz =  500, .hi_hz =  707 },
    {.lo_index = 12, .hi_index =  15, .lo_hz =  707, .hi_hz = 1000 },
    {.lo_index = 16, .hi_index =  22, .lo_hz = 1000, .hi_hz = 1414 },
    {.lo_index = 23, .hi_index =  31, .lo_hz = 1414, .hi_hz = 2000 },
    {.lo_index = 32, .hi_index =  45, .lo_hz = 2000, .hi_hz = 2828 },
    {.lo_index = 46, .hi_index =  63, .lo_hz = 2828, .hi_hz = 4000 },
    {.lo_index = 64, .hi_index =  90, .lo_hz = 4000, .hi_hz = 5656 },
    {.lo_index = 91, .hi_index = 127, .lo_hz = 5656, .hi_hz = 8000 }
};

/* Save data space
 * iCos(n) is iSin( n + 90deg )
 * for iSin(n), if n > FFT_SIZE/2 iSin(n) is negative wave
 * there's another optimization as well where you only need a quarter wave
 */
#define iSin(n) (((n) < (FFT_SIZE>>1)) ? \
                   i_sines[(n)] : \
                  -i_sines[(n)&((FFT_SIZE>>1)-1)] )

#define iCos(n) iSin((FFT_SIZE-1) & ((n)+(FFT_SIZE>>2)))

#define reverse_bits(n,k) bitreverse[n]

////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////
static void i_dft(const s64 *r_i, const s64 *i_i, s64 *r_o, s64 *i_o, LT_SIZE size, LT_SIZE stride) {
   for(LT_SIZE bin = 0; bin < size; bin++) {
      LT_SIZE i = 0;
      s64 total_i = 0.0;
      s64 total_q = 0.0;
      for(LT_SIZE s = 0; s < size; s++) {
         total_i +=  r_i[s*stride] * iCos(i) - i_i[s*stride] * iSin(i);
         total_q +=  r_i[s*stride] * iSin(i) + i_i[s*stride] * iCos(i);
         i += bin*(FFT_SIZE/size);
         if(i >= FFT_SIZE) i -= FFT_SIZE;
      }
      r_o[bin] = total_i >> I_TRIG_SCALE;
      i_o[bin] = total_q >> I_TRIG_SCALE;
   }
}

static void i_fft_256(const s64 *r_i, const s64 *i_i, s64 *r_o, s64 *i_o) {
    LT_SIZE i, stride, step;

    stride = FFT_SIZE/DFT_SIZE;
    for(i = 0; i < stride ; i++) {
        int out_offset = reverse_bits(i,stride) * DFT_SIZE;
        i_dft(r_i+i, i_i+i,  r_o+out_offset, i_o+out_offset, DFT_SIZE, stride);
    }

    stride = DFT_SIZE*2;
    step = FFT_SIZE/stride;
    while(stride <= FFT_SIZE) {
       for(i = 0; i < FFT_SIZE; i+= stride) {
          s64 *real = r_o+i;
          s64 *imag = i_o+i;
          LT_SIZE size = stride/2;
          for(LT_SIZE j = 0; j < size; j++) {
             s64 c = iCos(j*step), s = iSin(j*step);
             s64 rotated_r =  (real[size] * c - imag[size] * s)>>I_TRIG_SCALE;
             s64 rotated_i =  (real[size] * s + imag[size] * c)>>I_TRIG_SCALE;
             real[size] = real[0] - rotated_r;
             imag[size] = imag[0] - rotated_i;
             real[0]    = real[0] + rotated_r;
             imag[0]    = imag[0] + rotated_i;
             real++;
             imag++;
          }
       }
       stride *= 2;
       step   /= 2;
    }
    for(i = 0; i < FFT_SIZE ; i++) {
       r_o[i] /= FFT_SIZE;
       i_o[i] /= FFT_SIZE;
    }
}

static u32 sqrt_uint(u32 val) {
    if (val <= 1) {
        return val; // the square root of 0 is 0 and square root of 1 is 1
    }

    // Start with a rough guess
    u32 x = val / 2;

    u32 last_x; // to store value from previous iteration

    do {
        last_x = x;
        // Calculate a new approximation
        x = (x + (val / x)) / 2;
    } while (x < last_x); // If our guess didn't decrease, we've found our answer

    return last_x;
}

static u32 veclen(s32 x, s32 y)
{
    return( sqrt_uint(x*x + y*y) );
}

/* ----------------------------------------------------------------- */
/* ----------------------------------------------------------------- */

LTIntegerFFT * LTMediaDSPIntegerFFTImpl_Create(void)
{
    LTIntegerFFT *pFft = lt_malloc(sizeof(*pFft));

    return pFft;
}

static void LTMediaDSPIntegerFFTImpl_WriteInput(LTIntegerFFT * pFft, const s16 * pSamples, LT_SIZE nSamples )
{
    if ( nSamples == kLTIntegerFFT_FixedArraySize ) {
        LT_SIZE i;
        for ( i = 0; i < nSamples; i++ ) {
            pFft->in_r[i] = (s64)pSamples[i] << (I_TRIG_SCALE - 16);
            pFft->in_i[i] = 0;
        }
    }
}

static void LTMediaDSPIntegerFFTImpl_PerformFFT(LTIntegerFFT * pFft)
{
    i_fft_256(
        pFft->in_r,
        pFft->in_i,
        pFft->out_r,
        pFft->out_i );
}

static void LTMediaDSPIntegerFFTImpl_ReadLogarithmicOutput(LTIntegerFFT * pFft, LTIntegerFFT_LogOutput *pPower )
{
    for ( LT_SIZE i = 0; i < kLTIntegerFFT_FixedLogSize; i++ )
    {
        s64 pow_sum;

        pPower->band[i].lo_hz = band_bins[i].lo_hz;
        pPower->band[i].hi_hz = band_bins[i].hi_hz;
        pPower->band[i].n     = band_bins[i].hi_index + 1 - band_bins[i].lo_index;

        /* sum all output bands, then divide */
        pow_sum = 0;
        for ( LT_SIZE j = 0; j < pPower->band[i].n; j++ )
        {
            pow_sum += veclen(
                pFft->out_r[band_bins[i].lo_index + j],
                pFft->out_i[band_bins[i].lo_index + j] );
        }
        /* average for all the bands */
        pow_sum /= pPower->band[i].n;

        pPower->band[i].power = pow_sum;
    }
}

static void LTMediaDSPIntegerFFTImpl_DestroyIntegerFFT(LTIntegerFFT * pFft)
{
    if ( NULL != pFft ) {
        lt_free(pFft);
    }
}

define_LTLIBRARY_INTERFACE(ILTIntegerFFT)

    .WriteInput             = &LTMediaDSPIntegerFFTImpl_WriteInput,
    .PerformFFT             = &LTMediaDSPIntegerFFTImpl_PerformFFT,
    .ReadLogarithmicOutput  = &LTMediaDSPIntegerFFTImpl_ReadLogarithmicOutput,
    .DestroyIntegerFFT      = &LTMediaDSPIntegerFFTImpl_DestroyIntegerFFT

LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-May-23   diocletian  created
 */
