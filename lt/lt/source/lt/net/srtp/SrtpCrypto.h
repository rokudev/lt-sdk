/******************************************************************************
 * source/lt/net/srtp/SrtpCrypto.h - SRTP related crypto operations
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_SRTP_SRTPCRYPTO_H
#define ROKU_LT_SOURCE_LT_NET_SRTP_SRTPCRYPTO_H

#include <lt/LT.h>
#include <lt/net/dtls/LTNetDtls.h>

typedef enum {
    kCryptDirection_Encrypt,
    kCryptDirection_Decrypt,
} CryptDirection;

typedef struct {
    u8 * pEncryptionKey;
    u8 * pSaltingKey;
    u8 * pAuthKey;
} SrtpSessionKeyMaterial;

typedef struct {
    SrtpSessionKeyMaterial    rtpSend;
    SrtpSessionKeyMaterial    rtpReceive;
    SrtpSessionKeyMaterial    rtcpSend;
    SrtpSessionKeyMaterial    rtcpReceive;
} SrtpSessionKeys;

typedef struct {
    u8                     nCipherKeyLength;
    u8                     nCipherSaltLength;
    u32                    nMaximumLifetime;
    u8                     nAuthKeyLength;
    u8                     nAuthTagLength;
    u8                     nRtcpAuthTagLength;
} SrtpProtectionProfile;

typedef struct {
    SrtpProtectionProfile profile;
    SrtpSessionKeys       keys;
} SrtpAuthContext;

bool
SrtpCrypto_LibInit(void);

void
SrtpCrypto_LibFini(void);

SrtpAuthContext *
SrtpCrypto_CreateAuthContext(LTSocket hDtlsSocket);

void
SrtpCrypto_DestroyAuthContext(SrtpAuthContext *authCtx);

bool
SrtpCrypto_WriteAuthTag(SrtpProtectionProfile * pProfile, SrtpSessionKeyMaterial * pKeyMaterial, u8 * pAuthPortion, u32 nAuthPortionLen, u8 * pAuthTag, u32 nAuthTagLength);

bool
SrtpCrypto_VerifyAuthTag(SrtpProtectionProfile * pProfile, SrtpSessionKeyMaterial * pKeyMaterial, u8 * pAuthData, u32 nAuthDataLen, u8 * pRecvdMac, u32 nRecvdMacLen);

void
SrtpCrypto_CryptPayload(CryptDirection nCryptDir, SrtpProtectionProfile * pProfile, SrtpSessionKeyMaterial * pKeys, u64 nPacketIdx, u32 nSyncSource, u8 * pInputData, u32 nInputDataLen, u8 * pOutputData);

#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_SRTP_SRTPCRYPTO_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  12-Jun-22   trajan      created
 */
