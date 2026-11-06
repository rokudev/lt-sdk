/*******************************************************************************
 * lt/source/lt/device/power/LTDevicePower.c
 *
 * LT Device Library for ambient light sensors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTArray.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/power/LTDevicePower.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>
#include <lt/driver/power/LTDriverPower.h>
#include <lt/driver/power/LTDriverPowerClient.h>

// These values will only be used if the code was unable to pick them up from the JSON file
#define DEFAULT_SLEEP_IDLE_TRIGGER_TIMEOUT_MS   (1)   /* arbitrary */
#define DEFAULT_MINIMUM_SLEEP_DURATION_MS       (1000) /* arbitrary */
#define DEFAULT_MAXIMUM_SLEEP_DURATION_MS       (3000) /* arbitrary, but whatever we want the watchdog interval during sleep to be */

DEFINE_LTLOG_SECTION("dev.power")

/*  ___________________________
 *  Object private data members
 */
typedef_LTObjectImpl(LTDevicePower, LTDevicePowerImpl) {
} LTOBJECT_API;

static struct Statics {
    LTDriverPower              *driverPower;
    LTOThread                  *powerThread;
    bool                        ownsPowerThread;
    LTAtomic                    atomicSleepModeEnabled;
    LTArray                    *driverPowerClients;
    bool                        missingPowerClient;     // a configured client could not be added, so sleep stays off
    LTTime                      sleepIdleTriggerTimeout;
    LTTime                      minimumSleepDuration;
    LTTime                      defaultSleepIdleTriggerTimeout;
    LTTime                      defaultMinimumSleepDuration;
    LTTime                      defaultMaximumSleepDuration;
    LTDevicePower_WakeupReason  lastWakeupReason;
} S;

/*  ____________________________________________________
 *  LTCoreEnterSleepModeProc - LTCore calls this procedure
 *  procedure in the context of the powerThread after
 *  elevating powerThread to the highest possible priority.
 *  It is guaranteed that no other threads will run unless this procedure relinquishes the CPU
 *  This procedure and all of the procesdures it calls should not relinquish the CPU under penalty of
 *  undefined behavior.
 */
static LTTime LTCoreEnterSleepModeProc(LTTime softwareSleepWakeupTime, void * pClientData) {
    LT_UNUSED(pClientData);
    S.lastWakeupReason = kLTDevicePower_WakeupReason_Unknown;

    /* check maximum boundary */
    LTTime maxWakeupTime = LTTime_Add(LT_GetCore()->GetKernelTime(), S.defaultMaximumSleepDuration);
    
    if (LTTime_IsZero(softwareSleepWakeupTime) || LTTime_IsGreaterThan(softwareSleepWakeupTime, maxWakeupTime)) softwareSleepWakeupTime = maxWakeupTime;

#ifdef LTCORE_LOG_SLEEP_MODE_ACTIVITY
    static char s_timeBuff[24];
    LT_GetCore()->FormatCanonicalTimeString(softwareSleepWakeupTime, s_timeBuff, sizeof(s_timeBuff), true);
    LTLOG_STOMP("sleep.enter", "softwareRequiredWakeup at %s", s_timeBuff);
    LT_GetCore()->ConsoleStompChar('\a');
#endif

    /* call all of the powerDriverClient->GoToSleep() functions in reverse order */
    u32 nCount = S.driverPowerClients->API->GetCount(S.driverPowerClients);
    u32 nIndex = nCount;

    /* disable interrupts while calling the power driver clients and while calling the power driver to enter sleep */
    LT_SIZE nMask = LT_GetCore()->Disable();

    while (nIndex--) {
        LTDriverPowerClient *driverPowerClient = (LTDriverPowerClient *)S.driverPowerClients->API->Get(S.driverPowerClients, nIndex, NULL);
        if (driverPowerClient->API->GoToSleep) driverPowerClient->API->GoToSleep(driverPowerClient);
    }

    /* any client may veto hardware sleep for this attempt or wake us up earlier; WakeFromSleep is still called on every client below.
       Asked in GoToSleep order, so the first client in the list has the last word right before sleep */
    bool readyToSleep = true;
    nIndex = nCount;
    while (readyToSleep && nIndex--) {
        LTDriverPowerClient *driverPowerClient = (LTDriverPowerClient *)S.driverPowerClients->API->Get(S.driverPowerClients, nIndex, NULL);
        if (driverPowerClient->API->IsReadyToSleep) readyToSleep = driverPowerClient->API->IsReadyToSleep(driverPowerClient, &softwareSleepWakeupTime);
    }

    /* call the power driver to enter sleep, returning the duration spent in sleep and setting the wakeup reason */
    if (readyToSleep) {
        softwareSleepWakeupTime = S.driverPower->API->EnterSleep(softwareSleepWakeupTime, &S.lastWakeupReason);
    } else {
        softwareSleepWakeupTime = LTTime_Zero();
        S.lastWakeupReason      = kLTDevicePower_WakeupReason_SleepSkipped;
    }

    /* call all of the driverPowerClient->API->AwakenedFromSleep() functions in forward order */
    for (nIndex = 0; nIndex < nCount; nIndex++) {
        LTDriverPowerClient *driverPowerClient = (LTDriverPowerClient *)S.driverPowerClients->API->Get(S.driverPowerClients, nIndex, NULL);
        if (driverPowerClient->API->WakeFromSleep) driverPowerClient->API->WakeFromSleep(driverPowerClient);
    }
    LT_GetCore()->Enable(nMask);

    /* don't log the sleep duration here because the kernel time isn't readjusted yet; it will be logged in LTCore after the kernel time is adjusted */

    return softwareSleepWakeupTime;
}

