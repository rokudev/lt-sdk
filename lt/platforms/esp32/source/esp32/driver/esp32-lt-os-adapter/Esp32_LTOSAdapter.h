/******************************************************************************
 * <Esp32_LTOSAdapter.h>              Esp32 LT OS Adapter
 *                                    for WiFi & Bluetooth
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/
#ifndef ESP32_LT_OS_ADAPTER_H
#define ESP32_LT_OS_ADAPTER_H

#include <lt/core/LTCore.h>
#include <lt/LT.h>
LT_EXTERN_C_BEGIN

#define locate_data(n)                   __attribute__ ((section(n)))
#define IRAM_ATTR                        locate_data(".iram1")

/* Forces data into DRAM instead of flash */

#define DRAM_ATTR                        locate_data(".dram1")

#define OSI_VERSION                      0x00010002
#define OSI_MAGIC_VALUE                  0xfadebead

typedef void (*xt_handler)(void *);
typedef void (*coex_func_cb_t)(u32 event, int sched_cnt);


typedef void (*Osif_TaskProc_t)(void * pClientData);

typedef struct taskData_t_ {
    LTThread          hThread;
    Osif_TaskProc_t   pTaskProc;
    void            * pClientData;
} taskData_t;

enum eTimerState {
    kTimerStopped,
    kTimerRunning,
    kTimerZombie,
    kTimerDeleted,
};

typedef struct timerData_t_ {
    LTThread_TimerProc * pTimerProc;
    void               * pClientData;
    bool                 reload;
    u32                  interval_us;
    LTAtomic             timerState;
    LTList_Node          node;
} timerData_t;

struct osi_funcs_t {
    u32 _version;
    xt_handler (*_set_isr)(s32 n, xt_handler f, void * arg);
    void (*_ints_on)(u32 mask);
    void (*_interrupt_disable)(void);
    void (*_interrupt_restore)(void);
    void (*_task_yield)(void);
    void (*_task_yield_from_isr)(void);
    void *(*_semphr_create)(u32 max, u32 init);
    void (*_semphr_delete)(void * pSem);
    s32 (*_semphr_take_from_isr)(void * pSem, void * hptw);
    s32 (*_semphr_give_from_isr)(void * pSem, void * hptw);
    s32 (*_semphr_take)(void * pSem, u32 block_time_ms);
    s32 (*_semphr_give)(void * pSem);
    void *(*_mutex_create)(void);
    void (*_mutex_delete)(void * pMtx);
    s32 (*_mutex_lock)(void    * pMtx);
    s32 (*_mutex_unlock)(void  * pMtx);
    void *(* _queue_create)(u32 queue_len, u32 item_size);
    void (* _queue_delete)(void * pQueue);
    s32 (* _queue_send)(void * pQueue, void * item, u32 block_time_ms);
    s32 (* _queue_send_from_isr)(void * pQueue, void * item, void * hptw);
    s32 (* _queue_recv)(void * pQueue, void *item, u32 block_time_ms);
    s32 (* _queue_recv_from_isr)(void * pQueue, void * item, void * hptw);
    s32 (* _task_create)(void * task_func,
                             const char * name,
                             u32 stack_depth,
                             void * param,
                             u32 prio,
                             void * task_handle,
                             u32 core_id);
    void (* _task_delete)(void * task_handle);
    bool (* _is_in_isr)(void);
    s32 (* _cause_sw_intr_to_core)(s32 core_id, s32 intr_no);
    void *(* _malloc)(u32 size);
    void *(* _malloc_internal)(u32 size);
    void (* _free)(void *p);
    s32 (* _read_efuse_mac)(u8 mac[6]);
    void (* _srand)(u32 seed);
    s32 (* _rand)(void);
    u32 (* _btdm_lpcycles_2_us)(u32 cycles);
    u32 (* _btdm_us_2_lpcycles)(u32 us);
    bool (* _btdm_sleep_check_duration)(u32 *slot_cnt);
    void (* _btdm_sleep_enter_phase1)(u32 lpcycles);  /* called when interrupt is disabled */
    void (* _btdm_sleep_enter_phase2)(void);
    void (* _btdm_sleep_exit_phase1)(void);  /* called from ISR */
    void (* _btdm_sleep_exit_phase2)(void);  /* called from ISR */
    void (* _btdm_sleep_exit_phase3)(void);  /* called from task */
    bool (* _coex_bt_wakeup_request)(void);
    void (* _coex_bt_wakeup_request_end)(void);
    s32 (* _coex_bt_request)(u32 event,
                             u32 latency,
                             u32 duration);
    s32 (* _coex_bt_release)(u32 event);
    s32 (* _coex_register_bt_cb)(coex_func_cb_t cb);
    u32 (* _coex_bb_reset_lock)(void);
    void (* _coex_bb_reset_unlock)(u32 restore);
    s32 (* _coex_schm_register_btdm_callback)(void *callback);
    void (* _coex_schm_status_bit_clear)(u32 type, u32 status);
    void (* _coex_schm_status_bit_set)(u32 type, u32 status);
    u32 (* _coex_schm_interval_get)(void);
    u8 (* _coex_schm_curr_period_get)(void);
    void *(* _coex_schm_curr_phase_get)(void);
    s32 (* _coex_wifi_channel_get)(u8 *primary, u8 *secondary);
    s32 (* _coex_register_wifi_channel_change_callback)(void *cb);
    u32 _magic;
};

extern void *                  lt_sem_create(u32 max, u32 init);
extern void                    lt_sem_delete(void * sem);
extern s32                     lt_sem_take(void * sem, u32 block_time_tick);
extern s32                     lt_sem_give(void * sem);

void LTEsp32OSAdapter_LockWiFiApi(int line);
void LTEsp32OSAdapter_UnlockWiFiApi(void);

void LTEsp32OSAdapter_LibInit(void);
void LTEsp32OSAdapter_LibFini(void);

LT_EXTERN_C_END
#endif /* #ifndef ESP32_LT_OS_ADAPTER_H */
