/*******************************************************************************
 * <lt/device/authentication/LTDeviceAuthenticationImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Device Library for device authentication functionality.
 *
 * This library provides authentication services including key calculation,
 * encryption/decryption operations, efuse validation, and authentication
 * protocol implementation.
 *
 ******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/device/authentication/LTDeviceAuthentication.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/device/efuse/LTDeviceEfuse.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("dev.auth");

/*******************************************************************************
 * Access to the LTDriverAuthentication library, through which the Device accesses
 * authentication related functions:                                           */
static LTDriverLibrary *s_pDriver = NULL;

/*******************************************************************************
 * Access to the driver's ILTDriverAuthenticationDeviceUnit interface, through
 * which the device can access the driver-specific authentication related functions:     */
static ILTDriverAuthenticationDeviceUnit *s_iAuth = NULL;

/*******************************************************************************
 * Number of Device Units available according to the Driver:                  */
static u32 s_nNumDeviceUnits = 0;

/*******************************************************************************
 * Legacy support - maintain separate instances for crypto, efuse and entropy */
static LTDriverCryptoAes128Cbc *s_pLibAesCbc = NULL;
static LTDriverCryptoEntropy *s_pLibEntropy = NULL;
static LTDeviceEfuse *s_pLibEfuse = NULL;
static ILTDriverEfuseDeviceUnit *s_iEfuse = NULL;

/*******************************************************************************
 * Some needed forward declarations:                                          */
static bool LTDeviceAuthentication_IsSecureDevice(void);

/*******************************************************************************
 * The implementations of our functionality:                                  */
static bool AES_Encode_ECB(const u8 *keyPtr, const u8 *input, u8 *output, LT_SIZE dataLen) {

    // Try driver interface first
    if (s_iAuth && s_iAuth->AES_Encode_ECB) {
        return s_iAuth->AES_Encode_ECB(keyPtr, input, output, dataLen);
    }

    // Fallback to direct crypto driver
    if (!s_pLibAesCbc) return false;

    // IV == {0} on a CBC operation is equivalent to an ECB operation
    const u8 iv[AES128_CBC_IV_LENGTH] = {0};

    if (s_pLibAesCbc->API->Encrypt(keyPtr, iv, input, dataLen, output) != kLTSystemCrypto_Result_Ok)
        return false;

    return true;
}

static bool AES_Decode_ECB(const u8 *keyPtr, const u8 *input, u8 *output, LT_SIZE dataLen) {

    // Try driver interface first
    if (s_iAuth && s_iAuth->AES_Decode_ECB) {
        return s_iAuth->AES_Decode_ECB(keyPtr, input, output, dataLen);
    }

    // Fallback to direct crypto driver
    if (!s_pLibAesCbc) return false;

    // IV == {0} on a CBC operation is equivalent to an ECB operation
    const u8 iv[AES128_CBC_IV_LENGTH] = {0};

    if (s_pLibAesCbc->API->Decrypt(keyPtr, iv, input, dataLen, output) != kLTSystemCrypto_Result_Ok)
        return false;

    return true;
}

static bool LTDeviceAuthentication_CalculateAuthKey(u8 authKey[kLTAuthenticationKeyBytes]) {
    if (!authKey) return false;

    u8 out1[kLTAuthenticationKeyBytes] = {0};
    u8 out2[kLTAuthenticationKeyBytes] = {0};
    LT_SIZE dataLen = kLTAuthenticationKeyBytes;

    // These are the Auth Key generation seeds
    static const u8 Seed1[] = {0xd7, 0x50, 0x24, 0x8f, 0x83, 0xc7, 0x1f, 0x0d,
                               0x76, 0x57, 0x5d, 0x91, 0xa2, 0xf3, 0x09, 0xa5};
    static const u8 Seed2[] = {0xb7, 0x7d, 0xf6, 0x45, 0xd0, 0xa9, 0x17, 0x2a,
                               0x5b, 0x32, 0xb4, 0x18, 0x6f, 0x4e, 0xd8, 0x73};
    static const u8 Seed3[] = {0xf5, 0xd4, 0x77, 0x93, 0xc5, 0x0a, 0xcb, 0x6b,
                               0x10, 0x04, 0x6f, 0xbf, 0x25, 0xe4, 0x8e, 0x8b};

    // Use NULL as key to indicate AES_KEY_1 from efuse (like original RCUCrypto)
    if (AES_Decode_ECB(NULL /* null == AES_KEY_1 via efuse */, Seed1, out1, dataLen)) {
        if (AES_Decode_ECB(out1, Seed2, out2, dataLen)) {
           if (AES_Decode_ECB(out2, Seed3, authKey, dataLen)) {
                return true;
            }
        }
    }

    return false;
}

