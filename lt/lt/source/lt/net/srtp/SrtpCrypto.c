/*******************************************************************************
 * source/lt/net/srtp/SrtpCrypto.c - SRTP related crypto operations
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include "SrtpCrypto.h"

DEFINE_LTLOG_SECTION("srtp.crypt");

typedef enum {
    kKeyDerivationLabel_SRTP_Encryption      = 0x00,
    kKeyDerivationLabel_SRTP_Authentication  = 0x01,
    kKeyDerivationLabel_SRTP_Salting         = 0x02,
    kKeyDerivationLabel_SRTCP_Encryption     = 0x03,
    kKeyDerivationLabel_SRTCP_Authentication = 0x04,
    kKeyDerivationLabel_SRTCP_Salting        = 0x05,
} KeyDerivationLabel;

static LTSystemCrypto * s_pCrypto = NULL;

static u8 *
DeriveSessionKey(SrtpProtectionProfile * pProfile, const u8 * pMasterKey, const u8 * pMasterSalt, u8 nLabel, u32 nSessionKeyLen) {
    if (!pMasterKey || !pMasterSalt) return NULL;

    // Session key derivation per RFC3711 section 4.3
    u8 IV[pProfile->nCipherKeyLength];
    lt_memset(IV, 0, pProfile->nCipherKeyLength);
    lt_memcpy(IV, pMasterSalt, pProfile->nCipherSaltLength);
    IV[7] ^= nLabel;

    u8 * pSessionKey = lt_malloc(nSessionKeyLen);
    if (!pSessionKey) return NULL;
    lt_memset(pSessionKey, 0, nSessionKeyLen);

    LTSystemCryptoResult result = s_pCrypto->EncryptAES128CTR(pMasterKey, IV, pSessionKey, nSessionKeyLen, pSessionKey);
    if (result != kLTSystemCrypto_Result_Ok) {
        lt_free(pSessionKey);
        pSessionKey = NULL;
    }

    return pSessionKey;
}

static bool
DeriveSessionKeys(SrtpProtectionProfile * pProfile, SrtpSessionKeys * pSessionKeys, LTSocket hDtlsSocket) {
    /* Generate session encryption, salting, and authentication keys from the master
     * keys - currently always assume we are the server. (i.e. send keys are derived
     * from server master key/salt, receive keys from client master key/salt).
     * The key material is laid out in memory as:
     *     - client write master key
     *     - server write master key
     *     - client write master salt
     *     - server write master salt
     */

    ILTSocket * pDtlsSocket = lt_gethandleinterface(ILTSocket, hDtlsSocket);
    if (!pDtlsSocket) return false;

    u32 nMasterKeyMaterialSize = 2 * (pProfile->nCipherKeyLength + pProfile->nCipherSaltLength);
    u8 *pMasterKeyMaterial = lt_malloc(nMasterKeyMaterialSize);
    if (!pMasterKeyMaterial) return false;

    pDtlsSocket->GetProperty(hDtlsSocket, "srtp.keymaterial", pMasterKeyMaterial);
    u8 *pClientWriteMasterKey  = pMasterKeyMaterial;
    u8 *pServerWriteMasterKey  = pClientWriteMasterKey + pProfile->nCipherKeyLength;
    u8 *pClientWriteMasterSalt = pServerWriteMasterKey + pProfile->nCipherKeyLength;
    u8 *pServerWriteMasterSalt = pClientWriteMasterSalt + pProfile->nCipherSaltLength;

    bool bSuccess = false;
    do {
        /* Derive RTP send/receive keys */
        if (!(pSessionKeys->rtpReceive.pAuthKey        = DeriveSessionKey(pProfile, pServerWriteMasterKey, pServerWriteMasterSalt, kKeyDerivationLabel_SRTP_Authentication, pProfile->nAuthKeyLength))) break;
        if (!(pSessionKeys->rtpReceive.pEncryptionKey  = DeriveSessionKey(pProfile, pServerWriteMasterKey, pServerWriteMasterSalt, kKeyDerivationLabel_SRTP_Encryption,     pProfile->nCipherKeyLength))) break;
        if (!(pSessionKeys->rtpReceive.pSaltingKey     = DeriveSessionKey(pProfile, pServerWriteMasterKey, pServerWriteMasterSalt, kKeyDerivationLabel_SRTP_Salting,        pProfile->nCipherSaltLength))) break;
        if (!(pSessionKeys->rtpSend.pAuthKey           = DeriveSessionKey(pProfile, pClientWriteMasterKey, pClientWriteMasterSalt, kKeyDerivationLabel_SRTP_Authentication, pProfile->nAuthKeyLength))) break;
        if (!(pSessionKeys->rtpSend.pEncryptionKey     = DeriveSessionKey(pProfile, pClientWriteMasterKey, pClientWriteMasterSalt, kKeyDerivationLabel_SRTP_Encryption,     pProfile->nCipherKeyLength))) break;
        if (!(pSessionKeys->rtpSend.pSaltingKey        = DeriveSessionKey(pProfile, pClientWriteMasterKey, pClientWriteMasterSalt, kKeyDerivationLabel_SRTP_Salting,        pProfile->nCipherSaltLength))) break;

        /* Derive RTCP send/receive keys */
        if (!(pSessionKeys->rtcpReceive.pAuthKey       = DeriveSessionKey(pProfile, pServerWriteMasterKey, pServerWriteMasterSalt, kKeyDerivationLabel_SRTCP_Authentication, pProfile->nAuthKeyLength))) break;
        if (!(pSessionKeys->rtcpReceive.pEncryptionKey = DeriveSessionKey(pProfile, pServerWriteMasterKey, pServerWriteMasterSalt, kKeyDerivationLabel_SRTCP_Encryption,     pProfile->nCipherKeyLength))) break;
        if (!(pSessionKeys->rtcpReceive.pSaltingKey    = DeriveSessionKey(pProfile, pServerWriteMasterKey, pServerWriteMasterSalt, kKeyDerivationLabel_SRTCP_Salting,        pProfile->nCipherSaltLength))) break;
        if (!(pSessionKeys->rtcpSend.pAuthKey          = DeriveSessionKey(pProfile, pClientWriteMasterKey, pClientWriteMasterSalt, kKeyDerivationLabel_SRTCP_Authentication, pProfile->nAuthKeyLength))) break;
        if (!(pSessionKeys->rtcpSend.pEncryptionKey    = DeriveSessionKey(pProfile, pClientWriteMasterKey, pClientWriteMasterSalt, kKeyDerivationLabel_SRTCP_Encryption,     pProfile->nCipherKeyLength))) break;
        if (!(pSessionKeys->rtcpSend.pSaltingKey       = DeriveSessionKey(pProfile, pClientWriteMasterKey, pClientWriteMasterSalt, kKeyDerivationLabel_SRTCP_Salting,        pProfile->nCipherSaltLength))) break;
        bSuccess = true;
    } while(false);

    /* Clear out the master key buffer before freeing it */
    lt_memset(pMasterKeyMaterial, 0, nMasterKeyMaterialSize);
    lt_free(pMasterKeyMaterial);

    return bSuccess;
}

