/*******************************************************************************
 * source/lt/device/usb/LTDeviceUsbWebcam.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTArray.h>
#include <lt/device/usb/LTDeviceUsbClient.h>
#include <lt/device/usb/LTDeviceUsbWebcam.h>
#include <lt/device/identity/LTDeviceIdentity.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/video/LTDeviceVideo.h>
#include <lt/system/usb/LTSystemUsbBusManager.h>
#include <lt/device/pins/LTDevicePins.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/device/imagesensor/LTDeviceImageSensor.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include "LTDeviceUsbWebcamStd.h"

DEFINE_LTLOG_SECTION("dev.usbwebcam");

/* Note: Due to the device running as a high-speed USB device (1 packet every 125us), logging significantly impairs timing. 
    It is not uncommon for logs to cause the device not to enumerate on the host (especially on Linux) or for many packets to be dropped */
#define USBWEBCAM_DO_LOG     0
#if     USBWEBCAM_DO_LOG
#define DLOG                 LTLOG
#else
#define DLOG                 LTLOG_LOGNULL
#endif

enum {
    kEndpointControl = 0,
    kbInterval = 1,
    kVideoDeviceChannel = kLTDeviceVideo_Channel_H264HD,
    kVideoSource = kLTDeviceVideo_Source_0
};

/* ______________________________________ 
 * Forward Declaration
 */
typedef struct __attribute__((packed)) {
    LTUsbConfigDescriptor config;
    LTUsbInterfaceAssociationDescriptor interfaceAssociation;
    LTUsbInterfaceDescriptor standardVCInterface;
    LTDeviceUsbWebcamClassSpecificVCInterfaceDescriptor classSpecificVCInterface;
    LTDeviceUsbWebcamInputTerminalDescriptor inputTerminal;
    LTDeviceUsbWebcamProcessingUnitDescriptor processingUnit;
    LTDeviceUsbWebcamExtensionUnitDescriptor extensionUnit;
    LTDeviceUsbWebcamOutputTerminalDescriptor outputTerminal;
    LTUsbInterfaceDescriptor standardVSInterfaceSetting0;
    LTDeviceUsbWebcamClassSpecificVSHeaderDescriptor classSpecificVSHeader;
    LTDeviceUsbWebcamClassSpecificVSFormatDescriptor classSpecificVSFormat;
    LTDeviceUsbWebcamClassSpecificVSFrameDescriptor classSpecificVSFrame;
    LTDeviceUsbWebcamVSColorMatchingDescriptor colorMatchingDescriptor;
    LTUsbInterfaceDescriptor standardVSInterfaceSetting1;
    LTUsbEndpointDescriptor endpointStandardVSISO;
} LTDeviceUsbWebcamConfigDescriptor;

typedef struct LTDeviceUsbWebcamPort LTDeviceUsbWebcamPort;

typedef_LTObjectImpl(LTDeviceUsbWebcam, LTDeviceUsbWebcamImpl) {
    LTDeviceUsbWebcamPort *port;
} LTOBJECT_API;

// TODO: Rename - not strictly a port
struct LTDeviceUsbWebcamPort {
    LTDeviceUsbWebcamImpl *user;
    u16 frameCount;
    
    u8 *currentFrameDataAddress;
    u32 currentFrameDataLength;

    u8 *nextFrameDataAddress;
    u32 nextFrameDataLength;

    u32 currentFrameOffset;
    bool fid;
    bool frameReady;
    bool midFrame;
    u32 presentationTime;
    LTDeviceUsbWebcamVideoProbeCommitControls probeControls;
    LTDeviceUsbWebcamVideoProbeCommitControls commitControls;
    LTDeviceUsbWebcamUVCXVideoConfigProbeCommitControls uvcxProbeControls;
    LTDeviceUsbWebcamUVCXVideoConfigProbeCommitControls uvcxCommitControls;
    LTDeviceVideo_Source videoSource;
    bool isVideoDeviceStarted;
    bool firstStartVideoFrame;
    bool firstStartVidDevice;

    u16 SOFCounter;
    s32 previousSendTime;

    LTDeviceVideo_VideoData *vidData;
};

/* ________________
 * Static variables 
 */
static struct Statics {
    u32 endpointVS;
    u32 maxPacketSize;

    LTArray *stringDescriptors;
    LTDeviceUsbClient *lDevUsbClient;
    ILTDriverUsbClientDeviceUnit *iDevUsbClient;
    LTDeviceUnit hDevUsbClient;
    LTDeviceConfig *deviceConfig;
    LTDeviceIdentity *deviceIdentity;

    LTUsbDeviceDescriptor deviceDescriptor;
    LTUsbDeviceQualifierDescriptor deviceQualifierDescriptor;
    LTUsbOtherSpeedConfigDescriptor otherSpeedConfigDescriptor;

    LTDeviceUsbWebcamConfigDescriptor *configDescriptor;

    LTDeviceUsbWebcamPort *port;

    LTDeviceVideo *lDevVideo;
    LTMediaResolution *deviceResolution;
    u32 framerate;
    u32 streamBitrate;
    u32 frameInterval;

    LTDevicePins *pins;
    LTDeviceUnit ircut_A;
    LTDeviceUnit ircut_B;
    ILTDriverPins_BidirectionalBank *iPinBank;

    LTSystemUsbBusManager *usbManager;
    LTOThread *mainThread;
} S;

const char *kLTMailboxTransportUsb_UsbMode = "webcam";

/** ___________________________
 * IR Filter
 */
static void setupPins(void) {
    u32 index = 0;

    S.ircut_A = LTHANDLE_INVALID;
    S.ircut_B = LTHANDLE_INVALID;

    if (!S.pins->GetUnitNumberFromBankName("IRCUT A", &index)) {
       return;  
    } 
    S.ircut_A = S.pins->CreateDeviceUnitHandle(index);

    if (!S.pins->GetUnitNumberFromBankName("IRCUT B", &index)) {
        return;
    }
    S.ircut_B = S.pins->CreateDeviceUnitHandle(index);
    
    if (S.ircut_A == LTHANDLE_INVALID || S.ircut_B == LTHANDLE_INVALID) {
        LTLOG("setup.pins", "Could not configure IR pins");
        return;
    }
    S.iPinBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, S.ircut_A);

    S.iPinBank->ConfigureAsOutput(S.ircut_A, kLTDevicePin_PinConfiguration_OutputType_PushPull);
    S.iPinBank->ConfigureAsOutput(S.ircut_B, kLTDevicePin_PinConfiguration_OutputType_PushPull);
}

static void ircutClose(void) {
    if (S.ircut_A == LTHANDLE_INVALID || S.ircut_B == LTHANDLE_INVALID) {
        return;
    }

    S.iPinBank->Set(S.ircut_A, true);
    S.iPinBank->Set(S.ircut_B, false);
    S.mainThread->API->Sleep(LTTime_Milliseconds(100));
    S.iPinBank->Set(S.ircut_A, false);
}

/* _________________
 * Thread procedures
 */

/** 
 *  Splits up a video frame into payloads of max 1024 bytes to be sent to the host via the VideoStreaming endpoint. 
 *  A single H264 frame, especially HD, is almost always larger than the maximum endpoint packet size of 1024 bytes.
 * 
 *  A Presentation Time Stamp (PTS), System Time Clock (STC) and a 1KHz SOF Counter are included in the payload header. 
 */
