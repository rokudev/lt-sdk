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

#include <lt/device/imagesensor/LTDeviceImageSensor.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/monitor/LTNetMonitor.h>

#include "EyeClops.h"
#include "EyeClopsConnection.h"

/*___________
  #defines */
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_96x96
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_160x120    /* QQVGA */
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_176x144    /* QCIF  */
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_240x176    /* HQVGA */
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_240x240
#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_320x240    /* QVGA  */
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_400x296    /* CIF   */
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_480x320    /* HVGA  */
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_640x480    /* VGA   */
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_800x600    /* SVGA  */
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_1024x768   /* XGA   */
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_1280x720   /* HD    */
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_1280x1024  /* SXGA  */
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_1600x1200  /* UXGA  */
//#define IMAGE_SENSOR_FRAME_SIZE     kLTImageSensorFrameSize_2048x1536  /* QXGA  — 3MP sensors (OV3660, OV5640) */

#define IMAGE_SENSOR_PIXEL_FORMAT   kLTImageSensorPixelFormat_JPEG
#define VIDEO_CHANNEL               kLTDeviceVideo_Channel_ImageHD
#define EYECLOPS_THREAD_KEY         "eyeclop"

DEFINE_LTLOG_SECTION("eyeclops");

#define USE_DLOG    0
#if USE_DLOG
    #define DLOG    LTLOG
#else
    #define DLOG    LTLOG_LOGNULL
#endif

/*_____________________________________
  EyeClopsImpl private instance data */
typedef_LTObjectImpl(EyeClops, EyeClopsImpl) {
    LTOThread               *thread;
    LTDeviceVideo           *pDeviceVideo;
    LTDeviceImageSensor     *pDeviceImageSensor;
    LTNetCore               *pNetCore;
    LTNetMonitor            *pNetMonitor;
    LTDeviceVideo_VideoData *videoData;
    EyeClopsConnection      *connections;
    LTDeviceUnit             hUnitImageSensor;
    LTSocket                 hServerSocket;
} LTOBJECT_API;

/*_______________________
  forward declarations */
static void MyImageSensorPowerOnEventProc(LTImageSensorEvent event, void *pClientData);

/*___________________
  helper functions */
static EyeClopsImpl * GetEyeClopsFromCurrentThread(void) {
    LTOThread * thread = LT_GetCore()->GetCurrentThreadObject();
    return (EyeClopsImpl *)thread->API->GetThreadSpecificClientData(thread, EYECLOPS_THREAD_KEY);
}

static void AddConnection(EyeClopsImpl *eyeclops, LTSocket hSocket) {
    EyeClopsConnection *connection = lt_createobject(EyeClopsConnection);
    if (connection) {
        connection->API->Initialize(connection, (EyeClops *)eyeclops, hSocket);
        connection->API->SetNext(connection, eyeclops->connections);
        eyeclops->connections = connection;
    }
    else {
        LTLOG("addconnection", "out of memory");
    }
}

static EyeClopsConnection * FindConnection(EyeClopsImpl *eyeclops, LTSocket hSocket) {
    EyeClopsConnection *curr = eyeclops->connections;
    while (curr) {
        if (curr->API->GetSocket(curr) == hSocket) return curr;
        curr = curr->API->GetNext(curr);
    }
    return NULL;
}

static void KickConnections(EyeClopsImpl * eyeclops) {
    EyeClopsConnection *curr = eyeclops->connections, *next = NULL;
    while (curr) {
        next = curr->API->GetNext(curr); /* get next first, since FirstFrameAvailable might get rid of curr if all frame data sent */
        curr->API->FirstFrameAvailable(curr);
        curr = next;
    }
}

static bool AnyConnectionIsUsingVideoData(EyeClopsImpl * eyeclops, LTDeviceVideo_VideoData *videoData) {
    DLOG("any.connection", NULL);
    if (videoData) {
        EyeClopsConnection *curr = eyeclops->connections;
        while (curr) {
            if (curr->API->GetFrameInUse(curr) == videoData) {
                return true;
            }
            curr = curr->API->GetNext(curr);
        }
    }
    return false;
}

/*_________________________
  eyeclops API functions */
static LTDeviceVideo_VideoData * EyeClopsImpl_GetLatestFrame(EyeClopsImpl *eyeclops) {
    DLOG("getlatestframe", NULL);
    return eyeclops->videoData;
}

