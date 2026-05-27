/*******************************************************************************
 * source/lt/device/usb/LTDeviceUsbWebcam.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_DEVICE_USB_LTDEVICEUSBWEBCAMSTD_H_
#define ROKU_LT_SOURCE_LT_DEVICE_USB_LTDEVICEUSBWEBCAMSTD_H_

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/*
 * bcdUVC:
 */
typedef enum {
    kLTDeviceUsbWebcamStd_UVC_Spec_Version_1_1 = LT_LE16(0x0110)
} LTDeviceUsbWebcamStd_SpecVersion;

/*
 * bRequest (UVC 1.1)
 */
typedef enum {
    kLTDeviceUsbWebcamStd_Request_Undefined          = 0x00,
    kLTDeviceUsbWebcamStd_Request_Set_Cur            = 0x01,
    kLTDeviceUsbWebcamStd_Request_Get_Cur            = 0x81,
    kLTDeviceUsbWebcamStd_Request_Get_Min            = 0x82,
    kLTDeviceUsbWebcamStd_Request_Get_Max            = 0x83,
    kLTDeviceUsbWebcamStd_Request_Get_Res            = 0x84,
    kLTDeviceUsbWebcamStd_Request_Get_Len            = 0x85,
    kLTDeviceUsbWebcamStd_Request_Get_Info           = 0x86,
    kLTDeviceUsbWebcamStd_Request_Get_Def            = 0x87
} LTDeviceUsbWebcamStd_Request;

/*
 * bLength:
 */
typedef enum {
    kLTDeviceUsbWebcan_DescriptorSize_Class_Specific_Interrupt_Endpoint = 0x05,
    kLTDeviceUsbWebcam_DescriptorSize_Color_Matching = 0x06,
    kLTDeviceUsbWebcam_DescriptorSize_Interface_Association = 0x08,
    kLTDeviceUsbWebcam_DescriptorSize_Configuration = 0x09,
    kLTDeviceUsbWebcam_DescriptorSize_Output_Terminal = 0x09,
    kLTDeviceUsbWebcam_DescriptorSize_Standard_VC_Interface = 0x09,
    kLTDeviceUsbWebcam_DescriptorSize_Standard_VS_Interface = 0x09,
    kLTDeviceUsbWebcam_DescriptorSize_Processing_Unit = 0x0B,
    kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VC_Interface = 0x0D,
    kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VS_Header = 0x0E,
    kLTDeviceUsbWebcam_DescriptorSize_Input_Terminal = 0x11,
    kLTDeviceUsbWebcam_DescriptorSize_Device = 0x12,
    kLTDeviceUsbWebcam_DescriptorSize_Extension_Unit = 0x1A,
    kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VS_Format = 0x1C,
    kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VS_Frame = 0x1E,
    kLTDeviceUsbWebcam_DescriptorSize_Video_Format = 0x1B,
    kLTDeviceUsbWebcam_DescriptorSize_Video_Frame = 0x1E,
    kLTDeviceUsbWebcam_DescriptorSize_Payload_Header = 0x0C,
    kLTDeviceUsbWebcam_DescriptorSize_Payload_Video_Frame = 0x30,
    kLTDeviceUsbWebcam_DescriptorSize_Payload_Video_Format = 0x34
} LTDeviceUsbWebcamStd_DescriptorSize;

/*
 * wLength
 */
typedef enum {
    kLTDeviceUsbWebcam_wLength_Probe_Commit_Controls = 0x22,
    kLTDeviceUsbWebcam_wLength_UVCX_Probe_Commit_Control = 0x2E
} LTDeviceUsbWebcamStd_wLength;

/*
 * bDescriptorType
 */
