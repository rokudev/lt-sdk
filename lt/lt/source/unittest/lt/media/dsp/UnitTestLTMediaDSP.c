/*******************************************************************************
 *
 * UnitTestLTMediaDSP.c - unit test for DSP routines
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/media/dsp/LTMediaDSP.h>
#include <tilt/JiltEngine.h>

// Define a convenience macro that gets an array element count.
#define COUNTOF(x) (sizeof (x) / sizeof ((x)[0]))

static struct Statics {
    LTMediaDSP            *DSPLib;
    ILTIntegerFFT         *iIntegerFFT;
    ILTMediaDSPPWMRender  *iPWMRender;
} S;

static JiltEngine *s_engine;

/* This 256-sample input combines two sin waves.
 * Assuming sample rate is 16kHz, this input is
 *
 *  sample[t] = is the sum of these two sine waves
 *
 * { .frequency_hz = 240, .amplitude = 0.25, .phase = 0.0 },
 * { .frequency_hz = 4800, .amplitude = 0.5, .phase = 0.0 }
 */
static const s16 fft_input[kLTIntegerFFT_FixedArraySize] = {
       0,    16352,    -8094,    -7344,    18597,     3718,   -11192,    14650,
   15237,    -9436,     6627,    22632,    -2217,    -1922,    23516,     8090,
   -7406,    17817,    17757,    -7587,     7790,    23099,    -2451,    -2854,
   21893,     5792,   -10360,    14234,    13576,   -12328,     2531,    17368,
   -8603,    -9372,    15067,    -1281,   -17618,     6855,     6142,   -19751,
   -4814,    10164,   -15601,   -16102,     8665,    -7298,   -23198,     1763,
    1583,   -23737,    -8191,     7426,   -17676,   -17496,     7965,    -7298,
  -22498,     3157,     3658,   -20998,    -4814,    11411,   -13117,   -12404,
   13544,    -1281,   -16095,     9887,    10656,   -13794,     2531,    18834,
   -5683,    -5025,    20803,     5792,    -9269,    16405,    16808,    -8063,
    7790,    23576,    -1502,    -1442,    23757,     8090,    -7647,    17337,
   17042,    -8530,     6627,    21726,    -4022,    -4609,    19970,     3718,
  -12566,    11915,    11164,   -14810,        0,    14810,   -11164,   -11915,
   12566,    -3718,   -19970,     4609,     4022,   -21726,    -6627,     8530,
  -17042,   -17337,     7647,    -8090,   -23757,     1442,     1502,   -23576,
   -7790,     8063,   -16808,   -16405,     9269,    -5792,   -20803,     5025,
    5683,   -18834,    -2531,    13794,   -10656,    -9887,    16095,     1281,
  -13544,    12404,    13117,   -11411,     4814,    20998,    -3658,    -3157,
   22498,     7298,    -7965,    17496,    17676,    -7426,     8191,    23737,
   -1583,    -1763,    23198,     7298,    -8665,    16102,    15601,   -10164,
    4814,    19751,    -6142,    -6855,    17618,     1281,   -15067,     9372,
    8603,   -17368,    -2531,    12328,   -13576,   -14234,    10360,    -5792,
  -21893,     2854,     2451,   -23099,    -7790,     7587,   -17757,   -17817,
    7406,    -8090,   -23516,     1922,     2217,   -22632,    -6627,     9436,
  -15237,   -14650,    11192,    -3718,   -18597,     7344,     8094,   -16352,
       0,    16352,    -8094,    -7344,    18597,     3718,   -11192,    14650,
   15237,    -9436,     6627,    22632,    -2217,    -1922,    23516,     8090,
   -7406,    17817,    17757,    -7587,     7790,    23099,    -2451,    -2854,
   21893,     5792,   -10360,    14234,    13576,   -12328,     2531,    17368,
   -8603,    -9372,    15067,    -1281,   -17618,     6855,     6142,   -19751,
   -4814,    10164,   -15601,   -16102,     8665,    -7298,   -23198,     1763,
    1583,   -23737,    -8191,     7426,   -17676,   -17496,     7965,    -7298
}; /* min:   -23757 max:    23757 */

static void TestIntegerFFT(Tilt * tilt) {
    LTIntegerFFT            *pFFT;
    LTIntegerFFT_LogOutput   sPower;

    pFFT = S.DSPLib->CreateIntegerFFT();
    TILT_ASSERT_TRUE(tilt, pFFT != NULL, "couldn't create integer fft");

    S.iIntegerFFT->WriteInput(pFFT,fft_input,kLTIntegerFFT_FixedArraySize);
    S.iIntegerFFT->PerformFFT(pFFT);
    S.iIntegerFFT->ReadLogarithmicOutput(pFFT,&sPower);

    for (LT_SIZE i = 0; i < COUNTOF(sPower.band); i++) {
        TILT_INFO(tilt,"power[%2d] = { lo: %4d hi: %4d power: %d n: %d",
            i, sPower.band[i].lo_hz, sPower.band[i].hi_hz, sPower.band[i].power, sPower.band[i].n );
    }

    S.iIntegerFFT->DestroyIntegerFFT(pFFT);
}

