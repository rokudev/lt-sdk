/***********************************************************************************************************************
 * <platforms/common/source/common/driver/sk6805led/CommonDriverSK6805LEDImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * LT Driver Library for SK6805 LED controller
 **********************************************************************************************************************/
/** @file CommonDriverSK6805LEDImpl.c Implementation of LED control Driver for SK6805 */

#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/led/LTDeviceLED.h>
#include <lt/device/spi/LTDeviceSPI.h>
#include "LTChipSK6805.h"

#define CHIP_NAME          SK6805
#define IMPORTED_LIBRARIES (LTDeviceSPI)

/***********************************************************************************************************************
 * LED Group instance and LED instance data.                                                                          */

#define LED_CONTROLLER_DECLARE(Name, SPIport, NumLEDs) \
    static LTChipSK6805Config s_ChipConfig##Name = { \
        .nNumLEDs         = NumLEDs, \
        .nSPIUnitNum      = SPIport, \
    }; \
    static LEDGroupInstance s_LEDGroupInstance##Name = { \
        .groupDescriptor = { \
            .m_pGroupName        = #Name, \
            .m_groupType         = kLTDeviceLED_GroupType_IndicatorLamp, \
            .m_nNumElements      = NumLEDs, \
        }, \
        .pChipConfig = &s_ChipConfig##Name, \
    };

/* Platform defined variables: Controller name, SPI port num, channel mapping */
#define LED_CONTROLLER_LIST_START() \
    static LEDGroupInstance *s_LEDGroupInstance[] = {
#define LED_CONTROLLER_REGISTER(Name) \
        &s_LEDGroupInstance##Name,
#define LED_CONTROLLER_LIST_END() \
    };

#include "../led/CommonDriverLED_IndicatorLamp.c"

#undef LED_CONTROLLER_DECLARE
#undef LED_CONTROLLER_LIST_START
#undef LED_CONTROLLER_REGISTER
#undef LED_CONTROLLER_LIST_END

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  29-Oct-21   trajan      created
 */
