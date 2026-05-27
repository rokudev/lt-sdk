/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "host/ble_hs.h"
#include <lt/core/LTCore.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include "ble_store_config_priv.h"

DEFINE_LTLOG_SECTION("ble.pair");

static LTUtilityByteOps * s_pUtilityByteOps = NULL;
static LTSystemSettings * s_pSystemSettings = NULL;

static int
ble_store_config_conf_set(int argc, char **argv, char *val, uint32_t len);

#define BASE64_ENCODE_SIZE(input_len) ((input_len + 2) / 3 * 4)
#define BASE64_DECODE_SIZE(input_len) ((input_len + 3) / 4 * 3)

#define BLE_STORE_CONFIG_SEC_ENCODE_SZ      \
    BASE64_ENCODE_SIZE(sizeof (struct ble_store_value_sec))

#define BLE_STORE_CONFIG_SEC_SET_ENCODE_SZ  \
    (MYNEWT_VAL(BLE_STORE_MAX_BONDS) * BLE_STORE_CONFIG_SEC_ENCODE_SZ + 1)

#define BLE_STORE_CONFIG_CCCD_ENCODE_SZ     \
    BASE64_ENCODE_SIZE(sizeof (struct ble_store_value_cccd))

#define BLE_STORE_CONFIG_CCCD_SET_ENCODE_SZ \
    (MYNEWT_VAL(BLE_STORE_MAX_CCCDS) * BLE_STORE_CONFIG_CCCD_ENCODE_SZ + 1)

static int
ble_store_config_deserialize_arr(const char *enc,
                                 int enc_len,
                                 void *out_arr,
                                 int obj_sz,
                                 int max_objs,
                                 int *out_num_objs)
{
    u32 len = 0;
    u32 max_decode_sz = (u32)max_objs * (u32)obj_sz;
    // Decode directly into the target array, clamped to its capacity.
    // And bound the output with max_decode_sz.
    len = s_pUtilityByteOps->Base64Decode(enc, enc_len, out_arr, max_decode_sz);
    if (!len) {
        *out_num_objs = 0;
        return OS_EINVAL;
    }
    *out_num_objs = len / obj_sz;
    if (*out_num_objs > max_objs) {
        *out_num_objs = max_objs;
    }
    return 0;
}

static int
ble_store_config_conf_set(int argc, char **argv, char *val, uint32_t len)
{
    int rc;

    if (argc == 1) {
        if (strcmp(argv[0], "our_sec") == 0) {
            rc = ble_store_config_deserialize_arr(
                    val,
                    len,
                    ble_store_config_our_secs,
                    sizeof *ble_store_config_our_secs,
                    MYNEWT_VAL(BLE_STORE_MAX_BONDS),
                    &ble_store_config_num_our_secs);
            return rc;
        } else if (strcmp(argv[0], "peer_sec") == 0) {
            rc = ble_store_config_deserialize_arr(
                    val,
                    len,
                    ble_store_config_peer_secs,
                    sizeof *ble_store_config_peer_secs,
                    MYNEWT_VAL(BLE_STORE_MAX_BONDS),
                    &ble_store_config_num_peer_secs);
            return rc;
        } else if (strcmp(argv[0], "cccd") == 0) {
            rc = ble_store_config_deserialize_arr(
                    val,
                    len,
                    ble_store_config_cccds,
                    sizeof *ble_store_config_cccds,
                    MYNEWT_VAL(BLE_STORE_MAX_CCCDS),
                    &ble_store_config_num_cccds);
            return rc;
        }
    }
    return OS_ENOENT;
}

static int
ble_store_config_persist_sec_set(const char *setting_name,
                                 const struct ble_store_value_sec *secs,
                                 int num_secs)
{
    char buf[BLE_STORE_CONFIG_SEC_SET_ENCODE_SZ];
    bool bRet;
    int sec_size;
    sec_size = sizeof(*secs) * num_secs;
    assert(sec_size <= BLE_STORE_CONFIG_SEC_SET_ENCODE_SZ);

    if (sec_size) {
        uint32_t output_len = s_pUtilityByteOps->Base64Encode((u8*)secs, sec_size, buf, BLE_STORE_CONFIG_SEC_SET_ENCODE_SZ);
        LTLOG("sec.set", "Base64Encoded %s: %s, len %ld", setting_name, buf, output_len);
        bRet = s_pSystemSettings->SetBinaryValue(setting_name, (u8*)buf, output_len);
    } else {
        LTLOG("sec.delete", "Delete %s setting", setting_name);
        bRet = s_pSystemSettings->DeleteSetting(setting_name);
    }

    if (!bRet) {
        LTLOG("sec.setting.fail", "%d", bRet);
        return BLE_HS_ESTORE_FAIL;
    }

    return 0;
}

int
ble_store_config_persist_our_secs(void)
{
    int rc;

    rc = ble_store_config_persist_sec_set("ble_hs/our_sec",
                                          ble_store_config_our_secs,
                                          ble_store_config_num_our_secs);
    return rc;
}

int
ble_store_config_persist_peer_secs(void)
{
    int rc;

    rc = ble_store_config_persist_sec_set("ble_hs/peer_sec",
                                          ble_store_config_peer_secs,
                                          ble_store_config_num_peer_secs);
    return rc;
}

