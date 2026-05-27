/******************************************************************************
 * LTKArchArmCortexM_TrustZone.c                       LTK Microkernel Security
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/LT.h>

// TODO: Flow kernel arch down?
#define LT_LTKERNEL_ARCHITECTURE  LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V8M_MAIN
#define LTK_ARCH_ARM_CORTEX_M_ENABLE_PRIVATE_DEFS
#include "LTKernel.h"

#include "LTKArchArmCortexM_TrustZone.h"

enum {
    /* Sealing stacks with this value is recommended by ARM. This seal is also
     *  used to detect stack smashing in exception handler. */
    kLTKStackSeal = 0xfef5eda5
};

/************************
 * Contexts and Stacks */
static LTSecurityBSP_Context * s_pContexts;

static u8  * s_pStacks;
static u32   s_nStackSizeInBytes;
static u8    s_nNumContexts;

/************************
 * Hooks and Callbacks */
static void (*s_pFuncConsoleStomp)(const char *, ...);
static void (*s_pFuncCoreFaultHandler)(const char * excpStrID, bool bFromInterrupt, u32 * pRegContext, LT_SIZE contextSize) = NULL;

/***************
 * NSC Region */
static u32 * volatile s_pNSC;

/*******************************
 * Stack Pointer Manipulation */

LT_INLINE u32 * S_GetPSP(void) {
    u32 * psp;
    asm volatile ( "mrs %0, psp" : "=r" (psp) : : );
    return psp;
}

LT_INLINE void S_SetPSP(u32 * pStackPointer) {
    asm volatile ( "msr psp, %0" : : "r" (pStackPointer) : );
}

LT_INLINE void S_SetPSPLIM(u32 * pStackPointerLimit) {
    asm volatile ( "msr psplim, %0" : : "r" (pStackPointerLimit) : );
}

LT_INLINE void S_SetControl(u32 nControl) {
    asm volatile (
        "msr control, %0      \n\
         isb"
           : : "r" (nControl) : "memory"
    );
}

/******************
 * Fault Handler */