static void SendNewFrameProc(void *clientData) {
    LT_UNUSED(clientData);

    u32 maxPayloadSize = S.maxPacketSize;
    const u32 headerSize = sizeof(LTDeviceUsbWebcamPayloadHeader);
    const u32 maxDataSize = maxPayloadSize - headerSize;

    const LTTime kernelTime = LT_GetCore()->GetKernelTime();
    const s32 currentKernelTime = (s32)LTTime_GetMicroseconds(kernelTime);
    
    if (currentKernelTime - S.port->previousSendTime >= 1000) {
        S.port->SOFCounter = (S.port->SOFCounter + 1) & 0x3FF;
        S.port->previousSendTime = currentKernelTime;
    }

    u16 sourceClock[3];
    sourceClock[0] = (u16)(currentKernelTime & 0xFFFF);           // STC low
    sourceClock[1] = (u16)((currentKernelTime >> 16) & 0xFFFF);   // STC high
    sourceClock[2] = S.port->SOFCounter;  

    // Create H264 payload header
    LTDeviceUsbWebcamPayloadHeader header = (LTDeviceUsbWebcamPayloadHeader) {
        .bHeaderLength = headerSize,
        .bmHeaderInfo = 0x8C | (S.port->fid ? 0x01 : 0x00),  // PTS + SCR + FID
        .dwPresentationTime = S.port->presentationTime,
        .scrSourceClock = {sourceClock[0], sourceClock[1], sourceClock[2]} // 0-4 = STC,
    };

    if (!S.port->frameReady) {
        s32 written = S.iDevUsbClient->Write(S.hDevUsbClient, S.endpointVS, (const u8*)&header, headerSize);

        if (written < 0) {
            // Could be an LT_YELLOWALERT, however logging takes too long and causes errors on the host
            DLOG("vs.error", "Failed to write empty packet: %d", (int)written);
        }
        return;
    }

    // Check if we need to start a new frame
    if (!S.port->midFrame) {
        // Address always remains the same
        S.port->currentFrameDataAddress = S.port->nextFrameDataAddress;
        S.port->currentFrameDataLength = S.port->nextFrameDataLength;

        S.port->midFrame = true;
        S.port->currentFrameOffset = 0;
        DLOG("vs.start", "Starting transmission of frame size %u", S.port->currentFrameDataLength);
    }

    // Calculate how much data to send in this packet
    const u32 remainingBytes = S.port->currentFrameDataLength - S.port->currentFrameOffset;
    const u32 packetDataSize = (remainingBytes > maxDataSize) ? maxDataSize : remainingBytes;
    bool isLastPacket = (S.port->currentFrameOffset + packetDataSize >= S.port->currentFrameDataLength);

    // Set EOF bit for last packet
    if (isLastPacket) {
        header.bmHeaderInfo |= 0x02;
    }

    // Write the header to the EP FIFO but do not send it to the host
    s32 headerWritten = S.iDevUsbClient->FifoOnlyWrite(S.hDevUsbClient, S.endpointVS, (const u8*)&header, headerSize);

    if (headerWritten < 0 || (u32)headerWritten != headerSize) {
        // Could be an LT_YELLOWALERT, however logging takes too long and causes errors on the host
        DLOG("vs.error", "Failed to write packet: %d", (int)headerWritten);
        return;
    }
    
    if (packetDataSize > 0) {
        // Write the packet data to the EP FIFO and then send it along with the header
        s32 written = S.iDevUsbClient->Write(S.hDevUsbClient, S.endpointVS, S.port->currentFrameDataAddress + S.port->currentFrameOffset, packetDataSize);

        if (written < 0 || (u32)written != packetDataSize) {
            // Could be an LT_YELLOWALERT, however logging takes too long and causes errors on the host
            DLOG("vs.error", "Failed to write packet: %d", (int)written);
            return;
        }
    }

    S.port->currentFrameOffset += packetDataSize;

    // Check if frame transmission is complete
    if (isLastPacket) {
        S.port->midFrame = false;
        S.port->fid = !S.port->fid;
        S.port->frameReady = false;
        S.lDevVideo->ReleaseVideoData(kVideoDeviceChannel, S.port->vidData);
    }
}

static void VideoProc(LTDeviceVideo_Channel channel, LTDeviceVideo_Event event, LTDeviceVideo_VideoData *videoData, void *clientData) {
    LT_UNUSED(clientData);
    if (event == kLTDeviceVideo_Event_FrameReady) {
        switch(channel) {
            case kLTDeviceVideo_Channel_H264HD:
            case kLTDeviceVideo_Channel_H264SD:
                DLOG("h264", "vid %d chn %d type %d seq %u time %d len %u data %p, data at address %u", S.port->frameCount, channel, videoData->type, videoData->sequence, (s32)LTTime_GetMilliseconds(videoData->time), videoData->length, videoData->address, *videoData->address);

                if (!S.port->firstStartVidDevice) { S.port->firstStartVidDevice = true; }

                S.port->frameCount++;
                S.port->frameReady = true;

                S.port->nextFrameDataAddress = videoData->address;
                S.port->nextFrameDataLength = videoData->length;

                S.port->vidData = videoData;
                
                S.port->presentationTime = (s32)LTTime_GetMicroseconds(LT_GetCore()->GetKernelTime());
                
                // Queue the first video frame to be transmitted to start request back and forth. VS endpoint handler is then called after this
                if (!S.port->firstStartVideoFrame) {
                    // Preload first frame into current frame data objects
                    S.port->currentFrameDataAddress = videoData->address;
                    S.port->currentFrameDataLength = videoData->length;

                    S.mainThread->API->QueueTaskProc(S.mainThread, SendNewFrameProc, NULL, NULL);
                    S.port->SOFCounter = 0;
                    S.port->previousSendTime = S.port->presentationTime;

                    S.port->firstStartVideoFrame = true;
                }
                S.lDevVideo->Poll(kVideoDeviceChannel);
                break;
            default:
            break;
        }
    }
    else if (event == kLTDeviceVideo_Event_FrameDrop) {
        DLOG("video.proc", "video frame dropped");
    }
    else if (event == kLTDeviceVideo_Event_FrameFail) {
        DLOG("video.proc", "video frame failed before encoding");
    }
}

static void ResumeVideoDevice(void *clientData) {
    LT_UNUSED(clientData);
    // Significantly reduces non-existing PPS errors
    S.lDevVideo->RequestIdrFrame(kVideoDeviceChannel);
    S.lDevVideo->Resume();
    S.port->isVideoDeviceStarted = true;
}

static void PauseVideoDevice(void *clientData) {
    LT_UNUSED(clientData);
    S.lDevVideo->Pause();
}

static void CaptureProc(void *clientData) {
    LTDeviceVideo_Channel channel = kVideoDeviceChannel;
    S.lDevVideo->OnVideoEvent(channel, VideoProc, clientData);
    S.lDevVideo->Poll(kVideoDeviceChannel);
    // This ensures the video pipeline starts capturing again no matter if it was reassigned by the bus manager or not
    ResumeVideoDevice(clientData);
}

static void StartVideoDevice(void *clientData) {
    LT_UNUSED(clientData);
    // Initialize video source properly
    if (!S.lDevVideo->Enable(S.port->videoSource)) {
        LTLOG_YELLOWALERT("webcam.error", "Failed to enable video source");
        return;
    }

    if (!S.lDevVideo->Start(kVideoDeviceChannel)) {
        LTLOG_YELLOWALERT("webcam.error", "Failed to start H264 channel");
        return;
    }

    // Set ISP tuning mode to kLTDeviceVideo_ISPTuningModeNaturalDay, output is black and white otherwise
    LTDeviceVideo_ISPTuningMode newISPTuningMode = kLTDeviceVideo_ISPTuningModeNaturalDay;
    if (!S.lDevVideo->SetParam(kLTDeviceVideo_Param_ISPTuningMode, &newISPTuningMode)) {
        LTLOG_YELLOWALERT("start.video", "Failed to set ISP tuning mode to natural day");
    }

    // Disable OSD
    LTDeviceVideo_Osd osd = {};
    osd.channel = kVideoDeviceChannel;
    osd.data = "2025-09-04 13:30:00";
    osd.bEnable = false;
    if(!S.lDevVideo->SetParam(kLTDeviceVideo_Param_OsdLogo, &osd)) {
        LTLOG_YELLOWALERT("start.video", "Failed to disable OSD Logo");
    }

    if(!S.lDevVideo->SetParam(kLTDeviceVideo_Param_OsdTimestamp, &osd)) {
        LTLOG_YELLOWALERT("start.video", "Failed to disable OSD Timestamp");        
    }

    // Start video frame capture
    LTDeviceVideo_Channel channel = kVideoDeviceChannel;
    S.mainThread->API->QueueTaskProc(S.mainThread, CaptureProc, NULL, &channel);

    // Close IR filter to remove purple tint if possible
    ircutClose();

    S.port->isVideoDeviceStarted = true;
}

static void StopVideoDevice(void) {
    DLOG("stop.device", "Stopping device no release");

    // Ensures the video pipeline is capturing and able to serve other devices/drivers after the webcam driver is reassigned
    ResumeVideoDevice(NULL);

    S.port->firstStartVidDevice = false;

    S.lDevVideo->ReleaseVideoData(kVideoDeviceChannel, S.port->vidData);
    S.lDevVideo->NoVideoEvent(kVideoDeviceChannel, VideoProc);

    S.lDevVideo->Stop(kVideoDeviceChannel);

    // TODO: Investigate - supected conflict with AltaMfgVideo when doing S.lDevVideo->Disable()
}

static void LT_ISR_SAFE SetIoBuf(LTUsbIOBuffer *ioBuf, void *data, u32 size) {
    ioBuf->data = data;
    ioBuf->size = size;
    ioBuf->processed = 0;
    ioBuf->releaseProc = NULL;
}

/* _________________________________________________
 * UVC Request Methods
 */
