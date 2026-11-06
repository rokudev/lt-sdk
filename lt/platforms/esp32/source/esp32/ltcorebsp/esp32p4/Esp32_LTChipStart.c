/*******************************************************************************
 * esp32p4/Esp32_LTChipStart.c                                     ESP32-P4 BSP
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
 * Let a system reset reset the IO_MUX.
 *
 * The bit reads 0 out of a power-on reset, so this is not initialisation - it is
 * undoing what an earlier image may have left behind.  LP_SYS is always-on, so a
 * part that once ran an image setting the bit carries it into every subsequent
 * warm boot until it is powered down, and every reset esptool, rit or a DTR/RTS
 * toggle can drive is a warm one.  Held pads would keep the flash pins from
 * coming back as the ROM left them.
 *
 * Read-modify-write, unlike the esp32c3's bare store: the other fields of
 * SYS_CTRL configure which faults may reset the chip and are not spare.
 */
static void ESP32_IRAM_FUNC _ReleaseConsoleResetHolds(void) {
    ESP32_REG(LP_SYS_SYS_CTRL) &= ~ESP32_REG_MASK(LP_SYS_SYS_CTRL, IO_MUX_RESET_DISABLE);
}

/*
 * Mask interrupts globally before the first line of LT runs.
 *
 * The ROM and the bootloader both leave mstatus.MIE set, and nothing between
 * here and LTKThreadInitializeAndStartScheduler() expects to be interrupted:
 * LTCoreBSP_Initialize() arms the systimer tick, but mtvec does not become
 * LT's until LTKInitialize() runs later, and _LTKDispatcher() has no scheduler
 * to dispatch to until later still.  A tick landing in that window either
 * vectors into the dead bootloader or runs the kernel's queues before they are
 * initialised - which is a boot crash whose presence depends only on where the
 * code happens to be when the first millisecond elapses.
 *
 * mstatus.MIE only.  Per line enables are the CLIC's, not mie's, and
 * Esp32InitializeIRQs() clears them all before any line is attached.
 */
static void ESP32_IRAM_FUNC _MaskAllInterrupts(void) {
    asm volatile ("csrci mstatus, 1 << 3" : : : "memory");
}

/*
 * Bring the FPU out of reset.  This part is built -mabi=ilp32f and LTK's trap
 * handler saves f0-f31 unconditionally, so with mstatus.FS left Off the first
 * trap takes an illegal instruction on its own first FP store and re-enters
 * itself forever - a silent hang with no fault output.
 *
 * Writing fcsr and clearing the dirty bit afterwards leaves FS in the Clean
 * state the handler's fscsr/frcsr pair expects, rather than the Initial state
 * setting bit 13 alone would give.
 */
static void ESP32_IRAM_FUNC _EnableFPU(void) {
    asm volatile (
        "csrs    mstatus, %0   \n\
         csrw    fcsr, zero    \n\
         csrc    mstatus, %0"
            : : "r"(0x2000) : "memory"
    );
}

void ESP32_MEM_REGION(IRAM) call_start_cpu0(void) {

    /* Must be first: see _MaskAllInterrupts(). */
    _MaskAllInterrupts();

    /* Before any FP code and before the first trap; see _EnableFPU(). */
    _EnableFPU();

    /* Before anything that can fault, so a part carrying a stale reset hold from
     * a previous image is recovered by the first boot of this one rather than
     * needing a power cycle.  See _ReleaseConsoleResetHolds(). */
    _ReleaseConsoleResetHolds();

    /*
     * Before the first call into .flash.text, which is to say before almost
     * anything - the L2 is what reaches external flash on this part.  The
     * bootloader suspends it on its way out and leaves it to the application to
     * bring back.  See Esp32_CacheInitialize().
     */
    Esp32_CacheInitialize();

    /*
     * Nothing in the LT boot path feeds any watchdog, and the app inherits
     * whatever the ROM and the bootloader left armed - the bootloader arms the
     * RWDT for its own protection and hands it over still running - so turn all
     * of them off here rather than trusting what came before.
     * See Esp32DisableAllWatchdogs().
     */
    Esp32DisableAllWatchdogs();

    /* No vector table base to program here.  On RISC-V LTK owns mtvec and writes
     * it from LTKInitialize(). */

    Esp32_ResetReason nResetReason = esp_rom_get_reset_reason(kEsp32_CPU0);

    /* Clear BSS. Please do not attempt to do any complex stuff (like early logging) before this. */
    memset(&_bss_start, 0, (&_bss_end - &_bss_start) * sizeof(_bss_start));

    /* Unless waking from deep sleep (implying RTC memory is intact), clear RTC bss */
    if (nResetReason != kEsp32_ResetReason_CoreDeepSleep) {
        memset(&_rtc_bss_start, 0, (&_rtc_bss_end - &_rtc_bss_start) * sizeof(_rtc_bss_start));
    }

    esp_rom_printf("cpu_start: CPU 0 Running, Reset Reason: 0x%x\n", nResetReason);

    /* The second core is left in reset - LT has no SMP. */

    u32 nCpuFreqMHz = Esp32_ClockInitialize();

    esp_rom_printf("cpu_start: CPU freq: %u MHz\n", nCpuFreqMHz);

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
 *  23-Sep-26   claudius    created, from esp32c3/Esp32_LTChipStart.c
 */