static bool
InitProtectionProfile(SrtpProtectionProfile *profile, LTSocket hDtlsSocket) {
    LTDtlsSrtpProtectionProfileId profileId = 0;
    ILTSocket *pDtlsSocket = lt_gethandleinterface(ILTSocket, hDtlsSocket);
    if (!pDtlsSocket->GetProperty(hDtlsSocket, "srtp.protectionprofile", &profileId)) {
        LTLOG_YELLOWALERT("fail.dspp", "Failed to get DTLS/SRTP protection profile");
        return false;
    }

    lt_memset(profile, 0, sizeof(SrtpProtectionProfile));

    switch (profileId) {
        case kLTDtlsSrtpProtectionProfile_AES128_CM_SHA1_80:
            profile->nCipherKeyLength   = 16;
            profile->nCipherSaltLength  = 14;
            profile->nMaximumLifetime   = 1 << 31;
            profile->nAuthKeyLength     = 20;
            profile->nAuthTagLength     = 10;
            profile->nRtcpAuthTagLength = 10;
            break;

        case kLTDtlsSrtpProtectionProfile_AES128_CM_SHA1_32:
            profile->nCipherKeyLength   = 16;
            profile->nCipherSaltLength  = 14;
            profile->nMaximumLifetime   = 1 << 31;
            profile->nAuthKeyLength     = 20;
            profile->nAuthTagLength     = 4;
            profile->nRtcpAuthTagLength = 10;
            break;

        default:
            LTLOG_YELLOWALERT("bad.dspp", "Unsupported DTLS/SRTP protection profile: %d", profileId);
            return false;
    }
    return true;
}

SrtpAuthContext *
SrtpCrypto_CreateAuthContext(LTSocket hDtlsSocket) {
    SrtpAuthContext *authCtx = lt_malloc(sizeof(SrtpAuthContext));
    if (!authCtx) return NULL;
    if (!InitProtectionProfile(&authCtx->profile, hDtlsSocket) ||
        !DeriveSessionKeys(&authCtx->profile, &authCtx->keys, hDtlsSocket)) {
        SrtpCrypto_DestroyAuthContext(authCtx);
        return NULL;
    }
    return authCtx;
}

static void
FreeSessionKeyMaterial(SrtpProtectionProfile *profile, SrtpSessionKeyMaterial *keyMaterial) {
    if(keyMaterial->pAuthKey) {
        lt_memset(keyMaterial->pAuthKey, 0, profile->nAuthKeyLength);
        lt_free(keyMaterial->pAuthKey);
    }
    if(keyMaterial->pEncryptionKey) {
        lt_memset(keyMaterial->pEncryptionKey, 0, profile->nCipherKeyLength);
        lt_free(keyMaterial->pEncryptionKey);
    }
    if(keyMaterial->pSaltingKey) {
        lt_memset(keyMaterial->pSaltingKey, 0, profile->nCipherSaltLength);
        lt_free(keyMaterial->pSaltingKey);
    }
}

