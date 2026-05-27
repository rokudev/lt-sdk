/******************************************************************************
 * lt/source/lt/ltk/LTKArchXtensa.h                             LTK Microkernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHXTENSA_H
#define ROKU_LT_SOURCE_LT_LTK_LTKARCHXTENSA_H

#include <xtensa/config/core-isa.h>
#include <xtensa/config/specreg.h>
// Specify external registers implemented for the platform (if any)
//   If there are no such registers, extreg.h should be empty
#include <xtensa/config/extreg.h>

#include "LTKArchXtensa_Vectors.h"

#if !XCHAL_HAVE_XEA2
    #error "Xtensa implementation must use XEA2 architecture"
#endif

#if !XCHAL_HAVE_EXCEPTIONS || !XCHAL_HAVE_INTERRUPTS
    #error "Xtensa implementation must have exceptions and interrupts"
#endif

#if XCHAL_HAVE_BE
    #error "Xtensa implementation must be little endian"
#endif

#if __XTENSA_CALL0_ABI__ || !XCHAL_HAVE_WINDOWED
    #error "Xtensa CALL0 ABI not supported"
#endif

#if !XCHAL_HAVE_CCOUNT
    #error "Xtensa implementation must have CCOUNT timer"
#endif

/* Register Reads / Writes */

#define LTK_READ_SR(sr)               LTKArchXtensaReadSR_  ## sr ##_()
#define LTK_WRITE_SR(sr, value)       LTKArchXtensaWriteSR_ ## sr ##_(value)
#define LTK_WRITE_SR_RSYNC(sr, value) LTKArchXtensaWriteSRAndSync_ ## sr ##_(value)

#define LTKArchXtensaReadSpecialRegisterFunction(sr) \
    LT_INLINE u32 LTKArchXtensaReadSR_ ## sr ## _(void) { \
        u32 nValue; \
        asm volatile ( \
           "rsr %0, %1" \
               : "=r"(nValue) : "i"(sr) : \
        ); \
        return nValue; \
    }

#define LTKArchXtensaWriteSpecialRegisterFunction(sr) \
    LT_INLINE void LTKArchXtensaWriteSR_ ## sr ## _(u32 nValue) { \
        asm volatile ( \
           "wsr %0, %1" \
               : : "r"(nValue), "i"(sr) : \
        ); \
    }

#define LTKArchXtensaWriteSpecialRegisterAndSyncFunction(sr) \
    LT_INLINE void LTKArchXtensaWriteSRAndSync_ ## sr ## _(u32 nValue) { \
        asm volatile ( \
           "wsr %0, %1     \n" \
           "rsync" \
               : : "r"(nValue), "i"(sr) : \
        ); \
    }

/* Program state register */
LTKArchXtensaReadSpecialRegisterFunction(PS)
LTKArchXtensaWriteSpecialRegisterAndSyncFunction(PS)

/* Interrupt registers */
LTKArchXtensaReadSpecialRegisterFunction(INTERRUPT)
LTKArchXtensaReadSpecialRegisterFunction(INTENABLE)
LTKArchXtensaWriteSpecialRegisterFunction(INTENABLE)

/* Timer registers */
LTKArchXtensaReadSpecialRegisterFunction(CCOUNT)
LTKArchXtensaReadSpecialRegisterFunction(CCOMPARE_0)
LTKArchXtensaWriteSpecialRegisterFunction(CCOMPARE_0)

/* Fault registers */
LTKArchXtensaReadSpecialRegisterFunction(EXCCAUSE)
LTKArchXtensaReadSpecialRegisterFunction(EXCVADDR)

// Xtensa yields using an unmaskable service call
#define LTKERNEL_STYLE  LTKERNEL_STYLE_1

typedef struct {
    u32 nRegister[LTK_ARCH_XTENSA_SF_SIZE / 4];
} LTKStackFrame;

enum {
    kLTK_IdleThreadStackSize    = sizeof(LTKStackFrame) + LTK_ARCH_XTENSA_BSA_SIZE + 64,
    /* Xtensa adds 32 bytes to the stack per function call when sliding register window is used.
       add the overhead of 8 function calls to the base default */
    kLTK_DefaultThreadStackSize = 512 + 8 * 32     /* 768 bytes */
};

LT_INLINE bool InterruptsAreDisabled(void) LT_ISR_SAFE {
    u32 nMask = LTK_READ_SR(PS);
    return ((nMask & 0xf) == XCHAL_EXCM_LEVEL);
}

LT_INLINE bool InsideInterruptContext(void) LT_ISR_SAFE {
    extern volatile u32 _LTK_interruptNestingCounter;
    return _LTK_interruptNestingCounter > 0;
}

LT_INLINE u32 DisableInterruptsNested(void) LT_ISR_SAFE {
    u32 nMask;
    asm volatile (
       "rsil %0, %1"
           : "=r"(nMask) : "i"(XCHAL_EXCM_LEVEL) : "memory"
    );
    return nMask;
}

LT_INLINE void EnableInterruptsNested(u32 nMask) LT_ISR_SAFE {
    LTK_WRITE_SR_RSYNC(PS, nMask);
}

LT_INLINE bool InterruptsGotDisabled(u32 nMask) LT_ISR_SAFE {
    /* Return true if interrupts were originally enabled but then were disabled by last call to DisableInterruptsNested(). */
    return !(nMask & 0xf);
}

/* NOTE: LockScheduler() / UnlockScheduler() do not need to be called within kernel ISRs */
LT_INLINE u32 LockScheduler(void) {
    u32 nMask;
    asm volatile (
       "rsil %0, %1"
           : "=r"(nMask) : "i"(XCHAL_EXCM_LEVEL) : "memory"
    );
    return nMask;
}

LT_INLINE void UnlockScheduler(u32 nMask) {
    LTK_WRITE_SR_RSYNC(PS, nMask);
}

LT_INLINE void YieldThread(void) LT_ISR_SAFE {
    asm volatile ( "syscall" );
}

LT_INLINE void SetThreadState(LTKThread * pThread, LTKThreadState nState) LT_ISR_SAFE {
    asm volatile ( "dsync" );
    pThread->nState = nState;
}

LT_INLINE bool OCDIsAttached(void) LT_ISR_SAFE {
#if defined(DSRSET)
    u32 nDCR;
    asm volatile (
       "rer %0, %1"
           : "=r"(nDCR) : "r"(DSRSET) : "memory"
    );
    return (nDCR & 0x1);
#else
    return false;
#endif
}

LT_INLINE void BreakIntoOCD(void) LT_ISR_SAFE {
    asm volatile ( "break 1,15" );
}

LT_INLINE void *GetStackPointer(void) {
    void *pStack;
    asm volatile (
       "mov %0, a1"
           : "=r"(pStack) : : "memory"
    );
    return pStack;
}

#define LTK_ARCH_XTENSA_MAX_INTERRUPTS          32

#endif // #ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHXTENSA_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  29-Mar-22   tiberius    created
 */
