/*******************************************************************************
 * platforms/linux/source/linux/driver/pushbutton/LinuxDriverPushButtonImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Linux LT Driver Library for pushbutton access
 ******************************************************************************/
/** @file LinuxDriverPushButtonImpl.c Implementation of pushbutton driver */

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/pins/LTDevicePins.h>
#include <lt/device/pushbutton/LTDevicePushButton.h>

/* clang-format off */

DEFINE_LTLOG_SECTION("linux.drv.pbtn");

static ILTThread                       * s_iThread             = NULL;
static ILTDriverPins_InputBank         * s_pIInputBank         = NULL;
static ILTDriverPins_BidirectionalBank * s_pIBidirectionalBank = NULL;
static LTDevicePins                    * s_pDevicePins         = NULL;
static LTThread_TaskProc               * s_pButtonPressEventProc   = NULL;    /* proc to queue for press events */
static LTThread_TaskProc               * s_pButtonReleaseEventProc = NULL;    /* proc to queue for release events */
static LTThread                          s_hNotificationThread;               /* thread in which to queue the event procs */
static u32                               s_nDeviceUnitIndexBase;              /* the lowest Device Unit index provided by this Driver */

/*******************************************************************************
 * Push Button descriptions                                                   */

typedef struct {
    const char                           *pName;                        /* C-string name of the button                           */
    LTDeviceUnit                          hPinBank;                     /* GPIO pin bank handle                                  */
    LTDevicePin_PinType                   bankType;                     /* Bidirectional or Input - used for interface selection */
    bool                                  bInitiallyPressed;            /* true: initially pressed                               */
    bool                                  bCurrentlyPressed;            /* true: current, debounced state                        */
    bool                                  bTimerSet;                    /* debounce timer                                        */
} PushButtonInstance;

/* Container for all the PushButton instances */
typedef struct DeviceUnits {
    PushButtonInstance *pDeviceUnits;       /* Pointer into the heap where the Device Units are stored (as an array) */
    u32                 nNumDeviceUnits;    /* How many Device Units this Driver supplies                            */
} DeviceUnits;

static DeviceUnits s_DeviceUnits;

/** Function prototypes ********************************************************/
static void PushButtonISR(bool bPinHigh, void * pClData);
static void TaskProcButton(void * pClientData);

/** Utility Functions *********************************************************/

static u32 ReadPin(PushButtonInstance * pButton) {
    if (pButton->bankType == kLTDevicePin_PinType_Input)
        return s_pIInputBank->Read(pButton->hPinBank);
    return s_pIBidirectionalBank->Read(pButton->hPinBank);
}

static void DisablePinIRQ(PushButtonInstance * pButton) {
    if (pButton->bankType == kLTDevicePin_PinType_Input) {
        s_pIInputBank->DisableIRQ(pButton->hPinBank);
    } else {
        s_pIBidirectionalBank->DisableIRQ(pButton->hPinBank);
    }
}

static void EnablePinIRQ(PushButtonInstance * pButton, void * pClientData) {
    static const LTDevicePin_PinConfiguration_Trigger edge = kLTDevicePin_PinConfiguration_Trigger_BothEdges;
    static const LTTime debounce = LTTimeInitializer_Milliseconds(100);
    if (pButton->bankType == kLTDevicePin_PinType_Input)
        s_pIInputBank->EnableIRQ(pButton->hPinBank, edge, debounce, PushButtonISR, pClientData);
    else
        s_pIBidirectionalBank->EnableIRQ(pButton->hPinBank, edge, debounce, PushButtonISR, pClientData);
}

/** Callback Functions ********************************************************/
/********************************************************************************************************************************
 * The Button-press/release ISR.  Encode the button index in the client data pointer:                                          */
static void PushButtonISR(bool bPinHigh, void * pClData) { LT_UNUSED(bPinHigh);
    u32 nPushButtonIndex = (u32)((LT_SIZE)pClData);
    if (nPushButtonIndex >= s_DeviceUnits.nNumDeviceUnits) return;
    if (s_hNotificationThread) {
        s_iThread->QueueTaskProcIfRequired(s_hNotificationThread, TaskProcButton, NULL, pClData);
    }
}

