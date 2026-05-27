/*******************************************************************************
 * lt/source/lt/device/keypad/LTDeviceKeypadImpl.h
 *
 * LT Device Library for keypad functionality
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/device/config/LTDeviceKonfig.h>
#include <lt/device/keypad/LTDeviceKeypad.h>
#include <lt/driver/keypad/LTDriverKeypad.h>
#include <lt/core/LTCore.h>

/*__________________________
  LTDeviceKeypad #defines */
DEFINE_LTLOG_SECTION("ltdevicekeypad");

/*____________________
  LTLibrary binding */
define_LTObjectLibrary(1, NULL, NULL);

/*_______________________________
  LTDeviceKeypadImpl constants */
enum { kKeyDispatchBufferSize = 16 };

static const LTArgsDescriptor s_keypadEventArgs = { 2, { kLTArgType_pointer, kLTArgType_u32 } };

/*_________________________________________________
  typedef_LTObjectImpl with private data members */
typedef_LTObjectImpl(LTDeviceKeypad, LTDeviceKeypadImpl) {
    LTDriverKeypad *driver;
    LTEvent         hEvent;
    ILTEvent       *iEvent;
    u32             keysToDispatch[kKeyDispatchBufferSize];
    u32             numKeysToDispatch;
    bool            bDispatchInProgress;
} LTOBJECT_API;

/*______________________________________
  LTDeviceKeypad Event Dispatch procs */
static void LTDeviceKeypadImpl_KeyEventDispatchProc(LTEvent hEvent, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(hEvent);
    ((LTDeviceKeypad_RawKeyInputEventProc *)proc)(LTArgs_pointerAt(0, args), LTArgs_u32At(1, args), pClientData);
}

static void LTDeviceKeypadImpl_KeyEventDispatchCompleteProc(LTEvent hEvent, LTArgs *args) {
    LT_UNUSED(hEvent);
    lt_free(LTArgs_pointerAt(0, args));
}

/*______________________________________________________________________________
  LTDeviceKeypad ISR Event Notify Thread Proxy - LTEvent proxy thread context */
static void LTDeviceKeypadImpl_ISREventNotifyThreadProxy(void *pClientData) {
    u32 i, nNumKeys, *pKeys;
    LTDeviceKeypadImpl *keypad = (LTDeviceKeypadImpl *)pClientData;
    LT_SIZE nMask = LT_GetCore()->Disable();
    while (0 != (nNumKeys = keypad->numKeysToDispatch)) {
        LT_GetCore()->Enable(nMask);
        pKeys = lt_malloc(nNumKeys * sizeof(u32));
        nMask = LT_GetCore()->Disable();
        if (pKeys) for (i = 0; i < nNumKeys; i++) pKeys[i] = keypad->keysToDispatch[i];
        keypad->numKeysToDispatch -= nNumKeys;
        for (i = 0; i < keypad->numKeysToDispatch; i++) keypad->keysToDispatch[i] = keypad->keysToDispatch[i+nNumKeys];
        LT_GetCore()->Enable(nMask);
        if (pKeys) keypad->iEvent->NotifyEvent(keypad->hEvent, pKeys, nNumKeys);
        else {
            LTLOG_YELLOWALERT("isrproxy.key.drop", "dropping %lu keys", LT_Pu32(nNumKeys));
        }
        nMask = LT_GetCore()->Disable();
    }
    keypad->bDispatchInProgress = false;
    LT_GetCore()->Enable(nMask);
}

/*______________________________________________________
  LTDeviceKeypad ISR Key Input Callback - ISR context */
static void LTDeviceKeypadImpl_KeyInputProc(u32 *pRawKeys, u32 nNumKeys, void *pClientData) LT_ISR_SAFE {
    LTDeviceKeypadImpl *keypad = (LTDeviceKeypadImpl *)pClientData;
    if (nNumKeys == 0) {
        /* driver done sending keys, dispatch */
        dispatch:
        if (keypad->numKeysToDispatch && (! keypad->bDispatchInProgress)) {
            keypad->bDispatchInProgress = true;
            keypad->iEvent->NotifyEventFromISR(&LTDeviceKeypadImpl_ISREventNotifyThreadProxy, keypad);
        }
        return;
    }
    u32 keysAvail = kKeyDispatchBufferSize - keypad->numKeysToDispatch;
    u32 *keysToDispatch = keypad->keysToDispatch + keypad->numKeysToDispatch;
    while (keysAvail && nNumKeys) {
        *keysToDispatch++ = *pRawKeys++;
        keysAvail--;
        nNumKeys--;
        keypad->numKeysToDispatch++;
    }
    if (keysAvail == 0) {
        /* filled up the dispatch buffer; dispatch now */
        if (nNumKeys) {
            LTLOG_YELLOWALERT("isr.key.drop", "dropping %lu keys", LT_Pu32(nNumKeys));
        }
        goto dispatch;
    }
}

