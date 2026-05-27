/*******************************************************************************
 * lt/source/lt/device/keyinput/LTDeviceKeyInputImpl.h
 *
 * LT Device Library for key input functionality
 *
 * Provides notification for keypresses and handling of stuck keys
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTArray.h>

#include <lt/device/keyinput/LTDeviceKeyInput.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("lt.dev.keyinput");

/* clang-format off */

/*******************************************************************************
 * macros
******************************************************************************/

/*******************************************************************************
 * prototypes
*******************************************************************************/
static void DeviceKeyInputPressEventProc(LTEvent event, void *proc, LTArgs *args, void *data);
static void DeviceKeyInputReleaseEventProc(LTEvent event, void *proc, LTArgs *args, void *data);
static void DeviceKeyInputUnstuckEventProc(LTEvent event, void *proc, LTArgs *args, void *data);

/*******************************************************************************
 * typedefs
*******************************************************************************/
// key value and name map
typedef struct {
    LTDeviceKeyInputValue       nKey;   // key value
    const char                 *pName;  // key name
} KeyNameMapElement;

// event types. Also used as indices for the s_hKeyInputEvents array
typedef enum {
    kKeyPress   = 0,
    kKeyRelease = 1,
    kKeyUnstuck = 2
} KeyInputEventTypes;

typedef struct {
    LTEvent                     hEvent;             // the event handle
    LTEvent_DispatchProc       *pEventCallback;     // the event's callback
} KeyInputEvent;

typedef struct {
    LTOThread                  *pOThread;           // the thread that registered for the key hold event
    LTDeviceKeyInputValue       key;                // the key for the key hold event
    LTEvent                     hKeyEvent;          // the key hold event to fire
    LTDeviceKeyInputCallback   *pKeyPressCallback;  // key press callback to call if the button was released without the key hold event firing
    void                       *pPressClientInfo;   // client info for the press event
    LTTime                      keyHoldDuration;    // the time the key must be held before firing the event
    LTTime                      keyHoldNotifyTime;  // the time to notify the key hold event (0 if not pressed)
    bool                        bKeyPressed;        // flag to indicate that the key was pressed
    bool                        bKeyEventSent;      // flag to indicate that the event was sent so it's not sent again
} KeyHoldInfo;

/*******************************************************************************
 * consts
******************************************************************************/
static const LTArgsDescriptor LTDeviceKeyInputEventArgs = {
    1,
    { kLTArgType_u32 }      // the key value
};

static const u32                kThreadStackSize        = 1024;
static const LTTime             kKeyHoldTimerInterval   = LTTimeInitializer_Milliseconds(100);

// keep the order of the events the same as the order in KeyInputEventTypes as it's used for indexing
static KeyInputEvent     kKeyInputEvents[]              = {
    {   0,  DeviceKeyInputPressEventProc                },
    {   0,  DeviceKeyInputReleaseEventProc              },
    {   0,  DeviceKeyInputUnstuckEventProc              },
};
static const u8                 kKeyInputEventCount     = sizeof(kKeyInputEvents) / sizeof(KeyInputEvent);

/*******************************************************************************
 * static variables
*******************************************************************************/
// interfaces
static ILTEvent                *s_iLTEvent              = NULL;
static ILTDriverKeyInput       *s_iLTDriverKeyInput     = NULL;
static LTDriverLibrary         *s_pLibKeyInputDriver    = NULL;

// handles
static LTMutex                 *s_KeyInputMutex         = NULL;

//objects
static LTArray                 *s_pKeyHoldInfoArray     = NULL;
static LTOThread               *s_pOThread              = NULL;

static bool                     s_bThreadIsMine         = false;

// holds how many keys are pressed
static LTAtomic                 s_nPressedKeys;

// the last reported event
static LTDeviceKeyInputEvent    s_lastKeyEvent          = { false, false, kLTKeyInvalid };

// LTKeyInputValue to key name map
static const KeyNameMapElement  s_keyNameMap[]          = {
    // Gather names and values from the definitions file to generate
    // a lookup table, sorted by key value:
    #define LTKEYDEF(a, b) { LTKEYINPUTVALUENAME(a), #a },
    #include <lt/device/keyinput/LTDeviceKeyInputDefs.txt>
    #undef LTKEYDEF
};

/*******************************************************************************
 *
 * LTDeviceKeyInput private helper functions
 *
*******************************************************************************/

