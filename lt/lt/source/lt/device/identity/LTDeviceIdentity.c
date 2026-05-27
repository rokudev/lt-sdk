/*******************************************************************************
 * <lt/device/identity/LTDeviceIdentity.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Device Library for  device-specific identification artifacts
 *
 * Currently available device properties are device ID and serial number. AES keys could
 * be added next, if available on the device.
 *
 ******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/device/identity/LTDeviceIdentity.h>
#include <lt/driver/identity/LTDriverIdentity.h>
#include <lt/device/identity/LTManufacturingIdentity.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/flash/LTDeviceFlash.h>

// DEFINE_LTLOG_SECTION("device.identity");

static LTDriverIdentity *s_pLibIdentityDriver = NULL;

static LTSecurityStatus s_LTATvalid = 0;
static u32              s_LTATclaims = 0;

static const char *s_DeviceManufacturer;
static const char *s_DeviceModel;
static const char *s_DeviceType;

static const char *LTDeviceIdentity_GetManufacturer(void) {
    if (s_DeviceManufacturer) return s_DeviceManufacturer;
    return "";
}

static const char *LTDeviceIdentity_GetModel(void) {
    if (s_DeviceModel) return s_DeviceModel;
    return "";
}

static const char *LTDeviceIdentity_GetType(void) {
    if (s_DeviceType) return s_DeviceType;
    return "";
}

static const char *LTDeviceIdentity_GetDeviceId(void) {
    return s_pLibIdentityDriver->GetDeviceId();
}

static const char *LTDeviceIdentity_GetSerialNumber(void) {
    return s_pLibIdentityDriver->GetSerialNumber();
}

static bool LTDeviceIdentity_GetAESKey(u8 aesKey[kLTIdentityDeviceAESKeyBytes], u8 nKeyIndex) {
    return s_pLibIdentityDriver->GetAESKey(aesKey, nKeyIndex);
}

static void LTDeviceIdentity_GetMac(u8 mac[6]) {
    return s_pLibIdentityDriver->GetMac(mac);
}

static u32 LTDeviceIdentity_GetBoardRevision(void) {
    return s_pLibIdentityDriver->GetBoardRevision();
}

static bool LTDeviceIdentity_GetUds(u8 uds[kLTIdentityUdsBytes]) {
    return s_pLibIdentityDriver->GetUds(uds);
}

static bool LTDeviceIdentity_SetUds(const u8 uds[kLTIdentityUdsBytes]) {
    return s_pLibIdentityDriver->SetUds(uds);
}

static bool LTDeviceIdentity_IsValid(void) {
    /* driver should always return true if it doesn't have a real implementation for IsValid() */
    return s_pLibIdentityDriver->IsValid();
}

static LTSecurityStatus LTDeviceIdentity_IsUnsecured(void) {
    /* driver should always return LTSecurityStatus_Fail if it doesn't have a real
       implementation for IsUnsecured().  This will signify that the device is secured. */
    return s_pLibIdentityDriver->IsUnsecured();
}

static LTSecurityStatus LTDeviceIdentity_IsManufacturingFirmware(void) {
    LTManufacturingIdentity *pMfg = lt_openlibrary(LTManufacturingIdentity);
    if (pMfg == NULL) return kLTSecurityStatus_Fail;
    lt_closelibrary(pMfg);
    LT_ASSERT(pMfg);
    return kLTSecurityStatus_Success;
}

static u8 * ReadDataFromFlashPartition(char * pPartitionName, u32 nByteOffset, u32 nSizeInBytes) {
    if (!nSizeInBytes) return NULL;
    LTDeviceFlash *pDeviceFlash = (LTDeviceFlash *)LT_GetCore()->OpenLibrary("LTDeviceFlash");
    if (!pDeviceFlash) return NULL;
    u8 * pData = NULL;
    LTDeviceUnit hFlashUnit = pDeviceFlash->CreateDeviceUnitHandle(0);
    ILTFlashDeviceUnit *iFlashUnit = lt_gethandleinterface(ILTFlashDeviceUnit, hFlashUnit);
    if (iFlashUnit) {
        LTDeviceFlash_Partition part;
        if (pDeviceFlash->GetPartition(hFlashUnit, pPartitionName, &part)) {
            pData = (u8 *)lt_malloc(nSizeInBytes);
            if (pData && !iFlashUnit->ReadBytes(hFlashUnit, part.entry.nByteOffset + nByteOffset, nSizeInBytes, pData)) {
                lt_free(pData);
                pData = NULL;
            }
        }
    }
    LT_GetCore()->DestroyHandle(hFlashUnit);
    LT_GetCore()->CloseLibrary((LTLibrary *)pDeviceFlash);
    return pData;
}

