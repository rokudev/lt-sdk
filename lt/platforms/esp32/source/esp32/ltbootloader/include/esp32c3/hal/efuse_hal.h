/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "soc/soc_caps.h"
#include "hal/efuse_ll.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upstream reaches the generic hal/efuse_hal.h - the one that declares
   efuse_hal_chip_revision() - through an #include_next off the component include
   path, which puts the chip header first.  This tree searches include/ before
   include/<chip>/, so the generic declarations are named here instead.  See the
   same flattening in esp_efuse.h. */
uint32_t efuse_hal_chip_revision(void);
bool efuse_hal_flash_encryption_enabled(void);
uint32_t efuse_hal_get_major_chip_version(void);
uint32_t efuse_hal_get_minor_chip_version(void);

/**
 * @brief set eFuse timings
 *
 * @param apb_freq_hz APB frequency in Hz
 */
void efuse_hal_set_timing(uint32_t apb_freq_hz);

#ifdef __cplusplus
}
#endif
