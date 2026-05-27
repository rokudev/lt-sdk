/*******************************************************************************
 * source/lt/media/audioalarmdetector/LTMediaAudioAlarmDetection.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/media/audioalarmdetector/LTMediaAudioAlarmDetection.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/system/settings/LTSystemSettings.h>
#include "alarm_detect.h"


DEFINE_LTLOG_SECTION("audalarm");

/* note these values are logarithmic
 * They are also SWAGs based off running the camera and clapping my
 * hands fairly hard next to it.
 */
#define AUDIO_PCM_dB8_MIN_SENSITIVITY (61)
#define AUDIO_PCM_dB8_MAX_SENSITIVITY (20)

static LTMediaAudioAlarmFlags  s_AlarmEnableFlags = kLTMediaAudioAlarmFlags_None;
static LTMediaAudioAlarmFlags  s_AlarmResult = kLTMediaAudioAlarmFlags_None;
static u32                     s_AlarmSensitivity;
static u32                     s_LoudSensitivityMaxdB8;
static u32                     s_LoudSensitivityMindB8;
static LTTime                  s_LoudSoundStartTime;
static LTTime                  s_CooldownSetting = LTTimeInitializer_Seconds(300);
static u8                      s_LoudSoundStartdB8;
static u8                      s_LastAudio_dB8;
static bool                    s_NotInCoolDown = true;
static ILTThread              *s_pThread = NULL;

static const char *s_CoolDownSettingKey = "iot/audioDetectionCooldown";

/*____________________________________________
  LTMediaAudioAlarmDetection.c static constants */
static const LTArgsDescriptor           s_AudioAlarmDetectionEventArgs  = { 1, { kLTArgType_pointer } }; /* describes number and kind of args delivered to event callback */
static const LTMediaFormat              s_formatAudioCapture = {
    .nKind = kLTMediaKind_Audio,
    .nEncoding = kLTMediaEncoding_PCM,
    .params.pcm.nSampleRate = ALARM_DETECT_SAMPLE_RATE,
    .params.pcm.nChannels = 1
};


/*_______________________________________________________________________
  LTMediaAudioAlarmDetection.c static variables managed by LibInit/LibFini */
static LTMutex *                        s_mutex = NULL;
static LTEvent                          s_hAudioAlarmDetectionEvent = 0;

/*_________________________________________________________________________________
  LTMediaAudioAlarmDetection.c static variables managed by Start/StopDetectionEngine */
static LTThread                         s_hAudioAlarmDetectionThread = 0;
static LTDeviceMedia *                  s_deviceMedia = NULL;

static LTMediaSource                    s_mediaSourceAudioAlarm = 0;

/*________________________________________________
  LTMediaAudioAlarmDetection.c forward declarations */
static bool LTMediaAudioAlarmDetection_ThreadInit(void);
static void LTMediaAudioAlarmDetection_ThreadExit(void);
static void AudioDelayedStartup(void *clientData);

/*___________________________________________________
  Library public interface function implementation */
static void LTMediaAudioAlarmDetection_StartDetectionEngine(bool bTakeSnapshots) {
    LT_UNUSED(bTakeSnapshots);
    /* First, get the mutex interface from the mutex handle, then use it to lock the mutex */
    s_mutex->API->Lock(s_mutex);

    /* create and start the thread if it isn't already running */
    if (s_hAudioAlarmDetectionThread == 0) {
        /* open LTDeviceMedia and our two MediaSources; only then will we create and start a thread */
        LT_ASSERT(NULL == s_deviceMedia && 0 == s_mediaSourceAudioAlarm );
        s_deviceMedia = lt_openlibrary(LTDeviceMedia);
        if (s_deviceMedia) {
            s_mediaSourceAudioAlarm = s_deviceMedia->OpenSource((LTMediaFormat *)&s_formatAudioCapture);
            if (s_mediaSourceAudioAlarm)  {
                /* clone the regions of interest for the thread to use */
                s_hAudioAlarmDetectionThread = LT_GetCore()->CreateThread("AudioAlarmMonitor");
                ILTThread * iThread = lt_gethandleinterface(ILTThread, s_hAudioAlarmDetectionThread);
                iThread->SetStackSize(s_hAudioAlarmDetectionThread, 2048);
                iThread->Start(s_hAudioAlarmDetectionThread, &LTMediaAudioAlarmDetection_ThreadInit, &LTMediaAudioAlarmDetection_ThreadExit);
            } else {
                /* We may have successfully opened one source but not the other */
                if (s_mediaSourceAudioAlarm) {
                    LT_GetCore()->DestroyHandle(s_mediaSourceAudioAlarm);
                    s_mediaSourceAudioAlarm = 0;
                }
                if (s_deviceMedia) {
                    lt_closelibrary(s_deviceMedia);
                    s_deviceMedia = NULL;
                }
            }
        }
    }
    s_mutex->API->Unlock(s_mutex);
}

