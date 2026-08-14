/*******************************************************************************
 * platforms/esp32/source/esp32/driver/watchdog/esp32s3/Esp32s3DriverWatchdogImpl.c
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

#include "Esp32_Irq.h"
#include "Esp32_Watchdog.h"
#include "Esp32_Registers.h"
#include "Esp32_SoC.h"

/*
 * The RTC watchdog counts RTC_SLOW_CLK cycles, which on the esp32s3 is the
 * internal 150kHz RC oscillator unless the BSP has switched it to the 32kHz
 * crystal or the 8MHz-derived clock.  150kHz gives 3 ticks per 20us.
 *
 * The esp32 driver instead scales by g_ticks_per_us_pro / 500, using a ROM
 * variable the esp32s3 ROM does not export; that expression also assumes an
 * RTC clock derived from the CPU clock, and overshoots the requested timeout by
 * roughly 3x on a 160MHz part.  The conversion here is the accurate one.
 */
enum {
    kRTCSlowClockHz = 150000,

    /*
     * The reboot path resets the part *with* the RTC watchdog rather than waiting
     * on it, so this is a delay to the reset and not a timeout on anything - it
     * only has to be long enough for the three register writes that arm it to have
     * landed.  50ms is imperceptible and enormously more than that.
     * See Esp32s3DriverWatchdog_Reboot().
     */
    kRebootWatchdogTicks = kRTCSlowClockHz / 20,

    /*
     * Iterations of the spin that waits for the above.  At 240MHz a volatile
     * loop iteration is a handful of cycles, so this is on the order of a second:
     * comfortably past 50ms, and short enough that a watchdog which never fires
     * falls through to the SW_SYS_RST fallback rather than hanging.
     */
    kRebootWatchdogSpinLimit = 20000000,
};

static bool Esp32s3DriverWatchdogImpl_LibInit(void) {
    return true;
}

static void Esp32s3DriverWatchdogImpl_LibFini(void) {
}

static u32 Esp32s3DriverWatchdogImpl_GetNumDeviceUnits(void) {
    return 0;
}

static LTDeviceUnit Esp32s3DriverWatchdogImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LT_UNUSED(nDeviceUnitNumber);
    return 0;
}

static bool Esp32s3DriverWatchdog_ResetTimer(void) {
    Esp32PetRTCWatchdog();
    return true;
}

static bool Esp32s3DriverWatchdog_EnableTimer(void) {
    Esp32EnableRTCWatchdog();
    return true;
}

static bool Esp32s3DriverWatchdog_DisableTimer(void) {
    Esp32DisableRTCWatchdog();
    return true;
}

static bool Esp32s3DriverWatchdog_IsEnabled(void) {
    return Esp32IsEnabledRTCWatchdog();
}

static bool Esp32s3DriverWatchdog_SetTimeout(LTTime timeout) {
    s64 nMicroseconds = LTTime_GetMicroseconds(timeout);

    if (nMicroseconds < 0) return false;

    /* Clamp rather than wrap; the stage 0 hold count is 32 bits, which at
     * 150kHz is a little under eight hours */
    u64 nTicks = ((u64)nMicroseconds * kRTCSlowClockHz) / 1000000;
    if (nTicks > 0xffffffffull) nTicks = 0xffffffffull;

    Esp32SetTimeoutRTCWatchdog((u32)nTicks);
    return true;
}

