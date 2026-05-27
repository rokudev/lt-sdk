/*******************************************************************************
 *
 * LTUtilityTokenizer.c - Implementation of text tokenizer interface
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/utility/tokenizer/LTUtilityTokenizer.h>

enum {
    kSentinelLookupTableSize = 256 / 8
};

#define SET_BIT(p, bit)    ((u8*)(p))[(bit) / 8] |= (1 << ((bit) & 7))
#define TEST_BIT(p, bit)   (((u8*)(p))[(bit) / 8] & (1 << ((bit) & 7)))

struct sLTTokenizer {
    LTString                       pToken;
    u8                             sentinelLookupTable[kSentinelLookupTableSize];
    LTTokenizer_ProcessTokenProc * pProcessToken;
    void                         * pClientData;
};

static void LTUtilityTokenizer_SetSentinels(LTTokenizer * pTokenizer, const char * pSentinelStr);

static LTTokenizer * LTUtilityTokenizer_Create(const char * pSentinelStr, LTTokenizer_ProcessTokenProc * pProcessToken, void * pClientData) {
    LTTokenizer * pTokenizer = lt_malloc(sizeof(LTTokenizer));
    pTokenizer->pToken = ltstring_create("");
    pTokenizer->pProcessToken = pProcessToken;
    pTokenizer->pClientData = pClientData;
    LTUtilityTokenizer_SetSentinels(pTokenizer, pSentinelStr);
    return pTokenizer;
}

static void LTUtilityTokenizer_Destroy(LTTokenizer * pTokenizer) {
    if (!pTokenizer) return;
    if (pTokenizer->pToken) ltstring_destroy(pTokenizer->pToken);
    lt_free(pTokenizer);
}

static void LTUtilityTokenizer_SetSentinels(LTTokenizer * pTokenizer, const char * pSentinelStr) {
    lt_memset(pTokenizer->sentinelLookupTable, 0, sizeof(pTokenizer->sentinelLookupTable));
    while (*pSentinelStr != '\0') {
        SET_BIT(&pTokenizer->sentinelLookupTable, *pSentinelStr);
        pSentinelStr++;
    }
}

static u32 LTUtilityTokenizer_ProcessData(LTTokenizer * pTokenizer, const void * pData, u32 nDataLen, bool bMoreData) {
    for (u32 i = 0; i < nDataLen; ++i) {
        char c = ((char *)pData)[i];
        if (TEST_BIT(&pTokenizer->sentinelLookupTable, c)) {
            ltstring_stripwhitespace(&pTokenizer->pToken, true, true);
            bool bContinue = pTokenizer->pProcessToken(pTokenizer, pTokenizer->pToken, c, pTokenizer->pClientData);
            ltstring_empty(pTokenizer->pToken);
            if (!bContinue) return i + 1;
        } else {
            ltstring_appendchar(&pTokenizer->pToken, c);
        }
    }
    // add the last token if there's no more data
    if (!bMoreData) {
        if (!ltstring_isempty(pTokenizer->pToken)) {
            pTokenizer->pProcessToken(pTokenizer, pTokenizer->pToken, '\0', pTokenizer->pClientData);
            ltstring_empty(pTokenizer->pToken);
        }
    }

    return nDataLen;
}

/*___________________________________________
 / LTTokenizer library initialization */
static bool LTUtilityTokenizerImpl_LibInit(void) {
    return true;
}

static void LTUtilityTokenizerImpl_LibFini(void) {
}

/*___________________________________________________
 / LTTokenizer library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTUtilityTokenizer,)

    .CreateTokenizer   = &LTUtilityTokenizer_Create,
    .DestroyTokenizer  = &LTUtilityTokenizer_Destroy,
    .SetSentinels      = &LTUtilityTokenizer_SetSentinels,
    .ProcessData       = &LTUtilityTokenizer_ProcessData,

LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  25-Feb-22   trajan      created
 */
