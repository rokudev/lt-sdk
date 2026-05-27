/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreTimerTests.c>
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

enum {
    kMaxTimers                  = 4,   /**< Max number of timers to test concurrently */
    kTimerExpirationTolMS       = 3,   /**< Timer expiration tolerance */
};

typedef struct {
    LTTime  nextExpireTime;     /**< Time of next expected expiration */
    void  * proc;               /**< Timer proc being used */
    u16     timerMS;            /**< Underlying timer period */
    u16     autoCount;          /**< Remaining number of timer reschedules */
    u16     procCount;          /**< Number of times proc invoked */
    u16     altProcCount;       /**< Number of times alternate proc invoked */
    u16     errCount;           /**< Number of errors encountered */
    u8      releaseCount;       /**< Number of times release proc invoked */
} TimerData;

static void DoTimerProc(void * pClientData) {
    LTTime timeNow = LT_GetCore()->GetKernelTime();
    TimerData * pData = (TimerData *)pClientData;
    if (!LTTime_IsZero(pData->nextExpireTime)) {
        LTTime timeError = LTTime_Subtract(timeNow, pData->nextExpireTime);
        if (timeError.nNanoseconds < 0) timeError.nNanoseconds = 0 - timeError.nNanoseconds;
        if (LTTime_IsGreaterThan(timeError, LTTime_Milliseconds(kTimerExpirationTolMS))) {
            pData->errCount++;
        }
        pData->nextExpireTime = LTTime_Add(pData->nextExpireTime, LTTime_Milliseconds(pData->timerMS));
    } else {
        pData->nextExpireTime = LTTime_Add(timeNow, LTTime_Milliseconds(pData->timerMS));
    }
    pData->procCount++;
    if (pData->autoCount > 0) {
        pData->autoCount--;
    } else {
        LTOThread * thread = LT_GetCore()->GetCurrentThreadObject();
        thread->API->KillTimer(thread, pData->proc, pData);
    }
}

static void TimerProc0(void * pClientData) {
    DoTimerProc(pClientData);
}

static void TimerProc1(void * pClientData) {
    DoTimerProc(pClientData);
}

static void TimerProc2(void * pClientData) {
    DoTimerProc(pClientData);
}

static void TimerProc3(void * pClientData) {
    DoTimerProc(pClientData);
}

static void AltTimerProc(void * pClientData) {
    TimerData * pData = (TimerData *)pClientData;
    pData->altProcCount++;
    LTOThread * thread = LT_GetCore()->GetCurrentThreadObject();
    thread->API->KillTimer(thread, AltTimerProc, pData);
}

static void MyReleaseProc(LTThread_ReleaseReason reason, void * pClientData) {
    LT_UNUSED(reason);
    TimerData * pData = (TimerData *)pClientData;
    pData->releaseCount++;
}

static void ResetTimerData(TimerData data[kMaxTimers]) {
    lt_memset(data, 0, sizeof(TimerData) * kMaxTimers);
    for (u32 tmr = 0; tmr < kMaxTimers; tmr++) data[tmr].proc = TimerProc0;
    data[0].timerMS = 5;
    data[1].timerMS = 17;
    //data[2].timerMS = 77;
    //data[3].timerMS = 477;
    data[2].timerMS = 57;
    data[3].timerMS = 247;
}

