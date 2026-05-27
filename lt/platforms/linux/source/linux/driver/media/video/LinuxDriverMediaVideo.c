/*******************************************************************************
 * platforms/linux/source/linux/driver/media/video/LinuxDriverMediaVideo.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Linux LT Driver Library for videocapture functions
 *
 ******************************************************************************/
/** @file LinuxDriverMediaVideo.c Implementation of videocapture driver for Linux
 */

#include <string.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

#include <lt/LT.h>
#include <lt/device/media/LTDeviceMedia.h>


DEFINE_LTLOG_SECTION("video");

static const char *dev_name = "/dev/video0";

#define CLEAR(x) lt_memset(&(x), 0, sizeof(x))

typedef struct {
    int width;
    int height;
} Resolution;

typedef struct {
    u8 *start;
    u32 length;
} Buffer;

typedef struct {
    LTHandle           handle;
    LTMediaEncoding    mediaEncoding;
    LTEvent            event;
    int                fd;
    Buffer            *buffers;
    u32                numBuffers;
    bool               dataAllocated;
    struct v4l2_buffer buf;
    bool               running;
} LTMediaSourceContext;

static ILTMediaSource s_ILTMediaSource;

static ILTThread *s_pThread;
static ILTEvent  *s_pEvent;

static LTThread      s_CaptureThread;

static const LTArgsDescriptor s_MediaDataEventArgs =
    {1, {kLTArgType_pointer}};

static bool LTDriverMediaImpl_SupportsFormat(LTMediaFormat *pFormat);

/*******************************************************************************
 * ILTDriverVideoCapture implementation                                        */

static int
xioctl(int fh, unsigned long int request, void *arg) {
    int r;
    do {
        r = ioctl(fh, request, arg);
    } while ((r == -1) && (errno == EINTR));
    return r;
}

static bool
PollEncoderStream(LTMediaSourceContext *source) {
    while (source->running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(source->fd, &fds);
        struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        int r = select(source->fd + 1, &fds, NULL, NULL, &tv);
        if (-1 == r) {
            if (EINTR == errno) continue;
            LTLOG_REDALERT("poll.sel.err", "select error %d, %s", errno, strerror(errno));
        }
        if (0 == r) {
            LTLOG_REDALERT("poll.sel.timeout", "select timeout");
            return false;
        }

        CLEAR(source->buf);
        source->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        source->buf.memory = V4L2_MEMORY_MMAP;
        source->buf.flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
        if (-1 == xioctl(source->fd, VIDIOC_DQBUF, &source->buf)) {
            if (errno == EAGAIN) return false;
            LTLOG_REDALERT("poll.rd.err", "read error %d, %s", errno, strerror(errno));
        }
        LT_ASSERT(source->buf.index < source->numBuffers);

        source->dataAllocated = true;
        s_pEvent->NotifyEvent(source->event, source);

        return true;
    }

    return false;
}

static void
PollH264Stream(void *data) {
    LTMediaSourceContext *source = data;
    if (!PollEncoderStream(source) && source->running) {
        s_pThread->QueueTaskProcIfRequired(s_pThread->GetCurrentThread(), PollH264Stream, NULL, source);
    }
}

static void
DispatchH264DataEventCB(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTMediaSource_OnMediaEventProc *pCallback = (LTMediaSource_OnMediaEventProc *)pEventProc;
    LTMediaSourceContext *source  = LTArgs_pointerAt(0, pEventArgs);
    if (!source) return;
    Buffer *buffer = &source->buffers[source->buf.index];

    u8 *chunkStart = buffer->start;
    LT_ASSERT(chunkStart[0] == 0 && chunkStart[1] == 0 && chunkStart[2] == 0 && chunkStart[3] == 1);
    for (u32 i = 4; i < source->buf.bytesused; ++i) {
        bool bLastChunk = false;
        u8 *chunkEnd = NULL;
        if (i == (source->buf.bytesused - 1)) {
            bLastChunk = true;
            chunkEnd = &buffer->start[i];
        } else if ((i <= (source->buf.bytesused - 4)) &&
                   ((buffer->start[i+0] == 0) &&
                    (buffer->start[i+1] == 0) &&
                    (buffer->start[i+2] == 0) &&
                    (buffer->start[i+3] == 1))) {
            chunkEnd = &buffer->start[i - 1];
        }

        if (chunkEnd) {
            u64 timestamp = (u64)source->buf.timestamp.tv_sec * 1000000ULL + source->buf.timestamp.tv_usec;
            LTMediaData clientPacket = {
                .pData       = chunkStart,
                .nDataLen    = chunkEnd - chunkStart + 1,
                .nTimestamp  = timestamp,
                .bEndOfFrame = bLastChunk,
            };
            pCallback(kLTMediaEvent_Data, &clientPacket, pEventProcClientData);
            chunkStart = chunkEnd + 1;
        }
    }
}