/** Driver implementation *********************************************************/

/********************************************************************************************************************************
 * Access to Device Unit numbers and names:                                                                                    */

static bool LinuxDriverPushButton_GetPushButtonNameFromIndex(u32 nIndex, char * pPushButtonNameToSet, LT_SIZE nStringSizeBytes) {
    if (nIndex < s_DeviceUnits.nNumDeviceUnits) {
        lt_strncpyTerm(pPushButtonNameToSet, (s_DeviceUnits.pDeviceUnits + nIndex)->pName, nStringSizeBytes);
        return true;
    }
    return false;
}

static bool LinuxDriverPushButton_GetPushButtonIndexFromName(char const * pPushButtonName, u32 * pIndexToSet) {
    PushButtonInstance * pButton = s_DeviceUnits.pDeviceUnits;
    for (u32 n = 0; n < s_DeviceUnits.nNumDeviceUnits; ++n, ++pButton)
        if (!lt_strcmp(pPushButtonName, pButton->pName)) {
            *pIndexToSet = n;
            return true;
        }
    return false;
}

/********************************************************************************************************************************
 * Connection to the Device layer.
 * As the Driver will likely collect button press and release events through an interrupt service routine, its notification
 * of the Device layer must be lightweight; connection with the Device (through the (*Connect)() method) specifies the thread
 * and press/release procs resident in the driver that receive these light-weight notifications through QueueTaskProc().  That
 * thread, running outside the interrupt context, is free to notify any and all threads which are currently subscribed to
 * button-press and -release events:                                                                                           */

static void LinuxDriverPushButton_Connect(LTThread hThread, LTThread_TaskProc * pPressDispatchProc, LTThread_TaskProc * pReleaseDispatchProc, u32 nDeviceUnitIndexBase) {
    s_hNotificationThread = hThread;
    s_pButtonPressEventProc = pPressDispatchProc;
    s_pButtonReleaseEventProc = pReleaseDispatchProc;
    s_nDeviceUnitIndexBase = nDeviceUnitIndexBase;
}



/********************************************************************************************************************************
 * Current PushButton debounced state.                                                                                         */

static bool LinuxDriverPushButton_IsButtonPressed(u32 nIndex) {
    if (nIndex >= s_DeviceUnits.nNumDeviceUnits) return false;
    return (s_DeviceUnits.pDeviceUnits + nIndex)->bCurrentlyPressed;
}

static void LinuxDriverPushButton_Disconnect(void) {
    s_hNotificationThread = 0;
    s_pButtonPressEventProc = s_pButtonReleaseEventProc = NULL;
}

static void DebounceTimerProc(void * pClientData) {
    LTThread hThread = s_iThread->GetCurrentThread();
    s_iThread->KillTimer(hThread, &DebounceTimerProc, pClientData);
    u32 nPushButtonIndex = (u32)((LT_SIZE)pClientData);
    PushButtonInstance * pButton = s_DeviceUnits.pDeviceUnits + nPushButtonIndex;
    pButton->bTimerSet = false;

    bool bCurrentlyHigh = ReadPin(pButton);
    pButton->bCurrentlyPressed = bCurrentlyHigh;
    LTThread_TaskProc *pTaskProc =  bCurrentlyHigh ?
                                    s_pButtonPressEventProc :
                                    s_pButtonReleaseEventProc;
    if (pTaskProc) pTaskProc((void *)((LT_SIZE)(nPushButtonIndex + s_nDeviceUnitIndexBase)));
}

static void TaskProcButton(void * pClientData) {
    LTThread hThread = s_iThread->GetCurrentThread();
    u32 nPushButtonIndex = (u32)((LT_SIZE)pClientData);
    PushButtonInstance * pButton = s_DeviceUnits.pDeviceUnits + nPushButtonIndex;
    if (!pButton->bTimerSet) {
        pButton->bTimerSet = true;
        s_iThread->SetTimer(hThread, LTTime_Milliseconds(100), DebounceTimerProc, NULL, (void *)((LT_SIZE)nPushButtonIndex));
    }
}

