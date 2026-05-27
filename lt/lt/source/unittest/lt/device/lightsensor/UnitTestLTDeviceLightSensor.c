/*******************************************************************************
 * lt/source/unittest/lt/device/lightsensor/UnitTestLTDeviceLightSensor.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/lightsensor/LTDeviceLightSensor.h>
#include <tilt/JiltEngine.h>

/*********************************/
/***     Test definitions      ***/
/*********************************/

static struct Statics {
    LTDeviceLightSensor *pLightSensor;
} S;

static JiltEngine *s_engine;


static void GetChannels(Tilt *tilt) {
    u16 channels = S.pLightSensor->API->GetSupportedChannels(S.pLightSensor);
    if (!channels) {
        TILT_REPORT_FAILURE(tilt, "No channels supported");
        return;
    } else if (!(channels & kLTDeviceLightSensor_Channel_Visible)) {
        TILT_REPORT_FAILURE(tilt, "Visible light channel not supported: 0x%04lx", LT_Pu32(channels));
        return;
    }
    TILT_INFO(tilt, "Supported channels: 0x%04lx", LT_Pu32(channels));
}

static void GetVisibleLightTest(Tilt *tilt) {
    IlluminanceValue value = 0;
    if (!S.pLightSensor->API->GetChannelValue(S.pLightSensor, kLTDeviceLightSensor_Channel_Visible, &value)) {
        TILT_REPORT_FAILURE(tilt, "Failed to get photo sensor visible light");
        return;
    }
    TILT_INFO(tilt, "Visible light value: %lu millilux", LT_Pu32(value));
}

static void GetAll(Tilt *tilt) {
    u16 channels = S.pLightSensor->API->GetSupportedChannels(S.pLightSensor);
    while (channels) {
        /* Get the lowest bit set */
        u16 channel = channels & ~(channels - 1);
        channels &= ~channel;
        IlluminanceValue value = 0;
        if (!S.pLightSensor->API->GetChannelValue(S.pLightSensor, channel, &value)) {
            TILT_REPORT_FAILURE(tilt, "Failed to get channel 0x%04lx value", LT_Pu32(channel));
        } else {
            TILT_INFO(tilt, "channel 0x%04lx: %lu millilux", LT_Pu32(channel), LT_Pu32(value));
        }
    }
}

static void GetUnsupported(Tilt *tilt) {
    u16 channels = S.pLightSensor->API->GetSupportedChannels(S.pLightSensor);
    if (channels == LT_U16_MAX) {
        TILT_REPORT_FAILURE(tilt, "Too many channels supported");
        return;
    }
    /* Find an unsupported channel */
    int channel;
    for (channel = 1; channels & channel; channel <<= 1) {
        ;
    }
    TILT_INFO(tilt, "testing unsupported channel 0x%04lx", LT_Pu32(channel));

    IlluminanceValue value = 0;
    if (S.pLightSensor->API->GetChannelValue(S.pLightSensor, channel, &value)) {
        TILT_REPORT_FAILURE(tilt, "Expected failure for unsupported channel 0x%04lx, got %lu millilux", LT_Pu32(channel), LT_Pu32(value));
    }
}

static void BeforeAllTests(Tilt *tilt) {
    S = (struct Statics){};

    S.pLightSensor = lt_createobject(LTDeviceLightSensor);
    if (!S.pLightSensor) {
        TILT_REPORT_FAILURE(tilt, "Error creating object LTDeviceLightSensor");
        return;
    }
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    if (S.pLightSensor) {
        lt_destroyobject(S.pLightSensor);
        S.pLightSensor = NULL;
    }
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { GetChannels,         "GetChannels",             "Get supported channel info",         0 },
    { GetVisibleLightTest, "GetVisibleLightTest",     "Get visible light info",             0 },
    { GetAll,              "GetAll",                  "Get all supported channel values",   0 },
    { GetUnsupported,      "GetUnsupported",          "Get an unsupported channel",         0 },
};

static int UnitTestLTDeviceLightSensorImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceLightSensorImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceLightSensorImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceLightSensor, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceLightSensor, UnitTestLTDeviceLightSensorImpl_Run, 1536) LTLIBRARY_DEFINITION;
