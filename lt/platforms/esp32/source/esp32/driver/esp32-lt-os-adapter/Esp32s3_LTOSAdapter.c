/*******************************************************************************
 *
 * Esp32s3 half of the binary WiFi and BLE driver adapter for LT
 * -------------------------------------------------------------
 *
 * Everything here is either a register the esp32s3 places somewhere the esp32
 * does not, or an entry point the esp32 gets from ROM and this part does not.
 * See Esp32s3_LTOSAdapter.h for why the shared adapter cannot hold these
 * itself.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <esp32s3/Esp32_Clock.h>
#include <esp32s3/Esp32_Registers.h>
#include <rom/ets_sys.h>
#include "Esp32s3_LTOSAdapter.h"
#include "Esp32_LTOSAdapterOsi.h"

DEFINE_LTLOG_SECTION("esp32s3.osadapter");

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Number of fractional bits in a calibrated RTC slow clock period, and the
 * narrower fixed point the Wi-Fi light sleep timer wants.  RTC_CLK_CAL_FRACT
 * and SOC_WIFI_LIGHT_SLEEP_CLK_WIDTH in the SDK.
 */

#define ESP32S3_RTC_CLK_CAL_FRACT       19
#define ESP32S3_WIFI_LIGHT_SLEEP_WIDTH  12

#define ESP32S3_MAC_LEN                 6

/****************************************************************************
 * Name: Esp32s3_XtalFreqRegRead
 *
 * Description:
 *   Read the crystal frequency the bootloader recorded in RTC_XTAL_FREQ_REG.
 *
 * Returned Value:
 *   The raw register value, two 16-bit copies of the frequency in MHz.
 *
 ****************************************************************************/

u32 Esp32s3_XtalFreqRegRead(void)
{
    return ESP32_REG(RTC_CNTL_STORE4);
}

/****************************************************************************
 * Name: Esp32s3_ReadEfuseMac
 *
 * Description:
 *   Read the factory base MAC address out of efuse.  There is no MAC CRC on
 *   this part - the byte the esp32 keeps one in belongs to SPI_PAD_CONF_0 here
 *   - so there is nothing to validate the result against.
 *
 * Input Parameters:
 *   mac - MAC address buffer pointer
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

s32 Esp32s3_ReadEfuseMac(u8 mac[ESP32S3_MAC_LEN])
{
    u32 regval[2];
    u8 *data = (u8 *)regval;

    regval[0] = ESP32_REG(EFUSE_RD_MAC_SPI_SYS_0);
    regval[1] = ESP32_REG(EFUSE_RD_MAC_SPI_SYS_1);

    /* The address reads back least significant byte first. */

    for (int i = 0; i < ESP32S3_MAC_LEN; i++) {
        mac[i] = data[5 - i];
    }

    return 0;
}

/****************************************************************************
 * Name: Esp32s3_RandomRegRead
 *
 * Description:
 *   One sample from the hardware random number generator.
 *
 ****************************************************************************/

u32 Esp32s3_RandomRegRead(void)
{
    return ESP32_REG(WDEV_RND);
}

/****************************************************************************
 * Name: Esp32s3_WiFiResetMac
 *
 * Description:
 *   Reset the Wi-Fi hardware MAC by pulsing its bit in WIFI_RST_EN.  The other
 *   bits in that register belong to the BT MAC and to the shared basebands, so
 *   this is a read-modify-write under interrupt lock rather than a bare store.
 *
 ****************************************************************************/

void Esp32s3_WiFiResetMac(void)
{
    LTCore *pCore = LT_GetCore();

    LT_SIZE flags = pCore->Disable();
    ESP32_REG(APB_CTRL_WIFI_RST_EN) |= ESP32_REG_MASK(APB_CTRL_WIFI_RST, WIFIMAC);
    pCore->Enable(flags);

    flags = pCore->Disable();
    ESP32_REG(APB_CTRL_WIFI_RST_EN) &= ~ESP32_REG_MASK(APB_CTRL_WIFI_RST, WIFIMAC);
    pCore->Enable(flags);
}

/****************************************************************************
 * Name: Esp32s3_WiFiBtPowerDomainOn / Esp32s3_WiFiBtPowerDomainOff
 *
 * Description:
 *   Bring the shared Wi-Fi/BT modem power domain up, or put it back.  The
 *   esp32 has no such domain; on this part the modem sits behind a power gate
 *   and an isolation cell, and comes out of power-up needing a reset pulse
 *   across every block in MODEM_RESET_FIELD_WHEN_PU.  Until that happens the
 *   PHY registers do not answer, and register_chipv7_phy() never returns.
 *
 *   Wi-Fi and BLE share the domain, so this is reference counted the way IDF's
 *   esp_wifi_bt_power_domain_on() is: the reset pulse must not land under a
 *   controller that is already running.
 *
 ****************************************************************************/

