/******************************************************************************
 * FanControl.c                                       LT FanControl application
 *
 * The state of California introduced "newly reformulated gas" around 1995.
 * This is more commonly known as Ethanol gas.  As of 2026, 98% of gasoline
 * sold in the United States is E10 gas (90% gasoline / 10% ethanol).
 * There are fierce debates on the Internet about the detrimental effects
 * of Ethanol gas on old cars, including whether or not the ethanol
 * reduces the boiling point of the fuel.  Irrespective of cause, I have
 * experienced the following effect on my 1969 Mercury Monterey:
 *
 * When the car is driven for 10 minutes or more, and then turned off, the
 * fan stops spinning and the engine heats up a bit before it cools down.
 * The gas in the fuel bowl of the carburetor starts to boil and overflows
 * into the intake manifold until the fuel bowl is empty, at which point
 * the fuel bowl fills up, the fuel boils and overflows into the intake manifold,
 * and repeats. This causes the engine to get flooded and all of the fuel
 * in the line to the tank to drain, resulting in about 30 or more required
 * cranks of the starter to get the car started again as the mechanical fuel
 * pump works overtime to bring fuel into the carburetor.
 *
 * To remedy this situation, I have created this FanControl LT application
 * which detects when the engine is on for a period of time, and when the
 * engine is turned off, off actuates a 12v centrifugal fan which draws
 * fresh air from the passenger side wheel well and blows it through
 * heater hose directly onto the exterior of the fuel bowl of the carburetor.
 * After the vehicle has been off for 10 minutes, power to the fan is cut
 * and then power to the device itself is cut, which results in 0 current draw
 * from the device once the fan has completed its actuation cycle.
 *
 * The schematic for the fan controller device may be found in the file
 * FanControllerSchematic.pdf.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Don Woodward,  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/gpio/LTDeviceGpio.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>
#include <lt/system/shell/LTSystemShell.h>

/*______________________
  FanControl #defines */
DEFINE_LTLOG_SECTION("fanControl");
#define PIN_NAME_POWER_LATCH            "power_latch"
#define PIN_NAME_FAN_POWER              "fan_power"
#define PIN_NAME_CAR_RUNNING            "car_running"
#define WATCHDOG_PET_INTERVAL           LTTime_Seconds(1)
#define WATCHDOG_EXPIRATION_TIME        LTTime_Seconds(3)
#define CAR_RUNNING_INPUT_DEBOUNCE_TIME LTTime_Milliseconds(250)
#define MIN_CAR_RUNNING_BEFORE_FAN_TIME LTTime_Seconds(60 * 5)
#define FAN_ACTUATION_DURATION          LTTime_Seconds(60 * 10)

/*______________________________
  FanControl static variables */
static LTOThread *s_pThread = NULL;

/*____________________
  FanControl object */
typedef_LTObject(FanControl, 1) {
} LTOBJECT_API;

/*______________________________________
  FanControlImpl object instance data */
typedef_LTObjectImpl(FanControl, FanControlImpl) {
    LTDeviceGpio           *pGpio;
    LTDeviceWatchdog       *pLibWatchdog;
    LTSystemShell          *pLibSystemShell;
    int                     pinPowerLatch;
    int                     pinFanPower;
    int                     pinCarRunning;
} LTOBJECT_API;

/*_____________________________________
_/ FanControlImpl utility functions  */
static bool FanControlImpl_ThreadInit(void) {
    /* This app runs and never shuts down, so we only care to have a ThreadInit and no ThreadExit.
       We also don't care if we leak the FanControl object since we never destroy it. */
    return lt_createobject(FanControl) ? true : false;
}

