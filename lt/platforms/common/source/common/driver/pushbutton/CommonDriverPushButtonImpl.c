/*******************************************************************************
 * platforms/common/source/common/driver/pushbutton/CommonDriverPushButtonImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Common LT Driver Library for pushbutton access
 *
 ******************************************************************************/
/** @file CommonDriverPushButtonImpl.c Implementation of pushbutton driver */

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/pushbutton/LTDevicePushButton.h>
#include <lt/device/pins/LTDevicePins.h>

DEFINE_LTLOG_SECTION("lt.drv.pushbutton");

static ILTThread                       * s_iThread             = NULL;
static ILTDriverPins_InputBank         * s_pIInputBank         = NULL;
static ILTDriverPins_BidirectionalBank * s_pIBidirectionalBank = NULL;
static LTDevicePins                    * s_pDevicePins         = NULL;

/*******************************************************************************
 * Push Button descriptions                                                   */

typedef struct {
    char const          * name;              /* C-string name of the button     */
    char const          * pinName;           /* C-string name of the gpio-pin   */
    LTDeviceUnit          hPinBank;          /* GPIO pin bank handle            */
    LTDevicePin_PinType   bankType;          /* Bidirectional or Input          */
    LTDevicePin_PinConfiguration_PullType pullType; /* pull-up, pull-down, or no pull */
    bool                  bActiveHigh;       /* true: active-high               */
    bool                  bInitiallyPressed; /* true: initially pressed         */
    bool                  bCurrentlyPressed; /* true: current, debounced state  */
    bool                  bTimerSet;         /* debounce timer                  */
} PushButtonInstance;

static PushButtonInstance s_PushButtons[] = {
    #define PUSHBUTTONINSTANCE(buttonName, PinName, buttonActiveHigh) { .name = #buttonName, .pinName = #PinName, .bActiveHigh = buttonActiveHigh, .pullType = kLTDevicePin_PinConfiguration_PullType_NoPull },
    #define PUSHBUTTONINSTANCEWITHPULL(buttonName, PinName, buttonActiveHigh, pinPullType) { .name = #buttonName, .pinName = #PinName, .bActiveHigh = buttonActiveHigh, .pullType = pinPullType },
    #include LT_STRINGIFY(CONFIGURATION_FILE)
    #undef PUSHBUTTONINSTANCE
};

enum { kNumPushButtons = sizeof s_PushButtons / sizeof (PushButtonInstance) };

/********************************************************************************************************************************
 * Access to Device Unit numbers and names:                                                                                    */

static bool CommonDriverPushButton_GetPushButtonNameFromIndex(u32 nIndex, char * pPushButtonNameToSet, LT_SIZE nStringSizeBytes) {
    if (nIndex < kNumPushButtons) {
        lt_strncpyTerm(pPushButtonNameToSet, s_PushButtons[nIndex].name, nStringSizeBytes);
        return true;
    }
    return false;
}

