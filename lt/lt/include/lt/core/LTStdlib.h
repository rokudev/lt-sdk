/******************************************************************************
 * <lt/core/LTStdlib.h>                                      LTStdlib interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

/**
 * @defgroup ltcore_stdlib LTStdlib
 * @ingroup ltcore
 * @{
 *
 * @brief LTStdlib is a curated set of functions in the style of the C standard library.
 *
 * The purpose of LTStdlib is to:
 *   - make available the most commonly used subet of useful familiar std-c style functions
 *   - isolate LT libraries from vendor bugs, inconsistent standards compliance, and unwanted dependencies
 *   - prevent static linkage to maximize economy of code size and update flexibility
 *
 * Convenience macros are defined for optimizing ease of use that allow transparent function calling through
 * the LTStdlib interface without requiring an extra hop through the LTCore interface.  The macros
 * take the the names of their corresponding stdlib functions with lt_ prepended, e.g.
 * @c lt_memcpy, @c lt_strcmp, and @c lt_qsort.
 *
 * The lt_ prefix is intentional; it exists for avoidance of namespace collisions and for
 * elimination of non-deterministic unintended function invocation.
 *
 * In addition to traditional "lt_" stdlib style functions, also included are "ltstring_" functions that
 * provide a rich set of string operations with transparent memory allocation and on-demand allocation resizing
 * on type "LTString" - a standard heap allocated C-string character pointer (typedef char * LTString),
 * that can be passed into any function that accepts a char * or const char *, such as lt_strlen, lt_strcmp,
 * lt_strncpyTerm, etc.
 */

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTSTDLIB_H
#define ROKU_LT_INCLUDE_LT_CORE_LTSTDLIB_H

#include <lt/LT.h>
LT_EXTERN_C_BEGIN

/*******************************
*******************************
* LTStdlib callback typedefs */
typedef int   (LTStdlib_qsortCompareFunction)(const void * pValue1, const void * pValue2, void * pClientData);
typedef int   (LTStdlib_bsearchCompareFunction)(const void * pSearchTerm, const void * pValue, void * pClientData);

/**********************
 *********************
 * typedef LTString */
typedef char * LTString;
    /**< the type of a dynamically allocated C-string
     *
     * LTStrings are C-strings allocated from the LT memory allocator and automatically resized as required
     * when using ltstring_ functions. LTString pointers point to C-string data, so they may be passed into any
     * function that accepts a char * or const char * string argument, like lt_strcmp() or lt_strncpyTerm().
     *
     * LT strings may be created as follows: <pre>
     *   LTString initiallyEmptyString = ltstring_create("");
     *   LTString initiallyEmptyString = ltstring_createempty(initialCapacity);
     *   LTString initiallyEmptyString = NULL;
     *   LTString initializedString    = ltstring_create("initial value");
     *   LTString initializedSubstring = ltstring_createsubstring(string, x, y); </pre>
     *
     * Yes, NULL is a valid empty LTString and may be directly input to LTString functions, \a e.g.: <pre>
     *   LTString initiallyEmptyString = NULL;
     *   ltstring_set(&initiallyEmptyString, "initialized"); </pre>
     * Or, for example: <pre>
     *   LTString initiallyEmptyString = NULL; // or ltstring_create("") or ltstring_createempty(64) ...
     *   ltstring_format(&initiallyEmptyString, format, ...); </pre>
     *
     * LT strings must be destroyed when no longer in use: <pre>
     *   ltstring_destroy(ltString); // -OR-
     *   lt_free(ltString); </pre>
     *
     * The convenience of LTString and the ltstring_ functions is that the storage for the underlying string
     * will automatically grow to accommodate the string's size requirement. However the allocated size will
     * never shrink unless the client explicitly requests some trim, which reduces the string's allocation to
     * its minimum size: its current string length + 1. To trim a string to its minimum allocation size, use: <pre>
     *       ltstring_trim(&ltString); </pre>
     *
     * @note Never use the return value from any of the stdlib style substring functions (e.g. lt_strchr,
     *       lt_strrchr, or lt_strstr) as an LTString. A substring of an LTString is not an LTString; and
     *       using one as such will result in memory leaks, corruption and crashes. */

