/*******************************************************************************
 * LT Unit Test Camera Position library.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

/*
* The goal of this unit test is to:
* 1. Verify that the camera position library can transition between states correctly (Off, Sentry, Tracking, etc.). [IMPLEMENTED]
* 2. Verify that we can issue manual move commands to the camera position library. [NOT IMPLEMENTED]
* 3. Verify that we can set custom waypoints for the camera position library. [NOT IMPLEMENTED]
*/

#include <lt/core/LTThread.h>
#include <lt/media/cameraposition/LTMediaCameraPosition.h>
#include <tilt/JiltEngine.h>

typedef enum {
    kTestRunning_None,
    kTestRunning_StateTransition,
} TestRunning;

typedef enum {
    kTestEventCameraPositionReady = 0,
    kTestEventValidation,
    kTestEventCompletion,
} TestEventType;

typedef enum {
    kEventStateChange
} EventType;

typedef struct {
    LTList_Node node;
    EventType   event;
    union {
        LTMediaCameraPositionStateChangeEvent stateChange;
    } subEvent;
} EventsRecord;

typedef struct {
    u32 stateChangeCount;
    LTList eventsRecord;
} TestRecord;

static struct Statics {
    LTCore      *pCore;
    ILTThread   *iThread;
    ILTEvent    *iEvent;
    LTThread     hTestThread;
    LTEvent      hTestEvent;
    LTMutex     *testMutex;
    Tilt        *tilt;
    TestRecord  *testRecord;

    LTMediaCameraPosition                *pCameraPosition;
    LTMediaCameraPositionStateChangeEvent lastStateChange;
    LTMediaCameraPosition_Mode            lastReportedMode;

    TestRunning  testRunning;
    bool         bCameraPositionReady;
} S;

static JiltEngine *s_engine;

static const LTArgsDescriptor TestEventArgs = {1, {kLTArgType_u32}};

typedef void TestEventProc(TestEventType eventType, void *pClientData);
static void TestEvent(TestEventType eventType, void *pClientData);
static void TestEventDispatch(LTEvent event, void *proc, LTArgs *args, void *pClientData);

static void UnitTestDeferredCompletionCleanupHandler(Tilt *tilt, void *pClientData);

static void OnStateChangeEvent(LTMediaCameraPositionStateChangeEvent event, void *pClientData) {
    LT_UNUSED(pClientData);

    if(!S.bCameraPositionReady) {
        /* If we get here it must mean CameraPosition is ready. */
        S.bCameraPositionReady = true;
        S.iEvent->NotifyEvent(S.hTestEvent, kTestEventCameraPositionReady);
        return;
    }

    if (S.testRecord) {
        EventsRecord *pRecord = lt_malloc(sizeof(EventsRecord));
        pRecord->event = kEventStateChange;
        pRecord->subEvent.stateChange = event;
        LTList_AddTail(&S.testRecord->eventsRecord, &pRecord->node);
        S.testRecord->stateChangeCount++;
    }
    S.lastStateChange = event;
}

static bool CameraPositionThreadInit(void) {
    /* Open the camera position library. */
    S.pCameraPosition = lt_openlibrary(LTMediaCameraPosition);
    if (!S.pCameraPosition) {
        return false;
    }
    S.pCameraPosition->OnStateChangeEvent(OnStateChangeEvent, NULL, NULL);
    return true;
}

static void CameraPositionThreadDeInit(void) {
    S.testMutex->API->Lock(S.testMutex);

    if (S.pCameraPosition) {
        S.pCameraPosition->NoStateChangeEvent(OnStateChangeEvent);
        S.pCameraPosition->Switch(kLTMediaCameraPosition_IdleMode);
        lt_closelibrary(S.pCameraPosition);
    }

    S.pCameraPosition = NULL;
    S.testMutex->API->Unlock(S.testMutex);
}

static bool TestCameraPositionValidateState(Tilt *tilt, LTMediaCameraPosition_Mode expected, LTMediaCameraPosition_Mode actual) {
    if (expected != actual) {
        TILT_REPORT_FAILURE(tilt, "Mode not as expected. Expected: %d, Actual: %d", expected, actual);
        return false;
    }
    return true;
}

static void NotifyValidation(void *pClientData) {
    LT_UNUSED(pClientData);
    S.testRunning = kTestRunning_StateTransition;
    S.iEvent->NotifyEvent(S.hTestEvent, kTestEventValidation);
}