static void EyeClopsImpl_ReclaimVideoData(EyeClopsImpl *eyeclops, LTDeviceVideo_VideoData *videoData) {
    if (! AnyConnectionIsUsingVideoData(eyeclops, videoData)) {
        if (eyeclops->videoData == videoData) {
            DLOG("reclaim", "Reclaiming eyeclops->videoData %p", videoData);
            eyeclops->videoData = NULL;
        }
        else {
            DLOG("reclaim", "Reclaiming **NON** eyeclops->videoData %p", videoData);
        }
        eyeclops->pDeviceVideo->ReleaseVideoData(VIDEO_CHANNEL, videoData);
    }
}

static void EyeClopsImpl_TerminateConnection(EyeClopsImpl *eyeclops, EyeClopsConnection *connection) {
    DLOG("terminate.connection", NULL);
    EyeClopsConnection *prev = NULL, *next = NULL, *curr = eyeclops->connections;
    while (curr) {
        next = curr->API->GetNext(curr);
        if (curr == connection) {
            if (prev == NULL) eyeclops->connections = next;
            else prev->API->SetNext(prev, next);
            LTDeviceVideo_VideoData *videoData = curr->API->GetFrameInUse(curr);
            if (videoData) EyeClopsImpl_ReclaimVideoData(eyeclops, videoData);
            lt_destroyobject(curr);
            break;
        }
        prev = curr; curr = next;
    }
}

/*____________________
  socket event proc */
static void MySocketEventProc(LTSocket hSocket, LTSocket_Event event, void *pClientData) {
    EyeClopsImpl *eyeclops = (EyeClopsImpl *)pClientData;

    // socket ready means the server socket is ready; connected gives a new hSocket for the incoming connection
    switch (event) {
        case kLTSocket_Event_SocketReady:   lt_consoleprint("kLTSocket_Event_SocketReady\n");   return;
        case kLTSocket_Event_Connected:     AddConnection(eyeclops, hSocket);                   return;
        default: break;
    }

    /* something to do, find our connection and dispatch */
    EyeClopsConnection *connection = FindConnection(eyeclops, hSocket);
    if (connection == NULL) { lt_destroyhandle(hSocket); return; }

    switch (event) {
        case kLTSocket_Event_ReadReady:     connection->API->ReadReady(connection);                 break;
        case kLTSocket_Event_WriteReady:    connection->API->WriteReady(connection);                break;
        case kLTSocket_Event_Disconnected:  EyeClopsImpl_TerminateConnection(eyeclops, connection); break;
        default: break;
    }
}

/*_______________________________
  net status change event proc */
static void OnNetStatusChange(LTNetMonitor_Status status, void *clientData) {
    EyeClopsImpl *eyeclops = (EyeClopsImpl *)clientData;

    if (eyeclops->hServerSocket) {
        lt_destroyhandle(eyeclops->hServerSocket);
        eyeclops->hServerSocket = 0;
    }

    if (status == kLTNetMonitor_Status_NetworkUp) {
        eyeclops->hServerSocket = eyeclops->pNetCore->OpenSocket(0, "tcp listen port: 80", MySocketEventProc, eyeclops);
        if (0 == eyeclops->hServerSocket) LTLOG("sensor.netstatuschange.opensocket", "failed to open server socket on port 80");
    }
}

/*_________________________
  video frame event proc */
static void MyVideoEventProc(LTDeviceVideo_Channel channel, LTDeviceVideo_Event event, LTDeviceVideo_VideoData *videoData, void *clientData) {
    EyeClopsImpl * eyeclops = (EyeClopsImpl *)clientData;

    if (event != kLTDeviceVideo_Event_FrameReady) return;

    if (eyeclops->videoData == NULL) {
        /* this is the first frame in, set it and kick any connections waiting for it */
        eyeclops->videoData = videoData;
        KickConnections(eyeclops);
        return;
    }

    if (AnyConnectionIsUsingVideoData(eyeclops, eyeclops->videoData)) {
        /* a current connection is using the current frame, drop the incoming frame */
        eyeclops->pDeviceVideo->ReleaseVideoData(channel, videoData);
    }
    else {
        /* Release the previous frame and take the new one */
        LTDeviceVideo_VideoData *lastVideoData = eyeclops->videoData;
        eyeclops->videoData = videoData;
        eyeclops->pDeviceVideo->ReleaseVideoData(channel, lastVideoData);
    }
}

/*_____________________________
  sensor power on event proc */
