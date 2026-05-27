/*******************************************************************************
 * UnitTestLTDeviceFLUnit.c
 * -----------------------------------
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/flunit/LTDeviceFLUnit.h>
#include <tilt/JiltEngine.h>

#define DEVICE_TYPE_FL_UNIT          0x03
#define BRIGHTNESS_FL                100
#define BRIGHTNESS_FLASH_GRADE       0
#define BRIGHTNESS_ACCOMODATION_TIME 0

static JiltEngine     *s_engine;
static Tilt           *s_tilt;
static LTDeviceFLUnit *s_fldriver;
static bool            s_bSuccess;
static LTHandle        s_handle;

/*******************************************************************************
 * Test Functions
 ******************************************************************************/

static void UnitTestGetDeviceType(Tilt *tilt) {
    u8 deviceTypeIs = 0;
    s_bSuccess = s_fldriver->GetDeviceType(&s_handle, &deviceTypeIs);
    TILT_ASSERT_TRUE(tilt, deviceTypeIs == DEVICE_TYPE_FL_UNIT, "Device ID did not match");
}

static void UnitTestGetMacID(Tilt *tilt) {
    u8 macId[8];
    s_bSuccess = s_fldriver->GetMacID(&s_handle, macId);
    TILT_ASSERT_TRUE(tilt, macId[0] != 0, "Failed to get MAC ID");
}

static void UnitTestAuthenticateDevice(Tilt *tilt) {
    s_bSuccess = s_fldriver->AuthenticateDevice(&s_handle);
    TILT_ASSERT_TRUE(tilt, s_bSuccess, "Failed to Authenticate Device");
}

static void UnitTestFirmwareVersion(Tilt *tilt) {
    u32 fwVersion = 0;
    s_bSuccess = s_fldriver->FirmwareVersion(&s_handle, &fwVersion);
    TILT_ASSERT_TRUE(tilt, fwVersion != 0, "Failed to get Firmware Version");
}

static void UnitTestHardwareVersion(Tilt *tilt) {
    u32 hwVersion = 0;
    s_bSuccess = s_fldriver->HardwareVersion(&s_handle, &hwVersion);
    TILT_ASSERT_TRUE(tilt, hwVersion != 0, "Failed to get Hardware Version");
}

static void UnitTestGetEncryptedKey(Tilt *tilt) {
    u8 rKey[16];
    for (u8 i = 0; i < 16; i++) rKey[i] = i * 2;
    s_bSuccess = s_fldriver->GetEncryptedKey(&s_handle, rKey);
    TILT_ASSERT_TRUE(tilt, rKey[0] != 0, "Failed to get EncryptedKey");
}

static void UnitTestGetWatchdogTimer(Tilt *tilt) {
    u16 watchdogTimer = 0;
    s_bSuccess = s_fldriver->GetWatchdogTimer(&s_handle, &watchdogTimer);
    TILT_ASSERT_TRUE(tilt, watchdogTimer != 0, "Failed to get WatchdogTimer");
}

static void UnitTestSetWatchdogTimer(Tilt *tilt) {
    u16 watchdogTimer = 60;
    s_bSuccess = s_fldriver->SetWatchdogTimer(&s_handle, watchdogTimer);
    TILT_ASSERT_TRUE(tilt, s_bSuccess, "Failed to set WatchdogTimer");
}

static void UnitTestSetCommand(Tilt *tilt) {
    u8 cmd = LT_FL_CMD_SET_BRIGHTNESS_PARAMETER;
    u8 dataSend[] = {BRIGHTNESS_FL, BRIGHTNESS_FLASH_GRADE, BRIGHTNESS_ACCOMODATION_TIME};  // Brightness, Flash grade, 
                                                                                            // accommodation time
    s_bSuccess = s_fldriver->SetCommand(&s_handle, cmd, dataSend, sizeof(dataSend));
    TILT_ASSERT_TRUE(tilt, dataSend[0] == 0xff, "Failed to set command");
}

