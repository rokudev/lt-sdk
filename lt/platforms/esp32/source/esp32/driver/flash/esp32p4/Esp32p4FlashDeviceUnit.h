/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/esp32p4/Esp32p4FlashDeviceUnit.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32P4FLASHDEVICEUNIT_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32P4FLASHDEVICEUNIT_H

#include <lt/LTTypes.h>

/*_______________________________________________________________________
 / Esp32p4FlashDeviceUnit initialization and Handle Creation function */
bool Esp32p4FlashDeviceUnit_Initialize(void);
LTDeviceUnit Esp32p4FlashDeviceUnit_CreateHandle(void);
void Esp32p4FlashDeviceUnit_Finalize(void);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Sep-26   claudius    created, from the esp32c3 driver
 */

#endif /* PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_FLASH_ESP32P4FLASHDEVICEUNIT_H */
