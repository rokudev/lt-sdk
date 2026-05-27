/******************************************************************************
 * UnitTestLTDeviceIdentity.c
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/


#include <lt/LTTypes.h>
#include <lt/device/identity/LTDeviceIdentity.h>
#include <tilt/JiltEngine.h>

static JiltEngine        * s_engine;
static LTDeviceIdentity  * s_pIdentityDevice;

static void idSizeTest(Tilt * tilt) {
    const char *id = s_pIdentityDevice->GetDeviceId();

    TILT_ASSERT_TRUE(tilt, id != NULL, "failed to get device id");
    if (lt_strlen(id) == 12) TILT_INFO(tilt, "ok");
    else TILT_REPORT_FAILURE(tilt, "ID length is incorrect");
}

static void deviceIdTest(Tilt * tilt) {
    const char *id = s_pIdentityDevice->GetDeviceId();

    TILT_ASSERT_TRUE(tilt, id != NULL, "failed to get device id");
    if (lt_strncmp(id, "S0", 2)) TILT_INFO(tilt, id);
    else TILT_REPORT_FAILURE(tilt, "ID is invalid");
}

static void serialNumberTest(Tilt * tilt) {
    const char *sn = s_pIdentityDevice->GetSerialNumber();

    TILT_ASSERT_TRUE(tilt, sn != NULL, "failed to get serial number");
    if (lt_strncmp(sn, "X02400", 6)) TILT_INFO(tilt, sn);
    else TILT_REPORT_FAILURE(tilt, "Serial number is invalid");
}

static void aesKey(Tilt * tilt) {
    u8 key[kLTIdentityDeviceAESKeyBytes];
    lt_memset(key, 0, kLTIdentityDeviceAESKeyBytes);
    if (!s_pIdentityDevice->GetAESKey(key, 1))
        TILT_REPORT_FAILURE(tilt, "Error reported for a valid key index");
    lt_memset(key, 0, kLTIdentityDeviceAESKeyBytes);
    if (!s_pIdentityDevice->GetAESKey(key, 2))
        TILT_REPORT_FAILURE(tilt, "Error reported for a valid key index");
    lt_memset(key, 0, kLTIdentityDeviceAESKeyBytes);
    if (s_pIdentityDevice->GetAESKey(key, 0))
        TILT_REPORT_FAILURE(tilt, "Error not reported for a invalid key index");
}

static void udsTest(Tilt * tilt) {
    u8 uds[32] = {0x83,0x3F,0xE6,0x24,0x09,0x23,0x7B,0x9D,0x62,0xEC,0x77,0x58,0x75,0x20,0x91,0x1E,
                  0x9A,0x75,0x9C,0xEC,0x1D,0x19,0x75,0x5B,0x7D,0xA9,0x01,0xB9,0x6D,0xCA,0x3D,0x42};
    u8 key[16];
    lt_memset(key, 1, 16);
    u8 buf[32];
    lt_memset(buf, 0, 32);
    if (!s_pIdentityDevice->GetAESKey(key, 1))
        TILT_REPORT_FAILURE(tilt, "Error to read AES key 1");
    if (!s_pIdentityDevice->SetUds(uds))
        TILT_REPORT_FAILURE(tilt, "Error to write uds");
    if (!s_pIdentityDevice->GetAESKey(buf, 1))
        TILT_REPORT_FAILURE(tilt, "Error to read AES key 1 after writing uds");
    if (lt_memcmp(key, buf, 16) != 0)
        TILT_REPORT_FAILURE(tilt, "Error: AES key is changed after writing uds");
    if (!s_pIdentityDevice->GetUds(buf))
        TILT_REPORT_FAILURE(tilt, "Error to read uds");
    if (lt_memcmp(uds, buf, 32) != 0)
        TILT_REPORT_FAILURE(tilt, "Error: the written uds is different");
    if (s_pIdentityDevice->SetUds(buf))
        TILT_REPORT_FAILURE(tilt, "Error: shall not re-write UDS");
}

static void BeforeAllTests(Tilt * tilt) {
    s_pIdentityDevice = lt_openlibrary(LTDeviceIdentity);
    TILT_EXPECT_TRUE(tilt, s_pIdentityDevice != NULL, "Cannot open LTDeviceIdentity");
}

static void AfterAllTests(Tilt * tilt) {
    LT_UNUSED(tilt);
    lt_closelibrary(s_pIdentityDevice);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { idSizeTest,       "ID length",          "Verifies the device ID length",          0 },
    { deviceIdTest,     "Device ID",          "A test that expects device ID",          0 },
    { serialNumberTest, "Serial Number",      "A test that expects serial number",      0 },
    { aesKey,           "AES keys",           "A test that verifies valid key indices", 0 },
    { udsTest,          "Write and read uds", "A test that write and read uds",         0 },
};

static int UnitTestLTDeviceIdentityImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceIdentityImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceIdentityImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceIdentity, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceIdentity, UnitTestLTDeviceIdentityImpl_Run, 1536) LTLIBRARY_DEFINITION;
