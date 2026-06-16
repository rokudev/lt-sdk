/*******************************************************************************
 *
 * Esp32 binary WiFi and BLE driver adapters for LT
 * -----------------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <lt/device/identity/LTDeviceIdentity.h>
#include <lt/core/LTThread.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/core/LTCountingSemaphore.h>
#include <esp32/Esp32_Irq.h>
#include <esp32/Esp32_Registers.h>
#include "Esp32_LTOSAdapter.h"

DEFINE_LTLOG_SECTION("esp32.adpt");

/****************************************************************************
 * Typedefs
 ****************************************************************************/

typedef LT_SIZE size_t;

typedef struct handler_irq_info {
    xt_handler f_user;
    void     * arg;
    int        n;
    void     (*stub)(void);
} handler_irq_info;

/****************************************************************************
 * Macros
 ****************************************************************************/
#undef  va_list
#define va_list                                                     lt_va_list
#undef  va_start
#define va_start                                                    lt_va_start
#undef  va_arg
#define va_arg                                                      lt_va_arg
#undef  va_end
#define va_end                                                      lt_va_end
#undef  va_copy
#define va_copy                                                     lt_va_copy

/* Thread specific clientdata key */
#define ESP_THREAD_SPECIFIC_SEM_KEY                                 "ESPsem"

/* Thread Stack Sizes */
#define THREAD_STACK_SIZE_WIFI_DRIVER                               (5 * 1024)
#define THREAD_STACK_SIZE_WIFI_WORKER                               (2 * 1024)
#define THREAD_STACK_SIZE_WIFI_TIMERS                               (2 * 1024)

/* WiFi memory configurations: minimum memory configuration from IDF */
#define LT_ESP32_WIFI_STATIC_RXBUF_NUM                              4
#define LT_ESP32_WIFI_DYNAMIC_RXBUF_NUM                             8
#define LT_ESP32_WIFI_DYNAMIC_TXBUF_NUM                             8
#if 0 /* Disable AMPDU to save memory: minimum memory configuration from IDF */
#define LT_ESP32_WIFI_TX_AMPDU                                      1
#define LT_ESP32_WIFI_RX_AMPDU                                      1
#endif
#define LT_ESP32_WIFI_RXBA_AMPDU_WZ                                 6

/* DEBUG */
#ifdef LT_DEBUG
#define LT_DEBUG_WIRELESS_ERROR                                     1
#define LT_DEBUG_WIRELESS_WARN                                      1
/* Uncomment this for real debugging */
// #define LT_DEBUG_WIRELESS_INFO                                      1
#else
//#define LT_DEBUG_WIRELESS_ERROR                                     1
#endif

/* Timing definitions */
#define LT_CONFIG_USEC_PER_TICK                                     1000
#define TICK2MSEC(tick)                                             (tick)
#define TICK2USEC(tick)                                             ((tick) * LT_CONFIG_USEC_PER_TICK)
#define USEC2TICK(usec)                                             (((usec)+(LT_CONFIG_USEC_PER_TICK/2))/LT_CONFIG_USEC_PER_TICK)
#define MSEC2TICK(msec)                                             (msec)

/* TODO: check if this is reasonable defaults for LT */
#define ESP32_WIFI_PRIO                                             1
#define SLEEP_MS                                                    1
#define MAC_LEN                                                     6
#define DEFAULT_PS_MODE                                             WIFI_PS_NONE

#if 0 && defined(LT_DEBUG_WIRELESS_INFO) && LT_DEBUG_WIRELESS_INFO
#define LOG_ENTRY() LTLOG_DEBUG("enter", "%s", __FUNCTION__)
#define LOG_LEAVE() LTLOG_DEBUG("enter", "%s", __FUNCTION__)
#define LOG_ENTRY_TIMER(pt)
#define LOG_ENTRY_TIMER_ARM(pt, us, repeat)
#define LOG_TXDONE_ENTRY(...) LTLOG_DEBUG(__VA_ARGS__);
//#define LOG_ENTRY_TIMER(pt) LTLOG_DEBUG("timer", "%s %p", __FUNCTION__, pt)
//#define LOG_ENTRY_TIMER_ARM(pt, us, repeat) LTLOG_DEBUG("timer.arm", "%s %p us %u repeat %d", __FUNCTION__, pt, us, repeat)
#define LOG_ENTRY_TASK(name,stack_depth,prio,handle) LTLOG_DEBUG("enter", "%s: %s stack %d prio %d handle 0x%lx", __FUNCTION__, name, stack_depth, prio, LT_PLT_SIZE(handle))
//#define LOG_ENTRY_MUTEX(handle) LTLOG_DEBUG("enter", "%s: %lx", __FUNCTION__, LT_PLT_SIZE(handle))
#define LOG_ENTRY_MUTEX(handle)
#define LOG_ENTRY_SEM(handle)
//#define LOG_ENTRY_SEM(handle) LTLOG_DEBUG("enter", "%s: %lx", __FUNCTION__, LT_PLT_SIZE(handle))
#define LOG_ENTRY_ISR()
//LTLOG_DEBUG("enter", "%s", __FUNCTION__)
#define LOG_ENTRY_ISR2()
//LTLOG_DEBUG("enter", "%s", __FUNCTION__)
#define LOG_WORKER_ENTRY() LTLOG_DEBUG("enter", "%s", __FUNCTION__)
#define LOG_MUTEX_FAIL() LTLOG_DEBUG("mutex.fail", "%s: FAIL", __FUNCTION__)
//#define LOG_MUTEX_OK() LTLOG_DEBUG("mutex.ok", "%s: OK", __FUNCTION__)
#define LOG_MUTEX_OK()
#define LOG_ENTRY_GET_CURR_TASK()
#define LOG_ENTRY_MSG_WAIT(size) LTLOG_DEBUG("enter", "%s: size %lu", __FUNCTION__,LT_Pu32(size))
#define LOG_MSG_SUCCESS(item,size)
//#define LOG_MSG_SUCCESS(item,size) LTLOG_DEBUG("ok", "%s: msg 0x%lx", __FUNCTION__,LT_Pu32(*((u32*)item)))
#define LOG_MSG_FAIL() LT_ASSERT(0)
#define WIFI_DRIVER_FAIL() LT_ASSERT(0)
#else
#define LOG_ENTRY()
#define LOG_LEAVE()
#define LOG_TXDONE_ENTRY(...)
#define LOG_ENTRY_MUTEX(handle)
#define LOG_MUTEX_FAIL()
#define LOG_MUTEX_OK()
#define LOG_ENTRY_SEM(handle)
#define LOG_ENTRY_TASK(name,stack_depth,prio,handle)
#define LOG_ENTRY_ISR()
#define LOG_ENTRY_ISR2()
#define LOG_ENTRY_TIMER(pt)
#define LOG_ENTRY_TIMER_ARM(pt, us, repeat)
#define LOG_ENTRY_GET_CURR_TASK()
#define LOG_WORKER_ENTRY()
#define LOG_ENTRY_MSG_WAIT(size)
#define LOG_MSG_SUCCESS(item,size)
#define LOG_MSG_FAIL()
#define WIFI_DRIVER_FAIL()
#endif

#define _VA_LIST_
#define TIMER_INITIALIZED_VAL                                       (0x5aa5a55a)
/* Disable logging from the ROM code. */
#define RTC_DISABLE_ROM_LOG                                         ((1 << 0) | (1 << 16))

/* Sleep and wakeup interval control */
/* Threshold of interval in half slots to allow to fall into sleep mode */
#define LT_ESP32_BTDM_MIN_SLEEP_DURATION                            (24)

/* delay in half slots of modem wake up procedure, including re-enable PHY/RF */
#define LT_ESP32_BTDM_MODEM_WAKE_UP_DELAY                           (8)

#define LT_ESP32_TRACE                                              LTLOG_DEBUG("info", "%s, line#%d", __FUNCTION__, __LINE__)
#define LT_ESP32_SLEEP_MS                                           1

/* Number of fractional bits in values returned by rtc_clk_cal */
#define LT_ESP32_RTC_CLK_CAL_FRACT                                  19

#define LT_ESP32_XTHAL_SET_INTSET(v) \
do {\
  int __interrupt = (int)(v);\
  __asm__ __volatile__("wsr.intset %0" :: "a"(__interrupt):"memory");\
} while(0)

#define RSR(reg, curval)       asm volatile ("rsr %0, " #reg : "=r" (curval));

/* Defs for accessing entropy-related values */
#define CCOUNT                 (234)
#define APB_CYCLE_WAIT_NUM     (16)

/* Main wifi header for binary components */
#include <espidf_wifi.h>
#include <esp_coexist_internal.h>
#define _VA_LIST_DEFINED
#include <esp_bt.h>
#include <esp_system.h>
ESP_EVENT_DEFINE_BASE(WIFI_EVENT);


/* GLOBAL variables */
/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Wi-Fi sleep private data */

u32 g_phy_clk_en_cnt;

/* Reference count of enabling PHY */

uint8_t g_phy_access_ref;

/* Memory to store PHY digital registers */

u32 *g_phy_digital_regs_mem = NULL;

/* Indicate PHY is calibrated or not */

bool g_is_phy_calibrated = false;

/* Wi-Fi feature capacity data */

uint64_t g_wifi_feature_caps = CONFIG_FEATURE_WPA3_SAE_BIT;

/****************************************************************************
 * External functions
 ****************************************************************************/
extern void         btdm_in_wakeup_requesting_set(bool in_wakeup_requesting);
extern void         coex_dbg_set_log_level(int level);
extern int          coex_bt_request(u32 event, u32 latency, u32 duration);
extern int          coex_bt_release(u32 event);
extern int          coex_register_bt_cb(coex_func_cb_t cb);
extern void         coex_bb_reset_unlock(u32 restore);
extern u32          coex_bb_reset_lock(void);
extern int          coex_schm_register_btdm_callback(void *callback);
extern int          coex_wifi_channel_get(uint8_t *primary,
                                   uint8_t *secondary);
extern int          coex_register_wifi_channel_change_callback(void *cb);
extern u8           esp_crc8(u8 const * p, u32 len);
extern void         Esp32_JoinStatus_STA_CB_Success(void);
extern void         Esp32_JoinStatus_STA_CB_Failed(wifi_err_reason_t reason);
extern void         Esp32_ScanResult_STA_CB(unsigned int ap_num, wifi_ap_record_t *ap_list_buffer);
extern esp_err_t    esp_wifi_deinit(void);
extern int          esp_wifi_adapter_init(int *p_wifi_ref_cnt);

/* ESP32 ROM constant, defined in third-party/vendor-sdk/esp-idf-v4.4/components/esp_rom/esp32/ld/esp32.rom.ld */
extern u32 g_ticks_per_us_pro;

/*
-------------------------------------------------------------------------------
  Call this function to enable the specified interrupts on the core that runs
  this code.

    mask     - Bit mask of interrupts to be enabled.
-------------------------------------------------------------------------------
*/
extern void         xt_ints_on(uint32_t mask);

/*
-------------------------------------------------------------------------------
  Call this function to disable the specified interrupts on the core that runs
  this code.

    mask     - Bit mask of interrupts to be disabled.
-------------------------------------------------------------------------------
*/
extern void         xt_ints_off(uint32_t mask);


/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void *           esp_spin_lock_create(void);
static void             esp_spin_lock_delete(void *lock);
static u32              esp_wifi_int_disable(void *wifi_int_mux);
static void             esp_wifi_int_restore(void *wifi_int_mux, u32 tmp);
void *                  lt_sem_create(u32 max, u32 init);
void                    lt_sem_delete(void * sem);
s32                     lt_sem_take(void * sem, u32 block_time_tick);
s32                     lt_sem_give(void * sem);
static s32              lt_sem_take_from_isr(void * pSem, void *hptw);
static s32              lt_sem_give_from_isr(void * pSem, void *hptw);
static int              wifi_is_in_isr(void);
static void             lt_timer_disarm(void * timer);
static void             lt_timer_done(void * timer);
static void             lt_timer_setfn(void * timer, void * pfunction, void *parg);
static void             lt_timer_arm_us(void * timer, u32 us, bool repeat);
static void *           lt_malloc_internal(size_t size);
static int64_t          lt_timer_get_time(void);
static void             esp_free(void * ptr);
static void             DispatchAndKillTimer(void * pClientData);
static xt_handler       esp_ble_set_isr(s32 n, xt_handler f, void *arg) ;
static void IRAM_ATTR   interrupt_disable(void);
static void IRAM_ATTR   interrupt_restore(void);
static void IRAM_ATTR   lt_task_yield(void);
static void IRAM_ATTR   lt_task_yield_from_isr(void);
static void *           lt_mutex_create(void);
static void             lt_task_delete(void  * task_handle);
static void             lt_mutex_delete(void * pMtx);
static s32              lt_mutex_lock(void   * pMtx);
static s32              lt_mutex_unlock(void * pMtx);
static void *           lt_queue_create(u32 queue_len, u32 item_size);
static void             lt_queue_delete(void * pQueue);
static s32              lt_queue_send(void * pQueue, void *item,
                                      u32 block_time_tick);
static s32              lt_queue_send_from_isr(void * pQueue, void *item, void *hptw);
static s32              lt_queue_send_to_front(void * pQueue, void *item,
                                                u32 block_time_tick);
static s32              lt_queue_recv(void *  pQueue, void *item,
                                        u32 block_time_tick);
static u32              lt_queue_msg_waiting(void * pQueue);
static s32              lt_task_create_pinned_to_core(void * entry,
                                                        const char *name,
                                                        u32    stack_depth,
                                                        void * param,
                                                        u32    prio,
                                                        void * task_handle,
                                                        u32    coreID);
static bool IRAM_ATTR   is_in_isr_wrapper(void);
static s32 IRAM_ATTR    cause_sw_intr_to_core_wrapper(s32 coreID, s32 nIntr);
static void *           esp_malloc(u32 size);
static s32 IRAM_ATTR    read_mac_wrapper(u8 mac[6]);
static void IRAM_ATTR   srand_wrapper(u32 seed);
static s32              lt_rand_stub(void);
static u32 IRAM_ATTR    btdm_lpcycles_2_us(u32 cycles);
static u32 IRAM_ATTR    btdm_us_2_lpcycles(u32 us);
static bool             btdm_sleep_check_duration(u32 * pSlotCnt);
bool                    coex_bt_wakeup_request(void);
void                    coex_bt_wakeup_request_end(void);
static s32 IRAM_ATTR    coex_bt_request_wrapper(u32 event, u32 latency, u32 duration);
static s32              coex_bt_release_wrapper(u32 event);
static void             esp_ble_helper_handler0(void);
static void             esp_ble_helper_handler1(void);
static void             esp_ble_helper_handler2(void);
static void             esp_ble_helper_handler3(void);
static s32 IRAM_ATTR    queue_recv_from_isr_wrapper(void * pQueue,
                                                    void * pItem,
                                                    void * pHptw);
static void             xt_ints_on_wrapper(u32 mask);
static s32              coex_register_bt_cb_wrapper(coex_func_cb_t cb);
static s32              coex_schm_register_btdm_callback_wrapper(void *callback);
static s32              coex_wifi_channel_get_wrapper(u8 *primary,
                                                      u8 *secondary);
static s32              coex_register_wifi_channel_change_callback_wrapper(void *cb);
static s32              lt_task_create(void * entry, const char * name,
                                        u32 stack_depth, void *param,
                                        u32 prio, void * task_handle);
static bool             wifi_env_is_chip(void);
static void             wifi_set_intr(int32_t cpu_no, uint32_t intr_source,
                                         uint32_t intr_num, int32_t intr_prio);
static void             wifi_clear_intr(uint32_t intr_source, uint32_t intr_num);
static void             esp_set_isr(int32_t n, void *f, void *arg);
static void *           esp_thread_semphr_get(void);
static void *           lt_event_group_create(void);
static void             lt_event_group_delete(void *event);
static uint32_t         lt_event_group_set_bits(void *event, uint32_t bits);
static uint32_t         lt_event_group_clear_bits(void *event, uint32_t bits);
static uint32_t         lt_event_group_wait_bits(void *event,
                                                    uint32_t bits_to_wait_for,
                                                    int clear_on_exit,
                                                    int wait_for_all_bits,
                                                    uint32_t block_time_tick);
static void             lt_task_delay(uint32_t tick);
static int32_t          lt_task_ms_to_tick(uint32_t ms);
static void *           lt_task_get_current_task(void);
static int32_t          lt_task_get_max_priority(void);
static int32_t          lt_event_post(esp_event_base_t event_base,
                                        int32_t event_id,
                                        void *event_data,
                                        size_t event_data_size,
                                        uint32_t ticks);