static bool LTDeviceAuthentication_AuthEncode(u8 const in[kLTAuthenticationKeyBytes], u8 out[kLTAuthenticationKeyBytes]) {
    bool ret = false;
    u8 authKey[kLTAuthenticationKeyBytes] = {0};

    // Generate the auth key
    if (LTDeviceAuthentication_CalculateAuthKey(authKey)) {
        // Encode in to out with the authKey
        if (AES_Encode_ECB(authKey, in, out, kLTAuthenticationKeyBytes)) {
            ret = true;
        }
    }

    // Erase the authKey so it doesn't remain on the stack
    lt_memset(authKey, 0, kLTAuthenticationKeyBytes);

    return ret;
}

static bool LTDeviceAuthentication_AuthDecode(u8 const in[kLTAuthenticationKeyBytes], u8 out[kLTAuthenticationKeyBytes]) {
    bool ret = false;
    u8 authKey[kLTAuthenticationKeyBytes] = {0};

    // Generate the auth key
    if (LTDeviceAuthentication_CalculateAuthKey(authKey)) {
        // Decode in to out with the authKey
        if (AES_Decode_ECB(authKey, in, out, kLTAuthenticationKeyBytes)) {
            ret = true;
        }
    }

    // Erase the authKey so it doesn't remain on the stack
    lt_memset(authKey, 0, kLTAuthenticationKeyBytes);

    return ret;
}

static void LTDeviceAuthentication_GetRandom16(u8 data[kLTAuthenticationKeyBytes]) {
    if (!data || !s_pLibEntropy) return;

    u8 buf[32] = {0};
    if (s_pLibEntropy->API->GetEntropy(buf, sizeof(buf))) {
        lt_memcpy(data, buf, kLTAuthenticationKeyBytes);
    }
}

static void DumpBlock(LTSystemSchell *shell, u8 block[kLTAuthenticationKeyBytes], char const *msg) {
    if (!shell || !shell->API || !block || !msg) return;

    shell->API->Print(shell, "%s: ", msg);
    for (int i = 0; i < kLTAuthenticationKeyBytes; ++i ) {
        shell->API->Print(shell, "%02x ", block[i]);
    }
    shell->API->Print(shell, "\n");
}

static void DumpBlock4(LTSystemSchell *shell, u8 block[4], char const *msg) {
    if (!shell || !shell->API || !block || !msg) return;

    shell->API->Print(shell, "%s: ", msg);
    for (int i = 0; i < 4; ++i ) {
        shell->API->Print(shell, "%02x ", block[i]);
    }
    shell->API->Print(shell, "\n");
}

static u32 GetChipIDForKeyCheck(void) {
    // Try driver interface first
    if (s_iAuth && s_iAuth->GetChipIDForKeyCheck) {
        return s_iAuth->GetChipIDForKeyCheck();
    }

    // Fallback to efuse-based implementation
    u32 id = 0xffffffff;

    // Only secure devices have valid chip ids
    if (LTDeviceAuthentication_IsSecureDevice() && s_iEfuse) {
        // Try to get chip_id_for_key_check first
        s16 cidIdx = s_iEfuse->GetEfuseFieldIndexFromName("chip_id_for_key_check");
        if (cidIdx >= 0) {
            s_iEfuse->GetEfuseFieldData(cidIdx, &id);
        } else {
            // Fall back to regular chip_id
            cidIdx = s_iEfuse->GetEfuseFieldIndexFromName("chip_id");
            if (cidIdx >= 0) {
                s_iEfuse->GetEfuseFieldData(cidIdx, &id);
            }
        }
    }

    return id;
}

