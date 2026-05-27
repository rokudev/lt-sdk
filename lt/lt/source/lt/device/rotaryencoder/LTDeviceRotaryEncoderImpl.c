/*******************************************************************************
 * lt/source/lt/device/rotaryencoder/LTDeviceRotaryEncoderImpl.c
 *
 * LT Device Library for the rotary encoder
 *
 * Provides notification for rotary encoder position information
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTArray.h>

#include <lt/device/rotaryencoder/LTDeviceRotaryEncoder.h>

DEFINE_LTLOG_SECTION("lt.dev.rotaryencoder");

/*******************************************************************************
 * macros
******************************************************************************/

/*******************************************************************************
 * typedefs
*******************************************************************************/
// device driver info
typedef struct {
    u32                                         nDeviceUnitNumber;  // the device unit number
    ILTDriverRotaryEncoder                    * iDriver;            // pointer to the driver's interface
} RotaryEncoderDevice;

// device units create by clients
typedef struct {
    LTThread                                    hThread;            // the client's thread who created the handle
    RotaryEncoderDevice                       * pDevice;            // the device info
    LTDeviceRotaryEncoderPositionChangedProc  * pChangeProc;        // the event change proc to check when the notification is fired
    void                                      * pClientData;        // the client's data
    LTTime                                      tmEventInterval;    // event fire interval
    LTTime                                      tmNextEventTime;    // next event fire time
    RotaryEncoderPosition                       position;           // the last reported position
} RotaryEncoderClientInfo;

/******************************************************************************
 * consts
******************************************************************************/
static const u32                                kThreadStackSize            = 512;
// since the knob is moved by the user, reporting the position at 10Hz should be enough
static const LTTime                             kPositionTimerInterval      = LTTimeInitializer_Milliseconds(100);

/*******************************************************************************
 * static variables
*******************************************************************************/
// interfaces
static ILTThread                              * s_iLTThread                 = NULL;

// handles
static LTThread                                 s_hThread                   = 0;    // timer thread

// Objects
static LTMutex                                * s_mutex                     = NULL;
static LTArray                                * s_pArrayDriverLibs          = NULL; // array of all loaded drivers
static LTArray                                * s_pArrayDevices             = NULL; // array of all devices in all drivers (RotaryEncoderDevice)
static LTArray                                * s_pArrayDeviceUnits         = NULL; // array of created LTDeviceUnits

// the total number of devices across all the drivers
static u32                                      s_nTotalNumDeviceUnits      = 0;

/*******************************************************************************
 *
 * rotary encoder device helper functions
 *
*******************************************************************************/

/*******************************************************************************
 * Task proc for calling the client proc
*******************************************************************************/
static void PositionChangedTaskProc(void * pClientData) {

    s_mutex->API->Lock(s_mutex);
    RotaryEncoderClientInfo clientInfo;
    // set the proc to NULL in case the data wasn't there anymore
    clientInfo.pChangeProc = NULL;
    LTHandle hClient = VOIDPTR_TO_LTHANDLE(pClientData);
    RotaryEncoderClientInfo * pClientInfo = LT_GetCore()->ReserveHandlePrivateData(hClient);
    if (pClientInfo != NULL) {
        // make a copy of the client data so the mutex is not held while the client is processing the change
        clientInfo = *pClientInfo;
    }
    s_mutex->API->Unlock(s_mutex);
    LT_GetCore()->ReleaseHandlePrivateData(hClient, pClientInfo);

    if (clientInfo.pChangeProc != NULL)  {
        (*clientInfo.pChangeProc)(clientInfo.position, clientInfo.pClientData);
    }
}