static bool LT_ISR_SAFE RequestGetCur(LTUsbDeviceRequest const *request, LTUsbIOBuffer *ioBuf) {
    u8 unitId = (request->wIndex >> 8) & 0xFF;
    u8 controlSelector = request->wValue & 0xFF;

    DLOG("get.cur", "GETCUR REQUEST wValue %u, index: %u, unitID: %u, controlSelector %u", request->wValue, request->wIndex, unitId, controlSelector);

    // Check if request is for the extension unit (i.e. H264 UVCX)
    if (unitId == 0x03) {
        if (controlSelector == kLTDeviceUsbWebcam_UVCXControlSelector_Video_Config_Probe) { // UVCX_VIDEO_CONFIG_PROBE
            SetIoBuf(ioBuf, &S.port->uvcxProbeControls, sizeof(S.port->uvcxProbeControls));
            return true;
        } else if (controlSelector == kLTDeviceUsbWebcam_UVCXControlSelector_Video_Config_Commit) { // UVCX_VIDEO_CONFIG_COMMIT
            SetIoBuf(ioBuf, &S.port->uvcxCommitControls, sizeof(S.port->uvcxCommitControls));
            return true;
        }
    }

    // Otherwise proceed with standard requests
    S.port->probeControls = (LTDeviceUsbWebcamVideoProbeCommitControls) {
        .bmHint = 0x0001,                      // Resolution hint
        .bFormatIndex = 0x01,                 
        .bFrameIndex = 0x01,                   
        .dwFrameInterval = S.frameInterval,         
        .wKeyFrameRate = S.framerate,                   
        .wPFrameRate = 0,                      
        .wCompQuality = 0x00,                
        .wCompWindowSize = 0x00,               
        .wDelay = 0x00,                        
        .dwMaxVideoFrameSize = 0x00200000,     
        .dwMaxPayloadTransferSize = S.maxPacketSize,   
        .dwClockFrequency = 48000000,          // 48MHz clock (value irrelevant, must be non-zero for Windows)
        .bmFramingInfo = 0x03,                 // FID and EOF bits
        .bPreferredVersion = 0x01,             
        .bMinVersion = 0x01,                   
        .bMaxVersion = 0x01,                 
    };

    if (request->wValue == kLTDeviceUsbWebcam_ControlSelector_VS_Probe_Control) {
        SetIoBuf(ioBuf, &S.port->probeControls, sizeof(S.port->probeControls));
        return true;
    }
    else if (request->wValue == kLTDeviceUsbWebcam_ControlSelector_VS_Commit_Control) {
        SetIoBuf(ioBuf, &S.port->commitControls, sizeof(S.port->commitControls));
        return true;
    }
    return false;
}

static bool LT_ISR_SAFE RequestSetCur(LTUsbDeviceRequest const *request, LTUsbIOBuffer *ioBuf) {
    u8 unitId = (request->wIndex >> 8) & 0xFF;
    u8 controlSelector = (request->wValue) & 0xFF;

   DLOG("set.cur", "SETCUR REQUEST wValue %u, index: %u, unitID: %u, controlSelector %u", request->wValue, request->wIndex, unitId, controlSelector);

    // Check if request is for the extension unit (i.e. H264 UVCX specific)
    if (unitId == 0x03) {
        if (controlSelector == kLTDeviceUsbWebcam_UVCXControlSelector_Video_Config_Probe) {
            SetIoBuf(ioBuf, &S.port->uvcxProbeControls, sizeof(S.port->uvcxProbeControls));
            return true;
        } else if (controlSelector == kLTDeviceUsbWebcam_UVCXControlSelector_Video_Config_Commit) {
            SetIoBuf(ioBuf, &S.port->uvcxCommitControls, sizeof(S.port->uvcxCommitControls));
            return true;
        }
    }
    
    // Otherwise proceed with standard requests
    if (request->wValue == kLTDeviceUsbWebcam_ControlSelector_VS_Probe_Control) {
        SetIoBuf(ioBuf, &S.port->probeControls, sizeof(S.port->probeControls));
        return true;
    }
    else if (request->wValue == kLTDeviceUsbWebcam_ControlSelector_VS_Commit_Control) {
        SetIoBuf(ioBuf, &S.port->commitControls, sizeof(S.port->commitControls));
        return true;
    }
    // Not interested in other requests
    return false;
}

static bool LT_ISR_SAFE RequestGetMin(LTUsbDeviceRequest const *request, LTUsbIOBuffer *ioBuf) {
    u8 unitId = (request->wIndex >> 8) & 0xFF;
    u8 controlSelector = (request->wValue >> 8) & 0xFF;

    DLOG("get.min", "GETMIN REQUEST wValue %u, index: %u, unitID: %u, controlSelector %u", request->wValue, request->wIndex, unitId, controlSelector);

    // Check if request is for the extension unit (i.e. H264 UVCX specific)
    if (unitId == 0x03) {
        if (controlSelector == 0x01 || controlSelector == 0x02) {
            SetIoBuf(ioBuf, &S.port->uvcxProbeControls, sizeof(S.port->uvcxProbeControls));
            return true;
        }
    }

    if (request->wValue == kLTDeviceUsbWebcam_ControlSelector_VS_Probe_Control) {
        LTDeviceUsbWebcamVideoProbeCommitControls minProbeControls = (LTDeviceUsbWebcamVideoProbeCommitControls) {
            .bmHint = 0x0001,                                 // Resolution hint
            .bFormatIndex = 0x01,                
            .bFrameIndex = 0x01,                   
            .dwFrameInterval = 300000,            
            .wKeyFrameRate = 14,              
            .wPFrameRate = 0,                                // No P-frame rate control
            .wCompQuality = 0x00,                            // Default compression quality
            .wCompWindowSize = 0x00,                         // Default window size
            .wDelay = 0x00,                                  // No delay
            .dwMaxVideoFrameSize = 0x00000010,               // 2MB max frame size for H.264
            .dwMaxPayloadTransferSize = S.maxPacketSize,    // 1024 bytes max payload
            .dwClockFrequency = 48000000,                    // 48MHz clock
            .bmFramingInfo = 0x03,                           // FID and EOF bits
            .bPreferredVersion = 0x01,             
            .bMinVersion = 0x01,            
            .bMaxVersion = 0x01,                 
        };

        SetIoBuf(ioBuf, &minProbeControls, sizeof(minProbeControls));
        return true;
    }
    return false;
}

static bool LT_ISR_SAFE RequestGetMax(LTUsbDeviceRequest const *request, LTUsbIOBuffer *ioBuf) {
    u8 unitId = (request->wIndex >> 8) & 0xFF;
    u8 controlSelector = (request->wValue >> 8) & 0xFF;

    DLOG("get.max", "GETMAX REQUEST wValue %u, index: %u, unitID: %u, controlSelector %u", request->wValue, request->wIndex, unitId, controlSelector);

    if (unitId == 0x03) {
        if (controlSelector == 0x01 || controlSelector == 0x02) {
            SetIoBuf(ioBuf, &S.port->uvcxProbeControls, sizeof(S.port->uvcxProbeControls));
            return true;
        }
    }

    if (request->wValue == kLTDeviceUsbWebcam_ControlSelector_VS_Probe_Control) {
        LTDeviceUsbWebcamVideoProbeCommitControls maxProbeControls = (LTDeviceUsbWebcamVideoProbeCommitControls) {
            .bmHint = 0x0001,                               // Resolution hint
            .bFormatIndex = 0x01,             
            .bFrameIndex = 0x01,                   
            .dwFrameInterval = 700000,            
            .wKeyFrameRate = 33,           
            .wPFrameRate = 0,                                // No P-frame rate control
            .wCompQuality = 0x00,                            // Default compression quality
            .wCompWindowSize = 0x00,                         // Default window size
            .wDelay = 0x00,                                  // No delay
            .dwMaxVideoFrameSize = 0x00300000,               // 2MB max frame size for H.264
            .dwMaxPayloadTransferSize = S.maxPacketSize,    // 1024 bytes max payload (matches endpoint)
            .dwClockFrequency = 48000000,                    // 48MHz clock
            .bmFramingInfo = 0x03,                           // FID and EOF bits
            .bPreferredVersion = 0x01,        
            .bMinVersion = 0x01,      
            .bMaxVersion = 0x01,                 
        };

        SetIoBuf(ioBuf, &maxProbeControls, sizeof(maxProbeControls));
        return true;
    }
    return false;
}

