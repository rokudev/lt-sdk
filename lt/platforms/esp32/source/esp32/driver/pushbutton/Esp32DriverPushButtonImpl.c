/*******************************************************************************
 * platforms/esp32/source/esp32/driver/pushbutton/Esp32DriverPushButtonImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Esp32 LT Driver Library for pushbutton access
 ******************************************************************************/
/** @file Esp32DriverPushButtonImpl.c Implementation of pushbutton driver */

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/pushbutton/LTDevicePushButton.h>
#include <lt/device/pins/LTDevicePins.h>

DEFINE_LTLOG_SECTION("esp32.drv.pbtn");

static ILTThread                       * s_iThread             = NULL;
static ILTDriverPins_InputBank         * s_pIInputBank         = NULL;
static ILTDriverPins_BidirectionalBank * s_pIBidirectionalBank = NULL;
static LTDevicePins                    * s_pDevicePins         = NULL;

/*******************************************************************************
 * Push Button descriptions                                                   */

typedef struct {
    const char                           *pName;                        /* C-string name of the button                           */
    LTDeviceUnit                          hPinBank;                     /* GPIO pin bank handle                                  */
    LTDevicePin_PinType                   bankType;                     /* Bidirectional or Input - used for interface selection */
    bool                                  bActiveHigh;                  /* true: active-high                                     */
    bool                                  bInitiallyPressed;            /* true: initially pressed                               */
    bool                                  bCurrentlyPressed;            /* true: current, debounced state                        */
    bool                                  bTimerSet;                    /* debounce timer                                        */
    bool                                  bWasHighAtIRQ;                /* true if input was high at the interrupt               */
} PushButtonInstance;

LT_STATIC_ASSERT_SIZE_32_64(PushButtonInstance, 20, 28);

/* Container for all the PushButton instances */
typedef struct DeviceUnits {
    PushButtonInstance *pDeviceUnits;       /* Pointer into the heap where the Device Units are stored (as an array) */
    u32                 nNumDeviceUnits;    /* How many Device Units this Driver supplies                            */
} DeviceUnits;

LT_STATIC_ASSERT_SIZE_32_64(DeviceUnits, 8, 12);

/********************************************************************************************************************************
 * Debounce interval - this value reflects a tradeoff between button responsiveness, contact bounce, and the seemingly high
 * susceptibility of the SoC circuit (including GPIO inputs used for PushButtons) to noise induced from nearby high voltage
 * circuits (observed on the Indoor Plug).                                                                                     */
static const LTTime s_debounceInterval = LTTimeInitializer_Milliseconds(100);

static DeviceUnits s_DeviceUnits;

/********************************************************************************************************************************
 * Access to Device Unit numbers and names:                                                                                    */

static bool Esp32DriverPushButton_GetPushButtonNameFromIndex(u32 nIndex, char * pPushButtonNameToSet, LT_SIZE nStringSizeBytes) {
    if (nIndex < s_DeviceUnits.nNumDeviceUnits) {
        lt_strncpyTerm(pPushButtonNameToSet, (s_DeviceUnits.pDeviceUnits + nIndex)->pName, nStringSizeBytes);
        return true;
    }
    return false;
}