/*  _____________________________________
 *  PowerThread ThreadInit and ThreadExit
 */
static bool PowerThreadInit(void) {
    LT_GetCore()->SetEnterSleepModeIdleDelayAndMinimumSleepDuration(LTTime_Zero(), LTTime_Zero());
    LT_GetCore()->SetEnterSleepModeProc(&LTCoreEnterSleepModeProc, NULL);
    LTAtomic_Store(&S.atomicSleepModeEnabled, 0);
    /* Sleeping without one of the configured clients would leave its hardware unprepared, so stay awake */
    if (S.missingPowerClient) {
        LT_GetCore()->SetEnterSleepModeProc(NULL, NULL);
        return true;
    }
    /* Enable() runs after the proc is set, so sleep actions the driver registers count as the sleep thread's.
       A driver that cannot support sleep gets no proc, and the device just stays awake */
    if (S.driverPower->API->Enable && !S.driverPower->API->Enable(true)) {
        LT_GetCore()->SetEnterSleepModeProc(NULL, NULL);
        LTLOG_YELLOWALERT("driver.enable.fail", NULL);
    }
    return true;
}

static void PowerThreadExit(void) {
    LT_GetCore()->SetEnterSleepModeIdleDelayAndMinimumSleepDuration(LTTime_Zero(), LTTime_Zero());
    LT_GetCore()->SetEnterSleepModeProc(NULL, NULL);
    LTAtomic_Store(&S.atomicSleepModeEnabled, 0);
    if (S.driverPower->API->Enable) S.driverPower->API->Enable(false);
}

/*  _________________________________
 *  Object constructor and destructor
 */
static void LTDevicePowerImpl_DestructObject(LTDevicePowerImpl *power) {
    LT_UNUSED(power);
}

static bool LTDevicePowerImpl_ConstructObject(LTDevicePowerImpl *power) {
    LT_UNUSED(power);
    return true;
}

static void LTDevicePower_LibFini(void) {
    if (S.powerThread) {
        /* a thread bound through BindToCurrentThread belongs to the caller; only detach sleep handling from it */
        if (S.ownsPowerThread) lt_destroyobject(S.powerThread);
        else PowerThreadExit();
    }
    S.powerThread     = NULL;
    S.ownsPowerThread = false;
    if (S.driverPowerClients) {
        u32 nCount = S.driverPowerClients->API->GetCount(S.driverPowerClients);
        for (u32 i = 0; i < nCount; i++) {
            lt_destroyobject((LTObject *)S.driverPowerClients->API->Get(S.driverPowerClients, i, NULL));
        }
        lt_destroyobject(S.driverPowerClients);
    }
    lt_destroyobject(S.driverPower);
}

