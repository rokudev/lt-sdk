/*******************************************************************************
 * LTBootDriver.c                                        LT Bootloader for Esp32
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include "sdkconfig.h"

#include "bootloader_flash_priv.h"
#include "bootloader_common.h"
#include "bootloader_utility.h"

#include "esp_flash_encrypt.h"
#include "rom/secure_boot.h"

#include "LTBootDriver.h"
#include "LTBootPlatform.h"
#include "LTBootInternal.h"

/* Platform IDs */
#define PLID_FK  1
#define PLID_FL  2
#define PLID_FM  3
#define PLID_FP  4
#define PLID_FR  5
#define PLID_GC  6
#define PLID_GD  7
#define PLID_GL  8

enum {
    /* Expected chip revision */
    kLTBootExpectedChipRevision           = 3,                            /**< Chip revision MUST be this */

    /* Firmware magic number */
    kLTBootFirmwareMagic                  = ESP_APP_DESC_MAGIC_WORD,      /**< Firmware magic number */

    /* Bootloader Security Dynasty (anti-rollback) */
    kLTBootDynasty_Initial                = 0,                            /**< Initial dynasty */
    kLTBootDynasty_PostMigration          = 1,                            /**< Post firmware-migration dynasty */
      /* ... Put new dynasties here as needed ... */
    kLTBootDynasty_CurrentSecurityDynasty = kLTBootDynasty_PostMigration, /**< Current dynasty (bootloader anti-rollback version) */

    /* Partition table locations */
    kLTBootPrimaryPartitionTableOffset    = 0x11000,                      /**< Primary partition table is consulted first */
    kLTBootBackupPartitionTableOffset     = 0x12000,                      /**< Fallback partition table */
    kLTBootLoaderOffset                   = 0x1000,                       /**< Bootloader location */

    /* Flash write quantum */
    kLTBootWriteQuantum                   = 32
};

/* Image app description header (saved during check OTA partition) */
static esp_app_desc_t s_appDesc;

/* Image data cache (saved during application security check) */
static esp_image_metadata_t s_imageData;

LTBootSecurityCheck CheckEFuseBitIsSet(const esp_efuse_desc_t * pField[]) {
    volatile LTBootSecurityCheck nCheck = kLTBootSecurityCheck_Pass + 0x11;
    u8 nValue = 0x75;
    esp_efuse_read_field_blob(pField, &nValue, 1);
    nCheck -= (0x11 * nValue);
    return nCheck;
}

LTBootSecurityCheck CheckEFuseBitIsClear(const esp_efuse_desc_t * pField[]) {
    volatile LTBootSecurityCheck nCheck = kLTBootSecurityCheck_Pass + 0x11;
    u8 nValue = 0x75;
    esp_efuse_read_field_blob(pField, &nValue, 1);
    nCheck -= (0x11 * (nValue + 1));
    return nCheck;
}

