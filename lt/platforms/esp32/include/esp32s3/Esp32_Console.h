/******************************************************************************
 * Esp32_Console.h                                                 ESP32-S3 BSP
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
 *
 * The interface is identical on both parts, but note the one behavioural
 * difference documented on Esp32_ConsolePutChars() below - on this part the
 * console can drop output, and it has to be allowed to.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_CONSOLE_H
#define PLATFORMS_ESP32_INCLUDE_ESP32S3_CONSOLE_H

#include <lt/core/bsp/LTCoreBSP.h>

/*
 * Bring the console up and install its receive interrupt.  Called once, from
 * LTCoreBSP_Initialize(), which owns pCallbacks for the life of the system.
 */
void Esp32_ConsoleInitialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks);

/*
 * Write nChars unbuffered.  Safe to call from an ISR, and safe to call before
 * Esp32_ConsoleInitialize().
 *
 * Unlike the esp32's UART0, the far end of this console is a USB host, and a
 * host that is absent or not reading leaves the endpoint permanently full.
 * Output is therefore dropped rather than waited on once it is clear nothing is
 * collecting it; see the implementation.
 *
 * Called before Esp32_ConsoleInitialize() this also discards anything the host
 * has sent, since until the receive interrupt exists nothing else will, and an
 * unread byte stalls the host's write indefinitely.
 */
void LT_ISR_SAFE Esp32_ConsolePutChars(const char * pChars, u32 nChars);

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_CONSOLE_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jul-26   claudius    created
 *  03-Aug-26   claudius    note the receive-side drain on Esp32_ConsolePutChars
 */
