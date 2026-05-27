/*******************************************************************************
 * <lt/media/sourcemanager/LTMediaSourceManager.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 *
 ******************************************************************************/
/** @file LTMediaSourceManager.h header for LT Media Source Manager
  */

#ifndef LT_INCLUDE_LT_MEDIA_SOURCEMANAGER_H
#define LT_INCLUDE_LT_MEDIA_SOURCEMANAGER_H

#include <lt/core/LTCore.h>
#include <lt/device/media/LTDeviceMedia.h>

/**< LTMediaSourceManager handles the logistics of managing multiple LTMediaSource instances.
 * It manages the lifetime of LTMediaSource instances and the dispatch of media data of 
 * different formats to the appropriate sources.
 * LTMediaSourceManager inherits thread from the caller of DispatchMediaData.
*/

LT_EXTERN_C_BEGIN

typedef void (*LTMediaSourceManager_CaptureRequestCallback)(void *pClientData);

typedef_LTObject(LTMediaSourceManager, 1) {
    /**< Set the synchronous callback for capture requests 
     * Set the callback for the source manage to request new audio capture. 
     * This callback will be called when 
     * 1) The number of active sources changes from zero to one, or
     * 2) When the number of in-flight dispatched events becomes zero and the number of active sources is greater than zero.
     * 
     * @param srcMgr The source manager object
     * @param pfnCallback The callback function to call
     * @param pClientData The client data to pass to the callback
     * @return true if the callback was set, false if srcMgr is NULL
    */
    bool        (*SetCaptureRequestCallback)(LTMediaSourceManager *srcMgr, LTMediaSourceManager_CaptureRequestCallback pfnCallback, void *pClientData);
    
    /**< Check of there are any active sources with a given format ID
     * 
     * @param srcMgr The source manager object
     * @param formatId The format ID to check
     * @return true if there are active sources with the given format ID, false otherwise
    */
    bool        (*FormatRequested)(LTMediaSourceManager *srcMgr, u32 formatId);

    /**< Get the number of active (started) sources 
     * 
     * @param srcMgr The source manager object
     * @return The number of active sources
    */
    u32         (*GetActiveSourceCount)(LTMediaSourceManager *srcMgr);

    /**< Create a new LTMediaSource
     * 
     * The lifetime of the source object and subsequent interactions with the 
     * ILTMediaSource interface is managed by the source manager.
     * 
     * @param srcMgr The source manager object
     * @param formatEncoding The encoding of the format. 
     *                       This is used as return value for ILTMediaSource->GetMediaEncoding() 
     *                       on the source object
     * @param formatId The format ID of the format. This value is defined by the caller and is 
     *                 used to map format to sources when dispatching media data
     * @return The handle to the new source object
    */
    LTHandle    (*CreateSource)(LTMediaSourceManager *srcMgr, u32 formatEncoding, u32 formatId);

    /**< Dispatch Media Data to sources
     * 
     * Dispatch media data to all sources created with the given format ID
     * 
     * @param srcMgr The source manager object
     * @param formatId The format ID of the media data
     * @param data Pointer to the media data to dispatch
     * @return The number of sources that data was dispatched to
    */
    u32         (*DispatchMediaData)(LTMediaSourceManager *srcMgr, 
                                     u32 formatId,
                                     LTMediaData *data);
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef LT_INCLUDE_LT_MEDIA_SOURCEMANAGER_H */
