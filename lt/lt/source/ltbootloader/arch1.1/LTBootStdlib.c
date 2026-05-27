/*******************************************************************************
 * LTBootStdlib.c                                                  LT Bootloader
 *                                                                (Arch Ver 1.1)
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTBoot.h"
#include "LTBootDriver.h"
#include "LTBootPlatform.h"

#define LT_ALIGNMENT(p) ((LT_SIZE)(p) & (sizeof(LT_SIZE) - 1))

/* This structure limits the vsnprintf stack depth */
typedef struct {
    /* Format settings */
    u8   base;
    u8   isUpper;
    u8   isSigned;
    u8   minWidth;
    char padChar;
    /* State variables */
    u8   doNumeric;
    u8   inArg;
} State;

static const char LowerCaseDigits[] = "0123456789abcdef";
static const char UpperCaseDigits[] = "0123456789ABCDEF";

/* Built-in (optional) implementation of memcpy */
void * __wrap_memcpy(void * pDest, const void * pSource, LT_SIZE nCount) {
    // sanity check
    if (!pDest || !pSource) {
        return pDest;
    }
    const u8 * pSourceBytes = pSource;
    u8 * pDestBytes = pDest;
    if ((nCount >= (sizeof(LT_SIZE) * 4)) && (LT_ALIGNMENT(pSource) == LT_ALIGNMENT(pDest))) {
        /* Copy data byte-by-byte until the pointers are word-aligned */
        while (LT_ALIGNMENT(pSourceBytes) != 0) {
            *pDestBytes++ = *pSourceBytes++;
            nCount--;
        }
        LT_SIZE * pDestWords = (LT_SIZE *)pDestBytes;
        LT_SIZE const * pSourceWords = (const LT_SIZE *)pSourceBytes;
        /* Copy chunks of 8 words at a time in an unrolled loop */
        while (nCount >= sizeof(LT_SIZE) * 8) {
            *pDestWords++ = *pSourceWords++;
            *pDestWords++ = *pSourceWords++;
            *pDestWords++ = *pSourceWords++;
            *pDestWords++ = *pSourceWords++;
            *pDestWords++ = *pSourceWords++;
            *pDestWords++ = *pSourceWords++;
            *pDestWords++ = *pSourceWords++;
            *pDestWords++ = *pSourceWords++;
            nCount -= sizeof(LT_SIZE) * 8;
        }
        /* Now copy any leftover words that weren't copied in the unrolled loop */
        while (nCount >= sizeof(LT_SIZE)) {
            *pDestWords++ = *pSourceWords++;
            nCount -= sizeof(LT_SIZE);
        }
        pDestBytes = (u8 *)pDestWords;
        pSourceBytes = (const u8 *)pSourceWords;
    }
    /* Copy unaligned or small data byte-by-byte */
    while (nCount--) *pDestBytes++ = *pSourceBytes++;
    return pDest;
}

/* Built-in (optional) implementation of memset */
void * __wrap_memset(void * pDest, int c, LT_SIZE nCount) {
    c = (unsigned char) c;
    // sanity check
    if (!pDest) {
        return pDest;
    }
    u8 * pDestBytes = pDest;
    if (nCount >= (sizeof(LT_SIZE) * 4)) {
        /* Copy data byte-by-byte until the pointer is word-aligned */
        while (LT_ALIGNMENT(pDestBytes) != 0) {
            *pDestBytes++ = c;
            nCount--;
        }
        LT_SIZE * pDestWords = (LT_SIZE *)pDestBytes;
        LT_SIZE copyWord = c;
        for (u8 i = 0; i < sizeof(LT_SIZE)-1; ++i) copyWord = (copyWord << 8) + c;
        /* Copy chunks of 8 words at a time in an unrolled loop */
        while (nCount >= sizeof(LT_SIZE) * 8) {
            *pDestWords++ = copyWord;
            *pDestWords++ = copyWord;
            *pDestWords++ = copyWord;
            *pDestWords++ = copyWord;
            *pDestWords++ = copyWord;
            *pDestWords++ = copyWord;
            *pDestWords++ = copyWord;
            *pDestWords++ = copyWord;
            nCount -= sizeof(LT_SIZE) * 8;
        }
        /* Now copy any leftover words that weren't copied in the unrolled loop */
        while (nCount >= sizeof(LT_SIZE)) {
            *pDestWords++ = copyWord;
            nCount -= sizeof(LT_SIZE);
        }
        pDestBytes = (u8 *)pDestWords;
    }
    /* Copy unaligned or small data byte-by-byte */
    while (nCount--) *pDestBytes++ = c;
    return pDest;
}

/* Built-in (optional) implementation of strncpyTerm (improved version of strncpy) */
LT_SIZE LTBootStdlib_strncpyTerm(char * pStringDest, const char * pStringSource, LT_SIZE nDestBuffLen) {
    const char * pStringSourceOrig = pStringSource;
    if (pStringDest && nDestBuffLen) {
        if (pStringSource) while (--nDestBuffLen && *pStringSource) *pStringDest++ = *pStringSource++;
        *pStringDest = 0;
    }
    return pStringSource - pStringSourceOrig;
}

