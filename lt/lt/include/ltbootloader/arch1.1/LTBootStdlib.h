/*******************************************************************************
 * LTBootStdlib.h                                                  LT Bootloader
 *                                                                (Arch Ver 1.1)
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef __LTBOOT_STDLIB_H_
#define __LTBOOT_STDLIB_H_

#include "LTBoot.h"

/* Aliases for the actual functions
 *  Wrappers are sometimes useful when not compiling with libc */
#define LTBootStdlib_memcpy       (__wrap_memcpy)
#define LTBootStdlib_memset       (__wrap_memset)

void * LTBootStdlib_memcpy(void * pDest, const void * pSource, LT_SIZE nCount);
/**< Optional memcpy implementation */

void * LTBootStdlib_memset(void * pDest, int c, LT_SIZE nCount);
/**< Optional memset implementation */

LT_SIZE LTBootStdlib_strncpyTerm(char * pStringDest, const char * pStringSource, LT_SIZE nDestBuffLen);
/**< Optional strncpyTerm implementation (please see LTStdlib.h for detailed information) */

int LTBootStdlib_strncmp(const char * pString1, const char * pString2, LT_SIZE nCount);
/**< Optional strncmp implementation
  * Used if a suitable version does not already exist for the platform (configured in LTBootPlatform.h).
  *
  * @param[in] pString1 Pointer to left-hand string for comparison.
  * @param[in] pString2 Pointer to right-hand string for comparision.
  * @param[in] nCount Maximum number of characters to compare.
  * @return -1 if pString1 is before pString2 in lexicographical order.
  * @return +1 if pString1 is after pString2 in lexicographical order.
  * @return 0  if pString1 and pString2 compare equal or nCount is zero.
  */

int LTBootStdlib_snprintf(char * pStringDest, LT_SIZE size, const char * pFormat, ...);
/**< Optional simplified snprintf implementation
  * Used if a suitable version does not already exist for the platform (configured in LTBootPlatform.h).
  */

int LTBootStdlib_printf(const char * pFormat, ...);
/**< Optional simplified printf implementation
  * Used if a suitable version does not already exist for the platform (configured in LTBootPlatform.h).
  */

#endif

