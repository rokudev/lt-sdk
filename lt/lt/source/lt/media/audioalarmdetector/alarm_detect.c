/*******************************************************************************
 * source/lt/media/audioalarmdetector/alarm_detect.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/
#include "alarm_detect.h"

//#define DEBUG_ALARM_DETECT_C
#ifdef DEBUG_ALARM_DETECT_C
    DEFINE_LTLOG_SECTION("audtone");
    #define LTLOG_DEBUG_RETAIL(pTag, pFormat, ...)     LT_ISR_SAFE LT_GetCore()->Log(s_pLTLOG_Section,  pTag, kLTCore_LogFlags_LogTypeLog | (kLTCore_LogFlags_LogToConsole), pFormat, ##__VA_ARGS__)
#else
    #define LTLOG_DEBUG_RETAIL(...)
#endif

/** number of samples the algorithm expects to see in each analysis */
#define ALARM_DETECT_NUM_SAMPLES 62

/* Goertzel power is calculated as fixed point log2(p) with three lsbs.
 * this means a ~6dB change in power is represented as 0x08
 * The power threshold for tone detection must mean a tone must
 * come in at this much power over previous background noise calculations
 */
#define TONE_DETECTION_POWER_THRESHHOLD  (2 * 8)

/* This is how much a tone detection must fall (from peak level)
 * before it is considered the end of the tone.
 */
#define TONE_POWER_FALLING_THRESHOLD     8

#define MOVING_AVERAGE_POS_THRESHOLD     5
#define MOVING_AVERAGE_NEG_THRESHOLD     -5
/* Used to configure very fast rising power levels, will re-pin the starting time of the tone */
#define FAST_RISING_POWER_THRESHOLD      (2*8)

/* == 258 when ALARM_DETECT_NUM_SAMPLES == 62 */
#define TICKRATE (ALARM_DETECT_SAMPLE_RATE / ALARM_DETECT_NUM_SAMPLES)

#define TONE_LENGTH_FROM_MS(ms) ((TICKRATE * (ms)) / 1000)

#define FIRE_TONE_LENGTH        ((TICKRATE *  50) / 100)
#define FIRE_TONE_LENGTH_TOL    ((TICKRATE *  20) / 100)
#define FIRE_TONE_INTERVAL      ((TICKRATE * 100) / 100)
#define FIRE_TONE_INTERVAL_TOL  ((TICKRATE *  20) / 100)

#define SLOW_TONE_LENGTH        ((TICKRATE *  75) / 100)
#define SLOW_TONE_LENGTH_TOL    ((TICKRATE *  20) / 100)
#define SLOW_TONE_INTERVAL      ((TICKRATE * 150) / 100)
#define SLOW_TONE_INTERVAL_TOL  ((TICKRATE *  30) / 100)

/* carbom monoxide is 4 x 100ms tone + 100ms quiet */
#define CO_TONE_LENGTH          ((TICKRATE *  10) / 100)
#define CO_TONE_LENGTH_TOL      ((TICKRATE *   5) / 100)
#define CO_TONE_INTERVAL        ((TICKRATE *  20) / 100)
#define CO_TONE_INTERVAL_TOL    ((TICKRATE *   5) / 100)

/* TODO: not implemented, fast repeating tone */
#define FAST_TONE_LENGTH        ((TICKRATE *  10) / 100)
#define FAST_TONE_LENGTH_TOL    ((TICKRATE *   1) / 100)
#define FAST_TONE_INTERVAL      ((TICKRATE *  10) / 100)
#define FAST_TONE_INTERVAL_TOL  ((TICKRATE *   1) / 100)


/* Alarm detection (3+ tones) will shut off after 1.5 seconds */
#define ALARM_DISABLE_INTERVAL  ((TICKRATE * 150) / 100)

/* Alarm history goes ACTIVE after there have been
 * N detections in the last several seconds
 * Derivation: 
 *   the slow fire alarm tone is 3.75 seconds for 3 beeps and 2 intervals
 *   and they're paced at ~6 seconds per alarm.  So the time taken
 *   for these is: 12 seconds + 3.75 == 15.75 seconds
 */
