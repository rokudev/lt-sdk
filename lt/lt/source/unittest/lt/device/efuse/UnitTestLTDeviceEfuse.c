/*******************************************************************************
 * Efuse Device Test
 *
 * Test LTDeviceEfuse 
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/efuse/LTDeviceEfuse.h>

#include <tilt/JiltEngine.h>

static JiltEngine *s_engine;

/***********************************************************************************************************
 * Tilt tests                                                                                             */
static void BasicTest(Tilt *tilt) {
    LTDriverLibrary * pDriverEfuse;
    LTDeviceUnit hDeviceUnit = 0;
    ILTDriverEfuseDeviceUnit *iEfuse;
    u8 *pEfuseData1 = NULL;
    u8 *pEfuseData2 = NULL;

    pDriverEfuse = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceEfuse", 0);
    if (!pDriverEfuse) {
        TILT_REPORT_FAILURE(tilt, "Failed to open Efuse driver");
        return;
    }

    do {
        u16 numBytes;
        bool allZero = true;

        hDeviceUnit = pDriverEfuse->CreateDeviceUnitHandle(0);
        if (!hDeviceUnit) {
            TILT_REPORT_FAILURE(tilt, "Failed to create Efuse device unit handle");
            break;
        }

        iEfuse = lt_gethandleinterface(ILTDriverEfuseDeviceUnit, hDeviceUnit);
        if (!iEfuse) {
            TILT_REPORT_FAILURE(tilt, "Failed to get Efuse interface");
            break;
        }

        numBytes = iEfuse->GetNumberOfEfuseAreaBytes();
        if (numBytes == 0) {
            TILT_REPORT_FAILURE(tilt, "Failed to get number of Efuse bytes");
            break;
        }

        pEfuseData1 = (u8 *)lt_malloc(numBytes);
        pEfuseData2 = (u8 *)lt_malloc(numBytes);
        if (!pEfuseData1 || !pEfuseData2) {
            TILT_REPORT_FAILURE(tilt, "Failed to allocate Efuse data buffers");
            break;
        }

        lt_memset(pEfuseData1, 0, numBytes);
        lt_memset(pEfuseData2, 0, numBytes);

        for (u16 i = 0; i < numBytes; i++) {
            iEfuse->GetEfuseByte(i, pEfuseData1 + i);
        }

        for (u16 i = 0; i < numBytes; i++) {
            iEfuse->GetEfuseByte(i, pEfuseData2 + i);
        }

        if (lt_memcmp(pEfuseData1, pEfuseData2, numBytes) != 0) {
            TILT_REPORT_FAILURE(tilt, "Efuse data read mismatch");
            break;
        }

        // Check if any of the Efuse data is non-zero
        for (u16 i = 0; i < numBytes; i++) {
            if (pEfuseData1[i]) {
                allZero = false;
                break;
            }
        }

        // Assumes Efuse is not completely blank
        if (allZero) {
            TILT_REPORT_FAILURE(tilt, "Efuse data is all zero");
        }
    } while(0);

    lt_free(pEfuseData1);
    lt_free(pEfuseData2);
    lt_destroyhandle(hDeviceUnit);
    lt_closelibrary(pDriverEfuse);
}

static const TiltEngineTest s_tests[] = {
    { BasicTest,  "Basic Efuse test",  "The basic Efuse test",  0 },
};

static int UnitTestLTDeviceEfuseImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), NULL);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceEfuseImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceEfuseImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceEfuse, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceEfuse, UnitTestLTDeviceEfuseImpl_Run, 1536) LTLIBRARY_DEFINITION;
