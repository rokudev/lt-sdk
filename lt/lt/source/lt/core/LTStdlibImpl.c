/******************************************************************************
 * lt/core/LTStdlibImpl.c   LTStdlib - LT curated set of stdlib style functions
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTStdlibImpl.h"
#include "LTCoreImpl.h"

#define STRING_RAM_CRITICAL_EXPERIMENTAL        0  /* puts string functions in ram section */
#define STRING_BUILTIN_EXPERIMENTAL             0  /* uses gcc builtins for strlen, strcpy and strcmp */
#define STRING_STDLIB_EXPERIMENTAL              0  /* uses c stdlib for strlen, strcpy and strcmp, only works if !STRING_BUILTIN_EXPERIMENTAL */
#define STRING_LT_IMPLEMENTATION                (!STRING_BUILTIN_EXPERIMENTAL && !STRING_STDLIB_EXPERIMENTAL)
#define STRING_FASTER_LT_STRCMP_EXPERIMENTAL    1  /* only works if STRING_LT_IMPLEMENTATION */

#if STRING_RAM_CRITICAL_EXPERIMENTAL
    #define STRING_TEXT_RAM_CRITICAL LT_TEXT_RAM_CRITICAL(1)
#else
    #define STRING_TEXT_RAM_CRITICAL
#endif

#if STRING_BUILTIN_EXPERIMENTAL
    extern LT_SIZE __builtin_strlen(const char * pString);
    extern int     __builtin_strcmp(const char * pString1, const char * pString2);
    extern char *  __builtin_strcpy(char * pString1, const char * pString2);

    STRING_TEXT_RAM_CRITICAL
    LT_SIZE LTStdlibImpl_strlen(const char * pString) {
        return pString ? __builtin_strlen(pString) : 0;
    }
    STRING_TEXT_RAM_CRITICAL
    int LTStdlibImpl_strcmp(const char * pString1, const char * pString2) {
        if (pString1 && pString2) return __builtin_strcmp(pString1, pString2);
        return pString1 ? (*pString1 ? 1 : 0) : (*pString2 ? -1 : 0);
    }
    STRING_TEXT_RAM_CRITICAL
    char * LTStdlibImpl_strcpy(char * pStringDest, const char * pStringSource) {
        if (pStringDest && pStringSource) return __builtin_strcpy(pStringDest, pStringSource);
        return pStringDest;
    }
#else
    #if STRING_STDLIB_EXPERIMENTAL
        #include <string.h>
        STRING_TEXT_RAM_CRITICAL
        LT_SIZE LTStdlibImpl_strlen(const char * pString) {
            return pString ? strlen(pString) : 0;
        }
        STRING_TEXT_RAM_CRITICAL
        int LTStdlibImpl_strcmp(const char * pString1, const char * pString2) {
            if (pString1 && pString2) return strcmp(pString1, pString2);
            return pString1 ? (*pString1 ? 1 : 0) : (*pString2 ? -1 : 0);
        }
        STRING_TEXT_RAM_CRITICAL
        char * LTStdlibImpl_strcpy(char * pStringDest, const char * pStringSource) {
            if (pStringDest && pStringSource) return strcpy(pStringDest, pStringSource);
            return pStringDest;
        }
    #else
        STRING_TEXT_RAM_CRITICAL
        char * LTStdlibImpl_strcpy(char * pStringDest, const char * pStringSource) {
            char * pRetVal = pStringDest;
            while ((*pStringDest++ = *pStringSource++));
            return pRetVal;
        }
    #endif
#endif

/*_____________________________________________
 / LTStdlibImpl public api functions */

#define LT_ALIGNMENT(p) ((LT_SIZE)(p) & (sizeof(LT_SIZE) - 1))

int
LTStdlibImpl_memcmp(const void * pBuff1, const void * pBuff2, LT_SIZE nCount) {
    // comparison with sanity check
    if (!pBuff1 || !pBuff2) {
        if (pBuff1) {
            return 1;
        }
        if (pBuff2) {
            return -1;
        }
        return 0;
    }
    u8 b1, b2;
    const u8 * pBuff1Bytes = pBuff1;
    const u8 * pBuff2Bytes = pBuff2;
    if ((nCount >= (sizeof(LT_SIZE) * 4)) && (LT_ALIGNMENT(pBuff1) == LT_ALIGNMENT(pBuff2))) {
        /* Compare data byte-by-byte until the pointers are word-aligned */
        while (LT_ALIGNMENT(pBuff1Bytes) != 0) {
            if ((b1 = *pBuff1Bytes++) != (b2 = *pBuff2Bytes++)) return (int)b1 - (int)b2;
            nCount--;
        }
        const LT_SIZE * pBuff1Words = (const LT_SIZE *)pBuff1Bytes;
        const LT_SIZE * pBuff2Words = (const LT_SIZE *)pBuff2Bytes;
        LT_SIZE w1, w2;
        /* Compare chunks of 8 words at a time in an unrolled loop */
        while (nCount >= sizeof(LT_SIZE) * 8) {
            if ((w1 = *pBuff1Words++) != (w2 = *pBuff2Words++) ||
                (w1 = *pBuff1Words++) != (w2 = *pBuff2Words++) ||
                (w1 = *pBuff1Words++) != (w2 = *pBuff2Words++) ||
                (w1 = *pBuff1Words++) != (w2 = *pBuff2Words++) ||
                (w1 = *pBuff1Words++) != (w2 = *pBuff2Words++) ||
                (w1 = *pBuff1Words++) != (w2 = *pBuff2Words++) ||
                (w1 = *pBuff1Words++) != (w2 = *pBuff2Words++) ||
                (w1 = *pBuff1Words++) != (w2 = *pBuff2Words++)) {
                w1 = LT_LE_LT_SIZE(w1);
                w2 = LT_LE_LT_SIZE(w2);
                for (u8 i = 0; i < sizeof(LT_SIZE); ++i) {
                    if ((b1 = (w1 >> (i * 8))) != (b2 = (w2 >> (i * 8)))) return (int)b1 - (int)b2;
                }
            }
            nCount -= sizeof(LT_SIZE) * 8;
        }
        /* Now compare any leftover words that weren't compared in the unrolled loop */
        while (nCount >= sizeof(LT_SIZE)) {
            if ((w1 = *pBuff1Words++) != (w2 = *pBuff2Words++)) {
                w1 = LT_LE_LT_SIZE(w1);
                w2 = LT_LE_LT_SIZE(w2);
                for (u8 i = 0; i < sizeof(LT_SIZE); ++i) {
                    if ((b1 = (w1 >> (i * 8))) != (b2 = (w2 >> (i * 8)))) return (int)b1 - (int)b2;
                }
            }
            nCount -= sizeof(LT_SIZE);
        }
        pBuff1Bytes = (const u8 *)pBuff1Words;
        pBuff2Bytes = (const u8 *)pBuff2Words;
    }
    /* Compare unaligned or small data byte-by-byte */
    while (nCount--) {
        if ((b1 = *pBuff1Bytes++) != (b2 = *pBuff2Bytes++)) return (int)b1 - (int)b2;
    }
    return 0;
}

