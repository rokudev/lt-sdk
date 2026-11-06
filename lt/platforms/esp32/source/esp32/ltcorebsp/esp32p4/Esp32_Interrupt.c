/******************************************************************************
 * Esp32_Interrupt.c                                               ESP32-P4 BSP
 *
 * Interrupt dispatch, the system tick, and the cycle counter - the three things
 * LTCoreBSP_RISCV_SystemConfig asks a RISC-V BSP for beyond a stack top.
 *
 * The division of labour is the esp32c3's: LTK hands the raw mcause to
 * pDispatcher and expects the BSP to have done everything else, because a
 * RISC-V core has one trap vector and no notion of which peripheral caused it.
 * LTKSetInterruptVector() and LTKSetInterruptPriority() are no-ops in that port,
 * so the vector table below is the only one there is.
 *
 * What differs is the controller.  This part has a CLIC, which numbers its
 * lines from 16 - see Esp32_Irq.h - and reports that number in mcause, so the
 * dispatcher has to take the bias back off before it can index a table the rest
 * of the BSP addresses by CPU line.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/bsp/LTCoreBSP.h>

#include "Esp32_Irq.h"
#include "Esp32_Registers.h"
#include "Esp32_SoC.h"
#include "Esp32_Clock.h"
#include "Esp32_Interrupt.h"

/*___________________
  constants        */

enum {
    /* Must match kLTKTicksPerSecond, which lives in LTK's private header */
    kEsp32_TickRateHz               = 1000,
    /*
     * Systimer ticks per LT system tick.  The counter runs at a fixed 16MHz
     * whatever the CPU is doing, so this is 16000 - comfortably inside
     * TARGET0_CONF's 26 bit PERIOD field.
     */
    kEsp32_TickPeriod               = kEsp32_RegisterSYSTIMER_TICK_HZ / kEsp32_TickRateHz,
};

/*___________________
  static variables */

/* One slot per CPU interrupt line.  Line 0 is the matrix's "detached" encoding
 * rather than a line, so its slot is never used. */
static Esp32_InterruptVector * s_vectorTable[kEsp32_IrqNumber_Count];

/*___________________
  the systimer     */

/*
 * A snapshot of UNIT0.
 *
 * The counter is 52 bits across two registers and free running, so it cannot be
 * read in one go: writing UPDATE latches the pair, VALUE_VALID says the latch is
 * ready, and only then are VALUE_HI/VALUE_LO consistent with each other.
 */
static u64 LT_ISR_SAFE
_Esp32_SystimerCount(void) {
    ESP32_REG(SYSTIMER_UNIT0_OP) = ESP32_REG_MASK(SYSTIMER_UNIT0_OP, UPDATE);
    while (!(ESP32_REG(SYSTIMER_UNIT0_OP) & ESP32_REG_MASK(SYSTIMER_UNIT0_OP, VALUE_VALID)));
    u32 nUpper = ESP32_REG(SYSTIMER_UNIT0_VALUE_HI);
    u32 nLower = ESP32_REG(SYSTIMER_UNIT0_VALUE_LO);
    return ((u64)nUpper << 32) | nLower;
}

/*
 * The system tick.
 *
 * TARGET0 is in period mode, so the comparator re-arms itself at now + PERIOD
 * each time it fires and the handler has nothing to reprogram.  What it does have
 * to do is say how many ticks have passed, not how many interrupts it has taken:
 * a stretch with interrupts disabled longer than a millisecond - a flash erase,
 * say - yields one interrupt covering several tick periods, and returning 1 for
 * it would leave the clock permanently behind.  Hence the read back of the
 * counter and the division.
 *
 * The comparator is acknowledged before the counter is read, not after.  The
 * other order can clear a period boundary crossed in between and lose a tick.
 */
static u32 LT_ISR_SAFE
Esp32_TickISR(void) {
    static u64 s_nLastTickCount = 0;
    ESP32_REG(SYSTIMER_INT_CLR) = ESP32_REG_MASK(SYSTIMER_INT, TARGET0);

    u64 nNow     = _Esp32_SystimerCount();
    u64 nElapsed = nNow - s_nLastTickCount;
    u32 nTicks   = (u32)(nElapsed / kEsp32_TickPeriod);

    /* Keep the remainder, so a tick is never rounded away across two interrupts */
    s_nLastTickCount += (u64)nTicks * kEsp32_TickPeriod;

    return nTicks;
}

void
Esp32_InitializeTick(void) {
    /* TARGET0 watching UNIT0, reloading every kEsp32_TickPeriod counts.  The
     * HI/LO pair is unused in period mode but is staged along with CONF, so it is
     * cleared rather than left holding whatever the ROM put there. */
    ESP32_REG(SYSTIMER_TARGET0_HI)    = 0;
    ESP32_REG(SYSTIMER_TARGET0_LO)    = 0;
    ESP32_REG(SYSTIMER_TARGET0_CONF)  = ESP32_REG_MASK(SYSTIMER_TARGET0_CONF, PERIOD_MODE)
                                      | (kEsp32_TickPeriod << ESP32_REG_SHIFT(SYSTIMER_TARGET0_CONF, PERIOD));
    ESP32_REG(SYSTIMER_COMP0_LOAD)    = kEsp32_RegisterSYSTIMER_COMP0_LOAD_M;

    /*
     * The comparator is enabled only now, after COMP0_LOAD has latched the period.
     * Enabling it while PERIOD still read as the reset 0 leaves it wedged: it never
     * reaches a compare and TARGET0 never fires, so the tick is simply dead.  IDF's
     * systimer_hal does the same dance, disabling the alarm around any period change.
     */
    ESP32_REG(SYSTIMER_CONF)         |= ESP32_REG_MASK(SYSTIMER_CONF, TARGET0_WORK_EN);

    ESP32_REG(SYSTIMER_INT_CLR)       = ESP32_REG_MASK(SYSTIMER_INT, TARGET0);
    ESP32_REG(SYSTIMER_INT_ENA)       = ESP32_REG_MASK(SYSTIMER_INT, TARGET0);

    Esp32_AttachInterrupt(kEsp32_ExternalIrq_SystemTimer0, kEsp32_IrqNumber_SystemTick,
                          kEsp32_IrqType_Level, kEsp32_IrqPriority_SystemTick, Esp32_TickISR);
}