static u32 s_nPowerDomainRefs = 0;

void Esp32s3_WiFiBtPowerDomainOn(void)
{
    LTCore *pCore = LT_GetCore();

    LT_SIZE flags = pCore->Disable();
    if (s_nPowerDomainRefs++ == 0) {
        /* The per-block modem clocks IDF turns on once in esp_perip_clk_init()
         * and never gates again.  Without the Wi-Fi MAC bit the MAC registers
         * read back zero and ignore writes, and hal_init() spins forever
         * waiting on a ready bit that can never set. */

        ESP32_REG(APB_CTRL_WIFI_CLK_EN) |= kEsp32_RegisterAPB_CTRL_WIFI_CLK_WIFI_MAC_EN_M |
                                           kEsp32_RegisterAPB_CTRL_WIFI_CLK_BT_BASEBAND_EN_M |
                                           kEsp32_RegisterAPB_CTRL_WIFI_CLK_BT_LC_EN_M;

        ESP32_REG(RTC_CNTL_DIG_PWC) &= ~kEsp32_RegisterRTC_CNTL_DIG_PWC_WIFI_FORCE_PD_M;
        ets_delay_us(10);

        /* The reset pulse has to land while the shared clocks are running. */

        ESP32_REG(APB_CTRL_WIFI_CLK_EN) |= kEsp32_RegisterAPB_CTRL_WIFI_CLK_WIFI_BT_COMMON_M;
        ESP32_REG(APB_CTRL_WIFI_RST_EN) |= kEsp32_RegisterAPB_CTRL_WIFI_RST_MODEM_WHEN_PU_M;
        ESP32_REG(APB_CTRL_WIFI_RST_EN) &= ~kEsp32_RegisterAPB_CTRL_WIFI_RST_MODEM_WHEN_PU_M;

        ESP32_REG(RTC_CNTL_DIG_ISO) &= ~kEsp32_RegisterRTC_CNTL_DIG_ISO_WIFI_FORCE_ISO_M;

        ESP32_REG(APB_CTRL_WIFI_CLK_EN) &= ~kEsp32_RegisterAPB_CTRL_WIFI_CLK_WIFI_BT_COMMON_M;
    }
    pCore->Enable(flags);
}

void Esp32s3_WiFiBtPowerDomainOff(void)
{
    LTCore *pCore = LT_GetCore();

    LT_SIZE flags = pCore->Disable();
    if (s_nPowerDomainRefs > 0 && --s_nPowerDomainRefs == 0) {
        ESP32_REG(RTC_CNTL_DIG_ISO) |= kEsp32_RegisterRTC_CNTL_DIG_ISO_WIFI_FORCE_ISO_M;
        ESP32_REG(RTC_CNTL_DIG_PWC) |= kEsp32_RegisterRTC_CNTL_DIG_PWC_WIFI_FORCE_PD_M;
    }
    pCore->Enable(flags);
}

/****************************************************************************
 * Name: Esp32s3_SlowClkCalGet
 *
 * Description:
 *   The period of the radio low power clock, in the 12-bit fixed point the
 *   Wi-Fi light sleep timer uses rather than the 19-bit fixed point the system
 *   calibration is kept in.
 *
 *   When the low power clock is driven from the divided crystal the period is
 *   exactly 1us and there is nothing to measure.  Otherwise it comes from
 *   whoever last calibrated the RTC slow clock, and reads as zero if nobody
 *   has.
 *
 ****************************************************************************/

u32 Esp32s3_SlowClkCalGet(void)
{
    const u32 nShift = ESP32S3_RTC_CLK_CAL_FRACT - ESP32S3_WIFI_LIGHT_SLEEP_WIDTH;

    if (ESP32_REG(SYSTEM_BT_LPCK_DIV_FRAC) & ESP32_REG_MASK(SYSTEM_BT_LPCK_DIV_FRAC, LPCLK_SEL_XTAL)) {
        /* A period of exactly 1us, in the narrower fixed point. */

        return 1u << ESP32S3_WIFI_LIGHT_SLEEP_WIDTH;
    }

    return ESP32_REG(RTC_CNTL_STORE1) >> nShift;
}

/****************************************************************************
 * Name: Esp32s3_CpuClockMHzGet
 *
 * Description:
 *   The CPU clock in MHz.  The esp32 reads this from the ROM global
 *   g_ticks_per_us_pro, which this ROM does not keep.
 *
 ****************************************************************************/