static bool LTDevicePower_LibInit(void) {
    LTAtomic_Store(&S.atomicSleepModeEnabled, 0);
    S.lastWakeupReason = kLTDevicePower_WakeupReason_Unknown;
    S.powerThread = NULL;
    S.ownsPowerThread = false;
    S.missingPowerClient = false;

    const char *driverName = NULL;
    do {
        LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
        if (!deviceConfig) {
            LTLOG_YELLOWALERT("f.devc.open", "could not open device config");
            break;
        }
        driverName = deviceConfig->GetDriverAt("LTDevicePower", 0);
        if (!driverName) {
            LTLOG_YELLOWALERT("f.nodrv", "no driver name found in device config");
            break;
        }

        // read and build the array of LTDriverPowerClient objects
        if (NULL == (S.driverPowerClients = lt_createobject(LTArray))) {
            LTLOG_YELLOWALERT("f.arraycreate", "failed to create driverPowerClients array");
            break;
        }

        u32 section = deviceConfig->GetDeviceSection("LTDevicePower");
        LTString key = ltstring_create("");
        enum { kMaxIndices = 256 };
        for (int index = 0; index < kMaxIndices; index++) {
            ltstring_format(&key, "driverPowerClients/%d", index);
            const char * specialization = deviceConfig->ReadString(section, key);
            if (NULL == specialization) break;
            LTDriverPowerClient *driverPowerClient = (LTDriverPowerClient *)lt_createobject_named("LTDriverPowerClient", specialization);
            if (!driverPowerClient || S.driverPowerClients->API->Append(S.driverPowerClients, driverPowerClient) < 0) {
                if (driverPowerClient) lt_destroyobject(driverPowerClient);
                S.missingPowerClient = true;
                LTLOG_REDALERT("client.missing", "%s: sleep stays off", specialization);
            }
        }
        ltstring_destroy(key);
        /* driver power clients in wakeup init invocation order. TODO add irled, ircutA, ircutB, boost, LED logic (Jira-847)*/
        S.driverPowerClients->API->Trim(S.driverPowerClients);
        /* defaultSleepIdleTriggerTimeout: too low = go to sleep quickly, too high = never goes to sleep depending on event cadence in LT. Fine tuning will be coved in (JIRA 825)*/
        S.defaultSleepIdleTriggerTimeout = LTTime_Milliseconds(deviceConfig->ReadInteger(section, "sleepIdleTriggerTimeoutMS"));
        /* LTCore checks the trigger on its 1 ms idle tick, so a sub-millisecond timeout starts sleep on the next tick */
        s64 sleepIdleTriggerTimeoutUS = deviceConfig->ReadInteger(section, "sleepIdleTriggerTimeoutUS");
        if (sleepIdleTriggerTimeoutUS > 0) S.defaultSleepIdleTriggerTimeout = LTTime_Microseconds(sleepIdleTriggerTimeoutUS);
        /* Minimum amount of time sleep, used to determine if system go to sleep, or just skip sleep cycle if time asked to sleep is too low */
        S.defaultMinimumSleepDuration    = LTTime_Milliseconds(deviceConfig->ReadInteger(section, "minimumSleepDurationMS"));
        /* Maximum amount of time to sleep, used to override requests from kernel to sleep any time longer, or the default infinite */
        S.defaultMaximumSleepDuration    = LTTime_Milliseconds(deviceConfig->ReadInteger(section, "maximumSleepDurationMS"));

        if (LTTime_IsZero(S.defaultSleepIdleTriggerTimeout)) S.defaultSleepIdleTriggerTimeout = LTTime_Milliseconds(DEFAULT_SLEEP_IDLE_TRIGGER_TIMEOUT_MS);
        if (LTTime_IsZero(S.defaultMinimumSleepDuration))    S.defaultMinimumSleepDuration    = LTTime_Milliseconds(DEFAULT_MINIMUM_SLEEP_DURATION_MS);
        if (LTTime_IsZero(S.defaultMaximumSleepDuration))    S.defaultMaximumSleepDuration    = LTTime_Milliseconds(DEFAULT_MAXIMUM_SLEEP_DURATION_MS);

        S.sleepIdleTriggerTimeout = LTTime_Zero();
        S.minimumSleepDuration = LTTime_Zero();

        // done with LTDeviceConfig
        lt_closelibrary(deviceConfig);

        // create the driver object
        if (NULL == (S.driverPower = (LTDriverPower *)lt_createobject_named("LTDriverPower", driverName))) {
            LTLOG_YELLOWALERT("f.create.driver.object", "failed to create LTDriverPower object %s", driverName);
            break;
        }
        // Thread will be created later via BindToCurrentThread() or on first EnableSleepMode() call
        S.lastWakeupReason = kLTDevicePower_WakeupReason_Unknown;
        return true;
    } while (0);

    LTDevicePower_LibFini();
    return false;
}

