/*******************************************************************************
 *
 * DriverWiFiSupp: WiFi Driver Library using WPA Supplicant
 * ----------------------------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTArray.h>
#include <lt/device/wifi/LTDriverWiFi.h>
#include <lt/device/wifi/LTDriverWiFiUtil.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <lt/system/logger/LTSystemLogger_Records.h>
// Do not include LTDeviceWiFi.h to provide isolation from upper WiFi layers

#include <fcntl.h>  // for open(2) used in readfile()
#include <stddef.h> // for size_t used in wpa_ctrl.h
#include <unistd.h> // for read(2) and close(2) used in readfile()
#include "wpa_ctrl.h"
DEFINE_LTLOG_SECTION("linux.drv.wifi");
#define LTLOG_LEVEL(level, pTag, pFormat, ...)                          \
    if (level & LTAtomic_Load(&unit->DebugMode)) LTLOG(pTag, pFormat, ##__VA_ARGS__)
#define LTLOG_LEVEL_DEBUG(level, pTag, pFormat, ...) \
    if (level & LTAtomic_Load(&unit->DebugMode)) LTLOG_DEBUG(pTag, pFormat, ##__VA_ARGS__)
enum {
    // Bit 0-2: Common for all modules
    kLogDebug   = 0x01,
    kLogVerbose = 0x02,
    kLogEvent   = 0x04,
    // Bit 3+: Local for current module
    kLogState   = 0x08,  // Log WiFi StateMachine changes
    kLogRX      = 0x10,  // Log WiFi Tx
    kLogTX      = 0x20,  // Log WiFi Rx
    kLogMGMT    = 0x40,  // Log WiFi Mgmt frames
    kLogBLE     = 0x80,  // Log Bluetooth
    kLogWPA     = 0x100, // Log wpa_supplicant (LT on linux)
};

static const char ctrl_iface_path[] = "/var/run/wpa_supplicant/wlan0";

/*******************************************************************************
 * LT Related Declarations
 ******************************************************************************/

// interfaces
static ILTThread                    * iThread       = NULL;

// handles
static LTCore                       * pCore         = NULL;
static LTUtilityMacAddress          * pMacAddress   = NULL;
static LTUtilityByteOps             * pByte         = NULL;
static LTSystemLogger               * pLogger       = NULL;

/*******************************************************************************
 *
 * WiFi Driver Abstract Interface
 * ------------------------------
 *
 * This interface provides isolation between the WiFi state machine and vendor
 * specific WiFi driver and WPA supplicant implementations.
 *
 * IMPORTANT RULE: This is no blocking or timing at this driver interface.
 * That makes it possible for the LTDeviceWifi layer to handle all time
 * related conditions, including timeouts. Please heed this rule.
 *
 * This interface is called only by the WiFi state machine which coordinates
 * and synchronizes access to these features to minimize locking and reduce
 * complex state management within the vendor device implementation. Only
 * short-period blocking is allowed. The WiFi state machine will also prevent
 * overlapped calls that would cause driver conflicts or malfunctions. For
 * example, a scan will not be called while a join is being processed.
 *
 ******************************************************************************/

typedef u8 DriverWiFiSupp_ScanStatus;
enum DriverWiFiSupp_ScanStatus {
    // Initializing status:
    kDriverWiFiSupp_ScanStatus_Unknown = 0,  ///< not in use
    // Functioning status:
    kDriverWiFiSupp_ScanStatus_Starting,     ///< SCAN started
    kDriverWiFiSupp_ScanStatus_Scanning,     ///< AP scan in progress
    kDriverWiFiSupp_ScanStatus_Failed  = 10, ///< SCAN failed, see status flow
    kDriverWiFiSupp_ScanStatus_Aborted,      ///< SCAN stopped by user
    kDriverWiFiSupp_ScanStatus_Success = 20, ///< SCAN was successful
};

typedef u8 DriverWiFiSupp_DisconnectReason;
enum DriverWiFiSupp_DisconnectReason {
    /* Reason codes < 200: (IEEE Std 802.11-2016, 9.4.1.7, Table 9-45) */

    /* Local reason codes  */
    ///< CTRL-EVENT-NETWORK-NOT-FOUND
    kDriverWiFiSupp_DisconnectReason_NoApFound = 201,

    ///< CTRL-EVENT-ASSOC-REJECT
    kDriverWiFiSupp_DisconnectReason_Reject = 202,
};
static LTWiFi_DisconnectReason DriverWiFi_GetDiscReasonCode(LTDeviceUnit h_unit);

typedef struct WiFiUnit {
    // Unit variables for this module
    LTAtomic DebugMode;
    bool connected;
    LTWiFi_DriverState DriverState;
    LTWiFi_ApInfo ApJoinSpec;
    LTWiFi_ScanSpec ScanSpec;
    LTMutex *mutex;
    LTWiFi_JoinStatus_CB *join_callback;
    LTWiFi_JoinStatus join_status;
    LTWiFi_ScanResults_CB *scan_callback;
    DriverWiFiSupp_ScanStatus scan_status;
    LTThread monitor_LTThread;
    bool attached;
    struct wpa_ctrl *ctrl;
    struct wpa_ctrl *event;
    LTString ctrl_reply;
    DriverWiFiSupp_DisconnectReason disconnect_reason;
    LTWiFi_ApInfo ApInfo; // cached to allow disconnect logging
    char ApInfo_ie[2048]; // ApInfo IE vector

    LTAtomic joinSuccess;            ///< New Auto-rejoin successes
    LTAtomic joinFail;               ///< New Auto-rejoin fails
    LTAtomic joinDisc;               ///< New Auto-rejoin disconnects
} WiFiUnit;

static WiFiUnit s_unit;

#define CLEAR(v) lt_memset(v, 0, sizeof(*v))

// Get rid of unit handles in this driver - we don't use them and they just present more failure surface
// In fact, the LTDriverWiFi should be modified to take a data pointer, not a handle.
#define GET_UNIT(wu, hu) WiFiUnit *wu = &s_unit; LT_UNUSED(wu); LT_UNUSED(hu);
#define GET_UNIT_RETURN(wu, hu, code) WiFiUnit *wu = &s_unit; LT_UNUSED(wu); LT_UNUSED(hu); LT_UNUSED(code);

/** Driver Library Root Interface ********************************************/

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDriverWiFi, LinuxDriverWiFi);
/*
 *   This special macro is defined in LTTypes.h.  It provides a declaration
 *   of a  hidden library root interface of type LTDriverLibrary which carries
 *   the prototype functions immediately below.
 *   The first argument "LTDriverWiFi" tells LTCore that this driver library
 *   contains implementation(s) of LTDriverWiFi and it also slaves the library
 *   root interface version to the LTDriverWiFi version it was compiled against.
 *
 *   In total, this macro does 5 things:
 *    1. Creates a root library interface for the driver library
 *    2. Types it as an LTDriverLibrary, giving it the driver type and the
 *       prototype functions thereof
 *    3. Slaves the root interface version number to that of the driver
 *       interface number this library is compiled against so the Device
 *       library opening this driver library can find out the version of
 *       the driver interface(s) the library will supply before calling into
 *       the library to create unit handles
 *    4. Allows for more than one driver library to exist at runtime for the
 *       same device class.
 *    5. Communicates the driver mapping to LTCore so that at runtime When
 *       LTDeviceWiFi wants to know what driver libraries supply WiFi drivers,
 *       LTCore can tell it.
 *
 */

/*************************************************************
 * Utility functions for working with wpa_supplicant strings *
 *************************************************************/
// Parse wpa_supplicant replies for <name>=<value>
static void get_field_str(const char *text, const char *name,
                          char *value, LT_SIZE value_len) {
    char *textbuf, *line, *end, *val;

    if (value_len == 0 || value == NULL) return; // sanity check
    value[0] = '\0';
    textbuf = lt_strdup(text);
    for (line = textbuf; line != NULL; line = end) {
        if ((end = lt_strchr(line, '\n')) != NULL)
            *(end++) = '\0';
        if ((val = lt_strchr(line, '=')) != NULL)
            *(val++) = '\0';
        if ((lt_strcmp(name, line) == 0) && val != NULL) {
            lt_strncpyTerm(value, val, value_len);
            break;
        }
    }
    lt_free(textbuf);
}

static LTWiFi_ApSecurity get_field_security(const char *text) {
    char field[50];

    get_field_str(text, "flags", field, sizeof(field));
    if (lt_strlen(field) == 0)
        return kLTWiFi_ApSecurity_Unknown;
    if (lt_strstr(field, "[WPA2-SAE+PSK") != NULL)
        return kLTWiFi_ApSecurity_Wpa2Wpa3;
    if (lt_strstr(field, "[WPA2-PSK+SAE") != NULL)
        return kLTWiFi_ApSecurity_Wpa2Wpa3;
    if (lt_strstr(field, "[WPA2--CCMP") != NULL)
        return kLTWiFi_ApSecurity_Wpa3;
    if (lt_strstr(field, "[WPA2-SAE") != NULL)
        return kLTWiFi_ApSecurity_Wpa3;
    if (lt_strstr(field, "[WPA2-PSK-TKIP") != NULL)
        return kLTWiFi_ApSecurity_Wpa2Tkip;
    if (lt_strstr(field, "[WPA2-PSK") != NULL)
        return kLTWiFi_ApSecurity_Wpa2;
    if (lt_strstr(field, "[WPA-PSK") != NULL)
        return kLTWiFi_ApSecurity_Wpa;
    if (lt_strstr(field, "[WEP") != NULL)
        return kLTWiFi_ApSecurity_Wep;
    if (lt_strstr(field, "[WPA2-EAP") != NULL)
        return kLTWiFi_ApSecurity_Wpa2Eap;
    if (lt_strstr(field, "[WPA-EAP") != NULL)
        return kLTWiFi_ApSecurity_WpaEap;
    if (lt_strstr(field, "[WPA3-EAP") != NULL)
        return kLTWiFi_ApSecurity_Wpa3Eap;
    return kLTWiFi_ApSecurity_Open;
}

static LTWiFi_ApSecurity getAPSecurity(LTDeviceUnit h_unit, const char *status) {
    GET_UNIT_RETURN(unit, h_unit, kLTWiFi_ApSecurity_Unknown);
    char cipher[50];
    char sec[50];

    get_field_str(status, "pairwise_cipher", cipher, sizeof(cipher));
    get_field_str(status, "key_mgmt", sec, sizeof(sec));
    LTLOG_LEVEL_DEBUG(kLogDebug, "sec.km", "security key-mgmt: %s cipher: %s", sec, cipher);
    if (lt_strlen(cipher) == 0)
        return kLTWiFi_ApSecurity_Unknown;
    if (lt_strlen(sec) == 0)
        return kLTWiFi_ApSecurity_Unknown;
    if (lt_strstr(sec, "SAE"))
       return kLTWiFi_ApSecurity_Wpa3;
    if (lt_strstr(sec, "WPA2-"))
        return kLTWiFi_ApSecurity_Wpa2;
    if (lt_strstr(sec, "WPA-"))
        return kLTWiFi_ApSecurity_Wpa;
    if (lt_strstr(cipher, "WEP"))
        return kLTWiFi_ApSecurity_Wep;
    return kLTWiFi_ApSecurity_Open;
}

