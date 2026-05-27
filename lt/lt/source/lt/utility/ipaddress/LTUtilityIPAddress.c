/*******************************************************************************
 *
 * LTUtilityIPAddress
 * -------------------
 *
 * Common utility functions used for processing IP addresses.
 * This should not be combined with lower-layer network utilities because
 * it is possible for LT devices to operate at the MAC layer without IP.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/utility/ipaddress/LTUtilityIPAddress.h>

/*******************************************************************************
 * API Implementation
 ******************************************************************************/

static bool LTUtilityIPAddress_StringToIPAddress(const char *str, LTIPAddress *ip) {
    int n = 0;
    LT_SIZE len = lt_strlen(str);
    bool octet_started = false;
    u8 *octet = (u8 *)ip;
    u16 val = 0;
    *ip = 0;
    for (LT_SIZE i = 0; i < len && n < 4; i++) {
        char c = str[i];
        if (lt_isdigit(c)) {
            octet_started = true;
            val *= 10;
            val += c - '0';
            if (val > 255) {
                return false;
            }
            octet[n] = val;
        } else if (c == '.') {
            if(!octet_started) {
                return false;
            }
            octet_started = false;
            n++;
            val=0;
        }
        else {
            return false;
        }
    }
    if (n == 3 && octet_started) return true;
    return false;
}

static void LTUtilityIPAddress_IPAddressToString(const LTIPAddress ip, char *out) { // out >= 16 chars
    lt_snprintf(out, LTIPAddress_Strlen_Max, LT_PRIIP, LT_PLT_IP(ip));
}

static bool LTUtilityIPAddress_IsZero(const LTIPAddress a) {
    return (a == LTIPAddress_Zero);
}

static bool LTUtilityIPAddress_IsBroadcast(const LTIPAddress a) {
    return (a == LTIPAddress_Broadcast);
}

static bool LTUtilityIPAddress_IsEqual(const LTIPAddress a, const LTIPAddress b) {
    return (a == b);
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTUtilityIPAddressImpl_LibFini(void) {
}

static bool LTUtilityIPAddressImpl_LibInit(void) {
    return true;
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/

define_LTLIBRARY_ROOT_INTERFACE(LTUtilityIPAddress)
    .StringToIPAddress  = LTUtilityIPAddress_StringToIPAddress,
    .IPAddressToString  = LTUtilityIPAddress_IPAddressToString,
    .IsZero             = LTUtilityIPAddress_IsZero,
    .IsBroadcast        = LTUtilityIPAddress_IsBroadcast,
    .IsEqual            = LTUtilityIPAddress_IsEqual,
LTLIBRARY_DEFINITION;