static void LT_USED
LTKTrustZoneFaultHandler(u32 * msp, u32 * psp, u32 psr, u32 exc_rtn) {
    char * FaultType[] = {
        "Hard", "Mem", "Bus", "Usage", "Security", "Imprecise Bus"
    };

    u32 cfsr = LTK_REG(CFSR);
    u32 fault_no = (psr & 0xf) - 3;
    if (fault_no == 2 && (cfsr & 0x400)) fault_no = 5;
    bool in_isr = ((exc_rtn & 0x8) == 0x0);

    (*s_pFuncConsoleStomp)("\n*** %s Fault %s***\n", FaultType[fault_no], in_isr ? "IN ISR " : "");
    if ((exc_rtn & 0x10) == 0x0) (*s_pFuncConsoleStomp)("*** Lazy Floating Point Enabled ***\n");

    (*s_pFuncConsoleStomp)("  HFSR: %08X CFSR: %08X AFSR: %08X EXCR: %08X\n",
                LTK_REG(HFSR), cfsr, LTK_REG(AFSR), exc_rtn);
    (*s_pFuncConsoleStomp)(" MMFAR: %08X BFAR: %08X SFSR: %08X SFAR: %08X\n\n",
                LTK_REG(MMFAR), LTK_REG(BFAR), LTK_REG(SFSR), LTK_REG(SFAR));

    // Read non-secure stack pointers from MSP
    u32 * msp_ns = (u32 *)msp[0];
    u32 * psp_ns = (u32 *)msp[1];
    msp += 2;

    if (fault_no != 4) {
        // IS NOT a security fault (originated from S side) ...
        (*s_pFuncConsoleStomp)("   MSP: %08X  PSP: %08X\n", (u32)msp, (u32)psp);
        if ((cfsr & 0x100000) == 0x0) {
            // If this is a secure fault (NB: different from Security Fault)
            //   and not a stack overflow (STK_OVF) then print PC and LR of fault.
            u32 * sp = in_isr ? msp : psp;
            (*s_pFuncConsoleStomp)("    LR: %08X   PC: %08X\n", sp[5], sp[6]);
        } else {
            (*s_pFuncConsoleStomp)("!!! Secure Stack overflow (bottom) !!!\n");
        }
        for (u32 context = 0; context < s_nNumContexts; context++) {
            if (*s_pContexts[context].pStackLimit != kLTKStackSeal) {
                (*s_pFuncConsoleStomp)("!!! Secure Stack %u corruption (bottom) !!!\n", context);
            }
            u32 * top = (u32 *)((u8 *)s_pContexts[context].pStackLimit + s_nStackSizeInBytes - 8);
            if (*top != kLTKStackSeal) {
                (*s_pFuncConsoleStomp)("!!! Secure Stack %u corruption (top) !!!\n", context);
            }
        }
    } else {
        // IS a security fault (originated from NS side) ... dump stacks after validating pointers
        if (in_isr || psp_ns == NULL) {
            (*s_pFuncConsoleStomp)("NS Main Stack @%08X:\n", (u32)msp_ns);
            if (S_IsNonSecureAddressRange(msp_ns, 64)) {
                (*s_pFuncConsoleStomp)(" %08X %08X %08X %08X  (R0 R1 R2 R3)\n",  msp_ns[0], msp_ns[1], msp_ns[2], msp_ns[3]);
                (*s_pFuncConsoleStomp)(" %08X %08X %08X %08X (R12 LR PC PSR)\n", msp_ns[4], msp_ns[5], msp_ns[6], msp_ns[7]);
                msp_ns += 8;
                for (s32 ix = 0; ix < 8; ix++) {
                    (*s_pFuncConsoleStomp)(" %08X", msp_ns[ix]);
                    if ((ix & 0x3) == 0x3) (*s_pFuncConsoleStomp)("\n");
                }
                (*s_pFuncConsoleStomp)("\n");
            }
        }
        (*s_pFuncConsoleStomp)("NS Thread Stack @%08X:\n", (u32)psp_ns);
        if (S_IsNonSecureAddressRange(psp_ns, 64)) {
            (*s_pFuncConsoleStomp)(" %08X %08X %08X %08X  (R0 R1 R2 R3)\n",  psp_ns[0], psp_ns[1], psp_ns[2], psp_ns[3]);
            (*s_pFuncConsoleStomp)(" %08X %08X %08X %08X (R12 LR PC PSR)\n", psp_ns[4], psp_ns[5], psp_ns[6], psp_ns[7]);
            psp_ns += 8;
            for (s32 ix = 0; ix < 8; ix++) {
                (*s_pFuncConsoleStomp)(" %08X", psp_ns[ix]);
                if ((ix & 0x3) == 0x3) (*s_pFuncConsoleStomp)("\n");
            }
            (*s_pFuncConsoleStomp)("\n R4: %08X R5: %08X  R6: %08X  R7: %08X\n", msp[0], msp[1], msp[2], msp[3]);
            (*s_pFuncConsoleStomp)(" R8: %08X R9: %08X R10: %08X R11: %08X\n",   msp[4], msp[5], msp[6], msp[7]);
            if (s_pFuncCoreFaultHandler) {
                u32 * pFullContext = psp_ns - 9;
                pFullContext[0] = msp[0];
                pFullContext[1] = msp[1];
                pFullContext[2] = msp[2];
                pFullContext[3] = msp[3];
                pFullContext[4] = msp[4];
                pFullContext[5] = msp[5];
                pFullContext[6] = msp[6];
                pFullContext[7] = msp[7];
                pFullContext[8] = psp_ns[5];
                (*s_pFuncCoreFaultHandler)(FaultType[fault_no], in_isr, pFullContext, 0);
            }
            if (LTK_REG(DHCSR) & LTK_ARCH_ARM_CORTEX_M_VAL_DHCSR_DEBUG_ENABLED) {
                // Set INTSPIDEN bit, allowing secure debug
                LTK_REG(DAUTHCTRL) |= LTK_ARCH_ARM_CORTEX_M_VAL_DAUTHCTRL_INTSPIDEN;
                // Restore context at fault, all but PC, PC must be set in GDB manually:
                // (gdb) set $pc=*(u32*)($sp-8)
                asm volatile (
                    "msr msp, %0          \n\
                     mov r0, %1           \n\
                     ldmia r0, {r4-r11}   \n\
                     pop {r0-r3, r12, lr} \n\
                     add sp, #8           \n\
                     bkpt 1"
                    : : "r" (psp_ns-8), "r"(msp)
                );
                return;
            }
        }
    }
    (*s_pFuncConsoleStomp)("\n");
    while (1)
       ;
}