static uint32_t         lt_get_free_heap_size(void);
static void             esp_dport_access_stall_other_cpu_start(void);
static void             esp_dport_access_stall_other_cpu_end(void);
static void             wifi_apb80m_request(void);
static void             wifi_apb80m_release(void);
void             esp32_phy_disable(void);
void             esp32_phy_enable(void);
static void             esp32_phy_enable_clock(void);
static void             esp32_phy_disable_clock(void);
static int              wifi_phy_update_country_info(const char *country);
static int              esp_wifi_read_mac(uint8_t *mac, uint32_t type);
static void             lt_timer_arm(void *timer, uint32_t tmout, bool repeat);
static void             wifi_reset_mac(void);
static void             wifi_clock_enable(void);
static void             wifi_clock_disable(void);
static void             wifi_rtc_enable_iso(void);
static void             wifi_rtc_disable_iso(void);
static int              esp_nvs_set_i8(uint32_t handle, const char *key,
                                        int8_t value);
static int              esp_nvs_get_i8(uint32_t handle, const char *key,
                                        int8_t *out_value);
static int              esp_nvs_set_u8(uint32_t handle, const char *key,
                                        uint8_t value);
static int              esp_nvs_set_u16(uint32_t handle, const char *key,
                                            uint16_t value);
static int              esp_nvs_get_u16(uint32_t handle, const char *key,
                                            uint16_t *out_value);
static int              esp_nvs_open(const char *name, uint32_t open_mode,
                                        uint32_t *out_handle);
static void             esp_nvs_close(uint32_t handle);
static int              esp_nvs_commit(uint32_t handle);
static int              esp_nvs_set_blob(uint32_t handle, const char *key,
                                            const void *value, size_t length);
static int              esp_nvs_get_blob(uint32_t handle, const char *key,
                                            void *out_value, size_t *length);
static int              esp_nvs_erase_key(uint32_t handle, const char *key);
static int              lt_get_random(uint8_t *buf, size_t len);
static int              lt_get_time(void *t);
static void             lt_log_write(uint32_t level,
                                        const char *tag,
                                        const char *format, ...);
static void             lt_log_writev(uint32_t level, const char *tag,
                                        const char *format, va_list args);
static uint32_t         lt_log_timestamp(void);
static void *           lt_realloc_internal(void *ptr, size_t size);
static void *           lt_calloc_internal(size_t n, size_t size);
static void *           lt_zalloc_internal(size_t size);
static void *           lt_wifi_malloc(size_t size);
static void *           lt_wifi_realloc(void *ptr, size_t size);
static void *           lt_wifi_calloc(size_t n, size_t size);
static void *           lt_wifi_zalloc(size_t size);
static void *           lt_wifi_create_queue(int queue_len, int item_size);
static void             lt_wifi_delete_queue(void * pQueue);
static int              wifi_coex_init(void);
static void             wifi_coex_deinit(void);
static int              wifi_coex_enable(void);
static void             wifi_coex_disable(void);
static uint32_t         esp_coex_status_get(void);
static void             esp_coex_condition_set(uint32_t type, bool dissatisfy);
static int              esp_coex_wifi_request(uint32_t event, uint32_t latency,
                                                uint32_t duration);
static int              esp_coex_wifi_release(uint32_t event);
static int              wifi_coex_wifi_set_channel(uint8_t primary, uint8_t secondary);
static int              wifi_coex_get_event_duration(uint32_t event,
                                                        uint32_t *duration);
static int              wifi_coex_get_pti(uint32_t event, uint8_t *pti);
static void             wifi_coex_clear_schm_status_bit(uint32_t type,
                                                        uint32_t status);
static void             wifi_coex_set_schm_status_bit(uint32_t type,
                                                        uint32_t status);
static int              wifi_coex_set_schm_interval(uint32_t interval);
static uint32_t         wifi_coex_get_schm_interval(void);
static uint8_t          wifi_coex_get_schm_curr_period(void);
static void *           wifi_coex_get_schm_curr_phase(void);
static int              wifi_coex_set_schm_curr_phase_idx(int idx);
static int              wifi_coex_get_schm_curr_phase_idx(void);
static void *           esp_wifi_malloc(unsigned int size);
static int              esp_event_id_map(int event_id);
static int              esp_nvs_get_u8(uint32_t handle, const char *key,
                                        uint8_t *out_value);
static void             esp_evt_work_cb(void * data);
static void             esp_evt_work_complete_cb(LTThread_ReleaseReason, void * data);
static inline void      phy_digital_regs_load(void);

static void FreeHandleList(LTList *handleList);

/****************************************************************************
 * Static variables
 ****************************************************************************/

static volatile u32           g_nLastCycleCount     = 0;
static LT_SIZE                disableMask           = 0;
static u32                    disableCount          = 0;
static LTThread               g_hThread_wifi_wrk    = 0;
static LTThread               g_hThread_wifi_drv    = 0;
static LTThread               g_hThread_wifi_timers = 0;
static LTList                 g_threads;
static LTList                 g_timers;

static handler_irq_info g_irq_info[] = {
    {
        NULL,
        NULL,
        -1,
        esp_ble_helper_handler0
    },
    {
        NULL,
        NULL,
        -1,
        esp_ble_helper_handler1
    },
    {
        NULL,
        NULL,
        -1,
        esp_ble_helper_handler2
    },
    {
        NULL,
        NULL,
        -1,
        esp_ble_helper_handler3
    }
};


static int g_num_irq = sizeof(g_irq_info) / sizeof(g_irq_info[0]);

/* number of fractional bit for g_btdm_lpcycle_us */

static const DRAM_ATTR u8 g_btdm_lpcycle_us_frac = LT_ESP32_RTC_CLK_CAL_FRACT;

/* measured average low power clock period in micro seconds */

static const DRAM_ATTR u32 g_btdm_lpcycle_us = 2 << (g_btdm_lpcycle_us_frac);

static u8 wifi_mac_address[6] = {0};

/****************************************************************************
 * Enums
 ****************************************************************************/

enum esp_bt_coex_log_level
{
    ESP_BT_COEX_LOG_LEVEL_NONE = 0,
    ESP_BT_COEX_LOG_LEVEL_ERROR,
    ESP_BT_COEX_LOG_LEVEL_WARNING,
    ESP_BT_COEX_LOG_LEVEL_INFO,
    ESP_BT_COEX_LOG_LEVEL_DEBUG,
    ESP_BT_COEX_LOG_LEVEL_VERBOSE
};

enum wifi_adpt_evt_e
{
    WIFI_ADPT_EVT_SCAN_DONE = 0,
    WIFI_ADPT_EVT_STA_START,
    WIFI_ADPT_EVT_STA_CONNECT,
    WIFI_ADPT_EVT_STA_DISCONNECT,
    WIFI_ADPT_EVT_STA_AUTHMODE_CHANGE,
    WIFI_ADPT_EVT_STA_STOP,
    WIFI_ADPT_EVT_STA_BEACON_TIMEOUT,
    WIFI_ADPT_EVT_MAX,
};

enum esp32_rtc_xtal_freq_e {
    RTC_XTAL_FREQ_AUTO = 0,     /* Automatic XTAL frequency detection */
    RTC_XTAL_FREQ_40M = 40,     /* 40 MHz XTAL */
    RTC_XTAL_FREQ_26M = 26,     /* 26 MHz XTAL */
    RTC_XTAL_FREQ_24M = 24,     /* 24 MHz XTAL */
};

enum esp32_rtc_xtal_freq_e rtc_get_xtal(void)
__attribute__((alias("esp32_rtc_clk_xtal_freq_get")));



/****************************************************************************
 * Structures
 ****************************************************************************/

/* Wi-Fi event private data */

struct evt_adpt
{
  s32 id;               /* Event ID */
  u8 buf[0];           /* Event private data */
};

/* Wi-Fi time private data */
struct time_adpt {
    time_t      sec;          /* Second value */
    suseconds_t usec;         /* Micro second value */
};

coex_adapter_funcs_t g_coex_adapter_funcs = {
    ._version = COEX_ADAPTER_VERSION,
    ._spin_lock_create = esp_spin_lock_create,
    ._spin_lock_delete = esp_spin_lock_delete,
    ._int_enable = esp_wifi_int_restore,
    ._int_disable = esp_wifi_int_disable,
    ._task_yield_from_isr = lt_task_yield_from_isr,
    ._semphr_create = lt_sem_create,
    ._semphr_delete = lt_sem_delete,
    ._semphr_take_from_isr = lt_sem_take_from_isr,
    ._semphr_give_from_isr = lt_sem_give_from_isr,
    ._semphr_take = lt_sem_take,
    ._semphr_give = lt_sem_give,
    ._is_in_isr = wifi_is_in_isr,
    ._malloc_internal =  lt_malloc_internal,
    ._free = esp_free,
    ._timer_disarm = lt_timer_disarm,
    ._timer_done = lt_timer_done,
    ._timer_setfn = lt_timer_setfn,
    ._timer_arm_us = lt_timer_arm_us,
    ._esp_timer_get_time = lt_timer_get_time,
    ._magic = COEX_ADAPTER_MAGIC,
};

const struct osi_funcs_t g_osi_funcs = {
  ._magic                                      = OSI_MAGIC_VALUE,
  ._version                                    = OSI_VERSION,
  ._set_isr                                    = esp_ble_set_isr,
  ._ints_on                                    = xt_ints_on_wrapper,
  ._interrupt_disable                          = interrupt_disable,
  ._interrupt_restore                          = interrupt_restore,
  ._task_yield                                 = lt_task_yield,
  ._task_yield_from_isr                        = lt_task_yield_from_isr,
  ._semphr_create                              = lt_sem_create,
  ._semphr_delete                              = lt_sem_delete,
  ._semphr_take_from_isr                       = lt_sem_take_from_isr,
  ._semphr_give_from_isr                       = lt_sem_give_from_isr,
  ._semphr_take                                = lt_sem_take,
  ._semphr_give                                = lt_sem_give,
  ._mutex_create                               = lt_mutex_create,
  ._mutex_delete                               = lt_mutex_delete,
  ._mutex_lock                                 = lt_mutex_lock,
  ._mutex_unlock                               = lt_mutex_unlock,
  ._queue_create                               = lt_queue_create,
  ._queue_delete                               = lt_queue_delete,
  ._queue_send                                 = lt_queue_send,
  ._queue_send_from_isr                        = lt_queue_send_from_isr,
  ._queue_recv                                 = lt_queue_recv,
  ._queue_recv_from_isr                        = queue_recv_from_isr_wrapper,
  ._task_create                                = lt_task_create_pinned_to_core,
  ._task_delete                                = lt_task_delete,
  ._is_in_isr                                  = is_in_isr_wrapper,
  ._cause_sw_intr_to_core                      = cause_sw_intr_to_core_wrapper,
  ._malloc                                     = esp_malloc,
  ._malloc_internal                            = esp_malloc,
  ._free                                       = esp_free,
  ._read_efuse_mac                             = read_mac_wrapper,
  ._srand                                      = srand_wrapper,
  ._rand                                       = lt_rand_stub,
  ._btdm_lpcycles_2_us                         = btdm_lpcycles_2_us,
  ._btdm_us_2_lpcycles                         = btdm_us_2_lpcycles,
  ._btdm_sleep_check_duration                  = btdm_sleep_check_duration,
  ._coex_bt_wakeup_request                     = coex_bt_wakeup_request,
  ._coex_bt_wakeup_request_end                 = coex_bt_wakeup_request_end,
  ._coex_bt_request                            = coex_bt_request_wrapper,
  ._coex_bt_release                            = coex_bt_release_wrapper,
  ._coex_register_bt_cb                        = coex_register_bt_cb_wrapper,
  ._coex_bb_reset_lock                         = coex_bb_reset_lock,
  ._coex_bb_reset_unlock                       = coex_bb_reset_unlock,
  ._coex_schm_register_btdm_callback           = coex_schm_register_btdm_callback_wrapper,
  ._coex_schm_status_bit_clear                 = coex_schm_status_bit_clear,
  ._coex_schm_status_bit_set                   = coex_schm_status_bit_set,
  ._coex_schm_interval_get                     = coex_schm_interval_get,
  ._coex_schm_curr_period_get                  = coex_schm_curr_period_get,
  ._coex_schm_curr_phase_get                   = coex_schm_curr_phase_get,
  ._coex_wifi_channel_get                      = coex_wifi_channel_get_wrapper,
  ._coex_register_wifi_channel_change_callback = coex_register_wifi_channel_change_callback_wrapper,
};

/* Wi-Fi OS adapter data */
/* NOTE: non-static here to match the vendor header, also probably used directly by binaries */
wifi_osi_funcs_t g_wifi_osi_funcs = {
    ._version = ESP_WIFI_OS_ADAPTER_VERSION,
    ._env_is_chip = wifi_env_is_chip,
    ._set_intr = wifi_set_intr,
    ._clear_intr = wifi_clear_intr,
    ._set_isr = esp_set_isr,
    ._ints_on = xt_ints_on,
    ._ints_off = xt_ints_off,
    ._is_from_isr = is_in_isr_wrapper,
    ._spin_lock_create = esp_spin_lock_create,
    ._spin_lock_delete = esp_spin_lock_delete,
    ._wifi_int_disable = esp_wifi_int_disable,
    ._wifi_int_restore = esp_wifi_int_restore,
    ._task_yield_from_isr = lt_task_yield_from_isr,
    ._semphr_create = lt_sem_create,
    ._semphr_delete = lt_sem_delete,
    ._semphr_take = lt_sem_take,
    ._semphr_give = lt_sem_give,
    ._wifi_thread_semphr_get = esp_thread_semphr_get,
    ._mutex_create = lt_mutex_create,
    ._recursive_mutex_create = lt_mutex_create,
    ._mutex_delete = lt_mutex_delete,
    ._mutex_lock = lt_mutex_lock,
    ._mutex_unlock = lt_mutex_unlock,
    ._queue_create = lt_queue_create,
    ._queue_delete = lt_queue_delete,
    ._queue_send = lt_queue_send,
    ._queue_send_from_isr = lt_queue_send_from_isr,
    ._queue_send_to_back = lt_queue_send,
    ._queue_send_to_front = lt_queue_send_to_front,
    ._queue_recv = lt_queue_recv,
    ._queue_msg_waiting = lt_queue_msg_waiting,
    ._event_group_create = lt_event_group_create,
    ._event_group_delete = lt_event_group_delete,
    ._event_group_set_bits = lt_event_group_set_bits,
    ._event_group_clear_bits = lt_event_group_clear_bits,
    ._event_group_wait_bits = lt_event_group_wait_bits,
    ._task_create_pinned_to_core = lt_task_create_pinned_to_core,
    ._task_create = lt_task_create,
    ._task_delete = lt_task_delete,
    ._task_delay = lt_task_delay,
    ._task_ms_to_tick = lt_task_ms_to_tick,
    ._task_get_current_task = lt_task_get_current_task,
    ._task_get_max_priority = lt_task_get_max_priority,
    ._malloc = esp_wifi_malloc,
    ._free = esp_free,
    ._event_post = lt_event_post,
    ._get_free_heap_size = lt_get_free_heap_size,
    ._rand = (uint32_t (*)(void))lt_rand_stub,
    ._dport_access_stall_other_cpu_start_wrap =
    esp_dport_access_stall_other_cpu_start,
    ._dport_access_stall_other_cpu_end_wrap =
    esp_dport_access_stall_other_cpu_end,
    ._wifi_apb80m_request = wifi_apb80m_request,
    ._wifi_apb80m_release = wifi_apb80m_release,
    ._phy_disable = esp32_phy_disable,
    ._phy_enable = esp32_phy_enable,
    ._phy_common_clock_enable = esp32_phy_enable_clock,
    ._phy_common_clock_disable = esp32_phy_disable_clock,
    ._phy_update_country_info = wifi_phy_update_country_info,
    ._read_mac = esp_wifi_read_mac,
    ._timer_arm = lt_timer_arm,
    ._timer_disarm = lt_timer_disarm,
    ._timer_done = lt_timer_done,
    ._timer_setfn = lt_timer_setfn,
    ._timer_arm_us = lt_timer_arm_us,
    ._wifi_reset_mac = wifi_reset_mac,
    ._wifi_clock_enable = wifi_clock_enable,
    ._wifi_clock_disable = wifi_clock_disable,
    ._wifi_rtc_enable_iso = wifi_rtc_enable_iso,
    ._wifi_rtc_disable_iso = wifi_rtc_disable_iso,
    ._esp_timer_get_time = lt_timer_get_time,
    ._nvs_set_i8 = esp_nvs_set_i8,
    ._nvs_get_i8 = esp_nvs_get_i8,
    ._nvs_set_u8 = esp_nvs_set_u8,
    ._nvs_get_u8 = esp_nvs_get_u8,
    ._nvs_set_u16 = esp_nvs_set_u16,
    ._nvs_get_u16 = esp_nvs_get_u16,
    ._nvs_open = esp_nvs_open,
    ._nvs_close = esp_nvs_close,
    ._nvs_commit = esp_nvs_commit,
    ._nvs_set_blob = esp_nvs_set_blob,
    ._nvs_get_blob = esp_nvs_get_blob,
    ._nvs_erase_key = esp_nvs_erase_key,
    ._get_random = lt_get_random,
    ._get_time = lt_get_time,
    ._random = (long unsigned int (*)(void))lt_rand_stub,
    ._log_write = lt_log_write,
    ._log_writev = lt_log_writev,
    ._log_timestamp = lt_log_timestamp,
    ._malloc_internal =  lt_malloc_internal,
    ._realloc_internal = lt_realloc_internal,
    ._calloc_internal = lt_calloc_internal,
    ._zalloc_internal = lt_zalloc_internal,
    ._wifi_malloc = lt_wifi_malloc,
    ._wifi_realloc = lt_wifi_realloc,
    ._wifi_calloc = lt_wifi_calloc,
    ._wifi_zalloc = lt_wifi_zalloc,
    ._wifi_create_queue = lt_wifi_create_queue,
    ._wifi_delete_queue = lt_wifi_delete_queue,
    ._coex_init = wifi_coex_init,
    ._coex_deinit = wifi_coex_deinit,
    ._coex_enable = wifi_coex_enable,
    ._coex_disable = wifi_coex_disable,
    ._coex_status_get = esp_coex_status_get,
    ._coex_condition_set = esp_coex_condition_set,
    ._coex_wifi_request = esp_coex_wifi_request,
    ._coex_wifi_release = esp_coex_wifi_release,
    ._coex_wifi_channel_set = wifi_coex_wifi_set_channel,
    ._coex_event_duration_get = wifi_coex_get_event_duration,
    ._coex_pti_get = wifi_coex_get_pti,
    ._coex_schm_status_bit_clear = wifi_coex_clear_schm_status_bit,
    ._coex_schm_status_bit_set = wifi_coex_set_schm_status_bit,
    ._coex_schm_interval_set = wifi_coex_set_schm_interval,
    ._coex_schm_interval_get = wifi_coex_get_schm_interval,
    ._coex_schm_curr_period_get = wifi_coex_get_schm_curr_period,
    ._coex_schm_curr_phase_get = wifi_coex_get_schm_curr_phase,
    ._coex_schm_curr_phase_idx_set = wifi_coex_set_schm_curr_phase_idx,
    ._coex_schm_curr_phase_idx_get = wifi_coex_get_schm_curr_phase_idx,
    ._magic = ESP_WIFI_OS_ADAPTER_MAGIC,
};

