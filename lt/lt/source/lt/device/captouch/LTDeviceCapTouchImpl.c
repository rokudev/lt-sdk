/*******************************************************************************
 * lt/source/lt/device/captouch/LTDeviceCapTouchImpl.c
 *
 * LT Device Library for Capacitive Touch sensors
 *
 * Provides notification for capacitive touch motion detection
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/captouch/LTDeviceCapTouch.h>
#include <lt/driver/captouch/LTDriverCapTouch.h>
#include <lt/device/config/LTDeviceKonfig.h>

/*____________________________
  LTDeviceCapTouch #defines */
//DEFINE_LTLOG_SECTION("lt.dev.captouch");
#define CAP_TOUCH_MODE_SET_IN_PROGRESS_SLEEP_DELAY_MS (10)
#define CAP_TOUCH_MODE_TRIGGER_COUNT_DROP_THRESHOLD   (1000)

/*____________________
  LTLibrary binding */
define_LTObjectLibrary(1, NULL, NULL);

/*_______________________________
  LTDeviceKeypadImpl constants */
enum { kModeFlags_ModeChangeInProgress = (1 << 31) };
static const LTArgsDescriptor s_capTouchEventArgs = { 1, { kLTArgType_u32 } };

/*_________________________________________________
  typedef_LTObjectImpl with private data members */
typedef_LTObjectImpl(LTDeviceCapTouch, LTDeviceCapTouchImpl) {
    LTDriverCapTouch      *driver;
    ILTEvent              *iEvent;
    LTEvent                hEvent;
    ILTThread             *iThread;
    LTThread               hInitializingThread;
    LTAtomic               totalTriggerCount;
    LTAtomic               pendingTriggerCount;
} LTOBJECT_API;

/*_________________________________________
  LTDeviceCapTouchImpl private functions */
static void LTDeviceCapTouchImpl_CapTouchEventDispatchProc(LTEvent hEvent, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(hEvent);
    ((LTDeviceCapTouch_TriggerEventProc *)proc)(LTArgs_u32At(0, args), pClientData);
}

static void LTDeviceCapTouchImpl_StaticNotifyEventProc(void *pClientData) LT_ISR_SAFE {
    LTDeviceCapTouchImpl *capTouch = (LTDeviceCapTouchImpl *)pClientData;
    u32 numTriggers = LTAtomic_Load(&capTouch->pendingTriggerCount);
    while (! LTAtomic_CompareAndExchange(&capTouch->pendingTriggerCount, numTriggers, 0)) numTriggers = LTAtomic_Load(&capTouch->pendingTriggerCount);
    //LT_ASSERT(numTriggers > 0);
    if (numTriggers) capTouch->iEvent->NotifyEvent(capTouch->hEvent, numTriggers);
}

static void LTDeviceCapTouchImpl_StaticCapTouchMotionProc(void *pClientData) LT_ISR_SAFE {
    /* this procedure is running in ISR context */
    LTDeviceCapTouchImpl *capTouch = (LTDeviceCapTouchImpl *)pClientData;
    LTAtomic_FetchAdd(&capTouch->totalTriggerCount, 1);
    u32 nOldPendingTriggerCount = LTAtomic_FetchAdd(&capTouch->pendingTriggerCount, 1);
    if (nOldPendingTriggerCount > CAP_TOUCH_MODE_TRIGGER_COUNT_DROP_THRESHOLD) {
        /* trigger count is getting sky high without the task proc being processed.
           reset the pending trigger count to 1 and re queue */
           //LTLOG_YELLOWALERT("captouch.drop", "dropping %lu pending triggers", LT_Pu32(nOldPendingTriggerCount));
           nOldPendingTriggerCount = 0;
           LTAtomic_Store(&capTouch->pendingTriggerCount, 1);
    }
    if (nOldPendingTriggerCount == 0) {
        /* on the first pendingTrigger try to queue our notification onto the thread that called SetMode;
           if it fails (maybe the thread died?) then call NotifyEventFromISR which queues it on the CoreThread;
           we don't want to use the CoreThread unless absolutely necessary */
        if (! capTouch->iThread->QueueTaskProc(capTouch->hInitializingThread, &LTDeviceCapTouchImpl_StaticNotifyEventProc, NULL, pClientData)) {
            capTouch->iEvent->NotifyEventFromISR(&LTDeviceCapTouchImpl_StaticNotifyEventProc, pClientData);
        }
    }
}

