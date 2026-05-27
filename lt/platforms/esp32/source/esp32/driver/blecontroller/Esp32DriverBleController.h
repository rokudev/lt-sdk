/*******************************************************************************
 * platforms/esp32/source/esp32/driver/blecontroller/ESP32DriverBleController.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_BLECONTROLLER_ESP32DRIVERBLECONTROLLER_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_BLECONTROLLER_ESP32DRIVERBLECONTROLLER_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN
#include <lt/device/blecontroller/LTDeviceBleController.h>
#include "../esp32-lt-os-adapter/Esp32_LTOSAdapter.h"
#define _VA_LIST_DEFINED
#include <esp_bt.h>
#include <esp32/Esp32_Irq.h>
#include <esp32/phy_init_data.h>

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
#define ESP32_CONTROLLER_TASK_STACK         (3072)
#define ESP32_CONTROLLER_TASK_PRIORITY      (23)
#define LT_ESP32_TR_FAIL LTLOG_REDALERT     ("test", "FILE: %s, LINE#%d, function: %s", __FILE__, __LINE__, __FUNCTION__);
#define LT_ESP32_BIT(nr)                    (1UL << (nr))
#define LT_ESP32_RTC_CNTL_WIFI_FORCE_PD     (LT_ESP32_BIT(17))
#define LT_ESP32_RTC_CNTL_WIFI_FORCE_ISO    (LT_ESP32_BIT(28))

#define BTDM_ASYNC_WAKEUP_REQ_HCI           (0)
#define BTDM_ASYNC_WAKEUP_REQ_COEX          (1)
#define BTDM_ASYNC_WAKEUP_REQ_CTRL_DISA     (2)
#define BTDM_ASYNC_WAKEUP_REQMAX            (3)

/* Bluetooth system and controller config */
#define BTDM_CFG_BT_DATA_RELEASE            (1 << 0)
#define BTDM_CFG_HCI_UART                   (1 << 1)
#define BTDM_CFG_CONTROLLER_RUN_APP_CPU     (1 << 2)
#define BTDM_CFG_SCAN_DUPLICATE_OPTIONS     (1 << 3)
#define BTDM_CFG_SEND_ADV_RESERVED_SIZE     (1 << 4)
#define BTDM_CFG_BLE_FULL_SCAN_SUPPORTED    (1 << 5)

/* Sleep mode */
#define BTDM_MODEM_SLEEP_MODE_NONE          (0)
#define BTDM_MODEM_SLEEP_MODE_ORIG          (1)
#define BTDM_MODEM_SLEEP_MODE_EVED          (2)  // sleep mode for BLE controller, used only for internal test.

/* Bluetooth memory regions */
#define SOC_MEM_BT_DATA_START               (0x3ffae6e0)
#define SOC_MEM_BT_DATA_END                 (0x3ffaff10)
#define SOC_MEM_BT_EM_BTDM0_START           (0x3ffb0000)
#define SOC_MEM_BT_EM_BTDM0_END             (0x3ffb09a8)
#define SOC_MEM_BT_EM_BLE_START             (0x3ffb09a8)
#define SOC_MEM_BT_EM_BLE_END               (0x3ffb1ddc)
#define SOC_MEM_BT_EM_BTDM1_START           (0x3ffb1ddc)
#define SOC_MEM_BT_EM_BTDM1_END             (0x3ffb2730)
#define SOC_MEM_BT_BSS_START                (0x3ffb8000)
#define SOC_MEM_BT_BSS_END                  (0x3ffb9a20)
#define SOC_MEM_BT_MISC_START               (0x3ffbdb28)
#define SOC_MEM_BT_MISC_END                 (0x3ffbdb5c)

#define RTC_CLK_CAL_FRACT                   (19)

/****************************************************************************
 * Typedefs
 ****************************************************************************/
typedef void (* workitem_handler_t)(void *arg);
typedef struct btdm_dram_available_region_t {
    intptr_t start;
    intptr_t end;
} btdm_dram_available_region_t;

/****************************************************************************
 * Externally linked functions
 ****************************************************************************/
/* Enable/Disable coexist */
extern int         coex_enable(void);
extern void        coex_disable(void);

/* libbtdm_app.a functions: */
extern int         btdm_osi_funcs_register(void *osi_funcs);
extern void        btdm_controller_set_sleep_mode(u8 mode);
extern int         btdm_controller_init(u32 config_mask, esp_bt_controller_config_t *config_opts);
extern void        btdm_controller_deinit(void);
extern u8          btdm_controller_get_sleep_mode(void);
extern void        btdm_controller_enable_sleep(bool enable);
extern const char *btdm_controller_get_compile_version(void);
extern bool        btdm_power_state_active(void);
extern void        btdm_rf_bb_init_phase2(void);
extern int         btdm_controller_enable(esp_bt_mode_t mode);
extern void        btdm_controller_disable(void);
extern void        btdm_wakeup_request(void);
extern int         btdm_dispatch_work_to_controller(workitem_handler_t callback, void *arg, bool blocking);
extern void        btdm_in_wakeup_requesting_set(bool in_wakeup_requesting);

extern int         API_vhci_host_register_callback(const esp_vhci_host_callback_t *callback);
extern void        API_vhci_host_send_packet(u8 *pData, u16 len);
extern bool        API_vhci_host_check_send_available(void);

// Function from WiFi Adapter for coexist initialization
extern int          wifi_bt_coexist_init(void);

// BLE Adapter functions
extern void        lt_ble_drv_init(void);
extern void        esp32_phy_enable(void);
extern void        esp32_phy_disable(void);

/****************************************************************************
 * Externally linked variables
 ****************************************************************************/
/* Wi-Fi sleep private data */
extern u32 g_phy_clk_en_cnt;
/* Reference count of enabling PHY */
extern u32 g_phy_access_ref;
/* Indicate PHY is calibrated or not */
extern bool g_is_phy_calibrated;
/* Memory to store PHY digital registers */
extern u32 *g_phy_digital_regs_mem;
/*BLE Adapter functions*/
extern const struct osi_funcs_t g_osi_funcs;

/* Defined in linker scripts and esp-wireless libraries */
extern char     _bss_start_btdm;
extern char     _bss_end_btdm;
extern char     _data_start_btdm;
extern char     _data_end_btdm;
extern u32      _data_start_btdm_rom;
extern u32      _data_end_btdm_rom;

extern u32      _bt_bss_start;
extern u32      _bt_bss_end;
extern u32      _nimble_bss_start;
extern u32      _nimble_bss_end;
extern u32      _btdm_bss_start;
extern u32      _btdm_bss_end;
extern u32      _bt_data_start;
extern u32      _bt_data_end;
extern u32      _nimble_data_start;
extern u32      _nimble_data_end;
extern u32      _btdm_data_start;
extern u32      _btdm_data_end;

LT_EXTERN_C_END
#endif  // PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_BLECONTROLLER_ESP32DRIVERBLECONTROLLER_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  20-Dec-22   gallienus   created
 */
