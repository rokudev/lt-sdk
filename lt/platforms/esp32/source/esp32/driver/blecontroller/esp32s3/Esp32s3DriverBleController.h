/*******************************************************************************
 * platforms/esp32/source/esp32/driver/blecontroller/esp32s3/Esp32s3DriverBleController.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_BLECONTROLLER_ESP32S3_ESP32S3DRIVERBLECONTROLLER_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_BLECONTROLLER_ESP32S3_ESP32S3DRIVERBLECONTROLLER_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN
#include <lt/device/blecontroller/LTDeviceBleController.h>
#include "../../esp32-lt-os-adapter/Esp32_LTOSAdapterOsi.h"
#define _VA_LIST_DEFINED
/* IDF v4.4 builds the esp32s3 against components/bt/include/esp32c3/include, so
 * esp_bt.h under include/esp32s3 is a copy of the c3 header; see the .mk. */
#include <esp_bt.h>
#include <esp32s3/Esp32_Irq.h>
#include <esp32s3/phy_init_data.h>

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

/****************************************************************************
 * Macros
 ****************************************************************************/
/* The controller rejects any other value for these two, so they are not tunable. */
#define ESP32S3_CONTROLLER_TASK_STACK       ESP_TASK_BT_CONTROLLER_STACK
#define ESP32S3_CONTROLLER_TASK_PRIORITY    ESP_TASK_BT_CONTROLLER_PRIO

#define LT_ESP32_TR_FAIL LTLOG_REDALERT     ("test", "FILE: %s, LINE#%d, function: %s", __FILE__, __LINE__, __FUNCTION__);
#define LT_ESP32_BIT(nr)                    (1UL << (nr))

#define BTDM_ASYNC_WAKEUP_REQ_HCI           (0)
#define BTDM_ASYNC_WAKEUP_REQ_COEX          (1)
#define BTDM_ASYNC_WAKEUP_REQ_CTRL_DISA     (2)
#define BTDM_ASYNC_WAKEUP_REQMAX            (3)

/* Vendor signal posted to the controller task; the only one the S3 defines. */
#define BTDM_VND_OL_SIG_WAKEUP_TMR          (0)

/* Low power clock source selection for btdm_lpclk_select_src() */
#define BTDM_LPCLK_SEL_XTAL                 (0)
#define BTDM_LPCLK_SEL_XTAL32K              (1)
#define BTDM_LPCLK_SEL_RTC_SLOW             (2)
#define BTDM_LPCLK_SEL_8M                   (3)

#define RTC_CLK_CAL_FRACT                   (19)

/* The esp32s3 only supports a 40 MHz crystal (soc/rtc.h, RTC_XTAL_FREQ_40M). */
#define ESP32S3_XTAL_FREQ_MHZ               (40)

#define OSI_VERSION                         (0x00010008)
#define OSI_MAGIC_VALUE                     (0xFADEBEAD)

/****************************************************************************
 * Typedefs
 ****************************************************************************/
typedef void (*btdm_vnd_ol_task_func_t)(void *param);

/*
 * Copied verbatim from ESP-IDF v4.4 components/bt/controller/esp32c3/bt.c, which
 * is the controller port the esp32s3 uses.  btdm_osi_funcs_register() checks
 * _magic and _version and rejects anything else, so field order is ABI.  This
 * has nothing in common with the esp32 table in Esp32_LTOSAdapter.h beyond the
 * name: the magic/version words are swapped, the interrupt block grew from two
 * entries to five, and the tail is new.
 */
