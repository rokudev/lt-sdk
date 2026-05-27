/*******************************************************************************
 * lt/source/lt/system/usb/LTSystemUsbBusManager.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTArray.h>
#include <lt/core/LTCore.h>
#include <lt/product/config/LTProductConfig.h>
#include <lt/system/usb/LTSystemUsbBusManager.h>

DEFINE_LTLOG_SECTION("sys.usb.man")

#define USB_MANAGER_DO_DLOG 0
#if USB_MANAGER_DO_DLOG
#define DLOG LTLOG
#else
#define DLOG LTLOG_LOGNULL
#endif

typedef struct BusRequest {
    LTSystemUsbBusManager_BusReassignedCallback callback;
    void *clientData;
    LTThread thread;
} BusRequest;

static struct Statics {
    ILTThread *iThread;
    LTMutex *mutex;

    LTAssociativeArray *requests;
    BusRequest *defaultRequest;

    BusRequest *currentRequest;
    BusRequest *desiredRequest;
} S;

/*  ______________________________
 *  Object private data members */
typedef_LTObjectImpl(LTSystemUsbBusManager, LTSystemUsbBusManagerImpl) {
} LTOBJECT_API;

/*___________________________________________
/ LTSystemUsbBusManager private static functions */

static bool FindKeyEnumerator(LTAssociativeArray *array, const void *key, const u16 keySize, void *value, void *clientData) {
    LT_UNUSED(array);
    LT_UNUSED(keySize);
    const void **dataPtr = (const void**)clientData;
    const BusRequest *request = *dataPtr;
    if (request == value) {
        // Found the request, return a copy of the key and stop enumerating.
        // The key pointer will become invalid after returning from this function,
        // so we need to copy it.
        *dataPtr = (void *)lt_strdup((const char *)key);
        return false;
    }
    return true;
}

LTString GetName(BusRequest *request) {
    if (!request) {
        return NULL;
    }
    // Use the clientData to pass in the request, and also to get the name out.
    void *data = request;
    if (S.requests->API->Enumerate(S.requests, FindKeyEnumerator, &data)) {
        // If the enumeration completed, we did not find the request.
        return NULL;
    }
    return (LTString)data;
}

static void SwitchToDesiredRequest(void);

static BusRequest * NextRequest(void) {
    // Allow picking no mode explicitly.
    if (!S.desiredRequest) {
        return NULL;
    }
    // Pick the desired mode if it is registered.
    if (S.desiredRequest->callback) {
        return S.desiredRequest;
    }
    // Fall back to the default mode.
    if (S.defaultRequest && S.defaultRequest->callback) {
        return S.defaultRequest;
    }
    return NULL;
}

// This function runs on the thread of the new mode request.
static void AssignBus(void *clientData) {
    LT_UNUSED(clientData);
    S.mutex->API->Lock(S.mutex);

    if (S.currentRequest) {
        DLOG("assign.busy", NULL);
        // Probably got multiple switching requests too close together.
        // Re-run the switching logic to make sure we end up in the desired mode.
        SwitchToDesiredRequest();
        S.mutex->API->Unlock(S.mutex);
        return;
    }

    BusRequest *nextRequest = NextRequest();
    if (!nextRequest) {
        DLOG("assign.none", NULL);
        S.mutex->API->Unlock(S.mutex);
        return;
    }

    LTLOG("assign", "Assigning USB bus to mode \"%s\"", GetName(nextRequest));
    nextRequest->callback(true, nextRequest->clientData);
    S.currentRequest = nextRequest;

    S.mutex->API->Unlock(S.mutex);
}

// This function runs on the thread of the current mode request.
static void ReclaimBus(void * clientData) {
    LT_UNUSED(clientData);
    S.mutex->API->Lock(S.mutex);

    // Release the current mode.
    if (S.currentRequest) {
        LTLOG("reclaim", "Reclaiming USB bus from mode \"%s\"", GetName(S.currentRequest));
        S.currentRequest->callback(false, S.currentRequest->clientData);
        S.currentRequest = NULL;
    } else {
        DLOG("reclaim.empty", NULL);
    }

    // Now allocate the bus to the desired mode.
    SwitchToDesiredRequest();
    S.mutex->API->Unlock(S.mutex);
}