LTBootSecurityCheck GetApplicationSecureBootKeyDigest(const void ** ppDigest) {
   /* SHA256 public key digests
    *   Digests cover 776-byte ESP32 Secure Boot Version 2 format:
    *   {   u8  modulus[384];
    *       u32 exp;
    *       u8  r_inv[384];
    *       u32 m_prime;    }; */
    static const u8 s_appTrustedKeyDigest[32] = {
#if defined(LT_BOOTLOADER_SECURITY_DEV_BYPASS) || !defined(LT_PLATFORM_ID)
    /* SHA256 digest for secure_boot_test_key_application.pem public key */
    0x40, 0x09, 0xef, 0xab, 0x6a, 0xfe, 0x89, 0x56, 0xa4, 0xb8, 0xdd, 0x35, 0x3f, 0xc9, 0x74, 0x6b,
    0x3f, 0x24, 0xd4, 0xda, 0x46, 0xa6, 0xf6, 0x72, 0x0f, 0xd2, 0xc2, 0x58, 0x88, 0xf0, 0x01, 0x0a
#elif LT_PLATFORM_ID == PLID_FK
    /* Dimmable white bulb */
    0x2b, 0xea, 0x44, 0x78, 0xfa, 0xf6, 0xaa, 0x7d, 0x99, 0xfa, 0x43, 0x1d, 0xae, 0x98, 0xe1, 0xb0,
    0x1e, 0xe5, 0xd4, 0xd3, 0x37, 0xe7, 0x38, 0x2f, 0x60, 0x2b, 0x13, 0xda, 0x20, 0xa5, 0x9e, 0xf3
#elif LT_PLATFORM_ID == PLID_FL
    /* Color bulb */
    0x5e, 0x50, 0x8f, 0x38, 0x87, 0xe3, 0xdb, 0xe5, 0xb1, 0x39, 0xba, 0xa6, 0xed, 0x31, 0xf3, 0xac,
    0xc6, 0xbc, 0x11, 0x90, 0xbc, 0xc1, 0xc2, 0xe5, 0x64, 0xaa, 0x76, 0x07, 0xbd, 0xd1, 0x7b, 0x05
#elif LT_PLATFORM_ID == PLID_FM
    /* Indoor color light strip (16ft) */
    0xb5, 0x8d, 0xab, 0x54, 0x6a, 0x5b, 0xd5, 0xe1, 0x56, 0x87, 0xab, 0xbc, 0xb1, 0x6e, 0x27, 0xbe,
    0x35, 0x46, 0xfc, 0xca, 0x91, 0x98, 0x39, 0x4b, 0x36, 0x10, 0x90, 0x8e, 0x6d, 0xb6, 0xdc, 0x40
#elif LT_PLATFORM_ID == PLID_FP
    /* Indoor plug */
    0x50, 0x9e, 0x6d, 0xda, 0x4a, 0x31, 0xec, 0x88, 0x2a, 0xc5, 0x50, 0xdd, 0x7c, 0x6d, 0x0d, 0xa4,
    0x9f, 0x0a, 0x4c, 0x20, 0xcf, 0x1a, 0x22, 0xda, 0x6c, 0x99, 0x31, 0x29, 0x56, 0x66, 0x73, 0xa5
#elif LT_PLATFORM_ID == PLID_FR
    /* Outdoor plug */
    0x1d, 0x85, 0x09, 0xf4, 0xd4, 0x72, 0x57, 0x2f, 0x7e, 0xbe, 0xf9, 0x2f, 0x86, 0x46, 0x97, 0x3e,
    0x81, 0xed, 0xdd, 0x90, 0x34, 0x61, 0x3c, 0x75, 0x62, 0x8d, 0xde, 0xb9, 0xa3, 0x76, 0xac, 0x4d
#elif LT_PLATFORM_ID == PLID_GC
    /* Light strip PRO (16ft) */
    0x41, 0xf3, 0xa4, 0x4c, 0x5b, 0x5a, 0xd0, 0x31, 0x23, 0xf9, 0x76, 0x63, 0xf1, 0xea, 0x58, 0xf3,
    0x33, 0x44, 0xf2, 0xae, 0x37, 0xe9, 0x3d, 0xa9, 0x57, 0xd7, 0x01, 0xde, 0x7e, 0x16, 0x10, 0xcc
#elif LT_PLATFORM_ID == PLID_GD
    /* Light strip PRO (32ft) */
    0x5d, 0x21, 0x85, 0xd3, 0xae, 0x89, 0xf4, 0x47, 0x44, 0x50, 0xb5, 0x6c, 0xe4, 0xb8, 0xf4, 0xa4,
    0x79, 0xa5, 0x50, 0x23, 0x55, 0xa6, 0x6d, 0xdd, 0x45, 0x11, 0x09, 0x95, 0x64, 0x18, 0xd6, 0x98
#elif LT_PLATFORM_ID == PLID_GL
    /* Indoor color light strip (32ft) */
    0x6d, 0x47, 0xb5, 0xf1, 0xf8, 0xa8, 0x02, 0xa1, 0x85, 0x1b, 0xdb, 0x38, 0xf9, 0xf7, 0xe2, 0x5e,
    0x0f, 0xd9, 0x82, 0x3f, 0xb0, 0x41, 0x6b, 0xd5, 0xbb, 0x90, 0x73, 0x20, 0x37, 0x69, 0x9e, 0x7d
#else
    #error "Unknown Platform ID"
#endif
    };
    *((const u8 **)ppDigest) = s_appTrustedKeyDigest;
    return kLTBootSecurityCheck_Pass;
}

