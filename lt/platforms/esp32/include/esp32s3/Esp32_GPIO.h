/******************************************************************************
 * Esp32_GPIO.h                                                    ESP32-S3 BSP
 *
 * - Provides configuration and interrupt support for GPIO pins
 * - Supports configuring pins through the GPIO matrix
 * - Does not support RTC_GPIO
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The esp32s3 counterpart of include/esp32/Esp32_GPIO.h.  The call signatures are
 * the esp32's, so a driver written against one reads the same against the other,
 * but three things a caller can see have changed:
 *
 *   - There are 49 pads rather than 40, and GPIO22 through GPIO25 are not bonded
 *     out on any package.  Esp32GPIO_IsValidPin() names the gap; every entry
 *     point rejects those four the same way it rejects a pad past the end.
 *   - The IO_MUX function that hands a pad to the GPIO block is 1, not 2.
 *   - Pad hold covers GPIO0 through GPIO48 rather than stopping at GPIO23.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_GPIO_H
#define PLATFORMS_ESP32_INCLUDE_ESP32S3_GPIO_H

/******************************************************************************
 * consts
 *****************************************************************************/
enum {
    kEsp32GPIO_NumPins                 = 49,
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
 * Narrow read-modify-write accessors for the two IO_MUX pad fields that
 * Esp32GPIO_ConfigPin() would otherwise overwrite wholesale.  These exist for
 * pads that are driven by a peripheral rather than by the GPIO block - the
 * flash and PSRAM pads, for one - where the rest of ConfigPin's work (output
 * enable, pull configuration, defaulting the drive strength to 2) is either
 * wrong or already done by the peripheral.
 */
void Esp32GPIO_ConfigPinFunction(u8 nPin, Esp32GPIO_Function func);
void Esp32GPIO_ConfigPinDriveStrength(u8 nPin, u8 nDriveStrength);
void Esp32GPIO_ConfigMatrixPin(u8 nPin, u8 nSignal, Esp32GPIO_Direction direction, bool bInv);
void Esp32GPIO_ConfigPinHold(u8 nPin, bool bPinHold);
void Esp32GPIO_ClearAllPinHolds(void);
bool Esp32GPIO_AttachISR(u8 nPin, Esp32GPIO_Trigger trigger, Esp32_IRQCallback *pISR, void *pClientData);
void Esp32GPIO_DetachISR(u8 nPin);
bool Esp32GPIO_ReadPin(u8 nPin);
void Esp32GPIO_WritePin(u8 nPin, bool val);
void Esp32GPIO_ClearPinConfig(u8 nPin);

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_GPIO_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  29-Jul-26   claudius    created
 */
