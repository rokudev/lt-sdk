/*******************************************************************************
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTThread.h>
#include <lt/device/motor/LTDeviceMotorPanTilt.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <tilt/JiltEngine.h>

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(UnitTestLTDeviceMotorPanTilt, (LTDeviceConfig));

/*********************************/
/*** Mock Motor implementation ***/
/*********************************/
/**
 * Owl 360 camera specific constants
 */
enum {
    kMockMotorDefaultSpeed = 300,
    kMockMotorMinSpeed = 300,
    kMockMotorMaxSpeed = 900,
    kMockMotorMaxPanPosition = 2540,
    kMockMotorMaxTiltPosition = 720,
    kMockMotorMaxPanAngle = 355,        // Specific to the Owl 360 camera
    kMockMotorMaxTiltAngle = 44,        // Specific to the Owl 360 camera
    kMockMotorUpdateIntervalMs = 8,
};

/**
 * Wheeler camera specific constants
 */
enum {
    kMockWheelerDefaultSpeed = 1000,
    kMockWheelerMinSpeed = 500,
    kMockWheelerMaxSpeed = 3000,
    kMockWheelerMaxPanPosition = 7964,
    kMockWheelerMaxTiltPosition = 3072,
    kMockWheelerMaxPanAngle = 350,
    kMockWheelerMaxTiltAngle = 135,
};

enum {
    kMockUpdateIntervalMs = 8,
    kMockMaxUserSpeed = 9,
};

static u32 s_MockDefaultSpeed;
static u32 s_MockMinSpeed;
static u32 s_MockMaxSpeed;
static s32 s_MockMaxPanPosition;
static s32 s_MockMaxTiltPosition;
static s32 s_MockMaxPanAngle;
static s32 s_MockMaxTiltAngle;

typedef enum {
    kMockMotorState_Stopped = 0,
    kMockMotorState_Running = 1,
} MockMotorState;

enum {
    MockEOK = 0,
    MockEPERM = -1,
    MockEFAULT = -14,
};

typedef struct {
    s32 panPosition;
    s32 tiltPosition;
    MockMotorState state;
    s32 speedStepsPerSecond;
} MockMotorStateInfo;

typedef struct {
    s32 panSteps;
    s32 tiltSteps;
} MockMotorMoveData;

typedef struct {
    s32 maxPanPosition;
    s32 maxTiltPosition;
    s32 panPosition;
    s32 tiltPosition;
} MockMotorResetData;

typedef struct {
    LTMutex *mutex;
    s32 maxPanPosition;
    s32 maxTiltPosition;
    s32 panPosition;
    s32 tiltPosition;
    s32 panMovement;
    s32 tiltMovement;
    u32 maxSpeed;
    u32 minSpeed;
    MockMotorState state;
    u32 speedStepsPerSecond;
    bool injectFailureOnNextMove;
} MockMotorStateData;

enum {
    kMockMotorCommand_Stop = 1,
    kMockMotorCommand_Reset = 2,
    kMockMotorCommand_Move = 3,
    kMockMotorCommand_GetStatus = 4,
    kMockMotorCommand_Speed = 5,
    // Other commands not supported
};

/*********************************/
/***     Test definitions      ***/
/*********************************/
typedef enum {
    kTestRunning_None,
    kTestRunning_Reset,
    kTestRunning_Move,
    kTestRunning_Sentry,
    kTestRunning_SentryPauseResume,
} TestRunning;

typedef enum {
    kEventStateChange,
    kEventMovement,
    kEventSentryPathStateChange,
} EventType;

enum {
    kMovementPanTolerance = 1,
    kMovementTiltTolerance = 1,
};

typedef enum {
    kTestEventMotorReady = 0,
    kTestEventValidation,
    kTestEventCompletion,
} TestEventType;

static LTMotorPanTilt_Position moveSubTests[] = {
    { .pan = 100, .tilt = 43,  .relative = false },
    { .pan = 20,  .tilt = 10,  .relative = false },
    { .pan = 1,   .tilt = 1,   .relative = true  },
    { .pan = 20,  .tilt = 10,  .relative = true  },
    { .pan = -1,  .tilt = -1,  .relative = true  },
    { .pan = -25, .tilt = -6,  .relative = true  },
    { .pan = 70,  .tilt = -10, .relative = true  },
    { .pan = 10,  .tilt = 10,  .relative = false }, // Move to absolute known position
    { .pan = -7,  .tilt = -7,  .relative = true  }, // Move to relative position, in range
    { .pan = -7,  .tilt = -7,  .relative = true  }, // Move to relative position, out of range
    { .pan = (kMockMotorMaxPanAngle - 10),  .tilt = (kMockMotorMaxTiltAngle) -10,  .relative = false }, // Move to absolute known position
    { .pan = 7,   .tilt = 7,   .relative = true  }, // Move to relative position, in range
    { .pan = 7,   .tilt = 7,   .relative = true  }, // Move to relative position, out of range
};

typedef struct {
    LTList_Node node;
    EventType event;
    union {
        LTMotorPanTilt_StateChangeEvent stateChange;
        LTMotorPanTilt_MovementChangeEvent movement;
        LTMotorPanTilt_SentryEvent sentryPathStateChange;
    } subEvent;
} EventsRecord;

typedef struct {
    u32 getStatusCount;
    u32 moveInProgressCount;
    u32 correctSentryMoveCount;
    LTList eventsRecord;
} TestRecord;

typedef enum {
    SentryPauseResumeTestState_Idle,
    SentryPauseResumeTestState_Start,
    SentryPauseResumeTestState_Paused,
    SentryPauseResumeTestState_Resumed,
} SentryPauseResumeTestState;

static struct Statics {
    Tilt               *tilt;
    LTCore             *pCore;
    ILTThread          *iThread;
    ILTEvent           *iEvent;
    LTThread            hTestThread;
    LTEvent             hTestEvent;
    MockMotorStateData  mockMotor;
    LTMutex            *testMutex;
    LTMotorPanTilt     *pMotor;
    LTMotorPanTilt_StateChangeEvent lastStateChange;
    LTMotorPanTilt_Position         lastReportedPosition;
    LTMotorPanTilt_Position         preMovePosition;
    TestRecord         *testRecord;
    TestRunning         testRunning;
    u32                 movingSubTestIdx;
    LTMotorPanTilt_SentryPath *sentryTestExpectedPath;
    u32                 sentryTestExpectedPathIdx;
    bool                ascendingSentryPath;
    SentryPauseResumeTestState sentryPauseTestState;
    bool                motorReady;
} S;

static JiltEngine *s_engine;

typedef void TestEventProc(TestEventType eventType, void *pClientData);
static const LTArgsDescriptor TestEventArgs = {1, {kLTArgType_u32}};
static void TestEvent(TestEventType eventType, void *pClientData);
static void TestEventDispatch(LTEvent event, void *proc, LTArgs *args, void *pClientData);
static bool IsWheeler(void);