static void LTDevicePowerImpl_EnableSleepModeTaskProc(void *pClientData) {
    LTTime *times = (LTTime *)pClientData;
    S.sleepIdleTriggerTimeout = *times;
    S.minimumSleepDuration = *(times + 1);
    LT_GetCore()->SetEnterSleepModeIdleDelayAndMinimumSleepDuration(S.sleepIdleTriggerTimeout, S.minimumSleepDuration);
    LTAtomic_Store(&S.atomicSleepModeEnabled, 1);
}

static void LTDevicePowerImpl_EnableSleepModeTaskCompleteProc(LTThread_ReleaseReason releaseReason, void *pClientData) {
    LT_UNUSED(releaseReason);
    lt_free(pClientData);
}

static void LTDevicePowerImpl_DisableSleepModeTaskProc(void *pClientData) {
    LT_UNUSED(pClientData);
    LT_GetCore()->SetEnterSleepModeIdleDelayAndMinimumSleepDuration(LTTime_Zero(), LTTime_Zero());
    LTAtomic_Store(&S.atomicSleepModeEnabled, 0);
}

/*  __________________________________
 *  LTDevicePower public api functions
 */

/* Singleton-style getter for power thread - creates on first access if needed */
static LTOThread *GetPowerThread(void) {
    if (S.powerThread) {
        return S.powerThread;  /* Thread already exists */
    }
    
    /* Create and start a default internal thread */
    if (NULL == (S.powerThread = lt_createobject(LTOThread))) {
        LTLOG_YELLOWALERT("thread.create.fail", NULL);
        return NULL;
    }
    
    S.powerThread->API->SetStackSize(S.powerThread, 1024);
    S.powerThread->API->SetPriority(S.powerThread, 1);
    /* set the priority to 1, so that changes to sleep mode enablement will actuate over the default thread priority threads;
       this really isn't that important that it is ultra-responsive because sleep mode won't enter anyway until all threads are
       idle and any changes to sleep mode enablement will have already occurred.   The thread priority set here also won't
       affect the thread's priority during actuation of sleep mode because LTCore will have elevated it during actuation.
     */
    
    /* Start the power thread */
    S.powerThread->API->Start(S.powerThread, "LTDevicePower", PowerThreadInit, PowerThreadExit);
    S.ownsPowerThread = true;
    
    LTLOG("thread.created", NULL);
    return S.powerThread;
}

static void LTDevicePowerImpl_BindToCurrentThread(void) {
    /* an internal thread cannot be swapped out safely: its exit proc would clear the new registration */
    if (S.powerThread) {
        LTLOG_YELLOWALERT("bind.thread.inuse", NULL);
        return;
    }
    /* LTCore binds the enter-sleep proc and the driver's sleep actions to the calling thread */
    S.powerThread     = LT_GetCore()->GetCurrentThreadObject();
    S.ownsPowerThread = false;

    if (!PowerThreadInit()) {
        LTLOG_YELLOWALERT("bind.thread.init.fail", NULL);
        S.powerThread = NULL;
        return;
    }

    LTLOG("bind.thread.done", NULL);
}

static void LTDevicePowerImpl_EnableSleepMode(LTTime idleDelay, LTTime minimumSleepDuration) {
    LTOThread *powerThread = GetPowerThread();
    if (!powerThread) {
        LTLOG_YELLOWALERT("enable.nothread", NULL);
        return;
    }
    LTTime *times = lt_malloc(2 * sizeof(LTTime));
    if (times) {
        *times = LTTime_IsZero(idleDelay) ? S.defaultSleepIdleTriggerTimeout : idleDelay;
        *(times + 1) = LTTime_IsZero(minimumSleepDuration) ? S.defaultMinimumSleepDuration : minimumSleepDuration;
        powerThread->API->QueueTaskProc(powerThread, &LTDevicePowerImpl_EnableSleepModeTaskProc, &LTDevicePowerImpl_EnableSleepModeTaskCompleteProc, times);
    }
}

