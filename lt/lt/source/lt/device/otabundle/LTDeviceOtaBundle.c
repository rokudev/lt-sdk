/*******************************************************************************
 * LTDeviceOtaBundle.c: Multi-asset OTA driver
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/otabundle/LTDeviceOtaBundle.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>
#include <lt/system/settings/LTSystemSettings.h>

DEFINE_LTLOG_SECTION("dev.ota");
#define OTABUNDLE_DO_DLOG    1
#if     OTABUNDLE_DO_DLOG
#define DLOG                LTLOG
#else
#define DLOG                LTLOG_LOGNULL
#endif

/** Standard LT Interfaces ****************************************************/

enum {
    kMaxNumAssets   = 31,
    kManifest_Magic = 0x554D544C
};

typedef enum {
    kAssetState_PendingUpdate,
    kAssetState_UpdateApplied,
    kAssetState_UpdateValidated,
} AssetState;

typedef struct {
    u32  assetOffset;
    u32  assetSize;
    char assetName[16];
    char version[20];
    u8   hash[SHA256_HASH_LENGTH];
    u8   initData[52];
} AssetInfo;

LT_STATIC_ASSERT_SIZE_32_64(AssetInfo, 128, 128);

typedef struct {
    u32 magic;
    u32 numAssets;
    AssetInfo assets[];
} AssetManifest;

LT_STATIC_ASSERT_SIZE_32_64(AssetManifest, 8, 8);

typedef struct {
    AssetState          state;
    u32                 index;
    LTDriverOtaStorage *storageDriver;
} AssetDownloadContext;

typedef enum {
    kState_ReadManifest,
    kState_GetAssetVersion,
    kState_InitAssetUpdate,
    kState_ReadAssetData,
    kState_FinalizeAssetUpdate,
    kState_Reboot,
    kState_Done,
    kState_Error,
} State;

typedef struct {
    const char *assetName;
    const char *objectName;
} AssetHandler;

static State                s_state;
static AssetManifest       *s_manifest;
static AssetDownloadContext s_curAsset;
static u32                  s_numAssetHandlers = 0;
static AssetHandler        *s_assetHandlers = NULL;
static bool                 s_force;
static LTSystemSettings    *s_settings;

static void SetCurAssetState(const char *assetName, AssetState state) {
    s_curAsset.state = state;
    LTString assetStateKey = NULL;
    ltstring_format(&assetStateKey, "ota/%s/state", assetName);
    s_settings->SetIntegerValue(assetStateKey, (s64)s_curAsset.state);
    ltstring_destroy(assetStateKey);
}

static bool LTDeviceOtaBundle_IsValidated(void) {
    return true;
}

static bool LTDeviceOtaBundle_MarkValidated(void) {
    return true;
}

static bool LTDeviceOtaBundle_InitDownload(u32 imageSize, bool force) {
    LT_UNUSED(imageSize);
    s_state = kState_ReadManifest;
    s_force = force;
    s_curAsset.index = 0;
    s_curAsset.storageDriver = NULL;
    return true;
}

static void RebootProc(void *clientData) {
    LT_UNUSED(clientData);
    LTDeviceWatchdog *watchdog = lt_openlibrary(LTDeviceWatchdog);
    watchdog->Reboot();
}

static bool LTDeviceOtaBundle_GetNextRequest(LTOtaRequest *request) {
    switch (s_state) {
        case kState_ReadManifest:
            DLOG("req.manifest.slc", "Requesting manifest slice: 1024 bytes @ 0");
            *request = (LTOtaRequest) {
                .type = kLTOtaRequestType_SendImageSlice,
                .sliceParams = {
                    .offset = 0,
                    .size = 1024
                }
            };
            break;

        case kState_GetAssetVersion:
        case kState_InitAssetUpdate:
        case kState_FinalizeAssetUpdate:
            DLOG("req.noop", NULL);
            *request = (LTOtaRequest) { .type = kLTOtaRequestType_Nop };
            break;

        case kState_ReadAssetData: {
            AssetInfo *assetInfo = &s_manifest->assets[s_curAsset.index];
            DLOG("req.asset.slc", "Requesting asset slice: %lu bytes @ %02lx", LT_Pu32(assetInfo->assetSize), LT_Pu32(assetInfo->assetOffset));
            *request = (LTOtaRequest) {
                .type = kLTOtaRequestType_SendImageSlice,
                .sliceParams = {
                    .offset = assetInfo->assetOffset,
                    .size = assetInfo->assetSize
                }
            };
            break;
        }

        case kState_Reboot: {
            DLOG("req.reboot", "Requesting reboot to apply asset");
            *request = (LTOtaRequest) {
                .type = kLTOtaRequestType_WaitForReboot,
                .rebootParams = {
                    .minDelayMs = 1000,
                    .maxDelayMs = 22000
                }
            };
            LTOThread *thread =  LT_GetCore()->GetCurrentThreadObject();
            thread->API->SetTimer(thread, LTTime_Milliseconds(500), RebootProc, NULL, NULL);
            break;
        }

        case kState_Error:
            DLOG("req.err", NULL);
            *request = (LTOtaRequest) { .type = kLTOtaRequestType_Error };
            return false;

        case kState_Done:
            *request = (LTOtaRequest) { .type = kLTOtaRequestType_Nop };
            return false;
    }

    return true;
}