/****************************************************************************
 * static library, interface pointers, and initialization thereof
 ****************************************************************************/
static LTAtomic              s_referenceCount       = { 0 };
static LTCore               *s_core                 = NULL;
static ILTThread            *s_iThread              = NULL;
static LTMutex              *s_mutex_WiFiApi        = NULL;

typedef struct {
    LTList_Node node;
    LTHandle    handle;
} HandleListNode;

typedef struct ESP32AdapterQueue {
    LTList queuedNodes;
    LTList freeNodes;
    u32 queueLen;
    u32 itemSize;
} ESP32AdapterQueue;

LT_STATIC_ASSERT_SIZE_32_64(ESP32AdapterQueue, 24, 56);
LT_STATIC_ASSERT_SIZE_32_64(LTList_Node, 8, 16);
#define QUEUE_IS_FULL_SLEEP_TIME LTTime_Milliseconds(1)

// See LT-1252 regarding these locks
#ifdef DISABLE_WIFIAPI_LOCKS
#define LOCK_WIFIAPI()
#define UNLOCK_WIFIAPI()
#else
#define LOCK_WIFIAPI()   s_mutex_WiFiApi->API->Lock(s_mutex_WiFiApi)
#define UNLOCK_WIFIAPI() s_mutex_WiFiApi->API->Unlock(s_mutex_WiFiApi)
#endif

static LTTime TicksToAbsTime(u32 ticks) {
    if (ticks == 0)          return LTTime_Zero();
    if (ticks == 0xFFFFFFFF) return LTTime_Infinite();
    return LTTime_Add(s_core->GetKernelTime(), LTTime_Milliseconds(TICK2MSEC(ticks)));
}

/******************************************************************************/
// LT-1211: Track the last few WiFiApi mutex locks. Whenever a TryLock fails to
// get the lock, dump the tracking info to the log. Within the log line, the
// negative line number is the call that failed. This format is intentionally
// kept minimal to allow it to be stored across a crash.

typedef struct {
    LTThread thread; // thread that wants the lock
    s16      line;   // line number in Esp32DriverWiFi.c
} MutexTrack;
static MutexTrack s_Tracks[8] = {};
static LTAtomic   s_TrackIndex;

static char *GetThreadName(LTThread thread) {
    static char name[kLTThread_MaxNameBuff];
    s_iThread->GetName(thread, name);
    name[5] = 0;
    return name;
}

void LTEsp32OSAdapter_LockWiFiApi(int line) {
    // Do not lock the s_hMutex_WiFiApi on the wifi driver thread.
    // It can deadlock when calling LOCK_WIFIAPI() via a callback
    if (s_iThread->GetCurrentThread() == g_hThread_wifi_drv) return;

    // Track the last N lock locations; log lock conflict
    u16 index = LTAtomic_FetchAdd(&s_TrackIndex, 1);
    u16 i = index & 7;
    s_Tracks[i].line  = line;
    s_Tracks[i].thread = s_iThread->GetCurrentThread();
    bool okay = s_mutex_WiFiApi->API->TryLock(s_mutex_WiFiApi);
    if (!okay) {  // debug: || i == 7
        s_Tracks[i].line = -s_Tracks[i].line; // marks lock that failed
        char (*name)[8][6] = lt_malloc(sizeof(*name));
        lt_strncpyTerm((*name)[0], GetThreadName(s_Tracks[0].thread), 6);
        lt_strncpyTerm((*name)[1], GetThreadName(s_Tracks[1].thread), 6);
        lt_strncpyTerm((*name)[2], GetThreadName(s_Tracks[2].thread), 6);
        lt_strncpyTerm((*name)[3], GetThreadName(s_Tracks[3].thread), 6);
        lt_strncpyTerm((*name)[4], GetThreadName(s_Tracks[4].thread), 6);
        lt_strncpyTerm((*name)[5], GetThreadName(s_Tracks[5].thread), 6);
        lt_strncpyTerm((*name)[6], GetThreadName(s_Tracks[6].thread), 6);
        lt_strncpyTerm((*name)[7], GetThreadName(s_Tracks[7].thread), 6);
        LTLOG("trk", "%s:%d %s:%d %s:%d %s:%d %s:%d %s:%d %s:%d %s:%d",
            (*name)[0], s_Tracks[0].line,
            (*name)[1], s_Tracks[1].line,
            (*name)[2], s_Tracks[2].line,
            (*name)[3], s_Tracks[3].line,
            (*name)[4], s_Tracks[4].line,
            (*name)[5], s_Tracks[5].line,
            (*name)[6], s_Tracks[6].line,
            (*name)[7], s_Tracks[7].line
        );
        lt_free(name);
        s_mutex_WiFiApi->API->Lock(s_mutex_WiFiApi); // debug: if (!okay)
    }
}

void LTEsp32OSAdapter_UnlockWiFiApi(void) {
    if (s_iThread->GetCurrentThread() == g_hThread_wifi_drv) return;
    s_mutex_WiFiApi->API->Unlock(s_mutex_WiFiApi);
}

void LTEsp32OSAdapter_LibInit(void) {
    if (LTAtomic_FetchAdd(&s_referenceCount, 1) == 0) {
        s_core = LT_GetCore();
        s_mutex_WiFiApi = lt_createobject(LTMutex);
        s_iThread = lt_getlibraryinterface(ILTThread, s_core);
        LTList_Init(&g_threads);
        LTList_Init(&g_timers);
    }
}


void LTEsp32OSAdapter_LibFini(void) {
    if (LTAtomic_FetchSubtract(&s_referenceCount, 1) == 1) {
        FreeHandleList(&g_threads);
        lt_destroyobject(s_mutex_WiFiApi);
        s_mutex_WiFiApi = NULL;
        s_iThread = NULL;
        s_core    = NULL;
    }
}

/****************************************************************************
 * Definitions of static functions
 ****************************************************************************/

static bool AddHandleToList(LTList *list, LTHandle handle) {
    HandleListNode *node = lt_malloc(sizeof(HandleListNode));
    if (!node) return false;
    node->handle = handle;
    LTList_AddTail(list, &node->node);
    return true;
}

static void FreeHandleList(LTList *list) {
    LTList_ForEach(pNode, list) {
        HandleListNode *objectNode = LT_CONTAINER_OF(pNode, HandleListNode, node);
        s_core->DestroyHandle(objectNode->handle);
        lt_free(objectNode);
    }
    LTList_EndForEach;
    LTList_Init(list);
}

/****************************************************************************
 * Name: esp_wifi_int_disable
 *
 * Description:
 *   Enter critical section by disabling interrupts and taking the spin lock
 *   if in SMP mode.
 *
 * Input Parameters:
 *   wifi_int_mux - Spin lock data pointer
 *
 * Returned Value:
 *   CPU PS value.
 *
 ****************************************************************************/

static u32 IRAM_ATTR esp_wifi_int_disable(void * wifi_int_mux)
{
    LOG_ENTRY_ISR();
#ifdef LT_HAS_SMP
    /* Create a real spinlock */
    LT_ASSERT(0);
#else
    LT_UNUSED(wifi_int_mux);
    return (u32)s_core->Disable();
#endif
}

/****************************************************************************
 * Name: esp_spin_lock_create
 *
 * Description:
 *   Create spin lock in SMP mode
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Spin lock data pointer
 *
 ****************************************************************************/

static void *esp_spin_lock_create(void)
{
    LOG_ENTRY();
#ifdef LT_HAS_SMP
    /* Create a real spinlock */
    LT_ASSERT(0);
#else
    /* If return NULL, code may check fail  */
    return (void *)1;
#endif
}

/****************************************************************************
 * Name: esp_spin_lock_delete
 *
 * Description:
 *   Delete spin lock
 *
 * Input Parameters:
 *   lock - Spin lock data pointer
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void esp_spin_lock_delete(void *lock)
{
    LOG_ENTRY();
    LT_UNUSED(lock);
#ifdef LT_HAS_SMP
    /* Create a real spinlock */
    LT_ASSERT(0);
#else
    LT_ASSERT((int)lock == 1);
#endif
}

/****************************************************************************
 * Name: esp_wifi_int_restore
 *
 * Description:
 *   Exit from critical section by enabling interrupts and releasing the spin
 *   lock if in SMP mode.
 *
 * Input Parameters:
 *   wifi_int_mux - Spin lock data pointer
 *   tmp          - CPU PS value.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void IRAM_ATTR esp_wifi_int_restore(void *wifi_int_mux, u32 tmp)
{
    LOG_ENTRY_ISR();
#ifdef LT_HAS_SMP
    /* Create a real spinlock */
    LT_ASSERT(0);
#else
    LT_UNUSED(wifi_int_mux);
    s_core->Enable(tmp);
#endif
}

/****************************************************************************/
/* Create counting semaphore                                                */
/****************************************************************************/
void * lt_sem_create(u32 max, u32 init)
{
    LT_ASSERT(init <= max);
    LTCountingSemaphore *sem = lt_createobject(LTCountingSemaphore);
    if (!sem) return NULL;
    sem->API->Init(sem, max, init);
    LOG_ENTRY_SEM(sem);
    return (void *)sem;
}

/****************************************************************************/
/* Delete counting semaphore                                                */
/****************************************************************************/
void lt_sem_delete(void * pSem)
{
    LOG_ENTRY_SEM(pSem);
    if (pSem) lt_destroyobject((LTCountingSemaphore *)pSem);
}

/****************************************************************************/
/* Take counting semaphore                                                  */
/****************************************************************************/
static s32 lt_sem_take_from_isr(void *pSem, void *hptw)
{
    if (pSem == NULL) return false;
    *(int *)hptw = 0;
    LTCountingSemaphore *sem = (LTCountingSemaphore *)pSem;
    return sem->API->TryWait(sem) ? true : false;
}

s32 lt_sem_take(void * pSem, u32 block_ticks)
{
    if (!pSem) return false;
    LTCountingSemaphore *sem = (LTCountingSemaphore *)pSem;
    LTTime timeout;
    if (block_ticks == 0)          timeout = LTTime_Zero();
    else if (block_ticks == 0xFFFFFFFF) timeout = LTTime_Infinite();
    else                           timeout = LTTime_Milliseconds(TICK2MSEC(block_ticks));
    return sem->API->Wait(sem, timeout) ? true : false;
}

/****************************************************************************/
/* Give counting semaphore                                                  */
/****************************************************************************/
static s32 lt_sem_give_from_isr(void *pSem, void *hptw) LT_ISR_SAFE
{
    if (pSem == NULL) return false;
    *(int *)hptw = 0;
    LTCountingSemaphore *sem = (LTCountingSemaphore *)pSem;
    sem->API->Signal(sem);
    return true;
}

s32 lt_sem_give(void * pSem)
{
    if (!pSem) return false;
    LTCountingSemaphore *sem = (LTCountingSemaphore *)pSem;
    sem->API->SignalFromThread(sem);
    return true;
}

static int wifi_is_in_isr(void)
{
    return is_in_isr_wrapper();
}

#define WIFI_RX_BUFFER_SIZE 1696 /* WiFi allocates memory for RX ring buffers */
#define MAX_FRAMES (LT_ESP32_WIFI_DYNAMIC_RXBUF_NUM)
static char s_packpool[MAX_FRAMES*WIFI_RX_BUFFER_SIZE];
static LTAtomic s_freemask = { ((1 << MAX_FRAMES) - 1) };
#define ADDR_IN_POOL(addr) ((((char * )(addr)) >= s_packpool) && (((char * )(addr)) < (s_packpool+sizeof(s_packpool))))

static void *lt_pool_alloc(size_t size)
{
    if (size == WIFI_RX_BUFFER_SIZE) {
        for(u32 i=0; i<MAX_FRAMES; i++) {
            u32 msk = LTAtomic_Load(&s_freemask);
            while (msk & (1 << i)) {
                if (LTAtomic_CompareAndExchange(&s_freemask, msk, msk & ~(1 << i))) {
                    return (s_packpool+i*(WIFI_RX_BUFFER_SIZE));
                }
                msk = LTAtomic_Load(&s_freemask);
            }
        }
    }
    return NULL;
}

static void lt_pool_free(void *ptr)
{
    u32 i = ((u32)ptr - (u32)s_packpool)/WIFI_RX_BUFFER_SIZE;
    u32 msk = LTAtomic_Load(&s_freemask);
    while(!(msk & (1 << i))) {
        if (LTAtomic_CompareAndExchange(&s_freemask, msk, msk | (1 << i))) {
            return;
        } else {
            msk = LTAtomic_Load(&s_freemask);
        }
    }
    LT_ASSERT(0 && "Double free");
}
/****************************************************************************
 * Name: lt_malloc_internal
 *
 * Description:
 *   Drivers allocate a block of memory
 *
 * Input Parameters:
 *   size - memory size
 *
 * Returned Value:
 *   Memory pointer
 *
 ****************************************************************************/

static void *lt_malloc_internal(size_t size)
{
    LOG_ENTRY();
    return lt_malloc(size);
}

/****************************************************************************
 * Name: esp_free
 *
 * Description:
 *   Free a block of memory
 *
 * Input Parameters:
 *   ptr - memory block
 *
 * Returned Value:
 *   No
 *
 ****************************************************************************/

static void esp_free(void *ptr)
{
    if (ADDR_IN_POOL(ptr)) {
        lt_pool_free(ptr);
    } else {
        lt_free(ptr);
    }
}

