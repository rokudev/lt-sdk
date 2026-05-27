/*******************************************************************************
 * lt/source/lt/device/pushbutton/LTDevicePushButton.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/pushbutton/LTDevicePushButton.h>

DEFINE_LTLOG_SECTION("dev.pushbutton");

/*******************************************************************************
 * Access to the LTDriverPushButton library, through which the Device accesses
 * Push-Button-related functions:                                             */
static ILTEvent                *s_ILTEvent   = NULL;
static LTThread                 s_hThread    = 0;
static ILTThread               *s_ILTThread  = NULL;

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
        return false;
    }
    if (!(pLibraryList->ppDriverLibraries = lt_malloc(nDriverLibraries * sizeof(LTDriverLibrary *)))) {
        LTLOG_YELLOWALERT("drv.lib.array.oom", NULL);
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

/***********************************************************************************************************************
 ****************** END COMMON DRIVER MANAGEMENT ***********************************************************************
 **********************************************************************************************************************/

/*******************************************************************************
 * Standard Device Unit access:                                               */

/* Return the number of available push buttons: */
static u32 LTDevicePushButtonImpl_GetNumDeviceUnits(void) {
    return GetNumDeviceUnits(&s_DriverLibraries);
}

/* LTDevicePushButton does not use the Device Unit facility (see the header): */
static LTDeviceUnit LTDevicePushButtonImpl_CreateDeviceUnitHandle(u32 nIndex) {
    LT_UNUSED(nIndex);
    return 0;
}

/*******************************************************************************
 * Lists of press and release LTEvents for each available button.
 * During initialization, these pointers become the heads of lists of LTEvents
 * allocated on the heap:                                                    */
static LTEvent *s_PressEvents   = NULL;
static LTEvent *s_ReleaseEvents = NULL;

/*******************************************************************************
 * Reclaim all Event handles.
 * This function cannot assume that the handle lists are completely formed, or
 * even that the memory for the handle lists had been successfully allocated.
 * Since it may be called in response to a failed library initialization,
 * the handle lists may be in a partially formed state:                       */
static void DestroyEventHandles(void) {
    if (s_PressEvents) {
        LTHandle *pPressEvent = s_PressEvents;
        for (u32 i = LTDevicePushButtonImpl_GetNumDeviceUnits(); i; --i, ++pPressEvent)
            if (*pPressEvent) {
                s_ILTEvent->Destroy(*pPressEvent);
                *pPressEvent = 0;
            }
        lt_free(s_PressEvents);
    }
    if (s_ReleaseEvents) {
        LTHandle *pReleaseEvent = s_ReleaseEvents;
        for (u32 i = LTDevicePushButtonImpl_GetNumDeviceUnits(); i; --i, ++pReleaseEvent)
            if (*pReleaseEvent) {
                s_ILTEvent->Destroy(*pReleaseEvent);
                *pReleaseEvent = 0;
            }
        lt_free(s_ReleaseEvents);
    }
}

/*******************************************************************************
 * Press and release Event dispatch:                                          */

static const LTArgsDescriptor LTDevicePushButtonEventArgs = {
    1,
    { kLTArgType_u32 }  /* the index of the push button pressed or released */
};

static void LTDevicePushButtonPushEventProc(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    u32 nPushButtonIndex = LTArgs_u32At(0, args);
    (*(LTDevicePushButtonCallback *)proc)(nPushButtonIndex, data);
}

static void LTDevicePushButtonReleaseEventProc(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    u32 nPushButtonIndex = LTArgs_u32At(0, args);
    (*(LTDevicePushButtonCallback *)proc)(nPushButtonIndex, data);
}

/*******************************************************************************
 * Press and release task procs, invoked by the Driver.
 * The push button index is encoded in the client data pointer:               */

static void PushButtonPressEventTaskProc(void *pClientData) {
    u32 nIndex = (u32)((LT_SIZE)pClientData);
    s_ILTEvent->NotifyEvent(s_PressEvents[nIndex], nIndex);
}

static void PushButtonReleaseEventTaskProc(void *pClientData) {
    u32 nIndex = (u32)((LT_SIZE)pClientData);
    s_ILTEvent->NotifyEvent(s_ReleaseEvents[nIndex], nIndex);
}

/*******************************************************************************
 * Connect to the Driver upon LTThread start.
 * Disconnect from the Driver upon LTThread end:                              */

static bool ForAllDriverLibraries(void (*pProc)(ILTDriverPushButton *pIDriver)) {
    for (u32 i = 0; i < s_DriverLibraries.nNumDriverLibraries; ++i) {
        ILTDriverPushButton *pIDriver = lt_getlibraryinterface(ILTDriverPushButton, s_DriverLibraries.ppDriverLibraries[i]);
        if (!pIDriver) return false;
        (*pProc)(pIDriver);
    }
    return true;
}

static void ConnectDriver(ILTDriverPushButton *pIDriver) {
    pIDriver->Connect(s_ILTThread->GetCurrentThread(), PushButtonPressEventTaskProc, PushButtonReleaseEventTaskProc, 0);
}

static void ConnectDrivers(void *pClientData) { LT_UNUSED(pClientData); ForAllDriverLibraries(&ConnectDriver); }

static void DisconnectDriver(ILTDriverPushButton *pIDriver) { pIDriver->Disconnect(); }

/*******************************************************************************
 * Stop the Event thread, Unload the Driver library, and wipe out any data
 * structures that were built up.  This function and the functions it calls must
 * tolerate any full or partial degree of initialization.                     */
static void Shutdown(void) {
    ForAllDriverLibraries(&DisconnectDriver);
    if (s_hThread) {
        if (s_ILTThread) {
            s_ILTThread->Terminate(s_hThread);
            s_ILTThread->WaitUntilFinished(s_hThread, LTTime_Milliseconds(200));
            s_ILTThread->Destroy(s_hThread);
        }
        s_hThread = 0;
    }
    DestroyEventHandles();
    UnloadDriverLibraries(&s_DriverLibraries);
    s_ILTEvent                      = NULL;
    s_PressEvents = s_ReleaseEvents = NULL;
}

/***********************************************************************************************************************
 * Library startup and shutdown.
 * Attempt to open Driver Libraries. If no drivers available, close the Driver Library and return false.
 * Allocate and set up the LTEvent lists and initialize all LTEvents therein.
 * Start up the Event thread.
 * If any stage of initialization goes sideways, clean up and return false, else return true:                         */
static bool LTDevicePushButtonImpl_LibInit(void) {
    LTLOG_DEBUG("init", NULL);
    if (!LoadDriverLibraries("LTDevicePushButton", &s_DriverLibraries))          { Shutdown(); return false; }
    if (!(s_ILTEvent  = lt_getlibraryinterface(ILTEvent, LT_GetCore())))         { Shutdown(); return false; }
    if (!(s_ILTThread = lt_getlibraryinterface(ILTThread, LT_GetCore())))        { Shutdown(); return false; }
    if (!(s_hThread   = LT_GetCore()->CreateThread("dev-pushbutton")))           { Shutdown(); return false; }
    const LT_SIZE nHandleArrayBytes = sizeof (LTHandle) * LTDevicePushButtonImpl_GetNumDeviceUnits();
    /* Allocate and clear out memory for the Event lists: */
    if (!(s_PressEvents   = lt_malloc(nHandleArrayBytes)))                       { Shutdown(); return false; }
    lt_memset(s_PressEvents, 0, nHandleArrayBytes);
    if (!(s_ReleaseEvents = lt_malloc(nHandleArrayBytes)))                       { Shutdown(); return false; }
    lt_memset(s_ReleaseEvents, 0, nHandleArrayBytes);
    /* Set up one press and one release Event for each Push Button: */
    LTHandle *pPressEvent   = s_PressEvents;
    LTHandle *pReleaseEvent = s_ReleaseEvents;
    for (u32 i = LTDevicePushButtonImpl_GetNumDeviceUnits(); i; --i, ++pPressEvent, ++pReleaseEvent) {
        *pPressEvent   = LT_GetCore()->CreateEvent(&LTDevicePushButtonEventArgs, LTDevicePushButtonPushEventProc, NULL, NULL, NULL);
        *pReleaseEvent = LT_GetCore()->CreateEvent(&LTDevicePushButtonEventArgs, LTDevicePushButtonReleaseEventProc, NULL, NULL, NULL);
    }
    /* Start up the Event thread: */
    s_ILTThread->SetStackSize(s_hThread, 768);
    s_ILTThread->Start(s_hThread, NULL, NULL);
    s_ILTThread->QueueTaskProc(s_hThread, ConnectDrivers, NULL, NULL);
    return true;
}

static void LTDevicePushButtonImpl_LibFini(void) {
    LTLOG_DEBUG("fini", NULL);
    Shutdown();
}

/**********************************************************************************************************************************
 * Device Unit access and conversion:                                                                                            */

/* Return the Driver interface for the Device Unit specified by nDeviceUnitNumber, or NULL if the DU number is invalid
   or if the Library pointer, or the interface pointer, cannot be obtained; if the Library pointer is unavailable,
   *pIndexInDriver (through which the function returns the Driver's internal Device Unit index) is untouched: */
static ILTDriverPushButton *GetInterfaceAndIndex(u32 nDeviceUnitNumber, u32 *pIndexInDriver) {
    LTDriverLibrary *pLibrary = GetDriverLibraryProvidingDeviceUnit(&s_DriverLibraries, nDeviceUnitNumber, pIndexInDriver);
    if (!pLibrary) return NULL;
    return lt_getlibraryinterface(ILTDriverPushButton, pLibrary);
}

static bool LTDevicePushButtonImpl_GetPushButtonNameFromIndex(u32 nIndex, char *pPushButtonNameToSet, LT_SIZE nStringSizeBytes) {
    u32 nIndexInDriver;
    ILTDriverPushButton *pIDriver = GetInterfaceAndIndex(nIndex, &nIndexInDriver);
    return pIDriver ? pIDriver->GetPushButtonNameFromIndex(nIndexInDriver, pPushButtonNameToSet, nStringSizeBytes) : false;
}

static bool LTDevicePushButtonImpl_GetIndexFromPushButtonName(char const *pPushButtonName, u32 *pIndexToSet) {
    u32 nDeviceUnitIndexBase = 0;
    for (u32 i = 0; i < s_DriverLibraries.nNumDriverLibraries; ++i) {
        LTDriverLibrary *pLibrary = s_DriverLibraries.ppDriverLibraries[i];
        ILTDriverPushButton *pIDriver = lt_getlibraryinterface(ILTDriverPushButton, pLibrary);
        if (!pIDriver) return false;
        u32 nDeviceUnitIndex;
        if (pIDriver->GetPushButtonIndexFromName(pPushButtonName, &nDeviceUnitIndex)) {
            *pIndexToSet = nDeviceUnitIndexBase + nDeviceUnitIndex;
            return true;
        }
        nDeviceUnitIndexBase += pLibrary->GetNumDeviceUnits();
    }
    return false;
}

/**********************************************************************************************************************************
 * Event registration:                                                                                                           */

static void RegisterButtonEvent(u32 nIndex, LTEvent *pEvents, LTDevicePushButtonCallback *pCallback, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData) {
    if (nIndex < LTDevicePushButtonImpl_GetNumDeviceUnits())
        if (pEvents[nIndex])
            s_ILTEvent->RegisterForEvent(pEvents[nIndex], pCallback, pReleaseProc, pClientData, false);
}

static void LTDevicePushButtonImpl_RegisterForButtonPress(u32 nIndex, LTDevicePushButtonCallback *pCallback, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData) {
    RegisterButtonEvent(nIndex, s_PressEvents, pCallback, pReleaseProc, pClientData);
}

static void LTDevicePushButtonImpl_RegisterForButtonRelease(u32 nIndex, LTDevicePushButtonCallback *pCallback, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData) {
    RegisterButtonEvent(nIndex, s_ReleaseEvents, pCallback, pReleaseProc, pClientData);
}

/**********************************************************************************************************************************
 * Event unregistration:                                                                                                         */

static void UnregisterButtonEvent(u32 nIndex, LTEvent *pEvents, LTDevicePushButtonCallback *pCallback) {
    if (nIndex < LTDevicePushButtonImpl_GetNumDeviceUnits())
        if (pEvents[nIndex])
            s_ILTEvent->UnregisterFromEvent(pEvents[nIndex], pCallback);
}

static void LTDevicePushButtonImpl_UnregisterForButtonPress(u32 nIndex, LTDevicePushButtonCallback *pCallback) {
    UnregisterButtonEvent(nIndex, s_PressEvents, pCallback);
}

static void LTDevicePushButtonImpl_UnregisterForButtonRelease(u32 nIndex, LTDevicePushButtonCallback *pCallback) {
    UnregisterButtonEvent(nIndex, s_ReleaseEvents, pCallback);
}

/**********************************************************************************************************************************
 * Individual PushButton state:                                                                                                  */

static bool LTDevicePushButtonImpl_IsButtonPressed(u32 nIndex) {
    u32 nIndexInDriver;
    ILTDriverPushButton *pIDriver = GetInterfaceAndIndex(nIndex, &nIndexInDriver);
    return pIDriver ? pIDriver->IsButtonPressed(nIndexInDriver) : false;
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDevicePushButton)
    .GetPushButtonNameFromIndex = LTDevicePushButtonImpl_GetPushButtonNameFromIndex,
    .GetPushButtonIndexFromName = LTDevicePushButtonImpl_GetIndexFromPushButtonName,
    .RegisterForButtonPress     = LTDevicePushButtonImpl_RegisterForButtonPress,
    .RegisterForButtonRelease   = LTDevicePushButtonImpl_RegisterForButtonRelease,
    .UnregisterForButtonPress   = LTDevicePushButtonImpl_UnregisterForButtonPress,
    .UnregisterForButtonRelease = LTDevicePushButtonImpl_UnregisterForButtonRelease,
    .IsButtonPressed            = LTDevicePushButtonImpl_IsButtonPressed
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  26-Jan-21   constantine created
 *  12-Apr-23   constantine Convert to Device Config
 */
