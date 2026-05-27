/*******************************************************************************
 * lt/source/lt/device/pins/LTDevicePinsImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/pins/LTDevicePins.h>

DEFINE_LTLOG_SECTION("dev.pins");

#define LTDEVICEPINS_DO_DLOG     1
#if     LTDEVICEPINS_DO_DLOG
#define DLOG                    LTLOG
#else
#define DLOG                    LTLOG_LOGNULL
#endif

/***********************************************************************************************************************
 **************** BEGIN COMMON DRIVER MANAGEMENT ***********************************************************************
 **********************************************************************************************************************/

typedef struct DriverLibraryList {
    LT_SIZE             nNumDriverLibraries;
    LTDriverLibrary   **ppDriverLibraries;
} DriverLibraryList;

static DriverLibraryList s_DriverLibraries;

/***********************************************************************************************************************
 * Driver configuration.
 * Read the Device Config for this Device and load all listed Drivers, storing a pointer to each
 * in s_DriverLibraries.
 * Return the number of Driver Libraries actually loaded:                                                             */

static u32 LoadDriverLibraries(const char *pDeviceLibraryName, DriverLibraryList *pLibraryList) {
    if (!pDeviceLibraryName || !*pDeviceLibraryName || !pLibraryList) return 0;
    LTDeviceConfig *pDeviceConfig = lt_openlibrary(LTDeviceConfig);
    if (!pDeviceConfig) return false;
    LT_SIZE nDriverLibraries = pDeviceConfig->GetNumDrivers(pDeviceLibraryName);
    if (!nDriverLibraries) {
        LTLOG_YELLOWALERT("drv.lib.config.0", NULL);
        lt_closelibrary(pDeviceConfig);
        return false;
    }
    if (!(pLibraryList->ppDriverLibraries = lt_malloc(nDriverLibraries * sizeof(LTDriverLibrary *)))) {
        LTLOG_YELLOWALERT("drv.lib.array.oom", NULL);
        lt_closelibrary(pDeviceConfig);
        return false;
    }
    LTDriverLibrary **ppDriverLibrary = pLibraryList->ppDriverLibraries;
    for (LT_SIZE i = 0; i < nDriverLibraries; ++i) {
        LTLOG_DEBUG("drv.lib.name", "%s", pDeviceConfig->GetDriverAt(pDeviceLibraryName, i));
        if ((*ppDriverLibrary = (LTDriverLibrary *)LT_GetCore()->OpenLibrary(pDeviceConfig->GetDriverAt(pDeviceLibraryName, i))))
            ++ppDriverLibrary;  /* success - point to the next position in the array */
    }    /* (If the Driver didn't load, Core will complain via a YELLOWALERT) */
    /* If some Drivers failed to load, the array will be too big - trim it down: */
    pLibraryList->nNumDriverLibraries = ppDriverLibrary - pLibraryList->ppDriverLibraries;
    if (!pLibraryList->nNumDriverLibraries) {
        LTLOG_YELLOWALERT("drv.lib.0", NULL);
        lt_free(pLibraryList->ppDriverLibraries);
        pLibraryList->ppDriverLibraries = NULL;
    }
    else if (pLibraryList->nNumDriverLibraries < nDriverLibraries) {
        LTLOG_YELLOWALERT("drv.lib.n", "%lu/%lu", LT_PLT_SIZE(pLibraryList->nNumDriverLibraries), LT_PLT_SIZE(nDriverLibraries));
        lt_realloc(pLibraryList->ppDriverLibraries, pLibraryList->nNumDriverLibraries * sizeof(LTDriverLibrary *));
    }
    lt_closelibrary(pDeviceConfig);
    return pLibraryList->nNumDriverLibraries;
}

static void UnloadDriverLibraries(DriverLibraryList *pLibraryList) {
    if (pLibraryList && pLibraryList->ppDriverLibraries) {
        LTDriverLibrary **ppDriverLibrary = pLibraryList->ppDriverLibraries;
        for (LT_SIZE i = pLibraryList->nNumDriverLibraries; i; --i, ++ppDriverLibrary) LT_GetCore()->CloseLibrary((LTLibrary *)*ppDriverLibrary);
        lt_free(pLibraryList->ppDriverLibraries);
        pLibraryList->ppDriverLibraries = NULL;
        pLibraryList->nNumDriverLibraries = 0;
    }
}

/***********************************************************************************************************************
 * Return a pointer to the Driver Library which provides the Device Unit specified by the Device Unit index:          */
static LTDriverLibrary *GetDriverLibraryProvidingDeviceUnit(DriverLibraryList *pLibraryList, u32 nIndex, u32 *pIndexInDriver) {
    if (!pLibraryList) return NULL;
    u32 nNumDeviceUnits = 0;        /* running total of Device Units available from this Driver and all previous */
    u32 nDeviceUnitIndexBase = 0;   /* base index of Device Units provided by this Driver                        */
    for (u32 i = 0; i < pLibraryList->nNumDriverLibraries; ++i) {
        u32 nNumDeviceUnitsThisDriver = pLibraryList->ppDriverLibraries[i]->GetNumDeviceUnits();
        nNumDeviceUnits += nNumDeviceUnitsThisDriver;
        if (nNumDeviceUnits > nIndex) {
            if (pIndexInDriver) *pIndexInDriver = nIndex - nDeviceUnitIndexBase;
            return pLibraryList->ppDriverLibraries[i];
        }
        nDeviceUnitIndexBase += nNumDeviceUnitsThisDriver;
    }
    return NULL;   /* did not find a Driver with the right Device Unit index range */
}