/*
 * Restart the part, by letting the RTC watchdog do it rather than by storing
 * SW_SYS_RST.
 *
 * The two are not interchangeable, and the difference is the whole point.
 * SW_SYS_RST resets the digital system and nothing else: the RTC sub-system -
 * OPTIONS0 itself, the BBPLL's power state and its analog I2C configuration, the
 * RTC and super watchdogs, RTC_CNTL_USB_CONF and the RTC retention words - all
 * carry straight through into the warm boot, holding whatever the outgoing image
 * left in them while the logic that drives them is wiped out from under them.
 * RWDT stage action 4, RESET_RTC, is the widest reset the part can apply to
 * itself: digital system, RTC sub-system, and - because SETUP_EN also carries
 * CHIP_RESET_EN - a pulse on the chip reset pin.  It is as close to a power cycle
 * as software gets, and the RTC sub-system is precisely where this port has
 * repeatedly found state that poisons a warm boot.
 *
 * Reaching for it is not caution, it is where the evidence pointed.  A plain
 * SW_SYS_RST here left the board dead often enough to be unusable - silent,
 * unrecoverable by any reset esptool, rit or a DTR/RTS toggle could drive, and
 * cleared only by unplugging the part.  Three separate explanations were built and
 * killed on hardware (an unbounded I2C_MST_BBPLL_CAL_DONE wait in
 * rtc_clk_bbpll_configure(), a wedged host tty, and IO_MUX_RESET_DISABLE holding
 * the octal PSRAM's MSPI pads across the reset).  What they had in common is that
 * SW_SYS_RST's narrow scope was never going to clear whatever it is; a reset that
 * takes the RTC sub-system with it does not care which of them was right.
 *
 * Three things this function must not do, all of which were tried:
 *
 *   - Hold the console across the reset, via USB_RESET_DISABLE in
 *     RTC_CNTL_USB_CONF.  That leaves the CDC device's FIFOs, endpoint state and
 *     any transaction in flight alive across a reset that wipes everything
 *     driving them, and the ROM cannot get it going again.  It fails silently,
 *     because the USB Serial/JTAG answers descriptors and control transfers in
 *     fixed-function hardware with no CPU involvement: the port stays enumerated
 *     and looks perfectly healthy while producing no bytes ever again.
 *     _ReleaseConsoleResetHolds() clears those bits now.  Expect the tty to drop
 *     and re-enumerate on every reboot; that is correct behaviour, and it is what
 *     IDF does.
 *   - Park the BBPLL.  The USB Serial/JTAG needs a PLL-derived 48MHz, and
 *     powering the PLL down leaves the device unclocked and silent from here
 *     through the ROM and bootloader_clock_configure().  Espressif's own
 *     rtc_clk_set_bbpll_always_on() and the comment in rtc_clk_recalib_bbpll()
 *     both say as much: the PLL is deliberately kept alive when the USB
 *     Serial/JTAG is the console.
 *   - Reset the CPU rather than the system.  IDF's esp_restart_noos() finishes
 *     with SW_PROCPU_RST, which is why it has to hand-reset the GPIO matrix,
 *     SPI01, the timers, the UARTs and DMA first - a CPU-only reset leaves every
 *     one of those running.  Anything wider resets all of them in hardware, so
 *     none of that work belongs here.
 *
 * The SW_SYS_RST store is kept as an unreachable-in-practice fallback, so that a
 * watchdog which somehow does not fire degrades to the old behaviour instead of
 * hanging in this function.  Which path ran is visible from the next boot:
 * GetBootReason() reports RTC_WDT for the watchdog and SW for the fallback.
 *
 * Runs from IRAM so that none of it depends on a flash fetch.
 */
static void ESP32_IRAM_FUNC
Esp32s3DriverWatchdog_Reboot(void) {
    /* Nothing else gets to run between here and the reset */
    Esp32DisableInterrupts();

    Esp32SetTimeoutRTCWatchdog(kRebootWatchdogTicks);
    Esp32EnableRTCWatchdog();
    Esp32PetRTCWatchdog();

    /*
     * Spin until it fires.  Bounded only so the fallback below is reachable; the
     * count is deliberately crude, since the only thing that matters is that it
     * outlasts kRebootWatchdogTicks by a wide margin at any CPU clock this part
     * runs at.  volatile so the loop survives the optimiser.
     */
    for (volatile u32 i = 0; i < kRebootWatchdogSpinLimit; i++) { }

    ESP32_REG(RTC_CNTL_OPTIONS0) = ESP32_REG_VAL(RTC_CNTL, SW_SYS_RST);

    while (1) { }
}

