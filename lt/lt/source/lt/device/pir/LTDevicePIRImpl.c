/*******************************************************************************
 * lt/source/lt/device/PIR/LTDevicePIRImpl.c
 *
 * LT Device Library for the PIR sensors
 *
 * Provides notification for PIR motion detection
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTArray.h>

#include <lt/device/pir/LTDevicePIRDefs.h>
#include <lt/device/pir/LTDevicePIR.h>

DEFINE_LTLOG_SECTION("lt.dev.PIR");

/*******************************************************************************
 * macros
******************************************************************************/

/*******************************************************************************
 * typedefs
*******************************************************************************/
typedef struct {
    u32                                         nDeviceIndex;           // index of the DeviceUnit
    u16                                         nDriverId;              // an ID that identified which driver this is (used for notifications)
    LTDriverLibrary                           * pDriverLib;             // driver lib
    ILTDriverPIR                              * iDriverPIR;             // driver interface
    LTEvent                                     hEvent;                 // the event for the driver's motion notifications
    LTAtomic                                    eventInProgress;        // flag indicating event is being processed
} PIRDriverInstance;

/******************************************************************************
 * consts
******************************************************************************/
#define LTDEVICEPIR_THREAD_STACKSIZE   (1536)

static const LTArgsDescriptor PIRMotionEventArgs = {
    2,
    { kLTArgType_u8, kLTArgType_u32 },  // motion flag (true, false), and device unit index
};

/*******************************************************************************
 * static variables
*******************************************************************************/
// interfaces
static ILTThread                              * s_iLTThread                 = NULL;
static ILTEvent                               * s_iLTEvent                  = NULL;

// handles
static LTThread                                 s_hThread                   = 0;
static LTArray                                * s_pDeviceArray              = NULL;

static u8                                       s_nNextDriverId             = 0;

/*******************************************************************************
 *
 * PIR device helper functions
 *
*******************************************************************************/

/*******************************************************************************
 * Task proc for calling the client event handlers
*******************************************************************************/
static void MotionDetectionTaskProc(void * pClientData) {

    LTDevicePIRMotionEvent event = *(LTDevicePIRMotionEvent *)(&pClientData);
    u32 nSize = s_pDeviceArray->API->GetCount(s_pDeviceArray);
    u32 nIndex = event.nDriverId + event.nDeviceIndex;
    if (nIndex < nSize) {
        PIRDriverInstance * pInstance = s_pDeviceArray->API->Get(s_pDeviceArray, nIndex, NULL);
        if (pInstance != NULL) {
            if ((pInstance->hEvent != 0) && LTAtomic_CompareAndExchange(&pInstance->eventInProgress, 0, 1)) {
                s_iLTEvent->NotifyEvent(pInstance->hEvent, event.bMotion, nIndex);
            }
        }
    }
}

/*******************************************************************************
 * Motion detection event proc
*******************************************************************************/
static void DevicePIRMotionEventProc(LTEvent event, void * proc,
                                     LTArgs* args, void * data) {
    LT_UNUSED(event);
    bool bMotion = (LTArgs_u8At(0, args) != 0);
    (*(LTDevicePIRMotionProc *)proc)(bMotion, LTArgs_u32At(1, args), data);
}

/*******************************************************************************
 * Motion detection event proc complete
*******************************************************************************/
static void DevicePIRMotionEventProcComplete(LTEvent event, LTArgs* args) {
    LT_UNUSED(event);
    u32 nIndex = LTArgs_u32At(1, args);
    u32 nSize = s_pDeviceArray->API->GetCount(s_pDeviceArray);
    if (nIndex < nSize) {
        PIRDriverInstance * pInstance = s_pDeviceArray->API->Get(s_pDeviceArray, nIndex, NULL);
        if (pInstance != NULL) LTAtomic_Store(&pInstance->eventInProgress, 0);
    }
}

