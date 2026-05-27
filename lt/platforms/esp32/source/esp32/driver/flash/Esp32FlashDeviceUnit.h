/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/Esp32FlashDeviceUnit.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32FLASHDEVICEUNIT_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32FLASHDEVICEUNIT_H

#include <lt/LTTypes.h>

/*_______________________________________________________________________
 / Esp32FlashDeviceUnit initialization and Handle Creation function */
bool Esp32FlashDeviceUnit_Initialize(void);
LTDeviceUnit Esp32FlashDeviceUnit_CreateHandle(void);
void Esp32FlashDeviceUnit_Finalize(void);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-May-22   vitellius   created
 */

#endif /* PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32FLASHDEVICEUNIT_H */
