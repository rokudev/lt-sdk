/*******************************************************************************
 * lt/platforms/st/source/st/ltcorebsp/STStartup.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include "stm32h7xx_hal.h"


static LTBootReason s_bootReason;
static const char * s_pBootReasonString;

static void ReadBootReason(void) {
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDG1RST)) {
        s_bootReason = kLTBootReason_WatchdogReset;
        s_pBootReasonString = "IWDG";
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDG1RST)) {
        s_bootReason = kLTBootReason_WatchdogReset;
        s_pBootReasonString = "WWDG";
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)) {
        s_bootReason = kLTBootReason_Reset;
        s_pBootReasonString = "SW Reset";
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST)) {
        s_bootReason = kLTBootReason_Reset;
        s_pBootReasonString = "Brown Out";
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST)) {
        s_bootReason = kLTBootReason_PowerOn;
        s_pBootReasonString = "Power On";
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST)) {
        s_bootReason = kLTBootReason_PowerOn;
        s_pBootReasonString = "Pin Reset";
    } else {
        s_bootReason = kLTBootReason_Undefined;
        s_pBootReasonString = "Undefined";
    }
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

LTBootReason STStartup_GetBootReason(const char ** pBootReasonString) {
    if (pBootReasonString) *pBootReasonString = s_pBootReasonString;
    return s_bootReason;
}

void HalInit(void);

int main(void) {

    /* Read the boot reason first, before setting up any of the clocks */
    ReadBootReason();

    /* Initialize STM32 HAL */
    HalInit();

    /* Run LT */
    static const char * argv[] = { "ST", LT_GENESIS_LIBRARY };
    int argc = sizeof(argv) / sizeof(argv[0]);
    LT_Run(argc, argv);

    while (1);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Dec-21   tiberius     created
 */