struct osi_funcs_t {
    u32    _magic;
    u32    _version;
    void   (*_interrupt_set)(int cpu_no, int intr_source, int interrupt_no, int interrpt_prio);
    void   (*_interrupt_clear)(int interrupt_source, int interrupt_no);
    void   (*_interrupt_handler_set)(int interrupt_no, void *fn, void *arg);
    void   (*_interrupt_disable)(void);
    void   (*_interrupt_restore)(void);
    void   (*_task_yield)(void);
    void   (*_task_yield_from_isr)(void);
    void * (*_semphr_create)(u32 max, u32 init);
    void   (*_semphr_delete)(void *semphr);
    int    (*_semphr_take_from_isr)(void *semphr, void *hptw);
    int    (*_semphr_give_from_isr)(void *semphr, void *hptw);
    int    (*_semphr_take)(void *semphr, u32 block_time_ms);
    int    (*_semphr_give)(void *semphr);
    void * (*_mutex_create)(void);
    void   (*_mutex_delete)(void *mutex);
    int    (*_mutex_lock)(void *mutex);
    int    (*_mutex_unlock)(void *mutex);
    void * (*_queue_create)(u32 queue_len, u32 item_size);
    void   (*_queue_delete)(void *queue);
    int    (*_queue_send)(void *queue, void *item, u32 block_time_ms);
    int    (*_queue_send_from_isr)(void *queue, void *item, void *hptw);
    int    (*_queue_recv)(void *queue, void *item, u32 block_time_ms);
    int    (*_queue_recv_from_isr)(void *queue, void *item, void *hptw);
    int    (*_task_create)(void *task_func, const char *name, u32 stack_depth, void *param, u32 prio, void *task_handle, u32 core_id);
    void   (*_task_delete)(void *task_handle);
    bool   (*_is_in_isr)(void);
    int    (*_cause_sw_intr_to_core)(int core_id, int intr_no);
    void * (*_malloc)(size_t size);
    void * (*_malloc_internal)(size_t size);
    void   (*_free)(void *p);
    int    (*_read_efuse_mac)(u8 mac[6]);
    void   (*_srand)(unsigned int seed);
    int    (*_rand)(void);
    u32    (*_btdm_lpcycles_2_hus)(u32 cycles, u32 *error_corr);
    u32    (*_btdm_hus_2_lpcycles)(u32 hus);
    bool   (*_btdm_sleep_check_duration)(s32 *slot_cnt);
    void   (*_btdm_sleep_enter_phase1)(u32 lpcycles);
    void   (*_btdm_sleep_enter_phase2)(void);
    void   (*_btdm_sleep_exit_phase1)(void);
    void   (*_btdm_sleep_exit_phase2)(void);
    void   (*_btdm_sleep_exit_phase3)(void);
    void   (*_coex_wifi_sleep_set)(bool sleep);
    int    (*_coex_core_ble_conn_dyn_prio_get)(bool *low, bool *high);
    int    (*_coex_schm_register_btdm_callback)(void *callback);
    void   (*_coex_schm_status_bit_set)(u32 type, u32 status);
    void   (*_coex_schm_status_bit_clear)(u32 type, u32 status);
    u32    (*_coex_schm_interval_get)(void);
    u8     (*_coex_schm_curr_period_get)(void);
    void * (*_coex_schm_curr_phase_get)(void);
    void   (*_interrupt_on)(int intr_num);
    void   (*_interrupt_off)(int intr_num);
    void   (*_esp_hw_power_down)(void);
    void   (*_esp_hw_power_up)(void);
    void   (*_ets_backup_dma_copy)(u32 reg, u32 mem_addr, u32 num, bool to_rem);
    void   (*_ets_delay_us)(u32 us);
    void   (*_btdm_rom_table_ready)(void);
    bool   (*_coex_bt_wakeup_request)(void);
    void   (*_coex_bt_wakeup_request_end)(void);
};

/****************************************************************************
 * Externally linked functions
 ****************************************************************************/
/* Enable/Disable coexist -- libcoexist.a */
extern int         coex_init(void);
extern void        coex_deinit(void);
extern int         coex_enable(void);
extern void        coex_disable(void);
extern void        coex_pti_v2(void);
extern void        coex_schm_status_bit_set(u32 type, u32 status);
extern void        coex_schm_status_bit_clear(u32 type, u32 status);
extern u32         coex_schm_interval_get(void);
extern u8          coex_schm_curr_period_get(void);
extern void *      coex_schm_curr_phase_get(void);
extern int         coex_schm_register_btdm_callback(void *callback);

/* libbtdm_app.a functions.  The esp32's btdm_rf_bb_init_phase2(),
 * btdm_dispatch_work_to_controller() and btdm_controller_set_sleep_mode() have
 * no counterpart here and are absent from the esp32s3 blobs. */
extern int         btdm_osi_funcs_register(void *osi_funcs);
extern int         btdm_controller_init(esp_bt_controller_config_t *config_opts);
extern void        btdm_controller_deinit(void);
extern int         btdm_controller_enable(esp_bt_mode_t mode);
extern void        btdm_controller_disable(void);
extern const char *btdm_controller_get_compile_version(void);
extern bool        btdm_power_state_active(void);
extern void        btdm_wakeup_request(void);
extern void        btdm_in_wakeup_requesting_set(bool in_wakeup_requesting);
extern int         btdm_lpclk_select_src(u32 sel);
extern int         btdm_lpclk_set_div(u32 div);
extern int         btdm_vnd_offload_task_register(u32 sig, btdm_vnd_ol_task_func_t func);
extern int         btdm_vnd_offload_task_deregister(u32 sig);
extern int         r_btdm_vnd_offload_post(u32 sig, void *param);

extern int         API_vhci_host_register_callback(const esp_vhci_host_callback_t *callback);
extern void        API_vhci_host_send_packet(u8 *pData, u16 len);
extern bool        API_vhci_host_check_send_available(void);

/* esp32s3 ROM, see mastering/ld/esp32s3/rom/esp32s3.rom.ld */
extern void        btdm_controller_rom_data_init(void);
extern void        ets_delay_us(u32 us);

/* Adapter services shared with the esp32 driver, from Esp32_LTOSAdapter.c.
 * Esp32_LTOSAdapter.h is deliberately not included: it declares the esp32's
 * incompatible struct osi_funcs_t under the same name as the one above. */
extern void        LTEsp32OSAdapter_LibInit(void);
extern void        LTEsp32OSAdapter_LibFini(void);
extern void        esp32_phy_enable(void);
extern void        esp32_phy_disable(void);
extern int         wifi_bt_coexist_init(void);

/* Shared Wi-Fi/BT modem power domain, from Esp32s3_LTOSAdapter.c.  Reference
 * counted there, so Wi-Fi coming up later cannot reset this controller. */

extern void        Esp32s3_WiFiBtPowerDomainOn(void);
extern void        Esp32s3_WiFiBtPowerDomainOff(void);

/* Xtensa interrupt enable/disable, from the adapter's intrs.S */
extern void        xt_ints_on(u32 mask);
extern void        xt_ints_off(u32 mask);

LT_EXTERN_C_END
#endif  // PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_BLECONTROLLER_ESP32S3_ESP32S3DRIVERBLECONTROLLER_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  27-Aug-26   claudius    created
 */
