/*******************************************************************************
 * <lt/device/analogmic/LTDeviceAnalogMic.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltdevice_analogmic LTDeviceAnalogMic
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for analogmic functions.
 *
 * Provides general control for initializing, gain setting, 
 * audio capture start/stop.
 *
 * LT Libraries use LTDeviceAnalogMic to interact with the analog microphone.
 */

#ifndef LT_INCLUDE_LT_DEVICE_ANALOGMIC_LTDEVICEANALOGMIC_H
#define LT_INCLUDE_LT_DEVICE_ANALOGMIC_LTDEVICEANALOGMIC_H

#include <lt/core/LTTime.h>
#include <lt/core/LTThread.h>
LT_EXTERN_C_BEGIN

typedef void (LTDeviceAMicAudioCallback)(void *data, u16 framesize);

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceAnalogMic, 1);
/** LTDeviceAnalogMic Library ROOT INTERFACE */
struct LTDeviceAnalogMic {

    INHERIT_DEVICE_LIBRARY_BASE
    bool (* StartCap)(LTDeviceAMicAudioCallback *fp);
        /**< Start Audio capture. The callback will fire with samples buffers.
         *   @param[in] fp The callback function to receive audio samples.
         *   @return Success status. */
    bool (* StopCap)(void);
        /**< Stop capturing samples.
         *   @return Success status. */
    void (* SetGain)(int);
        /**< Set Gain value for Analog Microphone.
         *   @param[in] gain The gain value to set.
         *   @return N/A. */
    int (* GetGain)(void);
        /**< Get Gain value for Analog Microphone.
         *   @return The current gain value. */
    bool (* SetBuffSize)(int sz, s16* buf);
        /**< Set buffer size for Analog Microphone.
         *   @param[in] sz The buffer size in s16 samples to set.
         *   @param[in] buf The buffer to use for audio DMA.
         *   @return N/A. */
};

#ifndef DOXY_SKIP // [

/* This interface is to be used only by LTDeviceAnalogMic. */
typedef_LTLIBRARY_INTERFACE(ILTDriverAnalogMic, 1) {
    bool (* StartCap)(LTDeviceAMicAudioCallback *fp);
        /**< Start Audio capture. The callback will fire with samples buffers.
         *   @param[in] fp The callback function to receive audio samples.
         *   @return Success status. */
    bool (* StopCap)(void);
        /**< Stop capturing samples.
         *   @return Success status. */
    void (* SetGain)(int);
        /**< Set Gain value for Analog Microphone.
         *   @param[in] gain The gain value to set.
         *   @return N/A. */
    int (* GetGain)(void);
        /**< Get Gain value for Analog Microphone.
         *   @return The current gain value. */
    bool (* SetBuffSize)(int sz, s16* buf);
        /**< Set buffer size for Analog Microphone.
         *   @param[in] sz The buffer size to set.
         *   @param[in] buf The buffer to use for audio DMA.
         *   @return N/A. */
} LTLIBRARY_INTERFACE;

#endif // DOXY_SKIP  ]

/** @} */

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_ANALOGMIC_LTDEVICEANALOGMIC_H
