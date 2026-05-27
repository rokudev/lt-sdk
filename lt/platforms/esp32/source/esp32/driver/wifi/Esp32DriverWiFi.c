/*******************************************************************************
 *
 * Esp32DriverWiFi: LT WiFi driver for Esp32
 * -----------------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/utility/macaddress/LTUtilityMacAddress.h>
#include <lt/device/wifi/LTDriverWiFi.h>
#include <lt/device/wifi/LTDriverWiFiUtil.h>
#include "../esp32-lt-os-adapter/Esp32_LTOSAdapter.h"

typedef LT_SIZE size_t;
#undef  va_list
#define va_list     lt_va_list
#undef  va_start
#define va_start    lt_va_start
#undef  va_arg
#define va_arg      lt_va_arg
#undef  va_end
#define va_end      lt_va_end
#undef  va_copy
#define va_copy     lt_va_copy

#define _VA_LIST_
#include <espidf_wifi.h>
/* Do not include LTDeviceWiFi.h to provide isolation from upper WiFi layers */
DEFINE_LTLOG_SECTION("esp32.drv.wifi");

// #define DBLOG(...) LTLOG(__VA_ARGS__)
#define DBLOG(...)

/*******************************************************************************
 * WiFi Related Configuration
 ******************************************************************************/
#define DEFAULT_LISTEN_INTERVAL 3
// !!!FIXME: find a better way go deal with handle/index conversions
#define WLAN0_IDX 0
static LTDeviceUnit WLAN0_IDX_hUnit = 0;

// See LT-1252 regarding these locks
#ifdef DISABLE_WIFIAPI_LOCKS
#define LOCK_WIFIAPI()
#define UNLOCK_WIFIAPI()
#else
#define LOCK_WIFIAPI()   LTEsp32OSAdapter_LockWiFiApi(__LINE__)
#define UNLOCK_WIFIAPI() LTEsp32OSAdapter_UnlockWiFiApi()
#endif

static int HandleToIndex(LTDeviceUnit h_unit)
{
    (void) h_unit;
    return WLAN0_IDX;
}
/*******************************************************************************
 * LT Related Declarations
 ******************************************************************************/

static LTCore               * pCore             = NULL;
static ILTThread            * iThread           = NULL;
static LTUtilityMacAddress  * MAC_Library       = NULL;

#define CLEAR(v) lt_memset(v, 0, sizeof(*v))

/*******************************************************************************
 *
 * The DriverWiFi Abstract Interface
 * ---------------------------------
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
static LTWiFi_DisconnectReason DriverWiFi_GetDiscReasonCode(LTDeviceUnit h_unit);

/** Utility Functions *********************************************************/

static LTWiFi_ApSecurity Esp32ToLtSecurity(wifi_auth_mode_t authmode, wifi_cipher_type_t pairwise_cipher,  wifi_cipher_type_t group_cipher)
{
    LT_UNUSED(group_cipher);
    LTWiFi_ApSecurity retApSec = kLTWiFi_ApSecurity_Unknown;
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        retApSec = kLTWiFi_ApSecurity_Open;
        break;
    case WIFI_AUTH_WEP:
        retApSec = kLTWiFi_ApSecurity_Wep;
        break;
    case WIFI_AUTH_WPA_PSK:
        retApSec = kLTWiFi_ApSecurity_Wpa;
        break;
    case WIFI_AUTH_WPA2_PSK:
        retApSec = kLTWiFi_ApSecurity_Wpa2;
        break;
    case WIFI_AUTH_WPA_WPA2_PSK:
        retApSec = kLTWiFi_ApSecurity_Wpa2;
        break;
    default:
        retApSec = kLTWiFi_ApSecurity_Unknown;

    }

    switch (pairwise_cipher) {
    case WIFI_CIPHER_TYPE_NONE:
        retApSec = kLTWiFi_ApSecurity_Open;
        break;
    case WIFI_CIPHER_TYPE_WEP40:
    case WIFI_CIPHER_TYPE_WEP104:
        retApSec = kLTWiFi_ApSecurity_Wep;
        break;

    case WIFI_CIPHER_TYPE_TKIP:
        retApSec = kLTWiFi_ApSecurity_Wpa2Tkip;
        break;

    case WIFI_CIPHER_TYPE_CCMP:
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        retApSec = kLTWiFi_ApSecurity_Wpa2;
        break;

    case WIFI_CIPHER_TYPE_AES_CMAC128:
        retApSec = kLTWiFi_ApSecurity_WpaAes;
        break;
    default:
        break;
    }
    return retApSec;
}
#define MAX_CONNECT_RETRY 3
typedef struct WiFiUnit {
    bool            is_hardware_up;     // chip and lower driver operating
    bool            is_up;              // driver operating
    bool            is_connected;       // STA joined to AP (aka linked)
    bool            scan_active;        // scan in progress
    s8              debug_mode;         // for more verbose debug info

    // Hack for first connect
    bool            is_joining;
    s8              connect_retry;
    LTWiFi_ScanSpec scan_spec;
    LTWiFi_ApInfo   ap_join_spec;
    int ref_cnt;
    LTWiFi_JoinStatus join_status;
    LTWiFi_FrameRxCallback *rx_frame;
    void *rx_client_data;
    LTWiFi_JoinStatus_CB  *join_driver_CB;
    LTWiFi_ScanResults_CB *scan_driver_CB;
    bool            suppress_join_callback;
    wifi_scan_config_t scan_params;
    wifi_err_reason_t disconnect_reason;
    LTTime disconnect_time;
    bool disconnect_logging;
    u32             txFrameCount;
    u32             rxFrameCount;
} WiFiUnit;