static void TestCameraPositionReady(void *pClientData) {
    LT_UNUSED(pClientData);
    S.testMutex->API->Lock(S.testMutex);
    /* Setup the test record. */
    S.testRecord = lt_malloc(sizeof(TestRecord));
    if (S.testRecord == NULL) {
        TILT_REPORT_FAILURE(S.tilt, "Failed to allocate test record");
        S.testMutex->API->Unlock(S.testMutex);
        return;
    }
    S.testRecord->stateChangeCount = 0;
    LTList_Init(&S.testRecord->eventsRecord);
    S.testMutex->API->Unlock(S.testMutex);

    LTMediaCameraPosition_Mode expectedMode;
    bool testPassed = true;
    /* Validate state switch function. */

    /* Test Off mode. */
    expectedMode = kLTMediaCameraPosition_IdleMode;
    S.pCameraPosition->Switch(expectedMode);
    testPassed &= TestCameraPositionValidateState(S.tilt, expectedMode, S.pCameraPosition->GetMode());

    /* Test Sentry mode. */
    expectedMode = kLTMediaCameraPosition_SentryMode;
    S.pCameraPosition->Switch(expectedMode);
    testPassed &= TestCameraPositionValidateState(S.tilt, expectedMode, S.pCameraPosition->GetMode());

    /* Test Tracking mode. */
    expectedMode = kLTMediaCameraPosition_TrackingMode;
    S.pCameraPosition->Switch(expectedMode);
    testPassed &= TestCameraPositionValidateState(S.tilt, expectedMode, S.pCameraPosition->GetMode());

    /* Test SentryAndTracking mode. */
    expectedMode = kLTMediaCameraPosition_SentryAndTrackingMode;
    S.pCameraPosition->Switch(expectedMode);
    testPassed &= TestCameraPositionValidateState(S.tilt, expectedMode, S.pCameraPosition->GetMode());

    if(testPassed) {
        TILT_INFO(S.tilt, "State transition test passed");
    } else {
        TILT_REPORT_FAILURE(S.tilt, "State transition test failed");
    }

    S.iThread->SetTimer(S.hTestThread, LTTime_Microseconds(100), NotifyValidation, NULL, NULL);
}

static bool TestCameraPositionValidateTestEvents(Tilt *tilt, LTList *pRecordedEvents, EventsRecord *pExpectedStateChangeEvents, u32 expectedStateChangeEventsCount) {
    u32 stateChangeEventsIdx = 0;

    /* Checking that the number of state change events recorded is the same as the expected number. Also that the recorded state change events match the expected state change events in order of occurrence. */
    LTList_ForEach(pNode, pRecordedEvents) {
        EventsRecord *pRecord = LT_CONTAINER_OF(pNode, EventsRecord, node);
        if (pRecord->event == kEventStateChange) {
            if (stateChangeEventsIdx < expectedStateChangeEventsCount) {
                if (pRecord->subEvent.stateChange != pExpectedStateChangeEvents[stateChangeEventsIdx].subEvent.stateChange) {
                    TILT_REPORT_FAILURE(tilt, "StateChange event %d does not match expected event %d", pRecord->subEvent.stateChange, pExpectedStateChangeEvents[stateChangeEventsIdx].subEvent.stateChange);
                    return false;
                }
                stateChangeEventsIdx++;
            } else {
                TILT_REPORT_FAILURE(tilt, "Unexpected state change event recorded");
                return false;
            }
        }
    } LTList_EndForEach;

    if (stateChangeEventsIdx != expectedStateChangeEventsCount) {
        TILT_REPORT_FAILURE(tilt, "Not all expected state change events were recorded, got %d, expected %d", stateChangeEventsIdx, expectedStateChangeEventsCount);
        return false;
    }

    return true;
}

static void TestCameraPositionStateTransitionValidate(void* pClientData) {
    LT_UNUSED(pClientData);
    if (S.testRecord) {
        EventsRecord expectedEvents[] = {
            { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMediaCameraPositioningStateChangeEvent_Off },
            { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMediaCameraPositioningStateChangeEvent_Sentry },
            { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMediaCameraPositioningStateChangeEvent_Tracking },
            { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMediaCameraPositioningStateChangeEvent_SentryAndTracking },
        };

        bool testPassed = true;
        testPassed &= TestCameraPositionValidateTestEvents(S.tilt, &S.testRecord->eventsRecord, expectedEvents, sizeof(expectedEvents) / sizeof(expectedEvents[0]));

        if (!testPassed) {
            TILT_REPORT_FAILURE(S.tilt, "State transition events test failed");
        } else {
            TILT_INFO(S.tilt, "State transition events test passed");
        }
    } else {
        TILT_REPORT_FAILURE(S.tilt, "No test record");
    }

    S.testRunning = kTestRunning_None;
    S.iEvent->NotifyEvent(S.hTestEvent, kTestEventCompletion);
}

