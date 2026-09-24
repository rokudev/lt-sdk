/******************************************************************************
 * Esp32_Console.h                                                 ESP32-P4 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * There is one of these per chip because the parts this BSP builds for do not
 * reach their consoles the same way.  On the esp32p4 the console is UART0, on
 * GPIO37 and GPIO38, which the board brings out through a USB-UART bridge - so
 * this is the esp32's console, not the esp32c3's, which talks to the USB
 * serial/JTAG device directly.
 *
 * The practical difference is backpressure.  Output here blocks on a hardware
 * FIFO that always drains, so characters are never dropped and a print from an
 * ISR costs whatever the line rate costs.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32P4_CONSOLE_H
#define PLATFORMS_ESP32_INCLUDE_ESP32P4_CONSOLE_H

#include <lt/LTTypes.h>
#include <lt/core/bsp/LTCoreBSP.h>

/*
 * Bring up UART0 and attach its receive interrupt.  The callbacks are the
 * kernel's, and are called from the ISR as characters arrive.
 *
 * The baud rate and the pad routing are left as the ROM bootloader set them -
 * it has already configured UART0 to print its own messages, and reprogramming
 * the divider here would only break the line for as long as it takes to get the
 * same value back.
 */
void Esp32_ConsoleInitialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks);

/*
 * Write to the console, spinning until the transmit FIFO has room.  Safe from
 * an ISR and from a context with no scheduler - it takes no lock and blocks on
 * nothing but the hardware.
 */
void LT_ISR_SAFE Esp32_ConsolePutChars(const char * pChars, u32 nChars);

#endif /* PLATFORMS_ESP32_INCLUDE_ESP32P4_CONSOLE_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Sep-26   claudius    created
 */