/*___________________
  the cycle counter */

/*
 * UNIT0 is also LTK's clock, so it is started here rather than in
 * Esp32_InitializeTick(): LTCore reads the counter well before the tick is armed,
 * and LTKInitialize() caches the rate.  The comparator is left alone - see there.
 *
 * UNIT0_WORK_EN resets set on this part, so the counter is already running; the
 * write below is still made so that the counter's state does not depend on what
 * the ROM and the bootloader happened to leave behind.
 *
 * The counter is frozen while a debugger has the core halted, so a breakpoint
 * does not yield a burst of catch-up ticks on resume.
 */
void
Esp32_InitializeCycleCount(void) {
    Esp32_ClockEnablePeripheralClock(kEsp32_Clock_SYSTIMER);

    ESP32_REG(SYSTIMER_UNIT0_LOAD_HI) = 0;
    ESP32_REG(SYSTIMER_UNIT0_LOAD_LO) = 0;
    ESP32_REG(SYSTIMER_UNIT0_LOAD)    = kEsp32_RegisterSYSTIMER_UNIT0_LOAD_M;
    ESP32_REG(SYSTIMER_CONF)          = ESP32_REG_MASK(SYSTIMER_CONF, CLK_EN)
                                      | ESP32_REG_MASK(SYSTIMER_CONF, UNIT0_WORK_EN)
                                      | ESP32_REG_MASK(SYSTIMER_CONF, UNIT0_CORE0_STALL_EN);
}

/*
 * The systimer, not the RISC-V cycle CSR.
 *
 * LTCore's monotonic clock is this function scaled by nClockSpeedHz, so it has to
 * be a wall clock.  A CPU cycle counter is not: it stops dead for as long as the
 * idle thread sits in wfi, which leaves LTTime running at whatever fraction of
 * real time the CPU happens to be busy and every timeout in the system
 * correspondingly late.  UNIT0 keeps running at a fixed 16MHz regardless.
 *
 * 52 bits at 16MHz is 8.9 years, so unlike a 32 bit counter this needs no
 * software wrap carry.
 *
 * NOTE: interrupts must be disabled by the caller - all three call sites in
 * LTKArchRISC_V.c are.  The UPDATE/VALUE_VALID latch is single and shared with
 * Esp32_TickISR().
 */
u64 LT_ISR_SAFE
Esp32_GetCycleCount(void) {
    return _Esp32_SystimerCount();
}

/*___________________
  dispatch         */

u32 LT_ISR_SAFE
Esp32_InterruptDispatcher(u32 mcause) {
    /*
     * The low bits of mcause are the CLIC line number, which is the CPU line
     * biased by kEsp32_CLIC_ExternalBase - so the bias comes off here before the
     * table is indexed.  LTK masks the high bits off on the exception path only;
     * an interrupt arrives with bit 31 still set.
     */
    u32 nLine = mcause & 0x3ff;

    if (nLine < kEsp32_CLIC_ExternalBase) return 0;
    nLine -= kEsp32_CLIC_ExternalBase;

    if (nLine >= kEsp32_IrqNumber_Count) return 0;

    Esp32_InterruptVector * pVector = s_vectorTable[nLine];
    if (!pVector) return 0;

    u32 nTicks = (*pVector)();

    /* Only the tick line may advance the clock, whatever a handler returns */
    return (nLine == kEsp32_IrqNumber_SystemTick) ? nTicks : 0;
}

void
Esp32_AttachInterrupt(Esp32_ExternalIrq   nExternalIrq,
                      Esp32_IrqNumber     nCpuIrq,
                      Esp32_IrqType       nType,
                      Esp32_IrqPriority   nPriority,
                      Esp32_InterruptVector * pVector) {
    u32 nMask = Esp32DisableInterrupts();

    Esp32DisableCPUIrq(nCpuIrq);
    s_vectorTable[nCpuIrq] = pVector;
    Esp32SetCPUIrqType(nCpuIrq, nType);
    Esp32SetCPUIrqPriority(nCpuIrq, nPriority);
    Esp32ClearCPUIrq(nCpuIrq);
    Esp32MapExternalToCPUIrq(kEsp32_CPU0, nExternalIrq, nCpuIrq);
    Esp32EnableCPUIrq(nCpuIrq);

    Esp32EnableInterrupts(nMask);
}

void
Esp32_DetachInterrupt(Esp32_ExternalIrq nExternalIrq, Esp32_IrqNumber nCpuIrq) {
    u32 nMask = Esp32DisableInterrupts();

    Esp32DisableCPUIrq(nCpuIrq);
    Esp32MapExternalToCPUIrq(kEsp32_CPU0, nExternalIrq, kEsp32_IrqNumber_Detached);
    s_vectorTable[nCpuIrq] = NULL;

    Esp32EnableInterrupts(nMask);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Sep-26   claudius    created
 */