static bool LTDeviceAuthentication_ValidateKeyCheck(LTSystemSchell *shell) {
    // Try driver interface first
    if (s_iAuth && s_iAuth->ValidateKeyCheck) {
        return s_iAuth->ValidateKeyCheck(shell);
    }

    // Fallback to efuse-based implementation
    if (!shell || !s_iEfuse) return false;

    bool ret = false;

    // Run the validation algorithm:
    // The keyCheck is calculated using the following algorithm
    // aesEnc(paddedChipID, AES_KEY1) == key_check efuse field

    u8 paddedChipID[kLTAuthenticationKeyBytes] = {0};
    u8 enc[kLTAuthenticationKeyBytes] = {0};
    u8 keyCheck[4] = {0};

    // Get the chipID
    u32 chipID = GetChipIDForKeyCheck();

    // Get the key_check value from efuse
    s16 keyCheckIdx = s_iEfuse->GetEfuseFieldIndexFromName("key_check");
    if (keyCheckIdx >= 0) {
        s_iEfuse->GetEfuseFieldData(keyCheckIdx, keyCheck);
    }

    // Add the chip ID to the proper location in the padded array
    lt_memcpy(&paddedChipID[sizeof(paddedChipID) - sizeof(chipID)], &chipID, sizeof(chipID));
    DumpBlock(shell, paddedChipID, "pad");

    // Encrypt using AES_KEY_1 from efuse (NULL key)
    if (AES_Encode_ECB(NULL /* null == AES_KEY_1 via efuse */, paddedChipID, enc, kLTAuthenticationKeyBytes)) {
        DumpBlock(shell, enc, "enc");

        // Dump out the fields that need to match
        DumpBlock4(shell, &enc[sizeof(enc) - sizeof(keyCheck)], "chk");
        DumpBlock4(shell, keyCheck, "kyc");

        // Check for a match
        ret = (lt_memcmp(&enc[sizeof(enc) - sizeof(keyCheck)], keyCheck, sizeof(keyCheck)) == 0);
    }

    return ret;
}

static bool LTDeviceAuthentication_RunAuthentication(LTSystemSchell *shell) {
    if (!shell) return false;

    bool ret = false;
    u8 authKey[kLTAuthenticationKeyBytes] = {0};
    u8 r1[kLTAuthenticationKeyBytes] = {0};
    u8 cr1[kLTAuthenticationKeyBytes] = {0};
    u8 pr1[kLTAuthenticationKeyBytes] = {0};
    u8 r2[kLTAuthenticationKeyBytes] = {0};
    u8 cr2[kLTAuthenticationKeyBytes] = {0};
    u8 pr2[kLTAuthenticationKeyBytes] = {0};

    // Common: Generate the auth key
    if (!LTDeviceAuthentication_CalculateAuthKey(authKey)) {
        return false;
    }

    // Host side:
    // Get random r1
    LTDeviceAuthentication_GetRandom16(r1);
    DumpBlock(shell, r1, "  r1");

    // Encrypt r1 with auth key => cr1
    AES_Encode_ECB(authKey, r1, cr1, kLTAuthenticationKeyBytes);
    DumpBlock(shell, cr1, " cr1");

    // Remote side:
    // Get random r2
    LTDeviceAuthentication_GetRandom16(r2);
    DumpBlock(shell, r2, "  r2");

    // Encrypt r2 with auth key => cr2
    AES_Encode_ECB(authKey, r2, cr2, kLTAuthenticationKeyBytes);
    DumpBlock(shell, cr2, " cr2");

    // Decrypt cr1 into pr1
    AES_Decode_ECB(authKey, cr1, pr1, kLTAuthenticationKeyBytes);
    DumpBlock(shell, pr1, " pr1");

    // Host side:
    // Check that r1 == pr1
    if (lt_memcmp(r1, pr1, kLTAuthenticationKeyBytes) == 0) {
        // Decrypt cr2 into pr2
        AES_Decode_ECB(authKey, cr2, pr2, kLTAuthenticationKeyBytes);
        DumpBlock(shell, pr2, " pr2");

        // Remote side:
        // Check that r2 == pr2
        if (lt_memcmp(r2, pr2, kLTAuthenticationKeyBytes) == 0) {
            ret = true;
        }
    }

    return ret;
}

