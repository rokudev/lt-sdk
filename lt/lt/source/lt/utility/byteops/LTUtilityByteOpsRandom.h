/*******************************************************************************
 * source/lt/utility/LTUtilityByteOpsRandom.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_UTILITY_LTUTILITYBYTEOPSRANDOM_H
#define ROKU_LT_SOURCE_LT_UTILITY_LTUTILITYBYTEOPSRANDOM_H

#include <lt/LTTypes.h>

void LTUtilityByteOpsRandom_Init(void);
void LTUtilityByteOpsRandom_Fini(void);
void LTUtilityByteOpsRandom_GenRandomBytes(u8 * pBuffToFill, u32 nBytes);
void LTUtilityByteOpsRandom_GenRandomBytesAsHexString(u32 nNumHexBytes, char * pStringBuff, u32 nBuffSize);

#endif /* #ifndef ROKU_LT_SOURCE_LT_UTILITY_LTUTILITYBYTEOPSRANDOM_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Nov-21   augustus    created
 */