/********************************
********************************
* LTStdlib convenience macros */
#define lt_memcmp                   LT_GetStdlib()->memcmp
#define lt_memcpy                   LT_GetStdlib()->memcpy
#define lt_memmove                  LT_GetStdlib()->memmove
#define lt_memset                   LT_GetStdlib()->memset
#define lt_memdup(pMem, nBytes)     LT_GetStdlib()->memdup((pMem), (nBytes) comma_lt_callsite())

#define lt_malloc(nBytes)           LT_GetCore()->Alloc((nBytes) comma_lt_callsite())
#define lt_realloc(pMem, nBytes)    LT_GetCore()->ReAlloc((pMem), (nBytes) comma_lt_callsite())
#define lt_free(pMem)               LT_GetCore()->Free((pMem) comma_lt_callsite())
#define lt_malloc_from_region(region, nBytes) \
    LT_GetCore()->AllocFromRegion((LTMemoryRegion)(region), (nBytes) comma_lt_callsite())
    /**< Region-aware allocation.  Pulls the allocation from a BSP-registered region
     *   (e.g. DMA-only RAM).  The region is identified by an LTMemoryRegion value,
     *   typically obtained via LT_GetCore()->GetNamedMemoryRegion(name).  An
     *   LTMemoryRegion value of 0 is a sentinel meaning "no specific region": the
     *   call falls through to the unrestricted allocator (equivalent to lt_malloc).
     *   Non-zero values are 1-based indices into LTCoreBSP_LTHeapConfig.pRegions[].
     *   Returns NULL on invalid region, exhaustion, or zero size.  Free via lt_free().
     *
     *   Typical usage:
     *       LTMemoryRegion r = LT_GetCore()->GetNamedMemoryRegion("wifi_dma");
     *       void *p = lt_malloc_from_region(r, nBytes);  / falls back to lt_malloc if absent /
     */

#define lt_isalnum                  LT_GetStdlib()->isalnum
#define lt_isalpha                  LT_GetStdlib()->isalpha
#define lt_isdigit                  LT_GetStdlib()->isdigit
#define lt_isxdigit                 LT_GetStdlib()->isxdigit
#define lt_isspace                  LT_GetStdlib()->isspace
#define lt_isupper                  LT_GetStdlib()->isupper
#define lt_islower                  LT_GetStdlib()->islower
#define lt_toupper                  LT_GetStdlib()->toupper
#define lt_tolower                  LT_GetStdlib()->tolower
#define lt_hextobyte                LT_GetStdlib()->hextobyte

#define lt_strlen                   LT_GetStdlib()->strlen
#define lt_strcmp                   LT_GetStdlib()->strcmp
#define lt_strcasecmp               LT_GetStdlib()->strcasecmp
#define lt_strncmp                  LT_GetStdlib()->strncmp
#define lt_strncasecmp              LT_GetStdlib()->strncasecmp
#define lt_strchr                   LT_GetStdlib()->strchr
#define lt_strrchr                  LT_GetStdlib()->strrchr
#define lt_strstr                   LT_GetStdlib()->strstr
#define lt_strendswith              LT_GetStdlib()->strendswith
#define lt_strdup(pStr)             LT_GetStdlib()->strdup((pStr) comma_lt_callsite())
#define lt_strupper                 LT_GetStdlib()->strupper
#define lt_strlower                 LT_GetStdlib()->strlower
#define lt_strtod                   LT_GetStdlib()->strtod
#define lt_strtos32                 LT_GetStdlib()->strtos32
#define lt_strtos64                 LT_GetStdlib()->strtos64
#define lt_strtou32                 LT_GetStdlib()->strtou32
#define lt_strtou64                 LT_GetStdlib()->strtou64
#define lt_strncpyTerm              LT_GetStdlib()->strncpyTerm
#define lt_strncatTerm              LT_GetStdlib()->strncatTerm
#define lt_snprintf                 LT_GetStdlib()->snprintf
#define lt_vsnprintf                LT_GetStdlib()->vsnprintf
#define lt_u32toString              LT_GetStdlib()->u32toString

#define lt_qsort                    LT_GetStdlib()->qsort
#define lt_bsearch                  LT_GetStdlib()->bsearch
#define lt_bsearchIndex             LT_GetStdlib()->bsearchIndex

