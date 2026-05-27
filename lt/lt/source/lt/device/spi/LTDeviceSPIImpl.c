/*******************************************************************************
 * lt/source/lt/device/pins/LTDeviceSPIImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/spi/LTDeviceSPI.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("lt.dev.spi");
/*******************************************************************************
 * Access to the LTDriverSPI library, through which the Device accesses
 * SPI-related functions:                                                */
static LTDriverLibrary * s_pDriver_hw         = NULL;
static LTDriverLibrary * s_pDriver_bb         = NULL;
static ILTDriverSPI    * s_ILTDriver_hw       = NULL;
static ILTDriverSPI    * s_ILTDriver_bb       = NULL;
/*******************************************************************************
 * Number of Device Units available according to the Driver:                  */
static u32               s_nNumDeviceUnits_hw = 0;
static u32               s_nNumDeviceUnits_bb = 0;
static LTDeviceUnit    * s_Units_hw           = NULL;
static LTDeviceUnit    * s_Units_bb           = NULL;
/*******************************************************************************
 * Unload the Driver library:                                                 */
static void ShutDownDriver_hw(void) {
    if (s_Units_hw) lt_free(s_Units_hw);
    LT_GetCore()->CloseLibrary((LTLibrary *)s_pDriver_hw);
    s_pDriver_hw = NULL;
}
static void ShutDownDriver_bb(void) {
    if (s_Units_bb) lt_free(s_Units_bb);
    LT_GetCore()->CloseLibrary((LTLibrary *)s_pDriver_bb);
    s_pDriver_bb = NULL;
}
/***********************************************************************************************************************
 * Library startup and shutdown.
 * Attempt to open the Driver Library.  If successful, get the number of available Device Units from the Driver.
 * If no drivers available, close the Driver Library and return false.
 * Otherwise, the Device is open successfully - return true.                                                          */
static bool LTDeviceSPIImpl_LibInit(void) {
    s_pDriver_hw = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceSPI", 0);
    if (s_pDriver_hw) {
        s_ILTDriver_hw = lt_getlibraryinterface(ILTDriverSPI, s_pDriver_hw);
        if (s_ILTDriver_hw) {
            s_nNumDeviceUnits_hw = s_pDriver_hw->GetNumDeviceUnits();
            if (!s_nNumDeviceUnits_hw) { ShutDownDriver_hw(); }
        }
    }
    s_pDriver_bb = (LTDriverLibrary *)LT_GetCore()->OpenLibrary("LTDriverSPIBitbang");
    if (s_pDriver_bb) {
        s_ILTDriver_bb = lt_getlibraryinterface(ILTDriverSPI, s_pDriver_bb);
        if (s_ILTDriver_bb) {
            s_nNumDeviceUnits_bb = s_pDriver_bb->GetNumDeviceUnits();
            if (!s_nNumDeviceUnits_bb) { ShutDownDriver_bb(); }
        }
    }
    if (!s_nNumDeviceUnits_hw && !s_nNumDeviceUnits_bb) {
        LTLOG("lib.init.fail", "spi init failed");
        return false;
    }
    if (s_nNumDeviceUnits_hw) {
        s_Units_hw = lt_malloc(sizeof(LTDeviceUnit)*s_nNumDeviceUnits_hw);
    }
    if (s_nNumDeviceUnits_bb) {
        s_Units_bb = lt_malloc(sizeof(LTDeviceUnit)*s_nNumDeviceUnits_bb);
    }
    return true;
}

static void LTDeviceSPIImpl_LibFini(void) {
    if (s_nNumDeviceUnits_hw) { ShutDownDriver_hw(); }
    if (s_nNumDeviceUnits_bb) { ShutDownDriver_bb(); }
}

/***********************************************************************************************************************
 * Device Unit access and conversion:                                                                                 */
