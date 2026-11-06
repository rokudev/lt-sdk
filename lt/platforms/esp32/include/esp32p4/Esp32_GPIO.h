/******************************************************************************
 * Esp32_GPIO.h                                                    ESP32-P4 BSP
 *
 * - Provides configuration and interrupt support for GPIO pins
 * - Supports configuring pins through the GPIO matrix
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The esp32p4 counterpart of include/esp32c3/Esp32_GPIO.h.  The call signatures
 * are unchanged, so a driver written against either part reads the same, but
 * what lies under them differs:
 *
 *   - There are 55 pads, GPIO0 through GPIO54, with no gaps, so
 *     Esp32GPIO_IsValidPin() still has only the upper bound to check.  They do
 *     not fit in one register, though: every level, direction and status
 *     register in the implementation has a second bank for pads 32 and up.
 *   - GPIO37 and GPIO38 are the console UART's pads and GPIO35 is the BOOT
 *     button.  They are ordinary pads as far as this interface is concerned.
 *   - Pads 0 through 15 are also LP pads, which is visible only in
 *     Esp32GPIO_ConfigPinHold() - the hold bit for those lives in LP_IOMUX and
 *     the rest in HP_SYSTEM.  Pull configuration is the HP IO_MUX bits for
 *     every pad, as on the esp32c3.
 *   - The IO_MUX function that hands a pad to the GPIO block is 1, as on every
 *     other variant.
 *   - The part has four GPIO interrupt outputs.  Only the first is routed, so
 *     a pad's interrupt enable is a single bit as it is elsewhere.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32P4_GPIO_H
#define PLATFORMS_ESP32_INCLUDE_ESP32P4_GPIO_H

/******************************************************************************
 * consts
 *****************************************************************************/
enum {
    kEsp32GPIO_NumPins                 = 55,
    kEsp32GPIO_NumFunctions            = 5,     // IO_MUX function selects per pad
};

/******************************************************************************
 * Typedefs
 *****************************************************************************/

/*
 * Signal direction
 */
typedef u8 Esp32GPIO_Direction;
enum Esp32GPIO_Direction {
    kEsp32GPIO_Direction_Input          = 0,
    kEsp32GPIO_Direction_Output         = 1,
};

/*
 * Pull direction
 */
typedef u8 Esp32GPIO_PullType;
enum Esp32GPIO_PullType {
    kEsp32GPIO_PullNone                 = 0,
    kEsp32GPIO_PullUp                   = 1,
    kEsp32GPIO_PullDown                 = 2,
};

/*
 * Output type
 */
typedef u8 Esp32GPIO_OutputType;
enum Esp32GPIO_OutputType {
    kEsp32GPIO_OutputType_PushPull      = 0,
    kEsp32GPIO_OutputType_OpenDrain     = 1,
};

/*
 * IO_MUX functions
 *
 * MCU_SEL is three bits wide and this part uses 0 through 4 - two more than the
 * esp32c3 - though not every pad defines all five.
 */
typedef u8 Esp32GPIO_Function;
enum Esp32GPIO_Function {
    kEsp32GPIO_Function_0               = 0,
    kEsp32GPIO_Function_1               = 1,
    kEsp32GPIO_Function_GPIO            = 1,    // all pads use function 1 for GPIO
    kEsp32GPIO_Function_2               = 2,
    kEsp32GPIO_Function_3               = 3,
    kEsp32GPIO_Function_4               = 4,
};

/*
 * Interrupt trigger
 */
typedef u8 Esp32GPIO_Trigger;
enum Esp32GPIO_Trigger {
    kEsp32GPIO_Trigger_Disabled         = 0,
    kEsp32GPIO_Trigger_Rising           = 1,
    kEsp32GPIO_Trigger_Falling          = 2,
    kEsp32GPIO_Trigger_Both             = 3,
    kEsp32GPIO_Trigger_LowLevel         = 4,
    kEsp32GPIO_Trigger_HighLevel        = 5,
};

/*
 * Pad configuration read back from the IO_MUX and GPIO registers
 */
typedef struct {
    Esp32GPIO_Direction direction;      // is the output driver enabled?
    Esp32GPIO_PullType  pull;
    Esp32GPIO_Function  func;           // IO_MUX function select
    bool                bInputEnabled;  // IO_MUX FUN_IE
} Esp32GPIO_PinConfig;

typedef void (Esp32_IRQCallback)(u8 nPin, bool bPinHigh, void *pClientData);

/******************************************************************************
 * See implementation for more details
 *****************************************************************************/
bool Esp32GPIO_IsValidPin(u8 nPin);
bool Esp32GPIO_ConfigPin(u8 nPin,
                         Esp32GPIO_Direction direction,
                         Esp32GPIO_PullType pull,
                         Esp32GPIO_Function func);
void Esp32GPIO_ConfigOutputType(u8 nPin, Esp32GPIO_OutputType outputType);
/*
 * Narrow read-modify-write accessors for the IO_MUX pad fields that
 * Esp32GPIO_ConfigPin() would otherwise overwrite wholesale.  These exist for
 * pads that are driven by a peripheral rather than by the GPIO block - the
 * flash pads, for one - where the rest of ConfigPin's work (output enable, pull
 * configuration, defaulting the drive strength to 2) is either wrong or already
 * done by the peripheral.
 */
void Esp32GPIO_ConfigPinFunction(u8 nPin, Esp32GPIO_Function func);
void Esp32GPIO_ConfigPinDriveStrength(u8 nPin, u8 nDriveStrength);
void Esp32GPIO_ConfigPinPull(u8 nPin, Esp32GPIO_PullType pull);
void Esp32GPIO_ConfigPinInputEnable(u8 nPin, bool bEnable);
void Esp32GPIO_ConfigMatrixPin(u8 nPin, u8 nSignal, Esp32GPIO_Direction direction, bool bInv);
/*
 * Pad hold.  Honoured across a reset, but not across a deep sleep wake for pads
 * 16 and up - the register holding those bits is in a power domain that does
 * not survive it, which is why IDF calls the feature unusable on this part.
 */
void Esp32GPIO_ConfigPinHold(u8 nPin, bool bPinHold);
void Esp32GPIO_ClearAllPinHolds(void);
bool Esp32GPIO_AttachISR(u8 nPin, Esp32GPIO_Trigger trigger, Esp32_IRQCallback *pISR, void *pClientData);
void Esp32GPIO_DetachISR(u8 nPin);
bool Esp32GPIO_ReadPin(u8 nPin);
void Esp32GPIO_WritePin(u8 nPin, bool val);
void Esp32GPIO_ClearPinConfig(u8 nPin);
void Esp32GPIO_ClearPendingIRQ(u8 nPin);
bool Esp32GPIO_GetPinConfig(u8 nPin, Esp32GPIO_PinConfig *pConfig);

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32P4_GPIO_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  22-Sep-26   claudius    created
 */