static void LTDevicePowerImpl_DisableSleepMode(void) {
    LTOThread *powerThread = GetPowerThread();
    if (!powerThread) {
        LTLOG_YELLOWALERT("disable.nothread", NULL);
        return;
    }
    powerThread->API->QueueTaskProc(powerThread, &LTDevicePowerImpl_DisableSleepModeTaskProc, NULL, NULL);
}

static bool LTDevicePowerImpl_IsSleepModeEnabled(void) {
    return LTAtomic_Load(&S.atomicSleepModeEnabled) ? true : false;
}

static LTDevicePower_WakeupReason LTDevicePowerImpl_GetLastWakeupReason(void) {
    return S.lastWakeupReason;
}

static const char * LTDevicePowerImpl_WakeupReasonToString(LTDevicePower_WakeupReason reason) {
    switch (reason) {
        case kLTDevicePower_WakeupReason_Unknown:               return "Unknown";
        case kLTDevicePower_WakeupReason_SoftwareWakeupTimeout: return "SoftwareWakeupTimeout";
        case kLTDevicePower_WakeupReason_MotionEvent:           return "MotionEvent";
        case kLTDevicePower_WakeupReason_ButtonPress:           return "ButtonPress";
        case kLTDevicePower_WakeupReason_WiFiPacket:            return "WiFiPacket";
        case kLTDevicePower_WakeupReason_ChargeEvent:           return "ChargeEvent";
        case kLTDevicePower_WakeupReason_GPIOEvent:             return "GPIOEvent";
        case kLTDevicePower_WakeupReason_SleepSkipped:          return "SleepSkipped";
        default:                                                return "???";
    }
}

static void LTDevicePowerImpl_EnterShipmodeTaskProc(void *pClientData) {
    LT_UNUSED(pClientData);
    if (!S.driverPower) {
        LTLOG_YELLOWALERT("shipmode.nodriver", NULL);
        return;
    }
    LTLOG("shipmode.enter", NULL);

    /* in GoToSleep order, with interrupts enabled: a client may cut the power itself */
    u32 nIndex = S.driverPowerClients ? S.driverPowerClients->API->GetCount(S.driverPowerClients) : 0;
    while (nIndex--) {
        LTDriverPowerClient *driverPowerClient = (LTDriverPowerClient *)S.driverPowerClients->API->Get(S.driverPowerClients, nIndex, NULL);
        if (driverPowerClient->API->EnterShipmode) driverPowerClient->API->EnterShipmode(driverPowerClient);
    }

    if (S.driverPower->API->EnterShipmode) {
        LT_SIZE nMask = LT_GetCore()->Disable();
        S.driverPower->API->EnterShipmode();
        LT_GetCore()->Enable(nMask);
    }

    /* reaching here means shipmode entry failed; the clients are not restored, the caller recovers */
    LTLOG_REDALERT("shipmode.fail", NULL);
}

static void LTDevicePowerImpl_EnterShipmode(void) {
    LTOThread *powerThread = S.powerThread;
    /* from another thread this returns before shipmode is attempted, so that caller cannot see a failure */
    if (powerThread && powerThread != LT_GetCore()->GetCurrentThreadObject()) {
        powerThread->API->QueueTaskProc(powerThread, &LTDevicePowerImpl_EnterShipmodeTaskProc, NULL, NULL);
        return;
    }
    LTDevicePowerImpl_EnterShipmodeTaskProc(NULL);
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTDevicePower, LTDevicePowerImpl,
    BindToCurrentThread,
    EnableSleepMode,
    DisableSleepMode,
    IsSleepModeEnabled,
    GetLastWakeupReason,
    WakeupReasonToString,
    EnterShipmode
);

define_LTObjectLibrary(1, LTDevicePower_LibInit, LTDevicePower_LibFini);