/* Get bootloader version string, supports up to 9.9 (Major.Minor) */
const char * LTBootDriver_GetVersion(void) {
    static char s_bootVersion[4] = "0.0";
    u8 ver[2];
    /* Read version from bootloader header */
    if (LTBootDriverFlash_ReadBytes(kLTBootLoaderOffset + LT_OFFSET_OF(esp_image_header_t, major_rev), sizeof(ver), ver)) {
        if (ver[0] <= 9 && ver[1] <= 9) {
            s_bootVersion[0] = '0' + ver[0];
            s_bootVersion[2] = '0' + ver[1];
        }
    }
    return (const char *)s_bootVersion;
}

LTBootSecurityCheck LTBootDriver_TRNGDelay(void) {
    /* Wait at least 32 * 40 cycles to get 32 more bits of entropy for next time */
    u32 nRandom = (32 * 40) + (ESP32_REG(WDEV_RND) & 0x1fff);
    volatile LTBootSecurityCheck nCheck = kLTBootSecurityCheck_Pass - nRandom;
    for (volatile u32 nIdx = 0; nIdx < nRandom; nIdx++, nCheck++);
    return nCheck;
}

static LTBootSecurityCheck CheckSecureOTP(void) {
    volatile LTBootSecurityCheck nCheck = kLTBootSecurityCheck_Pass - 0xaa;
    /* Check BLK3 OTP is accessible */
    nCheck += 0x11 * (CheckEFuseBitIsClear(ESP_EFUSE_WR_DIS_BLK3) == kLTBootSecurityCheck_Pass);
    nCheck += 0x11 * (CheckEFuseBitIsClear(ESP_EFUSE_RD_DIS_BLK3) == kLTBootSecurityCheck_Pass);
    /* Check that ROM BASIC, UART, JTAG and EFUSE are secured. */
    nCheck += 0x11 * (CheckEFuseBitIsSet(ESP_EFUSE_CONSOLE_DEBUG_DISABLE) == kLTBootSecurityCheck_Pass);
#ifndef LT_BOOTLOADER_SECURITY_DEV_BYPASS
    nCheck += 0x11 * (CheckEFuseBitIsSet(ESP_EFUSE_UART_DOWNLOAD_DIS) == kLTBootSecurityCheck_Pass);
    nCheck += 0x11 * (CheckEFuseBitIsSet(ESP_EFUSE_DISABLE_JTAG) == kLTBootSecurityCheck_Pass);
    nCheck += 0x11 * (CheckEFuseBitIsSet(ESP_EFUSE_WR_DIS_EFUSE_RD_DISABLE) == kLTBootSecurityCheck_Pass);
    nCheck += 0x11 * esp_flash_encryption_enabled();
    nCheck += 0x11 * (CheckEFuseBitIsSet(ESP_EFUSE_DISABLE_DL_ENCRYPT) == kLTBootSecurityCheck_Pass);
    nCheck += 0x11 * (CheckEFuseBitIsSet(ESP_EFUSE_DISABLE_DL_DECRYPT) == kLTBootSecurityCheck_Pass);
    nCheck += 0x11 * (CheckEFuseBitIsSet(ESP_EFUSE_DISABLE_DL_CACHE) == kLTBootSecurityCheck_Pass);
#else
    /* Bypass the above checks on a security development unit. */
    nCheck += 0x77;
#endif
    return nCheck;
}

