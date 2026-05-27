/******************************************************************************
 * lt/source/core/LTStdlibImpl.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_CORE_LTSTDLIBIMPL_H
#define ROKU_LT_SOURCE_LT_CORE_LTSTDLIBIMPL_H

#include <lt/core/LTStdlib.h>
LT_EXTERN_C_BEGIN

int                 LTStdlibImpl_memcmp(const void * pBuff1, const void * pBuff2, LT_SIZE nCount);
void *              LTStdlibImpl_memcpy(void * pDest, const void * pSource, LT_SIZE nCount);
void *              LTStdlibImpl_memmove(void * pDest, const void * pSource, LT_SIZE nCount);
void *              LTStdlibImpl_memset(void * pDest, int c, LT_SIZE nCount);
void *              LTStdlibImpl_memdup(const void * pBuff, LT_SIZE nCount LT_CALLSITE_FUNCTION_PARAMETER);

int                 LTStdlibImpl_isalnum(int c);
int                 LTStdlibImpl_isalpha(int c);
int                 LTStdlibImpl_isdigit(int c);
int                 LTStdlibImpl_isxdigit(int c);
int                 LTStdlibImpl_isspace(int c);
int                 LTStdlibImpl_isupper(int c);
int                 LTStdlibImpl_islower(int c);
int                 LTStdlibImpl_toupper(int c);
int                 LTStdlibImpl_tolower(int c);

int  LT_ISR_SAFE    LTStdlibImpl_vsnprintf(char * pString, LT_SIZE nSize, const char * pFormat, lt_va_list arg);
int  LT_ISR_SAFE    LTStdlibImpl_snprintf(char * pString, LT_SIZE nSize, const char * pFormat, ...) LT_PRINTF_FORMAT_FUNCTION(3);

LT_SIZE LT_ISR_SAFE LTStdlibImpl_strncpyTerm(char * pStringDest, const char * pStringSource, LT_SIZE nDestBuffSize);
LT_SIZE             LTStdlibImpl_strlen(const char * pString);
int                 LTStdlibImpl_strcmp(const char * pString1, const char * pString2);
int                 LTStdlibImpl_strcasecmp(const char * pString1, const char * pString2);
int                 LTStdlibImpl_strncmp(const char * pString1, const char * pString2, LT_SIZE nCount);
int                 LTStdlibImpl_strncasecmp(const char * pString1, const char * pString2, LT_SIZE nCount);
char *              LTStdlibImpl_strchr(const char * pString, int c);
char *              LTStdlibImpl_strrchr(const char * pString, int c);
char *              LTStdlibImpl_strstr(const char * pString1, const char * pString2);
char *              LTStdlibImpl_strdup(const char * pString LT_CALLSITE_FUNCTION_PARAMETER);

double              LTStdlibImpl_strtod(const char * pString, char ** pStringToSet);
s32                 LTStdlibImpl_strtos32(const char * pString, char ** pStringToSet, int base);
s64                 LTStdlibImpl_strtos64(const char * pString, char ** pStringToSet, int base);
u32                 LTStdlibImpl_strtou32(const char * pString, char ** pStringToSet, int base);
u64                 LTStdlibImpl_strtou64(const char * pString, char ** pStringToSet, int base);

void                LTStdlibImpl_qsort(void * pBase, LT_SIZE nCount, LT_SIZE nSize, LTStdlib_qsortCompareFunction * pCompareFunc, void * pClientData);

u32                 LTStdlibImpl_u32toString(u32 number, char *buff, u32 formatFlags);


LT_EXTERN_C_END
#endif // #ifndef ROKU_LT_SOURCE_LT_CORE_LTSTDLIBIMPL_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  28-Dec-19   augustus    created
 *  10-Jan-20   augustus    added LT_snprintf so as not to have to thunk through core when in core
 *  05-Feb-21   augustus    renamed with LTStdlibImpl_<function>s, added full interface
 *  06-Feb-22   augustus    strlen now returns LT_SIZE instead of int
 */
