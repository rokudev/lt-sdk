/*******************************************************************************
 * <platforms/common/source/common/driver/lp55231/LTChipLP55231.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Control of the TI LP55231 LED driver IC.
 *
 * Support for communication with a TI LP55231 9-channel LED driver IC.
 * It includes data values, initialization and communication, and depends
 * on the calling LT Library to provide the actual means of communication.
 * Communication with the LP55231 is via I2C; this can be an actual I2C port or
 * a set of two GPIO pins which implement I2C behavior by bit-banging.  The
 * configuration and operation of the I2C port are typically platform-specific,
 * and therefore the calling library is typically <platform>DriverLED. LTDeviceLED
 * operates the Driver Library as an Indicator Lamp LED Group.
 *
 * The LP55231 provides 9 intensity-controllable LED driver channels. In addition
 * to the per-channel intensity settings, the device supports up to three master
 * fader units which can be used to vary the intensity of groups of LEDs. Using
 * a combination of per-channel intensity and fading, fine control over the color
 * and brightness of LEDs can be achieved.
 *
 * For more information, please see the LP55231 datasheet.
 ******************************************************************************/

#ifndef PLATFORMS_COMMON_SOURCE_COMMON_DRIVER_LED_LTCHIPLP55231_H
#define PLATFORMS_COMMON_SOURCE_COMMON_DRIVER_LED_LTCHIPLP55231_H

#include <lt/LTTypes.h>
#include <lt/device/i2c/LTDeviceI2C.h>
LT_EXTERN_C_BEGIN

/***************************************************************************************************
   Data structure for LED configuration.
   This data structure is not meant to be manipulated outside LTChipLP55231Impl, but appears
   here so that the calling LT Library can allocate memory for the LED configs however it wants.  */

/*  The 9 LED driver channels can be mapped to logical LEDs in any way that the designer desires.
    For instance, these 9 channels could be used to drive 3 RGB LEDs, 9 single-color LEDs, some number
    of dual-color LEDs, or any combination of these. This data structure is used to specify the mapping
    of up to three channels of color and one channel of intensity information to LED driver channels and
    master faders. One LTChipLP55231LEDConfig structure should be instantiated for each logical LED
    driven by the IC.

    This module represents LED colors using a 32-bit value whose bits map to intensity and color
    components as: IIXXYYZZ. These components are mapped to LP55231 channels and fader units as
    follows:

        fader[config.nFaderIndex] = II
        channel[config.nChannelMap[0]] = XX
        channel[config.nChannelMap[1]] = YY
        channel[config.nChannelMap[2]] = ZZ

    If any fader or channel config is set to 0, then the corresponding color component is not
    mapped to a channel or fader.

    Examples:

        - For an RGB LED connected to the LP55231 with red on channel 1, green on channel 2, blue on
          channel 3, all faded by master fader 1, the following configuration would be used to support
          a color format of IIRRGGBB:

            config.nFaderIndex = 1
            config.nChannelMap[0] = 1
            config.nChannelMap[1] = 2
            config.nChannelMap[2] = 3

        - For a red/blue LED connected to the LP55231 with red on channel 4, blue on channel 5,
          both faded by master fader 2, the following configuration would be used to support a
          color format of IIRRxxBB:

            config.nFaderIndex = 2
            config.nChannelMap[0] = 4
            config.nChannelMap[1] = 0
            config.nChannelMap[2] = 5

        - For a white LED connected to the LP55231 on channel 6, and not controlled by a fader,
          the following configuration would be used to support a color format of xxxxxxWW:

            config.nFaderIndex = 0
            config.nChannelMap[0] = 0
            config.nChannelMap[1] = 0
            config.nChannelMap[2] = 6
   */

enum { kLTChipLP55231_NumColorComponents = 3 };

typedef struct LTChipLP55231LEDConfig {
    u8 nFaderIndex;                                    /**< The master fader index which will control the
                                                            brightness of this LED. Valid values are 1-3 or
                                                            zero if no fader should be configured for this LED. */
    u8 nChannelMap[kLTChipLP55231_NumColorComponents]; /**< Determines which channel on the LP55231 maps to each
                                                            color component for this LED. Valid values are 1-9 or
                                                            zero if no channel is mapped to a given color component. */
} LTChipLP55231LEDConfig;

typedef struct LTChipLP55231Config {
    LTChipLP55231LEDConfig     * pLEDConfigs;   /**< Channel map configuration for each LED attached to the controller */
    u8                           nNumLEDs;      /**< Number of LEDs attached to this controller */
    u32                          nI2CUnitNum;   /**< I2C device unit number to use to talk to the controller */
    u8                           nI2CAddress;   /**< I2C address of the controller */
} LTChipLP55231Config;

/***************************************************************************************************
 * Data structure for chip config                                                                 */

typedef struct LTChipLP55231 LTChipLP55231;

/***************************************************************************************************
 * Support functions                                                                              */

LTChipLP55231 * LTChipLP55231_Initialize(LTDeviceI2C * pDeviceI2C, LTChipLP55231Config const * pConfig);
    /**< Initializes the LP55231 IC.
         @param pChip pointer to the chip config struct.
         @note %LTChipLP55231_Initialize() must be called before any other function in the
               LTChipLP55231 interface.
         @return true if the LP55231 device was successfully initialized, and is ready for use,
                 false otherwise.  Failure to initialize is typically caused by bad arguments;
                 zeroes for pointer values in the config struct, or out-of-range values. */

void LTChipLP55231_Finalize(LTChipLP55231 * pChip);
    /**< Finalizes the LP55231 IC.
         @param pChip pointer to the chip struct. */

u32 LTChipLP55231_GetColor(LTChipLP55231 * pChip, u8 nLEDIndex);
    /**< Gets the color of the given LED from the frame buffer. This might not
         correspond to the actual color of the LED if LTChipLP55231_Refresh() has
         not been called since the last LTChipLP55231_SetColor().
         @param pChip pointer to the chip config struct.
         @param nLEDIndex the index of the LED for which to return color info.
         @return color data for the LED in the format specified in this LED's
                 LTChipLP55231LEDConfig struct. */

void LTChipLP55231_SetColor(LTChipLP55231 * pChip, u8 nLEDIndex, u32 nColor);
    /**< Sets the color of the given LED.
         @param pChip pointer to the chip config struct.
         @param nLEDIndex the index of the LED to be updated.
         @param nColor color data for the LED in the format specified in this LED's
                       LTChipLP55231LEDConfig struct. */

void LTChipLP55231_Refresh(LTChipLP55231 * pChip);
    /**< Sends the current frame buffer (updated using LTChipSK6805_SetColor) to the chain of LEDs.
         @param pChip pointer to the chip config struct. */

LT_EXTERN_C_END
#endif // #ifndef PLATFORMS_COMMON_SOURCE_COMMON_DRIVER_LED_LTCHIPLP55231_H
/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Oct-21   trajan      created
 */