// This function needs to be called with the lock already held.
static void SwitchToDesiredRequest(void) {
    if (S.desiredRequest == S.currentRequest) {
        return;
    }

    // If the bus is currently held, reclaim it first.
    if (S.currentRequest) {
        S.iThread->QueueTaskProcIfRequired(S.currentRequest->thread, ReclaimBus, NULL, S.currentRequest);
        return;
    }

    // If the bus is not currently held, allocate it to the next mode.
    BusRequest *nextRequest = NextRequest();
    if (nextRequest) {
        S.iThread->QueueTaskProcIfRequired(nextRequest->thread, AssignBus, NULL, nextRequest);
    }
}

static bool LTSystemUsbBusManagerImpl_OnModeChange(const char *mode, LTSystemUsbBusManager_BusReassignedCallback cb, void * clientData) {
    if (!mode) {
        return false;
    }
    S.mutex->API->Lock(S.mutex);

    BusRequest *request = LTCStringKeyedArray_Get(S.requests, mode, NULL);
    if (!request) {
        LTLOG_YELLOWALERT("req.notfound", "USB bus mode %s not found", mode);
        S.mutex->API->Unlock(S.mutex);
        return false;
    }
    if (request->callback) {
        LTLOG_YELLOWALERT("req.exists", "USB bus request for %lu already exists", LT_Pu32(mode));
        S.mutex->API->Unlock(S.mutex);
        return false;
    }
    *request = (BusRequest) {
        .callback = cb,
        .clientData = clientData,
        .thread = S.iThread->GetCurrentThread(),
    };

    // Run the switching logic.
    // If the desired or the default mode has just been registered, we can activate it now.
    SwitchToDesiredRequest();

    S.mutex->API->Unlock(S.mutex);
    return true;
}

static void LTSystemUsbBusManagerImpl_NoModeChange(const char *mode) {
    if (!mode) {
        return;
    }
    S.mutex->API->Lock(S.mutex);

    BusRequest *request = LTCStringKeyedArray_Get(S.requests, mode, NULL);
    if (!request) {
        LTLOG_YELLOWALERT("req.notfound", "USB bus mode %s not found", mode);
        S.mutex->API->Unlock(S.mutex);
        return;
    }
    if (request == S.currentRequest) {
        // Immediately release the bus if it is currently held by the caller.
        LTLOG("release", "Releasing USB bus from mode \"%s\"", GetName(request));
        request->callback(false, request->clientData);
        S.currentRequest = NULL;

        // Clear the request for this mode.
        *request = (BusRequest) {0};

        // See if we should switch to something else, such as the default mode.
        SwitchToDesiredRequest();
    } else {
        // Clear the request for this mode.
        *request = (BusRequest) {0};
    }
    S.mutex->API->Unlock(S.mutex);
}

static bool LTSystemUsbBusManagerImpl_ChangeMode(const char *mode) {
    bool ret = false;
    S.mutex->API->Lock(S.mutex);

    if (!mode) {
        S.desiredRequest = NULL;
        if (S.currentRequest) {
            DLOG("chg.release", "Releasing USB bus");
            SwitchToDesiredRequest();
        } else {
            DLOG("chg.inact", "USB bus mode is already inactive");
        }
        ret = true;
    } else {
        BusRequest *request = LTCStringKeyedArray_Get(S.requests, mode, NULL);
        if (!request) {
            LTLOG_YELLOWALERT("chg.inv", "USB bus mode \"%s\" not found", mode);
        } else if (!request->callback) {
            LTLOG_YELLOWALERT("chg.empty", "USB bus mode \"%s\" is not registered", mode);
        } else {
            DLOG("chg", "Changing USB bus mode to \"%s\"", mode);
            S.desiredRequest = request;
            SwitchToDesiredRequest();
            ret = true;
        }
    }

    S.mutex->API->Unlock(S.mutex);
    return ret;
}

static const char *LTSystemUsbBusManagerImpl_GetCurrentMode(void) {
    return GetName(S.currentRequest);
}

typedef struct {
    LTSystemUsbBusManager_ListModesCallback callback;
    void *clientData;
} ListModesContext;

static bool ListModesEnumerator(LTAssociativeArray *array, const void *key, const u16 keySize, void *value, void *clientData) {
    LT_UNUSED(array);
    LT_UNUSED(value);
    LT_UNUSED(keySize);
    ListModesContext *context = (ListModesContext *)clientData;
    context->callback((const char *)key, context->clientData);
    return true;
}

