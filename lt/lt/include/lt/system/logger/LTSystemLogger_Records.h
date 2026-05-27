/******************************************************************************
 * LTSystemLogger_Records.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_LOGGER_LTSYSTEMLOGGER_RECORDS_H
#define ROKU_LT_INCLUDE_LT_LOGGER_LTSYSTEMLOGGER_RECORDS_H

#include "LTSystemLogger.h"

#define LTLogRecordPutPair(key, ...)       \
    LTMessagePackPut(mp, writer, (u8)key); \
    LTMessagePackPut(mp, writer, __VA_ARGS__);

typedef enum {
    kLTLogRecordType_Crash          = 1,
    kLTLogRecordType_Boot           = 2,
    kLTLogRecordType_SoftwareUpdate = 3,
    kLTLogRecordType_NetworkStatus  = 4,
    kLTLogRecordType_Events         = 5,
    kLTLogRecordType_System         = 6,
    kLTLogRecordType_Battery        = 7,
    kLTLogRecordType_Scribe         = 8,
    kLTLogRecordType_MediaUpload    = 9,
    kLTLogRecordType_ApInfo         = 10,
} LTLogRecordType;

/******************************************************************************
 * @name Standard Record 1: Crash
 * @{
 */

enum {
    kLTLogRecord_Crash_Core = 1,
    kLTLogRecord_Crash_FieldCount
};

typedef struct {
    void *Core;
    LT_SIZE CoreSize;
} LTLogRecordCrash;

LT_INLINE bool LTRecordCrashEncoder(const LTLogRecord *record,
                                    LTUtilityMessagePack *mp,
                                    LTMessagePack_Obj *writer) {
    LTLogRecordCrash *data = (LTLogRecordCrash *)record->payload;
    mp->PutMap(writer, kLTLogRecord_Crash_FieldCount - 1);
    LTLogRecordPutPair(kLTLogRecord_Crash_Core, (u8 *)data->Core, data->CoreSize);
    return true;
}

#define LTLOG_DEFINE_RECORD_CRASH(name) \
    static LTLogRecordCrash name;       \
    LTLOG_DEFINE_RECORD(name, kLTLogRecordType_Crash, &name, LTRecordCrashEncoder)

/** @} */

/******************************************************************************
 * @name Standard Record 2: Boot
 * @{
 */

enum {
    kLTLogRecord_Boot_Reason = 1,
    kLTLogRecord_Boot_Version,
    kLTLogRecord_Boot_DeviceId,
    kLTLogRecord_Boot_History,
    kLTLogRecord_Boot_FieldCount,

    kLTLogRecord_Boot_History_Length = 10
};

typedef enum {
    kLTLogRecord_Boot_Reason_Unknown = 0
} LTRecordBootReason;

typedef struct {
    LTRecordBootReason Reason;
    const char *SoftwareVersion;
    const char *DeviceId;
    LTTime History[kLTLogRecord_Boot_History_Length];
} LTLogRecordBoot;

LT_INLINE bool LTRecordBootEncoder(const LTLogRecord *record,
                                   LTUtilityMessagePack *mp,
                                   LTMessagePack_Obj *writer) {
    LTLogRecordBoot *data = (LTLogRecordBoot *)record->payload;
    mp->PutMap(writer, kLTLogRecord_Boot_FieldCount - 1);
    LTLogRecordPutPair(kLTLogRecord_Boot_Reason, (u8)data->Reason);
    LTLogRecordPutPair(kLTLogRecord_Boot_Version, data->SoftwareVersion);
    LTLogRecordPutPair(kLTLogRecord_Boot_DeviceId, data->DeviceId);
    mp->PutIntU32(writer, kLTLogRecord_Boot_History);
    mp->PutArray(writer, kLTLogRecord_Boot_History_Length);
    for (int i = 0; i < kLTLogRecord_Boot_History_Length; ++i) {
        LTMessagePackPut(mp, writer, data->History[i].nNanoseconds);
    }
    return true;
}

#define LTLOG_DEFINE_RECORD_BOOT(name) \
    static LTLogRecordBoot name;       \
    LTLOG_DEFINE_RECORD(name, kLTLogRecordType_Boot, &name, LTRecordBootEncoder)