/****************************************************************************
 * Name: lt_timer_disarm
 *
 * Description:
 *   Disable timer
 *
 * Input Parameters:
 *   ptimer - timer data pointer
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void KillZombieTimers(void *data)
{
    LT_UNUSED(data);
    LTList_ForEach(pNode, &g_timers) {
        timerData_t *pTimerData = LT_CONTAINER_OF(pNode, timerData_t, node);
        if (LTAtomic_CompareAndExchange(&pTimerData->timerState, kTimerZombie, kTimerStopped)) {
            s_iThread->KillTimer(g_hThread_wifi_timers, DispatchAndKillTimer, pTimerData);
        }
    } LTList_EndForEach;
}

static void lt_timer_disarm(void *ptimer) LT_ISR_SAFE
{
    struct ets_timer *ets_timer = (struct ets_timer *)ptimer;
    timerData_t * pTimerData = (timerData_t *)ets_timer->priv;
    LOG_ENTRY_TIMER(ptimer);
    if (ets_timer->expire == TIMER_INITIALIZED_VAL) {
        if (!s_core->InsideInterruptContext() && s_iThread->IsCurrentThread(g_hThread_wifi_timers)) {
            if (LTAtomic_CompareAndExchange(&pTimerData->timerState, kTimerRunning, kTimerStopped)) {
                s_iThread->KillTimer(g_hThread_wifi_timers, DispatchAndKillTimer, pTimerData);
            }
        } else if (LTAtomic_CompareAndExchange(&pTimerData->timerState, kTimerRunning, kTimerZombie)) {
            s_iThread->QueueTaskProcIfRequired(g_hThread_wifi_timers, KillZombieTimers, NULL, NULL);
        }
    }
}

/****************************************************************************
 * Name: lt_timer_done
 *
 * Description:
 *   Disable and free timer
 *
 * Input Parameters:
 *   ptimer - timer data pointer
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void lt_timer_done(void *ptimer)
{
    struct ets_timer *ets_timer = (struct ets_timer *)ptimer;
    timerData_t * pTimerData = (timerData_t *)ets_timer->priv;
    LOG_ENTRY_TIMER(ptimer);
    if (ets_timer->expire == TIMER_INITIALIZED_VAL) {
        lt_timer_disarm(ptimer);
        ets_timer->expire = 0;
        LTList_Remove(&pTimerData->node);
        lt_free(pTimerData);
        ets_timer->priv = NULL;
    }
    return;
}

/****************************************************************************
 * Name: lt_timer_setfn
 *
 * Description:
 *   Set timer callback function and private data
 *
 * Input Parameters:
 *   ptimer    - Timer data pointer
 *   pfunction - Callback function
 *   parg      - Callback function private data
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void lt_timer_setfn(void *ptimer, void *pfunction, void *parg)
{
    struct ets_timer *ets_timer = (struct ets_timer *)ptimer;
    LOG_ENTRY_TIMER(ptimer);
    if (ets_timer->expire != TIMER_INITIALIZED_VAL) {
        ets_timer->priv = NULL;
    }
    timerData_t * pTimerData = (timerData_t *)ets_timer->priv;
    if (ets_timer->priv == NULL) {
        pTimerData = (timerData_t *)lt_malloc(sizeof(timerData_t));
        LTAtomic_Store(&pTimerData->timerState, kTimerStopped);
        LTList_AddTail(&g_timers, &pTimerData->node);
        ets_timer->priv = pTimerData;
        ets_timer->expire = TIMER_INITIALIZED_VAL;
    }
    if(pTimerData) {
        pTimerData->pClientData = parg;
        pTimerData->pTimerProc  = (LTThread_TimerProc *)pfunction;
    }
}

static void lt_timer_arm_user_context(void *data)
{
    timerData_t *pTimerData = (timerData_t *)data;
    s_iThread->SetTimer(g_hThread_wifi_timers, LTTime_Microseconds(pTimerData->interval_us), DispatchAndKillTimer, NULL, pTimerData);
}

static void lt_timer_arm_us(void *ptimer, u32 us, bool repeat) LT_ISR_SAFE
{
    LOG_ENTRY_TIMER_ARM(ptimer, us, repeat);
    struct ets_timer *ets_timer = (struct ets_timer *)ptimer;
    timerData_t * pTimerData = (timerData_t *)ets_timer->priv;
    if (ets_timer->expire == TIMER_INITIALIZED_VAL) {
        pTimerData->reload      = repeat;
        pTimerData->interval_us = us;
        LTAtomic_Store(&pTimerData->timerState, kTimerRunning);

        if (!s_core->InsideInterruptContext()) {
            s_iThread->SetTimer(g_hThread_wifi_timers, LTTime_Microseconds(us), DispatchAndKillTimer, NULL, pTimerData);
        } else {
            s_iThread->QueueTaskProc(g_hThread_wifi_timers, lt_timer_arm_user_context, NULL, pTimerData);
        }
    }
}

/****************************************************************************
 * Name: lt_timer_get_time
 *
 * Description:
 *   Get system time of type int64_t
 *
 * Input Parameters:
 *   periph - No mean
 *
 * Returned Value:
 *   System time
 *
 ****************************************************************************/

static int64_t lt_timer_get_time(void)
{
    LOG_ENTRY();
    return (int64_t)LTTime_GetMicroseconds(s_core->GetKernelTime());
}

static void DispatchAndKillTimer(void * pClientData)
{
    timerData_t *pTimerData = (timerData_t *)pClientData;
    LOG_ENTRY();
    if (!pTimerData) return;
    u32 state = LTAtomic_Load(&pTimerData->timerState);
    if (state != kTimerRunning) return;
    if (!pTimerData->reload) {
        if (LTAtomic_CompareAndExchange(&pTimerData->timerState, kTimerRunning, kTimerStopped)) {
            s_iThread->KillTimer(g_hThread_wifi_timers, DispatchAndKillTimer, pClientData);
        }
    }
    /* call the timer callback */
    pTimerData->pTimerProc(pTimerData->pClientData);
}

int wifi_bt_coexist_init(void)
{
    coex_dbg_set_log_level(ESP_BT_COEX_LOG_LEVEL_INFO);
    esp_coex_adapter_register(&g_coex_adapter_funcs);
    coex_pre_init();
    return 0;
}

/****************************************************************************
 * Name: __assert_func
 *
 * Description:
 *   Delete timer and free resource
 *
 * Input Parameters:
 *   file  - assert file
 *   line  - assert line
 *   func  - assert function
 *   expr  - assert condition
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void __assert_func(const char *file, int line,
                   const char *func, const char *expr)
{
    LTLOG_REDALERT("assert","failed in %s, %s:%d (%s)", func, file, line, expr);
    LT_ASSERT(0);
    while(1);
}

int __wrap_sprintf(char* str, const char* fmt, ...)
{
    int ret;
    lt_va_list args;
    lt_va_start(args, fmt);
    /* FIXME: insecure - 1024?  */
    ret = lt_vsnprintf(str, 1024, fmt, args);
    lt_va_end(args);
    return ret;
}
int __wrap_puts(const char* str)
{
    s_core->ConsoleStomp("%s", str);
    return 0;
}

int __wrap_gettimeofday(struct timeval *restrict tv,
                        void* tz)
{
    LT_UNUSED(tz);
    LTTime currTime = s_core->GetKernelTime();
    LTTime secTime = LTTime_Seconds(LTTime_GetSeconds(currTime));
    tv->tv_sec  = (time_t)LTTime_GetSeconds(currTime);
    tv->tv_usec = (long)LTTime_GetMicroseconds(LTTime_Subtract(currTime, secTime));
    return 0;
}

int coexist_printf(const char *format, ...)
{
#ifdef LT_DEBUG_WIRELESS_INFO
    lt_va_list args;
    lt_va_start(args, format);
    LTLOGV("coexist", format, args);
    lt_va_end(args);
#else
    LT_UNUSED(format);
#endif
    return 0;
}

/****************************************************************************
 * Functions needed by libphy.a
 ****************************************************************************/
/****************************************************************************
 * Name: esp_dport_access_reg_read
 *
 * Description:
 *   Read regitser value safely in SMP
 *
 * Input Parameters:
 *   reg - Register address
 *
 * Returned Value:
 *   Register value
 *
 ****************************************************************************/

u32 IRAM_ATTR esp_dport_access_reg_read(u32 reg)
{
    return (*(volatile u32 *)(reg));
}
/****************************************************************************
 * Name: phy_enter_critical
 *
 * Description:
 *   Enter critical state
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   CPU PS value
 *
 ****************************************************************************/

u32 IRAM_ATTR phy_enter_critical(void)
{
    return (u32)s_core->Disable();
}

/****************************************************************************
 * Name: phy_exit_critical
 *
 * Description:
 *   Exit from critical state
 *
 * Input Parameters:
 *   level - CPU PS value
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void IRAM_ATTR phy_exit_critical(u32 level)
{
    s_core->Enable(level);
}

/****************************************************************************
 * Name: phy_printf
 *
 * Description:
 *   Output format string and its arguments
 *
 * Input Parameters:
 *   format - format string
 *
 * Returned Value:
 *   0
 *
 ****************************************************************************/

int phy_printf(const char *format, ...)
{

#ifdef LT_DEBUG_WIRELESS_INFO
    lt_va_list args;
    lt_va_start(args, format);
    LTLOGV("phy", format, args);
    lt_va_end(args);
#else
    LT_UNUSED(format);
#endif
    return 0;
}

/****************************************************************************
 * Name: esp32_clk_val_is_valid
 *
 * Description:
 *   Values of RTC_XTAL_FREQ_REG and RTC_APB_FREQ_REG are
 *   stored as two copies in lower and upper 16-bit halves.
 *   These are the routines to work with such a representation.
 *
 * Input Parameters:
 *   val   - register value
 *
 * Returned Value:
 *   true:  Valid register value.
 *   false: Invalid register value.
 *
 ****************************************************************************/

static inline bool esp32_clk_val_is_valid(u32 val)
{
    return (val & 0xffff) == ((val >> 16) & 0xffff)
           && val != 0 && val != UINT32_MAX;
}

/****************************************************************************
 * Name: esp32_rtc_clk_xtal_freq_get
 *
 * Description:
 *   Get main XTAL frequency
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   XTAL frequency (one of enum esp32_rtc_xtal_freq_e values)
 *
 ****************************************************************************/

enum esp32_rtc_xtal_freq_e IRAM_ATTR esp32_rtc_clk_xtal_freq_get(void)
{
    /* We may have already written XTAL value into RTC_XTAL_FREQ_REG */

    u32 xtal_freq_reg = ESP32_REG(RTC_CNTL_XTAL_FREQ);

    if (!esp32_clk_val_is_valid(xtal_freq_reg)) {
        return RTC_XTAL_FREQ_AUTO;
    }

    return (xtal_freq_reg & ~RTC_DISABLE_ROM_LOG) & UINT16_MAX;
}

/****************************************************************************
 * Name: esp_set_isr
 *
 * Description:
 *   Register interrupt function
 *
 * Input Parameters:
 *   n   - Interrupt ID
 *   f   - Interrupt function
 *   arg - Function private data
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
static void esp_set_isr(int32_t n, void *f, void *arg)
{
    LTLOG_DEBUG("set.isr", "n=%d f=%p arg=%p irq=%d\n", (int)n, f, arg, (int)n);
    LT_ASSERT(n == kEsp32_IrqNumber_WiFiMAC);
    for (int i=0; i<g_num_irq; i++) {
        if(g_irq_info[i].n == -1 || g_irq_info[i].n == n) {
            g_irq_info[i].f_user = f;
            g_irq_info[i].arg = arg;
            g_irq_info[i].n = n;
            s_core->SetInterruptVector(kEsp32_IrqNumber_WiFiMAC,(LTCore_InterruptHandler *)g_irq_info[i].stub, kEsp32_IrqPriority_WiFiMAC);
            return;
        }
    }
}

/****************************************************************************
 * Name: esp_ble_set_isr
 *
 * Description:
 *   Register interrupt function
 *
 * Input Parameters:
 *   n   - Interrupt ID
 *   f   - Interrupt function
 *   arg - Function private data
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static xt_handler esp_ble_set_isr(s32 n, xt_handler f, void *arg)
{

    LTLOG_DEBUG("set.isr", "n=%d f=%p arg=%p irq=%d\n", (int)n, f, arg, (int)n);
    LT_ASSERT(n == kEsp32_IrqNumber_BT_BB_NMI ||
              n == kEsp32_IrqNumber_RWBLE_IRQ ||
              n == kEsp32_IrqNumber_RWBT_NMI);

    for (s32 i = 0; i < g_num_irq; i++) {
        if(g_irq_info[i].n == -1 || g_irq_info[i].n == n) {
            g_irq_info[i].f_user = f;
            g_irq_info[i].arg    = arg;
            g_irq_info[i].n      = n;
            LTLOG_DEBUG("set.isr", "i=%d n=%d\n", (int)i, (int)n);
            s_core->SetInterruptVector(n, (LTCore_InterruptHandler *)g_irq_info[i].stub, kEsp32_IrqPriority_BLE);
            return NULL;
        }
    }
    return NULL;
}

/****************************************************************************
 * Name: interrupt_disable
 *
 * Description:
 *   Enter critical section by disabling interrupts and taking the spin lock
 *   if in SMP mode.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void IRAM_ATTR interrupt_disable(void)
{
    LT_SIZE nMask = s_core->Disable();
    if (disableCount++ == 0) {
        disableMask = nMask;
    } else {
        s_core->Enable(nMask);
    }
}

/****************************************************************************
 * Name: interrupt_restore
 *
 * Description:
 *   Exit from critical section by enabling interrupts and releasing the spin
 *   lock if in SMP mode.
 *
 * Input Parameters:
 *  None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void IRAM_ATTR interrupt_restore(void)
{
    LT_ASSERT(s_core->InterruptsAreDisabled());
    LT_ASSERT(disableCount >= 1);
    if (--disableCount == 0) {
        s_core->Enable(disableMask);
    }
}

static void IRAM_ATTR lt_task_yield(void)
{
    s_iThread->Yield();
}

static void IRAM_ATTR lt_task_yield_from_isr(void)
{
    return;
}

static void *lt_mutex_create(void)
{
    LTMutex *mutex = lt_createobject(LTMutex); LOG_ENTRY_MUTEX(mutex);
    if (!mutex) return NULL;
    return mutex;
}

static void lt_mutex_delete(void *pMtx)
{
    LTMutex *mutex = pMtx; LOG_ENTRY_MUTEX(mutex);
    lt_destroyobject(mutex);
}

static s32 lt_mutex_lock(void *pMtx)
{
    LTMutex *mutex = pMtx;
    if (mutex) { LOG_ENTRY_MUTEX(mutex);
        mutex->API->Lock(mutex);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static s32 lt_mutex_unlock(void *pMtx)
{
    LTMutex *mutex = pMtx;
    if (mutex) { LOG_ENTRY_MUTEX(mutex);
        mutex->API->Unlock(mutex);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static void * lt_queue_create(u32 queue_len, u32 item_size)
{
    LOG_ENTRY();

    LT_SIZE rawNodeSize = sizeof(LTList_Node) + ((item_size + 0x7) & ~0x7);
    LT_SIZE nBytes = sizeof(ESP32AdapterQueue) + queue_len * rawNodeSize;
    ESP32AdapterQueue * queue = (ESP32AdapterQueue *)lt_malloc(nBytes);
    if (! queue) return NULL;
    lt_memset(queue, 0, nBytes);
    LTList_Init(&queue->queuedNodes);
    LTList_Init(&queue->freeNodes);
    queue->queueLen = queue_len;
    queue->itemSize = item_size;
    LTList_Node *node = (LTList_Node *)(((u8 *)queue) + sizeof(ESP32AdapterQueue));
    while (queue_len--) {
        node->pPrev = node->pNext = node;
        LTList_AddTail(&queue->freeNodes, node);
        node = (LTList_Node *)((u8 *)node + rawNodeSize);
    }
    return (void *)queue;
}

static void lt_queue_delete(void * pQueue)
{
    LOG_ENTRY();
    //NOTE: we should probably empty the queue first (dependso on the OS implementation)
    lt_free(pQueue);
}

/****************************************************************************
 * Name: lt_queue_send
 *
 * Description:
 *   Send message of low priority to queue within a certain period of time
 *
 * Input Parameters:
 *   pQueue - Message queue data pointer
 *   item   - Message data pointer
 *   ticks  - Wait ticks
 *
 * Returned Value:
 *   True if success or false if fail
 *
 ****************************************************************************/

static s32 lt_queue_send(void * pQueue, void *item, u32 ticks)
{
    LT_SIZE mask;
    LTList_Node *node;
    LTTime expiration = TicksToAbsTime(ticks);
    ESP32AdapterQueue *queue = (ESP32AdapterQueue *)pQueue;
    do {
        mask = s_core->Disable();
        node = queue->freeNodes.pNext;
        if (node != &queue->freeNodes) {
            LTList_Remove(node);
            lt_memcpy((u8 *)node + sizeof(*node), item, queue->itemSize);
            LTList_AddTail(&queue->queuedNodes, node);
            s_core->Enable(mask);
            LOG_MSG_SUCCESS(item, queue->itemSize);
            return true;
        }
        s_core->Enable(mask);
        s_iThread->Sleep(QUEUE_IS_FULL_SLEEP_TIME);
    }
    while (LTTime_IsLessThan(s_core->GetKernelTime(), expiration));
    LOG_MSG_FAIL();
    return false;
}

/****************************************************************************
 * Name: lt_queue_send_from_isr
 *
 * Description:
 *   Send message of low priority to queue in ISR within
 *   a certain period of time
 *
 * Input Parameters:
 *   pQueue- Message queue data pointer
 *   item  - Message data pointer
 *   hptw  - No mean
 *
 * Returned Value:
 *   True if success or false if fail
 *
 ****************************************************************************/

static s32 lt_queue_send_from_isr(void * pQueue, void *item, void *hptw)
{
    LOG_ENTRY_ISR2();
    ESP32AdapterQueue *queue = (ESP32AdapterQueue *)pQueue;

    *((int *)hptw) = false;

    LT_SIZE mask = s_core->Disable();
    LTList_Node *node = queue->freeNodes.pNext;
    if (node == &queue->freeNodes) { s_core->Enable(mask); return false; }
    LTList_Remove(node);
    lt_memcpy((u8 *)node + sizeof(*node), item, queue->itemSize);
    LTList_AddTail(&queue->queuedNodes, node);
    s_core->Enable(mask);
    return true;
}