#define ALARM_HACTIVE_INTERVAL   (TICKRATE * 17)

/* Alarm history goes INACTIVE after there have been
 * ZERO detections in the last several seconds
 */
#define ALARM_HINACTIVE_INTERVAL (TICKRATE * 17)

#define TONE_DETECTION_COUNT    4
#define ALARM_HISTORY_COUNT     4
#define ALARMS_DETECTED_TO_TRIGGER 3

struct ToneDetector {
    LTMediaAudioAlarmFlags flag;
    /* Configuration for the tone intervals and power state monitoring */
    u32 tone;              /* which tone[] selected */
    u32 power_mix;         /* mix coefficient (of 64) for FAST moving average */
    u32 long_power_mix;    /* mix coefficient (of 64) for SLOW moving average */
    u32 tone_length;       /* number of active ticks to make a single alarm tone */
    u32 tone_length_tol;   /* acceptable tolerance of tone length */
    u32 tone_interval;     /* number of active ticks expected alarm tone interval */
    u32 tone_interval_tol; /* acceptable tolerance of tone interval */
    u32 tones_per_alarm;   /* number of tones to create an alarm */

    /* run-time moving average analysis */
    u32 t;                 /* current time in "ticks," or number of audio frames */
    s64 tpow;              /* DEBUG: current (s64) goertzel power for this audio frame */
    u32 power;             /* FAST moving average of tpow */
    u32 long_power;        /* SLOW moving average of tpow */
    u32 ma_background_pow; /* background power measured when inactive */

    /* thresholds for detecting the beginning and ending of a tone */
    /* The moving average analysis detects when the FAST and SLOW
     * moving average cross to indicate the beginning and ending
     * of a tone.
     * The threshold values add hysteresis to the crossings. This
     * does a good job of rejecting background noise, which is typically
     * within a small decibel range.
     */
    s32 ma_current_state;; /* 0 == syncing, +1 rising, -1 falling */
    s32 ma_pos_threshhold; /* configuration for debounce hysteresis */
    s32 ma_neg_threshhold; /* configuration for debounce hysteresis */
    s32 ma_pos_peak;       /* possibly used for tuning pos/neg threshold */
    s32 ma_neg_peak;       /* possibly used for tuning pos/neg threshold */

    /* Single Tone Detection */
    u32 ma_tone_start;     /* when did we cross positive */
    u32 ma_tone_start_pow; /* power level when crossing the start of the tone */
    u32 ma_tone_length;    /* length of tone as calculated by moving average */
    u32 ma_tone_count;     /* number of tones received */
    struct {
        u32 t0;
        u32 duration;
    } ma_detection[TONE_DETECTION_COUNT];

    /* Three/Four properly sized and spaced tones create an alarm */
    u32 ma_alarm_count;    /* number of alarms (three-tones) received */
    struct {
        u32 t0;
    } ma_alarm_history[ALARM_HISTORY_COUNT];
    u32 ma_alarm_history_active;
};

#define NUM_TONES 2
static s64 tpow[NUM_TONES];  /* goertzel power RESULT for tone */

/* machine-generated table of goertzel coefficients
 * for tone detectors and sample rates
 */
struct goertzel_coef {
   u32 sample_rate;
   u32 tone_hz;
   s32   coef;
};
#define NUM_GOERTZEL_COEFS 20
static const struct goertzel_coef s_goertzel_coef[NUM_GOERTZEL_COEFS] = {
    /* sample rate: 8000 */
    {  8000,   3100, -24916 },
    {  8000,    520,  30072 },
    {  8000,   3000, -23170 },
    {  8000,   3200, -26509 },
    /* sample rate: 16000 */
    { 16000,   3100,  11341 },
    { 16000,    520,  32087 },
    { 16000,   3000,  12539 },
    { 16000,   3200,  10125 },
    /* sample rate: 32000 */
    { 32000,   3100,  26882 },
    { 32000,    520,  32597 },
    { 32000,   3000,  27245 },
    { 32000,   3200,  26509 },
    /* sample rate: 44100 */
    { 44100,   3100,  29623 },
    { 44100,    520,  32678 },
    { 44100,   3000,  29820 },
    { 44100,   3200,  29420 },
    /* sample rate: 48000 */
    { 48000,   3100,  30106 },
    { 48000,    520,  32692 },
    { 48000,   3000,  30273 },
    { 48000,   3200,  29935 }
};