static int get_field_int(const char *text, const char *name, int default_value) {
    char field[50];
    int val = default_value;

    get_field_str(text, name, field, sizeof(field));
    if (lt_isdigit(field[0]) || field[0] == '-')
        val = lt_strtos32(field, NULL, 10);
    return val;
}

static void convert_unicode(char *text) {
    // Makes the changes inline in "text"
    LT_SIZE from = 0;
    LT_SIZE to = 0;

    // look for \\ \" \n \r \t \033 and \xff to convert
    for (from = 0, to = 0; (text[to] = text[from]) != '\0'; from++, to++) {
        if (text[from] == '\\') {
            switch (text[from + 1]) {
            case '\\':
            case '"':
                text[to] = text[from + 1];
                from++;
                break;
            case 'n':
                text[to] = '\n';
                from++;
                break;
            case 'r':
                text[to] = '\r';
                from++;
                break;
            case 't':
                text[to] = '\t';
                from++;
                break;
            case 'e':
                text[to] = '\033';
                from++;
                break;
            case 'x':
                if (lt_isxdigit(text[from + 2]) &&
                    lt_isxdigit(text[from + 3])) {
                    char hex[3] = {0};
                    hex[0] = text[from + 2];
                    hex[1] = text[from + 3];
                    text[to] = lt_strtos32(hex, NULL, 16);
                    from += 3;
                }
                break;
            default:
                break;
            }
        }
    }
}

static u8 freqToChan(u16 freq) {
    const int bandwidth = 5; // MHz
    // Note: Returns zero for invalid channels.
    if (freq >  5905) return 0;
    // 5G:
    if (freq >= 5000) return ((freq - 5000) / bandwidth);
    // 2.4G: No channel 14 allowed (no Japan)
    if (freq >  2472) return 0;  // no > ch 13
    // 2.4G:
    if (freq >= 2412) return ((freq - 2412) / bandwidth) + 1;
    return 0;
}

static u16 chanToFreq(u8 chan) {
    const int bandwidth = 5; // MHz
    // Note: Returns zero for invalid channels.
    if (chan > 181) return 0;
    // 5G:
    if (chan >= 36) return ((chan * bandwidth) + 5000);
    // 2.4G: No channel 14 allowed (no Japan)
    if (chan >  13) return 0;  // no > ch 13
    // 2.4G:
    if (chan >=  1) return ((chan - 1) * bandwidth) + 2412;
    return 0;
}

/*******************************************************
 * Utility functions for working with /proc and /sys   *
 *******************************************************/
static void readfile(char *value, LT_SIZE value_len, const char *pathname) {
    int fd;

    if (value != NULL && value_len > 0) {
        lt_memset(value, 0, value_len);
        if ((fd = open(pathname, O_RDONLY)) >= 0) {
            if (read(fd, value, value_len - 1) == -1) {
                LTLOG("readfile.read", "Error reading %s\n", pathname);
            }
            close(fd);
        }
    }
}

static void get_chip(char *chip, size_t chip_len) {
    char buf[60];
    int i;
    const char *known_chips[][2] = {{"sdio:007A:6011", "ATGBM6031"},
                                    {"sdio:024C:F179", "RTL8189"},
                                    {"sdio:024C:8179", "RTL8179"},
                                    {"usb:0BDA:C811", "RTL8811CU"},
                                    {"usb:0BDA:8176", "RTL8188CUS"},
                                    {"pci:8086:A0F0", "Intel AX201"},
                                    {NULL, NULL}};

    // Find WiFi chip identifier in /sys/class/net/wlan0
    readfile(buf, sizeof(buf), "/sys/class/net/wlan0/device/modalias");
    switch (lt_strlen(buf)) {
    case 19: // SDIO
        lt_snprintf(chip, chip_len, "%4.4s:%4.4s:%4.4s",
                    buf, &buf[9], &buf[14]);
        break;
    case 50: // USB
        lt_snprintf(chip, chip_len, "%3.3s:%4.4s:%4.4s",
                    buf, &buf[5], &buf[10]);
        break;
    case 54: // PCI
        lt_snprintf(chip, chip_len, "%3.3s:%4.4s:%4.4s",
                    buf, &buf[9], &buf[18]);
        break;
    default:
        lt_strncpyTerm(chip, buf, chip_len);
    }
    // Give a more descriptive name to know chips
    for (i = 0; known_chips[i][0] != NULL; i++) {
        if (lt_strcasecmp(known_chips[i][0], chip) == 0) {
            lt_strncpyTerm(chip, known_chips[i][1], chip_len);
            break;
        }
    }
}

/*******************************************************
 * Utility functions for logging AP info and IE vector *
 *******************************************************/
LTLOG_DEFINE_RECORD_APINFO(ApInfo); // Defines ApInfo and ApInfo_Record

static void getStr(char *dest, LT_SIZE destlen, u8 *data, u8 datalen, u8 offset, u8 strlen) {
    // Copy string from IE - IE strings aren't zero terminated
    if (offset <= datalen && destlen > 0) {
        LT_SIZE i;
        for (i = 0 ; (offset + i) < datalen && (i + 1) < destlen && i < strlen; i++) {
            dest[i] = data[offset + i];
        }
        dest[i] = '\0';
    } else {
        LTLOG_YELLOWALERT(__FUNCTION__, "parse_ies() is trying to read past buffer end");
    }
}

static u8 getBit(u8 *data, u8 datalen, u8 offset, u8 bit) {
    if (offset < datalen) {
        return (data[offset] >> bit) & 0x1;
    } else {
        LTLOG_YELLOWALERT(__FUNCTION__, "parse_ies() is trying to read past buffer end");
    }
    return 0;
}

static u8 getByte(u8 *data, u8 datalen, u8 offset) {
    if (offset < datalen) {
        return data[offset];
    } else {
        LTLOG_YELLOWALERT(__FUNCTION__, "parse_ies() is trying to read past buffer end");
    }
    return 0;
}

static u16 getLE16(u8 *data, u8 datalen, u8 offset) {
    if (offset + 1 < datalen) {
        return LT_LE16(*(u16 *)&data[offset]);
    } else {
        LTLOG_YELLOWALERT(__FUNCTION__, "parse_ies() is trying to read past buffer end");
    }
    return 0;
}

static u16 getBE16(u8 *data, u8 datalen, u8 offset) {
    if (offset + 1 < datalen) {
        return LT_BE16(*(u16 *)&data[offset]);
    } else {
        LTLOG_YELLOWALERT(__FUNCTION__, "parse_ies() is trying to read past buffer end");
    }
    return 0;
}

static u32 getLE32(u8 *data, u8 datalen, u8 offset) {
    if (offset + 3 < datalen) {
        return LT_LE32(*(u32 *)&data[offset]);
    } else {
        LTLOG_YELLOWALERT(__FUNCTION__, "parse_ies() is trying to read past buffer end");
    }
    return 0;
}

static u32 getBE32(u8 *data, u8 datalen, u8 offset) {
    if (offset + 3 < datalen) {
        return LT_BE32(*(u32 *)&data[offset]);
    } else {
        LTLOG_YELLOWALERT(__FUNCTION__, "parse_ies() is trying to read past buffer end");
    }
    return 0;
}

/* Information Element IDs - see include/linux/ieee80211.h */
enum ieee80211_eid {
    WLAN_EID_SSID = 0,
    WLAN_EID_DS_PARAMS = 3,
    WLAN_EID_QBSS_LOAD = 11,
    WLAN_EID_HT_CAPABILITY = 45,
    WLAN_EID_RSN = 48,
    WLAN_EID_HT_OPERATION = 61,
    WLAN_EID_MESH_CONFIG = 113,
    WLAN_EID_MESH_ID = 114,
    WLAN_EID_EXT_CAPABILITY = 127,
    WLAN_EID_VHT_CAPABILITY = 191,
    WLAN_EID_VHT_OPERATION = 192,
    WLAN_EID_REDUCED_NEIGHBOR_REPORT = 201,
    WLAN_EID_VENDOR_SPECIFIC = 221,
    WLAN_EID_EXTENSION = 255
};

/* Element ID Extensions for Element ID 255 - see include/linux/ieee80211.h */
enum ieee80211_eid_ext {
    WLAN_EID_EXT_HE_CAPABILITY = 35,
    WLAN_EID_EXT_HE_OPERATION = 36,
    WLAN_EID_EXT_HE_6GHZ_CAPA = 59
};

