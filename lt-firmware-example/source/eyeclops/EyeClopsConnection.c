/*******************************************************************************
 * lt-firmware-example/source/eyeclops/EyeClops.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 *******************************************************************************
 * LT Library that creates an image frame server on port 80
 * using LTDeviceImageSensor, LTDeviceVideo and LTNetCore
 *******************************************************************************/

#include "EyeClopsConnection.h"

/*___________
  #defines */
#define FAV_ICO_GET                         "GET /favicon.ico"
#define FAV_ICO_GET_LEN                     (sizeof(FAV_ICO_GET)-1)
#define IMAGE_GET                           "GET /image"
#define IMAGE_GET_LEN                       (sizeof(IMAGE_GET)-1)

#define HTTP_NO_RESPONSE_HEADER             "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n"
#define HTTP_NO_RESPONSE_HEADER_LEN         (sizeof(HTTP_NO_RESPONSE_HEADER)-1)
#define HTTP_IMAGE_HEADER                   "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n\r\n"
#define HTTP_IMAGE_HEADER_LEN               (sizeof(HTTP_IMAGE_HEADER)-1)

#define HTTP_VIDEO_BOUNDARY                 "YouHaveNoChanceToSurviveMakeYourTime"
#define HTTP_VIDEO_HEADER                   "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace;boundary=" HTTP_VIDEO_BOUNDARY "\r\nAccess-Control-Allow-Origin: *\r\n\r\n"
#define HTTP_VIDEO_HEADER_LEN               (sizeof(HTTP_VIDEO_HEADER)-1)
#define HTTP_VIDEO_CONTINUATION_HEADER      "\r\n--" HTTP_VIDEO_BOUNDARY "\r\nContent-Type: image/jpeg\r\n\r\n"
#define HTTP_VIDEO_CONTINUATION_HEADER_LEN  (sizeof(HTTP_VIDEO_CONTINUATION_HEADER)-1)

DEFINE_LTLOG_SECTION("eyeclopsconnection");

#define USE_DLOG    0
#if USE_DLOG
    #define DLOG    LTLOG
#else
    #define DLOG    LTLOG_LOGNULL
#endif

#define LOG_FPS     0

/*_____________________________________________
  EyeClopsConnectionImpl private state enums */
typedef enum ConnectionState {
    kReadHttpHeaders,
    kWriteHttpHeaders,
    kWriteData,
    kDataSendComplete,
    kWriteContinuationHeader,
} ConnectionState;

typedef enum GetRequest {
    kGetFavIco,
    kGetImage,
    kGetVideo
} GetRequest;

typedef enum SendResult {
    kSendError,
    kSendAgain,
    kSendComplete
} SendResult;

/*___________________________________________
  EyeClopsConnection private instance data */
typedef_LTObjectImpl(EyeClopsConnection, EyeClopsConnectionImpl) {
    EyeClops                *eyeclops;
    LTDeviceVideo_VideoData *videoData;
    LTSocket                hSocket;
    const u8 *              pBytesToSend;
    u32                     nSendBytesRemaining;
    ConnectionState         state;
    GetRequest              request;
    EyeClopsConnectionImpl  *next;
} LTOBJECT_API;

/*_______________________
  forward declarations */
static void EyeClopsConnectionImpl_WriteReady(EyeClopsConnectionImpl *connection);

/*___________________
  helper functions */
#if LOG_FPS
static void LogFps(u32 frameCount, u32 numFrames) {
    static LTTime thisTime;
    static LTTime lastTime;
    static bool   firstTime = true;

    if (firstTime) {
        lastTime = LT_GetCore()->GetKernelTime();
        firstTime = false;
        return;
    }
    thisTime = LT_GetCore()->GetKernelTime();
    u32 milliseconds = LTTime_GetMilliseconds(LTTime_Subtract(thisTime, lastTime));
    lastTime = thisTime;
    u32 fps = 1000000 * numFrames / milliseconds;
    u32 seconds = fps / 1000;
    u32 fractional = fps % 1000;
    LTLOG("frameCount", "frame %lu-%lu fps = %lu.%03lu", LT_Pu32(frameCount-numFrames), LT_Pu32(frameCount), LT_Pu32(seconds), LT_Pu32(fractional));
}
#endif