static void FanControlImpl_PowerOffOrReboot(void *clientData) {
    return;
    // the job of this function is to power off.  That should always work,
    // but in case it doesn't, use the watchdog to reboot the system
    FanControlImpl *fanControl = (FanControlImpl *)clientData;
    fanControl->pGpio->API->SetOutputValue(fanControl->pGpio, fanControl->pinFanPower, false);
    fanControl->pGpio->API->SetOutputValue(fanControl->pGpio, fanControl->pinPowerLatch, false);
    // give 200ms for the latch to settle
    s_pThread->API->Sleep(LTTime_Milliseconds(200));
    // should never get here; if we do, it means that between the time we entered this function and unlatched, the car transitioned to running again
    // use the watchdog to reboot the unit; it will sort itself out
    LTLOG_YELLOWALERT("power.off", "Unlatching power did not shut down the system.");
    fanControl->pLibWatchdog->DisableTimer();
    fanControl->pLibWatchdog->SetTimeout(LTTime_Milliseconds(200));
    fanControl->pLibWatchdog->EnableTimer();
    while (1) { }
}

/*_________________________________________________________
_/ FanControlImpl static timer functions and task procs  */
static void FanControlImpl_PetWatchdogTimer(void *clientData) {
    ((FanControlImpl *)clientData)->pLibWatchdog->ResetTimer();
}

static void FanControlImpl_CarIsRunningTimer(void *clientData) {
    /* the firing of this timer signifies the car has been running long enough to initiate Fan when car is no longer running */
    s_pThread->API->KillTimer(s_pThread, FanControlImpl_CarIsRunningTimer, clientData);
}

static void FanControlImpl_TurnFanOffTimer(void *clientData) {
    // when the fan has been running long enough, this timer expires and we'll power off
    FanControlImpl_PowerOffOrReboot((FanControlImpl *)clientData);
}

static void FanControlImpl_OnCarRunningDebouncedStatusChange(void *clientData) {
    FanControlImpl *fanControl = (FanControlImpl *)clientData;
    s_pThread->API->KillTimer(s_pThread, FanControlImpl_OnCarRunningDebouncedStatusChange, clientData);

    bool bCarIsRunning = fanControl->pGpio->API->GetInputValue(fanControl->pGpio, fanControl->pinCarRunning);
    if (bCarIsRunning) {
        LTLOG("status.change.running", "car is running");
        // car went from not running to running; turn the fan off and reset the CarOnTimer
        fanControl->pGpio->API->SetOutputValue(fanControl->pGpio, fanControl->pinFanPower, false);
        s_pThread->API->KillTimer(s_pThread, FanControlImpl_TurnFanOffTimer, fanControl);
        s_pThread->API->SetTimer(s_pThread, MIN_CAR_RUNNING_BEFORE_FAN_TIME, FanControlImpl_CarIsRunningTimer, NULL, fanControl);
    }
    else {
        // car is not running.  If we don't have the car is running timer running, we can turn on the fan, otherwise power off
        if (LTTime_IsZero(s_pThread->API->GetTimerExpirationKernelTime(s_pThread, FanControlImpl_CarIsRunningTimer, fanControl))) {
            LTLOG("status.change.notrunning", "running timer expired, engaging fan");
            // minimum car running time duration expired, turn on the fan
            fanControl->pGpio->API->SetOutputValue(fanControl->pGpio, fanControl->pinFanPower, true);
            s_pThread->API->SetTimer(s_pThread, FAN_ACTUATION_DURATION, FanControlImpl_TurnFanOffTimer, NULL, fanControl);
        }
        else {
            LTLOG("status.change.notrunning", "running timer unexpired, rebooting");
            FanControlImpl_PowerOffOrReboot(fanControl);
        }
    }
}

static void FanControlImpl_OnCarRunningStatusChange(void *clientData) {
    /* set/reset the debounce timer to execute status change once the input has been stable for the debounce time */
    s_pThread->API->SetTimer(s_pThread, CAR_RUNNING_INPUT_DEBOUNCE_TIME, FanControlImpl_OnCarRunningDebouncedStatusChange, NULL, clientData);
}

/*______________________
_/ FanControlImpl ISR */
static void FanControlImpl_PinCarRunningISR(void *clientData) {
    s_pThread->API->QueueTaskProcIfRequired(s_pThread, FanControlImpl_OnCarRunningStatusChange, NULL, clientData);
}

