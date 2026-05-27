/*******************************************************************************
 * platforms/linux/source/driver/usbserial/LinuxDriverUSBSerial.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Linux LT Driver Library for USB Serial
 *******************************************************************************/
/** @file LinuxDriverUSBSerial.c Implementation of USB Serial module for T31/Linux
 */

#include <fcntl.h>
#include <lt/core/LTCore.h>
#include <lt/driver/usbserial/LTDriverUSBSerial.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

DEFINE_LTLOG_SECTION("drv.usbserial");

/*******************************************************************************
 *
 * driver helper functions implementation
 *
 *******************************************************************************/

static bool LinuxDriverUSBSerialImpl_CheckAccess(u8 *port) {
    return access((const char *)port, F_OK) == 0 ? true : false;
}

static bool LinuxDriverUSBSerialImpl_OpenSerialPort(const char *port, s32 *portHandle) {
    *portHandle = open(port, O_RDWR | O_NOCTTY | O_SYNC);
    if (*portHandle == -1) {
        LTLOG_YELLOWALERT("f.open", "ttyUSB Port Not Open!\n");
        return false;
    }
    fcntl(*portHandle, F_SETFL, 0);
    return true;
}

static bool LinuxDriverUSBSerialImpl_CloseSerialPort(s32 *portHandle) {
    if (*portHandle != -1) {
        close(*portHandle);
    } else {
        LTLOG_YELLOWALERT("f.close", "ttyUSB Port Not Closed!!\n");
        return false;
    }
    return true;
}

static bool LinuxDriverUSBSerialImpl_ConfigureSerialPort(s32 *portHandle) {
    struct termios operation = {};
    if (tcgetattr(*portHandle, &operation) != 0) {
        LTLOG_YELLOWALERT("f.tcgetattr", "Init operation failed!\n");
        return false;
    }
    cfmakeraw(&operation);                   /* raw mode */
    operation.c_cflag     |= CLOCAL | CREAD; /* local mode and can read */
    operation.c_cflag     &= ~CSIZE;         /* NO mask */
    operation.c_cflag     |= CS8;            /* 8 bit data */
    operation.c_cflag     &= ~PARENB;        /* no parity check */
    operation.c_cflag     &= ~CSTOPB;        /* 1 stop bit; */
    operation.c_cc[VTIME]  = 0;
    operation.c_cc[VMIN]   = 1;
    cfsetispeed(&operation, B115200);
    cfsetospeed(&operation, B115200);
    tcflush(*portHandle, TCIFLUSH);
    if ((tcsetattr(*portHandle, TCSAFLUSH, &operation)) != 0) {
        LTLOG_YELLOWALERT("f.tcsetattr", "Set operation failed!\n");
        return false;
    }
    return true;
}

static s32 LinuxDriverUSBSerialImpl_WriteSerialPort(s32 *portHandle, u8 *data, size_t writeLen) {
    s32 retLen = 0;
    retLen = write(*portHandle, data, writeLen);
    if (retLen <= 0) {
        LTLOG_YELLOWALERT("f.write", "ttyUSB write failed\n");
        return 0;
    }
    return retLen;
}

static s32 LinuxDriverUSBSerialImpl_ReadSerialPort(s32 *portHandle, u8 *buf, size_t bytes) {
    s32 nHandles;
    s32 ret = 0;
    fd_set readHandles;
    struct timeval tv;

    tv.tv_sec  = 0;
    tv.tv_usec = 100000; //100ms timeout

    FD_ZERO(&readHandles);
    FD_SET(*portHandle, &readHandles);

    nHandles = select(*portHandle + 1, &readHandles, NULL, NULL, &tv);
    if (nHandles <= 0) {
        return -1;
    } else {
        if (FD_ISSET(*portHandle, &readHandles)) {
            ret = read(*portHandle, buf, bytes);
        }
    }
    return ret;
}

/*******************************************************************************
 *
 * driver interface implementation
 *
 *******************************************************************************/
static bool LinuxDriverUSBSerialImpl_ClosePort(s32 *portHandle) {
    return LinuxDriverUSBSerialImpl_CloseSerialPort(portHandle);
}

static bool LinuxDriverUSBSerialImpl_ConfigurePort(s32 *portHandle) {
    return LinuxDriverUSBSerialImpl_ConfigureSerialPort(portHandle);
}

static bool LinuxDriverUSBSerialImpl_OpenPort(const char *port, s32 *portHandle) {
    bool response = LinuxDriverUSBSerialImpl_OpenSerialPort(port, portHandle);
    if (response) {
        LinuxDriverUSBSerialImpl_ConfigureSerialPort(portHandle);
    }
    return response;
}

static bool LinuxDriverUSBSerialImpl_WriteBytes(s32 *portHandle, u8 *writePayload, size_t writeLen) {
    return LinuxDriverUSBSerialImpl_WriteSerialPort(portHandle, (u8 *)writePayload, writeLen) > 0;
}

static bool LinuxDriverUSBSerialImpl_ReadBytes(s32 *portHandle, u8 *readPayload, size_t readLen) {
    return LinuxDriverUSBSerialImpl_ReadSerialPort(portHandle, (u8 *)readPayload, readLen) > 0;
}

/********************************************************************************************************************************
 * Library initialization and finalization */
/*******************************************************************************
 * driver lib fini
 *******************************************************************************/
static void LinuxDriverUSBSerialImpl_LibFini(void) {}

/*******************************************************************************
 * driver lib init
 *******************************************************************************/
static bool LinuxDriverUSBSerialImpl_LibInit(void) {
    return true;
}

/*___________________________________________________
 / LinuxDriverUSBSerial library root interface binding */
typedef_LTLIBRARY_ROOT_INTERFACE(LinuxDriverUSBSerial, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LinuxDriverUSBSerial)
LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTDriverUSBSerial){
    .OpenPort       = &LinuxDriverUSBSerialImpl_OpenPort,
    .ClosePort      = &LinuxDriverUSBSerialImpl_ClosePort,
    .WriteBytes     = &LinuxDriverUSBSerialImpl_WriteBytes,
    .ReadBytes      = &LinuxDriverUSBSerialImpl_ReadBytes,
    .CheckAccess    = &LinuxDriverUSBSerialImpl_CheckAccess,
    .ConfigurePort  = &LinuxDriverUSBSerialImpl_ConfigurePort,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LinuxDriverUSBSerial, (ILTDriverUSBSerial));
