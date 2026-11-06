/*******************************************************************************
 * Esp32DriverAnalogMicImpl.c
 *
 * Esp32 LT Driver Library for analog microphone functions
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/*******************************************************************************
 * NOTE: This is an unimplemented skeleton driver.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/analogmic/LTDeviceAnalogMic.h>

//DEFINE_LTLOG_SECTION("esp32.drv.amic");

static bool Esp32DriverAnalogMicImpl_LibInit(void) {
    return true;
}

static void Esp32DriverAnalogMicImpl_LibFini(void) {
}

static u32 Esp32DriverAnalogMicImpl_GetNumDeviceUnits(void) {
    return 0;
}

static LTDeviceUnit Esp32DriverAnalogMicImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LT_UNUSED(nDeviceUnitNumber);
    return 0;
}

static bool Esp32DriverAnalogMic_StartCap(LTDeviceAMicAudioCallback *fp) {
    LT_UNUSED(fp);
    return true;
}

static bool Esp32DriverAnalogMic_StopCap(void) {
    return true;
}

static void Esp32DriverAnalogMic_SetGain(int gain_db) {
    LT_UNUSED(gain_db);
}

static int Esp32DriverAnalogMic_GetGain(void) {
    return 0;
}

static bool Esp32DriverAnalogMic_SetBuffSize(int sz, s16* buf) {
    LT_UNUSED(sz);
    LT_UNUSED(buf);
    return true;
}

define_LTLIBRARY_INTERFACE(ILTDriverAnalogMic) {
    .StartCap    = Esp32DriverAnalogMic_StartCap,
    .StopCap     = Esp32DriverAnalogMic_StopCap,
    .SetGain     = Esp32DriverAnalogMic_SetGain,
    .GetGain     = Esp32DriverAnalogMic_GetGain,
    .SetBuffSize = Esp32DriverAnalogMic_SetBuffSize
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(Esp32DriverAnalogMic, (ILTDriverAnalogMic))

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceAnalogMic, Esp32DriverAnalogMic);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  10-Sep-26   augustus   created
 */
