/*******************************************************************************
 * Shell handlers for video driver
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/httpclient/LTNetHttpClient.h>
#include <lt/media/motiondetection/LTMediaMotionDetection.h>
#include <lt/product/config/LTProductConfig.h>
#include <lt/system/ipc/buffermanager/LTIPCBufferManager.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

DEFINE_LTLOG_SECTION("shell.video");

#define MSS (u32)800
#define MAX_SOCKSPEC_STRING_SIZE    256


/*******************************************************************************
 * Static Variables & Types
 ******************************************************************************/

enum {
    kMinBitRate_SD = 150000,
    kMinBitrate_HD = 700000,
    kMaxBitRate = 2000000,
    kMaxPendingByteCount = 512*1024,
};

typedef struct {
    LTList_Node node;
    u8 *pData;
    u32 nDataLen;
    u32 nDataSent;
    bool bEndOfFrame;
} StreamingData;

/* State variables for local streaming instance (audio or video) */
typedef struct {
    LTMediaFormat *streamFormat;
    LTOThread *streamThread;
    LTMediaSource hMediaSource;
    LTSocket hStreamSocket;
    bool socketReady;
    bool startedSending;
    LTList pendingFrames;
    u32 pendingByteCount;   // Total number of bytes in the s_PendingFrames list
    char *tempSockSpec;     // Socket spec for stream - only valid when socket is first opened
} StreamingContext;

/* Static references to LT libraries and interfaces, opened for long periods of time */
static struct Statics {
    LTCore *core;
    LTSystemShell *shell;
    LTUtilityByteOps *byteOps;
    LTDeviceMedia *deviceMedia;
    LTNetCore *netCore;
    LTNetHttpClient *netHttpClient;
    LTMediaMotionDetection *motionDetection;
    LTTransport netTransport;
    LTIPCBufferManager *bufManager;

    ILTShell *iShell;
    ILTMediaSource *iMediaSourceSnapshot;
} S;

/* Media format selection for snapshot and streaming functions.
 * Snapshots default to HD JPEG, and it remembers the last user format selection between commands.
 */
static LTMediaFormat SnapshotFormat = {
    .nKind = kLTMediaKind_Image_HD,
    .nEncoding = kLTMediaEncoding_Jpeg,
};

static LTMediaFormat VideoStreamFormat = {
    .nKind = kLTMediaKind_Video_HD,
    .nEncoding = kLTMediaEncoding_H264,
    .params.h264.nProfile = 77, // Main profile
};

// Audio stream format - dynamically configured based on command arguments
static LTMediaFormat AudioStreamFormat = {
    .nKind = kLTMediaKind_Audio,
    .nEncoding = kLTMediaEncoding_PCM,
    .params.pcm.nSampleRate = 16000,
    .params.pcm.nBitsPerSample = 16,
    .params.pcm.nChannels = 1,
};

/**
 * Get audio stream format based on format string
 *
 * Supports MediaSourceManager's multi-format architecture:
 * - 16khz-pcm: Raw 16kHz PCM (format 0 - kAudio16KHzPCM)
 * - 8khz-pcm: Processed 8kHz PCM (format 2 - kAudio8KHzPCM)
 * - 8khz-raw: Raw 8kHz PCM (format 4 - kAudio8KHzRawPCM) [DISABLED - no direct access method]
 * - 8khz-g711: Processed 8kHz G.711 (format 3 - kAudio8KHzuPCM)
 * - 16khz-g711: Raw 16kHz G.711 (format 1 - kAudio16KHzuPCM)
 */
static bool ConfigureAudioStreamFormat(const char* formatStr) {
    if (!formatStr || 0 == lt_strcasecmp(formatStr, "16khz-pcm")) {
        // Raw 16kHz PCM - full bandwidth hardware audio
        AudioStreamFormat.nKind = kLTMediaKind_Audio;
        AudioStreamFormat.nEncoding = kLTMediaEncoding_PCM;
        AudioStreamFormat.params.pcm.nSampleRate = 16000;
        AudioStreamFormat.params.pcm.nBitsPerSample = 16;
        AudioStreamFormat.params.pcm.nChannels = 1;
        return true;
    }
    else if (0 == lt_strcasecmp(formatStr, "8khz-pcm")) {
        // Processed 8kHz PCM - downsampled + AEC + AGC + HPF
        AudioStreamFormat.nKind = kLTMediaKind_Audio;
        AudioStreamFormat.nEncoding = kLTMediaEncoding_PCM;
        AudioStreamFormat.params.pcm.nSampleRate = 8000;
        AudioStreamFormat.params.pcm.nBitsPerSample = 16;
        AudioStreamFormat.params.pcm.nChannels = 1;
        return true;
    }
    /* DISABLED: 8khz-raw format
     * Raw 8kHz PCM is dispatched internally by MediaSourceManager (kAudio8KHzRawPCM)
     * but there's no direct way for consumers to request it via LTMediaFormat.
     * Both 8khz-pcm and 8khz-raw would map to the same LTMediaFormat specification.
     * To access raw 8kHz, would need to extend MediaSourceManager interface.
     */
    else if (0 == lt_strcasecmp(formatStr, "8khz-raw")) {
        return false;  // Disabled - no direct access method available
    }
    else if (0 == lt_strcasecmp(formatStr, "8khz-g711")) {
        // Processed 8kHz G.711 μ-law - same as livestream format
        AudioStreamFormat.nKind = kLTMediaKind_Audio;
        AudioStreamFormat.nEncoding = kLTMediaEncoding_PCMU;
        AudioStreamFormat.params.g711.nSampleRate = 8000;
        return true;
    }
    else if (0 == lt_strcasecmp(formatStr, "16khz-g711")) {
        // Raw 16kHz G.711 μ-law - full bandwidth encoded
        AudioStreamFormat.nKind = kLTMediaKind_Audio;
        AudioStreamFormat.nEncoding = kLTMediaEncoding_PCMU;
        AudioStreamFormat.params.g711.nSampleRate = 16000;
        return true;
    }

    return false; // Invalid format
}

static const char s_FilenameTypeJpeg[] = "jpg";
static const char s_ContentTypeJpeg[] = "image/jpeg";
static const char s_FilenameTypeRaw[] = "raw";
static const char s_ContentTypeRaw[] = "image/x-dcraw";
static const char s_FilenameTypeJson[] = "json";
static const char s_ContentTypeJson[] = "application/json";

/* State variables for local snapshot upload */
static LTMediaSource MediaSourceSnapshot = 0;
static LTString s_SnapshotUploadUrl = NULL;
static u8 *s_LocalSnapshot;
static LTBufferID s_BufferedSnapshotID = LT_BUFFER_ID_INVALID;
static LTBuffer *s_BufferedSnapshot = NULL;
static u32 s_BufferedLenRemaining = 0;
static LTString s_pFilename = NULL;
static LTString s_pMetadataFilename = NULL;
static LTString s_pMetadataString = NULL;
static LTHttpRequest hRequest = 0;

/* State variables for local video and audio streaming */
static StreamingContext s_VideoStreamContext = {
    .streamFormat = &VideoStreamFormat,
};
static StreamingContext s_AudioStreamContext = {
    .streamFormat = &AudioStreamFormat,
};

static bool s_IsPanCamera;


/*******************************************************************************
 * Socket Connection Management
 ******************************************************************************/

/* Cutdown version of StringToIPAddress from LTNetHttpClientImpl.c that
 * checks if the supplied server is an IP address or not.
 */
static bool IsIPAddress(const char *str) {
    int dotQuadCount = 1;
    bool quad_started = false;
    int len = lt_strlen(str);

    while (len--) {
        char c = *str++;
        if (c>='0' && c<='9') {
            quad_started = true;
        }
        else if (c=='.') {
            if(!quad_started) {
                return false;
            }
            quad_started = false;
            dotQuadCount++;
        } else {
            return false;
        }
    }
    if(dotQuadCount==4 && quad_started) return true;
    return false;
}