static bool LT_ISR_SAFE RequestGetRes(LTUsbDeviceRequest const *request, LTUsbIOBuffer *ioBuf) {
    u8 unitId = (request->wIndex >> 8) & 0xFF;
    u8 controlSelector = (request->wValue >> 8) & 0xFF;

    DLOG("get.res", "GETRES REQUEST wValue %u, index: %u, unitID: %u, controlSelector %u", request->wValue, request->wIndex, unitId, controlSelector);

    if (unitId == 0x03) {
        LTDeviceUsbWebcamUVCXVideoConfigProbeCommitControls resUVCXProbeCommitControls = (LTDeviceUsbWebcamUVCXVideoConfigProbeCommitControls) {
            .dwFrameInterval = 100,
            .dwBitRate = 5000,
            .bmHints = 1,
            .wConfigurationIndex = 0,
            .wWidth = S.deviceResolution->width,
            .wHeight = S.deviceResolution->height,
            .wSliceUnits = 0,
            .wSliceMode = 0,
            .wProfile = 1, /* CHECK SPEC */
            .wIFramePeriod = 0,
            .wEstimatedVideoDelay = 10,
            .wEstimatedMaxConfigDelay = 50, 
            .bUsageType = 0,
            .bRateControlMode = 0, 
            .bTemporalScaleMode = 0,
            .bSpatialScaleMode = 0,
            .bSNRScaleMode = 0,
            .bStreamMuxOption = 0, 
            .bStreamFormat = 0,
            .bEntropyCABAC = 1,
            .bTimestamp = 1,
            .bNumOfReorderFrames = 0,
            .bPreviewFlipped = 0,
            .bView = 0,
            .bReserved1 = 0, 
            .bReserved2 = 0,
            .bStreamID = 0,
            .bSpatialLayerRatio = 0, 
            .wLeakyBucketSize = 10
        };

        if (controlSelector == 0x01 || controlSelector == 0x02) {
            SetIoBuf(ioBuf, &resUVCXProbeCommitControls, sizeof(resUVCXProbeCommitControls));
            return true;
        }
    }

    if (request->wIndex == 1) {
        if (request->wValue == kLTDeviceUsbWebcam_ControlSelector_VS_Probe_Control) {
            LTDeviceUsbWebcamVideoProbeCommitControls resolutionControls = {
                .bmHint = 0x0000,                    // No step for hints
                .bFormatIndex = 0x01,                // Step by 1 format
                .bFrameIndex = 0x01,                 // Step by 1 frame
                .dwFrameInterval = S.frameInterval,   // Step size for frame interval
                .wKeyFrameRate = 1,                  // Step by 1 for key frame rate
                .wPFrameRate = 0,                    // No step for P-frame rate
                .wCompQuality = 0x01,                // Step by 1 for quality (0-100)
                .wCompWindowSize = 0x00,             // No step for window size
                .wDelay = 0x00,                      // No step for delay
                .dwMaxVideoFrameSize = 0x00010000,   // Step size for max frame size (64KB increments)
                .dwMaxPayloadTransferSize = 0x0040,  // Step size for payload (64-byte increments)
                .dwClockFrequency = 1000000,         // Step size for clock (1MHz increments)
                .bmFramingInfo = 0x00,               // No step for framing info
                .bPreferredVersion = 0x01,          
                .bMinVersion = 0x01,                
                .bMaxVersion = 0x01,                 
            };
            
            SetIoBuf(ioBuf, &resolutionControls, sizeof(resolutionControls));
            return true;
        }
    }
    return false;
}

static bool LT_ISR_SAFE RequestGetDef(LTUsbDeviceRequest const *request, LTUsbIOBuffer *ioBuf) {
    u8 unitId = (request->wIndex >> 8) & 0xFF;
    u8 controlSelector = (request->wValue >> 8) & 0xFF;

    DLOG("get.def", "GETDEF REQUEST wValue %u, index: %u, unitID: %u, controlSelector %u", request->wValue, request->wIndex, unitId, controlSelector);

    if (unitId == 0x03) {
        if (controlSelector == 0x01 || controlSelector == 0x02) {
            SetIoBuf(ioBuf, &S.port->uvcxProbeControls, sizeof(S.port->uvcxProbeControls));
            return true;
        }
    }

    if (request->wIndex == 1) {
        if (request->wValue == kLTDeviceUsbWebcam_ControlSelector_VS_Probe_Control) {
            SetIoBuf(ioBuf, &S.port->probeControls, sizeof(S.port->probeControls));
            return true;
        }
    }
    return false;
}

static bool LT_ISR_SAFE RequestGetLen(LTUsbDeviceRequest const *request, LTUsbIOBuffer *ioBuf) {
    u8 unitId = (request->wIndex >> 8) & 0xFF;
    u8 controlSelector = (request->wValue >> 8) & 0xFF;

    DLOG("get.len", "GETLEN REQUEST wValue %u, index: %u, unitID: %u, controlSelector %u", request->wValue, request->wIndex, unitId, controlSelector);

    if (unitId == 0x03) {
        if (controlSelector == 0x01 || controlSelector == 0x02) {
            LT_SIZE probeCommitStructureSize = sizeof(LTDeviceUsbWebcamUVCXVideoConfigProbeCommitControls);
            SetIoBuf(ioBuf, &probeCommitStructureSize, sizeof(probeCommitStructureSize));
            return true;
        }
    }

    if (request->wIndex == 1){
        LT_SIZE probeCommitStructureSize = sizeof(LTDeviceUsbWebcamVideoProbeCommitControls);

        if (request->wValue == kLTDeviceUsbWebcam_ControlSelector_VS_Probe_Control) {
            SetIoBuf(ioBuf, &probeCommitStructureSize, sizeof(probeCommitStructureSize));
            return true;
        }
        else if (request->wValue == kLTDeviceUsbWebcam_ControlSelector_VS_Commit_Control) {
            SetIoBuf(ioBuf, &probeCommitStructureSize, sizeof(probeCommitStructureSize));
            return true;
        }
    }
    return false;
}

static bool LT_ISR_SAFE RequestGetInfo(LTUsbDeviceRequest const *request, LTUsbIOBuffer *ioBuf) {
    u8 interface = request->wIndex & 0xFF;
    u8 unitId = (request->wIndex >> 8) & 0xFF;
    u8 controlSelector = (request->wValue >> 8) & 0xFF;
    
    DLOG("init.uvc", "GET_INFO: Interface=%u, Unit=%u, Control=%u", interface, unitId, controlSelector);
    
    if (interface == 0) { 
        // Return that no controls are currently supported for the processing unit
        if (unitId == 0x02) {
            static u8 noControlsSupported = 0x00;
            SetIoBuf(ioBuf, &noControlsSupported, sizeof(noControlsSupported));
            return true;
        }
        // Return that UVCX_GET_CUR and UVCX_SET_CUR are supported for the extension unit
        else if (unitId == 0x03) {
            if (controlSelector == kLTDeviceUsbWebcam_UVCXControlSelector_Video_Config_Probe 
                || controlSelector == kLTDeviceUsbWebcam_UVCXControlSelector_Video_Config_Commit) { 
                static u8 controlCapabilities = 0x03; 
                SetIoBuf(ioBuf, &controlCapabilities, sizeof(controlCapabilities));
                return true;
            }
        }
    }
    else if (interface == 1) {
        if (request->wValue == kLTDeviceUsbWebcam_ControlSelector_VS_Probe_Control ||
            request->wValue == kLTDeviceUsbWebcam_ControlSelector_VS_Commit_Control) {
            static u8 streamingControlCapabilities = 0x03;
            SetIoBuf(ioBuf, &streamingControlCapabilities, sizeof(streamingControlCapabilities));
            return true;
        }
    }
    
    // For any unknown requests, return no capabilities instead of failing
    static u8 noControlsSupported = 0x00;
    SetIoBuf(ioBuf, &noControlsSupported, sizeof(noControlsSupported));
    return true; 
}


/* ___________________________________________________________________
 * Handlers
 */
static void LT_ISR_SAFE EndpointVSSendReadyHandler(void *clientData) {
    LT_UNUSED(clientData);

    /* Send a packet as soon as the endpoint is ready, payloads are not sent faster than requests come in. 
    Timing for sending when a SOF packet is received is handled by the USB controller */
    S.mainThread->API->QueueTaskProc(S.mainThread, SendNewFrameProc, NULL, NULL);
}

static void LT_ISR_SAFE ConfigOkHandler(bool ready, void *clientData) {
    LT_UNUSED(clientData);

    if (!ready) {
        if (S.port->isVideoDeviceStarted) {
            S.mainThread->API->QueueTaskProc(S.mainThread, PauseVideoDevice, NULL, NULL);
        }
        LTLOG_YELLOWALERT("sys.usbwebcam", "Error with webcam configuring webcam driver");
    }
}

static bool LT_ISR_SAFE DescriptorRequestHandler(LTUsbDeviceRequest const *request, LTUsbIOBuffer *ioBuf) {
    const u8 descriptorIndex = request->wValue & 0xff;
    const u8 descriptorType = request->wValue >> 8;

    // Device Qualifier and Other Speed Config descriptors not strictly necessary but required by Windows for high-speed devices
    switch (descriptorType) {
        case kLTDeviceUsbStd_DescriptorType_Device:
            SetIoBuf(ioBuf, &S.deviceDescriptor, LT_MIN((LT_SIZE)request->wLength, sizeof(S.deviceDescriptor)));
            return true;
        case kLTDeviceUsbStd_DescriptorType_DeviceQualifier:
            SetIoBuf(ioBuf, &S.deviceQualifierDescriptor, LT_MIN((LT_SIZE)request->wLength, sizeof(S.deviceQualifierDescriptor)));
            return true;
        case kLTDeviceUsbStd_DescriptorType_OtherSpeedConfig:
            SetIoBuf(ioBuf, &S.otherSpeedConfigDescriptor, LT_MIN((LT_SIZE)request->wLength, sizeof(S.otherSpeedConfigDescriptor)));
            SetIoBuf(ioBuf, &S.port->probeControls, sizeof(S.port->probeControls));
            return true;
        case kLTDeviceUsbStd_DescriptorType_Config:
            SetIoBuf(ioBuf, S.configDescriptor, LT_MIN((LT_SIZE)request->wLength, sizeof(*S.configDescriptor)));
            return true;
        case kLTDeviceUsbStd_DescriptorType_String: {
            const LTUsbStringDescriptor *stringDescriptor = S.stringDescriptors->API->Get(S.stringDescriptors, descriptorIndex, NULL);
            if (stringDescriptor) {
                SetIoBuf(ioBuf, (void *)stringDescriptor, LT_MIN(request->wLength, (u16)stringDescriptor->bLength));
                return true;
            }
            DLOG("desc.str", "Unknown string descriptor %u requested", (unsigned)descriptorIndex);
            return false;
        }
        default:
            // Descriptor type 15 is requested on Windows, not present in 1.1 spec
            DLOG("desc.unk", "Unknown descriptor requested, descriptorType %u", (unsigned)descriptorType);
            break;
    }
    return false;
}