// TODO: ROM .a file has symbol name collision
static void LT_VERBATIM LTKSecureFaultHandler(void) {
    asm volatile (
        "mrs r0, msp_ns                 \n\
         mrs r1, psp_ns                 \n\
         push {r0-r1,r4-r11}            \n\
         mrs r0, msp                    \n\
         mrs r1, psp                    \n\
         mrs r2, psr                    \n\
         mov r3, lr                     \n\
         b LTKTrustZoneFaultHandler"
            : : : "r0", "r1", "r2", "r3"
    );
}

/************************************
 * Secure Context Switch Functions */

void LT_NONSECURE_ENTRY NSC_InitContexts(void) {
    /* NB: Must be invoked from handler mode from the non-secure side */
    S_SetControl(0x2);
}

void LT_NONSECURE_ENTRY NSC_ResetSecureContext(s32 nContext) {
    /* Can be run from any context */
    s_pContexts[nContext].pStack = (u32 *)(s_pStacks + (nContext + 1)*s_nStackSizeInBytes - 8);
}

void LT_NONSECURE_ENTRY NSC_SwitchSecureContext(s32 nSaveContext, s32 nRestoreContext) {
    /* NB: Must be run from handler mode from the non-secure side */
    if (nSaveContext >= 0) s_pContexts[nSaveContext].pStack = S_GetPSP();
    S_SetPSPLIM(s_pContexts[nRestoreContext].pStackLimit);
    S_SetPSP(s_pContexts[nRestoreContext].pStack);
}

/**************************************************
 * Decode destination for ARM branch instruction */
static void * S_DecodeBranchDestination(u32 * pInst) {
    u32 nInst = *pInst;
    // Detect and decode b.w instruction (T4 variant)
    if ((nInst & 0xd000f800) == 0x9000f000) {
        s32 nOffset = ((nInst & 0x07ff0000) >> 15) | ((nInst & 0x000003ff) << 12);
        s32 nJ12    = ((nInst & 0x20000000) >>  6) | ((nInst & 0x08000000) >>  5);
        if (nInst & 0x0400) {
            // Negative, so sign-extend
            nOffset |= (nJ12 | 0xff000000);
        } else {
            // Positive, so invert j1 and j2
            nOffset |= (nJ12 ^ 0x00c00000);  // TODO: TEST
        }
        return (void *)((u8 *)pInst + nOffset + 5);
    }
    return NULL;
}

/**************************************
 * Map Secure call to its NSC Veneer */
void * S_GetNSCVeneerFor(void * pSecureFunc) {
    for (u32 nIx = 0; nIx < 100; nIx++) {
        if (pSecureFunc == S_DecodeBranchDestination(s_pNSC + nIx))
            return (void *)((u8 *)(s_pNSC + nIx) - 3);
    }
    return NULL;
}

/**********************************
 * Configure Contexts and Stacks */
void S_SetSecureContexts(LTSecurityBSP_Context * pContexts, u32 nNumContexts, u8 * pStacks,
                             u32 nStackSizeInBytes) {
    s_pContexts         = pContexts;
    s_nNumContexts      = nNumContexts;
    s_pStacks           = pStacks;
    s_nStackSizeInBytes = nStackSizeInBytes;
}

/**********************************
 * Initialize Security Container */
