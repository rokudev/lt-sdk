/*******************************************************************************
 * Esp32DriverADCImpl.c
 *
 * Esp32 LT Driver Library for ADC functions
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
/** @file Esp32DriverADCImpl.c Implementation of ADC driver */

/*******************************************************************************
 * NOTE: This is an unimplemented skeleton driver.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/adc/LTDeviceADC.h>
#include <lt/device/gpio/LTDeviceGpio.h>

DEFINE_LTLOG_SECTION("esp32.drv.adc");

/* This will probably get moved to the Device Config, but for now: */
typedef struct ChannelConfiguration {
    const char *pChannelName;
    u32 (*pChannelAcquisitionProc)(void);
} ChannelConfiguration;

enum {
    kNumCommonChannels = 2,
    kNumTotalChannels = 3
};
static u8   s_numCurrentChannels = kNumCommonChannels;
static LTMutex *s_pMutex = NULL;

// Gpio interface ptr needed to access named pins
static LTDeviceGpio * s_pGpio = NULL;

// Implemented within bouffalo_bl_adc.c
unsigned int Roku_SDK_GetVbat(void);
unsigned int Roku_SDK_GetTemperature(void);
unsigned int Roku_SDK_AdcGetGpioValueInMillivolts(u8 pin);

static u32 Esp32DriverADCChannel_GetVbat(void) {
    return 3300; /* millivolts */
}

static u32 Esp32DriverADCChannel_GetTemperature(void) {
    return 190; /* deci-celcius - 190 = 19 degrees */
}

static u32 Esp32DriverADCChannel_GetAlsValue(void) {
    return 0;
}

static const ChannelConfiguration s_ChannelConfiguration[kNumTotalChannels] = {
    { "vbat", Esp32DriverADCChannel_GetVbat },
    { "temp", Esp32DriverADCChannel_GetTemperature },
    { "als",  Esp32DriverADCChannel_GetAlsValue }
};

typedef_LTObjectImpl(LTADCChannel, Esp32DriverADCChannel) {
    const ChannelConfiguration *pChannelConfiguration;
} LTOBJECT_API;

static bool Esp32DriverADCChannel_ConstructObject(Esp32DriverADCChannel *pThis) { LT_UNUSED(pThis); return true; }
static void Esp32DriverADCChannel_DestructObject(Esp32DriverADCChannel *pThis) { LT_UNUSED(pThis); }

static bool Esp32DriverADCChannel_GetSample(Esp32DriverADCChannel *pThis, u32 *pSample) {
    if (!pThis->pChannelConfiguration->pChannelAcquisitionProc) return false;

    // Grab the lock
    s_pMutex->API->Lock(s_pMutex);

    // Perform the ADC conversion for this channel
    *pSample = pThis->pChannelConfiguration->pChannelAcquisitionProc();

    // Release the lock
    s_pMutex->API->Unlock(s_pMutex);

    return true;
}

static LTADCChannel *Esp32DriverADCChannel_GetChannel(u32 nChannel) {
    Esp32DriverADCChannel *pChannel = (Esp32DriverADCChannel *)lt_createobject_typed(LTADCChannel, Esp32DriverADCChannel);
    if (pChannel) {
        pChannel->pChannelConfiguration = &s_ChannelConfiguration[nChannel];
    }
    return (LTADCChannel *)pChannel;
}

static LTADCChannel *Esp32DriverADCChannel_GetChannelByName(const char *pChannelName) {
    for (u32 i = 0; i < s_numCurrentChannels; ++i)
        if (!lt_strcasecmp(pChannelName, s_ChannelConfiguration[i].pChannelName))
            return Esp32DriverADCChannel_GetChannel(i);
    return NULL;
}

static u32 Esp32DriverADCImpl_GetNumChannels(void) {
    return s_numCurrentChannels;
}

static u32 Esp32DriverADCImpl_EnumerateChannels(LTDeviceADC_EnumerateChannelProc *pChannelEnumerateProc, void *pClientData) {
    u32 n = 0;
    for (; n < s_numCurrentChannels && pChannelEnumerateProc(s_ChannelConfiguration[n].pChannelName, pClientData); ++n);
    return n + 1;
}

//
// Boilerplate functions needed for an LT driver
//

static bool Esp32DriverADCImpl_LibInit(void) {
    s_pGpio = lt_createdeviceobject(LTDeviceGpio);
    if (!s_pGpio) {
        LTLOG_REDALERT("init.create.dev.gpio.fail", NULL);
        return false;
    }

    // Create a mutex to guard access to the single ADC block
    s_pMutex = lt_createobject(LTMutex);

    LTLOG("init", "Number of available ADC channels: %u", s_numCurrentChannels);

    return true;
}

static void Esp32DriverADCImpl_LibFini(void) {
    lt_destroyobject(s_pGpio);
    lt_destroyobject(s_pMutex);
}

define_LTObjectImplPublic(LTADCChannel, Esp32DriverADCChannel, GetSample);

typedef_LTLIBRARY_ROOT_INTERFACE(Esp32DriverADC, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(Esp32DriverADC) LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTDriverADC) {
    .GetChannel        = Esp32DriverADCChannel_GetChannel,
    .GetChannelByName  = Esp32DriverADCChannel_GetChannelByName,
    .GetNumChannels    = Esp32DriverADCImpl_GetNumChannels,
    .EnumerateChannels = Esp32DriverADCImpl_EnumerateChannels
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(Esp32DriverADC, (ILTDriverADC) (Esp32DriverADCChannel));

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  10-Sep-26   augustus   created
 */