/** @} */

/******************************************************************************
 * @name Standard Record 3: Software Update
 * @{
 */

enum {
    kLTLogRecord_SoftwareUpdate_Event = 1,
    kLTLogRecord_SoftwareUpdate_Reason,
    kLTLogRecord_SoftwareUpdate_From,
    kLTLogRecord_SoftwareUpdate_To,
    kLTLogRecord_SoftwareUpdate_FieldCount
};

typedef enum {
    kLTLogRecord_SoftwareUpdate_Event_Available = 1,
    kLTLogRecord_SoftwareUpdate_Event_Started,
    kLTLogRecord_SoftwareUpdate_Event_Complete,
    kLTLogRecord_SoftwareUpdate_Event_Installing,
    kLTLogRecord_SoftwareUpdate_Event_Installed
} LTLogRecordSoftwareUpdateEvent;

typedef enum {
    kLTLogRecord_SoftwareUpdate_Reason_Unknown = 0
} LTLogRecordSoftwareUpdateReason;

typedef struct {
    LTLogRecordSoftwareUpdateEvent Event;
    LTLogRecordSoftwareUpdateReason Reason;
    const char *FromVersion;
    const char *ToVersion;
} LTLogRecordSoftwareUpdate;

LT_INLINE bool LTRecordSoftwareUpdateEncoder(const LTLogRecord *record,
                                             LTUtilityMessagePack *mp,
                                             LTMessagePack_Obj *writer) {
    LTLogRecordSoftwareUpdate *data = (LTLogRecordSoftwareUpdate *)record->payload;
    mp->PutMap(writer, kLTLogRecord_SoftwareUpdate_FieldCount - 1);
    LTLogRecordPutPair(kLTLogRecord_SoftwareUpdate_Event, (u8)data->Event);
    LTLogRecordPutPair(kLTLogRecord_SoftwareUpdate_Reason, (u8)data->Reason);
    LTLogRecordPutPair(kLTLogRecord_SoftwareUpdate_From, data->FromVersion);
    LTLogRecordPutPair(kLTLogRecord_SoftwareUpdate_To, data->ToVersion);
    return true;
}

#define LTLOG_DEFINE_RECORD_SOFTWAREUPDATE(name) \
    static LTLogRecordSoftwareUpdate name;       \
    LTLOG_DEFINE_RECORD(name, kLTLogRecordType_SoftwareUpdate, &name, LTRecordSoftwareUpdateEncoder)

/** @} */

/******************************************************************************
 * @name Standard Record 4: Network Status
 * @{
 */

enum {
    kLTLogRecord_NetworkStatus_Type = 1,
    kLTLogRecord_NetworkStatus_Channel,
    kLTLogRecord_NetworkStatus_RSSI,
    kLTLogRecord_NetworkStatus_SNR,
    kLTLogRecord_NetworkStatus_MCS,
    kLTLogRecord_NetworkStatus_Samples,
    kLTLogRecord_NetworkStatus_JoinSuccess,
    kLTLogRecord_NetworkStatus_JoinFail,
    kLTLogRecord_NetworkStatus_Disconnects,
    kLTLogRecord_NetworkStatus_TXTotal,
    kLTLogRecord_NetworkStatus_TXRetry,
    kLTLogRecord_NetworkStatus_TXDrop,
    kLTLogRecord_NetworkStatus_RXTotal,

    kLTLogRecord_NetworkStatus_FieldCount,
};

typedef enum {
    kLTLogRecord_NetworkStatus_Type_WiFi = 1,
    kLTLogRecord_NetworkStatus_Type_Ethernet
} LTLogRecordNetworkStatusType;

typedef enum {
    kLTLogRecord_MCSIndex_Count
} LTLogRecordMCSIndex;

typedef struct {
    s8 min;
    s8 max;
    s8 average;
    s8 latest;
} LTLogRecordStats;

