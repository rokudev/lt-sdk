/*******************************************************************************
 * source/unittest/lt/utility/macaddress/UnitTestLTUtilityMacAddress.c
 *    __  __      _ __ ______          __  __  ________  ____  _ ___ __
 *   / / / /___  (_) //_  __/__  _____/ /_/ / /_  __/ / / / /_(_) (_) /___  __
 *  / / / / __ \/ / __// / / _ \/ ___/ __/ /   / / / / / / __/ / / / __/ / / /
 * / /_/ / / / / / /_ / / /  __(__  ) /_/ /___/ / / /_/ / /_/ / / / /_/ /_/ /
 * \____/_/ /_/_/\__//_/  \___/____/\__/_____/_/  \____/\__/_/_/_/\__/\__, /
 *                                                                   /____/
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/utility/macaddress/LTUtilityMacAddress.h>
#include <tilt/JiltEngine.h>

// Define a convenience macro that gets an array element count.
#define COUNTOF(x) (sizeof (x) / sizeof ((x)[0]))

/*******************************************************************************
 * Static Variables
 ******************************************************************************/

typedef struct {
    const LTMacAddress mac;
    const char * mac_string;
} MACAndStringPair;

static const MACAndStringPair s_valid_macs[] = {
    { {{0x12, 0x34, 0x56, 0xab, 0xcd, 0xef}}, "12:34:56:ab:cd:ef" },
    { {{0x00, 0x34, 0x56, 0xab, 0xcd, 0xef}}, "00:34:56:ab:cd:ef" },
    { {{0x12, 0x34, 0x56, 0xab, 0xcd, 0xff}}, "12-34-56-ab-cd-ff" },
    { {{0x00, 0x00, 0x00, 0x00, 0x00, 0x01}}, "00-00-00-00-00-01" },
    { {{0xff, 0xff, 0xff, 0xff, 0xff, 0xfe}}, "ff:ff:ff:ff:ff:fe" }
};

static JiltEngine           *s_engine;
static LTUtilityMacAddress  *s_macAddress;

/*******************************************************************************
 * Test Functions
 ******************************************************************************/

void TestStringToMacAddress(Tilt * tilt) {
    static const char * const s_mac_strings_invalid[] = {
        "12,34,56,ab,cd,ef",
        "12 34 56 ab cd ef",
        "123456abcdef",
        "12:34:56:a:cd:ef",
        "12:34:566:ab:cd:ef",
        "12:34:56:ab:cd",
        "12:34:56:gb:cd:ef",
        "",
        NULL
    };

    LTMacAddress mac_test;
    for (u32 i = 0; i < COUNTOF(s_valid_macs); i++) {
        TILT_DEBUG(tilt, "Testing valid MAC string: %s\n", s_valid_macs[i].mac_string);
        TILT_EXPECT_TRUE(tilt, s_macAddress->StringToMacAddress(
            s_valid_macs[i].mac_string, &mac_test), "Failed to convert string to MAC");
        TILT_EXPECT_TRUE(tilt, s_macAddress->IsEqual(
            &mac_test, &s_valid_macs[i].mac), "MAC does not match string");
    }
    for (u32 i = 0; i < COUNTOF(s_mac_strings_invalid); i++) {
        TILT_DEBUG(tilt, "Testing invalid MAC string: %s\n", s_mac_strings_invalid[i]);
        TILT_EXPECT_FALSE(tilt, s_macAddress->StringToMacAddress(
            s_mac_strings_invalid[i], &mac_test), "MAC string should be invalid");
    }
}

void TestMacAddressToString(Tilt * tilt) {
    typedef struct {
        const LTMacAddress mac;
        const char delimiter;
        const char * mac_string;
    } MACToStringCase;

    static const MACToStringCase s_mac_to_string_cases[] = {
        { {{0x12, 0x34, 0x56, 0xab, 0xcd, 0xef}}, ':', "12:34:56:ab:cd:ef" },
        { {{0x00, 0x34, 0x56, 0xab, 0xcd, 0xef}}, ':', "00:34:56:ab:cd:ef" },
        { {{0x12, 0x34, 0x56, 0xab, 0xcd, 0xff}}, '-', "12-34-56-ab-cd-ff" },
        { {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}, '-', "00-00-00-00-00-00" }
    };

    char mac_string_test[18];
    for (u32 i = 0; i < COUNTOF(s_mac_to_string_cases); i++) {
        s_macAddress->MacAddressToString(
            &s_mac_to_string_cases[i].mac, mac_string_test, s_mac_to_string_cases[i].delimiter);
        TILT_DEBUG(tilt, "\nTesting: %s\nExpected: %s\n", mac_string_test,
            s_mac_to_string_cases[i].mac_string);
        TILT_EXPECT_TRUE(tilt, lt_strcmp(mac_string_test,
            s_mac_to_string_cases[i].mac_string) == 0, "MAC string not as expected");
    }
}