static void PollPendingFrames(StreamingContext *ctx) {
    bool partialWrite = false;
    while (!partialWrite && !LTList_IsEmpty(&ctx->pendingFrames)) {
        /* Peek at the head element to see what is left to do */
        LTList_Node *pNode = ctx->pendingFrames.pNext;
        StreamingData *dataPacket = LT_CONTAINER_OF(pNode, StreamingData, node);

        if (dataPacket->nDataSent < dataPacket->nDataLen) {
            ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, ctx->hStreamSocket);
            u32 nRemaining = dataPacket->nDataLen - dataPacket->nDataSent;
            u32 nWritten = pSocket->WriteSocket(ctx->hStreamSocket, &dataPacket->pData[dataPacket->nDataSent], LT_MIN(nRemaining, MSS));
            dataPacket->nDataSent += nWritten;
            if (nWritten < nRemaining) {
                /* Socket couldn't absorb all data so stop processing the list until we get another Write Ready event */
                partialWrite = true;
            }
            if (nWritten && !ctx->startedSending) {
                ctx->startedSending = true;
                LTLOG("stream.start", "Started sending %s stream", (ctx->streamFormat->nKind == kLTMediaKind_Audio) ? "audio" : "video");
            }
        }

        /* If the packet has been completely sent then we can free it up */
        if (dataPacket->nDataSent >= dataPacket->nDataLen) {
            ctx->pendingByteCount -= dataPacket->nDataLen;
            LTList_Remove(&dataPacket->node);
            lt_free(dataPacket);
        }
    }
}

static void TryToQueueFrame(StreamingData *newFrame, StreamingContext *ctx) {
    // TBD: max byte count should come from streaming context, as audio probably needs to be smaller than video
    if ((ctx->pendingByteCount + newFrame->nDataLen) < kMaxPendingByteCount) {
        ctx->pendingByteCount += newFrame->nDataLen;
        LTList_AddTail(&ctx->pendingFrames, &newFrame->node);
    } else {
        LTLOG_YELLOWALERT("stream.pending.full", "Pending byte count exceeded: %lu", LT_Pu32(ctx->pendingByteCount));
        lt_free(newFrame);
        // This is probably an issue with the socket, so these frames could be stuck on the list.
        // We shouldn't keep stale data in the pending frames list.
        //
        // We will discard untransmitted frames and resume transmission when the socket becomes available again.
        // If we have a partially transmitted frame at the head of the list, we should resume transmission
        // of that frame when the socket becomes available again.
        // To keep that partially transmitted frame and only discard the other frames on the list, we will:
        // 1. Temporarily remove the partially transmitted frame from the list and save it
        // 2. Discard all the other frames on the list, emptying the list in the process
        // 3. Add the partially transmitted frame back onto the list.
        bool headElementIsPartiallyTransmitted = false;
        LTList_Node *pHeadNode = ctx->pendingFrames.pNext;
        StreamingData* head = (StreamingData*) LT_CONTAINER_OF(pHeadNode, StreamingData, node);
        if (head->nDataSent > 0) {
            headElementIsPartiallyTransmitted = true;
            // we are just about to empty the entire list, but we want to keep this 'head' element,
            // so temporarily remove it, and add it back after we've emptied the list
            LTList_Remove(pHeadNode);
        }
        while (!LTList_IsEmpty(&ctx->pendingFrames)) {
            // Peek at the head element (NB: In LTList the next pointer points to the beginning of the list),
            // we'll remove from the head until the list is empty
            LTList_Node *pNode = ctx->pendingFrames.pNext;
            StreamingData* current = (StreamingData*) LT_CONTAINER_OF(pNode, StreamingData, node);
            ctx->pendingByteCount -= current->nDataLen;
            LTList_Remove(pNode);
            lt_free(current);
        }
        if (headElementIsPartiallyTransmitted) {
            LTList_AddTail(&ctx->pendingFrames, pHeadNode);
        }
    }
}

static void MediaSourceStreamProc(LTMediaEvent event, void *eventData, void *clientData) {
    StreamingContext *ctx = clientData;
    if (event != kLTMediaEvent_Data) { return; }
    if (!ctx || !ctx->socketReady) { return; }

    LTMediaData * data = eventData;
    LT_SIZE allocSize = sizeof(StreamingData) + data->nDataLen;
    bool bNeedsStartCode = false; // H264 stream only
    u8 h264StartCode[4] = { 0x00, 0x00, 0x00, 0x01 };

    if (ctx->streamFormat->nEncoding == kLTMediaEncoding_H264) {
        /* The H264 encoder can send multiple packets for a single polling period. This
        * will result in multiple rapid calls to this handler function. Each individual
        * packet needs to be encapsulated and may be sent in fragments over the socket.
        *
        * Pending frames are buffered locally in a linked list. The list has a maximum
        * size (measured in number of video data bytes stored) to prevent memory exhaustion.
        *
        */

        /* On some platforms (e.g. Alta) the frame start code does not get inserted into the local stream because it is a
        * raw H264 stream (as opposed to a framed WebRTC stream) so we have to add it. */
        if (data->nDataLen >= sizeof(h264StartCode)) {
            bNeedsStartCode = (lt_memcmp(data->pData, h264StartCode, sizeof(h264StartCode)) != 0);
        }

        if (bNeedsStartCode) { allocSize += sizeof(h264StartCode); }
    }

    StreamingData *dataPacket = lt_malloc(allocSize);
    if (!dataPacket) {
        LTLOG_YELLOWALERT("stream.alloc.err", "Out of memory: %lu", LT_PLT_SIZE(allocSize));
        return;
    }

    u8 *base = (u8 *)dataPacket + sizeof(StreamingData);
    dataPacket->pData = base;
    dataPacket->nDataLen = data->nDataLen;

    /* Inserting the start code */
    if (bNeedsStartCode) {
        lt_memcpy(base, h264StartCode, sizeof(h264StartCode));
        base += sizeof(h264StartCode);
        dataPacket->nDataLen += sizeof(h264StartCode);
    }

    lt_memcpy(base, data->pData, data->nDataLen);
    dataPacket->nDataSent = 0;
    dataPacket->bEndOfFrame = data->bEndOfFrame;

    TryToQueueFrame(dataPacket, ctx);
    if (ctx->socketReady) PollPendingFrames(ctx);
}

static void DestroyMediaSource(StreamingContext *ctx) {
    if (ctx->hMediaSource) {
        ILTMediaSource *iMediaSource = lt_gethandleinterface(ILTMediaSource, ctx->hMediaSource);
        iMediaSource->NoMediaEvent(ctx->hMediaSource, &MediaSourceStreamProc);
        S.core->DestroyHandle(ctx->hMediaSource);    // implicit Stop on Destroy
        ctx->hMediaSource = 0;
    }
}

static void OpenMediaSource(StreamingContext *ctx) {
    DestroyMediaSource(ctx);  // unexpected need to clean up old source
    ctx->hMediaSource = S.deviceMedia->OpenSource(ctx->streamFormat);
    if (ctx->hMediaSource) {
        ILTMediaSource *iMediaSource = lt_gethandleinterface(ILTMediaSource, ctx->hMediaSource);

        // Set a sensible default video bitrate based on requested stream
        if (ctx->streamFormat->nKind == kLTMediaKind_Video_SD) {
            iMediaSource->SetTargetBitrate(ctx->hMediaSource, kMinBitrate_HD / 2);
        } else if (ctx->streamFormat->nKind == kLTMediaKind_Video_HD) {
            iMediaSource->SetTargetBitrate(ctx->hMediaSource, kMaxBitRate / 2);
        }

        // Register for media events then start the stream
        iMediaSource->OnMediaEvent(ctx->hMediaSource, &MediaSourceStreamProc, ctx);
        iMediaSource->Start(ctx->hMediaSource);
    }
}

static void OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    StreamingContext *ctx = (StreamingContext *)clientData;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, hSocket);

    switch (event) {
        case kLTSocket_Event_DnsResolved:
        case kLTSocket_Event_SocketReady:
            LTLOG("sock.event", "Socket event: %d", event);
            break;

        case kLTSocket_Event_Connected:
            /* Request Media Events for stream */
            OpenMediaSource(ctx);
            LTLOG("sock.connected", "Connected to local server");
            ctx->startedSending = false;
            break;

        case kLTSocket_Event_WriteReady:
            if (!ctx->socketReady) { LTLOG("sock.write.ready", "Ready to send"); }
            ctx->socketReady = true;
            PollPendingFrames(ctx);
            break;

        case kLTSocket_Event_ReadReady:
            /* We don't expect to receive any data, so ignore read events and discard pending data */
            while (pSocket->ReadSocket(hSocket, NULL, 1024) > 0) {};
            break;

        case kLTSocket_Event_Disconnected:
            DestroyMediaSource(ctx);
            S.core->DestroyHandle(ctx->hStreamSocket);
            ctx->socketReady = false;
            ctx->startedSending = false;
            ctx->hStreamSocket = 0;
            LTLOG("sock.disconnected", "Disconnected from local server");
            break;

        case kLTSocket_Event_Error:
        case kLTSocket_Event_SocketError:
        case kLTSocket_Event_WriteError:
        case kLTSocket_Event_ReadError:
            if (pSocket) pSocket->DisconnectSocket(hSocket);
            ctx->socketReady = false;
            ctx->hStreamSocket = 0;
            /* FALLTHROUGH */

        case kLTSocket_Event_DnsError:
        case kLTSocket_Event_DnsTimeout:
        case kLTSocket_Event_ConnectError:
            LTLOG("sock.error", "socket %02lx event %02lx", LT_Pu32(hSocket), LT_Pu32(event));
            break;

        case kLTSocket_Event_ConnectTimeout:
            LTLOG("sock.connect.failed", "Unable to connect to specified host");
            break;

        default:
            LTLOG("sock.event.unhandled", "socket %02lx event 0x%02x", LT_Pu32(hSocket), event);
            break;
    }
}