static const char *FindAssetStorageDriver(const char *assetName) {
    for (u32 i = 0; i < s_numAssetHandlers; ++i) {
        if (lt_strcmp(s_assetHandlers[i].assetName, assetName) == 0) {
            return s_assetHandlers[i].objectName;
        }
    }
    LTLOG_YELLOWALERT("no.asset.stg", "No asset storage driver found for asset: %s", assetName);
    return NULL;
}

static bool StartAssetDownload(u32 assetIdx);

// called in OnAssetVersion
static void OnInitDownloadComplete(LTDriverOtaStorage *otas, LTDeviceOta_Status initResult) {
    LT_UNUSED(otas);

    AssetInfo *assetInfo = &s_manifest->assets[s_curAsset.index];
    if (initResult == kLTDeviceOta_Status_Ok) {
        LTLOG("init.do.update", "Updating asset '%s' to version %s", assetInfo->assetName, assetInfo->version);
        s_state = kState_ReadAssetData;
    } else {
        LTLOG_YELLOWALERT("init.err", "Asset update initialization failure - cannot continue");
        lt_destroyobject(s_curAsset.storageDriver);
        s_curAsset.storageDriver = NULL;
        s_state = kState_Error;
    }
}

// called in OnAssetVersion
static void DownloadNextAsset(void *clientData) {
    LT_UNUSED(clientData);
    if (!StartAssetDownload(++s_curAsset.index)) {
        LTLOG("bad.asset.st", NULL);
        s_state = kState_Error;
    }
}

// in swup.worker, queued after otas->API->GerVersion
static void OnAssetVersion(LTDriverOtaStorage *otas, const char *curVersion, bool bValidated) {
    if (otas != s_curAsset.storageDriver) LTLOG_YELLOWALERT("otas.mismatch", "cur index %u", (u16)s_curAsset.index);

    AssetInfo *assetInfo = &s_manifest->assets[s_curAsset.index];
    DLOG("asset.ver", "%s %s/%u -> %s/%u", assetInfo->assetName, curVersion, bValidated, assetInfo->version, s_curAsset.state);

    bool versionMatch = (lt_strcmp(curVersion, assetInfo->version) == 0);
    if (!versionMatch) {
        SetCurAssetState(assetInfo->assetName, kAssetState_PendingUpdate);
    }

    if (s_curAsset.state == kAssetState_UpdateApplied) {
        if (bValidated) {
            SetCurAssetState(assetInfo->assetName, kAssetState_UpdateValidated);
        } else {
            SetCurAssetState(assetInfo->assetName, kAssetState_PendingUpdate);
            // TODO track and limit attempts
        }
    }

    if ((s_curAsset.state == kAssetState_UpdateValidated) || (!s_force && versionMatch)) {
        LTLOG("init.skip.update", "Asset %s up to date %s/%u", assetInfo->assetName, curVersion, s_curAsset.state);
        lt_destroyobject(s_curAsset.storageDriver);
        s_curAsset.storageDriver = NULL;

        if (s_curAsset.index < s_manifest->numAssets - 1) {
            LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
            thread->API->QueueTaskProc(thread, DownloadNextAsset, NULL, NULL);
        } else {
            LTLOG("ver.ota.comp", "All asset updates completed");
            lt_free(s_manifest);
            s_manifest = NULL;
            s_state = kState_Done;
            s_settings->DeleteSettingsWithPrefix("ota/");
        }
    } else {
        DLOG("init.asset.upd", "Starting update of asset '%s' from version %s to version %s %s", assetInfo->assetName, curVersion, assetInfo->version, s_force ? "(forced)" : "");
        s_state = kState_InitAssetUpdate;
        // After init, OnInitDownloadComplete is called directly, not a queued proc.
        s_curAsset.storageDriver->API->InitDownload(s_curAsset.storageDriver, assetInfo->assetName, assetInfo->version, assetInfo->assetSize, OnInitDownloadComplete);
        // On failure, s_curAsset.storageDriver will be destroyed in OnInitDownloadComplete.
    }
}

