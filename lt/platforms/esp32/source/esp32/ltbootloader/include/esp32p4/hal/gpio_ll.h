/******************************************************************************
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
//
// The GPIO low level layer for the esp32p4, reduced to what the bootloader
// reaches for.
//
// IDF's esp32p4 gpio_ll.h is 815 lines against seven v5.4 register-struct
// headers (io_mux_struct.h, hp_system_struct.h, lp_iomux_struct.h,
// hp_sys_clkrst_struct.h, pmu_struct.h, usb_serial_jtag_struct.h,
// usb_wrap_struct.h).  Nothing in this library calls any of it: bootloader
// _common.c and bootloader_common_loader.c include the header without naming a
// single gpio_ll_ entry point, and bootloader_console.c's only use is inside
// CONFIG_ESP_CONSOLE_UART_CUSTOM, which is off here.  So only the one entry
// point that use would need is provided, written against the _REG macros as the
// esp32c3 header does.  Application GPIO on this part goes through
// Esp32_GPIO.c, not this file.
//
#pragma once

#include <stdint.h>
#include "soc/soc.h"
#include "soc/gpio_periph.h"
#include "soc/gpio_struct.h"
#include "soc/io_mux_reg.h"
#include "soc/usb_serial_jtag_reg.h"
#include "soc/usb_wrap_reg.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GPIO_LL_GET_HW(num) (((num) == 0) ? (&GPIO) : NULL)

/**
  * @brief  Select a function for the pin in the IOMUX.
  *
  * @param  pin_name IO_MUX register address of the pad
  * @param  func     IOMUX function number
  */
static inline __attribute__((always_inline)) void gpio_ll_iomux_func_sel(uint32_t pin_name, uint32_t func)
{
    /* Two internal USB PHYs share these four pads - PHY0 with the serial/JTAG
       block, PHY1 with USB OTG.  Claiming a pad for the IOMUX means releasing
       the PHY that owns it first. */
    if (pin_name == IO_MUX_GPIO24_REG || pin_name == IO_MUX_GPIO25_REG) {
        CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    } else if (pin_name == IO_MUX_GPIO26_REG || pin_name == IO_MUX_GPIO27_REG) {
        CLEAR_PERI_REG_MASK(USB_WRAP_OTG_CONF_REG, USB_WRAP_USB_PAD_ENABLE);
    }
    PIN_FUNC_SELECT(pin_name, func);
}

#ifdef __cplusplus
}
#endif