struct GoertzelAnalyzer
{
    s64        coef;
    s64        q1;
    s64        q2;
};
static struct GoertzelAnalyzer ga[NUM_TONES];

#define POWER_THRESHOLD 100

#define NUM_DETECTORS   4

static struct ToneDetector detect[NUM_DETECTORS] = {
    {   /* 3100 Hz Fire Alarm, standard timing (1.0s/beep) */
        .flag = kLTMediaAudioAlarmFlags_Smoke,
        .tone = 0,  /* 3100 Hz */
        .tones_per_alarm = 3,
        .power_mix = 9,     /* of 64 */
        .long_power_mix = 6, /* of 64 */
        .tone_length = FIRE_TONE_LENGTH,
        .tone_length_tol = FIRE_TONE_LENGTH_TOL-10,
        .tone_interval = FIRE_TONE_INTERVAL,
        .tone_interval_tol = FIRE_TONE_INTERVAL_TOL,
        .ma_pos_threshhold = 14,
        .ma_neg_threshhold = -9,
    },
    {   /* 3100 Hz Fire Alarm, slow timing (1.5s/beep) */
        .flag = kLTMediaAudioAlarmFlags_Smoke,
        .tone = 0,  /* 3100 Hz */
        .tones_per_alarm = 3,
        .power_mix = 7,     /* of 64 */
        .long_power_mix = 5, /* of 64 */
        .tone_length = SLOW_TONE_LENGTH,
        .tone_length_tol = SLOW_TONE_LENGTH_TOL-10,
        .tone_interval = SLOW_TONE_INTERVAL,
        .tone_interval_tol = SLOW_TONE_INTERVAL_TOL-15,
        .ma_pos_threshhold = 18,
        .ma_neg_threshhold = -9,
    },
    {   /* 520 Hz Fire Alarm, standard timing */
        .flag = kLTMediaAudioAlarmFlags_Smoke,
        .tone = 1,  /* 520 Hz */
        .tones_per_alarm = 3,
        .power_mix = 9,     /* of 64 */
        .long_power_mix = 6, /* of 64 */
        .tone_length = FIRE_TONE_LENGTH,
        .tone_length_tol = FIRE_TONE_LENGTH_TOL-15,
        .tone_interval = FIRE_TONE_INTERVAL,
        .tone_interval_tol = FIRE_TONE_INTERVAL_TOL,
        .ma_pos_threshhold = 18,
        .ma_neg_threshhold = -9,
    },
    {   /* 3100 Hz CO Detector, four fast beeps */
        .flag = kLTMediaAudioAlarmFlags_CarbonMonoxide,
        .tone = 0,  /* 3100 Hz */
        .tones_per_alarm = 4,
        .power_mix = 20,     /* of 64 */
        .long_power_mix = 13, /* of 64 */
        .tone_length = CO_TONE_LENGTH,
        .tone_length_tol = TONE_LENGTH_FROM_MS(25),
        .tone_interval = CO_TONE_INTERVAL,
        .tone_interval_tol = CO_TONE_INTERVAL_TOL,
        .ma_pos_threshhold = 8,
        .ma_neg_threshhold = -6,
    },
};

static s32 goertzel_lookup_coef(
    u32 sample_rate,
    u32 tone_hz )
{
    for ( unsigned i = 0; i < NUM_GOERTZEL_COEFS; i++ ) {
        if ( (s_goertzel_coef[i].sample_rate == sample_rate) &&
             (s_goertzel_coef[i].tone_hz == tone_hz) ) {
            return s_goertzel_coef[i].coef;
        }
    }
    return 0;
}