/****************************************************************************
 * Name: lt_queue_send_to_front
 *
 * Description:
 *   Send message of high priority to queue within a certain period of time
 *
 * Input Parameters:
 *   pQueue- Message queue data pointer
 *   item  - Message data pointer
 *   ticks - Wait ticks
 *
 * Returned Value:
 *   True if success or false if fail
 *
 ****************************************************************************/

static s32 lt_queue_send_to_front(void * pQueue, void *item,
                                      u32 ticks)
{
    LT_SIZE mask;
    LTList_Node *node;
    ESP32AdapterQueue *queue = (ESP32AdapterQueue *)pQueue;
    LTTime expiration = TicksToAbsTime(ticks);
    do {
        mask = s_core->Disable();
        node = queue->freeNodes.pNext;
        if (node != &queue->freeNodes) {
            LTList_Remove(node);
            lt_memcpy((u8 *)node + sizeof(*node), item, queue->itemSize);
            LTList_InsertHead(&queue->queuedNodes, node);
            s_core->Enable(mask);
            LOG_MSG_SUCCESS(item, queue->itemSize);
            return true;
        }
        s_core->Enable(mask);
        s_iThread->Sleep(QUEUE_IS_FULL_SLEEP_TIME);
    }
    while (LTTime_IsLessThan(s_core->GetKernelTime(), expiration));
    LOG_MSG_FAIL();
    return false;
}


/****************************************************************************
 * Name: lt_queue_recv
 *
 * Description:
 *   Receive message from queue within a certain period of time
 *
 * Input Parameters:
 *   pQueue- Message queue data pointer
 *   item  - Message data pointer
 *   ticks - Wait ticks
 *
 * Returned Value:
 *   True if success or false if fail
 *
 ****************************************************************************/

static s32 lt_queue_recv(void * pQueue, void *item, u32 ticks)
{
    LT_SIZE mask;
    LTList_Node *node;
    ESP32AdapterQueue *queue = (ESP32AdapterQueue *)pQueue;
    LTTime expiration = TicksToAbsTime(ticks);
    do {
        mask = s_core->Disable();
        node = queue->queuedNodes.pNext;
        if (node != &queue->queuedNodes) {
            LTList_Remove(node);
            lt_memcpy(item, (u8 *)node + sizeof(*node), queue->itemSize);
            LTList_AddTail(&queue->freeNodes, node);
            s_core->Enable(mask);
            LOG_MSG_SUCCESS(item, queue->itemSize);
            return true;
        }
        s_core->Enable(mask);
        s_iThread->Sleep(QUEUE_IS_FULL_SLEEP_TIME);
    }
    while (LTTime_IsLessThan(s_core->GetKernelTime(), expiration));
    LOG_MSG_FAIL();
    return false;
}

/****************************************************************************
 * Name: queue_recv_from_isr_wrapper
 *
 * Description:
 *   Receive message from queue within a certain period of time
 *
 * Input Parameters:
 *   pQueue - Message queue data pointer
 *   pItem  - Message data pointer
 *   pHptw - Unused
 *
 * Returned Value:
 *   True if success or false if fail
 *
 ****************************************************************************/

static s32 IRAM_ATTR queue_recv_from_isr_wrapper(void * pQueue,
                                                 void * pItem,
                                                 void * pHptw)
{
    LT_UNUSED(pHptw);
    LTLOG("LT.QUEUE.RECV.FROM.ISR.WRAPPER", "queue: 0x%lx, item: 0x%lx", LT_Pu32(pQueue), LT_Pu32(pItem));
    return 0;
}

/****************************************************************************
 * Name: lt_queue_msg_waiting
 *
 * Description:
 *   Get message number in the message queue
 *
 * Input Parameters:
 *   pQueue - Message queue data pointer
 *
 * Returned Value:
 *   Message number
 *
 ****************************************************************************/

static u32 lt_queue_msg_waiting(void * pQueue)
{
    u32 size = 0;
    ESP32AdapterQueue *queue = (ESP32AdapterQueue *)pQueue;

    LT_SIZE flags = s_core->Disable();
    LTList_ForEach(pNode, &queue->queuedNodes) { size++; } LTList_EndForEach;
    s_core->Enable(flags);

    LOG_ENTRY_MSG_WAIT(size);
    return size;
}

/****************************************************************************
 * Name: lt_task_create_pinned_to_core
 *
 * Description:
 *   Create task and bind it to target CPU, the task will run when it
 *   is created - TODO: SMP
 *
 * Input Parameters:
 *   entry       - Task entry
 *   name        - Task name
 *   stack_depth - Task stack size
 *   param       - Task private data
 *   prio        - Task priority
 *   task_handle - Task handle pointer which is used to pause, resume
 *                 and delete the task
 *   coreID     - CPU which the task runs in
 *
 * Returned Value:
 *   True if success or false if fail
 *
 ****************************************************************************/

static s32 lt_task_create_pinned_to_core(void * entry,
                                              const char *name,
                                              u32    stack_depth,
                                              void * param,
                                              u32    prio,
                                              void * task_handle,
                                              u32    coreID)
{
    LTLOG_DEBUG("on.core", "%d", (int)coreID);
    LT_UNUSED(coreID);
    return lt_task_create(entry, name, stack_depth, param, prio, task_handle);
}

/****************************************************************************
 * Name: lt_task_delete
 *
 * Description:
 *   Delete the target task
 *
 * Input Parameters:
 *   task_handle - Task handle pointer which is used to pause, resume
 *                 and delete the task
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void lt_task_delete(void *task_handle)
{
    LOG_ENTRY();
    LTThread hThread;
    if (task_handle) {
        hThread = VOIDPTR_TO_LTHANDLE(task_handle);
    } else {
        hThread = s_iThread->GetCurrentThread();
    }
    s_iThread->Terminate(hThread);
}

/****************************************************************************
 * Name: is_in_isr_wrapper
 *
 * Description: Check if in isr - not required at the moment
 *
 * Input Parameters:
 *  None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static bool IRAM_ATTR is_in_isr_wrapper(void) LT_ISR_SAFE
{
    return s_core->InsideInterruptContext();
}

/****************************************************************************
 * Name: cause_sw_intr_to_core_wrapper
 *
 * Description:
 *   Just a wrapper to cause_sw_intr
 *
 * Input Parameters:
 *  coreID - ID of the CPU core, not used.
 *  nIntr - Number of the software interrupt
 *
 * Returned Value:
 *   Always return OK.
 *
 ****************************************************************************/

static s32 IRAM_ATTR cause_sw_intr_to_core_wrapper(s32 coreID, s32 nIntr)
{
    LT_UNUSED(coreID);
    LT_ESP32_XTHAL_SET_INTSET((1 << nIntr));
    return ESP_OK;
}

/****************************************************************************
 * Name: convert_mac
 *
 * Description:
 *   Helper function for converting a MAC address to a BT or SOFT-AP address
 *
 * Input Parameters:
 *  mac  - MAC address buffer pointer
 *  type - type of the address left in mac
 *
 * Returned Value:
 *   0 if success or -1 if fail.
 *
 ****************************************************************************/
static s32 convert_mac(u8 mac[6], esp_mac_type_t type) {
    u8 tmp;
    int i;
    if (type == ESP_MAC_WIFI_SOFTAP) {
        tmp = mac[0];
        for (i = 0; i < 64; i++) {
            mac[0] = tmp | 0x02;
            mac[0] ^= i << 2;

            if (mac[0] != tmp) {
                break;
            }
        }

        if (i >= 64) {
            LTLOG_YELLOWALERT("error.softap.mac","Failed to generate SoftAP MAC\n");
            return -1;
        }
    }

    if (type == ESP_MAC_BT) {
        tmp = mac[0];
        for (i = 0; i < 64; i++) {
            mac[0] = tmp | 0x02;
            mac[0] ^= i << 2;

            if (mac[0] != tmp) {
                break;
            }
        }
        mac[5] += 1;
    }
    return 0;
}

/****************************************************************************
 * Name: esp_read_efuse_mac
 *
 * Description:
 *   Read MAC address from efuse
 *
 * Input Parameters:
 *   mac  - MAC address buffer pointer
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static s32 IRAM_ATTR esp_read_efuse_mac(u8 mac[6])
{
    uint32_t regval[2];
    uint8_t *data = (uint8_t *)regval;
    uint8_t crc;
    int i;

    regval[0] = ESP32_REG(MAC_ADDR0);
    regval[1] = ESP32_REG(MAC_ADDR1);

    crc = data[6];
    for (i = 0; i < MAC_LEN; i++) {
        mac[i] = data[5 - i];
    }

    if (crc != esp_crc8(mac, MAC_LEN)) {
        LTLOG("read.mac.error.crc","Failed to check MAC address CRC\n");
        return -1;
    }
    return 0;
}

/****************************************************************************
 * Name: esp_read_mac
 *
 * Description:
 *   Read MAC address supplied at the initialization time
 *
 * Input Parameters:
 *   mac  - MAC address buffer pointer
 *   type - MAC address type
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type)
{
    LOG_ENTRY();
    if (type > ESP_MAC_BT) {
        LTLOG("read.mac.error", "Input type is error=%d\n", type);
        return -1;
    }

    lt_memcpy(mac, wifi_mac_address, 6);
    return convert_mac(mac, type);
}

/****************************************************************************
 * Name: esp_malloc
 *
 * Description:
 *   Allocate a block of memory
 *
 * Input Parameters:
 *   size - memory size
 *
 * Returned Value:
 *   Memory pointer
 *
 ****************************************************************************/

static void * esp_malloc(u32 size)
{
    return lt_malloc(size);
}

/****************************************************************************
 * Name: esp_wifi_malloc
 *
 * Description:
 *   Allocate a block of memory
 *
 * Input Parameters:
 *   size - memory size
 *
 * Returned Value:
 *   Memory pointer
 *
 ****************************************************************************/

static void * esp_wifi_malloc(unsigned int size)
{
    return lt_malloc(size);
}

static s32 IRAM_ATTR read_mac_wrapper(u8 mac[6])
{
    esp_read_efuse_mac(mac);
    return convert_mac(mac, ESP_MAC_BT);
}

/****************************************************************************
 * Name: srand_wrapper
 *
 * Description:
 *   Seed the random number generator - not required at the moment
 *
 * Input Parameters:
 *  None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void IRAM_ATTR srand_wrapper(u32 seed)
{
    LT_UNUSED(seed);
}

/****************************************************************************
 * Name: lt_rand_stub
 *
 * Description:
 *   Get random data of type int - not required at the moment
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Random data
 *
 ****************************************************************************/

static u32 lt_get_ccount(void) LT_ISR_SAFE
{
    u32 res;
    RSR(CCOUNT, res);
    return res;
}

static s32 lt_rand_stub(void) LT_ISR_SAFE
{
    u32 cpuToApbFreqRatio = (g_ticks_per_us_pro == 0) ? 1 : g_ticks_per_us_pro / LT_MIN(g_ticks_per_us_pro, (u32)80);

    u32 ccount = 0;
    s32 res = 0;
    do {
        ccount = lt_get_ccount();
        res ^= ESP32_REG(WDEV_RND);
    } while ((ccount - g_nLastCycleCount) < (cpuToApbFreqRatio * APB_CYCLE_WAIT_NUM));
    g_nLastCycleCount = ccount;
    return res ^ ESP32_REG(WDEV_RND);
}

/****************************************************************************
 * Name: btdm_lpcycles_2_us
 *
 * Description:
 *    Converts a number of low power clock cycles into a duration in us
 *
 * Input Parameters:
 *    cycles - number of CPU cycles
 *
 * Returned Value:
 *    us - value equivalent to the CPU cycles
 *
 ****************************************************************************/

static u32 IRAM_ATTR btdm_lpcycles_2_us(u32 cycles)
{
    u64 us            = (u64)g_btdm_lpcycle_us * cycles;
    us                = (us + (1 << (g_btdm_lpcycle_us_frac - 1))) >> g_btdm_lpcycle_us_frac;
    return (u32)us;
}

/****************************************************************************
 * Name: btdm_us_2_lpcycles
 *
 * Description:
 * Converts a duration in half us into a number of low power clock cycles.
 *
 * Input Parameters:
 *  us
 *
 * Returned Value:
 *   cycles
 *
 ****************************************************************************/

static u32 IRAM_ATTR btdm_us_2_lpcycles(u32 us)
{
    return (u32) ((u64)(us) << g_btdm_lpcycle_us_frac) / g_btdm_lpcycle_us;
}

static bool btdm_sleep_check_duration(u32 * pSlotCnt)
{
    if (*pSlotCnt < LT_ESP32_BTDM_MIN_SLEEP_DURATION) {
        return false;
    }

    *pSlotCnt -= LT_ESP32_BTDM_MODEM_WAKE_UP_DELAY;
    return true;

}

static s32 IRAM_ATTR coex_bt_request_wrapper(u32 event, u32 latency, u32 duration)
{
    return coex_bt_request(event, latency, duration);
}

static s32 coex_bt_release_wrapper(u32 event)
{
    return coex_bt_release(event);
}

static void xt_ints_on_wrapper(u32 mask)
{
     xt_ints_on((unsigned int)mask);
}

static s32  coex_register_bt_cb_wrapper(coex_func_cb_t cb)
{
    return (s32)coex_register_bt_cb(cb);
}

static s32  coex_schm_register_btdm_callback_wrapper(void *callback)
{
    return (s32)coex_schm_register_btdm_callback(callback);
}

static s32  coex_wifi_channel_get_wrapper(u8 *primary, u8 *secondary)
{
    return (s32)coex_wifi_channel_get((uint8_t*)primary, (uint8_t*)secondary);
}

static s32  coex_register_wifi_channel_change_callback_wrapper(void *cb)
{
    return (s32)coex_register_wifi_channel_change_callback(cb);
}

#if 0
static void wrapThreadFunc(void * pClientData)
{
    taskData_t * pTaskData = (taskData_t *)pClientData;
    pTaskData->pTaskProc(pTaskData->pClientData);
}
#endif

/****************************************************************************
 * Name: lt_task_create
 *
 * Description:
 *   Create task and the task will run when it is created
 *
 * Input Parameters:
 *   entry       - Task entry
 *   name        - Task name
 *   stack_depth - Task stack size
 *   param       - Task private data
 *   prio        - Task priority
 *   task_handle - Task handle pointer which is used to pause, resume
 *                 and delete the task
 *
 * Returned Value:
 *   True if success or false if fail
 *
 ****************************************************************************/

static s32 lt_task_create(void * entry, const char * name,
                               u32 stack_depth, void *param,
                               u32 prio, void * task_handle)
{
    if (entry == NULL || name == NULL || stack_depth == 0) {
        return false;
    }

    prio = (prio > kLTThread_PriorityHighest) ? kLTThread_PriorityHighest : prio;

    g_hThread_wifi_drv = s_core->CreateThread(name);
    if (!g_hThread_wifi_drv) return false;
    if (!AddHandleToList(&g_threads, g_hThread_wifi_drv)) {
        s_core->DestroyHandle(g_hThread_wifi_drv);
        return false;
    }

    if (task_handle) *((LTThread*)task_handle) = g_hThread_wifi_drv;

    s_iThread->SetPriority(g_hThread_wifi_drv, prio);
    s_iThread->SetStackSize(g_hThread_wifi_drv, THREAD_STACK_SIZE_WIFI_DRIVER); // ignore stack_depth arg
    s_iThread->Start(g_hThread_wifi_drv, NULL, NULL);
    s_iThread->QueueTaskProc(g_hThread_wifi_drv, entry, NULL, param);

    return true;
}

static void esp_ble_helper_handler0(void)
{
    if (g_irq_info[0].f_user) {
        g_irq_info[0].f_user(g_irq_info[0].arg);
    }
}

static void esp_ble_helper_handler1(void)
{
    if (g_irq_info[1].f_user) {
        g_irq_info[1].f_user(g_irq_info[1].arg);
    }
}

static void esp_ble_helper_handler2(void)
{
    if (g_irq_info[2].f_user) {
        g_irq_info[2].f_user(g_irq_info[2].arg);
    }
}

static void esp_ble_helper_handler3(void)
{
    if (g_irq_info[3].f_user) {
        g_irq_info[3].f_user(g_irq_info[3].arg);
    }
}

void lt_ble_drv_init(void)
{
}

/****************************************************************************
 * Name: wifi_env_is_chip
 *
 * Description:
 *   Config chip environment
 *
 * Returned Value:
 *   True if on chip or false if on FPGA.
 *
 ****************************************************************************/

static bool wifi_env_is_chip(void)
{
    LOG_ENTRY();
    return true;
}

/****************************************************************************
 * Name: wifi_set_intr
 *
 * Description:
 *   Do nothing
 *
 * Input Parameters:
 *     cpu_no      - The CPU which the interrupt number belongs.
 *     intr_source - The interrupt hardware source number.
 *     intr_num    - The interrupt number CPU.
 *     intr_prio   - The interrupt priority.
 *
 * Returned Value:
 *     None
 *
 ****************************************************************************/