void TestIsZero(Tilt * tilt) {
    static LTMacAddress const s_mac_zero = LTMacAddress_Zero;
    TILT_EXPECT_TRUE(tilt, s_macAddress->IsZero(&s_mac_zero),
        "MAC should be all 0");
    for (u32 i = 0; i < COUNTOF(s_valid_macs); i++) {
        TILT_EXPECT_FALSE(tilt, s_macAddress->IsZero(
            &s_valid_macs[i].mac), "MAC should not be zero");
    }
}

void TestIsBroadcast(Tilt * tilt) {
    static LTMacAddress const s_mac_broadcast = LTMacAddress_Broadcast;
    TILT_EXPECT_TRUE(tilt, s_macAddress->IsBroadcast(
        &s_mac_broadcast), "Should be a broadcast MAC address");
    for (u32 i = 0; i < COUNTOF(s_valid_macs); i++) {
        TILT_EXPECT_FALSE(tilt, s_macAddress->IsBroadcast(
            &s_valid_macs[i].mac), "Should not be a broadcast MAC address");
    }
}

void TestIsEqual(Tilt * tilt) {
    typedef struct {
        const LTMacAddress mac1;
        const LTMacAddress mac2;
    } MACPair;
    static const MACPair s_equal_macs[] = {
        { {{0x12, 0x34, 0x56, 0xab, 0xcd, 0xef}}, {{0x12, 0x34, 0x56, 0xab, 0xcd, 0xef}} },
        { {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}, {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}} },
        { {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}}, {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}} }
    };
    static const MACPair s_diff_macs[] = {
        { {{0x12, 0x34, 0x56, 0xab, 0xcd, 0xef}}, {{0x01, 0x34, 0x56, 0xab, 0xcd, 0xef}} },
        { {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}, {{0x01, 0x00, 0x00, 0x00, 0x00, 0x00}} },
        { {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}}, {{0xff, 0xff, 0xff, 0xff, 0xff, 0xfe}} }
    };

    for (u32 i = 0; i < COUNTOF(s_equal_macs); i++) {
        TILT_EXPECT_TRUE(tilt, s_macAddress->IsEqual(
            &s_equal_macs[i].mac1, &s_equal_macs[i].mac2), "MACs should be equal");
    }
    for (u32 i = 0; i < COUNTOF(s_diff_macs); i++) {
        TILT_EXPECT_FALSE(tilt, s_macAddress->IsEqual(
            &s_diff_macs[i].mac1, &s_diff_macs[i].mac2), "MACs should be different");
    }
}

static void BeforeAllTests(Tilt *tilt) {
    s_macAddress = lt_openlibrary(LTUtilityMacAddress);
    TILT_EXPECT_TRUE(tilt, s_macAddress != NULL, "Cannot open LTUtilityMacAddress");
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_closelibrary(s_macAddress);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { TestStringToMacAddress,    "StringToMacAddress",    "Conversion of string to MAC address",       0 },
    { TestMacAddressToString,    "MacAddressToString",    "Conversion of MAC address to string",       0 },
    { TestIsZero,                "IsZero",                "Checks if MAC address is all 0s",           0 },
    { TestIsBroadcast,           "IsBroadcast",           "Checks if Broadcast MAC address",           0 },
    { TestIsEqual,               "IsEqual",               "Checks if 2 MAC addresses are the same",    0 }
};

static int UnitTestLTUtilityMacAddressImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

/*******************************************************************************
 * Library Initialization
 ******************************************************************************/

static bool UnitTestLTUtilityMacAddressImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTUtilityMacAddressImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

/*******************************************************************************
 * Library Root Interface Binding
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityMacAddress, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityMacAddress, UnitTestLTUtilityMacAddressImpl_Run, 1536) LTLIBRARY_DEFINITION;
