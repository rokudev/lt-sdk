/*******************************************************************************
 * include/lt/device/usb/LTDeviceUsbCDCStd.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_DEVICE_USB_LTDEVICEUSBCDCSTD_H_
#define ROKU_LT_SOURCE_LT_DEVICE_USB_LTDEVICEUSBCDCSTD_H_

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/*
 * bcdCDC:
 */
typedef enum {
    kLTDeviceUsbCDCStd_SpecVersion_1_2 = LT_LE16(0x0110),
} LTDeviceUsbCDCStd_SpecVersion;

/*
 * bRequest, CDC class-specific:
 */
typedef enum {
    kLTDeviceUsbCDCStd_Request_SetCommFeature      = 0x02,
    kLTDeviceUsbCDCStd_Request_GetCommFeature      = 0x03,
    kLTDeviceUsbCDCStd_Request_SetLineCoding       = 0x20,
    kLTDeviceUsbCDCStd_Request_GetLineCoding       = 0x21,
    kLTDeviceUsbCDCStd_Request_SetControlLineState = 0x22,
} LTDeviceUsbCDCStd_Request;

/*
 * bInterfaceProtocol:
 */
typedef enum {
    kLTDeviceUsbCDCStd_Protocol_None           = 0x00,
    kLTDeviceUsbCDCStd_Protocol_V25TER         = 0x01,
    kLTDeviceUsbCDCStd_Protocol_VendorSpecific = 0xff,
} LTDeviceUsbCDCStd_Protocol;

/*
 * The line coding properties
 */
typedef struct __attribute__((packed)) {
    u32 dwDTERate;
    u8 bCharFormat;
    u8 bParityType;
    u8 bDataBits;
} LTDeviceUsbCDCLineCoding;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbCDCLineCoding, 7, 7);

/*
 * wValue for SET_CONTROL_LINE_STATE request:
 */
typedef enum {
    kLTDeviceUsbCDCStd_ControlLineState_DTR = 0x01,  /* Bit 0, Data Terminal Ready */
    kLTDeviceUsbCDCStd_ControlLineState_RTS = 0x02,  /* Bit 1, Request To Send */
} LTDeviceUsbCDCStd_ControlLineState;

/*
 * bLength:
 */
typedef enum {
    kLTDeviceUsbCDCStd_DescriptorSize_Header         = 5,
    kLTDeviceUsbCDCStd_DescriptorSize_CallManagement = 5,
    kLTDeviceUsbCDCStd_DescriptorSize_ACM            = 4,
    kLTDeviceUsbCDCStd_DescriptorSize_Union          = 5,
} LTDeviceUsbCDCStd_DescriptorSize;

/*
 * bDescriptorType:
 */
typedef enum {
    kLTDeviceUsbCDCStd_DescriptorType_CSInterface = 0x24,
} LTDeviceUsbCDCStd_DescriptorType;

/*
 * bDescriptorSubtype:
 */
typedef enum {
    kLTDeviceUsbCDCStd_DescriptorSubtype_Header         = 0x00,
    kLTDeviceUsbCDCStd_DescriptorSubtype_CallManagement = 0x01,
    kLTDeviceUsbCDCStd_DescriptorSubtype_ACM            = 0x02,
    kLTDeviceUsbCDCStd_DescriptorSubtype_Union          = 0x06,
} LTDeviceUsbCDCStd_DescriptorSubtype;

/*
 * bmCapabilities:
 */
typedef enum {
    kLTDeviceUsbCDCStd_Capabilities_None   = 0x00,
    kLTDeviceUsbCDCStd_Capabilities_Line   = 0x02,
    kLTDeviceUsbCDCStd_Capabilities_Brk    = 0x04,
    kLTDeviceUsbCDCStd_Capabilities_Notify = 0x08,
} LTDeviceUsbCDCStd_Capabilities;

/*
 * kLTDeviceUsbCDCStd_DescriptorSubtype_Header:
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bDescriptorSubtype;
    u16 bcdCDC; /* spec release number */
} LTDeviceUsbCDCHeaderDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbCDCHeaderDescriptor, kLTDeviceUsbCDCStd_DescriptorSize_Header, kLTDeviceUsbCDCStd_DescriptorSize_Header);

/*
 * kLTDeviceUsbCDCStd_DescriptorSubtype_CallManagement:
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bDescriptorSubtype;
    u8 bmCapabilities;
    u8 bDataInterface;
} LTDeviceUsbCDCCallManagementDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbCDCCallManagementDescriptor, kLTDeviceUsbCDCStd_DescriptorSize_CallManagement, kLTDeviceUsbCDCStd_DescriptorSize_CallManagement);

/*
 * kLTDeviceUsbCDCStd_DescriptorSubtype_ACM:
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bDescriptorSubtype;
    u8 bmCapabilities;
} LTDeviceUsbCDCACMDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbCDCACMDescriptor, kLTDeviceUsbCDCStd_DescriptorSize_ACM, kLTDeviceUsbCDCStd_DescriptorSize_ACM);

/*
 * kLTDeviceUsbCDCStd_DescriptorSubtype_Union:
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;
    u8 bDescriptorSubtype;
    u8 bMasterInterface;
    u8 bSlaveInterface0;
} LTDeviceUsbCDCUnionDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceUsbCDCUnionDescriptor, kLTDeviceUsbCDCStd_DescriptorSize_Union, kLTDeviceUsbCDCStd_DescriptorSize_Union);

LT_EXTERN_C_END
#endif /* #define ROKU_LT_SOURCE_LT_DEVICE_USB_LTDEVICEUSBCDCSTD_H_ */
