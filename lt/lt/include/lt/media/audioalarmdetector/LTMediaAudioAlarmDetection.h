/*******************************************************************************
 * <lt/media/audioalarmdetector/LTMediaAudioAlarmDetection.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/
/** @file LTMediaAudioAlarmDetection.h header for LTMediaAudioAlarmDetection library root interface.
  */

#ifndef LT_INCLUDE_LT_MEDIA_LTMEDIAAUDIOALARMDETECTION_H
#define LT_INCLUDE_LT_MEDIA_LTMEDIAAUDIOALARMDETECTION_H

#include <lt/core/LTCore.h>
#include <lt/utility/messagepack/LTUtilityMessagePack.h>
#include <lt/device/media/LTDeviceMedia.h>

/* LTMediaAudioAlarmFlags is used to indicate which alarms are enabled. */
typedef enum {
    kLTMediaAudioAlarmFlags_None           = 0,
    kLTMediaAudioAlarmFlags_Smoke          = (1 << 0),
    kLTMediaAudioAlarmFlags_CarbonMonoxide = (1 << 1),
    kLTMediaAudioAlarmFlags_LoudSound      = (1 << 2),
    kLTMediaAudioAlarmFlags_All            = kLTMediaAudioAlarmFlags_Smoke |
                                             kLTMediaAudioAlarmFlags_CarbonMonoxide | 
                                             kLTMediaAudioAlarmFlags_LoudSound
} LTMediaAudioAlarmFlags;

/* LTMediaAudioAlarmState is used to indicate alarm detection events to clients. */
typedef enum {
    kLTMediaAudioAlarmState_Unknown,
    /* regular fire alarm */
    kLTMediaAudioAlarmState_Stopped,
    kLTMediaAudioAlarmState_Started,
    /* Carbon Monoxide detector alarm */
    kLTMediaAudioAlarmState_CO_Stopped,
    kLTMediaAudioAlarmState_CO_Started,
    /* Loud Sound detector alarm */
    kLTMediaAudioAlarmState_LoudSound_Stopped,
    kLTMediaAudioAlarmState_LoudSound_Started,
} LTMediaAudioAlarmState;


/*_______________________________________________________
  audio alarm detection event callback procedure typedef     /
              LTMediaAudioAlarmDetection_AudioAlarmEventProc */
typedef void (LTMediaAudioAlarmDetection_AudioAlarmEventProc)(LTMediaAudioAlarmState alarmState, void *clientData);
    /**< callback procedure type for notification of smoke detector sound events, signalling changes.
      *
      *  Clients register event procedures of this type by calling %OnAudioAlarmEvent() and unregister them by calling %OffAudioAlarmEvent().
      *
      *  @param alarmState: The new state of the alarm event. Used to indicate when an audio alarm has started or stopped.
      *  @param clientData: the clientData supplied when the event proc was registered.
      *
      *  @see OnAudioAlarmEvent, OffAudioAlarmEvent
      */

/*____________________________________________________________
  ***** LTLIBRARY_ROOT_INTERFACE !!!!                        /
  ***** LTLIBRARY_ROOT_INTERFACE !!!!                       /
                                 LTMediaAudioAlarmDetection   */
TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTMediaAudioAlarmDetection, 1);
struct LTMediaAudioAlarmDetectionApi {
    INHERIT_LIBRARY_BASE

    void (* StartDetectionEngine)(bool bTakeSnapshots);
        /**< starts audio alarm detection on the video data from the default audio capture LTMediaSource
          *  as provided by the LTDeviceMedia library used by the implementation.
          *
          *  @param bTakeSnapshots - specify whether to take audio snippets on loud sounds.
          */

    void (* StopDetectionEngine)(void);
        /**< stops audio alarm detection that was previously started by %StartDetectionEngine() */

    bool (* IsDetectionEngineRunning)(void);
        /**< determines whether or not the detection engine is running
          *
          * @return whether or not the detection engine is running
          */

    LTMediaAudioAlarmFlags (* GetAlarmFlags)(void);
        /**< gets the current alarm flags indicating which alarm types are active
          *
          * @return current alarm flags
          */

    void (* SetAlarmFlags)(LTMediaAudioAlarmFlags flags);
        /**< Sets the alarm flags used to enable/disable individual alarm types
          *
          * @param flags the alarm flags to be applied
          */

    void (* OnAudioAlarmEvent)(LTMediaAudioAlarmDetection_AudioAlarmEventProc * pAudioAlarmEventProc, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData);
        /**< Registers a audio alarm event callback procedure for receipt of alarm detection events as they occur.
          *  More than one client can register for notification and each client is automatically called back in their
          *  own thread context (i.e. which ever thread a client uses to call this registration function will be the thread that that respective client will have their callback function called on.
          *
          *  @param pAudioAlarmEventProc the clients event callback procedure
          *  @param pClientDataReleaseProc the clients clientData release proc that gets called when the client calls OffAudioAlarmEvent to unregister, or when this library is unloaded with clients still registered for this event
          *  @param pClientData client supplied optional context data that is passed back to the client's event callback during event notification
          */

    void (* NoAudioAlarmEvent)(LTMediaAudioAlarmDetection_AudioAlarmEventProc * pAudioAlarmEventProc);
        /**< Unregisters a previously registered event callback. */


    void (* SetDetectionSensitivity)(u32 sensitivity);
        /**< Set the sensitivity of the audio alarm detector
          *
          *  @param sensitivity - Sensitivity, 0 = no detection, 100 = maximum sensitivity or ~6dB difference over background
          */

};

LT_EXTERN_C_END
#endif /* #ifndef LT_INCLUDE_LT_MEDIA_LTMEDIAAUDIOALARMDETECTION_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
*/