static SendResult SendBytes(EyeClopsConnectionImpl *connection) {
    //DLOG("send.bytes", NULL);
    ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, connection->hSocket);
    while (connection->nSendBytesRemaining) {
        u32 bytesToSend = connection->nSendBytesRemaining;
        s32 bytesSent = iSocket->WriteSocket(connection->hSocket, connection->pBytesToSend, bytesToSend); //connection->nSendBytesRemaining);
        if (bytesSent == 0) { DLOG("send.bytes", "socket write buffer full"); return kSendAgain; }
        if (bytesSent  < 0) { LTLOG("send.bytes", "socket write error"); return kSendError; }
        connection->nSendBytesRemaining -= (u32)bytesSent;
        connection->pBytesToSend += bytesSent;
    }
    return kSendComplete;  /* fully sent */
}


/*________________
  api functions */
static void EyeClopsConnectionImpl_Initialize(EyeClopsConnectionImpl *connection, EyeClops *eyeclops, LTSocket hSocket) {
   connection->eyeclops = eyeclops;
   connection->hSocket = hSocket;
}

static void EyeClopsConnectionImpl_FirstFrameAvailable(EyeClopsConnectionImpl *connection) {
    if (connection->videoData == NULL && connection->state == kWriteData) EyeClopsConnectionImpl_WriteReady(connection);
}

static void EyeClopsConnectionImpl_ReadReady(EyeClopsConnectionImpl *connection) {
    if (connection->state != kReadHttpHeaders) return;
    enum { kReadBuffSize = 1024 };
    char *buff = (char *)lt_malloc(kReadBuffSize);
    if (buff) {
        ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, connection->hSocket);
        s32 sizeRead = iSocket->ReadSocket(connection->hSocket, buff, kReadBuffSize - 1);
        if (sizeRead > 0) {
            buff[sizeRead] = 0;
            DLOG("socket.read", "\n%s\n", buff);
            if (0 == lt_strncasecmp(buff, FAV_ICO_GET, FAV_ICO_GET_LEN)) connection->request = kGetFavIco;
            else if (0 == lt_strncasecmp(buff, IMAGE_GET, IMAGE_GET_LEN)) connection->request = kGetImage;
            else connection->request = kGetVideo;
            connection->state = kWriteHttpHeaders;
            lt_free(buff);
            EyeClopsConnectionImpl_WriteReady(connection); /* kick a write ready, it will work or we'll pend it */
            return;
        }
        else {
            lt_free(buff);
        }
    }

    /* the read didn't work, shutdown the connection */
    connection->eyeclops->API->TerminateConnection(connection->eyeclops, (EyeClopsConnection *)connection);
}