typedef struct {
    LTLogRecordNetworkStatusType type;    ///< Connection type
    u8 channel;                           ///< Current channel
    LTLogRecordStats rssiStats;           ///< Historical statistics of RSSI
    LTLogRecordStats snrStats;            ///< Historical statistics of SNR
    u8 mcs[kLTLogRecord_MCSIndex_Count];  ///< MCS histogram
    u32 sampleCount;                      ///< Total number of samples taken
    u32 joinSuccess;                      ///< Count of successful associations
    u32 joinFail;                         ///< Count of failed association attempts
    u32 disconnects;                      ///< Count of disconnects after association
    u64 txTotal;                          ///< Total TX frames sent
    u64 txRetry;                          ///< Total TX frames retried
    u64 txDrop;                           ///< Total TX frames dropped
    u64 rxTotal;                          ///< Total RX frames received
} LTLogRecordNetworkStatus;

LT_INLINE bool LTRecordNetworkStatusEncoder(const LTLogRecord *record,
                                            LTUtilityMessagePack *mp,
                                            LTMessagePack_Obj *writer) {
    LTLogRecordNetworkStatus *data = (LTLogRecordNetworkStatus *)record->payload;
    mp->PutMap(writer, kLTLogRecord_NetworkStatus_FieldCount - 1);
    LTLogRecordPutPair(kLTLogRecord_NetworkStatus_Type, (u8)data->type);
    LTLogRecordPutPair(kLTLogRecord_NetworkStatus_Channel, data->channel);
    LTMessagePackPut(mp, writer, (u8)kLTLogRecord_NetworkStatus_RSSI);
    mp->PutArray(writer, 4);
    LTMessagePackPut(mp, writer, data->rssiStats.min);
    LTMessagePackPut(mp, writer, data->rssiStats.max);
    LTMessagePackPut(mp, writer, data->rssiStats.average);
    LTMessagePackPut(mp, writer, data->rssiStats.latest);
    LTMessagePackPut(mp, writer, (u8)kLTLogRecord_NetworkStatus_SNR);
    mp->PutArray(writer, 4);
    LTMessagePackPut(mp, writer, data->snrStats.min);
    LTMessagePackPut(mp, writer, data->snrStats.max);
    LTMessagePackPut(mp, writer, data->snrStats.average);
    LTMessagePackPut(mp, writer, data->snrStats.latest);
    LTMessagePackPut(mp, writer, (u8)kLTLogRecord_NetworkStatus_MCS);
    mp->PutMap(writer, 0);
    LTLogRecordPutPair(kLTLogRecord_NetworkStatus_Samples, data->sampleCount);
    LTLogRecordPutPair(kLTLogRecord_NetworkStatus_JoinSuccess, data->joinSuccess);
    LTLogRecordPutPair(kLTLogRecord_NetworkStatus_JoinFail, data->joinFail);
    LTLogRecordPutPair(kLTLogRecord_NetworkStatus_Disconnects, data->disconnects);
    LTLogRecordPutPair(kLTLogRecord_NetworkStatus_TXTotal, data->txTotal);
    LTLogRecordPutPair(kLTLogRecord_NetworkStatus_TXRetry, data->txRetry);
    LTLogRecordPutPair(kLTLogRecord_NetworkStatus_TXDrop, data->txDrop);
    LTLogRecordPutPair(kLTLogRecord_NetworkStatus_RXTotal, data->rxTotal);

    return true;
}

#define LTLOG_DEFINE_RECORD_NETWORKSTATUS(name) \
    static LTLogRecordNetworkStatus name;       \
    LTLOG_DEFINE_RECORD(name, kLTLogRecordType_NetworkStatus, &name, LTRecordNetworkStatusEncoder)

/** @} */

/******************************************************************************
 * @name Standard Record 5: Events
 * @{
 */

enum {
    kLTLogRecord_Events_History = 1,
    kLTLogRecord_Events_FieldCount,

    kLTLogRecord_Events_History_Length = 10
};

typedef struct {
    LT_SIZE Count;
    struct {
        LTTime When;
        u64 Id;
        u8 Source;
        u8 Type;
    } Entries[kLTLogRecord_Events_History_Length];
} LTLogRecordEvents;