static void parse_ies(char *ie_string, LTLogRecordApInfo *record) {
    // Get the binary IE from Hex data
    u8 *ie = NULL;
    u32 ie_len;

    const LT_SIZE ie_size = 1024;
    if ((ie = lt_malloc(ie_size)) == NULL) return;
    lt_memset(ie, 0, ie_size);

    for (ie_len = 0 ; ie_len < ie_size ; ie_len++) {
        if (!lt_hextobyte(ie_string + 2 * ie_len, 2, &ie[ie_len])) break;
    }

    // Parse IE vector into tags/ext tags
    for (u32 n = 0 ; n + 1 < ie_len ; n += ie[n + 1] + 2) {
        u8 tag = ie[n];
        u8 datalen = ie[n + 1];
        u8 *data = &(ie[n + 2]);
        if (n + 2 + datalen > ie_len) break; // EOD

        switch (tag) {
        case WLAN_EID_SSID: // SSID (0)
            break;

        case WLAN_EID_DS_PARAMS: // Channel number (3)
            break;

        case WLAN_EID_QBSS_LOAD: if (datalen == 5) { // QBSS Load Element (11)
                float utilpct = (float)getByte(data, datalen, 2) / 2.55;
                record->utilpct = (int)(utilpct + 0.5);
            } break;

        case WLAN_EID_HT_CAPABILITY: if (datalen >= 26) { // HT Capabilities (45)
                u16 ht_cap = getLE16(data, datalen, 0);
                u32 rx_mcs = getLE32(data, datalen, 3);
                u32 txbf = getLE32(data, datalen, 21);

                record->ht_streams = 0;
                while ((rx_mcs & 0xff) != 0 && record->ht_streams < 4) {
                    record->ht_streams += 1;
                    rx_mcs = rx_mcs >> 8;
                }
                record->ht_en = (record->ht_streams > 0) ? 1 : 0;
                // LDPC
                record->ht_ldpc = ht_cap & 0x1;
                // TxBF
                record->ht_txbf = txbf & 0x1;
                // STBC
                record->ht_txstbc = (ht_cap >> 7) & 0x1;
                record->ht_rxstbc = (ht_cap >> 8) & 0x3;
            } break;

        case WLAN_EID_RSN: if (datalen >= 8) { // RSN Authentication methods support WPA3? (48)
                // RSN Header and Cipher Suite
                u16 pcs_count = getLE16(data, datalen, 6);
                // RSN Autk Key Management (AKM)
                if (datalen >= 8 + (4 * pcs_count) + 2) {
                    u16 akm_count = getLE16(data, datalen, 8 + (4 * pcs_count));
                    u8 akm_offset = 8 + (4 * pcs_count) + 2;
                    for (int i = 0 ; i < akm_count ; i++) {
                        u8 akm_idx = akm_offset + (4 * i);
                        if (akm_idx + 4 <= datalen) {
                            u32 oui = getBE32(data, datalen, akm_idx) >> 8;
                            u8 akm_type = getByte(data, datalen, akm_idx + 3);
                            if (oui == 0x000FAC) {
                                if (akm_type == 5 || akm_type == 8 || akm_type == 12) {
                                    record->wpa3 = 1;
                                }
                            }
                        }
                    }
                }
            } break;

        case WLAN_EID_HT_OPERATION: if (datalen >= 22) {// HT Information (61)
                u8 ht_info1 = getByte(data, datalen, 1);
                // Bandwidth
                record->ht_bw = ((ht_info1 >> 2) & 0x1) ? 40 : 20;
            } break;

        case WLAN_EID_MESH_CONFIG: if (datalen == 7) { // Mesh Configuration (113)
                u8 m_form = data[5];
                u8 m_cap = data[6];
                record->mesh_con_gate = m_form & 0x1;
                record->mesh_peers = (m_form >> 1) & 0x3f;
                record->mesh_fw = (m_cap >> 3) & 0x1;
            } break;

        case WLAN_EID_MESH_ID: if (datalen > 0) { // Mesh ID (114)
                getStr(record->mesh_id, sizeof(record->mesh_id), data, datalen, 0, datalen);
            } break;

        case WLAN_EID_EXT_CAPABILITY: if (datalen >= 3) { // Extended Capabilities (127)
                record->bss_trans = getBit(data, datalen, 2, 3);
                record->time_measure = getBit(data, datalen, 2, 7);
            } break;

        case WLAN_EID_VHT_CAPABILITY: if (datalen >= 12) { // VHT Capabilities (191)
                u32 vht_cap = getLE32(data, datalen, 0);
                u32 rx_mcs = getLE32(data, datalen, 4);
                record->vht_streams = 0;
                while ((rx_mcs & 0x3) != 0x3 && record->vht_streams < 8) {
                    record->vht_streams += 1;
                    rx_mcs = rx_mcs >> 2;
                }
                record->vht_en = (record->vht_streams > 0) ? 1 : 0;
                // LDPC
                record->vht_ldpc = (vht_cap >> 4) & 0x1;
                // TxBF (Either SU or MU Beamformer)
                record->vht_txbf = ((vht_cap >> 11) & 0x1) | ((vht_cap >> 19) & 0x1);
                // STBC
                record->vht_txstbc = (vht_cap >> 7) & 0x1;
                record->vht_rxstbc = (vht_cap >> 8) & 0x7;
            } break;

        case WLAN_EID_VHT_OPERATION: if (datalen >= 5) { // VHT Operation (192)
                u8 vht_cw = getByte(data, datalen, 0);
                u8 vht_ccs1 = getByte(data, datalen, 2);
                // Bandwidth
                record->vht_bw = record->ht_bw;
                if (vht_cw == 1 && vht_ccs1 == 0) record->vht_bw = 80;
                if (vht_cw == 1 && vht_ccs1 >= 1) record->vht_bw = 160;
                if (vht_cw >= 2) record->vht_bw = 160;
            } break;

        case WLAN_EID_REDUCED_NEIGHBOR_REPORT: if (datalen >= 4) { // HE 6 GHz Channel - RNR (201)
                u8 oclass = getByte(data, datalen, 2);
                u8 chan = getByte(data, datalen, 3);
                if (oclass >= 131 && oclass <= 136) {
                        record->he_wifi6e = 1;
                        record->he_wifi6e_chan = chan;
                }
            } break;

        case WLAN_EID_VENDOR_SPECIFIC: if (datalen >= 4) { // Vendor Specific Data (221)
                u32 oui = getBE32(data, datalen, 0);
                if (oui == 0x0050f204) { // WPS: Manufacturer, Model Name, Model Number, RF Bands
                    // Each WPS sub element starts with an type and a length encoded in LittleEndian
                    u16 vie_typ = 0;
                    u16 vie_len = 0;
                    for (u32 v = 4 ; v + 4 <= datalen ; v += vie_len + 4) {
                        vie_typ = getBE16(data, datalen, v);
                        vie_len = getBE16(data, datalen, v + 2);
                        if (v + 4 + vie_len > datalen) break; // EOD
                        switch (vie_typ) {
                        case 0x1021: // Manufactorer
                            getStr(record->manuf, sizeof(record->manuf), data, datalen, v + 4, vie_len);
                            break;
                        case 0x1023: // Model Name
                            getStr(record->model, sizeof(record->model), data, datalen, v + 4, vie_len);
                            break;
                        case 0x1024: // Model Number
                            getStr(record->modelno, sizeof(record->modelno), data, datalen, v + 4, vie_len);
                            break;
                        case 0x103c: // RF Bands
                            record->rf_bands = getByte(data, datalen, v + 4);
                            break;
                        default:
                            break;
                        }
                    }
                    break;
                }

                // Chipguess based on which Vendor Specific Tags OUI are found
                char *chipstr = NULL;
                switch (oui >> 8) {
                case 0x83fdf0: chipstr = "Qualcomm"; break;
                case 0x00037f: chipstr = "Atheros"; break;
                case 0x001018: chipstr = "Broadcom"; break;
                case 0x000ce7: chipstr = "MediaTek"; break;
                case 0x000c43: chipstr = "Ralink"; break;
                case 0x005043: chipstr = "Marvell"; break;
                case 0x002686: chipstr = "Quantenna"; break;
                case 0x00e04c: chipstr = "Realtek"; break;
                default: break;
                }
                if (chipstr != NULL) lt_strncpyTerm(record->chipguess, chipstr, sizeof(record->chipguess)); break;
            } break;

        case WLAN_EID_EXTENSION: if (datalen > 0) { // Extended tag (255)
                u8 exttag = data[0];
                data++;
                datalen--;
                switch (exttag) {
                case WLAN_EID_EXT_HE_CAPABILITY: if (datalen >= 21) { // HE Capabilities (35)
                        u8 bw_set = getByte(data, datalen, 6);
                        u16 he_phy8  = getLE16(data, datalen, 7);
                        u16 he_phy24 = getLE16(data, datalen, 9);
                        u16 he_phy40 = getLE16(data, datalen, 11);
                        u16 he_phy56 = getLE16(data, datalen, 13);
                        u16 he_phy72 = getLE16(data, datalen, 15);
                        u16 rx_mcs = getLE16(data, datalen, 17);
                        record->he_streams = 0;
                        while ((rx_mcs & 0x3) != 0x3 && record->he_streams < 8) {
                            record->he_streams += 1;
                            rx_mcs = rx_mcs >> 2;
                        }
                        record->he_en = (record->he_streams > 0) ? 1 : 0;
                        // LDPC
                        record->he_ldpc = (he_phy8 >> 5) & 0x1;
                        // Bandwidth
                        record->he_bw = 20;
                        if ((bw_set >> 1) & 0x1) record->he_bw = 40;
                        if ((bw_set >> 2) & 0x1) record->he_bw = 80;
                        if ((bw_set >> 3) & 0x1) record->he_bw = 160;
                        if ((bw_set >> 4) & 0x1) record->he_bw = 160;
                        // TxBF (Either SU or MU Beamformer)
                        record->he_txbf = ((he_phy24 >> 7) & 0x1) | ((he_phy24 >> 9) & 0x1);
                        // STBC
                        record->he_txstbc_lt80 = (he_phy8 >> 10) & 0x1;
                        record->he_rxstbc_lt80 = (he_phy8 >> 11) & 0x1;
                        record->he_txstbc_gt80 = (he_phy56 >> 6) & 0x1;
                        record->he_rxstbc_gt80 = (he_phy56 >> 7) & 0x1;
                        // MU-MIMO
                        record->he_mumimo_ul_fullbw = (he_phy8 >> 14) & 0x1;
                        record->he_mumimo_ul_partbw = (he_phy8 >> 15) & 0x1;
                        record->he_mumimo_dl_partbw = (he_phy40 >> 14) & 0x1;
                        // 1024 QAM Support < 242-tone RU
                        record->he_tx1024qam = (he_phy72 >> 2) & 0x1;
                        record->he_rx1024qam = (he_phy72 >> 3) & 0x1;
                    } break;

                case WLAN_EID_EXT_HE_OPERATION: if (datalen >= 3) { // HE Operation (36)
                        // ER SU Disable
                        record->he_ersu_dis = getBit(data, datalen, 2, 0);
                    } break;

                case WLAN_EID_EXT_HE_6GHZ_CAPA: if (datalen >= 3) { // HE 6 GHz Band Capabilities (59)
                        record->he_wifi6e = 1;
                    } break;

                default: // Unknown extended tag
                    //LTLOG("parse.ies.ext", "Unknown extended tag %d, len %d", exttag, datalen);
                    break;
                }
            } break; // END case 255: Extended tag

        default: // Unknown tag
            //LTLOG("parse.ies.tag", "Unknown tag %d, len %d", tag, datalen);
            break;
        } // END switch (tag)
    }
    lt_free(ie);
}