/*******************************************************************************
 * Returns the key hold info for the given thread and key
 * @note caller must lock the array since the function returns the pointer in the
 *       array so it can be modified
 * @note pass in NULL for pIndex if not needed
*******************************************************************************/
static KeyHoldInfo *GetKeyHoldInfo(LTOThread *pOThread, LTDeviceKeyInputValue nKey, u32 *pIndex) {
    KeyHoldInfo *pInfo = NULL;
    u32 nSize = s_pKeyHoldInfoArray->API->GetCount(s_pKeyHoldInfoArray);
    for (u32 i = 0; i < nSize && pInfo == NULL; ++i) {
        pInfo = s_pKeyHoldInfoArray->API->Get(s_pKeyHoldInfoArray, i, NULL);
        if (pIndex != NULL) {
            *pIndex = i;
        }
        if (pInfo->pOThread != pOThread || pInfo->key != nKey) {
            pInfo = NULL;
        }
    }
    return pInfo;
}

/*******************************************************************************
 * Timer proc for checking for key hold events
*******************************************************************************/
static void KeyHoldTimerProc(void *pClientData) {
    LT_UNUSED(pClientData);

    s_KeyInputMutex->API->Lock(s_KeyInputMutex);
    LTTime now = LT_GetCore()->GetKernelTime();
    u32 nSize = s_pKeyHoldInfoArray->API->GetCount(s_pKeyHoldInfoArray);
    for (u32 i = 0; i < nSize; ++i) {
        KeyHoldInfo *pInfo = s_pKeyHoldInfoArray->API->Get(s_pKeyHoldInfoArray, i, NULL);
        if (!pInfo->bKeyEventSent && pInfo->bKeyPressed) {
            if (LTTime_IsGreaterThanOrEqual(now, pInfo->keyHoldNotifyTime)) {
                pInfo->bKeyEventSent = true;
                s_iLTEvent->NotifyEvent(pInfo->hKeyEvent, pInfo->key);
            }
        }
    }
    s_KeyInputMutex->API->Unlock(s_KeyInputMutex);
}

/*******************************************************************************
 * LTEvent press proc
*******************************************************************************/
static void DeviceKeyInputPressEventProc(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    LTDeviceKeyInputValue nKey = LTArgs_u32At(0, args);

    s_KeyInputMutex->API->Lock(s_KeyInputMutex);
    // if the client registered for a key hold event, then don't notify them of the press
    KeyHoldInfo *pInfo = GetKeyHoldInfo(s_pOThread, nKey, NULL);
    if (pInfo != NULL) {
        pInfo->bKeyPressed          = true;
        pInfo->keyHoldNotifyTime    = LTTime_Add(LT_GetCore()->GetKernelTime(), pInfo->keyHoldDuration);
        pInfo->bKeyEventSent        = false;
        // set the key press callback
        pInfo->pKeyPressCallback    = (LTDeviceKeyInputCallback *)proc;
        pInfo->pPressClientInfo     = data;
    } else {
        (*(LTDeviceKeyInputCallback *)proc)(nKey, data);
    }
    s_KeyInputMutex->API->Unlock(s_KeyInputMutex);

}

/*******************************************************************************
 * LTEvent release proc
*******************************************************************************/
static void DeviceKeyInputReleaseEventProc(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);

    LTDeviceKeyInputValue nKey = LTArgs_u32At(0, args);

    s_KeyInputMutex->API->Lock(s_KeyInputMutex);
    KeyHoldInfo *pInfo = GetKeyHoldInfo(s_pOThread, nKey, NULL);
    if (pInfo != NULL) {
        if (!pInfo->bKeyEventSent && pInfo->bKeyPressed) {
            if (pInfo->pKeyPressCallback != NULL) {
                // increment an decrement the number of pressed keys in case the client call IsAnyKeyDown()
                LTAtomic_FetchAdd(&s_nPressedKeys, 1);
                pInfo->pKeyPressCallback(nKey, pInfo->pPressClientInfo);
                LTAtomic_FetchSubtract(&s_nPressedKeys, 1);
            }
        }
        pInfo->bKeyPressed = false;
    }
    s_KeyInputMutex->API->Unlock(s_KeyInputMutex);

    (*(LTDeviceKeyInputCallback *)proc)(nKey, data);
}

/*******************************************************************************
 * LTEvent hold proc
*******************************************************************************/
static void DeviceKeyInputHoldEventProc(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    LTDeviceKeyInputValue nKey = LTArgs_u32At(0, args);
    (*(LTDeviceKeyInputCallback *)proc)(nKey, data);
}

