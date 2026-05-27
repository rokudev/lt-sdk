/*******************************************************************************
 * <lt/device/PIR/LTDevicePIR.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 *  @file LTDevicePIR.h header for public interface class
 *  LTDevicePIR */

#ifndef LT_INCLUDE_LT_DEVICE_LTDEVICEPIR_H
#define LT_INCLUDE_LT_DEVICE_LTDEVICEPIR_H

#include <lt/LT.h>

LT_EXTERN_C_BEGIN

typedef void (LTDevicePIRMotionProc)(bool bMotionDetected, u32 nDeviceIndex, void * pClientData);
    /**< Callback signature for reporting motion from the driver to the device
     *   @param nMotion true if motion was detect; false if motion ended
     *   @param pClientData the client data that was passed in using the register functions */

/*******************************************************************************
 * PIR device interface (LTDevicePIR)
*******************************************************************************/
typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDevicePIR, 1) {
    /**< Interface for handling PIR notifications */

    bool (* GetDeviceNameFromUnitNumber)(u32 nDeviceUnitNumber, char const ** ppDeviceName);
        /**< obtains a pointer to the PIR sensor name.
         *  @param nDeviceUnitNumber the Device Unit index
         *  @param ppDeviceName address of pointer to set to address of name.
         *  @return true if *ppDeviceName was written, false if not. */

    bool (* GetUnitNumberFromDeviceName)(char const * pDeviceName, u32 * pDeviceUnitNumber);
        /**< obtains the Device Unit number of the named device.
         *  @param pDeviceName the name of the PIR sensor for which to retrieve the Device Unit number.
         *  @param pDeviceUnitNumber pointer to u32 to hold the Device Unit number.
         *  @return true and set *pDeviceUnitNumber if a device by the given name exists,
         *                false otherwise. */

    void (* RegisterForMotionDetection)(LTDeviceUnit hDeviceUnit, LTDevicePIRMotionProc * pMotionProc, LTThread_ClientDataReleaseProc *pReleaseProc, void * pClientData);
        /**< register the given proc for receiving motion detection events
         *  @param hDeviceUnit a device handle
         *  @param pMotionProc the proc for receiving motion detection events
         *  @param pClientData the client data to pass back up to the client in the event */

    void (* UnregisterFromMotionDetection)(LTDeviceUnit hDeviceUnit, LTDevicePIRMotionProc * pMotionProc);
        /**< unregisters the given proc from receiving motion detection events
         *  @param hDeviceUnit the device unit handle
         *  @param pMotionProc the proc to unregister from receiving motion detection events */

    bool (* SetSensitivity)(LTDeviceUnit hDeviceUnit, u8 nPercent);
        /**< Sets the PIR sensitivity
         *  @param hDeviceUnit the device unit to act upon
         *  @param nPercent the percentage of sensitivity. The higher the more sensitive
         *  @return true if sensitivity was set; false otherwise */

    bool (* GetSensitivity)(LTDeviceUnit hDeviceUnit, u8 *pValue);
         /**< Gets the current PIR sensitivity
          *  @param hDeviceUnit the device unit to act upon
          *  @param pValue the current sensitivity value; the meaning of its value is implementation-dependent.
          *  @return true if sensitivity was retrieved; false otherwise */

    bool (* SetMotionEndDelay)(LTDeviceUnit hDeviceUnit, LTTime interval);
        /**< Sets the interval where no motion is re-detected to avoid flip-flopping
         *  @param hDeviceUnit the device unit to act upon
         *  @param interval the interval to set
         *  @return true if delay was set; false otherwise */

    void (* Enable)(LTDeviceUnit hDeviceUnit, bool bEnable);
        /**< Enable/disable the driver
         *  @param hDeviceUnit the device unit to enable/disable
         *  @param bEnable enables the driver if true; disables it otherwise */

} LTLIBRARY_INTERFACE;


/*******************************************************************************
 * PIR driver interface (ILTDriverPIR)
*******************************************************************************/
typedef_LTLIBRARY_INTERFACE(ILTDriverPIR, 1) {
    /**< Interface for accessing the PIR driver */

    const char * (* GetDeviceName)(u32 nDeviceUnitNumber);
        /**< Reports the name of the driver
         *  @param nDeviceUnitNumber the device unit number
         *  @return the name of the driver */

    void (* Enable)(u32 nDeviceUnitNumber, bool bEnable);
        /**< Enable/disable the driver
         *  @param nDeviceUnitNumber the device index to enable/disable
         *  @param bEnable enables the driver if true; disables it otherwise */

    bool (* SetSensitivity)(u32 nDeviceUnitNumber, u8 nPercent);
        /**< Sets the PIR sensitivity
         *  @param nDeviceUnitNumber the device index
         *  @param nPercent the percentage of sensitivity. The higher the more sensitive
         *  @return true if sensitivity was set; false otherwise */

    bool (* GetSensitivity)(LTDeviceUnit hDeviceUnit, u8 *pValue);
         /**< Gets the current PIR sensitivity
          *  @param hDeviceUnit the device unit to act upon
          *  @param pValue the current sensitivity value; the meaning of its value is implementation-dependent.
          *  @return true if sensitivity was retrieved; false otherwise */

    bool (* SetMotionEndDelay)(u32 nDeviceUnitNumber, LTTime interval);
        /**< Sets the interval where no motion is re-detected to avoid flip-flopping
         *  @param nDeviceUnitNumber the device index
         *  @param interval the interval to set
         *  @return true if delay was set; false otherwise */

    void (* StartMotionDetection)(LTThread_TaskProc * pMotionTaskProc, u8 nDriverId);
        /**< Starts motion detection to the given TaskProc
         *  @param pMotionTaskProc the TaskProc to report motion to
         *  @param nDriverId a driver ID that gets sent back up in the LTDevicePIRMotionEvent struct, which
         *                   is what the driver sends up to the device in the client data of the TaskProc */

    void (* StopMotionDetection)(void);
        /**< Stops reporting motion detection */

} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_LTDEVICEPIR_H */
/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  10-Nov-21   vitellius   created
 */