static void recordApInfo(WiFiUnit *unit) {
    lt_memset(&ApInfo, 0, sizeof(ApInfo));

    // From LTWiFi_ApInfo
    lt_strncpyTerm(ApInfo.ssid, unit->ApInfo.ssid, sizeof(ApInfo.ssid));
    ApInfo.security = unit->ApInfo.security;
    pMacAddress->MacAddressToOui(&unit->ApInfo.bssid, ApInfo.oui, ':');
    ApInfo.hash = pMacAddress->MacAddressToHash(&unit->ApInfo.bssid);
    ApInfo.channel = unit->ApInfo.channel;
    ApInfo.bandwidth = unit->ApInfo.bandwidth;
    ApInfo.rssi = unit->ApInfo.rssi;
    ApInfo.snr = unit->ApInfo.snr;

    // From IE vector
    parse_ies(unit->ApInfo_ie, &ApInfo);
    pLogger->EncodeRecord(&ApInfo_Record);

    LTLOG_DEBUG("apinfo", "{\"ssid\":\"%s\", \"security\":%d, \"oui\":\"%s\", \"hash\":%lx, "
                "\"channel\":%d, \"bandwidth\":%d, \"rssi\":%d, \"snr\":%d, "
                "\"ht_streams\":%d, \"ht_bw\":%d, \"ht_ldpc\":%d, \"ht_txbf\":%d, "
                "\"ht_txstbc\":%d, \"ht_rxstbc\":%d, \"ht_en\":%d, "
                "\"vht_streams\":%d, \"vht_bw\":%d, \"vht_ldpc\":%d, \"vht_txbf\":%d, "
                "\"vht_txstbc\":%d, \"vht_rxstbc\":%d, \"vht_en\":%d, "
                "\"he_streams\":%d, \"he_bw\":%d, \"he_ldpc\":%d, \"he_txbf\":%d, "
                "\"he_txstbc_lt80\":%d, \"he_rxstbc_lt80\":%d, \"he_txstbc_gt80\":%d, \"he_rxstbc_gt80\":%d, "
                "\"he_en\":%d, \"he_mumimo_ul_fullbw\":%d, \"he_mumimo_ul_partbw\":%d, \"he_mumimo_dl_partbw\":%d, "
                "\"he_tx1024qam\":%d, \"he_rx1024qam\":%d, \"he_ersu_dis\":%d, "
                "\"he_wifi6e\":%d, \"he_wifi6e_chan\":%d, \"bss_trans\":%d, \"time_measure\":%d, "
                "\"mesh_id\":\"%s\", \"mesh_con_gate\":%d, \"mesh_peers\":%d, \"mesh_fw\":%d, "
                "\"manuf\":\"%s\", \"model\":\"%s\", \"modelno\":\"%s\", "
                "\"rf_bands\":%d, \"chipguess\":\"%s\", \"wpa3\":%d, \"utilpct\":%d}",
                ApInfo.ssid, ApInfo.security, ApInfo.oui, LT_Pu32(ApInfo.hash),
                ApInfo.channel, ApInfo.bandwidth, ApInfo.rssi, ApInfo.snr,
                ApInfo.ht_streams, ApInfo.ht_bw, ApInfo.ht_ldpc, ApInfo.ht_txbf,
                ApInfo.ht_txstbc, ApInfo.ht_rxstbc, ApInfo.ht_en,
                ApInfo.vht_streams, ApInfo.vht_bw, ApInfo.vht_ldpc, ApInfo.vht_txbf,
                ApInfo.vht_txstbc, ApInfo.vht_rxstbc, ApInfo.vht_en,
                ApInfo.he_streams, ApInfo.he_bw, ApInfo.he_ldpc, ApInfo.he_txbf,
                ApInfo.he_txstbc_lt80, ApInfo.he_rxstbc_lt80, ApInfo.he_txstbc_gt80, ApInfo.he_rxstbc_gt80,
                ApInfo.he_en, ApInfo.he_mumimo_ul_fullbw, ApInfo.he_mumimo_ul_partbw, ApInfo.he_mumimo_dl_partbw,
                ApInfo.he_tx1024qam, ApInfo.he_rx1024qam, ApInfo.he_ersu_dis,
                ApInfo.he_wifi6e, ApInfo.he_wifi6e_chan, ApInfo.bss_trans, ApInfo.time_measure,
                ApInfo.mesh_id, ApInfo.mesh_con_gate, ApInfo.mesh_peers, ApInfo.mesh_fw,
                ApInfo.manuf, ApInfo.model, ApInfo.modelno,
                ApInfo.rf_bands, ApInfo.chipguess, ApInfo.wpa3, ApInfo.utilpct);
}

/*******************************************************
 * Event monitor and handler for wpa_supplicant events *
 *******************************************************/
static void handleWiFiEvent(WiFiUnit *unit, char level, char *event,
                            char *param) {
    LTLOG_LEVEL(kLogWPA, "event.recv", "<%d><%s> <%s>\n", level, event, param);
    // Convert MACs in param string to OUI+hash
    char param_oui[100];
    char *pmac;
    lt_strncpyTerm(param_oui, param, sizeof(param_oui));
    for (pmac = param_oui ; (pmac = lt_strchr(pmac, ':')) != NULL ; pmac++) {
        int n;
        bool isMAC = true;
        if ((pmac - param_oui) < 2 || !lt_isxdigit(pmac[-1]) || !lt_isxdigit(pmac[-2]) ) continue;
        for (n=0 ; n<15 ; n += 3) {
            if (pmac[n] != ':' || !lt_isxdigit(pmac[n + 1]) || !lt_isxdigit(pmac[n + 2])) {
                isMAC = false;
                break;
            }
        }
        if (isMAC) {
            LTMacAddress mac = {};
            pmac -= 2; // point to start of MAC address
            pMacAddress->StringToMacAddress(pmac, &mac);
            pMacAddress->MacAddressToOui(&mac, pmac, ':');
            pmac += 17; // point past MAC address
            lt_snprintf(pmac, sizeof(param_oui) - (pmac - param_oui), " %lx%s",
                     LT_Pu32(pMacAddress->MacAddressToHash(&mac)),
                     &param[(pmac - param_oui)]);
            break;
        }
    }

    // Scan status events - used by DriverWiFiSupp_ScanCheck()
    // This is where a direct call to unit->scan_callback() could
    // be implemented instead of waiting for a poll in ScanCheck
    if (unit->scan_status == kDriverWiFiSupp_ScanStatus_Starting ||
        unit->scan_status == kDriverWiFiSupp_ScanStatus_Scanning) {

        if (lt_strcmp(event, "CTRL-EVENT-SCAN-STARTED") == 0) {
            unit->scan_status = kDriverWiFiSupp_ScanStatus_Scanning;
        } else if (lt_strcmp(event, "CTRL-EVENT-SCAN-RESULTS") == 0) {
            unit->scan_status = kDriverWiFiSupp_ScanStatus_Success;
        } else if (lt_strcmp(event, "CTRL-EVENT-SCAN-FAILED") == 0) {
            unit->scan_status = kDriverWiFiSupp_ScanStatus_Failed;
        }
    }

    // Update stats for the next GetMetrics() if wpa_supplicant is handling an auto-rejoin
    if ((unit->ApJoinSpec.options & kLTWiFi_JoinOption_Retry) &&
        unit->join_status == kLTWiFi_JoinStatus_Success) {
        if (lt_strcmp(event, "CTRL-EVENT-DISCONNECTED") == 0) {
            LTAtomic_FetchAdd(&unit->joinDisc, 1);
        }
        if (lt_strcmp(event, "CTRL-EVENT-NETWORK-NOT-FOUND") == 0 ||
            lt_strcmp(event, "CTRL-EVENT-ASSOC-REJECT") == 0) {
            LTAtomic_FetchAdd(&unit->joinFail, 1);
        }
        if (lt_strcmp(event, "CTRL-EVENT-CONNECTED") == 0) {
            LTAtomic_FetchAdd(&unit->joinSuccess, 1);
        }
    }

    // Join status events - used by DriverWiFiSupp_JoinCheck()
    // This is where a direct call to unit->join_callback() could
    // be implemented instead of waiting for a poll in JoinCheck
    if (unit->join_status >= kLTWiFi_JoinStatus_Starting &&
        unit->join_status < kLTWiFi_JoinStatus_Failed) {
        if (unit->join_status == kLTWiFi_JoinStatus_Starting &&
            lt_strcmp(event, "CTRL-EVENT-SCAN-STARTED") == 0) {
            unit->join_status = kLTWiFi_JoinStatus_Scanning;
        } else if (unit->join_status == kLTWiFi_JoinStatus_Scanning &&
                   lt_strcmp(event, "CTRL-EVENT-SCAN-RESULTS") == 0) {
            unit->join_status = kLTWiFi_JoinStatus_Associating;
        } else if (lt_strcmp(event, "Associated") == 0) {
            unit->join_status = kLTWiFi_JoinStatus_Associated;
        } else if (lt_strcmp(event, "CTRL-EVENT-ASSOC-REJECT") == 0) {
            unit->connected = false;
            unit->disconnect_reason = kDriverWiFiSupp_DisconnectReason_Reject;
            unit->join_status = kLTWiFi_JoinStatus_Failed;
        } else if (lt_strcmp(event, "CTRL-EVENT-CONNECTED") == 0) {
            unit->connected = true;
            unit->join_status = kLTWiFi_JoinStatus_Success;
            unit->ApInfo = (LTWiFi_ApInfo){0}; // Clear cache - filled on next GetApInfo()
            unit->ApInfo_ie[0] = '\0';
        }
    } else {
        // Auto rejoin success
        if (lt_strcmp(event, "CTRL-EVENT-CONNECTED") == 0) {
            unit->connected = true;
            unit->ApInfo = (LTWiFi_ApInfo){0}; // Clear cache - filled on next GetApInfo()
            unit->ApInfo_ie[0] = '\0';
        }
    }

    // Server logging for join/disconnect/rejoin
    if (lt_strcmp(event, "Trying") == 0) {
        LTLOG_SERVER("join.try", "<%d><%s> <%s>", level, event, param_oui);
    } else if (lt_strcmp(event, "CTRL-EVENT-ASSOC-REJECT") == 0) {
        LTLOG_SERVER("join.fail", "<%d><%s> <%s>", level, event, param_oui);
    } else if (lt_strcmp(event, "CTRL-EVENT-CONNECTED") == 0) {
        LTLOG_SERVER("join.done", "<%d><%s> <%s>", level, event, param_oui);
        char chip[16];
        get_chip(chip, sizeof(chip));
        LTLOG_SERVER("chip", "WiFi chip = <%s>", chip);
    }

    // We don't have a callback for disconnect events.
    // It will be reported in the next call to DriverWiFiSupp_GetApInfo()
    if (lt_strcmp(event, "CTRL-EVENT-DISCONNECTED") == 0) {
        char *strp = NULL;
        unit->disconnect_reason = 0;
        unit->connected = false;
        if ((strp = lt_strstr(param, "reason=")) != NULL) {
            strp += lt_strlen("reason=");
            unit->disconnect_reason = lt_strtou32(strp, NULL, 10);
        }

        // Log IE vector from the cached probe response
        if (unit->ApInfo_ie[0] != '\0') {
            char oui[20];
            pMacAddress->MacAddressToOui(&unit->ApInfo.bssid, oui, ':');
            LTLogRecordApInfo ApInfoIe = {};
            parse_ies(unit->ApInfo_ie, &ApInfoIe);
            LTLOG_SERVER("disc.ap.ie", "%lu, %d, %u, %s, %lx, \"%s\", %s, %s, %s, %s",
                         LT_Pu32(unit->disconnect_reason), unit->ApInfo.rssi, unit->ApInfo.channel,
                         oui, LT_Pu32(pMacAddress->MacAddressToHash(&unit->ApInfo.bssid)),
                         unit->ApInfo.ssid,
                         ApInfoIe.manuf, ApInfoIe.model, ApInfoIe.modelno,
                         unit->ApInfo_ie);
            unit->ApInfo_ie[0] = '\0';
        }
    } else if (lt_strcmp(event, "CTRL-EVENT-NETWORK-NOT-FOUND") == 0) {
        unit->disconnect_reason = kDriverWiFiSupp_DisconnectReason_NoApFound;
    }

    if (lt_strcmp(event, "CTRL-EVENT-DISCONNECTED") == 0 ||
        lt_strcmp(event, "CTRL-EVENT-NETWORK-NOT-FOUND") == 0 ||
        lt_strcmp(event, "CTRL-EVENT-ASSOC-REJECT") == 0) {
        LTLOG_SERVER("cb.dcrsn", "%d: <%d><%s> <%s>\n", unit->disconnect_reason,
                     level, event, param_oui);
    }

    // Log Errors from wpa_supplicant
    if (level >= 4) { // MSG_INFO=3, MSG_WARNING=4, MSG_ERROR=5
        LTLOG_SERVER("event.err", "<%d>%s %s\n", level, event, param);
    }
}