/**
 * Standard UVC 1.1 requests are GET_CUR, SET_CUR along with GET_MIN, GET_MAX, GET_INFO, GET_RES, GET_DEF, GET_LEN
 * 
 * GET_CUR and SET_CUR negotiate controls such as resolution and framerate between the host and device. Parameters are probed first before being committed. Data streaming can then begin.
 * If control values cannot be agreed upon, streaming does not begin.
 * Video data streaming begins by setting the alternate setting of the VideoStreaming interface to 1 by the host via SET_INTERFACE. To stop streaming, the alternate setting is set to 0.
 * 
 * An example stream negotiation would be:
 *  - PROBE_CONTROL(SET_CUR) [HOST -> DEVICE]
 *  - PROBE_CONTROL(GET_CUR) [DEVICE -> HOST] (PROBE_CONTROL pair repeated until suitable values are found)
 *  - COMMIT_CONTROL(SET_CUR) [HOST -> DEVICE]
 *  - SET_INTERFACE(1)
 * 
 * For H264, an extension unit is required for UVC 1.1 which implements UVCX requests:
 *  - UVCX_PROBE_CONTROL(GET_LEN) [HOST -> DEVICE]
 *  - UVCX_PROBE_CONTROL(GET_MAX) [HOST -> DEVICE]
 *  - UVCX_PROBE_CONTROL(SET_CUR) [HOST -> DEVICE]
 *  - UVCX_PROBE_CONTROL(GET_CUR) [HOST -> DEVICE]
 *  - UVCX_PROBE_CONTROL(GET_CUR) [HOST -> DEVICE]
 *  - UVCX_PROBE_CONTROL(SET_CUR) [HOST -> DEVICE]
 *  - STANDARD PROBE/COMMIT requests
 */ 
static bool LT_ISR_SAFE DeviceRequestHandler(void *clientData, LTDeviceUsbStd_ControlStage stage, LTUsbDeviceRequest const *request, LTUsbIOBuffer *ioBuf) {
    LT_UNUSED(clientData);
    u8 requestType = request->bmRequestType & kLTDeviceUsbStd_RequestType_Type_Mask;

    if (requestType == kLTDeviceUsbStd_RequestType_Type_Standard && request->bRequest == kLTDeviceUsbStd_Request_GetDescriptor) {
        if (stage != kLTDeviceUsbStd_ControlStage_Setup) {
            return true;
        }
        return DescriptorRequestHandler(request, ioBuf);
    }
    else if (requestType == kLTDeviceUsbStd_RequestType_Type_Standard && request->bRequest == kLTDeviceUsbStd_Request_SetInterface) {        
        // Alternate Setting on Interface 1 is set to 1 whenever frames are sent across. Start and stop video device whenever setting is changed
        if (request->wIndex == 1) {
            if (request->wValue == 0) {
                if (S.port->isVideoDeviceStarted) {
                    S.mainThread->API->QueueTaskProc(S.mainThread, PauseVideoDevice, NULL, NULL);
                    S.port->isVideoDeviceStarted = false;
                    S.port->firstStartVideoFrame = false;
                    DLOG("device.request", "Paused video device");
                }
                return true;
            }
            else if (request->wValue == 1) {
                if (!S.port->isVideoDeviceStarted) {
                    if (!S.port->firstStartVidDevice) {
                        DLOG("start.device", "Starting to queue capture");    
                        S.mainThread->API->QueueTaskProc(S.mainThread, StartVideoDevice, NULL, NULL);
                        S.port->isVideoDeviceStarted = true;
                    }
                    else {
                        DLOG("alternate.setting", "Resuming video device");
                        S.mainThread->API->QueueTaskProc(S.mainThread, ResumeVideoDevice, NULL, NULL);
                    }
                }
                return true;
            }
        }
    }
    else if (requestType == kLTDeviceUsbStd_RequestType_Type_Class) {
        const u8 recipient = request->bmRequestType & kLTDeviceUsbStd_RequestType_Recipient_Mask;
        if (recipient != kLTDeviceUsbStd_RequestType_Recipient_Interface) {
            return false;
        }
        if (stage == kLTDeviceUsbStd_ControlStage_Status) {
            return true;
        }
        // Standard UVC requests
        switch(request->bRequest) {
            case kLTDeviceUsbWebcamStd_Request_Set_Cur:
                return RequestSetCur(request, ioBuf);
            case kLTDeviceUsbWebcamStd_Request_Get_Cur:
                return RequestGetCur(request, ioBuf);
            case kLTDeviceUsbWebcamStd_Request_Get_Min:
                return RequestGetMin(request, ioBuf);
            case kLTDeviceUsbWebcamStd_Request_Get_Max:
                return RequestGetMax(request, ioBuf);
            case kLTDeviceUsbWebcamStd_Request_Get_Res:
                return RequestGetRes(request, ioBuf);
            case kLTDeviceUsbWebcamStd_Request_Get_Def:
                return RequestGetDef(request, ioBuf);
            case kLTDeviceUsbWebcamStd_Request_Get_Len:
                return RequestGetLen(request, ioBuf);
            case kLTDeviceUsbWebcamStd_Request_Get_Info:
                return RequestGetInfo(request, ioBuf);
            default:
                return true;
        }
    }
    else {
        return true;
    }
    return false;
}

static u8 AddStringDescriptor(const char *value) {
    if (!value) return 0;
    u32 valueLen = lt_strlen(value);
    if (valueLen == 0) {
        value = "?";
        valueLen = 1;
    }
    u32 descriptorLen = sizeof(LTUsbStringDescriptor) + (valueLen * sizeof(u16));
    LTUsbStringDescriptor *descriptor = lt_malloc(descriptorLen);
    if (!descriptor) return 0;
    descriptor->bDescriptorType = kLTDeviceUsbStd_DescriptorType_String;
    descriptor->bLength = descriptorLen;
    for (u32 i = 0; i < valueLen; ++i) {
        descriptor->wData[i] = LT_LE16(value[i]);
    }
    s32 strIndex = S.stringDescriptors->API->Append(S.stringDescriptors, descriptor);
    if ((strIndex <= 0) || (strIndex > 255)) return 0;
    return strIndex;
}

static bool InitStringDescriptors(void) {
    S.stringDescriptors = lt_createobject(LTArray);
    if (!S.stringDescriptors) return false;

    static const LTUsbStringDescriptor languageDescriptor = {
        .bLength = 4,
        .bDescriptorType = kLTDeviceUsbStd_DescriptorType_String,
        .wData = { LT_LE16(0x0409) }, /* language index (0x0409 = US-English) */
    };
    return S.stringDescriptors->API->Append(S.stringDescriptors, &languageDescriptor) == 0;
}

static bool InitPort(void) {
    S.port = lt_malloc(sizeof(LTDeviceUsbWebcamPort));
    if (!S.port) return false;

    *S.port = (LTDeviceUsbWebcamPort) {
        .user = NULL,
        .frameCount = 0,
        .videoSource = kVideoSource,
        .currentFrameDataAddress = NULL,
        .currentFrameDataLength = 0,
        .nextFrameDataAddress = NULL,
        .nextFrameDataLength = 0,
        .currentFrameOffset = 0,
        .fid = false,
        .frameReady = false,
        .midFrame = false,
        .presentationTime = 0,
        .isVideoDeviceStarted = false,
        .firstStartVideoFrame = false,
        .firstStartVidDevice = false,
        .previousSendTime = 0,
        .SOFCounter = 0,
        .vidData = NULL
    };
        
    return true;
}

// Returns a heap-allocated string suitable for use as a serial number.
static LTString GetSerialNumber(void) {
    // Use the device ID by default if available
    const char *deviceID = S.deviceIdentity->GetDeviceId();
    if (deviceID && lt_strlen(deviceID) > 0) {
        return lt_strdup(deviceID);
    }

    // Generate a fallback serial number, in case we're still on the manufacturing line.
    const u32 randomStringSize = 6;
    LTString tempSN = ltstring_createempty(randomStringSize);
    LTSystemSettings *settings = lt_openlibrary(LTSystemSettings);
    if (!settings) {
        LTLOG_YELLOWALERT("settings.open", "Failed to open LTSystemSettings library");
        return tempSN;
    }
    if (!settings->GetStringValue("mfg/tempSN", &tempSN))
    {
        DLOG("settings.read", "Failed to read tempSN from settings, writing new one");
        static LTUtilityByteOps * s_byteOps = NULL;
        if (!s_byteOps) s_byteOps = lt_openlibrary(LTUtilityByteOps);
        
        // Generate a random 6-character factory ID using hex digits
        if (s_byteOps) {
            s_byteOps->GenRandomBytesAsHexString(randomStringSize/2, tempSN, randomStringSize+1);
        }

        if (!settings->SetStringValue("mfg/tempSN", tempSN))
        {
            LTLOG_YELLOWALERT("settings.write", "Failed to write tempSN to settings");
        }
    }
    lt_closelibrary(settings);
    return tempSN;
}

