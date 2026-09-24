/*
 * SPDX-FileCopyrightText: 2020-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "soc/soc_caps.h"
#include "soc/assist_debug_reg.h"
#include "esp_bit_defs.h"
#include "esp_attr.h"
#include "riscv/csr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Only the entries the bootloader reaches are defined here.  The esp32c3 file of
 * this name carries the breakpoint, watchpoint and dedicated-GPIO helpers too;
 * none of them is expanded by anything this library builds, and the trigger and
 * fast-GPIO CSR layouts differ on this part, so guessing at them would be worse
 * than leaving them out. */

static inline int IRAM_ATTR cpu_ll_get_core_id(void)
{
    return RV_READ_CSR(mhartid);
}

/* This part has no performance-counter CSR block - the esp32c3 reads cycles from
 * CSR_PCCR_MACHINE, here it is the architectural mcycle. */
static inline uint32_t IRAM_ATTR cpu_ll_get_cycle_count(void)
{
    return RV_READ_CSR(mcycle);
}

static inline void IRAM_ATTR cpu_ll_set_cycle_count(uint32_t val)
{
    RV_WRITE_CSR(mcycle, val);
}

static inline void *cpu_ll_get_sp(void)
{
    void *sp;
    asm volatile ("mv %0, sp;" : "=r" (sp));
    return sp;
}

static inline void cpu_ll_init_hwloop(void)
{
    /* The hardware loop unit needs no bootloader-time setup. */
}

FORCE_INLINE_ATTR bool cpu_ll_is_debugger_attached(void)
{
    return REG_GET_BIT(ASSIST_DEBUG_CORE_0_DEBUG_MODE_REG, ASSIST_DEBUG_CORE_0_DEBUG_MODULE_ACTIVE);
}

static inline void cpu_ll_break(void)
{
    asm volatile("ebreak\n");
}

static inline void cpu_ll_waiti(void)
{
    asm volatile ("wfi\n");
}

#ifdef __cplusplus
}
#endif