static u32 GetNumDeviceUnits(DriverLibraryList *pLibraryList) {
    if (!pLibraryList) return 0;
    u32 nNumDeviceUnits = 0;
    for (u32 i = 0; i < pLibraryList->nNumDriverLibraries; ++i) nNumDeviceUnits += pLibraryList->ppDriverLibraries[i]->GetNumDeviceUnits();
    return nNumDeviceUnits;
}

static LTDeviceUnit CreateDeviceUnitHandle(DriverLibraryList *pLibraryList, u32 nDeviceUnitIndex) {
    u32 nIndexInDriver = 0;
    LTDriverLibrary *pLibrary = GetDriverLibraryProvidingDeviceUnit(pLibraryList, nDeviceUnitIndex, &nIndexInDriver);
    return pLibrary ? pLibrary->CreateDeviceUnitHandle(nIndexInDriver) : 0;
}

/***********************************************************************************************************************
 ****************** END COMMON DRIVER MANAGEMENT ***********************************************************************
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Standard Device Unit access:                                                                                       */

static u32 LTDevicePinsImpl_GetNumDeviceUnits(void) { return GetNumDeviceUnits(&s_DriverLibraries); }

/* Look through the Driver LT Libraries to find a Device Unit by number.  Return the handle, or 0 upon failure: */
static LTDeviceUnit LTDevicePinsImpl_CreateDeviceUnitHandle(u32 nDeviceUnitIndex) {
    return CreateDeviceUnitHandle(&s_DriverLibraries, nDeviceUnitIndex);
}

/***********************************************************************************************************************
 * Interface-specific Device Unit conversion:                                                                                */

/* Return the Driver interface for the Device Unit specified by nDeviceUnitIndex, or NULL if the DU number is invalid
   or if the Library pointer, or the interface pointer, cannot be obtained; if the Library pointer is unavailable,
   *pIndexInDriver (through which the function returns the Driver's internal Device Unit index) is untouched: */
static ILTDriverPins *GetInterfaceAndIndex(u32 nDeviceUnitIndex, u32 *pIndexInDriver) {
    LTDriverLibrary *pLibrary = GetDriverLibraryProvidingDeviceUnit(&s_DriverLibraries, nDeviceUnitIndex, pIndexInDriver);
    if (!pLibrary) return NULL;
    return lt_getlibraryinterface(ILTDriverPins, pLibrary);
}

static bool LTDevicePinsImpl_GetBankNameFromUnitNumber(u32 nDeviceUnitIndex, char const **ppPinBankNameToSet) {
    u32 nIndexInDriver = 0;
    ILTDriverPins *pILTDriverPins = GetInterfaceAndIndex(nDeviceUnitIndex, &nIndexInDriver);
    return pILTDriverPins ? pILTDriverPins->GetBankNameFromUnitNumber(nIndexInDriver, ppPinBankNameToSet) : false;
}

static bool LTDevicePinsImpl_GetUnitNumberFromBankName(char const *pPinBankName, u32 *pDeviceUnitNumberToSet) {
    u32 nDeviceUnitIndexBase = 0;
    for (u32 i = 0; i < s_DriverLibraries.nNumDriverLibraries; ++i) {
        LTDriverLibrary *pLibrary = s_DriverLibraries.ppDriverLibraries[i];
        ILTDriverPins *pILTDriverPins = lt_getlibraryinterface(ILTDriverPins, pLibrary);
        if (!pILTDriverPins) return false;
        u32 nDeviceUnitIndex;
        if (pILTDriverPins->GetUnitNumberFromBankName(pPinBankName, &nDeviceUnitIndex)) {
            *pDeviceUnitNumberToSet = nDeviceUnitIndexBase + nDeviceUnitIndex;
            return true;
        }
        nDeviceUnitIndexBase += pLibrary->GetNumDeviceUnits();
    }
    return false;
}

static bool LTDevicePinsImpl_GetBankTypeFromUnitNumber(u32 nDeviceUnitIndex, LTDevicePin_PinType *pPinType) {
    u32 nIndexInDriver = 0;
    ILTDriverPins *pILTDriverPins = GetInterfaceAndIndex(nDeviceUnitIndex, &nIndexInDriver);
    return pILTDriverPins ? pILTDriverPins->GetBankTypeFromUnitNumber(nIndexInDriver, pPinType) : false;
}

/***********************************************************************************************************************
 * Library startup and shutdown.
 * Attempt to open Driver Libraries. If no drivers available, close the Driver Library and return false.
 * Otherwise, the Device is open successfully - return true.                                                          */

static void LTDevicePinsImpl_LibFini(void) {
    LTLOG_DEBUG("fini", NULL);
    UnloadDriverLibraries(&s_DriverLibraries);
}

static bool LTDevicePinsImpl_LibInit(void) {
    LTLOG_DEBUG("init", NULL);
    if (!LoadDriverLibraries("LTDevicePins", &s_DriverLibraries)) { LTDevicePinsImpl_LibFini(); return false; }
    return true;
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDevicePins)
    .GetBankNameFromUnitNumber = LTDevicePinsImpl_GetBankNameFromUnitNumber,
    .GetUnitNumberFromBankName = LTDevicePinsImpl_GetUnitNumberFromBankName,
    .GetBankTypeFromUnitNumber = LTDevicePinsImpl_GetBankTypeFromUnitNumber
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  08-Feb-21   constantine created
 *  12-Apr-23   constantine Convert to Device Config
 */
