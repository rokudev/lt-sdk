/*******************************************************************************
 * ltshell/media/mp4/recorder/LTShellMediaMP4Recorder.c
 * 
 * Shell commands for testing LTMediaMP4Recorder
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/httpclient/LTNetHttpClient.h>
#include <lt/media/mp4/recorder/LTMediaMP4Recorder.h>
#include <lt/system/shell/LTSystemShell.h>

DEFINE_LTLOG_SECTION("shl.mp4");

static LTSystemShell        *s_Shell       = NULL;
static LTNetHttpClient      *s_Http        = NULL;
static LTMediaMP4Recorder   *s_Recorder    = NULL;

static LTNetHttpClient *s_Http;
static LTHttpRequest    s_hVideoRequest;

static u64                  s_fileSize;
static u64                  s_fileBytesRemaining;
static u8                  *s_writeBuf;
static LTFile              *s_videoFile;
static LTMediaMP4Recording *s_recording;
static LTString             s_httpUploadUrl;

static void OnVideoRequestEvent(LTHttpRequest hRequest, LTHttpRequestEvent event, void *clientData) {
    LTFile *file = clientData;
    ILTHttpRequest *request = lt_gethandleinterface(ILTHttpRequest, hRequest);
    LTLOG("http", "event: %lu", LT_Pu32(event));
    switch (event) {
        /* Write request body */
        case LTHttpRequestEvent_BodyWriteReady: {
            file->API->SeekToPosition(file, s_fileSize - s_fileBytesRemaining);
            while (s_fileBytesRemaining > 0) {
                u32 toRead = LT_MIN((u64)4096, s_fileBytesRemaining);
                u32 bytesRead = file->API->Read(file, toRead, s_writeBuf);

                LTLOG("http.read", "read: %ld bytes", LT_Ps32(bytesRead));

                s32 bytesWritten = request->WriteBodyData(s_hVideoRequest, s_writeBuf, bytesRead);
                LTLOG("http.write", "wrote: %ld bytes | remaining: %lu", LT_Ps32(bytesWritten), LT_Pu32(s_fileBytesRemaining));
                if (bytesWritten <= 0) break;
                s_fileBytesRemaining -= bytesWritten;
            }
        } break;

        /* Handle request response */
        case LTHttpRequestEvent_HeadComplete:
            LTLOG("http.head", "req head complete");
            break;
        case LTHttpRequestEvent_BodyDataAvailable:
            LTLOG("http.data", "req body data available");
            break;
        case LTHttpRequestEvent_RequestComplete:
            LTLOG("http.done", "req complete");
            lt_destroyobject(s_videoFile);
            s_videoFile = NULL;
            lt_free(s_writeBuf);
            s_writeBuf = NULL;
            lt_destroyhandle(s_hVideoRequest);
            s_hVideoRequest = 0;
            break;
        case LTHttpRequestEvent_Disconnected:
        case LTHttpRequestEvent_DisconnectPending:
        case LTHttpRequestEvent_Error:
        default:
            LTLOG("http.err", "err event: %lu", LT_Pu32(event));
            lt_destroyobject(s_videoFile);
            s_videoFile = NULL;
            lt_free(s_writeBuf);
            s_writeBuf = NULL;
            lt_destroyhandle(s_hVideoRequest);
            s_hVideoRequest = 0;
            break;
    }
}

static bool UploadVideo(LTFile *file) {
    s_Http = lt_openlibrary(LTNetHttpClient);
    s_hVideoRequest = s_Http->CreateRequest(s_httpUploadUrl, kLTHttpRequestMethod_POST, 0);
    
    ILTHttpRequest *request = lt_gethandleinterface(ILTHttpRequest, s_hVideoRequest);
    request->OnRequestEvent(s_hVideoRequest, OnVideoRequestEvent, file);
    s_fileSize = file->API->GetSize(file);
    LTHttpMultipartData formData[1] = {
        {
            .name        = "file",
            .contentType = "video/mp4",
            .filename    = "video.mp4",
            .data        = NULL,
            .size        = s_fileSize
        }
    };
    request->SetBody(s_hVideoRequest, "multipart/form-data", formData, 1);
    s_fileBytesRemaining = s_fileSize;
    s_writeBuf = lt_malloc(4096);
    if (!s_writeBuf) return false;
    LTLOG("req.sent", "request sent! filesize: %lu", LT_Pu32(s_fileSize));
    return request->SendRequest(s_hVideoRequest);
}