static void GoertzelAnalyzer_Initialize(
    struct GoertzelAnalyzer *ga,
    s64                coef )
{
    ga->q1 = ga->q2 = 0;
    ga->coef = coef;
}

static void GoertzelAnalyzer_Accumulate(
    struct GoertzelAnalyzer *ga,
    const s32               sample[],
    u32            nsamples )
{
    s64 q,q1,q2;
    /* load state */
    q1 = ga->q1; q2 = ga->q2;
    for ( u32 i = 0; i < nsamples; i++ )
    {
        q = (sample[i] + ((ga->coef * q1)>>14) - q2);
        q2 = q1;
        q1 = q;
    }
    /* store state */
    ga->q1 = q1; ga->q2 = q2;
}

static s64 GoertzelAnalyzer_GetPower(
    struct GoertzelAnalyzer *ga )
{
    s64 q;

    /* Calculate the power for nsamples window */
    q = (ga->q1 * ga->q1) +
        (ga->q2 * ga->q2) -
        (ga->q2 * ((ga->coef * ga->q1)>>14));

    // Preserve the power resolution for bigger swings

    return q;
}

static s64 GoertzelAnalyzer_Process(
    struct GoertzelAnalyzer *ga,
    const s32               sample[],
    u32                     nsamples )
{
    /* reset the state of the analyzer */
    GoertzelAnalyzer_Initialize(ga,ga->coef);
    GoertzelAnalyzer_Accumulate(ga,sample,nsamples);
    return GoertzelAnalyzer_GetPower(ga);
}

u32 log2_fixed3(u64 n)
{
    u64 i,frac,ret = 0;

    if ( 0 == n ) {
        return 0;
    }

    i = n;
    frac = 0;
    while( i ) {
        i >>= 1;
        ret += 1;
    }

    /* subtract one to properly remove the MSB
     * for example, if ret == 5, the number is
     * between 16 - 31, so we want to clear (1 << 4)
     */
    ret--;
    n &= ~(1UL << ret); /* clear the MSB */
    if ( ret < 5 ) {
        n <<= 5 - ret;
    } else {
        n >>= (ret - 5);
    }
    ret <<= 3;

    /* calculate fraction */
    if ( n >= 0xd ){         /* d41 */
        if ( n > 0x15 ){     /* 15d1 */
            if ( n > 0x1a ){ /* 1ab */
                frac = 7;
            } else {
                frac = 6;
            }
        } else {
            if ( n >= 0x11 ){ /* 1159 */
                frac = 5;
            } else {
                frac = 4;
            }
        }
    } else {
        if ( n >= 0x6 ){     /* 60d */
            if ( n >= 0x9 ){ /* 97f */
                frac = 3;
            } else {
                frac = 2;
            }
        } else {
            if ( n > 0x2 ){ /* 2e5 */
                frac = 1;
            } else {
                frac = 0;
            }
        }
    }
    ret += frac;
    return ret;
}

static void detect_initialize(
    struct ToneDetector *td )
{
    for ( int i = 0; i < ALARM_HISTORY_COUNT; i++ ) {
        td->ma_alarm_history[i].t0 = -ALARM_HINACTIVE_INTERVAL;
    }
}

static void detect_tone_power(
    struct ToneDetector *td )
{
    /* debug, get unmixed power */
    //td->tpow = log2_fixed3(tpow[td->tone]);
    td->tpow = tpow[td->tone];

    /* moving average: SHORT time mix */
    td->power = ((td->power*(64-td->power_mix)) +
                 (log2_fixed3(tpow[td->tone])*td->power_mix)+
                 32)>>6;

    /* moving average: LONG time mix */
    td->long_power = ((td->long_power*(64-td->long_power_mix)) +
                      (td->power*td->long_power_mix) +
                      32)>>6;

    s32 ma_delta;
    bool tone_just_ended = false;
    ma_delta = td->power - td->long_power;

