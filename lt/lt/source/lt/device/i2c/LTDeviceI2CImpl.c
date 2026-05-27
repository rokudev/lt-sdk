/******************************************************************************
 * lt/source/lt/device/i2c/LTDeviceI2CImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/i2c/LTDeviceI2C.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("lt.dev.i2c");

/******************************************************************************
 * Access to the LTDriverI2C library, through which the Device accesses
 * I2C-related functions:
 */
static LTDriverLibrary * s_pDriver_hw         = NULL;
static LTDriverLibrary * s_pDriver_bb         = NULL;
static ILTDriverI2C    * s_ILTDriver_hw       = NULL;
static ILTDriverI2C    * s_ILTDriver_bb       = NULL;

/******************************************************************************
 * Number of Device Units available according to the Driver:
 */
static u32               s_nNumDeviceUnits_hw = 0;
static u32               s_nNumDeviceUnits_bb = 0;

/******************************************************************************
 * Unload the Driver library:
 */
static void ShutDownDriver_hw(void) {
    LT_GetCore()->CloseLibrary((LTLibrary *)s_pDriver_hw);
    s_pDriver_hw = NULL;
}

static void ShutDownDriver_bb(void) {
    LT_GetCore()->CloseLibrary((LTLibrary *)s_pDriver_bb);
    s_pDriver_bb = NULL;
}

/******************************************************************************
 * Library startup and shutdown.
 * Attempt to open the Driver Library.  If successful, get the number of
 * available Device Units from the Driver.
 * If no drivers available, close the Driver Library and return false.
 * Otherwise, the Device is open successfully - return true.
 */
static bool LTDeviceI2CImpl_LibInit(void) {
    s_pDriver_hw = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceI2C", 0);
    if (s_pDriver_hw) {
        s_ILTDriver_hw = lt_getlibraryinterface(ILTDriverI2C, s_pDriver_hw);
        if (s_ILTDriver_hw) {
            s_nNumDeviceUnits_hw = s_pDriver_hw->GetNumDeviceUnits();
            if (!s_nNumDeviceUnits_hw) { ShutDownDriver_hw(); }
        }
    }
    s_pDriver_bb = (LTDriverLibrary *)
        LT_GetCore()->OpenLibrary("CommonDriverI2CBitbang");
    if (s_pDriver_bb) {
        s_ILTDriver_bb = lt_getlibraryinterface(ILTDriverI2C, s_pDriver_bb);
        if (s_ILTDriver_bb) {
            s_nNumDeviceUnits_bb = s_pDriver_bb->GetNumDeviceUnits();
            if (!s_nNumDeviceUnits_bb) { ShutDownDriver_bb(); }
        }
    }
    if (!s_nNumDeviceUnits_hw && !s_nNumDeviceUnits_bb) {
        LTLOG("lib.init.fail", "i2c init failed");
        return false;
    }
    return true;
}

static void LTDeviceI2CImpl_LibFini(void) {
    if (s_nNumDeviceUnits_hw) { ShutDownDriver_hw(); }
    if (s_nNumDeviceUnits_bb) { ShutDownDriver_bb(); }
}

/*******************************************************************************
 * Device Unit access and conversion:
 */

static u32 LTDeviceI2C_GetBusIndexFromName(const char *busName) {
    /* First i2c-hw buses will get count , then i2c-bigbang */
    u32 index = s_ILTDriver_hw->GetBusIndexFromName(busName);
    if (index == LT_U32_MAX) {
        index = s_ILTDriver_bb->GetBusIndexFromName(busName);
        return (index == LT_U32_MAX) ? LT_U32_MAX : index + s_nNumDeviceUnits_hw; /* bb will have an offset of hw */
    }
    return index;
}

static u32 LTDeviceI2CImpl_GetNumDeviceUnits(void) {
    return s_nNumDeviceUnits_hw + s_nNumDeviceUnits_bb;
}

