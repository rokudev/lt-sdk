/*******************************************************************************
 * platforms/linux/source/linux/driver/identity/LinuxDriverIdentity.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/flash/LTDeviceFlash.h>
#include <lt/driver/identity/LTDriverIdentity.h>

// DEFINE_LTLOG_SECTION("linux.identity")

static u8 s_aes_key1[] = { 0xa7, 0x63, 0x55, 0x79, 0x00, 0x00, 0xe5, 0xda, 0xc9, 0x97, 0xf5, 0xee, 0x36, 0xd5, 0x17, 0xdc };
static u8 s_aes_key2[] = { 0x1a, 0x0a, 0x60, 0x1e, 0x0a, 0x9b, 0xfa, 0xf7, 0xd8, 0x6c, 0x44, 0xff, 0x11, 0x19, 0x10, 0x99 };
static u8 s_dummyMac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static u8 s_uds[kLTIdentityUdsBytes];

static const char *LinuxDriverIdentity_GetDeviceId(void) {
    return "S0X70319R87T";
}

static const char *LinuxDriverIdentity_GetSerialNumber(void) {
    return "X02400A9R87T";
}

static bool LinuxDriverIdentity_GetAESKey(u8 aesKey[kLTIdentityDeviceAESKeyBytes], u8 nKeyIndex) {
    u8 *key;
    if (nKeyIndex == 1) {
        key = s_aes_key1;
    } else if (nKeyIndex == 2) {
        key = s_aes_key2;
    } else {
        return false;
    }
    lt_memcpy(aesKey, key, kLTIdentityDeviceAESKeyBytes);
    return true;
}

void LinuxDriverIdentity_GetMac(u8 mac[6]){
    lt_memcpy(mac, s_dummyMac, 6);
}

static u32 LinuxDriverIdentity_GetBoardRevision(void) { return LT_U32_MAX; }

static bool LinuxDriverIdentity_GetUds(u8 uds[kLTIdentityUdsBytes]) {
    lt_memcpy(uds, s_uds, kLTIdentityUdsBytes);
    return true;
}

static bool LinuxDriverIdentity_SetUds(const u8 uds[kLTIdentityUdsBytes]) {
    lt_memcpy(s_uds, uds, kLTIdentityUdsBytes);
    return true;
}

static bool LinuxDriverIdentity_IsValid(void) {
    // LTDeviceIdenticy.c says always return true if this function isn't implemented, so returning true
    return true;
}

static LTSecurityStatus LinuxDriverIdentity_IsUnsecured(void) {
    // FIXME LT-1880 implement this properly
    // LTDeviceIdentity.c says return kLTSecurityStatus_Fail if this function isn't implemented.
    // We deviate from this here because if we don't then LTSystemShell refuses to run
    return kLTSecurityStatus_Success;
}

static u32 LinuxDriverIdentity_GetLTATSize(void) {
    return 0;
}

static LTSecurityStatus LinuxDriverIdentity_AuthenticateLTAT(LTSecurityLTATPayload * pLTAT, u32 * pClaimMask) {
    LT_UNUSED(pLTAT); LT_UNUSED(pClaimMask);
    return kLTSecurityStatus_Fail;
}

static bool LTDriverIdentityImpl_LibInit(void) {
    return true;
}

static void LTDriverIdentityImpl_LibFini(void) {
    return;
}

define_LTLIBRARY_ROOT_INTERFACE(LTDriverIdentity)
    .GetDeviceId         = LinuxDriverIdentity_GetDeviceId,
    .GetSerialNumber     = LinuxDriverIdentity_GetSerialNumber,
    .GetAESKey           = LinuxDriverIdentity_GetAESKey,
    .GetMac              = LinuxDriverIdentity_GetMac,
    .GetBoardRevision    = LinuxDriverIdentity_GetBoardRevision,
    .SetUds              = LinuxDriverIdentity_SetUds,
    .GetUds              = LinuxDriverIdentity_GetUds,
    .IsValid             = LinuxDriverIdentity_IsValid,
    .IsUnsecured         = LinuxDriverIdentity_IsUnsecured,
    .GetLTATSize         = LinuxDriverIdentity_GetLTATSize,
    .AuthenticateLTAT    = LinuxDriverIdentity_AuthenticateLTAT,
LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  13-Apr-23   trajan      created
 *  13-Apr-23   constantine conform to the LTDriverIdentity singleton pattern
 */