// in swup.worker
static bool StartAssetDownload(u32 assetIdx) {
    AssetInfo *assetInfo = &s_manifest->assets[assetIdx];
    do {
        const char *driverName;
        if (!(driverName = FindAssetStorageDriver(assetInfo->assetName))) break;
        if (!(s_curAsset.storageDriver = (LTDriverOtaStorage *)lt_createobject_named("LTDriverOtaStorage", driverName))) break;

        s64 state;
        LTString assetStateKey = NULL;
        ltstring_format(&assetStateKey, "ota/%s/state", assetInfo->assetName);
        if (s_settings->GetIntegerValue(assetStateKey, &state)) {
            s_curAsset.state = state;
        } else {
            SetCurAssetState(assetInfo->assetName, kAssetState_PendingUpdate);
        }
        ltstring_destroy(assetStateKey);

        DLOG("check.asset.ver", "Checking asset '%s' current version", assetInfo->assetName);
        s_state = kState_GetAssetVersion;

        // After get version, OnAssetVersion is queued as a proc to execute next in this thread.
        // If a new version is available, the download will be started by OnAssetVersion.
        if (!s_curAsset.storageDriver->API->GetVersion(s_curAsset.storageDriver, assetInfo->assetName, OnAssetVersion)) break;
        return true;
    } while(false);

    if (s_curAsset.storageDriver) {
        lt_destroyobject(s_curAsset.storageDriver);
        s_curAsset.storageDriver = NULL;
    }
    LTLOG_YELLOWALERT("start.asset.upd.err", NULL);
    return false;
}

// called in otas->API->Finalize
static void OnAssetFinalized(LTDriverOtaStorage *otas, LTDeviceOta_Status result) {
    if (otas != s_curAsset.storageDriver) LTLOG_YELLOWALERT("otas.mismatch", "cur index %u", (u16)s_curAsset.index);
    lt_destroyobject(s_curAsset.storageDriver);
    s_curAsset.storageDriver = NULL;

    if (result == kLTDeviceOta_Status_NeedReboot) {
        AssetInfo *assetInfo = &s_manifest->assets[s_curAsset.index];
        SetCurAssetState(assetInfo->assetName, kAssetState_UpdateApplied);
        LTLOG("asset.end.needreboot", NULL);
        s_state = kState_Reboot;
        return;
    } else if (result == kLTDeviceOta_Status_Error) {
        LTLOG("asset.end.err", NULL);
        s_state = kState_Error;
        return;
    }

    LTLOG_YELLOWALERT("finalize.fail", "why here?!!I");
}

// in swup.worker
static bool LTDeviceOtaBundle_SaveSliceBlock(const u8 *data, u32 dataLen, u32 offsetToSave) {
    switch (s_state) {
        case kState_ReadManifest:
            DLOG("read.manifest", NULL);
            // save manifest data
            AssetManifest *receivedManifest = (AssetManifest *)data;
            if (receivedManifest->magic != kManifest_Magic) {
                LTLOG("bad.magic", "%08lX", LT_Pu32(receivedManifest->magic));
                return false;
            }
            if (receivedManifest->numAssets >= kMaxNumAssets) {
                LTLOG("bad.n.assets", NULL);
                return false;
            }
            u32 manifestSize = sizeof(AssetManifest) + sizeof(AssetInfo) * receivedManifest->numAssets;
            if (manifestSize > dataLen) {
                LTLOG("bad.mnfst.sz", NULL);
                return false;
            }

            s_manifest = lt_malloc(manifestSize);
            if (!s_manifest) {
                LTLOG("bad.mnfst.alloc", NULL);
                return false;
            }
            lt_memcpy(s_manifest, receivedManifest, manifestSize);

            if (!StartAssetDownload(s_curAsset.index = 0)) {
                LTLOG("bad.asset.st.0", NULL);
                lt_free(s_manifest);
                s_manifest = NULL;
                s_state = kState_ReadManifest;  // Set back to read manifest
                return false;
            }
            break;

        case kState_ReadAssetData:
            AssetInfo *assetInfo = &s_manifest->assets[s_curAsset.index];
            u32 remainingBytes = assetInfo->assetSize - offsetToSave;
            u32 bytesToWrite = LT_MIN(dataLen, remainingBytes);
            if (s_curAsset.storageDriver) {
                if (!s_curAsset.storageDriver->API->SaveBlock(s_curAsset.storageDriver, data, bytesToWrite, offsetToSave)) {
                    return false;
                };
            }

            offsetToSave += bytesToWrite;
            if (offsetToSave >= assetInfo->assetSize) {
                DLOG("asset.dl.comp", "Asset download complete: %lu/%lu", LT_Pu32(offsetToSave), LT_Pu32(assetInfo->assetSize));

                s_state = kState_FinalizeAssetUpdate;
                s_curAsset.storageDriver->API->Finalize(s_curAsset.storageDriver, assetInfo->assetSize, assetInfo->hash, OnAssetFinalized);
            }
            break;

        case kState_GetAssetVersion:     // shouldn't receive data while checking asset version
        case kState_InitAssetUpdate:     // shouldn't receive data while initializing asset update
        case kState_FinalizeAssetUpdate: // shouldn't receive data while finalizing asset update
        case kState_Reboot:              // shouldn't receive more data while waiting to reboot
        case kState_Done:                // shouldn't have received more data blocks after completion - move to error state
            LTLOG("extra.data", NULL);
            /* Intentional fall-through */

        case kState_Error:
            LTLOG("state.error", NULL);
            s_state = kState_Error;
            return false;
    }

    return true;
}