/*******************************************************************************
 * LTEvent unstuck proc
*******************************************************************************/
static void DeviceKeyInputUnstuckEventProc(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    LTDeviceKeyInputValue nKey = LTArgs_u32At(0, args);
    (*(LTDeviceKeyInputCallback *)proc)(nKey, data);
}

/*******************************************************************************
 * key press/release events from the driver
*******************************************************************************/
static void KeyPressTaskProc(void *pClientData) {
    // the key value is passed in pClientData as a u32
    // double cast is needed here to support both 32-bit and 64-bit systems
    u32 key = (u32)((LT_SIZE)pClientData);
    // clean the client data
    // update the last event
    LTDeviceKeyInputEvent newEvent = { (key & kLTKeyPressed), false, (key & ~kLTKeyPressed) };
    // send the event
    if (key != kLTKeyInvalid) {
        if (newEvent.press) {
            LTAtomic_FetchAdd(&s_nPressedKeys, 1);
            s_iLTEvent->NotifyEvent(kKeyInputEvents[kKeyPress].hEvent, newEvent.key);
        } else {
            LTAtomic_FetchSubtract(&s_nPressedKeys, 1);
            s_iLTEvent->NotifyEvent(kKeyInputEvents[kKeyRelease].hEvent, newEvent.key);
        }
    }
    // use the key hold mutex so there's no need to create a mutex just for the last event
    s_KeyInputMutex->API->Lock(s_KeyInputMutex);
    s_lastKeyEvent = newEvent;
    s_KeyInputMutex->API->Unlock(s_KeyInputMutex);
}

/*******************************************************************************
 * all keys released event
*******************************************************************************/
static void AllReleasedTaskProc(void *pClientData) {
    LT_UNUSED(pClientData);
    LTAtomic_Store(&s_nPressedKeys, 0);
}

/*******************************************************************************
 * all keys unstuck event
*******************************************************************************/
static void KeyUnstuckTaskProc(void *pClientData) {
    LT_UNUSED(pClientData);
    LTAtomic_Store(&s_nPressedKeys, 0);
    s_iLTEvent->NotifyEvent(kKeyInputEvents[kKeyUnstuck].hEvent, kLTKeyInvalid);
}

/*******************************************************************************
 * no keys stuck event
*******************************************************************************/
static void NoStuckKeyTaskProc(void *pClientData) {
    LT_UNUSED(pClientData);
    LTAtomic_Store(&s_nPressedKeys, 0);
    s_iLTEvent->NotifyEvent(kKeyInputEvents[kKeyUnstuck].hEvent, kLTKeyInvalid);
}

/*******************************************************************************
 * thread start proc
*******************************************************************************/
static void OnStartKeyInputThread(void* pClientData) {
    LT_UNUSED(pClientData);
    // start notifications from the driver
    s_iLTDriverKeyInput->StartNotification(KeyPressTaskProc,
                                           AllReleasedTaskProc,
                                           KeyUnstuckTaskProc,
                                           NoStuckKeyTaskProc);
}

/*******************************************************************************
 * thread stop proc
*******************************************************************************/
static void OnStopKeyInputThread(void) {
    s_iLTDriverKeyInput->StopNotification();
}

/*******************************************************************************
 *
 * LTDeviceKeyInput interface implementation
 *
*******************************************************************************/

/*******************************************************************************
 * Finalize the driver library by closing the driver library
*******************************************************************************/
static void LTDeviceKeyInputImpl_LibFini(void) {
    // first end the thread so nothing is accessed after it's cleaned up
    if (s_bThreadIsMine && s_pOThread != NULL) {
        lt_destroyobject(s_pOThread);
        s_pOThread = NULL;
        s_bThreadIsMine = false;
    }

    if (s_iLTEvent != NULL) {
        // clean the events
        for (u8 i = 0; i < kKeyInputEventCount; ++i) {
            if (kKeyInputEvents[i].hEvent != 0) {
                s_iLTEvent->Destroy(kKeyInputEvents[i].hEvent);
                kKeyInputEvents[i].hEvent = 0;
            }
        }
    }

    if (s_pKeyHoldInfoArray != NULL) {
        if (s_iLTEvent != NULL) {
            u32 nSize = s_pKeyHoldInfoArray->API->GetCount(s_pKeyHoldInfoArray);
            for (u32 i = 0; i < nSize; ++i) {
                KeyHoldInfo *pInfo = s_pKeyHoldInfoArray->API->Get(s_pKeyHoldInfoArray, i, NULL);
                if (pInfo->hKeyEvent != 0) {
                    s_iLTEvent->Destroy(pInfo->hKeyEvent);
                }
            }
        }
        lt_destroyobject(s_pKeyHoldInfoArray);
        s_pKeyHoldInfoArray = NULL;
    }
    if (s_KeyInputMutex) {
        lt_destroyobject(s_KeyInputMutex);
        s_KeyInputMutex = NULL;
    }

    lt_closelibrary(s_pLibKeyInputDriver);

    // clear interfaces and handles
    s_iLTEvent              = NULL;
    s_iLTDriverKeyInput     = NULL;
    s_pLibKeyInputDriver    = NULL;
    s_pKeyHoldInfoArray     = NULL;
    s_KeyInputMutex         = 0;

    LTAtomic_Store(&s_nPressedKeys, 0);
    s_lastKeyEvent.press    = false;
    s_lastKeyEvent.repeat   = false;
    s_lastKeyEvent.key      = kLTKeyInvalid;
}

