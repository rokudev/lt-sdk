/*******************************************************************************
 * platforms/esp32/source/esp32/driver/esp32-lt-os-adapter/Esp32s3_LTOSAdapter.h
 *
 * The esp32s3 half of the Wi-Fi/BT OS adapter.
 *
 * Esp32_LTOSAdapter.c carries the ~116 chip-neutral wrappers the blobs' two
 * function tables point at, and it is compiled for both parts.  It cannot
 * include <esp32s3/Esp32_Registers.h>, because Esp32_Irq.h pulls in the
 * register header sitting next to itself and the esp32 interrupt numbers are
 * what its ISR plumbing is written against.  So the handful of entry points
 * that touch registers the two chips place differently live in
 * Esp32s3_LTOSAdapter.c, which includes the esp32s3 register header in its own
 * translation unit, and the shared file calls through here.
 *
 * The file is only built when SOC_PLATFORM_NAME is esp32s3.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_ESP32_LT_OS_ADAPTER_ESP32S3_LTOSADAPTER_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_ESP32_LT_OS_ADAPTER_ESP32S3_LTOSADAPTER_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

/* RTC_XTAL_FREQ_REG, RTC_CNTL_STORE4 on this part.  Raw; the caller validates
 * the two-halves encoding. */

u32  Esp32s3_XtalFreqRegRead(void);

/* The factory base MAC out of efuse.  Returns 0 on success, -1 on failure. */

s32  Esp32s3_ReadEfuseMac(u8 mac[6]);

/* One sample of the hardware RNG, WDEV_RND. */

u32  Esp32s3_RandomRegRead(void);

/* Pulse SYSTEM_WIFIMAC_RST. */

void Esp32s3_WiFiResetMac(void);

/* Bring the shared Wi-Fi/BT modem power domain up, or put it back.  Reference
 * counted, because the power-up reset pulse must not land under a controller
 * that is already running.  The esp32 has no equivalent. */

void Esp32s3_WiFiBtPowerDomainOn(void);
void Esp32s3_WiFiBtPowerDomainOff(void);

/* RTC slow clock period for the Wi-Fi light sleep timer, in the blobs' 12-bit
 * fixed point rather than the system's 19-bit. */

u32  Esp32s3_SlowClkCalGet(void);

/* The CPU clock in MHz, standing in for the esp32's ROM g_ticks_per_us_pro. */

u32  Esp32s3_CpuClockMHzGet(void);

/* Route a peripheral interrupt source onto a CPU line through the esp32s3
 * interrupt mux, which lives in INTERRUPT_CORE0/1 rather than in DPORT.  The
 * shared adapter reaches the mux only through here, for the include reason
 * above.  Returns false, having routed nothing, if the request names a core
 * this platform does not run on or a line no peripheral may be routed to. */

bool Esp32s3_MapRadioIrq(s32 nCpu, u32 nExternalIrq, u32 nCpuIrq);

LT_EXTERN_C_END
#endif  // PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_ESP32_LT_OS_ADAPTER_ESP32S3_LTOSADAPTER_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created
 */
