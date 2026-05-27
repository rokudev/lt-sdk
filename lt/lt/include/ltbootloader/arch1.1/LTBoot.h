/*******************************************************************************
 * LTBoot.h                                                        LT Bootloader
 *                                                                (Arch Ver 1.1)
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef __LTBOOT_H_
#define __LTBOOT_H_

#include "lt/LTTypes.h"
#include "lt/core/LTSecurity.h"
#include "lt/device/flash/LTDeviceFlash.h"

#include "LTBootPlatform.h"
#include "LTBootStdlib.h"

#define LTBOOT_WEAK         __attribute__((weak))

enum {
    kLTBootNumFirmwarePartitions = 2,                           /**< Two firmware partitions: ping and pong */
    kLTBootDeviceIDLength        = kLTSecurity_DeviceIDLength,  /**< Device ID */
};

typedef u32 LTBootSecurityCheck;
enum LTBootSecurityCheck {
    kLTBootSecurityCheck_Pass      = 0x00013370,
    /**< Any other value is failure, but it might be better to reset rather
     *   than to return a different value. */
    kLTBootSecurityCheck_Fail      = 0xca110304,
};

/* Location of a single partition */
typedef struct {
    u32 nByteOffset;   /**< Byte offset of partition from the beginning of flash */
    u32 nSizeInBytes;  /**< Size of partition in bytes (multiple of the sector size) */
} LTBootPartition;

/* Boot information including partitions */
typedef struct {
    LTBootPartition  otainfo;                          /**< OTA information partition */
    LTBootPartition  ltat;                             /**< LTAT partition */
    LTBootPartition  fw[kLTBootNumFirmwarePartitions]; /**< Firmware partitions */
    LTBootReason     bootReason;                       /**< Boot reason */
    u16              nSectorSize;                      /**< Flash sector size in bytes */
    u8               usePrimaryTable;                  /**< True if using primary partition table */
    u8               nNumPartitions;                   /**< Number of partitions */
    u8               nNumFwPartitions;                 /**< Number of firmware partitions found */
    u8               nBootIdx;                         /**< Firmware index to boot from */
    u8               isBootStatusGood;                 /**< True if prior boot into this partition was successful */
} LTBootInfo;

/* Offset from start of image of signature block */
typedef u32 LTApplicationSigOffset;
enum LTAppplicationSigOffset {
    kLTApplicationSigOffset_None = 0,   /**< Signature block is NOT provided */
    /* Values 1 through 32 are reserved */
};

/* LT Application Header (Optional)
 *
 * Application signature check includes header and is checked over byte interval:
 *     [0, sizeof(LTApplicationHeader) + appHeader.imageSize - 1]
 * Signature block:
 *     appHeader.sigOffset is the offset of the "signature block." A signature block includes the signature
 *     along with required meta-data such as hardware accelerator values, nonces, public-keys, etc.  */

/* Application Header */
typedef struct {
    u32                     magic;        /**< Magic number used for sanity checking in partition. */
    u32                     imageSize;    /**< Size of image (NOT including header and NOT including signature block). */
    u32                     rsvd0[4];
    LTApplicationSigOffset  sigOffset;    /**< Signature block offset from image start NOT including header or 0 if not present. */
    u8                      appDynasty;   /**< Application dynasty (optional). */
    u8                      rsvd1[3];
} LTApplicationHeader;

LT_STATIC_ASSERT_SIZE_32_64(LTApplicationHeader, 32, 32);

typedef bool (LTBoot_PartitionEnumCallback)(const LTDeviceFlash_PartitionEntry * pPartition, void * pClientData);
/**< Flash partition enumeration callback function.
 * NB: Partition data is transitory and must be copied from the provided pointer.
 *
 * @param[in] pPartition Partition data.
 * @param[in] pClientData Pointer to private data supplied to call to EnumeratePartitions().
 * @return true to continue enumeration process.
 * @return false to abort enumeration process. */

bool LTBoot_EnumeratePartitions(LTBoot_PartitionEnumCallback * pCallback, void * pClientData);
/**< Invoke provided callback function for each partition on the flash device.
 * NB: The provided callback MUST NOT alter the partition table on flash.
 * May be invoked from LTBootDriver implementation.
 *
 * @param[in] pCallback Callback function invoked for each partition.
 * @param[in] pClientData Client-provided data to supply to callback function.
 * @return true if fully enumerated: all callbacks returned true.
 * @return false if enumeration aborted early: a callback returned false OR an error occurred. */

LTBootSecurityCheck LTBoot_CheckLTATClaims(LTSecurityClaimMask mask);
/**< Check bootloader security claims. Bootloader claims are stored in the first claim group.
 * May be invoked from the LTBootDriver implementation.
 *
 * @param[in] mask Claim bitmask to check.
 * @return kLTBootSecurityCheck_Pass if all claims set in the mask are also present in LTAT. */

u32 LTBoot_ProcessApplicationHeader(const LTApplicationHeader * pHeader, u32 magic, u32 sigBlockSize);
/**< Sanity Check LT Application Header and return size.
 * May be invoked from the LTBootDriver implementation.
 * NB: Will not return if header does not pass sanity checking.
 *
 * @param[in] pHeader Pointer to application header to check.
 * @param[in] magic Expected firmware magic number.
 * @param[in] sigBlockSize Size of signature block in bytes.
 * @return size in bytes of firmware image on flash including header and signature (if it exists). */

void LTBoot_Run(LTBootReason reason, const char * pReason) LT_NORETURN;
/**< Run LTBoot executive.
  * Invoked at bootloader startup after first performing C runtime and hardware initialization.
  * This function does NOT return.
  *
  * @param[in] reason LT boot reason.
  * @param[in] pString Optional boot reason string to print on console, set to NULL to not print the string. */

#endif