static void MyImageSensorPowerOnEventProc(LTImageSensorEvent event, void *pClientData) {
    EyeClopsImpl *eyeclops = (EyeClopsImpl *)pClientData;

    // unregister this event proc
    eyeclops->pDeviceImageSensor->NoImageSensorEvent(eyeclops->hUnitImageSensor, MyImageSensorPowerOnEventProc);

    if (event == kLTImageSensorEvent_PowerOn) {
        // image sensor is powered on, set its attributes and start video dma
        LTImageSensorFrameSize frameSize = IMAGE_SENSOR_FRAME_SIZE;
        LTImageSensorPixelFormat pixelFormat = IMAGE_SENSOR_PIXEL_FORMAT;
        eyeclops->pDeviceImageSensor->SetAttribute(eyeclops->hUnitImageSensor, kLTImageSensorAttribute_FrameSize, &frameSize);
        eyeclops->pDeviceImageSensor->SetAttribute(eyeclops->hUnitImageSensor, kLTImageSensorAttribute_PixelFormat, &pixelFormat);
        eyeclops->pDeviceVideo->Start(VIDEO_CHANNEL);
    }
}

/*_______________
 / thread init */
static bool EyeClops_ThreadInit(void) {
    EyeClopsImpl * eyeclops = GetEyeClopsFromCurrentThread();

    eyeclops->pDeviceImageSensor = lt_openlibrary(LTDeviceImageSensor);
    eyeclops->hUnitImageSensor = eyeclops->pDeviceImageSensor->CreateDeviceUnitHandle(0);
    eyeclops->pDeviceImageSensor->OnImageSensorEvent(eyeclops->hUnitImageSensor, MyImageSensorPowerOnEventProc, eyeclops);

    eyeclops->pDeviceVideo = lt_openlibrary(LTDeviceVideo);
    eyeclops->pDeviceVideo->OnVideoEvent(VIDEO_CHANNEL, MyVideoEventProc, eyeclops);
    eyeclops->pDeviceVideo->Enable(kLTDeviceVideo_Source_0);

    eyeclops->pNetCore = lt_openlibrary(LTNetCore);
    eyeclops->pNetMonitor = lt_openlibrary(LTNetMonitor);
    eyeclops->pNetMonitor->OnStatusChange(OnNetStatusChange, NULL, eyeclops);

    return true;
}

static void EyeClops_ThreadExit(void) {
    // cleanup what we initialized
    EyeClopsImpl * eyeclops = GetEyeClopsFromCurrentThread();

    while (eyeclops->connections) EyeClopsImpl_TerminateConnection(eyeclops, eyeclops->connections);
    if (eyeclops->hServerSocket) { lt_destroyhandle(eyeclops->hServerSocket); eyeclops->hServerSocket = 0; }
    if (eyeclops->pNetMonitor) {
        eyeclops->pNetMonitor->NoStatusChange(OnNetStatusChange);
        lt_closelibrary(eyeclops->pNetMonitor);
    }
    lt_closelibrary(eyeclops->pNetCore);

    if (eyeclops->pDeviceVideo) {
        eyeclops->pDeviceVideo->NoVideoEvent(VIDEO_CHANNEL, MyVideoEventProc);
        if (eyeclops->videoData) eyeclops->pDeviceVideo->ReleaseVideoData(VIDEO_CHANNEL, eyeclops->videoData);
        eyeclops->pDeviceVideo->Stop(VIDEO_CHANNEL);
        eyeclops->pDeviceVideo->Disable(kLTDeviceVideo_Source_0);
        lt_closelibrary(eyeclops->pDeviceVideo);
    }

    if (eyeclops->hUnitImageSensor) {
        eyeclops->pDeviceImageSensor->NoImageSensorEvent(eyeclops->hUnitImageSensor, MyImageSensorPowerOnEventProc);
        lt_destroyhandle(eyeclops->hUnitImageSensor);
    }
    lt_closelibrary(eyeclops->pDeviceImageSensor);
}

/*____________________
_/ eyeclops object  */
static bool EyeClopsImpl_ConstructObject(EyeClopsImpl * eyeclops) {
    eyeclops->thread = lt_createobject(LTOThread);
    eyeclops->thread->API->SetThreadSpecificClientData(eyeclops->thread, EYECLOPS_THREAD_KEY, NULL, eyeclops);
    eyeclops->thread->API->SetStackSize(eyeclops->thread, 2048);
    return eyeclops->thread->API->StartSynchronous(eyeclops->thread, "EyeClops", EyeClops_ThreadInit, EyeClops_ThreadExit);
}

static void EyeClopsImpl_DestructObject(EyeClopsImpl * eyeclops) {
    LTLOG("destruct", NULL);
    lt_destroyobject(eyeclops->thread);
}

/*_____________________________________________________________________________
  make EyeClops private - it can only be lt_createobject'd from this library */
define_LTObjectImplPrivate(EyeClops,  EyeClopsImpl,
    GetLatestFrame,
    ReclaimVideoData,
    TerminateConnection
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-May-26   augustus    created
 */