LT_INLINE bool LTRecordEventsEncoder(const LTLogRecord *record,
                                     LTUtilityMessagePack *mp,
                                     LTMessagePack_Obj *writer) {
    LTLogRecordEvents *data = (LTLogRecordEvents *)record->payload;
    mp->PutMap(writer, kLTLogRecord_Events_FieldCount - 1);
    LTMessagePackPut(mp, writer, (u8)kLTLogRecord_Events_History);
    mp->PutArray(writer, data->Count);
    for (LT_SIZE i = 0; i < data->Count; ++i) {
        mp->PutArray(writer, 4);
        LTMessagePackPut(mp, writer, data->Entries[i].When.nNanoseconds);
        LTMessagePackPut(mp, writer, data->Entries[i].Id);
        LTMessagePackPut(mp, writer, data->Entries[i].Source);
        LTMessagePackPut(mp, writer, data->Entries[i].Type);
    }
    return true;
}

#define LTLOG_DEFINE_RECORD_EVENTS(name) \
    static LTLogRecordEvents name;       \
    LTLOG_DEFINE_RECORD(name, kLTLogRecordType_Events, &name, LTRecordEventsEncoder)

/** @} */

/******************************************************************************
 * @name Standard Record 6: System
 * @{
 */

enum {
    kLTLogRecord_System_IdleTime = 1,
    kLTLogRecord_System_SeepTime,
    kLTLogRecord_System_Memory,
    kLTLogRecord_System_Temp,
    kLTLogRecord_System_FieldCount
};

typedef struct {
    float IdleTime;
    float SleepTime;
    float Memory;
    float Temp;
} LTLogRecordSystem;

LT_INLINE bool LTRecordSystemEncoder(const LTLogRecord *record,
                                     LTUtilityMessagePack *mp,
                                     LTMessagePack_Obj *writer) {
    LTLogRecordSystem *data = (LTLogRecordSystem *)record->payload;
    mp->PutMap(writer, kLTLogRecord_System_FieldCount - 1);
    LTLogRecordPutPair(kLTLogRecord_System_IdleTime, data->IdleTime);
    LTLogRecordPutPair(kLTLogRecord_System_SeepTime, data->SleepTime);
    LTLogRecordPutPair(kLTLogRecord_System_Memory, data->Memory);
    LTLogRecordPutPair(kLTLogRecord_System_Temp, data->Temp);
    return true;
}

#define LTLOG_DEFINE_RECORD_SYSTEM(name) \
    static LTLogRecordSystem name;       \
    LTLOG_DEFINE_RECORD(name, kLTLogRecordType_System, &name, LTRecordSystemEncoder)

/** @} */

/******************************************************************************
 * @name Standard Record 7: Battery
 * @{
 */

enum {
    kLTLogRecord_Battery_Level = 1,
    kLTLogRecord_Battery_Voltage,
    kLTLogRecord_Battery_FieldCount
};

typedef struct {
    float Level;
    float Voltage;
} LTLogRecordBattery;

LT_INLINE bool LTRecordBatteryEncoder(const LTLogRecord *record,
                                      LTUtilityMessagePack *mp,
                                      LTMessagePack_Obj *writer) {
    LTLogRecordBattery *data = (LTLogRecordBattery *)record->payload;
    mp->PutMap(writer, kLTLogRecord_Battery_FieldCount - 1);
    LTLogRecordPutPair(kLTLogRecord_Battery_Level, data->Level);
    LTLogRecordPutPair(kLTLogRecord_Battery_Voltage, data->Voltage);
    return true;
}

#define LTLOG_DEFINE_RECORD_BATTERY(name) \
    static LTLogRecordBattery name;       \
    LTLOG_DEFINE_RECORD(name, kLTLogRecordType_Battery, &name, LTRecordBatteryEncoder)

/** @} */

/******************************************************************************
 * @name Standard Record 9: Media Upload
 * @{
 */

enum {
    kLTLogRecord_MediaUpload_Type = 1,
    kLTLogRecord_MediaUpload_ID,
    kLTLogRecord_MediaUpload_ReqID,
    kLTLogRecord_MediaUpload_Success,
    kLTLogRecord_MediaUpload_Status,
    kLTLogRecord_MediaUpload_Latency,
    kLTLogRecord_MediaUpload_FieldCount
};

typedef enum {
    kLTLogRecord_MediaUpload_Type_Image = 1,
    kLTLogRecord_MediaUpload_Type_Video,
} LTLogRecordMediaUploadType;