static void UnitTestGetCommand(Tilt *tilt) {
    u8 cmd = LT_FL_CMD_GET_BRIGHTNESS_PARAMETER;
    u8 dataGet[3] = {};
    const u8 getDataLength = 3;
    s_bSuccess = s_fldriver->GetCommand(&s_handle, cmd, dataGet, getDataLength);
    TILT_ASSERT_TRUE(tilt, dataGet[0] != 0, "Failed to set command");
}

static void UnitTestPublicCmdType(Tilt *tilt) {
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_BRIGHTNESS_PARAMETER          == 0x44, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_BRIGHTNESS_PARAMETER_RESP     == 0x45, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_SET_BRIGHTNESS_PARAMETER          == 0x46, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_SET_BRIGHTNESS_PARAMETER_RESP     == 0x47, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_LIGHT_PHOTOSENSITIVE          == 0x48, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_LIGHT_PHOTOSENSITIVE_RESP     == 0x49, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_LIGHT_TOTAL_WORKING_TIME      == 0x4A, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_LIGHT_TOTAL_WORKING_TIME_RESP == 0x4B, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_PIR_TRIGGER_STATE             == 0x4C, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_PIR_TRIGGER_STATE_RESP        == 0x4D, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_FILL_LIGHT_BRIGHTNESS_CHANGE      == 0x52, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_FILL_LIGHT_BRIGHTNESS_CHANGE_RESP == 0x53, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_PIR_PARAMETER                 == 0x58, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_PIR_PARAMETER_RESP            == 0x59, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_SET_PIR_PARAMETER                 == 0x5A, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_SET_PIR_PARAMETER_RESP            == 0x5B, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_PIR_SILENT_TIME               == 0x5C, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_PIR_SILENT_TIME_RESP          == 0x5D, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_SET_PIR_SILENT_TIME               == 0x5E, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_SET_PIR_SILENT_TIME_RESP          == 0x5F, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_LIGHT_ON_OFF                  == 0x60, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_LIGHT_ON_OFF_RESP             == 0x61, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_PIR_TRIGGER_INFORMATION       == 0x96, "Invalid command value");
    TILT_ASSERT_TRUE(tilt, LT_FL_CMD_GET_PIR_TRIGGER_INFORMATION_RESP  == 0x97, "Invalid command value");
}

static void UnitTestPIRData(Tilt *tilt) {
    const u8 dataLength = 3;
    u8 cmd        = LT_FL_CMD_SET_PIR_PARAMETER;
    u8 data[]     = {200, 0x80, 0};  // Sensitivity (0-255), PIR Zone (0x20|0x40|0x80), PIR Algo (0/1)
    u8 dataSend[] = {200, 0x80, 0};
    u8 dataVerify[3];

    s_bSuccess = s_fldriver->SetCommand(&s_handle, cmd, dataSend, dataLength);
    TILT_ASSERT_TRUE(tilt, dataSend[0] == 0xff, "Get PIR Failed !");

    cmd = LT_FL_CMD_GET_PIR_PARAMETER;
    s_bSuccess = s_fldriver->GetCommand(&s_handle, cmd, dataVerify, dataLength);
    for (u8 i = 0; i < dataLength; i++) TILT_ASSERT_TRUE(tilt, dataVerify[i] == data[i], "Failed to Verify PIR Data !");
}

