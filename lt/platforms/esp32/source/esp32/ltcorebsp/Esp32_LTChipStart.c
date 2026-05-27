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
#include "esp32/Esp32_SoC.h"
#include "esp32/Esp32_Registers.h"
#include "esp32/Esp32_Irq.h"
#include "esp32/Esp32_Clock.h"
#include "esp32/Esp32_Watchdog.h"

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
    u32 nVal = ESP32_REG(DPORT_CPU1_CTRL_B);
    nVal &= ~ESP32_REG_MASK(DPORT_CPU1, CLKGATE_EN);
    ESP32_REG(DPORT_CPU1_CTRL_B) = nVal;

    u32 nCpuFreqMHz = Esp32_ClockInitialize();

    esp_rom_printf("cpu_start: CPU freq: %u MHz\n", nCpuFreqMHz);

#if 0
    /* TODO: RTC restore? */
#endif
    /* Not all types of reset clear this register, so clear it here to prevent holding pins
     * unexpectedly.
     */
    ESP32_REG(RTCIO_DIG_PAD_HOLD) = 0x0;

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
 */
