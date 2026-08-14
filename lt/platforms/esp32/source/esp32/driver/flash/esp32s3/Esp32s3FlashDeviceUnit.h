/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/esp32s3/Esp32s3FlashDeviceUnit.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32S3FLASHDEVICEUNIT_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32S3FLASHDEVICEUNIT_H

#include <lt/LTTypes.h>

/*_______________________________________________________________________
 / Esp32s3FlashDeviceUnit initialization and Handle Creation function */
bool Esp32s3FlashDeviceUnit_Initialize(void);
LTDeviceUnit Esp32s3FlashDeviceUnit_CreateHandle(void);
void Esp32s3FlashDeviceUnit_Finalize(void);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jul-26   claudius    created
 */

#endif /* PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32S3FLASHDEVICEUNIT_H */