void TestTimer(Tilt * tilt) {
    TimerData data[kMaxTimers];

    /* Create the test thread */
    LTOThread * thread = lt_createobject(LTOThread);
    thread->API->SetPriority(thread, kLTThread_PriorityHighest);
    thread->API->Start(thread, "TimerThread", NULL, NULL);

    /* API Abuse */
    {
        thread->API->SetTimer(thread, LTTime_Milliseconds(0), NULL, MyReleaseProc, &data[2]);
        thread->API->SetTimer(thread, LTTime_Milliseconds(0), NULL, NULL, &data[2]);
        thread->API->SetTimer(thread, LTTime_Milliseconds(0), NULL, NULL, NULL);
        //TODO: thread->API->SetTimer(NULL, LTTime_Milliseconds(0), NULL, NULL, NULL);
        thread->API->SetTimerAbsolute(thread, LTTime_Milliseconds(0), LTTime_Milliseconds(0), NULL, MyReleaseProc, &data[2]);
        thread->API->SetTimerAbsolute(thread, LTTime_Milliseconds(0), LTTime_Milliseconds(0), NULL, NULL, &data[2]);
        thread->API->SetTimerAbsolute(thread, LTTime_Milliseconds(0), LTTime_Milliseconds(0),  NULL, NULL, NULL);
        //TODO: thread->API->SetTimerAbsolute(NULL, LTTime_Milliseconds(0), LTTime_Milliseconds(0), NULL, NULL, NULL);
        thread->API->RestartTimer(thread, NULL, NULL);
        //TODO: thread->API->RestartTimer(NULL, NULL, NULL);
        thread->API->KillTimer(thread, NULL, NULL);
        //TODO: thread->API->KillTimer(NULL, NULL, NULL);
        thread->API->GetTimerExpirationKernelTime(thread, data[2].proc, &data[2]);
        thread->API->GetTimerExpirationKernelTime(thread, NULL, NULL);
        //TODO: thread->API->GetTimerExpirationKernelTime(NULL, data[2].proc, &data[2]);
    }

    /* Create several timers and kill them before they expire */
    {
        ResetTimerData(data);
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) thread->API->SetTimer(thread, LTTime_Milliseconds(data[tmr].timerMS), data[tmr].proc, MyReleaseProc, &data[tmr]);
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) thread->API->KillTimer(thread, data[tmr].proc, &data[tmr]);
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].procCount    == 0, "Proc count != 0");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].errCount     == 0, "Error count != 0");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].releaseCount == 1, "Release count != 1");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, LTTime_IsZero(thread->API->GetTimerExpirationKernelTime(thread, data[tmr].proc, &data[tmr])),
                                                                          "Get Expiration time != 0");
    }

    /* Create several timers and allow them to expire once each (one-shot) */
    {
        //enum { kWaitTimeMS = 750 };
        enum { kWaitTimeMS = 500 };
        ResetTimerData(data);
        LTTime timeNow = LT_GetCore()->GetKernelTime();
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) data[tmr].nextExpireTime = LTTime_Add(timeNow, LTTime_Milliseconds(data[tmr].timerMS));
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) thread->API->SetTimer(thread, LTTime_Milliseconds(data[tmr].timerMS), data[tmr].proc, MyReleaseProc, &data[tmr]);
        thread->API->Sleep(LTTime_Milliseconds(kWaitTimeMS));
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].procCount    == 1, "Proc count != 1");
        //TODO: for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].errCount     == 0, "Error count != 0");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].releaseCount == 1, "Release count != 1");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, LTTime_IsZero(thread->API->GetTimerExpirationKernelTime(thread, data[tmr].proc, &data[tmr])),
                                                                          "Get Expiration time != 0");
        /* Kill the timer again after the release, should do no harm */
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) thread->API->KillTimer(thread, data[tmr].proc, &data[tmr]);
        thread->API->Sleep(LTTime_Milliseconds(kWaitTimeMS));
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].procCount    == 1, "Proc count != 1");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].releaseCount == 1, "Release count != 1");
    }

    /* Repeat above but use different task procs for each timer */
    {
        //enum { kWaitTimeMS = 750 };
        enum { kWaitTimeMS = 500 };
        ResetTimerData(data);
        data[1].proc = TimerProc1; data[2].proc = TimerProc2; data[3].proc = TimerProc3;
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) thread->API->SetTimer(thread, LTTime_Milliseconds(data[tmr].timerMS), data[tmr].proc, MyReleaseProc, &data[tmr]);
        thread->API->Sleep(LTTime_Milliseconds(kWaitTimeMS));
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].procCount    == 1, "Proc count != 1");
        //TODO: for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].errCount     == 0, "Error count != 0");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].releaseCount == 1, "Release count != 1");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, LTTime_IsZero(thread->API->GetTimerExpirationKernelTime(thread, data[tmr].proc, &data[tmr])),
                                                                          "Get Expiration time != 0");
    }

    /* Timers with different procs but with same client data */
    {
        enum { kWaitTimeMS = 50 };
        ResetTimerData(data);
        thread->API->SetTimer(thread, LTTime_Milliseconds(data[0].timerMS), TimerProc0, MyReleaseProc, &data[0]);
        thread->API->SetTimer(thread, LTTime_Milliseconds(data[0].timerMS), AltTimerProc, MyReleaseProc, &data[0]);
        thread->API->Sleep(LTTime_Milliseconds(kWaitTimeMS));
        //TODO: TILT_EXPECT_TRUE(tilt, data[0].procCount    == 1, "Proc count != 1");
        //TODO: TILT_EXPECT_TRUE(tilt, data[0].altProcCount == 1, "Alt Proc count != 1");
        //TODO: TILT_EXPECT_TRUE(tilt, data[0].errCount     == 0, "Error count != 0");
        //TODO: TILT_EXPECT_TRUE(tilt, data[0].releaseCount == 2, "Release count != 2");
    }

    /* Create several timers and allow them to expire and auto-restart multiple times  */
    {
        //enum { kWaitTimeMS = 2000 };
        //static const u16 autoRestarts[kMaxTimers] = { 254, 30, 10, 2 };
        enum { kWaitTimeMS = 1000 };
        static const u16 autoRestarts[kMaxTimers] = { 127, 15, 5, 1 };
        ResetTimerData(data);
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) data[tmr].autoCount = autoRestarts[tmr];
        LTTime timeNow = LT_GetCore()->GetKernelTime();
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) data[tmr].nextExpireTime = LTTime_Add(timeNow, LTTime_Milliseconds(data[tmr].timerMS));
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) thread->API->SetTimer(thread, LTTime_Milliseconds(data[tmr].timerMS), data[tmr].proc, MyReleaseProc, &data[tmr]);
        thread->API->Sleep(LTTime_Milliseconds(kWaitTimeMS));
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].procCount    == autoRestarts[tmr] + 1, "Proc count != expected");
        //TODO: for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].errCount     == 0,                     "Error count != 0");
        //TODO: for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].releaseCount == 1,                     "Release count != 1");
    }

    /* Restarting a timer */
    {
        ResetTimerData(data);
        thread->API->SetTimer(thread, LTTime_Milliseconds(data[2].timerMS), data[2].proc, MyReleaseProc, &data[2]);
        /* Restart a timer a number of times such that it shouldn't execute its timer proc */
        for (u32 ix = 0; ix < 3; ix++) {
            thread->API->Sleep(LTTime_Milliseconds(data[2].timerMS >> 1));
            if (data[2].procCount) {
                TILT_EXPECT_TRUE(tilt, data[2].procCount == 0, "Proc count != 0");
                /* this iteration failed; kill, reset, and set again so that erronious failure on next iteration isn't reported */
                thread->API->KillTimer(thread, data[2].proc, &data[2]);
                ResetTimerData(data);
                thread->API->SetTimer(thread, LTTime_Milliseconds(data[2].timerMS), data[2].proc, MyReleaseProc, &data[2]);
            }
            thread->API->RestartTimer(thread, data[2].proc, &data[2]);
        }
        /* Do the same thing again but with the SetTimer() API */
        for (u32 ix = 0; ix < 3; ix++) {
            thread->API->Sleep(LTTime_Milliseconds(data[2].timerMS >> 1));
            if (data[2].procCount) {
                TILT_EXPECT_TRUE(tilt, data[2].procCount == 0, "Proc count != 0");
                /* this iteration failed; kill, reset, and set again so that erronious failure on next iteration isn't reported */
                thread->API->KillTimer(thread, data[2].proc, &data[2]);
                ResetTimerData(data);
            }
            thread->API->SetTimer(thread, LTTime_Milliseconds(data[2].timerMS), data[2].proc, MyReleaseProc, &data[2]);
        }
        /* Allow the timer proc to finally execute */
        thread->API->Sleep(LTTime_Milliseconds(data[2].timerMS + 2));
        TILT_EXPECT_TRUE(tilt, data[2].procCount    == 1, "Proc count != 1");
        TILT_EXPECT_TRUE(tilt, data[2].releaseCount == 1, "Release count != 1");
    }

    /* Create absolute timers and kill them before they expire */
    {
        ResetTimerData(data);
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) thread->API->SetTimerAbsolute(thread, LTTime_Zero(), LTTime_Milliseconds(data[tmr].timerMS),
                                                                                    data[tmr].proc, MyReleaseProc, &data[tmr]);
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) thread->API->KillTimer(thread, data[tmr].proc, &data[tmr]);
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].procCount    == 0, "Proc count != 0");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].errCount     == 0, "Error count != 0");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].releaseCount == 1, "Release count != 1");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, LTTime_IsZero(thread->API->GetTimerExpirationKernelTime(thread, data[tmr].proc, &data[tmr])),
                                                                          "Get Expiration time != 0");
    }

    /* Create absolute timers with zero reference time and allow them to expire and auto-restart multiple times  */
    {
        //enum { kWaitTimeMS = 2000 };
        //static const u16 autoRestarts[kMaxTimers] = { 258, 28, 12, 2 };
        enum { kWaitTimeMS = 1000 };
        static const u16 autoRestarts[kMaxTimers] = { 129, 14, 6, 1 };
        ResetTimerData(data);
        LTTime timeNow = LT_GetCore()->GetKernelTime();
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) data[tmr].nextExpireTime = LTTime_Add(timeNow, LTTime_Milliseconds(data[tmr].timerMS));
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) data[tmr].autoCount = autoRestarts[tmr];
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) thread->API->SetTimerAbsolute(thread, LTTime_Zero(), LTTime_Milliseconds(data[tmr].timerMS),
                                                                                    data[tmr].proc, MyReleaseProc, &data[tmr]);
        thread->API->Sleep(LTTime_Milliseconds(kWaitTimeMS));
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].procCount    == autoRestarts[tmr] + 1, "Proc count != expected");
        //TODO: for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].errCount     == 0,                     "Error count != 0");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].releaseCount == 1,                     "Release count != 1");
    }

    /* Set absolute timers with past reference time */
    {
//        static const u16 autoRestarts[kMaxTimers] = { 260, 27, 11, 2 };
//        enum { kRefTimeOffsetMS = 1200, kWaitTimeMS = 2000 };
        enum { kRefTimeOffsetMS = 600, kWaitTimeMS = 1000 };
        static const u16 autoRestarts[kMaxTimers] = { 130, 13, 5, 1 };
        ResetTimerData(data);
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) data[tmr].autoCount = autoRestarts[tmr];
        LTTime timePast = LTTime_Subtract(LT_GetCore()->GetKernelTime(), LTTime_Milliseconds(kRefTimeOffsetMS));
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) thread->API->SetTimerAbsolute(thread, timePast, LTTime_Milliseconds(data[tmr].timerMS),
                                                                                    data[tmr].proc, MyReleaseProc, &data[tmr]);
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) {
            u32 offsetMS = ((kRefTimeOffsetMS / data[tmr].timerMS) + 1) * data[tmr].timerMS;
            LTTime firstExpireTime = LTTime_Add(timePast, LTTime_Milliseconds(offsetMS));
            TILT_EXPECT_TRUE(tilt, LTTime_IsEqual(thread->API->GetTimerExpirationKernelTime(thread, data[tmr].proc, &data[tmr]), firstExpireTime),
                                                     "Unexpected expiration time");
            data[tmr].nextExpireTime = firstExpireTime;
        }
        thread->API->Sleep(LTTime_Milliseconds(kWaitTimeMS));
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].procCount    == autoRestarts[tmr] + 1, "Proc count != expected");
        //TODO: for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].errCount     == 0,                     "Error count != 0");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].releaseCount == 1,                     "Release count != 1");
    }

    /* Set absolute timers with future reference time */
    {
        //enum { kRefTimeOffsetMS = 500, kDwellTimeMS = 250, kWaitTimeMS = 2250 };
        //static const u16 autoRestarts[kMaxTimers] = { 260, 27, 11, 2 };
        enum { kRefTimeOffsetMS = 250, kDwellTimeMS = 125, kWaitTimeMS = 1125 };
        static const u16 autoRestarts[kMaxTimers] = { 130, 13, 5, 1 };
        ResetTimerData(data);
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) data[tmr].autoCount = autoRestarts[tmr];
        LTTime timeFuture = LTTime_Add(LT_GetCore()->GetKernelTime(), LTTime_Milliseconds(kRefTimeOffsetMS));
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) data[tmr].nextExpireTime = timeFuture;
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) thread->API->SetTimerAbsolute(thread, timeFuture, LTTime_Milliseconds(data[tmr].timerMS),
                                                                                    data[tmr].proc, MyReleaseProc, &data[tmr]);
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt,
                                                                    LTTime_IsEqual(thread->API->GetTimerExpirationKernelTime(thread, data[tmr].proc, &data[tmr]),
                                                                                       timeFuture), "Unexpected expiration time");
        thread->API->Sleep(LTTime_Milliseconds(kDwellTimeMS));
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].procCount    == 0, "Proc count != 0");
        thread->API->Sleep(LTTime_Milliseconds(kWaitTimeMS));
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].procCount    == autoRestarts[tmr] + 1, "Proc count != expected");
        //TODO:for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].errCount     == 0,                     "Error count != 0");
        for (u32 tmr = 0; tmr < kMaxTimers; tmr++) TILT_EXPECT_TRUE(tilt, data[tmr].releaseCount == 1,                     "Release count != 1");
    }

    /* Tear down test thread */
    thread->API->Terminate(thread);
    thread->API->WaitUntilFinished(thread, LTTime_Infinite());
    lt_destroyobject(thread);
}

