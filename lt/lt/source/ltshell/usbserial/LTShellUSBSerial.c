/*******************************************************************************
 * lt/source/ltshell/usbserial/LTShellUSBSerial.c
 *
 * USB Serial Shell Application
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/usbserial/LTDeviceUSBSerial.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/system/fs/LTFile.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTShellUSBSerial, (LTUtilityByteOps));

enum {
    kDataBufferSize = 64
};
#define DEFAULT_USB0_PORT "/dev/ttyUSB0"

DEFINE_LTLOG_SECTION("ltshell.usbserial");

/** Standard LT Interfaces ****************************************************/

static LTSystemShell     *s_Shell  = NULL;
static ILTShell          *s_IShell = NULL;
static LTDeviceUSBSerial *s_IUSB   = NULL;

/** Shell Variables ***********************************************************/

static const char *s_Port;
static s32 s_PortFd = -1;

/*******************************************************************************
 * Performs hex decoding
 ******************************************************************************/

static bool LTShellUSBSerial_FillWritePayload(LTShell hShell, int argc, const char *argv[], u8 *writePayload) {
    u8                payloadSize = argc - 1;
    s32               writeEle    = 0;
    LTUtilityByteOps *byteOps     = LT_GetLTUtilityByteOps();
    for (int curArg = 1; curArg <= payloadSize; curArg++) {
        u32 hexStrLen = lt_strlen(argv[curArg]);
        if (hexStrLen) {
            if (hexStrLen % 2 != 0) {
                s_IShell->Print(hShell, "Hex string must have an even number of characters.\n");
                return false;
            }
            writeEle = curArg - 1;
            byteOps->HexDecode(argv[curArg], hexStrLen, &writePayload[writeEle], payloadSize);
        }
    }
    return true;
}

/** Command Functions *********************************************************/

static int LTShellUSBSerial_Open(LTShell hShell, int argc, const char *argv[]) {
    if (argc == 2) {
        s_Port = argv[1];
    } else {
        s_Port = DEFAULT_USB0_PORT;
    }
    if (!s_IUSB->API->OpenPort(s_IUSB, s_Port, &s_PortFd)) {
        s_IShell->Print(hShell, "Error opening %s usb device\n", s_Port);
        return -1;
    }
    if (!s_IUSB->API->ConfigurePort(s_IUSB, &s_PortFd)) {
        s_IShell->Print(hShell, "Error configuring usb serial device\n");
        return -1;
    }
    s_IShell->Print(hShell, "serial port %s opened\n", s_Port);
    return 0;
}

static int LTShellUSBSerial_Close(LTShell hShell, int argc, const char *argv[]) {
    if (argc == 2) {
        s_Port =    argv[1];
    } else {
        s_Port = DEFAULT_USB0_PORT;
    }
    if (s_PortFd != -1) {
        if (!s_IUSB->API->ClosePort(s_IUSB, &s_PortFd)) {
            s_IShell->Print(hShell, "Failed to close the USB device\n");
            return -1;
        }
        s_IShell->Print(hShell, "%s port closed!\n", s_Port);
    }
    return 0;
}

static int LTShellUSBSerial_Write(LTShell hShell, int argc, const char *argv[]) {
    u8 payloadSize                   = argc - 1;
    u8 writePayload[kDataBufferSize] = {};
    if (argc < 2) {
        s_IShell->Print(hShell, "Usage - write <data>\n");
        return -1;
    }
    if (s_PortFd < 0) {
        s_IShell->Print(hShell, "USB serial port is uninitialised\n");
        return -1;
    }
    if (!LTShellUSBSerial_FillWritePayload(hShell, argc, argv, writePayload)) {
        s_IShell->Print(hShell, "Failed to Write\n");
        return -1;
    }
    if (!s_IUSB->API->WriteBytes(s_IUSB, &s_PortFd, writePayload, payloadSize)) {
        s_IShell->Print(hShell, "Failed to Write\n");
        return -1;
    }
    return 0;
}

static int LTShellUSBSerial_Read(LTShell hShell, int argc, const char *argv[]) {
    u8  readPayload[kDataBufferSize] = {};
    int readLen                      = 0;
    if (argc != 2) {
        s_IShell->Print(hShell, "Usage - read <number Of Bytes>\n");
        return -1;
    }
    if (s_PortFd != -1) {
        readLen = lt_strtou32(argv[1], NULL, 0);
        if (!s_IUSB->API->ReadBytes(s_IUSB, &s_PortFd, readPayload, readLen)) {
            s_IShell->Print(hShell, "Failed to read\n");
            return -1;
        }
        for (int readByte = 0; readByte < readLen; readByte++) {
            s_IShell->Print(hShell, "0x%02x\t", readPayload[readByte]);
        }
        s_IShell->Print(hShell, "\n");
    } else {
        s_IShell->Print(hShell, "USB serial port is uninitialised\n");
    }
    return 0;
}

static int LTShellUSBSerial_Settings(LTShell hShell, int argc, const char *argv[]) {
    if (argc == 5) {
        s_IShell->Print(hShell,
                        "port is %s, baudrate is %s, Bits is %s, stopbits is %s\n",
                        argv[1],
                        argv[2],
                        argv[3],
                        argv[4]);
    }
    LTLOG_YELLOWALERT("settings", "LTShellUSBSerial_settings");
    // TODO: Need to pass to USB Device API
    return 0;
}

/*******************************************************************************
 * shell command table
 ******************************************************************************/

static const LTSystemShell_CommandDesc USBSerialCommands[] = {
    { "open",     LTShellUSBSerial_Open,     "open /dev/ttyUSB*", NULL },
    { "close",    LTShellUSBSerial_Close,    "close port",        NULL },
    { "read",     LTShellUSBSerial_Read,     "read a port",       NULL },
    { "write",    LTShellUSBSerial_Write,    "write to a port",   NULL },
    { "settings", LTShellUSBSerial_Settings, "settings",          NULL },
    { }
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTShellUSBSerialImpl_LibFini(void) {
    if (s_Shell) {
        s_Shell->UnregisterCommands(USBSerialCommands);
        lt_closelibrary(s_Shell);
    }
    lt_destroyobject(s_IUSB);
    s_Port   = NULL;
    s_IShell = NULL;
    s_PortFd = -1;
}

static bool LTShellUSBSerialImpl_LibInit(void) {
    s_IUSB = lt_createobject(LTDeviceUSBSerial);
    if (!(s_Shell = lt_openlibrary(LTSystemShell))) return false;
    s_Shell->RegisterCommands(USBSerialCommands, (sizeof(USBSerialCommands) / sizeof(USBSerialCommands[0])));
    s_IShell = lt_getlibraryinterface(ILTShell, s_Shell);
    return true;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellUSBSerial, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellUSBSerial) LTLIBRARY_DEFINITION;