/* Correct output looks like this
Running test IntegerFFT...
  info: UnitTestLTMediaDSP.c, line 82: power[ 0] = { lo:    0 hi:   63 power: 13919 n: 1
  info: UnitTestLTMediaDSP.c, line 82: power[ 1] = { lo:   63 hi:  125 power: 15537 n: 1
  info: UnitTestLTMediaDSP.c, line 82: power[ 2] = { lo:  125 hi:  177 power: 22053 n: 1
  info: UnitTestLTMediaDSP.c, line 82: power[ 3] = { lo:  177 hi:  250 power: 47384 n: 1
  info: UnitTestLTMediaDSP.c, line 82: power[ 4] = { lo:  250 hi:  354 power: 47153 n: 2
  info: UnitTestLTMediaDSP.c, line 82: power[ 5] = { lo:  354 hi:  500 power: 17009 n: 2
  info: UnitTestLTMediaDSP.c, line 82: power[ 6] = { lo:  500 hi:  707 power: 8976 n: 4
  info: UnitTestLTMediaDSP.c, line 82: power[ 7] = { lo:  707 hi: 1000 power: 5988 n: 4
  info: UnitTestLTMediaDSP.c, line 82: power[ 8] = { lo: 1000 hi: 1414 power: 4699 n: 7
  info: UnitTestLTMediaDSP.c, line 82: power[ 9] = { lo: 1414 hi: 2000 power: 4122 n: 9
  info: UnitTestLTMediaDSP.c, line 82: power[10] = { lo: 2000 hi: 2828 power: 4249 n: 14
  info: UnitTestLTMediaDSP.c, line 82: power[11] = { lo: 2828 hi: 4000 power: 6042 n: 18
  info: UnitTestLTMediaDSP.c, line 82: power[12] = { lo: 4000 hi: 5656 power: 21535 n: 27
  info: UnitTestLTMediaDSP.c, line 82: power[13] = { lo: 5656 hi: 8000 power: 2221 n: 37
Finished test IntegerFFT: success
 */

static const unsigned char test_pwm_bitstream[16] = {
    0x00, 0x01, 0x80, 0x02, 0x40, 0x04, 0x20, 0x08,
    0x10, 0xff, 0xfe, 0x7f, 0xfe, 0xbf, 0xfb, 0xdf
};
#define PWMTEST_PERIOD      10
#define PWMTEST_ONE_WIDTH   8
#define PWMTEST_ZERO_WIDTH  2

static void TestPWMRender(Tilt * tilt) {
    LTMediaDSPPWMRender   *pPWM;
    LTPWMRenderElement     elem;

    pPWM = S.DSPLib->CreatePWMRender();

    TILT_ASSERT_TRUE(tilt, pPWM != NULL, "couldn't create pwm renderer");

    /* set total tick count to 10 clocks, 2 high for 0, 6 high for 1 */
    S.iPWMRender->SetBitstreamTickCounts(pPWM,
        PWMTEST_PERIOD,
        PWMTEST_ZERO_WIDTH,
        PWMTEST_ONE_WIDTH);

    for ( LT_SIZE i = 0; i < COUNTOF(test_pwm_bitstream)*8; i++ ) {

        S.iPWMRender->RenderElements(pPWM,&elem,test_pwm_bitstream,i,1);

        if ((1 != (elem.hi_ticks_val & 0x1)) ||
            (0 != (elem.lo_ticks_val & 0x1))) {
            TILT_REPORT_FAILURE(tilt,"level error at bit %d", i );
            break;
        }

        if (test_pwm_bitstream[i>>3] & (1 << (i&0x7))) {
            /* output should be a one */
            if ( (PWMTEST_ONE_WIDTH != (elem.hi_ticks_val & ~0x01)) ||
                 ((PWMTEST_PERIOD-PWMTEST_ONE_WIDTH) != (elem.lo_ticks_val & ~0x01)) ) {
                TILT_REPORT_FAILURE(tilt,"bit width error at bit %d", i );
                break;
            }
        } else {
            /* output should be a zero */
            if ( (PWMTEST_ZERO_WIDTH != (elem.hi_ticks_val & ~0x01)) ||
                 ((PWMTEST_PERIOD-PWMTEST_ZERO_WIDTH) != (elem.lo_ticks_val & ~0x01)) ) {
                TILT_REPORT_FAILURE(tilt,"bit width error at bit %d", i );
                break;
            }
        }
    }

    S.iPWMRender->DestroyPWMRender(pPWM);
}

static void BeforeAllTests(Tilt *tilt) {
    S.DSPLib = lt_openlibrary(LTMediaDSP);
    TILT_EXPECT_TRUE(tilt, S.DSPLib != NULL, "Cannot open LTMediaDSP");
    if (S.DSPLib == NULL) return;
    
    S.iIntegerFFT = lt_getlibraryinterface(ILTIntegerFFT, S.DSPLib);
    S.iPWMRender  = lt_getlibraryinterface(ILTMediaDSPPWMRender, S.DSPLib);
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_closelibrary(S.DSPLib);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { TestIntegerFFT,    "IntegerFFT",    "Integer FFT Routine",    0 },
    { TestPWMRender,     "PWMRender",     "PWM Render Routine",     0 }
};

static int UnitTestLTMediaDSPImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTMediaDSPImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTMediaDSPImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTMediaDSP, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTMediaDSP, UnitTestLTMediaDSPImpl_Run, 1536) LTLIBRARY_DEFINITION;
