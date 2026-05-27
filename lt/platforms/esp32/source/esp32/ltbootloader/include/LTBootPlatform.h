/*******************************************************************************
 * LTBootPlatform.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef _LTBOOT_PLATFORM_H_
#define _LTBOOT_PLATFORM_H_

/* Platform-specific definitions */

#include <string.h>

#include "esp_rom_sys.h"

#include "lt/LTTypes.h"
#include "Esp32_Registers.h"

/* Redirect to ESP32 ROM function */
#define LTBootPlatform_strncmp(s1, s2, n)     (strncmp((s1), (s2), (n)))

/* Redirect to ESP32 ROM function */
#define LTBootPlatform_printf(fmt, ...)       (esp_rom_printf(fmt __VA_OPT__(,) __VA_ARGS__))

#ifndef LT_DEBUG
  #define LTBootPlatform_DebugPrintf(...)
#else
  #define LTBootPlatform_DebugPrintf          LTBootPlatform_printf
#endif

/* For internal use only */
#define __INTERNAL_RESET()  {                                           \
    ESP32_REG(RTC_CNTL_OPTIONS0) = ESP32_REG_VAL(RTC_CNTL, SW_SYS_RST); \
    asm volatile ( "ill.n; ill.n" ); }

#define UART_TX_FIFO_BUSY \
    (ESP32_UART_REG(0, STATUS) & (ESP32_REG_MASK(UART_STATUS, TXFIFO_CNT) | ESP32_REG_MASK(UART_STATUS, UTX_OUT)))

/* Security abort - Abort gracefullly, allowing serial console to drain.
 *   Use this for general bootloader failures including security failures.
 *   Follow this with a security assertion to thwart fault-injection attacks. */
#define LTBootPlatform_SecurityAbort() { \
    while (UART_TX_FIFO_BUSY) {}         \
    __INTERNAL_RESET();                  \
    __INTERNAL_RESET();                  \
    __INTERNAL_RESET(); }

/* Security abort - Abort gracefullly, allowing serial console to drain and the watchdog
 *   timer to expire. Watchdog resets can failover to retry the other firmware partition.
 *   Follow this with a security assertion to thwart fault-injection attacks. */
#define LTBootPlatform_SecurityAbortWithFailover() { \
    while (1) {}                                     \
    __INTERNAL_RESET();                              \
    __INTERNAL_RESET();                              \
    __INTERNAL_RESET(); }

/* Security assertion check - Immediate abort if condition is not true.
 *    Perform normal check first then use this assertion as a backup
 *    to help thwart fault-injection attacks. */
#define LTBootPlatform_SecurityAssert(CND) { \
    asm volatile ("" ::: "memory");          \
    if (!(CND)) __INTERNAL_RESET();          \
    asm volatile ("" ::: "memory");          \
    if (!(CND)) __INTERNAL_RESET();          \
    asm volatile ("" ::: "memory");          \
    if (!(CND)) __INTERNAL_RESET(); }

#endif