static void LTMediaAudioAlarmDetection_StopDetectionEngine(void) {
    s_mutex->API->Lock(s_mutex);

    /* destroy the thread before destroying the media source and closing the device media library */
    if (s_hAudioAlarmDetectionThread) {
        ILTThread *iThread = lt_gethandleinterface(ILTThread, s_hAudioAlarmDetectionThread);
        iThread->KillTimer(s_hAudioAlarmDetectionThread, AudioDelayedStartup, NULL);
        iThread->Terminate(s_hAudioAlarmDetectionThread);
        iThread->WaitUntilFinished(s_hAudioAlarmDetectionThread, LTTime_Infinite());
        lt_destroyhandle(s_hAudioAlarmDetectionThread);
        s_hAudioAlarmDetectionThread = 0;
    }
    if (s_mediaSourceAudioAlarm) {
        LT_GetCore()->DestroyHandle(s_mediaSourceAudioAlarm);
        s_mediaSourceAudioAlarm = 0;
    }
    if (s_deviceMedia) {
        lt_closelibrary(s_deviceMedia);
        s_deviceMedia = NULL;
    }
    s_mutex->API->Unlock(s_mutex);
}

static bool LTMediaAudioAlarmDetection_IsDetectionEngineRunning(void) {
    s_mutex->API->Lock(s_mutex);
    bool bRetVal = (s_hAudioAlarmDetectionThread != 0);
    s_mutex->API->Unlock(s_mutex);
    return bRetVal;
}

static LTMediaAudioAlarmFlags LTMediaAudioAlarmDetection_GetAlarmFlags(void) {
    return s_AlarmEnableFlags;
}

static void LTMediaAudioAlarmDetection_SetAlarmFlags(LTMediaAudioAlarmFlags flags) {
    s_AlarmEnableFlags = flags;
}

static void LTMediaAudioAlarmDetection_OnAudioAlarmEvent(LTMediaAudioAlarmDetection_AudioAlarmEventProc * pAudioAlarmEventProc, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData) {
    ILTEvent * iEvent = lt_gethandleinterface(ILTEvent, s_hAudioAlarmDetectionEvent);
    iEvent->RegisterForEvent(s_hAudioAlarmDetectionEvent, pAudioAlarmEventProc, pClientDataReleaseProc, pClientData, false);
}

static void LTMediaAudioAlarmDetection_NoAudioAlarmEvent(LTMediaAudioAlarmDetection_AudioAlarmEventProc * pAudioAlarmEventProc) {
    ILTEvent * iEvent = lt_gethandleinterface(ILTEvent, s_hAudioAlarmDetectionEvent);
    iEvent->UnregisterFromEvent(s_hAudioAlarmDetectionEvent, pAudioAlarmEventProc);
}

static void LTMediaAudioAlarmDetection_SetDetectionSensitivity(u32 sensitivity) {
    //LTLOG_YELLOWALERT("sensitivity.in","set sensitivity to %d", sensitivity);
    if ( sensitivity > 100 ) {
        sensitivity = 100;
    }
    /* convert from 0-100 sensitivity to dB8
     *   0 = will not detect an alarm, in dB8 terms this is the largest change
     *        gain is a fixed value, s_LoudSensitivityMindB8
     *
     * 100 = will detect less slightly less than 6 dB of audio gain change
     *        gain is a fixed value, s_LoudSensitivityMaxdB8
     *
     */
    sensitivity = 100 - sensitivity;    /* invert it, less sensitive is a larger dB8 */
    sensitivity = (sensitivity * (s_LoudSensitivityMindB8 - s_LoudSensitivityMaxdB8)) / 100;
    /* add the dB8 baseline value */
    sensitivity += s_LoudSensitivityMaxdB8;
    //LTLOG_YELLOWALERT("sensitivity.out","set sensitivity to %d", sensitivity);

    s_AlarmSensitivity = sensitivity;
}


