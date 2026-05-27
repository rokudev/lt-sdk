/*******************************************************************************
 * <lt/device/pins/LTDevicePins.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltdevice_pins LTDevicePins
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library providing access to GPIO inputs and outputs
 */

#ifndef LT_INCLUDE_LT_DEVICE_PINS_LTDEVICEPINS_H
#define LT_INCLUDE_LT_DEVICE_PINS_LTDEVICEPINS_H

#include <lt/core/LTTime.h>
#include <lt/device/power/LTDevicePower.h>

LT_EXTERN_C_BEGIN

enum LTDevicePin_PinType {
    kLTDevicePin_PinType_Output        = 0,  /**< Output pin type */
    kLTDevicePin_PinType_Input         = 1,  /**< Input pin type*/
    kLTDevicePin_PinType_Bidirectional = 2,  /**< Bidirectional (configurable) pin type */
    kLTDevicePin_PinType_Invalid       = 3   /**< Unusable pin */
};
typedef u32  LTDevicePin_PinType;
typedef void LT_ISR_SAFE (LTDevicePin_IRQCallback)(bool bPinHigh, void * pClientData);
    /**< GPIO callback function, executing in interrupt context.
     *   @param[in] bPinHigh true if GPIO pin transitioned from logic-low to logic-high state,
     *              false otherwise.
     *   @param[in] pClientData Pointer to client data provisioned using EnableIRQ().  */

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDevicePins, 1);

/** The Device Pin Library is used for the configuration, monitoring and control of GPIO pins.
 *  Pins are organized in banks of up to 32-pins each. Pins in a given bank are all of the
 *  same type (e.g.: output, input or bidirectional).
 */
struct LTDevicePins {

    INHERIT_DEVICE_LIBRARY_BASE

    bool (* GetBankNameFromUnitNumber)(u32 nDeviceUnitNumber, char const ** ppPinBankNameToSet);
        /**< Obtains a pointer to the pin bank name.
         *   @param[in]  nDeviceUnitNumber The Device Unit index. Out of range values (>= the return
         *               value of GetNumDeviceUnits()) cause this function to leave *ppPinBankNameToSet
         *               untouched and return false.
         *   @param[out] ppPinBankNameToSet Pointer used to return the address of the pin bank name.
         *   @return     true if *ppPinBankNameToSet was written, false if not. */

    bool (* GetUnitNumberFromBankName)(char const * pPinBankName, u32 * pDeviceUnitNumberToSet);
        /**< Obtains the Device Unit number of the named pin bank.
         *   @param[in]  pPinBankName The name of the bank for which to retrieve The Device Unit number.
         *   @param[out] pDeviceUnitNumberToSet Pointer used to return The Device Unit number.
         *   @return     true and set *pDeviceUnitNumberToSet if a pin bank by the given name exists,
         *               false otherwise. */

    bool (* GetBankTypeFromUnitNumber)(u32 nDeviceUnitNumber, LTDevicePin_PinType * pPinTypeToSet);
        /**< Determines whether a bank is an output, input, bidirectional or invalid pin bank.
         *   @param[in]  nDeviceUnitNumber The Device Unit index. Out of range values (>= the return
         *               value of GetNumDeviceUnits()) cause this function to leave *pPinTypeToSet
         *               untouched and return false.
         *   @param[out] pPinTypeToSet Pointer used to return the pin type.
         *   @return     true and set *pPinTypeToSet for valid Device Unit Numbers, false otherwise. */

};

typedef enum LTDevicePin_PinConfiguration_OutputType {
    kLTDevicePin_PinConfiguration_OutputType_PushPull = 0, /**< Push-pull output transistor configuration */
    kLTDevicePin_PinConfiguration_OutputType_OpenDrain     /**< Open-drain output transistor configuration */
} LTDevicePin_PinConfiguration_OutputType;

typedef enum LTDevicePin_PinConfiguration_PullType {
    kLTDevicePin_PinConfiguration_PullType_NoPull = 0,     /**< Pin configured without pull-up or pull-down */
    kLTDevicePin_PinConfiguration_PullType_PullUp,         /**< Internal pull-up resistor pin configuration */
    kLTDevicePin_PinConfiguration_PullType_PullDown        /**< Internal pull-down resistor pin configuration */
} LTDevicePin_PinConfiguration_PullType;