/*******************************************************************************
 * Initialize the device
*******************************************************************************/
static bool LTDeviceKeyInputImpl_LibInit(void) {

    s_pLibKeyInputDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceKeyInput", 0);
    s_iLTDriverKeyInput = s_pLibKeyInputDriver ? lt_getlibraryinterface(ILTDriverKeyInput, s_pLibKeyInputDriver) : NULL;
    if (s_iLTDriverKeyInput == NULL) {
        LTLOG_REDALERT("driver.interface.fail", "Failed to get ILTDriverKeyInput interface");
        LTDeviceKeyInputImpl_LibFini();
        return false;
    }

    s_iLTEvent = lt_getlibraryinterface(ILTEvent, LT_GetCore());
    if (s_iLTEvent == NULL) {
        LTLOG_REDALERT("event.interface.fail", "Failed to get the ILTEvent interface");
        LTDeviceKeyInputImpl_LibFini();
        return false;
    }

    for (u8 i = 0; i < kKeyInputEventCount; ++i) {
        kKeyInputEvents[i].hEvent = LT_GetCore()->CreateEvent(&LTDeviceKeyInputEventArgs, kKeyInputEvents[i].pEventCallback, NULL, NULL, NULL);
        if (kKeyInputEvents[i].hEvent == 0) {
            LTLOG_REDALERT("event.create.fail", "Failed to create event %d", i);
            LTDeviceKeyInputImpl_LibFini();
            return false;
        }
    }

    s_pKeyHoldInfoArray = LTArray_CreateStructArray(sizeof(KeyHoldInfo));
    if (s_pKeyHoldInfoArray == NULL) {
        LTLOG_REDALERT("array.create.fail", "Failed to create s_pKeyHoldInfoArray");
        LTDeviceKeyInputImpl_LibFini();
        return false;
    }

    s_KeyInputMutex = lt_createobject(LTMutex);
    if (s_KeyInputMutex == 0) {
        LTLOG_REDALERT("mutex.create.fail", "Failed to create s_KeyInputMutex");
        LTDeviceKeyInputImpl_LibFini();
        return false;
    }

    return true;
}

static bool LTDeviceKeyInputImpl_Initialize(LTOThread *pOThread) {

    if (pOThread && s_pOThread && s_bThreadIsMine) {
        // replace the default thread with the given one
        // destroy default thread
        lt_destroyobject(s_pOThread);
        s_pOThread = NULL;
        s_bThreadIsMine = false;
    }
    if (!pOThread) {        /* no thread given - start one */
        if (!(pOThread = lt_createobject(LTOThread))) {
            LTLOG_YELLOWALERT("init.thread.create.fail", "Failed to create the device thread"); 
            return false;
        }
        pOThread->API->SetStackSize(pOThread, kThreadStackSize);
        pOThread->API->Start(pOThread, "dev-keyinput", NULL, OnStopKeyInputThread);
        s_bThreadIsMine = true;
    }
    s_pOThread = pOThread;
    s_pOThread->API->QueueTaskProc(s_pOThread, OnStartKeyInputThread, NULL, NULL);
    s_pOThread->API->SetTimer(s_pOThread, kKeyHoldTimerInterval, KeyHoldTimerProc, NULL, NULL);
    return true;
}
/*******************************************************************************
 * Enable the driver
*******************************************************************************/
static void LTDeviceKeyInputImpl_Enable(bool bEnable) {
    s_iLTDriverKeyInput->Enable(bEnable);
}