static bool CommonDriverPushButton_GetPushButtonIndexFromName(char const * pPushButtonName, u32 * pIndexToSet) {
    PushButtonInstance * pButton = s_PushButtons;
    for (u32 n = 0; n < kNumPushButtons; ++n, ++pButton)
        if (!lt_strcmp(pPushButtonName, pButton->name)) {
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

static void CommonDriverPushButton_Connect(LTThread hThread, LTThread_TaskProc * pPressDispatchProc,
                                                             LTThread_TaskProc * pReleaseDispatchProc,
                                                             u32 nDeviceUnitIndexBase) {
    s_hNotificationThread = hThread;
    s_pButtonPressEventProc = pPressDispatchProc;
    s_pButtonReleaseEventProc = pReleaseDispatchProc;
    s_nDeviceUnitIndexBase = nDeviceUnitIndexBase;
}

/********************************************************************************************************************************
 * Differentiation between Input Banks and Bidirectional Banks.
 * Allow ease of access of either type of Bank by moving all the input-or-bidirectional logic to here.
 * The validity of the Bank (as either input or bidirectional - associating an output Bank with a PushButton wouldn't make any
 * sense) is done in InitGPIO(), so these functions have the PRECONDITION that the type is either input or bidirectional.      */

static u32 ReadPin(PushButtonInstance * pButton) {
    if (pButton->bankType == kLTDevicePin_PinType_Input)
        return s_pIInputBank->Read(pButton->hPinBank);
    return s_pIBidirectionalBank->Read(pButton->hPinBank);
}

static void DisablePinIRQ(PushButtonInstance * pButton) {
    if (pButton->bankType == kLTDevicePin_PinType_Input)
        s_pIInputBank->DisableIRQ(pButton->hPinBank);
    else
        s_pIBidirectionalBank->DisableIRQ(pButton->hPinBank);
}

static void PushButtonISR(bool bPinHigh, void * pClData);

static void EnablePinIRQ(PushButtonInstance * pButton, void * pClientData) {
    static const LTDevicePin_PinConfiguration_Trigger edge = kLTDevicePin_PinConfiguration_Trigger_BothEdges;
    static const LTTime debounce = LTTimeInitializer_Milliseconds(100);
    if (pButton->bankType == kLTDevicePin_PinType_Input)
        s_pIInputBank->EnableIRQ(pButton->hPinBank, edge, debounce, PushButtonISR, pClientData);
    else
        s_pIBidirectionalBank->EnableIRQ(pButton->hPinBank, edge, debounce, PushButtonISR, pClientData);
}

/********************************************************************************************************************************
 * Current PushButton debounced state.                                                                                         */

static bool CommonDriverPushButton_IsButtonPressed(u32 nIndex) {
    if (nIndex >= kNumPushButtons) return false;
    return s_PushButtons[nIndex].bCurrentlyPressed;
}

static void CommonDriverPushButton_Disconnect(void) {
    s_hNotificationThread = 0;
    s_pButtonPressEventProc = s_pButtonReleaseEventProc = NULL;
}

static void DebounceTimerProc(void * pClientData) {
    LTThread hThread = s_iThread->GetCurrentThread();
    s_iThread->KillTimer(hThread, &DebounceTimerProc, pClientData);
    u32 nPushButtonIndex = (u32) pClientData;
    PushButtonInstance * pButton = s_PushButtons + nPushButtonIndex;
    pButton->bTimerSet = false;

    bool bCurrentlyHigh = ReadPin(pButton);
    LTThread_TaskProc *pTaskProc =   (pButton->bCurrentlyPressed = bCurrentlyHigh == pButton->bActiveHigh)
                                   ? s_pButtonPressEventProc
                                   : s_pButtonReleaseEventProc;
    if (pTaskProc) pTaskProc((void *)(nPushButtonIndex + s_nDeviceUnitIndexBase));
}

static void TaskProcButton(void * pClientData) {
    LTThread hThread = s_iThread->GetCurrentThread();
    u32 nPushButtonIndex = (u32) pClientData;
    PushButtonInstance * pButton = s_PushButtons + nPushButtonIndex;
    if (!pButton->bTimerSet) {
        pButton->bTimerSet = true;
        s_iThread->SetTimer(hThread, LTTime_Milliseconds(100), DebounceTimerProc, NULL, (void *)nPushButtonIndex);
    }
}

/********************************************************************************************************************************
 * The Button-press/release ISR.  Encode the button index in the client data pointer:                                          */
static void PushButtonISR(bool bPinHigh, void * pClData) { LT_UNUSED(bPinHigh);
    u32 nPushButtonIndex = (u32) pClData;
    if (nPushButtonIndex >= kNumPushButtons) return;
    if (s_hNotificationThread) {
        s_iThread->QueueTaskProc(s_hNotificationThread, TaskProcButton, NULL, pClData);
    }
}

/********************************************************************************************************************************
 * Platform-specific GPIO initialization and input                                                                             */

static bool InitGPIO(void) {
    PushButtonInstance * pButton = s_PushButtons;
    for (u32 i = 0; i < kNumPushButtons; ++i, ++pButton) {
        u32 nBankNumber = LT_U32_MAX;
        LTDevicePin_PinType PinType = kLTDevicePin_PinType_Invalid;
        if (!s_pDevicePins->GetUnitNumberFromBankName(pButton->pinName, &nBankNumber)) {
            LTLOG_REDALERT("fail.no.bank.number", "Unable to obtain bank number");
            return false;
        }
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
            s_pIInputBank->ConfigurePullType(pButton->hPinBank, pButton->pullType);
        } else {
            if (!s_pIBidirectionalBank)
                s_pIBidirectionalBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, pButton->hPinBank);
            if (!s_pIBidirectionalBank) {
                LTLOG_REDALERT("fail.configure.bidirectional", "Unable to configure bidirectional bank");
                return false;
            }
            s_pIBidirectionalBank->ConfigureAsInput(pButton->hPinBank, pButton->pullType);
        }
        bool bInitiallyHigh = ReadPin(pButton);
        pButton->bInitiallyPressed = pButton->bCurrentlyPressed = bInitiallyHigh == pButton->bActiveHigh;
        EnablePinIRQ(pButton, (void*)i);
    }
    return true;
}