typedef enum LTDevicePin_PinConfiguration_Trigger {
    kLTDevicePin_PinConfiguration_Trigger_RisingEdge = 0,  /**< IRQ callback on input rising edge */
    kLTDevicePin_PinConfiguration_Trigger_FallingEdge,     /**< IRQ callback on input falling edge */
    kLTDevicePin_PinConfiguration_Trigger_BothEdges,       /**< IRQ callback on input change */
    kLTDevicePin_PinConfiguration_Trigger_HighLevel,       /**< IRQ callback on high level trigger */
    kLTDevicePin_PinConfiguration_Trigger_LowLevel         /**< IRQ callback on low level trigger */
} LTDevicePin_PinConfiguration_Trigger;

TYPEDEF_LTLIBRARY_INTERFACE(ILTDriverPins_OutputBank, 1);

/** Library interface for a bank of output pins. */
struct ILTDriverPins_OutputBankApi {

    INHERIT_INTERFACE_BASE

    u32  (* GetNumPins)(LTDeviceUnit hPinBank);
        /**< Returns the number of pins in the pin bank.
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @return    The number of pins in the pin bank. */

    void (* ConfigureOutputType)(LTDeviceUnit hPinBank, LTDevicePin_PinConfiguration_OutputType outputType);
        /**< Sets the output type of the pins in the pin bank. All pins in the bank are set to the same output type.
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @param[in] outputType The output type to set for the pins in the pin bank.
         *      @arg kLTDevicePin_PinConfiguration_OutputType_PushPull
         *                         Causes the output pin to drive the pin to the logic-low level when a 0 is set for
         *                         that bit by a call to Set() (below), and drive the pin to the logic-high level when
         *                         a 1 is set for that bit.
         *      @arg kLTDevicePin_PinConfiguration_OutputType_OpenDrain
         *                         Causes the output pin to drive the pin to the logic-low level when a 0 is set for
         *                         that bit by a call to Set() (below), and to set the pin to a high-impedance state
         *                         when a 1 is set for that bit, allowing the pin to float high or be pulled high
         *                         externally (typically at the input end). */

    void (* ConfigureRebootHold)(LTDeviceUnit hPinBank, bool bRebootHold);
        /**< Configures bank to hold on to the pin level through the next reboot, or clears that
         *   setting and reverts the pin bank to the normal operation.
         *   @param[in] hPinBank    The Device Unit handle of the pin bank.
         *   @param[in] bRebootHold \a true for entering the hold mode,
         *                          \a false to exit the mode
         *
         *   After a reboot, all pin banks automatically exit the hold mode */

    void (* Set)(LTDeviceUnit hPinBank, u32 pinBits);
        /**< Drives pins in the pin bank high or low, according to the value in pinBits.
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @param[in] pinBits Value for the pins, LSB justified (e.g.: a bank of five pins uses LSB bits 0-4),
         *              where 0 drives the pin low and 1 drives the pin high. Unused bits are ignored. */

    u32  (* Read)(LTDeviceUnit hPinBank);
        /**< Returns the current state of the pins in the pin bank.
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @return    The current state of the pins, LSB justified (e.g.: a bank of five pins uses LSB bits 0-4),
         *              a 0 representing a logic-low level on the pin, a 1 representing a logic-high level. */

};

TYPEDEF_LTLIBRARY_INTERFACE(ILTDriverPins_BidirectionalBank, 1);

/** Library interface for a bank of bidirectional pins. */
struct ILTDriverPins_BidirectionalBankApi {

    INHERIT_INTERFACE_BASE

    u32  (* GetNumPins)(LTDeviceUnit hPinBank);
        /**< Returns the number of pins in the pin bank.
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @return    The number of pins in the pin bank. */

    void (* ConfigureAsOutput)(LTDeviceUnit hPinBank, LTDevicePin_PinConfiguration_OutputType outputType);
        /**< Switches bidirectional bank to output mode and sets output type of the pins in the pin bank.
         *   All pins in the bank are set to the same output type.
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @param[in] outputType The output type to set for the pins in the pin bank.
         *      @arg kLTDevicePin_PinConfiguration_OutputType_PushPull
         *                     Causes the output pin to drive the pin to the logic-low level when a 0 is set for
         *                     that bit by a call to Set() (below), and drive the pin to the logic-high level when
         *                     a 1 is set for that bit.
         *      @arg kLTDevicePin_PinConfiguration_OutputType_OpenDrain
         *                     Causes the output pin to drive the pin to the logic-low level when a 0 is set for
         *                     that bit by a call to Set() (below), and to set the pin to a high-impedance state
         *                     when a 1 is set for that bit, allowing the pin to float high or be pulled high
         *                     externally (typically at the input end). */