/*************************************/
/***   Mock Motor implementation *****/
/*************************************/
static void MockMotorMoveProc(void * pClientData) {
    S.mockMotor.mutex->API->Lock(S.mockMotor.mutex);
    u32 intervalSteps = (u32)S.mockMotor.speedStepsPerSecond / (1000 / kMockUpdateIntervalMs);
    if (IsWheeler()) intervalSteps = intervalSteps << 2;

    if (lt_abs(S.mockMotor.panMovement) < intervalSteps) {
        S.mockMotor.panPosition += S.mockMotor.panMovement;
        S.mockMotor.panMovement = 0;
    } else {
        if (S.mockMotor.panMovement > 0) {
            S.mockMotor.panPosition += intervalSteps;
            S.mockMotor.panMovement -= intervalSteps;
        } else if (S.mockMotor.panMovement < 0) {
            S.mockMotor.panPosition -= intervalSteps;
            S.mockMotor.panMovement += intervalSteps;
        }
    }

    if (lt_abs(S.mockMotor.tiltMovement) < intervalSteps) {
        S.mockMotor.tiltPosition += S.mockMotor.tiltMovement;
        S.mockMotor.tiltMovement = 0;
    } else {
        if (S.mockMotor.tiltMovement > 0) {
            S.mockMotor.tiltPosition += intervalSteps;
            S.mockMotor.tiltMovement -= intervalSteps;
        } else if (S.mockMotor.tiltMovement < 0) {
            S.mockMotor.tiltPosition -= intervalSteps;
            S.mockMotor.tiltMovement += intervalSteps;
        }
    }

    if (S.mockMotor.panMovement == 0 && S.mockMotor.tiltMovement == 0) {
        TILT_DEBUG(S.tilt, "MockMotorMoveProc: Movement completed");
        S.mockMotor.state = kMockMotorState_Stopped;
        S.iThread->KillTimer(S.hTestThread, MockMotorMoveProc, pClientData);
    }

    S.mockMotor.mutex->API->Unlock(S.mockMotor.mutex);
}

static s32 MockMockIO(s32 command, unsigned long argv) {
    TILT_DEBUG(S.tilt, "motorio", "MockMockIO: command=%d, argv=%lu", command, argv);
    MockMotorResetData *pResetData = (MockMotorResetData *)argv;
    MockMotorStateInfo *pStateInfo = (MockMotorStateInfo *)argv;
    MockMotorMoveData *pMoveData = (MockMotorMoveData *)argv;

    s32 steps = S.mockMotor.maxSpeed / (1000 / kMockUpdateIntervalMs);
    u32 speed;
    switch (command) {
        case kMockMotorCommand_Stop:
            TILT_DEBUG(S.tilt, "MockMockIO: Stop");
            S.mockMotor.mutex->API->Lock(S.mockMotor.mutex);
            S.iThread->KillTimer(S.hTestThread, MockMotorMoveProc, NULL);
            S.mockMotor.state = kMockMotorState_Stopped;
            S.mockMotor.mutex->API->Unlock(S.mockMotor.mutex);
            break;

        case kMockMotorCommand_Reset:
            TILT_DEBUG(S.tilt, "MockMockIO: Reset");
            // Simulate blocking behavior of a reset
            S.mockMotor.mutex->API->Lock(S.mockMotor.mutex);
            S.mockMotor.state = kMockMotorState_Running;

            while(S.mockMotor.panPosition < s_MockMaxPanPosition || S.mockMotor.tiltPosition < s_MockMaxTiltPosition) {
                if (S.mockMotor.panPosition < s_MockMaxPanPosition) {
                    S.mockMotor.panPosition += steps;
                }
                if (S.mockMotor.tiltPosition < s_MockMaxTiltPosition) {
                    S.mockMotor.tiltPosition += steps;
                }
                if (S.mockMotor.panPosition > s_MockMaxPanPosition) {
                    S.mockMotor.panPosition = s_MockMaxPanPosition;
                }
                if (S.mockMotor.tiltPosition > s_MockMaxTiltPosition) {
                    S.mockMotor.tiltPosition = s_MockMaxTiltPosition;
                }
                S.iThread->Sleep(LTTime_Milliseconds(kMockUpdateIntervalMs));
            }
            S.mockMotor.maxPanPosition = S.mockMotor.panPosition;
            S.mockMotor.maxTiltPosition = S.mockMotor.tiltPosition;

            //Move back to middle
            while (S.mockMotor.panPosition * 2 > S.mockMotor.maxPanPosition || S.mockMotor.tiltPosition * 2 > S.mockMotor.maxTiltPosition) {
                if (S.mockMotor.panPosition * 2 > S.mockMotor.maxPanPosition) {
                    S.mockMotor.panPosition -= steps;
                }
                if (S.mockMotor.tiltPosition * 2 > S.mockMotor.maxTiltPosition) {
                    S.mockMotor.tiltPosition -= steps;
                }
                if (S.mockMotor.panPosition * 2 < S.mockMotor.maxPanPosition) {
                    S.mockMotor.panPosition = S.mockMotor.maxPanPosition / 2;
                }
                if (S.mockMotor.tiltPosition * 2 < S.mockMotor.maxTiltPosition) {
                    S.mockMotor.tiltPosition = S.mockMotor.maxTiltPosition / 2;
                }
                S.iThread->Sleep(LTTime_Milliseconds(kMockUpdateIntervalMs));
            }
            pResetData->panPosition = S.mockMotor.panPosition;
            pResetData->tiltPosition = S.mockMotor.tiltPosition;
            pResetData->maxPanPosition = S.mockMotor.maxPanPosition;
            pResetData->maxTiltPosition = S.mockMotor.maxTiltPosition;
            S.mockMotor.state = kMockMotorState_Stopped;
            S.mockMotor.mutex->API->Unlock(S.mockMotor.mutex);
            break;

        case kMockMotorCommand_Move:
            TILT_DEBUG(S.tilt, "MockMockIO: Move");
            S.mockMotor.mutex->API->Lock(S.mockMotor.mutex);
            if (S.mockMotor.state == kMockMotorState_Running) {
                TILT_DEBUG(S.tilt, "MockMockIO: Motor is already moving");
                return MockEPERM;
            }
            if (S.mockMotor.injectFailureOnNextMove) {
                S.mockMotor.injectFailureOnNextMove = false;
                return MockEFAULT;
            }
            S.mockMotor.state = kMockMotorState_Running;
            S.mockMotor.panMovement = pMoveData->panSteps;
            S.mockMotor.tiltMovement = pMoveData->tiltSteps;
            S.iThread->SetTimer(S.hTestThread, LTTime_Milliseconds(kMockUpdateIntervalMs), MockMotorMoveProc, NULL, NULL);
            S.mockMotor.mutex->API->Unlock(S.mockMotor.mutex);
            break;

        case kMockMotorCommand_GetStatus:
            TILT_DEBUG(S.tilt, "MockMockIO: GetStatus");
            if (S.testRecord) {
                S.testRecord->getStatusCount++;
            }
            pStateInfo->panPosition = S.mockMotor.panPosition;
            pStateInfo->tiltPosition = S.mockMotor.tiltPosition;
            pStateInfo->state = S.mockMotor.state;
            pStateInfo->speedStepsPerSecond = S.mockMotor.speedStepsPerSecond;
            break;

        case kMockMotorCommand_Speed:
            speed = *(u32 *)argv;
            TILT_DEBUG(S.tilt, "MockMockIO: Speed %d", speed);
            if (speed < S.mockMotor.minSpeed || speed > S.mockMotor.maxSpeed) {
                TILT_DEBUG(S.tilt, "MockMockIO: Speed out of range min: %d max: %d", S.mockMotor.minSpeed, S.mockMotor.maxSpeed);
                return MockEFAULT;
            }

            S.mockMotor.speedStepsPerSecond = speed;
            break;

        default:
            TILT_REPORT_FAILURE(S.tilt, "MockMockIO: Unsupported command %d", command);
            return MockEPERM;
    }
    return MockEOK;
}

