/******************************************************************************
 * Esp32_Console.h                                                    ESP32 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The transport behind lt_consoleprint() and the shell's keyboard.
 *
 * There is one of these per chip because the two parts this BSP builds for do
 * not reach their consoles the same way: the esp32 talks to UART0, and the
 * esp32s3 talks to the USB serial/JTAG device.  Everything else in
 * Esp32_LTCoreBSP.c is common, so the transport is what moved out rather than
 * the file that uses it.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32_CONSOLE_H
#define PLATFORMS_ESP32_INCLUDE_ESP32_CONSOLE_H

#include <lt/core/bsp/LTCoreBSP.h>

/*
 * Bring the console up and install its receive interrupt.  Called once, from
 * LTCoreBSP_Initialize(), which owns pCallbacks for the life of the system.
 */
void Esp32_ConsoleInitialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks);

/*
 * Write nChars unbuffered, and do not return until they are on their way out.
 * Safe to call from an ISR, and safe to call before Esp32_ConsoleInitialize().
 */
void LT_ISR_SAFE Esp32_ConsolePutChars(const char * pChars, u32 nChars);

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32_CONSOLE_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jul-26   claudius    created
 */
