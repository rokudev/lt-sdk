/*******************************************************************************
 * source/lt/utility/LTUtilityByteOps.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include "LTUtilityByteOpsRandom.h"
#include "LTUtilityByteOpsUUID.h"
#include "LTUtilityByteOpsBase64.h"
#include "LTUtilityByteOpsHex.h"
#include "LTUtilityByteOpsCrc.h"

static void LTUtilityByteOpsCrc_SwapBytes(u8 *buf, u32 len) {
    if (!buf || len < 2) return;
    u8 *tail = buf + len -1;
    u8 tmp;
    while (buf < tail) {
        tmp = *buf;
        *buf = *tail;
        *tail = tmp;
        ++buf;
        --tail;
    }
}

/*___________________________________________
 / LTUtilityByteOps library initialization */
static bool LTUtilityByteOpsImpl_LibInit(void) {
    LTUtilityByteOpsRandom_Init();
    return true;
}

static void LTUtilityByteOpsImpl_LibFini(void) {
    LTUtilityByteOpsRandom_Fini();
}

/*___________________________________________________
 / LTUtilityByteOps library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTUtilityByteOps,)

    .GenRandomBytes                     = &LTUtilityByteOpsRandom_GenRandomBytes,
    .GenRandomBytesAsHexString          = &LTUtilityByteOpsRandom_GenRandomBytesAsHexString,

    .GenUUID                            = &LTUtilityByteOpsUUID_GenUUID,
    .UUIDToString                       = &LTUtilityByteOpsUUID_UUIDToString,
    .StringToUUID                       = &LTUtilityByteOpsUUID_StringToUUID,
    .IsNullUUID                         = &LTUtilityByteOpsUUID_IsNullUUID,

    .Base64Encode                       = &LTUtilityByteOpsBase64_Base64Encode,
    .Base64Decode                       = &LTUtilityByteOpsBase64_Base64Decode,
    .GetBase64EncodeBufferRequirement   = &LTUtilityByteOpsBase64_GetBase64EncodeBufferRequirement,
    .GetBase64DecodeBufferRequirement   = &LTUtilityByteOpsBase64_GetBase64DecodeBufferRequirement,
    .HexEncode                          = &LTUtilityByteOpsHex_HexEncode,
    .HexDecode                          = &LTUtilityByteOpsHex_HexDecode,

    .Crc32                              = &LTUtilityByteOpsCrc_Crc32,

    .SwapBytes                          = &LTUtilityByteOpsCrc_SwapBytes,
LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Nov-21   augustus    created
 *  28-Feb-22   commodus    added hex coding functions
 */