typedef struct {
    LTLogRecordMediaUploadType type;
    const char* id;
    const char* reqid;
    bool success;
    u32 statusCode;
    s64 latency;
} LTLogRecordMediaUpload;

LT_INLINE bool LTRecordMediaUploadEncoder(const LTLogRecord *record,
                                          LTUtilityMessagePack *mp,
                                          LTMessagePack_Obj *writer) {
    LTLogRecordMediaUpload *data = (LTLogRecordMediaUpload *)record->payload;
    mp->PutMap(writer, kLTLogRecord_MediaUpload_FieldCount - 1);
    LTLogRecordPutPair(kLTLogRecord_MediaUpload_Type, (u8)data->type);
    LTLogRecordPutPair(kLTLogRecord_MediaUpload_ID, data->id);
    LTLogRecordPutPair(kLTLogRecord_MediaUpload_ReqID, data->reqid);
    LTLogRecordPutPair(kLTLogRecord_MediaUpload_Success, data->success);
    LTLogRecordPutPair(kLTLogRecord_MediaUpload_Status, data->statusCode);
    LTLogRecordPutPair(kLTLogRecord_MediaUpload_Latency, data->latency);
    return true;
}

#define LTLOG_DEFINE_RECORD_MEDIAUPLOAD(name) \
    static LTLogRecordMediaUpload name;       \
    LTLOG_DEFINE_RECORD(name, kLTLogRecordType_MediaUpload, &name, LTRecordMediaUploadEncoder)

/** @} */

/******************************************************************************
 * @name Standard Record 10: WiFi AP Info
 * @{
 */

enum {
    // From LTWiFi_ApInfo
    kLTLogRecord_ApInfo_ssid = 1,
    kLTLogRecord_ApInfo_security,
    kLTLogRecord_ApInfo_oui,
    kLTLogRecord_ApInfo_hash,
    kLTLogRecord_ApInfo_channel,
    kLTLogRecord_ApInfo_bandwidth,
    kLTLogRecord_ApInfo_rssi,
    kLTLogRecord_ApInfo_snr,

