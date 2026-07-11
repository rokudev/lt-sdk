/*******************************************************************************
 * platforms/st/source/st/driver/watchdog/STDriverWatchdogImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>

#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_iwdg.h"
#include "stm32h7xx_hal_tim.h"

/* LSI oscillator nominal frequency driving the IWDG on STM32H7 */
static const u32 kIWDG_LSI_Hz           = 32000u;
static const u32 kIWDG_MaxReload        = 0x0FFFu;
static const u32 kIWDG_DefaultTimeoutMs = 5000u;

static IWDG_HandleTypeDef s_hIWDG;
static bool               s_bEnabled          = false;

/*******************************************************************************
 * Library initialization                                                      */

static bool STDriverWatchdogImpl_LibInit(void);
static void STDriverWatchdogImpl_LibFini(void);
static u32  STDriverWatchdogImpl_GetNumDeviceUnits(void);
static LTDeviceUnit STDriverWatchdogImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber);

/*******************************************************************************
 * Prescaler selection: choose the smallest prescaler that fits timeout in
 * [1, kIWDG_MaxReload] ticks.  Table is ordered smallest to largest.         */

static const struct {
    u32 code;
    u32 value;
} s_prescalers[] = {
    { IWDG_PRESCALER_4,   4   },
    { IWDG_PRESCALER_8,   8   },
    { IWDG_PRESCALER_16,  16  },
    { IWDG_PRESCALER_32,  32  },
    { IWDG_PRESCALER_64,  64  },
    { IWDG_PRESCALER_128, 128 },
    { IWDG_PRESCALER_256, 256 },
};

static void ConfigureTimeout(u32 timeoutMs) {
    if (timeoutMs == 0u) {
        s_hIWDG.Init.Prescaler = IWDG_PRESCALER_4;
        s_hIWDG.Init.Reload    = 0u;
        return;
    }
    for (u32 i = 0u; i < sizeof(s_prescalers) / sizeof(s_prescalers[0]); ++i) {
        u32 reload = (timeoutMs * kIWDG_LSI_Hz) / (1000u * s_prescalers[i].value);
        if (reload >= 1u && reload <= kIWDG_MaxReload) {
            s_hIWDG.Init.Prescaler = s_prescalers[i].code;
            s_hIWDG.Init.Reload    = reload;
            return;
        }
    }
    /* Requested timeout exceeds hardware maximum (~32 s); clamp to maximum. */
    s_hIWDG.Init.Prescaler = IWDG_PRESCALER_256;
    s_hIWDG.Init.Reload    = kIWDG_MaxReload;
}

/*******************************************************************************
 * TIM6 auto-tickle ISR — keeps the IWDG fed while the watchdog is "disabled" */

static TIM_HandleTypeDef s_hTIM6;

static void TickleTimerISR(void) {
    HAL_IWDG_Refresh(&s_hIWDG);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM6)
        TickleTimerISR();
}

void TIM6_DAC_IRQHandler(void) {
    HAL_TIM_IRQHandler(&s_hTIM6);
}

static void EnableTimerInterrupt(void) {
    __HAL_RCC_TIM6_CLK_ENABLE();

    /* Divide PCLK1 into a 1-second period across two 16-bit registers.
     * Period divisor = 10000; prescaler covers the rest. */
    u32 pclk1                    = HAL_RCC_GetPCLK1Freq();
    s_hTIM6.Instance             = TIM6;
    s_hTIM6.Init.Prescaler       = (u32)(pclk1 / 10000u) - 1u;
    s_hTIM6.Init.Period          = 10000u - 1u;
    s_hTIM6.Init.CounterMode     = TIM_COUNTERMODE_UP;
    s_hTIM6.Init.ClockDivision   = TIM_CLOCKDIVISION_DIV1;
    s_hTIM6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&s_hTIM6);

    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    HAL_TIM_Base_Start_IT(&s_hTIM6);
}

static void DisableTimerInterrupt(void) {
    HAL_TIM_Base_Stop_IT(&s_hTIM6);
    HAL_NVIC_DisableIRQ(TIM6_DAC_IRQn);
}

/*******************************************************************************
 * ILTDriverWatchdog implementation                                            */

static bool STDriverWatchdog_ResetTimer(void) {
    if (s_bEnabled)
        HAL_IWDG_Refresh(&s_hIWDG);
    return true;
}

static bool STDriverWatchdog_EnableTimer(void) {
    if (! s_bEnabled) {
        HAL_IWDG_Init(&s_hIWDG);
        DisableTimerInterrupt();
        s_bEnabled = true;
    }
    return true;
}

static bool STDriverWatchdog_DisableTimer(void) {
    /* The STM32H7 IWDG cannot be stopped once started; instead, set it to the max value allowed and then set it to tickle from an ISR. */
    if (s_bEnabled) {
        IWDG_HandleTypeDef hIWDG;
        hIWDG.Instance       = IWDG1;
        hIWDG.Init.Window    = IWDG_WINDOW_DISABLE;
        hIWDG.Init.Prescaler = IWDG_PRESCALER_256;
        hIWDG.Init.Reload    = kIWDG_MaxReload;
        HAL_IWDG_Init(&hIWDG);
        EnableTimerInterrupt();
        s_bEnabled = false;
    }
    return true;
}

static bool STDriverWatchdog_IsEnabled(void) {
    return s_bEnabled;
}

static bool STDriverWatchdog_SetTimeout(LTTime timeout) {
    ConfigureTimeout((u32)LTTime_GetMilliseconds(timeout));
    if (s_bEnabled) HAL_IWDG_Init(&s_hIWDG);
    return true;
}

static void STDriverWatchdog_Reboot(void) {
    HAL_NVIC_SystemReset();
}

/* externing function definitions is illegal in LT, except where necessary in drivers.
   Since the boot reason can only be determined before clocks are configured, it is
   necessary to read it in STStartup and retrieve it from there as well. */
extern LTBootReason STStartup_GetBootReason(const char ** pBootReasonString);

define_LTLIBRARY_INTERFACE(ILTDriverWatchdog) {
    .Reboot        = STDriverWatchdog_Reboot,
    .ResetTimer    = STDriverWatchdog_ResetTimer,
    .EnableTimer   = STDriverWatchdog_EnableTimer,
    .DisableTimer  = STDriverWatchdog_DisableTimer,
    .IsEnabled     = STDriverWatchdog_IsEnabled,
    .SetTimeout    = STDriverWatchdog_SetTimeout,
    .GetBootReason = STStartup_GetBootReason
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(STDriverWatchdog, (ILTDriverWatchdog))

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceWatchdog, STDriverWatchdog);

static bool STDriverWatchdogImpl_LibInit(void) {
    s_hIWDG.Instance    = IWDG1;
    s_hIWDG.Init.Window = IWDG_WINDOW_DISABLE;
    ConfigureTimeout(kIWDG_DefaultTimeoutMs);
    return true;
}

static void STDriverWatchdogImpl_LibFini(void) {
}

static u32 STDriverWatchdogImpl_GetNumDeviceUnits(void) {
    return 0;
}

static LTDeviceUnit STDriverWatchdogImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LT_UNUSED(nDeviceUnitNumber);
    return 0;
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  02-Jan-21   constantine created
 *  11-Jan-21   augustus    copied from Dan's Halford version
 *  09-Jul-26   augustus    implemented using STM32H7 IWDG HAL
 */
