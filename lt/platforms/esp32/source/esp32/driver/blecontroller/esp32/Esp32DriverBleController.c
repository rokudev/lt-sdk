/******************************************************************************
 * platforms/esp32/source/esp32/driver/blecontroller/Esp32DriverBleController.c
 *
 * Copyright 2019 Espressif Systems (Shanghai) PTE LTD
 *
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
 *
 * Modified by Roku, Inc. Please see changelog below for more information.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *****************************************************************************/

#include <lt/core/LTCore.h>
#include "Esp32DriverBleController.h"

DEFINE_LTLOG_SECTION("Esp32.Ble.Controller");

/****************************************************************************
 * Static variables
 ****************************************************************************/
/* number of fractional LT_ESP32_BIT for g_btdm_lpcycle_us */
static DRAM_ATTR u8 g_btdm_lpcycle_us_frac = 0;
/* measured average low power clock period in micro seconds */
static DRAM_ATTR u32 g_btdm_lpcycle_us = 0;
/* Used to sync controller wakeup request  */
static void *pWakeupReqSem = NULL;
/* stores last known controller status */
DRAM_ATTR esp_bt_controller_status_t btdm_controller_status = ESP_BT_CONTROLLER_STATUS_IDLE;
/* placed in specific memory region, defined in esp32 bluetooth documentation */
static const btdm_dram_available_region_t btdm_dram_available_region[] = {
    //following is .data
    { SOC_MEM_BT_DATA_START,     SOC_MEM_BT_DATA_END     },
    //following is memory which HW will use
    { SOC_MEM_BT_EM_BTDM0_START, SOC_MEM_BT_EM_BTDM0_END },
    { SOC_MEM_BT_EM_BLE_START,   SOC_MEM_BT_EM_BLE_END   },
    { SOC_MEM_BT_EM_BTDM1_START, SOC_MEM_BT_EM_BTDM1_END },
    //following is .bss
    { SOC_MEM_BT_BSS_START,      SOC_MEM_BT_BSS_END      },
    { SOC_MEM_BT_MISC_START,     SOC_MEM_BT_MISC_END     },
};
/* stores last wifi power save mode */
static wifi_ps_type_t wifi_ps_mode = WIFI_PS_MIN_MODEM;

static void Esp32DriverBleController_SendReady(void) {
    LTLOG_DEBUG("advertising", "send to controller ready");
}

static int Esp32DriverBleController_RecvReady(const u8 *data, u16 len) {
    if (!data) return -1;
    if (len < 2) return -1;
    /* Check second byte for HCI event. If event opcode is 0x0e, the event is
     * HCI Command Complete event. Since we have received "0x0e" event, we can
     * check for byte 4 for command opcode and byte 6 for it's return status. */
    if (data[1] == 0x0e) {
        if (len < 7) return -1;
        if (data[6] == 0) {
            LTLOG_DEBUG("RCV_PKT", "Event opcode 0x%02x success.", data[4]);
        }
        else {
            LTLOG_DEBUG("RCV_PKT", "Event opcode 0x%02x fail with reason: 0x%02x.", data[4], data[6]);
            return -1;
        }
    }
    else {
        LTLOG_DEBUG("RCV_PKT", "Len: %u", len);
    }

    return 0;
}

static LTBleController_VhciHostCallback s_vhciDefaultCallbacks = {
    .NotifySendReady = &Esp32DriverBleController_SendReady,
    .NotifyRecvReady = &Esp32DriverBleController_RecvReady,
};

/****************************************************************************
 * Static functions
 ****************************************************************************/
static void btdm_wakeup_request_callback(void *arg) {
    LT_UNUSED(arg);
    btdm_wakeup_request();
    if (pWakeupReqSem) {
        lt_sem_give(pWakeupReqSem);
    }
}

