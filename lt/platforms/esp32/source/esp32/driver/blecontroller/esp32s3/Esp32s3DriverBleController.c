/******************************************************************************
 * platforms/esp32/source/esp32/driver/blecontroller/esp32s3/Esp32s3DriverBleController.c
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

/*
 * Modelled on ESP-IDF v4.4 components/bt/controller/esp32c3/bt.c, which is the
 * controller port IDF builds for the esp32s3 as well.  It is not a variant of
 * the esp32 driver next door: btdm_controller_init() takes one argument instead
 * of two, the DRAM region table and btdm_rf_bb_init_phase2() are gone, memory
 * init is a single ROM call, and coex_pti_v2() is new.
 *
 * Controller sleep is not enabled, so the sleep and MAC/BB power-down entries in
 * the OSI table are inert stubs.
 */

#include <lt/core/LTCore.h>
#include "Esp32s3DriverBleController.h"

DEFINE_LTLOG_SECTION("esp32s3.Ble.Controller");

/****************************************************************************
 * Static variables
 ****************************************************************************/
/* number of fractional bits for g_btdm_lpcycle_us */
static u8 g_btdm_lpcycle_us_frac = 0;
/* measured average low power clock period in micro seconds */
static u32 g_btdm_lpcycle_us = 0;
/* Used to sync controller wakeup request */
static void *pWakeupReqSem = NULL;
/* stores last known controller status */
static esp_bt_controller_status_t btdm_controller_status = ESP_BT_CONTROLLER_STATUS_IDLE;
/* stores last wifi power save mode */
static wifi_ps_type_t wifi_ps_mode = WIFI_PS_MIN_MODEM;
/* LT primitives the OSI table is built from, see Esp32_LTOSAdapterOsi.h */
static const Esp32OSAdapterPrimitives *s_os = NULL;

static void Esp32s3DriverBleController_SendReady(void) {
    LTLOG_DEBUG("advertising", "send to controller ready");
}

static int Esp32s3DriverBleController_RecvReady(const u8 *data, u16 len) {
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
    .NotifySendReady = &Esp32s3DriverBleController_SendReady,
    .NotifyRecvReady = &Esp32s3DriverBleController_RecvReady,
};

/****************************************************************************
 * Interrupt plumbing
 *
 * The controller asks for its own interrupt lines at runtime, so nothing here
 * hardcodes a source or a CPU line.  LTCore's handlers take no argument, so the
 * controller's (fn, arg) pair is parked in a small table and reached through a
 * per-line stub, the same trick the esp32 adapter uses.
 ****************************************************************************/
typedef struct BleIrqSlot {
    void (*pUserHandler)(void *arg);
    void *pArg;
    s32   nCpuIrq;
} BleIrqSlot;

#define BLE_IRQ_SLOTS   (4)
static BleIrqSlot s_bleIrqSlots[BLE_IRQ_SLOTS];

static void BleIrqDispatch(u32 nSlot) {
    BleIrqSlot *pSlot = &s_bleIrqSlots[nSlot];
    if (pSlot->pUserHandler) pSlot->pUserHandler(pSlot->pArg);
}

static void BleIrqStub0(void) { BleIrqDispatch(0); }
static void BleIrqStub1(void) { BleIrqDispatch(1); }
static void BleIrqStub2(void) { BleIrqDispatch(2); }
static void BleIrqStub3(void) { BleIrqDispatch(3); }

static LTCore_InterruptHandler *const s_bleIrqStubs[BLE_IRQ_SLOTS] = {
    (LTCore_InterruptHandler *)&BleIrqStub0,
    (LTCore_InterruptHandler *)&BleIrqStub1,
    (LTCore_InterruptHandler *)&BleIrqStub2,
    (LTCore_InterruptHandler *)&BleIrqStub3,
};

/* Priority the controller asked for, kept per line so the handler can be
 * installed with it once _interrupt_handler_set arrives. */
static s32 s_bleIrqPriority[BLE_IRQ_SLOTS];