typedef enum {
    kLTDeviceUsbWebcam_DescriptorType_Device = 0x01,
    kLTDeviceUsbWebcam_DescriptorType_Configuration = 0x02,
    kLTDeviceUsbWebcam_DescriptorType_String = 0x03,
    kLTDeviceUsbWebcam_DescriptorType_Interface = 0x04,
    kLTDeviceUsbWebcam_DescriptorType_CS_Interrupt_Endpoint = 0x05,
    kLTDeviceUsbWebcam_DescriptorType_Interface_Association = 0x0B,
    kLTDeviceUsbWebcam_DescriptorType_CS_Interface = 0x24,
} LTDeviceUsbWebcamStd_DescriptorType;

/*
 * CS
 */
typedef enum {
    kLTDeviceUsbWebcam_ControlSelector_VS_Probe_Control = 0x0100,
    kLTDeviceUsbWebcam_ControlSelector_VS_Commit_Control = 0x0200,
} LTDeviceUsbWebcamStd_ControlSelector;

/*
 * UVCX CS
 */
typedef enum {
	kLTDeviceUsbWebcam_UVCXControlSelector_Video_Config_Probe = 0x01,
	kLTDeviceUsbWebcam_UVCXControlSelector_Video_Config_Commit = 0x02
} LTDeviceUsbWebcamStd_UVCXControlSelector;

/*
 * LTDeviceUsbWebcamConfigurationDescriptor:
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u16 wTotalLength;
    u8 bNumInterfaces;
    u8 bConfigurationValue;
    u8 iConfiguration;
    u8 bmAttributes;
    u8 bMaxPower;

} LTDeviceUsbWebcamConfigurationDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamConfigurationDescriptor, kLTDeviceUsbWebcam_DescriptorSize_Configuration, kLTDeviceUsbWebcam_DescriptorSize_Configuration);

/*
 * LTDeviceUsbWebcamInterfaceAssociationDescriptor
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bFirstInterface;
    u8 bInterfaceCount;
    u8 bFunctionClass;
    u8 bFunctionSubClass;
    u8 bFunctionProtocol;
    u8 iFunction;
} LTDeviceUsbWebcamInterfaceAssociationDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamInterfaceAssociationDescriptor, kLTDeviceUsbWebcam_DescriptorSize_Interface_Association, kLTDeviceUsbWebcam_DescriptorSize_Interface_Association);

/*
 * LTDeviceUsbWebcamStandardVCInterfaceDescriptor
 */

typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bInterfaceNumber;
    u8 bAlternateSetting;
    u8 bNumEndpoints;
    u8 bInterfaceClass;
    u8 bInterfaceSubClass;
    u8 bInterfaceProtocol;
    u8 iInterface;
} LTDeviceUsbWebcamStandardVCInterfaceDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamStandardVCInterfaceDescriptor, kLTDeviceUsbWebcam_DescriptorSize_Standard_VC_Interface, kLTDeviceUsbWebcam_DescriptorSize_Standard_VC_Interface);

/*
 * LTDeviceUsbWebcamClassSpecificVCInterfaceDescriptor
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bDescriptorSubType;
    u16 bcdUVC;
    u16 wTotalLength;
    u32 dwClockFrequency;
    u8 bInCollection;
    u8 baInterfaceNr;
} LTDeviceUsbWebcamClassSpecificVCInterfaceDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamClassSpecificVCInterfaceDescriptor, kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VC_Interface, kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VC_Interface);

/*
 * LTDeviceUsbWebcamClassSpecificInterruptEndpointDescriptor 
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bDescriptorSubType;
    u16 wMaxTransferSize;
} LTDeviceUsbWebcamClassSpecificInterruptEndpointDescriptor;

/*
 * LTDeviceUsbWebcamInputTerminalDescriptor
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bDescriptorSubType;
    u8 bTerminalID;
    u16 wTerminalType;
    u8 bAssocTerminal;
    u8 iTerminal;
    u16 wObjectiveFocalLengthMin;
    u16 wObjectiveFocalLengthMax;
    u16 wOcularFocalLength;
    u8 bControlSize;
    u16 bmControls;
} LTDeviceUsbWebcamInputTerminalDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamInputTerminalDescriptor, kLTDeviceUsbWebcam_DescriptorSize_Input_Terminal, kLTDeviceUsbWebcam_DescriptorSize_Input_Terminal);

/*
 * LTDeviceUsbWebcamProcessingUnitDescriptor
 */