/***********************************************/
/***   LTMotorPanTilt object event handlers  ***/
/***********************************************/
static void OnStateChangeEvent(LTMotorPanTilt_StateChangeEvent event, void *eventData, void *pClientData) {
    LT_UNUSED(eventData); LT_UNUSED(pClientData);
    S.testMutex->API->Lock(S.testMutex);
    if (S.lastStateChange != event) {
        if (S.testRecord) {
            EventsRecord *pRecord = lt_malloc(sizeof(EventsRecord));
            pRecord->event = kEventStateChange;
            pRecord->subEvent.stateChange = event;
            LTList_AddTail(&S.testRecord->eventsRecord, &pRecord->node);
            if (S.testRunning == kTestRunning_None) {
                if (S.tilt) TILT_REPORT_FAILURE(S.tilt, "Unexpected state change event %d", event);
            }
        }
        S.lastStateChange = event;
        if (event == kLTMotorPanTilt_StateChangeEvent_Idle) {
            LTMotorPanTilt_Position *currentPos = (LTMotorPanTilt_Position *)eventData;
            S.lastReportedPosition = *currentPos;
            if (S.testRecord) {
                if (!S.motorReady) {
                    TILT_INFO(S.tilt, "Motor ready");
                    S.motorReady = true;
                    LTList_ForEach(pNode, &S.testRecord->eventsRecord) {
                        EventsRecord *pRecord = LT_CONTAINER_OF(pNode, EventsRecord, node);
                        LTList_Remove(pNode);
                        lt_free(pRecord);
                    } LTList_EndForEach;
                    S.iEvent->NotifyEvent(S.hTestEvent, kTestEventMotorReady);
                } else {
                    S.iEvent->NotifyEvent(S.hTestEvent, kTestEventValidation);
                }
            }
        }
    }
    S.testMutex->API->Unlock(S.testMutex);
}

static void OnMovementEvent(LTMotorPanTilt_MovementChangeEvent event, void *eventData, void * pClientData) {
    LT_UNUSED(eventData); LT_UNUSED(pClientData);
    S.testMutex->API->Lock(S.testMutex);
    if (S.testRecord) {
        if (S.testRunning == kTestRunning_None) {
            if (S.tilt) TILT_REPORT_FAILURE(S.tilt, "Unexpected movement event %d", event);
        }
        if (event == kLTMotorPanTilt_MovementChangeEvent_InProgressMovement) {
            S.testRecord->moveInProgressCount++;
            if (S.testRunning == kTestRunning_SentryPauseResume) {
                if (S.sentryPauseTestState == SentryPauseResumeTestState_Start) {
                    S.pMotor->API->PauseSentryPath(S.pMotor, true);
                    S.sentryPauseTestState = SentryPauseResumeTestState_Paused;
                }
            }
            S.testMutex->API->Unlock(S.testMutex);
            return;
        }
        EventsRecord *pRecord = lt_malloc(sizeof(EventsRecord));
        pRecord->event = kEventMovement;
        pRecord->subEvent.movement = event;
        LTList_AddTail(&S.testRecord->eventsRecord, &pRecord->node);
    }
    S.testMutex->API->Unlock(S.testMutex);
}

static void OnSentryPathStateChangeEvent(LTMotorPanTilt_SentryEvent event, void *eventData, void *pClientData) {
    LT_UNUSED(eventData); LT_UNUSED(pClientData);
    S.testMutex->API->Lock(S.testMutex);
    if (S.testRecord) {
        EventsRecord *pRecord = lt_malloc(sizeof(EventsRecord));
        pRecord->event = kEventSentryPathStateChange;
        pRecord->subEvent.sentryPathStateChange = event;
        LTList_AddTail(&S.testRecord->eventsRecord, &pRecord->node);
        if (S.testRunning == kTestRunning_None) {
            if (S.tilt) TILT_REPORT_FAILURE(S.tilt, "Unexpected sentry path state change event %d", event);
        }
    }
    S.testMutex->API->Unlock(S.testMutex);
}

static bool IsWheeler(void) {
    const char *driverName = LT_GetLTDeviceConfig()->GetDriverAt("LTDeviceMotorPanTilt", 0);
    return lt_strcmp(driverName, "LTDriverMotorPanTiltMS320008N") == 0;
}

/***********************************************/
/***  LTMotorPanTilt object handling thread  ***/
/***********************************************/
static bool MotorThreadInit(void) {
    S.testMutex->API->Lock(S.testMutex);
    S.lastStateChange = kLTMotorPanTilt_StateChangeEvent_Error;
    S.pMotor = lt_createobject(LTMotorPanTilt);
    if (!S.pMotor) {
        TILT_REPORT_FAILURE(S.tilt, "Failed to create LTMotorPanTilt object");
        return false;
    }

    if (!S.pMotor->API->SetProperty(S.pMotor, "motoriofn", MockMockIO)) {
        TILT_REPORT_FAILURE(S.tilt, "Failed to set Mock IO function");
        return false;
    }

    if (!S.pMotor->API->OpenMotor(S.pMotor, kLTMotorPanTilt_Type_PanTilt) || !S.pMotor->API->IsOpen(S.pMotor)){
        TILT_REPORT_FAILURE(S.tilt, "Failed to open motor");
        return false;
    }

    S.pMotor->API->SetSpeed(S.pMotor, (s32)kMockMaxUserSpeed);
    S.pMotor->API->OnMotorStateChangeEvent(S.pMotor, OnStateChangeEvent, NULL);
    S.pMotor->API->OnMotorMovementEvent(S.pMotor, OnMovementEvent, NULL);
    S.pMotor->API->OnSentryPathStateChangeEvent(S.pMotor, OnSentryPathStateChangeEvent, NULL);
    S.testMutex->API->Unlock(S.testMutex);
    return true;
}

