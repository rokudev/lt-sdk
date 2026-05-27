/******************************************************************************
 * UnitTestLTDeviceConfig.c
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/


#include <lt/device/config/LTDeviceConfig.h>
#include <tilt/JiltEngine.h>

/*____________________
   Static variables */

static JiltEngine     *s_engine;
static LTDeviceConfig *s_deviceConfig;

/*_______________
  TestReadTree */

static void TestReadTree(Tilt *tilt) {
    u32 i;
    const char *defaultNetTransport = s_deviceConfig->GetDefaultNetTransport();
    u32              nNumTransports = s_deviceConfig->GetNumNetTransports();
    const char     *zerothTransport = s_deviceConfig->GetNetTransportAt(0);

    TILT_EXPECT_TRUE(tilt,                                                     (nNumTransports == 1), "got bogus value for num net transports");
    TILT_EXPECT_TRUE(tilt, zerothTransport && (0 == lt_strcmp(zerothTransport, defaultNetTransport)), "zeroth transport was not default");

    TILT_INFO(tilt,     "Default Net Transport   = %s", defaultNetTransport ? defaultNetTransport : "<none>");
    for (i = 0; i < nNumTransports; i++) {
        TILT_INFO(tilt, "        Net Transport %d = %s%s", (int)i, s_deviceConfig->GetNetTransportAt(i), i == nNumTransports - 1 ? "\n" : "");
    }

    /* print out the list of all of the devices and their drivers, just informational, tests follow */
    u32 nNumDriversTotal = 0;
    u32 nNumDevices = s_deviceConfig->GetNumDevices();
    TILT_INFO(tilt, "_______");
    TILT_INFO(tilt, "Devices");
    if (nNumDevices == 0) TILT_INFO(tilt, "None.");
    for (i = 0; i < nNumDevices; i++) {
        const char * pDeviceName = s_deviceConfig->GetDeviceAt(i);
        u32 nNumDrivers = s_deviceConfig->GetNumDrivers(pDeviceName);
        nNumDriversTotal += nNumDrivers;
        TILT_INFO(tilt, "Device %d: %s", (int)i, pDeviceName);
        if (nNumDrivers == 0) TILT_INFO(tilt, "          No Drivers");
        else {
            for (u32 j = 0; j < nNumDrivers; j++) {
                const char * pDriverName = s_deviceConfig->GetDriverAt(pDeviceName, j);
                TILT_INFO(tilt, "          Driver %d: %s", (int)j, pDriverName);
            }
        }
    }

    TILT_EXPECT_TRUE(tilt,  (nNumDevices > 0),      "no devices found");
    TILT_EXPECT_TRUE(tilt,  (nNumDriversTotal > 0), "no drivers found");

    u32 nDeviceSection = s_deviceConfig->GetDeviceSection("LTDeviceWatchdog");
    TILT_EXPECT_TRUE(tilt,  (nDeviceSection != 0), "failed to get device section for LTDeviceWatchdog");

    const char * driverName = s_deviceConfig->GetDriverAt("LTDeviceWatchdog", 0);
    TILT_EXPECT_TRUE(tilt,  (driverName != NULL), "failed to get LTDeviceWatchdog driver name");

    u32 nDriverSection = s_deviceConfig->GetDriverSection("LTDeviceWatchdog", driverName);
    TILT_EXPECT_TRUE(tilt,  (nDriverSection != 0), "failed to get driver section for LTDeviceWatchdog driver %s", driverName);

    // read uts and uti from device section
    const char *uts = s_deviceConfig->ReadString(nDeviceSection, "uts");
    TILT_EXPECT_TRUE(tilt,  (uts != NULL), "failed to read uts string variable from LTDeviceWatchdog section");
    TILT_EXPECT_TRUE(tilt,  (0 == lt_strcmp(uts, "pass")), "uts string variable \"%s\", expected \"pass\"", uts);
    s64 uti = s_deviceConfig->ReadInteger(nDeviceSection, "uti");
    TILT_EXPECT_TRUE(tilt,  (uti == 42), "uti integer variable %lld, expected 42", LT_Ps64(uti));

    // read stu and itu from driver section
    const char *stu = s_deviceConfig->ReadString(nDriverSection, "stu");
    TILT_EXPECT_TRUE(tilt,  (stu != NULL), "failed to read stu string variable from LTDeviceWatchdog driver %s section", driverName);
    TILT_EXPECT_TRUE(tilt,  (0 == lt_strcmp(stu, "ssap")), "stu string variable \"%s\", expected \"ssap\"", stu);
    s64 itu = s_deviceConfig->ReadInteger(nDriverSection, "itu");
    TILT_EXPECT_TRUE(tilt,  (itu == 24), "itu integer variable %lld, expected 24", LT_Ps64(itu));

    // read stu and itu as nested variables from device section
    stu = NULL; itu = 0;
    stu = s_deviceConfig->ReadString(nDeviceSection, "driver/0/stu");
    TILT_EXPECT_TRUE(tilt,  (stu != NULL), "failed to read driver/0/stu string variable from LTDeviceWatchdog section");
    TILT_EXPECT_TRUE(tilt,  (0 == lt_strcmp(stu, "ssap")), "driver/0/stu string variable \"%s\", expected \"ssap\"", stu);
    itu = s_deviceConfig->ReadInteger(nDeviceSection, "driver/0/itu");
    TILT_EXPECT_TRUE(tilt,  (itu == 24), "driver/0/itu integer variable %lld, expected 24", LT_Ps64(itu));
}

/*_____________
  Test Hooks */

static void BeforeAllTests(Tilt *tilt) {
    s_deviceConfig = lt_openlibrary(LTDeviceConfig);
    TILT_EXPECT_TRUE(tilt, s_deviceConfig != NULL, "Cannot open LTDeviceConfig");
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_closelibrary(s_deviceConfig);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

/*____________
  Test List */

static const TiltEngineTest s_tests[] = {
    { TestReadTree, "ReadValueFromTree", "read a value from the device tree", 0 },
};

/*_________________
  Test Executive */

static int UnitTestLTDeviceConfigImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

/*__________________
  Library Binding */

static bool UnitTestLTDeviceConfigImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceConfigImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceConfig, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceConfig, UnitTestLTDeviceConfigImpl_Run, 1536) LTLIBRARY_DEFINITION;
