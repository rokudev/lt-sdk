/*******************************************************************************
 * Esp32DriverIrTxImpl.c
 *
 * Esp32 LT Driver Library for IR Transmission
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
/** @file Esp32DriverIrTxImpl.c Implementation of IR Transmit driver */

/*******************************************************************************
 * NOTE: This is an unimplemented skeleton driver.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/irtx/LTDeviceIrTx.h>

//DEFINE_LTLOG_SECTION("esp32.drv.ir");

static const ILTDriverIrTxDeviceUnit s_ILTDriverIrTxDeviceUnit;

static void Esp32DriverIrTxImpl_TransmitIRViaPWMStream(u32 irCarrierFreq, float dutyCycle, u16 dataFrame[], u16 dataLength) {
    LT_UNUSED(irCarrierFreq);
    LT_UNUSED(dutyCycle);
    LT_UNUSED(dataFrame);
    LT_UNUSED(dataLength);
}


static void Esp32DriverIrTxImpl_TransmitNECViaHWBlock(u32 necData) {
    LT_UNUSED(necData);
}

//
// Boilerplate functions needed for an LT driver
//

static bool Esp32DriverIrTxImpl_LibInit(void) {
    return true;
}

static void Esp32DriverIrTxImpl_LibFini(void) {
}

static u32 Esp32DriverIrTxImpl_GetNumDeviceUnits(void) {
    return 1;
}

static LTDeviceUnit Esp32DriverIrTxImpl_CreateDeviceUnitHandle(u32 nDeviceUnitIndex) {
    LT_UNUSED(nDeviceUnitIndex);
    return LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverIrTxDeviceUnit, 1);
}

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceIrTx, Esp32DriverIrTx);

// The actual device unit I-interface for this driver
define_LTLIBRARY_INTERFACE(ILTDriverIrTxDeviceUnit)
    .TransmitIRViaPWMStream = Esp32DriverIrTxImpl_TransmitIRViaPWMStream,
    .TransmitNECViaHWBlock = Esp32DriverIrTxImpl_TransmitNECViaHWBlock,
LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(Esp32DriverIrTx, (ILTDriverIrTxDeviceUnit))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  10-Sep-26   augustus   created
 */