/*_________________________________
  Library private implementation */

static void LTMediaAudioAlarmDetection_AudioAlarmDetectionEventDispatchProc(LTEvent hEvent, void * pEventProc, LTArgs * pEventArgs, void * pEventProcClientData) {
    /* This function is called by the LTEvent subsystem in the context of the thread that registered pEventProc,
       Its job is to decode the event notification callback procedure type and its args and call the decoded
       event proc directly, using the decoded arguments and the client data registered by the client. */
    LT_UNUSED(hEvent);
    (*(LTMediaAudioAlarmDetection_AudioAlarmEventProc *)pEventProc)((LTMediaAudioAlarmState)LTArgs_pointerAt (0, pEventArgs), pEventProcClientData);
}

static bool DetectLoudSound(LTMediaData *mediaData) {
    bool bDetected = false;
    /* Perform "loud sound" detection based off the maximum PCM swing in the last 125 ms.
     * When a loud sound is detected, we'll see a FAST increase in the audio PCM values
     * When the sound dissipates, the audio PCM Max values will drop
     */
    #if 0
    LTLOG_YELLOWALERT("loud.check","last %3d cur %3d delta %3d sensitivity %2d starting %3d",
        (int)s_LastAudio_dB8,
        (int)mediaData->meta.audio.pcm_max_dB8,
        (int)mediaData->meta.audio.pcm_max_dB8 - (int)s_LastAudio_dB8,
        (int)s_AlarmSensitivity,
        (int)s_LoudSoundStartdB8 );
    #endif
    /* a loud sound is currently being detected */
    if ( s_AlarmResult & kLTMediaAudioAlarmFlags_LoudSound ) {
        /* get time since the alarm started */
        LTTime dt = LTTime_Subtract(LT_GetCore()->GetKernelTime(), s_LoudSoundStartTime);

        /* Once a loud sound has been detected, let it continue until 10 seconds have elapsed and the sound has decayed */
        if ( ( LTTime_GetSeconds(dt) < 10 ) &&
             ( mediaData->meta.audio.pcm_max_dB8 >= (s_LoudSoundStartdB8 - 8) )) {
            bDetected = true;
        }
    } else if (s_AlarmEnableFlags & kLTMediaAudioAlarmFlags_LoudSound) {
        /* see if a loud sound has started.
         * When a loud sound is detected, the pcm_max will increase quickly
         *    meta.audio.pcm_max_dB8 is fixed point, and is equivalent to
         *        (int)(8 * log2(pcm_max))
         *
         */
        if ( s_LastAudio_dB8 &&
             ( (mediaData->meta.audio.pcm_max_dB8 - s_LastAudio_dB8) > (s32)s_AlarmSensitivity ))
        {
            s_LoudSoundStartTime = LT_GetCore()->GetKernelTime();
            s_LoudSoundStartdB8 = mediaData->meta.audio.pcm_max_dB8;
            bDetected = true;
        }
    }
    /* save the current AGC gain for comparison next time */
    s_LastAudio_dB8 = mediaData->meta.audio.pcm_max_dB8;
    return bDetected;
}

static void
EndOfCooldownProc(void *clientData) {
    LT_UNUSED(clientData);
    s_pThread->KillTimer(s_pThread->GetCurrentThread(), EndOfCooldownProc, NULL);
    LTLOG_SERVER("cooldown.end", NULL);
    s_NotInCoolDown = true;
}

