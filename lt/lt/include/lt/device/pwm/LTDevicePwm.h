/*******************************************************************************
 * <lt/device/pwm/LTDevicePwm.h> LTDevicePwm
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_PWM_LTDEVICEPWM_H
#define LT_INCLUDE_LT_DEVICE_PWM_LTDEVICEPWM_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDevicePwm, 1) LTLIBRARY_EMPTY_INTERFACE;

typedef enum LTDevicePwm_ClockType {
    kLTDevicePwm_ClockType_Output_None = 0,        /**< Pwm configured with no clock output */
    kLTDevicePwm_ClockType_Output_Primary = 1,     /**< Pwm configured with Primary clock output (if any)  */
    kLTDevicePwm_ClockType_Output_Secondary = 2,   /**< Pwm configured with Secondary clock output (if any)  */
    kLTDevicePwm_ClockType_Output_Tertiary = 3,    /**< Pwm configured with Tertiary clock output (if any)  */

    kLTDevicePwm_ClockType_TOTAL,                  /**< The Total number of values defined in this enum */
} LTDevicePwm_ClockType;

TYPEDEF_LTLIBRARY_INTERFACE(ILTDriverPwmDeviceUnit, 1);

struct ILTDriverPwmDeviceUnitApi {

    INHERIT_INTERFACE_BASE

    bool (*InitPwmPin)(u8 pin, bool activeHigh, u32 frequency, u16 dutyCycle, bool start);
        /**<
         * @brief Initialize a pin for PWM usage
         * @param pin - the GPIO number of the pin
         * @param activeHigh - true for active high, false for active low
         * @param frequency  - the frequency (in Hz) to set it to.
         * @param dutyCycle  - the duty cycle (in permil) to set it to.
         *                     Duty cycle ranges from [0,1000].
         * @param start      - true to start the output, otherwise call
         *                     the Start() function to begin.
         *
         * @returns success
         */

    bool (*Start)(u8 pin);
        /**<
         * @brief Start outputting the (previously configured) PWM waveform on the specified pin
         * @param pin - the GPIO number of the pin
         *
         * @returns success
         */

    bool (*Stop)(u8 pin);
        /**<
         * @brief Stop outputting the PWM waveform on the specified pin
         * @param pin - the GPIO number of the pin
         *
         * @returns success
         */

    bool (*SetDutyCycle)(u8 pin, u16 dutyCycle);
        /**<
         * @brief Set the duty cycle for PWM at a pin (must be running already)
         * @param pin - the GPIO number of the pin
         * @param dutyCycle  - the duty cycle (in permil) to set it to.
         *                     Duty cycle ranges from [0,1000].
         *
         * @returns success
         */

    bool (*GetDutyCycle)(u8 pin, u16 *pDutyCycle);
        /**<
         * @brief Set the duty cycle for PWM at a pin (must be running already)
         * @param pin - the GPIO number of the pin
         * @param pDutyCycle  - ptr to var to hold the duty cycle (in permil).
         *                      Duty cycle ranges from [0,1000].
         *
         * @returns success
         */

    bool (*SetClockOutputPin)(u8 pin, bool enable, LTDevicePwm_ClockType clockType);
        /**<
         * @brief Enable or disable a clock output on the specified pin (if possible)
         * @param pin - the GPIO number of the pin
         * @param enable - Enable or Disable the output.
         * @param clockType - Specify the clock type to use (driver/platform specific)
         *
         *
         * @note this function isn't related to PWM, but allows configuration
         * of an alternate function on a GPIO pin to output one of the internal
         * clock signals if supported (which it is on one of our vendor's chips).
         *
         * It's here in the PWM device for now, and it could stay here permanently
         * if desired, since it allows for a frequency to be emitted from a pin
         * just like PWM does.  Or it could move to a GPIO or PinMux device instead.
         *
         * @returns success
         */
};

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_PWM_LTDEVICEPWM_H */