/*******************************************************************************
 * Timer proc for reporting the position
*******************************************************************************/
static void TimerProc(void * pClientData) {
    LT_UNUSED(pClientData);

    s_mutex->API->Lock(s_mutex);

    LTTime tmNow = LT_GetCore()->GetKernelTime();
    // go through all the device units and notify clients if the position changed
    u32 nSize = s_pArrayDeviceUnits->API->GetCount(s_pArrayDeviceUnits);
    for (u32 i = 0; i < nSize; ++i) {
        LTDeviceUnit hDeviceUnit = 0;
        s_pArrayDeviceUnits->API->Get(s_pArrayDeviceUnits, i, &hDeviceUnit);
        RotaryEncoderClientInfo * pClientInfo = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit);
        if (pClientInfo != NULL) {
            if (LTTime_IsGreaterThanOrEqual(tmNow, pClientInfo->tmNextEventTime)) {
                RotaryEncoderPosition newPosition = pClientInfo->pDevice->iDriver->GetPosition(pClientInfo->pDevice->nDeviceUnitNumber);
                // no need to check for speed as it would have changed if direction or position changed
                if (newPosition.direction != pClientInfo->position.direction ||
                    newPosition.nPosition != pClientInfo->position.nPosition) {
                    // update the client's position and pass the device unit handle to the thread
                    pClientInfo->position = newPosition;
                    s_iLTThread->QueueTaskProc(pClientInfo->hThread, PositionChangedTaskProc, NULL, LTHANDLE_TO_VOIDPTR(hDeviceUnit));
                    // update the next notification time
                    pClientInfo->tmNextEventTime = LTTime_Add(tmNow, pClientInfo->tmEventInterval);
                }
            }
            LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, pClientInfo);
        }
    }

    s_mutex->API->Unlock(s_mutex);
}

/*******************************************************************************
 * End the threads and cleans up libraries and handles
*******************************************************************************/
static void Cleanup(void) {
    // first end the thread so nothing is accessed after it's cleaned up
    if (s_iLTThread != NULL) {
        if (s_hThread != 0) {
            s_iLTThread->KillTimer(s_hThread, TimerProc, NULL);
            s_iLTThread->Terminate(s_hThread);
            s_iLTThread->WaitUntilFinished(s_hThread, LTTime_Milliseconds(500));
            s_iLTThread->Destroy(s_hThread);
            s_hThread = 0;
        }
        s_iLTThread = NULL;
    }

    lt_destroyobject(s_mutex);
    s_mutex = NULL;

    // arrays
    if (s_pArrayDriverLibs != NULL) {
        u32 nSize = s_pArrayDriverLibs->API->GetCount(s_pArrayDriverLibs);
        for (u32 i = 0; i < nSize; ++i) {
            LTDriverLibrary * pDriverLib = NULL;
            s_pArrayDriverLibs->API->Get(s_pArrayDriverLibs, i, &pDriverLib);
            LT_GetCore()->CloseLibrary((LTLibrary *)pDriverLib);
        }
        lt_destroyobject(s_pArrayDriverLibs);
        s_pArrayDriverLibs = NULL;
    }
    lt_destroyobject(s_pArrayDeviceUnits);
    s_pArrayDeviceUnits = NULL;
    lt_destroyobject(s_pArrayDevices);
    s_pArrayDevices = NULL;
}

/*******************************************************************************
 * Proc for loading all the available rotatry encoder drivers
*******************************************************************************/
static bool InstalledLibrariesEnumProc(const char * pLibraryName, void * pClientData) {
    LT_UNUSED(pClientData);

    if (lt_strstr(pLibraryName, "DriverRotaryEncoder") != NULL) {
        LTDriverLibrary * pDriver = (LTDriverLibrary *)LT_GetCore()->OpenLibrary(pLibraryName);
        if (pDriver == NULL) {
            LTLOG_REDALERT("enum.lib", "Failed to open library %s", pLibraryName);
            return false;
        }
        s_pArrayDriverLibs->API->Append(s_pArrayDriverLibs, &pDriver);

        // first load the driver library
        ILTDriverRotaryEncoder * iDriver = lt_getlibraryinterface(ILTDriverRotaryEncoder, pDriver);
        if (iDriver == NULL) {
            LTLOG_REDALERT("enum.lib", "Failed to get library interface for %s", pLibraryName);
            return false;
        }
        // go through the number of device units and create an entry in the array for each
        u32 nNumDevices = pDriver->GetNumDeviceUnits();
        for (u32 i = 0; i < nNumDevices; ++i, ++s_nTotalNumDeviceUnits) {
             RotaryEncoderDevice device = { i, iDriver };
             s_pArrayDevices->API->Append(s_pArrayDevices, &device);
        }
    }

    return true;
}

/*******************************************************************************
 *
 * rotary encoder device interface implementation
 *
*******************************************************************************/