static void
DispatchH264DataEventComplete(LTEvent hEvent, LTArgs *pEventArgs) {
    LT_UNUSED(hEvent);
    LTMediaSourceContext *source  = LTArgs_pointerAt(0, pEventArgs);
    if (!source) return;
    if (!source->dataAllocated) return;

    if (-1 == xioctl(source->fd, VIDIOC_QBUF, &source->buf)) {
        LTLOG_REDALERT("disp.qbuf.err", "VIDIOC_QBUF error %d, %s", errno, strerror(errno));
        return;
    }

    source->dataAllocated = false;
    if (source->running) {
        s_pThread->QueueTaskProcIfRequired(s_pThread->GetCurrentThread(), PollH264Stream, NULL, source);
    }
}

static void
LTVideoSource_OnMediaEvent(LTMediaSource hSource, LTMediaSource_OnMediaEventProc *pCallback, void *pClientData) {
    LTMediaSourceContext *source = LT_GetCore()->ReserveHandlePrivateData(hSource);
    if (!source) return;
    s_pEvent->RegisterForEvent(source->event, (void *)pCallback, NULL, pClientData, false);
    LT_GetCore()->ReleaseHandlePrivateData(hSource, source);
}

static void
LTVideoSource_NoMediaEvent(LTMediaSource hSource, LTMediaSource_OnMediaEventProc *pCallback) {
    LTMediaSourceContext *source = LT_GetCore()->ReserveHandlePrivateData(hSource);
    if (!source) return;
    s_pEvent->UnregisterFromEvent(source->event, (void *)pCallback);
    LT_GetCore()->ReleaseHandlePrivateData(hSource, source);
}

static void
LTVideoSource_OnModeChangeEvent(LTMediaSource hSource, LTMediaSource_OnModeChangeEventProc *pCallback, void *pClientData) {
    /* No mode change events available for this driver */
    LT_UNUSED(hSource);
    LT_UNUSED(pCallback);
    LT_UNUSED(pClientData);
}

static void
LTVideoSource_NoModeChangeEvent(LTMediaSource hSource, LTMediaSource_OnModeChangeEventProc *pCallback) {
    /* No mode change events available for this driver */
    LT_UNUSED(hSource);
    LT_UNUSED(pCallback);
}

static LTMediaEncoding
LTVideoSource_GetMediaEncoding(LTMediaSource hSource) {
    LTMediaSourceContext *source = LT_GetCore()->ReserveHandlePrivateData(hSource);
    if (!source) return kLTMediaEncoding_Unknown;
    LTMediaEncoding encoding = source->mediaEncoding;
    LT_GetCore()->ReleaseHandlePrivateData(hSource, source);
    return encoding;
}

static bool
InitMemMap(LTMediaSourceContext *source) {
	struct v4l2_requestbuffers req;

	CLEAR(req);
	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(source->fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			LTLOG_REDALERT("mm.sup.err", "%s does not support memory mapping", dev_name);
		} else {
            LTLOG_REDALERT("mm.rqbuf.err", "VIDIOC_REQBUFS error %d, %s", errno, strerror(errno));
		}
        return false;
	}

	if (req.count < 2) {
		LTLOG_REDALERT("mm.dev.oom", "Insufficient buffer memory on %s", dev_name);
		return false;
	}

    source->numBuffers = req.count;
	source->buffers = lt_malloc(req.count * sizeof(Buffer));
	if (!source->buffers) {
		LTLOG_REDALERT("mm.buf.oom", "Out of memory for video buffers");
		return false;
	}
    lt_memset(source->buffers, 0, req.count * sizeof(Buffer));

	for (u32 i = 0; i < req.count; ++i) {
		struct v4l2_buffer buf;
		CLEAR(buf);

		buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory      = V4L2_MEMORY_MMAP;
		buf.index       = i;

		if (-1 == xioctl(source->fd, VIDIOC_QUERYBUF, &buf)) {
            LTLOG_REDALERT("mm.qrybuf.err", "VIDIOC_QUERYBUF error %d, %s", errno, strerror(errno));
            return false;
        }

        Buffer *localBuf = &source->buffers[i];
		localBuf->length = buf.length;
		localBuf->start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, source->fd, buf.m.offset);
		if (MAP_FAILED == localBuf->start) {
            LTLOG_REDALERT("mm.err", "mmap error %d, %s", errno, strerror(errno));
			return false;
        }
	}
    return true;
}