static bool WriteDataToFlashPartition(char * pPartitionName, u32 nByteOffset, u32 nSizeInBytes, const u8 * pData) {
    if (!nSizeInBytes) return NULL;
    LTDeviceFlash *pDeviceFlash = (LTDeviceFlash *)LT_GetCore()->OpenLibrary("LTDeviceFlash");
    if (!pDeviceFlash) return NULL;
    LTDeviceUnit hFlashUnit = pDeviceFlash->CreateDeviceUnitHandle(0);
    ILTFlashDeviceUnit *iFlashUnit = lt_gethandleinterface(ILTFlashDeviceUnit, hFlashUnit);
    if (iFlashUnit) {
        LTDeviceFlash_Partition part;
        if (pDeviceFlash->GetPartition(hFlashUnit, pPartitionName, &part)) {
            if (!iFlashUnit->WriteBytes(hFlashUnit, part.entry.nByteOffset + nByteOffset, nSizeInBytes, pData)) {
                LT_GetCore()->DestroyHandle(hFlashUnit);
                LT_GetCore()->CloseLibrary((LTLibrary *)pDeviceFlash);
                return false;
            }
        }
    }
    LT_GetCore()->DestroyHandle(hFlashUnit);
    LT_GetCore()->CloseLibrary((LTLibrary *)pDeviceFlash);
    return true;
}

static bool EraseFlashPartition(char * pPartitionName) {
    LTDeviceFlash *pDeviceFlash = (LTDeviceFlash *)LT_GetCore()->OpenLibrary("LTDeviceFlash");
    if (!pDeviceFlash) return false;
    LTDeviceUnit hFlashUnit = pDeviceFlash->CreateDeviceUnitHandle(0);
    ILTFlashDeviceUnit *iFlashUnit = lt_gethandleinterface(ILTFlashDeviceUnit, hFlashUnit);
    bool success = false;
    if (iFlashUnit) success = pDeviceFlash->ErasePartition(hFlashUnit, pPartitionName);
    LT_GetCore()->DestroyHandle(hFlashUnit);
    LT_GetCore()->CloseLibrary((LTLibrary *)pDeviceFlash);
    return success;
}

static bool LTDeviceIdentity_InstallLTAT(const u8 * pLTAT_, u32 size) {
    /* Erase flash partition, then return immediately if LTAT is NULL */
    if (!EraseFlashPartition("ltat")) return false;
    if (!pLTAT_) return true;
    LTSecurityLTATPayload * pLTAT = (LTSecurityLTATPayload *)pLTAT_;
    /* Make sure LTAT passes sanity checking before installation */
    if (size != s_pLibIdentityDriver->GetLTATSize()) return false;
    if (pLTAT->hdr.nMagic != kLTSecurity_Magic_LTAT) return false;
    const char * deviceId = LTDeviceIdentity_GetDeviceId();
    if (lt_strlen(deviceId) == kLTSecurity_DeviceIDLength &&
           lt_strncmp(LTDeviceIdentity_GetDeviceId(), (char *)pLTAT->hdr.id, kLTSecurity_DeviceIDLength) == 0) {
        return WriteDataToFlashPartition("ltat", 0, size, pLTAT_);
    }
    return false;
}

