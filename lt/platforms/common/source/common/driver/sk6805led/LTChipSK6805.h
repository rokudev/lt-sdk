/*******************************************************************************
 * <platforms/common/source/common/driver/sk6805led/LTChipSK6805.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Control of the Shenzhen Normand Electronic Co. SK6805 Serial LED.
 *
 * Support for communication with a chain of SK6805 serial LEDs.
 * It includes data values, initialization and communication, and depends
 * on the calling LT Library to provide the actual means of communication.
 * Communication with the SK6805 is via SPI. The configuration and operation
 * of the SPI port are typically platform-specific, and therefore the calling
 * library is typically <platform>DriverLED. LTDeviceLED operates the Driver
 * Library as an Indicator Lamp LED Group.
 *
 * For more information, please see the SK6805 datasheet.
 ******************************************************************************/

#ifndef PLATFORMS_COMMON_SOURCE_COMMON_DRIVER_LED_LTCHIPSK6805_H
#define PLATFORMS_COMMON_SOURCE_COMMON_DRIVER_LED_LTCHIPSK6805_H

#include <lt/LTTypes.h>
#include <lt/device/spi/LTDeviceSPI.h>
LT_EXTERN_C_BEGIN

/***************************************************************************************************
   Data structure for chip configuration.
   This data structure is not meant to be manipulated outside LTChipSK6805Impl, but appears
   here so that the calling LT Library can allocate memory for the LED configs however it wants.  */

typedef struct LTChipSK6805Config {
    u8                          nNumLEDs;      /**< Number of LEDs in this group */
    u32                         nSPIUnitNum;   /**< SPI device unit number to use to talk to the controller */
} LTChipSK6805Config;

/***************************************************************************************************
 * Data structure for chip config                                                                 */

typedef struct LTChipSK6805 LTChipSK6805;

/***************************************************************************************************
 * Support functions                                                                              */

LTChipSK6805 * LTChipSK6805_Initialize(LTDeviceSPI * pDeviceSPI, LTChipSK6805Config const * pConfig);
    /**< Initializes the SK6805 IC.
         @param pChip pointer to the chip config struct.
         @note %LTChipSK6805_Initialize() must be called before any other function in the
               LTChipSK6805 interface.
         @return true if the SK6805 device was successfully initialized, and is ready for use,
                 false otherwise.  Failure to initialize is typically caused by bad arguments;
                 zeroes for pointer values in the config struct, or out-of-range values. */

void LTChipSK6805_Finalize(LTChipSK6805 * pChip);
    /**< Finalizes the SK6805 IC.
         @param pChip pointer to the chip struct. */

u32 LTChipSK6805_GetColor(LTChipSK6805 * pChip, u8 nLEDIndex);
    /**< Gets the color of the given LED from the frame buffer. This might not
         correspond to the actual color of the LED if LTChipSK6805_Refresh() has
         not been called since the last LTChipSK6805_SetColor().
         @param pChip pointer to the chip config struct.
         @param nLEDIndex the index of the LED for which to return color info.
         @return color data for the LED in xxRRGGBB format. */

void LTChipSK6805_SetColor(LTChipSK6805 * pChip, u8 nLEDIndex, u32 nColor);
    /**< Sets the color of the given LED in the frame buffer. LTChipSK6805_Refresh() must be
         called to send the updated color data to the LEDs.
         @param pChip pointer to the chip config struct.
         @param nLEDIndex the index of the LED to be updated.
         @param nColor color data for the LED in xxRRGGBB format. */

void LTChipSK6805_Refresh(LTChipSK6805 * pChip);
    /**< Sends the current frame buffer (updated using LTChipSK6805_SetColor) to the chain of LEDs.
         @param pChip pointer to the chip config struct. */

LT_EXTERN_C_END
#endif // #ifndef PLATFORMS_COMMON_SOURCE_COMMON_DRIVER_LED_LTCHIPSK6805_H
/******************************************************************************
 *  LOG
 ******************************************************************************
 *  29-Oct-21   trajan      created
 */