    kLTLogRecord_ApInfo_ht_streams,  // wlan.ht.mcsset.rxbitmask - counting #bitmasks != 0x00
    kLTLogRecord_ApInfo_ht_bw,       // wlan.ht.info.chanwidth
    kLTLogRecord_ApInfo_ht_ldpc,     // wlan.ht.capabilities.ldpccoding
    kLTLogRecord_ApInfo_ht_txbf,     // wlan.txbf.txbf
    kLTLogRecord_ApInfo_ht_txstbc,   // wlan.ht.capabilities.txstbc
    kLTLogRecord_ApInfo_ht_rxstbc,   // wlan.ht.capabilities.rxstbc
    kLTLogRecord_ApInfo_ht_en,       // "1" if ht_streams > 0
    kLTLogRecord_ApInfo_vht_streams, // wlan.vht.mcsset.rxmcsmap - counting #bitmasks != 0x3
    kLTLogRecord_ApInfo_vht_bw,      // wlan.vht.op.channelwidth, wlan.vht.op.channelcenter1 and wlan.ht.info.chanwidth
    kLTLogRecord_ApInfo_vht_ldpc,    // wlan.vht.capabilities.rxldpc
    kLTLogRecord_ApInfo_vht_txbf,    // wlan.vht.capabilities.subeamformer or mubeamformer
    kLTLogRecord_ApInfo_vht_txstbc,  // wlan.vht.capabilities.txstbc
    kLTLogRecord_ApInfo_vht_rxstbc,  // wlan.vht.capabilities.rxstbc
    kLTLogRecord_ApInfo_vht_en,      // "1" if vht_streams > 0
    kLTLogRecord_ApInfo_he_streams,  // wlan.ext_tag.he_mcs_map.rx_he_mcs_map_lte_80 - counting #bitmasks != 0x3
    kLTLogRecord_ApInfo_he_bw,       // wlan.ext_tag.he_phy_cap.fbytes
    kLTLogRecord_ApInfo_he_ldpc,     // wlan.ext_tag.he_phy_cap.ldpc_coding_in_payload
    kLTLogRecord_ApInfo_he_txbf,     // wlan.ext_tag.he_phy_cap.su_beamformer or mu_beamformer
    kLTLogRecord_ApInfo_he_txstbc_lt80,      // wlan.ext_tag.he_phy_cap.stbc_tx_lt_80mhz
    kLTLogRecord_ApInfo_he_rxstbc_lt80,      // wlan.ext_tag.he_phy_cap.stbc_rx_lt_80mhz
    kLTLogRecord_ApInfo_he_txstbc_gt80,      // wlan.ext_tag.he_phy_cap.stbc_tx_gt_80_mhz
    kLTLogRecord_ApInfo_he_rxstbc_gt80,      // wlan.ext_tag.he_phy_cap.stbc_rx_gt_80_mhz
    kLTLogRecord_ApInfo_he_en,       // "1" if he_streams > 0
    kLTLogRecord_ApInfo_he_mumimo_ul_fullbw, // wlan.ext_tag.he_phy_cap.full_bw_ul_mu_mimo
    kLTLogRecord_ApInfo_he_mumimo_ul_partbw, // wlan.ext_tag.he_phy_cap.partial_bw_ul_mu_mimo
    kLTLogRecord_ApInfo_he_mumimo_dl_partbw, // wlan.ext_tag.he_phy_cap.partial_bw_dl_mu_mimo
    kLTLogRecord_ApInfo_he_tx1024qam, // wlan.ext_tag.he_phy_cap.tx_1024_qam_support_lt_242_tone_ru
    kLTLogRecord_ApInfo_he_rx1024qam, // wlan.ext_tag.he_phy_cap.rx_1024_qam_support_lt_242_tone_ru
    kLTLogRecord_ApInfo_he_ersu_dis,  // wlan.ext_tag.he_operation.er_su_disable
    kLTLogRecord_ApInfo_he_wifi6e,    // wlan.tag.he_6ghz.cap_inf or wlan.rnr.tbtt_info.operating_class in 131-136 range
    kLTLogRecord_ApInfo_he_wifi6e_chan,// wlan.rnr.tbtt_info.channel_num
    kLTLogRecord_ApInfo_bss_trans,    // wlan.extcap.b19 - Extended capabilities BSS Transition flag
    kLTLogRecord_ApInfo_time_measure, // wlan.extcap.b23 - Extended capabilities Time Measurement flag
    kLTLogRecord_ApInfo_mesh_id,      // wlan.mesh.id
    kLTLogRecord_ApInfo_mesh_con_gate,// wlan.mesh.formation_info.connect_to_mesh_gate
    kLTLogRecord_ApInfo_mesh_peers,   // wlan.mesh.config.formation_info.num_peers
    kLTLogRecord_ApInfo_mesh_fw,      // wlan.mesh.config.cap.forwarding
    kLTLogRecord_ApInfo_manuf,        // wps.manufacturer
    kLTLogRecord_ApInfo_model,        // wps.model_name
    kLTLogRecord_ApInfo_modelno,      // wps.model_number
    kLTLogRecord_ApInfo_rf_bands,     // wps.rf_bands
    kLTLogRecord_ApInfo_chipguess,    // Qualcomm/Atheros/Broadcom/Mediatek/Ralink/Marvell/Quantenna/Realtek
    kLTLogRecord_ApInfo_wpa3,         // wlan.rsn.akms.type == 8 or 5
    kLTLogRecord_ApInfo_utilpct,      // wlan.qbss.cu / 2.55

    kLTLogRecord_ApInfo_FieldCount,
};