static void UnitTestBrightnessData(Tilt *tilt) {
    const u8 dataLength = 3;
    u8 cmd        = LT_FL_CMD_SET_BRIGHTNESS_PARAMETER;
    u8 data[]     = {BRIGHTNESS_FL, BRIGHTNESS_FLASH_GRADE, BRIGHTNESS_ACCOMODATION_TIME};
    u8 dataSend[] = {BRIGHTNESS_FL, BRIGHTNESS_FLASH_GRADE, BRIGHTNESS_ACCOMODATION_TIME};

    // When getting the brightness data, floodlight sends parameters in the below order
    // BRIGHTNESS_FL, BRIGHTNESS_FL, BRIGHTNESS_FLASH_GRADE
    u8 dataVerify[3];

    s_bSuccess = s_fldriver->SetCommand(&s_handle, cmd, dataSend, dataLength);
    TILT_ASSERT_TRUE(tilt, dataSend[0] == 0xff, "Set Brightness Failed !");

    cmd = LT_FL_CMD_GET_BRIGHTNESS_PARAMETER;
    s_bSuccess = s_fldriver->GetCommand(&s_handle, cmd, dataVerify, dataLength);

    TILT_ASSERT_TRUE(tilt, dataVerify[0] == data[0], "Failed to Verify Brightness Data !");

    lt_memset(dataSend,0,dataLength); //lights off on exit
    cmd = LT_FL_CMD_SET_BRIGHTNESS_PARAMETER;
    s_bSuccess = s_fldriver->SetCommand(&s_handle, cmd, dataSend, dataLength);
    TILT_ASSERT_TRUE(tilt, dataSend[0] == 0xff, "Set Brightness Failed !");
}

/**********************************************************************************
 * Tilt Test Section
 **********************************************************************************/
static const TiltEngineTest s_tests[] = {
    { UnitTestGetDeviceType,      "GetDeviceType",      "Get Device Type",                                0 },
    { UnitTestGetMacID,           "GetMacID",           "Get MAC ID",                                     0 },
    { UnitTestAuthenticateDevice, "AuthenticateDevice", "Authenticate a Connected Accessory",             0 },
    { UnitTestFirmwareVersion,    "FirmwareVersion",    "Check a Current Firmware Version",               0 },
    { UnitTestHardwareVersion,    "HardwareVersion",    "Get the Hardware Version",                       0 },
    { UnitTestGetEncryptedKey,    "GetEncryptedKey",    "Get encrypted key",                              0 },
    { UnitTestGetWatchdogTimer,   "GetWatchdogTimer",   "Get Watchdog Timer",                             0 },
    { UnitTestSetWatchdogTimer,   "SetWatchdogTimer",   "Set Watchdog Timer",                             0 },
    { UnitTestSetCommand,         "SetCommand",         "Set a Command",                                  0 },
    { UnitTestGetCommand,         "GetCommand",         "Get a Command",                                  0 },
    { UnitTestPublicCmdType,      "PublicCmdType",      "Sanity check for public FL Commands",            0 },
    { UnitTestPIRData,            "PIRData",            "Verify set and get data with PIR params",        0 },
    { UnitTestBrightnessData,     "BrightnessData",     "Verify set and get data with Brightness params", 0 }
};

static void BeforeAllTests(Tilt *tilt) {
    s_tilt     = tilt;
    s_fldriver = lt_openlibrary(LTDeviceFLUnit);
    TILT_EXPECT_TRUE(tilt, s_fldriver != NULL, "Cannot open LTDeviceFLUnit");
    if (!s_fldriver) return;

    s_handle = s_fldriver->CreateDeviceUnitHandle(0);
    s_fldriver->AuthenticateDevice(&s_handle);

    // Arbitrary values to initialize, as sensitivity defaults to zero.
    u8 data[3] = { 250, 0x80, 0 };  // Sensitivity (0-255), PIR Zone (0x20|0x40|0x80), PIR Algo (0/1)
    s_fldriver->SetCommand(&s_handle, LT_FL_CMD_SET_PIR_PARAMETER, data, 3);
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_destroyhandle(s_handle);
    lt_closelibrary(s_fldriver);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static int UnitTestLTDeviceFLUnitImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceFLUnitImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceFLUnitImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceFLUnit, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceFLUnit, UnitTestLTDeviceFLUnitImpl_Run, 1536) LTLIBRARY_DEFINITION;
