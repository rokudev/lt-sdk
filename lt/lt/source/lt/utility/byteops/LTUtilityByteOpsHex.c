/*******************************************************************************
 * source/lt/utility/LTUtilityByteOpsHex.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include "LTUtilityByteOpsHex.h"

/*_____________________________________
 / LTUtilityByteOpsHex static constants */
static const char s_hexCharsUpper[] = "0123456789ABCDEF";
static const char s_hexCharsLower[] = "0123456789abcdef";

u32 LTUtilityByteOpsHex_HexEncode(const u8 * pBytesToEncode, u32 nNumBytesToEncode, char  * pHexStringBufferToFill, u32 nHexStringBufferSize, bool bLowerCase) {
    if (!pBytesToEncode || !pHexStringBufferToFill || !nNumBytesToEncode || (nHexStringBufferSize < 2 * nNumBytesToEncode + 1)) return 0;

    const char * pHexChars = bLowerCase ? s_hexCharsLower : s_hexCharsUpper;
    u32 nRetVal = nNumBytesToEncode << 1;
    while (nNumBytesToEncode--) {
        *pHexStringBufferToFill++ = pHexChars[(*pBytesToEncode) >> 4];
        *pHexStringBufferToFill++ = pHexChars[(*pBytesToEncode++) & 0xF];
    }
    *pHexStringBufferToFill = 0;

    return nRetVal;
}

LT_INLINE u8 HexDecodeNybble(char ch) {
    if (ch >= '0' && ch <= '9') return (u8)(ch - '0');
    if (ch >= 'a' && ch <= 'f') return (u8)((ch - 'a') + 10);
    if (ch >= 'A' && ch <= 'F') return (u8)((ch - 'A') + 10);
    return 0xFF;
}

LT_INLINE bool HexDecodeByte(const char * pHexEncodedString, u8 * pByteToSet) {
    u8 hi = HexDecodeNybble(*pHexEncodedString++);
    u8 lo = HexDecodeNybble(*pHexEncodedString);
    if (0xFF == hi || 0xFF == lo) return false;
    *pByteToSet = (hi << 4) | lo; return true;
}

u32 LTUtilityByteOpsHex_HexDecode(const char * pHexEncodedString, u32 nHexEncodedStringStrLen, u8 * pDecodedDataBufferToFill, u32 nDecodedDataBufferSize) {
    nHexEncodedStringStrLen >>= 1; /* strlen is now num bytes to copy; bonus: automatically shifted out the odd bit if there was one! */
    if (!pHexEncodedString || !pDecodedDataBufferToFill || !nHexEncodedStringStrLen || (nDecodedDataBufferSize < nHexEncodedStringStrLen)) return 0;

    u32 nByteCounter = 0;
    while (nHexEncodedStringStrLen-- && HexDecodeByte(pHexEncodedString, pDecodedDataBufferToFill++)) {
        nByteCounter++;
        pHexEncodedString += 2;
    }
    return nByteCounter;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Feb-22   commodus    created
 */
