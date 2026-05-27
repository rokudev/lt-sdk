/*******************************************************************************
 * platforms/esp32/source/esp32/driver/identity/Esp32DriverIdentity.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <esp32/Esp32_SoC.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/flash/LTDeviceFlash.h>
#include <lt/driver/identity/LTDriverIdentity.h>
#include <lt/system/crypto/LTSystemCrypto.h>

DEFINE_LTLOG_SECTION("esp32.identity")

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTDriverIdentity, (LTDeviceFlash));

/* Device Identity properties on ESP32 products are saved in the partition
 * "prov", which is by default located at 0x3FF 000.
 * The content of the partition corresponds to the following struct
 * struct {
 *     char deviceId[12];
 *     u8   reserved[32];
 *     u8   aes_key1[16];
 *     u8   aes_key2[16];
 *     u8   reserved[3];
 *     u8   checksum;
 * };
 */

#define PROV_PARTITION "prov"

/* Maximum length of device ID and serial number on ESP32 products */
enum {
    kLTIdentityDeviceIdOffset = 0x0,
    kLTIdentityKeyOffset      = 0x2C,
    kLTIdentityMacOffset      = 0x50,
    kLTIdentityUdsHashOffset  = 0x58,
    kLTIdentityUdsHashBytes   = 8,
    kLTIdentityUdsOffset      = 0x60,    // append to the end of provisioningESP_t, must align to 0x20, uds (32 B) + sha (32 B)
    kLTIdentityHashOffset     = 0x80,

    kLTIdentityDeviceIDBytes               = 12,
    kLTIdentityDeviceIDStringLen           = kLTIdentityDeviceIDBytes + 1,
    kLTIdentityDeviceSerialNumberBytes     = 12,
    kLTIdentityDeviceSerialNumberStringLen = kLTIdentityDeviceSerialNumberBytes + 1,
    kLTIdentityDeviceChipIDBytes           =  5,
};

static LTDeviceFlash        *s_pDeviceFlash = NULL;
static LTDeviceUnit          s_hFlashHandle = 0;
static LTSystemCrypto       *s_pCrypto      = NULL;
static char                  s_DeviceId[kLTIdentityDeviceIDStringLen]               = { 0 };
static char                  s_SerialNumber[kLTIdentityDeviceSerialNumberStringLen] = { 0 };
static u8                    s_DeviceMac[6]                                         = { 0 };

typedef struct Esp32Identity {
     u8 deviceId[kLTIdentityDeviceIDBytes];       // 0x00
     u8 flashKey[32];                             // 0x0C
     u8 aesKey1[kLTIdentityDeviceAESKeyBytes];    // 0x2C
     u8 aesKey2[kLTIdentityDeviceAESKeyBytes];    // 0x3C
     u8 _reserved1[4];
     u8 mac[6];                                   // 0x50
     u8 _reserved2[2];
     u8 udsHash[kLTIdentityUdsHashBytes];         // 0x58
     u8 uds[kLTIdentityUdsBytes];                 // 0x60
     u8 hash[SHA256_HASH_LENGTH];                 // 0x80
} __attribute__((packed)) Esp32Identity;

static bool VerifyDeviceId(const u8 * pID) {
    // basic sanity check, verifying the first two character as S0
    if ((pID[0] != 'S') || (pID[1] != '0')) return false;

    /* @note: digit 5 is a year and has to be between 0 and 9, digit 6 is a month and has to be
     * either between 0 and 9, or A, C or D.
     */
    if ((pID[5] <= '9'  && pID[5] >= '0')
        && ((pID[6] <= '9'  && pID[6] >= '0') || pID[6] == 'A' || pID[6] == 'D' || pID[6] == 'C')) {
        return true;
    }
    return false;
}

