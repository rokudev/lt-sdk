/************************************************************************
 * LTNetHandle implementation
 * 
 * This file implements the LTNet Handle APIs
 * 
 * platforms/si91x/source/si91x/driver/wireless/common/LTNetHandle.c
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTNetHandle.h>
#if defined(DEBUG_ASSERT)
#define DEBUG_ASSERT_LTNETHANDLE(x) LT_ASSERT(x)
#else
#define DEBUG_ASSERT_LTNETHANDLE(x)
#endif

DEFINE_LTLOG_SECTION("lt.net.handle");
typedef enum {
    kLTNetHandleState_Disabled,
    kLTNetHandleState_Enabled
} LTNetHandleState;

typedef_LTObjectImpl(LTNetHandle, LTNetHandleImpl) {
    LTHandle            pollThread;
    LTCore              *core;
    ILTThread           *iThread;
    LTNetHandleState    state;
    LTNetHandlePollCb   pollCb;
    void                *pollCbData;
} LTOBJECT_API;

static bool LTNetHandleImpl_ConstructObject(LTNetHandleImpl *netHandle) {
    netHandle->core = LT_GetCore();
    netHandle->iThread = lt_getlibraryinterface(ILTThread, netHandle->core);
    netHandle->state = kLTNetHandleState_Disabled;
    netHandle->pollThread = netHandle->core->CreateThread("LTNetHandlePollThread");
    netHandle->iThread->SetStackSize(netHandle->pollThread, 1024);
    netHandle->iThread->Start(netHandle->pollThread, NULL, NULL);
    return true;
}

static void LTNetHandleImpl_DestructObject(LTNetHandleImpl *netHandle) {
    netHandle->iThread->Terminate(netHandle->pollThread);
    netHandle->iThread->WaitUntilFinished(netHandle->pollThread, LTTime_Infinite());
    lt_destroyhandle(netHandle->pollThread);
    return;
}

static bool LTNetHandleImpl_Add(LTNetHandle *handle, u32 priority, LTNetHandlePollCb pollCb, void *data) {
    if (!handle || !pollCb || (priority > kLTThread_PriorityHighest)) return false;
    LTNetHandleImpl *netHandle = (LTNetHandleImpl *)handle;
    netHandle->pollCb = pollCb;
    netHandle->pollCbData = data;
    netHandle->iThread->SetPriority(netHandle->pollThread, priority);
    return true;
}

static bool LTNetHandleImpl_Remove(LTNetHandle *handle, LTNetHandlePollCb pollCb) {
    if (!handle || !pollCb) return false;
    LTNetHandleImpl *netHandle = (LTNetHandleImpl *)handle;
    if (netHandle->pollCb != pollCb) return false;
    netHandle->pollCb = NULL;
    netHandle->pollCbData = NULL;
    return true;
}

static void LTNetHandleImpl_Enable(LTNetHandle *handle) {
    if (!handle) return;
    LTNetHandleImpl *netHandle = (LTNetHandleImpl *)handle;
    netHandle->state = kLTNetHandleState_Enabled;
    return;
}

static void LTNetHandleImpl_Disable(LTNetHandle *handle) {
    if (!handle) return;
    LTNetHandleImpl *netHandle = (LTNetHandleImpl *)handle;
    netHandle->state = kLTNetHandleState_Disabled;
    return;
}

static void LTNetHandleImpl_PollData(void *data);
static void LTNetHandleImpl_RetryTimerCb(void *data) {
    if (!data) return;
    LTNetHandleImpl *netHandle = (LTNetHandleImpl *)data;
    netHandle->iThread->KillTimer(netHandle->pollThread, LTNetHandleImpl_RetryTimerCb, data);
    LTNetHandleImpl_PollData(data);
    return;
}

static void LTNetHandleImpl_PollData(void *data) {
    if (!data) return;
    int max_budget = LTNETHANDLE_MAX_BUDGET;
    LTNetHandleImpl *netHandle = (LTNetHandleImpl *)data;
    if (netHandle->state != kLTNetHandleState_Enabled) return;
    if (netHandle->pollCb) {
        int workdone = netHandle->pollCb(netHandle->pollCbData, max_budget);
        if (workdone == 0) {
            //Back off for a while, start a timer of 10msec
            netHandle->iThread->SetTimer(netHandle->pollThread,
                LTTime_Milliseconds(10), LTNetHandleImpl_RetryTimerCb, NULL, data);
        } else if(workdone > max_budget) {
            LTLOG_REDALERT("exceed.max.budget", "LTNetHandleImpl_PollData: workdone > max_budget");
            DEBUG_ASSERT_LTNETHANDLE(0);
        }
    }
    return;
}

static void LTNetHandleImpl_Notify(LTNetHandle *handle) LT_ISR_SAFE{
    if (!handle) return;
    LTNetHandleImpl *netHandle = (LTNetHandleImpl *)handle;
    netHandle->iThread->QueueTaskProcIfRequired(netHandle->pollThread, LTNetHandleImpl_PollData, NULL, netHandle);
    return;
}

define_LTObjectImplPublic(LTNetHandle, LTNetHandleImpl,
    Add, Remove, Enable, Disable, Notify
);

define_LTOBJECT_EXPORTLIBRARY(LTNetHandle, 1, LTNetHandleImpl);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  28-Aug-24   galba       created
 */