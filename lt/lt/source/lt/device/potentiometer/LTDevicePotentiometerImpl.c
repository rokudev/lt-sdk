/*******************************************************************************
 * lt/source/lt/device/Potentiometer/LTDevicePotentiometerImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>

#include <lt/device/potentiometer/LTDevicePotentiometer.h>
#include <lt/device/config/LTDeviceConfig.h>

//DEFINE_LTLOG_SECTION("lt.dev.potentiometer");

/*******************************************************************************
 * libraries
*******************************************************************************/

/*******************************************************************************
 * static variables
*******************************************************************************/
static LTDriverLibrary * s_pDriverPotentiometer = NULL;

/*******************************************************************************
 *
 * helper functions
 *
*******************************************************************************/

/*******************************************************************************
 *
 * device interface implementation
 *
*******************************************************************************/

/*******************************************************************************
 * Initialize the device
*******************************************************************************/
static bool LTDevicePotentiometerImpl_LibInit(void) {
    return (s_pDriverPotentiometer = LTDeviceConfig_OpenDriverLibForDevice("LTDevicePotentiometer", 0));
}

/*******************************************************************************
 * Finalize the device
*******************************************************************************/
static void LTDevicePotentiometerImpl_LibFini(void) {
    lt_closelibrary(s_pDriverPotentiometer);
    s_pDriverPotentiometer = NULL;
}

static u32 LTDevicePotentiometerImpl_GetNumDeviceUnits(void) {
    return s_pDriverPotentiometer->GetNumDeviceUnits();
}

static LTDeviceUnit LTDevicePotentiometerImpl_CreateDeviceUnitHandle(u32 nDeviceNum) {
    return s_pDriverPotentiometer->CreateDeviceUnitHandle(nDeviceNum);
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDevicePotentiometer) LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Jan-22   vitellius   created
 *  03-Apr-23   augustus    load driver from LTDeviceConfig specification
 */