static bool
InitH264Encoder(LTMediaSourceContext *source) {
    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format fmt;

    if (-1 == xioctl(source->fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
            LTLOG_REDALERT("init.nodev", "%s is no V4L2 device", dev_name);
        } else {
            LTLOG_REDALERT("init.qrycap.err", "VIDIOC_QUERYCAP error %d, %s", errno, strerror(errno));
        }
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LTLOG_REDALERT("init.notcap", "%s is no video capture device", dev_name);
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        LTLOG_REDALERT("init.nostrm", "%s does not support streaming i/o", dev_name);
        return false;
    }

    CLEAR(cropcap);
    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (0 == xioctl(source->fd, VIDIOC_CROPCAP, &cropcap)) {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect; /* reset to default */

        if (-1 == xioctl(source->fd, VIDIOC_S_CROP, &crop)) {
            switch (errno) {
            case EINVAL:
                /* Cropping not supported. */
                break;
            default:
                /* Errors ignored. */
                break;
            }
        }
    } else {
        /* Errors ignored. */
    }

    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = 640;
    fmt.fmt.pix.height      = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;

    if (-1 == xioctl(source->fd, VIDIOC_S_FMT, &fmt)) {
        LTLOG_REDALERT("init.fmt.err", "VIDIOC_S_FMT error %d, %s", errno, strerror(errno));
        return false;
    }

    /* Note VIDIOC_S_FMT may change width and height. */

    return InitMemMap(source);
}

static void
LTVideoSource_Start(LTMediaSource hSource) {
    LTMediaSourceContext *source = LT_GetCore()->ReserveHandlePrivateData(hSource);
    if (!source) return;
    switch (source->mediaEncoding) {
        case kLTMediaEncoding_H264: {
            if (source->running) {
                LT_GetCore()->ReleaseHandlePrivateData(hSource, source);
                return;
            }
            for (u32 i = 0; i < source->numBuffers; ++i) {
                struct v4l2_buffer buf;
                CLEAR(buf);
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = i;
                buf.flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;

                LTLOG_DEBUG("strt.qbuf", "VIDIOC_QBUF bufs=%lu fd=%lu", LT_Pu32(source->numBuffers), LT_Pu32(source->fd));
                if (-1 == xioctl(source->fd, VIDIOC_QBUF, &buf)) {
                    LTLOG_REDALERT("strt.qbuf.err", "VIDIOC_QBUF error %d, %s", errno, strerror(errno));
                    LT_GetCore()->ReleaseHandlePrivateData(hSource, source);
                    return;
                }
            }
            enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (-1 == xioctl(source->fd, VIDIOC_STREAMON, &type)) {
                LTLOG_REDALERT("strt.strmon.err", "VIDIOC_STREAMON error %d, %s", errno, strerror(errno));
                LT_GetCore()->ReleaseHandlePrivateData(hSource, source);
                return;
            }

            source->running = true;
            s_pThread->QueueTaskProcIfRequired(s_CaptureThread, PollH264Stream, NULL, source);
            break;
        }
        default:
            LTLOG_YELLOWALERT("src.start.bad.encoding", "Unsupported encoding for source start: %lu", LT_Pu32(source->mediaEncoding));
            break;
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSource, source);
}

static void
LTVideoSource_Stop(LTMediaSource hSource) {
    LTMediaSourceContext *source = LT_GetCore()->ReserveHandlePrivateData(hSource);
    if (!source) return;

	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(source->fd, VIDIOC_STREAMOFF, &type)) {
        LTLOG_REDALERT("stop.strmoff.err", "VIDIOC_STREAMOFF error %d, %s", errno, strerror(errno));
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSource, source);
}

static void
LTVideoSource_Trigger(LTMediaSource hSource) {
    LT_UNUSED(hSource);
}

static void
LTVideoSource_SetTargetBitrate(LTMediaSource hSource, u32 targetBitrate) {
    LT_UNUSED(hSource);
    LT_UNUSED(targetBitrate);
}

static void
LTVideoSource_SetGopLength(LTMediaSource hSource, u32 nGopLength) {
    LT_UNUSED(hSource);
    LT_UNUSED(nGopLength);
}

