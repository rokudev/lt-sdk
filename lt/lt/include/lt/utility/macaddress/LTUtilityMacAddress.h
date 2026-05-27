/*******************************************************************************
 * LTUtilityMacAddress
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltutility_macaddress LTUtilityMacAddress
 * @ingroup ltutility
 *
 * @brief Common utility functions used for processing MAC addresses.
 *
 * This should not be combined with higher-layer network utilities because
 * it is possible for LT devices to operate at the MAC layer without IP.
 *
 */

#ifndef LTUTILITY_MACADDRESS_H
#define LTUTILITY_MACADDRESS_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

typedef struct LTMacAddress {
    u8 octet[6];
} LTMacAddress;

#define LTMacAddress_Broadcast {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}}
#define LTMacAddress_Zero {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}


/*******************************************************************************
** API
*******************************************************************************/

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTUtilityMacAddress, 1);

/**
 * @brief DOCUMENTATION_NEEDED.
 * @ingroup ltutility_macaddress
 */
struct LTUtilityMacAddressApi {

    INHERIT_LIBRARY_BASE

    bool (*StringToMacAddress)(const char *string, LTMacAddress *mac_address);
        /**<
        * Convert a string to a MAC address.
        * The MAC address separators can be ':' or '-'.
        * @param[in]  string MAC address string to parse.
        * @param[out] mac_address MAC address returned.
        * @return @p false if the MAC string is not valid.
        */

    void (*MacAddressToString)(const LTMacAddress *mac_address, char *string, char separator);
        /**<
        * Convert a MAC address to a MAC address string.
        * A digit separator can be specified. If NULL then there's no separator.
        * @param[in]  mac_address MAC address to convert.
        * @param[out] string MAC address string returned.
        * This array must be at least 18 characters long.
        * @param[in]  separator Hex digit separator, for example ':' returns 11:22:33:44:55:66.
        */

    void (*MacAddressToOui)(const LTMacAddress *mac_address, char *string, char separator);
        /**<
        * Convert a MAC address to a MAC address string with only the OUI part visible
        * A digit separator can be specified. If NULL then there's no separator.
        * @param[in]  mac_address MAC address to convert.
        * @param[out] string MAC address string returned.
        * This array must be at least 18 characters long.
        * @param[in]  separator Hex digit separator, for example ':' returns 11:22:33:xx:xx:xx.
        */

    u32 (*MacAddressToHash)(const LTMacAddress *mac_address);
        /**<
        * Convert a MAC address to a u32 hash value
        * Uses the same hash algorithm as RokuOS MACAddress::toHash()
        * @param[in] mac_address MAC address to convert.
        * @return @p hash value
        */

    bool (*IsZero)(const LTMacAddress *a);
        /**<
         * Check for zero MAC address.
         * @return @p true if MAC address is all zeroes.
         */

    bool (*IsBroadcast)(const LTMacAddress *a);
        /**<
         * Check for broadcast MAC address.
         * @return @p true if MAC address is broadcast.
         */

    bool (*IsEqual)(const LTMacAddress *a, const LTMacAddress *b);
        /**<
         * Compare two MAC addresses for equality.
         * @return @p true if MAC address are equal.
         */

};

LT_EXTERN_C_END
#endif /* #ifndef LTUTILITY_MACADDRESS_H */