/*************************************************************************************
 * Timer precision test */

#define NUM_TIMER_FIRES_TOTAL       20
#define TIMER_INTERVAL_MS           50
#define INTERVAL_ALLOWED_SLOP_MS    2
#define TOTAL_ALLOWED_SLOP_MS       (NUM_TIMER_FIRES_TOTAL*INTERVAL_ALLOWED_SLOP_MS)

typedef struct TimerPrecClientData {
    u32 nTimerFires;
    LTTime startFireTime;
    LTTime lastFireTime;
    LTTime accumulatedFireTime;
} TimerPrecClientData;

static void MyTimerProc(void * pClientData) {
    LTTime timeNow = LT_GetCore()->GetKernelTime();
    TimerPrecClientData * cd = (TimerPrecClientData *)pClientData;
    if (LTTime_IsZero(cd->startFireTime)) { cd->startFireTime = timeNow; cd->lastFireTime = timeNow; return; }

    LTTime interval = LTTime_Subtract(timeNow, cd->lastFireTime);
    cd->lastFireTime = timeNow;
    LTTime_AddTo(cd->accumulatedFireTime, interval);
    cd->nTimerFires++;
    if (cd->nTimerFires == NUM_TIMER_FIRES_TOTAL) {
        LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
        thread->API->KillTimer(thread, &MyTimerProc, pClientData);
        thread->API->Terminate(thread);
    }
}