static void MotorThreadDeInit(void) {
    S.testMutex->API->Lock(S.testMutex);
    lt_destroyobject(S.pMotor);
    S.pMotor = NULL;
    S.testMutex->API->Unlock(S.testMutex);
}

/*************************************/
/***   Test validation functions   ***/
/*************************************/
static bool TestMotorValidatePosition(Tilt *tilt, LTMotorPanTilt_Position *pExpected, LTMotorPanTilt_Position *pActual) {
    if (pExpected->relative || pActual->relative) {
        TILT_REPORT_FAILURE(tilt, "Relative position not supported");
        return false;
    }

    u32 panDiff = lt_labs(pActual->pan - pExpected->pan);
    u32 tiltDiff = lt_labs(pActual->tilt - pExpected->tilt);

    if (panDiff > kMovementPanTolerance || tiltDiff > kMovementTiltTolerance) {
        TILT_REPORT_FAILURE(tilt, "Motor position not as expected. Pan: %d, expected %d, Tilt: %d, expected %d. Tolerance (+- %d, +- %d)",
                            pActual->pan, pExpected->pan,
                            pActual->tilt, pExpected->tilt,
                            kMovementPanTolerance, kMovementTiltTolerance);
        return false;
    }
    return true;
}

// Validate the recorded events against the expected events, ensuring ordering within each event type is correct
static bool TestMotorValidateTestEvents(Tilt *tilt, LTList *pRecordedEvents,
                                         EventsRecord *pExpectedStateChangeEvents, u32 expectedStateChangeEventsCount,
                                         EventsRecord *pExpectedMovementEvents, u32 expectedMovementEventsCount,
                                         EventsRecord *pExpectedSentryPathStateChangeEvents, u32 expectedSentryPathStateChangeEventsCount) {
    u32 stateChangeEventsIdx = 0;
    u32 movementEventsIdx = 0;
    u32 sentryPathStateChangeEventsIdx = 0;

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
        } else if (pRecord->event == kEventMovement) {
            if (movementEventsIdx < expectedMovementEventsCount) {
                if (pRecord->subEvent.movement != pExpectedMovementEvents[movementEventsIdx].subEvent.movement) {
                    TILT_REPORT_FAILURE(tilt, "Movement event %d does not match expected event %d", pRecord->subEvent.movement, pExpectedMovementEvents[movementEventsIdx].subEvent.movement);
                    return false;
                }
                movementEventsIdx++;
            } else {
                TILT_REPORT_FAILURE(tilt, "Unexpected movement event recorded");
                return false;
            }
        } else if (pRecord->event == kEventSentryPathStateChange) {
            if (sentryPathStateChangeEventsIdx < expectedSentryPathStateChangeEventsCount) {
                if (pRecord->subEvent.sentryPathStateChange != pExpectedSentryPathStateChangeEvents[sentryPathStateChangeEventsIdx].subEvent.sentryPathStateChange) {
                    TILT_REPORT_FAILURE(tilt, "SentryPathStateChange event %d does not match expected event %d", pRecord->subEvent.sentryPathStateChange, pExpectedSentryPathStateChangeEvents[sentryPathStateChangeEventsIdx].subEvent.sentryPathStateChange);
                    return false;
                }
                sentryPathStateChangeEventsIdx++;
            } else {
                TILT_REPORT_FAILURE(tilt, "Unexpected sentry path state change event recorded");
                return false;
            }
        }
    } LTList_EndForEach;
    return true;
}

static void TestMotorResetMotorReady(void) {
    S.pMotor->API->Reset(S.pMotor, NULL);
}

static void TestMotorMoveMotorReady(void) {
    S.preMovePosition = S.lastReportedPosition;
    S.movingSubTestIdx = 0;

    // Start the move
    TILT_INFO(S.tilt, "Starting next move to position %d, %d %s",
                            moveSubTests[S.movingSubTestIdx].pan,
                            moveSubTests[S.movingSubTestIdx].tilt,
                            moveSubTests[S.movingSubTestIdx].relative ? "relative" : "absolute");
    S.pMotor->API->MoveToPosition(S.pMotor, &moveSubTests[S.movingSubTestIdx]);
}

static void TestMotorSentryMotorReady(void) {
    LT_SIZE sentryPathSize;
    LTMotorPanTilt_SentryPath *sentryPath;

    // Set the sentry path
    // Create a copy for validation
    sentryPathSize = sizeof(LTMotorPanTilt_SentryPath) + sizeof(LTMotorPanTilt_SentryWaypoint) * 3;
    S.sentryTestExpectedPath = lt_malloc(sentryPathSize);
    if (!S.sentryTestExpectedPath) {
        TILT_REPORT_FAILURE(S.tilt, "Failed to allocate memory for expected sentry path");
        S.iEvent->NotifyEvent(S.hTestEvent, kTestEventCompletion);
        return;
    }
    sentryPath = lt_malloc(sentryPathSize);
    if (!sentryPath) {
        TILT_REPORT_FAILURE(S.tilt, "Failed to allocate memory for sentry path");
        lt_free(S.sentryTestExpectedPath);
        S.sentryTestExpectedPath = NULL;
        S.iEvent->NotifyEvent(S.hTestEvent, kTestEventCompletion);
        return;
    }

    S.sentryTestExpectedPath->numWaypoints = 3;
    S.sentryTestExpectedPath->waypoints[0] = (LTMotorPanTilt_SentryWaypoint){ .dwellTime = LTTime_Milliseconds(200), (LTMotorPanTilt_Position) { .pan = 350, .tilt = 43, .relative = false} };
    S.sentryTestExpectedPath->waypoints[1] = (LTMotorPanTilt_SentryWaypoint){ .dwellTime = LTTime_Milliseconds(200), (LTMotorPanTilt_Position) { .pan = 90, .tilt = 22, .relative = false}  };
    S.sentryTestExpectedPath->waypoints[2] = (LTMotorPanTilt_SentryWaypoint){ .dwellTime = LTTime_Milliseconds(200), (LTMotorPanTilt_Position) { .pan = 10, .tilt = 10, .relative = false}  };
    S.sentryTestExpectedPathIdx = 0;
    S.ascendingSentryPath = true;

    // Create a copy for the driver
    lt_memcpy(sentryPath, S.sentryTestExpectedPath, sentryPathSize);

    S.iEvent->RegisterForEvent(S.hTestEvent, TestEvent, NULL, NULL, false);
    S.pMotor->API->SetSentryPath(S.pMotor, sentryPath);
}

