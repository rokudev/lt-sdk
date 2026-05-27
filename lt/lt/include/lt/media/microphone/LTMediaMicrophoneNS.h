/******************************************************************************
 * <lt/media/microphone/LTMediaMicrophoneNS.h      Microphone Noise Suppression
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltmedia_microphone_NS LTMediaMicrophoneNS
 * @ingroup ltmedia
 * @{
 *
 * @brief Webrtc NS routines
 *
 */

#ifndef ROKU_LT_INCLUDE_LT_MEDIA_MICROPHONE_LTMEDIAMICROPHONENS_H
#define ROKU_LT_INCLUDE_LT_MEDIA_MICROPHONE_LTMEDIAMICROPHONENS_H

#include <lt/LT.h>

LT_EXTERN_C_BEGIN

typedef enum {
    kLTMediaMicrophoneNS_Mode_Mild6db,
    kLTMediaMicrophoneNS_Mode_Medium10db,
    kLTMediaMicrophoneNS_Mode_Aggressive15db
} LTMediaMicrophoneNS_Mode;

/*_________________
 / LTMediaMicrophoneNS Object */
typedef_LTObject(LTMediaMicrophoneNS, 1) {

    void (*Process)(LTMediaMicrophoneNS *ns, const s16 *const *inFrame, int numBands, s16 *const *outFrame);
    /**< Perform noise suppression on a 10 ms frame
     *
     * @param ns NS instance
     * @param inFrame Input speech frame for each band
     * @param numBands Number of bands in input/output vector
     * @param outFrame Output speech frame for each band  */

    int (*SetPolicy)(LTMediaMicrophoneNS *ns, LTMediaMicrophoneNS_Mode mode);
    /**< Set aggressiveness of the noise suppression method.
     *
     * @param ns NS instance
     * @param mode 0: Mild (6 dB), 1: Medium (10 dB), 2: Aggressive (15 dB)
     * @return error
     *              :  0 - Normal operation.
     *              : -1 - Error  */

    s32 (*Init)(LTMediaMicrophoneNS *ns, u32 fs);
    /**< Initialize noise suppression.
     *
     * @param ns NS instance.
     * @param fs sampling rate
     *
     * @return error
     *              :  0 - Ok
     *              : -1 - Error  */

} LTOBJECT_API;

/** @} */

LT_EXTERN_C_END

#endif /* ROKU_LT_INCLUDE_LT_MEDIA_MICROPHONE_LTMEDIAMICROPHONENS_H */