static bool ReadDeviceId(void) {
    LTDeviceFlash_Partition partition;
    partition.entry.nByteOffset = 0;

    ILTFlashDeviceUnit *iFlashDeviceUnit = (ILTFlashDeviceUnit *)LT_GetCore()->GetHandleInterface(s_hFlashHandle);
    /* temp buffer to check if we are getting a valid value before filling deviceId */
    u8 deviceIdBuff[kLTIdentityDeviceIDStringLen] = {0};

    if (s_pDeviceFlash->GetPartition(s_hFlashHandle, PROV_PARTITION, &partition) && iFlashDeviceUnit->ReadBytes(s_hFlashHandle, partition.entry.nByteOffset + kLTIdentityDeviceIdOffset, kLTIdentityDeviceIDBytes, deviceIdBuff)) {
        if (deviceIdBuff[0] != '\0' && deviceIdBuff[0] != 0xFF && VerifyDeviceId(deviceIdBuff)) {
            lt_strncpyTerm(s_DeviceId, (char *)deviceIdBuff, kLTIdentityDeviceIDStringLen);
            return true;
        }
    }

    /* error messages */
    if (partition.entry.nByteOffset != 0x0)
        LTLOG_DEBUG("no.id", "device ID %.12s at addr 0x%lX is not valid", deviceIdBuff, LT_PLT_HANDLE(partition.entry.nByteOffset));
    else LTLOG_DEBUG("no.partition", "partition "PROV_PARTITION" can't be found");

    return false;
}

/* B, I, O, Q and Z are not used for 31-base representation */
static const char baseChars[] = "0123456789ACDEFGHJKLMNPRSTUVWXY";

static bool CalculateSerialNumber(void) {
    u32 sum = 0;
    u8 pos = kLTIdentityDeviceSerialNumberBytes;
    const char *codeChar;
    do {
        pos--;
        if ((codeChar = lt_strchr(baseChars, *(s_DeviceId + pos)))) {
            sum += (codeChar - baseChars);
        }
        else {
            /* an invalid char, return an error */
            return false;
        }
    } while (pos > kLTIdentityDeviceSerialNumberBytes - kLTIdentityDeviceChipIDBytes);
    sum = sum % 31;

    /* Serial number for ESP32 products always starts with X02400, then a checksum,
     * then the last 5 digits from the device ID.
     */
    lt_strncpyTerm(s_SerialNumber, "X02400", kLTIdentityDeviceSerialNumberStringLen);
    s_SerialNumber[6] = baseChars[sum];
    lt_strncpyTerm(&s_SerialNumber[7], s_DeviceId + kLTIdentityDeviceSerialNumberBytes - kLTIdentityDeviceChipIDBytes, kLTIdentityDeviceSerialNumberStringLen - 7);
    s_SerialNumber[kLTIdentityDeviceSerialNumberStringLen - 1] = 0;
    return true;
}

static bool ReadDeviceMac(void) {
    LTDeviceFlash_Partition partition;
    ILTFlashDeviceUnit *iFlashDeviceUnit = (ILTFlashDeviceUnit *)LT_GetCore()->GetHandleInterface(s_hFlashHandle);
    return s_pDeviceFlash->GetPartition(s_hFlashHandle, PROV_PARTITION, &partition) &&
           iFlashDeviceUnit->ReadBytes(s_hFlashHandle, partition.entry.nByteOffset + kLTIdentityMacOffset, 6, s_DeviceMac);
}

static const char *Esp32DriverIdentity_GetDeviceId(void) {
    return s_DeviceId;
}

static const char *Esp32DriverIdentity_GetSerialNumber(void) {
    return s_SerialNumber;
}

static bool Esp32DriverIdentity_GetAESKey(u8 aesKey[kLTIdentityDeviceAESKeyBytes], u8 nKeyIndex) {
    LTDeviceFlash_Partition partition;

    if (!aesKey) return false;

    ILTFlashDeviceUnit *iFlashDeviceUnit = (ILTFlashDeviceUnit *)LT_GetCore()->GetHandleInterface(s_hFlashHandle);

    u32 nKeyOffset = kLTIdentityKeyOffset; // defaults to AES1
    if (nKeyIndex < 1 || nKeyIndex > 2) return false;
    if (nKeyIndex == 2) nKeyOffset += kLTIdentityDeviceAESKeyBytes;

    if (s_pDeviceFlash->GetPartition(s_hFlashHandle, PROV_PARTITION, &partition) && iFlashDeviceUnit->ReadBytes(s_hFlashHandle, partition.entry.nByteOffset + nKeyOffset, kLTIdentityDeviceAESKeyBytes, aesKey)) {
        return true;
    }
    return false;
}

static void Esp32DriverIdentity_GetMac(u8 mac[6]) {
    lt_memcpy(mac, s_DeviceMac, 6);
}

static u32 Esp32DriverIdentity_GetBoardRevision(void) { return LT_U32_MAX; }