/*******************************************************************************
 * Register a client for key press events
*******************************************************************************/
static void LTDeviceKeyInputImpl_RegisterForKeyPress(LTDeviceKeyInputCallback *pCallback, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData) {
    s_iLTEvent->RegisterForEvent(kKeyInputEvents[kKeyPress].hEvent, pCallback, pReleaseProc, pClientData, false);
}

/*******************************************************************************
 * Unregister a client from key press events
*******************************************************************************/
static void LTDeviceKeyInputImpl_UnregisterFromKeyPress(LTDeviceKeyInputCallback *pCallback) {
    s_iLTEvent->UnregisterFromEvent(kKeyInputEvents[kKeyPress].hEvent, pCallback);
}

/*******************************************************************************
 * Register a client for key release events
*******************************************************************************/
static void LTDeviceKeyInputImpl_RegisterForKeyRelease(LTDeviceKeyInputCallback *pCallback, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData) {
    s_iLTEvent->RegisterForEvent(kKeyInputEvents[kKeyRelease].hEvent, pCallback, pReleaseProc, pClientData, false);
}

/*******************************************************************************
 * Unregister a client from key release events
*******************************************************************************/
static void LTDeviceKeyInputImpl_UnregisterFromKeyRelease(LTDeviceKeyInputCallback *pCallback) {
    s_iLTEvent->UnregisterFromEvent(kKeyInputEvents[kKeyRelease].hEvent, pCallback);
}

/*******************************************************************************
 * Register a client for key hold events
*******************************************************************************/
static void LTDeviceKeyInputImpl_RegisterForKeyHold(LTDeviceKeyInputCallback *pCallback, LTDeviceKeyInputValue nKey, LTTime keyHoldDuration, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData) {
    if (s_pOThread == NULL)
        LTDeviceKeyInputImpl_Initialize(NULL);
    KeyHoldInfo info;
    info.key                = nKey;
    info.pOThread           = s_pOThread;
    info.keyHoldDuration    = keyHoldDuration;
    info.keyHoldNotifyTime  = LTTimeInitializer_Infinite();
    info.bKeyPressed        = false;
    info.bKeyEventSent      = false;
    info.hKeyEvent          = LT_GetCore()->CreateEvent(&LTDeviceKeyInputEventArgs, DeviceKeyInputHoldEventProc, NULL, NULL, NULL);
    if (info.hKeyEvent != 0) {
        s_iLTEvent->RegisterForEvent(info.hKeyEvent, pCallback, pReleaseProc, pClientData, false);
        s_KeyInputMutex->API->Lock(s_KeyInputMutex);
        s_pKeyHoldInfoArray->API->Append(s_pKeyHoldInfoArray, &info);
        s_KeyInputMutex->API->Unlock(s_KeyInputMutex);
    }
}

/*******************************************************************************
 * Unregister a client from key hold events
*******************************************************************************/
static void LTDeviceKeyInputImpl_UnregisterFromKeyHold(LTDeviceKeyInputCallback *pCallback, LTDeviceKeyInputValue nKey) {
    s_KeyInputMutex->API->Lock(s_KeyInputMutex);
    u32 nIndex = 0;
    KeyHoldInfo *pInfo = GetKeyHoldInfo(s_pOThread, nKey, &nIndex);
    if (pInfo != NULL) {
        s_iLTEvent->UnregisterFromEvent(pInfo->hKeyEvent, pCallback);
        s_iLTEvent->Destroy(pInfo->hKeyEvent);
        s_pKeyHoldInfoArray->API->Remove(s_pKeyHoldInfoArray, nIndex);
    }
    s_KeyInputMutex->API->Unlock(s_KeyInputMutex);
}

/*******************************************************************************
 * Register a client for key unstuck events
*******************************************************************************/
static void LTDeviceKeyInputImpl_RegisterForKeyUnstuck(LTDeviceKeyInputCallback *pCallback, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData) {
    s_iLTEvent->RegisterForEvent(kKeyInputEvents[kKeyUnstuck].hEvent, pCallback, pReleaseProc, pClientData, false);
}

/*******************************************************************************
 * Unregister a client from key unstuck events
*******************************************************************************/
static void LTDeviceKeyInputImpl_UnregisterFromKeyUnstuck(LTDeviceKeyInputCallback *pCallback) {
    s_iLTEvent->UnregisterFromEvent(kKeyInputEvents[kKeyUnstuck].hEvent, pCallback);
}

/*******************************************************************************
 * Tells the driver to handle a key stuck problem
*******************************************************************************/
static void LTDeviceKeyInputImpl_HandleKeyStuck(void) {
    s_iLTDriverKeyInput->HandleKeyStuck();
}