static bool async_wakeup_request(int event) {
    bool do_wakeup_request = false;
    switch (event) {
        case BTDM_ASYNC_WAKEUP_REQ_HCI:
            btdm_in_wakeup_requesting_set(true);
            /* Intentional fall-through */

        case BTDM_ASYNC_WAKEUP_REQ_CTRL_DISA:
            if (!btdm_power_state_active()) {
                do_wakeup_request = true;
                btdm_dispatch_work_to_controller(btdm_wakeup_request_callback, NULL, true);
                lt_sem_take(pWakeupReqSem, 0xffffffff);
            }
            break;
        case BTDM_ASYNC_WAKEUP_REQ_COEX:
            if (!btdm_power_state_active()) {
                do_wakeup_request = true;
                btdm_wakeup_request();
            }
            break;
        default:
            return false;
    }
    return do_wakeup_request;
}

static void async_wakeup_request_end(int event) {
    bool request_lock = false;
    switch (event) {
        case BTDM_ASYNC_WAKEUP_REQ_HCI:
            request_lock = true;
            break;
        case BTDM_ASYNC_WAKEUP_REQ_COEX:
        case BTDM_ASYNC_WAKEUP_REQ_CTRL_DISA:
            request_lock = false;
            break;
        default:
            return;
    }

    if (request_lock) {
        btdm_in_wakeup_requesting_set(false);
    }

    return;
}

bool coex_bt_wakeup_request(void)
{
    return async_wakeup_request(BTDM_ASYNC_WAKEUP_REQ_COEX);
}

void coex_bt_wakeup_request_end(void)
{
    async_wakeup_request_end(BTDM_ASYNC_WAKEUP_REQ_COEX);
}

static void btdm_controller_mem_init(void)  {
    int btdm_dram_regions;
    /* initialise .data section */
    lt_memcpy(&_data_start_btdm, (void *)_data_start_btdm_rom, &_data_end_btdm - &_data_start_btdm);

    btdm_dram_regions = sizeof(btdm_dram_available_region) / sizeof(btdm_dram_available_region_t);

    //initial em, .bss section
    for (int i = 1; i < btdm_dram_regions; i++) {
        lt_memset((void *)btdm_dram_available_region[i].start, 0x0,
            btdm_dram_available_region[i].end - btdm_dram_available_region[i].start);
    }
}

static u32 BtdmCfgMaskLoad(void) {
    u32 mask = 0x0;

#ifdef CONFIG_UART_BTH4
    mask |= BTDM_CFG_HCI_UART;
#endif

#ifdef CONFIG_ESP32_BLE_RUN_APP_CPU
    mask |= BTDM_CFG_CONTROLLER_RUN_APP_CPU;
#endif

#ifdef CONFIG_ESP32_BLE_FULL_SCAN
    mask |= BTDM_CFG_BLE_FULL_SCAN_SUPPORTED;
#endif

    mask |= BTDM_CFG_SCAN_DUPLICATE_OPTIONS;

    mask |= BTDM_CFG_SEND_ADV_RESERVED_SIZE;

    return mask;
}

static inline void esp_bt_power_domain_on(void) {
    // Bluetooth module power up
    ESP32_REG(DPORT_WIFI_CLK_EN) &= ~(LT_ESP32_RTC_CNTL_WIFI_FORCE_PD | LT_ESP32_RTC_CNTL_WIFI_FORCE_ISO);
}

static inline void esp_bt_power_domain_off(void) {
    // Bluetooth module power off
    ESP32_REG(DPORT_WIFI_CLK_EN) |= (LT_ESP32_RTC_CNTL_WIFI_FORCE_PD | LT_ESP32_RTC_CNTL_WIFI_FORCE_ISO);
}

