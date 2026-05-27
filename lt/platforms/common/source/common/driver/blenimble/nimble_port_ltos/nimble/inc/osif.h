/**
 * Copyright (c) 2015, Realsil Semiconductor Corporation. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 */

#ifndef _OSIF_H_
#define _OSIF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "lt/LT.h"

#define OSIF_LT_LOG_ENABLED 0
#if OSIF_LT_LOG_ENABLED
#define OSIF_LT_LOG(fmt, ...) osif_lt_printf(fmt, ##__VA_ARGS__)
#else
#define OSIF_LT_LOG(fmt, ...)
#endif

typedef struct LTMutexMonitor {
    LTMutex *mutex;
    LTThread hThread;
} LTMutexMonitor;


bool osif_lt_init(void);

/* OS task interfaces */
bool osif_task_create(void **pp_handle, const char *p_name, void (*p_routine)(void *),
                      void *p_param, u16 stack_size, u16 priority);
bool osif_task_delete(void *p_handle);
bool osif_task_yield(void);
bool osif_task_priority_get(void *p_handle, u16 *p_priority);
bool osif_task_priority_set(void *p_handle, u16 priority);
bool osif_signal_init(void);
void osif_signal_deinit(void);
bool osif_task_signal_send(void *p_handle, u32 signal);
bool osif_task_signal_recv(u32 *p_signal, u32 wait_ms);
bool osif_task_signal_clear(void *p_handle);

/* OS synchronization interfaces */
u32 osif_lock(void);
void osif_unlock(u32 flags);
bool osif_sem_create(void **pp_handle, u32 init_count, u32 max_count);
bool osif_sem_delete(void *p_handle);
bool osif_sem_take(void *p_handle, u32 wait_ms);
bool osif_sem_give(void *p_handle);
u16 osif_sem_getvalue(void *p_handle);
bool osif_mutex_create(void **pp_handle);
bool osif_mutex_delete(void *p_handle);
bool osif_mutex_take(void *p_handle, u32 wait_ms);
bool osif_mutex_give(void *p_handle);


/* OS software timer interfaces */
bool osif_timer_id_get(void **pp_handle, u32 *p_timer_id);
bool osif_timer_create(void **pp_handle, const char *p_timer_name, u32 timer_id,
                       u32 interval_ms, bool reload, void (*p_timer_callback)(void *));
bool osif_timer_start(void **pp_handle);
bool osif_timer_restart(void **pp_handle, u32 interval_ms);
bool osif_timer_stop(void **pp_handle);
bool osif_timer_delete(void **pp_handle);
bool osif_timer_dump(void);
bool osif_timer_state_get(void **pp_handle, u32 *p_timer_state);

void osif_lt_printf(const char * fmt, ...);
#ifdef __cplusplus
}
#endif

#endif /* _OSIF_H_ */
