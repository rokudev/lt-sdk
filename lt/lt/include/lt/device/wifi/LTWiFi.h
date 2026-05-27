/*******************************************************************************
 *
 * LTWiFi: WiFi Definitions
 * ------------------------
 *
 * WiFi defines and structs that span the full WiFi stack, from application
 * code through device and driver interfaces.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#ifndef LTWIFI_H
#define LTWIFI_H

#include <lt/LTTypes.h>
#include <lt/utility/macaddress/LTUtilityMacAddress.h>
#include <lt/net/core/LTNetCore.h>

LT_EXTERN_C_BEGIN

/*******************************************************************************
** WiFi Configuration and Control
*******************************************************************************/

/**
 * @brief General information about the WiFi driver.
 *
 * This info may be printed to console or sent to login server for reference.
 */
typedef struct LTWiFi_DriverInfo {
    u16  edition;               ///< compatibility version of this struct
    char vendor[4];             ///< vendor code (eg. RTK)
    char product[16];           ///< product identifier (eg. AmebaD)
    char chip[16];              ///< WiFi chip identifier (eg. rtl8710c)
    char version[20];           ///< version string (free format)
    char updated[16];           ///< release date (example YYYY-MM-DD)
    LTMacAddress mac_address;   ///< MAC address of the WiFi device (STA)
} LTWiFi_DriverInfo;

#define kLTWiFi_DriverInfo_Edition 0

typedef u8 LTWiFi_DriverState;
/**
 * @brief Driver states that can be set by application.
 *
 * Not all states may be supported by underlying driver. For example,
 * a driver might implement Reset_Driver with Down & Up.
 *
 * Quick test for WiFi up is: state >= kLTWiFi_DriverState_Up
 *
 * Some API actions and options may cause driver to go down, and state machine
 * must wait for driver to come back up. For example, soft-AP setup
 */
enum LTWiFi_DriverState {
    kLTWiFi_DriverState_Unknown = 0,  ///< take the interface down
    kLTWiFi_DriverState_ResetChip,    ///< a full reset of chip and driver
    kLTWiFi_DriverState_ResetDriver,  ///< reset only the driver
    kLTWiFi_DriverState_Init,         ///< driver initializing
    kLTWiFi_DriverState_Down,         ///< take the interface down
    kLTWiFi_DriverState_RFDown,       ///< disable the radio(s)
    kLTWiFi_DriverState_Up = 32,      ///< wifi is operational
    kLTWiFi_DriverState_Connected,    ///< wifi is connected to endpoint
};

/**
 * @brief Return true if WiFi driver is ready.
 *
 * @return TRUE if WiFi is operational and ready for commands, otherwise FALSE
 */
#define LTWiFi_IsDriverUp (LTWiFi_GetDriverState() >= kLTWiFi_DriverState_Up)

/*******************************************************************************
** WiFi Scan Related
*******************************************************************************/

enum LTDEVICEWIFI_MAX {
    kLTWiFi_Max_Ssid = 32,      // up to 32 valid characters, not terminated by '\0'.
    kLTWiFi_Max_Pass = 64,      // up to 63 valid characters for WPA or 64 hex digits for WEP, not terminated by '\0'.
};

typedef struct LTWiFi_AccessCredentials {
// support WPA only, 32 max chars in ssid and 63 max chars in pwd
    char ssid[kLTWiFi_Max_Ssid + 6];  ///< make room for a trailing '\0' past 32 and pad up so total struct size is multiple of 8
    char pwd[kLTWiFi_Max_Pass];       ///< up to 63 chraracters, then include '\0'
    u8 ssidLen;                       ///< number of characters in ssid, excluding '\0'
    u8 pwdLen;                        ///< number of characters in password, excluding '\0'
} LTWiFi_AccessCredentials;
LT_STATIC_ASSERT_SIZE_32_64(LTWiFi_AccessCredentials, 104, 104);

/**
 * @brief Scan options are specified as bit flags.
 */
enum LTWiFi_ScanOption {
    kLTWiFi_ScanOption_Secure  = (1<<0), ///< only find secure APs (not open)
    kLTWiFi_ScanOption_Passive = (1<<1), ///< only passive (not active) scan
    kLTWiFi_ScanOption_Hidden  = (1<<2), ///< include hidden APs
    kLTWiFi_ScanOption_Direct  = (1<<3), ///< include "DIRECT-" SSIDs
    kLTWiFi_ScanOption_Dups    = (1<<4), ///< include duplicate SSIDs
};

/**
 * @brief Specification for how scan is to operate
 *
 * All fields are optional. Clear to zero to use the defaults.
 * @ingroup ltdevice_wifi_struct
 */
