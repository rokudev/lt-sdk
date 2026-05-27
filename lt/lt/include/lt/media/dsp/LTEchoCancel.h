/******************************************************************************
 * <lt/media/dsp/LTEchoCancel.h>                           LT Echo Cancellation
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltmedia_dsp_echocancel LTEchoCancel
 * @ingroup ltmedia_dsp
 * @{
 *
 * @brief Echo Cancellation utilities.
 *
 */

#ifndef ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTECHOCANCEL_H
#define ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTECHOCANCEL_H

#include <lt/LTObject.h>

LT_EXTERN_C_BEGIN

typedef struct LTEchoCancel LTEchoCancel;

/*_________________
 / LTEchoCancel Object */
typedef_LTObject(LTEchoCancel, 1) {

    bool (*InitState)(LTEchoCancel *echo, u16 frame_size, u16 filter_length, u32 sample_rate);
    /** Init a new echo canceller state
     * @param echo Echo canceller
     * @param frame_size Number of samples to process at one time (should correspond to 10-20 ms)
     * @param filter_length Number of samples of echo to cancel (should generally correspond to 100-500 ms)
     * @return Newly-created echo canceller state
     */

    void (*GetConfig)(LTEchoCancel *echo, u16 *pFrame_size, u32 *pSample_rate);
    /** Get Current configuration of the echo canceller
     * @param echo Echo canceller
     * @param pFrame_size size of each frame in samples
     * @param pSample_rate sample rate
    */

    void (*DestroyState)(LTEchoCancel *echo);
    /** Destroys an echo canceller state
     * @param echo Echo canceller
    */

    void (*Capture)(LTEchoCancel *echo, const s16 *rec, s16 *out);
    /** Perform echo cancellation using internal playback buffer, which is delayed by two frames
     * to account for the delay introduced by most soundcards (but it could be off!)
     * @param echo Echo canceller
     * @param rec Signal from the microphone (near end + far end echo)
     * @param out Returns near-end signal with echo removed
    */

    void (*Playback)(LTEchoCancel *echo, const s16 *play);
    /** Let the echo canceller know that a frame was just queued to the soundcard
     * @param echo Echo canceller
     * @param play Signal played to the speaker (received from far end)
    */

    void (*ResetState)(LTEchoCancel *echo);
    /** Reset the echo canceller to its original state
     * @param echo Echo canceller
     */

    void (*EnableNoiseSuppression)(LTEchoCancel *echo, bool enable);
    /** Enable/Disable noise suppression
     * @param enable {true,false}
     */

} LTOBJECT_API;

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_MEDIA_DSP_LTECHOCANCEL_H */