static void LTSystemUsbBusManagerImpl_ListModes(LTSystemUsbBusManager_ListModesCallback callback, void *clientData) {
    if (!callback) {
        return;
    }
    // The structure of the requests array does not change during the lifetime of this library,
    // so enumerating its keys while not holding the mutex is safe.
    ListModesContext context = {
        .callback = callback,
        .clientData = clientData
    };
    S.requests->API->Enumerate(S.requests, ListModesEnumerator, &context);
}

/*__________________________________________
  LTSystemUsbBusManager library initialization */

static bool InitModes(void) {
    LTProductConfig *config = lt_openlibrary(LTProductConfig);
    u32 section = config->GetLibraryConfigSection("LTSystemUsbBusManager");
    if (!section) {
        LTLOG_YELLOWALERT("init.no_section", "No USB bus modes section in product config");
        lt_closelibrary(config);
        return false;
    }

    LTResourceValue value;
    if (!config->ReadFirstItemInArray(section, "modes", &value)) {
        LTLOG_YELLOWALERT("init.no_modes", "No USB bus modes found in product config");
        lt_closelibrary(config);
        return false;
    }
    if (value.type == kLTResourceValueType_String) {
        if (!LTCStringKeyedArray_Set(S.requests, value.string, NULL)) {
            LTLOG_YELLOWALERT("init.alloc", "Failed to register USB bus mode \"%s\"", value.string);
            lt_closelibrary(config);
            return false;
        }
    }
    while (config->ReadNextItemInArray(&value)) {
        if (value.type == kLTResourceValueType_String) {
            if (!LTCStringKeyedArray_Set(S.requests, value.string, NULL)) {
                LTLOG_YELLOWALERT("init.alloc.i", "Failed to register USB bus mode \"%s\"", value.string);
                lt_closelibrary(config);
                return false;
            }
        }
    }

    if (!S.requests->API->GetCount(S.requests)) {
        LTLOG_YELLOWALERT("init.empty", "No USB bus modes found in settings");
        lt_closelibrary(config);
        return false;
    }

    const char *defaultModeName = config->ReadString(section, "default_mode");
    if (!defaultModeName) {
        LTLOG("init.def", "No default USB bus mode found in config");
        S.defaultRequest = NULL;
    } else {
        S.defaultRequest = LTCStringKeyedArray_Get(S.requests, defaultModeName, NULL);
        if (!S.defaultRequest) {
            LTLOG_YELLOWALERT("init.nodef", "Default USB bus mode \"%s\" not found in config", defaultModeName);
            lt_closelibrary(config);
            return false;
        }
    }

    lt_closelibrary(config);
    return true;
}

static bool LTSystemUsbBusManagerImpl_ConstructObject(LTSystemUsbBusManagerImpl *busManager) {
    LT_UNUSED(busManager);
    return true;
}

static void LTSystemUsbBusManagerImpl_DestructObject(LTSystemUsbBusManagerImpl *busManager) {
    LT_UNUSED(busManager);
}

static void LTSystemUsbBusManager_LibFini(void) {
    lt_destroyobject(S.requests);
    lt_destroyobject(S.mutex);
}

static bool LTSystemUsbBusManager_LibInit(void) {
    S = (struct Statics) {0};
    do {
        S.iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
        if (!S.iThread) break;
        S.mutex = lt_createobject(LTMutex);
        if (!S.mutex) break;
        S.requests = lt_createobject(LTAssociativeArray);
        if (!S.requests) break;
        S.requests->API->InitAsStructArray(S.requests, sizeof(BusRequest));

        if (!InitModes()) break;
        S.currentRequest = NULL;
        S.desiredRequest = S.defaultRequest;
        return true;
    } while (false);

    LTSystemUsbBusManager_LibFini();
    return false;
}

/*___________________________________________________________
  Object API definition and library root interface binding */

  define_LTObjectImplPublic(LTSystemUsbBusManager, LTSystemUsbBusManagerImpl,
    OnModeChange,
    NoModeChange,
    ChangeMode,
    GetCurrentMode,
    ListModes,
);

define_LTObjectLibrary(1, LTSystemUsbBusManager_LibInit, LTSystemUsbBusManager_LibFini);
