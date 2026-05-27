/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreEventTests.c>
 *    __  __      _ __ ______          __  __  ____________
 *   / / / /___  (_) //_  __/__  _____/ /_/ / /_  __/ ____/___  ________
 *  / / / / __ \/ / __// / / _ \/ ___/ __/ /   / / / /   / __ \/ ___/ _ \
 * / /_/ / / / / / /_ / / /  __(__  ) /_/ /___/ / / /___/ /_/ / /  /  __/
 * \____/_/ /_/_/\__//_/  \___/____/\__/_____/_/  \____/\____/_/   \___/
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <tilt/JiltEngine.h>
#include "UnitTestLTCoreHelpers.h"

#define NUM_EVENT_NOTIFICATIONS 50

static Tilt                             * s_tilt                  = NULL;
static LTOThread                        * s_pEventNotifierThread  = NULL;
static LTOThread                        * s_pEventReceiverThread  = NULL;
static LTEvent                            s_hEvent                = 0;


static const LTArgsDescriptor s_unitTestLTEvent_EventArgs = {
    5,
    {
        kLTArgType_u32,     /*  notifyCount  */
        kLTArgType_u32,     /*  dispatchCount  */
        kLTArgType_u32,     /*  receiptCount  */
        kLTArgType_u32,     /*  completionCount */
        kLTArgType_pointer, /*  use this to remember clientData for dispatch and dispatch complete tests, but don't pass to the event proc */
    }
};

typedef void (UnitTestLTEvent_EventProc)(u32 notifyCount, u32 dispatchCount, u32 receiptCount, u32 completionCount, void * pClientData);
typedef struct UnitTestLTEvent_RecevierClientData {
    u32 notifyCount;          /* in s_pThreadEventNotifier,   incremented when notify is called */
    u32 dispatchCount;        /* in s_pEventReceiverThread,   incremented within LTEventTestEventProc */
    u32 receiptCount;        /* in s_pEventReceiverThread,   incremented within LTEventTestEventProc */
    u32 completionCount;      /* in s_pThreadEventNotifier,   incremented within EventDispatchCompleteProc */
} UnitTestLTEvent_RecevierClientData;

typedef UnitTestLTEvent_RecevierClientData TestCounters;

/*********************************************************************************************
 * Test callbacks: NotifyEventTaskProc, EventDispatchProc, EventProc, EventDispatchCompleteProc  */
static void MyUnitTestLTEvent_NotifyEventTaskProc(void *pClientData) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hEvent);
    UnitTestLTEvent_RecevierClientData * pCD = (UnitTestLTEvent_RecevierClientData *)pClientData;
    TILT_EXPECT_TRUE(s_tilt, pCD->dispatchCount      == pCD->notifyCount, "Notify: dispatchCount");
    TILT_EXPECT_TRUE(s_tilt, pCD->receiptCount       == pCD->notifyCount, "Notify: receiptCount");
    TILT_EXPECT_TRUE(s_tilt, pCD->completionCount    == pCD->notifyCount, "Notify: completionCount");
    pCD->notifyCount++;
    iEvent->NotifyEvent(s_hEvent, pCD->notifyCount, pCD->dispatchCount, pCD->receiptCount, pCD->completionCount, pCD);
}

static void MyUnitTestLTEvent_EventDispatchProc(LTEvent hEvent, void * pEventProc, LTArgs * pEventArgs, void * pEventProcClientData) {
    LT_UNUSED(hEvent);
    TestCounters * counters = (TestCounters *)LTArgs_pointerAt(4, pEventArgs);
    TILT_EXPECT_TRUE(s_tilt, counters != NULL, "Dispatch: counters clientData");
    if (counters == NULL) return;
    counters->dispatchCount++;
    TILT_EXPECT_TRUE(s_tilt, counters->dispatchCount == counters->notifyCount, "Dispatch: notifyCount");
    TILT_EXPECT_TRUE(s_tilt, counters->dispatchCount == counters->receiptCount + 1, "Dispatch: receiptCount");
    TILT_EXPECT_TRUE(s_tilt, counters->dispatchCount == counters->completionCount + 1, "Dispatch: completionCount");

    (*(UnitTestLTEvent_EventProc *)pEventProc)(
        LTArgs_u32At(0, pEventArgs),
        LTArgs_u32At(1, pEventArgs),
        LTArgs_u32At(2, pEventArgs),
        LTArgs_u32At(3, pEventArgs),
        pEventProcClientData);
}

