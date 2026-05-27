/*******************************************************************************
 * LTUtilityIPAddress
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltutility_ipaddress LTUtilityIPAddress
 * @ingroup ltutility
 *
 * @brief Common utility functions used for processing IP addresses.
 *
 * This should not be combined with lower-layer network utilities because
 * it is possible for LT devices to operate at the MAC layer without IP.
 *
 */

#ifndef LTUTILITY_IPADDRESS_H
#define LTUTILITY_IPADDRESS_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

typedef u32 LTIPAddress;

#define LTIPAddress_Strlen_Max 16

#define LTIPAddress_Broadcast 0xffffffff
#define LTIPAddress_Zero      0x00000000
#define LTIPAddress_Loopback  0x0100007f

/* PRINTF STYLE MACRO: LT_PRIIP and LT_PLT_IP(LTIPAddress_var) */
/* Example: lt_snprintf(buf, buflen, "IP:<" LT_PRIIP ">", LT_PLT_IP(ip)); */
#define LT_PRIIP "%lu.%lu.%lu.%lu"
#define LT_PLT_IP(x)                                                    \
    LT_Pu32((x) & 0xFF), LT_Pu32(((x) >> 8) & 0xFF),                    \
        LT_Pu32(((x) >> 16) & 0xFF), LT_Pu32(((x) >> 24) & 0xFF)

/*******************************************************************************
** API
*******************************************************************************/

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTUtilityIPAddress, 1);

/**
 * @brief DOCUMENTATION_NEEDED.
 * @ingroup ltutility_ipaddress
 */
struct LTUtilityIPAddressApi {

    INHERIT_LIBRARY_BASE

    bool (*StringToIPAddress)(const char *string, LTIPAddress *ip_address);
        /**<
        * Convert a string to a IP address.
        * @param[in]  string IP address string to parse.
        * @param[out] ip_address IP address returned.
        * @return @p false if the IP string is not parsed.
        */

    void (*IPAddressToString)(const LTIPAddress ip_address, char *string);
        /**<
        * Convert a IP address to a IP address string.
        * @param[in]  ip_address IP address to convert.
        * @param[out] string IP address string returned.
        * This array must be at least 16 characters long.
        */

    bool (*IsZero)(const LTIPAddress a);
        /**<
         * Check for zero IP address.
         * @return @p true if IP address is all zeroes.
         */

    bool (*IsBroadcast)(const LTIPAddress a);
        /**<
         * Check for broadcast IP address.
         * @return @p true if IP address is broadcast.
         */

    bool (*IsEqual)(const LTIPAddress a, const LTIPAddress b);
        /**<
         * Compare two MAC addresses for equality.
         * @return @p true if MAC address are equal.
         */

};

LT_EXTERN_C_END
#endif /* #ifndef LTUTILITY_IPADDRESS_H */
