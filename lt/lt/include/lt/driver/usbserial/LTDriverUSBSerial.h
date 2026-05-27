/*******************************************************************************
 * lt/include/lt/driver/usbserial/LTDriverUSBSerial.h
 *
 * Interface for a USB Serial Port
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DRIVER_USBSERIAL_LTLINUXDRIVERUSBSERIAL_H
#define LT_INCLUDE_LT_DRIVER_USBSERIAL_LTLINUXDRIVERUSBSERIAL_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

typedef_LTLIBRARY_INTERFACE(ILTDriverUSBSerial, 1) {
    bool (*OpenPort)(const char *port, s32 *portHandle);
    /**< Open a usb serial port
     *
     *   @param[in]  port a usb serial port to open
     *   @param[out] portHandle a pointer to the file descriptor of usb serial device
     *   @return true if successfully opens usb port and false otherwise.
     */
    bool (*ClosePort)(s32 *portHandle);
    /**< Close opened usb serial port file descriptor portHandle.
     *
     *   @param[in] portHandle a pointer to the file descriptor of usb serial port
     *   @return true if successfully closed usb portHandle and false otherwise.
     */
    bool (*WriteBytes)(s32 *portHandle, u8 *writePayload, LT_SIZE writeLen);
    /**< Write writeLen bytes to usb serial port file descriptor portHandle.
     *
     *   @param[in] portHandle a pointer to the usb serial port file descriptor portHandle
     *   @param[in] writePayload a pointer to the buffer to write to portHandle
     *   @param[in] writeLen the number of bytes to write to portHandle
     *   @return true if successfully write to usb portHandle and false otherwise
     */
    bool (*ReadBytes)(s32 *portHandle, u8 *readPayload, LT_SIZE readLen);
    /**< Read readLen  bytes from file descriptor of usb serial device portHandle.
     *
     *   @param[in]  portHandle a pointer to the file descriptor of usb serial port
     *   @param[out] readPayload a pointer to the buffer to store read readLen bytes from portHandle
     *   @param[in]  readLen the number of bytes to read from portHandle
     *   @return true if successfully read response from portHandle and false otherwise.
     */

    bool (*CheckAccess)(u8 *port);
    /**< @brief Check if access to the specific port is available or not.
     *   @param[in]  port  usb serial port to open
     *   @return true if access is available, false otherwise.
     */

    bool (*ConfigurePort)(s32 *portHandle);
    /**< Configure opened usb serial port file descriptor portHandle
     *
     *  @param[in] portHandle a pointer to the usb serial port file descriptor portHandle
     *  @return true if successfully configure portHandle and false otherwise
     */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DRIVER_USBSERIAL_LTLINUXDRIVERUSBSERIAL_H */