/*******************************************************************************
 * Initialize the device
*******************************************************************************/
static bool LTDeviceRotaryEncoderImpl_LibInit(void) {
    // initialize the static variables
    s_iLTThread                     = NULL;
    s_pArrayDriverLibs              = NULL;
    s_pArrayDevices                 = NULL;
    s_pArrayDeviceUnits             = NULL;
    s_hThread                       = 0;
    s_mutex                         = NULL;
    s_nTotalNumDeviceUnits          = 0;

    // load the IThread interface for creating our thread
    s_iLTThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    if (s_iLTThread == NULL) {
        LTLOG_REDALERT("init.thread", "Failed to get IThread interface");
        Cleanup();
        return false;
    }

    // create the device thread
    s_hThread   = LT_GetCore()->CreateThread("dev-rotaryencoder");
    if (s_hThread == 0) {
        LTLOG_REDALERT("init.create.thread", "Failed to create the device thread");
        Cleanup();
        return false;
    }

    s_mutex = lt_createobject(LTMutex);
    if (s_mutex == 0) {
        LTLOG_REDALERT("init.mutex.handle", "Failed to create the client info mutex");
        Cleanup();
        return false;
    }

    s_pArrayDriverLibs = lt_createobject(LTArray);
    if (s_pArrayDriverLibs == NULL) {
        LTLOG_REDALERT("array.create.fail", "Failed to create the drivers libs array");
        Cleanup();
        return false;
    }
    s_pArrayDevices = LTArray_CreateStructArray(sizeof(RotaryEncoderDevice));
    if (s_pArrayDevices == NULL) {
        LTLOG_REDALERT("array.create.fail", "Failed to create the devices array");
        Cleanup();
        return false;
    }
    s_pArrayDeviceUnits = LTArray_CreateStructArray(sizeof(LTDeviceUnit));
    if (s_pArrayDeviceUnits == NULL) {
        LTLOG_REDALERT("array.create.fail", "Failed to create the device units array");
        Cleanup();
        return false;
    }

    // load all the installed drivers
    if (!LT_GetCore()->EnumerateInstalledLibraries(InstalledLibrariesEnumProc, NULL)) {
        LTLOG_REDALERT("drivers.load.failed", "Failed to load the drivers");
        return false;
    }

    // start the thread for the timer
    s_iLTThread->SetStackSize(s_hThread, kThreadStackSize);
    s_iLTThread->Start(s_hThread, NULL, NULL);
    s_iLTThread->SetTimer(s_hThread, kPositionTimerInterval, TimerProc, NULL, NULL);

    return true;
}

/*******************************************************************************
 * Finalize the driver library by closing the driver library
*******************************************************************************/
static void LTDeviceRotaryEncoderImpl_LibFini(void) {
    Cleanup();
}

/*******************************************************************************
 * Register a client for position changes
*******************************************************************************/
static void LTDeviceRotaryEncoderImpl_RegisterForPositionChanges(LTDeviceUnit hDeviceUnit, LTDeviceRotaryEncoderPositionChangedProc * pChangeProc,
                                                                 LTTime interval, void * pClientData) {
    if (hDeviceUnit != 0) {
        // limit the interval to > kPositionTimerInterval
        if (LTTime_IsLessThan(interval, kPositionTimerInterval)) {
            LTLOG_DEBUG("register.interval", "Setting the interval to %lld milliseconds", LT_Ps64(LTTime_GetMilliseconds(kPositionTimerInterval)));
            interval = kPositionTimerInterval;
        }

        s_mutex->API->Lock(s_mutex);

        RotaryEncoderClientInfo * pClientInfo = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit);
        if (pClientInfo != NULL) {
            pClientInfo->hThread                    = s_iLTThread->GetCurrentThread();
            pClientInfo->pChangeProc                = pChangeProc;
            pClientInfo->pClientData                = pClientData;
            pClientInfo->tmEventInterval            = interval;
            pClientInfo->tmNextEventTime            = LTTime_Add(LT_GetCore()->GetKernelTime(), interval);
            pClientInfo->position.nPosition         = 0;
            pClientInfo->position.direction         = kRotationDirectionNone;
            pClientInfo->position.nRotationSpeed    = 0;

            s_pArrayDeviceUnits->API->Append(s_pArrayDeviceUnits, &hDeviceUnit);
            LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, pClientInfo);
        }

        s_mutex->API->Unlock(s_mutex);
    }
}

/*******************************************************************************
 * Unregister a client from position changes
*******************************************************************************/
static void LTDeviceRotaryEncoderImpl_UnregisterFromPositionChanges(LTDeviceUnit hDeviceUnit) {
    if (hDeviceUnit != 0) {
        s_mutex->API->Lock(s_mutex);

        u32 nSize = s_pArrayDeviceUnits->API->GetCount(s_pArrayDeviceUnits);
        for (u32 i = 0; i < nSize; ++i) {
            LTDeviceUnit * pDeviceUnit = s_pArrayDeviceUnits->API->Get(s_pArrayDeviceUnits, i, NULL);
            if (*pDeviceUnit == hDeviceUnit) {
                s_pArrayDeviceUnits->API->Remove(s_pArrayDeviceUnits, i);
                break;
            }
        }

        s_mutex->API->Unlock(s_mutex);
    }
}