// set uds to null to only check if a valid uds exists.
static bool Esp32DriverIdentity_GetUds(u8 uds[kLTIdentityUdsBytes]) {
    if (!s_hFlashHandle) return false;
    LTDeviceFlash_Partition partition;
    ILTFlashDeviceUnit *iFlashDeviceUnit = lt_gethandleinterface(ILTFlashDeviceUnit, s_hFlashHandle);
    if (!s_pDeviceFlash->GetPartition(s_hFlashHandle, PROV_PARTITION, &partition)) return false;
    u8 h[SHA256_HASH_LENGTH], u[kLTIdentityUdsBytes];
    bool ret = false;
    do {
        // read existing uds and hash
        if (!iFlashDeviceUnit->ReadBytes(s_hFlashHandle, partition.entry.nByteOffset + kLTIdentityUdsOffset, kLTIdentityUdsBytes, u)) break;
        s_pCrypto->GenDigestSHA256(u, kLTIdentityUdsBytes, h);
        // read uds hash (only 8 bytes)
        if (!iFlashDeviceUnit->ReadBytes(s_hFlashHandle, partition.entry.nByteOffset + kLTIdentityUdsHashOffset, kLTIdentityUdsHashBytes, h + kLTIdentityUdsHashBytes)) break;
        // check if the two hashes match
        if (lt_memcmp(h, h + kLTIdentityUdsHashBytes, kLTIdentityUdsHashBytes) != 0) break;
        // existing hash is valid
        if (uds) lt_memcpy(uds, u, kLTIdentityUdsBytes);
        ret = true;
    } while (0);
    // clear uds from temporary buffer
    lt_memset(u, 0, kLTIdentityUdsBytes);
    return ret;
}

static bool Esp32DriverIdentity_SetUds(const u8 uds[kLTIdentityUdsBytes]) {
    if (!uds || !s_hFlashHandle) return false;

    // check if UDS exists already
    if (Esp32DriverIdentity_GetUds(NULL)) return false;

    LTDeviceFlash_Partition partition;
    ILTFlashDeviceUnit *iFlashDeviceUnit = lt_gethandleinterface(ILTFlashDeviceUnit, s_hFlashHandle);
    if (!s_pDeviceFlash->GetPartition(s_hFlashHandle, PROV_PARTITION, &partition)) return false;

    Esp32Identity *prov = lt_malloc(sizeof(Esp32Identity));
    if (!prov) return false;
    lt_memset(prov, 0, sizeof(Esp32Identity));

    bool ret = false;
    do {
        // read bytes until uds hash offset
        if (!iFlashDeviceUnit->ReadBytes(s_hFlashHandle, partition.entry.nByteOffset, kLTIdentityUdsHashOffset, (u8 *)prov)) break;
        // compute uds hash, but only 8 bytes will be saved. overwrite to uds in prov, but it's ok.
        if (kLTSystemCrypto_Result_Ok != s_pCrypto->GenDigestSHA256(uds, kLTIdentityUdsBytes, (u8 *)prov->udsHash)) break;
        // write uds
        lt_memcpy(prov->uds, uds, kLTIdentityUdsBytes);
        // compute hash of prov
        if (kLTSystemCrypto_Result_Ok != s_pCrypto->GenDigestSHA256((u8 *)prov, kLTIdentityHashOffset, (u8 *)prov->hash)) break;
        // write to prov partition
        u32 nStartSector = iFlashDeviceUnit->ByteOffsetToSectorNumber(s_hFlashHandle, partition.entry.nByteOffset);
        iFlashDeviceUnit->EraseSectors(s_hFlashHandle, nStartSector, 1);
        if (!iFlashDeviceUnit->WriteBytes(s_hFlashHandle, partition.entry.nByteOffset, sizeof(Esp32Identity), (u8 *)prov)) break;
        ret = true;
    } while (0);

    lt_memset(prov, 0, sizeof(Esp32Identity));
    lt_free(prov);
    return ret;
}

static bool Esp32DriverIdentity_IsValid(void) {
    // LTDeviceIdenticy.c says always return true if this function isn't implemented, so returning true
    return true;
}

static LTSecurityStatus Esp32DriverIdentity_IsUnsecured(void) {
    // FIXME LT-1880 implement this properly
    return kLTSecurityStatus_Success;
}

static u32 Esp32DriverIdentity_GetLTATSize(void) {
    return sizeof(LTSecurityLTATPayload) + kEsp32_LTATSignatureSize;
}