LTBootSecurityCheck LTBootDriver_CheckChipSecurity(void) {
    volatile LTBootSecurityCheck nCheck = kLTBootSecurityCheck_Pass - 0x22;
    u8 chipRev = bootloader_common_get_chip_revision();
    if (chipRev != kLTBootExpectedChipRevision) {
        LTBootPlatform_printf("Bad chip revision %u\n", chipRev);
    } else nCheck += 0x11;
    if (IsSecureBootDisabled()) {
        nCheck += 0x11;
        LTBootPlatform_SecurityAssert(IsSecureBootDisabled());
    } else {
        nCheck += 0x11 * (CheckSecureOTP() == kLTBootSecurityCheck_Pass);
        if (nCheck != kLTBootSecurityCheck_Pass) {
            LTBootPlatform_printf("Insecure fuse settings\n");
            LTBootPlatform_SecurityAbort();
        }
        LTBootPlatform_SecurityAssert(CheckSecureOTP() == kLTBootSecurityCheck_Pass);
    }
    return nCheck;
}

/* Perform sanity check on partition and load application description */
bool LTBootDriver_CheckOTAPartition(const LTBootInfo * pBootInfo, u32 nBootIdx) {
    u32 nAppDescOffset = pBootInfo->fw[nBootIdx].nByteOffset;
    nAppDescOffset    += sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    if (LTBootDriverFlash_ReadBytes(nAppDescOffset, sizeof(esp_app_desc_t), (u8 *)&s_appDesc)) {
        if (s_appDesc.magic_word == kLTBootFirmwareMagic) return true;
    }
    return false;
}

static LTBootSecurityCheck ProcessBootloaderDynasty(void) {
    const u32 nCurrentDynasty = kLTBootDynasty_CurrentSecurityDynasty;
    volatile LTBootSecurityCheck nCheck = kLTBootSecurityCheck_Pass - 0x22;
    /* Bootloader anti-rollback check */
    volatile u32 nRequiredDynasty = 0xffff;
    nCheck += 0x11 * (esp_efuse_read_field_cnt(ESP_EFUSE_BOOT_SECURE_VERSION, (size_t *)&nRequiredDynasty) == ESP_OK);
    if (nCurrentDynasty >= nRequiredDynasty) nCheck += 0x11;
    else return kLTBootSecurityCheck_Fail;
    if (nCurrentDynasty > nRequiredDynasty) {
        /* This bootloader's dynasty is now the lowest accepted dynasty
         * The versions are already limited in the configuration to the values between 0 and 16,
         * so we don't have to check here that the difference is less than or equal 16. */
        esp_efuse_write_field_cnt(ESP_EFUSE_BOOT_SECURE_VERSION, nCurrentDynasty - nRequiredDynasty);
        LTBootPlatform_DebugPrintf("Writing dynasty %u\n", nCurrentDynasty);
    }
    return nCheck;
}

LTBootSecurityCheck LTBootDriver_CheckBootloaderSecurity(void) {
    volatile LTBootSecurityCheck nCheck = kLTBootSecurityCheck_Fail;
    /* Only update application dynasty if prior boot into partition was successful */
    nCheck = ProcessBootloaderDynasty();
    if (nCheck != kLTBootSecurityCheck_Pass) {
        LTBootPlatform_printf("Insecure bootloader dynasty %u\n", kLTBootDynasty_CurrentSecurityDynasty);
        /* Bootloader dynasty abort is fatal (no failover) */
        LTBootPlatform_SecurityAbort();
    }
    LTBootPlatform_SecurityAssert(ProcessBootloaderDynasty() == kLTBootSecurityCheck_Pass);
    return nCheck;
}

