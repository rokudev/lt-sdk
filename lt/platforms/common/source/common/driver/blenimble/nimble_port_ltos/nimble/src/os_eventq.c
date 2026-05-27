/*******************************************************************************
 * platforms/esp32/source/esp32/driver/bt/nimble_port_ltos/os_eventq.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <stdint.h>

#include "nimble/nimble_npl.h"
#include "osif.h"
static LTThread        ble_ev_thr;
ILTThread              *iThread;
LTCore                 *core;
#define BLE_NIMBLE_HOST_STACK_SIZE 2048

#if PRINTFSUPPORT
DEFINE_LTLOG_SECTION("ltos.ble.nimble");
#define PLOG(...) LTLOG(__VA_ARGS__)
int printf(const char *format, ...) {
    char s_workString1 [256];
    lt_va_list vaList;
    lt_va_start(vaList, format);
    int ret = lt_vsnprintf(s_workString1, sizeof(s_workString1), format, vaList);
    LTLOG("printf", s_workString1);
    lt_va_end(vaList);
    return ret;
}
#else
#define PLOG(...)
#endif

typedef struct NimblePortInitCtx {
    LTHandle            hDevice;
    int                 ble_host_priority;
    LTThread            bleHostThr;
} NimblePortInitCtx;

void ble_npl_eventq_init(struct ble_npl_eventq *evq, void *ctx)
{
    NimblePortInitCtx* nim_ctx = (NimblePortInitCtx*)ctx;
    core = LT_GetCore();
    iThread = lt_getlibraryinterface(ILTThread, core);
    ble_ev_thr = core->CreateThread("blehost");
    if (!ble_ev_thr)
    {
        LT_ASSERT(0); //No Ble No IOT
        return;
    }
    nim_ctx->bleHostThr = ble_ev_thr;
    iThread->SetPriority(ble_ev_thr, nim_ctx->ble_host_priority);
    iThread->SetStackSize(ble_ev_thr, BLE_NIMBLE_HOST_STACK_SIZE);
    iThread->SetThreadSpecificClientData(ble_ev_thr, "lt_ble", NULL, LTHANDLE_TO_VOIDPTR(nim_ctx->hDevice));
    iThread->Start(ble_ev_thr, NULL, NULL);
    evq->pHandle = LTHANDLE_TO_VOIDPTR(ble_ev_thr);
}

void ble_npl_eventq_deinit(struct ble_npl_eventq *evq)
{
    iThread->Terminate(ble_ev_thr);
    iThread->WaitUntilFinished(ble_ev_thr, LTTime_Infinite());
    lt_destroyhandle(ble_ev_thr);
}

bool ble_npl_eventq_is_empty(struct ble_npl_eventq *evq)
{
    PLOG(__FUNCTION__, "Called ble_npl_eventq_is_empty");
    return true;
}

int ble_npl_eventq_inited(const struct ble_npl_eventq *evq)
{
    return (evq->pHandle != NULL);
}

static void ble_npl_event_process(void *arg)
{
    struct ble_npl_event *ev = (struct ble_npl_event *)arg;
    ble_npl_event_run(ev);
}

void ble_npl_eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    if (evq->pHandle == NULL)
    {
        return;
    }
    if (VOIDPTR_TO_LTHANDLE(evq->pHandle) != ble_ev_thr) {
        PLOG(__FUNCTION__, "Event Queue Handle Mismatch");
        LT_ASSERT(0);
        return;
    }
    iThread->QueueTaskProc(ble_ev_thr, ble_npl_event_process, NULL, (void *)ev);
}

inline struct ble_npl_event * ble_npl_eventq_get(struct ble_npl_eventq *evq, ble_npl_time_t tmo)
{
    LT_ASSERT(0); //Unreachable Path
    return NULL;
}

void ble_npl_eventq_run(struct ble_npl_eventq *evq)
{
    LT_ASSERT(0); //Unreachable Path
    return;
}


// ========================================================================
//                         Event Implementation
// ========================================================================

void ble_npl_event_init(struct ble_npl_event *ev, ble_npl_event_fn *fn,
                   void *arg)
{
    if (ev != NULL)
    {
        lt_memset(ev, 0, sizeof(*ev));
        ev->ev_cb  = fn;
        ev->ev_arg = arg;
    }
}

void ble_npl_event_deinit(struct ble_npl_event *ev)
{
    if (ev != NULL)
    {
        ev->ev_cb  = NULL;
        ev->ev_arg = NULL;
    }
}

bool ble_npl_event_is_queued(struct ble_npl_event *ev)
{
    if (ev != NULL)
    {
        return ev->ev_queued;
    }
    else
    {
        return false;
    }
}

void * ble_npl_event_get_arg(struct ble_npl_event *ev)
{
    if (ev != NULL)
    {
        return ev->ev_arg;
    }
    else
    {
        return NULL;
    }
}

void ble_npl_event_set_arg(struct ble_npl_event *ev, void *arg)
{
    if (ev != NULL)
    {
        ev->ev_arg = arg;
    }
}

void ble_npl_event_run(struct ble_npl_event *ev)
{
    if (ev != NULL && ev->ev_cb != NULL)
    {
        ev->ev_cb(ev);
    }
}

void ble_npl_eventq_remove(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    LT_UNUSED(evq);
    // NIMBLEDO CRITICAL: this doesn't make much sense
    /* we simply mark the event as unqueued. we will ignore these elements
     * when receiving from the queue */
    ev->ev_queued = 0;
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Jul-22   vespasian   created
 */
