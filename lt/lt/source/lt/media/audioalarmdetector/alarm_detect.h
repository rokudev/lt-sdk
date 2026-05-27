/******************************************************************************
 * alarm_detect.h
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/media/audioalarmdetector/LTMediaAudioAlarmDetection.h>

/** PCM Audio sample rate to feed the detector */
#define ALARM_DETECT_SAMPLE_RATE 8000

void AudioAlarmDetect_Initialize( void );

LTMediaAudioAlarmFlags AudioAlarmDetect_Analyze(
    LTMediaAudioAlarmFlags flags,
    const s16 *pPCM,
    LT_SIZE    sampleRate,
    bool       isStereo,
    LT_SIZE    nSamples );
