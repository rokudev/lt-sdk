/*******************************************************************************
 * <platforms/common/source/common/driver/lp55231/LTChipIS31FL3208A.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Control of the Lumissil IS31FL3208A LED driver IC.
 *
 * Support for communication with a Lumissil IS31FL3208A 18-channel LED driver IC.
 * It includes data values, initialization and communication, and depends
 * on the calling LT Library to provide the actual means of communication.
 * Communication with the IS31FL3208A is via I2C.  The configuration and operation
 * of the I2C port are typically platform-specific, and therefore the calling library
 * is typically <platform>DriverLED. LTDeviceLED operates the Driver Library as an
 * Indicator Lamp LED Group.
 *
 * The IS31FL3208A provides 18 intensity-controllable LED driver channels.
 *
 * For more information, please see the IS31FL3208A datasheet.
 ******************************************************************************/

#ifndef PLATFORMS_COMMON_SOURCE_COMMON_DRIVER_LED_LTCHIPIS31FL3208A_H
#define PLATFORMS_COMMON_SOURCE_COMMON_DRIVER_LED_LTCHIPIS31FL3208A_H

#include <lt/LTTypes.h>
#include <lt/device/i2c/LTDeviceI2C.h>
#include <lt/device/pins/LTDevicePins.h>
LT_EXTERN_C_BEGIN

/***************************************************************************************************
   Data structure for LED configuration.
   This data structure is not meant to be manipulated outside LTChipIS31FL3208AImpl, but appears
   here so that the calling LT Library can allocate memory for the LED configs however it wants.  */

/*  The 18 LED driver channels can be mapped to logical LEDs in any way that the designer desires.
    For instance, these 18 channels could be used to drive 6 RGB LEDs, 18 single-color LEDs, 9
    dual-color LEDs, or any combination of these. This data structure is used to specify the mapping
    of up to three channels of color and one channel of intensity information to LED driver channels.
    One LTChipIS31FL3208ALEDConfig structure should be instantiated for each logical LED driven
    by the IC.

    This module represents LED colors using a 32-bit value whose bits map to intensity and color
    components as: IIXXYYZZ. These components are mapped to IS31FL3208A channels as follows:

        channel[config.nChannelMap[0]] = XX
        channel[config.nChannelMap[1]] = YY
        channel[config.nChannelMap[2]] = ZZ

    If any channel config is set to 0, then the corresponding color component is not
    mapped to a channel.

    Examples:

        - For an RGB LED connected to the IS31FL3208A with red on channel 1, green on channel 2, blue on
          channel 3, the following configuration would be used to support
          a color format of xxRRGGBB:

            config.nChannelMap[0] = 1
            config.nChannelMap[1] = 2
            config.nChannelMap[2] = 3

        - For a red/blue LED connected to the IS31FL3208A with red on channel 4, blue on channel 5,
          the following configuration would be used to support a color format of xxRRxxBB:

            config.nChannelMap[0] = 4
            config.nChannelMap[1] = 0
            config.nChannelMap[2] = 5

        - For a white LED connected to the IS31FL3208A on channel 6 the following configuration
          would be used to support a color format of xxxxxxWW:

            config.nChannelMap[0] = 0
            config.nChannelMap[1] = 0
            config.nChannelMap[2] = 6
   */

enum { kLTChipIS31FL3208A_NumColorComponents = 3 };

typedef struct LTChipIS31FL3208ALEDConfig {
    u8 nChannelMap[kLTChipIS31FL3208A_NumColorComponents]; /**< Determines which channel on the IS31FL3208A maps to each
                                                            color component for this LED. Valid values are 1-9 or
                                                            zero if no channel is mapped to a given color component. */
} LTChipIS31FL3208ALEDConfig;

typedef struct LTChipIS31FL3208AConfig {
    LTChipIS31FL3208ALEDConfig * pLEDConfigs;   /**< Channel map configuration for each LED attached to the controller */
    u8                           nNumLEDs;      /**< Number of LEDs attached to this controller */
    u32                          nI2CUnitNum;   /**< I2C device unit number to use to talk to the controller */
    u8                           nI2CAddress;   /**< I2C address of the controller */
    const char *                 pResetPinName; /**< Name of the reset pin to used to enable/disable the controller */
} LTChipIS31FL3208AConfig;

/***************************************************************************************************
 * Data structure for chip config                                                                 */

typedef struct LTChipIS31FL3208A LTChipIS31FL3208A;

/***************************************************************************************************
 * Support functions                                                                              */

LTChipIS31FL3208A * LTChipIS31FL3208A_Initialize(LTDeviceI2C * pDeviceI2C, LTDevicePins * pDevicePins, LTChipIS31FL3208AConfig const * pConfig);
    /**< Initializes the IS31FL3208A IC.
         @param pChip pointer to the chip config struct.
         @note %LTChipIS31FL3208A_Initialize() must be called before any other function in the
               LTChipIS31FL3208A interface.
         @return true if the IS31FL3208A device was successfully initialized, and is ready for use,
                 false otherwise.  Failure to initialize is typically caused by bad arguments;
                 zeroes for pointer values in the config struct, or out-of-range values. */

void LTChipIS31FL3208A_Finalize(LTChipIS31FL3208A * pChip);
    /**< Finalizes the IS31FL3208A IC.
         @param pChip pointer to the chip struct. */

u32 LTChipIS31FL3208A_GetColor(LTChipIS31FL3208A * pChip, u8 nLEDIndex);
    /**< Gets the color of the given LED from the frame buffer. This might not
         correspond to the actual color of the LED if LTChipSK6805_Refresh() has
         not been called since the last LTChipSK6805_SetColor().
         @param pChip pointer to the chip config struct.
         @param nLEDIndex the index of the LED for which to return color info.
         @return color data for the LED in xxRRGGBB format. */

void LTChipIS31FL3208A_SetColor(LTChipIS31FL3208A * pChip, u8 nLEDIndex, u32 nColor);
    /**< Sets the color of the given LED. LTChipIS31FL3208A_Refresh must be called to
         apply any color changes that were made (allows for batch color updates).
         @param pChip pointer to the chip config struct.
         @param nLEDIndex the index of the LED to be updated.
         @param nColor color data for the LED in the format specified in this LED's
                       LTChipIS31FL3208ALEDConfig struct. */

void LTChipIS31FL3208A_Refresh(LTChipIS31FL3208A * pChip);
    /**< Applies updates to LED colors made using LTChipIS31FL3208A_SetColor.
         @param pChip pointer to the chip config struct. */

LT_EXTERN_C_END
#endif // #ifndef PLATFORMS_COMMON_SOURCE_COMMON_DRIVER_LED_LTCHIPIS31FL3208A_H
/******************************************************************************
 *  LOG
 ******************************************************************************
 *  28-Oct-21   trajan      created
 */
