/*******************************************************************************
 * lt/source/unittest/lt/device/floodlight/UnitTestLTDeviceFloodlight.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTThread.h>
#include <lt/device/floodlight/LTDeviceFloodlight.h>
#include <lt/device/usbserial/LTDeviceUSBSerial.h>
#include <tilt/JiltEngine.h>

static struct Statics {
    Tilt               *tilt;
    LTDeviceFloodlight *pLight;
    LTCore             *pCore;
    ILTThread          *iThread;
    LTMutex            *testMutex;
} S;

static JiltEngine *s_engine;

enum {
    kLightOnTimeSecond     = 1,
    kAccomodationTimeUnits = 100,
};

/*****************************************
 *
 * LTDeviceFloodlight Test Functions
 *
 *****************************************/

static void CurrentBrightnessTest(Tilt *tilt) {
    u32 getbrightness;
    u32 bightnessPercent = 50;  // percent
    bool ret;

    ret = S.pLight->API->SetObjectBrightness(S.pLight, bightnessPercent);
    TILT_ASSERT_TRUE(tilt, ret, "set object brightness failed !!");

    /* Apply brightness */
    ret = S.pLight->API->LightOn(S.pLight);
    TILT_ASSERT_TRUE(tilt, ret, "light on failed !!");

    ret = S.pLight->API->GetCurrentBrightness(S.pLight, &getbrightness);
    TILT_ASSERT_TRUE(tilt, ret, "get current brightness failed !!");

    /* Off when done */
    S.iThread->Sleep(LTTime_Milliseconds(500));
    ret = S.pLight->API->LightOff(S.pLight);  // light off
    TILT_ASSERT_TRUE(tilt, ret, "light off failed !!");

    TILT_ASSERT_TRUE(tilt, bightnessPercent == getbrightness, "Not getting same brightness value !!");
}

static void ObjectBrightnessTest(Tilt *tilt) {
    u32 getBrightness;
    bool ret;

    for (u32 setBrightness = 0; setBrightness <= 100; setBrightness += 10) {
        ret = S.pLight->API->SetObjectBrightness(S.pLight, setBrightness);
        TILT_ASSERT_TRUE(tilt, ret, "set object brightness failed !!");

        /* Apply brightness */
        ret = S.pLight->API->LightOn(S.pLight);
        TILT_ASSERT_TRUE(tilt, ret, "light on failed !!");

        S.pLight->API->GetObjectBrightness(S.pLight, &getBrightness);
        TILT_ASSERT_TRUE(tilt, setBrightness == getBrightness, "Not getting same object brightness value");

        /**delay to visually observe the change in brightness*/
        S.iThread->Sleep(LTTime_Milliseconds(100));
        ret = S.pLight->API->GetCurrentBrightness(S.pLight, &getBrightness);  // To check, if actually applied or not
        TILT_ASSERT_TRUE(tilt, ret, "get current brightness failed !!");
        TILT_ASSERT_TRUE(tilt, setBrightness == getBrightness, "same brightness not observed on FL");
    }
    ret = S.pLight->API->LightOff(S.pLight);  // light off
    TILT_ASSERT_TRUE(tilt, ret, "light off failed !!");
}

static void FlashFrequencyTest(Tilt *tilt) {
    u32 getflashFrequency;
    bool ret;

    for (u32 setflashFrequency = 10; setflashFrequency > 0; setflashFrequency--) {
        ret = S.pLight->API->SetFlashFrequency(S.pLight, setflashFrequency);
        TILT_ASSERT_TRUE(tilt, ret, "set flash frequency failed !!");
        ret = S.pLight->API->GetFlashFrequency(S.pLight, &getflashFrequency);
        TILT_ASSERT_TRUE(tilt, ret, "get flash frequency failed !!");
        TILT_ASSERT_TRUE(tilt, getflashFrequency == setflashFrequency, "flash frequency is not same as set !!");
        /**delay to visually observe the flashing pattern*/
        S.iThread->Sleep(LTTime_Milliseconds(200));
    }

    // /**delay to visually observe the flashing pattern*/
    ret = S.pLight->API->LightOff(S.pLight);  // light off
    TILT_ASSERT_TRUE(tilt, ret, "light off failed !!");
}

static void AccomodationTimeTest(Tilt *tilt) {
    u32 setAccTime = 10;  // setting acc time to 10*100 msec
    u32 getAccTime;
    u32 currentBrightness;
    bool ret;

    /**Turn on light with some initial brightness **/
    ret = S.pLight->API->LightOn(S.pLight);
    TILT_ASSERT_TRUE(tilt, ret, "light on failed !!");
    ret = S.pLight->API->SetAccommodationTime(S.pLight, setAccTime);
    TILT_ASSERT_TRUE(tilt, ret, "set accommodation time failed !!");
    S.pLight->API->GetAccommodationTime(S.pLight, &getAccTime);
    TILT_ASSERT_TRUE(tilt, setAccTime == getAccTime, "Not same light accommodation as set !!");

    /**Turning off light to observe change in brightness **/
    LTTime startTime = LT_GetCore()->GetKernelTime();
    ret = S.pLight->API->LightOff(S.pLight);
    TILT_ASSERT_TRUE(tilt, ret, "light off failed !!");

    do {
        ret = S.pLight->API->GetCurrentBrightness(S.pLight, &currentBrightness);
        TILT_ASSERT_TRUE(tilt, ret, "get current brightness failed !!");
    } while (currentBrightness != 0);

    LTTime endTime = LT_GetCore()->GetKernelTime();
    LTTime_SubtractFrom(endTime, startTime);

    TILT_ASSERT_TRUE(tilt,LTTime_IsLessThanOrEqual(endTime, LTTime_Milliseconds(setAccTime * kAccomodationTimeUnits)),
                     "Brightness didn't change in expected duration");
}

static void BeforeAllTests(Tilt *tilt) {
    S         = (struct Statics) {};
    S.tilt    = tilt;
    S.pCore   = LT_GetCore();
    S.iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());

    S.pLight = lt_createobject(LTDeviceFloodlight);
    TILT_EXPECT_TRUE(tilt, S.pLight != NULL, "Cannot open LTDeviceFloodlight");
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_destroyobject(S.pLight);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { CurrentBrightnessTest, "CurrentBrightnessTest", "observe transient brightness value on FL",   0 },
    { ObjectBrightnessTest,  "ObjectBrightnessTest",  "observe target brightness value on FL",      0 },
    { FlashFrequencyTest,    "FlashFrequencyTest",    "check flash frequency of FL Light",          0 },
    { AccomodationTimeTest,  "AccomodationTimeTest",  "check transient time b/w brightness levels", 0 },
};

static int UnitTestLTDeviceFloodlightImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceFloodlightImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceFloodlightImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceFloodlight, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceFloodlight, UnitTestLTDeviceFloodlightImpl_Run, 1536) LTLIBRARY_DEFINITION;