/* Built-in (optional) implementation of strncmp */
int LTBootStdlib_strncmp(const char * pString1, const char * pString2, LT_SIZE nCount) {
    if (nCount) {
        if (!pString1) return (pString2 && *pString2) ? -1 : 0;
        if (!pString2) return *pString1 ? 1 : 0;
        while (nCount--) {
            if (0 == *pString1)            return (0 == *pString2) ? 0 : -1;
            if (*pString1 > *pString2)     return 1;
            if (*pString1++ < *pString2++) return -1;
        }
    }
    return 0;
}

static u32 Itoa(char * restrict pOut, State * pState, s32 in) {
    u32 adj = (u32)in;
    s32 cnt = 0;
    if (pState->base == 16) {
        const char * restrict pDigits = LowerCaseDigits;
        if (pState->isUpper) pDigits = UpperCaseDigits;
        const u32 mask = (1 << 4) - 1;
        do {
            *pOut++ = pDigits[adj & mask];
            adj >>= 4;
            cnt++;
        } while (adj != 0);
    } else {
        if (pState->isSigned && in < 0) adj = (u32)-in;
        // Determine digits (in reverse order)
        do {
            *pOut++ = LowerCaseDigits[adj % pState->base];
            adj = adj / pState->base;
            cnt++;
        } while (adj != 0);
        // Write sign
        if (pState->isSigned && in < 0) {
            *pOut++ = '-';
            cnt++;
        }
    }
    // Pad to minimum number of digits
    for (; cnt < pState->minWidth; cnt++) *pOut++ = pState->padChar;
    // Reverse digit order in place
    for (s32 idx = 0; idx < ((cnt + 1) >> 1); idx++) {
        char tmp = pOut[idx - cnt];
        pOut[idx - cnt] = pOut[-idx - 1];
        pOut[-idx - 1] = tmp;
    }
    return cnt;
}

static void WriteBuf(char * restrict * pOut, const char * restrict pIn, s32 nLen, s32 * pRem) {
    u32 nCnt = nLen;
    if (nLen > *pRem) {
        if (*pRem < 0) nCnt = 0;
        else nCnt = *pRem;
    }
    *pRem -= nLen;
    for (; nCnt > 0; nCnt--) {
        **pOut = *pIn++;
        (*pOut)++;
    }
}

/* Built-in (optional) implementation of vsnprintf */
int LTBootStdlib_vsnprintf(char * restrict pBuffer, LT_SIZE sz, const char * restrict pFmt, lt_va_list args) {
    const char * restrict pCh = pFmt;
    char * restrict pOut = pBuffer;
    s32 rem = (s32)sz;
    State state = { .inArg = false };
    for (; *pCh != '\0'; pCh++) {
        if (!state.inArg) {
            if (*pCh == '%') {
                // Found argument, set default state
                state.minWidth = 0;
                state.padChar  = ' ';
                state.inArg    = true;
            } else WriteBuf(&pOut, pCh, 1, &rem);
        } else if (*pCh >= '0' && *pCh <= '9') {
            if (state.minWidth == 0 && *pCh == '0') state.padChar = '0';
            else state.minWidth = (10 * state.minWidth) + (*pCh - '0');
        } else {
            // Argument will be consumed (unless modifier found)
            state.inArg     = false;
            state.doNumeric = false;
            switch (*pCh) {
            case '%': {
                char c = '%';
                WriteBuf(&pOut, &c, 1, &rem);
                break;
            }
            case 'c': {
                char c = (char)lt_va_arg(args, int);
                WriteBuf(&pOut, &c, 1, &rem);
                break;
            }
            case 's': {
                char * arg = lt_va_arg(args, char *);
                for (; *arg != '\0'; arg++)
                    WriteBuf(&pOut, arg, 1, &rem);
                break;
            }
            case 'l':
                state.inArg     = true;
                break;
            case 'd':
                state.base      = 10;
                state.isSigned  = true;
                state.doNumeric = true;
                break;
            case 'u':
                state.base      = 10;
                state.isSigned  = false;
                state.doNumeric = true;
                break;
            case 'x':
                state.base      = 16;
                state.isUpper   = false;
                state.isSigned  = false;
                state.doNumeric = true;
                break;
            case 'X':
                state.base      = 16;
                state.isUpper   = true;
                state.isSigned  = false;
                state.doNumeric = true;
                break;
            default:
                break;
            }
            // Convert numeric types to text
            if (state.doNumeric) {
                s32 arg32 = lt_va_arg(args, s32);
                char tmp32[11];
                u32 cnt = Itoa(tmp32, &state, arg32);
                WriteBuf(&pOut, tmp32, cnt, &rem);
            }
        }
    }
    *pOut = '\0';
    return (int)sz - rem;
}

/* Built-in (optional) implementation of snprintf */
int LTBootStdlib_snprintf(char * pStringDest, LT_SIZE size, const char * pFmt, ...) {
    lt_va_list args;
    lt_va_start(args, pFmt);
    int cnt = LTBootStdlib_vsnprintf(pStringDest, size, pFmt, args);
    lt_va_end(args);
    return cnt;
}

/* Built-in (optional) implementation of printf */
int LTBootStdlib_printf(const char * pFmt, ...) {
    enum { nBufferSize = LTBootPlatform_PrintBufferSize };
    static char buffer[nBufferSize];
    lt_va_list args;
    lt_va_start(args, pFmt);
    int cnt = LTBootStdlib_vsnprintf(buffer, nBufferSize, pFmt, args);
    lt_va_end(args);
    if (cnt > nBufferSize) cnt = nBufferSize;
    LTBootDriverSerial_PutCharsToConsole(buffer, cnt);
    return cnt;
}