static WiFiUnit s_Esp32WiFiUnit;
/** Unit Functions ************************************************************/

WiFiUnit *CreateWiFiUnit(void)
{
    WiFiUnit *unit = &s_Esp32WiFiUnit;
    if (!unit->is_hardware_up) {
        CLEAR(unit);
    } else {
        int ref_cnt = unit->ref_cnt;
        CLEAR(unit);
        unit->ref_cnt = ref_cnt;
        unit->is_hardware_up = true;
    }
    unit->disconnect_logging = true;
    return unit;
}

void DestroyWiFiUnit(WiFiUnit *unit)
{
    LT_UNUSED(unit);
}

// Get rid of unit handles in this driver - we don't use them and they just present more failure surface
// In fact, the LTDriverWiFi should be modified to take a data pointer, not a handle.
#define GET_UNIT(wu, hu) LT_UNUSED(hu); WiFiUnit *wu = &s_Esp32WiFiUnit;
#define GET_UNIT_RETURN(wu, hu, code) LT_UNUSED(hu); LT_UNUSED(code); WiFiUnit *wu = &s_Esp32WiFiUnit;

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/** Driver ********************************************************************/

static void Roku_GetChipInfo(char **id, u8 *cver)
{
    *id = "Esp32";
    *cver = 1;
}
static bool esp_wifi_is_up(void)
{
    wifi_mode_t mode;
    LOCK_WIFIAPI();
    esp_err_t ret = esp_wifi_get_mode(&mode);
    UNLOCK_WIFIAPI();
    if (ret == ESP_OK) {
        return true;
    }
    return false;
}

static bool esp_wifi_is_connected(void)
{
    wifi_mode_t mode;
    esp_err_t ret = esp_wifi_get_mode(&mode);
    if (ret == ESP_OK && mode == WIFI_MODE_STA) {
        LTDeviceUnit h_unit = WLAN0_IDX_hUnit;
        if (!h_unit) return false;
        GET_UNIT_RETURN(unit, h_unit, false);
        return (unit->is_connected);
    }
    return false;
}

extern esp_err_t esp_wifi_deinit(void);
extern int esp_wifi_adapter_init(int *p_wifi_ref_cnt);

static bool DriverWiFi_GetDriverInfo(LTDeviceUnit h_unit, LTWiFi_DriverInfo *info)
{
    GET_UNIT_RETURN(unit, h_unit, false);
    if (!esp_wifi_is_up()) unit->is_up = false;
    CLEAR(info);
    esp_read_mac((u8 *)&info->mac_address, ESP_MAC_WIFI_STA);
    // This code to change once Realtek provides actual API function for it:
    char *id;
    u8 cver;
    Roku_GetChipInfo(&id, &cver);
    char version[2];
    version[0] = '0' + cver;
    version[1] = 0;

    lt_strncpyTerm(info->vendor,  "Espressif",       sizeof(info->vendor));
    lt_strncpyTerm(info->product, id,          sizeof(info->product));
    lt_strncpyTerm(info->version, version,     sizeof(info->version));
    lt_strncpyTerm(info->updated, "2022-05-19", sizeof(info->updated));
    return unit->is_up;
}

