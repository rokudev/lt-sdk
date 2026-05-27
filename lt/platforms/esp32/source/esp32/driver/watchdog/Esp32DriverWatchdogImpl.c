/*******************************************************************************
 * platforms/esp32/source/esp32/driver/watchdog/Esp32DriverWatchdogImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>

#include <lt/device/watchdog/LTDeviceWatchdog.h>

#include "Esp32_Watchdog.h"
#include "Esp32_Registers.h"
#include "Esp32_SoC.h"

extern u32              g_ticks_per_us_pro;
static const u32        kWDTTicksPerUS  = 500;

static bool Esp32DriverWatchdogImpl_LibInit(void) {
    return true;
}

static void Esp32DriverWatchdogImpl_LibFini(void) {
}

static u32 Esp32DriverWatchdogImpl_GetNumDeviceUnits(void) {
    return 0;
}

static LTDeviceUnit Esp32DriverWatchdogImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LT_UNUSED(nDeviceUnitNumber);
    return 0;
}

static bool Esp32DriverWatchdog_ResetTimer(void) {
    Esp32PetRTCWatchdog();
    return true;
}

static bool Esp32DriverWatchdog_EnableTimer(void) {
    Esp32EnableRTCWatchdog();
    return true;
}

static bool Esp32DriverWatchdog_DisableTimer(void) {
    Esp32DisableRTCWatchdog();
    return true;
}

static bool Esp32DriverWatchdog_IsEnabled(void) {
    return Esp32IsEnabledRTCWatchdog();
}

static bool Esp32DriverWatchdog_SetTimeout(LTTime timeout) {
    Esp32SetTimeoutRTCWatchdog(LTTime_GetMicroseconds(timeout) * g_ticks_per_us_pro / kWDTTicksPerUS);
    return true;
}

static void Esp32DriverWatchdog_Reboot(void) {
    /* Reboot using system reset */
    ESP32_REG(RTC_CNTL_OPTIONS0) = ESP32_REG_VAL(RTC_CNTL, SW_SYS_RST);
}

static LTBootReason Esp32DriverWatchdog_GetBootReason(const char ** pReasonString) {
    static struct {
        const char * pReason;
        LTBootReason bootReason;
    } reasons[] = {
        { "Power On",    kLTBootReason_PowerOn       },   /* 0x01 */
        { "Undef",       kLTBootReason_Undefined     },
        { "SW",          kLTBootReason_Reset         },   /* 0x03 */
        { "Undef",       kLTBootReason_Undefined     },
        { "Deep Sleep",  kLTBootReason_DeepSleep     },   /* 0x05 */
        { "SDIO",        kLTBootReason_WatchdogReset },   /* 0x06 */
        { "MWDT",        kLTBootReason_WatchdogReset },   /* 0x07 */
        { "MWDT1",       kLTBootReason_WatchdogReset },   /* 0x08 */
        { "RTC_WDT",     kLTBootReason_WatchdogReset },   /* 0x09 */
        { "Undef",       kLTBootReason_Undefined     },
        { "CPU MWDT" ,   kLTBootReason_WatchdogReset },   /* 0x0b */
        { "CPU SW",      kLTBootReason_Reset         },   /* 0x0c */
        { "CPU RTC_WDT", kLTBootReason_WatchdogReset },   /* 0x0d */
        { "CPU1_CPU0",   kLTBootReason_Reset         },   /* 0x0e */
        { "Brown Out",   kLTBootReason_Reset         },   /* 0x0f */
        { "SYS RTC_WDT", kLTBootReason_WatchdogReset }    /* 0x10 - (N.B.: Bootloader watchdog) */
    };

    LTBootReason reasonValue = kLTBootReason_Undefined;
    Esp32_ResetReason nResetReason = esp_rom_get_reset_reason(kEsp32_CPU0);

    if (nResetReason > 0 && nResetReason <= (sizeof(reasons)/sizeof(reasons[0]))) {
        if (pReasonString) *pReasonString = reasons[nResetReason - 1].pReason;
        reasonValue = reasons[nResetReason - 1].bootReason;
    }
    return reasonValue;
}

define_LTLIBRARY_INTERFACE(ILTDriverWatchdog) {
    .Reboot        = Esp32DriverWatchdog_Reboot,
    .ResetTimer    = Esp32DriverWatchdog_ResetTimer,
    .EnableTimer   = Esp32DriverWatchdog_EnableTimer,
    .DisableTimer  = Esp32DriverWatchdog_DisableTimer,
    .IsEnabled     = Esp32DriverWatchdog_IsEnabled,
    .SetTimeout    = Esp32DriverWatchdog_SetTimeout,
    .GetBootReason = Esp32DriverWatchdog_GetBootReason,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(Esp32DriverWatchdog, (ILTDriverWatchdog))

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceWatchdog, Esp32DriverWatchdog);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  18-Jul-22   vitellius   Created
 */