/********************************************************************************************************************************
 * Device-unit creation interface.
 * Currently, LTDevicePushButton does not use the Device Unit interface, but once Event notifications are able to occur in an
 * interrupt context, that may change.                                                                                         */
static u32 LinuxDriverPushButtonImpl_GetNumDeviceUnits(void) { return s_DeviceUnits.nNumDeviceUnits; }

/* No handles, single context */
static LTDeviceUnit LinuxDriverPushButtonImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) { LT_UNUSED(nDeviceUnitNumber); return 0; }

/********************************************************************************************************************************
 * GPIO initialization                                                                                                         */

static bool InitializeDeviceUnit(PushButtonInstance *pButton, u32 nBankNumber, LTDevicePin_PinConfiguration_PullType pullType) {
    LTDevicePin_PinType PinType = kLTDevicePin_PinType_Invalid;
    if (!s_pDevicePins->GetBankTypeFromUnitNumber(nBankNumber, &PinType)) {
        LTLOG_REDALERT("fail.no.bank.type", "Unable to obtain bank type");
        return false;
    }
    if (PinType != kLTDevicePin_PinType_Input && PinType != kLTDevicePin_PinType_Bidirectional) {
        LTLOG_REDALERT("fail.inval.bank.type", "Invalid bank type");
        return false;
    }
    pButton->bankType = PinType;
    if (!(pButton->hPinBank = s_pDevicePins->CreateDeviceUnitHandle(nBankNumber))) {
        LTLOG_REDALERT("fail.bank.handle", "Unable to obtain bank handle");
        return false;
    }
    if (PinType == kLTDevicePin_PinType_Input) {
        if (!s_pIInputBank)
            s_pIInputBank = lt_gethandleinterface(ILTDriverPins_InputBank, pButton->hPinBank);
        if (!s_pIInputBank) {
            LTLOG_REDALERT("fail.configure.input", "Unable to configure input bank");
            return false;
        }
    } else {
        if (!s_pIBidirectionalBank)
            s_pIBidirectionalBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, pButton->hPinBank);
        if (!s_pIBidirectionalBank) {
            LTLOG_REDALERT("fail.configure.bidirectional", "Unable to configure bidirectional bank");
            return false;
        }
        s_pIBidirectionalBank->ConfigureAsInput(pButton->hPinBank, pullType);
    }

    bool bInitiallyHigh = ReadPin(pButton);
    pButton->bInitiallyPressed = bInitiallyHigh;
    EnablePinIRQ(pButton, (void*)(pButton - s_DeviceUnits.pDeviceUnits));
    return true;
}

/********************************************************************************************************************************
 * Device Unit configuration                                                                                                   */

/* Data structure containing the entire context for initialization: */
typedef struct LinuxDriverPushButtonConfigContext {
    LTDeviceConfig       *pDeviceConfig;
    PushButtonInstance   *pNextInstanceToInitialize;       /* pointer to next place in s_DeviceUnits.pDeviceUnits to place a Device Unit instance */
    u32                   driverConfigOffset;              /* The Device Config offset for this Driver */
    u32                   nInitialNumDeviceUnitInstances;  /* number of Device units in the Device Config for this Driver Library */
} LinuxDriverPushButtonConfigContext;

/* Reclaim all resources used by the context: */
static LinuxDriverPushButtonConfigContext *DestroyConfigurationContext(LinuxDriverPushButtonConfigContext *pContext) {
    if (pContext) {
        lt_closelibrary(pContext->pDeviceConfig);
        lt_free(pContext);
    }
    return NULL;
}