    /* we've currently detected a tone */
    if ( td->ma_current_state > 0 ) {
        if ( ma_delta > td->ma_pos_peak ) {
            /* have we GREATLY exceeded the starting power crossing? */
            if ( (td->power - td->ma_tone_start_pow) > FAST_RISING_POWER_THRESHOLD ) {
                /* shave time off the start */
                td->ma_tone_start = td->t;
                td->ma_tone_start_pow = td->power;
            }

            td->ma_pos_peak = ma_delta;
        }
        /* END_OF_TONE: short moving average drops below long moving average */
        if ( ma_delta < td->ma_neg_threshhold ) {
            /* reset the negative peak */
            td->ma_neg_peak = td->ma_neg_threshhold;

            /* signal has dropped to negative */
            td->ma_current_state = -1;
            td->ma_tone_length = td->t - td->ma_tone_start;
            /* we have a tone length now */
            if ( (td->ma_tone_length > (td->tone_length - td->tone_length_tol)) &&
                 (td->ma_tone_length < (td->tone_length + td->tone_length_tol)) )
            {
                tone_just_ended = true;
            }
            else
            {
                //td->ma_tone_length = 0;
            }
        }
    } else if ( td->ma_current_state < 0 ) {
        /* keep stats */
        if ( ma_delta < td->ma_neg_peak ) {
            td->ma_neg_peak = ma_delta;
        }
        /* START_OF_TONE: short moving average rises above long moving average */
        if ( ma_delta > td->ma_pos_threshhold ) {
            /* reset the positive peak */
            td->ma_pos_peak = td->ma_pos_threshhold;

            /* signal has risen to positive */
            td->ma_current_state = 1;
            td->ma_tone_start = td->t;
            td->ma_tone_start_pow = td->power;
        }
    } else {
        /* will only happen once at startup */
        if ( ma_delta > td->ma_pos_threshhold ) {
            td->ma_current_state = 1;
        } else if ( ma_delta < td->ma_neg_threshhold ) {
            td->ma_current_state = -1;
        }
    }

    /* A tone of a valid length has just finished */
    if ( tone_just_ended ) {
        bool insert_the_tone = false;

        /* This is NOT first tone of a burst of three or four */
        if ( td->ma_tone_count ) {
            /* We know this tone is the proper length from above.
             * CHECK: Is it at the appropriate interval from the last tone?
             */
            if ( ( (td->ma_tone_start - td->ma_detection[0].t0) >
                   (td->tone_interval - td->tone_interval_tol)) &&
                 ( (td->ma_tone_start - td->ma_detection[0].t0) <
                   (td->tone_interval + td->tone_interval_tol)) ) {
                insert_the_tone = true;
            } else {
                /* has it been a long time since the last tone? */
                if ( ( (td->ma_tone_start - td->ma_detection[0].t0) >
                       (td->tone_interval + td->tone_interval_tol)) )
                {
                    /* restart the tone counter */
                    td->ma_tone_count = 0;
                    insert_the_tone = true;
                } else {
                    /* A tone has arrived early!?!?
                     * This or the previous tone could be a false trigger.
                     *
                     * TODO: Decide whether to reject this tone or the
                     * previous one.  If we do nothing this tone will
                     * be discarded even though it might be the correct
                     * one to use.
                     */
                }
            }
        } else {
            /* set the base time of this to look for a second burst */
            insert_the_tone = true;
        }

        if ( insert_the_tone ) {
            /* roll detections out by one */
            for ( int i = TONE_DETECTION_COUNT - 1; i > 0; i-- ) {
                td->ma_detection[i] = td->ma_detection[i-1];
            }
            /* insert this at the beginning of the array */
            td->ma_detection[0].t0 = td->ma_tone_start;
            td->ma_detection[0].duration = td->t - td->ma_tone_start;

            /* Is this going to be our last tone required to set the alarm */
            if ( td->ma_tone_count == (td->tones_per_alarm - 1)) {
                /* We've got a burst of three/four tones */
                td->ma_alarm_count++;

                /* shift in the current alarm time into the history */
                for ( int i = ALARM_HISTORY_COUNT - 1; i > 0; i-- ) {
                    td->ma_alarm_history[i] = td->ma_alarm_history[i-1];
                }
                /* record the time the first tone started */
                td->ma_alarm_history[0].t0 =
                    td->ma_detection[td->tones_per_alarm - 1].t0;
            }

            /* This is just here to constrain the plot visualization tool */
            if ( ++td->ma_tone_count >= td->tones_per_alarm ) {
                td->ma_tone_count = td->tones_per_alarm;
            }
        } else {
            /* ignore the tone, could be random noises showing up from other sounds.
             * human conversation often has 3.1 kHz components, and often has the
             * pattern of a CO alarm.
             */
        }
    }

