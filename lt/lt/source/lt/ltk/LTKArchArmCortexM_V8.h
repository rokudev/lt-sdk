/******************************************************************************
 * lt/source/lt/ltk/LTKArchArmCortexM_V8.h                      LTK Microkernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHARMCORTEXM_V8_H
#define ROKU_LT_SOURCE_LT_LTK_LTKARCHARMCORTEXM_V8_H

#if defined(LTK_ARCH_ARM_CORTEX_M_ENABLE_PRIVATE_DEFS)
  #if (defined(__ARM_ARCH_8M_MAIN__) && __ARM_ARCH_8M_MAIN__ == 1U) || (defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3))
    #define LTK_ARCH_ARM_CORTEX_M_SPLIM_SECTION \
        asm volatile ( "MSR psplim, %0" : : "r" (pRunThread->pStackBottom) );
  #endif
  #define LTK_ARCH_ARM_CORTEX_M_EXC_RETURN_UNSECURE   0xffffffbc
  #define LTK_ARCH_ARM_CORTEX_M_SET_EXC_RETURN_SECTION \
      if (LTK_REG(CPUID_NS) == 0) { \
          s_nInitialExcReturn = LTK_ARCH_ARM_CORTEX_M_EXC_RETURN_UNSECURE; \
      }
  #if (!defined(__ARM_ARCH_8M_MAIN__) && !defined(__ARM_ARCH_8M_BASE__))
    #error "Error: Not compiling for ARM Arch v8m!"
  #endif

  // ARM Cortex-M Register Access

  // SCB:CPUID_NS
  #define LTK_ARCH_ARM_CORTEX_M_REG_CPUID_NS       (*(volatile u32 *)0xe002ed00)

  // SCB:DAUTHCTRL
  #define LTK_ARCH_ARM_CORTEX_M_REG_DAUTHCTRL      (*(volatile u32 *)0xe000ee04)
  // SPIDENSEL(0), SPNIDENSEL(2) bits are set: secure debugging is controlled by
  // INTSPIDEN(1) bit, which is not set - effectively disables secure debugging
  #define LTK_ARCH_ARM_CORTEX_M_VAL_DAUTHCTRL_SPIDENSEL	   (1<<0)
  #define LTK_ARCH_ARM_CORTEX_M_VAL_DAUTHCTRL_INTSPIDEN	   (1<<1)
  #define LTK_ARCH_ARM_CORTEX_M_VAL_DAUTHCTRL_SPNIDENSEL   (1<<2)
  #define LTK_ARCH_ARM_CORTEX_M_VAL_DIS_SEC_DBG    (LTK_ARCH_ARM_CORTEX_M_VAL_DAUTHCTRL_SPIDENSEL | LTK_ARCH_ARM_CORTEX_M_VAL_DAUTHCTRL_SPNIDENSEL)
  // DHCSR
  #define LTK_ARCH_ARM_CORTEX_M_REG_DHCSR          (*(volatile u32 *)0xe000edf0)
  #define LTK_ARCH_ARM_CORTEX_M_VAL_DHCSR_DEBUG_ENABLED    (1<<0)
  // Interrupts
  #if (defined (__ARM_ARCH_8M_BASE__) && __ARM_ARCH_8M_BASE__ == 1U)
     #define LTK_ARCH_ARM_CORTEX_M_MAX_IRQ         239
  #else
     #define LTK_ARCH_ARM_CORTEX_M_MAX_IRQ         479
  #endif
#endif

#endif // #ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHARMCORTEXM_V8_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  06-May-21   tiberius    created
 */