static LTBootReason Esp32s3DriverWatchdog_GetBootReason(const char ** pReasonString) {
    /*
     * The esp32s3 reset codes are not contiguous - there is no 0x06 and no 0x0e
     * - so unlike the esp32 driver this cannot be a table indexed by the code.
     */
    static const struct {
        Esp32_ResetReason   resetReason;
        const char *        pReason;
        LTBootReason        bootReason;
    } reasons[] = {
        { kEsp32_ResetReason_PowerOnReset,  "Power On",     kLTBootReason_PowerOn       },
        { kEsp32_ResetReason_RtcSwSys,      "SW",           kLTBootReason_Reset         },
        { kEsp32_ResetReason_CoreDeepSleep, "Deep Sleep",   kLTBootReason_DeepSleep     },
        { kEsp32_ResetReason_CoreMWDT0,     "MWDT0",        kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_CoreMWDT1,     "MWDT1",        kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_CoreRTCWDT,    "RTC_WDT",      kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_Intrusion,     "Intrusion",    kLTBootReason_Reset         },
        { kEsp32_ResetReason_Cpu0MWDT0,     "CPU0 MWDT0",   kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_RtcSwCpu,      "CPU SW",       kLTBootReason_Reset         },
        { kEsp32_ResetReason_Cpu0RTCWDT,    "CPU0 RTC_WDT", kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_SysBrownOut,   "Brown Out",    kLTBootReason_Reset         },
        { kEsp32_ResetReason_SysRTCWDT,     "SYS RTC_WDT",  kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_Cpu1MWDT1,     "CPU1 MWDT1",   kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_SuperWDT,      "Super WDT",    kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_GlitchRTC,     "Glitch",       kLTBootReason_Reset         },
        { kEsp32_ResetReason_EFuse,         "eFuse CRC",    kLTBootReason_Reset         },
        { kEsp32_ResetReason_UsbUartChip,   "USB UART",     kLTBootReason_ResetExternal },
        { kEsp32_ResetReason_UsbJtagChip,   "USB JTAG",     kLTBootReason_ResetExternal },
        { kEsp32_ResetReason_PowerGlitch,   "Power Glitch", kLTBootReason_Reset         },
    };

    Esp32_ResetReason nResetReason = esp_rom_get_reset_reason(kEsp32_CPU0);

    for (u32 i = 0; i < (sizeof(reasons) / sizeof(reasons[0])); i++) {
        if (reasons[i].resetReason == nResetReason) {
            if (pReasonString) *pReasonString = reasons[i].pReason;
            return reasons[i].bootReason;
        }
    }

    return kLTBootReason_Undefined;
}

/*
 * Library Interface
 */
define_LTLIBRARY_INTERFACE(ILTDriverWatchdog) {
    .Reboot        = Esp32s3DriverWatchdog_Reboot,
    .ResetTimer    = Esp32s3DriverWatchdog_ResetTimer,
    .EnableTimer   = Esp32s3DriverWatchdog_EnableTimer,
    .DisableTimer  = Esp32s3DriverWatchdog_DisableTimer,
    .IsEnabled     = Esp32s3DriverWatchdog_IsEnabled,
    .SetTimeout    = Esp32s3DriverWatchdog_SetTimeout,
    .GetBootReason = Esp32s3DriverWatchdog_GetBootReason,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(Esp32s3DriverWatchdog, (ILTDriverWatchdog))

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceWatchdog, Esp32s3DriverWatchdog);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  29-Jul-26   claudius    created
 *  10-Aug-26   claudius    Reboot arms the RTC watchdog to cover the warm boot
 *  10-Aug-26   claudius    backstop resets the system only, so the console lives
 *  10-Aug-26   claudius    reverted: the backstop needs the bigger hammer
 *  10-Aug-26   claudius    Reboot resets via RWDT RESET_RTC, not SW_SYS_RST
 */