#define lt_abs                      lt_labs
#define lt_labs                     lt_inline_labs
#define lt_llabs                    lt_inline_llabs
#define lt_fabs                     lt_inline_fabs
#define lt_isinf                    lt_inline_isinf
#define lt_isnan                    lt_inline_isnan
#define lt_sqrtf                    LT_GetStdlib()->sqrtf

#define ltstring_create             lt_strdup
#define ltstring_createempty        ltstring_inline_createempty
#define ltstring_createsubstring    LT_GetStdlib()->LTStringCreateSubstring
#define ltstring_destroy            lt_free
#define ltstring_setcapacity        LT_GetStdlib()->LTStringSetCapacity
#define ltstring_trim(s)            LT_GetStdlib()->LTStringSetCapacity((s), 1, false)
#define ltstring_set                LT_GetStdlib()->LTStringSet
#define ltstring_vformat            LT_GetStdlib()->LTStringVFormat
#define ltstring_format             LT_GetStdlib()->LTStringFormat
#define ltstring_appendformat       LT_GetStdlib()->LTStringAppendFormat
#define ltstring_append             LT_GetStdlib()->LTStringAppend
#define ltstring_appendbytes        LT_GetStdlib()->LTStringAppendBytes
#define ltstring_appendchar         LT_GetStdlib()->LTStringAppendChar
#define ltstring_removechars        LT_GetStdlib()->LTStringRemoveChars
#define ltstring_insert             LT_GetStdlib()->LTStringInsert
#define ltstring_stripwhitespace    LT_GetStdlib()->LTStringStripWhitespace
#define ltstring_empty              ltstring_inline_empty
#define ltstring_isempty            ltstring_inline_isempty