static void TestMotorResetValidate(void) {
    if (S.testRecord) {
        LTMotorPanTilt_Position maxPos;
        LTMotorPanTilt_Position minPos;
        LTMotorPanTilt_Position expectedPos;

        EventsRecord expectedEvents[] = {
            { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMotorPanTilt_StateChangeEvent_Resetting },
            { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMotorPanTilt_StateChangeEvent_Calibrating },
            { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMotorPanTilt_StateChangeEvent_Idle },
        };

        // Check that status were requested
        if (S.testRecord->getStatusCount == 0) {
            TILT_REPORT_FAILURE(S.tilt, "No status requests during reset");
        }

        TestMotorValidateTestEvents( S.tilt, &S.testRecord->eventsRecord,
                                     expectedEvents, sizeof(expectedEvents) / sizeof(expectedEvents[0]),
                                     NULL,           0,
                                     NULL,           0);

        // Check that the motor max position was set as expected
        expectedPos.relative = false;
        expectedPos.pan = s_MockMaxPanAngle;
        expectedPos.tilt = s_MockMaxTiltAngle;
        S.pMotor->API->GetMaxPosition(S.pMotor, &maxPos);
        TestMotorValidatePosition(S.tilt, &expectedPos, &maxPos);

        // Check that the motor min position was set as expected
        expectedPos.pan = 0;
        expectedPos.tilt = 0;
        expectedPos.relative = false;
        S.pMotor->API->GetMinPosition(S.pMotor, &minPos);
        TestMotorValidatePosition(S.tilt, &expectedPos, &minPos);

        // Check that the motor was reset to the correct position. i.e. centered, within tolerance
        expectedPos.pan = s_MockMaxPanAngle / 2;
        expectedPos.tilt = s_MockMaxTiltAngle / 2;
        TestMotorValidatePosition(S.tilt, &expectedPos, &S.lastReportedPosition);
    } else {
        TILT_REPORT_FAILURE(S.tilt, "No test record");
    }

    // Clear the recorded events for the next move
    LTList_ForEach(pNode, &S.testRecord->eventsRecord) {
        EventsRecord *pRecord = LT_CONTAINER_OF(pNode, EventsRecord, node);
        LTList_Remove(pNode);
        lt_free(pRecord);
    } LTList_EndForEach;

    S.testRunning = kTestRunning_None;
    S.iEvent->NotifyEvent(S.hTestEvent, kTestEventCompletion);
}

static void TestMotorMoveValidate(void) {
    if (S.testRecord) {
        // The ordering of events between state changes and movement events is not guaranteed
        // however, the ordering of the events within the same type needs to be validated
        bool failureEventExpected = false;
        EventsRecord expectedStateChangeEvents[] = {
            { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMotorPanTilt_StateChangeEvent_Moving },
            { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMotorPanTilt_StateChangeEvent_Idle },
        };
        EventsRecord expectedMovementEventsSuccess[] = {
            { (LTList_Node){}, kEventMovement, .subEvent.movement = kLTMotorPanTilt_MovementChangeEvent_StartedMovement },
            { (LTList_Node){}, kEventMovement, .subEvent.movement = kLTMotorPanTilt_MovementChangeEvent_CompletedMovement },
        };
        EventsRecord expectedMovementEventsFailure[] = {
            { (LTList_Node){}, kEventMovement, .subEvent.movement = kLTMotorPanTilt_MovementChangeEvent_StartedMovement },
            { (LTList_Node){}, kEventMovement, .subEvent.movement = kLTMotorPanTilt_MovementChangeEvent_FailedMovement},
        };
        LTMotorPanTilt_Position expectedPos;
        expectedPos.relative = false;
        if (moveSubTests[S.movingSubTestIdx].relative) {
            expectedPos = S.preMovePosition;
            expectedPos.pan += moveSubTests[S.movingSubTestIdx].pan;
            expectedPos.tilt += moveSubTests[S.movingSubTestIdx].tilt;
        } else {
            expectedPos.pan = moveSubTests[S.movingSubTestIdx].pan;
            expectedPos.tilt = moveSubTests[S.movingSubTestIdx].tilt;
        }

        // Check bounds and note if a failure event is expected
        if (expectedPos.pan > s_MockMaxPanAngle) {
            expectedPos.pan = s_MockMaxPanAngle;
            failureEventExpected = true;
        } else if (expectedPos.pan < 0) {
            expectedPos.pan = 0;
            failureEventExpected = true;
        }
        if (expectedPos.tilt > s_MockMaxTiltAngle) {
            expectedPos.tilt = s_MockMaxTiltAngle;
            failureEventExpected = true;
        } else if (expectedPos.tilt < 0) {
            expectedPos.tilt = 0;
            failureEventExpected = true;
        }

        // Check that status were requested
        if (S.testRecord->getStatusCount == 0) {
            TILT_REPORT_FAILURE(S.tilt, "No status requests during reset");
        }

        // Check that movement in progress events were recorded
        if (S.testRecord->moveInProgressCount == 0) {
            TILT_REPORT_FAILURE(S.tilt, "No movement in progress events recorded");
        }

        if (failureEventExpected) {
            TestMotorValidateTestEvents(S.tilt, &S.testRecord->eventsRecord,
                                        expectedStateChangeEvents, sizeof(expectedStateChangeEvents) / sizeof(expectedStateChangeEvents[0]),
                                        expectedMovementEventsFailure, sizeof(expectedMovementEventsFailure) / sizeof(expectedMovementEventsFailure[0]),
                                        NULL, 0);
        } else {
            TestMotorValidateTestEvents(S.tilt, &S.testRecord->eventsRecord,
                                        expectedStateChangeEvents, sizeof(expectedStateChangeEvents) / sizeof(expectedStateChangeEvents[0]),
                                        expectedMovementEventsSuccess, sizeof(expectedMovementEventsSuccess) / sizeof(expectedMovementEventsSuccess[0]),
                                        NULL, 0);
        }

        // Check that the motor position is as expected
        TestMotorValidatePosition(S.tilt, &expectedPos, &S.lastReportedPosition);

        // Check that GetCurrentPosition returns the same position
        LTMotorPanTilt_Position currentPos;
        if (S.pMotor->API->GetCurrentPosition(S.pMotor, &currentPos)) {
            TestMotorValidatePosition(S.tilt, &expectedPos, &currentPos);
        } else {
            TILT_REPORT_FAILURE(S.tilt, "Failed to get current position");
        }

        // Clear the recorded events for the next move
        LTList_ForEach(pNode, &S.testRecord->eventsRecord) {
            EventsRecord *pRecord = LT_CONTAINER_OF(pNode, EventsRecord, node);
            LTList_Remove(pNode);
            lt_free(pRecord);
        } LTList_EndForEach;

        // Move to the next test or conclude the test
        if (S.movingSubTestIdx + 1 >= sizeof(moveSubTests) / sizeof(moveSubTests[0])) {
            // All moves have been completed
            S.testRunning = kTestRunning_None;
            S.iEvent->NotifyEvent(S.hTestEvent, kTestEventCompletion);
        } else {
            S.testRecord->getStatusCount = 0;
            S.testRecord->moveInProgressCount = 0;
            S.movingSubTestIdx++;
            S.preMovePosition = S.lastReportedPosition;
            // Start next move
            TILT_INFO(S.tilt, "Starting next move to position %d, %d %s",
                            moveSubTests[S.movingSubTestIdx].pan,
                            moveSubTests[S.movingSubTestIdx].tilt,
                            moveSubTests[S.movingSubTestIdx].relative ? "relative" : "absolute");
            S.pMotor->API->MoveToPosition(S.pMotor, &moveSubTests[S.movingSubTestIdx]);
        }
    } else {
        TILT_REPORT_FAILURE(S.tilt, "No test record");
    }
}