static bool EnumeratePartitionsCallback(const LTDeviceFlash_PartitionEntry * pPartition, void * pClientData) {
    u32 * pOffset = (u32 *)pClientData;
    if (LTBootPlatform_strncmp(pPartition->name, "prov", kLTDeviceFlash_MaxTypeSize - 1) == 0) {
        *pOffset = pPartition->nByteOffset;
        return false;
    }
    return true;
}

bool LTBootDriver_GetDeviceID(u8 deviceID[kLTBootDeviceIDLength]) {
    u32 nByteOffset = (u32)-1;
    /* Find provisioning partition and read Device ID from it */
    LTBoot_EnumeratePartitions(EnumeratePartitionsCallback, (void *)&nByteOffset);
    if (nByteOffset != (u32)-1) {
        if (LTBootDriverFlash_ReadBytes(nByteOffset, kLTBootDeviceIDLength, deviceID)) {
            return true;
        }
    }
    return false;
}

static LTBootSecurityCheck ProcessApplicationDynasty(u32 nCurrentDynasty, bool bWriteEnable) {
    /* Field size AND maximum supported dynasty */
    const u32 nFieldSize = 16;
    volatile LTBootSecurityCheck nCheck = kLTBootSecurityCheck_Pass - 0x22 - nFieldSize;
    u32 nTemp = 0;
    nCheck += esp_efuse_get_field_size(ESP_EFUSE_SECURE_VERSION);
    nCheck += 0x11 * (esp_efuse_read_field_blob(ESP_EFUSE_SECURE_VERSION, &nTemp, nFieldSize) == ESP_OK);
    u32 nRequiredDynasty = __builtin_popcount(nTemp & ((1ULL << nFieldSize) - 1));
    /* Application anti-rollback check */
    if (nCurrentDynasty >= nRequiredDynasty) nCheck += 0x11;
    else return kLTBootSecurityCheck_Fail;
    if (bWriteEnable && nCurrentDynasty > nRequiredDynasty && nCurrentDynasty <= nFieldSize) {
        LTBootPlatform_DebugPrintf("Updating app dynasty to %u\n", nCurrentDynasty);
        /* NB: Only burn new bits */
        u32 nCurrentMask = (1ULL << nRequiredDynasty) - 1;
        u32 nNewMask     = (1ULL << nCurrentDynasty)  - 1;
        u32 nBurnMask    = nNewMask - nCurrentMask;
        esp_efuse_write_field_blob(ESP_EFUSE_SECURE_VERSION, &nBurnMask, nFieldSize);
    }
    return nCheck;
}

static LTBootSecurityCheck CheckAndUpdateApplicationDynasty(const LTBootInfo * pBootInfo) {
    volatile LTBootSecurityCheck nCheck = kLTBootSecurityCheck_Fail;
    /* Only update application dynasty if prior boot into partition was successful */
    nCheck = ProcessApplicationDynasty(s_appDesc.secure_version, pBootInfo->isBootStatusGood);
    if (nCheck != kLTBootSecurityCheck_Pass) {
        LTBootPlatform_printf("Insecure app dynasty %u\n", s_appDesc.secure_version);
        /* Application dynasty abort should result in failover */
        LTBootPlatform_SecurityAbortWithFailover();
    }
    LTBootPlatform_SecurityAssert(ProcessApplicationDynasty(s_appDesc.secure_version, false) == kLTBootSecurityCheck_Pass);
    return nCheck;
}

