/******************************************************************************
 * ExampleSystemCrashdump.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <example/mockcloudconfig/MockCloudConfig.h>

#include <lt/net/core/LTNetCore.h>
#include <lt/system/crashdump/LTSystemCrashdump.h>

DEFINE_LTLOG_SECTION("example.crashdump");

static struct {
    struct {
        ILTThread *thread;
        ILTSystemCrashdumpClient *client;
    } i;

    LTTransport transport;
    LTSystemCrashdumpClient client;
    LTSystemCrashdump_Event event;
    LTThread thread;
} S;

static u8 *bad_memory = NULL;

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(ExampleSystemCrashdump, (LTNetCore)(LTSystemCrashdump));

static bool DoCrash(void) {
    volatile u8 bad_access = *bad_memory;
    return bad_access;

    return true;
}

static void OnUploadEvent(LTSystemCrashdump_Event event, void *data) {
    LT_UNUSED(data);

    S.event = event;

    switch (event) {
        case kLTSystemCrashdump_Event_UploadFailed:
            LTLOG_REDALERT("upload.failed", " ");
            break;
        case kLTSystemCrashdump_Event_UploadComplete:
            LTLOG("upload.complete", " ");
            break;
        case kLTSystemCrashdump_Event_NoDump:
            LTLOG("upload.nodump", " ");
            break;
    }

    LT_GetLTSystemCrashdump()->Erase();

    S.i.thread->Terminate(S.thread);
}

static void OnTransportEvent(LTTransport transport, LTTransport_Event event, void *data) {
    LT_UNUSED(transport);
    LT_UNUSED(data);

    if (event == kLTTransport_Event_Up) {
        S.client = LTHANDLE_INVALID;
        S.client = LT_GetLTSystemCrashdump()->CreateClient(transport);
        if (!S.client) {
            LTLOG_REDALERT("transport.client.null", " ");
            goto failure;
        }
        S.i.client = lt_gethandleinterface(ILTSystemCrashdumpClient, S.client);
        if (!S.i.client) {
            LTLOG_REDALERT("transport.iclient.null", " ");
            goto failure;
        }
        S.i.client->OnUploadEvent(S.client, OnUploadEvent, NULL, NULL);
        S.i.client->CheckAndStartUpload(S.client);
    }

    return;
failure:
    LT_GetCore()->DestroyHandle(S.client);
}

static bool DoUpload(void) {
    if (!LT_GetLTSystemCrashdump()->Check()) {
        LTLOG_REDALERT("upload.absent", " ");
    }

    S.event     = kLTSystemCrashdump_Event_UploadFailed;
    S.transport = LT_GetLTNetCore()->OpenTransport(NULL, OnTransportEvent, NULL);

    return true;
}

static void DoUploadCleanup(void) {
    S.i.client->Destroy(S.client);
    LT_GetCore()->DestroyHandle(S.transport);
    S.client    = LTHANDLE_INVALID;
    S.transport = LTHANDLE_INVALID;
}

static int Run(int argc, const char **argv) {
    if (argc != 3) {
        return -1;
    }
    S.thread = LT_GetCore()->CreateThread("ExampleCrashdump");
    if (!S.thread) {
        return false;
    }
    S.i.thread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    if (!S.i.thread) {
        return false;
    }
    S.i.thread->SetStackSize(S.thread, 4096);
    if (lt_strcmp(argv[2], "crash") == 0) {
        S.i.thread->Start(S.thread, DoCrash, NULL);
    } else if (lt_strcmp(argv[2], "upload") == 0) {
        S.i.thread->Start(S.thread, DoUpload, DoUploadCleanup);
    }
    S.i.thread->WaitUntilFinished(S.thread, LTTime_Seconds(30));
    S.i.thread->Destroy(S.thread);

    return 0;
}

static bool ExampleSystemCrashdumpImpl_LibInit(void) {
    lt_memset(&S, 0, sizeof(S));

    MockConfig();

    return true;
}

static void ExampleSystemCrashdumpImpl_LibFini(void) {}

typedef_LTLIBRARY_ROOT_INTERFACE(ExampleSystemCrashdump, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(ExampleSystemCrashdump, Run)
LTLIBRARY_DEFINITION;