static void TestMotorDelayedSuccessProc(void *pClientData) {
    S.iThread->KillTimer(S.hTestThread, TestMotorDelayedSuccessProc, pClientData);
    S.iEvent->NotifyEvent(S.hTestEvent, kTestEventCompletion);
}

static void TestMotorSentryValidate(void) {
    bool testFailed = false;
    if (!S.tilt) return;
    if (!S.testRecord) {
        TILT_INFO(S.tilt, "No test record");
        return;
    }
    if (!S.sentryTestExpectedPath) {
        TILT_INFO(S.tilt, "No sentry path");
        return;
    }

    EventsRecord expectedStateChangeEvents[] = {
        { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMotorPanTilt_StateChangeEvent_Moving},
        { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMotorPanTilt_StateChangeEvent_Idle},
    };
    EventsRecord expectedMovementEvents[] = {
        { (LTList_Node){}, kEventMovement, .subEvent.movement = kLTMotorPanTilt_MovementChangeEvent_StartedMovement},
        { (LTList_Node){}, kEventMovement, .subEvent.movement = kLTMotorPanTilt_MovementChangeEvent_CompletedMovement},
    };
    EventsRecord expectedSentryPathStateChangeEvents[] = {
        { (LTList_Node){}, kEventSentryPathStateChange, .subEvent.sentryPathStateChange = kLTMotorPanTilt_SentryEvent_MovingToWaypoint},
        { (LTList_Node){}, kEventSentryPathStateChange, .subEvent.sentryPathStateChange = kLTMotorPanTilt_SentryEvent_Dwelling},
    };

    // Check that status were requested
    if (S.testRecord->getStatusCount == 0) {
        TILT_REPORT_FAILURE(S.tilt, "No status requests during reset");
        testFailed = true;
    }

    // Check that movement in progress events were recorded
    if (S.testRecord->moveInProgressCount == 0) {
        TILT_REPORT_FAILURE(S.tilt, "No movement in progress events recorded");
        testFailed = true;
    }

    if (!testFailed && !TestMotorValidateTestEvents(S.tilt, &S.testRecord->eventsRecord,
                                    expectedStateChangeEvents,             sizeof(expectedStateChangeEvents) / sizeof(expectedStateChangeEvents[0]),
                                    expectedMovementEvents,                sizeof(expectedMovementEvents) / sizeof(expectedMovementEvents[0]),
                                    expectedSentryPathStateChangeEvents,   sizeof(expectedSentryPathStateChangeEvents) / sizeof(expectedSentryPathStateChangeEvents[0]))) {
        testFailed = true;
    }

    if(!testFailed && !TestMotorValidatePosition(S.tilt, &S.sentryTestExpectedPath->waypoints[S.sentryTestExpectedPathIdx].position, &S.lastReportedPosition)) {
        testFailed = true;
    }

    if (!testFailed) {
        S.testRecord->correctSentryMoveCount++;
        if (S.sentryTestExpectedPathIdx == (S.sentryTestExpectedPath->numWaypoints - 1)) S.ascendingSentryPath = false;
        else if (S.sentryTestExpectedPathIdx == 0) S.ascendingSentryPath = true;
        S.ascendingSentryPath ? ++S.sentryTestExpectedPathIdx : --S.sentryTestExpectedPathIdx;


        // Clear the recorded events for the next move
        LTList_ForEach(pNode, &S.testRecord->eventsRecord) {
            EventsRecord *pRecord = LT_CONTAINER_OF(pNode, EventsRecord, node);
            LTList_Remove(pNode);
            lt_free(pRecord);
        } LTList_EndForEach;

        // if we have verified all waypoints + loop back to the first waypoint, then the test is complete
        // it will have 2n - 1 no of stops
        if (S.testRecord->correctSentryMoveCount > (2 * S.sentryTestExpectedPath->numWaypoints - 1)) {
            // Move to pause/resume test
            S.testRunning = kTestRunning_SentryPauseResume;
            S.sentryPauseTestState = SentryPauseResumeTestState_Start;
            TILT_INFO(S.tilt, "Sentry Path verified, testing pause sentry path");
            // Nothing to be done here, actions will be triggered by the next waypoint move
        } else {
            S.testRecord->getStatusCount = 0;
            S.testRecord->moveInProgressCount = 0;
            TILT_INFO(S.tilt, "Sentry Waypoint verified");
        }
    } else {
        // Test failed, signal a stop to the sentry path test
        TILT_INFO(S.tilt, "Failed to verify sentry Waypoint %d", S.sentryTestExpectedPathIdx);
        S.testRunning = kTestRunning_None;
        S.iEvent->NotifyEvent(S.hTestEvent, kTestEventCompletion);
    }
}

