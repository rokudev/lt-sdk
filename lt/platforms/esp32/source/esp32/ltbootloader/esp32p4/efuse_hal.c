/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reduced to the three entries the shared sources need, as the esp32c3 copy is.
 * The upstream esp32p4 file also carries efuse_hal_read/program/rs_calculate/
 * clear_program_registers and efuse_hal_is_coding_error_in_block, which exist
 * only for the v5.4 esp_efuse_utility.c this tree does not use.
 */

#include "sdkconfig.h"
#include <sys/param.h>
#include "soc/soc_caps.h"
#include "hal/assert.h"
#include "hal/efuse_hal.h"
#include "hal/efuse_ll.h"
#include "esp_attr.h"

IRAM_ATTR uint32_t efuse_hal_get_major_chip_version(void)
{
    return efuse_ll_get_chip_wafer_version_major();
}

IRAM_ATTR uint32_t efuse_hal_get_minor_chip_version(void)
{
    return efuse_ll_get_chip_wafer_version_minor();
}

/******************* eFuse control functions *************************/

void efuse_hal_set_timing(uint32_t apb_freq_hz)
{
    (void) apb_freq_hz;
    /* The efuse clock is fixed on this chip - keep the reset timing settings. */
}