static void interrupt_set_wrapper(int cpu_no, int intr_source, int intr_num, int intr_prio) {
    LTLOG_DEBUG("intr.set", "cpu=%d src=%d num=%d prio=%d", cpu_no, intr_source, intr_num, intr_prio);
    if (cpu_no != 0) {
        LTLOG_REDALERT("intr.set", "only core 0 is routed, refusing cpu %d", cpu_no);
        return;
    }
    /* The controller picks its own lines - RWBLE on 5 and BT baseband on 8, as
     * the allocation table in Esp32_Irq.h records - so check rather than
     * dictate.  A line the level 1 dispatcher cannot reach would leave the
     * controller silently deaf. */
    if (!Esp32IrqLineIsAssignable((Esp32_IrqNumber)intr_num)) {
        LTLOG_REDALERT("intr.set", "cpu line %d takes no peripheral, source %d not routed",
                       intr_num, intr_source);
        return;
    }
    Esp32MapExternalToCPUIrq(kEsp32_CPU0, (Esp32_ExternalIrq)intr_source, (Esp32_IrqNumber)intr_num);
    for (u32 i = 0; i < BLE_IRQ_SLOTS; i++) {
        if (s_bleIrqSlots[i].nCpuIrq == intr_num || s_bleIrqSlots[i].nCpuIrq == -1) {
            s_bleIrqSlots[i].nCpuIrq = intr_num;
            s_bleIrqPriority[i]      = intr_prio;
            return;
        }
    }
    LTLOG_REDALERT("intr.set", "no free slot for cpu irq %d", intr_num);
}

static void interrupt_clear_wrapper(int intr_source, int intr_num) {
    LT_UNUSED(intr_source); LT_UNUSED(intr_num);
}

static void interrupt_handler_set_wrapper(int n, void *fn, void *arg) {
    for (u32 i = 0; i < BLE_IRQ_SLOTS; i++) {
        if (s_bleIrqSlots[i].nCpuIrq == n) {
            s_bleIrqSlots[i].pUserHandler = (void (*)(void *))fn;
            s_bleIrqSlots[i].pArg         = arg;
            LT_GetCore()->SetInterruptVector(n, s_bleIrqStubs[i], s_bleIrqPriority[i]);
            return;
        }
    }
    LTLOG_REDALERT("intr.handler", "cpu irq %d was never routed", n);
}

static void interrupt_on_wrapper(int intr_num)  { xt_ints_on(1U << intr_num);  }
static void interrupt_off_wrapper(int intr_num) { xt_ints_off(1U << intr_num); }

/****************************************************************************
 * OSI entries with no LT primitive behind them
 ****************************************************************************/
static u32 btdm_lpcycles_2_hus(u32 cycles, u32 *error_corr) {
    u64 local_error_corr = (error_corr == NULL) ? 0 : (u64)(*error_corr);
    u64 res = (u64)g_btdm_lpcycle_us * cycles * 2;
    local_error_corr += res;
    res = (local_error_corr >> g_btdm_lpcycle_us_frac);
    local_error_corr -= (res << g_btdm_lpcycle_us_frac);
    if (error_corr) {
        *error_corr = (u32)local_error_corr;
    }
    return (u32)res;
}

static u32 btdm_hus_2_lpcycles(u32 hus) {
    /* Sleep duration must stay under ~100s or this overflows. */
    u64 cycles = ((u64)(hus) << g_btdm_lpcycle_us_frac) / g_btdm_lpcycle_us;
    cycles >>= 1;
    return (u32)cycles;
}

/* Sleep is not enabled, so the controller is told never to sleep. */
static bool btdm_sleep_check_duration(s32 *slot_cnt) { LT_UNUSED(slot_cnt); return false; }
static void btdm_sleep_enter_phase1_wrapper(u32 lpcycles) { LT_UNUSED(lpcycles); }
static void btdm_sleep_enter_phase2_wrapper(void) { }
static void btdm_sleep_exit_phase3_wrapper(void) { }
static void coex_wifi_sleep_set_hook(bool sleep) { LT_UNUSED(sleep); }

/* MAC/BB power down is not enabled, so these never run. */
static void btdm_hw_mac_power_up_wrapper(void) { }
static void btdm_hw_mac_power_down_wrapper(void) { }
static void btdm_backup_dma_copy_wrapper(u32 reg, u32 mem_addr, u32 num, bool to_mem) {
    LT_UNUSED(reg); LT_UNUSED(mem_addr); LT_UNUSED(num); LT_UNUSED(to_mem);
}
static void btdm_funcs_table_ready_wrapper(void) { }

/* libcoexist.a on this part has no coex_core_ble_conn_dyn_prio_get, so the
 * dynamic priority hint is declined rather than left dangling. */
static int coex_core_ble_conn_dyn_prio_get_wrapper(bool *low, bool *high) {
    LT_UNUSED(low); LT_UNUSED(high);
    return -1;
}

