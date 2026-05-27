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

void HalInit(void);

int main(void) {
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
 *  22-Dec-21   tiberius    created
 */
