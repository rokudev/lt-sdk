/*******************************************************************************
 * platforms/esp32/source/esp32/driver/watchdog/esp32p4/Esp32p4DriverWatchdogImpl.c
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
 * The RWDT counts RTC_SLOW_CLK cycles, which is the internal RC oscillator
 * unless something has switched it.  136kHz here rather than the esp32c3's
 * 150kHz - SOC_CLK_RC_SLOW_FREQ_APPROX.
 */
enum {
    kRTCSlowClockHz = 136000,

    /*
     * The reboot path resets the part *with* the RWDT rather than waiting on
     * it, so this is a delay to the reset and not a timeout on anything - it
     * only has to be long enough for the register writes that arm it to have
     * landed.  50ms is imperceptible and enormously more than that.
     * See Esp32p4DriverWatchdog_Reboot().
     */
    kRebootWatchdogTicks = kRTCSlowClockHz / 20,

    /*
     * Iterations of the spin that waits for the above.  At 400MHz a volatile
     * loop iteration is a handful of cycles, so this is on the order of a
     * second: comfortably past 50ms, and short enough that a watchdog which
     * never fires falls through to the SW_RST fallback rather than hanging.
     */
    kRebootWatchdogSpinLimit = 50000000,
};

/*
 * Hardware multiplies the stage 0 hold count by 2, 4, 8 or 16 according to
 * EFUSE_WDT_DELAY_SEL, so a count computed straight from the clock rate would
 * time out at least twice as late as asked.  The efuse never changes at
 * runtime, so this is read once.
 */
static u32 Esp32p4DriverWatchdog_DelayMultiplier(void) {
    u32 nSel = (ESP32_REG(EFUSE_RD_REPEAT_DATA1) & ESP32_REG_MASK(EFUSE, WDT_DELAY_SEL))
                   >> ESP32_REG_SHIFT(EFUSE, WDT_DELAY_SEL);
    return 2u << nSel;
}

static bool Esp32p4DriverWatchdogImpl_LibInit(void) {
    return true;
}

static void Esp32p4DriverWatchdogImpl_LibFini(void) {
}

static u32 Esp32p4DriverWatchdogImpl_GetNumDeviceUnits(void) {
    return 0;
}

static LTDeviceUnit Esp32p4DriverWatchdogImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LT_UNUSED(nDeviceUnitNumber);
    return 0;
}

static bool Esp32p4DriverWatchdog_ResetTimer(void) {
    Esp32PetRTCWatchdog();
    return true;
}

static bool Esp32p4DriverWatchdog_EnableTimer(void) {
    Esp32EnableRTCWatchdog();
    return true;
}

static bool Esp32p4DriverWatchdog_DisableTimer(void) {
    Esp32DisableRTCWatchdog();
    return true;
}

static bool Esp32p4DriverWatchdog_IsEnabled(void) {
    return Esp32IsEnabledRTCWatchdog();
}

static bool Esp32p4DriverWatchdog_SetTimeout(LTTime timeout) {
    s64 nMicroseconds = LTTime_GetMicroseconds(timeout);

    if (nMicroseconds < 0) return false;

    /* Clamp rather than wrap; the stage 0 hold count is 32 bits, which at
     * 136kHz and the smallest multiplier is a little over four hours */
    u64 nTicks = ((u64)nMicroseconds * kRTCSlowClockHz) / 1000000;
    nTicks /= Esp32p4DriverWatchdog_DelayMultiplier();
    if (nTicks > 0xffffffffull) nTicks = 0xffffffffull;

    Esp32SetTimeoutRTCWatchdog((u32)nTicks);
    return true;
}

/*
 * Restart the part, by letting the RWDT do it rather than by storing SW_RST.
 *
 * The two are not interchangeable.  SW_RST resets the digital system and
 * nothing else: the always-on sub-system - SYS_CTRL itself, the PLL power
 * states, the RWDT and super watchdog, and the LP retention words - all carry
 * straight through into the warm boot, holding whatever the outgoing image left
 * in them while the logic that drives them is wiped out from under them.  RWDT
 * stage action 4, reset RTC, is the widest reset the part can apply to itself.
 * The esp32s3 port arrived at this the hard way, after a plain software reset
 * left boards unrecoverable by any host-driven reset; the shared state that
 * causes it exists here too, so the same reset is used.
 *
 * Nothing here holds the console across the reset.  The esp32c3's
 * USB_RESET_DISABLE has no counterpart on this part because the console is a
 * UART behind an external bridge, which is unaffected by a chip reset.
 *
 * The SW_RST store is kept as an unreachable-in-practice fallback, so that a
 * watchdog which somehow does not fire degrades to a narrower reset instead of
 * hanging in this function.  Which path ran is visible from the next boot:
 * GetBootReason() reports SYS RWDT for the watchdog and CPU SW for the
 * fallback.
 *
 * Runs from IRAM so that none of it depends on a flash fetch.
 */
