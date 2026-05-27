/******************************************************************************
 * lt/source/lt/device/mipicsi/LTDeviceMipiCsi.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/mipicsi/LTDeviceMipiCsi.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("lt.dev.mipicsi");

/******************************************************************************
 * Access to the LTDriverMipiCsi library, through which the Device accesses
 * MIPI-CSI-related functions:
 */
static LTDriverLibrary  * s_pDriver         = NULL;
static ILTDriverMipiCsi * s_ILTDriver       = NULL;

/******************************************************************************
 * Number of Device Units available according to the Driver:
 */
static u32               s_nNumDeviceUnits = 0;

/******************************************************************************
 * Unload the Driver library:
 */
static void ShutDownDriver(void) {
    LT_GetCore()->CloseLibrary((LTLibrary *)s_pDriver);
    s_pDriver = NULL;
}

/******************************************************************************
 * Library startup and shutdown.
 * Attempt to open the Driver Library.  If successful, get the number of
 * available Device Units from the Driver.
 * If no drivers available, close the Driver Library and return false.
 * Otherwise, the Device is open successfully - return true.
 */
static bool LTDeviceMipiCsiImpl_LibInit(void) {
    s_pDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceMipiCsi", 0);
    if (s_pDriver) {
        s_ILTDriver = lt_getlibraryinterface(ILTDriverMipiCsi, s_pDriver);
        if (s_ILTDriver) {
            s_nNumDeviceUnits = s_pDriver->GetNumDeviceUnits();
            if (!s_nNumDeviceUnits) { ShutDownDriver(); }
        }
    }
    if (!s_nNumDeviceUnits) {
        LTLOG("lib.init.fail", "mipi-csi init failed");
        return false;
    }
    return true;
}

static void LTDeviceMipiCsiImpl_LibFini(void) {
    if (s_nNumDeviceUnits) { ShutDownDriver(); }
}

/*******************************************************************************
 * Device Unit access and conversion:
 */

static u32 LTDeviceMipiCsi_GetBusIndexFromName(const char *busName) {
    return s_ILTDriver->GetBusIndexFromName(busName);
}

static u32 LTDeviceMipiCsiImpl_GetNumDeviceUnits(void) {
    return s_nNumDeviceUnits;
}

static LTDeviceUnit LTDeviceMipiCsiImpl_CreateDeviceUnitHandle(
    u32 nDeviceUnitNumber) {

    LTDeviceUnit hDeviceUnit = 0;
    if (s_nNumDeviceUnits) {
        if (nDeviceUnitNumber < s_nNumDeviceUnits) {
            hDeviceUnit =
                s_pDriver->CreateDeviceUnitHandle(nDeviceUnitNumber);
        }
        nDeviceUnitNumber -= s_nNumDeviceUnits;
    }
    return hDeviceUnit;
}

static void LTDeviceMipiCsi_EnableExtclk(LTDeviceUnit hUnit, bool enable) {
    s_ILTDriver->EnableExtclk(hUnit, enable);
}

static void LTDeviceMipiCsi_EnableOutput(LTDeviceUnit hUnit, bool enable) {
    s_ILTDriver->EnableOutput(hUnit, enable);
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceMipiCsi)
    .GetBusIndexFromName    = LTDeviceMipiCsi_GetBusIndexFromName,
    .EnableExtclk           = LTDeviceMipiCsi_EnableExtclk,
    .EnableOutput           = LTDeviceMipiCsi_EnableOutput,
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  17-Jan-24   trajan      created
 */