static bool DriverWiFi_SetDriverState(LTDeviceUnit h_unit, LTWiFi_DriverState state)
{
    GET_UNIT_RETURN(unit, h_unit, false);
    switch (state) {
    case kLTWiFi_DriverState_Up:
        if (unit->is_up) return true; // don't attempt UP if already UP
        // Bring up HW first:
        if (!unit->is_hardware_up) {
            if (esp_wifi_adapter_init(&(unit->ref_cnt)) != ESP_OK) {
                LTLOG_REDALERT("up.fail", "UP failed");
                return false;
            }
            unit->is_hardware_up = true;
        }
        LOCK_WIFIAPI();
        esp_err_t ret;
#if 0   // It seems counter productive to call stop in the UP flow. A DriverState_Down
        // Would do this for us in the case we want to down-then-up the driver.
        esp_err_t ret = esp_wifi_stop();
        if (ret != ESP_OK) {
            UNLOCK_WIFIAPI();
            LTLOG_REDALERT("wifi.stop.err1", "Failed to stop. ret=%d\n", ret);
            return false;
        }
#endif
        ret = esp_wifi_set_mode(WIFI_MODE_STA);
        if (ret != ESP_OK) {
            UNLOCK_WIFIAPI();
            LTLOG_REDALERT("up.fail1", "Failed to set Wi-Fi mode=%d ret=%d\n", WIFI_MODE_STA, ret);
            return false;
        }
        ret = esp_wifi_start();
        if (ret != ESP_OK) {
            UNLOCK_WIFIAPI();
            LTLOG_REDALERT("up.fail2", " start ret=%d", ret);
            return false;
        }
        unit->is_up = true;
        UNLOCK_WIFIAPI();
        break;
    case kLTWiFi_DriverState_Down:
        LOCK_WIFIAPI();
        ret = esp_wifi_disconnect();
        if (ret != ESP_OK) {
            UNLOCK_WIFIAPI();
            LTLOG_REDALERT("down.fail.disconn", "disconnect failed, ret=%d", ret);
            return false;
        }
        unit->is_connected = false; // !!! if was connected, needs to notify app
        unit->is_up = false;
        ret = esp_wifi_stop();
        if (ret != ESP_OK) {
            UNLOCK_WIFIAPI();
            LTLOG_REDALERT("down.fail.stop", "stop failed, ret=%d", ret);
            return false;
        }
        UNLOCK_WIFIAPI();
        ret = esp_wifi_deinit();
        if (ret != ESP_OK) {
            LTLOG_REDALERT("down.fail", "down failed");
            return false;
        }
        unit->is_hardware_up = false;
        break;
    case kLTWiFi_DriverState_ResetChip:
    // Can we do it on ESP32 chip???.
    case kLTWiFi_DriverState_ResetDriver:
        // This is how Realtek wants us to do it, but lacks fundamental control.
        DriverWiFi_SetDriverState(h_unit, kLTWiFi_DriverState_Down);
        DriverWiFi_SetDriverState(h_unit, kLTWiFi_DriverState_Up);
        break;
    default:
        return false;
    }
    return true;
}

static LTWiFi_DriverState DriverWiFi_GetDriverState(LTDeviceUnit h_unit)
{
    GET_UNIT_RETURN(unit, h_unit, kLTWiFi_DriverState_Unknown);
    // Use and active check in case driver fails in some serious way.
    // Of course, this requires an accurate chip, phy, and driver UP status.
    if (esp_wifi_is_up()) {
        if (esp_wifi_is_connected()) {
            return kLTWiFi_DriverState_Connected;
        }
        unit->is_connected = false;
        return kLTWiFi_DriverState_Up;
    }
    if (!unit->is_hardware_up) {
        return kLTWiFi_DriverState_Init;
    }
    return kLTWiFi_DriverState_Down;
}

static bool DriverWiFi_SetOption(LTDeviceUnit h_unit, char const *option, void *value)
{
    GET_UNIT_RETURN(unit, h_unit, false);
    LTLOG_DEBUG("set.opt", "set option %s", option);
    if (lt_strcmp(option, "debug_mode") == 0) {
        int mode = *(int*)value;  // !!! ugly, find better way
        unit->debug_mode = mode;
        return true;
    }
    if (lt_strcmp(option, "mac_address") == 0) {
#if 0
        LTMacAddress mac = *(LTMacAddress *)value;
        // !!! DriverInfo.mac_address = mac;
        char str[20];
        MAC_Library->MacAddressToString(&mac, str, 0); // Not in std ':' notation
        return esp_wifi_set_mac_address(str) == 0;
#else
        return false;
#endif
    }
    if (lt_strcmp(option, "channel") == 0) {
        LOCK_WIFIAPI();
        esp_err_t ret = esp_wifi_set_channel(*(int*)value,WIFI_SECOND_CHAN_NONE);
        UNLOCK_WIFIAPI();
        if (ret != ESP_OK) {
            return false;
        }
        return true;
    }
    if (lt_strcmp(option, "LogDisconnectReason") == 0) {
        switch (*(int*)value) {
        case 0: unit->disconnect_logging = false; break;
        case 1: unit->disconnect_logging = true; break;
        case 2: LTLOG("dcrsn", "%03lu.%06lu, %d",
                      (u32)LTTime_GetSeconds(unit->disconnect_time),
                      (u32)LTTime_GetMicroseconds(LTTime_FractionalSeconds(unit->disconnect_time)),
                      unit->disconnect_reason);
            break;
        default:
            return false;
        }
        return true;
    }
    return false;
}

static bool DriverWiFi_GetOption(LTDeviceUnit h_unit, char const *option, void *value)
{
    LT_UNUSED(value);
    if (lt_strcmp(option, "mac_address") == 0) {
        // !!! *(LTMacAddress *)value = DriverInfo.mac_address;
        return true;
    }
    if (lt_strcmp(option, "channel") == 0) {
        u8 channel;
        wifi_second_chan_t sec_channel;
        LOCK_WIFIAPI();
        esp_err_t ret = esp_wifi_get_channel(&channel,&sec_channel);
        UNLOCK_WIFIAPI();
        if (ret == ESP_OK) {
            LTLOG_DEBUG("chan", "channel is %u", (unsigned int)channel);
            *(int*)value = (int)channel;
            return true;
        } else {
            return false;
        }
    }
    if (lt_strcmp(option, "channels_1-14") == 0) {
        wifi_country_t country;
        LOCK_WIFIAPI();
        esp_err_t ret = esp_wifi_get_country(&country);
        UNLOCK_WIFIAPI();
        if (ret == ESP_OK) {
            *(int*)value = (1 << country.nchan) - 1; // Bit array of channels
            return true;
        } else {
            return false;
        }
    }
    if (lt_strcmp(option, "DisconnectReason") == 0) {
        *(int*)value = (int)DriverWiFi_GetDiscReasonCode(h_unit);
        return true;
    }
    return false;
}