/************************
************************
* LTStdlib Interface */
typedef_LTLIBRARY_INTERFACE(LTStdlib, 1) {

    int      (* memcmp)(const void * pBuff1, const void * pBuff2, LT_SIZE nCount);
    void *   (* memcpy)(void * pDest, const void * pSource, LT_SIZE nCount);
    void *   (* memmove)(void * pDest, const void * pSource, LT_SIZE nCount);
    void *   (* memset)(void * pDest, int c, LT_SIZE nCount);
    void *   (* memdup)(const void * pBuff, LT_SIZE nCount LT_CALLSITE_FUNCTION_PARAMETER);

    int      (* isalnum)(int c);
    int      (* isalpha)(int c);
    int      (* isdigit)(int c);
    int      (* isxdigit)(int c);
    int      (* isspace)(int c);
    int      (* isupper)(int c);
    int      (* islower)(int c);
    int      (* toupper)(int c);
    int      (* tolower)(int c);

    bool     (* hextobyte)(const char * pString, u8 numDigits, u8 * pDest);
        /**< Convert one or two hexadecimal nibbles to an unsigned integer byte, ignoring case.
         * @param[in] pString pointer to hex digits to convert.
         * @param[in] numDigits convert one (1) or two (2) hex nibbles.
         * @param[out] pDest Destination for conversion: range is [0x0, 0xf] for one nibble
         *                   and [0x0, 0xff] for two nibbles.
         * @return true if hex digits are valid and conversion is successful.
         * @return false if digits are invalid, pString is NULL or numDigits > 2. */

    int      (* vsnprintf)(char * pString, LT_SIZE nSize, const char * pFormat, lt_va_list args) LT_ISR_SAFE;
        /**<
         * Produces printf-formatted output, writing at most nSize bytes
         * (including the null terminator) to a buffer.
         *
         * @param[in] pString The buffer to receive the formatted output.
         * @param[in] nSize The maximum number of bytes to write to the buffer (including
         *                  the null terminator).
         * @param[in] pFormat The @p printf format string with the following format specifiers:<pre>
         *                        %%c
         *                        %[-][width.prec]s
         *                        %[-][0][width][l|ll]<u|d|x|X>
         *                        %%<p|P>
         *                        %[.prec]<e|f> </pre>
         *                    where width and prec are the minimum width and precision, respectively.
         *                    A <code>*</code> may be substituted for width or prec where the desired
         *                    value is placed in the argument list before the numeric argument.
         * @param[in] args Arguments to be formatted, depending on the value of @p pFormat.
         * @return a negative number if an error occurred.
         * @return the number of characters occupied by the resultant output string,
         *         not including the null-terminator, independent of the nCount buffer
         *         passed in.  If the return value is greater than or equal to nCount,
         *         then the string was truncated.
         * @note A handy trick is to pass in NULL for pString and 0 for nCount.  The
         *       return value (assuming no error) will be the number of characters,
         *       excluding the null terminator, required to hold the string.  Add 1
         *       to the return value to allocate a buffer of required length.
         * @see snprintf() */

    int      (* snprintf)(char * pString, LT_SIZE nSize, const char * pFormat, ...) LT_ISR_SAFE;
        /**<
         * Produces printf-formatted output, writing at most nCount bytes
         * (including the null terminator) to a buffer.
         *
         * @note This and other unsafe functions may be deprecated in the future.
         * @param pString The buffer to receive the formatted output.
         * @param nSize the maximum number of bytes to write to the buffer (including
         *             the null terminator)
         * @param pFormat the @p printf format string
         * @param ... Arguments to be formatted, depending on the value of @p pFormat
         * @return a negative number if an error occurred.
         * @return the number of characters occupied by the resultant output string,
         *         not including the null-terminator, independent of the nCount buffer
         *         passed in.  If the return value is greater than or equal to nCount,
         *         then the string was truncated.
         * @note A handy trick is to pass in NULL for pString and 0 for nCount.  The
         *       return value (assuming no error) will be the number of characters,
         *       excluding the null terminator, required to hold the string.  Add 1
         *       to the return value to allocate a buffer of required length.
         * @see vsnprintf() */

    LT_SIZE  (* strlen)(const char * pString);
    int      (* strcmp)(const char * pString1, const char * pString2);
    int      (* strcasecmp)(const char * pString1, const char * pString2);
    int      (* strncmp)(const char * pString1, const char * pString2, LT_SIZE nCount);
    int      (* strncasecmp)(const char * pString1, const char * pString2, LT_SIZE nCount);
    char *   (* strchr)(const char * pString, int c);
    char *   (* strrchr)(const char * pString, int c);
    char *   (* strstr)(const char * pString1, const char * pString2);

    bool     (* strendswith)(const char * pString, const char * pStringSuffix);
        /**< See if the string has the given suffix.
         * @param[in] pString string to evaluate.
         * @param[in] pStringSuffix suffix to check pString with.
         * @return true if pString ends with the given pSuffix, otherwise false. */

    LT_SIZE  (* strncpyTerm)(char * pStringDest, const char * pStringSource, LT_SIZE nDestBuffSize) LT_ISR_SAFE;
        /**< strncpyTerm is not a stdlib function, it is the LT substitute for strncpy.
         *  Unlike strncpy, strncpyTerm guarantees the destination buffer is always
         *  null-terminated, even when the destination buffer is too small to accommodate
         *  the entire source string plus null terminator.  Also unlike strncpy, strncpyTerm
         *  doesn't fill the entire remaining destination buffer with zeros when the source
         *  string is smaller than the destination buffer.  Unless nDestBuffSize is 0 or
         *  pStringDest is NULL, strncpyTerm always writes a single null-term character at
         *  pStringDest[strlen(pStringSource)] when strlen(pStringSource) < nDestBuffLen
         *  or at pStringDest[nDestBuffLen-1] when strlen(pStringSource) >= nDestBuffLen.
         *  In addition, strncpyTerm returns the number of bytes written to the destination
         *  buffer (not including the null-term). This provides a simple way for successive
         *  calls to strncpyTerm to perform string concatenation for great justice.  Because of
         *  the semantic differences between them, strncpyTerm doesn't take the name of strncpy. */

    LT_SIZE  (* strncatTerm)(char * pStringDest, const char * pStringSource, LT_SIZE nDestBuffSize) LT_ISR_SAFE;
        /**< strncatTerm is not a stdlib  function, it is the LT substitute for strcat, strncat.
         *  It will cat as much as pStringDest as will fit onto the end of pStringDest within nDestBuffSize.
         *  nDestBuffSize is the size of the buffer starting at pStringDest so if pStringDest is
         *  "abcd" and nDestBuffSize is 8, at most 3 characters will be written on to the end of "abcd" followed
         *  by a null-terminator */

    char *   (* strupper)(char * pString);
        /**< Change all lower case letters in null-terminated C-string to upper case.
         * @param[in, out] pString to change to upper case.
         * @return the same string pointer passed in. */

    char *   (* strlower)(char * pString);
        /**< Change all upper case letters in null-terminated C-string to lower case.
         * @param[in, out] pString to change to lower case.
         * @return the same string pointer passed in. */

/*  _______________________________________________________________________
    The following function(s) return an LTString allocated from the heap */

    LTString (* strdup)(const char * pString LT_CALLSITE_FUNCTION_PARAMETER);
        /**< duplicate a string into a freshly allocated LTString
         * @note the client is responsible for freeing the newly allocated returned string
         * @param pString the string to duplicate  */

    LTString (* LTStringCreateSubstring)(const char * pString, u32 bytePos, u32 numBytes);
        /**< Allocate a new LT string created from the specified substring.
         * @note The client is responsible for freeing the newly allocated returned string
         * @note The substring must be valid, i.e.: bytePos + numBytes must be < strlen(string).
         * @param[in] string string to extract substring from.
         * @param[in] bytePos position of first character to extract substring from.
         * @param[in] numBytes length of returned string not including null terminator
         * @return the newly allocated substring or NULL if allocation fails
         *         or specified substring is invalid.  */

/*  _________________________________________________________________________
    The following functions may dynamically reallocate the LT string buffer /
    and therefore may change the *ltString pointer address, which is why   /
    the functions take LTString * instead of LTString.                   */

    void     (* LTStringSetCapacity)(LTString * ltString, u32 capacity, bool allowTruncate);
        /**< Set the string capacity, truncating the string if allowed.
         * @note If allowTruncate is false then capacity will not decrease below the string length + 1.
         * @note Truncated strings remain null-terminated.
         * @param[in, out] the address of the ltString to change capacity.
         * @param[in] capacity the new capacity of the string (including null terminator).
         *            if capacity is set zero actual capacity will still be one to accommodate null.
         * @param[in] allowTruncate true if operation is allowed to truncate the string. */

    void     (* LTStringSet)(LTString * ltString, const char * pString);
        /**< Replace *ltString with the specified string.
         * @param[in, out] address of ltString to set.
         * @param[in] pString the string to set with.  */

    void     (* LTStringVFormat)(LTString * ltString, const char * format, lt_va_list args);
        /**< Set *ltString to the specified formatted string.
         * @param[in, out] address of ltString to format.
         * @param[in] format string containing format.
         * @param[in] args variable argument list.
         * @see vsnprintf()  */

    void     (* LTStringFormat)(LTString * ltString, const char * format, ...);
        /**< Set *ltString to the specified formatted string.
         * @param[in, out] address of ltString to format.
         * @param[in] format string containing format.
         * @param[in] ... variable argument list.
         * @see vsnprintf()  */

    void     (* LTStringAppendFormat)(LTString * ltString, const char * format, ...);
        /**< Append the specified formatted string to *ltString.
         * @param[in, out] address of ltString to append to.
         * @param[in] format string containing format.
         * @param[in] ... variable argument list.
         * @see vsnprintf()  */

    void     (* LTStringAppend)(LTString * ltString, const char * pString);
        /**< Append specified string to *ltString.
         * @param[in, out] address of ltString to append to.
         * @param[in] pString the string to append.  */

   void      (* LTStringAppendBytes)(LTString * ltString, const char * pString, u32 maxBytes);
        /**< Append bytes from specified string to *ltString.
         * @param[in, out] address of ltString to append to.
         * @param[in] pString the string to append with.
         * @param[in] maxBytes the maximum number of bytes to append from pString.  */

    void     (* LTStringAppendChar)(LTString * ltString, char ch);
        /**< Append a character to *ltString.
         * @param[in, out] address of ltString to append character to.
         * @param[in] ch character to append.  */

    void     (* LTStringRemoveChars)(LTString * ltString, u32 bytePos, u32 numBytes);
        /**< Remove character(s) from *ltString.
         * @param[in, out] address of ltString to remove character from.
         * @param[in] bytePos position of first character to remove, use (u32)-1 to remove
         *                    last character(s).
         * @param[in] numBytes number of bytes to remove.  */

    void     (* LTStringInsert)(LTString * ltString, u32 bytePos, const char * pString);
        /**< Insert another string into *ltString.
         * @param[in, out] address of ltString to remove character from.
         * @param[in] bytePos byte position in * ltString where to insert pString.
         * @param[in] pString string to insert.  */

    void     (* LTStringStripWhitespace)(LTString * ltString, bool fromStart, bool fromEnd);
        /**< Strip whitespace from start and/or end of *ltString.
         * @param[in, out] address of ltString to remove character from.
         * @param[in] fromStart true to remove whitespace from start of string.
         * @param[in] fromEnd true to remove whitespace from end of string.  */

   /*****/

    double   (*   strtod)(const char * pString, char ** pStringToSet);
    s32      (* strtos32)(const char * pString, char ** pStringToSet, int base);
    s64      (* strtos64)(const char * pString, char ** pStringToSet, int base);
    u32      (* strtou32)(const char * pString, char ** pStringToSet, int base);
    u64      (* strtou64)(const char * pString, char ** pStringToSet, int base);
        /* Because the sizes of long and long long vary on different platforms,
         * LT provides these explicitly typed functions as substitutes for
         * strtol, strtoll, stroul, and strtoull.  Aside from the deterministic
         * specification of return value, they are equivalent. */

    float    (* sqrtf)(float arg);
        /** An implementation of square root using the Babylonian estimation method.
         *  If a negative number is provided, zero will be returned.  */

    void     (* qsort)(void * pBase, LT_SIZE nCount, LT_SIZE nSize, LTStdlib_qsortCompareFunction * pCompareFunc, void * pClientData);
        /**< reentrant quick sort
         *
         * qsort uses the quick sort algorithm to sort elements of the specified array
         * @param pBase the base of the array to sort
         * @param nCount the number of elements in the array
         * @param nSize the size of each array element
         * @param pCompareFunc the element comparison function
         * @param pClientData client data passed through to pCompareFunc */

    void *   (* bsearch)(const void * pSearchTerm, const void * pBase, LT_SIZE nCount, LT_SIZE nSize, LTStdlib_bsearchCompareFunction * pCompareFunc, void * pClientData);
        /**< binary search an array returning a pointer to the found element
         *
         * bsearch uses the binary search algorithm to find an element in a sorted array
         * @param pSearchTerm the search term to find
         * @param pBase the base of the array to search
         * @param nCount the number of elements in the array
         * @param nSize the size of each array element
         * @param pCompareFunc the element comparison function
         * @param pClientData client data passed through to pCompareFunc
         * @return a pointer to the found element or NULL if the element is not found */

    LT_SIZE   (* bsearchIndex)(const void * pSearchTerm, const void * pBase, LT_SIZE nCount, LT_SIZE nSize, LTStdlib_bsearchCompareFunction * pCompareFunc, void * pClientData);
        /**< binary search an array returning the index of found element
         *
         * bsearchIndex uses the binary search algorithm to find the index of an element in a sorted array
         * @param pSearchTerm the search term to find
         * @param pBase the base of the array to search
         * @param nCount the number of elements in the array
         * @param nSize the size of each array element
         * @param pCompareFunc the element comparison function
         * @param pClientData client data passed through to pCompareFunc
         * @return the index of the found element or nCount if the element is not found */

    u32      (*u32toString)(u32 number, char *buff, u32 formatFlags);
        /**< formats a u32 into an ascii string
         *
         * %LTStdlibImpl_u32toString performs an itoa type conversion of a 32 bit unsigned integer
         * into a string that can be right justified, left justified, 0 padded, space padded or not padded.
         * The string in buff is always null-terminated.  The purpose of this function is to provide basic
         * unsigned integer to ascii formatted conversion in a fixed field width without incurring the
         * stack space overhead penalty of lt_snprintf.
         * Here's how it works:
         * The formatFlags parameter encodes the field width in the low 8 bytes (formatFlags & 0xFF).
         * The field width is the number of characters the decoded number will be right
         * or left justified into.  This is not the same as the buffer size.  The actual buffer size must be
         * at least the field width plus 1 for the null terminator.
         * If formatFlags specifies kLTStdlib_FormatFlags_RightJustify, then the null terminator will always
         * be placed into *(buff + (formatFlags & 0xFF)).  If formatFlags specifies kLTStdlib_FormatFlags_LeftJustify,
         * and one of the padding flags is set, then the entire field width will be filled with the number and padding as
         * needed and the null terminator will also be placed after the field at *(buff + (formatFlags & 0xFF)).
         * If formatFlags specifies kLTStdlib_FormatFlags_LeftJustify, and neither of the padding flags is set, then
         * the null terminator will be placed immediately following the last digit.  To pad the number with preceding zeros
         * kLTStdlib_FormatFlags_RightJustify | kLTStdlib_FormatFlags_PadWithZeros must be used.
         * The return value indicates the actual number of digits in the number placed into the field, not accounting for padding.
         * If the field width is smaller than the number of digits of the number, then 0 is returned and the contents of
         * all field bytes will be set to 0, i.e. lt_memset(buff, 0, (formatFlags & 0xFF)).
         *
         * @param number the u32 number to convert to string
         * @param buff the buffer where to write the ascii representation of the number
         * @widthFlags the width and flags for formatting in buff
         * @return the number of digits placed in buff (sans padding), 0 if not enough room
         *
         * @note LeftJustify is the default when neither LeftJustify nor RightJustify is specified
         * @note the maximum number of digits a u32 can hold is 10, which means a field width of 10 (buffer size of 11 bytes)
         * is the minimum required to represent any u32 value.  Feel free to pass in smaller field widths if you know
         * the valid range of the number is smaller than the full u32 range.
         * @see LTStdlib_FormatFlags */
} LTLIBRARY_INTERFACE;