static LTSecurityStatus Esp32DriverIdentity_AuthenticateLTAT(LTSecurityLTATPayload * pLTAT, u32 * pClaimMask) {
    *((volatile u32 *)pClaimMask) = 0;
    u8 imageDigest[32]    = { [ 0 ... 31 ] = 0x33 };
    u8 verifiedDigest[32] = { [ 0 ... 31 ] = 0xee };
    LTDeviceConfig * pDeviceConfig = (LTDeviceConfig *)LT_GetCore()->OpenLibrary("LTDeviceConfig");
    if (!pDeviceConfig) return kLTSecurityStatus_Fail;
    u32 section = pDeviceConfig->GetDeviceSection("LTDeviceIdentity");
    u32 size = 0;
    const u8 * ltatTrustedKeyDigest = pDeviceConfig->ReadBinary(section, "driver/0/ltatDigest", &size);
    volatile LTSecurityStatus nCheck = kLTSecurityStatus_Success - kEsp32_SecurityCheckStatus_Success;
    if (size == 32 && ltatTrustedKeyDigest) {
        s_pCrypto->GenDigestSHA256((u8 *)pLTAT, sizeof(LTSecurityLTATPayload), imageDigest);
        /* Invoke ROM routine to check RSA3072 signature */
        nCheck += ets_secure_boot_verify_signature((u8 *)(pLTAT + 1), imageDigest, ltatTrustedKeyDigest, verifiedDigest);
        nCheck -= lt_memcmp(verifiedDigest, imageDigest, 32);
    }
    LT_GetCore()->CloseLibrary((LTLibrary *)pDeviceConfig);
    if (nCheck == kLTSecurityStatus_Success) {
        *pClaimMask = pLTAT->hdr.mask[kLTSecurityClaimGroup2];
    }
    return nCheck;
}

static void ShutdownDriverIdentity(void) {
    lt_closelibrary(s_pCrypto);
    s_pCrypto = NULL;
    lt_destroyhandle(s_hFlashHandle);
    s_hFlashHandle = 0;
    lt_closelibrary(s_pDeviceFlash);
    s_pDeviceFlash = NULL;
}

static bool LTDriverIdentityImpl_LibInit(void) {
    do {
        s_pDeviceFlash = (LTDeviceFlash *) (LT_GetCore()->OpenLibrary("LTDeviceFlash"));
        if (!s_pDeviceFlash) {
            LTLOG_REDALERT("libinit.flash", "Cannot load LTDeviceFlash");
            break;
        }

        s_hFlashHandle = LT_GetLTDeviceFlash()->CreateDeviceUnitHandle(0);
        if (!s_hFlashHandle) {
            LTLOG_REDALERT("libinit.flashdev", "Cannot load LTDeviceFlash device unit");
            break;
        }

        s_pCrypto = lt_openlibrary(LTSystemCrypto);
        if (!s_pCrypto) {
            LTLOG_REDALERT("libinit.crypto", "Cannot load LTSystemCrypto");
            break;
        }

        if (!ReadDeviceId()) {
            LTLOG_REDALERT("libinit.rd.did", "Failed to read device ID");
            break;
        }
        if (!CalculateSerialNumber()) {
            LTLOG_REDALERT("libinit.calc.sn", "Failed to calculate serial number");
            break;
        }

        if (!ReadDeviceMac()) {
            LTLOG_REDALERT("libinit.rd.mac", "Failed to read device mac");
            break;
        }
        return true;
    } while (0);

    // only any failure, shutdown and clean up.
    ShutdownDriverIdentity();
    return false;
}

static void LTDriverIdentityImpl_LibFini(void) {
    ShutdownDriverIdentity();
}

define_LTLIBRARY_ROOT_INTERFACE(LTDriverIdentity) {
    .GetDeviceId         = Esp32DriverIdentity_GetDeviceId,
    .GetSerialNumber     = Esp32DriverIdentity_GetSerialNumber,
    .GetAESKey           = Esp32DriverIdentity_GetAESKey,
    .GetMac              = Esp32DriverIdentity_GetMac,
    .GetBoardRevision    = Esp32DriverIdentity_GetBoardRevision,
    .SetUds              = Esp32DriverIdentity_SetUds,
    .GetUds              = Esp32DriverIdentity_GetUds,
    .IsValid             = Esp32DriverIdentity_IsValid,
    .IsUnsecured         = Esp32DriverIdentity_IsUnsecured,
    .GetLTATSize         = Esp32DriverIdentity_GetLTATSize,
    .AuthenticateLTAT    = Esp32DriverIdentity_AuthenticateLTAT,
} LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  11-Jul-22   commodus    created
 */
