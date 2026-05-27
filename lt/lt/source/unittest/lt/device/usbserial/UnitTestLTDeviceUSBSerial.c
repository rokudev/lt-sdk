/*******************************************************************************
 * lt/source/unittest/lt/device/usbserial/UnitTestLTDeviceUSBSerial.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTThread.h>
#include <lt/device/usbserial/LTDeviceUSBSerial.h>
#include <tilt/JiltEngine.h>

/******************************************************************************
 * Static Definitions
 ******************************************************************************/
static struct serial_statics {
    LTDeviceUSBSerial      *pUSBSerial;
} S;

static JiltEngine *s_engine;

static u32 SerialChecksum(u8 *data, s32 len) {
    u32 hexSum = 0;
    if (!data) return 0;
    for (; len; --len) hexSum += *data++;
    return hexSum;
}

/*******************************************************************************
 * Test Function
 *******************************************************************************/
/******************************************************************************
 * writePayload description:
 * form             length      details
 * Protocol head     2 bytes    0xAA 0x55
 *                              (camera->floodlight)
 * characteristcs    1          0x43
 *                              (camera order to CH552)
 * packet length     1          0x03
 *                              (package length is length of payload except
 *                               payload head + characteristcs + packet length)
 * package name      1          0x27
 *                              (used to distinguish between different commands
 *                               in the same protocol)
 * main body         n[0~50]    (command body)
 * checksum          2          (checksum all previous data cumulative sums)
 * ****************************************************************************/
static void TestUSBSerial(Tilt *tilt) {
    static const char port[]   = "/dev/ttyUSB0";
    s32               s_portFd = 0;

    S.pUSBSerial = lt_createobject(LTDeviceUSBSerial);
    if (!S.pUSBSerial) {
        TILT_REPORT_FAILURE(tilt, "Failed to create LTDeviceUSBSerial object");
    }
    if (!S.pUSBSerial->API->OpenPort(S.pUSBSerial, (const char *)port, &s_portFd)) {
        TILT_REPORT_FAILURE(tilt, "Error opening usb device");
    }
    if (!S.pUSBSerial->API->ConfigurePort(S.pUSBSerial, &s_portFd)) {
        TILT_REPORT_FAILURE(tilt, "Error configuring usb serial device");
    }

    u8  readPayload[8]                     = {0};
    u8  writePayload[7]                    = {0xAA, 0x55, 0x43, 0x03, 0x27};
    u64 checksum                           = SerialChecksum(writePayload, (sizeof(writePayload) - 2));
    writePayload[sizeof(writePayload) - 2] = (checksum >> 8) & 0xff;
    writePayload[sizeof(writePayload) - 1] = (checksum >> 0) & 0xff;

    /**expected data upon read */
    u8 expectedResponse[8]                     = {0x55, 0xAA, 0x43, 0x04, 0x28, 0x03};
    checksum                                   = SerialChecksum(expectedResponse, (sizeof(expectedResponse) - 2));
    expectedResponse[sizeof(expectedResponse) - 2] = (checksum >> 8) & 0xff;
    expectedResponse[sizeof(expectedResponse) - 1] = (checksum >> 0) & 0xff;

    if (!S.pUSBSerial->API->WriteBytes(S.pUSBSerial, &s_portFd, writePayload, sizeof(writePayload))) {
        TILT_REPORT_FAILURE(tilt, "Error writing usb serial port");
    }
    if (!S.pUSBSerial->API->ReadBytes(S.pUSBSerial, &s_portFd, readPayload, sizeof(readPayload))) {
        TILT_REPORT_FAILURE(tilt, "Error Reading usb device");
    }
    if (!S.pUSBSerial->API->ClosePort(S.pUSBSerial, &s_portFd)) {
        TILT_REPORT_FAILURE(tilt, "Error closing usb device");
    }
    for (u8 i = 0; i < sizeof(expectedResponse); i++) {
        TILT_ASSERT_TRUE(tilt, expectedResponse[i] == readPayload[i], "Response do not match with expected response");
    }

    lt_destroyobject(S.pUSBSerial);
}

static const TiltEngineTest s_tests[] = {
    { TestUSBSerial, "UnitTestUSBSerial", "USB Serial test", 0 },
};

static int UnitTestLTDeviceUSBSerialImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), NULL);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceUSBSerialImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceUSBSerialImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceUSBSerial, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceUSBSerial, UnitTestLTDeviceUSBSerialImpl_Run, 1536) LTLIBRARY_DEFINITION;