static void CleanupSnapshot(void) {
    if (s_LocalSnapshot || s_BufferedSnapshotID) {
        LTLOG_DEBUG("snap.cleanup", "Freed motion snapshot data");
        if (s_BufferedSnapshotID) {
            lt_destroyobject(s_BufferedSnapshot);
            s_BufferedSnapshot = NULL;
            S.bufManager->API->DestroySourceBuffer(s_BufferedSnapshotID);
            s_BufferedSnapshotID = LT_BUFFER_ID_INVALID;
        }
        lt_free(s_LocalSnapshot);
        s_LocalSnapshot = NULL;
        ltstring_destroy(s_pFilename);
        s_pFilename = NULL;
        ltstring_destroy(s_pMetadataFilename);
        s_pMetadataFilename = NULL;
        ltstring_destroy(s_pMetadataString);
        s_pMetadataString = NULL;
    }
}

static void OnRequestEvent(LTHttpRequest hRequest, LTHttpRequestEvent event, void * pClientData) {
    LT_UNUSED(pClientData);
    #define DUMMY_BUF_SIZE 128
    static u8 dummyBuf[DUMMY_BUF_SIZE];

    ILTHttpRequest *pIHttpRequest = lt_gethandleinterface(ILTHttpRequest, hRequest);

    if (event != LTHttpRequestEvent_BodyWriteReady) LTLOG_DEBUG("event.http", "OnRequestEvent: %d", event);
    u32 nStatusCode = 0;
    switch(event) {
        case LTHttpRequestEvent_HeadComplete:
            nStatusCode = pIHttpRequest->GetResponseStatusCode(hRequest);
            LTLOG_DEBUG("event.head", "Head: Status Code: %lu. Resp Len: %lu", LT_Pu32(nStatusCode), LT_Pu32(pIHttpRequest->GetResponseContentLength(hRequest)));
            if (nStatusCode == 200) {
                LTLOG_DEBUG("event.head.succ", "File upload successful");
            } else {
                LTLOG("event.head.err", "File upload failed with status code %lu", LT_Pu32(nStatusCode));
                CleanupSnapshot();
                lt_destroyhandle(hRequest);
            }
            break;

        case LTHttpRequestEvent_BodyWriteReady:
            // Called when processing data wrapped in an LTBuffer
            if (s_BufferedSnapshot) {
                // LTBuffer access, so we can just use streaming reads from the buffer
                // until the socket is full or the upload is finished
                u32 toWrite = (u32)(LT_MIN(MSS, s_BufferedLenRemaining));
                LTBufferAccess access = s_BufferedSnapshot->API->StartRead(s_BufferedSnapshot, toWrite);
                if (!access.span.data) {
                    // Fatal error, abort request
                    s_BufferedSnapshot->API->FinishRead(s_BufferedSnapshot, access);
                    LTLOG_YELLOWALERT("bodywrite.img.buf.fail", "Failed to read from buffer 0x%08lx", LT_Pu32(s_BufferedSnapshotID));
                    CleanupSnapshot();
                    lt_destroyhandle(hRequest);
                    break;
                }
                if (!access.span.size) {
                    // No data available, wait for next event
                    LTLOG_YELLOWALERT("span.zero", "Span size is zero, upload will stall");
                    s_BufferedSnapshot->API->FinishRead(s_BufferedSnapshot, access);
                    break;
                }

                s32 bytesWritten = pIHttpRequest->WriteBodyData(hRequest, access.span.data, access.span.size);
                if (bytesWritten < 0) {
                    // On error, a YELLOWALERT is logged in WriteBodyData
                    // and the next event this callback receives will be LTHttpRequestEvent_Error,
                    // resulting in cleanup of the current outgoing request.
                    access.span.size = 0;
                    s_BufferedSnapshot->API->FinishRead(s_BufferedSnapshot, access);
                    break;
                }

                access.span.size = (u32)bytesWritten;
                s_BufferedSnapshot->API->FinishRead(s_BufferedSnapshot, access);
                s_BufferedLenRemaining -= bytesWritten;
                if (0) {
                    static u32 prevLog = 0;
                    if (!prevLog) prevLog = s_BufferedLenRemaining;
                    if ((s_BufferedLenRemaining > 10240 && s_BufferedLenRemaining + 10240 < prevLog) ||
                            (s_BufferedLenRemaining < 10240 && s_BufferedLenRemaining + 1024 < prevLog) || s_BufferedLenRemaining < 1024) {
                        prevLog = s_BufferedLenRemaining;
                        LTLOG("snap.up", "%lu bytes remaining", LT_Pu32(s_BufferedLenRemaining));
                    }
                }
            }
            break;

        case LTHttpRequestEvent_BodyDataAvailable:
            /* We don't care about the HTTP response code - read and discard the response body data */
            pIHttpRequest->ReadBodyData(hRequest, dummyBuf, DUMMY_BUF_SIZE);
            break;

        case LTHttpRequestEvent_RequestComplete:
            LTLOG("snap.done", "Request completed successfully");
            /* FALLTHROUGH */

        default:
            /* In all other cases the request has completed or errored, and we can free up resources */
            CleanupSnapshot();
            lt_destroyhandle(hRequest);
            break;
    }
}

static void InitiateSnapshotUpload(u32 snapshotSize, LTMediaEncoding snapshotEncoding) {
    if (!s_pMetadataFilename || !s_pMetadataString) {
        LTLOG_YELLOWALERT("snapshot.metadata.err", "Missing metadata for snapshot upload.");
        CleanupSnapshot();
        return;
    }

    // The HTTP request now looks very similar to the form data used by IotServiceMediaUpload. This allows
    // the local upload server to receive snapshots either manually-triggered or uploaded by that library.
    LTHttpMultipartData formData[] = {
        {
            .name        = "metadata",
            .contentType = s_ContentTypeJson,
            .filename    = s_pMetadataFilename,
            .data        = s_pMetadataString,
            .size        = lt_strlen(s_pMetadataString),
        },
        // frameMeta not included in manual uploads
        {
            .name        = "file",
            .filename    = s_pFilename,
            .data        = s_LocalSnapshot,
            .size        = snapshotSize,
        },
    };

    if (snapshotEncoding == kLTMediaEncoding_Raw) {
        formData[1].contentType = s_ContentTypeRaw;
    } else {
        formData[1].contentType = s_ContentTypeJpeg;
    }

    /* Build and send the HTTP request */
    hRequest = S.netHttpClient->CreateRequest(s_SnapshotUploadUrl, kLTHttpRequestMethod_POST, S.netTransport);
    ILTHttpRequest *pIHttpRequest = lt_gethandleinterface(ILTHttpRequest, hRequest);
    pIHttpRequest->SetBody(hRequest, "multipart/form-data", formData, sizeof(formData) / sizeof(LTHttpMultipartData));
    pIHttpRequest->OnRequestEvent(hRequest, OnRequestEvent, NULL);
    if (!pIHttpRequest->SendRequest(hRequest)) {
        LTLOG_YELLOWALERT("snapshot.proc.send.err", "SendRequest failed for snapshot.");
        CleanupSnapshot();
        lt_destroyhandle(hRequest);
    } else {
        LTLOG("snap.uploading", NULL);
    }
}

