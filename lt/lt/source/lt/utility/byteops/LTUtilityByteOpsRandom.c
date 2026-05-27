/*******************************************************************************
 * source/lt/utility/LTUtilityByteOpsRandom.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include "LTUtilityByteOpsRandom.h"

/*___________________________________
 / LTUtilityByteOpsRandom #defines */
#define PERMUTED_CONGRUENTIAL_GENERATOR_MULTIPLIER      LT_CONSTU64(6364136223846793005)

#define INFOPRINT_ENTROPY                               0
#if     INFOPRINT_ENTROPY
DEFINE_LTLOG_SECTION("byteops.random");
#endif

/*___________________________________________
 / LTUtilityByteOpsRandom static variables */
static LTCore    * s_pCore;
static LTMutex   * s_mutex;
static u64         s_nState;         /* this is the seed and state for the Permuted Congruential Generator */
static u64         s_nIncrement;     /* this is the stream for the Permuted Congruential Generator */
static u64         s_nStateOld;
static u32         s_nXORShifted;
static u32         s_nRotated;
static u32         s_nRandomResult;
static u8 *        s_pRandomResult = (u8 *)&s_nRandomResult;
                /* Note: The variables after s_nIncrement are working variables.  They are static in order to save stack space. */

/*__________________________________________________________
 / LTUtilityByteOpsRandom private static helper functions */
static bool GetHardwareEntropy(u64 *pEntropy) {
    bool bGotHardwareEntropy = false;
    LTDriverCryptoEntropy *hwEntropy = lt_createobject(LTDriverCryptoEntropy);
    if (hwEntropy) {
        bGotHardwareEntropy = hwEntropy->API->GetEntropy((u8 *)pEntropy, SHA256_HASH_LENGTH);
        lt_destroyobject(hwEntropy);
    }
    return bGotHardwareEntropy;
}

static void GetSoftwareEntropy(u64 *pEntropy) {
    /* use variations of memory access times and yield times to generate entropy */
    LTAtomic stackAtomic = { 1 };
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, s_pCore);
    for (u32 i = 0; i < 4; i++) {
        s_nIncrement = 0; /* borrow s_nIncrement as a loop counter */
        LTTime start = s_pCore->GetKernelTime();
        LTTime stop = LTTime_Add(start, LTTime_Milliseconds(5));
        while (LTTime_IsLessThan(s_pCore->GetKernelTime(), stop)) {
            s_nIncrement++;
            s_nXORShifted = LTAtomic_Load(&stackAtomic); /* borrow s_nXORShifted and s_nRotated for temp vars */
            s_nRotated = (s_nXORShifted + 1) * (u32)s_nIncrement;
            LTAtomic_Store(&stackAtomic, s_nRotated);
        }
        iThread->Sleep(LTTime_Zero()); /* yield */
        stop = s_pCore->GetKernelTime();
        pEntropy[i] = stop.nNanoseconds * s_nIncrement;
#if INFOPRINT_ENTROPY
    LTLOG("entropy", "Loop Counter %ld: %lld", LT_Pu32(i), LT_Pu64(s_nIncrement));
#endif
    }
    /* reset borrowed variables */
    s_nIncrement = 0;
    s_nXORShifted = 0;
    s_nRotated = 0;
}

static void InitializePermutedCongruentialGenerator(void) {
    /* try to get hardware entropy from LTDriverCryptoEntropy; otherwise fall back to software entropy */
    u64 * pEntropy = lt_malloc(SHA256_HASH_LENGTH);
    LT_ASSERT(pEntropy != NULL);

#if INFOPRINT_ENTROPY
    bool bSoftware = false;
    LTTime start = s_pCore->GetKernelTime();

    if (! GetHardwareEntropy(pEntropy)) {
        GetSoftwareEntropy(pEntropy);
        bSoftware = true;
    }

    LTTime duration = LTTime_Subtract(s_pCore->GetKernelTime(), start);
    LTLOG("entropy", "Entropy: %llx-%llx-%llx-%llx", LT_Pu64(pEntropy[0]), LT_Pu64(pEntropy[1]), LT_Pu64(pEntropy[2]), LT_Pu64(pEntropy[3]));
    LTLOG("entropy", "Generated %s entropy in %lu.%06lu seconds",
        bSoftware ? "software" : "hardware",
        LT_Pu32(LTTime_GetSeconds(duration)),
        LT_Pu32(LTTime_GetMicroseconds(LTTime_FractionalSeconds(duration))));
#else
    if (! GetHardwareEntropy(pEntropy)) GetSoftwareEntropy(pEntropy);
#endif

    /* seed with entropy */
    s_nState  =     (((pEntropy[0] << 1u) | 1u) << 32u) | ((pEntropy[1] << 1u) | 1u);
    s_nIncrement  = (((pEntropy[2] << 1u) | 1u) << 32u) | ((pEntropy[3] << 1u) | 1u);

    lt_memset(pEntropy, 0, SHA256_HASH_LENGTH);
    lt_free(pEntropy);

    /* ready the permuted congruential generator state */
    s_nState += s_nIncrement;
    s_nState *= PERMUTED_CONGRUENTIAL_GENERATOR_MULTIPLIER;
    s_nState += s_nIncrement;
}