void *
LTStdlibImpl_memcpy(void * pDest, const void * pSource, LT_SIZE nCount) {
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

static void
LTStdlibImpl_memcpy_reversed(void * pDest, const void * pSource, LT_SIZE nCount) {
    // sanity check
    if (!pDest || !pSource) {
        return;
    }
    const u8 * pSourceBytes = pSource;
    u8 * pDestBytes = pDest;
    /* Move pointers to end of buffer since we're copying in reverse */
    pSourceBytes += nCount - 1;
    pDestBytes += nCount - 1;
    if ((nCount >= (sizeof(LT_SIZE) * 4)) && (LT_ALIGNMENT(pSourceBytes) == LT_ALIGNMENT(pDestBytes))) {
        /* Copy data byte-by-byte until the pointers are word-aligned */
        while (LT_ALIGNMENT(pSourceBytes) != (sizeof(LT_SIZE) - 1)) {
            *pDestBytes-- = *pSourceBytes--;
            nCount--;
        }
        LT_SIZE       * pDestWords   = (LT_SIZE *)((LT_SIZE)pDestBytes & ~(sizeof(LT_SIZE) - 1));
        LT_SIZE const * pSourceWords = (LT_SIZE *)((LT_SIZE)pSourceBytes & ~(sizeof(LT_SIZE) - 1));
        /* Copy chunks of 8 words at a time in an unrolled loop */
        while (nCount >= sizeof(LT_SIZE) * 8) {
            *pDestWords-- = *pSourceWords--;
            *pDestWords-- = *pSourceWords--;
            *pDestWords-- = *pSourceWords--;
            *pDestWords-- = *pSourceWords--;
            *pDestWords-- = *pSourceWords--;
            *pDestWords-- = *pSourceWords--;
            *pDestWords-- = *pSourceWords--;
            *pDestWords-- = *pSourceWords--;
            nCount -= sizeof(LT_SIZE) * 8;
        }
        /* Now copy any leftover words that weren't copied in the unrolled loop */
        while (nCount >= sizeof(LT_SIZE)) {
            *pDestWords-- = *pSourceWords--;
            nCount -= sizeof(LT_SIZE);
        }
        pDestBytes = (u8 *)pDestWords + sizeof(LT_SIZE) - 1;
        pSourceBytes = (const u8 *)pSourceWords + sizeof(LT_SIZE) - 1;
    }
    /* Copy unaligned or small data byte-by-byte */
    while (nCount--) *pDestBytes-- = *pSourceBytes--;
}

void *
LTStdlibImpl_memmove(void * pDest, const void * pSource, LT_SIZE nCount) {
    // sanity check
    if (!pDest || !pSource) {
        return pDest;
    }
    if (pDest == pSource) {
        return pDest;
    } else if ((pDest < pSource) || ((u8*)pDest >= ((const u8*)pSource + nCount))) {
        LTStdlibImpl_memcpy(pDest, pSource, nCount);
    } else {
        LTStdlibImpl_memcpy_reversed(pDest, pSource, nCount);
    }
    return pDest;
}

void *
LTStdlibImpl_memset(void * pDest, int c, LT_SIZE nCount) {
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

void *
LTStdlibImpl_memdup(const void *pBuff, LT_SIZE nCount LT_CALLSITE_FUNCTION_PARAMETER) {
    void *pCopy = LTCoreImpl_Alloc(nCount LT_CALLSITE_FUNCTION_PARAMETER_PASSTHRU);
    if (!pCopy) return NULL;
    LTStdlibImpl_memcpy(pCopy, pBuff, nCount);
    return pCopy;
}

int
LTStdlibImpl_isalnum(int c) {
    return (((unsigned)c | 32) - 'a' < 26) || ((unsigned)c - '0' < 10);
}

int
LTStdlibImpl_isalpha(int c) {
    return ((unsigned)c | 32) - 'a' < 26;
}

int
LTStdlibImpl_isdigit(int c) {
    return (unsigned)c - '0' < 10;
}

int
LTStdlibImpl_isxdigit(int c) {
    return ((unsigned)c - '0' < 10) || (((unsigned)c|32)-'a' < 6);
}

int
LTStdlibImpl_isspace(int c) {
    return c == ' ' || (unsigned)c - '\t' < 5;
}

int
LTStdlibImpl_isupper(int c) {
    return (unsigned)c - 'A' < 26;
}

int
LTStdlibImpl_islower(int c) {
    return (unsigned)c - 'a' < 26;
}

int
LTStdlibImpl_toupper(int c) {
    return ((unsigned)c - 'a' < 26) ? (c & 0x5f) : c;
}

int
LTStdlibImpl_tolower(int c) {
    return ((unsigned)c - 'A' < 26) ? (c | 32) : c;
}

#if STRING_LT_IMPLEMENTATION
    STRING_TEXT_RAM_CRITICAL
    LT_SIZE LTStdlibImpl_strlen(const char * pString) {
        LT_SIZE nLen = 0;
        if (pString) while (*pString++) nLen++;
        return nLen;
    }

    STRING_TEXT_RAM_CRITICAL
    int LTStdlibImpl_strcmp(const char * pString1, const char * pString2) {
        #if STRING_FASTER_LT_STRCMP_EXPERIMENTAL
            if (pString1 && pString2) {
                while (*pString1 == *pString2++) if (*pString1++ == 0) return 0;
                return (*(unsigned char *)pString1 - *((unsigned char *)(--pString2)));
            }
            return pString1 ? (*pString1 ? 1 : 0) : (pString2 && *pString2 ? -1 : 0);
        #else
            if (!pString1) return (pString2 && *pString2) ? -1 : 0; /* DRW: if either string NULL, and other is non-null-non-empty, other greater */
            if (!pString2) return *pString1 ? 1 : 0;                /*      If both strings are (either null or empty), they are equal */
            while (1) {
                if (0 == *pString1)            return (0 == *pString2) ? 0 : -1;
                if (*pString1 > *pString2)     return 1;
                if (*pString1++ < *pString2++) return -1;
            }
        #endif
    }
#endif

int
LTStdlibImpl_strcasecmp(const char * pString1, const char * pString2) {
    if (!pString1) return (pString2 && *pString2) ? -1 : 0; /* DRW: if either string NULL, and other is non-null-non-empty, other greater */
    if (!pString2) return *pString1 ? 1 : 0;                /*      If both strings are (either null or empty), they are equal */
    char c1, c2;
    while (1) {
        if (0 == *pString1) return (0 == *pString2) ? 0 : -1;
        c1 = LTStdlibImpl_toupper(*pString1++);
        c2 = LTStdlibImpl_toupper(*pString2++);
        if (c1 > c2)        return  1;
        if (c1 < c2)        return -1;
    }
}

int
LTStdlibImpl_strncmp(const char * pString1, const char * pString2, LT_SIZE nCount) {
    if (nCount) {
        if (!pString1) return (pString2 && *pString2) ? -1 : 0; /* DRW: if either string NULL, and other is non-null-non-empty, other greater */
        if (!pString2) return *pString1 ? 1 : 0;                /*      If both strings are (either null or empty), they are equal */
        while (nCount--) {
            if (0 == *pString1)            return (0 == *pString2) ? 0 : -1;
            if (*pString1 > *pString2)     return 1;
            if (*pString1++ < *pString2++) return -1;
        }
    }
    return 0;
}

int
LTStdlibImpl_strncasecmp(const char * pString1, const char * pString2, LT_SIZE nCount) {
    if (nCount) {
        if (!pString1) return (pString2 && *pString2) ? -1 : 0; /* DRW: if either string NULL, and other is non-null-non-empty, other greater */
        if (!pString2) return *pString1 ? 1 : 0;                /*      If both strings are (either null or empty), they are equal */
        char c1, c2;
        while (nCount--) {
            if (0 == *pString1) return (0 == *pString2) ? 0 : -1;
            c1 = LTStdlibImpl_toupper(*pString1++);
            c2 = LTStdlibImpl_toupper(*pString2++);
            if (c1 > c2)        return  1;
            if (c1 < c2)        return -1;
        }
    }
    return 0;
}

char *
LTStdlibImpl_strchr(const char * pString, int c) {
    if (pString) {
        while (*pString != c) {
            if (0 == *pString) return NULL;
            pString++;
        }
        return (char *)pString;
    }
    return NULL;
}

char *
LTStdlibImpl_strrchr(const char * pString, int c) {
    char * pFound = NULL;
    if (pString) {
        while (1) {
            if ((char)c == *pString) pFound = (char *)pString;
            if (0 == *pString) break;
            pString++;
        }
    }
    return pFound;
}

char *
LTStdlibImpl_strstr(const char * pString1, const char * pString2) {
    if (pString1 && pString2) {
        int nStr2Len = LTStdlibImpl_strlen(pString2);
        while (*pString1) {
            if ((*pString1 == *pString2) && (LTStdlibImpl_strncmp(pString1, pString2, nStr2Len) == 0)) return (char*)pString1;
            pString1++;
        }
    }
    return NULL;
}

static bool
LTStdlibImpl_strendswith(const char * pString, const char * pStringSuffix) {
    u32 len2     = LTStdlibImpl_strlen(pStringSuffix);
    s32 startPos = LTStdlibImpl_strlen(pString) - len2;
    if (startPos < 0) return false;
    return (LTStdlibImpl_strncmp(pString + startPos, pStringSuffix, len2) == 0);
}

STRING_TEXT_RAM_CRITICAL
LT_SIZE LT_ISR_SAFE LTStdlibImpl_strncpyTerm(char * pStringDest, const char * pStringSource, LT_SIZE nDestBuffLen) {
    const char * pStringSourceOrig = pStringSource;
    if (pStringDest && nDestBuffLen) {
        if (pStringSource) while (--nDestBuffLen && *pStringSource) *pStringDest++ = *pStringSource++;
        *pStringDest = 0;
    }
    return pStringSource - pStringSourceOrig;
}

STRING_TEXT_RAM_CRITICAL
LT_SIZE LT_ISR_SAFE LTStdlibImpl_strncatTerm(char * pStringDest, const char * pStringSource, LT_SIZE nDestBuffLen) {
    const char * pStringSourceOrig = pStringSource;
    if (pStringDest && nDestBuffLen) {
        while (nDestBuffLen > 1 && *pStringDest) { pStringDest++; nDestBuffLen--; }
        if (pStringSource) while (--nDestBuffLen && *pStringSource) *pStringDest++ = *pStringSource++;
        if (pStringSource != pStringSourceOrig) *pStringDest = 0;
    }
    return pStringSource - pStringSourceOrig;
}

static char *
LTStdlibImpl_strupper(char *pString) {
    for (char *pCh = pString; *pCh != '\0'; pCh++) *pCh = LTStdlibImpl_toupper(*pCh);
    return pString;
}

static char *
LTStdlibImpl_strlower(char *pString) {
    for (char *pCh = pString; *pCh != '\0'; pCh++) *pCh = LTStdlibImpl_tolower(*pCh);
    return pString;
}

static bool
LTStdlibImpl_hextobyte(const char *pString, u8 numDigits, u8 *pDest) {
    if (!pString || numDigits > 2) return false;
    u8 val = 0;
    while (numDigits--) {
        val <<= 4;
        char c = *pString++;
        if (c >= '0' && c <= '9')      val += c - '0';
        else if (c >= 'a' && c <= 'f') val += 0xa + c - 'a';
        else if (c >= 'A' && c <= 'F') val += 0xa + c - 'A';
        else return false;
    };
    *pDest = val;
    return true;
}

/*_______________________
 / LT String Utilities */

LTString
LTStdlibImpl_strdup(const char * pString LT_CALLSITE_FUNCTION_PARAMETER) {
    char * s = NULL;
    if (pString) {
        int len = LTStdlibImpl_strlen(pString);
        if (NULL != (s = LTCoreImpl_Alloc(len + 1 LT_CALLSITE_FUNCTION_PARAMETER_PASSTHRU))) LTStdlibImpl_memcpy(s, pString, len + 1);
    }
    return s;
}

static LTString
LTStdlibImpl_LTStringCreateSubstring(const char *string, u32 bytePos, u32 numBytes) {
    u32 len = LTStdlibImpl_strlen(string);
    if (bytePos + numBytes > len) return NULL;
    char *string2 = lt_malloc(numBytes + 1);
    if (string2) LTStdlibImpl_strncpyTerm(string2, string + bytePos, numBytes + 1);
    return string2;
}

static void
LTStdlibImpl_LTStringSetCapacity(LTString *ltString, u32 capacity, bool allowTruncate) {
    if (!capacity) capacity = 1;
    if (*ltString) {
        if (!allowTruncate) {
            u32 requiredCap = LTStdlibImpl_strlen(*ltString) + 1;
            if (capacity < requiredCap) capacity = requiredCap;
        }
        *ltString = lt_realloc(*ltString, capacity);
    } else {
        *ltString = lt_malloc(capacity);
        capacity = 1;
    }
    LT_ASSERT(*ltString);
    (*ltString)[capacity - 1] = '\0';
}

static void
LTStdlibImpl_LTStringSet(LTString *ltString, const char *string2) {
    u32 requiredCap = LTStdlibImpl_strlen(string2) + 1;
    u32 currentCap = 0;
    if (*ltString) {
        currentCap = LT_GetCore()->GetAllocSize(*ltString);
        LT_ASSERT(currentCap);
    }
    if (requiredCap > currentCap) {
        *ltString = lt_realloc(*ltString, requiredCap);
        LT_ASSERT(*ltString);
    }
    LTStdlibImpl_strncpyTerm(*ltString, string2, requiredCap);
}

static void
LTStdlibImpl_LTStringVFormat(LTString *ltString, const char *format, lt_va_list args) {
    lt_va_list dupargs;
    lt_va_copy(dupargs, args);

    u32 currentCap = 0;
    if (*ltString) {
        currentCap = LT_GetCore()->GetAllocSize(*ltString);
        LT_ASSERT(currentCap);
    }
    u32 requiredCap = LTStdlibImpl_vsnprintf(*ltString, currentCap, format, dupargs) + 1;

    if (requiredCap > currentCap) {
        *ltString = lt_realloc(*ltString, requiredCap);
        LT_ASSERT(*ltString);
        LTStdlibImpl_vsnprintf(*ltString, requiredCap, format, args);
    }
}

static void
LTStdlibImpl_LTStringFormat(LTString *ltString, const char *format, ...) {
    lt_va_list args;
    lt_va_start(args, format);
    LTStdlibImpl_LTStringVFormat(ltString, format, args);
    lt_va_end(args);
}

static void
LTStdlibImpl_LTStringAppend(LTString *ltString, const char *string2) {
    u32 requiredCap = LTStdlibImpl_strlen(*ltString) + LTStdlibImpl_strlen(string2) + 1;
    u32 currentCap = 0;
    if (*ltString) {
        currentCap = LT_GetCore()->GetAllocSize(*ltString);
        LT_ASSERT(currentCap);
    }
    if (requiredCap > currentCap) {
        *ltString = lt_realloc(*ltString, requiredCap);
        LT_ASSERT(*ltString);
        if (currentCap == 0) **ltString = '\0';
    }
    LTStdlibImpl_strncatTerm(*ltString, string2, requiredCap);
}

static void
LTStdlibImpl_LTStringAppendBytes(LTString *ltString, const char *string2, u32 maxBytes) {
    u32 requiredCap = LTStdlibImpl_strlen(string2);
    if (maxBytes < requiredCap) requiredCap = maxBytes;
    requiredCap += LTStdlibImpl_strlen(*ltString) + 1;
    u32 currentCap = 0;
    if (*ltString) {
        currentCap = LT_GetCore()->GetAllocSize(*ltString);
        LT_ASSERT(currentCap);
    }
    if (requiredCap > currentCap) {
        *ltString = lt_realloc(*ltString, requiredCap);
        LT_ASSERT(*ltString);
        if (currentCap == 0) **ltString = '\0';
    }
    LTStdlibImpl_strncatTerm(*ltString, string2, requiredCap);
}

static void
LTStdlibImpl_LTStringAppendFormat(LTString *ltString, const char *format, ...) {
    lt_va_list args;
    lt_va_start(args, format);
    char *string2 = NULL;
    LTStdlibImpl_LTStringVFormat(&string2, format, args);
    lt_va_end(args);
    LTStdlibImpl_LTStringAppend(ltString, string2);
    lt_free(string2);
}

static void
LTStdlibImpl_LTStringAppendChar(LTString *ltString, const char ch) {
    u32 requiredCap = LTStdlibImpl_strlen(*ltString) + 2;
    u32 currentCap = 0;
    if (*ltString) {
        currentCap  = LT_GetCore()->GetAllocSize(*ltString);
        LT_ASSERT(currentCap);
    }
    if (requiredCap > currentCap) {
        *ltString = lt_realloc(*ltString, requiredCap);
        LT_ASSERT(*ltString);
    }
    (*ltString)[requiredCap - 2] = ch;
    (*ltString)[requiredCap - 1] = '\0';
}

static void
LTStdlibImpl_LTStringRemoveChars(LTString *ltString, u32 bytePos, u32 numBytes) {
    if (! *ltString) return;
    u32 len = LTStdlibImpl_strlen(*ltString);
    if (bytePos == (u32)-1) {
        if (len - numBytes < len) {
            (*ltString)[len - numBytes] = '\0';
        }
        return;
    }
    u32 suffix = bytePos + numBytes;
    if (suffix > len) return;
    LTStdlibImpl_memmove(*ltString + bytePos, *ltString + suffix, len - suffix + 1);
}

static void
LTStdlibImpl_LTStringInsert(LTString *ltString, u32 bytePos, const char *string2) {
    u32 len1 = LTStdlibImpl_strlen(*ltString);
    if (bytePos > len1) return;
    u32 len2 = LTStdlibImpl_strlen(string2);
    u32 requiredCap = len1 + len2 + 1;
    u32 currentCap  = LT_GetCore()->GetAllocSize(*ltString);
    LT_ASSERT(currentCap);
    if (requiredCap > currentCap) {
        *ltString = lt_realloc(*ltString, requiredCap);
        LT_ASSERT(*ltString);
    }
    LTStdlibImpl_memmove(*ltString + bytePos + len2, *ltString + bytePos,  len1 - bytePos + 1);
    LTStdlibImpl_memcpy(*ltString + bytePos, string2, len2);
}

static void
LTStdlibImpl_LTStringStripWhitespace(LTString * ltString, bool fromStart, bool fromEnd) {
    if (! *ltString) return;
    u32 len = LTStdlibImpl_strlen(*ltString);
    if (fromEnd) {
        while (len && LTStdlibImpl_isspace((*ltString)[len - 1])) len--;
        (*ltString)[len] = '\0';
    }
    if (fromStart) {
        u32 ix = 0;
        while (LTStdlibImpl_isspace((*ltString)[ix])) ix++;
        if (ix) LTStdlibImpl_memmove(*ltString, *ltString + ix, len - ix + 1);
    }
}

/*____________________________
 / String to number functions */

#define DBL_COMP_MANTISSA_WHOLE_PART       0
#define DBL_COMP_MANTISSA_FRACTIONAL_PART  1
#define DBL_COMP_EXPONENT                  2

static bool
ParseNaN(const char ** ppString) {
    if ((((*ppString)[0] == 'n') || ((*ppString)[0] == 'N')) &&
        (((*ppString)[1] == 'a') || ((*ppString)[1] == 'A')) &&
        (((*ppString)[2] == 'n') || ((*ppString)[2] == 'N'))) {
        *ppString += 3;
        return true;
    }
    return false;
}

static bool
ParseInfinity(const char ** ppString) {

    if ((((*ppString)[0] == 'i') || ((*ppString)[0] == 'I')) &&
        (((*ppString)[1] == 'n') || ((*ppString)[1] == 'N')) &&
        (((*ppString)[2] == 'f') || ((*ppString)[2] == 'F'))) {
        *ppString += 3;
        if ((((*ppString)[0] == 'i') || ((*ppString)[0] == 'I')) &&
            (((*ppString)[1] == 'n') || ((*ppString)[1] == 'N')) &&
            (((*ppString)[2] == 'i') || ((*ppString)[2] == 'I')) &&
            (((*ppString)[3] == 't') || ((*ppString)[3] == 'T')) &&
            (((*ppString)[4] == 'y') || ((*ppString)[4] == 'Y'))) {
            *ppString += 5;
        }
        return true;
    }
    return false;
}

static void
ParseBase(const char ** ppString, int * pMantissaBase, int * pExpBase, char * pExpChar) {
    if (((*ppString)[0] == '0') &&
        (((*ppString)[1] == 'x') || ((*ppString)[1] == 'X'))) {
        *pMantissaBase = 16;
        *pExpBase = 2;
        *pExpChar = 'p';
        *ppString += 2;
    } else {
        *pMantissaBase = 10;
        *pExpBase = 10;
        *pExpChar = 'e';
    }
}

static double
ScaleDouble(int nMantissaBase, double fMantissa, int nMantissaSign, int nFractionDigits, int nExpBase, int nExponent, int nExponentSign) {
    bool bShouldScale = false;
    double scaleFactor = 1.0;
    double fValue;
    if (nExponent > 0) {
        for (; nExponent > 0; --nExponent) scaleFactor *= nExpBase;
        if (nExponentSign == -1) scaleFactor = 1.0 / scaleFactor;
        bShouldScale = true;
    }
    if (nFractionDigits > 0) {
        double fractionFactor = 1.0;
        for (; nFractionDigits > 0; --nFractionDigits) fractionFactor *= nMantissaBase;
        scaleFactor /= fractionFactor;
        bShouldScale = true;
    }
    if (bShouldScale) {
        if (fMantissa > (LT_DBL_MAX / scaleFactor)) {
            fValue = LT_HUGE_VAL;
        } else fValue = fMantissa * scaleFactor;
    } else fValue = fMantissa;
    if (nMantissaSign == -1) return -fValue;
    else                     return fValue;
}

double
LTStdlibImpl_strtod(const char * pString, char ** pStringToSet) {
    if (!pString) return 0.0;
    int curComponent = DBL_COMP_MANTISSA_WHOLE_PART;
    int nMantissaSign = 0;
    int nMantissaBase;
    double fMantissa = 0;
    int nFractionDigits = 0;
    int nExponentSign = 0;
    int nExpBase;
    int nExponent = 0;
    char cExpChar;
    while (LTStdlibImpl_isspace(*pString)) ++pString;
         if (*pString == '+') { nMantissaSign =  1; ++pString; }
    else if (*pString == '-') { nMantissaSign = -1; ++pString; }
    if (ParseNaN(&pString)) {
        if (pStringToSet) *pStringToSet = (char*)pString;
        return LT_NAN;
    }
    if (ParseInfinity(&pString)) {
        if (pStringToSet) *pStringToSet = (char*)pString;
        return (nMantissaSign == -1) ? -LT_INFINITY : LT_INFINITY;
    }
    ParseBase(&pString, &nMantissaBase, &nExpBase, &cExpChar);
    const double maxMantissaValue = LT_DBL_MAX / nMantissaBase;
    for (; *pString; ++pString) {
        u8 nDigit;
        if (LTStdlibImpl_isdigit(*pString)) {
            nDigit = *pString - '0';
        } else if ((nMantissaBase == 16) && LTStdlibImpl_isxdigit(*pString)) {
            nDigit = LTStdlibImpl_tolower(*pString) - 'a' + 10;
        } else if ((curComponent == DBL_COMP_MANTISSA_WHOLE_PART) && (*pString == '.')) {
            curComponent = DBL_COMP_MANTISSA_FRACTIONAL_PART;
            continue;
        } else if ((curComponent < DBL_COMP_EXPONENT) && (LTStdlibImpl_tolower(*pString) == cExpChar)) {
            curComponent = DBL_COMP_EXPONENT;
            continue;
        } else if ((curComponent == DBL_COMP_EXPONENT) && (nExponentSign == 0) && (*pString == '-')) {
            nExponentSign = -1;
            continue;
        } else if ((curComponent == DBL_COMP_EXPONENT) && (nExponentSign == 0) && (*pString == '+')) {
            nExponentSign = 1;
            continue;
        } else break;
        if (nDigit >= nMantissaBase) break;
        if ((curComponent == DBL_COMP_MANTISSA_WHOLE_PART) ||
            (curComponent == DBL_COMP_MANTISSA_FRACTIONAL_PART)) {
            if (fMantissa <= maxMantissaValue) fMantissa = (fMantissa * nMantissaBase) + nDigit;
            if (curComponent == DBL_COMP_MANTISSA_FRACTIONAL_PART) ++nFractionDigits;
        } else if (curComponent == DBL_COMP_EXPONENT) {
            nExponent = (nExponent * 10) + nDigit; /* The exponent is always expressed in decimal */
        }
    }
    if (pStringToSet != NULL) *pStringToSet = (char*)pString;
    return ScaleDouble(nMantissaBase, fMantissa, nMantissaSign, nFractionDigits, nExpBase, nExponent, nExponentSign);
}

/* This macro generates a function definition for the LTStdlibImpl_strto[u/s][32/64] family of functions.
   @param TypePrefix - the return type prefix (e.g. u or s for unsigned or signed respectively)
   @param NumBits - the number of bits for the return type (e.g. 32 or 64)
   @param MaxPositiveMagnitude - the maximum magnitude of a positive value of the given type
   @param MaxNegativeMagnitude - the maximum magnitude of a negative value of the given type.
                                 Note that this value is a MAGNITUDE not a signed value. For
                                 example, a type whose most negative value is -5 would specify
                                 a MaxNegativeMagnitude of 5. */
#define STRING_TO_NUMBER(TypePrefix, NumBits, MaxPositiveMagnitude, MaxNegativeMagnitude)                  \
TypePrefix##NumBits                                                                                        \
LTStdlibImpl_strto##TypePrefix##NumBits(const char * pString, char ** pStringToSet, int base) {            \
    if (!pString) return 0;                                                                                \
    u##NumBits nValue = 0;                                                                                 \
    bool bIsNegative = false;                                                                              \
    bool bOverflow = false;                                                                                \
    while (LTStdlibImpl_isspace(*pString)) ++pString;                                                      \
         if (*pString == '+') {                     ++pString; }                                           \
    else if (*pString == '-') { bIsNegative = true; ++pString; }                                           \
    if (*pString == '0') {                                                                                 \
        ++pString;                                                                                         \
        if ((*pString == 'b') || (*pString == 'B')) {                                                      \
            if (base == 0) base = 2;                                                                       \
            if (base == 2) ++pString;                                                                      \
        } else if ((*pString == 'x') || (*pString == 'X')) {                                               \
            if (base == 0)  base = 16;                                                                     \
            if (base == 16) ++pString;                                                                     \
        } else if (base == 0) base = 8;                                                                    \
    } else if (base == 0) base = 10;                                                                       \
    const u##NumBits maxValue = (bIsNegative ? MaxNegativeMagnitude : MaxPositiveMagnitude);               \
    const u##NumBits maxLastUpper = maxValue / base;                                                       \
    const u##NumBits maxLastDigit = maxValue % base;                                                       \
    for (; *pString != '\0'; ++pString) {                                                                  \
        u8 nDigit;                                                                                         \
             if (LTStdlibImpl_isdigit(*pString)) nDigit = *pString - '0';                                  \
        else if (LTStdlibImpl_isalpha(*pString)) nDigit = LTStdlibImpl_tolower(*pString) - 'a' + 10;       \
        else break;                                                                                        \
        if (nDigit >= base) break;                                                                         \
        if (!bOverflow) {                                                                                  \
            if ((nValue < maxLastUpper) || ((nValue == maxLastUpper) && (nDigit <= maxLastDigit))) {       \
                nValue = (nValue * base) + nDigit;                                                         \
            } else {                                                                                       \
                nValue = maxValue;                                                                         \
                bOverflow = true;                                                                          \
            }                                                                                              \
        }                                                                                                  \
    }                                                                                                      \
    if (pStringToSet != NULL) *pStringToSet = (char*)pString;                                              \
    if (bIsNegative) return (TypePrefix##NumBits)-nValue;                                                  \
    else             return (TypePrefix##NumBits)nValue;                                                   \
}

STRING_TO_NUMBER(s, 32, LT_S32_MAX, ((u32)LT_S32_MAX) + 1)
STRING_TO_NUMBER(s, 64, LT_S64_MAX, ((u64)LT_S64_MAX) + 1)
STRING_TO_NUMBER(u, 32, LT_U32_MAX, LT_U32_MAX)
STRING_TO_NUMBER(u, 64, LT_U64_MAX, LT_U64_MAX)

/*____________________________
 / The illustrious quicksort */

#define INSERTION_SORT_THRESHOLD           10
#define ELEM_PTR(ElemIdx)                  (((u8*)(pBase)) + ((nSize) * (ElemIdx)))
#define COMPARE_ELEM(Elem1Idx, Elem2Idx)   pCompareFunc(ELEM_PTR(Elem1Idx), ELEM_PTR(Elem2Idx), pClientData)

static void
SwapElements(void * pBase, LT_SIZE nSize, int nIdx1, int nIdx2, int * pPivot) {
    if (pPivot) {
            if (*pPivot == nIdx1) *pPivot = nIdx2;
       else if (*pPivot == nIdx2) *pPivot = nIdx1;
    }
    u8* pElem1 = ELEM_PTR(nIdx1);
    u8* pElem2 = ELEM_PTR(nIdx2);
    for (LT_SIZE i = 0; i < nSize; ++i, ++pElem1, ++pElem2) {
        u8 b = *pElem1;
        *pElem1 = *pElem2;
        *pElem2 = b;
    }
}

static void
InsertionSort(void * pBase, LT_SIZE nSize, int nLow, int nHigh, LTStdlib_qsortCompareFunction * pCompareFunc, void * pClientData) {
    for (int i = nLow + 1; i <= nHigh; ++i) {
        for (int j = i; (j > nLow) && COMPARE_ELEM(j-1, j) > 0; --j) {
            SwapElements(pBase, nSize, j, j-1, NULL);
        }
    }
}

static LT_SIZE
Log2(LT_SIZE nValue) {
    LT_SIZE nLog = 0;
    while (nValue > 1) {
        nValue >>= 1;
        nLog++;
    }
    return nLog;
}

#define SQRTF_MAX_ITERATIONS 100

float
LTStdlibImpl_sqrtf(float arg) {
    if (lt_isnan(arg) || (arg < 0)) return LT_NAN;
    if (lt_isinf(arg))              return LT_INFINITY;
    if (arg == 0 || arg == 1)       return arg;

    float error = 0.0001; // Define the precision of the output
    float sqrt = arg;
    u32 iterations = 0;
    while ((lt_fabs(sqrt * sqrt - arg) > error) && (iterations++ < SQRTF_MAX_ITERATIONS)) { // Continue until the precision is satisfied
        sqrt = (sqrt + arg / sqrt) / 2;
    }

    return sqrt;
}

void
LTStdlibImpl_qsort(void * pBase, LT_SIZE nCount, LT_SIZE nSize, LTStdlib_qsortCompareFunction * pCompareFunc, void * pClientData) {
    if (!pBase || !pCompareFunc || !nCount || !nSize) return;

    typedef struct { int nLow; int nHigh; } Partition;

    LT_SIZE nMaxPartitions = Log2(nCount);
    Partition partitionStack[nMaxPartitions];
    partitionStack[0].nLow  = 0;
    partitionStack[0].nHigh = nCount - 1;
    int nPartitionStackPtr = 0;

    while (nPartitionStackPtr >= 0) {
        int nLow = partitionStack[nPartitionStackPtr].nLow;
        int nHigh = partitionStack[nPartitionStackPtr].nHigh;
        --nPartitionStackPtr;

        /* For small arrays, use insertion sort since it is faster and this helps
         * further limit stack depth. */
        if ((nHigh - nLow) <= INSERTION_SORT_THRESHOLD) {
            InsertionSort(pBase, nSize, nLow, nHigh, pCompareFunc, pClientData);
            continue;
        }

        /* 1. SELECT PIVOT - Median-of-three pivot selection ala Sedgewick. This pivot
         * selection scheme prevents the algorithm from degrading to log(n^2) complexity
         * when sorting already-sorted arrays. */
        int nMiddle = nLow + ((nHigh - nLow) / 2);
        if (COMPARE_ELEM(nMiddle, nLow) < 0)
            SwapElements(pBase, nSize, nLow, nMiddle, NULL);
        if (COMPARE_ELEM(nHigh, nLow) < 0)
            SwapElements(pBase, nSize, nLow, nHigh, NULL);
        if (COMPARE_ELEM(nMiddle, nHigh) < 0)
            SwapElements(pBase, nSize, nMiddle, nHigh, NULL);
        int nPivot = nHigh;

        /* 2. PARTITION - Form "fat partitions" ala Bentley and McIlroy - that is, split the
         * array into three groups: items less than, greater than or equal to the pivot value.
         * At the end of this procedure, nLeft and nRight point to the edges of the pool of
         * values equal to the pivot. This scheme handles duplicate values better than using
         * a single pivot value since all values equal to the pivot are accumulated in the
         * middle of the array in a single linear pass of the partitioning scheme, and only
         * the extremities need be sorted afterwards. */
        int nRight = nHigh;
        int nLeft = nLow;
        int nCurElem = nLow;
        while (nCurElem <= nRight) {
            int nComparisonResult = COMPARE_ELEM(nCurElem, nPivot);
                 if (nComparisonResult < 0) SwapElements(pBase, nSize, nLeft++,  nCurElem++, &nPivot);
            else if (nComparisonResult > 0) SwapElements(pBase, nSize, nRight--, nCurElem,   &nPivot);
            else                            ++nCurElem;
        }

        /* 3. PUSH PARTITIONS - The smaller partition is placed on the stack last (so that it
         * is processed first.) This optimization limits stack depth to log(n) worst case. */
        int nLeftPartitionSize = ((nLeft - 1) - nLow);
        int nRightPartitionSize = (nHigh - (nRight + 1));
        if ((nLeftPartitionSize >= nRightPartitionSize) && (nLeftPartitionSize > 0)) {
            ++nPartitionStackPtr;
            LT_ASSERT((LT_SIZE)nPartitionStackPtr < nMaxPartitions);
            partitionStack[nPartitionStackPtr].nLow = nLow;
            partitionStack[nPartitionStackPtr].nHigh = nLeft - 1;
        }
        if (nRightPartitionSize > 0) {
            ++nPartitionStackPtr;
            LT_ASSERT((LT_SIZE)nPartitionStackPtr < nMaxPartitions);
            partitionStack[nPartitionStackPtr].nLow = nRight + 1;
            partitionStack[nPartitionStackPtr].nHigh = nHigh;
        }
        if ((nLeftPartitionSize < nRightPartitionSize) && (nLeftPartitionSize > 0)) {
            ++nPartitionStackPtr;
            LT_ASSERT((LT_SIZE)nPartitionStackPtr < nMaxPartitions);
            partitionStack[nPartitionStackPtr].nLow = nLow;
            partitionStack[nPartitionStackPtr].nHigh = nLeft - 1;
        }
    }
}

LT_SIZE
LTStdlibImpl_bsearchIndex(const void *pSearchTerm, const void * pBase, LT_SIZE nCount, LT_SIZE nSize, LTStdlib_bsearchCompareFunction * pCompareFunc, void * pClientData) {
    // check input, and reject arrays of size > 0x7FFFFFFF; we use s32s in the algorithm and we won't likely see an array with 2 billion elements
    if (!pBase || !pCompareFunc || !nCount || !nSize || nSize > 0x7FFFFFFF) return nCount;
    s32 lowerBound = 0;
    s32 upperBound = (s32)nCount - 1;
    while (lowerBound <= upperBound)
    {
        s32 midPoint = lowerBound + ((upperBound - lowerBound) / 2);
        int result = pCompareFunc(pSearchTerm, ((u8 *)pBase + (midPoint * nSize)), pClientData);
        if (result < 0) upperBound = midPoint - 1;
        else if (result > 0) lowerBound = midPoint + 1;
        else return (LT_SIZE)midPoint;
    }
    return nCount;
}

void *
LTStdlibImpl_bsearch(const void *pSearchTerm, const void * pBase, LT_SIZE nCount, LT_SIZE nSize, LTStdlib_bsearchCompareFunction * pCompareFunc, void * pClientData) {
    if (!pBase || !pCompareFunc || !nCount || !nSize) return NULL;
    LT_SIZE nIndex = LTStdlibImpl_bsearchIndex(pSearchTerm, pBase, nCount, nSize, pCompareFunc, pClientData);
    return (nIndex == nCount) ? NULL : (void *)((u8 *)pBase + (nIndex * nSize));
}

u32 LTStdlibImpl_u32toString(u32 number, char *buff, u32 formatFlags) {
    u32 digitsPlaced = (formatFlags & kLTStdlib_FormatFlags_WidthMask); // borrow digitsPlaced to store the field width
    if (0 == digitsPlaced || NULL == buff) return 0;
    char *pChar = buff + digitsPlaced - 1;
    if (0 == number) {
        *pChar-- = '0';
        digitsPlaced = 1;
    }
    else {
        digitsPlaced = 0;
        while (number) {
            if (pChar < buff) {
                /* won't fit in allotted space, reset entire field to zeros */
                digitsPlaced = (formatFlags & kLTStdlib_FormatFlags_WidthMask); /* borrow digits placed again */
                while (digitsPlaced--) *buff++ = 0;
                return 0;
            }
            *pChar-- = (number % 10) + '0'; number /= 10;
            digitsPlaced++;
         }
    }
    // ok now the number is right justified in the field with no null terminator
    if (formatFlags & kLTStdlib_FormatFlags_RightJustify) {
        /* pad if requested */
             if (formatFlags & kLTStdlib_FormatFlags_PadWithZeros)  while (pChar >= buff) *pChar-- = '0';
        else if (formatFlags & kLTStdlib_FormatFlags_PadWithSpaces) while (pChar >= buff) *pChar-- = ' ';
        /* easy, just terminate after field */
        buff[formatFlags & kLTStdlib_FormatFlags_WidthMask] = 0;
    }
    else {
        /* move the characters to the beginning of the buffer */
        number = (formatFlags & kLTStdlib_FormatFlags_WidthMask) - digitsPlaced; /* borrow number to mean offset from beginning, no longer needed */
        if (number) {
            pChar++;
            buff += (formatFlags & kLTStdlib_FormatFlags_WidthMask);
            while (pChar < buff) { *(pChar - number) = *pChar; pChar++; }
            pChar -= ((formatFlags & kLTStdlib_FormatFlags_WidthMask) - digitsPlaced);
                 if (formatFlags & kLTStdlib_FormatFlags_PadWithZeros)  while (pChar < buff) *pChar++ = '0';
            else if (formatFlags & kLTStdlib_FormatFlags_PadWithSpaces) while (pChar < buff) *pChar++ = ' ';
        }
        else pChar = pChar + digitsPlaced + 1;
        *pChar = 0;
    }
    return digitsPlaced;
}

/*____________________________________
 / LTStdlibImpl Interface Definition */
define_LTLIBRARY_INTERFACE(LTStdlib, 0, 1)
    .memcmp         = &LTStdlibImpl_memcmp,
    .memcpy         = &LTStdlibImpl_memcpy,
    .memmove        = &LTStdlibImpl_memmove,
    .memset         = &LTStdlibImpl_memset,
    .memdup         = &LTStdlibImpl_memdup,

    .isalnum        = &LTStdlibImpl_isalnum,
    .isalpha        = &LTStdlibImpl_isalpha,
    .isdigit        = &LTStdlibImpl_isdigit,
    .isxdigit       = &LTStdlibImpl_isxdigit,
    .isspace        = &LTStdlibImpl_isspace,
    .isupper        = &LTStdlibImpl_isupper,
    .islower        = &LTStdlibImpl_islower,
    .toupper        = &LTStdlibImpl_toupper,
    .tolower        = &LTStdlibImpl_tolower,

    .vsnprintf      = &LTStdlibImpl_vsnprintf,
    .snprintf       = &LTStdlibImpl_snprintf,

    .strlen         = &LTStdlibImpl_strlen,
    .strcmp         = &LTStdlibImpl_strcmp,
    .strcasecmp     = &LTStdlibImpl_strcasecmp,
    .strncmp        = &LTStdlibImpl_strncmp,
    .strncasecmp    = &LTStdlibImpl_strncasecmp,
    .strchr         = &LTStdlibImpl_strchr,
    .strrchr        = &LTStdlibImpl_strrchr,
    .strstr         = &LTStdlibImpl_strstr,

    .strendswith    = &LTStdlibImpl_strendswith,
    .strncpyTerm    = &LTStdlibImpl_strncpyTerm,
    .strncatTerm    = &LTStdlibImpl_strncatTerm,
    .strupper       = &LTStdlibImpl_strupper,
    .strlower       = &LTStdlibImpl_strlower,
    .hextobyte      = &LTStdlibImpl_hextobyte,

    .strdup         = &LTStdlibImpl_strdup,

    .LTStringCreateSubstring  = &LTStdlibImpl_LTStringCreateSubstring,
    .LTStringSetCapacity      = &LTStdlibImpl_LTStringSetCapacity,
    .LTStringSet              = &LTStdlibImpl_LTStringSet,
    .LTStringVFormat          = &LTStdlibImpl_LTStringVFormat,
    .LTStringFormat           = &LTStdlibImpl_LTStringFormat,
    .LTStringAppendFormat     = &LTStdlibImpl_LTStringAppendFormat,
    .LTStringAppend           = &LTStdlibImpl_LTStringAppend,
    .LTStringAppendBytes      = &LTStdlibImpl_LTStringAppendBytes,
    .LTStringAppendChar       = &LTStdlibImpl_LTStringAppendChar,
    .LTStringRemoveChars      = &LTStdlibImpl_LTStringRemoveChars,
    .LTStringInsert           = &LTStdlibImpl_LTStringInsert,
    .LTStringStripWhitespace  = &LTStdlibImpl_LTStringStripWhitespace,

    .strtod         = &LTStdlibImpl_strtod,
    .strtos32       = &LTStdlibImpl_strtos32,
    .strtos64       = &LTStdlibImpl_strtos64,
    .strtou32       = &LTStdlibImpl_strtou32,
    .strtou64       = &LTStdlibImpl_strtou64,

    .sqrtf          = &LTStdlibImpl_sqrtf,

    .qsort          = &LTStdlibImpl_qsort,
    .bsearch        = &LTStdlibImpl_bsearch,
    .bsearchIndex   = &LTStdlibImpl_bsearchIndex,

    .u32toString    = &LTStdlibImpl_u32toString

LTLIBRARY_DEFINITION;

/*________________________
 / LT_GetStdlib support */
LTStdlib * LT_ISR_SAFE LT_GetStdlib(void) {
    return (LTStdlib *)&s_LTStdlib;
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  05-Feb-21   augustus    created
 *  06-Feb-22   augustus    strlen now returns LT_SIZE instead of int
 *  06-Feb-22   augustus    added EXPERIMENTAL string functions for dhrystone based
 *                          differential analysis of this stdlib impl performance
 *  31-Mar-24   augustus    added strncatTerm, bsearch, and bsearchIndex
 */