static void MediaSourceSnapshotProc(LTMediaEvent event, void *eventData, void *clientData) {
    LT_UNUSED(clientData);
    if (!s_SnapshotUploadUrl) { return; }
    if (event == kLTMediaEvent_Error || !eventData) { return; }

    if (s_LocalSnapshot != NULL || s_BufferedSnapshotID != LT_BUFFER_ID_INVALID) {
        LTLOG_YELLOWALERT("snapshot.in.progress", "Error: Snapshot upload already in progress.");
        return;
    }

    /* Copy LTMediaData JPEG information to locally-owned buffer (as the JPEG data will expire
     * after this event has been handled). If the snapshot type is raw then we can't copy it,
     * as it's too large for available system memory so we ignore the event.
     */
    if (SnapshotFormat.nEncoding != kLTMediaEncoding_Jpeg) {
        LTLOG_YELLOWALERT("snapshot.format.error", "Unsupported encoding for snapshot: %d", (int)SnapshotFormat.nEncoding);
        return;
    }

    LTMediaData *Snapshot = eventData;

    if (event == kLTMediaEvent_RemoteBuffer) {
        s_BufferedSnapshotID = Snapshot->ipcBufferID;
        s_BufferedSnapshot = S.bufManager->API->AccessBuffer(s_BufferedSnapshotID);
        if (!s_BufferedSnapshot) {
            S.bufManager->API->DestroySourceBuffer(s_BufferedSnapshotID);
            s_BufferedSnapshotID = LT_BUFFER_ID_INVALID;
            return;
        }
        s_BufferedLenRemaining = Snapshot->nDataLen;

    } else {
        s_LocalSnapshot = lt_malloc(Snapshot->nDataLen);
        if (!s_LocalSnapshot) {
            LTLOG_YELLOWALERT("snapshot.alloc.err", "Error: Failed to allocate buffer (%u) for snapshot upload.", (unsigned int)Snapshot->nDataLen);
            return;
        }

        lt_memcpy(s_LocalSnapshot, Snapshot->pData, Snapshot->nDataLen);
    }

    /* Attempt to send the snapshot to a local HTTP server endpoint.
     * We do not currently check the network state before attempting to send,
     * as it's easy to just try, and get an error back if something is not configured correctly.
     */

    const char *pFilenameType = s_FilenameTypeJpeg;
    if (SnapshotFormat.nEncoding == kLTMediaEncoding_Raw) {
        pFilenameType = s_FilenameTypeRaw;
    }
    s_pFilename = ltstring_create("");
    ltstring_format(&s_pFilename, "snap-%lu.%s", LT_Pu32(Snapshot->nTimestamp), pFilenameType);

    // Create dummy metadata for the image - this only needs to include the image size
    s_pMetadataFilename = ltstring_create("");
    ltstring_format(&s_pMetadataFilename, "snap-%lu.%s", LT_Pu32(Snapshot->nTimestamp), s_FilenameTypeJson);
    s_pMetadataString = ltstring_create("");

    LTMediaResolution imageResolution;
    if (SnapshotFormat.nKind == kLTMediaKind_Image_HD) {
        S.deviceMedia->GetProperty("resolution.hd", &imageResolution);
    }
    else if (SnapshotFormat.nKind == kLTMediaKind_Image_SD) {
        S.deviceMedia->GetProperty("resolution.sd", &imageResolution);
    }

    ltstring_appendformat(&s_pMetadataString,
        "{\n"
        "    \"curSnapshot\": 1,\n"
        "    \"maxSnapshots\": 1,\n"
        "    \"image\": { \"width\": %d, \"height\": %d }\n"
        "}\n",
        imageResolution.width, imageResolution.height);

    InitiateSnapshotUpload(Snapshot->nDataLen, SnapshotFormat.nEncoding);
}

static void StreamingStart(void *pClientData) {
    StreamingContext *ctx = pClientData;
    ctx->socketReady = false;
    ctx->hStreamSocket = S.netCore->OpenSocket(S.netTransport, ctx->tempSockSpec, OnSocketEvent, ctx);
    if (!ctx->hStreamSocket) {
        LTLOG("sock.init.fail", "Failed to open socket for streaming");
    }
}

static void RequestBitrate(void *pClientData) {
    // Only valid for video streaming just now
    StreamingContext *ctx = &s_VideoStreamContext;
    if (ctx->hMediaSource) {
        u32 requestedBitrate = (u32)((LT_SIZE)pClientData);
        ILTMediaSource *iMediaSource = lt_gethandleinterface(ILTMediaSource, ctx->hMediaSource);
        iMediaSource->SetTargetBitrate(ctx->hMediaSource, requestedBitrate);
    }
}

void StreamingStarted(LTThread_ReleaseReason releaseReason, void *pClientData) {
    LT_UNUSED(releaseReason);
    StreamingContext *ctx = pClientData;
    lt_free(ctx->tempSockSpec);
    ctx->tempSockSpec = NULL;
}

static void StreamingStop(void *pClientData) {
    StreamingContext *ctx = pClientData;

    DestroyMediaSource(ctx);
    if (ctx->hStreamSocket) {
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, ctx->hStreamSocket);
        pSocket->DisconnectSocket(ctx->hStreamSocket);
        S.core->DestroyHandle(ctx->hStreamSocket);
    }
    ctx->socketReady = false;
    ctx->hStreamSocket = 0;
}

static void OnMotionEvent(LTMediaMotionState motionState,
                          u8 *motionUuid,
                          LTMediaMotionDetection_Snapshot *motionSnapshot,
                          void *clientData) {
    LT_UNUSED(clientData);

    // If the motion event isn't a snapshot then return now
    if (motionState != kLTMediaMotionState_Snapshot) return;

    if (!s_SnapshotUploadUrl) {
        LTLOG_YELLOWALERT("motionsnap.url.missing", "Error: No URL for snapshot upload server.");
        return;
    }

    if (s_LocalSnapshot != NULL || s_BufferedSnapshotID != LT_BUFFER_ID_INVALID) {
        LTLOG_YELLOWALERT("motionsnap.in.progress", "Error: Snapshot upload already in progress.");
        return;
    }

    s_LocalSnapshot = lt_malloc(motionSnapshot->nImageLen);
    if (!s_LocalSnapshot) {
        LTLOG_YELLOWALERT("motionsnap.alloc.err", "Error: Failed to allocate buffer (%u) for snapshot upload.", (unsigned int)motionSnapshot->nImageLen);
        return;
    }
    lt_memcpy(s_LocalSnapshot, motionSnapshot->pImageData, motionSnapshot->nImageLen);

    const char *pFilenameType = s_FilenameTypeJpeg;
    if (SnapshotFormat.nEncoding == kLTMediaEncoding_Raw) {
        pFilenameType = s_FilenameTypeRaw;
    }

    char uuid[40];
    if (!S.byteOps->UUIDToString(motionUuid, uuid, sizeof(uuid))) {
        uuid[0] = '\0';
    }

    s_pFilename = ltstring_create("");
    ltstring_format(&s_pFilename, "snap-%s-%lu.%s", uuid, motionSnapshot->curSnapshotIndex, pFilenameType);

    s_pMetadataFilename = ltstring_create("");
    ltstring_format(&s_pMetadataFilename, "snap-%s-%lu.%s", uuid, motionSnapshot->curSnapshotIndex, s_FilenameTypeJson);
    s_pMetadataString = ltstring_create("");
    u32 moxelWidth = 0, moxelHeight = 0;
    if (motionSnapshot->pMotionROIs && motionSnapshot->pMotionROIs->numRegions > 0) {
        moxelWidth = motionSnapshot->pMotionROIs->motionFrameSize.width;
        moxelHeight = motionSnapshot->pMotionROIs->motionFrameSize.height;
    }
    ltstring_appendformat(&s_pMetadataString,
        "{\n"
        "    \"uuid\": \"%s\",\n"
        "    \"curSnapshot\": %d,\n"
        "    \"maxSnapshots\": %d,\n"
        "    \"image\": { \"width\": %d, \"height\": %d },\n"
        "    \"moxelFrame\": { \"width\": %d, \"height\": %d },\n"
        "    \"nvision\": %s,\n",
        uuid, motionSnapshot->curSnapshotIndex + 1, motionSnapshot->maxSnapshotCount,
        motionSnapshot->resolution.width, motionSnapshot->resolution.height, moxelWidth, moxelHeight,
        motionSnapshot->bNightVisionActive ? "true" : "false");

    ltstring_appendformat(&s_pMetadataString, "    \"regions\": [\n");
    if (motionSnapshot->pMotionROIs && motionSnapshot->pMotionROIs->numRegions > 0) {
        for (u32 i=0; i<motionSnapshot->pMotionROIs->numRegions; i++) {
            if (i > 0) {
                ltstring_appendformat(&s_pMetadataString, ",\n");
            }
            LTMediaMotionDetection_Region *region = &motionSnapshot->pMotionROIs->region[i];
            ltstring_appendformat(&s_pMetadataString, "        { \"x1\": %d, \"y1\": %d, \"x2\": %d, \"y2\": %d }",
                                    region->x1, region->y1, region->x2, region->y2);
        }
    }
    ltstring_appendformat(&s_pMetadataString, "\n    ]\n}\n");

    InitiateSnapshotUpload(motionSnapshot->nImageLen, motionSnapshot->nEncoding);
}