typedef struct {
    // From LTWiFi_ApInfo
    char ssid[34];
    u8 security;
    char oui[18];
    u32 hash;
    u8 channel;
    u8 bandwidth;
    s8 rssi;
    u8 snr;

    // From IE vector
    u8 ht_streams;
    u8 ht_bw;
    bool ht_ldpc;
    bool ht_txbf;
    u8 ht_txstbc;
    u8 ht_rxstbc;
    bool ht_en;
    u8 vht_streams;
    u8 vht_bw;
    bool vht_ldpc;
    bool vht_txbf;
    u8 vht_txstbc;
    u8 vht_rxstbc;
    bool vht_en;
    u8 he_streams;
    u8 he_bw;
    bool he_ldpc;
    bool he_txbf;
    u8 he_txstbc_lt80;
    u8 he_rxstbc_lt80;
    u8 he_txstbc_gt80;
    u8 he_rxstbc_gt80;
    bool he_en;
    bool he_mumimo_ul_fullbw;
    bool he_mumimo_ul_partbw;
    bool he_mumimo_dl_partbw;
    bool he_tx1024qam;
    bool he_rx1024qam;
    bool he_ersu_dis;
    bool he_wifi6e;
    u8 he_wifi6e_chan;
    bool bss_trans;
    bool time_measure;
    char mesh_id[33];
    bool mesh_con_gate;
    u8 mesh_peers;
    bool mesh_fw;
    char manuf[65];
    char model[33];
    char modelno[33];
    u8 rf_bands;
    char chipguess[16];
    bool wpa3;
    u8 utilpct;
} LTLogRecordApInfo;

LT_INLINE bool LTRecordApInfoEncoder(const LTLogRecord *record,
                                     LTUtilityMessagePack *mp,
                                     LTMessagePack_Obj *writer) {
    LTLogRecordApInfo *data = (LTLogRecordApInfo *)record->payload;
    mp->PutMap(writer, kLTLogRecord_ApInfo_FieldCount - 1);

    // From LTWiFi_ApInfo
    LTLogRecordPutPair(kLTLogRecord_ApInfo_ssid, data->ssid);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_security, data->security);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_oui, data->oui);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_hash, data->hash);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_channel, data->channel);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_bandwidth, data->bandwidth);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_rssi, data->rssi);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_snr, data->snr);

    // From IE vector
    LTLogRecordPutPair(kLTLogRecord_ApInfo_ht_streams, data->ht_streams);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_ht_bw, data->ht_bw);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_ht_ldpc, data->ht_ldpc);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_ht_txbf, data->ht_txbf);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_ht_txstbc, data->ht_txstbc);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_ht_rxstbc, data->ht_rxstbc);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_ht_en, data->ht_en);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_vht_streams, data->vht_streams);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_vht_bw, data->vht_bw);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_vht_ldpc, data->vht_ldpc);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_vht_txbf, data->vht_txbf);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_vht_txstbc, data->vht_txstbc);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_vht_rxstbc, data->vht_rxstbc);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_vht_en, data->vht_en);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_streams, data->he_streams);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_bw, data->he_bw);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_ldpc, data->he_ldpc);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_txbf, data->he_txbf);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_txstbc_lt80, data->he_txstbc_lt80);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_rxstbc_lt80, data->he_rxstbc_lt80);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_txstbc_gt80, data->he_txstbc_gt80);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_rxstbc_gt80, data->he_rxstbc_gt80);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_en, data->he_en);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_mumimo_ul_fullbw, data->he_mumimo_ul_fullbw);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_mumimo_ul_partbw, data->he_mumimo_ul_partbw);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_mumimo_dl_partbw, data->he_mumimo_dl_partbw);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_tx1024qam, data->he_tx1024qam);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_rx1024qam, data->he_rx1024qam);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_ersu_dis, data->he_ersu_dis);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_wifi6e, data->he_wifi6e);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_he_wifi6e_chan, data->he_wifi6e_chan);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_bss_trans, data->bss_trans);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_time_measure, data->time_measure);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_mesh_id, data->mesh_id);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_mesh_con_gate, data->mesh_con_gate);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_mesh_peers, data->mesh_peers);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_mesh_fw, data->mesh_fw);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_manuf, data->manuf);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_model, data->model);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_modelno, data->modelno);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_rf_bands, data->rf_bands);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_chipguess, data->chipguess);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_wpa3, data->wpa3);
    LTLogRecordPutPair(kLTLogRecord_ApInfo_wpa3, data->utilpct);

    return true;
}

#define LTLOG_DEFINE_RECORD_APINFO(name) \
    static LTLogRecordApInfo name;       \
    LTLOG_DEFINE_RECORD(name, kLTLogRecordType_ApInfo, &name, LTRecordApInfoEncoder)

/** @} */

#undef LTLogRecordPutPair

#endif
