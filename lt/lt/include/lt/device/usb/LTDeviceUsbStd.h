/*******************************************************************************
 * lt/include/lt/device/usb/LTDeviceUsbStd.h
 *
 * Struct and enum definitions for use across USB client, host, and device class implementations.
 *
 * NOTE: The prefixed naming convention (b for byte; w for word; bm for bitmap; i for index; id for identifier) is used
 * for parity with the USB spec, which uses that convention to refer to those structures and fields. This naming
 * convention should NOT be used in other non-USB related LT variables and files.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_USB_LTDEVICEUSBSTD_H
#define LT_INCLUDE_LT_DEVICE_USB_LTDEVICEUSBSTD_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

/*
 * bLength:
 */
typedef enum {
    kLTDeviceUsbStd_DescriptorSize_Device               = 18,
    kLTDeviceUsbStd_DescriptorSize_Config               = 9,
    kLTDeviceUsbStd_DescriptorSize_String               = 2,
    kLTDeviceUsbStd_DescriptorSize_Interface            = 9,
    kLTDeviceUsbStd_DescriptorSize_InterfaceAssociation = 8,
    kLTDeviceUsbStd_DescriptorSize_Endpoint             = 7,
    kLTDeviceUsbStd_DescriptorSize_DeviceQualifier      = 10,
    kLTDeviceUsbStd_DescriptorSize_OtherSpeedConfig     = 9,
} LTDeviceUsbStd_DescriptorSize;

/*
 * bDescriptorType:
 */
typedef enum {
    kLTDeviceUsbStd_DescriptorType_Device               = 0x01,
    kLTDeviceUsbStd_DescriptorType_Config               = 0x02,
    kLTDeviceUsbStd_DescriptorType_String               = 0x03,
    kLTDeviceUsbStd_DescriptorType_Interface            = 0x04,
    kLTDeviceUsbStd_DescriptorType_Endpoint             = 0x05,
    kLTDeviceUsbStd_DescriptorType_DeviceQualifier      = 0x06,
    kLTDeviceUsbStd_DescriptorType_OtherSpeedConfig     = 0x07,
    kLTDeviceUsbStd_DescriptorType_InterfacePower       = 0x08,
    kLTDeviceUsbStd_DescriptorType_OTG                  = 0x09,
    kLTDeviceUsbStd_DescriptorType_Debug                = 0x0a,
    kLTDeviceUsbStd_DescriptorType_InterfaceAssociation = 0x0b,
    kLTDeviceUsbStd_DescriptorType_Class_HID_HID        = 0x21,
    kLTDeviceUsbStd_DescriptorType_Class_HID_Report     = 0x22,
    kLTDeviceUsbStd_DescriptorType_Class_HID_Physical   = 0x23,
} LTDeviceUsbStd_DescriptorType;

/*
 * bcdUSB:
 */
typedef enum {
    kLTDeviceUsbStd_SpecVersion_1_1    = LT_LE16(0x0110),
    kLTDeviceUsbStd_SpecVersion_2_0    = LT_LE16(0x0200),
} LTDeviceUsbStd_SpecVersion;

/*
 * bDeviceClass / bInterfaceClass:
 */
typedef enum {
    kLTDeviceUsbStd_Class_None          = 0x00,
    kLTDeviceUsbStd_Class_Comm          = 0x02,
    kLTDeviceUsbStd_Class_CDCData       = 0x0a,
    kLTDeviceUsbStd_Class_Video         = 0x0e,
    kLTDeviceUsbStd_Class_MultiFunction = 0xef,
    kLTDeviceUsbStd_Class_Vendor        = 0xff,
} LTDeviceUsbStd_Class;

/*
 * bDeviceSubClass / bInterfaceSubClass:
 */
typedef enum {
    kLTDeviceUsbStd_SubClass_None                       = 0x00,
    kLTDeviceUsbStd_SubClass_Video                      = 0x01,
    kLTDeviceUsbStd_SubClass_CDC_ACM                    = 0x02,
    kLTDeviceUsbStd_SubClass_MultiFunction              = 0x02,
    kLTDeviceUsbStd_SubClass_Video_Interface_Collection = 0x03,
    kLTDeviceUsbStd_SubClass_Vendor                     = 0xff,
} LTDeviceUsbStd_SubClass;

/*
 * bDeviceProtocol:
 */
typedef enum {
    kLTDeviceUsbStd_Protocol_None          = 0x00,
    kLTDeviceUsbStd_Protocol_MultiFunction = 0x01,
    kLTDeviceUsbStd_Protocol_Vendor        = 0xff,
} LTDeviceUsbStd_Protocol;

/*
 * bmAttributes:
 */