LTBootSecurityCheck LTBootDriver_AuthenticateLTAT(const LTBootInfo * pBootInfo) {
    u32 nOffsetLTAT = pBootInfo->ltat.nByteOffset;
    u8 imageDigest[32]    = { [ 0 ... 31 ] = 0xee };
    u8 verifiedDigest[32] = { [ 0 ... 31 ] = 0x33 };
    const u8 ltatTrustedKeyDigest[32] = {
#if defined(LT_BOOTLOADER_SECURITY_DEV_BYPASS) || !defined(LT_PLATFORM_ID)
    /* SHA256 digest for ltat_test_key.pem public key */
    0x74, 0x2c, 0xc9, 0xa0, 0x7a, 0x27, 0x70, 0x94, 0x3e, 0x9a, 0xcc, 0x3f, 0x60, 0x1a, 0x28, 0x39,
    0xb6, 0x61, 0xeb, 0xa8, 0x09, 0xac, 0xfd, 0x7f, 0x28, 0x6b, 0x49, 0x2b, 0x26, 0x4a, 0xeb, 0xd3
#elif LT_PLATFORM_ID == PLID_FK
    /* Dimmable white bulb */
    0x2f, 0xc8, 0x3c, 0x02, 0x71, 0x12, 0x16, 0xf7, 0xa9, 0x6c, 0x13, 0xca, 0x42, 0x5e, 0x42, 0xf6,
    0x7c, 0x81, 0xc8, 0x4c, 0x74, 0xc9, 0x6a, 0x2c, 0x40, 0x24, 0x21, 0xfb, 0xf9, 0x87, 0x28, 0xcd
#elif LT_PLATFORM_ID == PLID_FL
    /* Color bulb */
    0x7f, 0x8a, 0x2e, 0xc6, 0xc7, 0x6c, 0x63, 0x50, 0xc2, 0x99, 0x6b, 0xec, 0xfd, 0x43, 0x77, 0x4a,
    0x9d, 0x74, 0x9a, 0xc0, 0xc9, 0x3e, 0x9f, 0x3a, 0xa6, 0xda, 0x58, 0x69, 0x26, 0xbd, 0x38, 0x3d
#elif LT_PLATFORM_ID == PLID_FM
    /* Indoor color light strip (16ft) */
    0xe0, 0x48, 0x04, 0x58, 0x89, 0x7b, 0x02, 0x17, 0x06, 0xff, 0xc4, 0xa1, 0x5e, 0x88, 0x32, 0x55,
    0xc4, 0x71, 0x0e, 0x4f, 0x6d, 0xeb, 0x9f, 0x07, 0xe8, 0x26, 0x86, 0x67, 0x39, 0xfc, 0x1c, 0x0f
#elif LT_PLATFORM_ID == PLID_FP
    /* Indoor plug */
    0xfc, 0x29, 0x56, 0x40, 0x9f, 0x8b, 0xc2, 0xa7, 0xca, 0x06, 0x1b, 0xb4, 0xcb, 0x2c, 0x64, 0xaa,
    0xae, 0xab, 0x3a, 0x53, 0xe7, 0x13, 0xef, 0xd3, 0x7a, 0xb4, 0xc3, 0xe6, 0xf8, 0x2e, 0x9c, 0xf4
#elif LT_PLATFORM_ID == PLID_FR
    /* Outdoor plug */
    0x39, 0xea, 0xba, 0x65, 0x9c, 0xb8, 0xaa, 0x27, 0x38, 0x39, 0x6c, 0x4e, 0x9c, 0x10, 0x5b, 0xba,
    0x7c, 0x67, 0x05, 0xc7, 0xc0, 0x78, 0x31, 0xf0, 0xe3, 0xc3, 0x66, 0xb0, 0x6b, 0x60, 0xf7, 0x89
#elif LT_PLATFORM_ID == PLID_GC
    /* Light strip PRO (16ft) */
    0x39, 0x53, 0xdc, 0x31, 0xa6, 0x95, 0xc3, 0xbb, 0x88, 0xfc, 0x90, 0xcd, 0x80, 0x30, 0x58, 0x3e,
    0x54, 0x94, 0x79, 0x9f, 0x7c, 0xba, 0x41, 0x58, 0x76, 0x9d, 0x25, 0xd4, 0xfe, 0xcd, 0x71, 0xd9
#elif LT_PLATFORM_ID == PLID_GD
    /* Light strip PRO (32ft) */
    0x46, 0x7d, 0xf0, 0xd1, 0xe3, 0x7b, 0xb1, 0x4e, 0x4f, 0x71, 0x58, 0xc6, 0x1b, 0x77, 0xb1, 0xe7,
    0xe5, 0xd7, 0x69, 0x3d, 0xab, 0xf5, 0xff, 0x56, 0x27, 0xc8, 0xd0, 0x38, 0x4c, 0x1a, 0x89, 0xce
#elif LT_PLATFORM_ID == PLID_GL
    /* Indoor color light strip (32ft) */
    0x43, 0x50, 0xd0, 0x5a, 0xa6, 0xaa, 0xde, 0x7e, 0xee, 0x77, 0x4d, 0xb1, 0x58, 0x32, 0x8b, 0x2f,
    0x4e, 0xb3, 0x1d, 0x8b, 0xb5, 0x38, 0x9e, 0xff, 0x73, 0xbb, 0x38, 0x77, 0xd7, 0x94, 0x0c, 0xea
#else
    #error "Unknown Platform ID"
#endif
    };
    volatile LTBootSecurityCheck nCheck = kLTBootSecurityCheck_Pass - SBV2_SUCCESS;
    if (bootloader_sha256_flash_contents(nOffsetLTAT, sizeof(LTSecurityLTATPayload), imageDigest) == ESP_OK) {
        void * pSig = (u8 *)bootloader_mmap(nOffsetLTAT + sizeof(LTSecurityLTATPayload), sizeof(ets_secure_boot_signature_t));
        u32 nReturn = ets_secure_boot_verify_signature(pSig, imageDigest, ltatTrustedKeyDigest, verifiedDigest);
        bootloader_munmap(pSig);
        LTBootPlatform_SecurityAssert(nReturn == SBV2_SUCCESS);
        nCheck += nReturn;
        LTBootPlatform_SecurityAssert(memcmp(verifiedDigest, imageDigest, 32) == 0);
    }
    return nCheck;
}