/*___________________________________
  LTDeviceCapTouchImpl constructors */
static void LTDeviceCapTouchImpl_DestructObject(LTDeviceCapTouchImpl *capTouch) {
    lt_destroyobject(capTouch->driver);
    lt_destroyhandle(capTouch->hEvent);
}

static bool LTDeviceCapTouchImpl_ConstructObject(LTDeviceCapTouchImpl *capTouch) {
    do
    {
        if (NULL == (capTouch->driver = lt_createdriverobject_fordevice(LTDriverCapTouch, capTouch))) break;
        if (LTHANDLE_INVALID == (capTouch->hEvent = LT_GetCore()->CreateEvent(&s_capTouchEventArgs, &LTDeviceCapTouchImpl_CapTouchEventDispatchProc, NULL, NULL, NULL))) break;
        capTouch->iEvent = lt_gethandleinterface(ILTEvent, capTouch->hEvent);
        capTouch->iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
        return true;
    } while (false);
    lt_destroyobject(capTouch->driver);
    lt_destroyhandle(capTouch->hEvent);
    return false;
}

/*_____________________________________
  LTDeviceCapTouchImpl API Functions */
static void
LTDeviceCapTouchImpl_OnCapTouchTriggerEvent(LTDeviceCapTouchImpl *capTouch,LTDeviceCapTouch_TriggerEventProc *triggerEventProc, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void * clientData) {
    capTouch->iEvent->RegisterForEvent(capTouch->hEvent, triggerEventProc, clientDataReleaseProc, clientData, false);
}

static void
LTDeviceCapTouchImpl_NoCapTouchTriggerEvent(LTDeviceCapTouchImpl *capTouch, LTDeviceCapTouch_TriggerEventProc *triggerEventProc) {
    capTouch->iEvent->UnregisterFromEvent(capTouch->hEvent, triggerEventProc);
}

static bool
LTDeviceCapTouchImpl_Initialize(LTDeviceCapTouchImpl *capTouch, LTDeviceCapTouch_Mode mode) {
    LTThread hThreadCurrent = capTouch->iThread->GetCurrentThread();
    u32 nMask = LT_GetCore()->Disable();
    LTThread hThreadOld = capTouch->hInitializingThread;
    capTouch->hInitializingThread = hThreadCurrent;
    LT_GetCore()->Enable(nMask);
    if (capTouch->driver->API->Initialize(capTouch->driver, mode, hThreadCurrent, &LTDeviceCapTouchImpl_StaticCapTouchMotionProc, capTouch)) {
        return true;
    }
    nMask = LT_GetCore()->Disable();
    capTouch->hInitializingThread = hThreadOld;
    LT_GetCore()->Enable(nMask);
    return false;
}

static LTDeviceCapTouch_Mode
LTDeviceCapTouchImpl_GetMode(LTDeviceCapTouchImpl *capTouch) {
    return capTouch->driver->API->GetMode(capTouch->driver);
}

static bool
LTDeviceCapTouchImpl_IsCapTouchTriggerActive(LTDeviceCapTouchImpl *capTouch) {
    return capTouch->driver->API->IsCapTouchTriggerActive(capTouch->driver);
}

static void
LTDeviceCapTouchImpl_Enable(LTDeviceCapTouchImpl *capTouch, bool bEnable) {
    capTouch->driver->API->Enable(capTouch->driver, bEnable);
}

static u32
LTDeviceCapTouchImpl_GetTotalTriggerCount(LTDeviceCapTouchImpl *capTouch) {
    return LTAtomic_Load(&capTouch->totalTriggerCount);
}

static void LTDeviceCapTouchImpl_ResetTotalTriggerCount(LTDeviceCapTouchImpl *capTouch) {
    LTAtomic_Store(&capTouch->totalTriggerCount, 0);
}

static bool
LTDeviceCapTouchImpl_ResetTest(LTDeviceCapTouchImpl *capTouch) {
    return capTouch->driver->API->ResetTest(capTouch->driver);
}

/*_____________________________
  LTDeviceKeypad api binding */
define_LTObjectImplPublic(LTDeviceCapTouch, LTDeviceCapTouchImpl,
    OnCapTouchTriggerEvent,
    NoCapTouchTriggerEvent,
    Initialize,
    Enable,
    GetMode,
    IsCapTouchTriggerActive,
    GetTotalTriggerCount,
    ResetTotalTriggerCount,
    ResetTest
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  26-Mar-26   augustus    created from macrinus's device
 */