u32 Esp32s3_CpuClockMHzGet(void)
{
    return Esp32_ClockGetMHz();
}

/****************************************************************************
 * Name: Esp32s3_MapRadioIrq
 *
 * Description:
 *   Point one peripheral interrupt source at one CPU line on core 0.  The
 *   radio blobs choose their own line numbers - the Wi-Fi MAC takes line 0 and
 *   the BLE controller's r_intc_init asks for RWBLE on 5 and BT baseband on 8 -
 *   so the number is checked rather than dictated here.  A line the level 1
 *   dispatcher cannot reach would leave the source silently dead, and line 6
 *   would displace LTK's tick.
 *
 * Input Parameters:
 *   nCpu         - core to route to; only core 0 runs LT
 *   nExternalIrq - peripheral source, an ETS_*_INTR_SOURCE value
 *   nCpuIrq      - Xtensa CPU interrupt line
 *
 * Returned Value:
 *   true if the source was routed, false if nothing was written
 *
 ****************************************************************************/

bool Esp32s3_MapRadioIrq(s32 nCpu, u32 nExternalIrq, u32 nCpuIrq)
{
    if (nCpu != kEsp32_CPU0) {
        LTLOG_REDALERT("irq.map", "only core 0 is routed, refusing core %ld", LT_Ps32(nCpu));
        return false;
    }

    if (!Esp32IrqLineIsAssignable((Esp32_IrqNumber)nCpuIrq)) {
        LTLOG_REDALERT("irq.map", "cpu line %lu takes no peripheral, source %lu not routed",
                       LT_Pu32(nCpuIrq), LT_Pu32(nExternalIrq));
        return false;
    }

    Esp32MapExternalToCPUIrq(kEsp32_CPU0, (Esp32_ExternalIrq)nExternalIrq, (Esp32_IrqNumber)nCpuIrq);
    return true;
}

/****************************************************************************
 * Name: abort
 *
 * Description:
 *   libpp and libcoexist call abort() on internal invariant failures.  The
 *   esp32 resolves it from ROM through esp32.rom.redefined.ld; this ROM has no
 *   equivalent, so the blobs need one supplied.  There is nothing to recover
 *   to - the radio is in an undefined state by the time it is reached - so
 *   this reports and stops.
 *
 ****************************************************************************/

LT_NORETURN void abort(void)
{
    LTLOG_REDALERT("wl.abort", "wireless blob called abort()");
    LT_GetCore()->DebugBreak();

    for (;;) {
    }
}

/****************************************************************************
 * ets_timer
 *
 * The esp32 takes these from ROM through
 * mastering/ld/esp32/rom/esp32.rom.redefined.ld; the esp32s3 ROM exports only
 * ets_delay_us, and IDF supplies the rest from esp_timer's
 * ets_timer_legacy.c.  The WPA supplicant embeds an ETSTimer in wpa_sm, in
 * wpa_authenticator and in the WPS state machine, and arms it in place.
 *
 * ETSTimer is layout-identical to the struct ets_timer the shared adapter's
 * timer wrappers already work on - five words, timer_arg lining up with the
 * priv field they hang their own bookkeeping off - so these are forwarders,
 * not a second implementation.  Those wrappers run their callbacks on the
 * Wi-Fi timer thread, which esp_wifi_init creates, so nothing here may be
 * armed before the radio is up.
 ****************************************************************************/

void ets_timer_init(void)
{
    /* No global state to set up; the wrappers allocate per timer on setfn. */
}

void ets_timer_deinit(void)
{
}

void ets_timer_arm(ETSTimer *timer, uint32_t tmout, bool repeat)
{
    LTEsp32OSAdapter_GetPrimitives()->TimerArm(timer, tmout, repeat);
}

void ets_timer_arm_us(ETSTimer *ptimer, uint32_t us, bool repeat)
{
    LTEsp32OSAdapter_GetPrimitives()->TimerArmUs(ptimer, us, repeat);
}

void ets_timer_disarm(ETSTimer *timer)
{
    LTEsp32OSAdapter_GetPrimitives()->TimerDisarm(timer);
}

void ets_timer_setfn(ETSTimer *ptimer, ETSTimerFunc *pfunction, void *parg)
{
    LTEsp32OSAdapter_GetPrimitives()->TimerSetFn(ptimer, (void *)pfunction, parg);
}

void ets_timer_done(ETSTimer *ptimer)
{
    LTEsp32OSAdapter_GetPrimitives()->TimerDone(ptimer);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created
 */