static void MyUnitTestLTEvent_EventProc(u32 notifyCount, u32 dispatchCount, u32 receiptCount, u32 completionCount, void *pClientData) {
    UnitTestLTEvent_RecevierClientData * pCD = (UnitTestLTEvent_RecevierClientData *)pClientData;
    TILT_EXPECT_TRUE(s_tilt, pCD->notifyCount        == pCD->dispatchCount, "EventProc: notifyCount == dispatchCount");
    TILT_EXPECT_TRUE(s_tilt, pCD->dispatchCount      == pCD->notifyCount, "EventProc: dispatchCount");
    TILT_EXPECT_TRUE(s_tilt, pCD->receiptCount       == pCD->notifyCount - 1, "EventProc: receiptCount");
    TILT_EXPECT_TRUE(s_tilt, pCD->completionCount    == pCD->notifyCount - 1, "EventProc: completionCount");

    TILT_EXPECT_TRUE(s_tilt, notifyCount             == pCD->notifyCount, "EventProc: eventArgs notifyCount");
    TILT_EXPECT_TRUE(s_tilt, dispatchCount + 1       == pCD->dispatchCount, "EventProc: eventArgs dispatchCount");
    TILT_EXPECT_TRUE(s_tilt, receiptCount            == pCD->receiptCount, "EventProc: eventArgs receiptCount");
    TILT_EXPECT_TRUE(s_tilt, completionCount         == pCD->completionCount, "EventProc: eventArgs completionCount");
    pCD->receiptCount++;
}

static void MyUnitTestLTEvent_EventDispatchCompleteProc(LTEvent hEvent, LTArgs * pEventArgs) {
    LT_UNUSED(hEvent);
    TestCounters * counters = (TestCounters *)LTArgs_pointerAt(4, pEventArgs);
    TILT_EXPECT_TRUE(s_tilt, counters != NULL, "DispatchComplete: counters clientData");
    if (counters == NULL) return;
    counters->completionCount++;
    TILT_EXPECT_TRUE(s_tilt, counters->completionCount == counters->notifyCount, "DispatchComplete: notifyCount");
    TILT_EXPECT_TRUE(s_tilt, counters->completionCount == counters->dispatchCount, "DispatchComplete: dispatchCount");
    TILT_EXPECT_TRUE(s_tilt, counters->completionCount == counters->receiptCount, "DispatchComplete: receiptCount");
    if (counters->completionCount == NUM_EVENT_NOTIFICATIONS) {
        s_pEventNotifierThread->API->Terminate(s_pEventNotifierThread);
        s_pEventReceiverThread->API->Terminate(s_pEventReceiverThread);
    }
    else {
        s_pEventNotifierThread->API->QueueTaskProc(s_pEventNotifierThread, &MyUnitTestLTEvent_NotifyEventTaskProc, NULL, counters);
    };
}

/*******************************************************************************
 * TestLTEvent - run the test */
void TestLTEvent(Tilt * tilt) {
    s_tilt = tilt;
    TestCounters counters = { 0, 0, 0, 0 };

    /* Create and start threads */
    s_pEventNotifierThread = lt_createobject(LTOThread);
    s_pEventReceiverThread = lt_createobject(LTOThread);
    s_pEventReceiverThread->API->Start(s_pEventReceiverThread, "EventReceiver", NULL, NULL);
    s_pEventNotifierThread->API->Start(s_pEventNotifierThread, "EventNotifier", NULL, NULL);

    /* create event and register receiver */
    s_hEvent = LT_GetCore()->CreateEvent(&s_unitTestLTEvent_EventArgs, &MyUnitTestLTEvent_EventDispatchProc, MyUnitTestLTEvent_EventDispatchCompleteProc, NULL, NULL);
    ILTEvent * iEvent = lt_gethandleinterface(ILTEvent, s_hEvent);
    iEvent->RegisterThreadForEvent(s_hEvent, s_pEventReceiverThread->API->GetThreadHandle(s_pEventReceiverThread), &MyUnitTestLTEvent_EventProc, NULL, &counters, false);

    /* kick off the notification and wait for it all to finish */
    s_pEventNotifierThread->API->QueueTaskProc(s_pEventNotifierThread, &MyUnitTestLTEvent_NotifyEventTaskProc, NULL, &counters);
    s_pEventReceiverThread->API->WaitUntilFinished(s_pEventReceiverThread, LTTime_Infinite());
    s_pEventNotifierThread->API->WaitUntilFinished(s_pEventNotifierThread, LTTime_Infinite());

    TILT_EXPECT_TRUE(s_tilt, counters.notifyCount     == NUM_EVENT_NOTIFICATIONS, "LTEventTestResult: notifyCount");
    TILT_EXPECT_TRUE(s_tilt, counters.dispatchCount   == NUM_EVENT_NOTIFICATIONS, "LTEventTestResult: dispatchCount");
    TILT_EXPECT_TRUE(s_tilt, counters.receiptCount    == NUM_EVENT_NOTIFICATIONS, "LTEventTestResult: receiptCount");
    TILT_EXPECT_TRUE(s_tilt, counters.completionCount == NUM_EVENT_NOTIFICATIONS, "LTEventTestResult: completionCount");

    LT_GetCore()->DestroyHandle(s_hEvent);
    lt_destroyobject(s_pEventNotifierThread);
    lt_destroyobject(s_pEventReceiverThread);
    s_hEvent = 0;
    s_pEventNotifierThread = s_pEventReceiverThread = NULL;
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Feb-23   geta        created
 *  03-Jun-24   augustus    rewrote to be deterministic
 */