static void FiniGPIO(void) {
    PushButtonInstance * pButton = s_PushButtons;
    for (u32 i = 0; i < kNumPushButtons; ++i, ++pButton) {
        if (pButton->hPinBank) {
            DisablePinIRQ(pButton);
            LT_GetCore()->DestroyHandle(pButton->hPinBank);
            pButton->hPinBank = 0;
        }
    }
    lt_closelibrary(s_pDevicePins); s_pDevicePins = NULL;
}

define_LTDEVICE_DRIVER_IMPLEMENTATION(ILTDriverPushButton, CommonDriverPushButton);

/********************************************************************************************************************************
 * Library initialization and deinitialization                                                                                 */
static bool CommonDriverPushButtonImpl_LibInit(void) {
    if (!(s_pDevicePins = (LTDevicePins *)LT_GetCore()->OpenLibrary("LTDevicePins"))) return false;
    if (!(s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore())))               return false;
    if (!InitGPIO()) {                                                    FiniGPIO(); return false; }
    return true;
}

static void CommonDriverPushButtonImpl_LibFini(void) {
    FiniGPIO();
}

static ILTDriverPushButton s_ILTDriverPushButton;
/* This is a forward declaration of the ILTDriverPushButton interface instance that gets
 * defined at the end of this file by the macro define_LTLIBRARY_INTERFACE(ILTDriverPushButton).
 * The variable name has to be s_LTDriverWiFi because that is what the macro defines. */

/********************************************************************************************************************************
 * Device-unit creation interface.
 * Currently, LTDevicePushButton does not use the Device Unit interface, but once Event notifications are able to occur in an
 * interrupt context, that may change.                                                                                         */
static u32 CommonDriverPushButtonImpl_GetNumDeviceUnits(void) { return kNumPushButtons; }

LTDeviceUnit CommonDriverPushButtonImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) { LT_UNUSED(nDeviceUnitNumber); return 0; }

LTLIBRARY_EXPORT_INTERFACES(CommonDriverPushButton, (ILTDriverPushButton))

define_LTLIBRARY_INTERFACE(ILTDriverPushButton) {
    .GetPushButtonNameFromIndex = CommonDriverPushButton_GetPushButtonNameFromIndex,
    .GetPushButtonIndexFromName = CommonDriverPushButton_GetPushButtonIndexFromName,
    .Connect                    = CommonDriverPushButton_Connect,
    .Disconnect                 = CommonDriverPushButton_Disconnect,
    .IsButtonPressed            = CommonDriverPushButton_IsButtonPressed
} LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  05-Mar-21   constantine created
 *  05-Jun-21   constantine moved from TiptonDriverPushButtonImpl.c to Ameba
 *  22-Jul-21   titus       converted from Ameba to Common HW independent driver
 */
