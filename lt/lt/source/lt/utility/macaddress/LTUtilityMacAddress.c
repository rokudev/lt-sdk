/*******************************************************************************
 *
 * LTUtilityMacAddress
 * -------------------
 *
 * Common utility functions used for processing MAC addresses.
 * This should not be combined with higher-layer network utilities because
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
#include <lt/utility/macaddress/LTUtilityMacAddress.h>

static LTCore     *pCore;

/*******************************************************************************
 * 32-bit hash function for MacAddressToHash()
 * Compatible with C++ std::hash() in gcc 32-bit.
 * Based on public domain MurmurHash2() by Austin Appleby
 * See https://github.com/explosion/murmurhash/blob/master/murmurhash/\
 * include/murmurhash/MurmurHash2.h
 ******************************************************************************/
static u32 std_hash(const void * key, LT_SIZE len) {
    // 'm' and 'r' are mixing constants generated offline.
    // They're not really 'magic', they just happen to work well.
    const u32 m    = 0x5bd1e995;
    const int r    = 24;
    const u32 seed = 0xc70f6907UL;

    // Initialize the hash to a 'random' value
    u32 h = seed ^ len;

    // Mix 4 bytes at a time into the hash
    const unsigned char * data = (const unsigned char *)key;
    while (len >= 4) {
        u32 k = *(u32 *)data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len  -= 4;
    }

    // Handle the last few bytes of the input array
    switch (len) {
        case 3: h ^= data[2] << 16; /* fallthrough */
        case 2: h ^= data[1] << 8;  /* fallthrough */
        case 1: h ^= data[0];
                h *= m;
    };

    // Do a few final mixes of the hash to ensure the last few
    // bytes are well-incorporated.
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return h;
}

/*******************************************************************************
 * API Implementation
 ******************************************************************************/

static bool LTUtilityMacAddress_StringToMacAddress(const char *str, LTMacAddress *mac) {
    u32 n = 0;
    for (u32 i = 0; ; i += 3) {
        if (!lt_hextobyte(str + i, 2, &mac->octet[n++])) return false;
        if (n < 6) {
            if (str[i + 2] != ':' && str[i + 2] != '-') return false;
        } else {
            return (str[i + 2] == '\0');
        }
    }
}

static void LTUtilityMacAddress_MacAddressToString(const LTMacAddress *mac, char *out, char sep) { // str >= 18 chars
    static const char hex[] = "0123456789abcdef";
    char *cp = out;
    for (int n = 0; ; n++) {
        *cp++ = hex[(mac->octet[n] >> 4) & 0xF];
        *cp++ = hex[mac->octet[n] & 0xF];
        if (n >= 5) break;
        if (sep) *cp++ = sep;
    }
    *cp = 0;
}

static void LTUtilityMacAddress_MacAddressToOui(const LTMacAddress *mac, char *out, char sep) { // str >= 18 chars
    LTUtilityMacAddress_MacAddressToString(mac, out, sep);
    out[9] = out[10] = out[12] = out[13] = out[15] = out[16] = 'x';  // 12:34:56:xx:xx:xx
}

static u32 LTUtilityMacAddress_MacAddressToHash(const LTMacAddress *mac) { // str >= 18 chars
    char macstr[18];
    LTUtilityMacAddress_MacAddressToString(mac, macstr, ':');
    return std_hash(macstr, 17);
}

static bool LTUtilityMacAddress_IsZero(const LTMacAddress *a) {
    return (a->octet[0] | a->octet[1] | a->octet[2] | a->octet[3] | a->octet[4] | a->octet[5]) == 0;
}

static bool LTUtilityMacAddress_IsBroadcast(const LTMacAddress *a) {
    return (a->octet[0] & a->octet[1] & a->octet[2] & a->octet[3] & a->octet[4] & a->octet[5]) == 0xFF;
}

static bool LTUtilityMacAddress_IsEqual(const LTMacAddress *a, const LTMacAddress *b) {
    return a->octet[5] == b->octet[5]
        && a->octet[4] == b->octet[4]
        && a->octet[3] == b->octet[3]
        && a->octet[2] == b->octet[2]
        && a->octet[1] == b->octet[1]
        && a->octet[0] == b->octet[0];
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTUtilityMacAddressImpl_LibFini(void) {
}

static bool LTUtilityMacAddressImpl_LibInit(void) {
    pCore = LT_GetCore();
    return true;
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/

define_LTLIBRARY_ROOT_INTERFACE(LTUtilityMacAddress)
    .StringToMacAddress = LTUtilityMacAddress_StringToMacAddress,
    .MacAddressToString = LTUtilityMacAddress_MacAddressToString,
    .MacAddressToOui    = LTUtilityMacAddress_MacAddressToOui,
    .MacAddressToHash   = LTUtilityMacAddress_MacAddressToHash,
    .IsZero             = LTUtilityMacAddress_IsZero,
    .IsBroadcast        = LTUtilityMacAddress_IsBroadcast,
    .IsEqual            = LTUtilityMacAddress_IsEqual,
LTLIBRARY_DEFINITION;
