/*******************************************************************************
 * source/lt/utility/LTUtilityByteOpsRandom.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_UTILITY_LTUTILITYBYTEOPSUUID_H
#define ROKU_LT_SOURCE_LT_UTILITY_LTUTILITYBYTEOPSUUID_H

#include <lt/LTTypes.h>

void LTUtilityByteOpsUUID_GenUUID(u8 uuid[16]);
bool LTUtilityByteOpsUUID_UUIDToString(const u8 uuid[16], char * pStringBuff, u32 nBuffSize);
bool LTUtilityByteOpsUUID_StringToUUID(const char * pUUIDString, u8 uuid[16]);
bool LTUtilityByteOpsUUID_IsNullUUID(const u8 uuid[16]);
void LTUtilityByteOpsUUID_NullifyUUID(u8 uuid[16]);

#endif /* #ifndef ROKU_LT_SOURCE_LT_UTILITY_LTUTILITYBYTEOPSUUID_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Nov-21   augustus    created
 */
