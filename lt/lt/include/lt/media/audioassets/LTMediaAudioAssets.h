/*******************************************************************************
 * <lt/media/audioassets/LTMediaAudioAssets.h>
 *
 *   LT Audio Assets Library
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#ifndef LT_INCLUDE_LT_MEDIA_AUDIOASSETS_H
#define LT_INCLUDE_LT_MEDIA_AUDIOASSETS_H

LT_EXTERN_C_BEGIN

typedef enum {
    kLTAudioAssets_PlayOptions_Async            = 0,
    kLTAudioAssets_PlayOptions_Blocking         = (1 << 24),
    kLTAudioAssets_PlayOptions_Looped           = (1 << 25),
    kLTAudioAssets_PlayOptions_LoopDurationMask = (1 << 24) - 1,
} LTAudioAssets_PlayOptions;

typedef void (*LTAudioAssets_PlaybackCompletionCallback)(u32 assetID, void *pClientData); 

#define AUDIO_ASSETS_INTERFACE                                                                  \
    u32  (* GetAudioAssetCount) (void);                                                         \
        /**< gets the number of audio assets available in the library                           \
         *                                                                                      \
         *   @param hSink the handle of the media sink.                                         \
         *   @return The number of audio assets available.                                      \
         */                                                                                     \
    bool (* GetAudioAssetInfo) (u32 assetID, char **ppName, LT_SIZE *pLen);                     \
        /**< gets the name and size of an audio asset.                                          \
         *                                                                                      \
         *   @param assetID Index reference the audio asset, in the range of 0 to value         \
         *                  returned by GetAudioAssetCount()                                    \
         *   @param ppName  Pointer to a char array where the NUL-terminated asset name string  \
         *                  will be copied. Ignored if supplied as NULL.                         \
         *   @param pLen    Pointer to LT_SIZE variable where the byte length of asset will be  \
         *                  copied. Ignored if supplied as NULL.                                 \
         *   @return        false if asset does not exist (assetID out of bound).               \
         *                  true otherwise.                                                     \
         */                                                                                     \
    bool (* GetAudioAssetIDByName) (const char *pName, u32 *pAssetID);                          \
        /**< gets the assetID of the string name of asset                                       \
         *                                                                                      \
         *   @param pName       NUL-terminated asset name string                                \
         *   @param pAssetID    Pointer to u32 variable where the assetID will be               \
         *                      copied. Ignored if supplied as NULL.                             \
         *   @return            false if asset is not found. true otherwise.                    \
         */                                                                                     \
    bool (* Play) (u32 assetID, u32 options, LTAudioAssets_PlaybackCompletionCallback cb, void *pClientData);                                                   \
        /**< Playback the asset referenced by assetID                                           \
         *                                                                                      \
         *   @param assetID   asset ID of the audio asset to playback                           \
         *   @param options   Playback options: async or blocking, looped or one-shot           \
         *   @param cb        Callback function to be called when playback is complete.         \
         *                    If the playback is blocking, there will be no callback.           \
         *                    Set to NULL if not needed.                                        \
         *   @param pClientData  Pointer to be passed to the completion callback function.      \
         *   @return          false if asset is not found. true otherwise.                      \
         */                                                                                     \
    bool (* PlayByName) (const char *pname, u32 options, LTAudioAssets_PlaybackCompletionCallback cb, void *pClientData);                                       \
        /**< Playback the assetID of the string name of asset                                   \
         *                                                                                      \
         *   @param pname     NUL-terminated asset name string                                  \
         *   @param options   Playback options: async or blocking, looped or one-shot           \
         *   @param cb        Callback function to be called when playback is complete.         \
         *                    If the playback is blocking, there will be no callback.           \
         *                    Set to NULL if not needed.                                        \
         *   @param pClientData  Pointer to be passed to the completion callback function.      \
         *   @return          false if asset is not found. true otherwise.                      \
         */                                                                                     \
    void (* Stop) (void);                                                                       \
        /**< Stops any in-progress playback                                                     \
         */                                                                                     \


/**
 * ILTAudioAssetsLibrary - Audio Assets Library interface
 *
 */
typedef_LTLIBRARY_INTERFACE(ILTAudioAssetsLibrary, 1) {
/** ___________________________________
  *   @section ILTAudioAssetsLibrary Interface
  *   * @brief an interface for audio assets library.
  *   *
  *   * ILTAudioAssetsLibrary provides a simple interface interacting with audio assets library.
  \______________________________________________________________________________________*/
    AUDIO_ASSETS_INTERFACE
} LTLIBRARY_INTERFACE;

/**
 * LTMediaAudioAssets - Audio Assets Library root interface
 *
 */
typedef_LTLIBRARY_ROOT_INTERFACE(LTMediaAudioAssets, 1) {
/** ___________________________________
  *   @section LTMediaAudioAssets Root Interface
  *   * @brief This interface mirrors ILTAudioAssetsLibrary for audio assets library.
  *   *
  *   * LTMediaAudioAssets mirrors ILTAudioAssetsLibrary to provide a transparent interface
  *   * to the product-specific audio assets library.
  \______________________________________________________________________________________*/
    AUDIO_ASSETS_INTERFACE
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif //LT_INCLUDE_LT_MEDIA_AUDIOASSETS_H