typedef struct LTWiFi_ScanSpec {
    u16 edition;                ///< compatibility version of this struct
    u16 options;                ///< ScanOption bit flags
    char *ssid;                 ///< directed/wildcard scan (optional)
    LTMacAddress *bssid;        ///< send probe with BSSID (optional)
    u8 *channel;                ///< array of channels, null terminated
    s8 rssi;                    ///< ignore signals lower than this RSSI
    u8 probes;                  ///< number of probe-requests
    u8 chan_dwell;              ///< channel dwell time (milliseconds)
    u8 home_dwell;              ///< home channel dwell time (milliseconds)
} LTWiFi_ScanSpec;

#define kLTWiFi_ScanSpec_Edition 0

typedef u8 LTWiFi_ApSecurity;
/**
 * @brief AP security modes
 * @ingroup ltdevice_wifi_enum
 */
enum LTWiFi_ApSecurity {
    kLTWiFi_ApSecurity_Unknown = 0,
    kLTWiFi_ApSecurity_Open,      ///< no security
    kLTWiFi_ApSecurity_Wep,       ///< wired equivalent
    kLTWiFi_ApSecurity_Wpa,       ///< standard WPA (TKIP)
    kLTWiFi_ApSecurity_WpaAes,    ///< non-standard WPA
    kLTWiFi_ApSecurity_WpaEap,    ///< enterprise
    kLTWiFi_ApSecurity_Wpa2,      ///< standard WPA2 (AES)
    kLTWiFi_ApSecurity_Wpa2Tkip,  ///< non-standard WPA2
    kLTWiFi_ApSecurity_Wpa2Eap,   ///< enterprise
    kLTWiFi_ApSecurity_Wpa2Wpa3,  ///< wpa2 wpa3 mixed mode
    kLTWiFi_ApSecurity_Wpa3,      ///< standard WPA3 (SAE)
    kLTWiFi_ApSecurity_Wpa3Eap,   ///< WPA3 enterprise
    kLTWiFi_ApSecurity_Max
};
#define LTWIFI_IS_SECURE(s) (s) > kLTWiFi_ApSecurity_Open}

#ifndef DOXY_SKIP // [
/**
 * @brief AP security strings - paired with enum
 */
#define LTWiFi_SecurityStrings \
    "Unknown",  \
    "Open",     \
    "WEP",      \
    "WPA",      \
    "WPA-AES",  \
    "WPA-EAP",  \
    "WPA2",     \
    "WPA-TKIP", \
    "WPA2-EAP", \
    "WPA2WPA3", \
    "WPA3",     \
    "WPA3-EAP"

#define kLTWiFi_MaxSecurityStringLength 8
#endif // DOXY_SKIP  ]

/**
 * @brief AP information used for scan, join, etc.
 *
 * The ApInfo structure is used for several API functions:
 * 1. For SCAN results (all fields except pass)
 * 2. For JOIN specification (ssid, pass, and optional fields)
 * 3. For getting AP information (all fields except pass)
 * 4. For AP (Soft-AP) settings
 *
 * @ingroup ltdevice_wifi_struct
 */
typedef struct LTWiFi_ApInfo {
    u16 edition;                ///< compatibility version of this struct
    u16 options;                ///< special options as bit flags
    char ssid[34];              ///< SSID or wildcard
    char *pass;                 ///< used for JOIN (NULL for SCAN)
    LTWiFi_ApSecurity security; ///< security modes (e.g. WPA2-AES)
    LTMacAddress bssid;         ///< BSSID of AP (or ZERO for don't care)
    u8 channel;                 ///< operating channel
    u8 bandwidth;               ///< operating bandwidth (e.g. 20, 40, 80)
    s8 rssi;                    ///< RSSI of AP (or min-RSSI on JOIN)
    u8 snr;                     ///< signal-to-noise ratio (or zero if NA)
    u8 mode;                    ///< BSS mode (optional, e.g. 11ax, 11ac, 11n, legacy)
//  u8 *ies;                    ///< IE info elements (optional)
//  u16 ie_size;                ///<
} LTWiFi_ApInfo;

#define LTWiFi_ApInfo_Edition 0

/**
 * @brief Callback function used for AP scan results.
 *
 * A pointer to the current AP information structure passed as an argument.
 * The callback may get called from driver thread context.
 * It must process the info very quickly and not block for any reason.
 * The info data is only valid during the callback, and results can be
 * ignored or copied if they are needed by the application.
 *
 * @param[in] ap: AP information or NULL when no more APs found.
 * @ingroup ltdevice_wifi_cb
 */
typedef void (LTWiFi_ScanResults_CB)(LTDeviceUnit unit, LTWiFi_ApInfo *ap);

/*******************************************************************************
** WiFi Connection Related
*******************************************************************************/

/**
 * @brief Join options are specified as bit flags in the ApInfo structure.
 *
 * @ingroup ltdevice_wifi_enum
 */
