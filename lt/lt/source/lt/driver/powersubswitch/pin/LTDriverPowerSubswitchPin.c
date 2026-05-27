/*******************************************************************************
* lt/source/lt/driver/powersubswitch/pin/LTDriverPowerSubswitchPin.c
*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
*******************************************************************************/

#include <lt/driver/powersubswitch/LTDriverPowerSubswitch.h>
#include <lt/device/pins/LTDevicePins.h>
#include <lt/device/config/LTDeviceKonfig.h>
#include <lt/core/LTCore.h>

/* _____________
/ #defines  */
#define MINIMUM_PIN_TWIDDLE_INTERVAL    LTTime_Milliseconds(10)
#define DEVICE_CLASS_KEY                "LTDevicePowerSubswitch"
#define CONFIG_PIN_NAME_KEY             "pin"

DEFINE_LTLOG_SECTION("subswitch.pin");
#define USE_DLOG 1
#if USE_DLOG
#define DLOG LTLOG
#else
#define DLOG LTLOG_NULL
#endif

/* _______________________
/ object impl struct  */
typedef_LTObjectImpl(LTDriverPowerSubswitch, LTDriverPowerSubswitchPin) {
    const char                      *unitName;
    LTDevicePins                    *devicePins;
    ILTDriverPins_BidirectionalBank *pinBank;
    LTDeviceUnit                     hPin;
    LTTime                           lastPinTwiddleTime;
} LTOBJECT_API;

/* ________________________
/ object constructors  */
static bool LTDriverPowerSubswitchPin_ConstructObject(LTDriverPowerSubswitchPin *subswitch) {
    if (NULL == (subswitch->devicePins = lt_openlibrary(LTDevicePins))) return false;
    return true;
}

static void LTDriverPowerSubswitchPin_DestructObject(LTDriverPowerSubswitchPin *subswitch) {
    lt_destroyhandle(subswitch->hPin);
    lt_closelibrary(subswitch->devicePins);
}

/* _____________________________
/ private helper functions  */
static void EnforceTwiddleDelayAndTwiddlePin(LTDriverPowerSubswitchPin *subswitch, u32 pinValue) {
    if (! LTTime_IsZero(subswitch->lastPinTwiddleTime)) {
        LTTime twiddleOkTime = LTTime_Add(subswitch->lastPinTwiddleTime, MINIMUM_PIN_TWIDDLE_INTERVAL);
        LTTime kernelTime = LT_GetCore()->GetKernelTime();
        if (LTTime_IsGreaterThan(twiddleOkTime, kernelTime)) {
            lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Subtract(twiddleOkTime, kernelTime));
        }
    }
    subswitch->pinBank->Set(subswitch->hPin, pinValue);
    subswitch->lastPinTwiddleTime = LT_GetCore()->GetKernelTime();
    DLOG(pinValue ? "power.on" : "power.off", "%s", subswitch->unitName);
}

/* ______________________________
/ public api implementation  */
static void LTDriverPowerSubswitchPin_SetUnitName(LTDriverPowerSubswitchPin *subswitch, const char *unitName) {
    u32 nUnitIndex = 0;
    LTDeviceKonfig *config = NULL;
    do {
        if (!(config = lt_createobject(LTDeviceKonfig))) break;
        u32 section = config->API->GetDeviceUnitConfigSectionByName(config, DEVICE_CLASS_KEY, unitName);
        const char *pinName = config->API->ReadString(config, section, CONFIG_PIN_NAME_KEY);
        lt_destroyobject(config);

        if (!subswitch->devicePins->GetUnitNumberFromBankName(pinName, &nUnitIndex)) break;
        if (LTHANDLE_INVALID == (subswitch->hPin = subswitch->devicePins->CreateDeviceUnitHandle(nUnitIndex))) break;
        if (NULL == (subswitch->pinBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, subswitch->hPin))) break;

        subswitch->unitName = unitName;
        return;
    } while (false);

    lt_destroyhandle(subswitch->hPin);
    lt_closelibrary(subswitch->devicePins);
}

static const char * LTDriverPowerSubswitchPin_GetUnitName(LTDriverPowerSubswitchPin *subswitch) {
    return subswitch->unitName;
}

static void LTDriverPowerSubswitchPin_PowerOn(LTDriverPowerSubswitchPin *subswitch) {
    EnforceTwiddleDelayAndTwiddlePin(subswitch, 1);
}

static void LTDriverPowerSubswitchPin_PowerOff(LTDriverPowerSubswitchPin *subswitch) {
    EnforceTwiddleDelayAndTwiddlePin(subswitch, 0);
}

static bool LTDriverPowerSubswitchPin_IsDevicePoweredOn(LTDriverPowerSubswitchPin *subswitch) {
    return subswitch->pinBank->Read(subswitch->hPin) & 1;
}

define_LTObjectImplPublic(LTDriverPowerSubswitch, LTDriverPowerSubswitchPin,
    SetUnitName,
    GetUnitName,
    PowerOn,
    PowerOff,
    IsDevicePoweredOn);

define_LTObjectLibrary(1, NULL, NULL);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-Mar-25   augustus    created
 *  16-Apr-25   aurelian    moved to LTDriverPowerSubswitchPin
 */
