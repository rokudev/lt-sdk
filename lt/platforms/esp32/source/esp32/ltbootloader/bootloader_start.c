
#include "bootloader_utility.h"

#include <stdbool.h>
#include "esp_log.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"

#include "LTBoot.h"
#include "LTBootPlatform.h"

/*
 * We arrive here after the ROM bootloader finished loading this second stage bootloader from flash.
 * The hardware is mostly uninitialized, flash cache is down and the app CPU is in reset.
 * We do have a stack, so we can do the initialization in C.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 */
void LT_NORETURN call_start_cpu0(void) {
    // Hardware initialization
    if (bootloader_init() != ESP_OK) {
        bootloader_reset();
    }

    /* Translate esp32 reset reason to LTBootReason */
    soc_reset_reason_t resetReason = esp_rom_get_reset_reason(0);

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

    LTBootReason bootReason = kLTBootReason_Undefined;
    const char * pReason = "Undef";
    if (resetReason > 0 && resetReason <= (sizeof(reasons)/sizeof(reasons[0]))) {
        pReason    = reasons[resetReason - 1].pReason;
        bootReason = reasons[resetReason - 1].bootReason;
    }

    // FIXME: This is defined, what do we do about deep sleep?
#ifdef CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP
    // If this boot is a wake up from the deep sleep then go to the short way,
    // try to load the application which worked before deep sleep.
    // It skips a lot of checks due to it was done before (while first boot).
    bootloader_utility_load_boot_image_from_deep_sleep();
    // If it is not successful try to load an application as usual.
#endif

    LTBoot_Run(bootReason, pReason);
    /* UNREACHABLE */
}

// This provides an aborta and eliminates some linker warnings
void abort(void) {
    LTBootPlatform_SecurityAbort();
    while (1);
}
