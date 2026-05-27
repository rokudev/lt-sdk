/***********************************************************************************************************************
 * <platforms/common/source/common/driver/lp55231/CommonDriverLP55231LEDImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * LT Driver Library for LP55231 LED controller
 **********************************************************************************************************************/
/** @file CommonDriverLP55231LEDImpl.c Implementation of LED control Driver for LP55231 */

#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/led/LTDeviceLED.h>
#include <lt/device/i2c/LTDeviceI2C.h>
#include "LTChipLP55231.h"

#define CHIP_NAME          LP55231
#define IMPORTED_LIBRARIES (LTDeviceI2C)

/***********************************************************************************************************************
 * LED Group instance and LED instance data.                                                                          */

 #define LED_CONTROLLER_DECLARE(Name, I2Cport, I2CAddress) \
    static LTChipLP55231LEDConfig s_LEDConfigs##Name[]; \
    static LTChipLP55231Config s_ChipConfig##Name = { \
        .nI2CAddress      = I2CAddress, \
        .pLEDConfigs      = s_LEDConfigs##Name, \
        .nNumLEDs         = kNumLEDs##Name, \
        .nI2CUnitNum      = I2Cport, \
    }; \
    static LEDGroupInstance s_LEDGroupInstance##Name = { \
        .groupDescriptor = { \
            .m_pGroupName        = #Name, \
            .m_groupType         = kLTDeviceLED_GroupType_IndicatorLamp, \
            .m_nNumElements      = kNumLEDs##Name, \
        }, \
        .pChipConfig = &s_ChipConfig##Name, \
    };

#define LED_INSTANCE_LIST_START(Name) \
    static LTChipLP55231LEDConfig s_LEDConfigs##Name[] = {
#define LED_INSTANCE(fader, xxChannel, yyChannel, zzChannel) \
        { \
            .nFaderIndex = fader, \
            .nChannelMap = { xxChannel, yyChannel, zzChannel }, \
        },
#define LED_INSTANCE_LIST_END(Name) \
    }; \
    enum { kNumLEDs##Name = sizeof(s_LEDConfigs##Name) / sizeof(s_LEDConfigs##Name[0]) };

/* Platform defined variables: Controller name, I2C port num, channel mapping */
#define LED_CONTROLLER_LIST_START() \
    static LEDGroupInstance *s_LEDGroupInstance[] = {
#define LED_CONTROLLER_REGISTER(Name) \
        &s_LEDGroupInstance##Name,
#define LED_CONTROLLER_LIST_END() \
    };

#include "../led/CommonDriverLED_IndicatorLamp.c"

#undef LED_CONTROLLER_DECLARE
#undef LED_INSTANCE_LIST_START
#undef LED_INSTANCE
#undef LED_INSTANCE_LIST_END
#undef LED_CONTROLLER_LIST_START
#undef LED_CONTROLLER_REGISTER
#undef LED_CONTROLLER_LIST_END

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Oct-21   trajan      created
 */