typedef enum {
    kLTDeviceUsbStd_Attribute_Config_One             = 0x80,
    kLTDeviceUsbStd_Attribute_Config_SelfPower       = 0x40,
    kLTDeviceUsbStd_Attribute_Config_BusPower        = 0x00,
    kLTDeviceUsbStd_Attribute_Config_Wakeup          = 0x20,
    kLTDeviceUsbStd_Attribute_Config_Battery         = 0x10,
    kLTDeviceUsbStd_Attribute_EPTransfer_Control     = 0x00,
    kLTDeviceUsbStd_Attribute_EPTransfer_Isochronous = 0x01,
    kLTDeviceUsbStd_Attribute_EPTransfer_Bulk        = 0x02,
    kLTDeviceUsbStd_Attribute_EPTransfer_Interrupt   = 0x03,
    kLTDeviceUsbStd_Attribute_EPTransfer_Mask        = 0x03,
} LTDeviceUsbStd_Attribute;

/*
 * bEndpointAddress:
 * Bits 0..3 represent the endpoint number
 * Bit 7 represents the endpoint direction (0 = Out, 1 = In)
 *
 * Example usage: An endpoint address for endpoint 2, in the in direction
 *  LTDeviceUsbStd_EndpointAddress myEndpointAddress = 2 | kLTDeviceUsbStd_EndpointAddress_Dir_In;
 */
typedef_LTENUM_SIZED(LTDeviceUsbStd_EndpointAddress, u8) {
    kLTDeviceUsbStd_EndpointAddress_Dir_In      = 0x80,
    kLTDeviceUsbStd_EndpointAddress_Dir_Out     = 0x00,
    kLTDeviceUsbStd_EndpointAddress_Dir_Mask    = 0x80,
    kLTDeviceUsbStd_EndpointAddress_Number_Mask = 0x0f,
};

/*
 * Common descriptor header shared by all USB descriptors.
 * Useful for unpacking a descriptor before knowing its bDescriptorType.
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;

    u8 data[];
} LTUsbUnknownDescriptor;

/*
 * kLTDeviceUsbStd_DescriptorType_Device:
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;

    u16 bcdUSB;
    u8 bDeviceClass;
    u8 bDeviceSubClass;
    u8 bDeviceProtocol;
    u8 bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8 iManufacturer;
    u8 iProduct;
    u8 iSerialNumber;
    u8 bNumConfigurations;
} LTUsbDeviceDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTUsbDeviceDescriptor, kLTDeviceUsbStd_DescriptorSize_Device, kLTDeviceUsbStd_DescriptorSize_Device);

/*
 * kLTDeviceUsbStd_DescriptorType_Config:
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

    u8 interfaceData[];
} LTUsbConfigDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTUsbConfigDescriptor, kLTDeviceUsbStd_DescriptorSize_Config, kLTDeviceUsbStd_DescriptorSize_Config);

/*
 * kLTDeviceUsbStd_DescriptorType_String:
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;

    u16 wData[];
} LTUsbStringDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTUsbStringDescriptor, kLTDeviceUsbStd_DescriptorSize_String, kLTDeviceUsbStd_DescriptorSize_String);

/*
 * kLTDeviceUsbStd_DescriptorType_InterfaceAssociation:
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
} LTUsbInterfaceAssociationDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTUsbInterfaceAssociationDescriptor, kLTDeviceUsbStd_DescriptorSize_InterfaceAssociation, kLTDeviceUsbStd_DescriptorSize_InterfaceAssociation);

/*
 * kLTDeviceUsbStd_DescriptorType_Interface:
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
} LTUsbInterfaceDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTUsbInterfaceDescriptor, kLTDeviceUsbStd_DescriptorSize_Interface, kLTDeviceUsbStd_DescriptorSize_Interface);

/*
 * kLTDeviceUsbStd_DescriptorType_Endpoint:
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;

    u8 bEndpointAddress;
    u8 bmAttributes;
    u16 wMaxPacketSize;
    u8 bInterval;
} LTUsbEndpointDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTUsbEndpointDescriptor, kLTDeviceUsbStd_DescriptorSize_Endpoint, kLTDeviceUsbStd_DescriptorSize_Endpoint);

/*
 * kLTDeviceUsbStd_DescriptorType_DeviceQualifier:
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;

    u16 bcdUSB;
    u8 bDeviceClass;
    u8 bDeviceSubClass;
    u8 bDeviceProtocol;
    u8 bMaxPacketSize0;
    u8 bNumConfigurations;
    u8 bReserved;
} LTUsbDeviceQualifierDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTUsbDeviceQualifierDescriptor, kLTDeviceUsbStd_DescriptorSize_DeviceQualifier, kLTDeviceUsbStd_DescriptorSize_DeviceQualifier);

/*
 * kLTDeviceUsbStd_DescriptorType_OtherSpeedConfig:
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
} LTUsbOtherSpeedConfigDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTUsbOtherSpeedConfigDescriptor, kLTDeviceUsbStd_DescriptorSize_OtherSpeedConfig, kLTDeviceUsbStd_DescriptorSize_OtherSpeedConfig);

/*
 * kLTDeviceUsbStd_DescriptorType_Class_*:
 */