/*___________________________________________________________
 / LTUtilityByteOpsRandom library private helper functions */
void LTUtilityByteOpsRandom_Init(void) {
    /* just init s_mutex here and lazy init the rest in GenRandomBytes */
    s_mutex = lt_createobject(LTMutex);
}

void LTUtilityByteOpsRandom_Fini(void) {
    s_nState = s_nIncrement = s_nStateOld = 0;
    s_nXORShifted = s_nRotated = s_nRandomResult = 0;
    lt_destroyobject(s_mutex);
    s_mutex = NULL;
    s_pCore = NULL;
}

/*______________________________________________________________________
 / LTUtilityByteOpsRandom LTUtilityByteOps public interface functions */
void LTUtilityByteOpsRandom_GenRandomBytes(u8 * pBuffToFill, u32 nBytes) {
    if (pBuffToFill && nBytes) {
        s_mutex->API->Lock(s_mutex);
        if (NULL == s_pCore) {
            /* lazy init */
            s_pCore = LT_GetCore();
            InitializePermutedCongruentialGenerator();
        }
        while (nBytes > 3) {
            s_nStateOld = s_nState;
            s_nState = s_nStateOld * PERMUTED_CONGRUENTIAL_GENERATOR_MULTIPLIER + s_nIncrement;
            s_nXORShifted = (u32)(((s_nStateOld >> 18u) ^ s_nStateOld) >> 27u);
            s_nRotated = (u32)(s_nStateOld >> 59u);
            s_nRandomResult = (s_nXORShifted >> s_nRotated) | (s_nXORShifted << ((u32)(-((s32)s_nRotated)) & 31));
            *pBuffToFill++ = s_pRandomResult[0]; *pBuffToFill++ = s_pRandomResult[1];
            *pBuffToFill++ = s_pRandomResult[2]; *pBuffToFill++ = s_pRandomResult[3];
            nBytes -= 4;
        }
        if (nBytes) {
            s_nStateOld = s_nState;
            s_nState = s_nStateOld * PERMUTED_CONGRUENTIAL_GENERATOR_MULTIPLIER + s_nIncrement;
            s_nXORShifted = (u32)(((s_nStateOld >> 18u) ^ s_nStateOld) >> 27u);
            s_nRotated = (u32)(s_nStateOld >> 59u);
            s_nRandomResult = (s_nXORShifted >> s_nRotated) | (s_nXORShifted << ((u32)(-((s32)s_nRotated)) & 31));
            while (nBytes--) *pBuffToFill++ = *s_pRandomResult++;
            s_pRandomResult = (u8 *)&s_nRandomResult;
        }
        s_mutex->API->Unlock(s_mutex);
    }
}

void LTUtilityByteOpsRandom_GenRandomBytesAsHexString(u32 nNumHexBytes, char * pStringBuff, u32 nBuffSize) {
   if (0 == nNumHexBytes || NULL == pStringBuff || nBuffSize <= nNumHexBytes * 2) return;
   u8 * pRandom = lt_malloc(nNumHexBytes);
   if (NULL == pRandom) return;
   LTUtilityByteOpsRandom_GenRandomBytes(pRandom, nNumHexBytes);
   const char * hex = "0123456789abcdef";
   u8 * pRandomOrig = pRandom;
   while (nNumHexBytes--) {
       *pStringBuff++ = hex[(*pRandom >> 4) & 0xF];
       *pStringBuff++ = hex[*pRandom & 0xF];
       pRandom++;
    }
    *pStringBuff = 0;
    lt_free(pRandomOrig);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Nov-21   augustus    created
 */