static char * s_pTimeString;
static const char * TimeString1(LTTime time) { LT_GetCore()->FormatCanonicalTimeString(time, s_pTimeString,      24, true); return s_pTimeString; }
static const char * TimeString2(LTTime time) { LT_GetCore()->FormatCanonicalTimeString(time, s_pTimeString + 24, 24, true); return s_pTimeString + 24; }

void TestTimerPrecision(Tilt * tilt) {
    TimerPrecClientData cd = { 0, {0}, {0}, {0} };

    // ok for the timer tests we'll create a thread of highest priority; let it run for 20 iterations of 50ms
    // and see how close we get to 1s total time.
    LTOThread *thread = lt_createobject(LTOThread);
    thread->API->SetPriority(thread, kLTThread_PriorityHighest);
    thread->API->Start(thread, "TimerThread", NULL, NULL);
    thread->API->SetTimer(thread, LTTime_Milliseconds(TIMER_INTERVAL_MS), &MyTimerProc, NULL, &cd);
    thread->API->WaitUntilFinished(thread, LTTime_Infinite());
    lt_destroyobject(thread);

    LTTime        totalTimeExpected = LTTime_Multiply(LTTime_Milliseconds(TIMER_INTERVAL_MS), NUM_TIMER_FIRES_TOTAL);
    LTTime          totalTimeActual = LTTime_Subtract(cd.lastFireTime, cd.startFireTime);
    LTTime          totalSlopActual = LTTime_Subtract(totalTimeActual, totalTimeExpected);
    LTTime  averageIntervalRealized = LTTime_Divide(cd.accumulatedFireTime, cd.nTimerFires);
    LTTime      averageSlopRealized = LTTime_Subtract(averageIntervalRealized, LTTime_Milliseconds(TIMER_INTERVAL_MS));

    if (NULL != (s_pTimeString = lt_malloc(48))) {
        TILT_REPORT(tilt, "TimerPrec:   TotalTimeExpected%s,       TotalTimeActual%s", TimeString1(totalTimeExpected), TimeString2(totalTimeActual));
        TILT_REPORT(tilt, "TimerPrec:    TimerIntervalSet%s, TimerIntervalRealized%s", TimeString1(LTTime_Milliseconds(TIMER_INTERVAL_MS)), TimeString2(averageIntervalRealized));
        TILT_REPORT(tilt, "TimerPrec:    TotalSlopAllowed%s,       TotalSlopActual%s", TimeString1(LTTime_Milliseconds(TOTAL_ALLOWED_SLOP_MS)), TimeString2(totalSlopActual));
        TILT_REPORT(tilt, "TimerPrec: IntervalSlopAllowed%s,  IntervalSlopRealized%s", TimeString1(LTTime_Milliseconds(INTERVAL_ALLOWED_SLOP_MS)), TimeString2(averageSlopRealized));
        lt_free(s_pTimeString); s_pTimeString = NULL;
    }

    // test on slop absolute value
    if (totalSlopActual.nNanoseconds     < 0) totalSlopActual.nNanoseconds     = 0 - totalSlopActual.nNanoseconds;
    if (averageSlopRealized.nNanoseconds < 0) averageSlopRealized.nNanoseconds = 0 - averageSlopRealized.nNanoseconds;

    TILT_EXPECT_TRUE(tilt, cd.nTimerFires == NUM_TIMER_FIRES_TOTAL, "TimerPrec: nTimerFires");
    TILT_EXPECT_TRUE(tilt, !LTTime_IsGreaterThan(totalSlopActual, LTTime_Milliseconds(TOTAL_ALLOWED_SLOP_MS)), "TimerPrec: total slop greater than allowed");
    TILT_EXPECT_TRUE(tilt, !LTTime_IsGreaterThan(averageSlopRealized, LTTime_Milliseconds(INTERVAL_ALLOWED_SLOP_MS)), "TimerPrec: average interval slop exceeds interval allowed slop");
}