/*******************************************************************************
 * checks if any button is pressed
*******************************************************************************/
static bool LTDeviceKeyInputImpl_IsAnyKeyDown(void) {
    return (LTAtomic_Load(&s_nPressedKeys) > 0);
}

/*******************************************************************************
 * returns the last keyinput event
*******************************************************************************/
static LTDeviceKeyInputEvent LTDeviceKeyInputImpl_GetLastKeyInputEvent(void) {
    s_KeyInputMutex->API->Lock(s_KeyInputMutex);
    LTDeviceKeyInputEvent lastEvent = s_lastKeyEvent;
    s_KeyInputMutex->API->Unlock(s_KeyInputMutex);

    return lastEvent;
}

/*******************************************************************************
 * returns the name for the given key value
*******************************************************************************/
static const char *LTDeviceKeyInputImpl_GetKeyNameFromKeyValue(LTDeviceKeyInputValue key)
{
    const char *name = NULL;
    for (u8 i = 0; (i < (sizeof(s_keyNameMap) / sizeof(KeyNameMapElement))) && name == NULL; ++i) {
        if (s_keyNameMap[i].nKey == key) {
            name = s_keyNameMap[i].pName;
        }
    }
    return name;
}

/*******************************************************************************
 * returns the key value for the given key name
*******************************************************************************/
static LTDeviceKeyInputValue LTDeviceKeyInputImpl_GetKeyValueFromKeyName(const char *name)
{
    LTDeviceKeyInputValue value = kLTKeyInvalid;
    for (u8 i = 0; (i < (sizeof(s_keyNameMap) / sizeof(KeyNameMapElement))) && value == kLTKeyInvalid; ++i) {
        if (lt_strcmp(name, s_keyNameMap[i].pName) == 0) {
            value = s_keyNameMap[i].nKey;
        }
    }
    return value;
}

/*******************************************************************************
 * returns number of units. Not used
*******************************************************************************/
static u32 LTDeviceKeyInputImpl_GetNumDeviceUnits(void) {
    // This is not supported by the driver, so just return 0
    return 0;
}

/*******************************************************************************
 * creates a handle for the given unit. Not used
*******************************************************************************/
static LTDeviceUnit LTDeviceKeyInputImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LT_UNUSED(nDeviceUnitNumber);
    // This is not supported by the driver, so just return 0
    return 0;
}

/*******************************************************************************
 * Interface definition
*******************************************************************************/
define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceKeyInput) {
    .Initialize                 = LTDeviceKeyInputImpl_Initialize,
    .Enable                     = LTDeviceKeyInputImpl_Enable,
    .RegisterForKeyPress        = LTDeviceKeyInputImpl_RegisterForKeyPress,
    .UnregisterFromKeyPress     = LTDeviceKeyInputImpl_UnregisterFromKeyPress,
    .RegisterForKeyRelease      = LTDeviceKeyInputImpl_RegisterForKeyRelease,
    .UnregisterFromKeyRelease   = LTDeviceKeyInputImpl_UnregisterFromKeyRelease,
    .RegisterForKeyHold         = LTDeviceKeyInputImpl_RegisterForKeyHold,
    .UnregisterFromKeyHold      = LTDeviceKeyInputImpl_UnregisterFromKeyHold,
    .RegisterForKeyUnstuck      = LTDeviceKeyInputImpl_RegisterForKeyUnstuck,
    .UnregisterFromKeyUnstuck   = LTDeviceKeyInputImpl_UnregisterFromKeyUnstuck,
    .HandleKeyStuck             = LTDeviceKeyInputImpl_HandleKeyStuck,
    .IsAnyKeyDown               = LTDeviceKeyInputImpl_IsAnyKeyDown,
    .GetLastKeyInputEvent       = LTDeviceKeyInputImpl_GetLastKeyInputEvent,
    .GetKeyNameFromKeyValue     = LTDeviceKeyInputImpl_GetKeyNameFromKeyValue,
    .GetKeyValueFromKeyName     = LTDeviceKeyInputImpl_GetKeyValueFromKeyName
} LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  14-Sep-21   constantine created
 *  05-Nov-21   vitellius   Completed implementation
 *  08-Dec-21   vitellius   Added key hold feature
 *  17-Oct-22   augustus    added LTThread_ClientDataReleaseProc to registration functions
 *  03-Apr-23   augustus    load driver from LTDeviceConfig specification
 */