static void LTMediaAudioAlarmDetection_MediaSourceAudioEventProc(LTMediaEvent event, void *eventData, void *clientData) {
    LT_UNUSED(clientData);
    if (event == kLTMediaEvent_Error) { return; }
    ILTEvent * iEvent = lt_gethandleinterface(ILTEvent, s_hAudioAlarmDetectionEvent);
    LTMediaData *mediaData = eventData;
    LTMediaAudioAlarmFlags audResult = kLTMediaAudioAlarmFlags_None;

    if (s_AlarmEnableFlags & (kLTMediaAudioAlarmFlags_Smoke | kLTMediaAudioAlarmFlags_CarbonMonoxide)) {
        audResult = AudioAlarmDetect_Analyze(
                s_AlarmEnableFlags,
                (s16 *)mediaData->pData,
                ALARM_DETECT_SAMPLE_RATE,
                false,                     /* mono */
                (mediaData->nDataLen >> 1) /* 2 bytes per sample */
            );
    }

    if ((s_AlarmEnableFlags & kLTMediaAudioAlarmFlags_LoudSound) && DetectLoudSound(mediaData)) {
        audResult |= kLTMediaAudioAlarmFlags_LoudSound;
    }

    if ( s_AlarmResult != audResult ) {
        LTLOG("alarm.change", "Audio Alarm State Change: %lu -> %lu", LT_Pu32(s_AlarmResult), LT_Pu32(audResult));

        /* fire alarm status changed */
        if ( (kLTMediaAudioAlarmFlags_Smoke & audResult) ^
             (kLTMediaAudioAlarmFlags_Smoke & s_AlarmResult) )
        {
            if ( kLTMediaAudioAlarmFlags_Smoke & audResult ) {
                iEvent->NotifyEvent(s_hAudioAlarmDetectionEvent, kLTMediaAudioAlarmState_Started);
            } else {
                iEvent->NotifyEvent(s_hAudioAlarmDetectionEvent, kLTMediaAudioAlarmState_Stopped);
            }
        }

        /* carbon monoxide alarm status changed */
        if ( (kLTMediaAudioAlarmFlags_CarbonMonoxide & audResult) ^
             (kLTMediaAudioAlarmFlags_CarbonMonoxide & s_AlarmResult) )
        {
            if ( kLTMediaAudioAlarmFlags_CarbonMonoxide & audResult ) {
                iEvent->NotifyEvent(s_hAudioAlarmDetectionEvent, kLTMediaAudioAlarmState_CO_Started);
            } else {
                iEvent->NotifyEvent(s_hAudioAlarmDetectionEvent, kLTMediaAudioAlarmState_CO_Stopped);
            }
        }

        /* Loud Sound status changed */
        if ( (kLTMediaAudioAlarmFlags_LoudSound & audResult) ^
             (kLTMediaAudioAlarmFlags_LoudSound & s_AlarmResult) )
        {
            if (s_NotInCoolDown) {
                if ( kLTMediaAudioAlarmFlags_LoudSound & audResult ) {
                    iEvent->NotifyEvent(s_hAudioAlarmDetectionEvent, kLTMediaAudioAlarmState_LoudSound_Started);
                } else {
                    iEvent->NotifyEvent(s_hAudioAlarmDetectionEvent, kLTMediaAudioAlarmState_LoudSound_Stopped);
                    s_NotInCoolDown = false;
                    LTLOG_SERVER("cooldown.start", "%llus cooldown timer", LT_Ps64(LTTime_GetSeconds(s_CooldownSetting)));
                    // Start sound event cooldown timer
                    s_pThread->SetTimer(s_pThread->GetCurrentThread(), s_CooldownSetting, EndOfCooldownProc, NULL, NULL);
                }
            }
        }

        /* store for next time */
        s_AlarmResult = audResult;
    }
}

static void AudioDelayedStartup(void *clientData) {
    LT_UNUSED(clientData);
    ILTThread * iThread = lt_gethandleinterface(ILTThread, s_hAudioAlarmDetectionThread);
    iThread->KillTimer(s_hAudioAlarmDetectionThread, AudioDelayedStartup, NULL);

    ILTMediaSource *iMediaSourceAudioAlarm = lt_gethandleinterface(ILTMediaSource, s_mediaSourceAudioAlarm);
    iMediaSourceAudioAlarm->OnMediaEvent(s_mediaSourceAudioAlarm, &LTMediaAudioAlarmDetection_MediaSourceAudioEventProc, NULL);
}

static bool LTMediaAudioAlarmDetection_ThreadInit(void) {
    ILTMediaSource *iMediaSourceAudioAlarm = lt_gethandleinterface(ILTMediaSource, s_mediaSourceAudioAlarm);
    ILTThread * iThread = lt_gethandleinterface(ILTThread, s_hAudioAlarmDetectionThread);

    iMediaSourceAudioAlarm->Start(s_mediaSourceAudioAlarm);
    
    /* Delay detection notification start to allow initial audio data to settle */
    iThread->SetTimer(s_hAudioAlarmDetectionThread, LTTime_Seconds(2), AudioDelayedStartup, NULL, NULL);

    return true;
}