/* Allocate memory for the context, and fill in as much as possible: */
static LinuxDriverPushButtonConfigContext *CreateConfigurationContext(void) {
    LinuxDriverPushButtonConfigContext *pContext = lt_malloc(sizeof(LinuxDriverPushButtonConfigContext));
    if (   !pContext
        || !(pContext->pDeviceConfig                  = lt_openlibrary(LTDeviceConfig))
        || !(pContext->driverConfigOffset             = pContext->pDeviceConfig->GetDriverSection("LTDevicePushButton", "LinuxDriverPushButton"))
        || !(pContext->nInitialNumDeviceUnitInstances = pContext->pDeviceConfig->GetNumDeviceUnits(pContext->driverConfigOffset))) return DestroyConfigurationContext(pContext);
    return pContext;
}

/* Configure one Device Unit instance from the configuration information in the Device Config.
   If the Device Unit instance is successfully configured, advance the instance pointer to the next instance,
   and increment the number of available Device Units.  Otherwise, leave the pointer and count alone, and the
   next iteration of the callback will rewrite the current instance (if that happens, ConfigureDeviceUnits()
   reallocs the Device Unit instance array to trim off the unused portion).
   Return true upon success, false if any part of obtaining the configuration information from the Device
   Config fails (this causes the loop iterating on Device Units to terminate early): */
static bool ConfigureDeviceUnit(LinuxDriverPushButtonConfigContext *pContext, u32 nDeviceUnitIndex) {
    if (s_DeviceUnits.nNumDeviceUnits >= pContext->nInitialNumDeviceUnitInstances) {
        /* All the allocated Device Unit instance storage has already been written.  Ignore any additional configurations: */
        LTLOG_REDALERT("cdu.config.excess", NULL);
        return false;
    }
    PushButtonInstance *pInstance = pContext->pNextInstanceToInitialize;
    u32 deviceUnitSection = pContext->pDeviceConfig->GetDeviceUnitSectionAt(pContext->driverConfigOffset, nDeviceUnitIndex);
    if (!deviceUnitSection) {
        LTLOG_YELLOWALERT("cdu.no", NULL);
        return false;
    }
    LTResourceValue duPinNameValue;
    if (!LT_GetCore()->ReadResourceValue(pContext->pDeviceConfig->GetResourceTree(), deviceUnitSection, "pin", &duPinNameValue)) {
        LTLOG_YELLOWALERT("cdu.pin.no", NULL);
        return false;
    }
    if (duPinNameValue.type != kLTResourceValueType_String) {
        LTLOG_YELLOWALERT("cdu.pin.type", NULL);
        return false;
    }
    u32 nPinDeviceUnitIndex = LT_U32_MAX;
    if (!s_pDevicePins->GetUnitNumberFromBankName(duPinNameValue.string, &nPinDeviceUnitIndex)) {
        LTLOG_YELLOWALERT("cdu.pin.nf", NULL);
        return false;
    }
    if (!(pInstance->pName = pContext->pDeviceConfig->ReadString(deviceUnitSection, "name"))) {
        LTLOG_YELLOWALERT("cdu.name", NULL);
        return false;
    }

    /* Default to no pull */
    LTDevicePin_PinConfiguration_PullType pullType = kLTDevicePin_PinConfiguration_PullType_NoPull;

    if (!InitializeDeviceUnit(pInstance, nPinDeviceUnitIndex, pullType)) {
        LTLOG_YELLOWALERT("cdu.init", "init of LED \"%s\" failed", pInstance->pName);
        return false;
    } else {
        /* Successfully configured this Device Unit instance.
           Count it and advance to the next place in the Device Unit instance array: */
        ++pContext->pNextInstanceToInitialize;
        ++s_DeviceUnits.nNumDeviceUnits;
        LTLOG_DEBUG("cdu", "pin %lu: \"%s\"", LT_Pu32(nPinDeviceUnitIndex), pInstance->pName);
    }
    return true;
}