typedef enum LTStdlib_FormatFlags {        /**< format flags for u32ToString() */
    kLTStdlib_FormatFlags_WidthMask      = 0xff,        /**< width of format field is specified in low 8 bits with this as the mask */
    kLTStdlib_FormatFlags_LeftJustify    = (1 << 9),    /**< left justify within the format field width */
    kLTStdlib_FormatFlags_RightJustify   = (1 << 10),   /**< right justify within the format field width */
    kLTStdlib_FormatFlags_PadWithZeros   = (1 << 11),   /**< pad unused field space with zeros */
    kLTStdlib_FormatFlags_PadWithSpaces  = (1 << 12)    /**< pad unused field space with spaces */
} LTStdlib_FormatFlags;

/********************************************
 ********************************************
 * LT_GetStdlib global function declaration *
 *
 * CAUTION: Except for LT_GetCore() and LT_GetStdlib(), declaring global symbols is illegal.
 *          Do not replicate this pattern in your own library interfaces.
 *
 * LTCore does this with LT_GetStdlib() by leveraging the method by
 * which every library gets its own copy of the global LT_GetCore() function.
 * You don't have that facility to leverage; you are required to only declare
 * symbols in your library public interface without static linkage, by using
 * one or more of the LTLIBRARY_INTERFACE macros found in LTTypes.h.
 */
