/*
 * SPDX-FileCopyrightText: 2020-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>

#include "hal/cpu_hal.h"
#include "bootloader_mem.h"
#include "soc/cpu.h"

void bootloader_init_mem(void)
{
    /* MEMCTL controls L1 cache */
#if XCHAL_ERRATUM_572
    uint32_t memctl = XCHAL_CACHE_MEMCTL_DEFAULT;
    WSR(MEMCTL, memctl);
#endif // XCHAL_ERRATUM_572

    // protect memory region
    esp_cpu_configure_region_protection();
}
