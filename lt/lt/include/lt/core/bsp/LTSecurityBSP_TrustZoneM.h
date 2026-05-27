/******************************************************************************
 * LTSecurityBSP_TrustZoneM.h            Security BSP Interface for TrustZone M
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_BSP_LTSECURITYBSP_TRUSTZONEM_H
#define ROKU_LT_INCLUDE_LT_CORE_BSP_LTSECURITYBSP_TRUSTZONEM_H

#include <lt/LT.h>
#include <lt/core/LTSecurity.h>

/* Attribute for entrypoint calls to secure word from non-secure world */
#define LT_NONSECURE_ENTRY        __attribute__((cmse_nonsecure_entry))

/* Attribute for calls from secure to non-secure world */
#define LT_NONSECURE_CALL         __attribute__((cmse_nonsecure_call))

/*******************
 * Secure Context */
typedef struct LTSecurityBSP_Context {
    u32 * pStack;
    u32 * pStackLimit;

} LTSecurityBSP_Context;

/*
 *  Functions that must be implemented in BSP
 */

/*******************************************************
 * Initialize BSP (called from the secure bootloader) */
extern void S_LTSecurityBSP_Initialize(void (**ppFuncConsoleStomp)(const char *, ...), void (**ppFuncCoreFaultHandler)(const char * ExcpStrID, bool bFromInterrupt, u32 * pRegContext, LT_SIZE contextSize));

/****************************************
 * Obtain Security Descriptor from BSP */
extern void S_LTSecurityBSP_GetInfo(LTSecurity_Info * pInfo);

/*
 *  API Functions
 */

/* Returns address of Non-Secure Callable veneer for given function */
void * S_GetNSCVeneerFor(void * pSecureFunc);

/* Configure secure contexts and stacks */
void S_SetSecureContexts(LTSecurityBSP_Context * pContexts, u32 nNumContexts, u8 * pStacks, u32 nStackSizeInBytes);

/*
 * API Pointer Validation
 *   API Pointers *MUST* be validated to prevent modification of secure data or code
 */

/* Obtain TT Response */
LT_ISR_SAFE LT_INLINE u32 S_GetTT(const void * pAddress) {
    u32 nTT;
    asm volatile (
       "tt %0, %1"
         : "=r" (nTT) : "r" (pAddress) :
    );
    return nTT;
}

/********************************
 * Check Address Is Non-Secure */
LT_ISR_SAFE LT_INLINE bool S_IsNonSecureAddress(const void * pAddress) {
    u32 nTT = S_GetTT(pAddress);
    return (nTT & 0x400000) == 0x0;
}

/**************************************
 * Check Address Range Is Non-Secure */
LT_ISR_SAFE LT_INLINE bool
S_IsNonSecureAddressRange(const void * pAddress, LT_SIZE nSize) {
    u32 nTT_Start = S_GetTT(pAddress);
    u32 nTT_End   = S_GetTT((u8 *)pAddress + nSize - 1);
    /* Endpoints in same region AND security attribute is non-secure */
    return (nTT_Start == nTT_End) && ((nTT_Start & 0x400000) == 0x0);
}

#endif // #ifndef ROKU_LT_INCLUDE_LT_CORE_BSP_LTSECURITYBSP_TRUSTZONEM_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  02-Nov-21   tiberius    created
 */
