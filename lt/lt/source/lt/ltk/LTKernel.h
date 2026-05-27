/******************************************************************************
 * lt/source/lt/ltk/LTKernel.h                                       LTK Kernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_LTK_LTKERNEL_H
#define ROKU_LT_SOURCE_LT_LTK_LTKERNEL_H

#include <lt/LTTypes.h>
#include "../core/LTKernel.h"
#include "../core/LTThreadImpl.h"

/**********************
 * Common Definitions */

#ifdef LT_LITTLEENDIAN
  #define IS_LITTLEENDIAN    1
#else
  #define IS_LITTLEENDIAN    0
#endif

#define LTK_WEAK             __attribute__((weak))

enum {
    kLTKNoSuchThread        = 0,
    kLTKStackFillValue      = 0xca110911,
    kLTKNumThreadPriorities = kLTThread_PrivateNumberOfPriorities
};

/***********************
 * System Tick Counter */
typedef union {
    u64 nCount;
    struct {
#if IS_LITTLEENDIAN
        u32 nLower;
        u32 nUpper;
#else
        u32 nUpper;
        u32 nLower;
#endif
    };
} Tick;

/***********************************
 * List of supported architectures */
#define LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V6M         0
#define LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V7M         1
#define LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V8M_BASE    2
#define LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V8M_MAIN    3
#define LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V8M         LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V8M_MAIN
#define LT_LTKERNEL_ARCHITECTURE_ARM_V5                 4
#define LT_LTKERNEL_ARCHITECTURE_XTENSA                 5
#define LT_LTKERNEL_ARCHITECTURE_RISC_V                 6
#define LT_LTKERNEL_ARCHITECTURE_BSP_DEFINED_HOST_OS    7
/* The numerical indexes (above) don't matter as long as they are unique */

#if defined(LT_LTKERNEL_ARCHITECTURE)
  #if (LT_LTKERNEL_ARCHITECTURE == LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V6M)
    #include "LTKArchArmCortexM_Base.h"
  #elif (LT_LTKERNEL_ARCHITECTURE == LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V7M)
    #include "LTKArchArmCortexM_Main.h"
  #elif (LT_LTKERNEL_ARCHITECTURE == LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V8M_BASE)
    #include "LTKArchArmCortexM_V8.h"
    #include "LTKArchArmCortexM_Base.h"
  #elif (LT_LTKERNEL_ARCHITECTURE == LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V8M_MAIN)
    #include "LTKArchArmCortexM_V8.h"
    #include "LTKArchArmCortexM_Main.h"
  #elif (LT_LTKERNEL_ARCHITECTURE == LT_LTKERNEL_ARCHITECTURE_ARM_V5)
    #include "LTKArchArm_V5.h"
  #elif (LT_LTKERNEL_ARCHITECTURE == LT_LTKERNEL_ARCHITECTURE_XTENSA)
    #include "LTKArchXtensa.h"
  #elif (LT_LTKERNEL_ARCHITECTURE == LT_LTKERNEL_ARCHITECTURE_RISC_V)
    #include "LTKArchRISC_V.h"
  #elif (LT_LTKERNEL_ARCHITECTURE == LT_LTKERNEL_ARCHITECTURE_BSP_DEFINED_HOST_OS)
  #else
    #error "LT_LTKERNEL_ARCHITECTURE not supported (see platform Makefile.config)"
  #endif
#else
  #error "LT_LTKERNEL_ARCHITECTURE must be defined in platform Makefile.config"
#endif

/****************
 * Kernel Style *
 *  Style 1 - The scheduler is always invoked on interrupt return; unmaskable service calls are used
 *              to yield from thread contexts.
 *  Style 2 - The scheduler is implemented as a standalone maskable software interrupt and is invoked
 *              as required from interrupts or thread contexts.
 */
#define LTKERNEL_STYLE_1    1
#define LTKERNEL_STYLE_2    2

#if   LTKERNEL_STYLE == LTKERNEL_STYLE_1
#elif LTKERNEL_STYLE == LTKERNEL_STYLE_2
#else
  #error "Must specify LTKERNEL_STYLE"
#endif

/*********************
 * Private functions */
void _LTKInitialize(const LTCoreBSP *, LTCoreBSP_LTCoreLogFunction *, void (*)(void *));
bool _LTKSetTimeout(u64);
void _LTKInitThread(LTKThread *, u8, void *, s32, u8 *, u32);
void _LTKThreadExit(void);
void _LTKStackCheck(LTKThread * pThread) LT_ISR_SAFE;

/****************
 * Private data */
extern LTKList       _LTK_runQueues[kLTKNumThreadPriorities];
extern LTKThread   * _LTK_pRunningThread;
extern LTKThread     _LTK_idleThread;
extern u8            _LTK_idleThreadStack[];

#endif // #ifndef ROKU_LT_SOURCE_LT_LTK_LTKERNEL_H

