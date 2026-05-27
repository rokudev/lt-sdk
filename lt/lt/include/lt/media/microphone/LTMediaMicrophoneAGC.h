/******************************************************************************
 * <lt/media/microphone/LTMediaMicrophoneAGC.h>                  Microphone AGC
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltmedia_microphone_agc LTMediaMicrophoneAGC
 * @ingroup ltmedia
 * @{
 *
 * @brief Microphone AGC
 *
 */

#ifndef ROKU_LT_INCLUDE_LT_MEDIA_MICROPHONE_LTMEDIAMICROPHONEAGC_H
#define ROKU_LT_INCLUDE_LT_MEDIA_MICROPHONE_LTMEDIAMICROPHONEAGC_H

#include <lt/LT.h>

LT_EXTERN_C_BEGIN

typedef struct {
    s16 targetLevelDbfs;    /**< default 3 (-3 dBOv) TODO: Clarify this */
    s16 compressionGaindB;  /**< default 9 dB */
    u8  limiterEnable;      /**< default kAgcTrue (on) */
} LTMediaMicrophoneAGC_Config;

typedef enum {
    kLTMediaMicrophoneAGC_Mode_Unchanged,
    kLTMediaMicrophoneAGC_Mode_AdaptiveAnalog,
    kLTMediaMicrophoneAGC_Mode_AdaptiveDigital,
    kLTMediaMicrophoneAGC_Mode_FixedDigital
} LTMediaMicrophoneAGC_Mode;

typedef struct {
    s32 minLevel;
    s32 maxLevel;
} LTMediaMicrophoneAGC_Level;

/*__________________________
 / LTMediaMicrophoneAGC Object */