static void MotionSnapEnable(void *pClientData) {
    LT_UNUSED(pClientData);

    if (S.motionDetection) {
        S.motionDetection->OnMotionEvent(OnMotionEvent, NULL, NULL);
        LTLOG("shell.snap.motion.en", "Local snapshot server upload enabled");
    }
}

static void MotionSnapDisable(void *pClientData) {
    LT_UNUSED(pClientData);

    if (S.motionDetection) {
        S.motionDetection->NoMotionEvent(OnMotionEvent);
        lt_closelibrary(S.motionDetection);
        S.motionDetection = NULL;
        LTLOG("shell.snap.motion.dis", "Local snapshot server upload disabled");
    }
}

static bool LTShellVideo_ThreadInit(void) {
    return true;
}

static void LTShellVideo_ThreadExit(void) {
    if (S.motionDetection) MotionSnapDisable(NULL);
}

static bool LTShellAudio_ThreadInit(void) {
    return true;
}

static void LTShellAudio_ThreadExit(void) {
}

void LTShellVideoDriverImpl_SetMotionSnapshotURL(const char *pSnapshotURL) {
    if (s_SnapshotUploadUrl) {
        ltstring_destroy(s_SnapshotUploadUrl);
        s_SnapshotUploadUrl = NULL;
    }

    if (pSnapshotURL) {
        s_SnapshotUploadUrl = ltstring_create(pSnapshotURL);

        if (!S.motionDetection) {
            S.motionDetection = lt_openlibrary(LTMediaMotionDetection);
        }
        if (!S.motionDetection) {
            LTLOG_YELLOWALERT("shell.snap.motdet", "ERROR: couldn't open motion detection library\n");
            return;
        }
        s_VideoStreamContext.streamThread->API->QueueTaskProcIfRequired(s_VideoStreamContext.streamThread, MotionSnapEnable, NULL, NULL);
    } else if (S.motionDetection) {
        /* Shutdown local server upload */
        s_VideoStreamContext.streamThread->API->QueueTaskProcIfRequired(s_VideoStreamContext.streamThread, MotionSnapDisable, NULL, NULL);
    }
}


/*******************************************************************************
 * Shell Interface
 ******************************************************************************/

/* Snapshot command handler.
 *
 * All command parameters are optional, and are used to set (or modify) the
 * resolution & image format for the snapshot, and the server upload URL.
 * The first time the command is issued the URL must be provided, then it
 * can be omitted on subsequent requests.
 */
/* ASPEN-7948: This command does not work on the Alta camera, with the snapshot transfer
 * to the local server getting stuck indefinitely. Further investigation needed! */
static int LTShellVideo_Snap(LTShell hShell, int argc, const char *argv[]) {
    bool formatChanged = false, motionChanged = false;

    if (s_LocalSnapshot != NULL || s_BufferedSnapshotID != LT_BUFFER_ID_INVALID) {
        S.iShell->Print(hShell, "ERROR: Snapshot upload already in progress.\n");
        return -1;
    }

    /* Process the optional arguments - we don't really care about duplication or ordering so don't check for that */
    for (int arg=1; arg<argc; arg++) {

        if (0 == lt_strcasecmp(argv[arg], "jpg")) {
            if (SnapshotFormat.nEncoding != kLTMediaEncoding_Jpeg) {
                SnapshotFormat.nEncoding = kLTMediaEncoding_Jpeg;
                formatChanged = true;
            }
        } else if (0 == lt_strcasecmp(argv[arg], "raw")) {
            /* Raw upload feasture is not currently supported, due to size and need to copy */
            /* TODO: Support for raw upload is experimentally available in git branch user/nstewart/dev/raw_snapshot */
            S.iShell->Print(hShell, "ERROR: Raw upload not currently supported\n");
            return -1;

            if (SnapshotFormat.nEncoding != kLTMediaEncoding_Raw) {
                SnapshotFormat.nEncoding = kLTMediaEncoding_Raw;
                formatChanged = true;
            }
        } else if (0 == lt_strcasecmp(argv[arg], "sd")) {
            if (SnapshotFormat.nKind != kLTMediaKind_Image_SD) {
                SnapshotFormat.nKind = kLTMediaKind_Image_SD;
                formatChanged = true;
            }

        } else if (0 == lt_strcasecmp(argv[arg], "hd")) {
            if (SnapshotFormat.nKind != kLTMediaKind_Image_HD) {
                SnapshotFormat.nKind = kLTMediaKind_Image_HD;
                formatChanged = true;
            }
        } else if (0 == lt_strcasecmp(argv[arg], "motion")) {
            if (!S.motionDetection) {
                S.motionDetection = lt_openlibrary(LTMediaMotionDetection);
                if (!S.motionDetection) {
                    S.iShell->Print(hShell, "ERROR: couldn't open motion detection library\n");
                    return -1;
                }
                s_VideoStreamContext.streamThread->API->QueueTaskProcIfRequired(s_VideoStreamContext.streamThread, MotionSnapEnable, NULL, NULL);
                motionChanged = true;
            } else {
                S.iShell->Print(hShell, "Motion detection snapshots already enabled\n");
                return -1;
            }
        } else if (0 == lt_strcasecmp(argv[arg], "nomotion")) {
            if (S.motionDetection) {
                s_VideoStreamContext.streamThread->API->QueueTaskProcIfRequired(s_VideoStreamContext.streamThread, MotionSnapDisable, NULL, NULL);
                motionChanged = true;
            } else {
                S.iShell->Print(hShell, "Motion detection snapshots already disabled\n");
                return -1;
            }
        } else {
            /* Everything else is assumed to be a server address */
            if (s_SnapshotUploadUrl) {
                ltstring_destroy(s_SnapshotUploadUrl);
            }
            s_SnapshotUploadUrl = ltstring_create(argv[arg]);
        }
    }

    if (motionChanged && formatChanged) {
        S.iShell->Print(hShell, "WARNING: changing motion detection and format at the same time not recommended\n");
    }

    if (!s_SnapshotUploadUrl) {
        S.iShell->Print(hShell, "ERROR: no local upload server provided\n");
        return -1;
    }

    if (formatChanged && MediaSourceSnapshot) {
        /* Media source is already open but needs to change, so close the old one first */
        S.core->DestroyHandle(MediaSourceSnapshot);
        MediaSourceSnapshot = 0;
        S.iMediaSourceSnapshot = NULL;
    }

    if (!MediaSourceSnapshot && S.deviceMedia) {
        MediaSourceSnapshot = S.deviceMedia->OpenSource(&SnapshotFormat);
        if (!MediaSourceSnapshot) {
            S.iShell->Print(hShell, "ERROR: couldn't open media source (%lu,%lu)\n", LT_Pu32(SnapshotFormat.nEncoding), LT_Pu32(SnapshotFormat.nKind));
            return -1;
        }

        /* Register for events when this image type is generated (this includes snapshots
         * triggered by the motion detection library)
         */
        S.iMediaSourceSnapshot = lt_gethandleinterface(ILTMediaSource, MediaSourceSnapshot);
        S.iMediaSourceSnapshot->OnMediaEvent(MediaSourceSnapshot, &MediaSourceSnapshotProc, NULL);
    }

    /* Everything is configured by now, so request a snapshot if user requested immediate snapshot */
    if (!motionChanged || formatChanged) {
        S.iShell->Print(hShell, "Triggering snapshot\n");
        if (S.iMediaSourceSnapshot) {
            S.iMediaSourceSnapshot->Trigger(MediaSourceSnapshot);
        } else {
            S.iShell->Print(hShell, "ERROR: no media source available for snapshot\n");
            return -1;
        }
    }

    return 0;
}

static void HandleShellStreamStop(LTShell hShell, StreamingContext *ctx) {
    const char *pStreamType = (ctx->streamFormat->nEncoding == kLTMediaEncoding_H264) ? "video" : "audio";

    if (ctx->hStreamSocket) {
        ctx->streamThread->API->QueueTaskProcIfRequired(ctx->streamThread, StreamingStop, NULL, ctx);
        ILTThread *pILTThread = lt_getlibraryinterface(ILTThread, S.core);
        u32 deadlocked = 20;
        do {
            pILTThread->Sleep(LTTime_Milliseconds(50));
            deadlocked--;
        } while (ctx->socketReady && deadlocked);
        if (!deadlocked) {
            /* We ran out of attempts, so the socket didn't cleanly disconnect */
            LTLOG("sock.hung", "Streaming socket didn't cleanly disconnect");
            DestroyMediaSource(ctx);
            S.core->DestroyHandle(ctx->hStreamSocket);
            ctx->socketReady = false;
            ctx->hStreamSocket = 0;
        }
        S.iShell->Print(hShell, "Local %s streaming stopped\n", pStreamType);
    } else {
        S.iShell->Print(hShell, "Local %s streaming not running\n", pStreamType);
    }

}
/* Video streaming command handler.
 *
 * Start or stop video streaming to a local server.
 */
