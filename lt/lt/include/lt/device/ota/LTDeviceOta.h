/*******************************************************************************
 * lt/device/ota/LTDeviceOta.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_OTA_LTDECVICEOTA_H
#define LT_INCLUDE_LT_DEVICE_OTA_LTDECVICEOTA_H

#include <lt/LTTypes.h>
#include <lt/system/crypto/LTSystemCrypto.h>
LT_EXTERN_C_BEGIN

/**
 * @brief LTDeviceOta Library Root Interface.
 */
typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceOta, 1) {

    // Step 1, initialize OTA internal if any.
    bool (*Init)(void);
    /**< Initialize and prepre OTA for swup */

    // Step 2, check if OTA storage is good for taking a new OTA image.
    bool (*IsValidated)(void);
    /**< Check if storage is in good for swup */

    // Step 3, SWUP occurs, manifest and image header are downloaded and verified */

    // Step 4, check if storage is larger than the image size indicated in image header.
    bool (*CheckStorage)(u32 imageSize);
    /**< Check if storage is larger than the image size */

    // Step 5, prepare storage, such as erase old data or open a file.
    bool (*PrepareStorage)(u32 imageSize);
    /**< Prepare storage for image */

    // Step 6, download data blocks of OTA image and save to storage.
    bool (*SaveBlock)(const u8 *data, u32 dataLen, u32 offsetToSave);
    /**< Save a block
     *   @param[in]  data          The data in block
     *   @param[in]  dataLen       The block length, in Bytes
     *   @param[in]  offsetToSave  The offset of the storage where the data block should be saved.
     */

    // Step 7, indicate image downloading is complete and verify the image in storage.
    bool (*VerifyImage)(u32 imageSize, const u8 imageHash[SHA256_HASH_LENGTH]);
    /**< Verify the update image */

    // Step 8, apply update
    bool (*ApplyUpdate)(const char *version);
    /**< Invoke the OTA mechanism to update*/

    // Step 9, complete and cleanup if any
    void (*Complete)(void);
    /**< Complete after either a failed or successful update. */

    // Other helper operations.

    bool (*MarkValidated)(void);
    /**< Mark OTA storage good for swup */

} LTLIBRARY_INTERFACE;

typedef_LTLIBRARY_INTERFACE(ILTOta, 1) {

    bool (*Init)(void);
    bool (*IsValidated)(void);
    bool (*CheckStorage)(u32 imageSize);
    bool (*PrepareStorage)(u32 imageSize);
    bool (*SaveBlock)(const u8 *data, u32 dataLen, u32 offsetToSave);
    bool (*VerifyImage)(u32 imageSize, const u8 imageHash[SHA256_HASH_LENGTH]);
    bool (*ApplyUpdate)(const char *version);
    void (*Complete)(void);
    bool (*MarkValidated)(void);

} LTLIBRARY_INTERFACE;

/* OTA firmware image Header */
enum {
    kLTDeviceOta_ImageHeader_Magic     = 0x55534B52,
    kLTDeviceOta_ImageHeader_SeedLen   = 4,
    kLTDeviceOta_ImageHeader_NonceLen  = 16,
};

typedef struct LTDeviceOta_ImageHeader {
    /**< Plaintext Portion */
    u32  magic;                                       /**< Magic Number */
    u8   seed[kLTDeviceOta_ImageHeader_SeedLen];      /**< Root Firmware Encryption Key Seed */
    u8   nonce[kLTDeviceOta_ImageHeader_NonceLen];    /**< Nonce used to create FW encryption key and initial IV */
    /**< Encrypted Portion */
    struct {
        char version[16];                             /**< Firmware version */
        u16  blockCount;                              /**< Total number of ciphertext blocks */
        u16  blockSize;                               /**< Size of a full encrypted block including 16-byte auth tag */
        u32  plaintextSize;                           /**< Total length of plaintext firmware image */
        u8   plaintextHash[SHA256_HASH_LENGTH];       /**< SHA256 hash of plaintext firmware image */
    } encrypted;
    u8 authTag[AES128_GCM_MAX_TAG_LENGTH];            /**< AES-GCM authentication tag of encrypted portion */
} LTDeviceOta_ImageHeader;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceOta_ImageHeader, 96, 96)

/* PingPong OTA partition type */
typedef u8 PingPongOtaPartitionType;
enum {
    kPingPongOtaPartitionType_Manufacturing = 0x00,
    kPingPongOtaPartitionType_Customer      = 0xff
};

/* PingPong OTA partition status */
typedef u32 PingPongOtaPartitionStatus;
enum {
    kPingPongOtaPartitionStatus_Good        = 0x444f4f47,
    kPingPongOtaPartitionStatus_Unknown     = 0xffffffff
};

/* PingPong OTA partition information
 *  Two instances of OTA partition information are stored, one for ping, one for pong.
 *  Each instance is stored starting at a flash sector boundary in the ota data partition.
 */
enum {
    kPingPongOta_UpdatePartition_Magic      = 0x4941544f,  /**< Magic number for OTA data */
    kPingPongOta_StatusOffset               = 256,         /**< Offset from start of info where status is stored */
    kPingPongOta_FirmwareBufLen             = 4096,        /**< Firmware copy buffer size */
};
typedef struct {
    u32                       nMagic;   /**< Magic Number */
    u32                       nCount;   /**< Update partition with higher count is tried first */
    char                      ver[16];  /**< Firmware version */
    PingPongOtaPartitionType  type;     /**< Partition type (manufacturing or customer-facing) */
    u8                        rsvd[3];  /**< Reserved (set to zero) */
    u32                       nCRC;     /**< CRC for nMagic through rsvd */
    /* ... pad to status offset ...
     *       status offset is a power-of-2 and will therefore be a multiple of the flash write quantum ...
     * PingPongOtaPartitionStatus  status; */
} PingPongOta_PartitionInformation;

LT_STATIC_ASSERT_SIZE_32_64(PingPongOta_PartitionInformation, 32, 32)

LT_EXTERN_C_END
#endif /* LT_INCLUDE_LT_DEVICE_OTA_LTDECVICEOTA_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  07-Aug-23   gallienus   created
 */