/*******************************************************************************
 * End the threads and cleans up libraries and handles
*******************************************************************************/
static void Cleanup(void) {
    // first end the thread so nothing is accessed after it's cleaned up
    if (s_iLTThread != NULL) {
        if (s_hThread != 0) {
            s_iLTThread->Terminate(s_hThread);
            s_iLTThread->WaitUntilFinished(s_hThread, LTTime_Milliseconds(500));
            s_iLTThread->Destroy(s_hThread);
            s_hThread = 0;
        }
    }

    if (s_pDeviceArray != 0) {
        u32 size = s_pDeviceArray->API->GetCount(s_pDeviceArray);
        for (u32 i = 0; i < size; ++i) {
            PIRDriverInstance * pInstance = s_pDeviceArray->API->Get(s_pDeviceArray, i, NULL);
            // only close the first one as there can be multiple
            if (pInstance->nDeviceIndex == 0) {
                LT_GetCore()->CloseLibrary((LTLibrary *)pInstance->pDriverLib);
            }
            if (pInstance->hEvent != 0) {
                s_iLTEvent->Destroy(pInstance->hEvent);
            }
        }
        lt_destroyobject(s_pDeviceArray);
        s_pDeviceArray = NULL;
    }

    s_iLTThread = NULL;
    s_iLTEvent  = NULL;
}

/*******************************************************************************
 * Proc for loading all the available PIR sensor drivers
*******************************************************************************/
static bool InstalledLibrariesEnumProc(const char * pLibraryName, void * pClientData) {
    LT_UNUSED(pClientData);

    if (lt_strstr(pLibraryName, "DriverPIR") != NULL) {
        LTDriverLibrary * pDriverLib = (LTDriverLibrary *)LT_GetCore()->OpenLibrary(pLibraryName);
        if (pDriverLib == NULL) {
            LTLOG_REDALERT("fail.enum.lib", "Failed to open library %s", pLibraryName);
            return false;
        }
        ILTDriverPIR * iDriverPIR = lt_getlibraryinterface(ILTDriverPIR, pDriverLib);
        if (iDriverPIR == NULL) {
            LTLOG_REDALERT("fail.enum.interface", "Failed to get library interface for %s", pLibraryName);
            LT_GetCore()->CloseLibrary((LTLibrary *)pDriverLib);
            return false;
        }

        u32 nNumDevices = pDriverLib->GetNumDeviceUnits();
        for (u32 i = 0; i < nNumDevices; ++i) {
            PIRDriverInstance instance;
            instance.pDriverLib     = pDriverLib;
            instance.iDriverPIR     = iDriverPIR;
            instance.nDeviceIndex   = i;
            instance.nDriverId      = s_nNextDriverId;
            instance.hEvent         = LT_GetCore()->CreateEvent(&PIRMotionEventArgs, DevicePIRMotionEventProc, DevicePIRMotionEventProcComplete, NULL, NULL);
            if (instance.hEvent == 0) {
                LTLOG_REDALERT("fail.event.handle", "Failed to create driver motion event");
                LT_GetCore()->CloseLibrary((LTLibrary *)pDriverLib);
                return false;
            }
            LTAtomic_Store(&instance.eventInProgress, 0);
            s_pDeviceArray->API->Append(s_pDeviceArray, &instance);
        }
        ++s_nNextDriverId;
        LTLOG_DEBUG("driver.load", "Loaded PIR driver %s", pLibraryName);
    }

    return true;
}

/*******************************************************************************
 * Thread start
*******************************************************************************/
static bool OnStartThread(void) {

    u32 nSize = s_pDeviceArray->API->GetCount(s_pDeviceArray);
    for (u32 i = 0; i < nSize; ++i) {
        PIRDriverInstance * pInstance = s_pDeviceArray->API->Get(s_pDeviceArray, i, NULL);
        if (pInstance != NULL) {
            // only call start for the first device as there can be multiple
            if (pInstance->nDeviceIndex == 0) {
                pInstance->iDriverPIR->StartMotionDetection(MotionDetectionTaskProc, pInstance->nDriverId);
            }
        }
    }
    return true;
}

/*******************************************************************************
 * Thread end
*******************************************************************************/
static void OnEndThread(void) {

    u32 nSize = s_pDeviceArray->API->GetCount(s_pDeviceArray);
    for (u32 i = 0; i < nSize; ++i) {
        PIRDriverInstance * pInstance = s_pDeviceArray->API->Get(s_pDeviceArray, i, NULL);
        if (pInstance != NULL) {
            // make sure the driver is disabled in case the client didn't disabled it
            pInstance->iDriverPIR->Enable(pInstance->nDeviceIndex, false);
            // only call stop for the first device as there can be multiple
            if (pInstance->nDeviceIndex == 0) {
                pInstance->iDriverPIR->StopMotionDetection();
            }
        }
    }
}

/*******************************************************************************
 *
 * PIR device interface implementation
 *
*******************************************************************************/