static void DriverWiFi_GetMetrics(LTDeviceUnit h_unit, LTWiFi_Metrics *metrics, LT_SIZE sizeOfMetrics)
{
    LT_UNUSED(sizeOfMetrics);
    GET_UNIT(unit, h_unit);
    if (unit->debug_mode) {
        esp_wifi_statis_dump(WIFI_STATIS_ALL);
    }
    lt_memset(metrics, 0, sizeOfMetrics);
    metrics->tx_frame_count = unit->txFrameCount;
    metrics->rx_frame_count = unit->rxFrameCount;
}

/** Scan **********************************************************************/
void Esp32_ScanResult_STA_CB(unsigned int ap_num, wifi_ap_record_t *ap_list_buffer)
{
    LTDeviceUnit h_unit = WLAN0_IDX_hUnit;
    u16 bss_count = 0;
    if (!h_unit) return;
    GET_UNIT(unit, h_unit);
    if(!unit) {
        return;
    }
    if (!unit->scan_driver_CB) {
        LTLOG("sta.cb.nocb", "no callback");
        return;
    }
    if (ap_list_buffer == NULL) {
        unit->scan_active = false;
        unit->scan_driver_CB(h_unit, NULL);
        return;
    }
    u16 num = (u16)ap_num;
    LOCK_WIFIAPI();
    esp_err_t ret = esp_wifi_scan_get_ap_records(&num, ap_list_buffer);
    UNLOCK_WIFIAPI();
    if (ret != ESP_OK || num != ap_num) {
        LTLOG("sta.cb.nodata", "error %d: num received %d, expected %d",(int)ret, (int)num, (int)ap_num);
        return;
    }
    for (bss_count = 0; bss_count < num; bss_count++) {
        LTWiFi_ApInfo ap;
        CLEAR(&ap);
        ap.rssi = ap_list_buffer[bss_count].rssi;
        ap.channel = ap_list_buffer[bss_count].primary;
        ap.security = Esp32ToLtSecurity(ap_list_buffer[bss_count].authmode, ap_list_buffer[bss_count].pairwise_cipher, ap_list_buffer[bss_count].group_cipher);
        lt_strncpyTerm(ap.ssid, (const char *)ap_list_buffer[bss_count].ssid, sizeof(ap.ssid)-1);
        lt_memcpy(ap.bssid.octet, ap_list_buffer[bss_count].bssid, 6);
        //LTLOG_DEBUG("scan.cb", "ssid %s channel %d rssi %d", ap.ssid, (int)ap.channel, ap.rssi);
        unit->scan_driver_CB(h_unit, &ap); // callback will copy the ap struct
    }
    unit->scan_active = false;
    unit->scan_driver_CB(h_unit, NULL);
    return;
}
static void DriverWiFi_ScanStart(LTDeviceUnit h_unit, LTWiFi_ScanSpec *spec, LTWiFi_ScanResults_CB callback)
{
    GET_UNIT(unit, h_unit);
    if (!unit->is_up) return;
    unit->scan_spec = *spec;
    unit->scan_active = false;
    unit->scan_driver_CB = callback; // h_unit will provide context
    // Note: The state machine guarantees there will never be simultaneous
    // requests to the WiFi driver scan and joins.
    // Note: unit struct not local, resides in handle
    int result = ESP_OK;
    CLEAR(&(unit->scan_params));
    unit->scan_params.scan_type = WIFI_SCAN_TYPE_ACTIVE; /* WIFI_SCAN_TYPE_PASSIVE; */
    if (spec->channel) {
        unit->scan_params.channel = spec->channel[0]; /* FIXME: handle the whole array */
    }
    if (spec->ssid) {
        unit->scan_params.ssid = (u8*)spec->ssid; // caller guarantees lifefime
    }
    LOCK_WIFIAPI();
    esp_wifi_scan_stop();
    result = esp_wifi_scan_start(&(unit->scan_params), false);
    UNLOCK_WIFIAPI();
    if (result != ESP_OK) {
        LTLOG("scan.fail", "scan fail: %d", result);
        // !!! add notification that it failed?
        return;
    }
    unit->scan_active = true;
}

static void DriverWiFi_ScanCheck(LTDeviceUnit h_unit)
{
    // Esp32 has true callback, so this helper function isn't needed.
    LT_UNUSED(h_unit);
}

static void DriverWiFi_ScanAbort(LTDeviceUnit h_unit)
{
    // !!! TBD
    GET_UNIT(unit, h_unit);
    if (!unit->is_up) return;
}

/** Join **********************************************************************/