static bool LTDeviceAuthentication_IsSecureDevice(void) {
    // Try driver interface first
    if (s_iAuth && s_iAuth->IsSecureDevice) {
        return s_iAuth->IsSecureDevice();
    }

    // Fallback to efuse-based implementation
    if (!s_iEfuse) return false;

    bool ret = false;

    // Read the efuse data to see if the FW is encrypted
    s16 idx = s_iEfuse->GetEfuseFieldIndexFromName("fw_encrypted");
    if (idx >= 0) {
        u8 val = 0;
        if (s_iEfuse->GetEfuseFieldData(idx, &val)) {
            if (val) ret = true;
        }
    }

    return ret;
}

static bool LTDeviceAuthentication_IsUsingDevKeys(void) {
    // Try driver interface first
    if (s_iAuth && s_iAuth->IsUsingDevKeys) {
        return s_iAuth->IsUsingDevKeys();
    }

    // Default to false (not using DEV keys)
    return false;
}

static u32 LTDeviceAuthentication_GetChipID(void) {
    // Try driver interface first
    if (s_iAuth && s_iAuth->GetChipID) {
        return s_iAuth->GetChipID();
    }

    // Fallback to efuse-based implementation
    u32 id = 0xffffffff;

    // Only secure devices have valid chip ids
    if (LTDeviceAuthentication_IsSecureDevice() && s_iEfuse) {
        // Read the actual efuse data for the chip id
        s16 cidIdx = s_iEfuse->GetEfuseFieldIndexFromName("chip_id");
        if (cidIdx >= 0) {
            s_iEfuse->GetEfuseFieldData(cidIdx, &id);
        }
    }

    return id;
}

/*******************************************************************************
 * Unload the Driver library:                                                 */
static void ShutDownDriver(void) {
    if (s_pDriver) {
        LT_GetCore()->CloseLibrary((LTLibrary *)s_pDriver);
        s_pDriver = NULL;
    }
}

static u32 LTDeviceAuthenticationImpl_GetNumDeviceUnits(void) {
    return s_nNumDeviceUnits;
}

static LTDeviceUnit LTDeviceAuthenticationImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LTDeviceUnit hDeviceUnit = 0;
    if (s_pDriver && nDeviceUnitNumber < s_nNumDeviceUnits)
        hDeviceUnit = s_pDriver->CreateDeviceUnitHandle(nDeviceUnitNumber);
    return hDeviceUnit;
}

/*******************************************************************************
 * Library startup and shutdown:                                              */
static void LTDeviceAuthenticationImpl_LibFini(void) {
    if (s_pLibAesCbc) lt_closelibrary(s_pLibAesCbc);
    if (s_pLibEntropy) lt_closelibrary(s_pLibEntropy);
    if (s_pLibEfuse) lt_closelibrary(s_pLibEfuse);
    ShutDownDriver();
    s_pLibAesCbc = NULL;
    s_pLibEntropy = NULL;
    s_iEfuse = NULL;
    s_pLibEfuse = NULL;
    s_iAuth = NULL;
    s_nNumDeviceUnits = 0;
}