static void TestMotorSentryPauseResumeValidate(void) {
    bool testFailed = false;
    if (!S.tilt) return;
    if (!S.testRecord) {
        TILT_INFO(S.tilt, "No test record");
        return;
    }
    if (!S.sentryTestExpectedPath) {
        TILT_INFO(S.tilt, "No sentry path");
        return;
    }

    // Check that status were requested
    if (S.testRecord->getStatusCount == 0) {
        TILT_REPORT_FAILURE(S.tilt, "No status requests during reset");
        testFailed = true;
    }

    // Check that movement in progress events were recorded
    if (S.testRecord->moveInProgressCount == 0) {
        TILT_REPORT_FAILURE(S.tilt, "No movement in progress events recorded");
        testFailed = true;
    }

    if (S.sentryPauseTestState == SentryPauseResumeTestState_Paused) {
        EventsRecord expectedStateChangeEvents[] = {
            { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMotorPanTilt_StateChangeEvent_Moving},
            { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMotorPanTilt_StateChangeEvent_Idle},
        };
        EventsRecord expectedMovementEvents[] = {
            { (LTList_Node){}, kEventMovement, .subEvent.movement = kLTMotorPanTilt_MovementChangeEvent_StartedMovement},
            { (LTList_Node){}, kEventMovement, .subEvent.movement = kLTMotorPanTilt_MovementChangeEvent_CompletedMovement},
        };
        EventsRecord expectedSentryPathStateChangeEvents[] = {
            { (LTList_Node){}, kEventSentryPathStateChange, .subEvent.sentryPathStateChange = kLTMotorPanTilt_SentryEvent_MovingToWaypoint},
            { (LTList_Node){}, kEventSentryPathStateChange, .subEvent.sentryPathStateChange = kLTMotorPanTilt_SentryEvent_Paused},
        };

        if(!testFailed && !TestMotorValidateTestEvents(S.tilt, &S.testRecord->eventsRecord,
                                    expectedStateChangeEvents,             sizeof(expectedStateChangeEvents) / sizeof(expectedStateChangeEvents[0]),
                                    expectedMovementEvents,                sizeof(expectedMovementEvents) / sizeof(expectedMovementEvents[0]),
                                    expectedSentryPathStateChangeEvents,   sizeof(expectedSentryPathStateChangeEvents) / sizeof(expectedSentryPathStateChangeEvents[0]))) {
            testFailed = true;
        }

        // Clear the recorded events for the next move
        LTList_ForEach(pNode, &S.testRecord->eventsRecord) {
            EventsRecord *pRecord = LT_CONTAINER_OF(pNode, EventsRecord, node);
            LTList_Remove(pNode);
            lt_free(pRecord);
        } LTList_EndForEach;

        if (!testFailed) {
            // Resume the sentry path
            S.pMotor->API->PauseSentryPath(S.pMotor, false);
            S.sentryPauseTestState = SentryPauseResumeTestState_Resumed;
            return;
        }
        // Test failed, fall through to cleanup
    } else if (S.sentryPauseTestState == SentryPauseResumeTestState_Resumed) {
        EventsRecord expectedStateChangeEvents[] = {
            { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMotorPanTilt_StateChangeEvent_Moving},
            { (LTList_Node){}, kEventStateChange, .subEvent.stateChange = kLTMotorPanTilt_StateChangeEvent_Idle},
        };
        EventsRecord expectedMovementEvents[] = {
            { (LTList_Node){}, kEventMovement, .subEvent.movement = kLTMotorPanTilt_MovementChangeEvent_StartedMovement},
            { (LTList_Node){}, kEventMovement, .subEvent.movement = kLTMotorPanTilt_MovementChangeEvent_CompletedMovement},
        };
        EventsRecord expectedSentryPathStateChangeEvents[] = {
            { (LTList_Node){}, kEventSentryPathStateChange, .subEvent.sentryPathStateChange = kLTMotorPanTilt_SentryEvent_MovingToWaypoint},
            { (LTList_Node){}, kEventSentryPathStateChange, .subEvent.sentryPathStateChange = kLTMotorPanTilt_SentryEvent_Dwelling},
        };

        if(!testFailed && !TestMotorValidateTestEvents(S.tilt, &S.testRecord->eventsRecord,
                                    expectedStateChangeEvents,             sizeof(expectedStateChangeEvents) / sizeof(expectedStateChangeEvents[0]),
                                    expectedMovementEvents,                sizeof(expectedMovementEvents) / sizeof(expectedMovementEvents[0]),
                                    expectedSentryPathStateChangeEvents,   sizeof(expectedSentryPathStateChangeEvents) / sizeof(expectedSentryPathStateChangeEvents[0]))) {
            testFailed = true;
        }

        if(!testFailed && !TestMotorValidatePosition(S.tilt, &S.sentryTestExpectedPath->waypoints[S.sentryTestExpectedPathIdx].position, &S.lastReportedPosition)) {
            testFailed = true;
        }
    }

    // clear the recorded events
    LTList_ForEach(pNode, &S.testRecord->eventsRecord) {
        EventsRecord *pRecord = LT_CONTAINER_OF(pNode, EventsRecord, node);
        LTList_Remove(pNode);
        lt_free(pRecord);
    } LTList_EndForEach;

    if (!testFailed) {
        TILT_INFO(S.tilt, "Sentry Path Pause Resume verified, disabling sentry mode");
        S.pMotor->API->SetSentryPath(S.pMotor, NULL);
        // Wait for potential next move
        S.iThread->SetTimer(S.hTestThread, LTTime_Milliseconds(500), TestMotorDelayedSuccessProc, NULL, NULL);
        S.testRunning = kTestRunning_None;
    } else {
        // Test failed, signal a stop to the sentry path test
        TILT_INFO(S.tilt, "Failed to verify sentry Waypoint %d", S.sentryTestExpectedPathIdx);
        S.testRunning = kTestRunning_None;
        S.iEvent->NotifyEvent(S.hTestEvent, kTestEventCompletion);
    }
}

/*****************************************************/
/***   Deferred completion Test cleanup functions  ***/
/*****************************************************/
static void DeferredCompletionCleanupHandler(Tilt *tilt, void *pClientData) {
    LT_UNUSED(pClientData);
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
            TILT_INFO(tilt, "Unexpected Event %d after timeout", pRecord->event);
            LTList_Remove(pNode);
            lt_free(pRecord);
        } LTList_EndForEach
        lt_free(S.testRecord);
        S.testRecord = NULL;
    }

    lt_free(S.sentryTestExpectedPath);
    S.sentryTestExpectedPath = NULL;
    S.sentryTestExpectedPathIdx = 0;
}

