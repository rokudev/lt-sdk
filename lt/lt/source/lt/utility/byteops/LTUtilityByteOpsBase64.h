/*******************************************************************************
 * source/lt/utility/LTUtilityByteOpsBase64.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_UTILITY_LTUTILITYBYTEOPSBASE64_H
#define ROKU_LT_SOURCE_LT_UTILITY_LTUTILITYBYTEOPSBASE64_H

#include <lt/LTTypes.h>

u32 LTUtilityByteOpsBase64_Base64Encode(const u8 * pBytesToEncode, u32 nNumBytesToEncode, char * pBase64StringBufferToFill, u32 nBase64StringBufferSize);
u32 LTUtilityByteOpsBase64_Base64Decode(const char * pBase64EncodedString, u32 nBase64EncodedStringStrLen, u8 * pDecodedDataBufferToFill, u32 nDecodedDataBufferSize);
u32 LTUtilityByteOpsBase64_GetBase64EncodeBufferRequirement(u32 nNumBytesToEncode);
u32 LTUtilityByteOpsBase64_GetBase64DecodeBufferRequirement(u32 nBase64EncodedStringStrLen);

#endif /* #ifndef ROKU_LT_SOURCE_LT_UTILITY_LTUTILITYBYTEOPSBASE64_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Nov-21   augustus    created
 */