/*________________________________
_/ FanControlImpl constructors  */
static bool FanControlImpl_ConstructObject(FanControlImpl * fanControl) {

    // cache the current thread object; normally we don't do this because the object will
    // be destroyed if the thread terminates, but we never terminate the thread so it's ok
    s_pThread = LT_GetCore()->GetCurrentThreadObject();

    // create the gpio object
    fanControl->pGpio = lt_createdeviceobject(LTDeviceGpio);

    // first set the power pin high to latch power to myself so when car turns off, power stays on
    fanControl->pinPowerLatch = fanControl->pGpio->API->GetNamedPinValueFromName(fanControl->pGpio, PIN_NAME_POWER_LATCH);
    fanControl->pGpio->API->SetGpioModeFromIndex(fanControl->pGpio, fanControl->pinPowerLatch, kLTDeviceGpio_ModeType_Output);
    fanControl->pGpio->API->SetOutputValue(fanControl->pGpio, fanControl->pinPowerLatch, true);

    // now setup the watchdog so if we hang or crash we will reboot which will unlatch the power
    fanControl->pLibWatchdog = lt_openlibrary(LTDeviceWatchdog);
    fanControl->pLibWatchdog->SetTimeout(WATCHDOG_EXPIRATION_TIME);
    fanControl->pLibWatchdog->EnableTimer();
    s_pThread->API->SetTimer(s_pThread, WATCHDOG_PET_INTERVAL, FanControlImpl_PetWatchdogTimer, NULL, fanControl);

    // open the shell for debugging / logging purposes
    fanControl->pLibSystemShell = lt_openlibrary(LTSystemShell);

    // configure the fan power pin
    fanControl->pinFanPower = fanControl->pGpio->API->GetNamedPinValueFromName(fanControl->pGpio, PIN_NAME_FAN_POWER);
    fanControl->pGpio->API->SetGpioModeFromIndex(fanControl->pGpio, fanControl->pinFanPower, kLTDeviceGpio_ModeType_Output);
    fanControl->pGpio->API->SetOutputValue(fanControl->pGpio, fanControl->pinFanPower, false);

    // configure the car running pin
    fanControl->pinCarRunning = fanControl->pGpio->API->GetNamedPinValueFromName(fanControl->pGpio, PIN_NAME_CAR_RUNNING);
    fanControl->pGpio->API->SetGpioModeFromIndex(fanControl->pGpio, fanControl->pinCarRunning, kLTDeviceGpio_ModeType_Input);
    fanControl->pGpio->API->SetISR(fanControl->pGpio, fanControl->pinCarRunning, FanControlImpl_PinCarRunningISR, kLTDeviceGPIO_TriggerType_BothEdges, fanControl);

    if (! fanControl->pGpio->API->GetInputValue(fanControl->pGpio, fanControl->pinCarRunning)) {
        // we've just rebooted and the car is not running.  Should never happen
        LTLOG_YELLOWALERT("boot.car.not.running", "System booted with car not running");
        FanControlImpl_PowerOffOrReboot(fanControl);
    }

    // set the car is running timer
    s_pThread->API->SetTimer(s_pThread, MIN_CAR_RUNNING_BEFORE_FAN_TIME, FanControlImpl_CarIsRunningTimer, NULL, fanControl);

    return true;
}

static void FanControlImpl_DestructObject(FanControlImpl * fanControl) {
    /* this will never get called since we never destroy the fanControl object */
    LT_UNUSED(fanControl);
}

/*_____________________________________________________________________________
  make FanControl private - it can only be lt_createobject'd from this library */
define_LTObjectImplPrivate(FanControl, FanControlImpl,
);

/*_____________________
  FanControlApp_Main */
static int FanControlApp_Main(int argc, const char **argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    LTOThread *thread = lt_createobject(LTOThread);
    thread->API->Start(thread, "FanControl", FanControlImpl_ThreadInit, NULL);
    return 0;
}

define_LTLIBRARY_APPLICATION(FanControlApp, 1, 0); /* (appName, version, stackSize 0=default) */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-Sep-26   dwoodward    created
 */
