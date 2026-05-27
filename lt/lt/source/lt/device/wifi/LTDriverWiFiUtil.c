/******************************************************************************
 * LTDriverWiFiUtil.c
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/LT.h>
#include <lt/device/wifi/LTDriverWiFi.h>
#include <lt/device/wifi/LTDriverWiFiUtil.h>

LTWiFi_DisconnectReason LTDriverUtil_IeeeToLtDisconnectReason(WiFiDriver_80211ReasonCode reason)
{
    LTWiFi_DisconnectReason dcrsn = kLTWiFi_DisconnectReason_Generic;
    switch (reason) {
    /* Reason codes (IEEE Std 802.11-2016, 9.4.1.7, Table 9-45) */
    case WLAN_REASON_UNSPECIFIED:
        dcrsn = kLTWiFi_DisconnectReason_Generic;
        break;
    case WLAN_REASON_PREV_AUTH_NOT_VALID:
    case WLAN_REASON_DEAUTH_LEAVING:
        dcrsn = kLTWiFi_DisconnectReason_ApReceiveDeauth;
        break;
    case WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY:
    case WLAN_REASON_DISASSOC_AP_BUSY:
        dcrsn = kLTWiFi_DisconnectReason_ApReceiveDisassoc;
        break;
    case WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA:
    case WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA:
        dcrsn = kLTWiFi_DisconnectReason_ApReceiveDeauth;
        break;
    case WLAN_REASON_DISASSOC_STA_HAS_LEFT:
    case WLAN_REASON_STA_REQ_ASSOC_WITHOUT_AUTH:
    case WLAN_REASON_PWR_CAPABILITY_NOT_VALID:
    case WLAN_REASON_SUPPORTED_CHANNEL_NOT_VALID:
    case WLAN_REASON_BSS_TRANSITION_DISASSOC:
        dcrsn = kLTWiFi_DisconnectReason_ApReceiveDisassoc;
        break;
    case WLAN_REASON_INVALID_IE:
    case WLAN_REASON_MICHAEL_MIC_FAILURE:
        dcrsn = kLTWiFi_DisconnectReason_Generic;
        break;
    case WLAN_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WLAN_REASON_GROUP_KEY_UPDATE_TIMEOUT:
    case WLAN_REASON_IE_IN_4WAY_DIFFERS:
    case WLAN_REASON_GROUP_CIPHER_NOT_VALID:
    case WLAN_REASON_PAIRWISE_CIPHER_NOT_VALID:
        dcrsn = kLTWiFi_DisconnectReason_ApReceiveDeauth;
        break;
    case WLAN_REASON_AKMP_NOT_VALID:
    case WLAN_REASON_UNSUPPORTED_RSN_IE_VERSION:
    case WLAN_REASON_INVALID_RSN_IE_CAPAB:
    case WLAN_REASON_IEEE_802_1X_AUTH_FAILED:
    case WLAN_REASON_CIPHER_SUITE_REJECTED:
    case WLAN_REASON_TDLS_TEARDOWN_UNREACHABLE:
    case WLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED:
    case WLAN_REASON_SSP_REQUESTED_DISASSOC:
    case WLAN_REASON_NO_SSP_ROAMING_AGREEMENT:
    case WLAN_REASON_BAD_CIPHER_OR_AKM:
    case WLAN_REASON_NOT_AUTHORIZED_THIS_LOCATION:
    case WLAN_REASON_SERVICE_CHANGE_PRECLUDES_TS:
    case WLAN_REASON_UNSPECIFIED_QOS_REASON:
    case WLAN_REASON_NOT_ENOUGH_BANDWIDTH:
    case WLAN_REASON_DISASSOC_LOW_ACK:
    case WLAN_REASON_EXCEEDED_TXOP:
    case WLAN_REASON_STA_LEAVING:
    case WLAN_REASON_END_TS_BA_DLS:
    case WLAN_REASON_UNKNOWN_TS_BA:
        dcrsn = kLTWiFi_DisconnectReason_Generic;
        break;
    case WLAN_REASON_TIMEOUT:
        dcrsn = kLTWiFi_DisconnectReason_DriverApKeepaliveTimeout;
        break;
    case WLAN_REASON_PEERKEY_MISMATCH:
    case WLAN_REASON_AUTHORIZED_ACCESS_LIMIT_REACHED:
        dcrsn = kLTWiFi_DisconnectReason_ApReceiveDeauth;
        break;
    case WLAN_REASON_EXTERNAL_SERVICE_REQUIREMENTS:
    case WLAN_REASON_INVALID_FT_ACTION_FRAME_COUNT:
    case WLAN_REASON_INVALID_PMKID:
    case WLAN_REASON_INVALID_MDE:
    case WLAN_REASON_INVALID_FTE:
        dcrsn = kLTWiFi_DisconnectReason_Generic;
        break;
    default:
        dcrsn = kLTWiFi_DisconnectReason_Generic;
    }
    return dcrsn;
}
