/*******************************************************************************
 * source/lt/utility/LTUtilityByteOpsBase64.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include "LTUtilityByteOpsBase64.h"

/*_____________________________________
 / LTUtilityByteOpsBase64 static constants */
static const char s_cb64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";                   // Translation Table as described in RFC1113
static const char s_cd64[] = "|$$$}rstuvwxyz{$$$$$$$>?@ABCDEFGHIJKLMNOPQRSTUVW$$$$$$XYZ[\\]^_`abcdefghijklmnopq";  // Translation Table from base64.sourceforge.net/b64.c

/*_______________________________________________
 / LTUtilityByteOpsBase64 private helper functions */
static void Base64EncodeBlock(unsigned char * in, unsigned char * out, int len)
{
    unsigned char b[3] = {
        in[0],
        (len > 1) ? in[1] : 0,
        (len > 2) ? in[2] : 0,
    };
    out[0] = (unsigned char) s_cb64[ (int)(b[0] >> 2) ];
    out[1] = (unsigned char) s_cb64[ (int)(((b[0] & 0x03) << 4) | ((b[1] & 0xf0) >> 4)) ];
    out[2] = (unsigned char) (len > 1 ? s_cb64[ (int)(((b[1] & 0x0f) << 2) | ((b[2] & 0xc0) >> 6)) ] : '=');
    out[3] = (unsigned char) (len > 2 ? s_cb64[ (int)(b[2] & 0x3f) ] : '=');
}

static void Base64DecodeBlock(unsigned char * in, unsigned char * out, u32 inputChars)
{
    /* Writing out incomplete bytes is not permitted, so at least 2 input characters are needed to write any output
     * bytes */
    if (inputChars < 2) { return; }

    out[0] = (unsigned char) (in[0] << 2 | in[1] >> 4);
    if (inputChars >= 3) out[1] = (unsigned char) (in[1] << 4 | in[2] >> 2);
    if (inputChars == 4) out[2] = (unsigned char) (((in[2] << 6) & 0xc0) | in[3]);
}

/*_______________________________________________________________________
 / LTUtilityByteOpsBase64  LTUtilityByteOps public interface functions */
u32 LTUtilityByteOpsBase64_Base64Encode(const u8 * pBytesToEncode, u32 nNumBytesToEncode, char * pBase64StringBufferToFill, u32 nBase64StringBufferSize) {
    if ((NULL == pBytesToEncode) || (0 == nNumBytesToEncode) || (NULL == pBase64StringBufferToFill)) return 0;
    u32 nBufferSizeRequired = LTUtilityByteOpsBase64_GetBase64EncodeBufferRequirement(nNumBytesToEncode);
    if (nBase64StringBufferSize < nBufferSizeRequired) return 0;

    while (nNumBytesToEncode) {
        u32 len = (nNumBytesToEncode > 2) ? 3 : nNumBytesToEncode;
        Base64EncodeBlock((unsigned char *)pBytesToEncode, (unsigned char *)pBase64StringBufferToFill, (int)len);
        pBytesToEncode += len;
        pBase64StringBufferToFill += 4;
        nNumBytesToEncode -= len;
    }
    *pBase64StringBufferToFill = 0;
    return (nBufferSizeRequired-1);
}

u32 LTUtilityByteOpsBase64_Base64Decode(const char * pBase64EncodedString, u32 nBase64EncodedStringStrLen, u8 * pDecodedDataBufferToFill, u32 nDecodedDataBufferSize) {
    if ((NULL == pBase64EncodedString) || (0 == nBase64EncodedStringStrLen) || (NULL == pDecodedDataBufferToFill)) return 0;
    u32 nDecodedLength = LTUtilityByteOpsBase64_GetBase64DecodeBufferRequirement(nBase64EncodedStringStrLen);

    // decrease the required decode buffer length to account for base64 end padding, if any
    if ('=' == pBase64EncodedString[nBase64EncodedStringStrLen-1]) {
        nDecodedLength--;
        if ((nBase64EncodedStringStrLen > 1) && ('=' == pBase64EncodedString[nBase64EncodedStringStrLen-2])) nDecodedLength--;
    }
    if (nDecodedDataBufferSize < nDecodedLength) return 0;

    signed char * pString = (signed char *)pBase64EncodedString;
    unsigned char in[5];
    int v = 0;
    u32 i;

    nDecodedLength = 0;
    *in = 0;

    while (nBase64EncodedStringStrLen)
    {
        for (i = 0; i < 4 && nBase64EncodedStringStrLen;)
        {
            v = (int)*pString++; nBase64EncodedStringStrLen--;
            v = ((v < 43 || v > 122) ? 0 : (int) s_cd64[ v - 43 ]);
            if (v != 0) v = ((v == (int)'$') ? 0 : v - 61);
            if (v != 0) in[i++] = (unsigned char)(v-1);
        }
        in[i] = 0;
        if (i > 1)
        {
            /* Write to the buffer only as many complete valid bytes as can be read from this block of input chars */
            Base64DecodeBlock(in, (unsigned char *)pDecodedDataBufferToFill, i);
            i--; // Number of output bytes will always be one less than number of input base 64 chars
            nDecodedLength += i;
            pDecodedDataBufferToFill += i;
        }
    }
    return nDecodedLength;
}

u32 LTUtilityByteOpsBase64_GetBase64EncodeBufferRequirement(u32 nNumBytesToEncode) {
    return (((nNumBytesToEncode + 2) / 3) * 4) + 1;
}

u32 LTUtilityByteOpsBase64_GetBase64DecodeBufferRequirement(u32 nBase64EncodedStringStrLen) {
    return ((nBase64EncodedStringStrLen + 3) / 4) * 3;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Nov-21   augustus    created
 */