static void wifi_set_intr(int32_t cpu_no, uint32_t intr_source,
                          uint32_t intr_num, int32_t intr_prio)
{
    LTLOG_DEBUG("set.intr", "cpu_no=%d, intr_source=%d, intr_num=%u, intr_prio=%d\n",
                (int)cpu_no, (int)intr_source, (int)intr_num, (int)intr_prio);
    LT_ASSERT(cpu_no == 0);
    LT_ASSERT(intr_source == kEsp32_ExternalIrq_WIFIMAC);
    LT_ASSERT(intr_num == kEsp32_IrqNumber_WiFiMAC);
    LT_ASSERT(intr_prio == kEsp32_IrqPriority_WiFiMAC);
    if (cpu_no == 0 && intr_source == kEsp32_ExternalIrq_WIFIMAC && intr_num == kEsp32_IrqNumber_WiFiMAC && intr_prio == kEsp32_IrqPriority_WiFiMAC) {
        Esp32MapExternalToCPUIrq(0, kEsp32_ExternalIrq_WIFIMAC, kEsp32_IrqNumber_WiFiMAC);
    } else {
        LTLOG_REDALERT("set.intr.fatal", "Unexpected interrupt confioguration");
    }
}

static void IRAM_ATTR wifi_clear_intr(uint32_t intr_source,
                                      uint32_t intr_num)
{
    LOG_ENTRY();
    LT_UNUSED(intr_source);
    LT_UNUSED(intr_num);
}

static void ReleaseLTThreadSemaphore(LTThread_ReleaseReason releaseReason, void * pClientData) {
    LT_ASSERT(   (releaseReason == kLTThread_ReleaseReason_ThreadSpecificReset)   // thread specific client data was set to a new value or cleared
              || (releaseReason == kLTThread_ReleaseReason_ThreadSpecificPurge)); // thread is shutting down due to Terminate(); all thread specific data is released
    LT_UNUSED(releaseReason);
    lt_sem_delete(pClientData);
}

/****************************************************************************
 * Name: esp_thread_semphr_get
 *
 * Description:
 *   Get thread self's semaphore
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Semaphore data pointer
 *
 ****************************************************************************/

static void * esp_thread_semphr_get(void)
{
    void       *sem     = NULL;
    LTThread    hThread = s_iThread->GetCurrentThread();

    if (NULL == (sem = s_iThread->GetThreadSpecificClientData(hThread, ESP_THREAD_SPECIFIC_SEM_KEY))) {
        if (NULL != (sem = lt_sem_create(1, 0))) s_iThread->SetThreadSpecificClientData(hThread, ESP_THREAD_SPECIFIC_SEM_KEY, ReleaseLTThreadSemaphore, sem);
        else { LTLOG("get.th.sem.err","Failed to create semaphore\n"); }
    }

    return sem;
}


/****************************************************************************
 * Name: lt_event_group_create
 *
 * Description:
 *   LT does not support it
 *
 ****************************************************************************/

static void *lt_event_group_create(void)
{
    LT_ASSERT(0);
    return NULL;
}

/****************************************************************************
 * Name: esp_event_group_delete
 *
 * Description:
 *   LT does not support it
 *
 ****************************************************************************/

static void lt_event_group_delete(void *event)
{
    LT_UNUSED(event);
    LT_ASSERT(0);
}

/****************************************************************************
 * Name: lt_event_group_set_bits
 *
 * Description:
 *   LT does not support it
 *
 ****************************************************************************/

static uint32_t lt_event_group_set_bits(void *event, uint32_t bits)
{
    LT_UNUSED(event);
    LT_UNUSED(bits);
    LT_ASSERT(0);
    return false;
}

/****************************************************************************
 * Name: lt_event_group_clear_bits
 *
 * Description:
 *   LT does not support it
 *
 ****************************************************************************/

static uint32_t lt_event_group_clear_bits(void *event, uint32_t bits)
{
    LT_UNUSED(event);
    LT_UNUSED(bits);
    LT_ASSERT(0);
    return false;
}

/****************************************************************************
 * Name: lt_event_group_wait_bits
 *
 * Description:
 *   LT does not support it
 *
 ****************************************************************************/

static uint32_t lt_event_group_wait_bits(void *event,
                                         uint32_t bits_to_wait_for,
                                         int clear_on_exit,
                                         int wait_for_all_bits,
                                         uint32_t block_time_tick)
{
    LT_UNUSED(event);
    LT_UNUSED(bits_to_wait_for);
    LT_UNUSED(clear_on_exit);
    LT_UNUSED(wait_for_all_bits);
    LT_UNUSED(block_time_tick);
    LT_ASSERT(0);
    return false;
}

/****************************************************************************
 * Name: lt_task_delay
 *
 * Description:
 *   Current task wait for some ticks
 *
 * Input Parameters:
 *   tick - Waiting ticks
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void lt_task_delay(uint32_t tick)
{
    LOG_ENTRY();
    u32 ms = TICK2MSEC(tick);
    s_iThread->Sleep(LTTime_Milliseconds(ms));
}

/****************************************************************************
 * Name: lt_task_ms_to_tick
 *
 * Description:
 *   Transform from millim seconds to system ticks
 *
 * Input Parameters:
 *   ms - Millim seconds
 *
 * Returned Value:
 *   System ticks
 *
 ****************************************************************************/

static int32_t lt_task_ms_to_tick(uint32_t ms)
{
    return MSEC2TICK(ms);
}

/****************************************************************************
 * Name: lt_task_get_current_task
 *
 *
 ****************************************************************************/

static void * lt_task_get_current_task(void)
{
    LOG_ENTRY_GET_CURR_TASK();
    return (void*) s_iThread->GetCurrentThread();
}

/****************************************************************************
 * Name: lt_task_get_max_priority
 *
 * Description:
 *   Get OS task maximum priority
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Task maximum priority
 *
 ****************************************************************************/

static int32_t lt_task_get_max_priority(void)
{
    return kLTThread_PriorityHighest;
}

/****************************************************************************
 * Name: lt_event_post
 *
 * Description:
 *   Active work queue and let the work to process the cached event
 *
 * Input Parameters:
 *   event_base      - Event set name
 *   event_id        - Event ID
 *   event_data      - Event private data
 *   event_data_size - Event data size
 *   ticks           - Waiting system ticks
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int32_t lt_event_post(esp_event_base_t event_base,
                             int32_t event_id,
                             void *event_data,
                             size_t event_data_size,
                             uint32_t ticks)
{
    size_t size;
    int32_t id;
    struct evt_adpt *pevt_adpt;
    LT_UNUSED(event_base);
    LT_UNUSED(ticks); // TODO: should it be checked somewhere in event processing thread?
    LTLOG_DEBUG("evt.post", "Event: base=%s id=%d data=%p data_size=%d ticks=%lu\n", event_base,
                (int)event_id, event_data, (int)event_data_size, LT_Pu32(ticks));

    id = esp_event_id_map(event_id);

    if (id < 0) {
        LTLOG("evt.post.err1", "No process event %d\n", (int)event_id);
        return -1;
    }

    size = event_data_size + sizeof(struct evt_adpt);
    pevt_adpt = lt_malloc(size);
    if (!pevt_adpt) {
        LTLOG("evt.post.err2", "Failed to alloc %d memory\n", size);
        return -1;
    }

    pevt_adpt->id = id;
    lt_memcpy(pevt_adpt->buf, event_data, event_data_size);

    s_iThread->QueueTaskProc(g_hThread_wifi_wrk, esp_evt_work_cb, esp_evt_work_complete_cb, pevt_adpt);

    return 0;
}

/****************************************************************************
 * Name: lt_get_free_heap_size
 *
 * Description:
 *   Get free heap size by byte
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Free heap size
 *
 ****************************************************************************/

static uint32_t lt_get_free_heap_size(void)
{
    LOG_ENTRY();
    return s_core->GetAvailableSystemRAM();
}

static void esp_dport_access_stall_other_cpu_start(void)
{
#ifdef LT_HAS_SMP
    LT_ASSERT(0);
#endif
}

void esp_dport_access_stall_other_cpu_end(void)
{
#ifdef LT_HAS_SMP
    LT_ASSERT(0);
#endif
}

/****************************************************************************
 * Name: wifi_apb80m_request
 *
 * Description:
 *   Take Wi-Fi lock in auto-sleep
 *
 ****************************************************************************/

static void wifi_apb80m_request(void)
{

}

/****************************************************************************
 * Name: wifi_apb80m_release
 *
 * Description:
 *   Release Wi-Fi lock in auto-sleep
 *
 ****************************************************************************/

static void wifi_apb80m_release(void)
{
}

/* TODO: Revise that for SMP, these should be a interrupt disabling spinlocks */
/****************************************************************************
 * Name: esp32_phy_enable_clock
 *
 * Description:
 *   Enable PHY hardware clock
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void esp32_phy_enable_clock(void)
{
    LOG_ENTRY_ISR();
    LT_SIZE flags = s_core->Disable();
    if (g_phy_clk_en_cnt == 0) {
        u32 nValue = ESP32_REG(DPORT_WIFI_CLK_EN);
        nValue |= ESP32_REG_MASK(DPORT_WIFI_CLK_EN, WIFI_BT_COMMON);
        ESP32_REG(DPORT_WIFI_CLK_EN) = nValue;
    }
    g_phy_clk_en_cnt++;
    s_core->Enable(flags);
}

/****************************************************************************
 * Name: esp32_phy_disable_clock
 *
 * Description:
 *   Disable PHY hardware clock
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void esp32_phy_disable_clock(void)
{
    LOG_ENTRY_ISR();
    LT_SIZE flags = s_core->Disable();
    if (g_phy_clk_en_cnt > 0) {
        g_phy_clk_en_cnt--;
        if (g_phy_clk_en_cnt == 0) {
            u32 nValue = ESP32_REG(DPORT_WIFI_CLK_EN);
            nValue &= ~ESP32_REG_MASK(DPORT_WIFI_CLK_EN, WIFI_BT_COMMON);
            ESP32_REG(DPORT_WIFI_CLK_EN) = nValue;
        }
    }
    s_core->Enable(flags);
}
/****************************************************************************
 * Name: esp32_phy_disable
 *
 * Description:
 *   Deinitialize PHY hardware
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void esp32_phy_disable(void)
{
    LOG_ENTRY();
    LT_SIZE flags = s_core->Disable();

    g_phy_access_ref--;

    if (g_phy_access_ref == 0) {
        /* Disable PHY and RF. */

        phy_close_rf();

        /* Disable Wi-Fi/BT common peripheral clock.
         * Do not disable clock for hardware RNG.
         */

        esp32_phy_disable_clock();
    }
    s_core->Enable(flags);
}

/****************************************************************************
 * Name: esp32_phy_enable
 *
 * Description:
 *   Initialize PHY hardware
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void esp32_phy_enable(void)
{
    esp_phy_calibration_data_t *cal_data = NULL;
    LOG_ENTRY();
#ifdef LT_DEBUG_WIRELESS_INFO
    char *phy_version = get_phy_version_str();
    LTLOG_DEBUG("phy.enable", "phy_version %s\n", phy_version);
#endif
    if (g_phy_access_ref == 0 && g_is_phy_calibrated == false) {
        cal_data = lt_malloc(sizeof(esp_phy_calibration_data_t));
        if (!cal_data) {
            LTLOG("phy.enable.error", "ERROR: Failed to allocate PHY calibration data buffer.");
            return;
        }
        lt_memset(cal_data, 0, sizeof(esp_phy_calibration_data_t));
    }
    LT_SIZE flags = s_core->Disable();

    if (g_phy_access_ref == 0) {
        esp32_phy_enable_clock();
        if (g_is_phy_calibrated == false && cal_data) {
            register_chipv7_phy(&phy_init_data, cal_data, PHY_RF_CAL_FULL);
            g_is_phy_calibrated = true;
        } else {
            phy_wakeup_init();
            phy_digital_regs_load();
        }

        coex_bt_high_prio();
    }

    g_phy_access_ref++;
    s_core->Enable(flags);
    if (cal_data) {
        lt_free(cal_data);
    }
}


static int wifi_phy_update_country_info(const char *country)
{
    LT_UNUSED(country);
    return -1;
}

/****************************************************************************
 * Name: esp_wifi_read_mac
 *
 * Description:
 *   Read MAC address from efuse
 *
 * Input Parameters:
 *   mac  - MAC address buffer pointer
 *   type - MAC address type
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int esp_wifi_read_mac(uint8_t *mac, uint32_t type)
{
    LOG_ENTRY();
    return esp_read_mac(mac, type);
}

/****************************************************************************
 * Name: lt_timer_arm
 *
 * Description:
 *   Set timer timeout period and repeat flag
 *
 * Input Parameters:
 *   ptimer - timer data pointer
 *   ms     - millim seconds
 *   repeat - true: run cycle, false: run once
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void lt_timer_arm(void *ptimer, uint32_t ms, bool repeat)
{
    LOG_ENTRY_TIMER(ptimer);
    lt_timer_arm_us(ptimer, ms * 1000, repeat);
}

/****************************************************************************
 * Name: wifi_reset_mac
 *
 * Description:
 *   Reset Wi-Fi hardware MAC
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void wifi_reset_mac(void)
{
    LOG_ENTRY();
    LT_SIZE flags = s_core->Disable();
    u32 nValue = ESP32_REG(DPORT_WIFI_RST_EN);
    nValue |= ESP32_REG_MASK(DPORT_WIFI_RST_EN, MAC_RST_EN);
    ESP32_REG(DPORT_WIFI_RST_EN) = nValue;
    s_core->Enable(flags);
    /* No wait here ??? */
    flags = s_core->Disable();
    nValue = ESP32_REG(DPORT_WIFI_RST_EN);
    nValue &= ~ESP32_REG_MASK(DPORT_WIFI_RST_EN, MAC_RST_EN);
    ESP32_REG(DPORT_WIFI_RST_EN) = nValue;
    s_core->Enable(flags);
}

/****************************************************************************
 * Name: wifi_clock_enable
 *
 * Description:
 *   Enable Wi-Fi clock
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void wifi_clock_enable(void)
{
    LOG_ENTRY();
    LT_SIZE flags = s_core->Disable();
    u32 nValue = ESP32_REG(DPORT_WIFI_CLK_EN);
    nValue |= ESP32_REG_MASK(DPORT_WIFI_CLK_EN, WIFI_EN);
    ESP32_REG(DPORT_WIFI_CLK_EN) = nValue;
    s_core->Enable(flags);
}

/****************************************************************************
 * Name: wifi_clock_disable
 *
 * Description:
 *   Disable Wi-Fi clock
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void wifi_clock_disable(void)
{
    LOG_ENTRY();
    LT_SIZE flags = s_core->Disable();
    u32 nValue = ESP32_REG(DPORT_WIFI_CLK_EN);
    nValue &= ~ESP32_REG_MASK(DPORT_WIFI_CLK_EN, WIFI_EN);
    ESP32_REG(DPORT_WIFI_CLK_EN) = nValue;
    s_core->Enable(flags);
}

static void wifi_rtc_enable_iso(void)
{
}

static void wifi_rtc_disable_iso(void)
{
}

/****************************************************************************
 * Name: esp_nvs_set_i8
 *
 * Description:
 *   Save data of type int8_t into file system
 *
 * Input Parameters:
 *   handle - NVS handle
 *   key    - Data index
 *   value  - Stored data
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int esp_nvs_set_i8(uint32_t handle,
                          const char *key,
                          int8_t value)
{
    LT_UNUSED(handle);
    LT_UNUSED(key);
    LT_UNUSED(value);
    LT_ASSERT(0);
    return -1;
}

/****************************************************************************
 * Name: esp_nvs_get_i8
 *
 * Description:
 *   Read data of type int8_t from file system
 *
 * Input Parameters:
 *   handle    - NVS handle
 *   key       - Data index
 *   out_value - Read buffer pointer
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int esp_nvs_get_i8(uint32_t handle,
                          const char *key,
                          int8_t *out_value)
{
    LT_UNUSED(handle);
    LT_UNUSED(key);
    LT_UNUSED(out_value);
    LT_ASSERT(0);
    return -1;
}

/****************************************************************************
 * Name: esp_nvs_set_u8
 *
 * Description:
 *   Save data of type uint8_t into file system
 *
 * Input Parameters:
 *   handle - NVS handle
 *   key    - Data index
 *   value  - Stored data
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int esp_nvs_set_u8(uint32_t handle,
                          const char *key,
                          uint8_t value)
{
    LT_UNUSED(handle);
    LT_UNUSED(key);
    LT_UNUSED(value);
    LT_ASSERT(0);
    return -1;
}

/****************************************************************************
 * Name: esp_nvs_get_u8
 *
 * Description:
 *   Read data of type uint8_t from file system
 *
 * Input Parameters:
 *   handle    - NVS handle
 *   key       - Data index
 *   out_value - Read buffer pointer
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int esp_nvs_get_u8(uint32_t handle,
                          const char *key,
                          uint8_t *out_value)
{
    LT_UNUSED(handle);
    LT_UNUSED(key);
    LT_UNUSED(out_value);
    LT_ASSERT(0);
    return -1;
}

/****************************************************************************
 * Name: esp_nvs_set_u16
 *
 * Description:
 *   Save data of type uint16_t into file system
 *
 * Input Parameters:
 *   handle - NVS handle
 *   key    - Data index
 *   value  - Stored data
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int esp_nvs_set_u16(uint32_t handle,
                           const char *key,
                           uint16_t value)
{
    LT_UNUSED(handle);
    LT_UNUSED(key);
    LT_UNUSED(value);
    LT_ASSERT(0);
    return -1;
}

/****************************************************************************
 * Name: esp_nvs_get_u16
 *
 * Description:
 *   Read data of type uint16_t from file system
 *
 * Input Parameters:
 *   handle    - NVS handle
 *   key       - Data index
 *   out_value - Read buffer pointer
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int esp_nvs_get_u16(uint32_t handle,
                           const char *key,
                           uint16_t *out_value)
{
    LT_UNUSED(handle);
    LT_UNUSED(key);
    LT_UNUSED(out_value);
    LT_ASSERT(0);
    return -1;
}

/****************************************************************************
 * Name: esp_nvs_open
 *
 * Description:
 *   Create a file system storage data object
 *
 * Input Parameters:
 *   name       - Storage index
 *   open_mode  - Storage mode
 *   out_handle - Storage handle
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int esp_nvs_open(const char *name,
                        uint32_t open_mode,
                        uint32_t *out_handle)
{
    LT_UNUSED(name);
    LT_UNUSED(open_mode);
    LT_UNUSED(out_handle);
    LT_ASSERT(0);
    return -1;
}

/****************************************************************************
 * Name: esp_nvs_close
 *
 * Description:
 *   Close storage data object and free resource
 *
 * Input Parameters:
 *   handle - NVS handle
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static void esp_nvs_close(uint32_t handle)
{
    LT_UNUSED(handle);
    LT_ASSERT(0);
}

/****************************************************************************
 * Name: esp_nvs_commit
 *
 * Description:
 *   This function has no practical effect
 *
 ****************************************************************************/