    void (* ConfigureAsInput)(LTDeviceUnit hPinBank, LTDevicePin_PinConfiguration_PullType pullType);
        /**< Switches bidirectional bank to input mode and sets the pull type of the pins in the pin bank.
         *   All pins in the bank are set to the same pull type.
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @param[in] pullType The pull type to set for the pins in the pin bank.
         *      @arg kLTDevicePin_PinConfiguration_PullType_NoPull
         *                 Disconnects any internal pull-up or pull-down from each pin in the bank.
         *      @arg kLTDevicePin_PinConfiguration_PullType_PullDown
         *                 Connects an internal pull-down resistor to each pin in the bank.
         *      @arg kLTDevicePin_PinConfiguration_PullType_PullUp
         *                 Connects an internal pull-up resistor to each pin in the bank. */

    void (* ConfigureAsWakeupSource)(LTDeviceUnit hPinBank, bool bEnableWakeup, bool bActiveHigh, LTDevicePower_WakeupReason reason);
        /**< API does nothing on platforms which do not sleep
         *   otherwise will set pin bank represented by hPinBank as a wake up source
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @param[in] bEnableWakeup  \a true enable wakeup from sleep
         *                             \a false disable wakeup from sleep
         *   @param[in] bActiveHigh    \a true if the interrupt input is active high
         *   @param[in] reason         The wakeup reason associated with the pin bank */

    void (* ConfigureRebootHold)(LTDeviceUnit hPinBank, bool bRebootHold);
        /**< Configures bidirectional bank to hold on to the pin level through the next reboot,
         *   or clears that setting and reverts the pin bank to the normal operation.
         *   All pins in the bank must be currently set to output mode, otherwise a call to this
         *   API is a NOP.
         *   @param[in] hPinBank    The Device Unit handle of the pin bank.
         *   @param[in] bRebootHold \a true for entering the hold mode,
         *                          \a false to exit the mode
         *
         *   After a reboot, all pin banks automatically exit the hold mode */

    void (* Set)(LTDeviceUnit hPinBank, u32 pinBits);
        /**< Drives pins in the pin bank high or low, according to the value in pinBits.
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @param[in] pinBits Value for the pins, LSB justified (e.g.: a bank of five pins uses LSB bits 0-4),
         *              where 0 drives the pin low and 1 drives the pin high. Unused bits are ignored. */

    u32  (* Read)(LTDeviceUnit hPinBank);
        /**< Returns the current state of the pins in the pin bank (state is current input or last written output).
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @return    The current state of the pins, LSB justified (e.g.: a bank of five pins uses LSB bits 0-4),
         *              where 0 represents a logic-low level on the pin and 1 represents a logic-high level. */

    void (* EnableIRQ)(LTDeviceUnit hPinBank, LTDevicePin_PinConfiguration_Trigger trigger, LTTime tmDebounce, LTDevicePin_IRQCallback * pClientISRCb, void * pClientData);
        /**< Enables interrupt on a pin bank
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @param[in] trigger The event to trigger the interrupt with.
         *      @arg kLTDevicePin_PinConfiguration_Trigger_RisingEdge
         *                 Causes the interrupt to be triggered when pin transitions from logic-low to logic-high.
         *      @arg kLTDevicePin_PinConfiguration_Trigger_FallingEdge
         *                 Causes the interrupt to be triggered when pin transitions from logic-high to logic-low.
         *      @arg kLTDevicePin_PinConfiguration_Trigger_BothEdges
         *                 Causes the interrupt to be triggered when pin transitions from either logic level to the other.
         *   @param[in] tmDebounce Debounce time, 0 means no debounce
         *   @param[in] pClientISRCb Pointer to user-defined interrupt handler that runs in IRQ context.
         *   @param[in] pClientData Parameter passed to the user-defined interrupt handler */

    void (* DisableIRQ)(LTDeviceUnit hPinBank);
        /**< Disables interrupts on a pin bank
         *   @param[in] hPinBank The Device Unit handle of the pin bank. */

};

TYPEDEF_LTLIBRARY_INTERFACE(ILTDriverPins_InputBank, 1);

/** Library interface for a bank of input pins. */
struct ILTDriverPins_InputBankApi {

    INHERIT_INTERFACE_BASE

    u32  (* GetNumPins)(LTDeviceUnit hPinBank);
        /**< Returns the number of pins in the pin bank.
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @return    The number of pins in the pin bank. */

    void (* ConfigurePullType)(LTDeviceUnit hPinBank, LTDevicePin_PinConfiguration_PullType pullType);
        /**< Sets the pull type of the pins in the pin bank. All pins in the bank are set to the same pull type.
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @param[in] pullType The pull type to set for the pins in the pin bank.
         *      @arg kLTDevicePin_PinConfiguration_PullType_NoPull
         *                 Disconnects any internal pull-up or pull-down from each pin in the bank.
         *      @arg kLTDevicePin_PinConfiguration_PullType_PullDown
         *                 Connects an internal pull-down resistor to each pin in the bank.
         *      @arg kLTDevicePin_PinConfiguration_PullType_PullUp
         *                 Connects an internal pull-up resistor to each pin in the bank. */