// These callback functions interface the Esp32 WiFi API into the LT WiFi.
// They execute in the context of the Esp32 WiFi code, so beware of that.


static void Esp32_JoinStatus_STA_CB(LTWiFi_JoinStatus status)
{
    LTDeviceUnit h_unit = WLAN0_IDX_hUnit;
    if (!h_unit) return;
    GET_UNIT(unit, h_unit);
    unit->join_status = status;
    unit->is_connected = status == kLTWiFi_JoinStatus_Success;
    if (unit->suppress_join_callback == true) {
        return;
    }
    unit->join_driver_CB(h_unit, status);
}

void Esp32_JoinStatus_STA_CB_Success(void)
{
    LTDeviceUnit h_unit = WLAN0_IDX_hUnit;
    if (!h_unit) return;
    GET_UNIT(unit, h_unit);
    unit->is_joining = false;
    Esp32_JoinStatus_STA_CB(kLTWiFi_JoinStatus_Success);
}
void Esp32_JoinStatus_STA_CB_Failed(wifi_err_reason_t reason)
{
    LTDeviceUnit h_unit = WLAN0_IDX_hUnit;
    if (!h_unit) return;
    GET_UNIT(unit, h_unit);
    unit->disconnect_reason = reason;
    unit->disconnect_time = LT_GetCore()->GetClockTimeUTC();
    if (LTTime_IsZero(unit->disconnect_time)) {
        unit->disconnect_time = LT_GetCore()->GetKernelTime();
    }
    if (unit->disconnect_logging) {
        LTLOG_SERVER("cb.dcrsn", "%d", reason);
    }
    if (unit->is_joining && unit->connect_retry < MAX_CONNECT_RETRY) {
        LOCK_WIFIAPI();
        int ret = esp_wifi_connect();
        UNLOCK_WIFIAPI();
        unit->connect_retry++;
        if (ret == ESP_OK) {
            return;
        }
    }
    unit->is_joining = false;
    Esp32_JoinStatus_STA_CB(kLTWiFi_JoinStatus_Failed);
}

static void DriverWiFi_JoinStart(LTDeviceUnit h_unit, LTWiFi_ApInfo *ap_spec, LTWiFi_JoinStatus_CB callback)
{
    wifi_config_t wifi_cfg;
    esp_err_t ret;
    GET_UNIT(unit, h_unit);
    if (!unit->is_up) return;
    unit->join_status = kLTWiFi_JoinStatus_Unknown;
    unit->join_driver_CB = callback;  // h_unit will provide context
    unit->ap_join_spec = *ap_spec; // ap validated already, no further need to do so
    unit->join_status = kLTWiFi_JoinStatus_Starting;
    unit->txFrameCount = 0;
    unit->rxFrameCount = 0;

    // LT-1206 - oddly, the JoinAbort disconnect is not enough
    unit->suppress_join_callback = true;
    LOCK_WIFIAPI();
    esp_wifi_disconnect();
    UNLOCK_WIFIAPI();
    for (int retry = 0 ; unit->is_connected && retry < 50 ; retry++) {
        iThread->Sleep(LTTime_Milliseconds(1)); // Wait for disconnect
    }
    unit->suppress_join_callback = false;
    unit->is_connected = false;
    unit->is_joining = true;
    unit->connect_retry = 1;

    LOCK_WIFIAPI();
    CLEAR(&wifi_cfg);
    ret = esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        UNLOCK_WIFIAPI();
        unit->join_status = kLTWiFi_JoinStatus_Failed;
        LTLOG("join.fail.0", "join failed: get config ret=%d", ret);
        return;
    }
    lt_memcpy(wifi_cfg.sta.ssid, &ap_spec->ssid, lt_strlen(ap_spec->ssid) + 1);
    if (ap_spec->pass != NULL) {
        lt_memcpy(wifi_cfg.sta.password, (unsigned char *)ap_spec->pass, lt_strlen(ap_spec->pass) + 1);
    } else {
        wifi_cfg.sta.password[0] = '\0';
    }
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.listen_interval = DEFAULT_LISTEN_INTERVAL;
    wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_cfg.sta.channel = 0; // we may want to set the channel from WSM for initial join try
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        UNLOCK_WIFIAPI();
        unit->join_status = kLTWiFi_JoinStatus_Failed;
        LTLOG("join.fail.1", "join failed: set config ret=%d", ret);
        return;
    }
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        UNLOCK_WIFIAPI();
        unit->join_status = kLTWiFi_JoinStatus_Failed;
        LTLOG("join.fail.2", "join failed: %d sec: %lu ssid: %s", ret, LT_Pu32(ap_spec->security), ap_spec->ssid);
        return;
    }
    UNLOCK_WIFIAPI();
}