static LTMediaSource
LTVideoSource_Open(LTMediaFormat *pFormat) {
    if (!LTDriverMediaImpl_SupportsFormat(pFormat)) return 0;

    LTMediaSource hSource = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTMediaSource, sizeof(LTMediaSourceContext));
    LTMediaSourceContext *source = LT_GetCore()->ReserveHandlePrivateData(hSource);
    if (!source) return 0;
    do {
        lt_memset(source, 0, sizeof(LTMediaSourceContext));
        source->handle = hSource;
        source->event   = LT_GetCore()->CreateEvent(&s_MediaDataEventArgs, DispatchH264DataEventCB, DispatchH264DataEventComplete, NULL, NULL);
        source->mediaEncoding = pFormat->nEncoding;

        struct stat st;
        if (-1 == stat(dev_name, &st)) {
            LTLOG_REDALERT("open.stat.err", "Cannot identify '%s': %d, %s", dev_name, errno, strerror(errno));
            break;
        }

        if (!S_ISCHR(st.st_mode)) {
            LTLOG_REDALERT("open.ischr.err", "%s is no device", dev_name);
            break;
        }

        source->fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
        if (-1 == source->fd) {
            LTLOG_REDALERT("open.err", "Cannot open '%s': %d, %s", dev_name, errno, strerror(errno));
            break;
        }

        if (!InitH264Encoder(source)) {
            LTLOG_REDALERT("enc.init.err", "H.264 encoder init failed");
            break;
        }
        LTLOG_DEBUG("enc.init.succ", "H264 Encoder initialized");

        LT_GetCore()->ReleaseHandlePrivateData(hSource, source);
        return hSource;
    } while(false);

    LT_GetCore()->ReleaseHandlePrivateData(hSource, source);
    lt_destroyhandle(hSource);
    return 0;
}

static void
LTVideoSource_Destroy(LTMediaSource hSource) {
    LTMediaSourceContext *source = LT_GetCore()->ReserveHandlePrivateData(hSource);
    if (!source) return;
    LTVideoSource_Stop(hSource);
    LT_GetCore()->ReleaseHandlePrivateData(hSource, source);
}

static LTMediaSink
LTVideoSink_Open(LTMediaFormat *pFormat) {
    /* Currently don't support video output */
    LT_UNUSED(pFormat);
    return 0;
}

/*_________________________________________
 / LTDriverMedia library implementation */

static bool
LTDriverMediaImpl_SupportsFormat(LTMediaFormat *pFormat) {
    LT_UNUSED(pFormat);
    if (pFormat->nKind == kLTMediaKind_Video_HD || pFormat->nKind == kLTMediaKind_Video_SD) {
        if (pFormat->nEncoding != kLTMediaEncoding_H264) return false;
        return true;
    }
    return false;
}

static bool
T31DriverMediaVideoImpl_LibInit(void) {
    s_pThread  = lt_getlibraryinterface(ILTThread, LT_GetCore());
    s_pEvent   = lt_getlibraryinterface(ILTEvent, LT_GetCore());

    s_CaptureThread = LT_GetCore()->CreateThread("VideoCapture");
    s_pThread->SetStackSize(s_CaptureThread, 2048);
    s_pThread->Start(s_CaptureThread, NULL, NULL);

    return true;
}

static void
T31DriverMediaVideoImpl_LibFini(void) {
    s_pThread->Terminate(s_CaptureThread);
    s_pThread->WaitUntilFinished(s_CaptureThread, LTTime_Infinite());
    lt_destroyhandle(s_CaptureThread);
    s_CaptureThread = 0;
}

/*___________________________________________________
 / T31DriverMediaVideo library root interface binding */
typedef_LTLIBRARY_ROOT_INTERFACE(T31DriverMediaVideo, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(T31DriverMediaVideo)    LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTDriverMedia) {
    .SupportsFormat = &LTDriverMediaImpl_SupportsFormat,
    .OpenSource     = &LTVideoSource_Open,
    .OpenSink       = &LTVideoSink_Open,
} LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTMediaSource, LTVideoSource_Destroy) {
    .GetMediaEncoding = &LTVideoSource_GetMediaEncoding,
    .OnMediaEvent     = &LTVideoSource_OnMediaEvent,
    .NoMediaEvent     = &LTVideoSource_NoMediaEvent,
    .OnModeChangeEvent = &LTVideoSource_OnModeChangeEvent,
    .NoModeChangeEvent = &LTVideoSource_NoModeChangeEvent,
    .Start            = &LTVideoSource_Start,
    .Stop             = &LTVideoSource_Stop,
    .Trigger          = &LTVideoSource_Trigger,
    .SetTargetBitrate = &LTVideoSource_SetTargetBitrate,
    .SetGopLength     = &LTVideoSource_SetGopLength,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(T31DriverMediaVideo, (ILTDriverMedia));

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  26-Sep-23   trajan      created
 */