static int LTShellVideo_Stream(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2 || argc > 4) {
        S.iShell->Print(hShell, "ERROR: invalid arguments\n");
        return -1;
    }

    StreamingContext *ctx = &s_VideoStreamContext;

    if (0 == lt_strcasecmp(argv[1], "stop")) {
        /* Stop existing stream if running */
        HandleShellStreamStop(hShell, ctx);

    } else {
        // Try to start stream, and check for optional stream quality indicator
        int hostArg = 1;

        if (0 == lt_strcasecmp(argv[hostArg], "sd")) {
            VideoStreamFormat.nKind = kLTMediaKind_Video_SD;
            hostArg++;
        } else if (0 == lt_strcasecmp(argv[hostArg], "hd")) {
            VideoStreamFormat.nKind = kLTMediaKind_Video_HD;
            hostArg++;
        }

        const char *pHost = argv[hostArg++];
        const char *endpointType = IsIPAddress(pHost) ? "ip" : "host";

        const char *pPort;
        if (argc > hostArg) { pPort = argv[hostArg++]; } else { pPort = "8020"; }

        ctx->tempSockSpec = lt_malloc(MAX_SOCKSPEC_STRING_SIZE);
        lt_snprintf(ctx->tempSockSpec, MAX_SOCKSPEC_STRING_SIZE,  "tcp %s: %s port: %s", endpointType, pHost, pPort);

        /* Streaming is started from a separate thread to manage the socket interactions */
        S.iShell->Print(hShell, "Local %s video streaming requested: %s\n", (VideoStreamFormat.nKind == kLTMediaKind_Video_HD) ? "HD":"SD", ctx->tempSockSpec);
        ctx->streamThread->API->QueueTaskProcIfRequired(ctx->streamThread, StreamingStart, StreamingStarted, ctx);
    }

    return 0;
}

/* Video streaming bitrate command handler.
 */
static int LTShellVideo_Rate(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        S.iShell->Print(hShell, "ERROR: missing argument\n");
        return -1;
    }

    if (s_VideoStreamContext.hMediaSource == LTHANDLE_INVALID) {
        S.iShell->Print(hShell, "Local video streaming not running\n");
        return -1;
    }

    u32 requestedBitrate = 0;
    // SD and HD are used as shortcuts for the default (middle-ish of band) bitrates for the SD and HD streams
    if (0 == lt_strcasecmp(argv[1], "sd")) {
        requestedBitrate = kMinBitrate_HD / 2;
    } else if (0 == lt_strcasecmp(argv[1], "hd")) {
        requestedBitrate = kMaxBitRate / 2;
    } else {
        requestedBitrate = lt_strtou32(argv[1], NULL, 0);
    }

    if (requestedBitrate > 0) {
        if (requestedBitrate < kMinBitRate_SD) {
            S.iShell->Print(hShell, "WARNING! Requested bit rate is too low.\n");
            requestedBitrate = kMinBitRate_SD;
        } else if (requestedBitrate > kMaxBitRate) {
            S.iShell->Print(hShell, "WARNING! Requested bit rate is too high.\n");
            requestedBitrate = kMaxBitRate;
        }
        s_VideoStreamContext.streamThread->API->QueueTaskProcIfRequired(s_VideoStreamContext.streamThread, RequestBitrate, NULL, (void *)((LT_SIZE)requestedBitrate));
        S.iShell->Print(hShell, "Requesting bit rate: %lu (actual may be higher if other consumers running)\n", LT_Pu32(requestedBitrate));
    } else {
        S.iShell->Print(hShell, "ERROR: invalid bitrate ('sd', 'hd', or a decimal bps value)\n");
        return -1;
    }
    return 0;
}

/* Video pipeline statistics command handler.
 */
static int LTShellVideo_Stats(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argv);

    if (argc > 1) {
        S.iShell->Print(hShell, "ERROR: this command doesn't take arguments\n");
        return -1;
    }

    /* Get the number of pipeline (corresponding to different image resolutions) then alloc a stats struct */
    u32 numPipelines;
    if (!S.deviceMedia->GetProperty("pipeline.count", &numPipelines)) {
        S.iShell->Print(hShell, "ERROR: failed to get pipeline count\n");
        return -1;
    }

    LTMediaVideoPipelineStats *pipelineStats = lt_malloc(sizeof(LTMediaVideoPipelineStats) * numPipelines);
    if (!pipelineStats) {
        S.iShell->Print(hShell, "ERROR: failed to alloc pipeline stats struct\n");
        return -1;
    }

    if (!S.deviceMedia->GetProperty("pipeline.stats", pipelineStats)) {
        S.iShell->Print(hShell, "ERROR: failed to get pipeline stats\n");
        lt_free(pipelineStats);
        return -1;
    }

    for (u32 i = 0; i < numPipelines; ++i) {
        LTLOG("stats.pt", "Pipeline %lu", LT_Pu32(i));
        if (pipelineStats[i].pipelineEnabled & (1 << kLTMediaEncoding_Raw)) {
            LTLOG("stats.ft", "    Raw frame channel");
            LTLOG("stats.fe", "            Event count: %lu", LT_Pu32(pipelineStats[i].rawEventCount));
        }
        if (pipelineStats[i].pipelineEnabled & (1 << kLTMediaEncoding_H264)) {
            LTLOG("stats.ht", "    H.264 channel");
            LTLOG("stats.he", "            Event count: %lu", LT_Pu32(pipelineStats[i].h264EventCount));
        }
        if (pipelineStats[i].pipelineEnabled & (1 << kLTMediaEncoding_Motion)) {
            LTLOG("stats.mt", "    Motion channel");
            LTLOG("stats.mr", "        Reference count: %lu", LT_Pu32(pipelineStats[i].motionRefCount));
            LTLOG("stats.md", "            Event count: %lu", LT_Pu32(pipelineStats[i].motionEventCount));
        }
        if (pipelineStats[i].pipelineEnabled & (1 << kLTMediaEncoding_Jpeg)) {
            LTLOG("stats.jt", "    JPEG channel");
            LTLOG("stats.jd", "            Event count: %lu", LT_Pu32(pipelineStats[i].jpegEventCount));
        }
    }

    lt_free(pipelineStats);
    return 0;
}

/* IR cut filter command handler.
 */
static int LTShellVideo_IrCut(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        S.iShell->Print(hShell, "ERROR: missing argument\n");
        return -1;
    }

    if (0 == lt_strcmp(argv[1], "off")) {
        u32 filter = 0;
        S.deviceMedia->SetProperty("nightvision.filter", &filter);

    } else if (0 == lt_strcmp(argv[1], "on")) {
        u32 filter = 1;
        S.deviceMedia->SetProperty("nightvision.filter", &filter);

    } else {
        S.iShell->Print(hShell, "ERROR: unknown argument: %s\n", argv[1]);
        return -1;
    }

    return 0;
}

/* IR LEDs command handler.
 */
static int LTShellVideo_IrLed(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        S.iShell->Print(hShell, "ERROR: missing argument\n");
        return -1;
    }
    if (s_IsPanCamera) {
        if (0 == lt_strcmp(argv[1], "off")) {
            u32 irLed = 0;
            S.deviceMedia->SetProperty("nightvision.distance", &irLed);

        } else if (0 == lt_strcmp(argv[1], "on")) {
            u32 irLed = 1;
            S.deviceMedia->SetProperty("nightvision.distance", &irLed);

        } else {
            S.iShell->Print(hShell, "ERROR: unknown argument: %s\n", argv[1]);
            S.iShell->Print(hShell, "usage: video irled [off|on]\n");
            return -1;
        }
    } else {
        if (0 == lt_strcmp(argv[1], "off")) {
            u32 irLed = 0;
            S.deviceMedia->SetProperty("nightvision.distance", &irLed);

        } else if (0 == lt_strcmp(argv[1], "near")) {
            u32 irLed = 1;
            S.deviceMedia->SetProperty("nightvision.distance", &irLed);

        } else if (0 == lt_strcmp(argv[1], "far")) {
            u32 irLed = 2;
            S.deviceMedia->SetProperty("nightvision.distance", &irLed);

        } else if (0 == lt_strcmp(argv[1], "on")) {
            u32 irLed = 1;
            S.deviceMedia->SetProperty("nightvision.distance", &irLed);

        } else {
            S.iShell->Print(hShell, "ERROR: unknown argument: %s\n", argv[1]);
            S.iShell->Print(hShell, "usage: video irled [near|far|off|on]\n");
            return -1;
        }
    }

    return 0;
}