// Retrieve any unsolicited events from wpa_supplicant.
// Used for commands that take a long time to complete like
// Scan or Join.
// This uses a separate socket connection than the control interface
static void getWiFiEvents(void *arg) {
    WiFiUnit *unit = (WiFiUnit *)arg;
    const LT_SIZE REPLY_LEN = 2048;
    char *reply;
    char *event;
    char *param;
    char level;
    char EMPTYSTRING[] = "";
    LT_SIZE reply_len;
    int err;

    if ((reply = lt_malloc(REPLY_LEN)) == NULL) return;
    lt_memset(reply, 0, REPLY_LEN);

    for (;;) {
        reply_len = REPLY_LEN;
        unit->mutex->API->Lock(unit->mutex);
        if (unit->event == NULL) {
            unit->mutex->API->Unlock(unit->mutex);
            break;
        }
        err = wpa_ctrl_recv(unit->event, reply, &reply_len);
        unit->mutex->API->Unlock(unit->mutex);
        if (!err && reply_len > 3) {
            reply[reply_len] = '\0';
            event = &reply[3];
            param = lt_strchr(event, ' ');
            level = reply[1] - '0';
            if (param != NULL) {
                param[0] = '\0';
                param++;
            } else {
                param = EMPTYSTRING;
            }
            handleWiFiEvent(unit, level, event, param);
        } else {
            if (!err && reply_len == 0) {
                LTLOG_DEBUG("event.monitor.eof",
                            "EOF received from wpa_supplicant\n");
            }
            break;
        }
    }
    lt_free(reply);
}

// Thread for monitoring wpa_supplicant unsolited events
static bool WiFiEventMonitor_InitThread(void) {
    void *unit = iThread->GetThreadSpecificClientData(
        iThread->GetCurrentThread(), "unit");
    iThread->SetTimer(iThread->GetCurrentThread(), LTTime_Milliseconds(300),
                      getWiFiEvents, NULL, unit);
    return true;
}

// Thread for monitoring wpa_supplicant unsolited events
static void WiFiEventMonitor_ExitThread(void) {
    void *unit = iThread->GetThreadSpecificClientData(
        iThread->GetCurrentThread(), "unit");
    iThread->KillTimer(iThread->GetCurrentThread(), getWiFiEvents, unit);
}

/*******************************************************
 * Socket connection to/from wpa_supplicant            *
 * Opened/checked/re-opened by request() function      *
 *******************************************************/
void wpa_quit(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);

    unit->mutex->API->Lock(unit->mutex);
    if (unit->event) {
        if (unit->attached) {
            wpa_ctrl_detach(unit->event);
            unit->attached = false;
        }
        wpa_ctrl_close(unit->event);
        unit->event = NULL;
    }
    if (unit->ctrl) {
        wpa_ctrl_close(unit->ctrl);
        unit->ctrl = NULL;
    }
    unit->mutex->API->Unlock(unit->mutex);
}

bool wpa_init(LTDeviceUnit h_unit) {
    GET_UNIT_RETURN(unit, h_unit, false);
    int error;

    // If port is already open, close all and reopen:
    if (unit->ctrl || unit->event) wpa_quit(h_unit);

    unit->ctrl = wpa_ctrl_open(ctrl_iface_path);
    if (!unit->ctrl) {
        static LTTime lastlogtime = {0}; // Rate limit error logs
        LTTime now;
        wpa_quit(h_unit);

        // Log this at most once pr. second
        now = pCore->GetKernelTime();
        if (LTTime_IsGreaterThan(LTTime_Subtract(now, lastlogtime),
                                 LTTime_Seconds(1))) {
            LTLOG("open.fail", "wpa_ctrl_open failed: %s\n", ctrl_iface_path);
            lastlogtime = now;
        }
        return false;
    }
    unit->event = wpa_ctrl_open(ctrl_iface_path);
    if (!unit->event) {
        wpa_quit(h_unit);
        LTLOG("evopen.fail", "event port failed: %s\n", ctrl_iface_path);
        return false;
    }
    error = wpa_ctrl_attach(unit->event);
    if (error) {
        unit->attached = false;
        wpa_quit(h_unit);
        LTLOG("attach.fail", "wpa_ctrl_attach failed: %d\n", error);
        return false;
    }
    unit->attached = true;
    return true;
}

/*******************************************************
 * Command interface to wpa_supplicant                 *
 * request()/ask()/command()                           *
 *******************************************************/

// Sends a command to the control interface and returns the response
static const char *request(LTDeviceUnit h_unit, const char *cmd) {
    GET_UNIT_RETURN(unit, h_unit, unit->ctrl_reply);
    const LT_SIZE REPLY_LEN = 8192;
    int err;
    char *reply;
    LT_SIZE reply_len = REPLY_LEN;

    ltstring_empty(unit->ctrl_reply);

    if (unit->ctrl == NULL) {
        if (wpa_init(h_unit) == false) {
            return unit->ctrl_reply;
        }
    }

    if ((reply = lt_malloc(REPLY_LEN)) == NULL) return unit->ctrl_reply;
    lt_memset(reply, 0, REPLY_LEN);

    err = wpa_ctrl_request(unit->ctrl, cmd, lt_strlen(cmd), reply, &reply_len, NULL);
    if (err) {
        LTLOG("request.err", "wpa_ctrl_request(%p, %s, ...) returned %d\n",
              unit->ctrl, cmd, err);
        lt_free(reply);
        wpa_init(h_unit);
        return unit->ctrl_reply;
    }

    ltstring_set(&unit->ctrl_reply, reply);
    ltstring_stripwhitespace(&unit->ctrl_reply, true, true);

    /* kLogWPA: Log all wpa_supplicant command/response */
    if (lt_strlen(unit->ctrl_reply) < 200 ||
        (kLogVerbose & LTAtomic_Load(&unit->DebugMode))) {
        LTLOG_LEVEL(kLogWPA, "request", "%s -> %s", cmd, unit->ctrl_reply);
    } else {
        LTLOG_LEVEL(kLogWPA, "request.trunc", "%s -> %u bytes",
                    cmd, lt_strlen(unit->ctrl_reply));
    }
    lt_free(reply);
    return unit->ctrl_reply;
}

/* Sends a command to the control interface and expects a particular response */
static bool ask(LTDeviceUnit h_unit, const char *command, const char *response) {
    GET_UNIT_RETURN(unit, h_unit, false);
    bool result = false;
    const char *reply = request(h_unit, command);

    if (lt_strlen(reply) > 0) {
        if (lt_strcmp(reply, response) == 0) {
            result = true;
        }
    }
    return result;
}

// Sends a command to the control interface and expects OK response back
static bool command(LTDeviceUnit h_unit, const char *cmd, ...) {
    GET_UNIT_RETURN(unit, h_unit, false);
    bool result = false;

    LTString command = NULL;
    lt_va_list list;
    lt_va_start(list, cmd);
    ltstring_vformat(&command, cmd, list);
    lt_va_end(list);

    result = ask(h_unit, command, "OK");
    ltstring_destroy(command);

    return result;
}

/*******************************************************
 * LinuxDriverWiFi functionality implementation        *
 *******************************************************/
static bool LinuxDriverWiFiImpl_LibInit(void) {
    // Zero all unit variables
    lt_memset(&s_unit, 0, sizeof(s_unit));

    // interfaces
    pCore = LT_GetCore();
    pMacAddress = lt_openlibrary(LTUtilityMacAddress);
    pByte = lt_openlibrary(LTUtilityByteOps);
    pLogger = lt_openlibrary(LTSystemLogger);
    if (!pCore || !pMacAddress || !pByte) {
        LinuxDriverWiFiImpl_LibFini();
        return false;
    }

    // handles
    iThread = lt_getlibraryinterface(ILTThread, pCore);
    if (!iThread) {
        LinuxDriverWiFiImpl_LibFini();
        return false;
    }

    // Initialize unit variables
    LTAtomic_Store(&s_unit.DebugMode, 0);
    s_unit.ctrl_reply = ltstring_create("");
    s_unit.mutex = lt_createobject(LTMutex);
    s_unit.monitor_LTThread = pCore->CreateThread("WiFi events");
    if (!s_unit.ctrl_reply || !s_unit.mutex || !s_unit.monitor_LTThread) {
        LinuxDriverWiFiImpl_LibFini();
        return false;
    }

    // Start WiFi event monitor
    iThread->SetStackSize(s_unit.monitor_LTThread, 16 * 1024);
    iThread->SetThreadSpecificClientData(s_unit.monitor_LTThread, "unit", NULL,
                                         &s_unit);
    iThread->Start(s_unit.monitor_LTThread, WiFiEventMonitor_InitThread,
                   WiFiEventMonitor_ExitThread);
    return true;
}

static void LinuxDriverWiFiImpl_LibFini(void) {
    // Stop threads and close any open libraries
    if (iThread && s_unit.monitor_LTThread) {
        iThread->Destroy(s_unit.monitor_LTThread);
    }
    s_unit.monitor_LTThread = 0;

    if (s_unit.mutex) {
        lt_destroyobject(s_unit.mutex);
        s_unit.mutex = NULL;
    }
    s_unit.mutex = NULL;

    ltstring_destroy(s_unit.ctrl_reply);
    s_unit.ctrl_reply = NULL;

    iThread = NULL;

    if (pLogger) pLogger->RemoveRecord(&ApInfo_Record);
    lt_closelibrary(pLogger);
    lt_closelibrary(pByte);
    lt_closelibrary(pMacAddress);
    pByte = NULL;
    pMacAddress = NULL;
}

static u32 LinuxDriverWiFiImpl_GetNumDeviceUnits(void) {
    // This driver library only knows how to control one WiFi interface
    return 1;
}

static LTDriverWiFi s_LTDriverWiFi;
/* This is a forward declaration of the LTDriverWiFi interface instance that
   gets defined at the end of this file by the macro
   define_LTLIBRARY_INTERFACE(LTDriverWiFi).
   The variable name has to be s_LTDriverWiFi because that is what the macro
   defines. */

static LTDeviceUnit
LinuxDriverWiFiImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    /* we only manage one physical WiFi hardware block (unit) so
       nDeviceUnitNumber must be 0 */
    if (nDeviceUnitNumber > 0) return (LTDeviceUnit)0;

    /* units are not the same as unit handles.  Multiple handles can exist
       for the same unit.
       In practice, LTDeviceWiFi is our only client and probably will only
       call us once per unit number, so likely a 1:1 mapping, but not
       necessarily. Since the client who calls create is the owner of the
       resource, and up to them to destroy the handle, we won't manage lists
       of handles or anything like that here. */

    LTDeviceUnit hUnit = LT_GetCore()->CreateHandle(
        (LTInterface *)&s_LTDriverWiFi, sizeof(WiFiUnit *));
    //See note above the GET_UNIT define
    return hUnit;
}

/* no explicit DestroyDeviceUnitHandle function to match
   CreateDeviceUnitHandle because handle interfaces have a built in
   Destroy() and LT_GetCore()->DestroyHandle always works. */

/*******************************************************************************
 * LTDriverWiFi API Functions
 ******************************************************************************/
