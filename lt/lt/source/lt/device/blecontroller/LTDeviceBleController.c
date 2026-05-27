/*******************************************************************************
 *
 * LTDeviceBleController.c: BLE Controller Device
 * ----------------------------------------------
 *
 * Provides the primary functions for link-layer BLE controller. All
 * platform-dependent features and functions are hidden below this layer and
 * shall not be called or otherwise referenced directly from higher levels.
 * Application code must pass through the this layer to properly sequence
 * states, minimize locking, and avoid conflicts within the driver.
 *
 * See LTDeviceBleController.h for interface documentation.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/blecontroller/LTDeviceBleController.h>
#include <lt/device/config/LTDeviceConfig.h>

// DEFINE_LTLOG_SECTION("ble.controller");

/** Standard LT Interfaces ****************************************************/

static LTDriverLibrary *s_driver = NULL;

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTDeviceBleControllerImpl_LibFini(void) {
    lt_closelibrary(s_driver);
    s_driver = NULL;
}

static bool LTDeviceBleControllerImpl_LibInit(void) {
    return (s_driver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceBleController", 0));
}

static u32 LTDeviceBleControllerImpl_GetNumDeviceUnits(void) {
    return s_driver->GetNumDeviceUnits();
}

static LTDeviceUnit LTDeviceBleControllerImpl_CreateDeviceUnitHandle(u32 deviceUnitNumber) {
    return s_driver->CreateDeviceUnitHandle(deviceUnitNumber);
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceBleController) {
} LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  20-Dec-22   gallienus   created
 *  03-Apr-23   augustus    load driver from LTDeviceConfig specification
 */