/*******************************************************************************
 * Initialize the device
*******************************************************************************/
static bool LTDevicePIRImpl_LibInit(void) {
    s_nNextDriverId = 0;

    s_iLTThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    if (s_iLTThread == NULL) {
        LTLOG_REDALERT("init.thread", "Failed to get IThread interface");
        Cleanup();
        return false;
    }

    s_hThread   = LT_GetCore()->CreateThread("dev-PIR");
    if (s_hThread == 0) {
        LTLOG_REDALERT("init.create.thread", "Failed to create the device thread");
        Cleanup();
        return false;
    }

    s_pDeviceArray = LTArray_CreateStructArray(sizeof(PIRDriverInstance));
    if (s_pDeviceArray == 0) {
        LTLOG_REDALERT("array.create.fail", "Failed to create the drivers libs array");
        Cleanup();
        return false;
    }

    s_iLTEvent = lt_getlibraryinterface(ILTEvent, LT_GetCore());
    if (s_iLTEvent == NULL) {
        LTLOG_REDALERT("event.interface.failed", "Failed to get the ILTEvent interface");
        Cleanup();
        return false;
    }

    // load all the installed drivers
    if (!LT_GetCore()->EnumerateInstalledLibraries(InstalledLibrariesEnumProc, NULL)) {
        LTLOG_REDALERT("drivers.load.failed", "Failed to load the drivers");
        Cleanup();
        return false;
    }

    s_iLTThread->SetStackSize(s_hThread, LTDEVICEPIR_THREAD_STACKSIZE);
    s_iLTThread->Start(s_hThread, OnStartThread, OnEndThread);

    return true;
}

/*******************************************************************************
 * Finalize the driver library by closing the driver library
*******************************************************************************/
static void LTDevicePIRImpl_LibFini(void) {
    Cleanup();
}

/*******************************************************************************
 * obtains a pointer to the PIR sensor name
*******************************************************************************/
static bool LTDevicePIRImpl_GetDeviceNameFromUnitNumber(u32 nDeviceUnitNumber, char const ** ppDeviceName) {
    bool bSuccess = false;
    if (nDeviceUnitNumber < s_pDeviceArray->API->GetCount(s_pDeviceArray)) {
         PIRDriverInstance * pInstance = s_pDeviceArray->API->Get(s_pDeviceArray, nDeviceUnitNumber, NULL);
         if (pInstance != NULL) {
            *ppDeviceName = pInstance->iDriverPIR->GetDeviceName(pInstance->nDeviceIndex);
            bSuccess = (*ppDeviceName != NULL);
         }
    }
    return bSuccess;
}

/*******************************************************************************
 * obtains the Device Unit number of the named PIR
*******************************************************************************/
static bool LTDevicePIRImpl_GetUnitNumberFromDeviceName(char const * pDeviceName, u32 * pDeviceUnitNumber) {
    bool bSuccess = false;
    u32 size = s_pDeviceArray->API->GetCount(s_pDeviceArray);
    for (u32 i = 0; i < size && !bSuccess; ++i) {
        PIRDriverInstance * pInstance = s_pDeviceArray->API->Get(s_pDeviceArray, i, NULL);
        if (pInstance != NULL) {
            const char * pName = pInstance->iDriverPIR->GetDeviceName(pInstance->nDeviceIndex);
            if (pName != NULL) {
                if (lt_strcasecmp(pName, pDeviceName) == 0) {
                    bSuccess = true;
                    *pDeviceUnitNumber = i;
                }
            }
        }
    }
    return bSuccess;
}

/*******************************************************************************
 * register the given proc for receiving motion detection events
*******************************************************************************/
static void LTDevicePIRImpl_RegisterForMotionDetection(LTDeviceUnit hDeviceUnit, LTDevicePIRMotionProc * pMotionProc, LTThread_ClientDataReleaseProc *pReleaseProc, void * pClientData) {
    PIRDriverInstance ** ppInstance = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit);
    if (ppInstance != NULL) {
        if (*ppInstance != NULL) {
            s_iLTEvent->RegisterForEvent((*ppInstance)->hEvent, pMotionProc, pReleaseProc, pClientData, false);
        }
        LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, ppInstance);
    }
}

/*******************************************************************************
 * Unregister a client from position changes
*******************************************************************************/
static void LTDevicePIRImpl_UnregisterFromMotionDetection(LTDeviceUnit hDeviceUnit, LTDevicePIRMotionProc * pMotionProc) {
    PIRDriverInstance ** ppInstance = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit);
    if (ppInstance != NULL) {
        if (*ppInstance != NULL) {
            s_iLTEvent->UnregisterFromEvent((*ppInstance)->hEvent, pMotionProc);
        }
        LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, ppInstance);
    }
}