static int esp_nvs_commit(uint32_t handle)
{
    LT_UNUSED(handle);
    return 0;
}

/****************************************************************************
 * Name: esp_nvs_set_blob
 *
 * Description:
 *   Save a block of data into file system
 *
 * Input Parameters:
 *   handle - NVS handle
 *   key    - Data index
 *   value  - Stored buffer pointer
 *   length - Buffer length
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int esp_nvs_set_blob(uint32_t handle,
                            const char *key,
                            const void *value,
                            size_t length)
{
    LT_UNUSED(handle);
    LT_UNUSED(key);
    LT_UNUSED(value);
    LT_UNUSED(length);
    LT_ASSERT(0);
    return -1;
}

/****************************************************************************
 * Name: esp_nvs_get_blob
 *
 * Description:
 *   Read a block of data from file system
 *
 * Input Parameters:
 *   handle    - NVS handle
 *   key       - Data index
 *   out_value - Read buffer pointer
 *   length    - Buffer length
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int esp_nvs_get_blob(uint32_t handle,
                            const char *key,
                            void *out_value,
                            size_t *length)
{
    LT_UNUSED(handle);
    LT_UNUSED(key);
    LT_UNUSED(out_value);
    LT_UNUSED(length);
    LT_ASSERT(0);
    return -1;
}

/****************************************************************************
 * Name: esp_nvs_erase_key
 *
 * Description:
 *   Read a block of data from file system
 *
 * Input Parameters:
 *   handle    - NVS handle
 *   key       - Data index
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int esp_nvs_erase_key(uint32_t handle, const char *key)
{
    LT_UNUSED(handle);
    LT_UNUSED(key);
    LT_ASSERT(0);
    return -1;
}

/****************************************************************************
 * Name: lt_get_random
 *
 * Description:
 *   Fill random data int given buffer of given length
 *
 * Input Parameters:
 *   buf - buffer pointer
 *   len - buffer length
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int lt_get_random(uint8_t *buf, size_t len) LT_ISR_SAFE
{
    LOG_ENTRY();
    if (!buf) {
        return -1;
    }

    while (len > 0) {
        u32 word = lt_rand_stub();
        u32 bytesToCopy = LT_MIN(len, sizeof(word));
        lt_memcpy(buf, &word, bytesToCopy);
        buf += bytesToCopy;
        len -= bytesToCopy;
    }

    return 0;
}

/****************************************************************************
 * Name: lt_get_time
 *
 * Description:
 *   Get std C time
 *
 * Input Parameters:
 *   t - buffer to store time of type timeval
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

static int lt_get_time(void *t)
{
    LOG_ENTRY();
    struct time_adpt * time_adpt = (struct time_adpt *)t;
    LTTime currTime = s_core->GetKernelTime();
    LTTime secTime  = LTTime_Seconds(LTTime_GetSeconds(currTime));
    time_adpt->sec  = (time_t)LTTime_GetSeconds(currTime);
    time_adpt->usec = (suseconds_t)LTTime_GetMicroseconds(LTTime_Subtract(currTime,secTime));
    return 0;
}

/****************************************************************************
 * Name: lt_log_write
 *
 * Description:
 *   Output log with by format string and its arguments
 *
 * Input Parameters:
 *   level  - log level, no mean here
 *   tag    - log TAG, no mean here
 *   format - format string
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void lt_log_write(uint32_t level,
                         const char *tag,
                         const char *format, ...)
{
    LT_UNUSED(tag);
    lt_va_list args;
    lt_va_start(args, format);
    switch (level) {
#ifdef LT_DEBUG_WIRELESS_ERROR
    case ESP_LOG_ERROR:
        LTLOGV_REDALERT(tag, format, args);
        break;
#endif
#ifdef LT_DEBUG_WIRELESS_WARN
    case ESP_LOG_WARN:
        LTLOGV_YELLOWALERT(tag, format, args);
        break;
#endif
#ifdef LT_DEBUG_WIRELESS_INFO
    case ESP_LOG_INFO:
        LTLOGV(tag, format, args);
        break;
    default:
        LTLOGV_DEBUG(tag, format, args);
        break;
#endif
    }
    lt_va_end(args);
}

/****************************************************************************
 * Name: lt_log_writev
 *
 * Description:
 *   Output log with by format string and its arguments
 *
 * Input Parameters:
 *   level  - log level, no mean here
 *   tag    - log TAG, no mean here
 *   format - format string
 *   args   - arguments list
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void lt_log_writev(uint32_t level, const char *tag,
                          const char *format, lt_va_list args)
{
    switch (level) {
#ifdef LT_DEBUG_WIRELESS_ERROR
    case ESP_LOG_ERROR:
        LTLOGV_REDALERT(tag, format, args);
        break;
#endif
#ifdef LT_DEBUG_WIRELESS_WARN
    case ESP_LOG_WARN:
        LTLOGV_YELLOWALERT(tag, format, args);
        break;
#endif
#ifdef LT_DEBUG_WIRELESS_INFO
    case ESP_LOG_INFO:
        LTLOGV(tag, format, args);
        break;
    default:
        LTLOGV_DEBUG(tag, format, args);
        break;
#else
    default:
        LT_UNUSED(tag);
        LT_UNUSED(format);
        LT_UNUSED(args);
#endif
    }
}

/****************************************************************************
 * Name: lt_realloc_internal
 *
 * Description:
 *   Drivers allocate a block of memory by old memory block
 *
 * Input Parameters:
 *   ptr  - old memory pointer
 *   size - memory size
 *
 * Returned Value:
 *   New memory pointer
 *
 ****************************************************************************/

static void * lt_realloc_internal(void *ptr, size_t size)
{
    LOG_ENTRY();
    LT_ASSERT(!ADDR_IN_POOL(ptr));
    return lt_realloc(ptr, size);
}

/****************************************************************************
 * Name: lt_calloc_internal
 *
 * Description:
 *   Drivers allocate some continuous blocks of memory
 *
 * Input Parameters:
 *   n    - memory block number
 *   size - memory block size
 *
 * Returned Value:
 *   New memory pointer
 *
 ****************************************************************************/

static void * lt_calloc_internal(size_t n, size_t size)
{
    LOG_ENTRY();
    void * mem = lt_malloc(n*size);
    if (mem) lt_memset(mem, 0, n*size);
    return mem;
}
void * __wrap_calloc(size_t n, size_t size)
{
    return lt_calloc_internal(n,size);
}
/****************************************************************************
 * Name: lt_zalloc_internal
 *
 * Description:
 *   Drivers allocate a block of memory and clear it with 0
 *
 * Input Parameters:
 *   size - memory size
 *
 * Returned Value:
 *   New memory pointer
 *
 ****************************************************************************/

static void * lt_zalloc_internal(size_t size)
{
    LOG_ENTRY();
    void * ptr = lt_malloc(size);
    if (ptr) {
        lt_memset(ptr, 0, size);
    }
    return ptr;
}

/****************************************************************************
 * Name: lt_wifi_malloc
 *
 * Description:
 *   Applications allocate a block of memory
 *
 * Input Parameters:
 *   size - memory size
 *
 * Returned Value:
 *   Memory pointer
 *
 ****************************************************************************/

static void * lt_wifi_malloc(size_t size)
{
    LOG_ENTRY();
    void * ptr = lt_pool_alloc(size);
    return (ptr)?(ptr):(lt_malloc(size));
}

/****************************************************************************
 * Name: lt_wifi_realloc
 *
 * Description:
 *   Applications allocate a block of memory by old memory block
 *
 * Input Parameters:
 *   ptr  - old memory pointer
 *   size - memory size
 *
 * Returned Value:
 *   New memory pointer
 *
 ****************************************************************************/

static void * lt_wifi_realloc(void *ptr, size_t size)
{
    LOG_ENTRY();
    LT_ASSERT(!ADDR_IN_POOL(ptr));
    return lt_realloc(ptr, size);
}

/****************************************************************************
 * Name: lt_wifi_calloc
 *
 * Description:
 *   Applications allocate some continuous blocks of memory
 *
 * Input Parameters:
 *   n    - memory block number
 *   size - memory block size
 *
 * Returned Value:
 *   New memory pointer
 *
 ****************************************************************************/

static void * lt_wifi_calloc(size_t n, size_t size)
{
    LOG_ENTRY();
    void *mem = lt_malloc(n*size);
    if (mem) lt_memset(mem, 0, n*size);
    return mem;
}

/****************************************************************************
 * Name: lt_wifi_zalloc
 *
 * Description:
 *   Applications allocate a block of memory and clear it with 0
 *
 * Input Parameters:
 *   size - memory size
 *
 * Returned Value:
 *   New memory pointer
 *
 ****************************************************************************/

static void * lt_wifi_zalloc(size_t size)
{
    LOG_ENTRY();
    return lt_zalloc_internal(size);
}

/****************************************************************************
 * Name: lt_wifi_create_queue
 *
 * Description:
 *   Create Wi-Fi static message queue
 *
 * Input Parameters:
 *   queue_len - queue message number
 *   item_size - message size
 *
 * Returned Value:
 *   Wi-Fi static message queue data pointer
 *
 ****************************************************************************/

static void * lt_wifi_create_queue(int queue_len, int item_size)
{
    wifi_static_queue_t * wifi_queue;
    LOG_ENTRY();
    wifi_queue = lt_malloc(sizeof(wifi_static_queue_t));
    if (!wifi_queue) {
        LTLOG("wifi.q.create.error","Failed to allocate\n");
        return NULL;
    }

    wifi_queue->handle = lt_queue_create(queue_len, item_size);
    if (!wifi_queue->handle) {
        LTLOG("wifi.q.create.error2","Failed to create queue\n");
        lt_free(wifi_queue);
        return NULL;
    }

    return wifi_queue;
}

/****************************************************************************
 * Name: lt_wifi_delete_queue
 *
 * Description:
 *   Delete Wi-Fi static message queue
 *
 * Input Parameters:
 *   queue - Wi-Fi static message queue data pointer
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void lt_wifi_delete_queue(void * pQueue)
{
    LOG_ENTRY();
    wifi_static_queue_t *wifi_queue = (wifi_static_queue_t *)pQueue;

    lt_queue_delete(wifi_queue->handle);
    lt_free(wifi_queue);
}

static int wifi_coex_init(void)
{
    return coex_init();
}

static void wifi_coex_deinit(void)
{
    coex_deinit();
}

static int wifi_coex_enable(void)
{
    return coex_enable();
}

/****************************************************************************
 * Name: lt_log_timestamp
 *
 * Description:
 *   Get system time by millim second
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   System time
 *
 ****************************************************************************/

static uint32_t lt_log_timestamp(void)
{
    return (uint32_t)(lt_timer_get_time() / 1000);
}

static uint32_t esp_coex_status_get(void)
{
    return coex_status_get();
}

static void esp_coex_condition_set(uint32_t type, bool dissatisfy)
{
    coex_condition_set(type, dissatisfy);
}

static int esp_coex_wifi_request(uint32_t event, uint32_t latency,
                                 uint32_t duration)
{
    return coex_wifi_request(event, latency, duration);
}

static int esp_coex_wifi_release(uint32_t event)
{
    return coex_wifi_release(event);
}

static int wifi_coex_wifi_set_channel(uint8_t primary, uint8_t secondary)
{
    return coex_wifi_channel_set(primary, secondary);
}

static int wifi_coex_get_event_duration(uint32_t event, uint32_t *duration)
{
    return coex_event_duration_get(event, duration);
}

static int wifi_coex_get_pti(uint32_t event, uint8_t *pti)
{
    LT_UNUSED(event);
    LT_UNUSED(pti);
    return 0;
}

static void wifi_coex_clear_schm_status_bit(uint32_t type, uint32_t status)
{
    coex_schm_status_bit_clear(type, status);
}

static void wifi_coex_set_schm_status_bit(uint32_t type, uint32_t status)
{
    coex_schm_status_bit_set(type, status);
}

static int wifi_coex_set_schm_interval(uint32_t interval)
{
    return coex_schm_interval_set(interval);
}

static uint32_t wifi_coex_get_schm_interval(void)
{
    return coex_schm_interval_get();
}

static uint8_t wifi_coex_get_schm_curr_period(void)
{
    return coex_schm_curr_period_get();
}

static void *wifi_coex_get_schm_curr_phase(void)
{
    return coex_schm_curr_phase_get();
}

static int wifi_coex_set_schm_curr_phase_idx(int idx)
{
    return coex_schm_curr_phase_idx_set(idx);
}

static int wifi_coex_get_schm_curr_phase_idx(void)
{
    return coex_schm_curr_phase_idx_get();
}

/****************************************************************************
 * Name: esp_event_id_map
 *
 * Description:
 *   Transform from esp-idf event ID to Wi-Fi adapter event ID
 *
 * Input Parameters:
 *   event_id - esp-idf event ID
 *
 * Returned Value:
 *   Wi-Fi adapter event ID
 *
 ****************************************************************************/

static int esp_event_id_map(int event_id)
{
    int id;
    LOG_ENTRY();
    if (event_id >= WIFI_EVENT_MAX) {
        return -1;
    }
    switch (event_id) {
    case WIFI_EVENT_SCAN_DONE:
        id = WIFI_ADPT_EVT_SCAN_DONE;
        break;
    case WIFI_EVENT_STA_START:
        id = WIFI_ADPT_EVT_STA_START;
        break;

    case WIFI_EVENT_STA_CONNECTED:
        id = WIFI_ADPT_EVT_STA_CONNECT;
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
        id = WIFI_ADPT_EVT_STA_DISCONNECT;
        break;

    case WIFI_EVENT_STA_AUTHMODE_CHANGE:
        id = WIFI_ADPT_EVT_STA_AUTHMODE_CHANGE;
        break;

    case WIFI_EVENT_STA_STOP:
        id = WIFI_ADPT_EVT_STA_STOP;
        break;
    case WIFI_EVENT_STA_BEACON_TIMEOUT:
        id = WIFI_ADPT_EVT_STA_BEACON_TIMEOUT;
        break;
    default:
        return -1;
    }

    return id;
}

/* Needs to be DeviceWiFi callback */
/****************************************************************************
 * Name: esp_wifi_scan_event_parse
 *
 * Description:
 *   Parse scan information
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *     None
 *
 ****************************************************************************/

void esp_wifi_scan_event_parse(void)
{
    wifi_ap_record_t * ap_list_buffer = NULL;
    uint16_t bss_total = 0;
    esp_wifi_scan_get_ap_num(&bss_total);
    LTLOG_DEBUG("scan.evt.parse", "num %u", bss_total);
    if (bss_total == 0) {
        Esp32_ScanResult_STA_CB(0, NULL);
        return;
    }
    ap_list_buffer = lt_malloc(bss_total * sizeof(wifi_ap_record_t));
    if(!ap_list_buffer) {
        LTLOG_YELLOWALERT("scan.no.memory", "cannot allocate mem");
        return;
    }
    /* NOTE: Driver requires this buffer be zeroed! */
    lt_memset(ap_list_buffer, 0, bss_total * sizeof(wifi_ap_record_t));
    Esp32_ScanResult_STA_CB(bss_total, ap_list_buffer);
    if (ap_list_buffer) {
        lt_free(ap_list_buffer);
    }
}

