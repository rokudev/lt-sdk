/*******************************************************************************
 * Esp32DriverPwmImpl.c
 *
 * Esp32 LT Driver Library for PWM functions
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
/** @file Esp32DriverPwmImpl.c Implementation of PWM driver */

/*******************************************************************************
 * NOTE: This is an unimplemented skeleton driver.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/pwm/LTDevicePwm.h>

//DEFINE_LTLOG_SECTION("esp32.drv.pwm");

static const ILTDriverPwmDeviceUnit s_ILTDriverPwmDeviceUnit;

static bool Esp32DriverPwmImpl_InitPwmPin(u8 pin, bool activeHigh, u32 frequency, u16 dutyCycle, bool start) {
    LT_UNUSED(pin);
    LT_UNUSED(activeHigh);
    LT_UNUSED(frequency);
    LT_UNUSED(dutyCycle);
    LT_UNUSED(start);

    return true;
}

static bool Esp32DriverPwmImpl_Start(u8 pin) {
    LT_UNUSED(pin);
    return true;
}

static bool Esp32DriverPwmImpl_Stop(u8 pin) {
    LT_UNUSED(pin);
    return true;
}

static bool Esp32DriverPwmImpl_SetDutyCycle(u8 pin, u16 dutyCycle) {
    LT_UNUSED(pin);
    LT_UNUSED(dutyCycle);
    return true;
}

static bool Esp32DriverPwmImpl_GetDutyCycle(u8 pin, u16 *pDutyCycle) {
    LT_UNUSED(pin);
    LT_UNUSED(pDutyCycle);
    return true;
}

static bool Esp32DriverPwmImpl_SetClockOutputPin(u8 pin, bool enable, LTDevicePwm_ClockType clockType) {
    LT_UNUSED(pin);
    LT_UNUSED(enable);
    LT_UNUSED(clockType);
    return true;
}

//
// Boilerplate functions needed for an LT driver
//

 static bool Esp32DriverPwmImpl_LibInit(void) {
    return true;
}

 static void Esp32DriverPwmImpl_LibFini(void) {
}

 static u32 Esp32DriverPwmImpl_GetNumDeviceUnits(void) {
    return 1;
}

 static LTDeviceUnit Esp32DriverPwmImpl_CreateDeviceUnitHandle(u32 nDeviceUnitIndex) {
    LT_UNUSED(nDeviceUnitIndex);

    return LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverPwmDeviceUnit, 1);
}

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDevicePwm, Esp32DriverPwm);

// The actual device unit I-interface for this driver
define_LTLIBRARY_INTERFACE(ILTDriverPwmDeviceUnit)
    .InitPwmPin         = Esp32DriverPwmImpl_InitPwmPin,
    .Start              = Esp32DriverPwmImpl_Start,
    .Stop               = Esp32DriverPwmImpl_Stop,
    .SetDutyCycle       = Esp32DriverPwmImpl_SetDutyCycle,
    .GetDutyCycle       = Esp32DriverPwmImpl_GetDutyCycle,
    .SetClockOutputPin  = Esp32DriverPwmImpl_SetClockOutputPin,
LTLIBRARY_DEFINITION;
LTLIBRARY_EXPORT_INTERFACES(Esp32DriverPwm, (ILTDriverPwmDeviceUnit))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  10-Sep-26   augustus   created
 */