int
ble_store_config_persist_cccds(void)
{
    char buf[BLE_STORE_CONFIG_CCCD_SET_ENCODE_SZ];
    bool bRet;
    int cccd_size;
    cccd_size = sizeof *ble_store_config_cccds * ble_store_config_num_cccds;
    assert(cccd_size <= BLE_STORE_CONFIG_CCCD_SET_ENCODE_SZ);

    if (cccd_size) {
        uint32_t output_len = s_pUtilityByteOps->Base64Encode((u8*)ble_store_config_cccds, cccd_size, buf, BLE_STORE_CONFIG_CCCD_SET_ENCODE_SZ);
        LTLOG("cccds.set", "Base64Encoded ble_hs/cccd: %s, len %ld", buf, output_len);
        bRet = s_pSystemSettings->SetBinaryValue("ble_hs/cccd", (u8*)buf, output_len);
    } else {
        LTLOG("cccds.delete", "Delete ble_hs/cccd setting");
        bRet = s_pSystemSettings->DeleteSetting("ble_hs/cccd");
    }

    if (!bRet) {
        LTLOG("cccds.setting.fail", "%d", bRet);
        return BLE_HS_ESTORE_FAIL;
    }

    return 0;
}

void
ble_store_config_conf_init(void)
{
    int rc = OS_OK;
    bool bRet = true;
    u8 buf_sec[BLE_STORE_CONFIG_SEC_SET_ENCODE_SZ] = {0};
    u8 buf_cccd[BLE_STORE_CONFIG_CCCD_SET_ENCODE_SZ] = {0};
    u32 valueSize = 0;
    const char *errorStr = NULL;
    char *setting_name[] = {"our_sec", "peer_sec", "cccd"};

    s_pUtilityByteOps = lt_openlibrary(LTUtilityByteOps);
    if (!s_pUtilityByteOps) {
        LTLOG("library.open.fail.util", "Unable to open LTUtilityByteOps library");
        return;
    }

    s_pSystemSettings = lt_openlibrary(LTSystemSettings);
    if (!s_pSystemSettings) {
        if (s_pUtilityByteOps)      { lt_closelibrary(s_pUtilityByteOps);    s_pUtilityByteOps = NULL; }
        LTLOG("library.open.fail.settings", "Unable to open LTSystemSettings library");
        return;
    }

    do {
        // Get the length of the ble_hs/our_sec setting
        // NOTE: Pass NULL buffer and valueSize=0 so GetBinaryValue only returns
        // the data size without writing.
        valueSize = 0;
        s_pSystemSettings->GetBinaryValue("ble_hs/our_sec", NULL, &valueSize);
        if (valueSize) {
            if (valueSize > BLE_STORE_CONFIG_SEC_SET_ENCODE_SZ) { errorStr = "our_sec bad get size"; break; }
            // Read and save the setting for Nimble stack
            bRet = s_pSystemSettings->GetBinaryValue("ble_hs/our_sec", buf_sec, &valueSize);
            if (!bRet) { errorStr = "our_sec bad get"; break; }
            LTLOG("init.our.sec", "Base64Encoded ble_hs/our_sec: %s, len %ld", buf_sec, valueSize);
            rc = ble_store_config_conf_set(1, &setting_name[0], (char*)buf_sec, valueSize);
            if (rc != 0) { errorStr = "our_sec bad set"; break; }
        }

        // Get the length of the peer_sec setting
        valueSize = 0;
        s_pSystemSettings->GetBinaryValue("ble_hs/peer_sec", NULL, &valueSize);
        if (valueSize) {
            if (valueSize > BLE_STORE_CONFIG_SEC_SET_ENCODE_SZ) { errorStr = "peer_sec bad get size"; break; }
            // Read and save the setting for Nimble stack
            bRet = s_pSystemSettings->GetBinaryValue("ble_hs/peer_sec", buf_sec, &valueSize);
            if (!bRet) { errorStr = "peer_sec bad get"; break; }
            LTLOG("init.peer.sec", "Base64Encoded ble_hs/peer_sec: %s, len %ld", buf_sec, valueSize);
            rc = ble_store_config_conf_set(1, &setting_name[1], (char*)buf_sec, valueSize);
            if (rc != 0) { errorStr = "peer_sec bad set"; break; }
        }

        // Get the length of the cccd setting
        valueSize = 0;
        s_pSystemSettings->GetBinaryValue("ble_hs/cccd", NULL, &valueSize);
        if (valueSize) {
            if (valueSize > BLE_STORE_CONFIG_CCCD_SET_ENCODE_SZ) { errorStr = "cccd bad get size"; break; }
            // Read and save the setting for Nimble stack
            bRet = s_pSystemSettings->GetBinaryValue("ble_hs/cccd", buf_cccd, &valueSize);
            if (!bRet) { errorStr = "cccd bad get"; break; }
            LTLOG("init.cccds", "Base64Encoded ble_hs/cccd: %s, len %ld", buf_cccd, valueSize);
            rc = ble_store_config_conf_set(1, &setting_name[2], (char*)buf_cccd, valueSize);
            if (rc != 0) { errorStr = "cccd bad set"; break; }
        }
    } while (false);

    if (!bRet || rc != 0 || errorStr != NULL) {
        LTLOG("init.err", "%s", errorStr);
        if (s_pUtilityByteOps)      { lt_closelibrary(s_pUtilityByteOps);    s_pUtilityByteOps = NULL; }
        if (s_pSystemSettings)      { lt_closelibrary(s_pSystemSettings);    s_pSystemSettings = NULL; }
    }
}