/*******************************************************************************
 * returns number of units from the driver
*******************************************************************************/
static u32 LTDeviceRotaryEncoderImpl_GetNumDeviceUnits(void) {
    return s_nTotalNumDeviceUnits;
}

/*******************************************************************************
 * creates a handle for the given unit and create a client entry for registration
*******************************************************************************/
static LTDeviceUnit LTDeviceRotaryEncoderImpl_CreateDeviceUnitHandle(u32 nDeviceNum) {
    LTDeviceUnit hDeviceUnit = 0;

    if (nDeviceNum < s_nTotalNumDeviceUnits) {
        s_mutex->API->Lock(s_mutex);

        RotaryEncoderDevice * pDevice = s_pArrayDevices->API->Get(s_pArrayDevices, nDeviceNum, NULL);
        if (pDevice != NULL) {
            hDeviceUnit = LT_GetCore()->CreateHandle((LTInterface *)pDevice->iDriver, sizeof(RotaryEncoderClientInfo));
            if (hDeviceUnit != 0) {
                RotaryEncoderClientInfo * pClientInfo = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit);
                if (pClientInfo != NULL) {
                    lt_memset(pClientInfo, 0, sizeof(RotaryEncoderClientInfo));
                    // only need to set the device info here; the rest will be set when the client registers
                    pClientInfo->pDevice = pDevice;
                    LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, pClientInfo);
                }
            }
        }

        s_mutex->API->Unlock(s_mutex);
    }
    return hDeviceUnit;
}

/*******************************************************************************
 * name of the encoder for the given device number
*******************************************************************************/
static bool LTDeviceRotaryEncoderImpl_GetEncoderNameFromUnitNumber(u32 nDeviceUnitNumber, char const ** ppEncoderName) {
    bool bSuccess = false;

    if (ppEncoderName != NULL) {
        if (nDeviceUnitNumber < s_nTotalNumDeviceUnits) {
            RotaryEncoderDevice * pDevice = s_pArrayDevices->API->Get(s_pArrayDevices, nDeviceUnitNumber, NULL);
            if (pDevice != NULL) {
                bSuccess = pDevice->iDriver->GetEncoderNameFromUnitNumber(pDevice->nDeviceUnitNumber, ppEncoderName);
            }
        }
    }

    return bSuccess;
}

/*******************************************************************************
 * device number for the given encoder name
*******************************************************************************/
static bool LTDeviceRotaryEncoderImpl_GetUnitNumberFromEncoderName(char const * pEncoderName, u32 * pDeviceUnitNumber) {
    bool bSuccess = false;

    if (pEncoderName != NULL && pDeviceUnitNumber != NULL) {
        u32 nSize = s_pArrayDevices->API->GetCount(s_pArrayDevices);
        for (u32 i = 0; i < nSize && !bSuccess; ++i) {
            RotaryEncoderDevice * pDevice = s_pArrayDevices->API->Get(s_pArrayDevices, i, NULL);
            if (pDevice != NULL) {
                char * pName = NULL;
                if (pDevice->iDriver->GetUnitNumberFromEncoderName(pName, pDeviceUnitNumber)) {
                    bSuccess = (lt_strcasecmp(pEncoderName, pName) == 0);
                    if (bSuccess) {
                        *pDeviceUnitNumber = i;
                    }
                }
            }
        }
    }
    return bSuccess;
}

/*******************************************************************************
 * Interface definition
*******************************************************************************/
define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceRotaryEncoder) {
    .GetEncoderNameFromUnitNumber   = LTDeviceRotaryEncoderImpl_GetEncoderNameFromUnitNumber,
    .GetUnitNumberFromEncoderName   = LTDeviceRotaryEncoderImpl_GetUnitNumberFromEncoderName,
    .RegisterForPositionChanges     = LTDeviceRotaryEncoderImpl_RegisterForPositionChanges,
    .UnregisterFromPositionChanges  = LTDeviceRotaryEncoderImpl_UnregisterFromPositionChanges,
} LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  16-Nov-21   vitellius   created
 */