enum LTWiFi_JoinOption {
    kLTWiFi_JoinOption_Rejoin  = (1<<0), ///< enable auto-rejoin
    kLTWiFi_JoinOption_Retry   = (1<<1), ///< if join fails, immediately retry
    kLTWiFi_JoinOption_Strong  = (1<<2), ///< join strongest AP with same SSID
};

typedef u8 LTWiFi_JoinStatus;
/**
 * @brief WiFi join status indicators.
 *
 * These will be reported as the join operation progresses.
 * Note: These are grouped for greater-than/less-than status comparisons
 * for failure vs success. Keep that relationship for new constants.
 *
 * @ingroup ltdevice_wifi_enum
 */
enum LTWiFi_JoinStatus  {
    // Initializing status:
    kLTWiFi_JoinStatus_Unknown = 0,     ///< not in use
    // Functioning status:
    kLTWiFi_JoinStatus_Starting,        ///< JOIN started
    kLTWiFi_JoinStatus_Scanning,        ///< AP scan in progress
    kLTWiFi_JoinStatus_Associating,     ///< AP association in progress
    kLTWiFi_JoinStatus_Associated,      ///< AP successfully associated
    kLTWiFi_JoinStatus_Authenticating,  ///< AP 4-way handshake in progress
    kLTWiFi_JoinStatus_Authenticated,   ///< AP 4-way handshake success
    // Terminating status:
    kLTWiFi_JoinStatus_Failed  = 10,    ///< JOIN failed, see status flow
    kLTWiFi_JoinStatus_Aborted,         ///< JOIN stopped by user
    kLTWiFi_JoinStatus_Success = 20,    ///< JOIN was successful
};

/**
 * @brief Callback function used for indicating JOIN status.
 *
 * The callback may get called from driver thread context.
 * It must process the info quickly and not block for any reason.
 *
 * If status >= kLTWiFi_JoinStatus_Failed then the join is done.

 * If status >= kLTWiFi_JoinStatus_Success then the join succeeded.
 *
 * @param[in] unit: The device unit.
 * @param[in] status: The status of the join.
 * @ingroup ltdevice_wifi_cb
 */
typedef void (LTWiFi_JoinStatus_CB)(LTDeviceUnit unit, LTWiFi_JoinStatus status);

typedef u8 LTWiFi_DisconnectReason;
/**
 * @brief LT Defined disconnect reasons.
 *
 * Vendor disconnect reasons will be translated to these types.
 * It will be using 1st nibble of u8
 *
 * @ingroup ltdevice_wifi_enum
 */
enum LTWiFi_DisconnectReason {
    kLTWiFi_DisconnectReason_Unknown = 0,
    kLTWiFi_DisconnectReason_System,                   ///< DC initiated by higher layer.
    kLTWiFi_DisconnectReason_DriverGeneric,            ///< Generic DC initiated by Driver.
    kLTWiFi_DisconnectReason_DriverNetdevDown,         ///< Network interface is down.
    kLTWiFi_DisconnectReason_DriverDfsDetection,       ///< DFS is detected the channel.
    kLTWiFi_DisconnectReason_DriverApUnsupChan,        ///< AP switched to an unsupp. ch.
    kLTWiFi_DisconnectReason_DriverApDfsChan,          ///< AP switched to an DFS channel.
    kLTWiFi_DisconnectReason_DriverApKeepaliveTimeout, ///< Connection to AP is lost.
    kLTWiFi_DisconnectReason_DriverApRoam,             ///< Roaming Failed.
    kLTWiFi_DisconnectReason_DriverNoApFound,          ///< AP not found
    kLTWiFi_DisconnectReason_ApReceiveDeauth,          ///< Deauth Received.
    kLTWiFi_DisconnectReason_ApReceiveDisassoc,        ///< Disassoc Received.
    kLTWiFi_DisconnectReason_Generic,                  ///< Uncategorized DC Reasons.
    kLTWiFi_DisconnectReason_Max
};

typedef u8 LTWiFi_NetworkDisconnectReason;
/**
 * @brief LT Defined disconnect reasons above L2 layer.
 *
 * Disconnect reasons above L2 Wifi layer.
 * It will be using 2nd nibble of u8
 *
 * @ingroup ltdevice_wifi_enum
 */
enum LTWiFi_NetworkDisconnectReason {
    kLTWiFi_NetworkDisconnectReason_Unknown = 0x10,
    kLTWiFi_NetworkDisconnectReason_NoIp    = 0x20,        ///< No DHCP Ip addr.
    kLTWiFi_NetworkDisconnectReason_Max     = 0xF0
};

/**
 * @brief WiFi metrics for current operation and session.
 *
 * Metric fields are cleared on reset.
 *
 * @ingroup ltdevice_wifi_struct
 */