LTBootSecurityCheck LTBootDriver_LoadAndAuthenticateApplication(const LTBootInfo * pBootInfo) {
    esp_partition_pos_t part;
    part.offset = pBootInfo->fw[pBootInfo->nBootIdx].nByteOffset;
    part.size   = pBootInfo->fw[pBootInfo->nBootIdx].nSizeInBytes;
    volatile LTBootSecurityCheck nCheck = kLTBootSecurityCheck_Pass - 0x22;
    nCheck += 0x11 * (CheckAndOrLoadImage(ESP_IMAGE_LOAD, &part, &s_imageData) == ESP_OK);
    if (nCheck != kLTBootSecurityCheck_Pass - 0x11) {
        LTBootPlatform_printf("Firmware load failure\n");
        LTBootPlatform_SecurityAbortWithFailover();
    }
    nCheck += 0x11 * (CheckAndUpdateApplicationDynasty(pBootInfo) == kLTBootSecurityCheck_Pass);
    return nCheck;
}

void LTBootDriver_RunApplication(const LTBootInfo * pBootInfo) {
    LT_UNUSED(pBootInfo);
    BootApplication(&s_imageData);
}

u32 LTBootDriverFlash_GetBytesPerSector(void) {
    return FLASH_SECTOR_SIZE;
}

u16 LTBootDriverFlash_GetWriteQuantum(void) {
    return kLTBootWriteQuantum;
}

bool LTBootDriverFlash_GetPartitionTableOffset(u32 * pByteOffsetToSet, bool bGetPrimary) {
    if (bGetPrimary) *pByteOffsetToSet = kLTBootPrimaryPartitionTableOffset;
    else *pByteOffsetToSet = kLTBootBackupPartitionTableOffset;
    return true;
}

bool LTBootDriverFlash_ReadBytes(u32 nByteOffset, u32 nNumBytes, u8 * pBuff) {
    u8 * pMap = (u8 *)bootloader_mmap(nByteOffset, nNumBytes);
    if (!pMap) return false;
    memcpy(pBuff, pMap, nNumBytes);
    bootloader_munmap(pMap);
    return true;
}
