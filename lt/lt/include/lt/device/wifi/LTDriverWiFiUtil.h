/*******************************************************************************
 *
 * LTDriverWiFiUtil: WiFi Driver Utilities
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/
#ifndef __LTWIFIDRIVERUTIL_H__
#define __LTWIFIDRIVERUTIL_H__
#include <lt/LT.h>
#include <lt/device/wifi/LTDriverWiFi.h>
#include <lt/device/wifi/LTDriverWiFiUtil.h>
/**
 * @brief 802.11 Standard reason codes.
 */
typedef enum WiFiDriver_80211ReasonCode {
    WLAN_REASON_UNSPECIFIED                         = 1,
    WLAN_REASON_PREV_AUTH_NOT_VALID                 = 2,
    WLAN_REASON_DEAUTH_LEAVING                      = 3,
    WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY          = 4,
    WLAN_REASON_DISASSOC_AP_BUSY                    = 5,
    WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA       = 6,
    WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA      = 7,
    WLAN_REASON_DISASSOC_STA_HAS_LEFT               = 8,
    WLAN_REASON_STA_REQ_ASSOC_WITHOUT_AUTH          = 9,
    WLAN_REASON_PWR_CAPABILITY_NOT_VALID            = 10,
    WLAN_REASON_SUPPORTED_CHANNEL_NOT_VALID         = 11,
    WLAN_REASON_BSS_TRANSITION_DISASSOC             = 12,
    WLAN_REASON_INVALID_IE                          = 13,
    WLAN_REASON_MICHAEL_MIC_FAILURE                 = 14,
    WLAN_REASON_4WAY_HANDSHAKE_TIMEOUT              = 15,
    WLAN_REASON_GROUP_KEY_UPDATE_TIMEOUT            = 16,
    WLAN_REASON_IE_IN_4WAY_DIFFERS                  = 17,
    WLAN_REASON_GROUP_CIPHER_NOT_VALID              = 18,
    WLAN_REASON_PAIRWISE_CIPHER_NOT_VALID           = 19,
    WLAN_REASON_AKMP_NOT_VALID                      = 20,
    WLAN_REASON_UNSUPPORTED_RSN_IE_VERSION          = 21,
    WLAN_REASON_INVALID_RSN_IE_CAPAB                = 22,
    WLAN_REASON_IEEE_802_1X_AUTH_FAILED             = 23,
    WLAN_REASON_CIPHER_SUITE_REJECTED               = 24,
    WLAN_REASON_TDLS_TEARDOWN_UNREACHABLE           = 25,
    WLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED           = 26,
    WLAN_REASON_SSP_REQUESTED_DISASSOC              = 27,
    WLAN_REASON_NO_SSP_ROAMING_AGREEMENT            = 28,
    WLAN_REASON_BAD_CIPHER_OR_AKM                   = 29,
    WLAN_REASON_NOT_AUTHORIZED_THIS_LOCATION        = 30,
    WLAN_REASON_SERVICE_CHANGE_PRECLUDES_TS         = 31,
    WLAN_REASON_UNSPECIFIED_QOS_REASON              = 32,
    WLAN_REASON_NOT_ENOUGH_BANDWIDTH                = 33,
    WLAN_REASON_DISASSOC_LOW_ACK                    = 34,
    WLAN_REASON_EXCEEDED_TXOP                       = 35,
    WLAN_REASON_STA_LEAVING                         = 36,
    WLAN_REASON_END_TS_BA_DLS                       = 37,
    WLAN_REASON_UNKNOWN_TS_BA                       = 38,
    WLAN_REASON_TIMEOUT                             = 39,
    /* No reason code defined 40 ~ 44 */
    WLAN_REASON_PEERKEY_MISMATCH                    = 45,
    WLAN_REASON_AUTHORIZED_ACCESS_LIMIT_REACHED     = 46,
    WLAN_REASON_EXTERNAL_SERVICE_REQUIREMENTS       = 47,
    WLAN_REASON_INVALID_FT_ACTION_FRAME_COUNT       = 48,
    WLAN_REASON_INVALID_PMKID                       = 49,
    WLAN_REASON_INVALID_MDE                         = 50,
    WLAN_REASON_INVALID_FTE                         = 51,
} WiFiDriver_80211ReasonCode;
LTWiFi_DisconnectReason LTDriverUtil_IeeeToLtDisconnectReason(WiFiDriver_80211ReasonCode reason);
#endif