static void ets_delay_us_wrapper(u32 us) { ets_delay_us(us); }

/****************************************************************************
 * Wakeup request
 ****************************************************************************/
static bool async_wakeup_request(int event) {
    bool do_wakeup_request = false;
    switch (event) {
        case BTDM_ASYNC_WAKEUP_REQ_HCI:
            btdm_in_wakeup_requesting_set(true);
            /* Intentional fall-through */

        case BTDM_ASYNC_WAKEUP_REQ_CTRL_DISA:
            if (!btdm_power_state_active()) {
                do_wakeup_request = true;
                /* The esp32's btdm_dispatch_work_to_controller() does not exist
                 * here; the controller task is reached by posting a vendor
                 * signal instead. */
                r_btdm_vnd_offload_post(BTDM_VND_OL_SIG_WAKEUP_TMR, NULL);
                s_os->SemTake(pWakeupReqSem, OSI_FUNCS_TIME_BLOCKING);
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

/****************************************************************************
 * OSI table
 *
 * Field order is the controller's ABI, so the initializers are designated and
 * kept in the order Esp32s3DriverBleController.h declares them.
 ****************************************************************************/
static struct osi_funcs_t s_osiFuncs;

/* LT's s32 is long while the controller's table declares the same slots int.
 * Identical width, distinct type, so every entry is cast to its own slot. */
#define OSI_SET(field, fn)  s_osiFuncs.field = (__typeof__(s_osiFuncs.field))(fn)

static void OsiFuncsBuild(void) {
    s_osiFuncs._magic   = OSI_MAGIC_VALUE;
    s_osiFuncs._version = OSI_VERSION;
    OSI_SET(_interrupt_set, &interrupt_set_wrapper);
    OSI_SET(_interrupt_clear, &interrupt_clear_wrapper);
    OSI_SET(_interrupt_handler_set, &interrupt_handler_set_wrapper);
    OSI_SET(_interrupt_disable, s_os->InterruptDisable);
    OSI_SET(_interrupt_restore, s_os->InterruptRestore);
    OSI_SET(_task_yield, s_os->TaskYield);
    OSI_SET(_task_yield_from_isr, s_os->TaskYieldFromIsr);
    OSI_SET(_semphr_create, s_os->SemCreate);
    OSI_SET(_semphr_delete, s_os->SemDelete);
    OSI_SET(_semphr_take_from_isr, s_os->SemTakeFromIsr);
    OSI_SET(_semphr_give_from_isr, s_os->SemGiveFromIsr);
    OSI_SET(_semphr_take, s_os->SemTake);
    OSI_SET(_semphr_give, s_os->SemGive);
    OSI_SET(_mutex_create, s_os->MutexCreate);
    OSI_SET(_mutex_delete, s_os->MutexDelete);
    OSI_SET(_mutex_lock, s_os->MutexLock);
    OSI_SET(_mutex_unlock, s_os->MutexUnlock);
    OSI_SET(_queue_create, s_os->QueueCreate);
    OSI_SET(_queue_delete, s_os->QueueDelete);
    OSI_SET(_queue_send, s_os->QueueSend);
    OSI_SET(_queue_send_from_isr, s_os->QueueSendFromIsr);
    OSI_SET(_queue_recv, s_os->QueueRecv);
    OSI_SET(_queue_recv_from_isr, s_os->QueueRecvFromIsr);
    OSI_SET(_task_create, s_os->TaskCreatePinnedToCore);
    OSI_SET(_task_delete, s_os->TaskDelete);
    OSI_SET(_is_in_isr, s_os->IsInIsr);
    /* The esp32s3 controller never posts a cross-core software interrupt. */
    OSI_SET(_cause_sw_intr_to_core, NULL);
    OSI_SET(_malloc, s_os->MallocInternal);
    OSI_SET(_malloc_internal, s_os->MallocInternal);
    OSI_SET(_free, s_os->Free);
    OSI_SET(_read_efuse_mac, s_os->ReadEfuseMac);
    OSI_SET(_srand, s_os->Srand);
    OSI_SET(_rand, s_os->Rand);
    OSI_SET(_btdm_lpcycles_2_hus, &btdm_lpcycles_2_hus);
    OSI_SET(_btdm_hus_2_lpcycles, &btdm_hus_2_lpcycles);
    OSI_SET(_btdm_sleep_check_duration, &btdm_sleep_check_duration);
    OSI_SET(_btdm_sleep_enter_phase1, &btdm_sleep_enter_phase1_wrapper);
    OSI_SET(_btdm_sleep_enter_phase2, &btdm_sleep_enter_phase2_wrapper);
    OSI_SET(_btdm_sleep_exit_phase1, NULL);
    OSI_SET(_btdm_sleep_exit_phase2, NULL);
    OSI_SET(_btdm_sleep_exit_phase3, &btdm_sleep_exit_phase3_wrapper);
    OSI_SET(_coex_wifi_sleep_set, &coex_wifi_sleep_set_hook);
    OSI_SET(_coex_core_ble_conn_dyn_prio_get, &coex_core_ble_conn_dyn_prio_get_wrapper);
    OSI_SET(_coex_schm_register_btdm_callback, &coex_schm_register_btdm_callback);
    OSI_SET(_coex_schm_status_bit_set, &coex_schm_status_bit_set);
    OSI_SET(_coex_schm_status_bit_clear, &coex_schm_status_bit_clear);
    OSI_SET(_coex_schm_interval_get, &coex_schm_interval_get);
    OSI_SET(_coex_schm_curr_period_get, &coex_schm_curr_period_get);
    OSI_SET(_coex_schm_curr_phase_get, &coex_schm_curr_phase_get);
    OSI_SET(_interrupt_on, &interrupt_on_wrapper);
    OSI_SET(_interrupt_off, &interrupt_off_wrapper);
    OSI_SET(_esp_hw_power_down, &btdm_hw_mac_power_down_wrapper);
    OSI_SET(_esp_hw_power_up, &btdm_hw_mac_power_up_wrapper);
    OSI_SET(_ets_backup_dma_copy, &btdm_backup_dma_copy_wrapper);
    OSI_SET(_ets_delay_us, &ets_delay_us_wrapper);
    OSI_SET(_btdm_rom_table_ready, &btdm_funcs_table_ready_wrapper);
    OSI_SET(_coex_bt_wakeup_request, &coex_bt_wakeup_request);
    OSI_SET(_coex_bt_wakeup_request_end, &coex_bt_wakeup_request_end);
}

/****************************************************************************
 * Static functions
 ****************************************************************************/
/* The esp32's btdm_dram_available_region[] table has no counterpart; on this
 * part the controller's data lives in ROM and is copied out by ROM code. */
static void btdm_controller_mem_init(void) {
    btdm_controller_rom_data_init();
}

static int esp32s3_bt_controller_init(void) {
    esp_bt_controller_config_t btCfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    s_os = LTEsp32OSAdapter_GetPrimitives();
    for (u32 i = 0; i < BLE_IRQ_SLOTS; i++) s_bleIrqSlots[i].nCpuIrq = -1;

    wifi_bt_coexist_init();
    OsiFuncsBuild();
    if (btdm_osi_funcs_register(&s_osiFuncs) != 0) {
        LTLOG_REDALERT("controller_init", "OSI function table rejected by the controller");
        return -1;
    }
    LTLOG_DEBUG("controller_init", "BT controller compile version [%s]", btdm_controller_get_compile_version());

    Esp32s3_WiFiBtPowerDomainOn();
    btdm_controller_mem_init();
    pWakeupReqSem = s_os->SemCreate(1, 0);

    /* Bring up the BT baseband and link controller clocks. */
    ESP32_REG(APB_CTRL_WIFI_CLK_EN) |= (kEsp32_RegisterAPB_CTRL_WIFI_CLK_BT_BASEBAND_EN_M |
                                                      kEsp32_RegisterAPB_CTRL_WIFI_CLK_BT_LC_EN_M);

    /* Sleep is off, so the low power clock is simply the crystal.  Note the
     * 1 << here: the esp32 driver uses 2 << because its lpcycle is a full
     * microsecond, while this controller counts half microseconds. */
    if (btdm_lpclk_select_src(BTDM_LPCLK_SEL_XTAL) == 0) {
        btdm_lpclk_set_div(ESP32S3_XTAL_FREQ_MHZ);
    }
    g_btdm_lpcycle_us_frac = RTC_CLK_CAL_FRACT;
    g_btdm_lpcycle_us      = 1 << (g_btdm_lpcycle_us_frac);

    coex_init();

    btCfg.controller_task_stack_size = ESP32S3_CONTROLLER_TASK_STACK;
    btCfg.controller_task_prio       = ESP32S3_CONTROLLER_TASK_PRIORITY;
    btCfg.magic                      = ESP_BT_CTRL_CONFIG_MAGIC_VAL;
    btCfg.bluetooth_mode             = ESP_BT_MODE_BLE;

    if (btdm_controller_init(&btCfg) != 0) {
        LT_ESP32_TR_FAIL;
        return -1;
    }
    LTLOG_DEBUG("controller_init", "The ble controller initialized successfully");

    btdm_controller_status = ESP_BT_CONTROLLER_STATUS_INITED;

    return 0;
}

static int esp32s3_bt_controller_enable(void) {
    /* WiFi crashes if WIFI_PS_NONE is used together with Bluetooth */
    esp_wifi_get_ps(&wifi_ps_mode);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    /* Order is fixed: phy enable, then the baseband comes up inside
     * btdm_controller_enable(), then coex_pti_v2().  There is no
     * btdm_rf_bb_init_phase2() on this part. */
    esp32_phy_enable();
    coex_enable();
    if (btdm_controller_enable(ESP_BT_MODE_BLE) != 0) {
        LT_ESP32_TR_FAIL;
        return -1;
    }
    coex_pti_v2();
    LTLOG_DEBUG("controller_enable", "The ble controller enabled successfully");

    btdm_controller_status = ESP_BT_CONTROLLER_STATUS_ENABLED;
    return 0;
}

static bool sControllerInitialized = false;
static bool Esp32s3DriverBleController_Enable(bool enable) {
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
            if (esp32s3_bt_controller_init() != 0) {
                LTLOG_REDALERT("DriverBleControllerInit", "Failed to initialize the controller");
                return false;
            }
            sControllerInitialized = true;
        }

        if (esp32s3_bt_controller_enable() != 0) {
            LTLOG_REDALERT("DriverBleControllerEnable", "Failed to enable the controller");
            return false;
        }

    } else {
        /* LT cannot cleanly shut down the BT thread, so the controller is left
         * initialized once it has come up; see the esp32 driver for the same. */
    }
    return true;
}

static bool Esp32s3DriverBleController_RegisterCallbacks(const LTBleController_VhciHostCallback *vhciHostCallbacks) {
    return 0 == API_vhci_host_register_callback((const esp_vhci_host_callback_t *)(vhciHostCallbacks ? vhciHostCallbacks : &s_vhciDefaultCallbacks));
}

static bool Esp32s3DriverBleController_Send(const u8 *data, u16 len) {
    if (!API_vhci_host_check_send_available()) {
        LTLOG("chk.send", "Controller not ready to receive packets");
        return false;
    }
    async_wakeup_request(BTDM_ASYNC_WAKEUP_REQ_HCI);
    API_vhci_host_send_packet((u8 *)data, len);
    async_wakeup_request_end(BTDM_ASYNC_WAKEUP_REQ_HCI);
    return true;
}

static bool Esp32s3DriverBleController_GetConnectionInfo(u16 connHandle, LTBleConnectionInfo *info) {
    LT_UNUSED(connHandle); LT_UNUSED(info);
    return false;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static ILTBleController s_ILTBleController;

static u32 Esp32s3DriverBleControllerImpl_GetNumDeviceUnits(void) {
    return 1;
}

static LTDeviceUnit Esp32s3DriverBleControllerImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNum) {
    LT_UNUSED(nDeviceUnitNum);
    return LT_GetCore()->CreateHandle((LTInterface *)&s_ILTBleController, 1);
}

static bool Esp32s3DriverBleControllerImpl_LibInit(void) {
    LTEsp32OSAdapter_LibInit();
    return true;
}

static void Esp32s3DriverBleControllerImpl_LibFini(void) {
    LTEsp32OSAdapter_LibFini();
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/
define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceBleController, Esp32s3DriverBleController);

define_LTLIBRARY_INTERFACE(ILTBleController) {
    .Enable            = &Esp32s3DriverBleController_Enable,
    .RegisterCallbacks = &Esp32s3DriverBleController_RegisterCallbacks,
    .Send              = &Esp32s3DriverBleController_Send,
    .GetConnectionInfo = &Esp32s3DriverBleController_GetConnectionInfo,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(Esp32s3DriverBleController, (ILTBleController))

/**********************************************************************************
 *  LOG
 **********************************************************************************
 *  27-Aug-26   claudius    created
 */
