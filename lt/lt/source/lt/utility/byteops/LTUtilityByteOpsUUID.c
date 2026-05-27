/*******************************************************************************
 * source/lt/utility/LTUtilityByteOpsUUID.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include "LTUtilityByteOpsUUID.h"
#include "LTUtilityByteOpsRandom.h"

/*_________________________________________________
 / LTUtilityByteOpsUUID private helper functions */
static void ByteToString(const u8 byte, char * pString) {
    u8 nybble = (byte >> 4) & 0x0F;
    if (nybble < 10) *pString++ = '0' + nybble; else *pString++ = 'a' + (nybble - 10);
    nybble = byte & 0x0F;
    if (nybble < 10) *pString   = '0' + nybble; else *pString   = 'a' + (nybble - 10);
}

static bool StringToByte(const char * pString, u8 * pByte) {
    u8 hiHalf = *pString++;
    u8 loHalf = *pString;
    if (hiHalf >= '0' && hiHalf <= '9') hiHalf -= '0';
    else {
        if (hiHalf >= 'a' && hiHalf <= 'f') hiHalf = ((hiHalf - 'a') + 10);
        else return false;
    }
    if (loHalf >= '0' && loHalf <= '9') loHalf -= '0';
    else {
        if (loHalf >= 'a' && loHalf <= 'f') loHalf = ((loHalf - 'a') + 10);
        else return false;
    }
    *pByte = ((hiHalf << 4) & 0xF0) | (loHalf & 0x0F);
    return true;
}
/*____________________________________________________________________
 / LTUtilityByteOpsUUID LTUtilityByteOps public interface functions */
void LTUtilityByteOpsUUID_GenUUID(u8 uuid[16]) {
    LTUtilityByteOpsRandom_GenRandomBytes(uuid, 16);
    uuid[6] = ((uuid[6] & 0x0F) | 0x40); /* version  */
    uuid[8] = ((uuid[8] & 0x3F) | 0x80); /* reserved */
}

bool LTUtilityByteOpsUUID_UUIDToString(const u8 uuid[16], char * pStringBuff, u32 nBuffSize) {
    if (NULL == pStringBuff || nBuffSize < 37) return false;
    // "00112233-4455-4677-8899-aabbccddeeff"
    const u8 * pByte = uuid;
    u32 i = 0;
    for (; i < 4; i++) {
        ByteToString(*pByte++, pStringBuff);
        pStringBuff += 2;
    }
    *pStringBuff++ = '-';
    for (i = 0; i < 3; i++) {
        ByteToString(*pByte++, pStringBuff); pStringBuff += 2;
        ByteToString(*pByte++, pStringBuff); pStringBuff += 2;
        *pStringBuff++ = '-';
    }
    for (i = 0; i < 6; i++) {
        ByteToString(*pByte++, pStringBuff);
        pStringBuff += 2;
    }
    *pStringBuff = 0;

    return true;
}

bool LTUtilityByteOpsUUID_StringToUUID(const char * pUUIDString, u8 uuid[16]) {
    if (NULL == pUUIDString) return false;
    // "00112233-4455-4677-8899-aabbccddeeff"
    u8 * pByte = uuid;
    u32 i = 0;
    for (; i < 4; i++) {
        if (! StringToByte(pUUIDString, pByte)) return false;
        pUUIDString += 2; pByte++;
    }
    if (*pUUIDString != '-') return false;
    pUUIDString++;
    for (i = 0; i < 3; i++) {
        if (! StringToByte(pUUIDString, pByte)) return false;
        pUUIDString += 2; pByte++;
        if (! StringToByte(pUUIDString, pByte)) return false;
        pUUIDString += 2; pByte++;
        if (*pUUIDString != '-') return false;
        pUUIDString++;
    }
    for (i = 0; i < 6; i++) {
        if (! StringToByte(pUUIDString, pByte)) return false;
        pUUIDString += 2; pByte++;
    }
    if (*pUUIDString != 0) return false;
    return true;
}

bool LTUtilityByteOpsUUID_IsNullUUID(const u8 uuid[16]) {
    u32 nSize = 16;
    while (nSize--) if (uuid[nSize]) return false;
    return true;
}

void LTUtilityByteOpsUUID_NullifyUUID(u8 uuid[16]) {
    u32 nSize = 16;
    while (nSize--) uuid[nSize] = 0;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Nov-21   augustus    created
 */