static bool Esp32DriverPushButton_GetPushButtonIndexFromName(char const * pPushButtonName, u32 * pIndexToSet) {
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

static LTThread_TaskProc * s_pButtonPressEventProc   = NULL;    /* proc to queue for press events */
static LTThread_TaskProc * s_pButtonReleaseEventProc = NULL;    /* proc to queue for release events */
static LTThread            s_hNotificationThread;               /* thread in which to queue the event procs */
static u32                 s_nDeviceUnitIndexBase;              /* the lowest Device Unit index provided by this Driver */

static void Esp32DriverPushButton_Connect(LTThread hThread, LTThread_TaskProc * pPressDispatchProc, LTThread_TaskProc * pReleaseDispatchProc, u32 nDeviceUnitIndexBase) {
    s_hNotificationThread = hThread;
    s_pButtonPressEventProc = pPressDispatchProc;
    s_pButtonReleaseEventProc = pReleaseDispatchProc;
    s_nDeviceUnitIndexBase = nDeviceUnitIndexBase;
}

/********************************************************************************************************************************
 * Differentiation between Input Banks and Bidirectional Banks.
 * Allow ease of access of either type of Bank by moving all the input-or-bidirectional logic to here.
 * These functions have the PRECONDITION that the type is either input or bidirectional;  The validity of the Bank (as either
 * input or bidirectional - associating an output Bank with a PushButton wouldn't make any sense) is established in
 * InitializeDeviceUnit().                                                                                                     */

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

static void PushButtonISR(bool bPinHigh, void * pClData);

static void EnablePinIRQ(PushButtonInstance * pButton) {
    static const LTDevicePin_PinConfiguration_Trigger edge = kLTDevicePin_PinConfiguration_Trigger_BothEdges;
    static const LTTime debounce = LTTimeInitializer_Zero();    /* ESP32 does not support debounce */
    if (pButton->bankType == kLTDevicePin_PinType_Input)
        s_pIInputBank->EnableIRQ(pButton->hPinBank, edge, debounce, PushButtonISR, pButton);
    else
        s_pIBidirectionalBank->EnableIRQ(pButton->hPinBank, edge, debounce, PushButtonISR, pButton);
}

/********************************************************************************************************************************
 * Current PushButton debounced state.                                                                                         */

static bool Esp32DriverPushButton_IsButtonPressed(u32 nIndex) {
    if (nIndex >= s_DeviceUnits.nNumDeviceUnits) return false;
    return (s_DeviceUnits.pDeviceUnits + nIndex)->bCurrentlyPressed;
}

static void Esp32DriverPushButton_Disconnect(void) {
    s_hNotificationThread = 0;
    s_pButtonPressEventProc = s_pButtonReleaseEventProc = NULL;
}

/********************************************************************************************************************************
 * Debounce logic.                                                                                                             */

/* DebounceTimerProc() enforces that client notifications (through (*pTaskProc)()) are of alternating presses and releases.
   This rejects a particular kind of glitch (often associated with electrical noise) which only generates one interrupt
   (not two, one for each level as a normal pulse would). */
static void DebounceTimerProc(void * pClientData) {
    LTThread hThread = s_iThread->GetCurrentThread();
    s_iThread->KillTimer(hThread, &DebounceTimerProc, pClientData);
    PushButtonInstance * pButton = pClientData;
    pButton->bTimerSet = false;
    bool bCurrentlyHigh = ReadPin(pButton);
    if (pButton->bWasHighAtIRQ != bCurrentlyHigh) return; /* reject a glitch */
    bool bCurrentlyPressed = bCurrentlyHigh == pButton->bActiveHigh;
    if (bCurrentlyPressed != pButton->bCurrentlyPressed) {  /* reject one-sided interrupts */
        LTThread_TaskProc *pTaskProc =   (pButton->bCurrentlyPressed = bCurrentlyPressed)
                                       ? s_pButtonPressEventProc
                                       : s_pButtonReleaseEventProc;
        if (pTaskProc) pTaskProc((void *)(pButton - s_DeviceUnits.pDeviceUnits + s_nDeviceUnitIndexBase));
    }
}

static void TaskProcButton(void * pClientData) {
    LTThread hThread = s_iThread->GetCurrentThread();
    /* See the comment for PushButtonISR() regarding the use of the LSB for the state of the PushButton GPIO Input. */
    PushButtonInstance * pButton = (PushButtonInstance *)((u32)pClientData & ~1);
    if (!pButton->bTimerSet) {
        pButton->bWasHighAtIRQ = (u32)pClientData & 1;
        pButton->bTimerSet = true;
        s_iThread->SetTimer(hThread, s_debounceInterval, DebounceTimerProc, NULL, pButton);
    }
}

/********************************************************************************************************************************
 * The Button-press/release ISR.  Encode the button index in the client data pointer.
       Set the LSB if the input was high at the time of the interrupt.  This is used later in the debounce logic in order to
       reject glitches (defined as the input being in the opposite state of the level which triggered the interrupt at the
       end of the debounce interval).
       Passing the state as a hitchhiker in a pointer avoids the trouble of having to allocate memory to hold the state
       (anywhere, including in static storage).  Besides, static storage (per button instance) can't be used because a glitch
       that generates two interrupts (one in one state and the next in the other state) would defeat the debounce logic.
       This tactic assumes that all pointers to client data have at least two-byte alignment (making the MSB insignificant);
       all current and anticipated architectures associated with this platform would enforce at least word alignment (four
       bytes), so it seems safe to employ the tactic.
       QueueTaskProcIfRequired() will only fire once per TaskProc/ReleaseProc/ClientData triplet, if a TaskProc with that
       triplet is already in the queue.  That means that rapid-fire alternating interrupts (high-low-high-low, as would be
       triggered by lots of bounce or ringing) will queue two TaskProcs instead the normal one.  The logic in TaskProcButton()
       will reject the second one.                                                                                             */
static void PushButtonISR(bool bPinHigh, void * pClientData) {
    if (bPinHigh) pClientData = (void *)((u32)pClientData | 1); /* input was high at the time of the interrupt */
    if (s_hNotificationThread) s_iThread->QueueTaskProcIfRequired(s_hNotificationThread, TaskProcButton, NULL, pClientData);
}

/********************************************************************************************************************************
 * Device-unit creation interface.
 * Currently, LTDevicePushButton does not use the Device Unit interface, but once Event notifications are able to occur in an
 * interrupt context, that may change.                                                                                         */
static u32 Esp32DriverPushButtonImpl_GetNumDeviceUnits(void) { return s_DeviceUnits.nNumDeviceUnits; }

static LTDeviceUnit Esp32DriverPushButtonImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) { LT_UNUSED(nDeviceUnitNumber); return 0; }

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
        s_pIInputBank->ConfigurePullType(pButton->hPinBank, pullType);
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
    pButton->bInitiallyPressed = pButton->bCurrentlyPressed = bInitiallyHigh == pButton->bActiveHigh;
    EnablePinIRQ(pButton);
    return true;
}