static void DriverWiFi_JoinCheck(LTDeviceUnit h_unit)
{
    GET_UNIT(unit, h_unit);
    if (!unit->is_up) {
        //        if (unit->join_status == kLTWiFi_JoinStatus_Unknown) {
        //            return kLTWiFi_JoinStatus_Failed;
        //        }
        // !!! add notifier?
        unit->join_status = kLTWiFi_JoinStatus_Failed; // must unreg handler
    }
    if (unit->join_status >= kLTWiFi_JoinStatus_Success) { // includes Fail
#if 0
        /* Remove callbacks */
#endif
        //return unit->join_status; // done, do not JoinCheck again
    }
    //return kLTWiFi_JoinStatus_Associating; // assumed
}

static void DriverWiFi_JoinAbort(LTDeviceUnit h_unit)
{
    GET_UNIT(unit, h_unit);
    if (!unit->is_up) return;
    LOCK_WIFIAPI();
    esp_wifi_disconnect();
    UNLOCK_WIFIAPI();
    unit->is_connected = false;
    unit->is_joining = false;
}


static bool DriverWiFi_GetApInfo(LTDeviceUnit h_unit, LTWiFi_ApInfo *ap)
{
    GET_UNIT_RETURN(unit, h_unit, false);
    if (!unit->is_up || !unit->is_connected) return false;
    CLEAR(ap);
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret != ESP_OK) {
        LTLOG("ap.info", "FAILED");
        return false;
    }
    ap->rssi = ap_info.rssi;
    ap->channel = ap_info.primary;
    ap->pass = NULL;
    ap->security = Esp32ToLtSecurity(ap_info.authmode, ap_info.pairwise_cipher, ap_info.group_cipher);
    lt_memcpy(&ap->ssid, ap_info.ssid, lt_strlen((const char *)ap_info.ssid) + 1);
    lt_memcpy(&ap->bssid, ap_info.bssid, sizeof(ap->bssid));
    return true;
}

static void DriverWiFi_Disconnect(LTDeviceUnit h_unit)
{
    GET_UNIT(unit, h_unit);
    if (!unit->is_up) return;
    LOCK_WIFIAPI();
    esp_wifi_disconnect();
    UNLOCK_WIFIAPI();
    unit->is_connected = false;
}

/** Soft AP *******************************************************************/
/* This is never tested on Esp32 and so far will keep it off */
static bool DriverWiFi_ApStart(LTDeviceUnit h_unit, LTWiFi_ApInfo *ap_spec /* callback? */)
{
    GET_UNIT_RETURN(unit, h_unit, false);
    if (!unit->is_up) return false;
    int result;
#if 0
    result = esp32_start_softap();
#else
    result = ESP_OK;
#endif
    if (result != ESP_OK) {
        LTLOG("sap.fail", "soft AP failed: %d ssid: %s %s %d", result, ap_spec->ssid, ap_spec->pass, ap_spec->channel);
        return false;
    }
    return true;
}

/** Transfer ******************************************************************/

static bool DriverWiFi_TransmitFrames(LTDeviceUnit h_unit, const LTBufferChain *bufferChain)
{
    GET_UNIT_RETURN(unit, h_unit, false);
    if (!unit->is_up || !esp_wifi_is_connected()) return false; // use esp call because unit->is_connected may not be updated yet
    LTEsp32OSAdapter_LockWiFiApi(__LINE__);
    while (bufferChain) {
        int ret = esp_wifi_internal_tx(WIFI_IF_STA, bufferChain->buffer, bufferChain->bytesUsed);
        DBLOG("wf.tx", "buf %p len %lu ret %d", bufferChain->buffer, bufferChain->bytesUsed, ret);
        unit->txFrameCount++; // assumes 1-to-1 with buffer
        if (ret != ESP_OK) {
            LTEsp32OSAdapter_UnlockWiFiApi();
            // LTLOG_REDALERT("tx.fail", "transmit failed: 0x%08lx len: %lu", LT_Ps32(ret), LT_Pu32(bufferChain->bytesUsed));
            return false;
        }
        bufferChain = bufferChain->next;
    }
    LTEsp32OSAdapter_UnlockWiFiApi();
    return true;
}
/* Note:
 *     All TX done/RX done/Error trigger functions are not called from
 *     interrupts, this is much different from ethernet driver, including:
 *       * wlan_rx_done
 *       * wlan_tx_done
 *
 *     These functions are called in a Wi-Fi private thread. So we just use
 *     mutex/semaphore instead of disable interrupt, if necessary.
 */
/****************************************************************************
 * Function: lt_wlan_sta_rx_done
 *
 * Description:
 *   Wi-Fi RX done callback function. If this is called, it means receiving
 *   packet.
 *
 * Input Parameters:
 *   unit   - Reference to the driver state structure
 *   buffer - Wi-Fi received packet buffer
 *   len    - Length of received packet
 *   eb     - Wi-Fi receive callback input eb pointer
 *
 * Returned Value:
 *   0 on success or a negated errno on failure
 *
 ****************************************************************************/

