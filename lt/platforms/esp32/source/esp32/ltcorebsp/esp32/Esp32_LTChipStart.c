/*******************************************************************************
 * ESP32 Startup
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <string.h>

#include <lt/LT.h>

/*
 * Unprefixed: the platform's Makefile.config puts include/$(SOC_PLATFORM_NAME)
 * on the search path, and that is where the register map, the reset reason
 * table and the clock gates this file is built against come from.
 */
#include "Esp32_SoC.h"
#include "Esp32_Registers.h"
#include "Esp32_Irq.h"
#include "Esp32_Clock.h"
#include "Esp32_GPIO.h"
#include "Esp32_Watchdog.h"

#define PLATFORM_NAME "Esp32"
 /* DRW 27-Feb-23 : this method of parameterizing drivers is going away soon */

enum {
    /* Application Security Dynasties (anti-rollback) */
    kApplicationDynasty_Initial        = 0,                            /**< Initial dynasty */
      /* ... Put new dynasties here as needed ... */
    kApplicationDynasty_CurrentDynasty = kApplicationDynasty_Initial,  /**< Current dynasty (application anti-rollback version) */
};

extern int _vector_table;
extern int _rtc_bss_start;
extern int _rtc_bss_end;
extern int _bss_start;
extern int _bss_end;

typedef struct {
    u32   nMagicNumber;
    u32   nSecurityDynasty;
    u32   nRsvd[2];
    char  appVersion[32];
    char  projectName[32];
    char  compileTime[16];
    char  compileDate[16];
    char  sdkVersion[32];
    u8    appDigestSHA256[32];
    u32   nRsvd2[20];
} ApplicationDescriptor;

/* Application version info */
const __attribute__((section(".rodata_desc")))
ApplicationDescriptor applicationDescriptor = {
    .nMagicNumber     = 0xabcd5432,
    .nSecurityDynasty = kApplicationDynasty_CurrentDynasty,
    .appVersion       = "1",
    .projectName      = PLATFORM_NAME,
    .compileTime      = __TIME__,
    .compileDate      = __DATE__,
    .sdkVersion       = "",
};

void ESP32_MEM_REGION(IRAM) call_start_cpu0(void) {

    /*
     * Nothing in the LT boot path feeds any watchdog, and the app inherits
     * whatever the ROM and the bootloader left armed - the bootloader arms the
     * RWDT for its own protection and hands it over still running - so turn all
     * of them off here rather than trusting what came before.  The previous code
     * covered only the RWDT.  See Esp32DisableAllWatchdogs().
     */
    Esp32DisableAllWatchdogs();

    /* Re-map exception vectors for this image */
    Esp32SetVectorTableBaseAddress(&_vector_table);

    Esp32_ResetReason nResetReason = esp_rom_get_reset_reason(kEsp32_CPU0);

    /* Clear BSS. Please do not attempt to do any complex stuff (like early logging) before this. */
    memset(&_bss_start, 0, (&_bss_end - &_bss_start) * sizeof(_bss_start));

    /* Unless waking from deep sleep (implying RTC memory is intact), clear RTC bss */
    if (nResetReason != kEsp32_ResetReason_CoreDeepSleep) {
        memset(&_rtc_bss_start, 0, (&_rtc_bss_end - &_rtc_bss_start) * sizeof(_rtc_bss_start));
    }

    esp_rom_printf("cpu_start: CPU 0 Running, Reset Reason: 0x%x\n", nResetReason);

    /* Stop other core for now */
    Esp32_ClockDisableAppCpu();

    u32 nCpuFreqMHz = Esp32_ClockInitialize();

    esp_rom_printf("cpu_start: CPU freq: %u MHz\n", nCpuFreqMHz);

#if 0
    /* TODO: RTC restore? */
#endif
    /* Not all types of reset clear the pad hold registers, so clear them here to
     * prevent holding pins unexpectedly.
     */
    Esp32GPIO_ClearAllPinHolds();

    /* Global initializers */
    typedef void (GlobalInitFunc)(void);
    extern GlobalInitFunc * __init_array_start;
    extern GlobalInitFunc * __init_array_end;
    GlobalInitFunc ** ppInitFunc;
    for (ppInitFunc = &__init_array_end - 1; ppInitFunc >= &__init_array_start; ppInitFunc--) {
        (*ppInitFunc)();
    }

    static const char *argv[] = { PLATFORM_NAME, LT_GENESIS_LIBRARY };
    int argc = sizeof argv / sizeof argv[0];

    LT_Run(argc, argv);

}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  15-Aug-23   commodus    cleared RTCIO_DIG_PAD_HOLD
 *  03-Aug-26   claudius    disable every watchdog, not just RWDT
 */