/****************************************************************************
 * Name: esp_evt_work_cb
 *
 * Description:
 *   Process the cached event
 *
 * Input Parameters:
 *   arg - No mean
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
static void esp_evt_work_cb(void * data)
{
    struct evt_adpt *pevt_adpt = (struct evt_adpt *)data;
    LOG_WORKER_ENTRY();
    if (!pevt_adpt) {
        return;
    }
    LOCK_WIFIAPI();
    switch (pevt_adpt->id) {
    case WIFI_ADPT_EVT_SCAN_DONE:
        esp_wifi_scan_event_parse();
        break;
    case WIFI_ADPT_EVT_STA_START:
        LTLOG_DEBUG("cb.start","Wi-Fi sta start\n");
        break;

    case WIFI_ADPT_EVT_STA_CONNECT:
        LTLOG_DEBUG("sta.connect", "Wi-Fi sta connect\n");
        Esp32_JoinStatus_STA_CB_Success();
        break;

    case WIFI_ADPT_EVT_STA_DISCONNECT:
        wifi_event_sta_disconnected_t *disconnected =
            (wifi_event_sta_disconnected_t *) pevt_adpt->buf;
        LTLOG_DEBUG("sta.disconnect", "Wi-Fi sta disconnect\n");
        Esp32_JoinStatus_STA_CB_Failed(disconnected->reason);
        break;

    case WIFI_ADPT_EVT_STA_STOP:
        LTLOG_DEBUG("sta.stop","Wi-Fi sta stop\n");
        break;
    case WIFI_ADPT_EVT_STA_BEACON_TIMEOUT:
        LTLOG_DEBUG("sta.beacon","Timeout!\n");
        break;
    default:
        break;
    }
    UNLOCK_WIFIAPI();
    LOG_LEAVE();
}

static void esp_evt_work_complete_cb(LTThread_ReleaseReason reason, void * data)
{
    LOG_ENTRY();
    LT_UNUSED(reason);
    if(data) {
        lt_free(data);
    }
}

/****************************************************************************
 * Name: phy_digital_regs_load
 *
 * Description:
 *   Load  PHY digital registers.
 *
 ****************************************************************************/

static inline void phy_digital_regs_load(void)
{
    LOG_ENTRY();
    if (g_phy_digital_regs_mem != NULL) {
        phy_dig_reg_backup(false, g_phy_digital_regs_mem);
    }
}



static void wifi_coex_disable(void)
{
    coex_disable();
}

/****************************************************************************
 * Name: wifi_errno_trans
 *
 * Description:
 *   Transform from ESP Wi-Fi error code to NuttX error code
 *
 * Input Parameters:
 *   ret - ESP Wi-Fi error code
 *
 * Returned Value:
 *   OS error code
 *
 ****************************************************************************/

static int32_t wifi_errno_trans(int ret)
{
    int wifierr;
    LOG_ENTRY();
    /* Unmask component error bits */
    wifierr = ret & 0xfff;
    if (wifierr == ESP_OK) {
        return ESP_OK;
    } else if (wifierr == ESP_ERR_NO_MEM) {
        return -1;
    } else if (wifierr == ESP_ERR_INVALID_ARG) {
        return -2;
    } else if (wifierr == ESP_ERR_INVALID_STATE) {
        return -3;
    } else if (wifierr == ESP_ERR_INVALID_SIZE) {
        return -4;
    } else if (wifierr == ESP_ERR_NOT_FOUND) {
        return -5;
    } else if (wifierr == ESP_ERR_NOT_SUPPORTED) {
        return -6;
    } else if (wifierr == ESP_ERR_TIMEOUT) {
        return -7;
    } else if (wifierr == ESP_ERR_INVALID_MAC) {
        return -8;
    } else {
        return ESP_FAIL;
    }
}

/****************************************************************************
 * Name: esp_wifi_tx_done_cb
 *
 * Description:
 *   Wi-Fi TX done callback function.
 *
 ****************************************************************************/

static IRAM_ATTR void esp_wifi_tx_done_cb(uint8_t ifidx, uint8_t * data,
        uint16_t * len, bool txstatus)
{
    LT_UNUSED(ifidx);
    LT_UNUSED(data);
    LT_UNUSED(len);
    LT_UNUSED(txstatus);
    LOG_TXDONE_ENTRY("tx.done.cb", "ifidx=%d data=%p *len=%p txstatus=%d\n",
                     ifidx, data, len, txstatus);
}

/****************************************************************************
 * Name: esp_wifi_deinit
 *
 * Description:
 *   Deinitialize Wi-Fi and free resource
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   0 if success or others if fail
 *
 ****************************************************************************/

esp_err_t esp_wifi_deinit(void)
{
    int ret;
    LOG_ENTRY();
    LOCK_WIFIAPI();
    ret = esp_supplicant_deinit();
    if (ret) {
        UNLOCK_WIFIAPI();
        LTLOG_YELLOWALERT("deinit.supp.error", "Failed to deinitialize supplicant\n");
        return ret;
    }

    ret = esp_wifi_deinit_internal();
    if (ret != 0) {
        UNLOCK_WIFIAPI();
        LTLOG_YELLOWALERT("deinit.wifi.error", "Failed to deinitialize Wi-Fi\n");
        return ret;
    }
    UNLOCK_WIFIAPI();
    if (g_hThread_wifi_wrk) {
        s_iThread->Destroy(g_hThread_wifi_wrk);
        g_hThread_wifi_wrk = 0;
    }
    if (g_hThread_wifi_timers) {
        s_iThread->Destroy(g_hThread_wifi_timers);
        g_hThread_wifi_timers = 0;
    }

    return ret;
}

int esp_wifi_adapter_init(int *p_wifi_ref_cnt)
{
    int ret;
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    LOG_ENTRY();
    if (*p_wifi_ref_cnt) {
        LTLOG_DEBUG("init.mult", "Wi-Fi adapter is already initialized\n");
        (*p_wifi_ref_cnt)++;
        return ESP_OK;
    }

    LTDeviceIdentity *pIdentity = lt_openlibrary(LTDeviceIdentity);
    if (pIdentity) {
        pIdentity->GetMac(wifi_mac_address);
        lt_closelibrary(pIdentity);
    }
    else {
        esp_read_efuse_mac(wifi_mac_address);
    }

    wifi_bt_coexist_init();

    wifi_cfg.nvs_enable = 0;

#ifdef LT_ESP32_WIFI_TX_AMPDU
    wifi_cfg.ampdu_tx_enable = 1;
#else
    wifi_cfg.ampdu_tx_enable = 0;
#endif

#ifdef LT_ESP32_WIFI_RX_AMPDU
    wifi_cfg.ampdu_rx_enable = 1;
#else
    wifi_cfg.ampdu_rx_enable = 0;
#endif

#ifdef LT_ESP32_WIFI_STA_DISCONNECT_PM
    wifi_cfg.sta_disconnected_pm = true;
#else
    wifi_cfg.sta_disconnected_pm = false;
#endif

    wifi_cfg.rx_ba_win          = LT_ESP32_WIFI_RXBA_AMPDU_WZ;
    wifi_cfg.static_rx_buf_num  = LT_ESP32_WIFI_STATIC_RXBUF_NUM;
    wifi_cfg.dynamic_rx_buf_num = LT_ESP32_WIFI_DYNAMIC_RXBUF_NUM;
    wifi_cfg.dynamic_tx_buf_num = LT_ESP32_WIFI_DYNAMIC_TXBUF_NUM;
    ret = esp_wifi_init(&wifi_cfg);
    if (ret) {
        LTLOG("init.error", "Failed to initialize Wi-Fi error=%d\n", ret);
        ret = wifi_errno_trans(ret);
        goto errout_init_wifi;
    }

    ret = esp_wifi_set_ps(DEFAULT_PS_MODE);
    if (ret) {
        LTLOG("ps.set","Failed to set PS\n");
    }

    ret = esp_wifi_set_tx_done_cb(esp_wifi_tx_done_cb);
    if (ret) {
        LTLOG("tx.callback.error", "Failed to register TX done callback ret=%d\n", ret);
        ret = wifi_errno_trans(ret);
        goto errout_init_txdone;
    }

    (*p_wifi_ref_cnt)++;
    LTLOG_DEBUG("init.ok", "OK to initialize Wi-Fi adapter\n");

    return ESP_OK;

errout_init_txdone:
    esp_wifi_deinit();
errout_init_wifi:
    return ret;
}

void * __wrap_malloc(uint32_t size)
{
    return esp_malloc(size);
}

void * __wrap_realloc(void * pData, LT_SIZE size)
{
    return lt_realloc(pData, size);
}

void __wrap_free(void * ptr)
{
    esp_free(ptr);
}

/****************************************************************************
 * Name: esp_wifi_init
 *
 * Description:
 *   Initialize Wi-Fi
 *
 * Input Parameters:
 *   config - Initialization config parameters
 *
 * Returned Value:
 *   0 if success or others if fail
 *
 ****************************************************************************/

esp_err_t esp_wifi_init(const wifi_init_config_t * config)
{
    int32_t ret = -1;
    LOG_ENTRY();
    if (!g_hThread_wifi_wrk) {
        g_hThread_wifi_wrk = s_core->CreateThread("EspWifiWorker");
        //s_iThread->SetPriority(g_hThread_wifi_wrk, prio);
        s_iThread->SetStackSize(g_hThread_wifi_wrk, THREAD_STACK_SIZE_WIFI_WORKER);
        s_iThread->Start(g_hThread_wifi_wrk, NULL, NULL);
    }

    if (!g_hThread_wifi_timers) {
        g_hThread_wifi_timers = s_core->CreateThread("EspWifiTimers");
        s_iThread->SetPriority(g_hThread_wifi_timers, kLTThread_PriorityHighest);
        s_iThread->SetStackSize(g_hThread_wifi_timers, THREAD_STACK_SIZE_WIFI_TIMERS);
        s_iThread->Start(g_hThread_wifi_timers, NULL, NULL);
    }
    LOCK_WIFIAPI();
    coex_init();
    ret = esp_wifi_init_internal(config);
    if (ret) {
        UNLOCK_WIFIAPI();
        goto cleanup;
    }

    ret = esp_supplicant_init();
    if (ret) {
        LTLOG("init.supplicant.err", "Failed to initialize WPA supplicant error=%d\n", (int)ret);
        esp_wifi_deinit_internal();
        UNLOCK_WIFIAPI();
        goto cleanup;
    }
    UNLOCK_WIFIAPI();
    return 0;
cleanup:
    if (g_hThread_wifi_wrk) {
        s_iThread->Destroy(g_hThread_wifi_wrk);
        g_hThread_wifi_wrk = 0;
    }
    if (g_hThread_wifi_timers) {
        s_iThread->Destroy(g_hThread_wifi_timers);
        g_hThread_wifi_timers = 0;
    }
    WIFI_DRIVER_FAIL();
    return ret;
}

/****************************************************************************
 * Name: esp_timer_stop
 *
 * Description:
 *   Stop timer
 *
 * Input Parameters:
 *   timer  - Timer handle pointer
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

esp_err_t esp_timer_stop(esp_timer_handle_t timer)
{
    LOG_ENTRY();
    timerData_t *pTimerData = (timerData_t *) timer;
    if (!pTimerData) return ESP_ERR_INVALID_ARG;
    if (LTAtomic_CompareAndExchange(&pTimerData->timerState, kTimerRunning, kTimerStopped)) {
        s_iThread->KillTimer(g_hThread_wifi_timers, DispatchAndKillTimer, (void *)pTimerData);
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

/****************************************************************************
 * Name: esp_timer_get_time
 *
 * Description:
 *   Get system time of type int64_t
 *
 * Input Parameters:
 *   periph - No mean
 *
 * Returned Value:
 *   System time
 *
 ****************************************************************************/

int64_t esp_timer_get_time(void)
{
    LOG_ENTRY();
    return lt_timer_get_time();
}

/****************************************************************************
 * Name: esp_timer_start_once
 *
 * Description:
 *   Start timer with one shot mode
 *
 * Input Parameters:
 *   timer      - Timer handle pointer
 *   timeout_us - Timeout value by micro second
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
    LOG_ENTRY();
    timerData_t *pTimerData = (timerData_t *) timer;
    if (!pTimerData) return ESP_ERR_INVALID_ARG;
    pTimerData->reload      = false;
    pTimerData->interval_us = timeout_us;
    if (LTAtomic_CompareAndExchange(&pTimerData->timerState, kTimerStopped, kTimerRunning)) {
        s_iThread->SetTimer(g_hThread_wifi_timers, LTTime_Microseconds(timeout_us), DispatchAndKillTimer, NULL, pTimerData);
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

/****************************************************************************
 * Name: esp_timer_delete
 *
 * Description:
 *   Delete timer and free resource
 *
 * Input Parameters:
 *   timer  - Timer handle pointer
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

esp_err_t esp_timer_delete(esp_timer_handle_t timer)
{
    LOG_ENTRY();
    timerData_t * pTimerData = (timerData_t *) timer;
    esp_timer_stop(timer);
    LTList_Remove(&pTimerData->node);
    lt_free(pTimerData);
    return 0;
}

/* libwpa_supplicant.a */
void esp_fill_random(void * buf, size_t len) LT_ISR_SAFE
{
    lt_get_random(buf, len);
}

/* NOTE: Nutt-X used esp32 HW timers here, we try to use software LT timers for this, seems fine */
/****************************************************************************
 * Name: esp_timer_create
 *
 * Description:
 *   Create timer with given arguments
 *
 * Input Parameters:
 *   create_args - Timer arguments data pointer
 *   out_handle  - Timer handle pointer
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

esp_err_t esp_timer_create(const esp_timer_create_args_t *create_args, esp_timer_handle_t *out_handle)
{
    timerData_t * pTimerData = NULL;
    LOG_ENTRY();
    pTimerData = (timerData_t *) lt_malloc(sizeof(timerData_t));
    LTAtomic_Store(&pTimerData->timerState, kTimerStopped);
    pTimerData->pClientData = create_args->arg;
    pTimerData->pTimerProc  = (LTThread_TimerProc *) create_args->callback;
    LTList_AddTail(&g_timers, &pTimerData->node);
    *out_handle = (esp_timer_handle_t) pTimerData;
    return ESP_OK;
}

/****************************************************************************
 * Name: net80211_printf
 *
 * Description:
 *   Output format string and its arguments
 *
 * Input Parameters:
 *   format - format string
 *
 * Returned Value:
 *   0
 *
 ****************************************************************************/

int net80211_printf(const char *format, ...)
{
#ifdef LT_DEBUG_WIRELESS_INFO
    lt_va_list args;
    lt_va_start(args, format);
    LTLOGV("net80211", format, args);
    lt_va_end(args);
#else
    LT_UNUSED(format);
#endif
    return 0;
}

/****************************************************************************
 * Name: esp_event_send_internal
 *
 * Description:
 *   Post event message to queue
 *
 * Input Parameters:
 *   event_base      - Event set name
 *   event_id        - Event ID
 *   event_data      - Event private data
 *   event_data_size - Event data size
 *   ticks_to_wait   - Waiting system ticks
 *
 * Returned Value:
 *   Task maximum priority
 *
 ****************************************************************************/

esp_err_t esp_event_send_internal(esp_event_base_t event_base,
                                  int32_t event_id,
                                  void * event_data,
                                  size_t event_data_size,
                                  uint32_t ticks_to_wait)
{
    int32_t ret;
    LOG_ENTRY();
    ret = lt_event_post(event_base, event_id, event_data,
                        event_data_size, ticks_to_wait);

    return ret;
}

/****************************************************************************
 * Name: __wrap_intr_matrix_set
 *
 * Description:
 *   Map external Irq to CPU Irq
 *
 * Input Parameters:
 *   cpu_no      - Must be 0 until SMP is implemented
 *   intr_source - External Irq number
 *   intr_num    - CPU Irq number
 *   intr_prio   - Priority of the interrupt, ignored at the moment
 *
 ****************************************************************************/

void __wrap_intr_matrix_set(s32 cpu_no, u32 intr_source,
                          u32 intr_num, s32 intr_prio)
{
    LT_UNUSED(intr_prio);
    LTLOG_DEBUG("wrap_intr_matrix_set", "cpu_no=%lu, intr_source=%lu, intr_num=%lu, intr_prio=%lx\n",
                LT_Pu32(cpu_no), LT_Pu32(intr_source), LT_Pu32(intr_num), LT_Pu32(intr_prio));
    if (cpu_no == 0) {
        Esp32MapExternalToCPUIrq(0, intr_source, intr_num);
    } else {
        LTLOG_REDALERT("set.intr.fatal", "Unexpected interrupt configuration");
    }
}