/*__________________________________
  LTDeviceKeypadImpl constructors */
static void LTDeviceKeypadImpl_DestructObject(LTDeviceKeypadImpl *keypad) {
    lt_destroyobject(keypad->driver);
    lt_destroyhandle(keypad->hEvent);
}

static bool LTDeviceKeypadImpl_ConstructObject(LTDeviceKeypadImpl *keypad) {
    bool bSuccess = false;
    do {
        if (NULL == (keypad->driver = lt_createdriverobject_fordevice(LTDriverKeypad, keypad))) break;

        if (LTHANDLE_INVALID == (keypad->hEvent = LT_GetCore()->CreateEvent(&s_keypadEventArgs, &LTDeviceKeypadImpl_KeyEventDispatchProc, &LTDeviceKeypadImpl_KeyEventDispatchCompleteProc, NULL, NULL))) break;
        keypad->iEvent = lt_gethandleinterface(ILTEvent, keypad->hEvent);
        bSuccess = true;
    }
    while (false);
    if (! bSuccess) LTDeviceKeypadImpl_DestructObject(keypad);

    return bSuccess;
}

/*_______________________________
  LTDeviceKeypad API functions */
static void LTDeviceKeypadImpl_OnKeyInput(LTDeviceKeypadImpl *keypad, LTDeviceKeypad_RawKeyInputEventProc *pEventProc, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData) {
    keypad->iEvent->RegisterForEvent(keypad->hEvent, pEventProc, pReleaseProc, pClientData, false);
}

static void LTDeviceKeypadImpl_NoKeyInput(LTDeviceKeypadImpl *keypad, LTDeviceKeypad_RawKeyInputEventProc *pEventProc) {
    keypad->iEvent->UnregisterFromEvent(keypad->hEvent, pEventProc);
}

static void LTDeviceKeypadImpl_Start(LTDeviceKeypadImpl *keypad) {
    keypad->driver->API->Start(keypad->driver, &LTDeviceKeypadImpl_KeyInputProc, keypad);
}

static void LTDeviceKeypadImpl_Stop(LTDeviceKeypadImpl *keypad) {
    keypad->driver->API->Stop(keypad->driver);
}

static bool LTDeviceKeypadImpl_IsKeyDown(LTDeviceKeypadImpl *keypad, LTKey key) {
    return keypad->driver->API->IsKeyDown(keypad->driver, key);
}

static bool LTDeviceKeypadImpl_IsAnyKeyDown(LTDeviceKeypadImpl *keypad) {
    return keypad->driver->API->IsAnyKeyDown(keypad->driver);
}

static LTKey LTDeviceKeypadImpl_GetStuckKey(LTDeviceKeypadImpl *keypad) {
    return keypad->driver->API->GetStuckKey(keypad->driver);
}

static void LTDeviceKeypadImpl_DispatchRawKeySequence(LTDeviceKeypadImpl *keypad, u32 *pRawKeys, u32 nNumKeys) {
    u32 * keys = lt_malloc(nNumKeys * sizeof(u32));
    if (keys) {
        for (u32 i = 0; i < nNumKeys; i++) keys[i] = pRawKeys[i];
        keypad->iEvent->NotifyEvent(keypad->hEvent, keys, nNumKeys);
    }
}

static void LTDeviceKeypadImpl_DispatchKeyDown(LTDeviceKeypadImpl *keypad, LTKey key) {
    key = LTKey_MakeKeyDown(key);
    LTDeviceKeypadImpl_DispatchRawKeySequence(keypad, &key, 1);
}

static void LTDeviceKeypadImpl_DispatchKeyUp(LTDeviceKeypadImpl *keypad, LTKey key) {
    key = LTKey_MakeKeyUp(key);
    LTDeviceKeypadImpl_DispatchRawKeySequence(keypad, &key, 1);
}

static void LTDeviceKeypadImpl_DispatchKeyPress(LTDeviceKeypadImpl *keypad, LTKey key) {
    u32 keys[2];
    keys[0] = LTKey_MakeKeyDown(key);
    keys[1] = LTKey_MakeKeyUp(key);
    LTDeviceKeypadImpl_DispatchRawKeySequence(keypad, keys, 2);
}

/*_____________________________
  LTDeviceKeypad api binding */
define_LTObjectImplPublic(LTDeviceKeypad, LTDeviceKeypadImpl,
    OnKeyInput,
    NoKeyInput,
    Start,
    Stop,
    IsKeyDown,
    IsAnyKeyDown,
    GetStuckKey,
    DispatchKeyDown,
    DispatchKeyUp,
    DispatchKeyPress,
    DispatchRawKeySequence
);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  26-Jan-25   augustus    created
 */
