/*******************************************************************************
 * lt/source/lt/device/usbserial/LTDeviceUSBSerial.c
 *
 * LT Device Library for the USB Serial
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/usbserial/LTDeviceUSBSerial.h>
#include <lt/driver/usbserial/LTDriverUSBSerial.h>

DEFINE_LTLOG_SECTION("ldev.usbserial")

/*  ___________________________
 *  Object private data members
 */
typedef_LTObjectImpl(LTDeviceUSBSerial, LTDeviceUSBSerialImpl) {
    LTMutex            *mutex;
    LTLibrary          *pDriverLibrary;
    ILTDriverUSBSerial *iDriver;
} LTOBJECT_API;

/* _______________________________
 *  Object private utility functions
 */
static bool LTDeviceUSBSerialImpl_ReadBytes(LTDeviceUSBSerialImpl *usb, s32 *portHandle, u8 *readPayload, u32 readLength) {
    if (!readPayload || !portHandle) return false;
    usb->mutex->API->Lock(usb->mutex);
    bool success = usb->iDriver->ReadBytes(portHandle, readPayload, readLength);
    usb->mutex->API->Unlock(usb->mutex);
    if (!success) LTLOG_YELLOWALERT("f.read", "failed to read from USB serial");
    return success;
}

static bool LTDeviceUSBSerialImpl_WriteBytes(LTDeviceUSBSerialImpl *usb, s32 *portHandle, u8 *writePayload, u32 writeLength) {
    if (!writePayload || !portHandle) return false;
    usb->mutex->API->Lock(usb->mutex);
    bool success = usb->iDriver->WriteBytes(portHandle, writePayload, writeLength);
    usb->mutex->API->Unlock(usb->mutex);
    if (!success) LTLOG_YELLOWALERT("f.write", "failed to write to usb serial");
    return success;
}

static bool LTDeviceUSBSerialImpl_CheckPort(LTDeviceUSBSerialImpl *usb, u8 *port) {
    if (!port) return false;
    usb->mutex->API->Lock(usb->mutex);
    if (!usb->iDriver->CheckAccess(port)) {
        LTLOG_REDALERT("dev.access.f", "failed to access port %s", port);
        usb->mutex->API->Unlock(usb->mutex);
        return false;
    }
    usb->mutex->API->Unlock(usb->mutex);
    return true;
}

static bool LTDeviceUSBSerialImpl_OpenPort(LTDeviceUSBSerialImpl *usb, const char *port, s32 *portHandle) {
    if (!port || !portHandle) return false;
    usb->mutex->API->Lock(usb->mutex);
    bool success = usb->iDriver->OpenPort(port, portHandle);
    usb->mutex->API->Unlock(usb->mutex);
    if (!success) LTLOG_YELLOWALERT("f.open", "failed to open port %s", port);
    return success;
}

static bool LTDeviceUSBSerialImpl_ClosePort(LTDeviceUSBSerialImpl *usb, s32 *portHandle) {
    if (!portHandle) return false;
    usb->mutex->API->Lock(usb->mutex);
    bool success = usb->iDriver->ClosePort(portHandle);
    usb->mutex->API->Unlock(usb->mutex);
    if (!success) LTLOG_YELLOWALERT("f.close", "failed to close port");
    return success;
}

static bool LTDeviceUSBSerialImpl_ConfigurePort(LTDeviceUSBSerialImpl *usb, s32 *portHandle) {
    if (!portHandle) return false;
    usb->mutex->API->Lock(usb->mutex);
    bool success = usb->iDriver->ConfigurePort(portHandle);
    usb->mutex->API->Unlock(usb->mutex);
    if (!success) LTLOG_YELLOWALERT("f.configure", "failed to configure usb serial");
    return success;
}

/*  _________________________________
 *  Object constructor and destructor
 */
static void LTDeviceUSBSerialImpl_DestructObject(LTDeviceUSBSerialImpl *usb) {
    lt_destroyobject(usb->mutex);
    lt_closelibrary(usb->pDriverLibrary);
}

static bool LTDeviceUSBSerialImpl_ConstructObject(LTDeviceUSBSerialImpl *usb) {
    LTDeviceConfig *deviceConfig;
    const char *driverName = NULL;

    do {
        deviceConfig = lt_openlibrary(LTDeviceConfig);
        if (!deviceConfig) {
            LTLOG_YELLOWALERT("f.devconfig", "could not open device config library");
            break;
        }

        driverName = deviceConfig->GetDriverAt("LTDeviceUSBSerial", 0);
        if (!driverName) {
            LTLOG_YELLOWALERT("f.nodrv", "no driver name found in device config");
            lt_closelibrary(deviceConfig);
            break;
        }
        usb->pDriverLibrary = LT_GetCore()->OpenLibrary(driverName);
        lt_closelibrary(deviceConfig);

        usb->iDriver = (ILTDriverUSBSerial *)lt_getlibraryinterface(ILTDriverUSBSerial, usb->pDriverLibrary);
        if (!usb->iDriver) {
            LTLOG_YELLOWALERT("f.drv.iface", "could not get driver usb interface");
            break;
        }

        usb->mutex = lt_createobject(LTMutex);
        if (!usb->mutex) {
            LTLOG_YELLOWALERT("f.mutex", "could not create mutex");
            break;
        }
        return true;
    } while (0);

    LTDeviceUSBSerialImpl_DestructObject(usb);
    return false;
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTDeviceUSBSerial, LTDeviceUSBSerialImpl,
    OpenPort,
    ClosePort,
    ReadBytes,
    WriteBytes,
    CheckPort,
    ConfigurePort
);

define_LTOBJECT_EXPORTLIBRARY(
    LTDeviceUSBSerial, 1, 
    LTDeviceUSBSerialImpl
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Jul-24   caracalla   Created USB Serial Device lib
 */