/**
 *  The device is described as a high-speed Miscallaneous (0x0F) USB device with 2 interfaces, each of the Video class (0x0E) interface type. A high-speed device sends requests every 125us
 * 
 *  Endpoint structure:
 *   - EP0 (64 bytes) [BIDIRECTIONAL]: Control Endpoint (requests, configuration etc.)
 *   - EP4 (1024 bytes, asynchronous + isochronous) [DEVICE -> HOST]: VideoStreaming interface endpoint - video frame data
 *   - TODO: EP1: VideoControl endpoint - modify parameters such as brightness, exposure, framerate. Also enable still image capture
 * 
 *  There are 2 interfaces:
 *   - Interface 0: All PROBE/COMMIT negotiation and other standard requests. Operates on EP0.
 *   - Interface 1: VideoStreaming interface where frame data is transmitted to the host. Operates exclusively on EP4.
 */
static bool InitUsbDevice(void) {
    u32 deviceSection, vendorId, productId;

    bool configValid = (deviceSection = S.deviceConfig->GetDeviceSection("LTDeviceUsbWebcam")) &&
                       (vendorId = S.deviceConfig->ReadInteger(deviceSection, "config/vendorId")) &&
                       (productId = S.deviceConfig->ReadInteger(deviceSection, "config/productId"));

    
    if (!configValid) return false;

    const char *modelName = S.deviceIdentity->GetModel();
    char modelNameCamera[sizeof(*modelName) + 11];
    lt_snprintf(modelNameCamera, sizeof(modelNameCamera), "%s Camera", modelName);

    if (!InitStringDescriptors()) return false;

    if (!InitPort()) return false;

    S.lDevUsbClient = lt_openlibrary(LTDeviceUsbClient);
    if (!S.lDevUsbClient) {
        DLOG("ctr.open", "Failed to open LTDeviceUsbClient library");
        return false;
    }

    S.lDevVideo = lt_openlibrary(LTDeviceVideo);
    if (!S.lDevVideo) {
        DLOG("ctr.open", "Failed to open video library");
        return false;
    }

    S.pins = lt_openlibrary(LTDevicePins);
    if (!S.pins) return false;

    setupPins();

    S.deviceResolution = lt_malloc(sizeof(LTMediaResolution));
    const LTDeviceVideo_Param resParam = (LTDeviceVideo_Channel)kVideoDeviceChannel == kLTDeviceVideo_Channel_H264HD ? kLTDeviceVideo_Param_ResolutionHD : kLTDeviceVideo_Param_ResolutionSD;

    // Get device resolution
    if (!S.lDevVideo->GetParam(resParam, S.deviceResolution)) {
        LTLOG_YELLOWALERT("start.device", "Could not obtain device resolution");
        return false;
    }
    DLOG("start.device", "Resolution width: %u, height: %u", S.deviceResolution->width, S.deviceResolution->height);

    // This parameter is not implemented in any driver except ak3918x, default is 20FPS
    if (!S.lDevVideo->GetParam(kLTDeviceVideo_Param_Framerate, &S.framerate) || S.framerate == 0) {
        LTLOG_YELLOWALERT("start.device", "Could not obtain device framerate, defaulting to 20 FPS");
        S.framerate = 20;
    }
    DLOG("start.device", "Framerate %u", S.framerate);

    // Value does not seem to matter as long as it is larger than the actual bitrate
    S.streamBitrate = 1152000;
    S.frameInterval = 10000000 / S.framerate;

    S.hDevUsbClient = LTHANDLE_INVALID;
    const u32 numHandles = S.lDevUsbClient->GetNumDeviceUnits();
    u8 numEndpoints = 0;

    for (u32 i = 0; i < numHandles; i++) {
        LTDeviceUnit client = S.lDevUsbClient->CreateDeviceUnitHandle(i);
        
        if (!client) {
            continue;
        }
        S.iDevUsbClient = lt_gethandleinterface(ILTDriverUsbClientDeviceUnit, client);

        const u32 requiredEndpoints = 2; // Control endpoint + VS endpoint

        numEndpoints = S.iDevUsbClient->GetNumEndpoints(client);

        u8 maxEPNum = -1;
        u32 epMaxPacketSize = 0;

        for (u8 j = 1; j < numEndpoints; j++) {
            const u16 packetSize = S.iDevUsbClient->GetEndpointMaxPacketSize(client, j);

            if (packetSize > epMaxPacketSize) {
                epMaxPacketSize = packetSize;
                maxEPNum = j;
            }
        }

        S.endpointVS = maxEPNum;
        S.maxPacketSize = epMaxPacketSize;

        if (numEndpoints < requiredEndpoints) {
            lt_destroyhandle(client);
            continue;
        }
        
        S.hDevUsbClient = client;
        break;
    }
    if (!S.hDevUsbClient) {
        DLOG("ctr.nohandle", "Failed to find a suitable device among 1 units");
        return false;
    }

    const LTString serialNumber = GetSerialNumber();

    // Set device descriptor
    S.deviceDescriptor = (LTUsbDeviceDescriptor) {
        .bLength            = kLTDeviceUsbStd_DescriptorSize_Device,
        .bDescriptorType    = kLTDeviceUsbStd_DescriptorType_Device,
        .bcdUSB             = kLTDeviceUsbStd_SpecVersion_2_0,
        .bDeviceClass       = kLTDeviceUsbStd_Class_MultiFunction,
        .bDeviceSubClass    = kLTDeviceUsbStd_SubClass_MultiFunction,
        .bDeviceProtocol    = kLTDeviceUsbStd_Protocol_MultiFunction,
        .bMaxPacketSize0    = S.iDevUsbClient->GetEndpointMaxPacketSize(S.hDevUsbClient, kEndpointControl),
        .idVendor           = LT_LE16(vendorId),
        .idProduct          = LT_LE16(productId),
        .bcdDevice          = 0x01,
        .iManufacturer      = AddStringDescriptor(S.deviceIdentity->GetManufacturer()),
        .iProduct           = AddStringDescriptor(modelNameCamera),
        .iSerialNumber      = AddStringDescriptor(serialNumber),
        .bNumConfigurations = 1,
    };

    // Specify configuration for USB 2.0 full-speed mode - same as high-speed mode
    S.deviceQualifierDescriptor = (LTUsbDeviceQualifierDescriptor) {
        .bLength = kLTDeviceUsbStd_DescriptorSize_DeviceQualifier,
        .bDescriptorType = kLTDeviceUsbStd_DescriptorType_DeviceQualifier,
        .bcdUSB = kLTDeviceUsbStd_SpecVersion_2_0,
        .bDeviceClass = kLTDeviceUsbStd_Class_MultiFunction,
        .bDeviceSubClass = kLTDeviceUsbStd_SubClass_MultiFunction,
        .bDeviceProtocol = kLTDeviceUsbStd_Protocol_MultiFunction,
        .bMaxPacketSize0 = S.iDevUsbClient->GetEndpointMaxPacketSize(S.hDevUsbClient, kEndpointControl),
        .bNumConfigurations = 1,
        .bReserved = 0
    };

    S.otherSpeedConfigDescriptor = (LTUsbOtherSpeedConfigDescriptor) {
        .bLength = kLTDeviceUsbStd_DescriptorSize_OtherSpeedConfig,
        .bDescriptorType = kLTDeviceUsbStd_DescriptorType_OtherSpeedConfig,
        .wTotalLength = sizeof(*S.configDescriptor),
        .bNumInterfaces = 2,
        .bConfigurationValue = 1,
        .iConfiguration = 0,
        .bmAttributes = kLTDeviceUsbStd_Attribute_Config_BusPower | kLTDeviceUsbStd_Attribute_Config_One,
        .bMaxPower = 0xFA
    };

    ltstring_destroy(serialNumber);
    if (!S.deviceDescriptor.iManufacturer || !S.deviceDescriptor.iProduct || !S.deviceDescriptor.iSerialNumber) return false;

    S.configDescriptor = lt_malloc(sizeof(LTDeviceUsbWebcamConfigDescriptor));
    S.configDescriptor->config = (LTUsbConfigDescriptor) {
        .bLength             = kLTDeviceUsbStd_DescriptorSize_Config,
        .bDescriptorType     = kLTDeviceUsbStd_DescriptorType_Config,
        .wTotalLength        = sizeof(*S.configDescriptor),
        .bNumInterfaces      = 2,
        .bConfigurationValue = 1,
        .iConfiguration      = 0,
        .bmAttributes        = kLTDeviceUsbStd_Attribute_Config_BusPower | kLTDeviceUsbStd_Attribute_Config_One,
        .bMaxPower           = 0xFA,
    };

    S.configDescriptor->interfaceAssociation = (LTUsbInterfaceAssociationDescriptor) {
        .bLength = kLTDeviceUsbStd_DescriptorSize_InterfaceAssociation,
        .bDescriptorType = kLTDeviceUsbStd_DescriptorType_InterfaceAssociation,
        .bFirstInterface = 0,
        .bInterfaceCount = 2,
        .bFunctionClass = kLTDeviceUsbStd_Class_Video,
        .bFunctionSubClass = kLTDeviceUsbStd_SubClass_Video_Interface_Collection,
        .bFunctionProtocol = 0,
        .iFunction = 0,
    };

    S.configDescriptor->standardVCInterface = (LTUsbInterfaceDescriptor) {
        .bLength            = kLTDeviceUsbStd_DescriptorSize_Interface,
        .bDescriptorType    = kLTDeviceUsbStd_DescriptorType_Interface,
        .bInterfaceNumber   = 0,
        .bAlternateSetting  = 0,
        .bNumEndpoints      = 0,
        .bInterfaceClass    = kLTDeviceUsbStd_Class_Video,
        .bInterfaceSubClass = kLTDeviceUsbStd_SubClass_Video,
        .bInterfaceProtocol = 0x00,
        .iInterface         = 0
    };

    // Set all class specific descriptors
    S.configDescriptor->classSpecificVCInterface = (LTDeviceUsbWebcamClassSpecificVCInterfaceDescriptor) {
        .bLength = kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VC_Interface,
        .bDescriptorType = kLTDeviceUsbWebcam_DescriptorType_CS_Interface,
        .bDescriptorSubType = 0x01,
        .bcdUVC = kLTDeviceUsbWebcamStd_UVC_Spec_Version_1_1,
        .wTotalLength = sizeof(LTDeviceUsbWebcamClassSpecificVCInterfaceDescriptor) + sizeof(LTDeviceUsbWebcamInputTerminalDescriptor) + sizeof(LTDeviceUsbWebcamProcessingUnitDescriptor) + sizeof(LTDeviceUsbWebcamOutputTerminalDescriptor) + sizeof(LTDeviceUsbWebcamExtensionUnitDescriptor),
        .dwClockFrequency = 48000000,
        .bInCollection = 0x01,
        .baInterfaceNr = 0x01
    };

    /* Video Chain: | Input Terminal-> Processing Unit -> Extension Unit -> Output Terminal |
     * Processing unit currently has no controls but is present for host compatibility */
    S.configDescriptor->inputTerminal = (LTDeviceUsbWebcamInputTerminalDescriptor) {
        .bLength = kLTDeviceUsbWebcam_DescriptorSize_Input_Terminal,
        .bDescriptorType = kLTDeviceUsbWebcam_DescriptorType_CS_Interface,
        .bDescriptorSubType = 0x02,
        .bTerminalID = 0x01,
        .wTerminalType = 0x0201,
        .bAssocTerminal = 0,
        .iTerminal = 0x00,
        .wObjectiveFocalLengthMin = 0x0000,
        .wObjectiveFocalLengthMax = 0x0000,
        .wOcularFocalLength = 0x0000,
        .bControlSize = 0x02,
        .bmControls = 0x0000,
    };
    
    S.configDescriptor->processingUnit = (LTDeviceUsbWebcamProcessingUnitDescriptor) {
        .bLength = kLTDeviceUsbWebcam_DescriptorSize_Processing_Unit,
        .bDescriptorType = kLTDeviceUsbWebcam_DescriptorType_CS_Interface,
        .bDescriptorSubType = 0x05,
        .bUnitID = 0x02,
        .bSourceID = 0x01, // Input Terminal ID
        .wMaxMultiplier = 0,
        .bControlSize = 1,
        .bmControls = 0,
        .iProcessing = 0,
        .bmVideoStandards = 0
    };

    S.configDescriptor->extensionUnit = (LTDeviceUsbWebcamExtensionUnitDescriptor) {
        .bLength = kLTDeviceUsbWebcam_DescriptorSize_Extension_Unit,
        .bDescriptorType = kLTDeviceUsbWebcam_DescriptorType_CS_Interface,
        .bDescriptorSubtype = 0x06,
        .bUnitID = 0x03,
        .guidExtensionCode = { 0x41, 0x76, 0x9E, 0xA2, 0x04, 0xDE, 0xE3, 0x47, 0x8B, 0x2B, 0xF4, 0x34, 0x1A, 0xFF, 0x00, 0x3B }, /* H264 Extension Unit GUID */
        .bNumControls = 0x02,
        .bNrInPins = 0x01,
        .baSourceID1 = 0x02,
        .bControlSize = 0x01,
        .bmControls = 0x03, /* Only UVCX_VIDEO_CONFIG_PROBE and UVCX_VIDEO_CONFIG_COMMIT */
        .iExtension = 0x00
    };


    S.configDescriptor->outputTerminal = (LTDeviceUsbWebcamOutputTerminalDescriptor) {
        .bLength = kLTDeviceUsbWebcam_DescriptorSize_Output_Terminal,
        .bDescriptorType = kLTDeviceUsbWebcam_DescriptorType_CS_Interface,
        .bDescriptorSubtype = 0x03,
        .bTerminalID = 0x04,
        .wTerminalType = 0x0101,
        .bAssocTerminal = 0,
        .bSourceID = 0x03,
        .iTerminal = 0x00
    };

    /* End of video chain descriptors */

    S.configDescriptor->standardVSInterfaceSetting0 = (LTUsbInterfaceDescriptor) {
        .bLength = kLTDeviceUsbWebcam_DescriptorSize_Standard_VS_Interface,
        .bDescriptorType = kLTDeviceUsbStd_DescriptorType_Interface,
        .bInterfaceNumber = 0x01,
        .bAlternateSetting = 0x00,
        .bNumEndpoints = 0,
        .bInterfaceClass = 0x0E,
        .bInterfaceSubClass = 0x02,
        .bInterfaceProtocol = 0x00,
        .iInterface = 0x00
    };

    S.configDescriptor->standardVSInterfaceSetting1 = (LTUsbInterfaceDescriptor) {
        .bLength = kLTDeviceUsbWebcam_DescriptorSize_Standard_VS_Interface,
        .bDescriptorType = kLTDeviceUsbStd_DescriptorType_Interface,
        .bInterfaceNumber = 0x01,
        .bAlternateSetting = 0x01,
        .bNumEndpoints = 1,
        .bInterfaceClass = 0x0E,
        .bInterfaceSubClass = 0x02,
        .bInterfaceProtocol = 0x00,
        .iInterface = 0x00
    };

    S.configDescriptor->endpointStandardVSISO = (LTUsbEndpointDescriptor) {
        .bLength = kLTDeviceUsbStd_DescriptorSize_Endpoint,
        .bDescriptorType = kLTDeviceUsbStd_DescriptorType_Endpoint,
        .bEndpointAddress = 0x80 | S.endpointVS,
        .bmAttributes = 0x05, /*Asynchronous + Isochronous*/
        .wMaxPacketSize = S.maxPacketSize,
        .bInterval = kbInterval
    };

    S.configDescriptor->classSpecificVSHeader = (LTDeviceUsbWebcamClassSpecificVSHeaderDescriptor) {
        .bLength = kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VS_Header,
        .bDescriptorType = kLTDeviceUsbWebcam_DescriptorType_CS_Interface,
        .bDescriptorSubType = 0x01,
        .bNumFormats = 0x01,
        .wTotalLength = sizeof(LTDeviceUsbWebcamClassSpecificVSHeaderDescriptor) + sizeof(LTDeviceUsbWebcamClassSpecificVSFormatDescriptor) + sizeof(LTDeviceUsbWebcamClassSpecificVSFrameDescriptor) + sizeof(LTDeviceUsbWebcamVSColorMatchingDescriptor),
        .bEndpointAddress = 0x80 | S.endpointVS,
        .bmInfo = 0x00,
        .bTerminalLink = 0x04,
        .bStillCaptureMethod = 0x00,
        .bTriggerSupport = 0x00,
        .bTriggerUsage = 0x00,
        .bControlSize = 0x01,
        .bmaControls = 0x00
    };

    S.configDescriptor->classSpecificVSFormat = (LTDeviceUsbWebcamClassSpecificVSFormatDescriptor) {
        .bLength = kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VS_Format,
        .bDescriptorType = kLTDeviceUsbWebcam_DescriptorType_CS_Interface,
        .bDescriptorSubType = 0x10, // VS_FORMAT_FRAME_BASED
        .bFormatIndex = 0x01,
        .bNumFrameDescriptors = 0x01,
        .guidFormat = {0x48, 0x32, 0x36, 0x34, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}, // MEDIASUBTYPE_H264 GUID
        .bBitsPerPixel = 16,
        .bDefaultFrameIndex = 0x01,
        .bAspectRatioX = 0x00,
        .bAspectRatioY = 0x00,
        .bmInterlaceFlags = 0x00,
        .bCopyProtect = 0x00,
        .bVariableSize = 1
    };

    S.configDescriptor->classSpecificVSFrame = (LTDeviceUsbWebcamClassSpecificVSFrameDescriptor) {
        .bLength = kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VS_Frame,
        .bDescriptorType = kLTDeviceUsbWebcam_DescriptorType_CS_Interface,
        .bDescriptorSubType = 0x11, // VS_FRAME_FRAME_BASED
        .bFrameIndex = 0x01,
        .bmCapabilities = 0x00, // Fixed Frame Rate
        .wWidth = S.deviceResolution->width,
        .wHeight = S.deviceResolution->height,
        .dwMinBitRate = S.streamBitrate,
        .dwMaxBitRate = S.streamBitrate,
        .dwDefaultFrameInterval = S.frameInterval,
        .bFrameIntervalType = 0x01,
        .dwBytesPerLine = 0,
        .dwFrameInterval = {S.frameInterval}
    };

    // TODO: Update values
    S.configDescriptor->colorMatchingDescriptor = (LTDeviceUsbWebcamVSColorMatchingDescriptor) {
        .bLength = kLTDeviceUsbWebcam_DescriptorSize_Color_Matching,
        .bDescriptorType = kLTDeviceUsbWebcam_DescriptorType_CS_Interface,
        .bDescriptorSubType = 0x0D,
        .bColorPrimaries = 1,
        .bTransferCharacteristics = 1,
        .bMatrixCoefficients = 4
    };

    const LTDeviceUsbWebcamVideoProbeCommitControls baseProbeCommitControls = (LTDeviceUsbWebcamVideoProbeCommitControls) {
        .bmHint = 0x0001,                               // Resolution hint
        .bFormatIndex = 0x01,                  
        .bFrameIndex = 0x01,                   
        .dwFrameInterval = S.frameInterval,         
        .wKeyFrameRate = S.framerate,                   
        .wPFrameRate = 0,                               // No P-frame rate control
        .wCompQuality = 0x00,                           // Default compression quality
        .wCompWindowSize = 0x00,                        // Default window size
        .wDelay = 0x00,                                 // No delay
        .dwMaxVideoFrameSize = 0x00200000,              // 2MB max frame size
        .dwMaxPayloadTransferSize = S.maxPacketSize,   // 1024 bytes max payload
        .dwClockFrequency = 48000000,                   
        .bmFramingInfo = 0x03,                          // FID and EOF bits
        .bPreferredVersion = 0x01,                     
        .bMinVersion = 0x01,                            
        .bMaxVersion = 0x01,                            
    };

    S.port->probeControls = baseProbeCommitControls;
    S.port->commitControls = baseProbeCommitControls;

    // TODO: Optimize values
    const LTDeviceUsbWebcamUVCXVideoConfigProbeCommitControls baseUVCXProbeCommitControls = (LTDeviceUsbWebcamUVCXVideoConfigProbeCommitControls) {
        .dwFrameInterval = S.frameInterval,
        .dwBitRate = S.streamBitrate,
        .bmHints = 1,
        .wConfigurationIndex = 0,
        .wWidth = S.deviceResolution->width,
        .wHeight = S.deviceResolution->height,
        .wSliceUnits = 0,
        .wSliceMode = 0,
        .wProfile = 0x6400,
        .wIFramePeriod = 0,
        .wEstimatedVideoDelay = 40,
        .wEstimatedMaxConfigDelay = 250, 
        .bUsageType = 0,
        .bRateControlMode = 0, 
        .bTemporalScaleMode = 0,
        .bSpatialScaleMode = 0,
        .bSNRScaleMode = 0,
        .bStreamMuxOption = 0, 
        .bStreamFormat = 0,
        .bEntropyCABAC = 1,
        .bTimestamp = 1,
        .bNumOfReorderFrames = 0,
        .bPreviewFlipped = 0,
        .bView = 0,
        .bReserved1 = 0, 
        .bReserved2 = 0,
        .bStreamID = 0x04,
        .bSpatialLayerRatio = 0, 
        .wLeakyBucketSize = 200
    };

    S.port->uvcxProbeControls = baseUVCXProbeCommitControls;
    S.port->uvcxCommitControls = baseUVCXProbeCommitControls;

    S.iDevUsbClient->SetDeviceRequestHandler(S.hDevUsbClient, &DeviceRequestHandler, NULL);

    if (!S.iDevUsbClient->ConfigureEndpoint(S.hDevUsbClient, &S.configDescriptor->endpointStandardVSISO, &EndpointVSSendReadyHandler, S.port)) {
        LTLOG_YELLOWALERT("start.vs", "VS endpoint config failed");
        return false;
    }

    if(!S.iDevUsbClient->Enable(S.hDevUsbClient, ConfigOkHandler, NULL)) {
        LTLOG_YELLOWALERT("start.enable", "Device enable failed");
        return false;
    }

    return true;
}

