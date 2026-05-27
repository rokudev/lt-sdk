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

#ifndef _NPL_OS_TYPES_H
#define _NPL_OS_TYPES_H

#include <stdint.h>

/* The highest and lowest task priorities */

typedef uint32_t ble_npl_time_t;
typedef int32_t  ble_npl_stime_t;

typedef int ble_npl_stack_t;

#define N_EVENTS_IN_EVQ 5

struct ble_npl_event;
struct ble_npl_eventq {
    void * pHandle;
    struct ble_npl_event * evt;
    volatile uint16_t evCnt;
};

struct ble_npl_mutex {
    void * pHandle;
};

struct ble_npl_sem {
    void * pHandle;
};

struct ble_npl_task {
    void * pHandle;
};

typedef void (*ble_npl_task_func_t)(void *);

int ble_npl_task_init(struct ble_npl_task *pTask, const char *name, ble_npl_task_func_t func,
		 void *arg, uint8_t prio, ble_npl_time_t sanity_itvl,
		 ble_npl_stack_t *stack_bottom, uint16_t stack_size);

int ble_npl_task_remove(struct ble_npl_task *pTask);

uint8_t ble_npl_task_count(void);

void ble_npl_task_yield(void);

struct ble_npl_callout;
#include "nimble/nimble_npl.h"
struct ble_npl_event {
    uint8_t                 ev_queued;
    ble_npl_event_fn       *ev_cb;
    void                   *ev_arg;
};

struct ble_npl_callout {
    struct ble_npl_event    c_ev;
    struct ble_npl_eventq  *c_evq;
    uint32_t                c_ticks;
    void                   *c_timer;
    bool                    c_active;
};


#endif // _NPL_OS_TYPES_H