    /* see if alarms should be activated or deactivated */
    if ( td->ma_alarm_history_active ) {
        /* if the most recent alarm was too long ago */
        if ( (td->t - td->ma_alarm_history[0].t0) > ALARM_HINACTIVE_INTERVAL ) {
            td->ma_alarm_history_active = 0;
        }
    } else {
        u32 recent_alarms = 0;
        for ( int i = 0; i < ALARM_HISTORY_COUNT; i++ ) {
            if ( (td->t - td->ma_alarm_history[i].t0) < ALARM_HACTIVE_INTERVAL ) {
                recent_alarms++;
            }
        }
        if ( recent_alarms >= ALARMS_DETECTED_TO_TRIGGER ) {
            td->ma_alarm_history_active = 1;
        }
    }

    if ( td->ma_current_state <= 0 ) {
        td->ma_background_pow = ((td->ma_background_pow*(64-td->long_power_mix)) +
                          (td->long_power*td->long_power_mix) +
                          32)>>6;
    }

    /* If we're not currently detecting a tone
     * allow timeout since last detected tone
     */
    if ( (td->ma_current_state <= 0) &&
         ((td->t - td->ma_detection[0].t0) > (td->tone_interval + td->tone_interval_tol)) ) {
        td->ma_tone_count = 0;
    }

    /* increment time in ticks */
    td->t++;
}

/* ---------------------------------------------------------------------------- */
/* ---------------------------------------------------------------------------- */
/* External API */
/* ---------------------------------------------------------------------------- */
/* ---------------------------------------------------------------------------- */

static LTMediaAudioAlarmFlags s_AlarmState = kLTMediaAudioAlarmFlags_None;
static s32 rsamples[ALARM_DETECT_NUM_SAMPLES];
static u32 rsamples_read;
static u32 tone[NUM_TONES] = {3100,520};