static u32 LTDeviceSPI_GetBusIndexFromName(const char *busName) {
    /* First spi-hw buses will get count , then spi-bitbang */
    if (s_ILTDriver_hw) {
        u32 index = s_ILTDriver_hw->GetBusIndexFromName(busName);
        if (index != LT_U32_MAX) {
            return index;
        }
    }
    if (s_ILTDriver_bb) {
        u32 index = s_ILTDriver_bb->GetBusIndexFromName(busName);
        return (index == LT_U32_MAX) ? LT_U32_MAX : index + s_nNumDeviceUnits_hw; /* bb will have an offset of hw */
    }
    return LT_U32_MAX;
}

static u32 LTDeviceSPIImpl_GetNumDeviceUnits(void) { return s_nNumDeviceUnits_hw + s_nNumDeviceUnits_bb; }

static LTDeviceUnit LTDeviceSPIImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LTDeviceUnit hDeviceUnit = 0;
    if (s_nNumDeviceUnits_hw) {
        if (nDeviceUnitNumber < s_nNumDeviceUnits_hw) {
            hDeviceUnit = s_pDriver_hw->CreateDeviceUnitHandle(nDeviceUnitNumber);
            s_Units_hw[nDeviceUnitNumber] = hDeviceUnit;
        }
        nDeviceUnitNumber -= s_nNumDeviceUnits_hw;
    }
    if (s_nNumDeviceUnits_bb && nDeviceUnitNumber < s_nNumDeviceUnits_bb) {
        hDeviceUnit = s_pDriver_bb->CreateDeviceUnitHandle(nDeviceUnitNumber);
        s_Units_bb[nDeviceUnitNumber] = hDeviceUnit;
    }
    return hDeviceUnit;
}
static ILTDriverSPI* UnitToDriverInterface(LTDeviceUnit unit) {
    ILTDriverSPI *intf = (ILTDriverSPI *)(LT_GetCore()->GetHandleInterface(unit));
    if (intf == s_ILTDriver_hw || intf == s_ILTDriver_bb) return intf;
    return NULL;
}
static bool LTDeviceSPIImpl_GetDeviceCapabilities(LTDeviceUnit unit, LTDeviceSPI_Capabilities* pCaps) {
    ILTDriverSPI *Idrv = UnitToDriverInterface(unit);
    return ((Idrv)?(Idrv->GetDeviceCapabilities(unit, pCaps)):(false));
}
static bool LTDeviceSPIImpl_GetDeviceConfiguration(LTDeviceUnit unit, LTDeviceSPI_Configuration* pSPIConfig) {
    ILTDriverSPI *Idrv = UnitToDriverInterface(unit);
    return ((Idrv)?(Idrv->GetDeviceConfiguration(unit, pSPIConfig)):(false));
}

static bool LTDeviceSPIImpl_SetDeviceConfiguration(LTDeviceUnit unit, const LTDeviceSPI_Configuration* pSPIConfig) {
    ILTDriverSPI *Idrv = UnitToDriverInterface(unit);
    return ((Idrv)?(Idrv->SetDeviceConfiguration(unit, pSPIConfig)):(false));
}

static bool LTDeviceSPIImpl_SPIMasterTransfer(LTDeviceUnit unit, u8 *rx_buffer, u8 *tx_buffer, u32 buff_len, LTSPI_SPIMasterTransferStatusCallback *pCallback, void *pClientData) {
    ILTDriverSPI *Idrv = UnitToDriverInterface(unit);
    if (!Idrv) {
        LTLOG_YELLOWALERT("spi.transfer", "no driver for unit");
        return false;
    }
    return Idrv->SPIMasterTransfer(unit, rx_buffer, tx_buffer, buff_len, pCallback, pClientData);
}


define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceSPI)
    .GetBusIndexFromName = LTDeviceSPI_GetBusIndexFromName,
    .GetDeviceCapabilities = LTDeviceSPIImpl_GetDeviceCapabilities,
    .GetDeviceConfiguration = LTDeviceSPIImpl_GetDeviceConfiguration,
    .SetDeviceConfiguration = LTDeviceSPIImpl_SetDeviceConfiguration,
    .SPIMasterTransfer = LTDeviceSPIImpl_SPIMasterTransfer
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  14-Jun-21   titus       created
 *  03-Apr-23   augustus    load driver from LTDeviceConfig specification
 */