static void LTMediaAudioAlarmDetection_ThreadExit(void) {
    ILTThread * iThread = lt_gethandleinterface(ILTThread, s_hAudioAlarmDetectionThread);
    iThread->KillTimer(s_hAudioAlarmDetectionThread, AudioDelayedStartup, NULL);

    ILTMediaSource *iMediaSourceAudioAlarm = lt_gethandleinterface(ILTMediaSource, s_mediaSourceAudioAlarm);
    iMediaSourceAudioAlarm->Stop(s_mediaSourceAudioAlarm);
    iMediaSourceAudioAlarm->NoMediaEvent(s_mediaSourceAudioAlarm, &LTMediaAudioAlarmDetection_MediaSourceAudioEventProc);
}

/*_____________________________________
  Library Initialization and Cleanup */
static bool LTMediaAudioAlarmDetectionImpl_LibInit(void) {
    LTSystemSettings * pSystemSettings;
    /* set default values in case they're missing from config */
    s_LoudSensitivityMindB8 = AUDIO_PCM_dB8_MIN_SENSITIVITY;
    s_LoudSensitivityMaxdB8 = AUDIO_PCM_dB8_MAX_SENSITIVITY;

    s_pThread = lt_getlibraryinterface(ILTThread, LT_GetCore());

    if ( NULL != (pSystemSettings = lt_openlibrary(LTSystemSettings))) {
        s64 val;

        if ( pSystemSettings->GetIntegerValue("iot/audioLoudSoundMindB8", &val) ) {
            s_LoudSensitivityMindB8 = (u32) val;
        }

        if ( pSystemSettings->GetIntegerValue("iot/audioLoudSoundMaxdB8", &val) ) {
            s_LoudSensitivityMaxdB8 = (u32) val;
        }

        if ( pSystemSettings->GetIntegerValue(s_CoolDownSettingKey, &val) ) {
            s_CooldownSetting = LTTime_Seconds(val);
        }

        lt_closelibrary(pSystemSettings);
    } else {
        LTLOG("library.open.fail.settings", "Unable to open LTSystemSettings library");
    }
    s_AlarmSensitivity = s_LoudSensitivityMaxdB8;

    /* Create our mutex */
    if (!(s_mutex = lt_createobject(LTMutex))) { return false; } /* abort load if the mutex can't be created. */
        /* NOTE: s_mutex is used only to ensure thread safety of multiple clients calling into the Start and StopDetectionEngine functions simultaneously. The motion detection thread doesn't need it or use it. */

    AudioAlarmDetect_Initialize();

    /* Create the event we'll use to notify motion event receivers that motion has been detected */
    if (!(s_hAudioAlarmDetectionEvent =
            LT_GetCore()->CreateEvent(
                &s_AudioAlarmDetectionEventArgs,
                (void *)&LTMediaAudioAlarmDetection_AudioAlarmDetectionEventDispatchProc,
                NULL, NULL, NULL))) {
        /* couldn't create event; clean up what we made so far and bail */
        lt_destroyobject(s_mutex);
        s_mutex = NULL;
        return false;
    }

    return true; /* LibInit successful; lib will proceed with loading on return true */
}

static void LTMediaAudioAlarmDetectionImpl_LibFini(void) {

    /* call StopDetectionEngine() in case the client did not, then destroy event and mutex */
    LTMediaAudioAlarmDetection_StopDetectionEngine();

    LT_GetCore()->DestroyHandle(s_hAudioAlarmDetectionEvent);
    lt_destroyobject(s_mutex);
    s_hAudioAlarmDetectionEvent = 0;
    s_mutex = NULL;
    s_pThread = NULL;
}

/*________________________________________________________
  LTMediaAudioAlarmDetection library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTMediaAudioAlarmDetection)
    .StartDetectionEngine       = LTMediaAudioAlarmDetection_StartDetectionEngine,
    .StopDetectionEngine        = LTMediaAudioAlarmDetection_StopDetectionEngine,
    .IsDetectionEngineRunning   = LTMediaAudioAlarmDetection_IsDetectionEngineRunning,
    .GetAlarmFlags              = LTMediaAudioAlarmDetection_GetAlarmFlags,
    .SetAlarmFlags              = LTMediaAudioAlarmDetection_SetAlarmFlags,
    .OnAudioAlarmEvent          = LTMediaAudioAlarmDetection_OnAudioAlarmEvent,
    .NoAudioAlarmEvent          = LTMediaAudioAlarmDetection_NoAudioAlarmEvent,
    .SetDetectionSensitivity    = LTMediaAudioAlarmDetection_SetDetectionSensitivity
LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 */