/********************************************************************************************************************************
 * Device Unit configuration                                                                                                   */

/* Data structure containing the entire context for initialization: */
typedef struct Esp32DriverPushButtonConfigContext {
    LTDeviceConfig       *pDeviceConfig;
    PushButtonInstance   *pNextInstanceToInitialize;       /* pointer to next place in s_DeviceUnits.pDeviceUnits to place a Device Unit instance */
    u32                   driverConfigOffset;              /* The Device Config offset for this Driver */
    u32                   nInitialNumDeviceUnitInstances;  /* number of Device units in the Device Config for this Driver Library */
} Esp32DriverPushButtonConfigContext;

LT_STATIC_ASSERT_SIZE_32_64(Esp32DriverPushButtonConfigContext, 16, 24);

/* Reclaim all resources used by the context: */
static Esp32DriverPushButtonConfigContext *DestroyConfigurationContext(Esp32DriverPushButtonConfigContext *pContext) {
    if (pContext) {
        lt_closelibrary(pContext->pDeviceConfig);
        lt_free(pContext);
    }
    return NULL;
}

/* Allocate memory for the context, and fill in as much as possible: */
static Esp32DriverPushButtonConfigContext *CreateConfigurationContext(void) {
    Esp32DriverPushButtonConfigContext *pContext = lt_malloc(sizeof(Esp32DriverPushButtonConfigContext));
    if (   !pContext
        || !(pContext->pDeviceConfig                  = lt_openlibrary(LTDeviceConfig))
        || !(pContext->driverConfigOffset             = pContext->pDeviceConfig->GetDriverSection("LTDevicePushButton", "Esp32DriverPushButton"))
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
static bool ConfigureDeviceUnit(Esp32DriverPushButtonConfigContext *pContext, u32 nDeviceUnitIndex) {
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
    const char *pActive = pContext->pDeviceConfig->ReadString(deviceUnitSection, "active");
    pInstance->bActiveHigh = true;      /* default to active-high if "active" attribute is bad or missing */
    if (pActive) {
             if (!lt_strcmp(pActive, "low" )) pInstance->bActiveHigh = false;
        else if ( lt_strcmp(pActive, "high")) LTLOG_YELLOWALERT("cdu.pin.active", "\"%s\" invalid", pActive);
    }
    const char *pPull = pContext->pDeviceConfig->ReadString(deviceUnitSection, "pull");
    /* Default to no pull if "pull" attribute is bad or missing: */
    LTDevicePin_PinConfiguration_PullType pullType = kLTDevicePin_PinConfiguration_PullType_NoPull;
    if (pPull) {
             if (!lt_strcmp(pPull, "up"))   pullType = kLTDevicePin_PinConfiguration_PullType_PullUp;
        else if (!lt_strcmp(pPull, "down")) pullType = kLTDevicePin_PinConfiguration_PullType_PullDown;
        else if ( lt_strcmp(pPull, "none")) LTLOG_YELLOWALERT("cdu.pin.pull", "\"%s\" invalid", pPull);
    }
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
    Esp32DriverPushButtonConfigContext *pContext = CreateConfigurationContext();
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

static bool Shutdown(Esp32DriverPushButtonConfigContext *pContext) {
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

static bool Esp32DriverPushButtonImpl_LibInit(void) {
    LTLOG_DEBUG("init", NULL);
    return (   (s_pDevicePins = (LTDevicePins *)LT_GetCore()->OpenLibrary("LTDevicePins"))
            && (s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()))
            && ConfigureDeviceUnits()) ? true : Shutdown(NULL);
}

static void Esp32DriverPushButtonImpl_LibFini(void) { LTLOG_DEBUG("fini", NULL); Shutdown(NULL); }

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDevicePushButton, Esp32DriverPushButton);

define_LTLIBRARY_INTERFACE(ILTDriverPushButton)
    .GetPushButtonNameFromIndex = Esp32DriverPushButton_GetPushButtonNameFromIndex,
    .GetPushButtonIndexFromName = Esp32DriverPushButton_GetPushButtonIndexFromName,
    .Connect                    = Esp32DriverPushButton_Connect,
    .Disconnect                 = Esp32DriverPushButton_Disconnect,
    .IsButtonPressed            = Esp32DriverPushButton_IsButtonPressed
LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(Esp32DriverPushButton, (ILTDriverPushButton))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  14-Nov-22   constantine created using CommonDriverPushButton
 *  20-Apr-23   constantine converted to the new Device Config Arbolation hotness
 */