typedef struct __attribute__((packed)) {
    u8  bLength;
    u8  bDescriptorType;     
    u8  bDescriptorSubType;   
    u8  bUnitID;
    u8  bSourceID;
    u16 wMaxMultiplier;
    u8  bControlSize;
    u8  bmControls;        
    u8  iProcessing;
    u8  bmVideoStandards;
} LTDeviceUsbWebcamProcessingUnitDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamProcessingUnitDescriptor, kLTDeviceUsbWebcam_DescriptorSize_Processing_Unit, kLTDeviceUsbWebcam_DescriptorSize_Processing_Unit);

/*
 * LTDeviceUsbWebcamExtensionUnitDescriptor
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bDescriptorSubtype;
    u8 bUnitID;
    u8 guidExtensionCode[16];
    u8 bNumControls;
    u8 bNrInPins;
    u8 baSourceID1;
    u8 bControlSize;
    u8 bmControls;
    u8 iExtension;
} LTDeviceUsbWebcamExtensionUnitDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamExtensionUnitDescriptor, kLTDeviceUsbWebcam_DescriptorSize_Extension_Unit, kLTDeviceUsbWebcam_DescriptorSize_Extension_Unit);

/*
 * LTDeviceUsbWebcamOutputTerminalDescriptor
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bDescriptorSubtype;
    u8 bTerminalID;
    u16 wTerminalType;
    u8 bAssocTerminal;
    u8 bSourceID;
    u8 iTerminal;
} LTDeviceUsbWebcamOutputTerminalDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamOutputTerminalDescriptor, kLTDeviceUsbWebcam_DescriptorSize_Output_Terminal, kLTDeviceUsbWebcam_DescriptorSize_Output_Terminal);

/*
 * LTDeviceUsbWebcamStandardVSInterfaceDescriptor
 */
 typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bInterfaceNumber;
    u8 bAlternateSetting;
    u8 bNumEndpoints;
    u8 bInterfaceClass;
    u8 bInterfaceSubClass;
    u8 bInterfaceProtocol;
    u8 iInterface;
 } LTDeviceUsbWebcamStandardVSInterfaceDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamStandardVSInterfaceDescriptor, kLTDeviceUsbWebcam_DescriptorSize_Standard_VS_Interface, kLTDeviceUsbWebcam_DescriptorSize_Standard_VS_Interface);

/*
 * LTDeviceUsbWebcamClassSpecificVSHeaderDescriptor
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bDescriptorSubType;
    u8 bNumFormats;
    u16 wTotalLength;
    u8 bEndpointAddress;
    u8 bmInfo;
    u8 bTerminalLink;
    u8 bStillCaptureMethod;
    u8 bTriggerSupport;
    u8 bTriggerUsage;
    u8 bControlSize;
    u8 bmaControls;
} LTDeviceUsbWebcamClassSpecificVSHeaderDescriptor;


LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamClassSpecificVSHeaderDescriptor, kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VS_Header, kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VS_Header);

/*
 * LTDeviceUsbWebcamClassSpecificVSFormatDescriptor
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bDescriptorSubType;
    u8 bFormatIndex;
    u8 bNumFrameDescriptors;
    u8 guidFormat[16];
    u8 bBitsPerPixel;
    u8 bDefaultFrameIndex;
    u8 bAspectRatioX;
    u8 bAspectRatioY;
    u8 bmInterlaceFlags;
    u8 bCopyProtect;
    u8 bVariableSize;
} LTDeviceUsbWebcamClassSpecificVSFormatDescriptor;


LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamClassSpecificVSFormatDescriptor, kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VS_Format, kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VS_Format);

/*
 * LTDeviceUsbWebcamClassSpecificVSFrameDescriptor
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bDescriptorSubType;
    u8 bFrameIndex;
    u8 bmCapabilities;
    u16 wWidth;
    u16 wHeight;
    u32 dwMinBitRate;
    u32 dwMaxBitRate;
    u32 dwDefaultFrameInterval;
    u8 bFrameIntervalType;
    u32 dwBytesPerLine;
    u32 dwFrameInterval[1];
} LTDeviceUsbWebcamClassSpecificVSFrameDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamClassSpecificVSFrameDescriptor, kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VS_Frame, kLTDeviceUsbWebcam_DescriptorSize_Class_Specific_VS_Frame);

/*
 * LTDeviceUsbWebcamVSColorMatchingDescriptor
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bDescriptorSubType;
    u8 bColorPrimaries;
    u8 bTransferCharacteristics;
    u8 bMatrixCoefficients;
} LTDeviceUsbWebcamVSColorMatchingDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamVSColorMatchingDescriptor, kLTDeviceUsbWebcam_DescriptorSize_Color_Matching, kLTDeviceUsbWebcam_DescriptorSize_Color_Matching);

/*
 * LTDeviceUsbWebcamPayloadHeader
 */
