/*******************************************************************************
 * <lt/device/gpio/LTDeviceGpio.h> LTDeviceGpio
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_GPIO_LTDEVICEGPIO_H
#define LT_INCLUDE_LT_DEVICE_GPIO_LTDEVICEGPIO_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

typedef enum LTDeviceGpio_ModeType {
    kLTDeviceGpio_ModeType_Error  = -1,        /**< Unknown or incorrect */

    kLTDeviceGpio_ModeType_Input  = 0,        /**< Gpio configured as Input */
    kLTDeviceGpio_ModeType_Output,            /**< Gpio configured as Output */
    kLTDeviceGpio_ModeType_Both,              /**< Gpio configured as Both In and Out (shouldn't be possible) */
    kLTDeviceGpio_ModeType_HighZ,             /**< Gpio configured as high impedance */
    kLTDeviceGpio_ModeType_AlternateFunction, /**< Gpio configured as Alternate function */
} LTDeviceGpio_ModeType;

typedef enum LTDeviceGpio_PullType {
    kLTDeviceGpio_PullType_Error  = -1,    /**< Unknown or incorrect */

    kLTDeviceGpio_PullType_NoPull = 0,     /**< Gpio configured without pull-up or pull-down */
    kLTDeviceGpio_PullType_PullUp,         /**< Gpio configured with internal pull-up resistor */
    kLTDeviceGpio_PullType_PullDown        /**< Gpio configured with internal pull-down resistor */
} LTDeviceGpio_PullType;

typedef enum LTDeviceGPIO_TriggerType {
    kLTDeviceGPIO_TriggerType_Error  = -1,    /**< Unknown or incorrect */
    kLTDeviceGPIO_TriggerType_FallingEdge = 0,/**< Trigger on rising edge */
    kLTDeviceGPIO_TriggerType_RisingEdge,     /**< Trigger on falling edge */
    kLTDeviceGPIO_TriggerType_BothEdges,      /**< Trigger on both edges */
    kLTDeviceGPIO_TriggerType_LowLevel,       /**< Trigger on low level */
    kLTDeviceGPIO_TriggerType_HighLevel,      /**< Trigger on high level */
} LTDeviceGPIO_TriggerType;

typedef void (LTDeviceGPIO_ISR)(void* pClientData) LT_ISR_SAFE;
/**< GPIO interrupt service routine template*/

/* ___________________
   LTDeviceGpio API */