LTStdlib * LT_GetStdlib(void); /**< for avoiding extra hops in convenience macros */

/******************************
******************************
* LTStdlib inline functions */
LT_INLINE u32      lt_inline_labs(s32 nNum)                  { return ((nNum < 0) ? (u32)(0 - nNum) : (u32)nNum);      } /**< returns absolute value of @c nNum */
LT_INLINE u64      lt_inline_llabs(s64 nNum)                 { return ((nNum < 0) ? (u64)(0 - nNum) : (u64)nNum);      } /**< returns absolute value of @c nNum */
LT_INLINE float    lt_inline_fabs(float nNum)                { return ((nNum < 0.0f) ? (float)(-nNum) : (float)nNum);  } /**< returns absolute value of @c nNum */
LT_INLINE bool     lt_inline_isinf(double x)                 { return (((x) == LT_INFINITY) || ((x) == -LT_INFINITY)); } /**< returns true iff @c x is infinity */
LT_INLINE bool     lt_inline_isnan(double x)                 { return ((x) != (x));                                    } /**< returns true iff @c x is NaN      */

LT_INLINE void     ltstring_inline_empty(char * str)         { if (str) *str = '\0';                                   } /**< empties the @c str string         */
LT_INLINE bool     ltstring_inline_isempty(const char * str) { return (!str || ('\0' == *str));                        } /**< returns true if @c str is empty   */
LT_INLINE LTString ltstring_inline_createempty(u32 capacity) { LTString ltstring = NULL;
                                                               ltstring_setcapacity(&ltstring, capacity, false);
                                                               return ltstring; }                       /**< create an empty string with an initial @c capacity */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_LTSTDLIB_H */
/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  12-Aug-20   augustus    created
 *  06-Feb-22   augustus    strlen now returns LT_SIZE instead of int
 */