static void DestroyUsbDevice(void) {
    DLOG("destroy.device", "Destroying USB Device");
    // Kill video device
    StopVideoDevice();

    S.iDevUsbClient->Disable(S.hDevUsbClient);

    if (S.hDevUsbClient) {
        lt_destroyhandle(S.hDevUsbClient);
        S.hDevUsbClient = 0;
    }

    if (S.lDevUsbClient) {
        lt_closelibrary(S.lDevUsbClient);
        S.lDevUsbClient = NULL;
    }

    if (S.lDevVideo) {
        lt_closelibrary(S.lDevVideo);
    }

    if (S.pins) {
        lt_closelibrary(S.pins);    
    }

    if (S.stringDescriptors) {
        for (u32 i = 1; i < S.stringDescriptors->API->GetCount(S.stringDescriptors); ++i) {
            lt_free(S.stringDescriptors->API->Get(S.stringDescriptors, i, NULL));
        }
        lt_destroyobject(S.stringDescriptors);
    }   

    if (S.port) {
        lt_free(S.port);
        S.port = NULL;
    }

    if (S.configDescriptor) {
        lt_free(S.configDescriptor);
        S.configDescriptor = NULL;
    }
}

/* ____________________________________________
 * Object constructor and destructor
 */

static void LTDeviceUsbWebcamImpl_DestructObject(LTDeviceUsbWebcamImpl *device) {
    LT_UNUSED(device);
    DLOG("dtr", "Destructing LTDeviceUsbWebcam");
    if (device && device->port) device->port->user = NULL;
}