typedef_LTObject(LTDeviceGpio, 1) {

    u16 (*GetNumberOfGpios)(LTDeviceGpio *gpio);
        /**<
         * @brief Get the number of GPIOs on this platform.
         *
         * @returns The number of GPIOs on the system.
         */

    char const * (*GetGpioNameFromIndex)(LTDeviceGpio *gpio, u16 gpioIndex);
        /**<
         * @brief Get the name string of the specified GPIO
         *
         * @param gpioIndex - The index of this GPIO (ranging from 0 to GetNumberOfGpios - 1)
         *
         * @returns The string name of the GPIO (or NULL on error)
         */

    LTDeviceGpio_ModeType (*GetGpioModeFromIndex)(LTDeviceGpio *gpio, u16 gpioIndex);
        /**<
         * @brief Get the Mode type of the specified GPIO
         *
         * @param gpioIndex - The index of this GPIO (ranging from 0 to GetNumberOfGpios - 1)
         *
         * @returns The mode of the GPIO (or -1 on error)
         */

    LTDeviceGpio_PullType (*GetGpioPullFromIndex)(LTDeviceGpio *gpio, u16 gpioIndex);
        /**<
         * @brief Get the Pull type of the specified GPIO
         *
         * @param gpioIndex - The index of this GPIO (ranging from 0 to GetNumberOfGpios - 1)
         *
         * @returns The pull of the GPIO (or -1 on error)
         */

    bool (*SetGpioModeFromIndex)(LTDeviceGpio *gpio, u16 gpioIndex, LTDeviceGpio_ModeType mode);
        /**<
         * @brief Set the Mode type of the specified GPIO
         *
         * @param gpioIndex - The index of this GPIO (ranging from 0 to GetNumberOfGpios - 1)
         * @param mode - The mode of the GPIO
         *
         * @returns Success
         */

    bool (*SetGpioPullFromIndex)(LTDeviceGpio *gpio, u16 gpioIndex, LTDeviceGpio_PullType pull);
        /**<
         * @brief Set the Pull type of the specified GPIO
         *
         * @param gpioIndex - The index of this GPIO (ranging from 0 to GetNumberOfGpios - 1)
         * @param mode - The pull of the GPIO
         *
         * @returns Success
         */

    u16 (*GetNumberOfAlternateFunctions)(LTDeviceGpio *gpio);
        /**<
         * @brief Get the number of alternate functions a GPIO can be set to on this platform.
         *
         * @returns The number of alternate functions on the system (or 0 on error).
         */

    char const * (*GetAlternateFunctionNameFromIndex)(LTDeviceGpio *gpio, u16 afIndex);
        /**<
         * @brief Get the name string of the specified alternate function
         *
         * @param afIndex - The index of this alternate function (ranging from 0 to GetNumberOfAlternateFunctions - 1)
         *
         * @returns The string name of the alternate function (or NULL on error)
         */

    int (*GetGpioAlternateFunctionFromIndex)(LTDeviceGpio *gpio, u16 gpioIndex);
        /**<
         * @brief Get the alternate function of the specified GPIO
         *
         * @param gpioIndex - The index of this GPIO (ranging from 0 to GetNumberOfGpios - 1)
         *
         * @returns The alternate function index of the GPIO (or -1 on error)
         */

    bool (*SetGpioAlternateFunctionFromIndex)(LTDeviceGpio *gpio, u16 gpioIndex, u16 afIndex);
        /**<
         * @brief Set the alternate function of the specified GPIO
         *
         * @param gpioIndex - The index of this GPIO (ranging from 0 to GetNumberOfGpios - 1)
         * @param afIndex - The index of the alternate function (ranging from 0 to GetNumberOfAlternateFunctions - 1)
         *
         * @returns Success
         */

    bool (*SetOutputValue)(LTDeviceGpio *gpio, u16 gpioIndex, bool value);
        /**<
         * @brief Sets (writes) the output value of the specified GPIO
         *
         * @param gpioIndex - The index of this GPIO (ranging from 0 to GetNumberOfGpios - 1)
         * @param value - The value to drive the GPIOs output
         *
         * @returns Success
         */

    bool (*GetInputValue)(LTDeviceGpio *gpio, u16 gpioIndex);
        /**<
         * @brief Gets (reads) the input value of the specified GPIO.
         *
         * @param gpioIndex - The index of this GPIO (ranging from 0 to GetNumberOfGpios - 1)
         *
         * @returns The value read from the GPIOs input
         */

    u16 (*GetNumberOfNamedPins)(LTDeviceGpio *gpio);
        /**<
         * @brief Get the number of named GPIO pins on this platform.
         *
         * @returns The number of named pins on the system (or 0 on error).
         */

    char const * (*GetNamedPinFromIndex)(LTDeviceGpio *gpio, u16 npIndex);
        /**<
         * @brief Get the name string of the specified named pin
         *
         * @param npIndex - The index of this named pin (ranging from 0 to GetNumberOfNamedPins - 1)
         *
         * @returns The string name of the named pin (or NULL on error)
         */

    int (*GetNamedPinValueFromIndex)(LTDeviceGpio *gpio, u16 npIndex);
        /**<
         * @brief Gets the value of the named pin by index
         *
         * @param npIndex - The index of this named pin (ranging from 0 to GetNumberOfNamedPins - 1)
         *
         * @returns The value of this named pin (or -1 on error)
         */

    int (*GetNamedPinValueFromName)(LTDeviceGpio *gpio, char const *npName);
        /**<
         * @brief Gets the value of the named pin by index
         *
         * @param npName - The name string of this named pin
         *
         * @returns The value of this named pin (or -1 on error)
         */

    bool (*SetISR)(LTDeviceGpio *gpio, u16 gpioIndex, LTDeviceGPIO_ISR *pISR, LTDeviceGPIO_TriggerType triggerType, void* pClientData);
        /**<
         * @brief Sets the interrupt service routine for the specified GPIO
         *
         * @param gpioIndex - The index of this GPIO (ranging from 0 to GetNumberOfGpios - 1)
         *
         * @param pISR - The ISR to be called when the GPIO interrupts. Set NULL to disable the interrupt.
         *
         * @param triggerType - The type of trigger for the interrupt
         *
         * @param pClientData - User data to be passed to the ISR
         *
         * @returns Success
         */

    void (*ClearGPIOPendingIRQ)(LTDeviceGpio *gpio, s16 gpioIndex);
        /**<
         * @brief Clears the pending GPIO IRQ
         *
         * @param gpioIndex - The index of this GPIO
         *
         * @returns void
         */

    u32 (*GetWakeupGPIO)(LTDeviceGpio *gpio);
        /**<
         * @brief Get a bitfield indicating all the GPIOs that triggered the interrupt.
         *
         * @returns The bitfield of the interrupting GPIOs.
         */
} LTOBJECT_API;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_GPIO_LTDEVICEGPIO_H */