static int Command_Mp4(LTShell hShell, int argc, const char *argv[]) {
    s_Recorder->Start(false); // Start recording pipeline (without preroll) in case it has not been started

    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    do {
        if (argc > 3 || argc < 2) break;
        
        if (lt_strncmp(argv[1], "url", 3) == 0 && argc == 3) {
            // Set the upload url
            ltstring_set(&s_httpUploadUrl, argv[2]);
            iShell->Print(hShell, "Finished MP4 will be uploaded to: %s\n", argv[2]);
        } else if (lt_strncmp(argv[1], "start", 5) == 0) {
            // Start recording
            if (s_videoFile) {
                iShell->Print(hShell, "Recording already in progress\n");
            }
            iShell->Print(hShell, "Begin recording...\n");
            s_videoFile = lt_createobject_typed(LTFile, LTMemoryFile);
            s_recording = s_Recorder->CreateRecording(s_videoFile, (LTMediaMP4Recording_Config){.audio.enabled = true, .video.enabled = true });
            if (!s_recording) {
                LTLOG_YELLOWALERT("create.fail", "failed to create recording");
                break;
            }
            s_recording->API->Start(s_recording);
        } else if (lt_strncmp(argv[1], "stop", 4) == 0) {
            // Stop recording
            if (!s_videoFile) {
                iShell->Print(hShell, "No recording in progress\n");
            } 
            iShell->Print(hShell, "Stop and upload recording...\n");
            s_recording->API->Finish(s_recording);
            s_videoFile->API->Open(s_videoFile, false);
            UploadVideo(s_videoFile);
        } else {
            break;
        }
        return 0;
    } while (false);

    iShell->Print(hShell, "Usage:\nmp4 [start|stop]\nmp4 url <http://example.com>\n");
    return 0;
}

static int Command_Stream(LTShell hShell, int argc, const char *argv[]){
    s_Recorder->Start(false); // Start recording pipeline (without preroll) in case it has not been started

    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    do {
        if (argc < 2 || 3 < argc) break;
        
        if (lt_strncmp(argv[1], "url", 3) == 0 && argc == 3) {
            // Set the upload url
            ltstring_set(&s_httpUploadUrl, argv[2]);
            iShell->Print(hShell, "Finished MP4 will be uploaded to: \"%s\"\n", argv[2]);
        } else if (lt_strncmp(argv[1], "start", 5) == 0) {
            // Start recording
            iShell->Print(hShell, "Begin recording...\n");
            s_videoFile = lt_createobject_typed(LTFile, LTMemoryFile);
            s_recording = s_Recorder->CreateRecording(NULL, (LTMediaMP4Recording_Config){.audio.enabled = true, .video.enabled = true });
            if (!s_recording) {
                LTLOG_YELLOWALERT("create.fail", "failed to create recording");
                break;
            }
            s_recording->API->Start(s_recording);
        } else if (lt_strncmp(argv[1], "stop", 4) == 0) {
            // Stop recording
            if (!s_videoFile) {
                iShell->Print(hShell, "No recording in progress\n");
            } 
            iShell->Print(hShell, "Stop and upload recording...\n");
            s_recording->API->Finish(s_recording);
        } else {
            break;
        }
        return 0;
    } while (false);

    iShell->Print(hShell, "Usage:\nstream [start|stop]\nstream url <http://example.com>\n");
    return 0;
}

static const LTSystemShell_CommandDesc s_Mp4Commands[] = {
    { "mp4",         Command_Mp4,       "mp4",    NULL },
    { "stream",      Command_Stream,    "stream", NULL },
};

static void LTShellMediaMP4RecorderImpl_LibFini(void) {
    s_Shell->UnregisterCommands(s_Mp4Commands);
    ltstring_destroy(s_httpUploadUrl);

    lt_closelibrary(s_Shell);
    s_Shell = NULL;
    lt_closelibrary(s_Http);
    s_Http = NULL;
    lt_closelibrary(s_Recorder);
    s_Recorder = NULL;
}

static bool LTShellMediaMP4RecorderImpl_LibInit(void) {
    do {
        s_Shell = lt_openlibrary(LTSystemShell);
        if (!s_Shell) break;
        
        s_Http = lt_openlibrary(LTNetHttpClient);
        if (!s_Http) break;

        s_Recorder = lt_openlibrary(LTMediaMP4Recorder);
        if (!s_Recorder) break;
        
        s_Shell->RegisterCommands(s_Mp4Commands, sizeof(s_Mp4Commands)/sizeof(s_Mp4Commands[0]));
        
        // Default upload url placeholder
        s_httpUploadUrl = ltstring_create("http://192.168.0.101:8000");
        if (!s_httpUploadUrl) break;

        return true;
    } while (false);
    LTShellMediaMP4RecorderImpl_LibFini();
    return false;
}

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellMediaMP4Recorder, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellMediaMP4Recorder) LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  31-May-24   aurelian    created
 *  22-Jul-24   decius      added streaming function
 */