static bool LTDeviceUsbWebcamImpl_ConstructObject(LTDeviceUsbWebcamImpl *device) {
    DLOG("ctr", "Constructing LTDeviceUsbWebcam");
    device->port = NULL;
    return true;
}

static void Private_UsbBusAssigned(bool acquired, void * clientData) {
    LT_UNUSED(clientData);

    if (acquired) {
        InitUsbDevice();
    } else {
        DestroyUsbDevice();
    }
}

static bool UsbManager_ThreadInit(void) {
    S.usbManager = lt_createobject(LTSystemUsbBusManager);
    if (S.usbManager) {
        // Register for the USB bus and wait for it to be assigned to us.
        return S.usbManager->API->OnModeChange(kLTMailboxTransportUsb_UsbMode, Private_UsbBusAssigned, NULL);
    } else {
        // Unmanaged USB bus, acquire immediately.
        return InitUsbDevice();
    }
}

static void UsbManager_ThreadExit(void) {
    if (S.usbManager) {
        // This will call Private_ReleaseUsbBus if needed.
        S.usbManager->API->NoModeChange(kLTMailboxTransportUsb_UsbMode);
        lt_destroyobject(S.usbManager);
        S.usbManager = NULL;
    } else {
        DestroyUsbDevice();
    }

}

/* _______________________________________
 * LibInit and LibFini
 */
static bool LTDeviceUsbWebcamLibImpl_LibInit(void) {
    S = (struct Statics){0};

    if (!(S.deviceConfig = lt_openlibrary(LTDeviceConfig))) return false;
    if (!(S.deviceIdentity = lt_openlibrary(LTDeviceIdentity))) return false;
    DLOG("sys.webcam", "Initialising webcam driver");

    S.mainThread = lt_createobject(LTOThread);
    if (!S.mainThread) return false;

    S.mainThread->API->SetStackSize(S.mainThread, 2048);
    S.mainThread->API->Start(S.mainThread, "dev.webcam", UsbManager_ThreadInit, UsbManager_ThreadExit);

    return true;
}

static void LTDeviceUsbWebcamLibImpl_LibFini(void) {
    S.mainThread->API->Terminate(S.mainThread);
    S.mainThread->API->WaitUntilFinished(S.mainThread, LTTime_Infinite());
    lt_destroyobject(S.mainThread);

    lt_closelibrary(S.deviceConfig);
    lt_closelibrary(S.deviceIdentity);
}

define_LTObjectImplPublic(LTDeviceUsbWebcam, LTDeviceUsbWebcamImpl);

typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceUsbWebcamLib, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTDeviceUsbWebcamLib) LTLIBRARY_DEFINITION;
LTLIBRARY_EXPORT_INTERFACES(LTDeviceUsbWebcamLib, (LTDeviceUsbWebcamImpl));