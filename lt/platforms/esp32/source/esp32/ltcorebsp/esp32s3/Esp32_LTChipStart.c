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
#include "Esp32_Cache.h"

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

/*
 * Let a system reset reset the USB serial/JTAG, and the IO_MUX with it.
 *
 * Both of RTC_CNTL_USB_CONF's reset-disable bits are cleared here.  They already
 * read 0 out of a power-on reset, so this is not initialisation - it is undoing
 * what an earlier image may have left behind.  The register is RTC sub-system, so
 * it survives SW_SYS_RST: a part that once ran an image setting either bit carries
 * it into every subsequent warm boot until it is powered down, and no reset that
 * esptool, rit or a DTR/RTS toggle can drive will clear it, because every one of
 * those is itself a warm reset.  Clearing them unconditionally on the way up is
 * the only way back out.
 *
 * USB is the only console this part has - the module brings D+/D- out to its only
 * connector and fits no USB-to-UART bridge, leaving UART0 on GPIO43/44
 * electrically unreachable - so the temptation to hold it across a reboot is
 * strong, and both bits were set here at various points between 31-Jul-26 and
 * 10-Aug-26.  Both are traps:
 *
 *   - IO_MUX_RESET_DISABLE holds *every* pin's mux, not just the two USB pads.
 *     The octal PSRAM init routes pads 27, 28 and 31 to 37 to their MSPI
 *     functions at max drive (see _Esp32_PSRAM_InitPins()), and 33 to 37 are
 *     precisely the ones the ROM expects to find as plain GPIOs, because the
 *     flash fitted here is quad.
 *   - USB_RESET_DISABLE is a half reset.  The CDC device's FIFOs, endpoint state
 *     and any transaction in flight survive, while every scrap of digital logic
 *     that drives them is reset out from under it.  The ROM then comes up and
 *     writes its banner into a peripheral left mid-transaction.  The failure is
 *     silent and total: the descriptors and control transfers are answered by
 *     fixed-function hardware with no CPU involvement, so the device stays
 *     enumerated and the host sees a perfectly healthy port that never produces a
 *     byte again.
 *
 * IDF sets neither bit.  The cost of that is real - the port re-enumerates on
 * every reboot and a host watching the boot log loses it before it can read why
 * the board reset, which makes a boot loop look exactly like a single hang - but
 * it is a cost paid on the host, where dmesg can still see the disconnect and the
 * re-attach.  Holding the bits pays it on the target, where nothing can see
 * anything.
 *
 * Depends on nothing: RTC_CNTL is always accessible, and USB_CONF is not behind
 * the WDTWPROTECT lock that guards the watchdog registers in the same block.
 */
static void ESP32_IRAM_FUNC _ReleaseConsoleResetHolds(void) {
    ESP32_REG(RTC_CNTL_USB_CONF) &= ~(ESP32_REG_MASK(RTC_CNTL_USB_CONF, USB_RESET_DISABLE) |
                                      ESP32_REG_MASK(RTC_CNTL_USB_CONF, IO_MUX_RESET_DISABLE));
}

void ESP32_MEM_REGION(IRAM) call_start_cpu0(void) {

    /* Before anything that can fault, so a part carrying a stale reset hold from
     * a previous image is recovered by the first boot of this one rather than
     * needing a power cycle.  See _ReleaseConsoleResetHolds(). */
    _ReleaseConsoleResetHolds();

    /*
     * Before the first call into .flash.text, which is to say before almost
     * anything, and before anything maps flash or PSRAM through the cache.  Fixes
     * both caches' geometry, enables the instruction cache and divides the MMU
     * table between the two buses.  Unlike the esp32, this part's bootloader
     * leaves none of that done for the application.  See Esp32_CacheInitialize().
     */
    Esp32_CacheInitialize();

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
 *  29-Jul-26   claudius    ported to the esp32s3
 *  31-Jul-26   claudius    hold the USB console across a system reset
 *  03-Aug-26   claudius    tell the cache where irom's MMU entries end
 *  03-Aug-26   claudius    enable the instruction cache before flash text
 *  03-Aug-26   claudius    disable every watchdog, not just RWDT
 *  03-Aug-26   claudius    moved cache bring-up out to Esp32_Cache.c
 *  04-Aug-26   claudius    split out of the esp32 platform root
 *  05-Aug-26   claudius    ESP32_IRAM_FUNC
 *  10-Aug-26   claudius    stopped holding IO_MUX across reset; it broke warm boot
 *  10-Aug-26   claudius    stopped holding USB too; it broke warm boot silently
 */