static void EyeClopsConnectionImpl_WriteReady(EyeClopsConnectionImpl *connection) {
    if (connection->state == kReadHttpHeaders) return; /* might get a write ready before a read ready */
    if (connection->state == kDataSendComplete) {
        connection->eyeclops->API->TerminateConnection(connection->eyeclops, (EyeClopsConnection *)connection);
        return;
    }

    SendResult result;
    /* write the headers */
    if (connection->state == kWriteHttpHeaders) {
        if (connection->nSendBytesRemaining == 0) {
            switch (connection->request) {
                case kGetFavIco: connection->pBytesToSend = (const u8 *)HTTP_NO_RESPONSE_HEADER; connection->nSendBytesRemaining = HTTP_NO_RESPONSE_HEADER_LEN; break;
                case kGetImage:  connection->pBytesToSend = (const u8 *)HTTP_IMAGE_HEADER;       connection->nSendBytesRemaining = HTTP_IMAGE_HEADER_LEN;       break;
                case kGetVideo:  connection->pBytesToSend = (const u8 *)HTTP_VIDEO_HEADER;       connection->nSendBytesRemaining = HTTP_VIDEO_HEADER_LEN;       break;
                default: break;
            }
        }
        result = SendBytes(connection);
        if (result == kSendAgain) return;
        if (result == kSendError || connection->request == kGetFavIco) {
            connection->eyeclops->API->TerminateConnection(connection->eyeclops, (EyeClopsConnection *)connection);
            return;
        }
        connection->pBytesToSend = NULL;
        connection->nSendBytesRemaining = 0;
        if (connection->request == kGetImage) connection->state = kWriteData;
        else connection->state = kWriteContinuationHeader;
    }

continuation:
    if (connection->state == kWriteContinuationHeader) {
        if (connection->nSendBytesRemaining == 0) {
            connection->pBytesToSend = (const u8*)HTTP_VIDEO_CONTINUATION_HEADER;
            connection->nSendBytesRemaining = HTTP_VIDEO_CONTINUATION_HEADER_LEN;
        }
        result = SendBytes(connection);
        if (result == kSendAgain) return;
        if (result == kSendError) { connection->eyeclops->API->TerminateConnection(connection->eyeclops, (EyeClopsConnection *)connection); return; }
        connection->pBytesToSend = NULL;
        connection->nSendBytesRemaining = 0;
        connection->state = kWriteData;
    }

    /* write the data */
    if (connection->videoData == NULL) {
        connection->videoData = connection->eyeclops->API->GetLatestFrame(connection->eyeclops);
        if (connection->videoData == NULL) return; /* first frame not ready yet, we'll get kicked */
        connection->pBytesToSend = connection->videoData->address;
        connection->nSendBytesRemaining = connection->videoData->length;
    }

    result = SendBytes(connection);
    switch (result) {
        case kSendAgain:
            break;
        case kSendComplete:
            {
                LTDeviceVideo_VideoData *videoData = connection->videoData;
                connection->videoData = NULL;
                // terminate the connection on the next write ready, to close the socket once the write buffers have flushed
                // but first mark that we're done with the videoData and instruct EyeClops to reclaim it
                connection->eyeclops->API->ReclaimVideoData(connection->eyeclops, videoData);
                if (connection->request == kGetImage) connection->state = kDataSendComplete;
                else {
                    connection->pBytesToSend = NULL;
                    connection->nSendBytesRemaining = 0;
                    connection->state = kWriteContinuationHeader;
                    #if LOG_FPS
                        static int frameCount = 0;
                        if ((frameCount % 100) == 0) LogFps(frameCount, 100);
                        frameCount++;
                    #endif
                    goto continuation;
                }
            }
            break;
        case kSendError:
            connection->eyeclops->API->TerminateConnection(connection->eyeclops, (EyeClopsConnection *)connection);
            break;
    }
}

static LTSocket EyeClopsConnectionImpl_GetSocket(EyeClopsConnectionImpl *connection) {
    return connection->hSocket;
}

static LTDeviceVideo_VideoData * EyeClopsConnectionImpl_GetFrameInUse(EyeClopsConnectionImpl *connection) {
    return connection->videoData;
}

static EyeClopsConnectionImpl * EyeClopsConnectionImpl_GetNext(EyeClopsConnectionImpl *connection) {
    return connection->next;
}

static void EyeClopsConnectionImpl_SetNext(EyeClopsConnectionImpl *connection, EyeClopsConnectionImpl *next) {
    connection->next = next;
}

/*______________________________
_/ EyeClopsConnection object  */
static bool EyeClopsConnectionImpl_ConstructObject(EyeClopsConnectionImpl * connection) {
    LT_UNUSED(connection);
    return true;
}

static void EyeClopsConnectionImpl_DestructObject(EyeClopsConnectionImpl * connection) {
    lt_destroyhandle(connection->hSocket);
}

/*_______________________________________________________________________________________
  make EyeClopsConnection private - it can only be lt_createobject'd from this library */
define_LTObjectImplPrivate(EyeClopsConnection,  EyeClopsConnectionImpl,
    Initialize,
    FirstFrameAvailable,
    ReadReady,
    WriteReady,
    GetSocket,
    GetFrameInUse,
    GetNext,
    SetNext
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-May-26   augustus    created
 */
