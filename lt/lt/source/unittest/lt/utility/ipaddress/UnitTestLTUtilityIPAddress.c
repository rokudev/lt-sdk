/*******************************************************************************
 * source/unittest/lt/utility/ipaddress/UnitTestLTUtilityIPAddress.c
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
#include <lt/utility/ipaddress/LTUtilityIPAddress.h>
#include <tilt/JiltEngine.h>

// Define a convenience macro that gets an array element count.
#define COUNTOF(x) (sizeof(x) / sizeof((x)[0]))

/*******************************************************************************
 * Static Variables
 ******************************************************************************/

typedef struct {
    const LTIPAddress ip;
    const char       *ip_string;
} IPAndStringPair;

static const IPAndStringPair s_valid_ips[] = {
    { 0x0100a8c0,           "192.168.0.1"     },
    { 0x59432d7b,           "123.45.67.89"    },
    { 0x00ea0100,           "0.1.234.0"       },
    { 0x01000000,           "0.0.0.1"         },
    { 0xFEFFFFFF,           "255.255.255.254" },
    { LTIPAddress_Loopback, "127.0.0.1"       }
};

static JiltEngine           *s_engine;
static LTUtilityIPAddress   *s_ipAddress;

/*******************************************************************************
 * Test Functions
 ******************************************************************************/

static void TestStringToIPAddress(Tilt *tilt) {
    static const char * const s_ip_strings_invalid[] = {
        "12,34,56,78",
        "12 34 56 78",
        "12.34.56.78 ",
        "12.34.56.78.",
        "192.168.256.1",
        "10.0..1",
        "A1.0.0.1",
        "",
        NULL
    };

    LTIPAddress ip_test;
    for (u32 i = 0; i < COUNTOF(s_valid_ips); i++) {
        TILT_DEBUG(tilt, "Testing valid IP string: %s\n", s_valid_ips[i].ip_string);
        TILT_EXPECT_TRUE(tilt, s_ipAddress->StringToIPAddress(
            s_valid_ips[i].ip_string, &ip_test), "Failed to convert string to IP");
        TILT_EXPECT_TRUE(tilt, s_ipAddress->IsEqual(
            ip_test, s_valid_ips[i].ip), "IP does not match string");
    }
    for (u32 i = 0; i < COUNTOF(s_ip_strings_invalid); i++) {
        TILT_DEBUG(tilt, "Testing invalid IP string: %s\n", s_ip_strings_invalid[i]);
        TILT_EXPECT_FALSE(tilt, s_ipAddress->StringToIPAddress(
            s_ip_strings_invalid[i], &ip_test), "IP string should be invalid");
    }
}

static void TestIPAddressToString(Tilt *tilt) {
    char ip_string_test[LTIPAddress_Strlen_Max];
    for (u32 i = 0; i < COUNTOF(s_valid_ips); i++) {
        s_ipAddress->IPAddressToString(
            s_valid_ips[i].ip, ip_string_test);
        TILT_DEBUG(tilt, "\nTesting: %s\nExpected: %s\n", ip_string_test,
            s_valid_ips[i].ip_string);
        TILT_EXPECT_TRUE(tilt, lt_strcmp(ip_string_test,
            s_valid_ips[i].ip_string) == 0, "IP string not as expected");
    }

    for (u32 i = 0; i < COUNTOF(s_valid_ips); i++) {
        lt_snprintf(ip_string_test, LTIPAddress_Strlen_Max, 
                    LT_PRIIP, LT_PLT_IP(s_valid_ips[i].ip));
        TILT_DEBUG(tilt, "\nTesting: %s\nExpected: %s\n", ip_string_test,
            s_valid_ips[i].ip_string);
        TILT_EXPECT_TRUE(tilt, lt_strcmp(ip_string_test,
            s_valid_ips[i].ip_string) == 0, "IP string not as expected");
    }
}

static void TestIsZero(Tilt *tilt) {
    static LTIPAddress const s_ip_zero = LTIPAddress_Zero;
    TILT_EXPECT_TRUE(tilt, s_ipAddress->IsZero(s_ip_zero),
        "IP should be all 0");
    for (u32 i = 0; i < COUNTOF(s_valid_ips); i++) {
        TILT_EXPECT_FALSE(tilt, s_ipAddress->IsZero(
            s_valid_ips[i].ip), "IP should not be zero");
    }
}

static void TestIsBroadcast(Tilt *tilt) {
    static LTIPAddress const s_ip_broadcast = LTIPAddress_Broadcast;
    TILT_EXPECT_TRUE(tilt, s_ipAddress->IsBroadcast(
        s_ip_broadcast), "Should be a broadcast IP address");
    for (u32 i = 0; i < COUNTOF(s_valid_ips); i++) {
        TILT_EXPECT_FALSE(tilt, s_ipAddress->IsBroadcast(
            s_valid_ips[i].ip), "Should not be a broadcast IP address");
    }
}

static void TestIsEqual(Tilt *tilt) {
    typedef struct {
        const LTIPAddress ip1;
        const LTIPAddress ip2;
    } IPPair;
    static const IPPair s_equal_ips[] = {
        { 0x123456AB, 0x123456AB },
        { 0x00000000, 0x00000000 },
        { 0xFFFFFFFF, 0xFFFFFFFF }
    };
    static const IPPair s_diff_ips[] = {
        { 0x123456AB, 0x013456AB },
        { 0x00000000, 0x01000000 },
        { 0xFFFFFFFF, 0xFFFFFFFE }
    };

    for (u32 i = 0; i < COUNTOF(s_equal_ips); i++) {
        TILT_EXPECT_TRUE(tilt, s_ipAddress->IsEqual(
            s_equal_ips[i].ip1, s_equal_ips[i].ip2), "IPs should be equal");
    }
    for (u32 i = 0; i < COUNTOF(s_diff_ips); i++) {
        TILT_EXPECT_FALSE(tilt, s_ipAddress->IsEqual(
            s_diff_ips[i].ip1, s_diff_ips[i].ip2), "IPs should be different");
    }
}

static void BeforeAllTests(Tilt *tilt) {
    s_ipAddress = lt_openlibrary(LTUtilityIPAddress);
    TILT_EXPECT_TRUE(tilt, s_ipAddress != NULL, "Cannot open LTUtilityIPAddress");
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_closelibrary(s_ipAddress);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { TestStringToIPAddress, "StringToIPAddress", "Conversion of string to IP address",    0 },
    { TestIPAddressToString, "IPAddressToString", "Conversion of IP address to string",    0 },
    { TestIsZero,            "IsZero",            "Checks if IP address is all 0s",        0 },
    { TestIsBroadcast,       "IsBroadcast",       "Checks if Broadcast IP address",        0 },
    { TestIsEqual,           "IsEqual",           "Checks if 2 IP addresses are the same", 0 }
};

static int UnitTestLTUtilityIPAddressImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

/*******************************************************************************
 * Library Initialization
 ******************************************************************************/

static bool UnitTestLTUtilityIPAddressImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTUtilityIPAddressImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

/*******************************************************************************
 * Library Root Interface Binding
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityIPAddress, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityIPAddress, UnitTestLTUtilityIPAddressImpl_Run, 1536) LTLIBRARY_DEFINITION;
