/******************************************************************************
 * Esp32_Clock.c                                                   ESP32-P4 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>

#include "Esp32_SoC.h"
#include "Esp32_Registers.h"
#include "Esp32_Clock.h"

/*
 * As on the other variants, nothing here changes a clock frequency; this file
 * only reports what the ROM and the second stage bootloader left running.
 *
 * On this part the bootstrap is the bootloader's: its rtc_clk.c calibrates the
 * CPLL and takes the CPU to CPU_CLK_FREQ_MHZ_BTLD.  Raising the part beyond
 * that needs the efuse calibrated regulator bias this tree's efuse HAL does not
 * expose, so it is left alone here.
 */

/* Reports the CPU frequency in MHz.  PROVIDEd by
 * mastering/ld/esp32p4/rom/esp32p4.rom.ld. */
u32 ets_get_cpu_frequency(void);

/* return configured clock speed in MHz */
u32 Esp32_ClockGetMHz(void) {
    /*
     * The ROM getter rather than HP_CLKRST_CPU_SRC_FREQ0: IDF's esp_rom_caps.h
     * sets ESP_ROM_GET_CLK_FREQ for this part, which is its own statement that
     * the ROM entry is how the CPU rate is read here, and the vendored
     * bootloader's rtc_clk_apb_freq_get() already depends on it.
     */
    return ets_get_cpu_frequency();
}

/* Initialize clocks */
u32 ESP32_MEM_REGION(IRAM) Esp32_ClockInitialize(void) {
    return Esp32_ClockGetMHz();
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Sep-26   claudius    created
 *  01-Oct-26   claudius    the bootloader now brings up the CPLL
 */