/* TODO: This should be static and NOT NSC_ */
void LT_NONSECURE_ENTRY NSC_InitializeContainer(void) {
    // Set fault handlers
    volatile u32 * pVectorTable = (u32 *)LTK_REG(VTOR);
    for (u32 ix = 3; ix <= 7; ix++)
        pVectorTable[ix] = (u32)&LTKSecureFaultHandler;
    // Prioritize secure exceptions and enable all secure mode faults
    LTK_REG(AIRCR) = (LTK_REG(AIRCR) & LTK_REG_VAL(AIRCR_SEC_MASK)) | LTK_REG_VAL(AIRCR_SEC);
    // Trap Divide By 0 and disable "Unintentional" Alignment Faults
    LTK_REG(CCR) |=  LTK_REG_VAL(DIV0_TRAP);
    LTK_REG(CCR) &= ~LTK_REG_VAL(UNALIGN_TRAP);
    // Enable Bus, Memory, Usage and Security Faults in general
    LTK_REG(SHCSR) |= LTK_REG_VAL(FAULT_ENABLE);
    // Disable secure state debug monitor
    LTK_REG(DAUTHCTRL) = LTK_REG_VAL(DIS_SEC_DBG);
    // Initialize the BSP (hold pNSC to prevent overwrite when initializing BSS)
    u32 * volatile pSave = s_pNSC;
    S_LTSecurityBSP_Initialize(&s_pFuncConsoleStomp, &s_pFuncCoreFaultHandler);
    s_pNSC = pSave;
    // Security check
    if (S_IsNonSecureAddress(NSC_InitializeContainer)) {
        (*s_pFuncConsoleStomp)("TZ not secure, aborting...\n");
        /* Hang if not secure */
        while (1)
          ;
    }
    // Check contexts
    if (s_nNumContexts == 0 || s_pStacks == NULL) {
        (*s_pFuncConsoleStomp)("TZ contexts not set, aborting...\n");
        while (1)
           ;
    }
    // Initialize stack pointers and seal stacks
    for (u32 nContext = 0; nContext < s_nNumContexts; nContext++) {
        s_pContexts[nContext].pStackLimit      = (u32 *)(s_pStacks + nContext*s_nStackSizeInBytes);
        s_pContexts[nContext].pStack           = (u32 *)(s_pStacks + (nContext + 1)*s_nStackSizeInBytes - 8);
        (s_pContexts[nContext].pStackLimit)[0] = kLTKStackSeal;
        (s_pContexts[nContext].pStack)[0]      = kLTKStackSeal;
        (s_pContexts[nContext].pStack)[1]      = kLTKStackSeal;
    }
    // Set initial stack pointers and set thread mode on secure side
    S_SetPSPLIM(s_pContexts[0].pStackLimit);
    S_SetPSP(s_pContexts[0].pStack);
}

/***********************
 * S_GetTrustZoneInfo */
static LTKTrustZoneInitFunc * LT_USED S_GetTrustZoneInfo(LTKTrustZoneM * pInfo, u32 * pPC) {
    if (pInfo && !S_IsNonSecureAddressRange(pInfo, sizeof(LTKTrustZoneM))) return NULL;
    /* Move to first NSC veneer branch and save pointer */
    while (S_DecodeBranchDestination(pPC) == NULL) pPC++;
    s_pNSC = pPC;
    /* Obtain BSP information (if requested) or pointer to initialization function. */
    if (pInfo) {
        S_LTSecurityBSP_GetInfo(&pInfo->SecurityInfo);
        pInfo->pInitContexts  = S_GetNSCVeneerFor(NSC_InitContexts);
        pInfo->pResetContext  = S_GetNSCVeneerFor(NSC_ResetSecureContext);
        pInfo->pSwitchContext = S_GetNSCVeneerFor(NSC_SwitchSecureContext);
    }
    // TODO: Return pointer to secure initialization function to bootloader
    return S_GetNSCVeneerFor(&NSC_InitializeContainer);
}

/*********************************************************************************
 * NSC_GetSecurityInfo (Non-Secure Callable Stub)
 *    1. This function must be placed at the start of the NSC callable area:
 *        A section name is specified for use in linker scripts.
 *        Only this one function should exist in this section.
 *    2. The BSP configures LTCore with the starting location of the NSC area.
 *    3. LTCore invokes this function to discover the contents of the container. */
LT_SECTION(".roku.sgstubs.text")
LTKTrustZoneInitFunc * LT_VERBATIM NSC_GetSecurityInfo(LTKTrustZoneM * pInfo) {
    LT_USED_ARG(pInfo);
    asm volatile (
       "sg                        \n\
        mov r1, pc                \n\
        push {lr}                 \n\
        bl S_GetTrustZoneInfo     \n\
        pop {lr}                  \n\
        mov r1, lr                \n\
        mov r2, lr                \n\
        mov r3, lr                \n\
        mov r12, lr               \n\
        msr apsr_nzcvq, lr        \n\
        bxns lr"
    );
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  02-Nov-21   tiberius    created
 */