/*************************************/
/***         Test functions        ***/
/*************************************/
static void TestEvent(TestEventType eventType, void *pClientData) {
    LT_UNUSED(pClientData);
    S.testMutex->API->Lock(S.testMutex);
    switch (eventType) {
        case kTestEventMotorReady:
            // Motor is ready
            if (S.testRunning == kTestRunning_Reset) {
                TestMotorResetMotorReady();
            } else if (S.testRunning == kTestRunning_Move) {
                TestMotorMoveMotorReady();
            } else if (S.testRunning == kTestRunning_Sentry) {
                 TestMotorSentryMotorReady();
            }
            break;
        case kTestEventValidation:
            // Validation event is triggered
            if (S.testRunning == kTestRunning_Reset) {
                TestMotorResetValidate();
            } else if (S.testRunning == kTestRunning_Move) {
                TestMotorMoveValidate();
            } else if (S.testRunning == kTestRunning_Sentry) {
                TestMotorSentryValidate();
            } else if (S.testRunning == kTestRunning_SentryPauseResume) {
                TestMotorSentryPauseResumeValidate();
            }
            break;
        case kTestEventCompletion:
            if (S.testRecord) {
                LTList_ForEach(pNode, &S.testRecord->eventsRecord) {
                    EventsRecord *pRecord = LT_CONTAINER_OF(pNode, EventsRecord, node);
                    TILT_REPORT_FAILURE(S.tilt, "Unexpected Event %d at end of test", pRecord->event);
                    LTList_Remove(pNode);
                    lt_free(pRecord);
                } LTList_EndForEach
                lt_free(S.testRecord);
                S.testRecord = NULL;
            }
            s_engine->API->SignalTestCompletion(s_engine, S.tilt->API->GetTestName());
            break;
        default:
            TILT_REPORT_FAILURE(S.tilt, "Unknown event type %d", eventType);
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

static void UnitTestMotorReset(Tilt *tilt) {
    S.testMutex->API->Lock(S.testMutex);
    S.lastStateChange = kLTMotorPanTilt_StateChangeEvent_Error;

    //Setup the test record
    S.testRecord = lt_malloc(sizeof(TestRecord));
    if (!S.testRecord) {
        TILT_REPORT_FAILURE(tilt, "Failed to allocate memory for test record");
        return;
    }
    S.testRecord->getStatusCount = 0;
    LTList_Init(&S.testRecord->eventsRecord);
    S.testRunning = kTestRunning_Reset;
    S.motorReady = false;
    S.iEvent->RegisterForEvent(S.hTestEvent, TestEvent, NULL, NULL, false);

    // Start the motor thread
    S.hTestThread = S.pCore->CreateThread("motorobject");
    S.iThread->Start(S.hTestThread, MotorThreadInit, MotorThreadDeInit);
    S.testMutex->API->Unlock(S.testMutex);

    s_engine->API->DeferTestCompletion(s_engine, LTTime_Milliseconds(10000), DeferredCompletionCleanupHandler, (void*)1);
}

static void UnitTestMotorMove(Tilt *tilt) {
    S.testMutex->API->Lock(S.testMutex);
    S.lastStateChange = kLTMotorPanTilt_StateChangeEvent_Error;

    //Setup the test record
    S.testRecord = lt_malloc(sizeof(TestRecord));
    if (!S.testRecord) {
        TILT_REPORT_FAILURE(tilt, "Failed to allocate memory for test record");
        return;
    }
    S.testRecord->getStatusCount = 0;
    S.testRecord->moveInProgressCount = 0;
    LTList_Init(&S.testRecord->eventsRecord);
    S.testRunning = kTestRunning_Move;
    S.motorReady = false;
    S.iEvent->RegisterForEvent(S.hTestEvent, TestEvent, NULL, NULL, false);

    // Start the motor thread
    S.hTestThread = S.pCore->CreateThread("motorobject");
    S.iThread->Start(S.hTestThread, MotorThreadInit, MotorThreadDeInit);
    S.testMutex->API->Unlock(S.testMutex);

    s_engine->API->DeferTestCompletion(s_engine, LTTime_Milliseconds(20000), DeferredCompletionCleanupHandler, (void*)1);
}

static void UnitTestMotorSentry(Tilt *tilt) {
    S.testMutex->API->Lock(S.testMutex);
    S.lastStateChange = kLTMotorPanTilt_StateChangeEvent_Error;

    //Setup the test record
    S.testRecord = lt_malloc(sizeof(TestRecord));
    if (!S.testRecord) {
        TILT_REPORT_FAILURE(tilt, "Failed to allocate memory for test record");
        return;
    }
    S.testRecord->getStatusCount = 0;
    S.testRecord->moveInProgressCount = 0;
    S.testRecord->correctSentryMoveCount = 0;
    LTList_Init(&S.testRecord->eventsRecord);
    S.testRunning = kTestRunning_Sentry;
    S.motorReady = false;
    S.iEvent->RegisterForEvent(S.hTestEvent, TestEvent, NULL, NULL, false);

    // Start the motor thread
    S.hTestThread = S.pCore->CreateThread("motorobject");
    S.iThread->Start(S.hTestThread, MotorThreadInit, MotorThreadDeInit);
    S.testMutex->API->Unlock(S.testMutex);

    s_engine->API->DeferTestCompletion(s_engine, LTTime_Milliseconds(20000), DeferredCompletionCleanupHandler, (void*)1);
}

static void BeforeAllTests(Tilt *tilt) {
    S = (struct Statics) {};
    S.tilt       = tilt;
    S.pCore      = LT_GetCore();
    S.iThread    = lt_getlibraryinterface(ILTThread, LT_GetCore());
    S.iEvent     = lt_getlibraryinterface(ILTEvent, LT_GetCore());
    S.hTestEvent = S.pCore->CreateEvent(&TestEventArgs, TestEventDispatch, NULL, NULL, NULL);
    TILT_EXPECT_TRUE(tilt, S.hTestEvent != 0, "Cannot create test event");
    if (S.hTestEvent == 0) return;

    S.testMutex = lt_createobject(LTMutex);
    TILT_EXPECT_TRUE(tilt, S.testMutex != NULL, "Cannot create test mutex");

    S.mockMotor = (MockMotorStateData) {};
    S.mockMotor.mutex = lt_createobject(LTMutex);
    TILT_EXPECT_TRUE(tilt, S.mockMotor.mutex != NULL, "Cannot create test mutex");

    if (IsWheeler()) {
        s_MockDefaultSpeed     = (u32)kMockWheelerDefaultSpeed;
        s_MockMinSpeed         = (u32)kMockWheelerMinSpeed;
        s_MockMaxSpeed         = (u32)kMockWheelerMaxSpeed;
        s_MockMaxPanPosition   = (s32)kMockWheelerMaxPanPosition;
        s_MockMaxTiltPosition  = (s32)kMockWheelerMaxTiltPosition;
        s_MockMaxPanAngle      = (s32)kMockWheelerMaxPanAngle;
        s_MockMaxTiltAngle     = (s32)kMockWheelerMaxTiltAngle;
    } else {
        s_MockDefaultSpeed     = (u32)kMockMotorDefaultSpeed;
        s_MockMinSpeed         = (u32)kMockMotorMinSpeed;
        s_MockMaxSpeed         = (u32)kMockMotorMaxSpeed;
        s_MockMaxPanPosition   = (s32)kMockMotorMaxPanPosition;
        s_MockMaxTiltPosition  = (s32)kMockMotorMaxTiltPosition;
        s_MockMaxPanAngle      = (s32)kMockMotorMaxPanAngle;
        s_MockMaxTiltAngle     = (s32)kMockMotorMaxTiltAngle;
    }
    S.mockMotor.speedStepsPerSecond = s_MockDefaultSpeed;
    S.mockMotor.maxSpeed = s_MockMaxSpeed;
    S.mockMotor.minSpeed = s_MockMinSpeed;
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_destroyhandle(S.hTestEvent);
    lt_destroyobject(S.mockMotor.mutex);
    lt_destroyobject(S.testMutex);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { UnitTestMotorReset,   "Reset",    "Motor reset test",           0 },
    { UnitTestMotorMove,    "Move",     "Motor move test",            0 },
    { UnitTestMotorSentry,  "Sentry",   "Sentry path following test", 0 },
};

static int UnitTestLTDeviceMotorPanTiltImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceMotorPanTiltImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceMotorPanTiltImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceMotorPanTilt, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceMotorPanTilt, UnitTestLTDeviceMotorPanTiltImpl_Run, 1536) LTLIBRARY_DEFINITION;