/* Nightvision mode command handler.
 */
static int LTShellVideo_NVision(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        S.iShell->Print(hShell, "ERROR: missing argument\n");
        return -1;
    }

    if (0 == lt_strcmp(argv[1], "day")) {
        u32 mode = 0;
        S.deviceMedia->SetProperty("nightvision.mode", &mode);

    } else if (0 == lt_strcmp(argv[1], "night")) {
        u32 mode = 1;
        S.deviceMedia->SetProperty("nightvision.mode", &mode);

    } else if (0 == lt_strcmp(argv[1], "autodusk")) {
        u32 condition = 0;
        S.deviceMedia->SetProperty("nightvision.conditions", &condition);
        u32 mode = 2;
        S.deviceMedia->SetProperty("nightvision.mode", &mode);

    } else if (0 == lt_strcmp(argv[1], "autodark")) {
        u32 condition = 1;
        S.deviceMedia->SetProperty("nightvision.conditions", &condition);
        u32 mode = 2;
        S.deviceMedia->SetProperty("nightvision.mode", &mode);

    } else if (0 == lt_strcmp(argv[1], "enable")) {
        bool enable = true;
        S.deviceMedia->SetProperty("nightvision.enable", &enable);

    } else if (0 == lt_strcmp(argv[1], "disable")) {
        bool enable = false;
        S.deviceMedia->SetProperty("nightvision.enable", &enable);

    } else {
        S.iShell->Print(hShell, "ERROR: unknown argument: %s\n", argv[1]);
        return -1;
    }

    return 0;
}

/* Camera sensor parameters command handler.
 */
static int LTShellVideo_Sensor(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argv);

    if (argc > 1) {
        S.iShell->Print(hShell, "ERROR: this command doesn't take arguments\n");
        return -1;
    }

    LTMediaCameraSensor params;
    if (S.deviceMedia->GetProperty("camera.sensor", &params)) {
        static const char modeStrings[2][8] = {"  [DAY]", "[NIGHT]"};
        u8 modeIndex = (params.flags & LTMediaCameraFlags_NightVision) ? 1 : 0;

        LTLOG("sensor.ev.attr", "%s u32ExposureTime: %lu, u32AnalogGain: %lu, u32DGain: %lu, u32Iso: %lu",
            modeStrings[modeIndex], LT_Pu32(params.exposureValue), LT_Pu32(params.analogGain), LT_Pu32(params.digitalGain), LT_Pu32(params.totalIso));

        LTLOG("sensor.wb.stat", "%s gb_gain: %d, gr_gain: %d -- gb_gain_gbl: %d, gr_gain_gbl: %d -- gb_gain_buf: %d, gr_gain_buf: %d",
            modeStrings[modeIndex], params.blueGain, params.redGain, params.blueGainRaw, params.redGainRaw, params.blueGainBuf, params.redGainBuf);
    } else {
        S.iShell->Print(hShell, "ERROR: failed to read camera sensor parameters\n");
        return -1;
    }

    return 0;
}

/* Video metadata flags command handler.
 */
static int LTShellVideo_Metadata(LTShell hShell, int argc, const char *argv[]) {
    if (argc > 2) {
        S.iShell->Print(hShell, "ERROR: incorrect arguments\n");
        return -1;
    }

    if (argc == 1) {
        u32 flags;
        if (S.deviceMedia->GetProperty("metadata.flags", &flags)) {
            S.iShell->Print(hShell, "%lu (%s/%s/%s/%s)\n", LT_Pu32(flags),
                flags&1 ? "Sensor":"--", flags&2 ? "Motion ROI":"--", flags&4 ? "Motion Frame":"--", flags&8 ? "Dropped":"--");
        } else {
            S.iShell->Print(hShell, "ERROR: failed to get metadata flags.\n");
            return -1;
        }

    } else {
        u32 flags = lt_strtou32(argv[1], NULL, 0);
        if (S.deviceMedia->SetProperty("metadata.flags", &flags)) {
            S.iShell->Print(hShell, "Metadata flags updated.\n");
        } else {
            S.iShell->Print(hShell, "ERROR: failed to set metadata flags.\n");
            return -1;
        }
    }

    return 0;
}

/* Audio streaming command handler with format selection.
 *
 * Start or stop audio streaming to a local server with format selection.
 * Supports MediaSourceManager's multi-format architecture for audio analysis.
 */
static int LTShellAudio_Stream(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2 || argc > 4) {  // Allow up to 4 args: cmd, ip, port, format
        S.iShell->Print(hShell, "Usage: audio stream <ip> [port] [format]\n");
        S.iShell->Print(hShell, "       audio stream stop\n");
        S.iShell->Print(hShell, "Formats: 16khz-pcm (default), 8khz-pcm, 8khz-g711, 16khz-g711\n");
        S.iShell->Print(hShell, "  16khz-pcm  - Raw 16kHz PCM (full bandwidth hardware audio)\n");
        S.iShell->Print(hShell, "  8khz-pcm   - Processed 8kHz PCM (downsampled + AEC + AGC + HPF)\n");
        // S.iShell->Print(hShell, "  8khz-raw   - Raw 8kHz PCM (downsampled only, no preprocessing) [DISABLED]\n");
        S.iShell->Print(hShell, "  8khz-g711  - Processed 8kHz G.711 \u03bc-law (same as livestream)\n");
        S.iShell->Print(hShell, "  16khz-g711 - Raw 16kHz G.711 \u03bc-law (full bandwidth encoded)\n");
        return -1;
    }

    StreamingContext *ctx = &s_AudioStreamContext;

    if (0 == lt_strcasecmp(argv[1], "stop")) {
        /* Stop existing stream if running */
        HandleShellStreamStop(hShell, ctx);
    } else {
        const char *pHost = argv[1];
        const char *pPort = (argc > 2) ? argv[2] : "8021";
        const char *pFormat = (argc > 3) ? argv[3] : "16khz-pcm";

        // Validate and configure format
        if (!ConfigureAudioStreamFormat(pFormat)) {
            S.iShell->Print(hShell, "ERROR: Invalid format '%s'\n", pFormat);
            S.iShell->Print(hShell, "Supported formats: 16khz-pcm, 8khz-pcm, 8khz-g711, 16khz-g711\n");
            return -1;
        }

        const char *endpointType = IsIPAddress(pHost) ? "ip" : "host";
        ctx->tempSockSpec = lt_malloc(MAX_SOCKSPEC_STRING_SIZE);
        lt_snprintf(ctx->tempSockSpec, MAX_SOCKSPEC_STRING_SIZE, "tcp %s: %s port: %s", endpointType, pHost, pPort);

        // Generate descriptive status message based on format
        const char *encodingStr = "Unknown";
        u32 sampleRateKHz = 0;
        const char *processingStr = "unknown processing";

        if (AudioStreamFormat.nEncoding == kLTMediaEncoding_PCM) {
            encodingStr = "PCM";
            sampleRateKHz = AudioStreamFormat.params.pcm.nSampleRate / 1000;
            processingStr = (sampleRateKHz == 16) ? "raw hardware audio" : "processed (AEC+AGC+HPF)";
        } else if (AudioStreamFormat.nEncoding == kLTMediaEncoding_PCMU) {
            encodingStr = "G.711";
            sampleRateKHz = AudioStreamFormat.params.g711.nSampleRate / 1000;
            processingStr = (sampleRateKHz == 16) ? "raw hardware audio" : "processed (AEC+AGC+HPF)";
        }

        S.iShell->Print(hShell, "Local %s %lu kHz audio streaming requested: tcp ip: %s port: %s\n",
                       encodingStr, LT_Pu32(sampleRateKHz), pHost, pPort);
        S.iShell->Print(hShell, "Format: %s (%s)\n", pFormat, processingStr);

        /* Start streaming from separate thread */
        ctx->streamThread->API->QueueTaskProcIfRequired(ctx->streamThread, StreamingStart, StreamingStarted, ctx);
    }

    return 0;
}

/* Forward-decl for command arrays, due to circular references */
static int LTShellVideoCmds_Help(LTShell hShell, int argc, const char *argv[]);
static int LTShellAudioCmds_Help(LTShell hShell, int argc, const char *argv[]);