static bool DriverWiFiSupp_GetDriverInfo(LTDeviceUnit h_unit,
                                         LTWiFi_DriverInfo *info) {
    GET_UNIT_RETURN(unit, h_unit, false);
    char buf[50];

    lt_strncpyTerm(info->vendor, "SUP", sizeof(info->vendor));
    lt_strncpyTerm(info->product, "wpa_supplicant", sizeof(info->product));
    get_chip(info->chip, sizeof(info->chip));
    const char *reply = request(h_unit, "GET version");
    lt_strncpyTerm(info->version, reply, sizeof(info->version));
    // I can't find the compile date via wpa_supplicant interface
    lt_strncpyTerm(info->updated, "Unknown", sizeof(info->updated));
    reply = request(h_unit, "STATUS");
    get_field_str(reply, "address", buf, sizeof(buf));
    pMacAddress->StringToMacAddress(buf, &info->mac_address);

    return true;
}

static bool DriverWiFiSupp_SetDriverState(LTDeviceUnit h_unit,
                                          LTWiFi_DriverState state) {
    GET_UNIT_RETURN(unit, h_unit, false);
    unit->DriverState = state;
    switch (state) {
    case kLTWiFi_DriverState_Up:
        pCore->ConsolePrint("--- LTDriverWiFi driver up ---\n");
        break;
    case kLTWiFi_DriverState_Down:
        command(h_unit, "DISCONNECT");
        unit->connected = false;
        pCore->ConsolePrint("--- LTDriverWiFi driver down ---\n");
        break;
    case kLTWiFi_DriverState_ResetChip:
    case kLTWiFi_DriverState_ResetDriver:
        unit->connected = false;
        pCore->ConsolePrint("--- LTDriverWiFi reset ---\n");
        command(h_unit, "RECONFIGURE");
        break;
    default:
        return false;
    }
    return true;
}

static LTWiFi_DriverState DriverWiFiSupp_GetDriverState(LTDeviceUnit h_unit) {
    GET_UNIT_RETURN(unit, h_unit, kLTWiFi_DriverState_Unknown);
    char wpa_state[50];

    const char *reply = request(h_unit, "STATUS");
    get_field_str(reply, "wpa_state", wpa_state, sizeof(wpa_state));

    // Default is kLTWiFi_DriverState_Up, but keep old unit->DriverState
    // "Connected" state if wpa_state is inconclusive
    if (unit->DriverState != kLTWiFi_DriverState_Connected)
        unit->DriverState = kLTWiFi_DriverState_Up;

    if (ltstring_isempty(reply))
        unit->DriverState = kLTWiFi_DriverState_Down;
    if (lt_strcmp("COMPLETED", wpa_state) == 0)
        unit->DriverState = kLTWiFi_DriverState_Connected;
    if (lt_strcmp("DISCONNECTED", wpa_state) == 0)
        unit->DriverState = kLTWiFi_DriverState_Up;

    return unit->DriverState;
}

static bool DriverWiFiSupp_SetOption(LTDeviceUnit h_unit, char const *option,
                                     void *value) {
    GET_UNIT_RETURN(unit, h_unit, false);
    char mac[50];

    if (lt_strcmp(option, "mac_address") == 0) {
        pMacAddress->MacAddressToString((LTMacAddress *)value, mac, ':');
        LTLOG("set.err", "Setting mac address is not supported. (%s)", mac);
        return false;
    }

    if (lt_strcmp(option, "debug_mode") == 0) {
        LTAtomic_Store(&unit->DebugMode, *((u32 *)value));
        return true;
    }

    if (lt_strcmp(option, "channel") == 0) {
        u32 channel = *((u32 *)value);
        return command(h_unit, "CHAN_SWITCH 5 %u ht", chanToFreq(channel));
    }

    return false;
}

static bool DriverWiFiSupp_GetOption(LTDeviceUnit h_unit, char const *option,
                                     void *value) {
    GET_UNIT_RETURN(unit, h_unit, false);

    if (lt_strcmp(option, "mac_address") == 0) {
        char buf[50];
        const char *reply = request(h_unit, "STATUS");
        get_field_str(reply, "address", buf, sizeof(buf));
        pMacAddress->StringToMacAddress(buf, (LTMacAddress *)value);
        return true;
    }

    if (lt_strcmp(option, "channel") == 0) {
        const char *reply = request(h_unit, "STATUS");
        *((u32 *)value) = freqToChan(get_field_int(reply, "freq", 0));
        return true;
    }

    // Return a bit-array with available channels
    // Used by LTShellWiFi: wifi channel list
    if (lt_strcmp(option, "channels_1-14") == 0 ||
        lt_strcmp(option, "channels_36-144") == 0 ||
        lt_strcmp(option, "channels_149-183") == 0) {

        const char *chans = request(h_unit, "GET_CAPABILITY freq");
        u32 from = 1, to = 14, step = 1, channels = 0;
        if (lt_strcmp(option, "channels_36-144") == 0) {
            from = 36; to = 144; step = 4;
        }
        if (lt_strcmp(option, "channels_149-183") == 0) {
            from = 149; to = 183; step = 4;
        }

        while ((chans = lt_strstr(chans, "\n ")) != NULL) {
            u32 ch = lt_strtou32(chans, NULL, 10);
            if (ch >= from && ch <= to) {
                channels |= 1 << ((ch - from) / step);
            }
            chans++;
        }
        *((u32 *)value) = channels;
        return true;
    }

    if (lt_strcmp(option, "DisconnectReason") == 0) {
        *(int*)value = (int)DriverWiFi_GetDiscReasonCode(h_unit);
        return true;
    }
    return false;
}

static void DriverWiFiSupp_GetMetrics(LTDeviceUnit h_unit, LTWiFi_Metrics *metrics, LT_SIZE sizeOfMetrics) {
    GET_UNIT(unit, h_unit);

    // "PKTCNT_POLL" in wpa_supplicant provides TXGOOD, TXBAD and RXGOOD
    // We still can't get tx-retries, rx-retries, rx-drops and cca though.
    const char *reply = request(h_unit, "PKTCNT_POLL");
    lt_memset(metrics, 0, sizeOfMetrics);
    metrics->tx_frame_count = get_field_int(reply, "TXGOOD", 0);
    metrics->tx_frame_drops = get_field_int(reply, "TXBAD", 0);
    metrics->rx_frame_count = get_field_int(reply, "RXGOOD", 0);
    metrics->joinSuccess = LTAtomic_Exchange(&unit->joinSuccess, 0);
    metrics->joinFail = LTAtomic_Exchange(&unit->joinFail, 0);
    metrics->joinDisc = LTAtomic_Exchange(&unit->joinDisc, 0);
}
/** Scan **********************************************************************/

static void DriverWiFiSupp_ScanStart(LTDeviceUnit h_unit, LTWiFi_ScanSpec *spec,
                                     LTWiFi_ScanResults_CB callback) {
    GET_UNIT(unit, h_unit);
    unit->ScanSpec = *spec;
    unit->scan_callback = callback;

    if (!command(h_unit, "BSS_FLUSH 5")) {
        unit->scan_callback(h_unit, NULL);
        unit->scan_status = kDriverWiFiSupp_ScanStatus_Failed;
        return;
    }

    LTString scan_cmd = ltstring_create("SCAN ");
    if (spec->ssid != NULL && lt_strlen(spec->ssid) > 0) {
        LT_SIZE hex_len;
        char *hex;
        hex_len = 2 * lt_strlen(spec->ssid) + 1;
        if ((hex = lt_malloc(hex_len)) != NULL) {
            pByte->HexEncode((u8 *)(spec->ssid), lt_strlen(spec->ssid), hex,
                             hex_len, false);
            ltstring_append(&scan_cmd, " ssid ");
            ltstring_append(&scan_cmd, hex);
            lt_free(hex);
        } else {
            unit->scan_callback(h_unit, NULL);
            unit->scan_status = kDriverWiFiSupp_ScanStatus_Failed;
            return;
        }
    }
    if (spec->channel != NULL && spec->channel[0] != 0) {
        LT_SIZE i;
        ltstring_appendformat(&scan_cmd, " freq=%u", chanToFreq(spec->channel[0]));
        for (i = 1; spec->channel[i] != 0; i++) {
            ltstring_appendformat(&scan_cmd, ",%u", chanToFreq(spec->channel[i]));
        }
    }

    if (spec->options & kLTWiFi_ScanOption_Passive) {
        ltstring_appendformat(&scan_cmd, " passive=1");
    }

    unit->scan_status = kDriverWiFiSupp_ScanStatus_Starting;
    if (!command(h_unit, scan_cmd)) {
        unit->scan_callback(h_unit, NULL);
        unit->scan_status = kDriverWiFiSupp_ScanStatus_Failed;
    }
    ltstring_destroy(scan_cmd);
}

/*******************************************************************************
 * Helper functions for DriverWiFiSupp_ScanCheck: apSortSSID()/apSortRSSI()
 *******************************************************************************/
static int apSortSSID(const void *pElement1, const void *pElement2, void *pClientData) {
    LT_UNUSED(pClientData);
    LTWiFi_ApInfo *ap1 = (LTWiFi_ApInfo *)pElement1;
    LTWiFi_ApInfo *ap2 = (LTWiFi_ApInfo *)pElement2;
    int result;

    // Sort by: (SSID, security, RSSI, channel)
    if ((result = lt_strcmp(ap2->ssid, ap1->ssid)) == 0)
        if ((result = ap2->security - ap1->security) == 0)
            if ((result = ap2->rssi - ap1->rssi) == 0)
                result = ap2->channel - ap1->channel;
    return result;
}

static int apSortRSSI(const void *pElement1, const void *pElement2, void *pClientData) {
    LT_UNUSED(pClientData);
    LTWiFi_ApInfo *ap1 = (LTWiFi_ApInfo *)pElement1;
    LTWiFi_ApInfo *ap2 = (LTWiFi_ApInfo *)pElement2;
    int result;

    // Sort by: (RSSI, channel)
    if ((result = ap2->rssi - ap1->rssi) == 0)
        result = ap2->channel - ap1->channel;
    return result;
}