void
SrtpCrypto_DestroyAuthContext(SrtpAuthContext *authCtx) {
    if (!authCtx) return;
    FreeSessionKeyMaterial(&authCtx->profile, &authCtx->keys.rtpSend);
    FreeSessionKeyMaterial(&authCtx->profile, &authCtx->keys.rtpReceive);
    FreeSessionKeyMaterial(&authCtx->profile, &authCtx->keys.rtcpSend);
    FreeSessionKeyMaterial(&authCtx->profile, &authCtx->keys.rtcpReceive);
    lt_free(authCtx);
}

void
SrtpCrypto_CryptPayload(CryptDirection nCryptDir, SrtpProtectionProfile * pProfile, SrtpSessionKeyMaterial * pKeys, u64 nPacketIdx, u32 nSyncSource, u8 * pInputData, u32 nInputDataLen, u8 * pOutputData) {
    // IV = (k_s * 2^16) XOR (SSRC * 2^64) XOR (i * 2^16)
    u8 IV[pProfile->nCipherKeyLength];
    lt_memset(IV, 0, pProfile->nCipherKeyLength);
    lt_memcpy(IV, pKeys->pSaltingKey, pProfile->nCipherSaltLength);
    IV[4]  ^= (nSyncSource >> 24) & 0xFF;
    IV[5]  ^= (nSyncSource >> 16) & 0xFF;
    IV[6]  ^= (nSyncSource >> 8)  & 0xFF;
    IV[7]  ^= (nSyncSource >> 0)  & 0xFF;
    IV[8]  ^= (nPacketIdx  >> 40) & 0xFF;
    IV[9]  ^= (nPacketIdx  >> 32) & 0xFF;
    IV[10] ^= (nPacketIdx  >> 24) & 0xFF;
    IV[11] ^= (nPacketIdx  >> 16) & 0xFF;
    IV[12] ^= (nPacketIdx  >> 8)  & 0xFF;
    IV[13] ^= (nPacketIdx  >> 0)  & 0xFF;

    LTSystemCryptoResult result;
    if (nCryptDir == kCryptDirection_Encrypt) {
        result = s_pCrypto->EncryptAES128CTR(pKeys->pEncryptionKey, IV, pInputData, nInputDataLen, pOutputData);
    } else {
        result = s_pCrypto->DecryptAES128CTR(pKeys->pEncryptionKey, IV, pInputData, nInputDataLen, pOutputData);
    }
    if (result != kLTSystemCrypto_Result_Ok) {
        LTLOG_YELLOWALERT("crypt.fail", "Error crypting SRTP payload");
    }
}

bool
SrtpCrypto_WriteAuthTag(SrtpProtectionProfile * pProfile, SrtpSessionKeyMaterial * pKeyMaterial, u8 * pAuthPortion, u32 nAuthPortionLen, u8 * pAuthTag, u32 nAuthTagLength) {
    if (!pAuthPortion || !pAuthTag) return false;

    u8 macBuf[pProfile->nAuthKeyLength];
    if (s_pCrypto->GenHMACSHA1(pKeyMaterial->pAuthKey, pProfile->nAuthKeyLength, pAuthPortion, nAuthPortionLen, macBuf) != kLTSystemCrypto_Result_Ok) {
        LTLOG_DEBUG("mac.init", "Error in HMAC-SHA1");
        return false;
    }

    // The auth tag might be shorter than the resulting MAC. We just copy the first
    // part of the MAC into the auth tag.
    lt_memcpy(pAuthTag, macBuf, nAuthTagLength);
    return true;
}

bool
SrtpCrypto_VerifyAuthTag(SrtpProtectionProfile * pProfile, SrtpSessionKeyMaterial * pKeyMaterial, u8 * pAuthData, u32 nAuthDataLen, u8 * pRecvdMac, u32 nRecvdMacLen) {
    u8 calcAuthTag[nRecvdMacLen];
    if (SrtpCrypto_WriteAuthTag(pProfile, pKeyMaterial, pAuthData, nAuthDataLen, calcAuthTag, sizeof(calcAuthTag))) {
        return lt_memcmp(pRecvdMac, calcAuthTag, nRecvdMacLen) == 0;
    }
    return false;
}

bool
SrtpCrypto_LibInit(void) {
    s_pCrypto = lt_openlibrary(LTSystemCrypto);
    return s_pCrypto != NULL;
}

void
SrtpCrypto_LibFini(void) {
    lt_closelibrary(s_pCrypto);
    s_pCrypto = NULL;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  12-Jun-22   trajan      created
 */