static int esp32_bt_controller_init(void) {
    esp_bt_controller_config_t btCfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    int err;
    u32 btdmCfgMask = BtdmCfgMaskLoad();
    wifi_bt_coexist_init();
    lt_ble_drv_init();
    err = btdm_osi_funcs_register((struct osi_funcs_t *)&g_osi_funcs);
    if (err != 0) {
        LTLOG_REDALERT("controller_init", "Error, probably invalid OSI Functions, %d\n", err);
        return -1;
    }
    LTLOG_DEBUG("controller_init", "BT controller compile version [%s]\n", btdm_controller_get_compile_version());
    esp_bt_power_domain_on();
    btdm_controller_mem_init();
    pWakeupReqSem = lt_sem_create(1, 0);

    ESP32_REG(DPORT_WIFI_CLK_EN) |= kEsp32_RegisterDPORT_WIFI_CLK_BT_EN_M;

    /* set default sleep clock cycle and its fractional LT_ESP32_BITs */
    g_btdm_lpcycle_us_frac           = RTC_CLK_CAL_FRACT;
    g_btdm_lpcycle_us                = 2 << (g_btdm_lpcycle_us_frac);

    btCfg.controller_task_stack_size = ESP32_CONTROLLER_TASK_STACK;
    btCfg.magic                      = ESP_BT_CONTROLLER_CONFIG_MAGIC_VAL;
    btCfg.controller_task_prio       = ESP32_CONTROLLER_TASK_PRIORITY;
    btCfg.controller_debug_flag      = 0;
    btCfg.normal_adv_size            = 200;
    btCfg.bt_max_sync_conn           = 0;
    btCfg.mode                       = ESP_BT_MODE_BLE;

    if (btdm_controller_init(btdmCfgMask, &btCfg) != 0) {
        LT_ESP32_TR_FAIL;
        return -1;
    }
    LTLOG_DEBUG("controller_enable", "The ble controller initialized successfully");

    btdm_controller_status = ESP_BT_CONTROLLER_STATUS_INITED;

    return 0;
}
#if 0
static int esp32_bt_controller_deinit(void) {
    if (btdm_controller_status != ESP_BT_CONTROLLER_STATUS_INITED) {
        LTLOG_REDALERT("DriverBTControllerDeinit", "Controller was not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    btdm_controller_deinit();

    ESP32_REG(DPORT_WIFI_CLK_EN) &= ~kEsp32_RegisterDPORT_WIFI_CLK_BT_EN_M;
    lt_sem_delete(pWakeupReqSem);
    pWakeupReqSem = NULL;

    btdm_controller_status = ESP_BT_CONTROLLER_STATUS_IDLE;

    g_btdm_lpcycle_us = 0;
    btdm_controller_set_sleep_mode(BTDM_MODEM_SLEEP_MODE_NONE);
    esp_bt_power_domain_off();

    return 0;
}
static int esp32_bt_controller_disable(void) {
    if (btdm_controller_status != ESP_BT_CONTROLLER_STATUS_ENABLED) {
        return ESP_ERR_INVALID_STATE;
    }

    btdm_controller_disable();
    coex_disable();
    esp32_phy_disable();
    esp_wifi_set_ps(wifi_ps_mode);
    btdm_controller_status = ESP_BT_CONTROLLER_STATUS_INITED;

    return 0;
}
#endif
static int esp32_bt_controller_enable(void) {
    /* WiFi crashes if WIFI_PS_NONE is used together with Bluetooth */
    esp_wifi_get_ps(&wifi_ps_mode);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    esp32_phy_enable();
    coex_enable();
    btdm_rf_bb_init_phase2();
    if (btdm_controller_enable(ESP_BT_MODE_BLE) != 0) {
        LT_ESP32_TR_FAIL;
        return -1;
    }
    LTLOG_DEBUG("controller_enable", "The ble controller enabled successfully");

    btdm_controller_status = ESP_BT_CONTROLLER_STATUS_ENABLED;
    return 0;
}

static bool sControllerInitialized = false;
static bool Esp32DriverBleController_Enable(bool enable) {
    if (enable) {
        if (btdm_controller_status == ESP_BT_CONTROLLER_STATUS_ENABLED) {
            LTLOG_DEBUG("DriverBleControllerInit", "Controller already enabled");
            return true;
        }
        if (btdm_controller_status != ESP_BT_CONTROLLER_STATUS_IDLE) {
            LTLOG_REDALERT("DriverBleControllerInit", "Invalid controller status %lx", LT_Pu32(btdm_controller_status));
            return false;
        }

        if (!sControllerInitialized) {
            if(esp32_bt_controller_init() != 0) {
                LTLOG_REDALERT("DriverBleControllerInit", "Failed to initialize the controller");
                return false;
            }
            sControllerInitialized = true;
        }

        if (esp32_bt_controller_enable() != 0) {   // ESP_BT_MODE_BTDM
            LTLOG_REDALERT("DriverBleControllerEnable", "Failed to enable the controller");
            return false;
        }

    } else {
#if 0 /* LT cannot clearly shutdown BT thread, leave BT initialized forever as this is the only consistent state */
        if (esp32_bt_controller_disable() != 0) {
            LTLOG_REDALERT("DriverBleControllerDisable", "Failed to disable the controller");
            return false;
        }

        if (esp32_bt_controller_deinit() != 0) {
            LTLOG_REDALERT("DriverBleControllerDeinit", "Failed to deinit the controller");
            return false;
        }
#endif
    }
    return true;
}

static bool Esp32DriverBleController_RegisterCallbacks(const LTBleController_VhciHostCallback *vhciHostCallbacks) {
    return 0 == API_vhci_host_register_callback((const esp_vhci_host_callback_t *)(vhciHostCallbacks ? vhciHostCallbacks : &s_vhciDefaultCallbacks));
}

static bool Esp32DriverBleController_Send(const u8 *data, u16 len) {
    if (!API_vhci_host_check_send_available()) {
        LTLOG("chk.send", "Controller not ready to receive packets");
        return false;
    }
    async_wakeup_request(BTDM_ASYNC_WAKEUP_REQ_HCI);
    API_vhci_host_send_packet((u8 *)data, len);
    async_wakeup_request_end(BTDM_ASYNC_WAKEUP_REQ_HCI);
    return true;
}

static bool Esp32DriverBleController_GetConnectionInfo(u16 connHandle, LTBleConnectionInfo *info) {
    LT_UNUSED(connHandle); LT_UNUSED(info);
    return false;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static ILTBleController s_ILTBleController;

static u32 Esp32DriverBleControllerImpl_GetNumDeviceUnits(void) {
    return 1;
}

static LTDeviceUnit Esp32DriverBleControllerImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNum) {
    LT_UNUSED(nDeviceUnitNum);
    return LT_GetCore()->CreateHandle((LTInterface *)&s_ILTBleController, 1);
}

static bool Esp32DriverBleControllerImpl_LibInit(void) {
    LTEsp32OSAdapter_LibInit();
    return true;
}

static void Esp32DriverBleControllerImpl_LibFini(void) {
    return;
    LTEsp32OSAdapter_LibFini();
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/
define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceBleController, Esp32DriverBleController);

define_LTLIBRARY_INTERFACE(ILTBleController) {
    .Enable            = &Esp32DriverBleController_Enable,
    .RegisterCallbacks = &Esp32DriverBleController_RegisterCallbacks,
    .Send              = &Esp32DriverBleController_Send,
    .GetConnectionInfo = &Esp32DriverBleController_GetConnectionInfo,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(Esp32DriverBleController, (ILTBleController))

/**********************************************************************************
 *  LOG
 **********************************************************************************
 *  05-Aug-22   vespasian   created
 *  15-Aug-22   vespasian   Original code was modified and reorganized by Roku
 *  12-Sep-22   vespasian   Isolated controller driver functions
 *  20-Dec-22   gallienus   Restructured and cleaned
 */