static void DriverWiFiSupp_ScanCheck(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);
    u32 i;
    LTWiFi_ScanSpec *spec = &unit->ScanSpec;
    LTArray *apLTArray;

    if (unit->scan_status == kDriverWiFiSupp_ScanStatus_Starting ||
        unit->scan_status == kDriverWiFiSupp_ScanStatus_Scanning) {
        return; // Still scanning
    }

    if (unit->scan_status == kDriverWiFiSupp_ScanStatus_Failed) {
        LTLOG("scan.fail", "WiFi scan failed\n");
        unit->scan_callback(h_unit, NULL);
    }

    // Get the AP list and return it:
    apLTArray = LTArray_CreateStructArray(sizeof(LTWiFi_ApInfo));
    for (i = 0; i < 500; i++) {
        LTWiFi_ApInfo ap = {0};
        char cmd[20];
        char buf[50];

        lt_snprintf(cmd, sizeof(cmd), "BSS %u", i);
        const char *bss = request(h_unit, cmd);
        if (lt_strlen(bss) == 0) break;

        ap.security = get_field_security(bss);

        get_field_str(bss, "ssid", ap.ssid, sizeof(ap.ssid));
        convert_unicode(ap.ssid);

        ap.rssi = get_field_int(bss, "level", 0);

        ap.channel = freqToChan(get_field_int(bss, "freq", 0));

        get_field_str(bss, "bssid", buf, sizeof(buf));
        pMacAddress->StringToMacAddress(buf, &ap.bssid);

        // Don't add mesh nodes to list of "real" access points
        get_field_str(bss, "flags", buf, sizeof(buf));
        if (lt_strstr(buf, "[MESH]") != NULL) continue;

        // Directed scans only return the specified SSID
        if (spec->ssid && lt_strcmp(spec->ssid, ap.ssid)) continue;

        // kLTWiFi_ScanOption_Secure: only find secure APs (not open)
        if ((spec->options & kLTWiFi_ScanOption_Secure) &&
            (ap.security <= kLTWiFi_ApSecurity_Open)) continue;

        // kLTWiFi_ScanOption_Hidden: include hidden APs
        if (!(spec->options & kLTWiFi_ScanOption_Hidden) &&
            (lt_strlen(ap.ssid) == 0)) continue;

        // kLTWiFi_ScanOption_Direct: include "DIRECT-" SSIDs
        if (!(spec->options & kLTWiFi_ScanOption_Direct) &&
            (lt_strncmp("DIRECT-", ap.ssid, 7) == 0)) continue;

        apLTArray->API->Append(apLTArray, &ap);
    }

    // kLTWiFi_ScanOption_Dups: include duplicate SSIDs
    if ((spec->options & kLTWiFi_ScanOption_Dups) == 0) {
        LTWiFi_ApInfo ap = {0};
        LTWiFi_ApInfo lastap = {0};
        u32 nIndex = 0;

        // Sort the array by (SSID, security, RSSI, channel)
        apLTArray->API->Sort(apLTArray, apSortSSID, NULL);

        // Remove duplicate (SSID, security), keep the highest RSSI
        while (nIndex < apLTArray->API->GetCount(apLTArray)) {
            apLTArray->API->Get(apLTArray, nIndex, &ap);
            if (lt_strcmp(lastap.ssid, ap.ssid) == 0 &&
                lastap.security == ap.security) {
                apLTArray->API->Remove(apLTArray, nIndex);
            } else {
                lastap = ap;
                nIndex++;
            }
        }
    }

    // Sort the array by (RSSI, channel)
    apLTArray->API->Sort(apLTArray, apSortRSSI, NULL);
    for (i = 0; i < apLTArray->API->GetCount(apLTArray); i++) {
        unit->scan_callback(h_unit, apLTArray->API->Get(apLTArray, i, NULL));
    }

    unit->scan_callback(h_unit, NULL);
    lt_destroyobject(apLTArray);
    return;
}

static void DriverWiFiSupp_ScanAbort(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);
    command(h_unit, "ABORT_SCAN");
    unit->scan_status = kDriverWiFiSupp_ScanStatus_Aborted;
}

/** Join **********************************************************************/

static bool DriverWiFiSupp_IsSaeSupported(LTDeviceUnit h_unit) {
    const char *driver_flags = request(h_unit, "DRIVER_FLAGS");
    if (lt_strlen(driver_flags) == 0) return false;
    return (lt_strstr(driver_flags, "\nSAE\n") != NULL);
}

static void DriverWiFiSupp_JoinStart(LTDeviceUnit h_unit,
                                     LTWiFi_ApInfo *ap_spec,
                                     LTWiFi_JoinStatus_CB callback) {
    GET_UNIT(unit, h_unit);
    u8 n;
    s32 net_num;
    char *fmt;

    unit->ApJoinSpec = *ap_spec;
    unit->connected = false;
    unit->join_callback = callback;
    unit->join_status = kLTWiFi_JoinStatus_Starting;

    if (!command(h_unit, "BSS_FLUSH 5")) {
        unit->join_callback(h_unit, kLTWiFi_JoinStatus_Failed);
        unit->join_status = kLTWiFi_JoinStatus_Failed;
        return;
    }

    // Only use a single network block, remove all others:
    for (n = 0; n < 8; n++) {
        command(h_unit, "REMOVE_NETWORK %u", n); // ignore result
    }

    const char *reply = request(h_unit, "ADD_NETWORK");
    if (!lt_isdigit(reply[0])) {
        LTLOG_DEBUG("join.net_num", "failed to get net num = <%s>\n", reply);
        unit->join_callback(h_unit, kLTWiFi_JoinStatus_Failed);
        return;
    } else {
        net_num = lt_strtos32(reply, NULL, 10);
    }

    // Set BSSID if specified:
    if (!pMacAddress->IsZero(&ap_spec->bssid) &&
        !pMacAddress->IsBroadcast(&ap_spec->bssid)) {
        char mac_str[20];
        pMacAddress->MacAddressToString(&ap_spec->bssid, mac_str, ':');
        command(h_unit, "SET_NETWORK %d bssid %s", net_num, mac_str);
    }

    // Set the SSID:
    if (!command(h_unit, "SET_NETWORK %d ssid \"%s\"", net_num, ap_spec->ssid))
        goto fail_join;
    // Set the password:
    if (ap_spec->pass == NULL || lt_strlen(ap_spec->pass) == 0 ||
        ap_spec->security == kLTWiFi_ApSecurity_Wep) {
        if (!command(h_unit, "SET_NETWORK %d key_mgmt NONE", net_num))
            goto fail_join;
    } else {
        // If not set this defaults to "WPA-PSK WPA-EAP".
        // So we preserve that and add WPA-PSK-SHA256
        // and SAE for WPA3 if the driver supports it.
        const char *key_mgmt = DriverWiFiSupp_IsSaeSupported(h_unit) ?
                               "WPA-PSK WPA-EAP WPA-PSK-SHA256 SAE" :
                               "WPA-PSK WPA-EAP WPA-PSK-SHA256";
        if (!command(h_unit, "SET_NETWORK %d key_mgmt %s", net_num, key_mgmt))
            goto fail_join;
        // When connecting to a WPA3 only AP, we would like to
        // set MFPR bit to 1 in association request.
        if ((ap_spec->security == kLTWiFi_ApSecurity_Wpa3) ||
            (ap_spec->security == kLTWiFi_ApSecurity_Wpa3Eap)) {
            if (!command(h_unit, "SET_NETWORK %d ieee80211w 2", net_num))
                goto fail_join;
        }
        if (lt_strlen(ap_spec->pass) == 64) {
            // Raw hex PSK, no quotes
            if (!command(h_unit, "SET_NETWORK %d psk %s",
                         net_num, ap_spec->pass))
                goto fail_join;
        } else {
            if (!command(h_unit, "SET_NETWORK %d psk \"%s\"",
                         net_num, ap_spec->pass))
                goto fail_join;
        }
    }
    // WEP is special:
    if (ap_spec->security == kLTWiFi_ApSecurity_Wep && ap_spec->pass != NULL) {
        LT_SIZE len = lt_strlen(ap_spec->pass);
        if (len == 10 || len == 26) {
            fmt = "SET_NETWORK %d wep_key%u %s";
        } else {
            fmt = "SET_NETWORK %d wep_key%u \"%s\"";
        }
        for (unsigned n = 0; n < 4; n++) {
            if (!command(h_unit, fmt, net_num, n, ap_spec->pass)) goto fail_join;
        }
    }
    // Add special join parameters:
    if (ap_spec->channel != 0) {
        command(h_unit, "SET_NETWORK %d scan_freq %u", net_num,
                chanToFreq(ap_spec->channel));
    } else {
        command(h_unit, "SET_NETWORK %d scan_freq none", net_num);
    }
    command(h_unit, "SET_NETWORK %d scan_ssid 1", net_num); // ignore result
    command(h_unit, "SET_NETWORK %d disabled 0", net_num); // ignore result

    // If the SSID is not found in a scan, default was to sleep 5 seconds
    if (!command(h_unit, "SCAN_INTERVAL 1")) goto fail_join;

    if (!command(h_unit, "SELECT_NETWORK %d", net_num)) goto fail_join;
    command(h_unit, "STA_AUTOCONNECT %d",
            (ap_spec->options & kLTWiFi_JoinOption_Retry) ? 1 : 0); // ignore result
    return;
fail_join:
    command(h_unit, "REMOVE_NETWORK %d", net_num); // ignore result
    unit->join_callback(h_unit, kLTWiFi_JoinStatus_Failed);
    unit->join_status = kLTWiFi_JoinStatus_Failed;
    return;
}

static void DriverWiFiSupp_JoinCheck(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);
    if (unit->join_status >= kLTWiFi_JoinStatus_Associating) {
        if (unit->join_callback) {
            unit->join_callback(h_unit, unit->join_status);
        }
        if (unit->join_status >= kLTWiFi_JoinStatus_Failed) {
            unit->join_callback = NULL;
        }
    }
}

static void DriverWiFiSupp_JoinAbort(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);
    unit->join_status = kLTWiFi_JoinStatus_Aborted;
    if (unit->join_callback) {
        unit->join_callback(h_unit, unit->join_status);
        unit->join_callback = NULL;
    }
    command(h_unit, "DISCONNECT"); // ignore result
    unit->connected = false;
}