static LTSecurityStatus LTDeviceIdentity_CheckLTATClaims(LTSecurityClaimGroup group, LTSecurityClaimMask mask) {
    if (s_pLibIdentityDriver->CheckLTATClaims)
        return s_pLibIdentityDriver->CheckLTATClaims(group, mask);

    /* Only claim group 2 is currently supported */
    if (group != kLTSecurityClaimGroup2 || !mask) return kLTSecurityStatus_Fail;
    if (s_LTATvalid != kLTSecurityStatus_Success) {
        u32 ltatSize = s_pLibIdentityDriver->GetLTATSize();
        LTSecurityLTATPayload * pLTAT = (LTSecurityLTATPayload *)ReadDataFromFlashPartition("ltat", 0, ltatSize);
        if (!pLTAT) return kLTSecurityStatus_Fail;
        if (pLTAT->hdr.nMagic == kLTSecurity_Magic_LTAT) {
            const char *deviceId = LTDeviceIdentity_GetDeviceId();
            if (lt_strlen(deviceId) == kLTSecurity_DeviceIDLength &&
                    lt_strncmp(LTDeviceIdentity_GetDeviceId(), (char *)pLTAT->hdr.id, kLTSecurity_DeviceIDLength) == 0) {
                s_LTATvalid = s_pLibIdentityDriver->AuthenticateLTAT(pLTAT, &s_LTATclaims);
            }
        }
        lt_free(pLTAT);
    }
    LTSecurityStatus status = kLTSecurityStatus_Fail;
    if (s_LTATvalid == kLTSecurityStatus_Success) {
        if ((s_LTATclaims & mask) == mask) {
            return kLTSecurityStatus_Success;
        }
    }
    return status;
}

/*******************************************************************************
 * Library startup and shutdown:                                              */
static void LTDeviceIdentityImpl_LibFini(void) {
    if (s_pLibIdentityDriver) lt_closelibrary(s_pLibIdentityDriver);
    s_pLibIdentityDriver = NULL;
    s_LTATvalid = 0;
    s_LTATclaims = 0;
}

static bool LTDeviceIdentityImpl_LibInit(void) {
    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    if (!deviceConfig) return false;
    bool result = false;
    do {
        const char *libName;
        if (!(libName = deviceConfig->GetDriverAt("LTDeviceIdentity", 0))) break;
        if (!(s_pLibIdentityDriver = (LTDriverIdentity *)LT_GetCore()->OpenLibrary(libName))) break;
        u32 deviceSection;
        if (!(deviceSection = deviceConfig->GetDeviceSection("LTDeviceIdentity"))) break;
        if (!(s_DeviceManufacturer = deviceConfig->ReadString(deviceSection, "identity/manufacturer"))) break;
        if (!(s_DeviceModel        = deviceConfig->ReadString(deviceSection, "identity/model"))) break;
        if (!(s_DeviceType         = deviceConfig->ReadString(deviceSection, "identity/type"))) break;
        result = true;
    } while (false);
    lt_closelibrary(deviceConfig);
    return result;
}

define_LTLIBRARY_ROOT_INTERFACE(LTDeviceIdentity) {
    .GetManufacturer         = LTDeviceIdentity_GetManufacturer,
    .GetModel                = LTDeviceIdentity_GetModel,
    .GetType                 = LTDeviceIdentity_GetType,
    .GetDeviceId             = LTDeviceIdentity_GetDeviceId,
    .GetSerialNumber         = LTDeviceIdentity_GetSerialNumber,
    .GetAESKey               = LTDeviceIdentity_GetAESKey,
    .GetMac                  = LTDeviceIdentity_GetMac,
    .GetBoardRevision        = LTDeviceIdentity_GetBoardRevision,
    .SetUds                  = LTDeviceIdentity_SetUds,
    .GetUds                  = LTDeviceIdentity_GetUds,
    .IsValid                 = LTDeviceIdentity_IsValid,
    .IsUnsecured             = LTDeviceIdentity_IsUnsecured,
    .IsManufacturingFirmware = LTDeviceIdentity_IsManufacturingFirmware,
    .InstallLTAT             = LTDeviceIdentity_InstallLTAT,
    .CheckLTATClaims         = LTDeviceIdentity_CheckLTATClaims,
} LTLIBRARY_DEFINITION

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  13-Jul-22   commodus    created
 *  03-Apr-23   augustus    load driver from LTDeviceConfig specification
 *  13-Apr-23   constantine remove interface type check from  Driver Library load
 *  12-May-24   augustus    cast s_pLibIdentityDriver once on open, not on every use
 *  12-May-24   augustus    IsValid() and IsUnsecured() are no longer optional for the driver
*/