/*******************************************************************************
 * Library startup and shutdown.
 * Attempt to open the Driver Library.  If successful, get the number of
 * available Device Units from the Driver.
 * Note that a missing driver is not fatal, since the default implementation
 * will be used instead                                                       */
static bool LTDeviceAuthenticationImpl_LibInit(void) {
    LTLOG("init.begin", NULL);

    // Attempt to open the authentication driver
    if ((s_pDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceAuthentication", 0))) {

        if (!(s_nNumDeviceUnits = s_pDriver->GetNumDeviceUnits())) {
            LTLOG("init.fail.no_units", "Driver returned zero device units");
            ShutDownDriver();
            return false;
        }

        // Test for and confirm there is at least one device unit
        if (s_nNumDeviceUnits < 1) {
            LTLOG("init.fail.no_valid_units", "Driver has no valid device units");
            ShutDownDriver();
            return false; }

        // Get the authentication device unit interface
        s_iAuth = lt_gethandleinterface(ILTDriverAuthenticationDeviceUnit,
                  s_pDriver->CreateDeviceUnitHandle(0));

        if (!s_iAuth) {
            LTLOG("init.fail.no_auth_interface", "Failed to get authentication device unit interface");
            ShutDownDriver();
            return false;
        }
    }

    // Open legacy support libraries for efuse and entropy
    s_pLibEfuse = lt_openlibrary(LTDeviceEfuse);
    if (!s_pLibEfuse) {
        LTLOG("init.fail.no_efuse_lib", "Failed to open LTDeviceEfuse library");
        LTDeviceAuthenticationImpl_LibFini();
        return false;
    }

    // Create efuse device unit handle and get interface
    LTDeviceUnit hEfuseUnit = s_pLibEfuse->CreateDeviceUnitHandle(0);
    s_iEfuse = lt_gethandleinterface(ILTDriverEfuseDeviceUnit, hEfuseUnit);
    if (!s_iEfuse) {
        LTLOG("init.fail.no_efuse_interface", "Failed to get efuse device unit interface");
        LT_GetCore()->DestroyHandle(hEfuseUnit);
        LTDeviceAuthenticationImpl_LibFini();
        return false;
    }

    // Destroy the handle immediately after getting the interface
    LT_GetCore()->DestroyHandle(hEfuseUnit);

    // Try to open AES CBC crypto driver for fallback
    s_pLibAesCbc = lt_createobject_typed(LTDriverCryptoAes128Cbc, LTHardwareCryptoAes128Cbc);

    if (!s_pLibAesCbc) {
        LTLOG("init.fail.no_aes_crypto", "Failed to create AES CBC crypto driver");
        LTDeviceAuthenticationImpl_LibFini();
        return false;
    }

    // Try to open entropy driver
    s_pLibEntropy = lt_createobject(LTDriverCryptoEntropy);
    if (!s_pLibEntropy) {
        LTLOG("init.fail.no_entropy", "Failed to create entropy driver");
        LTDeviceAuthenticationImpl_LibFini();
        return false;
    }

    LTLOG("init.end", "driver:%d", (s_pDriver != NULL));

    return true;
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceAuthentication)
    .CalculateAuthKey    = LTDeviceAuthentication_CalculateAuthKey,
    .AuthEncode          = LTDeviceAuthentication_AuthEncode,
    .AuthDecode          = LTDeviceAuthentication_AuthDecode,
    .GetRandom16         = LTDeviceAuthentication_GetRandom16,
    .ValidateKeyCheck    = LTDeviceAuthentication_ValidateKeyCheck,
    .RunAuthentication   = LTDeviceAuthentication_RunAuthentication,
    .IsSecureDevice      = LTDeviceAuthentication_IsSecureDevice,
    .IsUsingDevKeys      = LTDeviceAuthentication_IsUsingDevKeys,
    .GetChipID           = LTDeviceAuthentication_GetChipID,
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  31-Jul-25   claudius    created from RCUCrypto.cpp
 */
