/*******************************************************************************
 * <lt/device/adc/LTDeviceADC.h> LTDeviceADC
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup LTDeviceADC LTDeviceADC
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for reading voltages from A/D converters.
 *
 * This Library provides access to a platform's A/D converters, which are
 * represented, and operated on, by Device Unit Handles which are supplied
 * by the underlying Driver Library.  A Handle represents one A/D "channel".
 */

#ifndef LT_INCLUDE_LT_DEVICE_ADC_LTDEVICEADC_H
#define LT_INCLUDE_LT_DEVICE_ADC_LTDEVICEADC_H

#include <lt/LTTypes.h>
#include <lt/LTObject.h>
#include <lt/core/LTTime.h>

LT_EXTERN_C_BEGIN

/*______________________________________________________________________________
   A/D Channel Object Interface
        In order to access an A/D-converter channel, a client calls
        GetChannel() (see below) to obtain an LTObject representing
        the channel, then calls GetSample() on the LTObject, as in:

            LTADCChannel *pChannel = pDeviceADC->GetChannel("encabulator_output_mV");
               (...)
            u32 sample = 0;
            if (pObject->API->GetSample(&sample)) {
                (do something with sample)
            }

        When finished, the client may dispose of the object with:

            lt_destroyobject(pChannel);

        Channels provided by the Driver may include "raw" channels (returning
        as the sample the unconverted reading from the A/D (e.g., bvat_raw), a
        reading converted to a real-world unit (e.g., bvat_ms), or the minimum
        or maximum possible raw or converted reading of an actual channel (e.g.,
        temp_raw_max).                                                        */

typedef bool (SampleCallback)(u32 sample, void *callbackData);

typedef_LTObject(LTADCChannel, 1) {

    bool (*GetSample)(LTADCChannel *pThis, u32 *pSample);
        /**<
         * @brief Sample the channel.  Units are platform- and channel-specific.
         *
         * @param pThis pointer to the channel object
         * @param pSample where to store the sample
         * @return true if the sample was taken, false otherwise
         */

    bool (*GetSamples)(LTADCChannel *pThis, u32 count, SampleCallback *pSampleCallback, void *clientData);
        /**<
         * @brief Use an ADC channel to get samples.
         * 
         * @param pThis            pointer to the channel object
         * @param count            count of samples to get
         * @param pSampleCallback  callback when getting a sample, typedef as SampleCallback
         * @param clientData       data for use by callback, if needed
         * @return true if all samples were taken, false otherwise.
         * @note  This is a block IO.
         */

} LTOBJECT_API;

/*______________________________________________________________________________
   Device Library Root Interface                                              */

typedef bool (LTDeviceADC_EnumerateChannelProc)(const char *pChannelName, void *pClientData);
    /**<
     * @brief Callback for channel enumeration
     *
     * @param pClientData client data pointer
     * @return true to continue enumeration, false to stop
     */

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTDeviceADC, 1);

struct LTDeviceADCApi {

    INHERIT_LIBRARY_BASE

    LTADCChannel *(*GetChannel)(u32 nChannelIndex);
        /**<
         * @brief Create a Device Unit Object for the A/D input channel of the
         *        given index
         *
         * @param nChannelIndex the index of the A/D input channel
         * @return the channel object, or NULL if no such channel exists
         *
         * @see %GetChannelByName
         */

    LTADCChannel *(*GetChannelByName)(char const *pChannelName);
        /**<
         * @brief Create a Device Unit Object for the A/D input channel of the
         *        given name
         *
         * @param pChannelName the name of the channel
         * @return the channel object, or NULL if no such channel exists
         *
         * @see %GetChannel
         */

    u32 (*GetNumChannels)(void);
        /**<
         * @brief Obtain the number of A/D channels available through LTDeviceADC
         *
         * @return the number of A/D channels
         */
        
    u32 (*EnumerateChannels)(LTDeviceADC_EnumerateChannelProc *pChannelEnumerationProc, void *pClientData);
        /**<
         * @brief Enumerate the channels available by LTDeviceADC
         *
         * For Unit Tests, to create a list of channels to test.
         *
         * @param pChannelEnumerationProc callback to return the channel name
         * @param pClientData client data pointer
         * @return the number of channels enumerated
         */
};

/*______________________________________________________________________________
   Driver Library Root Interface                                              */

#ifndef DOXY_SKIP // [
/* LTDriverADC Interface - this interface is to be used only by LTDeviceADC. */
typedef_LTLIBRARY_INTERFACE(ILTDriverADC, 1) {

    LTADCChannel *(*GetChannel)(u32 nChannelIndex);
        /**<
         * @brief Create a Device Unit Object for the A/D input channel of the
         *        given index
         *
         * @param nChannelIndex the index of the A/D input channel
         * @return the channel object, or NULL if no such channel exists
         *
         * @see %GetChannelByName
         */

    LTADCChannel *(*GetChannelByName)(char const *pChannelName);
        /**<
         * @brief Create a Device Unit Object for the A/D input channel of the
         *        given name
         *
         * @param pChannelName the name of the channel
         * @return the channel object, or NULL if no such channel exists
         *
         * @see %GetChannel
         */

    u32 (*GetNumChannels)(void);
        /**<
         * @brief Obtain the number of A/D channels made available by the Driver
         *
         * @return the number of A/D channels
         */

    u32 (*EnumerateChannels)(LTDeviceADC_EnumerateChannelProc *pChannelEnumerationProc, void *pClientData);
        /**<
         * @brief Enumerate the channels made available by the Driver
         *
         * For Unit Tests, to create a list of channels to test.
         *
         * @param pChannelEnumerationProc callback to return the channel name
         * @param pClientData client data pointer
         * @return the number of channels enumerated
         */

} LTLIBRARY_INTERFACE;
#endif  // DOXY_SKIP ]

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_ADC_LTDEVICEADC_H */

/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Nov-23   nero        created
 *  31-Jan-24   constantine Rework to make generic
 */
