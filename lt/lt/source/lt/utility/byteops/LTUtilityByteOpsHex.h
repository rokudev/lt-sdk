/*******************************************************************************
 * source/lt/utility/LTUtilityByteOpsHex.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_UTILITY_LTUTILITYBYTEOPSHEX_H
#define ROKU_LT_SOURCE_LT_UTILITY_LTUTILITYBYTEOPSHEX_H

#include <lt/LTTypes.h>

u32 LTUtilityByteOpsHex_HexEncode(const u8 * pBytesToEncode, u32 nNumBytesToEncode, char  * pHexStringBufferToFill, u32 nHexStringBufferSize, bool bLowerCase);

u32 LTUtilityByteOpsHex_HexDecode(const char * pHexEncodedString, u32 nHexEncodedStringStrLen, u8 * pDecodedDataBufferToFill, u32 nDecodedDataBufferSize);

#endif /* #ifndef ROKU_LT_SOURCE_LT_UTILITY_LTUTILITYBYTEOPSHEX_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Feb-22   commodus    created
 */