static bool DriverWiFiSupp_GetApInfo(LTDeviceUnit h_unit, LTWiFi_ApInfo *ap) {
    GET_UNIT_RETURN(unit, h_unit, false);
    char buf[50];
    char bssid[50];
    int noise;
    int age;

    // "STATUS" information from wpa_supplicant
    const char *reply = request(h_unit, "STATUS");
    if (ltstring_isempty(reply)) unit->connected = false;

    get_field_str(reply, "wpa_state", buf, sizeof(buf));
    if (lt_strcmp(buf, "DISCONNECTED") == 0) {
        unit->connected = false;
    }

    if (!unit->connected) {
        // Return cached ApInfo
        *ap = unit->ApInfo;
        return false;
    }

    get_field_str(reply, "bssid", bssid, sizeof(buf));
    pMacAddress->StringToMacAddress(bssid, &ap->bssid);

    get_field_str(reply, "ssid", ap->ssid, sizeof(ap->ssid));
    convert_unicode(ap->ssid);

    ap->security = getAPSecurity(h_unit, reply);

    ap->channel = freqToChan(get_field_int(reply, "freq", 0));

    // "SIGNAL_POLL" information from wpa_supplicant
    reply = request(h_unit, "SIGNAL_POLL");
    ap->bandwidth = get_field_int(reply, "WIDTH", 0);
    ap->rssi = get_field_int(reply, "RSSI", 0);

    // Find BSS info matching current bssid - usually BSS 0
    for (int i = 0; i < 20 ; i++) {
        char cmd[10];
        lt_snprintf(cmd, sizeof(cmd), "BSS %u", i);
        reply = request(h_unit, cmd);
        if (ltstring_isempty(reply)) {
            // No more BSS data
            if (i > 0) { // Then just use BSS 0 or noise calculation
                reply = request(h_unit, "BSS 0");
            }
            break;
        }

        get_field_str(reply, "bssid", buf, sizeof(buf)); // Read bssid from BSS into buf
        if (lt_strcmp(bssid, buf) == 0) { // Found it
            // cache IE from BSS if the bssid match bssid from STATUS
            get_field_str(reply, "ie", unit->ApInfo_ie, sizeof(unit->ApInfo_ie));
            break;
        }
    }

    // Use BSS information from wpa_supplicant to get noise level
    // The noise level is from the last scan so it will be stale
    // but it is better than nothing and noise level is mostly
    // a fairly constant function of antenna and radio gain.
    ap->snr = 0;
    noise = get_field_int(reply, "noise", 0);
    if (ap->rssi != 0 && noise < ap->rssi) {
        ap->snr = ap->rssi - noise;
    }

    // Update ApInfo cache value
    unit->ApInfo = *ap;

    // Log scan results and current AP IE. Rate limit to 6 hours.
    age = get_field_int(reply, "age", 0);

    if (unit->connected && age > 10 && age < 120) { // Recent scan data available
        static LTTime lastlogtime = {-18000000000000LL}; // Enable logging after 1 hours
        LTTime now;

        // Log this at most once pr. 6 hours
        now = pCore->GetKernelTime();
        if (LTTime_IsGreaterThan(LTTime_Subtract(now, lastlogtime), LTTime_Seconds(6*60*60))) {
            lastlogtime = now;

            // Log IE vector from this connection probe response
            recordApInfo(unit);
            pMacAddress->MacAddressToOui(&ap->bssid, buf, ':');
            LTLOG_SERVER("scan.ap.ie", "%s, %lx, \"%s\", %u, %u, %u, %s",
                         buf, LT_Pu32(pMacAddress->MacAddressToHash(&unit->ApInfo.bssid)), unit->ApInfo.ssid,
                         unit->ApInfo.channel, 1/*isMyBssid*/, 1/*isMySsid*/, unit->ApInfo_ie);

            // Log all scan results in buffer
            int bssindex = 1;
            do {
                char cmd[10];
                LTWiFi_ApInfo bss;
                char flags[30];
                int speed;
                bss.rssi = get_field_int(reply, "level", 0);
                bss.channel = freqToChan(get_field_int(reply, "freq", 0));
                get_field_str(reply, "flags", flags, sizeof(flags));
                get_field_str(reply, "ssid", bss.ssid, sizeof(bss.ssid));
                convert_unicode(bss.ssid);
                get_field_str(reply, "bssid", buf, sizeof(buf));
                pMacAddress->StringToMacAddress(buf, &bss.bssid);
                speed = get_field_int(reply, "est_throughput", 0);
                pMacAddress->MacAddressToOui(&bss.bssid, buf, ':');
                LTLOG_SERVER("scan.ap", "%d, %u, %s, %s, %lx, \"%s\", %d kbps",
                             bss.rssi, bss.channel, flags, buf,
                             LT_Pu32(pMacAddress->MacAddressToHash(&bss.bssid)),
                             bss.ssid, speed);
                lt_snprintf(cmd, sizeof(cmd), "BSS %u", bssindex++);
                reply = request(h_unit, cmd);
            } while (!ltstring_isempty(reply) && bssindex <= 20);
        }
    }

    pMacAddress->MacAddressToString(&ap->bssid, buf, ':');
    LTLOG_LEVEL_DEBUG(kLogDebug, "getapinfo", "ssid=%s bssid=%s channel=%u security=%d "
                      "bw=%u rssi=%d snr=%u connected=%s",
                      ap->ssid, buf, ap->channel, ap->security,
                      ap->bandwidth, ap->rssi, ap->snr,
                      unit->connected? "true" : "false");
    return unit->connected;
}

static bool DriverWiFiSupp_GetLinkState(LTHandle h_unit) {
    // Return true if STA is connected to the AP.
    return (DriverWiFiSupp_GetDriverState(h_unit) ==
            kLTWiFi_DriverState_Connected);
}

static void DriverWiFiSupp_Disconnect(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);
    command(h_unit, "DISCONNECT"); // ignore result
    unit->connected = false;
}

static bool DriverWiFiSupp_ApStart(LTDeviceUnit h_unit, LTWiFi_ApInfo *ap) {
    GET_UNIT_RETURN(unit, h_unit, false);
    u8 n;
    s32 net_num;

    unit->connected = false;

    // Only use a single network block, remove all others:
    for (n = 0; n < 8; n++) {
        command(h_unit, "REMOVE_NETWORK %d", n); // ignore result
    }

    const char *reply = request(h_unit, "ADD_NETWORK");
    if (!lt_isdigit(reply[0])) {
        LTLOG_DEBUG("softap.net_num", "failed to get net num = <%s>\n", reply);
        return false;
    } else {
        net_num = lt_strtos32(reply, NULL, 10);
    }

    // Set the SSID:
    if (!command(h_unit, "SET_NETWORK %d ssid \"%s\"", net_num, ap->ssid))
        goto fail_ap;
    // Configure as softap:
    if (!command(h_unit, "SET_NETWORK %d mode 2", net_num)) goto fail_ap;
    // Set the password:
    if (ap->pass == NULL || lt_strlen(ap->pass) == 0 ||
        ap->security == kLTWiFi_ApSecurity_Wep) {
        if (!command(h_unit, "SET_NETWORK %d key_mgmt NONE", net_num))
            goto fail_ap;
    } else {
        // Use wpa2
        if (!command(h_unit, "SET_NETWORK %d key_mgmt WPA-PSK", net_num))
            goto fail_ap;

        if (lt_strlen(ap->pass) == 64) {
            // Raw hex PSK, no quotes
            if (!command(h_unit, "SET_NETWORK %d psk %s", net_num, ap->pass))
                goto fail_ap;
        } else {
            if (!command(h_unit, "SET_NETWORK %d psk \"%s\"", net_num, ap->pass))
                goto fail_ap;
        }
    }
    // Optionally set AP channel:
    if (ap->channel != 0) {
        command(h_unit, "SET_NETWORK %d frequency %u", net_num,
                chanToFreq(ap->channel));
    }
    command(h_unit, "SET_NETWORK %d disabled 0", net_num); // ignore result

    if (!command(h_unit, "SELECT_NETWORK %d", net_num)) goto fail_ap;
    return true;
fail_ap:
    command(h_unit, "REMOVE_NETWORK %d", net_num); // ignore result
    return false;
}

static bool DriverWiFiSupp_ReceiveFrame(LTDeviceUnit h_unit,
                                        LTWiFi_FrameRxCallback *pCallback,
                                        void *pClientData)
{
    // Receive WiFi frames
    // return true when callback is set successfully
    LT_UNUSED(h_unit);
    LT_UNUSED(pCallback);
    LT_UNUSED(pClientData);
    LTLOG_DEBUG("rxframe.noimpl", "not implemented: ReceiveFrame");
    return false;
}

static bool DriverWiFiSupp_TransmitFrames(LTDeviceUnit h_unit,
                                      const LTBufferChain *bufferChain)
{
    // Transmit one or more WiFi frames
    // return true when frames are successfully queued for transmission
    LT_UNUSED(h_unit);
    LT_UNUSED(bufferChain);
    LTLOG_DEBUG("txframe.noimpl", "not implemented: TransmitFrames");
    return false;
}

static void DriverWiFiSupp_SniffFrames(LTDeviceUnit h_unit,
                                       LTWiFi_SniffCallback callback)
{
    // WiFi sniffer
    LT_UNUSED(h_unit);
    LT_UNUSED(callback);
    LTLOG("sniff.noimpl", "not implemented: SniffFrames");
}

static int DriverWiFiSupp_SendFrame(LTDeviceUnit h_unit, LTWiFi_Frame *frame) {
    // Send raw WiFi frames
    // return -1 on error
    LT_UNUSED(h_unit);
    LT_UNUSED(frame);
    LTLOG("sndframe.noimpl", "not implemented: SendFrame");
    return -1;
}

static LTWiFi_DisconnectReason DriverWiFi_GetDiscReasonCode(LTDeviceUnit h_unit) {
    GET_UNIT_RETURN(unit, h_unit, kLTWiFi_DisconnectReason_Generic);
    getWiFiEvents(unit); // Flush event queue
    LTWiFi_DisconnectReason dcrsn = kLTWiFi_DisconnectReason_Generic;
    switch (unit->disconnect_reason) {
        /* Local reason codes  */
        case kDriverWiFiSupp_DisconnectReason_NoApFound:
            // 201: CTRL-EVENT-NETWORK-NOT-FOUND:14
            dcrsn = kLTWiFi_DisconnectReason_DriverNoApFound;
            break;
        case kDriverWiFiSupp_DisconnectReason_Reject:
            // 202: CTRL-EVENT-ASSOC-REJECT
            dcrsn = kLTWiFi_DisconnectReason_ApReceiveDeauth;
            break;

        default:
            dcrsn = LTDriverUtil_IeeeToLtDisconnectReason((WiFiDriver_80211ReasonCode)unit->disconnect_reason);
    }
    return dcrsn;
}

/*******************************************************************************
 * LTDriverWiFi Interface
 ******************************************************************************/

static void LinuxDriverWiFiSupp_OnDestroyHandle(LTHandle h_unit) {
    GET_UNIT(unit, h_unit);
    /* this is where you could do something if you wanted to do something
       when the client destroys the handle.
       You don't have to free anythign here because you just stored a
       pointer in the handle, you didn't store a pointer to something you
       allocated in the handle.
    */
    wpa_quit(h_unit); // Close socket connections and free memory
}

define_LTLIBRARY_INTERFACE(LTDriverWiFi, LinuxDriverWiFiSupp_OnDestroyHandle)
    .GetDriverInfo  = DriverWiFiSupp_GetDriverInfo,
    .GetDriverState = DriverWiFiSupp_GetDriverState,
    .SetDriverState = DriverWiFiSupp_SetDriverState,
    .SetOption      = DriverWiFiSupp_SetOption,
    .GetOption      = DriverWiFiSupp_GetOption,
    .GetMetrics     = DriverWiFiSupp_GetMetrics,
    .ScanStart      = DriverWiFiSupp_ScanStart,
    .ScanCheck      = DriverWiFiSupp_ScanCheck,
    .ScanAbort      = DriverWiFiSupp_ScanAbort,
    .JoinStart      = DriverWiFiSupp_JoinStart,
    .JoinCheck      = DriverWiFiSupp_JoinCheck,
    .JoinAbort      = DriverWiFiSupp_JoinAbort,
    .GetApInfo      = DriverWiFiSupp_GetApInfo,
    .GetLinkState   = DriverWiFiSupp_GetLinkState,
    .Disconnect     = DriverWiFiSupp_Disconnect,
    .ApStart        = DriverWiFiSupp_ApStart,
    .ReceiveFrame   = DriverWiFiSupp_ReceiveFrame,
    .TransmitFrames = DriverWiFiSupp_TransmitFrames,
    .SniffFrames    = DriverWiFiSupp_SniffFrames,
    .SendFrame      = DriverWiFiSupp_SendFrame,
    .GetDiscReasonCode = DriverWiFi_GetDiscReasonCode,
LTLIBRARY_DEFINITION;