typedef struct LTWiFi_Metrics {
    u16 edition;                ///< compatibility version of this struct
    u32 tx_frame_count;         ///< total frames transmitted
    u32 tx_frame_drops;         ///< dropped TX frame counter
    u32 tx_frame_retries;       ///< TX frame retry counter
    u32 rx_frame_count;         ///< total frames received
    u32 rx_frame_drops;         ///< when known (missing sequence numbers)
    u32 bad_frames;             ///< invalid frames (false alarms, wrong CRC)
    u32 cca;                    ///< clear channel assessments

    // If Auto-rejoin is handled in the lower level or wpa_supplicant then the next
    // three variables contains number of instances since last call to GetMetrics()
    u32 joinSuccess;            ///< New Auto-rejoin successes (optional)
    u32 joinFail;               ///< New Auto-rejoin fails (optional)
    u32 joinDisc;               ///< New Auto-rejoin disconnects (optional)
} LTWiFi_Metrics;

#define LTWiFi_Metrics_Edition 1

/**
 * @brief LTDeviceWiFi ReceiveFrame() callback
 *
 * After proper setup with ReceiveFrame(), this function will be called for
 * every WiFi frame that gets received.
 *
 * @param[in] pBuff: The buffer to hold received data.
 * @param[in] pClientData: The context used for processing.
 * @return TODO.
 * @ingroup ltdevice_wifi_cb
 */
typedef bool (LTWiFi_FrameRxCallback)(LTBufferChain *bufferChain, void *pClientData); // !!! Rename to LTWiFi_ReceiveFrame_CB

/*******************************************************************************
** WiFi 802.11 Frame Related
*******************************************************************************/

/**
 * @brief IEEE-802.11 3-Address frame header.
 *
 * The basic WiFi frame header. 4-Address header would be a separate struct.
 *
 * @ingroup ltdevice_wifi_struct
 */
typedef struct LTWiFi_Header {
    u16           control;      ///< Type of frame, DS, frags, retry, privacy
    u16           duration;     ///< On-air frame time in microseconds
    LTMacAddress  addr1;        ///< Receiver MAC address, defined by DS bits
    LTMacAddress  addr2;        ///< Transmitter MAC address, defined by DS bits
    LTMacAddress  addr3;        ///< Definition set by DS bits
    u16           sequence;     ///< Sequence and fragmentation numbers
} LTWiFi_Header;

/**
 * @brief IEEE-802.11 frame function control
 * @ingroup ltdevice_wifi_enum
 */
enum LTWiFi_FrameFC {
    kLTWiFi_FrameFC_AssocReq = 0x00,    ///< NEEDS_DOCUMENTATION
    kLTWiFi_FrameFC_Data     = 0x08,    ///< NEEDS_DOCUMENTATION
    kLTWiFi_FrameFC_ProbeReq = 0x40,    ///< NEEDS_DOCUMENTATION
    kLTWiFi_FrameFC_Beacon   = 0x80,    ///< NEEDS_DOCUMENTATION
    kLTWiFi_FrameFC_Action   = 0xD0,    ///< NEEDS_DOCUMENTATION
};

/**
 * @brief IEEE-802.11 QOS Access Category
 * @ingroup ltdevice_wifi_enum
 */
enum LTWiFi_AccessCategory {
    kLTWiFi_AC_VO = 0,
    kLTWiFi_AC_VI = 1,
    kLTWiFi_AC_BE = 2,
    kLTWiFi_AC_BK = 3
};

/**
 * @brief 802.11 WiFi frame with size field
 *
 * This is used to hold the actual frame. This header section is followed
 * by data. To access data, use (u8*)(frame+1).
 *
 * @ingroup ltdevice_wifi_struct
 */
typedef struct LTWiFi_Frame {
    u16           size;         ///< Size of header+data. Does not include this size field.
    LTWiFi_Header header;       ///< 802.11 header.
    // u8         data[xxx]     ///< 802.11 payload (or extended header+payload).
} LTWiFi_Frame;

/**
 * @brief 802.11 WiFi frame reference when received
 *
 * This only points to the frame (and its data) and any other information
 * related to the frame.
 *
 * @ingroup ltdevice_wifi_struct
 */
typedef struct LTWiFi_FrameInfo {
    LTWiFi_Header *frame;       ///< pointer to frame+data
    u16           size;         ///< total size of frame+data
    s8            rssi;         ///< signal strength
} LTWiFi_FrameInfo;

typedef void (LTWiFi_SniffCallback)(LTWiFi_FrameInfo *);

/**
 * @brief WiFi drv tx queue stats.
 *
 * @ingroup ltdevice_wifi_struct
 */
typedef struct LTWiFi_DrvTxQStats {
    u32 max_queue_num;
    u32 queue_inuse_num;
    u8 overfull;
} LTWiFi_DrvTxQStats;

LT_EXTERN_C_END
#endif /* LTWIFI_H */