typedef struct __attribute__((packed)) {
    u8 bHeaderLength;
    u8 bmHeaderInfo;
    u32 dwPresentationTime;
    u16 scrSourceClock[3];   
} LTDeviceUsbWebcamPayloadHeader;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamPayloadHeader, kLTDeviceUsbWebcam_DescriptorSize_Payload_Header, kLTDeviceUsbWebcam_DescriptorSize_Payload_Header);

/*
 * LTDeviceUsbWebcamVideoProbeCommitControls
 */
typedef struct __attribute__((packed)) {
    u16 bmHint;
    u8  bFormatIndex;
    u8  bFrameIndex;
    u32 dwFrameInterval;
    u16 wKeyFrameRate;
    u16 wPFrameRate;
    u16 wCompQuality;
    u16 wCompWindowSize;
    u16 wDelay;
    u32 dwMaxVideoFrameSize;
    u32 dwMaxPayloadTransferSize;
    u32 dwClockFrequency;
    u8  bmFramingInfo;
    u8  bPreferredVersion;
    u8  bMinVersion;
    u8  bMaxVersion;
} LTDeviceUsbWebcamVideoProbeCommitControls;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamVideoProbeCommitControls, kLTDeviceUsbWebcam_wLength_Probe_Commit_Controls, kLTDeviceUsbWebcam_wLength_Probe_Commit_Controls);

/*
 * LTDeviceUsbWebcamUVCXVideoConfigProbeCommitControls
 */
typedef struct __attribute__((packed)) {
	u32	dwFrameInterval;
	u32	dwBitRate;
	u16	bmHints;
	u16	wConfigurationIndex;
	u16	wWidth;
	u16	wHeight;
	u16	wSliceUnits;
	u16	wSliceMode;
	u16	wProfile;
	u16	wIFramePeriod;
	u16	wEstimatedVideoDelay;
	u16	wEstimatedMaxConfigDelay;
	u8	bUsageType;
	u8	bRateControlMode;
	u8	bTemporalScaleMode;
	u8	bSpatialScaleMode;
	u8	bSNRScaleMode;
	u8	bStreamMuxOption;
	u8	bStreamFormat;
	u8	bEntropyCABAC;
	u8	bTimestamp;
	u8	bNumOfReorderFrames;
	u8	bPreviewFlipped;
	u8	bView;
	u8	bReserved1;
	u8	bReserved2;
	u8	bStreamID;
	u8	bSpatialLayerRatio;
	u16	wLeakyBucketSize;
} LTDeviceUsbWebcamUVCXVideoConfigProbeCommitControls;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbWebcamUVCXVideoConfigProbeCommitControls, kLTDeviceUsbWebcam_wLength_UVCX_Probe_Commit_Control, kLTDeviceUsbWebcam_wLength_UVCX_Probe_Commit_Control);

LT_EXTERN_C_END
#endif