static void TestEvent(TestEventType eventType, void *pClientData) {
    LT_UNUSED(pClientData);

    S.testMutex->API->Lock(S.testMutex);
    switch (eventType) {
        case kTestEventValidation:
            if (S.testRunning == kTestRunning_StateTransition) {
                TestCameraPositionStateTransitionValidate((void*)1);
            }
            break;
        case kTestEventCompletion:
            s_engine->API->SignalTestCompletion(s_engine, S.tilt->API->GetTestName());
            break;
        case kTestEventCameraPositionReady:
            TestCameraPositionReady((void*)1);
            break;
        default:
            break;
    }
    S.testMutex->API->Unlock(S.testMutex);
}

static void TestEventDispatch(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    TestEventType eventType = (TestEventType)LTArgs_u32At(0, args);
    TestEventProc *callback = (TestEventProc *)proc;
    callback(eventType, pClientData);
}

static void UnitTestCameraPositionStateTransition(Tilt * tilt) {
    LT_UNUSED(tilt);
    /* Initialise test event related fields. */
    S.lastStateChange = kLTMediaCameraPositioningStateChangeEvent_Off;
    S.bCameraPositionReady = false;
    S.iEvent->RegisterForEvent(S.hTestEvent, TestEvent, NULL, NULL, false);

    /* Start the camera position thread. */
    S.hTestThread = S.pCore->CreateThread("camerapositiontest");
    S.iThread = lt_gethandleinterface(ILTThread, S.hTestThread);
    S.iThread->SetStackSize(S.hTestThread, 1024);
    S.iThread->Start(S.hTestThread, CameraPositionThreadInit, CameraPositionThreadDeInit);

    /* Defer test completion. */
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Milliseconds(15000), UnitTestDeferredCompletionCleanupHandler, (void*)1);
}

static void CleanUp(void) {
    S.iEvent->UnregisterFromEvent(S.hTestEvent, TestEvent);
    if (S.hTestThread != LTHANDLE_INVALID) {
        S.iThread->Terminate(S.hTestThread);
        S.iThread->WaitUntilFinished(S.hTestThread, LTTime_Milliseconds(2000));
        lt_destroyhandle(S.hTestThread);
        S.hTestThread = LTHANDLE_INVALID;
    }

    if (S.testRecord) {
        LTList_ForEach(pNode, &S.testRecord->eventsRecord) {
            EventsRecord *pRecord = LT_CONTAINER_OF(pNode, EventsRecord, node);
            LTList_Remove(pNode);
            lt_free(pRecord);
        } LTList_EndForEach
        lt_free(S.testRecord);
        S.testRecord = NULL;
    }
}

static void UnitTestDeferredCompletionCleanupHandler(Tilt *tilt, void *pClientData) {
    LT_UNUSED(tilt); LT_UNUSED(pClientData);
    CleanUp();
}

static void BeforeAllTests(Tilt *tilt) {
    S = (struct Statics) {
        .pCore   = LT_GetCore(),
        .iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .iEvent  = lt_getlibraryinterface(ILTEvent, LT_GetCore()),
        .tilt    = tilt
    };

    S.testMutex = lt_createobject(LTMutex);
    TILT_EXPECT_TRUE(tilt, S.testMutex != NULL, "Cannot create mutex");

    S.hTestEvent = S.pCore->CreateEvent(&TestEventArgs, TestEventDispatch, NULL, NULL, NULL);
    TILT_EXPECT_TRUE(tilt, S.hTestEvent != 0, "Cannot create event");
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_destroyobject(S.testMutex);
    lt_destroyhandle(S.hTestEvent);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { UnitTestCameraPositionStateTransition, "StateTransition", "Camera position state transition test", 0 },
};

static int UnitTestLTMediaCameraPositionImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTMediaCameraPositionImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTMediaCameraPositionImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTMediaCameraPosition, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTMediaCameraPosition, UnitTestLTMediaCameraPositionImpl_Run, 1536) LTLIBRARY_DEFINITION;
