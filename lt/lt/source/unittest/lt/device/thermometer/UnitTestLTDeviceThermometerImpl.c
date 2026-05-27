/*******************************************************************************
 * Temperature Sensor Unit Test
 *
 * Test LTDeviceThermometer and <platform>DriverThermometer.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/device/thermometer/LTDeviceThermometer.h>

DEFINE_LTLOG_SECTION("thermotest");

static ILTThread                * s_iThread      = NULL;
static LTDeviceThermometer      * s_pThermometer = NULL;
static ILTThermometerDeviceUnit * s_iThermometerDeviceUnit = NULL;
static u32                        s_nNumIterations = 0;

/***********************************************************************************************************
 * Collect and log readings from the temperature sensor.                                                  */
static int TestThermometer(LTDeviceUnit hThermometer) {
    /* Count the digits in the number of iteration in order to Line up all the temperature-reading logs.
     * Make a format that pads the reading number accordingly: */
    u32 nPlaces = 0;
    for (u32 nNumIterations = s_nNumIterations; nNumIterations; ++nPlaces, nNumIterations /= 10);
    char format[80];
    lt_snprintf(format, 80, "%%%dd. Temperature is %%ld.%%ld degrees C.", nPlaces);
    u32 nIteration = 1;  /* the reading's serial number */
    for (;;) {
        s32 temperature = s_iThermometerDeviceUnit->Read(hThermometer);
        s32 tenths = temperature % 10;  /* Convert degrees Milligrade to Centigrade */
        temperature /= 10;
        LTLOG("read", "%lu. Temperature is %ld.%ld", LT_Pu32(nIteration), LT_Ps32(temperature), LT_Ps32(tenths));
        if (++nIteration > s_nNumIterations) break;
        s_iThread->Sleep(LTTime_Milliseconds(500));
    }
    return 0;
}

/***********************************************************************************************************
 * Open the Device library.  Secure a Device Unit handle for every available instance and perform the
 * test on each:                                                                                          */
static int RunThermometerTest(void) {
    int nResult = 0;
    s_pThermometer = (LTDeviceThermometer *)LT_GetCore()->OpenLibrary("LTDeviceThermometer");
    if (!s_pThermometer) {
        LTLOG_REDALERT("open.fail", "Unable to open LTThermometer Device library");
        nResult = 20;
    } else {
        u32 nNumDeviceUnits = s_pThermometer->GetNumDeviceUnits();
        LTLOG( "units", "LTDeviceThermometer reports %lu Device Unit%s",
                                                  LT_Pu32(nNumDeviceUnits),
                                                  nNumDeviceUnits == 1 ? "" : "s");
        if (nNumDeviceUnits == 0) {
            LTLOG_REDALERT("open.fail", "LTDeviceThermometer provides no Device Units");
            nResult = 21;
        } else {
            for (u32 i = 0; i < nNumDeviceUnits; ++i) {
                LTDeviceUnit hThermometer = s_pThermometer->CreateDeviceUnitHandle(i);
                s_iThermometerDeviceUnit = lt_gethandleinterface(ILTThermometerDeviceUnit, hThermometer);
                nResult = TestThermometer(hThermometer);
                LT_GetCore()->DestroyHandle(hThermometer);
            }
        }
        LT_GetCore()->CloseLibrary((LTLibrary *)s_pThermometer);
        s_pThermometer = NULL;
    }
    return nResult;
}

/***********************************************************************************************************
 * Output usage hints                                                                                     */
static void DisplayUsage(int const argc, char const **argv) {
    char const *pInvocation = "UnitTestLTDeviceThermometer";
    if (argc >= 2) pInvocation = argv[1];
    LTLOG( "usage", "usage: %s [number of readings]", pInvocation);
}

/***********************************************************************************************************
 * Gather command-line options.
 *  One option available: the number of readings taken for each temperature sensor.                       */
static int GatherOptions(int const argc, char const ** argv) {
    int nResult = 0;
    s_nNumIterations = 1;
    if (argc > 2) {
        if ((s_nNumIterations = lt_strtou32(argv[2], NULL, 10)) == 0) {
            DisplayUsage(argc, argv);   /* failed to parse the number. */
            nResult = 10;
        }
    }
    return nResult;
}

static bool UnitTestLTDeviceThermometerImpl_LibInit(void) {
    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    return true;
}

static void UnitTestLTDeviceThermometerImpl_LibFini(void) {}

/* Return value: 1-9:   problem in UnitTestLTDeviceThermometerImpl_Run()
 *               10-19: problem in GatherOptions()
 *               20-29: problem in RunThermometerTest()
 *               30-39: problem in TestThermometer() */
static int UnitTestLTDeviceThermometerImpl_Run(int argc, char const ** argv) {
    int nResult = GatherOptions(argc, argv);
    if (nResult == 0)
        nResult = RunThermometerTest();
    return nResult;
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceThermometer, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceThermometer, UnitTestLTDeviceThermometerImpl_Run, 2048) LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  27-Jan-21   constantine created
 *  19-Oct-21   constantine moved to UnitTestLTDeviceThermometer
 */
