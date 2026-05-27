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
#define DEFAULT_WATCHDOG_SLEEP_SLOP_MS          (500)  /* upon entering sleep the watchdog will actually be set to max + slop */

DEFINE_LTLOG_SECTION("dev.power")

/*  ___________________________
 *  Object private data members
 */
typedef_LTObjectImpl(LTDevicePower, LTDevicePowerImpl) {
} LTOBJECT_API;

static struct Statics {
    LTDeviceWatchdog           *pDeviceWatchdog;
    LTDriverPower              *driverPower;
    LTOThread                  *powerThread;
    LTAtomic                    atomicSleepModeEnabled;
    LTArray                    *driverPowerClients;
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

    static char s_timeBuff[24];
    LT_GetCore()->FormatCanonicalTimeString(softwareSleepWakeupTime, s_timeBuff, sizeof(s_timeBuff), true);
    LTLOG_STOMP("sleep.enter", "softwareRequiredWakeup at %s", s_timeBuff);
    LT_GetCore()->ConsoleStompChar('\a');

    /* call all of the powerDriverClient->GoToSleep() functions in reverse order */
    u32 nCount = S.driverPowerClients->API->GetCount(S.driverPowerClients);
    u32 nIndex = nCount;

    /* disable interrupts while calling the power driver clients and while calling the power driver to enter sleep */
    LT_SIZE nMask = LT_GetCore()->Disable();

    while (nIndex--) {
        LTDriverPowerClient *driverPowerClient = (LTDriverPowerClient *)S.driverPowerClients->API->Get(S.driverPowerClients, nIndex, NULL);
        driverPowerClient->API->GoToSleep(driverPowerClient);
    }

    /* call the power driver to enter sleep, returning the duration spent in sleep and setting the wakeup reason */
    softwareSleepWakeupTime = S.driverPower->API->EnterSleep(softwareSleepWakeupTime, &S.lastWakeupReason);

    /* call all of the driverPowerClient->API->AwakenedFromSleep() functions in forward order */
    for (nIndex = 0; nIndex < nCount; nIndex++) {
        LTDriverPowerClient *driverPowerClient = (LTDriverPowerClient *)S.driverPowerClients->API->Get(S.driverPowerClients, nIndex, NULL);
        driverPowerClient->API->WakeFromSleep(driverPowerClient);
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
    return true;
}

static void PowerThreadExit(void) {
    LT_GetCore()->SetEnterSleepModeIdleDelayAndMinimumSleepDuration(LTTime_Zero(), LTTime_Zero());
    LT_GetCore()->SetEnterSleepModeProc(NULL, NULL);
    LTAtomic_Store(&S.atomicSleepModeEnabled, 0);
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
        lt_destroyobject(S.powerThread);
    }
    if (S.driverPowerClients) {
        u32 nCount = S.driverPowerClients->API->GetCount(S.driverPowerClients);
        for (u32 i = 0; i < nCount; i++) {
            lt_destroyobject((LTObject *)S.driverPowerClients->API->Get(S.driverPowerClients, i, NULL));
        }
        lt_destroyobject(S.driverPowerClients);
    }
    lt_closelibrary(S.pDeviceWatchdog);
    lt_destroyobject(S.driverPower);
}

static bool LTDevicePower_LibInit(void) {
    LTAtomic_Store(&S.atomicSleepModeEnabled, 0);
    S.lastWakeupReason = kLTDevicePower_WakeupReason_Unknown;

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
            if (driverPowerClient) S.driverPowerClients->API->Append(S.driverPowerClients, driverPowerClient);
        }
        ltstring_destroy(key);
        /* driver power clients in wakeup init invocation order. TODO add irled, ircutA, ircutB, boost, LED logic (Jira-847)*/
        S.driverPowerClients->API->Trim(S.driverPowerClients);
        /* defaultSleepIdleTriggerTimeout: too low = go to sleep quickly, too high = never goes to sleep depending on event cadence in LT. Fine tuning will be coved in (JIRA 825)*/
        S.defaultSleepIdleTriggerTimeout = LTTime_Milliseconds(deviceConfig->ReadInteger(section, "sleepIdleTriggerTimeoutMS"));
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
        if (NULL == (S.powerThread = lt_createobject(LTOThread))) {
            LTLOG_YELLOWALERT("f.threadcreate", "powerThread creation failed");
            break;
        }
        S.powerThread->API->SetStackSize(S.powerThread, 1024);
        S.powerThread->API->SetPriority(S.powerThread, 1);
        /* set the priority to 1, so that changes to sleep mode enablement will actuate over the default thread priority threads;
           this really isn't that important that it is ultra-responsive because sleep mode won't enter anyway until all threads are
           idle and any changes to sleep mode enablement will have already occurred.   The thread priority set here also won't
           affect the thread's priority during actuation of sleep mode because LTCore will have elevated it during actuation.
         */

        // open the watchdog, ok if it doesn't exist
        S.pDeviceWatchdog = lt_openlibrary(LTDeviceWatchdog);

        // start the power thread
        S.powerThread->API->Start(S.powerThread, "LTDevicePower", PowerThreadInit, PowerThreadExit);
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
static void LTDevicePowerImpl_EnableSleepMode(LTTime idleDelay, LTTime minimumSleepDuration) {
    LTTime *times = lt_malloc(2 * sizeof(LTTime));
    if (times) {
        *times = LTTime_IsZero(idleDelay) ? S.defaultSleepIdleTriggerTimeout : idleDelay;
        *(times + 1) = LTTime_IsZero(minimumSleepDuration) ? S.defaultMinimumSleepDuration : minimumSleepDuration;
        S.powerThread->API->QueueTaskProc(S.powerThread, &LTDevicePowerImpl_EnableSleepModeTaskProc, &LTDevicePowerImpl_EnableSleepModeTaskCompleteProc, times);
    }
}

static void LTDevicePowerImpl_DisableSleepMode(void) {
    S.powerThread->API->QueueTaskProc(S.powerThread, &LTDevicePowerImpl_DisableSleepModeTaskProc, NULL, NULL);
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
        default:                                                return "???";
    }
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTDevicePower, LTDevicePowerImpl,
    EnableSleepMode,
    DisableSleepMode,
    IsSleepModeEnabled,
    GetLastWakeupReason,
    WakeupReasonToString
);

define_LTObjectLibrary(1, LTDevicePower_LibInit, LTDevicePower_LibFini);