typedef_LTObject(LTMediaMicrophoneAGC, 1) {

    void (*SetMicVol)(LTMediaMicrophoneAGC *agc, s32 micVol);
    /**< Sets the microphone volume.
     * This is used in AdaptiveDigital mode to set the initial gain instead of
     * starting from no gain.
     *
     * @param agc AGC instance
     * @param micVol Microphone volume.
     */

    int (*GetAddFarEndError)(LTMediaMicrophoneAGC *agc, LT_SIZE samples);
    /**< Analyses the number of samples passed to the far end.
     *
     * @param agc AGC instance
     * @param samples Number of samples in input vector.
     * @return error
     *             :  0 - Normal operation.
     *             : -1 - Error.
     */

    int (*AddFarEnd)(LTMediaMicrophoneAGC *agc, const s16 *inFar, LT_SIZE samples);
    /**< Processes a 10 ms frame of far-end speech to determine
     * if there is active speech. The length of the input speech vector must be
     * given in samples (80 when FS=8000, and 160 when FS=16000, FS=32000 or
     * FS=48000).
     *
     * @param agc AGC instance.
     * @param inFar Far-end input speech vector
     * @param samples Number of samples in input vector
     * @return error
     *              :  0 - Normal operation.
     *              : -1 - Error
     */

    int (*AddMic)(LTMediaMicrophoneAGC *agc,
                     s16 *const *inMic,
                     LT_SIZE numBands,
                     LT_SIZE samples);
    /**< Processes a 10 ms frame of microphone speech to determine
     * if there is active speech. The length of the input speech vector must be
     * given in samples (80 when FS=8000, and 160 when FS=16000, FS=32000 or
     * FS=48000). For very low input levels, the input signal is increased in level
     * by multiplying and overwriting the samples in inMic[].
     *
     *  should be called before any further processing of the
     * near-end microphone signal.
     *
     * @param agc AGC instance.
     * @param inMic Microphone input speech vector for each band
     * @param numBands Number of bands in input vector
     * @param samples Number of samples in input vector
     *
     * @return error
     *              :  0 - Normal operation.
     *              : -1 - Error
     */

    int (*VirtualMic)(LTMediaMicrophoneAGC *agc,
                         s16 *const *inMic,
                         LT_SIZE numBands,
                         LT_SIZE samples,
                         s32 micLevelIn,
                         s32 *micLevelOut);
    /**< Replaces the analog microphone with a virtual one.
     * It is a digital gain applied to the input signal and is used in the
     * agcAdaptiveDigital mode where no microphone level is adjustable. The length
     * of the input speech vector must be given in samples (80 when FS=8000, and 160
     * when FS=16000, FS=32000 or FS=48000).
     *
     * @param agc AGC instance.
     * @param inMic Microphone input speech vector for each band
     * @param numBands Number of bands in input vector
     * @param samples Number of samples in input vector
     * @param micLevelIn Input level of microphone (static)
     * @param inMic Microphone output after processing (L band)
     * @param inMic_H Microphone output after processing (H band)
     * @param micLevelOut Adjusted microphone level after processing
     *
     * @return error
     *              :  0 - Normal operation.
     *              : -1 - Error
     */

    int (*Analyze)(LTMediaMicrophoneAGC *agc, const s16 *const *inNear,
                      LT_SIZE numBands,
                      LT_SIZE samples,
                      s32 inMicLevel,
                      s32 *outMicLevel,
                      s16 echo,
                      u8 *saturationWarning,
                      s32 gains[11]);
    /**< Analyzes 10 ms frame and produces the analog and digital gains required to normalize the signal.
    *
    * The gain adjustments are done only during active periods of speech. The length
    * of the speech vectors must be given in samples (80 when FS=8000, and 160 when FS=16000,
    * FS=32000 or FS=48000). The echo parameter can be used to ensure the AGC will not adjust
    * upward in the presence of echo.
    *
    *  should be called after processing the near-end microphone
    * signal, in any case after any echo cancellation.
    *
    * @param agc AGC instance
    * @param inNear Near-end input speech vector for each band
    * @param numBands Number of bands in input/output vector
    * @param samples Number of samples in input/output vector
    * @param inMicLevel Current microphone volume level
    * @param echo Set to 0 if the signal passed to add_mic is
    *                            almost certainly free of echo; otherwise set
    *                            to 1. If you have no information regarding echo
    *                            set to 0.
    * @param outMicLevel Adjusted microphone volume level
    * @param saturationWarning A returned value of 1 indicates a saturation event
    *                            has occurred and the volume cannot be further
    *                            reduced. Otherwise will be set to 0.
    * @param gains Vector of gains to apply for digital normalization
    *
    * @return error
    *              :  0 - Normal operation.
    *              : -1 - Error
    */

    int (*Process)(const LTMediaMicrophoneAGC *agc,
                      const s32 gains[11],
                      const s16 *const *in_near,
                      LT_SIZE numBands,
                      s16 *const *out);
    /**< Process 10 ms frame by applying precomputed digital gains.
    *
    * @param agc AGC instance
    * @param gains Vector of gains to apply for digital normalization
    * @param in_near Near-end input speech vector for each band
    * @param numBands Number of bands in input/output vector
    * @param out Gain-adjusted near-end speech vector
    *            May be the same vector as the input.
    *
    * @return error
    *              :  0 - Normal operation.
    *              : -1 - Error
    */

    int (*SetConfig)(LTMediaMicrophoneAGC *agc, LTMediaMicrophoneAGC_Config config);
    /**< Set the config parameters
    *
    * @param agc AGC instance
    * @param config config struct
    *
    * @return error
    *              :  0 - Normal operation.
    *              : -1 - Error
    */

    int (*GetConfig)(LTMediaMicrophoneAGC *agc, LTMediaMicrophoneAGC_Config *config);
    /**< Return config parameters (targetLevelDbfs, compressionGaindB and limiterEnable).
    *
    * @param agc AGC instance
    *
    * @param config config struct
    *
    * @return error
    *              :  0 - Normal operation.
    *              : -1 - Error
    */

    int (*Init)(LTMediaMicrophoneAGC *agc, s32 minLevel, s32 maxLevel, LTMediaMicrophoneAGC_Mode agcMode, u32 fs);
    /**< Initialize an AGC instance.
    *
    * @param agc AGC instance.
    * @param minLevel Minimum possible mic level
    * @param maxLevel Maximum possible mic level
    * @param agcMode  AGC Mode
    * @param fs Sampling frequency
    *
    * @return error
    *              :  0 - Ok
    *              : -1 - Error
    */

} LTOBJECT_API;

/** @} */

LT_EXTERN_C_END

#endif /* #ifndef ROKU_LT_INCLUDE_LT_MEDIA_MICROPHONE_LTMEDIAMICROPHONEAGC_H */