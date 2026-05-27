/*******************************************************************************
 * lt/source/example/media/ExampleDeviceMedia.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <tilt/TiltImpl.c>

/****************************************************************************/
/* Consts                                                                   */
/****************************************************************************/

static ILTThread     *s_thread     = NULL;
static LTCore        *s_core       = NULL;
static LTThread       s_mainThread = 0;
static LTDeviceMedia *s_media      = NULL;

/*******************************************************************************
 * Event Dispatch Procs
 ******************************************************************************/

static void
OnMediaEvent(LTMediaEvent event, void *eventData, void * pClientData) {
    LT_UNUSED(pClientData);
    if (event == kLTMediaEvent_Data) {
        LTMediaData *mediaData = eventData;
        LTLOG("med.evt", "Media data event - data: %p, size: %lu, timestamp: %lu", mediaData->pData, LT_Pu32(mediaData->nDataLen), LT_Pu32(mediaData->nTimestamp));
    }
}

static bool OnThreadStart(void) {
    LTMediaFormat format = {
        .nKind = kLTMediaKind_Image_HD,
        .nEncoding = kLTMediaEncoding_Jpeg,
    };
    LTMediaSource hSource = s_media->OpenSource(&format);
    ILTMediaSource *iSource = lt_gethandleinterface(ILTMediaSource, hSource);
    iSource->OnMediaEvent(hSource, OnMediaEvent, NULL);
    iSource->Trigger(hSource);
    return true;
}

static void OnThreadExit(void) {
}

static void StartMedia(void) {
    s_mainThread = s_core->CreateThread("MediaTest");
    if (!s_mainThread) return;
    s_thread = lt_getlibraryinterface(ILTThread, s_core);
    s_thread->SetStackSize(s_mainThread, 2048);
    s_thread->Start(s_mainThread, OnThreadStart, OnThreadExit);
    s_thread->WaitUntilFinished(s_mainThread, LTTime_Infinite());
    lt_destroyhandle(s_mainThread);
    s_mainThread = 0;
}

static void TestMedia(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "media test start");
    StartMedia();
    TILT_MESSAGE(trc, "media test done");
}

static const TiltImplTestSpecifier s_mediaTests[] = {
    {TestMedia ,  "media", "Test media",  0},
};

static void ShutdownExampleDeviceMedia(void) {
    if (s_mainThread) lt_destroyhandle(s_mainThread);
    s_mainThread = 0;
    s_thread = NULL;
    if (s_media) lt_closelibrary(s_media);
    s_media = NULL;
    s_core = NULL;
    ShutdownTilt();
}

static bool ExampleDeviceMediaImpl_LibInit(void) {
    s_core   = LT_GetCore();
    s_thread = lt_getlibraryinterface(ILTThread, s_core);

    do {
        s_media = lt_openlibrary(LTDeviceMedia);
        if (!s_media) break;

        return InitializeTilt(s_mediaTests, sizeof(s_mediaTests) / sizeof(s_mediaTests[0]), NULL);
    } while (0);

    ShutdownExampleDeviceMedia();
    return false;
}

static void ExampleDeviceMediaImpl_LibFini(void) {
    ShutdownExampleDeviceMedia();
}

static int ExampleDeviceMedia_Run(int argc, const char **argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    StartMedia();
    return 0;
}

typedef_LTLIBRARY_ROOT_INTERFACE(ExampleDeviceMedia, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(ExampleDeviceMedia, ExampleDeviceMedia_Run, 8192) LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(ExampleDeviceMedia, (ITilt));

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  14-Jun-23   trajan      created
 */
