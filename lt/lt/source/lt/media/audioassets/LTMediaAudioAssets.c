/*******************************************************************************
 * lt/source/lt/media/audioassets/LTMediaAudioAssets.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include <lt/core/LTCore.h>
#include <lt/media/audioassets/LTMediaAudioAssets.h>
#include <lt/product/config/LTProductConfig.h>

DEFINE_LTLOG_SECTION("audio.assets");

LTLibrary *                 s_pAudioAssets = NULL;
ILTAudioAssetsLibrary *     s_iAudioAssets = NULL;

/***************************************************************************************************
 * Library startup and shutdown.
 */
static bool LTMediaAudioAssetsImpl_LibInit(void) {
    // open the product's audio assets Library.
    LTProductConfig *productConfig = lt_openlibrary(LTProductConfig);
    u32 libSection = productConfig ? productConfig->GetLibraryConfigSection("LTMediaAudioAssets") : 0;
    const char *aaLibName = libSection ? productConfig->ReadString(libSection, "assetlib") : NULL;
    s_pAudioAssets = aaLibName ? LT_GetCore()->OpenLibrary(aaLibName) : NULL;
    s_iAudioAssets = s_pAudioAssets ? lt_getlibraryinterface(ILTAudioAssetsLibrary, s_pAudioAssets) : NULL;

    // log on failure
    if (! s_iAudioAssets) {
        if (! aaLibName) LTLOG_YELLOWALERT("missing", "assetlib not found in ProductConfig.json");
        else if (s_pAudioAssets) LTLOG_YELLOWALERT("bad.lib", "ILTAudioAssetsLibray not exported from %s", aaLibName);
    } /* failure to open aaLibName logged by LTCore, no need to log that case here */

    lt_closelibrary(productConfig);
    return (s_iAudioAssets != NULL);
}

static void LTMediaAudioAssetsImpl_LibFini(void) {
    lt_closelibrary(s_pAudioAssets);
    s_pAudioAssets = NULL;
    s_iAudioAssets = NULL;
}

/***************************************************************************************************
 * Passthrough functions.
 *
 * No NULL check on s_iAudioAssets since library should have failed to open.
 */
static u32  LTMediaAudioAssetsImpl_GetAudioAssetCount(void)                                      { return s_iAudioAssets->GetAudioAssetCount();                      }
static bool LTMediaAudioAssetsImpl_GetAudioAssetInfo(u32 assetID, char **ppName, LT_SIZE *pLen)  { return s_iAudioAssets->GetAudioAssetInfo(assetID, ppName, pLen);  }
static bool LTMediaAudioAssetsImpl_GetAudioAssetIDByName(const char *pName, u32 *pAssetID)       { return s_iAudioAssets-> GetAudioAssetIDByName(pName, pAssetID);   }
static bool LTMediaAudioAssetsImpl_Play(u32 assetID, 
                                        u32 options,
                                        LTAudioAssets_PlaybackCompletionCallback cb,
                                        void *pClientData)                                       { return s_iAudioAssets->Play(assetID, options, cb, pClientData);   }
static bool LTMediaAudioAssetsImpl_PlayByName(const char *pname, u32 options, 
                                        LTAudioAssets_PlaybackCompletionCallback cb,
                                        void *pClientData)                                       { return s_iAudioAssets->PlayByName(pname, options, cb, pClientData); }
static void LTMediaAudioAssetsImpl_Stop(void)                                                    { s_iAudioAssets->Stop();                                           }

/***************************************************************************************************
 * LTAudioAsset Library definition
 */
define_LTLIBRARY_ROOT_INTERFACE(LTMediaAudioAssets) {
    .GetAudioAssetCount         = LTMediaAudioAssetsImpl_GetAudioAssetCount,
    .GetAudioAssetInfo          = LTMediaAudioAssetsImpl_GetAudioAssetInfo,
    .GetAudioAssetIDByName      = LTMediaAudioAssetsImpl_GetAudioAssetIDByName,
    .Play                       = LTMediaAudioAssetsImpl_Play,
    .PlayByName                 = LTMediaAudioAssetsImpl_PlayByName,
    .Stop                       = LTMediaAudioAssetsImpl_Stop,
} LTLIBRARY_DEFINITION;

