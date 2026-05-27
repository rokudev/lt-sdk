/*******************************************************************************
 * platforms/esp32/source/esp32/driver/bt/nimble_port_ltos/os_callout.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include "nimble/nimble_npl.h"
#include "osif.h"

#define MAX_WAIT 0xFFFFFFFF

#include <lt/LT.h>
typedef struct timerData_t_
{
    LTThread hThread;
    LTThread_TimerProc * pTimerProc;
    void * pClientData;
    bool   reload;
    u32 interval_ms;
    u32 timerState;
} timerData_t;

static void ble_npl_callout_timer_cb(void * arg)
{
    timerData_t            * pTimerData = (timerData_t *)arg;
    struct ble_npl_callout * c          = pTimerData->pClientData;

    osif_timer_stop(&(c->c_timer));
    if (c->c_active) {
        /* Invoke callback */
        if (c->c_evq) {
            ble_npl_eventq_put(c->c_evq, &c->c_ev);
        } else {
            c->c_ev.ev_cb(&c->c_ev);
        }
    }
    c->c_active = false;
}

void ble_npl_callout_init(struct ble_npl_callout *c,
                          struct ble_npl_eventq *evq,
                          ble_npl_event_fn *ev_cb,
                          void *ev_arg)
{
    /* Initialize the callout. */
    lt_memset(c, 0, sizeof(*c));
    c->c_ev.ev_cb  = ev_cb;
    c->c_ev.ev_arg = ev_arg;
    c->c_evq       = evq;
    c->c_active    = false;
    c->c_timer     = NULL;
}

int ble_npl_callout_inited(struct ble_npl_callout *c)
{
    return (c->c_timer != NULL);
}

void ble_npl_callout_stop(struct ble_npl_callout *c)
{
    if (!ble_npl_callout_inited(c)) {
        return;
    }

    c->c_active = false;
    if (c->c_timer) {
        osif_timer_stop(&(c->c_timer));
        osif_timer_delete(&(c->c_timer));
    }
    c->c_timer = NULL;
}

ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *c, ble_npl_time_t ticks)
{
#ifndef OS_CALLOUT_SUPPORT
    c->c_active = true;
    return 0;
#endif
    if(ticks == 0) {
        //We do not support tick0, so we will set it to 1 tick so that we can get immediate notification
        ticks = 1;
    }
    c->c_ticks = ticks;
    if(c->c_timer) osif_timer_stop(&(c->c_timer));
    osif_timer_create(&(c->c_timer), NULL, (uint32_t)c, (uint32_t)ticks, false, ble_npl_callout_timer_cb);
    c->c_active = true;
    return 0;
}

bool ble_npl_callout_is_active(struct ble_npl_callout *c)
{
    return c->c_active;
}

ble_npl_time_t ble_npl_callout_get_ticks(struct ble_npl_callout *c)
{
    return c->c_ticks;
}

void ble_npl_callout_deinit(struct ble_npl_callout *c)
{
    c->c_active    = false;
    if (c->c_timer) {
        osif_timer_stop(&(c->c_timer));
        osif_timer_delete(&(c->c_timer));
    }
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Jul-22   vespasian   created
 */