static esp_err_t lt_wlan_sta_rx_done(void *buffer, uint16_t len, void *eb)
{
    GET_UNIT_RETURN(unit, WLAN0_IDX_hUnit,false);
    LTBufferChain bufferChain = { .next = NULL, .buffer = lt_malloc(len), .size = len, .bytesUsed = len };
    DBLOG("wf.rx", "buf %p len %u rxcb %p", bufferChain.buffer, len, unit ? unit->rx_frame : NULL);
    unit->rxFrameCount++; // assumes 1-to-1 with buffer
    if (bufferChain.buffer) {
        lt_memcpy(bufferChain.buffer, buffer, len);
        if (unit == NULL || unit->rx_frame == NULL || !unit->rx_frame(&bufferChain,unit->rx_client_data)) {
            lt_free(bufferChain.buffer);
        }
    }
    if (eb) {
        LTEsp32OSAdapter_LockWiFiApi(__LINE__);
        esp_wifi_internal_free_rx_buffer(eb);
        LTEsp32OSAdapter_UnlockWiFiApi();
    }
    return ESP_OK;
}

static bool DriverWiFi_ReceiveFrame(LTDeviceUnit h_unit, LTWiFi_FrameRxCallback * pCallback, void * pClientData)
{
    GET_UNIT_RETURN(unit, h_unit,false);
    unit->rx_frame = pCallback;
    unit->rx_client_data = pClientData;
    LTEsp32OSAdapter_LockWiFiApi(__LINE__);
    esp_err_t ret = esp_wifi_internal_reg_rxcb(WIFI_IF_STA, lt_wlan_sta_rx_done);
    LTEsp32OSAdapter_UnlockWiFiApi();
    // !!!FIXME
    if (HandleToIndex(h_unit) == WLAN0_IDX) {
        WLAN0_IDX_hUnit = h_unit;
    }
    return (ret == ESP_OK);
}

/** Sniff used for MiniMesh and Promiscuous Monitor ***************************/

static LTWiFi_SniffCallback *SniffCallback;

static void DriverWiFi_SniffCallback(void *buf, wifi_promiscuous_pkt_type_t type)
{
    LTWiFi_FrameInfo info;
    wifi_promiscuous_pkt_t *sniffer = (wifi_promiscuous_pkt_t *)buf;
    info.frame = (LTWiFi_Header*)sniffer->payload;
    info.size  = sniffer->rx_ctrl.sig_len;
    info.rssi  = sniffer->rx_ctrl.rssi;
    if (sniffer->rx_ctrl.rx_state || type == WIFI_PKT_MISC) return;
    // LTLOG_DEBUG("sniff.cbk", "Got %u bytes, rssi %d, channel %u", info.size, info.rssi, (unsigned)sniffer->rx_ctrl.channel);
    if (SniffCallback) SniffCallback(&info); // arg valid only during callback
}


static void DriverWiFi_SniffFrames(LTDeviceUnit h_unit, LTWiFi_SniffCallback callback)
{
    GET_UNIT(unit, h_unit);
    esp_err_t ret;
    if (!unit->is_up) return;
    if (callback) {
        SniffCallback = callback;
        esp_wifi_set_promiscuous_rx_cb(DriverWiFi_SniffCallback);
        ret = esp_wifi_set_promiscuous(true);
        if (ret != ESP_OK) {
            LTLOG("sniff.frames","Error setting promisc mode %d", ret);
        }

    } else {
        // SniffCallback stays assigned in case it gets called before unreg happens
        esp_wifi_set_promiscuous(false);
    }
}

static int DriverWiFi_SendFrame(LTDeviceUnit h_unit, LTWiFi_Frame *frame)
{
    LT_UNUSED(h_unit);
    /* TODO: validate how flexible is it !!!*/
    esp_err_t ret = esp_wifi_80211_tx(WIFI_IF_STA, &(frame->header), frame->size, false);
    return (ESP_OK == ret)?(0):(ret);
}

static LTWiFi_DisconnectReason DriverWiFi_GetDiscReasonCode(LTDeviceUnit h_unit) {
    GET_UNIT_RETURN(unit, h_unit, kLTWiFi_DisconnectReason_Generic);

    LTWiFi_DisconnectReason dcrsn = kLTWiFi_DisconnectReason_Generic;
    switch (unit->disconnect_reason) {
    case WIFI_REASON_BEACON_TIMEOUT:
        dcrsn = kLTWiFi_DisconnectReason_DriverApKeepaliveTimeout;
        break;
    case WIFI_REASON_NO_AP_FOUND:
        dcrsn = kLTWiFi_DisconnectReason_DriverNoApFound;
        break;
    case WIFI_REASON_AUTH_FAIL:
        dcrsn = kLTWiFi_DisconnectReason_ApReceiveDeauth;
        break;
    case WIFI_REASON_ASSOC_FAIL:
        dcrsn = kLTWiFi_DisconnectReason_ApReceiveDisassoc;
        break;
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        dcrsn = kLTWiFi_DisconnectReason_Generic;
        break;
    case WIFI_REASON_CONNECTION_FAIL:
        dcrsn = kLTWiFi_DisconnectReason_DriverApKeepaliveTimeout;
        break;
    case WIFI_REASON_AP_TSF_RESET:
    case WIFI_REASON_ROAMING:
        dcrsn = kLTWiFi_DisconnectReason_Generic;
        break;
    default:
        dcrsn = LTDriverUtil_IeeeToLtDisconnectReason((WiFiDriver_80211ReasonCode)unit->disconnect_reason);
    }
    return dcrsn;
}

