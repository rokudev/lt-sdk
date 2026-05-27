/*******************************************************************************
 * <lt/device/rotaryencoder/LTDeviceRotaryEncoder.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 *  @file LTDeviceRotaryEncoder.h header for public interface class
 *  LTDeviceRotaryEncoder */

#ifndef LT_INCLUDE_LT_DEVICE_LTDEVICEROTARYENCODER_H
#define LT_INCLUDE_LT_DEVICE_LTDEVICEROTARYENCODER_H

#include <lt/LT.h>
#include <lt/device/rotaryencoder/LTDeviceRotaryEncoderDefs.h>

LT_EXTERN_C_BEGIN

typedef void (LTDeviceRotaryEncoderPositionChangedProc)(RotaryEncoderPosition position, void * pClientData);
    /**< Callback signature for reporting position from the driver to the device
     *   @param nPosition the new position value
     *   @param pClientData the client data that was passed in using the register functions */

/*******************************************************************************
 * Rotary encoder device interface (LTDeviceRotaryEncoder)
*******************************************************************************/
typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceRotaryEncoder, 1) {
    /**< Interface for handling Rotary Encoder notifications */

    bool (* GetEncoderNameFromUnitNumber)(u32 nDeviceUnitNumber, char const ** ppEncoderName);
        /**< obtains a pointer to the encoder's name.
         *   @param nDeviceUnitNumber the Device Unit index.  Invalid values (>= the return value of
         *          GetNumDeviceUnits())
         *   @param ppEncoderName address of pointer to set to address of name.
         *   @return true if *ppEncoderName was written, false if not. */

    bool (* GetUnitNumberFromEncoderName)(char const * pEncoderName, u32 * pDeviceUnitNumber);
        /**< obtains the Device Unit number of the named encoder.
         *   @param pEncoderName the name of the encoder for which to retrieve the Device Unit number.
         *   @param pDeviceUnitNumberToSet pointer to u32 to hold the Device Unit number.
         *   @return true and set *pDeviceUnitNumberToSet if a pin bank by the given name exists,
         *                false otherwise. */

    void (* RegisterForPositionChanges)(LTDeviceUnit hDeviceUnit, LTDeviceRotaryEncoderPositionChangedProc * pChangeProc, LTTime interval, void * pClientData);
        /**< register the given proc for receiving position change events
         *   @param hDeviceUnit a device handle created by CreateDeviceUnitHandle() and CreateDeviceUnitHandleByName()
         *   @param pChangeProc the proc for receiving position change events
         *   @param interval the interval for reporting new positions. Position is reported at 10Hz max with increments of 100 milliseconds
         *   @param pClientData the client data to pass back up to the client in the event
         *
         *   @note the notification proc is called in the calling thread's context
         *   @note interval multiples of 10 milliseconds
         *   @note a client can register multiple times with different notification interval and call unregister once when its done */

    void (* UnregisterFromPositionChanges)(LTDeviceUnit hDeviceUnit);
        /**< unregisters the given proc from receiving position change events
         *   @param hDeviceUnit the device unit handle
         *
         *   @note unregister only needs to be called once and it will unregister all intervals */
} LTLIBRARY_INTERFACE;


/*******************************************************************************
 * Rotary encoder driver interface (ILTDriverRotaryEncoder)
*******************************************************************************/
typedef_LTLIBRARY_INTERFACE(ILTDriverRotaryEncoder, 1) {
    /**< Interface for accessing the rotary encoder driver */

    bool (* GetEncoderNameFromUnitNumber)(u32 nDeviceUnitNumber, char const ** ppEncoderName);
        /**< obtains a pointer to the encoder's name.
         *   @param nDeviceUnitNumber the Device Unit index.  Invalid values (>= the return value of
         *          GetNumDeviceUnits())
         *   @param ppEncoderName address of pointer to set to address of name.
         *   @return true if *ppEncoderName was written, false if not. */

    bool (* GetUnitNumberFromEncoderName)(char const * pEncoderName, u32 * pDeviceUnitNumber);
        /**< obtains the Device Unit number of the named encoder.
         *   @param pEncoderName the name of the encoder for which to retrieve the Device Unit number.
         *   @param pDeviceUnitNumberToSet pointer to u32 to hold the Device Unit number.
         *   @return true and set *pDeviceUnitNumberToSet if a pin bank by the given name exists,
         *                false otherwise. */

    RotaryEncoderPosition (* GetPosition)(u32 nDeviceNumber);
        /**< returns the current position info for the given device
         *   @param hDevice the handle to the device */

    void (* EnableAcceleration)(u32 nDeviceNumber, bool bEnable);
        /**< Enables position change acceleration, which is changing the position in larger increments when the encoder is rotated faster
         *   i.e. the position would change by 5 positions per tick instead of 1 for example (depending on how fast the user is moving the knob)
         *   @param nDeviceNum  the device number to enable/disable the acceleration on
         *   @param bEnable true to enable speed acceleration; false otherwise. */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_LTDEVICEROTARYENCODER_H */
/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  10-Nov-21   vitellius   created
 */