// in swup.worker
static void LTDeviceOtaBundle_Complete(void) {
    DLOG("complete", "%p", s_curAsset.storageDriver);
    if (s_curAsset.storageDriver) {
        lt_destroyobject(s_curAsset.storageDriver);
        s_curAsset.storageDriver = NULL;
    }
    if (s_manifest) {
        lt_free(s_manifest);
        s_manifest = NULL;
    }
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void ShutdownOta(void) {
    lt_free(s_assetHandlers);
    s_assetHandlers = NULL;
    s_numAssetHandlers = 0;
    lt_closelibrary(s_settings);
}

static bool LTDeviceOtaBundleImpl_LibInit(void) {
    lt_memset(&s_curAsset, 0, sizeof(s_curAsset));
    do {
        LTDeviceConfig *deviceConfig;
        u32 driverSection;
        if (!(s_settings = lt_openlibrary(LTSystemSettings))) break;
        if (!(deviceConfig = lt_openlibrary(LTDeviceConfig))) break;

        for (u32 driverNum = 0; driverNum < deviceConfig->GetNumDrivers("LTDeviceOtaBundle"); ++driverNum) {
            const char *driverName, *objectName;
            if (!(driverName = deviceConfig->GetDriverAt("LTDeviceOtaBundle", driverNum)))          break;
            if (!(driverSection = deviceConfig->GetDriverSection("LTDeviceOtaBundle", driverName))) break;
            if (!(objectName = deviceConfig->ReadString(driverSection, "object")))                  break;
            u32 numUnits;
            if (!(numUnits = deviceConfig->GetNumDeviceUnits(driverSection)))                       break;

            s_assetHandlers = lt_realloc(s_assetHandlers, (s_numAssetHandlers + numUnits) * sizeof(AssetHandler));
            for (u32 unit = 0; unit < numUnits; ++unit) {
                u32 unitSection = deviceConfig->GetDeviceUnitSectionAt(driverSection, unit);
                const char *assetName  = deviceConfig->ReadString(unitSection, "name");
                s_assetHandlers[s_numAssetHandlers + unit].assetName  = assetName;
                s_assetHandlers[s_numAssetHandlers + unit].objectName = objectName;
            }
            s_numAssetHandlers += numUnits;
        }
        lt_closelibrary(deviceConfig);
        return true;
    } while (0);

    ShutdownOta();
    LTLOG_YELLOWALERT("error", "fail to init");
    return false;
}

static void LTDeviceOtaBundleImpl_LibFini(void) {
    ShutdownOta();
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/
define_LTLIBRARY_ROOT_INTERFACE(LTDeviceOtaBundle) {
    .IsValidated      = &LTDeviceOtaBundle_IsValidated,
    .MarkValidated    = &LTDeviceOtaBundle_MarkValidated,
    .InitDownload     = &LTDeviceOtaBundle_InitDownload,
    .GetNextRequest   = &LTDeviceOtaBundle_GetNextRequest,
    .SaveSliceBlock   = &LTDeviceOtaBundle_SaveSliceBlock,
    .Complete         = &LTDeviceOtaBundle_Complete,
} LTLIBRARY_DEFINITION;

/**********************************************************************************
 *  LOG
 **********************************************************************************
 *  26-Jan-25   trajan      created
 */