/*******************************************************************************
 * LTDriverWiFi Hidden Library Interface
 ******************************************************************************/

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDriverWiFi, Esp32DriverWiFi);
/*
*   This special macro is defined in LTTypes.h.  It provides a declaration
*   of a  hidden library root interface of type LTDriverLibrary which carries the
*   prototype functions immediately below.  The first argument "LTDriverWiFi tells
*   LTCore that this driver library implements LTDriverWiFi and it also slaves
*   the library root interface version to the LTDriverWiFi version it was compiled
*   against.
*
*   In total, this macro does 5 things:
*    1. Creates a root library interface for the driver library
*    2. Types it as an LTDriverLibrary, giving it the driver type and the prototype functions thereof
*    3. Slaves the root interface version number to that of device's root interface version number so Device can know what its dealing with
*    4. Allows for more than one driver library to exist at runtime for the same device class.
*    5. Communicates the driver mapping to LTCore so that at runtime When LTDeviceWiFi wants to know
*        what WiFi drivers exist, LTCore can tell it.
*
*/

static bool Esp32DriverWiFiImpl_LibInit(void)
{
    pCore = LT_GetCore();
    if (!(MAC_Library = lt_openlibrary(LTUtilityMacAddress))) return false;
    iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    LTEsp32OSAdapter_LibInit();
    return true;
}

static void Esp32DriverWiFiImpl_LibFini(void)
{
    lt_closelibrary(MAC_Library);       // null okay
    LTEsp32OSAdapter_LibFini();
}

static u32 Esp32DriverWiFiImpl_GetNumDeviceUnits(void)
{
    return 1; /* this driver library only knows how to control one physical WiFi hardware block */
}

static LTDriverWiFi s_LTDriverWiFi;
/* This is a forward declaration of the LTDriverWiFi interface instance that gets defined at the end of this file
   by the macro define_LTLIBRARY_INTERFACE(LTDriverWiFi).  The variable name has to be s_LTDriverWiFi because
   that is what the macro defines. */

static LTDeviceUnit Esp32DriverWiFiImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber)
{
    /* Units are not the same as unit handles.  Multiple handles can exist for the same unit.
       In practice, LTDeviceWiFi is our only client and probably will only call us once per
       unit number, so likely a 1:1 mapping, but not necessarily. Since the client who calls create
       is the owner of the resource, and up to them to destroy the handle, we won't manage lists
       of handles or anything like that here. */
    if (nDeviceUnitNumber > 0) return (LTDeviceUnit)0;  // Only one, the STA for the network

    LTDeviceUnit hUnit = LT_GetCore()->CreateHandle((LTInterface *)&s_LTDriverWiFi, sizeof(WiFiUnit *));
    if (hUnit) {
        //See note above the GET_UNIT define
        //WiFiUnit **ppUnit = (WiFiUnit **)LT_GetCore()->GetHandlePrivateData(hUnit);
        //*ppUnit = CreateWiFiUnit();
        CreateWiFiUnit();
    }
    if (HandleToIndex(hUnit) == WLAN0_IDX) {
        WLAN0_IDX_hUnit = hUnit;
    }
    return hUnit;
}

// DestroyDeviceUnitHandle() not needed because handle interface provides the destroy.
static void Esp32DriverWiFi_OnDestroyHandle(LTHandle h_unit)
{
    GET_UNIT(unit, h_unit);
    DriverWiFi_SetDriverState(h_unit,kLTWiFi_DriverState_Down);
    DestroyWiFiUnit(unit);
}

define_LTLIBRARY_INTERFACE(LTDriverWiFi, Esp32DriverWiFi_OnDestroyHandle)
    .GetDriverInfo       = DriverWiFi_GetDriverInfo,
    .SetDriverState      = DriverWiFi_SetDriverState,
    .GetDriverState      = DriverWiFi_GetDriverState,
    .SetOption           = DriverWiFi_SetOption,
    .GetOption           = DriverWiFi_GetOption,
    .GetMetrics          = DriverWiFi_GetMetrics,
    .ScanStart           = DriverWiFi_ScanStart,
    .ScanCheck           = DriverWiFi_ScanCheck,
    .ScanAbort           = DriverWiFi_ScanAbort,
    .JoinStart           = DriverWiFi_JoinStart,
    .JoinCheck           = DriverWiFi_JoinCheck,
    .JoinAbort           = DriverWiFi_JoinAbort,
    .GetApInfo           = DriverWiFi_GetApInfo,
    .Disconnect          = DriverWiFi_Disconnect,
    .ApStart             = DriverWiFi_ApStart,
    .TransmitFrames      = DriverWiFi_TransmitFrames,
    .ReceiveFrame        = DriverWiFi_ReceiveFrame,
    .SniffFrames         = DriverWiFi_SniffFrames,
    .SendFrame           = DriverWiFi_SendFrame,
    .GetDiscReasonCode   = DriverWiFi_GetDiscReasonCode,
};