/*******************************************************************************
 * Sets the PIR sensitivity
*******************************************************************************/
static bool LTDevicePIRImpl_SetSensitivity(LTDeviceUnit hDeviceUnit, u8 nPercent) {
    bool bResult = false;
    PIRDriverInstance ** ppInstance = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit);
    if (ppInstance != NULL) {
        if (*ppInstance != NULL) {
            bResult = (*ppInstance)->iDriverPIR->SetSensitivity((*ppInstance)->nDeviceIndex, nPercent > 100 ? 100 : nPercent);
        }
        LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, ppInstance);
    }
    return bResult;
}

/*******************************************************************************
 * Gets the current PIR sensitivity
*******************************************************************************/
static bool LTDevicePIRImpl_GetSensitivity(LTDeviceUnit hDeviceUnit, u8 *pValue) {
    bool bResult = false;
    PIRDriverInstance ** ppInstance = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit);
    if (ppInstance != NULL) {
        if (*ppInstance != NULL) {
            bResult = (*ppInstance)->iDriverPIR->GetSensitivity((*ppInstance)->nDeviceIndex, pValue);
        }
        LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, ppInstance);
    }
    return bResult;
}

/*******************************************************************************
 * Sets the interval where no motion is re-detected to avoid flip-flopping
*******************************************************************************/
static bool LTDevicePIRImpl_SetMotionEndDelay(LTDeviceUnit hDeviceUnit, LTTime interval) {
    bool bResult = false;
    PIRDriverInstance ** ppInstance = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit);
    if (ppInstance != NULL) {
        if (*ppInstance != NULL) {
            bResult = (*ppInstance)->iDriverPIR->SetMotionEndDelay((*ppInstance)->nDeviceIndex, interval);
        }
        LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, ppInstance);
    }
    return bResult;
}

/*******************************************************************************
 * Enables/disables the driver
*******************************************************************************/
static void LTDevicePIRImpl_Enable(LTDeviceUnit hDeviceUnit, bool bEnable) {
    PIRDriverInstance ** ppInstance = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit);
    if (ppInstance != NULL) {
        if (*ppInstance != NULL) {
            (*ppInstance)->iDriverPIR->Enable((*ppInstance)->nDeviceIndex, bEnable);
        }
        LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, ppInstance);
    }
}

/*******************************************************************************
 * returns number of units from the driver
*******************************************************************************/
static u32 LTDevicePIRImpl_GetNumDeviceUnits(void) {
    return s_pDeviceArray->API->GetCount(s_pDeviceArray);
}

/*******************************************************************************
 * creates a handle for the given unit and create a client entry for registration
*******************************************************************************/
static LTDeviceUnit LTDevicePIRImpl_CreateDeviceUnitHandle(u32 nDeviceNum) {
    LTDeviceUnit hDeviceUnit = 0;

    if (nDeviceNum < s_pDeviceArray->API->GetCount(s_pDeviceArray)) {
        PIRDriverInstance * pInstance = s_pDeviceArray->API->Get(s_pDeviceArray, nDeviceNum, NULL);
        if (pInstance != NULL) {
            hDeviceUnit = LT_GetCore()->CreateHandle((LTInterface *)pInstance->iDriverPIR, sizeof(PIRDriverInstance *));
            if (hDeviceUnit != 0) {
                PIRDriverInstance ** ppInstance = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit);
                if (ppInstance != NULL) {
                    *ppInstance = pInstance;
                    LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, ppInstance);
                }
            }
        }
    }
    return hDeviceUnit;
}

/*******************************************************************************
 * Interface definition
*******************************************************************************/
define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDevicePIR) {
    .GetDeviceNameFromUnitNumber        = LTDevicePIRImpl_GetDeviceNameFromUnitNumber,
    .GetUnitNumberFromDeviceName        = LTDevicePIRImpl_GetUnitNumberFromDeviceName,
    .RegisterForMotionDetection         = LTDevicePIRImpl_RegisterForMotionDetection,
    .UnregisterFromMotionDetection      = LTDevicePIRImpl_UnregisterFromMotionDetection,
    .SetSensitivity                     = LTDevicePIRImpl_SetSensitivity,
    .GetSensitivity                     = LTDevicePIRImpl_GetSensitivity,
    .SetMotionEndDelay                  = LTDevicePIRImpl_SetMotionEndDelay,
    .Enable                             = LTDevicePIRImpl_Enable,
  }  LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  16-Nov-21   vitellius   created
 */