static const LTSystemShell_CommandDesc VideoCommands[] = {
    /* Help must be first in this list */
    { "help",       LTShellVideoCmds_Help,  "                                           - list supported commands", NULL },
    { "snap",       LTShellVideo_Snap,      "[jpg|raw] [sd|hd] [local_upload_URL]       - send snapshot to local server", NULL },
    { "stream",     LTShellVideo_Stream,    "[sd|hd] [stop|local_upload_server] [port]  - start or stop video streaming to a local server", NULL },
    { "rate",       LTShellVideo_Rate,      "bps                                        - when streaming, set requested bitrate to given bits per second", NULL },
    { "stats",      LTShellVideo_Stats,     "                                           - print video pipeline statistics", NULL },
    { "ircut",      LTShellVideo_IrCut,     "[on|off]                                   - enable or disable IR Cut filter", NULL },
    { "irled",      LTShellVideo_IrLed,     "[near|far|off|on]                          - set IR LEDs to given mode", NULL },
    { "nvision",    LTShellVideo_NVision,   "[day|night|autodusk|autodark|enable|disable] - enable/disable night vision, or control night vision mode", NULL },
    { "sensor",     LTShellVideo_Sensor,    "                                           - read current camera sensor parameters", NULL },
    { "meta",       LTShellVideo_Metadata,  "[value]                                    - set or get video metadata flags", NULL },
    { }
};

static const LTSystemShell_CommandDesc AudioCommands[] = {
    /* Help must be first in this list */
    { "help",       LTShellAudioCmds_Help,  "                                   - list supported commands", NULL },
    { "stream",     LTShellAudio_Stream,    "[stop|<ip>] [port] [format]         - stream audio with format selection (16khz-pcm, 8khz-pcm, 8khz-g711, 16khz-g711)", NULL },
    { }
};

static int LTShellVideoCmds_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    for (int n = 0; VideoCommands[n].pCommand; n++) {
        S.iShell->Print(hShell, "  %8s %s\n", VideoCommands[n].pCommand, VideoCommands[n].pDescription);
    }
    return 0;
}

static int LTShellAudioCmds_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    for (int n = 0; AudioCommands[n].pCommand; n++) {
        S.iShell->Print(hShell, "  %8s %s\n", AudioCommands[n].pCommand, AudioCommands[n].pDescription);
    }
    return 0;
}

static int LTShellVideo_Handler(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;
    if (argc > 1) {
        for (int n = 0; VideoCommands[n].pCommand; n++) {
            if (0 == lt_strcmp(VideoCommands[n].pCommand, argv[1])) {
                cmd = n;
                break;
            }
        }
    }
    return VideoCommands[cmd].pCommandProc(hShell, argc-1, argv+1);
}

static int LTShellAudio_Handler(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;
    if (argc > 1) {
        for (int n = 0; AudioCommands[n].pCommand; n++) {
            if (0 == lt_strcmp(AudioCommands[n].pCommand, argv[1])) {
                cmd = n;
                break;
            }
        }
    }
    return AudioCommands[cmd].pCommandProc(hShell, argc-1, argv+1);
}

static void LTShell_VideoHelp(LTShell hShell, int argc, const char ** argv) {
    /* This LTShell Help proc is so "help video" works in addition to "video help".
    * It prints usage and calls the regular LTShellVideo_Help
    */
    S.iShell->Print(hShell, "usage: video <command> [args]\nCommands:\n");
    (void)LTShellVideoCmds_Help(hShell, argc, argv);
}

static void LTShell_AudioHelp(LTShell hShell, int argc, const char ** argv) {
    S.iShell->Print(hShell, "usage: audio <command> [args]\nCommands:\n");
    (void)LTShellAudioCmds_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc LTShellVideo_Commands[] = {
    { "video", LTShellVideo_Handler, "video driver commands", LTShell_VideoHelp },
};

static const LTSystemShell_CommandDesc LTShellAudio_Commands[] = {
    { "audio", LTShellAudio_Handler, "audio driver commands", LTShell_AudioHelp },
};


/*******************************************************************************
 * Module Initialisation
 *
 * This is not currently a standalone library - it's part of LTShellMedia because
 * that is the top-level media library used to indirectly access the underlying
 * drivers for video etc.
 *
 * LibInit and LibFini functions are provided to be called when the main shell
 * library is opened or closed.
 ******************************************************************************/
void CleanupStreamingContext(StreamingContext *ctx) {
    DestroyMediaSource(ctx);

    if (ctx->hStreamSocket) {
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, ctx->hStreamSocket);
        pSocket->DisconnectSocket(ctx->hStreamSocket);
        S.core->DestroyHandle(ctx->hStreamSocket);
        ctx->socketReady = false;
        ctx->hStreamSocket = 0;
    }
    lt_destroyobject(ctx->streamThread);
    ctx->streamThread = 0;
}

void LTShellVideoDriverImpl_LibFini(void) {
    CleanupStreamingContext(&s_VideoStreamContext);
    CleanupStreamingContext(&s_AudioStreamContext);
    CleanupSnapshot();

    if (MediaSourceSnapshot) {
        S.core->DestroyHandle(MediaSourceSnapshot);
        MediaSourceSnapshot = 0;
        S.iMediaSourceSnapshot = NULL;
    }

    if (s_SnapshotUploadUrl) {
        ltstring_destroy(s_SnapshotUploadUrl);
        s_SnapshotUploadUrl = NULL;
    }

    lt_closelibrary(S.motionDetection);
    if (S.netTransport) lt_destroyhandle(S.netTransport);
    lt_closelibrary(S.netHttpClient);
    lt_closelibrary(S.netCore);
    lt_closelibrary(S.deviceMedia);
    lt_closelibrary(S.byteOps);
    lt_destroyobject(S.bufManager);

    if (S.shell) {
        S.shell->UnregisterCommands(LTShellVideo_Commands);
        S.shell->UnregisterCommands(LTShellAudio_Commands);
        lt_closelibrary(S.shell);
    }
    S = (struct Statics) {};
}

bool LTShellVideoDriverImpl_LibInit(void) {
    S = (struct Statics) {
        .core = LT_GetCore()
    };
    const char *reason;
    LTProductConfig *config = lt_openlibrary(LTProductConfig);
    do {
        reason = "no shell";
        if (!(S.shell = lt_openlibrary(LTSystemShell))) break;
        S.shell->RegisterCommands(LTShellVideo_Commands, sizeof(LTShellVideo_Commands) / sizeof(LTShellVideo_Commands[0]));
        S.shell->RegisterCommands(LTShellAudio_Commands, sizeof(LTShellAudio_Commands) / sizeof(LTShellAudio_Commands[0]));
        S.iShell = lt_getlibraryinterface(ILTShell, S.shell);

        S.bufManager = lt_createobject(LTIPCBufferManager); // allowed to fail, might not exist on the current platform

        reason = "no UtilityByteOps";
        if (!(S.byteOps = lt_openlibrary(LTUtilityByteOps))) break;
        reason = "no LTDeviceMedia";
        if (!(S.deviceMedia = lt_openlibrary(LTDeviceMedia))) break;
        reason = "no LTNetCore";
        if (!(S.netCore = lt_openlibrary(LTNetCore))) break;
        reason = "no LTNetHttpClient";
        if (!(S.netHttpClient = lt_openlibrary(LTNetHttpClient))) break;
        reason = "no Net Transport";
        if (!(S.netTransport = S.netCore->OpenTransport(NULL, NULL, NULL))) break;

        reason = "video thread failed";
        if (!(s_VideoStreamContext.streamThread = lt_createobject(LTOThread))) break;
        s_VideoStreamContext.streamThread->API->SetStackSize(s_VideoStreamContext.streamThread, 2048);
        s_VideoStreamContext.streamThread->API->Start(s_VideoStreamContext.streamThread, "ShellVideo", LTShellVideo_ThreadInit, LTShellVideo_ThreadExit);

        reason = "audio thread failed";
        if (!(s_AudioStreamContext.streamThread = lt_createobject(LTOThread))) break;
        s_AudioStreamContext.streamThread->API->SetStackSize(s_AudioStreamContext.streamThread, 2048);
        s_AudioStreamContext.streamThread->API->Start(s_AudioStreamContext.streamThread, "ShellAudio", LTShellAudio_ThreadInit, LTShellAudio_ThreadExit);

        LTList_Init(&s_VideoStreamContext.pendingFrames);
        LTList_Init(&s_AudioStreamContext.pendingFrames);
        //This hack is so that to determine the IR LED has near/far modes or single mode
        s_IsPanCamera = config->GetLibraryConfigSection("LTCameraPosition");
        return true;
    } while (false);

    LTLOG_YELLOWALERT("init.fail", "init failed: %s", reason);
    LTShellVideoDriverImpl_LibFini();
    return false;
}