static LTDeviceUnit LTDeviceI2CImpl_CreateDeviceUnitHandle(
    u32 nDeviceUnitNumber) {

    LTDeviceUnit hDeviceUnit = 0;
    if (s_nNumDeviceUnits_hw) {
        if (nDeviceUnitNumber < s_nNumDeviceUnits_hw) {
            hDeviceUnit =
                s_pDriver_hw->CreateDeviceUnitHandle(nDeviceUnitNumber);
        }
        nDeviceUnitNumber -= s_nNumDeviceUnits_hw;
    }
    if (s_nNumDeviceUnits_bb && nDeviceUnitNumber < s_nNumDeviceUnits_bb) {
        hDeviceUnit = s_pDriver_bb->CreateDeviceUnitHandle(nDeviceUnitNumber);
    }
    return hDeviceUnit;
}

static ILTDriverI2C* UnitToDriverInterface(LTDeviceUnit unit) {
    ILTDriverI2C* intf = (ILTDriverI2C*)
        (LT_GetCore()->GetHandleInterface(unit));
    if (intf == s_ILTDriver_hw || intf == s_ILTDriver_bb) return intf;
    return NULL;
}

static bool LTDeviceI2CImpl_GetDeviceCapabilities(LTDeviceUnit unit, LTDeviceI2C_Capabilities* pCaps) {
    ILTDriverI2C * Idrv = UnitToDriverInterface(unit);
    return ((Idrv) ? (Idrv->GetDeviceCapabilities(unit, pCaps)) : (false));
}
static bool LTDeviceI2CImpl_GetDeviceConfiguration(LTDeviceUnit unit, LTDeviceI2C_Configuration* pI2CConfig) {
    ILTDriverI2C * Idrv = UnitToDriverInterface(unit);
    return ((Idrv) ? (Idrv->GetDeviceConfiguration(unit, pI2CConfig)) : (false));
}

static bool LTDeviceI2CImpl_SetDeviceConfiguration(LTDeviceUnit unit, const LTDeviceI2C_Configuration* pI2CConfig) {
    ILTDriverI2C * Idrv = UnitToDriverInterface(unit);
    return ((Idrv) ? (Idrv->SetDeviceConfiguration(unit, pI2CConfig)) : (false));
}

static void LTDeviceI2CImpl_SetTransferTimeout(LTDeviceUnit unit, LTTime timeout) {
    ILTDriverI2C * Idrv = UnitToDriverInterface(unit);
    return ((Idrv) ? (Idrv->SetTransferTimeout(unit, timeout)) : (false));
}

static bool LTDeviceI2CImpl_I2CMasterTransfer(LTDeviceUnit unit, u8 addr,
    void * rx_buffer, u32 rx_len, const void * tx_buffer, u32 tx_len,
    bool issue_start, bool issue_stop,
    LTI2C_I2CMasterTransferStatusCallback *pCallback, void *pClientData) {
    ILTDriverI2C * Idrv = UnitToDriverInterface(unit);
    return ((Idrv) ? (Idrv->I2CMasterTransfer(unit, addr, rx_buffer, rx_len, tx_buffer, tx_len, issue_start, issue_stop, pCallback, pClientData)) : (false));
}

static bool LTDeviceI2CImpl_Reset(LTDeviceUnit unit) {
    ILTDriverI2C * Idrv = UnitToDriverInterface(unit);
    return ((Idrv) ? (Idrv->Reset(unit)) : (false));
}

static bool LTDeviceI2CImpl_ProbeAddress(LTDeviceUnit unit, u8 addr) {
    ILTDriverI2C * Idrv = UnitToDriverInterface(unit);
    return ((Idrv) ? (Idrv->ProbeAddress(unit,addr)) : (false));
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceI2C)
    .GetBusIndexFromName    = LTDeviceI2C_GetBusIndexFromName,
    .GetDeviceCapabilities  = LTDeviceI2CImpl_GetDeviceCapabilities,
    .GetDeviceConfiguration = LTDeviceI2CImpl_GetDeviceConfiguration,
    .SetDeviceConfiguration = LTDeviceI2CImpl_SetDeviceConfiguration,
    .SetTransferTimeout     = LTDeviceI2CImpl_SetTransferTimeout,
    .I2CMasterTransfer      = LTDeviceI2CImpl_I2CMasterTransfer,
    .Reset                  = LTDeviceI2CImpl_Reset,
    .ProbeAddress           = LTDeviceI2CImpl_ProbeAddress,
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  22-Jun-21   titus       created
 *  29-Sep-21   commodus    removed arrays of handles
 *  03-Apr-23   augustus    load driver from LTDeviceConfig specification
 */
