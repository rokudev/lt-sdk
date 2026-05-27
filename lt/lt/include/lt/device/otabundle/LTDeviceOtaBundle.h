/*******************************************************************************
 * lt/device/otabundle/LTDeviceOtaBundle.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_OTABUNDLE_LTDEVICEOTABUNDLE_H
#define LT_INCLUDE_LT_DEVICE_OTABUNDLE_LTDEVICEOTABUNDLE_H

#include <lt/LTTypes.h>
#include <lt/system/crypto/LTSystemCrypto.h>
LT_EXTERN_C_BEGIN

typedef enum {
    kLTOtaRequestType_Nop,
    kLTOtaRequestType_SendImageSlice,
    kLTOtaRequestType_WaitForReboot,
    kLTOtaRequestType_Error,
} LTOtaRequestType;

typedef struct {
    LTOtaRequestType type;
    union {
        struct {
            u32 offset;
            u32 size;
        } sliceParams;
        struct {
            u32 minDelayMs;
            u32 maxDelayMs;
        } rebootParams;
    };
} LTOtaRequest;

typedef enum {
    kLTDeviceOta_Status_Ok,
    kLTDeviceOta_Status_Error,
    kLTDeviceOta_Status_NeedReboot,
} LTDeviceOta_Status;

/**
 * @brief LTDeviceOtaBundle Library Root Interface.
 */
typedef_LTLIBRARY_ROOT_INTERFACE(LTDeviceOtaBundle, 1) {
    bool (*IsValidated)(void);
    bool (*MarkValidated)(void);
    bool (*InitDownload)(u32 imageSize, bool force);
    bool (*GetNextRequest)(LTOtaRequest *request);
    
    bool (*SaveSliceBlock)(const u8 *data, u32 dataLen, u32 offsetToSave);
    /**< Save a slice block.
     *   @param data          The data block of a slice to save
     *   @param dataLen       The data length
     *   @param offsetToSave  The offset of storage to save the data
     *   @return  true on success
     *            false on failure
     */

    void (*Complete)(void);
    /**< Complete after either a failed or successful update. */
} LTLIBRARY_INTERFACE;

typedef struct LTDriverOtaStorage LTDriverOtaStorage;
typedef void (LTDriverOtaStorage_GetVersionCallback)(LTDriverOtaStorage *otas, const char *version, bool bValidated);
/**< Callback proc after getting version. */
typedef void (LTDriverOtaStorage_StatusCallback)(LTDriverOtaStorage *otas, LTDeviceOta_Status status);
/**< Callback for InitDownload and Finalize. */

typedef struct {
    LTDriverOtaStorage                    *otas;
    char                                  *version;
    bool                                   validated;
    LTDriverOtaStorage_GetVersionCallback *getVersionCallback;
} LTDriverOtaStorage_VersionData;

/* All APIs are synchronous and deterministic with timeout.
 * The timeout is determined by implementation of specific LTDriverOtaStorage.
 * All APIs are called sequentially in a single dedicated thread (e.g. swup.worker). */
typedef_LTObject(LTDriverOtaStorage, 1) {
    bool (*GetVersion)(LTDriverOtaStorage *otas, const char *assetName, LTDriverOtaStorage_GetVersionCallback *getVersionCallback);
    /**< Get version.
     *   @param otas                The storage object
     *   @param assetName           The asset name
     *   @param getVersionCallback  The callback queued to execute after getting version. This proc avoids recursive calls in GetVersion as well as safe to destroy otas if necessary.
     *   @return  true on success
     *            false on failure or timeout
     */
    
    void (*InitDownload)(LTDriverOtaStorage *otas, const char *assetName, const char *newVersion, u32 imageSize, LTDriverOtaStorage_StatusCallback *initCallback);
    /**< Initialize download.
     *   @param otas          The storage object
     *   @param assetName     The asset name
     *   @param newVersion    The new version  
     *   @param imageSize     The image size
     *   @param initCallback  The callback to handle the result of init download. This is not a proc.
     *   @note  No return. Failure is handled by initCallback.
     */

    bool (*SaveBlock)(LTDriverOtaStorage *otas, const u8 *data, u32 dataLen, u32 offsetToSave);
    /**< Save a data block.
     *   @param otas          The storage object
     *   @param data          The data to save
     *   @param dataLen       The length of data, in Bytes
     *   @param offsetToSave  The offset of the storage to save data
     *   @return true on success
     *           false on failure or timeout
     */

    void (*Finalize)(LTDriverOtaStorage *otas, u32 imageSize, u8 imageHash[SHA256_HASH_LENGTH], LTDriverOtaStorage_StatusCallback *finalizeCallback);
    /**< Finalize update.
     *   @param otas              The storage object
     *   @param imageSize         The image size
     *   @param imageHash         The image hash
     *   @param finalizeCallback  The callback to handle the finalize result. This is not a proc.
     *   @note  No return. Failure is handled by finalizeCallback.
     */
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* LT_INCLUDE_LT_DEVICE_OTABUNDLE_LTDEVICEOTABUNDLE_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  26-Jan-25   trajan      created
 */
