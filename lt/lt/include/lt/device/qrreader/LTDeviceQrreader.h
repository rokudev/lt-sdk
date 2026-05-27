/*******************************************************************************
 * include/lt/device/qrreader/LTDeviceQrreader.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_DEVICE_QRREADER_LTDEVICEQRREADER_H
#define ROKU_LT_INCLUDE_LT_DEVICE_QRREADER_LTDEVICEQRREADER_H

#include <lt/LTTypes.h>
#include <lt/core/LTThread.h>
#include <lt/device/media/LTDeviceMedia.h>

LT_EXTERN_C_BEGIN

typedef enum {
    kLTDeviceQrreaderEvent_Start         = 0x0101,
    kLTDeviceQrreaderEvent_Decode        = 0x0102,
    kLTDeviceQrreaderEvent_CodeReady     = 0x0103,
    kLTDeviceQrreaderEvent_Retry         = 0x01FE,        // maybe temporary error, so retry
    kLTDeviceQrreaderEvent_Fail          = 0x01FF,        // fatal failure, no retry
} LTDeviceQrreaderEvent;

typedef void (*LTDeviceQrreaderEventProc)(LTDeviceQrreaderEvent event, void *data);

/**
 * @brief LTDeviceQrreader Library Root Interface.
 */
typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceQrreader, 1) {

    bool (*Start)(bool bStopOnDecode);
    /**< Start Qr code scanning and decoding.
     * @param bStopOnDecode   true, stop QR scanning after decoding and send event out.
     *                        false, continue QR scanning even after decoding and don't send any event out. This if for testing.
     */

    void (*Stop)(void);
    /**< Stop Qr code scanning and decoding. */

    bool (*GetCode)(char *code, u16 *codeLen);
    /**<
     * Get code from qr reader. Should be called upon receiving the kLTDeviceQrreaderEvent_Decoded event.
     * 
     * @param[out]    code     buffer to store code, must hold the qr code string and '\0'.
     * @param[in/out]  codeLen  the max length of code buffer, including '\0'.
     * @return true   Get a valid code.
     * @return false  No valid code, or buffer is too small, or invalid input.
     * @note  On return, codeLen is the actual length of code, including '\0'.
     */

    void (*OnStatusChange)(LTDeviceQrreaderEventProc proc, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void * clientData);
    void (*NoStatusChange)(LTDeviceQrreaderEventProc proc);
    /**< Register and unregister event call backs. */

} LTLIBRARY_INTERFACE;

/* Interface to driver QR reader */
typedef_LTLIBRARY_INTERFACE(ILTQrreader, 1) {
    bool (*Start)(bool bStopOnDecode);
    void (*Stop)(void);
    bool (*GetCode)(char *code, u16 *codeLen);
    void (*OnStatusChange)(LTDeviceQrreaderEventProc proc, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void * clientData);
    void (*NoStatusChange)(LTDeviceQrreaderEventProc proc);
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif  // ROKU_LT_INCLUDE_LT_DEVICE_QRREADER_LTDEVICEQRREADER_H