static LTMediaAudioAlarmFlags AudioAlarmDetect_PushSamples(
    LTMediaAudioAlarmFlags flags,
    const s16 *pPCM,
    LT_SIZE    sampleRate,
    bool       isStereo,
    LT_SIZE    nSamples )
{
    /* start with the current alarm state */
    LTMediaAudioAlarmFlags result = s_AlarmState;

    LT_SIZE     sample_pitch;
    LT_SIZE     input_offset = 0;
    s32         sample_max = 0;

    sample_pitch = sampleRate / ALARM_DETECT_SAMPLE_RATE;
    if ( isStereo ) {
        sample_pitch *= 2;
    }

    /* copy the samples into our analysis buffer */
    for ( input_offset = 0; input_offset < nSamples; input_offset += sample_pitch ) {
        rsamples[rsamples_read++] = (s32) pPCM[input_offset];
        if ( pPCM[input_offset] >= 0 ) {
            if (pPCM[input_offset] > sample_max)
                sample_max = pPCM[input_offset];
        } else {
            if (pPCM[input_offset] < -sample_max)
                sample_max = -pPCM[input_offset];
        }

        /* we've completed a block of input */
        if ( ALARM_DETECT_NUM_SAMPLES == rsamples_read ){
            LT_SIZE n;

            /* Do power detection for the different frequencies */
            for ( n = 0; n < NUM_TONES; n++ ) {
                s32 coef;
                coef = goertzel_lookup_coef(ALARM_DETECT_SAMPLE_RATE,tone[n]);
                GoertzelAnalyzer_Initialize(&ga[n],coef);
                tpow[n] = GoertzelAnalyzer_Process(&ga[n],rsamples,ALARM_DETECT_NUM_SAMPLES);

              #ifdef DEBUG_ALARM_DETECT_C
                if ( 1 && 0 == n ) { /* Using this as stimulus to the test simulator */
                    LTLOG_DEBUG_RETAIL("goertzel.power","max %d tpow %lld",
                        sample_max,
                        tpow[n]
                        );
                }
              #endif /* DEBUG_ALARM_DETECT_C */
            }

            for ( n = 0; n < NUM_DETECTORS; n++ ) {
                if (flags & detect[n].flag) {
                    detect_tone_power(&detect[n]);

                    if ( detect[n].ma_alarm_history_active ) {
                        s_AlarmState |= detect[n].flag;
                    } else {
                        s_AlarmState &= ~detect[n].flag;
                    }
                    /* bitwise OR so we don't lose dropped detections during analysis */
                    result |= s_AlarmState;
                #ifdef DEBUG_ALARM_DETECT_C
                    if ( 0 && 0 == n ) {
                        u32 sample_log;
                        u32 ch = 3;
                        sample_log = log2_fixed3(sample_max);

                        LTLOG_DEBUG_RETAIL("goertzel.results","%1x p:%2d.%03d [tp %lld s %2d c %1d sp %3d tl %2d p %3d lp %3d bp %3d]",
                            result, (sample_log >> 3), (sample_log & 0x7) * 125,
                            detect[ch].tpow,
                            detect[ch].ma_current_state,
                            detect[ch].ma_tone_count,
                            detect[ch].ma_tone_start_pow,
                            detect[ch].ma_tone_length,
                            detect[ch].power,
                            detect[ch].long_power,
                            detect[ch].ma_background_pow
                        );
                    }
                #endif /* DEBUG_ALARM_DETECT_C */
                }
            }
            /* go back to the beginning of the input buffer */
            rsamples_read = 0;
        }
    }

  #ifdef DEBUG_ALARM_DETECT_C
    if ( 0 ) {
        u32 sample_log;

        char  dstr[9];
        dstr[0] = detect[0].ma_tone_count ? '0'+ detect[0].ma_tone_count: ' ';
        dstr[1] = detect[0].ma_alarm_history_active ? '!' : '.';
        dstr[2] = detect[1].ma_tone_count ? '0'+ detect[1].ma_tone_count: ' ';
        dstr[3] = detect[1].ma_alarm_history_active ? '!' : '.';
        dstr[4] = detect[2].ma_tone_count ? '0'+ detect[2].ma_tone_count: ' ';
        dstr[5] = detect[2].ma_alarm_history_active ? '!' : '.';
        dstr[6] = detect[3].ma_tone_count ? '0'+ detect[3].ma_tone_count: ' ';
        dstr[7] = detect[3].ma_alarm_history_active ? '!' : '.';
        dstr[8] = '\0';

        sample_log = log2_fixed3(sample_max);

        LTLOG_DEBUG_RETAIL("goertzel.results","%1x p:%2d.%03d [%s %3d-%3d %3d-%3d %3d-%3d %3d-%3d]",
            result, (sample_log >> 3), (sample_log & 0x7) * 125, dstr,
            detect[0].long_power, detect[0].ma_tone_length,
            detect[1].long_power, detect[1].ma_tone_length,
            detect[2].long_power, detect[2].ma_tone_length,
            detect[3].long_power, detect[3].ma_tone_length
        );
    }
  #endif /* DEBUG_ALARM_DETECT_C */


    return result;
}

void AudioAlarmDetect_Initialize( void )
{
    for ( int n = 0; n < NUM_DETECTORS; n++ ) {
        detect_initialize(&detect[n]);
    }
}

LTMediaAudioAlarmFlags AudioAlarmDetect_Analyze(
    LTMediaAudioAlarmFlags flags,
    const s16 *pPCM,
    LT_SIZE    sampleRate,
    bool       isStereo,
    LT_SIZE    nSamples )
{
    return( AudioAlarmDetect_PushSamples(flags, pPCM, sampleRate, isStereo, nSamples) );
}