typedef struct __attribute__((packed)) {
    u8 bLength;
    u8 bDescriptorType;

    u8 classData[];
} LTDeviceUsbStd_ClassDescriptor;

/*
 * bmRequestType:
 */
typedef enum {
    // Bits 4..0
    kLTDeviceUsbStd_RequestType_Recipient_Device    = 0x00,
    kLTDeviceUsbStd_RequestType_Recipient_Interface = 0x01,
    kLTDeviceUsbStd_RequestType_Recipient_Endpoint  = 0x02,
    kLTDeviceUsbStd_RequestType_Recipient_Other     = 0x03,
    kLTDeviceUsbStd_RequestType_Recipient_Mask      = 0x1f,
    // Bits 6..5
    kLTDeviceUsbStd_RequestType_Type_Standard       = 0x00,
    kLTDeviceUsbStd_RequestType_Type_Class          = 0x20,
    kLTDeviceUsbStd_RequestType_Type_Vendor         = 0x40,
    kLTDeviceUsbStd_RequestType_Type_Mask           = 0x60,
    // Bit 7
    kLTDeviceUsbStd_RequestType_Dir_HostToDevice    = 0x00,
    kLTDeviceUsbStd_RequestType_Dir_DeviceToHost    = 0x80,
    kLTDeviceUsbStd_RequestType_Dir_Mask            = 0x80,
} LTDeviceUsbStd_RequestType;

/*
 * bRequest:
 */
typedef enum {
    kLTDeviceUsbStd_Request_GetStatus     = 0,
    kLTDeviceUsbStd_Request_ClearFeature  = 1,
    kLTDeviceUsbStd_Request_SetFeature    = 3,
    kLTDeviceUsbStd_Request_SetAddress    = 5,
    kLTDeviceUsbStd_Request_GetDescriptor = 6,
    kLTDeviceUsbStd_Request_SetDescriptor = 7,
    kLTDeviceUsbStd_Request_GetConfig     = 8,
    kLTDeviceUsbStd_Request_SetConfig     = 9,
    kLTDeviceUsbStd_Request_GetInterface  = 10,
    kLTDeviceUsbStd_Request_SetInterface  = 11,
    kLTDeviceUsbStd_Request_SynchFrame    = 12,
} LTDeviceUsbStd_Request;

/*
 * wValue:
 */
typedef enum {
    kLTDeviceUsbStd_FeatureSelector_EndpointHalt       = 0,
    kLTDeviceUsbStd_FeatureSelector_DeviceRemoteWakeup = 1,
    kLTDeviceUsbStd_FeatureSelector_TestMode           = 2,
} LTDeviceUsbStd_FeatureSelector;

/*
 * wIndex:
 */
typedef enum {
    kLTDeviceUsbStd_Index_Endpoint_Mask    = 0x007f,
    kLTDeviceUsbStd_Index_Test_J           = 0x0100,
    kLTDeviceUsbStd_Index_Test_K           = 0x0200,
    kLTDeviceUsbStd_Index_Test_SE0_NAK     = 0x0300,
    kLTDeviceUsbStd_Index_Test_Packet      = 0x0400,
    kLTDeviceUsbStd_Index_Test_ForceEnable = 0x0500,
    kLTDeviceUsbStd_Index_Test_Mask        = 0xff00,
} LTDeviceUsbStd_Index;

enum {
    kUsbSetupPacketSize = 8,
};

/*
 * 8-byte "Setup packet" used in control transfers
 */
typedef struct __attribute__((packed)) {
    u8 bmRequestType;
    u8 bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
} LTUsbDeviceRequest;

LT_STATIC_ASSERT_SIZE_32_64(LTUsbDeviceRequest, kUsbSetupPacketSize, kUsbSetupPacketSize);

/*
 * Control Transfer Stage
 */
typedef_LTENUM_SIZED(LTDeviceUsbStd_ControlStage, u8){
    kLTDeviceUsbStd_ControlStage_Idle = 0,
    kLTDeviceUsbStd_ControlStage_Setup,
    kLTDeviceUsbStd_ControlStage_DataIn,
    kLTDeviceUsbStd_ControlStage_DataOut,
    kLTDeviceUsbStd_ControlStage_Status,
};

LT_EXTERN_C_END
#endif  // LT_INCLUDE_LT_DEVICE_USB_LTDEVICEUSBSTD_H