    void (* ConfigureAsWakeupSource)(LTDeviceUnit hPinBank, bool bEnableWakeup, bool bActiveHigh, LTDevicePower_WakeupReason reason);
        /**< API does nothing on platforms which do not sleep
         *   otherwise will set pin bank represented by hPinBank as a wake up source
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @param[in] bEnableWakeup  \a true enable wakeup from sleep
         *                             \a false disable wakeup from sleep
         *   @param[in] bActiveHigh    \a true if the interrupt input is active high
         *   @param[in] reason         The wakeup reason associated with the pin bank */

    u32  (* Read)(LTDeviceUnit hPinBank);
        /**< Returns the current input state of the pins in the pin bank.
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @return    The current state of the pins, LSB justified (e.g.: a bank of five pins uses bits 0-4),
         *              where 0 represents a logic-low level on the pin and 1 represents a logic-high level. */

    void (* EnableIRQ)(LTDeviceUnit hPinBank, LTDevicePin_PinConfiguration_Trigger trigger, LTTime tmDebounce, LTDevicePin_IRQCallback * pClientISRCb, void * pClientData);
        /**< Enables interrupt on a pin bank
         *   @param[in] hPinBank The Device Unit handle of the pin bank.
         *   @param[in] trigger The trigger event for the interrupt.
         *      @arg kLTDevicePin_PinConfiguration_Trigger_RisingEdge
         *                 Causes the interrupt to be triggered when pin transitions from logic-low to logic-high.
         *      @arg kLTDevicePin_PinConfiguration_Trigger_FallingEdge
         *                 Causes the interrupt to be triggered when pin transitions from logic-high to logic-low.
         *      @arg kLTDevicePin_PinConfiguration_Trigger_BothEdges
         *                 Causes the interrupt to be triggered when pin transitions from either logic level to the other.
         *   @param[in] tmDebounce Debounce time, 0 means no debounce.
         *   @param[in] pClientISRCb Pointer to user-defined interrupt handler that runs in IRQ context.
         *   @param[in] pClientData Parameter passed to the user-defined interrupt handler */

    void (* DisableIRQ)(LTDeviceUnit hPinBank);
        /**< Disables interrupts on a pin bank
         *   @param[in] hPinBank The Device Unit handle of the pin bank. */

};

#ifndef DOXY_SKIP // [
typedef_LTLIBRARY_INTERFACE(ILTDriverPins, 1) {
    /**< LTDriverPins internal interface, ONLY to be used by LTDevicePins */

    bool (* GetBankNameFromUnitNumber)(u32 nDeviceUnitNumber, char const ** ppPinBankNameToSet);
        /**< Obtains a pointer to the pin bank name.
         *   @param[in]  nDeviceUnitNumber The Device Unit index. Out of range values (>= the return value
         *               of GetNumDeviceUnits()) cause this function to leave *ppPinBankNameToSet
         *               untouched and return false.
         *   @param[out] ppPinBankNameToSet Pointer used to return the address of the pin bank name.
         *   @return     true if *ppPinBankNameToSet was written, false if not. */

    bool (* GetUnitNumberFromBankName)(char const * pPinBankName, u32 * pDeviceUnitNumberToSet);
        /**< Obtains the Device Unit number of the named pin bank.
         *   @param[in]  pPinBankName The name of the bank for which to retrieve The Device Unit number.
         *   @param[out] pDeviceUnitNumberToSet Pointer used to return The Device Unit number.
         *   @return     true and set *pDeviceUnitNumberToSet if a pin bank by the given name exists,
         *               false otherwise. */

    bool (* GetBankTypeFromUnitNumber)(u32 nDeviceUnitNumber, LTDevicePin_PinType * pPinTypeToSet);
        /**< Determines whether a bank is an output, input, bidirectional or invalid pin bank.
         *   @param[in]  nDeviceUnitNumber The Device Unit index. Out of range values (>= the return
         *               value of GetNumDeviceUnits()) cause this function to leave *pPinTypeToSet
         *               untouched and return false.
         *   @param[out] pPinTypeToSet Pointer used to return the pin type.
         *   @return     true and set *pPinTypeToSet for valid Device Unit Numbers, false otherwise. */

} LTLIBRARY_INTERFACE;
#endif // DOXY_SKIP  ]

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_PINS_LTDEVICEPINS_H

/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  07-Feb-21   constantine created
 *  14-Aug-23   commodus    added ConfigureRebootHold
 */