static bool ConfigureDeviceUnits(void) {
    if (s_DeviceUnits.nNumDeviceUnits || s_DeviceUnits.pDeviceUnits) return false;   /* already configured - do not allocate and configure again */
    LinuxDriverPushButtonConfigContext *pContext = CreateConfigurationContext();
    if (!pContext) {
        LTLOG_REDALERT("cdus.context", NULL);
        return false;
    }
    LT_SIZE nInstanceStorageBytes = pContext->nInitialNumDeviceUnitInstances * sizeof(PushButtonInstance);
    if (!(s_DeviceUnits.pDeviceUnits = pContext->pNextInstanceToInitialize = lt_malloc(nInstanceStorageBytes))) {
        LTLOG_REDALERT("cdus.oom", NULL);
        return DestroyConfigurationContext(pContext);
    }
    lt_memset(s_DeviceUnits.pDeviceUnits, 0, nInstanceStorageBytes);        /* cleanliness is next to godliness */
    for (u32 i = 0; i < pContext->nInitialNumDeviceUnitInstances && ConfigureDeviceUnit(pContext, i); ++i);
    if (!s_DeviceUnits.nNumDeviceUnits || s_DeviceUnits.nNumDeviceUnits > pContext->nInitialNumDeviceUnitInstances) {
        LTLOG_REDALERT("du.n.fault", "expected %lu; actual %lu", LT_Pu32(pContext->nInitialNumDeviceUnitInstances), LT_Pu32(s_DeviceUnits.nNumDeviceUnits));
        return DestroyConfigurationContext(pContext);
    } else if (s_DeviceUnits.nNumDeviceUnits < pContext->nInitialNumDeviceUnitInstances) {
        /* Ended up with fewer Device Units than expected.  Give back some memory and attempt to carry on: */
        LTLOG_YELLOWALERT("du.n.fewer", "expected %lu; actual %lu", LT_Pu32(pContext->nInitialNumDeviceUnitInstances), LT_Pu32(s_DeviceUnits.nNumDeviceUnits));
        lt_realloc(s_DeviceUnits.pDeviceUnits, s_DeviceUnits.nNumDeviceUnits * sizeof(PushButtonInstance));
    } else {
        LTLOG_DEBUG("du.n", "%lu", LT_Pu32(s_DeviceUnits.nNumDeviceUnits));
    }
    DestroyConfigurationContext(pContext);
    return true;
}

/********************************************************************************************************************************
 * Library finalization or bailure                                                                                             */
static bool Shutdown(LinuxDriverPushButtonConfigContext *pContext) {
    DestroyConfigurationContext(pContext);
    if (s_DeviceUnits.pDeviceUnits) {
        PushButtonInstance * pButton = s_DeviceUnits.pDeviceUnits;
        for (u32 i = s_DeviceUnits.nNumDeviceUnits; i; --i, ++pButton) {
            if (pButton->hPinBank) {
                DisablePinIRQ(pButton);
                LT_GetCore()->DestroyHandle(pButton->hPinBank);
            }
        }
        lt_free(s_DeviceUnits.pDeviceUnits);
        s_DeviceUnits.pDeviceUnits = NULL;
    }
    s_DeviceUnits.nNumDeviceUnits = 0;
    lt_closelibrary(s_pDevicePins); s_pDevicePins = NULL;
    return NULL;
}

/********************************************************************************************************************************
 * Library initialization and finalization                                                                                     */
static bool LinuxDriverPushButtonImpl_LibInit(void) {
    return (   (s_pDevicePins = (LTDevicePins *)LT_GetCore()->OpenLibrary("LTDevicePins"))
            && (s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()))
            && ConfigureDeviceUnits()) ? true : Shutdown(NULL);
}

static void LinuxDriverPushButtonImpl_LibFini(void) { Shutdown(NULL); }

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDevicePushButton, LinuxDriverPushButton);

define_LTLIBRARY_INTERFACE(ILTDriverPushButton)
    .GetPushButtonNameFromIndex = LinuxDriverPushButton_GetPushButtonNameFromIndex,
    .GetPushButtonIndexFromName = LinuxDriverPushButton_GetPushButtonIndexFromName,
    .Connect                    = LinuxDriverPushButton_Connect,
    .Disconnect                 = LinuxDriverPushButton_Disconnect,
    .IsButtonPressed            = LinuxDriverPushButton_IsButtonPressed
LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LinuxDriverPushButton, (ILTDriverPushButton))