static void ESP32_IRAM_FUNC
Esp32p4DriverWatchdog_Reboot(void) {
    /* Nothing else gets to run between here and the reset */
    Esp32DisableInterrupts();

    Esp32SetTimeoutRTCWatchdog(kRebootWatchdogTicks);
    Esp32EnableRTCWatchdog();
    Esp32PetRTCWatchdog();

    /*
     * Spin until it fires.  Bounded only so the fallback below is reachable;
     * the count is deliberately crude, since the only thing that matters is
     * that it outlasts kRebootWatchdogTicks by a wide margin at any CPU clock
     * this part runs at.  volatile so the loop survives the optimiser.
     */
    for (volatile u32 i = 0; i < kRebootWatchdogSpinLimit; i++) { }

    /* Read-modify-write: the rest of SYS_CTRL selects which faults may reset
     * the chip and must not be cleared */
    ESP32_REG(LP_SYS_SYS_CTRL) |= ESP32_REG_MASK(LP_SYS_SYS_CTRL, SW_RST);

    while (1) { }
}

static LTBootReason Esp32p4DriverWatchdog_GetBootReason(const char ** pReasonString) {
    /*
     * The reset codes are sparse - odd values with gaps - so unlike the esp32
     * driver this cannot be a table indexed by the code.  The values are
     * soc_reset_reason_t; see Esp32_SoC.h.
     */
    static const struct {
        Esp32_ResetReason   resetReason;
        const char *        pReason;
        LTBootReason        bootReason;
    } reasons[] = {
        { kEsp32_ResetReason_ChipPowerOn,     "Power On",     kLTBootReason_PowerOn       },
        { kEsp32_ResetReason_CoreSW,          "SW",           kLTBootReason_Reset         },
        { kEsp32_ResetReason_CoreDeepSleep,   "Deep Sleep",   kLTBootReason_DeepSleep     },
        { kEsp32_ResetReason_CoreMWDT,        "MWDT",         kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_CoreRWDT,        "RWDT",         kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_CpuMWDT,         "CPU MWDT",     kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_CpuSW,           "CPU SW",       kLTBootReason_Reset         },
        { kEsp32_ResetReason_CpuRWDT,         "CPU RWDT",     kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_SysBrownOut,     "Brown Out",    kLTBootReason_Reset         },
        { kEsp32_ResetReason_SysRWDT,         "SYS RWDT",     kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_SysSuperWDT,     "Super WDT",    kLTBootReason_WatchdogReset },
        { kEsp32_ResetReason_CorePowerGlitch, "Power Glitch", kLTBootReason_Reset         },
        { kEsp32_ResetReason_CoreEFuseCRC,    "eFuse CRC",    kLTBootReason_Reset         },
        { kEsp32_ResetReason_CoreUsbJtag,     "USB JTAG",     kLTBootReason_ResetExternal },
        { kEsp32_ResetReason_CoreUsbUart,     "USB UART",     kLTBootReason_ResetExternal },
        { kEsp32_ResetReason_CpuJtag,         "CPU JTAG",     kLTBootReason_ResetExternal },
        { kEsp32_ResetReason_CpuLockup,       "CPU Lockup",   kLTBootReason_Reset         },
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
    .Reboot        = Esp32p4DriverWatchdog_Reboot,
    .ResetTimer    = Esp32p4DriverWatchdog_ResetTimer,
    .EnableTimer   = Esp32p4DriverWatchdog_EnableTimer,
    .DisableTimer  = Esp32p4DriverWatchdog_DisableTimer,
    .IsEnabled     = Esp32p4DriverWatchdog_IsEnabled,
    .SetTimeout    = Esp32p4DriverWatchdog_SetTimeout,
    .GetBootReason = Esp32p4DriverWatchdog_GetBootReason,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(Esp32p4DriverWatchdog, (ILTDriverWatchdog))

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceWatchdog, Esp32p4DriverWatchdog);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  23-Sep-26   claudius    created, from esp32c3/Esp32c3DriverWatchdogImpl.c
 */
