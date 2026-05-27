/*******************************************************************************
 * LT Motion Detection library.
 *
 * Indoor Camera Motion Detection Library - handles motion detection logic.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/media/motiondetection/LTMediaMotionDetection.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <lt/product/config/LTProductConfig.h>
#include <lt/system/settings/LTSystemSettings.h>
#include "LTMediaMotionDetection_Private.h"
#include <lt/media/objectdetection/LTMediaObjectDetection.h>

/*____________________________________
  LTMediaMotionDetection.c #defines */
DEFINE_LTLOG_SECTION("motion");

// #define P(...) LTLOG_DEBUG(__VA_ARGS__)
#define P(...)
// #define PV(...) LTLOG_VERBOSE(__VA_ARGS__)
#define PV(...)

/** Global variables shared between LTMediaMotionDetection source files. */
struct Globals s_LTMediaMotionDetection_Private;

/** LTMediaSnapshotState is used by motion events to signal improved frame for CV inference. */
typedef enum {
  kLTMotionSnapshotState_Assessing,     // Assessing if current motion conditions are better than any previous snapshot
  kLTMotionSnapshotState_Requested,     // Snapshot requested, waiting for data from driver, no further assessments occur at this time
  kLTMotionSnapshotState_Available,     // Snapshot is available to be sent. s_pMotionSnapshot guaranteed to be non-NULL
  kLTMotionSnapshotState_Notifying,     // Snapshot has been notified out to clients. Snapshot data cannot be modified within this state
} LTMediaSnapshotState;

static u32   s_MinMotionFramesForEvent      = 20;  /**< Minimum frames with motion detected before a motion event starts */
static u32   s_MinFramesUntilEventEnd       = 20;  /**< Minimum frames with no motion before current motion event stops */
static u32   s_MaxFramesUntilFirstSnapshot  = 40;  /**< Maximum frames until first snapshot notification */
static u32   s_MaxFramesForBestSnapshot     = 30;  /**< Maximum frames until latest snapshot is notified */
static u32   s_MinFramesMotionEventDuration = 160; /**< Minimum frames for the motion event duration */
static u32   s_MaxFramesToAllowAbort        = 35;  /**< Motion can only be aborted within this time from start of event */
static u32   s_framesNoMotion               = 0;
static u32   s_framesOfMotion               = 0;
static float s_bestBoundingBoxWeighting     = LT_DBL_MAX;
static u32   s_framesSinceSnapshotAvailable = 0;
static u32   s_framesSinceMotionStart       = 0;
static u8    s_snapshotsUploaded            = 0;
static u8    s_maxSnapshotsPerMotionEvent   = 3;    /**< Maximum snapshots that can be notified within a single motion event */
static u32   s_pauseOnStartupTimeMilliseconds = 2000;    /**< Time to wait before starting motion detection after startup */

static LTMediaMotionState   s_motionState   = kLTMediaMotionState_Stopped;
static LTMediaSnapshotState s_snapshotState = kLTMotionSnapshotState_Assessing;
static LTMediaMotionCause   s_motionCause;
static LTMediaObjectDetection *s_pObjectDetection = NULL;

#define BYTES_PER_WORD sizeof(LTMediaMotionDetection_MotionZoneBitmapWord) // number of bytes in a single motion zone bitmap word
#define BITMAP_WORD_LEN (BYTES_PER_WORD * 8)                               // number of bits in a single motion zone bitmap word
#define ENERGY_HISTOGRAM_BINS (MOTION_SENSITIVITY_BUCKETS + 1)

/*____________________________________________
  LTMediaMotionDetection.c static constants */

/* Motion Event args: MotionState (enum); Pointer to UUID; Pointer to snapshot */
static const LTArgsDescriptor s_MotionDetectionEventArgs = {3, {kLTArgType_s32, kLTArgType_pointer, kLTArgType_pointer}};
/* Motion Metadata Event args: Pointer to Metadata; Pointer to UUID */
static const LTArgsDescriptor s_MotionMetadataEventArgs  = {2, {kLTArgType_pointer, kLTArgType_pointer}};
/* Zone state event arg: ZoneState (enum)*/
static const LTArgsDescriptor s_ZoneStateEventArgs  = {1, {kLTArgType_s32}};

static const LTMediaFormat    s_formatMotionFrames       = {.nKind = kLTMediaKind_Motion, .nEncoding = kLTMediaEncoding_Motion};
static const LTMediaFormat    s_formatSnapshotFrames     = {.nKind = kLTMediaKind_Image_SD, .nEncoding = kLTMediaEncoding_Jpeg};

/* Motion Detection Sensitivity */

/* The value here is a local copy of the user setting for motion detection sensitivity set in the Smart Home app, with
 * setting key "iot/motionDetectionSensitivity". First the default value for this setting is read from the relevant
 * LTProductConfig.json, then if a value for this setting exists in device flash the default value will be overwritten
 * by the camera app. In general the default value set in this library's LibInit will always be overridden. */
static u8 s_motionDetectionSensitivity; // 0-100

/** Motion Sensitivity Modes which control the set of sensitivity parameters used for motion detection. */
typedef enum {
    kLTMotionSensitivityMode_Normal,
    kLTMotionSensitivityMode_NightIRLEDs,
    // kLTMotionSensitivityMod_DayLowlight,
    // kLTMotionSensitivityMod_RainWindMode,
    kLTMotionSensitivityMode_Count
} MotionSensitivityMode;

typedef MotionSensitivityParameters MotionSensitivityTable[MOTION_SENSITIVITY_BUCKETS];

/** Master motion sensitivity table - holds separate motion detection sensitivity tables for each sensitivity mode.
 *  Each mode's table is read from config settings separately during library initialisation. */
static MotionSensitivityTable s_motionSensitivityTables[kLTMotionSensitivityMode_Count] = {0};

/** Current motion detection sensitivity mode. */
static MotionSensitivityMode s_motionSensitivityMode = kLTMotionSensitivityMode_Normal;

/* Constants derived from video driver properties. */
static LTMediaResolution SnapshotResolution;   // SD JPEG Resolution
static u32 BitmapWords;

/* Constants defined by product configuration. */
/* 'BitGrid' refers to the user-specified zone configuration in the Smart Home app, which is scaled up to the
 * native shape of motion data for internal motion zone calculations. */
static u8 BitGridWidth;
static u8 BitGridHeight;

/*_______________________________________________________________________
  LTMediaMotionDetection.c static variables managed by LibInit/LibFini */
static LTMutex                                  *s_mutex = NULL;
static LTEvent                                  s_hMotionDetectionEvent = 0;
static LTEvent                                  s_hMotionMetadataEvent = 0;
static LTEvent                                  s_hZoneStateEvent = 0;
static u8                                       s_MotionUuid[LT_UUID_BYTE_LEN] = {0};
static LTMediaMotionDetection_MotionZoneBitmap  *s_motionZoneBitmap = NULL;
static LTMediaMotionDetection_MotionZoneBitGrid *s_motionZoneBitGrid = NULL;
static bool                                     s_bMotionZoneEnabled = false;
static LTMediaMotionTimingMode                  s_motionTimingMode = kLTMediaMotionTimingMode_Off;
/* For each sensitivity threshold, report the number of moxels in each frame for which the energy level is equal to or
 * above that threshold but below the next one up. There is one extra bin - it tells you the number of moxels which
 * would not have triggered at any sensitivity threshold on the curve. */
static u32                                      s_preDownsamplingEnergyHistogramCounts[ENERGY_HISTOGRAM_BINS] = {0};
static u32                                      s_postDownsamplingEnergyHistogramCounts[ENERGY_HISTOGRAM_BINS] = {0};
/* Object reference to the selected motion detection algorithm. */
static LTMediaMotionDetectionAlgorithm          *s_algorithm = NULL;

/*_________________________________________________________________________________
  LTMediaMotionDetection.c static variables managed by Start/StopDetectionEngine */
static LTThread                                s_hMotionDetectionThread = 0;
static LTUtilityByteOps                        *s_pUtilityByteOps = NULL;
static LTMediaSource                           s_mediaSourceMotion = 0;
static LTMediaSource                           s_mediaSourceSnapshot = 0;
static LTMediaMotionDetection_Snapshot         *s_pMotionSnapshot = NULL;   // Must be synced with the kLTMotionSnapshotState_Assessing state
static LTMediaMotionDetection_Regions          *s_snapshotMotionROIs = NULL;
static bool                                    s_bMotionPaused = false;
static LTMediaMotionFlags                      s_detectionFlags = kLTMediaMotionFlags_All;
static bool                                    s_bLogMotionEventStats = true; /* Disable during unit testing only. */

/**________________________________________________________________________________
  Counts for bench-marking and optimising motion-detection parameters */
static u32 s_totalMotionFrameCount = 0;

/*________________________________________________
  LTMediaMotionDetection.c forward declarations */
static void ResetMotionState(void);
static bool LTMediaMotionDetection_ThreadInit(void);
static void LTMediaMotionDetection_ThreadExit(void);
static void SetBitmapProc(void *pClientData);
static void MotionStartEventStatsLogging(void);
static void MotionDelayedStartup(void *pClientData);

/** Close and reset handles used by the motion thread. This occurs normally when the motion
 *  engine is stopped, and also for cleanup during error handling.
 */
static void CloseThreadHandles(void) {
    LT_GetCore()->DestroyHandle(s_mediaSourceMotion);
    s_mediaSourceMotion = LTHANDLE_INVALID;
    LT_GetCore()->DestroyHandle(s_mediaSourceSnapshot);
    s_mediaSourceSnapshot = LTHANDLE_INVALID;
    lt_closelibrary(S.pDeviceMedia);
    S.pDeviceMedia = NULL;
}

/*___________________________________________________
  Library public interface function implementation */
static bool LTMediaMotionDetection_StartDetectionEngine(bool bTakeSnapshots) {
    bool bResult = false;

    s_mutex->API->Lock(s_mutex);

    /* create and start the thread if it isn't already running (the while is single-shot) */
    while (s_hMotionDetectionThread == 0) {
        /* open our two MediaSources; only then will we create and start a thread */
        if (S.pDeviceMedia || s_mediaSourceMotion || s_mediaSourceSnapshot) {
            /* There's an unexpected mismatch between these handles and the motion thread.
             * These shouldn't exist if there's no thread yet. But it's safe to close now and recreate.
             */
            CloseThreadHandles();
        }

        S.pDeviceMedia = lt_openlibrary(LTDeviceMedia);
        if (!S.pDeviceMedia) {
            LTLOG_YELLOWALERT("start.eng.no.devicemedia", "StartDetectionEngine() failed: couldn't open LTDeviceMedia.");
            break;
        }

        s_mediaSourceMotion = S.pDeviceMedia->OpenSource((LTMediaFormat *)&s_formatMotionFrames);
        if (bTakeSnapshots) {
            s_mediaSourceSnapshot = S.pDeviceMedia->OpenSource((LTMediaFormat *)&s_formatSnapshotFrames);
        }
        if (!s_mediaSourceMotion || (!s_mediaSourceSnapshot && bTakeSnapshots)) {
            LTLOG_YELLOWALERT("start.eng.no.sources", "StartDetectionEngine() failed: couldn't open all sources.");

            /* Cleanup any sources that were able to open */
            CloseThreadHandles();
            break;
        }

        s_hMotionDetectionThread = LT_GetCore()->CreateThread("MotionMonitor");
        if (!s_hMotionDetectionThread) {
            LTLOG_YELLOWALERT("start.eng.no.thread", "StartDetectionEngine() failed: couldn't start thread.");
            break;
        }

        s_algorithm->API->ResetHeatmap(s_algorithm);

        ILTThread *iThread = lt_gethandleinterface(ILTThread, s_hMotionDetectionThread);
        iThread->SetStackSize(s_hMotionDetectionThread, 1536);
        iThread->StartSynchronous(s_hMotionDetectionThread, &LTMediaMotionDetection_ThreadInit, &LTMediaMotionDetection_ThreadExit);

        bResult = true;
    }
    s_mutex->API->Unlock(s_mutex);
    return bResult;
}

static bool LTMediaMotionDetection_PauseMotionDetection(void) {
    if (s_hMotionDetectionThread) {
        if (!s_bMotionPaused) {
            s_bMotionPaused = true;
            LTLOG_DEBUG("api.pause", "Pausing motion detection");
        } else {
            LTLOG_DEBUG("api.paused", "Motion detection already paused");
        }
        return true;
    }
    return false;
}

static bool LTMediaMotionDetection_ResumeMotionDetection(void) {
    if (s_hMotionDetectionThread) {
        if (s_bMotionPaused) {
            s_bMotionPaused = false;
            LTLOG_DEBUG("api.resume", "Resuming motion detection");
        } else {
            LTLOG_DEBUG("api.resumed", "Motion detection already running");
        }
        return true;
    }
    return false;
}

static void LTMediaMotionDetection_StopDetectionEngine(void) {
    if (!s_mutex->API->TryLock(s_mutex)) {
        LTLOG("stop.eng.lockfail", "StopDetectionEngine() failed to acquire mutex lock.");
        return;
    }

    if (s_hMotionDetectionThread) {
        /* destroy the thread before destroying the other media sources and closing the device media library */
        ILTThread *iThread = lt_gethandleinterface(ILTThread, s_hMotionDetectionThread);
        iThread->KillTimer(s_hMotionDetectionThread, MotionDelayedStartup, NULL);
        iThread->Terminate(s_hMotionDetectionThread);
        iThread->WaitUntilFinished(s_hMotionDetectionThread, LTTime_Infinite());
        lt_destroyhandle(s_hMotionDetectionThread);
        s_hMotionDetectionThread = 0;
        CloseThreadHandles();
    }
    ResetMotionState();

    s_mutex->API->Unlock(s_mutex);
}

static bool LTMediaMotionDetection_IsDetectionEngineRunning(void) {
    s_mutex->API->Lock(s_mutex);
    bool bRetVal = (s_hMotionDetectionThread != 0);
    s_mutex->API->Unlock(s_mutex);
    return bRetVal;
}

static bool LTMediaMotionDetection_ExternalTrigger(LTMediaMotionCause trigger) {
    // External triggers are ignored when the detection engine is not running
    // Note that the trigger is accepted even if detection is paused, as it's an edge-triggered event that can't be lost

    if (!s_hMotionDetectionThread) {
        return false;
    }

    // Ignore invalid trigger types
    if (trigger != kLTMediaMotionCause_PIR) {
        LTLOG_YELLOWALERT("ext.trigger.invalid", "Invalid external trigger type: %lu", LT_Pu32(trigger));
        return false;
    }

    // Ignore trigger if motion is already detected
    if (s_motionState != kLTMediaMotionState_Stopped) {
        LTLOG_YELLOWALERT("ext.trigger.already.detecting", "External trigger ignored: already detecting motion");
        return false;
    }

    // Override the entry conditions and start a new motion event
    s_motionCause = trigger;
    LTLOG("ext.trigger", "%lu", LT_Pu32(trigger));

    // Reset counters for new motion event
    ResetMotionState();

    /* Update state, generate a new motion event UUID, and notify clients.
     * Also log motion event start statistics to cloud for data analysis.
     */
    s_motionState = kLTMediaMotionState_Started;
    s_pUtilityByteOps->GenUUID(s_MotionUuid);
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hMotionDetectionEvent);
    iEvent->NotifyEvent(s_hMotionDetectionEvent, s_motionState, s_MotionUuid, NULL);
    MotionStartEventStatsLogging();

    // TBD - Set a property in the media device to indicate external trigger, this is to embed in NALU SEI only
    // TBD - also add this for moxel-triggered motion. Or should the SEI builder just register for motion events?
    //s_deviceMedia->SetProperty("motion.trigger", trigger);

    return true;
}

static void LTMediaMotionDetection_SetMotionZones(LTMediaMotionDetection_MotionZoneBitGrid *pBitGrid) {
    /* Sets the motion zone bitmap according to the supplied buffer, which is done by scaling using the supplied
     * height and width values, in the context of the detection thread.
     * We malloc a new bitmap and set it from the supplied buffer using scaling. If the buffer is NULL then we
     * default to the whole frame. We then queue the SetBitmapProc to swap the current library motion zones bitmap to
     * point at our new bitmap. This is done in the motion detection thread to ensure that the bitmap won't be updated
     * while a frame is in the middle of being processed. */

    if (!pBitGrid || !pBitGrid->width || !pBitGrid->height || !pBitGrid->bitmapSize) {
        LTLOG_REDALERT("zones.set.invalidinput", "Invalid input to SetMotionZones. Zones update aborted.");
        return;
    }

    u32 nBufferBytes = (pBitGrid->width * pBitGrid->height + 7) / 8;
    if (pBitGrid->bitmapSize != nBufferBytes) {
        LTLOG_REDALERT("zones.set.invalid.bufsize", "Total bytes %lu doesn't match grid size %u x %u", LT_Pu32(pBitGrid->bitmapSize), pBitGrid->width, pBitGrid->height);
        return;
    }

    u32 xScale = S.horizontalMoxels / pBitGrid->width;
    u32 yScale = S.verticalMoxels / pBitGrid->height;
    u32 nBitmapBytes = nBufferBytes * xScale * yScale;
    /* We require that the incoming BitGrid dimensions must match the values read from product configuration. */
    if ((pBitGrid->width != BitGridWidth) || (pBitGrid->height != BitGridHeight)) {
        LTLOG_REDALERT("zones.set.invalid.dim", "Bit grid size %u x %u does not match expected size %u x %u.", pBitGrid->width, pBitGrid->height, BitGridWidth, BitGridHeight);
        return;
    }

    if (s_motionZoneBitGrid) {
        lt_free(s_motionZoneBitGrid);
    }
    LT_SIZE motionZoneBitGridSize = sizeof(LTMediaMotionDetection_MotionZoneBitGrid) + pBitGrid->bitmapSize;
    s_motionZoneBitGrid = lt_malloc(motionZoneBitGridSize);
    if (!s_motionZoneBitGrid) {
        LTLOG_REDALERT("zones.set.bitgrid.malloc", "Could not allocate memory for local copy of zone setting bitGrid.");
        return;
    }
    lt_memcpy(s_motionZoneBitGrid, pBitGrid, motionZoneBitGridSize);

    /* Create a new bitmap to replace our current motion zone bitmap with. */
    LTMediaMotionDetection_MotionZoneBitmap *pNewMotionZoneBitmap = lt_malloc(sizeof(LTMediaMotionDetection_MotionZoneBitmap));
    if (pNewMotionZoneBitmap) { pNewMotionZoneBitmap->pData = lt_malloc(nBitmapBytes); }
    if (!pNewMotionZoneBitmap || !pNewMotionZoneBitmap->pData) {
        LTLOG_REDALERT("zones.set.bmp.malloc.fail", "Could not allocate memory for the new motion zone bitmap.");
        if (pNewMotionZoneBitmap) { lt_free(pNewMotionZoneBitmap); }
        return;
    }
    pNewMotionZoneBitmap->nBitmapWords = BitmapWords;
    pNewMotionZoneBitmap->nBitmapWordLen = BITMAP_WORD_LEN;
    lt_memset(pNewMotionZoneBitmap->pData, 0, BitmapWords * BYTES_PER_WORD);

    /* Set the motion zone bitmap, scaling up input to match the dimensions of the base motion output. */
    u32 userGridLen = pBitGrid->width * pBitGrid->height;
    for (u32 userGridCurBit = 0; userGridCurBit < userGridLen; userGridCurBit++) {
        // Get current bit from user bit grid (coarse scale)
        u16 currentBit = ((pBitGrid->bitmap[userGridCurBit / 8] << (userGridCurBit % 8)) & 0x80) ? 1 : 0;

        // Replicate single bit from user grid X*Y times across and down the internal bit grid, to scale to same dimensions as the moxel frame
        u32 startY = (userGridCurBit / pBitGrid->width) * yScale;
        u32 startX = (userGridCurBit % pBitGrid->width) * xScale;
        for (u32 y = startY; y < startY + yScale; y++) {
            for (u32 x = startX; x < startX + xScale; x++) {
                u32 internalGridCurBit = x + y * S.horizontalMoxels;
                u32 wordIndex = internalGridCurBit / pNewMotionZoneBitmap->nBitmapWordLen;
                u32 bitIndex = internalGridCurBit % pNewMotionZoneBitmap->nBitmapWordLen;
                pNewMotionZoneBitmap->pData[wordIndex] |= (currentBit << bitIndex);
            }
        }
    }

    ILTThread *iThread = lt_gethandleinterface(ILTThread, s_hMotionDetectionThread);
    if (LTMediaMotionDetection_IsDetectionEngineRunning()) {
        iThread->QueueTaskProcIfRequired(s_hMotionDetectionThread, SetBitmapProc, NULL, pNewMotionZoneBitmap);
    } else {
        /* If the detection engine is not running then we can safely swap the motion zone bitmaps in the current thread. */
        SetBitmapProc(pNewMotionZoneBitmap);
    }
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hMotionDetectionEvent);
    iEvent->NotifyEvent(s_hZoneStateEvent, kLTMediaMotionState_SetMotionZone);
}

static void LTMediaMotionDetection_GetMotionZones(void **valueOut, u32 *valueOutSize) {
    /* Fetch a copy of the motion zone setting copy from this library, rather than reading it from flash memory.
     * If the zone setting doesn't exist locally yet then the caller will see that *valueOut is a null pointer. */

    if (!valueOut || !valueOutSize) { return; }
    if (s_motionZoneBitGrid) {
        *valueOutSize = sizeof(LTMediaMotionDetection_MotionZoneBitGrid) + s_motionZoneBitGrid->bitmapSize;
        *valueOut = lt_memdup(s_motionZoneBitGrid, *valueOutSize);
    } else {
        *valueOutSize = 0;
        *valueOut = NULL;
    }
}

bool LTMediaMotionDetection_IsMotionZoneEnabled(void) {
    return s_bMotionZoneEnabled;
}

static void LTMediaMotionDetection_EnableMotionZones(bool enable) {
    s_bMotionZoneEnabled = enable;
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hMotionDetectionEvent);
    if (enable) iEvent->NotifyEvent(s_hZoneStateEvent, kLTMediaMotionState_EnableMotionZone);
}

static u8 LTMediaMotionDetection_GetMotionEventMaxSnapshots(void) {
    return s_maxSnapshotsPerMotionEvent;
}

static void LTMediaMotionDetection_SetMotionEventMaxSnapshots(u8 maxSnapshots) {
    if (maxSnapshots == 0) maxSnapshots = 1;
    if (maxSnapshots > 3) maxSnapshots = 3;     // feature is limited to 3 snapshots for now

    s_maxSnapshotsPerMotionEvent = maxSnapshots;
    LTLOG("set.max.snapshots", "Set max snapshots per motion event to: %u", maxSnapshots);
}

static void LTMediaMotionDetection_SetMotionTimingMode(LTMediaMotionTimingMode mode) {
    s_motionTimingMode = mode;
}

static void LTMediaMotionDetection_OnMotionEvent(LTMediaMotionDetection_MotionEventProc *pMotionEventProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hMotionDetectionEvent);
    iEvent->RegisterForEvent(s_hMotionDetectionEvent, pMotionEventProc, pClientDataReleaseProc, pClientData, false);
}

static void LTMediaMotionDetection_NoMotionEvent(LTMediaMotionDetection_MotionEventProc *pMotionEventProc) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hMotionDetectionEvent);
    iEvent->UnregisterFromEvent(s_hMotionDetectionEvent, pMotionEventProc);
}

static void LTMediaMotionDetection_OnMetadataEvent(LTMediaMotionDetection_MetadataEventProc *pMetadataEventProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hMotionMetadataEvent);
    iEvent->RegisterForEvent(s_hMotionMetadataEvent, pMetadataEventProc, pClientDataReleaseProc, pClientData, false);
}

static void LTMediaMotionDetection_NoMetadataEvent(LTMediaMotionDetection_MetadataEventProc *pMetadataEventProc) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hMotionMetadataEvent);
    iEvent->UnregisterFromEvent(s_hMotionMetadataEvent, pMetadataEventProc);
}

static void LTMediaMotionDetection_OnZoneStateEvent(LTMediaMotionDetection_ZoneStateEventProc *pZoneStateEventProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hZoneStateEvent);
    iEvent->RegisterForEvent(s_hZoneStateEvent, pZoneStateEventProc, pClientDataReleaseProc, pClientData, false);
}

static void LTMediaMotionDetection_NoZoneStateEvent(LTMediaMotionDetection_ZoneStateEventProc *pZoneStateEventProc) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hZoneStateEvent);
    iEvent->UnregisterFromEvent(s_hZoneStateEvent, pZoneStateEventProc);
}

static void LTMediaMotionDetection_SetSensitivity(u8 value) {
    if (value > 100) {
        LTLOG_YELLOWALERT("set.sensitivity.toohigh", "SetSensitivity() called with value %lu. Capping at 100.", LT_Pu32(value));
        s_motionDetectionSensitivity = 100;
    } else {
        s_motionDetectionSensitivity = value;
    }
    u32 sensitivityBin = s_motionDetectionSensitivity / 10;

    /* Choose threshold and minimum island size based on current sensitivity mode and motion sensitivity setting (0 -
     * 100), and update the algorithm's parameters accordingly. */
    MotionSensitivityParameters sensitivityParams = s_motionSensitivityTables[s_motionSensitivityMode][sensitivityBin];
    s_algorithm->API->SetDifferenceThreshold(s_algorithm, sensitivityParams.sensitivityThreshold);
    s_algorithm->API->SetMinimumIslandSize(s_algorithm, sensitivityParams.minimumIslandSize);

    /* Adjust heatmap sensitivity parameters based on the motion sensitivity setting (0 - 100). */
    s_algorithm->API->SetHeatmapSensitivity(s_algorithm, s_motionDetectionSensitivity);
}

static u8 LTMediaMotionDetection_GetSensitivity(void) {
    return s_motionDetectionSensitivity;
}

/* Public getters, setters and wrappers exposed for unit testing */
static void GetMotionStateParameters(u32 *pMinMotionFramesForEvent, u32 *pMinFramesUntilEventEnd, u32 *pMaxFramesUntilFirstSnapshot, u32 *pMaxFramesForBestSnapshot,u32 *pMinMotionFramesEventDuration) {
    if (pMinMotionFramesForEvent) *pMinMotionFramesForEvent = s_MinMotionFramesForEvent;
    if (pMinFramesUntilEventEnd) *pMinFramesUntilEventEnd = s_MinFramesUntilEventEnd;
    if (pMaxFramesUntilFirstSnapshot) *pMaxFramesUntilFirstSnapshot = s_MaxFramesUntilFirstSnapshot;
    if (pMaxFramesForBestSnapshot) *pMaxFramesForBestSnapshot = s_MaxFramesForBestSnapshot;
    if (pMinMotionFramesEventDuration) *pMinMotionFramesEventDuration = s_MinFramesMotionEventDuration;
}

static void GetMotionState(LTMediaMotionState *pMotionState, u32 *pFramesOfMotion, u32 *pFramesNoMotion, float *pBestBoundingBoxWeighting, u32 *pFramesSinceSnapshotAvailable, u32 *pFramesSinceMotionStart, u8 *pSnapshotsUploaded) {
    if (pMotionState) { *pMotionState = s_motionState; }
    if (pFramesOfMotion) { *pFramesOfMotion = s_framesOfMotion; }
    if (pFramesNoMotion) { *pFramesNoMotion = s_framesNoMotion; }
    if (pBestBoundingBoxWeighting) { *pBestBoundingBoxWeighting = s_bestBoundingBoxWeighting; }
    if (pFramesSinceSnapshotAvailable) { *pFramesSinceSnapshotAvailable = s_framesSinceSnapshotAvailable; }
    if (pFramesSinceMotionStart) { *pFramesSinceMotionStart = s_framesSinceMotionStart; }
    if (pSnapshotsUploaded) { *pSnapshotsUploaded = s_snapshotsUploaded; }
}

void GetMotionZoneBitmap(LTMediaMotionDetection_MotionZoneBitmap **pMotionZoneBitmap) {
    *pMotionZoneBitmap = s_motionZoneBitmap;
}

static void ResetMotionState(void) {
    s_motionState = kLTMediaMotionState_Stopped;
    s_snapshotState = kLTMotionSnapshotState_Assessing;
    s_framesOfMotion = 0;
    s_framesNoMotion = 0;
    s_bestBoundingBoxWeighting = LT_DBL_MAX;
    s_framesSinceSnapshotAvailable = 0;
    s_framesSinceMotionStart = 0;
    s_snapshotsUploaded = 0;

    // Reset object detection results to prevent stale results from previous events
    if (s_pObjectDetection) {
        s_pObjectDetection->API->ResetResults(s_pObjectDetection);
    }
}

static LTMediaMotionDetectionAlgorithm *LTMediaMotionDetection_GetActiveAlgorithm(void) {
    ltobject_addref(s_algorithm);
    return s_algorithm;
}

static void SetMotionEventStatsLogging(bool bLogMotionEventStats) {
    s_bLogMotionEventStats = bLogMotionEventStats;
}

static void LTMediaMotionDetection_SetDetectionFlags(LTMediaMotionFlags flags) {
    s_detectionFlags = flags;
    if (s_algorithm->API->IsObjectDetectionEnabled(s_algorithm)) {
        if (s_pObjectDetection && s_detectionFlags == kLTMediaMotionFlags_All) {
            lt_destroyobject(s_pObjectDetection);
            s_pObjectDetection = NULL;
            LTLOG("motion.flag.no.objdet", "Object detection object destroyed");
        }
        if (!s_pObjectDetection && s_detectionFlags != kLTMediaMotionFlags_All) {
            s_pObjectDetection = lt_createobject(LTMediaObjectDetection);
            LTLOG("motion.flag.objdet", "Object detection object created");
        }
    }
}

static LTMediaMotionFlags LTMediaMotionDetection_GetDetectionFlags(void) {
    return s_detectionFlags;
}

static void SetMinFramesMotionEventDuration(u32 minFramesMotionEventDuration) {
    s_MinFramesMotionEventDuration = minFramesMotionEventDuration;
}

bool CheckHiddenIntegerSetting(LTSystemSettings *systemSettings, const char *settingName, s64 *value);
bool CheckHiddenStringSetting(LTSystemSettings *systemSettings, const char *settingName, char **value);
static bool DecodeSensitivityTableString(const char *stringSetting, u8 *pDecodeBuffer, u32 nDecodeBufferSize);
static LTString SensitivityTableToLogString(u32 tableIndex);

static void LTMediaMotionDetection_ApplyConfiguration(void) {
    LTSystemSettings *systemSettings = NULL;
    systemSettings = lt_openlibrary(LTSystemSettings);
    if (!systemSettings) {
        LTLOG_YELLOWALERT("apply.cfg.sysset.fail", "Could not open LTSystemSettings while applying ConfigService setting overrides.");
        return;
    }
    s64 value;
    LTString stringValue = ltstring_create("");
    bool ret = false;

    /* For each overridable motion parameter, check a hidden ConfigService setting to see if the default value needs
     * to be overridden. */

    /* Maximum number of snapshots */
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/motionEventMaxSnapshots", &value);
    if (ret && value >= 0) { LTMediaMotionDetection_SetMotionEventMaxSnapshots((u8)value); }

    /* Minimum frames with motion detected before a motion event starts */
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/minMotionFramesForEvent", &value);
    if (ret && value >= 0) { s_MinMotionFramesForEvent = value; }

    /* Minimum frames with no motion before current motion event stops */
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/minFramesUntilEventEnd", &value);
    if (ret && value >= 0) { s_MinFramesUntilEventEnd = value; }

    /* Maximum frames until first snapshot notification */
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/maxFramesUntilFirstSnapshot", &value);
    if (ret && value >= 0) { s_MaxFramesUntilFirstSnapshot = value; }

    /* Maximum frames until latest snapshot is notified */
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/maxFramesForBestSnapshot", &value);
    if (ret && value >= 0) { s_MaxFramesForBestSnapshot = value; }

    /* Minimum number of frames for the motion event */
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/minFramesMotionEventDuration", &value);
    if (ret && value >= 0) { s_MinFramesMotionEventDuration = value; }

    /* Maximum number of frames to allow the motion event to be aborted */
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/maxFramesToAllowAbort", &value);
    if (ret && value >= 0) { s_MaxFramesToAllowAbort = value; }

    /* Motion detection sensitivity tables */
    const char *sensitivityTableSettingsKeys[kLTMotionSensitivityMode_Count] = { "motion/sensitivityTables/Normal", "motion/sensitivityTables/NightIR" };
    for (u32 sensitivityModeIndex = 0; sensitivityModeIndex < kLTMotionSensitivityMode_Count; sensitivityModeIndex++) {
        ret = CheckHiddenStringSetting(systemSettings, sensitivityTableSettingsKeys[sensitivityModeIndex], &stringValue);
        if (ret && !DecodeSensitivityTableString(stringValue, (u8 *)s_motionSensitivityTables[sensitivityModeIndex], sizeof(MotionSensitivityTable))) {
            LTLOG_YELLOWALERT("sense.cfg.override.fail", "Failed to read sensitivity config override for %s", sensitivityTableSettingsKeys[sensitivityModeIndex]);
        }
    }

    /* Check hidden ConfigService settings for algorithm parameters and override with any values found. */
    s_algorithm->API->ApplyConfiguration(s_algorithm);

    /* Recalculate the algorithm's various sensitivity parameters (including heatmap sensitivity) using the new
     * configuration values. */
    LTMediaMotionDetection_SetSensitivity(s_motionDetectionSensitivity);

    lt_free(stringValue);
    lt_closelibrary(systemSettings);
}

static u32 GetMotionFrameCount(void) {
    return s_totalMotionFrameCount;
}

static void ResetMotionFrameCount(void) {
    s_totalMotionFrameCount = 0;
}

/*_________________________________
  Library private implementation */

static char *LTMediaMotionDetection_TimeWithUnits(char *buf, int bufsize, LTTime const *t, bool bAbbreviatedUnits) {
    u32 nSeconds      = (u32)LTTime_GetSeconds(*t);
    u32 nMicroseconds = (u32)LTTime_GetMicroseconds(LTTime_FractionalSeconds(*t));
    u32 nNanoseconds  = (u32)LTTime_GetNanoseconds(LTTime_FractionalMicroseconds(*t));
    const char *pSeconds      = bAbbreviatedUnits ? "s" : ((1 == nSeconds) ? "second" : "seconds");
    const char *pMicroseconds = bAbbreviatedUnits ? "us" : ((1 == nMicroseconds) ? "microsecond" : "microseconds");
    const char *pNanoseconds  = bAbbreviatedUnits ? "ns" : ((1 == nNanoseconds) ? "nanosecond" : "nanoseconds");
    if (nSeconds && nNanoseconds) {
        lt_snprintf(buf, bufsize, "%ld %s, %06ld %s, %03ld %s", nSeconds, pSeconds, nMicroseconds, pMicroseconds, nNanoseconds, pNanoseconds);
    } else if (nSeconds) {
        lt_snprintf(buf, bufsize, "%ld %s, %06ld %s", nSeconds, pSeconds, nMicroseconds, pMicroseconds);
    } else {
        lt_snprintf(buf, bufsize, "%ld %s, %03ld %s", nMicroseconds, pMicroseconds, nNanoseconds, pNanoseconds);
    }
    return buf;  /* return a pointer to the string */
}

/** Log the kernel time difference between the supplied start time and the current (end) time, as well as whether the
 *  frame had motion. Depending on the motion timing mode a different context message will be displayed to confirm
 *  what the timing represents. */
static void LTMediaMotionDetection_LogTimeDifference(LTTime startTime, bool motionFrame) {
    char *modeContextStr;

    switch (s_motionTimingMode) {
        case kLTMediaMotionTimingMode_CheckMotion:
            modeContextStr = "Timing check of motion frame only";
            break;
        case kLTMediaMotionTimingMode_FullMotionProc:
            modeContextStr = "Timing whole motion proc call";
            break;
        case kLTMediaMotionTimingMode_Off:
            return;
        default:
            LTLOG_YELLOWALERT("timing.err", "Error: Timing Mode has unknown value! Could not log time.");
            return;
    }

    u32 strbufSize = 80;
    char strbuf[strbufSize];
    LTTime endTime = LT_GetCore()->GetKernelTime();
    LTTime elapsed = LTTime_Subtract(endTime, startTime);
    char *processTimeStr = LTMediaMotionDetection_TimeWithUnits(strbuf, strbufSize, &elapsed, true);
    char *motionFrameStr = motionFrame ? "true" : "false";
    LTLOG("timing.diff", "%s. Motion: %s. Time taken: %s", modeContextStr, motionFrameStr, processTimeStr);
}

static void LTMediaMotionDetection_MotionDetectionEventDispatchProc(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    /* This function is called by the LTEvent subsystem in the context of the thread that registered pEventProc,
       Its job is to decode the event notification callback procedure type and its args and call the decoded
       event proc directly, using the decoded arguments and the client data registered by the client. */
    LT_UNUSED(hEvent);
    (*(LTMediaMotionDetection_MotionEventProc *)pEventProc)(
        (LTMediaMotionState)LTArgs_s32At(0, pEventArgs),
        (u8 *)LTArgs_pointerAt(1, pEventArgs),
        (LTMediaMotionDetection_Snapshot *)LTArgs_pointerAt(2, pEventArgs),
        pEventProcClientData);
}

static void LTMediaMotionDetection_ZoneStateEventDispatchProc(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    LT_UNUSED(hEvent);
    (*(LTMediaMotionDetection_ZoneStateEventProc *)pEventProc)(
        (LTMediaZoneState)LTArgs_s32At(0, pEventArgs),
        pEventProcClientData);
}

static void CleanupSnapshot(void) {
    /* Do not clean up snapshot if clients still have a reference to the snapshot */
    if (s_snapshotState == kLTMotionSnapshotState_Notifying) return;

    LTMediaMotionDetection_FreeRegionList(s_snapshotMotionROIs);
    s_snapshotMotionROIs = NULL;
    lt_free(s_pMotionSnapshot);
    s_pMotionSnapshot = NULL;
    s_snapshotState = kLTMotionSnapshotState_Assessing;
}

static void LTMediaMotionDetection_MotionDetectionEventDispatchCompleteProc(LTEvent hEvent, LTArgs *pEventArgs) {
    /* This function gets called after all of registered event clients have been fully notified.
     * It will free the motion snapshot data, if available.
     */
    LTMediaMotionState state = LTArgs_s32At(0, pEventArgs);
    if (state == kLTMediaMotionState_Snapshot) {
        LTLOG_DEBUG("snap.done", NULL);
        // Advance snapshot states and clean up snapshot
        s_snapshotState = kLTMotionSnapshotState_Available;     // ensure state is no longer "Notifying" before cleanup
        CleanupSnapshot();
    }
    LT_UNUSED(hEvent);
}

static void LTMediaMotionDetection_MotionMetadataEventDispatchProc(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    /* This function is called by the LTEvent subsystem in the context of the thread that registered pEventProc,
       Its job is to decode the event notification callback procedure type and its args and call the decoded
       event proc directly, using the decoded arguments and the client data registered by the client. */
    LT_UNUSED(hEvent);
    (*(LTMediaMotionDetection_MetadataEventProc *)pEventProc)(
        (LTMediaMotionDetection_Metadata *)LTArgs_pointerAt (0, pEventArgs),
        (u8 *)LTArgs_pointerAt (1, pEventArgs),
        pEventProcClientData);
}

static void LTMediaMotionDetection_MotionMetadataEventDispatchCompleteProc(LTEvent hEvent, LTArgs *pEventArgs) {
    /* This function gets called after all of registered event clients have been fully notified.  Its job is to
       free the region list we allocated and notified the event with. */
    LT_UNUSED(hEvent);
    LTMediaMotionDetection_Metadata *metadata = LTArgs_pointerAt (0, pEventArgs);
    LT_UNUSED(metadata);
}

void AddMoxelToEnergyHistogram(u8 moxelValue, bool preDownsampling) {
    /* Add to either the preDownsampling energy histogram or the postDownsampling energy histogram. */
    u32 *pBinCounts = preDownsampling ? s_preDownsamplingEnergyHistogramCounts :
                                        s_postDownsamplingEnergyHistogramCounts;
    /* Find the lowest sensitivity setting that would pick up the moxel and increment the corresponding histogram
     * bin. */
    for (u8 sensitivityIndex = 0; sensitivityIndex < MOTION_SENSITIVITY_BUCKETS; sensitivityIndex++) {
        if (moxelValue >= s_motionSensitivityTables[s_motionSensitivityMode][sensitivityIndex].sensitivityThreshold) {
            pBinCounts[sensitivityIndex]++;
            return;
        }
    }
    /* If we got here then the moxel would not have triggered for any sensitivity threshold on the curve, so increment
     * the final bin to indicate this. */
    pBinCounts[ENERGY_HISTOGRAM_BINS - 1]++;
}

static void SetBitmapProc(void *pClientData) {
    /* Set the motion zone bitmap to point at the provided cloned bitmap. This must be done in the motion detection
     * thread via a TaskProc to avoid invalidating a pointer that the motion detection thread is currently using from
     * another thread, i.e. the genesis thread (unless the detection engine is not running in which case it must
     * instead be called in the active thread - this happens when the camera boots and motion detection is disabled). */
    LTMediaMotionDetection_MotionZoneBitmap *pNewMotionZoneBitmap = (LTMediaMotionDetection_MotionZoneBitmap *)pClientData;

    /* Swap the motion zone bitmap for our clone and free the old bitmap memory.
     * These frees should be safe since this proc is always called in the motion detection thread when motion detection
     * is running. */
    if (s_motionZoneBitmap) {
        lt_free(s_motionZoneBitmap->pData);
        lt_free(s_motionZoneBitmap);
    }
    s_motionZoneBitmap = pNewMotionZoneBitmap;
}


static bool ShouldSendSnapshotBasedOnObjectDetection(void) {

    if (!s_algorithm->API->IsObjectDetectionEnabled(s_algorithm)) {
        LTLOG_DEBUG("snapshot.objdet.disabled", "Object detection is disabled - sending all snapshots");
        return true; // Algorithm doesn't want object detection - send all snapshots
    }

    if (s_detectionFlags == kLTMediaMotionFlags_All) {
        LTLOG_DEBUG("snapshot.objdet.all", "All object detection flags are set - sending all snapshot");
        return true;
    }

    // If object detection is not available, always send snapshots
    if (!s_pObjectDetection) {
        LTLOG_DEBUG("snapshot.objdet.unavailable", "Object detection LTObject is unavailable - sending all snapshots");
        return true;
    }

    // Get actual detection results from object detection system
    LTMediaMotionFlags detectionResults = s_pObjectDetection->API->GetResults(s_pObjectDetection);

    if (s_detectionFlags & detectionResults) {
        LTLOG_DEBUG("snapshot.objdet.match", "Detected objects (0x%08lx) allowed by user settings (0x%08lx)", LT_Pu32(detectionResults), LT_Pu32(s_detectionFlags));
        return true;
    }

    LTLOG("snapshot.objdet.filtered", "Object detection found no relevant objects - filtering snapshot");
    return false;
}

/** Function to calculate euclidean distance from the image centre, for the first ROI (guaranteed largest). */
static float CalculateDistanceFromImageCentre(void) {
    float BBoxXCenter, BBoxYCenter, HorizontalCenter, VerticalCenter;

    // calculate image centre
    HorizontalCenter = S.horizontalMoxels / 2.0;
    VerticalCenter = S.verticalMoxels / 2.0;

    // calculate bounding box center
    LTMediaMotionDetection_Regions *pMotionROIs = s_algorithm->API->GetMotionROIs(s_algorithm);
    BBoxXCenter = (pMotionROIs->region[0].x1 + pMotionROIs->region[0].x2) / 2.0;
    BBoxYCenter = (pMotionROIs->region[0].y1 + pMotionROIs->region[0].y2) / 2.0;
    lt_free(pMotionROIs);

    float dx = BBoxXCenter - HorizontalCenter;
    float dy = BBoxYCenter - VerticalCenter;

    float dx_squared = dx * dx;
    float dy_squared = dy * dy;

    float distance = lt_sqrtf(dx_squared + dy_squared);

    return distance;
}

static void TryNotifySnapshot(const char *notifyReason) {
    // Snapshot data not available to be sent to clients
    if (s_snapshotState != kLTMotionSnapshotState_Available) return;

    // Don't upload if motion event was aborted or allowed snapshots has been reached
    if (s_motionState == kLTMediaMotionState_Aborted || s_snapshotsUploaded >= s_maxSnapshotsPerMotionEvent) {
        // Suppress additional snapshots for long-lived motion events
        // Current snapshot is cleaned up, and the assessment function won't trigger any new ones
        CleanupSnapshot();
        char *extraReason = "(max uploads reached)";
        if (s_motionState == kLTMediaMotionState_Aborted) extraReason = "(motion event aborted)";

        LTLOG("snap.suppress", "%s %s", notifyReason, extraReason);
        return;
    }

    if (!ShouldSendSnapshotBasedOnObjectDetection()) {
        LTLOG("snap.objdet.filtered", "%s - filtered by object detection", notifyReason);
        CleanupSnapshot();
        return;
    }

    // Advance state before notifying to protect s_pMotionSnapshot from being freed before DispatchComplete
    s_snapshotsUploaded++;
    s_snapshotState = kLTMotionSnapshotState_Notifying;
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hMotionDetectionEvent);
    iEvent->NotifyEvent(s_hMotionDetectionEvent, kLTMediaMotionState_Snapshot, s_MotionUuid, s_pMotionSnapshot);

    s_bestBoundingBoxWeighting = LT_DBL_MAX;
    s_framesSinceSnapshotAvailable = 0;

    LTLOG("snap.notify", "%s", notifyReason);
}

static void AssessMotionSnapshot(float curBoundingBoxWeighting) {

    // Snapshot media source does not exist so we can't request a snapshot
    if (!s_mediaSourceSnapshot) return;

    // Stop assessing after all allowed snapshots have been uploaded
    if (s_snapshotsUploaded && (s_snapshotsUploaded >= s_maxSnapshotsPerMotionEvent)) return;

    // Pause assessing if a snapshot has been requested or is being notified
    if (s_snapshotState == kLTMotionSnapshotState_Requested || s_snapshotState == kLTMotionSnapshotState_Notifying) return;

    // If the maximum time to hold a snapshot before it is notified has passed, notify the snapshot
    if (s_snapshotState == kLTMotionSnapshotState_Available) {
        s_framesSinceSnapshotAvailable++;

        if (s_snapshotsUploaded == 0 && s_framesSinceMotionStart >= s_MaxFramesUntilFirstSnapshot) {
            // If no snapshots have been uploaded yet we make sure the cloud gets _something_ within a certain time
            TryNotifySnapshot("First snapshot time expired");
        } else if (s_snapshotsUploaded > 0 && s_framesSinceSnapshotAvailable >= s_MaxFramesForBestSnapshot) {
            // Otherwise regularly send the best snapshot available - this is timed from the first triggered snapshot after the last notify (not necessarily the most recent)
            TryNotifySnapshot("Best snapshot time expired");
        }

        // Pause assessing (again) if a snapshot is *now* being notified
        if (s_snapshotState == kLTMotionSnapshotState_Notifying) return;
    }

    // Check if the latest motion ROI has better weighting than the ROI of the last snapshot taken
    if (curBoundingBoxWeighting < s_bestBoundingBoxWeighting) {
        // Cleanup if any existing snapshot
        CleanupSnapshot();

        // Save the current motion ROIs so snapshot event can pass along the ROIs associated with the image
        s_snapshotMotionROIs = s_algorithm->API->GetMotionROIs(s_algorithm);

        // Trigger a snapshot
        ILTMediaSource *iMediaSource = lt_gethandleinterface(ILTMediaSource, s_mediaSourceSnapshot);
        iMediaSource->Trigger(s_mediaSourceSnapshot);

        // Advance state and note ROI
        s_bestBoundingBoxWeighting = curBoundingBoxWeighting;
        s_snapshotState = kLTMotionSnapshotState_Requested;
        P("snap.trig", "Snapshot triggered");
    }
}

static void MotionStartEventStatsLogging(void) {
    if (!s_bLogMotionEventStats) return;

    char motionEventUUID[40];
    if (!s_pUtilityByteOps->UUIDToString(s_MotionUuid, motionEventUUID, sizeof(motionEventUUID))) {
        motionEventUUID[0] = '\0';
    }

    /* Fetch current night vision flag to determine daynight mode, and current sensor ISO value. */
    LTMediaCameraSensor sensorParams = {0};
    S.pDeviceMedia->GetProperty("camera.sensor", &sensorParams);
    const char *modeString = sensorParams.flags & LTMediaCameraFlags_NightVision ? "night" : "day";

    /* Fetch night vision distance - this does not directly specify whether the IR LEDs are on/off but describes the
     * camera behaviour when in night mode. When looked at in conjunction with the daynight mode, the IR LED status can
     * be inferred. (If the camera is in day mode then the IR LEDs should be off regardless of the night vision
     * distance setting.) */
    u32 nightVisionDistance = 0;
    S.pDeviceMedia->GetProperty("nightvision.distance", &nightVisionDistance);
    const char *nightVisionDistanceString = nightVisionDistance == 0 ? "off" : nightVisionDistance == 1 ? "near" : nightVisionDistance == 2 ? "far" : "on (unknown)";

    /* IMPORTANT NOTE: the following logs assume the value of ENERGY_HISTOGRAM_BINS to be 12 and the value of
     * MAX_NUM_MOTION_REGIONS to be 3. If this changes, the logs must be updated accordingly. */

    LTMediaMotionDetectionAlgorithm_Parameters algoParams;
    s_algorithm->API->GetParameters(s_algorithm, &algoParams);

    MotionIslandSizes biggestIslandSizes = s_algorithm->API->GetBiggestIslandSizes(s_algorithm);

    /* Log the main motion event started statistics. */
    LTLOG("evt.started.stats", "Motion started event. UUID: %s. Trigger: %lu. Sensitivity setting: %lu. Actual threshold: %lu. DayNight Mode: %s. NV Distance: %s. ISO Value: %lu. Biggest island sizes: %lu %lu %lu. Moxel energy post-downsampled histogram: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu.",
        motionEventUUID,
        LT_Pu32(s_motionCause),
        LT_Pu32(s_motionDetectionSensitivity),
        LT_Pu32(algoParams.differenceThreshold),
        modeString,
        nightVisionDistanceString,
        LT_Pu32(sensorParams.totalIso),
        LT_Pu32(biggestIslandSizes.sizes[0]),
        LT_Pu32(biggestIslandSizes.sizes[1]),
        LT_Pu32(biggestIslandSizes.sizes[2]),
        LT_Pu32(s_postDownsamplingEnergyHistogramCounts[0]),
        LT_Pu32(s_postDownsamplingEnergyHistogramCounts[1]),
        LT_Pu32(s_postDownsamplingEnergyHistogramCounts[2]),
        LT_Pu32(s_postDownsamplingEnergyHistogramCounts[3]),
        LT_Pu32(s_postDownsamplingEnergyHistogramCounts[4]),
        LT_Pu32(s_postDownsamplingEnergyHistogramCounts[5]),
        LT_Pu32(s_postDownsamplingEnergyHistogramCounts[6]),
        LT_Pu32(s_postDownsamplingEnergyHistogramCounts[7]),
        LT_Pu32(s_postDownsamplingEnergyHistogramCounts[8]),
        LT_Pu32(s_postDownsamplingEnergyHistogramCounts[9]),
        LT_Pu32(s_postDownsamplingEnergyHistogramCounts[10]),
        LT_Pu32(s_postDownsamplingEnergyHistogramCounts[11]));

    /* Log less important statistics separately. */
    const char *downsamplingStrategyString = algoParams.downsamplingStrategy == kLTMediaMotionDownsamplingStrategy_MaxPooling ? "max" : algoParams.downsamplingStrategy == kLTMediaMotionDownsamplingStrategy_AveragePooling ? "avg" : "none";
    const char *roiCalculationStrategyString = algoParams.roiCalculationStrategy == kLTMediaMotionROICalculationStrategy_MultipleROIs ? "multi" : algoParams.roiCalculationStrategy == kLTMediaMotionROICalculationStrategy_SingleROI ? "single" : "unknown";
    LTLOG("evt.started.stats.extra", "UUID: %s. Downsampling Scale Factor: %lu. Minimum Island Size: %lu. Use Pooling: %s. ROIs: %s. Moxel energy pre-downsampled histogram: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu.",
        motionEventUUID,
        LT_Pu32(algoParams.downsamplingScaleFactor),
        LT_Pu32(algoParams.minimumIslandSize),
        downsamplingStrategyString,
        roiCalculationStrategyString,
        LT_Pu32(s_preDownsamplingEnergyHistogramCounts[0]),
        LT_Pu32(s_preDownsamplingEnergyHistogramCounts[1]),
        LT_Pu32(s_preDownsamplingEnergyHistogramCounts[2]),
        LT_Pu32(s_preDownsamplingEnergyHistogramCounts[3]),
        LT_Pu32(s_preDownsamplingEnergyHistogramCounts[4]),
        LT_Pu32(s_preDownsamplingEnergyHistogramCounts[5]),
        LT_Pu32(s_preDownsamplingEnergyHistogramCounts[6]),
        LT_Pu32(s_preDownsamplingEnergyHistogramCounts[7]),
        LT_Pu32(s_preDownsamplingEnergyHistogramCounts[8]),
        LT_Pu32(s_preDownsamplingEnergyHistogramCounts[9]),
        LT_Pu32(s_preDownsamplingEnergyHistogramCounts[10]),
        LT_Pu32(s_preDownsamplingEnergyHistogramCounts[11]));

    /* Log the heatmap parameters. */
    MotionHeatmapParameters heatmapParameters = {0};
    s_algorithm->API->GetHeatmapParameters(s_algorithm, &heatmapParameters);
    LTLOG_SERVER("evt.started.stats.heatmap.params", "UUID: %s. Enable Heatmap: %lu. Heatmap Cap: %.1f. Heatmap Threshold: %.1f. Heatmap Gain Intercept 1: %.2f. Heatmap Gain Intercept 2: %.2f. Heatmap Gain Gradient 1: %.3f. Heatmap Gain Gradient 2: %.3f. Heatmap Drop Gradient: %.3f. Heatmap Gain: %.2f. Heatmap Drop: %.2f.",
        motionEventUUID,
        LT_Pu32(heatmapParameters.bEnabled),
        heatmapParameters.cap,
        heatmapParameters.threshold,
        heatmapParameters.gainGradientHigh,
        heatmapParameters.gainInterceptHigh,
        heatmapParameters.gainGradientLow,
        heatmapParameters.gainGradientHigh,
        heatmapParameters.dropGradient,
        heatmapParameters.gain,
        heatmapParameters.drop);

    /* Log BFS parameters. */
    const char *bfsEnabledString = s_algorithm->API->IsBFSEnabled(s_algorithm) ? "enabled" : "disabled";
    LTLOG_SERVER("evt.started.stats.bfs.settings", "UUID: %s. BFS: %s. BFS distance: %lu",
        motionEventUUID,
        bfsEnabledString,
        LT_Pu32(s_algorithm->API->GetBFSDistance(s_algorithm)));
}

static void MotionStopEventStatsLogging(void) {
    if (!s_bLogMotionEventStats) return;

    char motionEventUUID[40];
    if (!s_pUtilityByteOps->UUIDToString(s_MotionUuid, motionEventUUID, sizeof(motionEventUUID))) {
        motionEventUUID[0] = '\0';
    }

    /* Log motion event stopped statistics. */
    char *evtType = "stopped";
    if (s_motionState == kLTMediaMotionState_Aborted) evtType = "aborted";
    LTLOG("evt.stopped.stats", "Motion %s event. UUID: %s. Number of event frames: %lu. Number of snapshots uploaded: %lu.",
        evtType, motionEventUUID,
        LT_Pu32(s_framesSinceMotionStart),
        LT_Pu32(s_snapshotsUploaded));
}

static void LTMediaMotionDetection_MediaSourceMotionEventProc(LTMediaEvent event, void *eventData, void *clientData) {
    LT_UNUSED(clientData);
    if (event == kLTMediaEvent_Error) { return; }
    /* Early return to guarantee that we do not keep generating metadata events after pausing MD engine. */
    if (s_bMotionPaused) { return; }
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hMotionDetectionEvent);
    LTMediaData *baseMotionOutput = eventData;
    bool bMotionFrame = false;
    float curBoundingBoxWeighting = LT_DBL_MAX;

    LTTime startTime = LT_GetCore()->GetKernelTime();

    /* Reset the per-frame motion energy histogram counts. */
    lt_memset(s_preDownsamplingEnergyHistogramCounts, 0, sizeof(s_preDownsamplingEnergyHistogramCounts));
    lt_memset(s_postDownsamplingEnergyHistogramCounts, 0, sizeof(s_postDownsamplingEnergyHistogramCounts));

    /* Check the frame for relevant motion and calculate any motion ROIs. */
    bMotionFrame = s_algorithm->API->CheckFrameForMotion(s_algorithm, baseMotionOutput);

    /* Time the call to CheckFrameForMotion() if desired */
    if (s_motionTimingMode == kLTMediaMotionTimingMode_CheckMotion) {
        LTMediaMotionDetection_LogTimeDifference(startTime, bMotionFrame);
    }

    /* Continuous counter of frames since the start of the current motion event (if any).
     * Used to ensure activities such as snapshot notification occur within a maximum time.
     *
     * Reset after the motion event ends.
     */
    if (s_motionState == kLTMediaMotionState_Started) {
        s_framesSinceMotionStart++;
    } else {
        s_framesSinceMotionStart = 0;
    }

    /* MOTION STATE MACHINE
     *
     * When there is currently no motion, count the number of frames with motion above
     * the detection threshold. When this exceeds a limit a new motion event is started.
     *
     * While a motion event is active, the motion is assessed to determine if a snapshot
     * should be taken. The snapshots are used in the notification to users, and also
     * sent to the CV Facade for object classification. Multiple snapshots may be used
     * to increase the accuracy of CV Facade classification.
     *
     * While a motion event is active, the number of frames with no motion is counted.
     * When this exceeds a limit the motion event is ended. Together the limits for
     * starting and ending motion events provide some hysteresis.
     */
    if (bMotionFrame) {
        s_framesNoMotion = 0;
        s_framesOfMotion++;
        curBoundingBoxWeighting = CalculateDistanceFromImageCentre();
        s_totalMotionFrameCount++;

        /* (1) Check for the start of a new motion event. This will send an initial
         * motion event to registered parties.
         */
        if (s_motionState == kLTMediaMotionState_Stopped && s_framesOfMotion >= s_MinMotionFramesForEvent) {
            // Reset counters for new motion event
            s_framesSinceMotionStart = 0;
            s_framesSinceSnapshotAvailable = 0;
            s_snapshotsUploaded = 0;
            s_bestBoundingBoxWeighting = LT_DBL_MAX;

            /* Update state, generate a new motion event UUID, and notify clients. Also log motion event start
             * statistics to cloud for data analysis. */
            s_motionCause = kLTMediaMotionCause_Moxel;
            s_motionState = kLTMediaMotionState_Started;
            s_pUtilityByteOps->GenUUID(s_MotionUuid);
            iEvent->NotifyEvent(s_hMotionDetectionEvent, s_motionState, s_MotionUuid, NULL);
            MotionStartEventStatsLogging();
        }

        /* (2) Process the motion region(s) of interest and notify clients */
        /* ASPEN-7056: Update this to set ROI coordinates as floating point numbers between 0 and 1. */
        LTMediaMotionDetection_Regions *pMotionROIs = s_algorithm->API->GetMotionROIs(s_algorithm);
        for (u8 n = 0; n < pMotionROIs->numRegions; n++) {
            P("med.proc.roi.coords", "ROI x1: %lu, y1: %lu, x2: %lu, y2: %lu\r\n",
                LT_Pu32(pMotionROIs->region[n].x1),
                LT_Pu32(pMotionROIs->region[n].y1),
                LT_Pu32(pMotionROIs->region[n].x2),
                LT_Pu32(pMotionROIs->region[n].y2));
        }
        S.pDeviceMedia->SetProperty("motion.bounds", pMotionROIs);  // always update video driver (for OSD) even if no motion; the video driver now owns the pointer and is responsible for freeing it.
        if (s_motionState == kLTMediaMotionState_Started) {
            // Notify registered clients of the motion metadata. They are required to copy the ROIs if they want to keep them.
            LTMediaMotionDetection_Metadata *motionMetadata = s_algorithm->API->GetMotionMetadata(s_algorithm);
            iEvent->NotifyEvent(s_hMotionMetadataEvent, motionMetadata, s_MotionUuid, NULL);

            // Trigger object detection with motion metadata
            if (s_pObjectDetection) {
                s_pObjectDetection->API->RunObjectDetection(s_pObjectDetection, motionMetadata);
            }
        }

    } else { /* Current frame has no motion */
        s_framesOfMotion = 0;
        s_framesNoMotion++;

        if (s_motionState == kLTMediaMotionState_Started) {
            /* Check if the current motion event needs to end:
            *  1. If the event was externally triggered and no motion has been detected by moxel analysis.
            * OR
            *  2a. If the number of frames with no motion exceeds a limit, and
            *  2b. If the number of frames since the start of the motion event exceeds a limit.
            */
            if (s_motionCause != kLTMediaMotionCause_Moxel && s_framesNoMotion >= s_MinFramesUntilEventEnd && s_framesSinceMotionStart <= s_MaxFramesToAllowAbort) {
                s_motionState = kLTMediaMotionState_Aborted;
            } else if (s_framesNoMotion >= s_MinFramesUntilEventEnd && s_framesSinceMotionStart >= s_MinFramesMotionEventDuration) {
                // Normal event end any time there is a long period of no motion and minimum duration has been reached
                s_motionState = kLTMediaMotionState_Stopped;
            }

            if (s_motionState != kLTMediaMotionState_Started) {
                if (s_snapshotState == kLTMotionSnapshotState_Requested) {
                    // If a snapshot has only just been requested then it can be ignored when it finally arrives
                    s_snapshotState = kLTMotionSnapshotState_Assessing;
                }

                // If a snapshot is currently available then send it before the motion event ends
                TryNotifySnapshot("Sending snapshot as motion event ending");

                iEvent->NotifyEvent(s_hMotionDetectionEvent, s_motionState, s_MotionUuid, NULL);
                LTLOG("snap.count", "Motion event ended with %u uploaded snapshots", s_snapshotsUploaded);

                MotionStopEventStatsLogging();
                s_motionState = kLTMediaMotionState_Stopped;    // reset if we aborted (but report the abort in the stop event logging)
            }
        }
        S.pDeviceMedia->SetProperty("motion.bounds", NULL);
    }

    // Assess motion snapshot requirements based on current motion state and contents of frame
    if (s_motionState == kLTMediaMotionState_Started) {
        AssessMotionSnapshot(curBoundingBoxWeighting);
    } else if (s_motionState == kLTMediaMotionState_Stopped && s_pObjectDetection) {
         // Reset object detection results when motion event ends to prevent stale results
        s_pObjectDetection->API->ResetResults(s_pObjectDetection);
    }

    /* Time the whole call to MediaSourceMotionEventProc() if desired */
    if (s_motionTimingMode == kLTMediaMotionTimingMode_FullMotionProc) {
        LTMediaMotionDetection_LogTimeDifference(startTime, bMotionFrame);
    }
}

static void SwitchMotionSensitivityMode(MotionSensitivityMode mode) {
    if (mode >= kLTMotionSensitivityMode_Count) { return; }
    s_motionSensitivityMode = mode;

    /* Recalculate the algorithm's difference threshold using the new sensitivity mode's adjustment factor. */
    LTMediaMotionDetection_SetSensitivity(s_motionDetectionSensitivity);
}

static void
LTMediaMotionDetection_MediaSourceModeChangeEventProc(LTMediaModeChangeEvent event, void *eventData, void *clientData) {
    LT_UNUSED(eventData);
    LT_UNUSED(clientData);

    switch (event) {
        case kLTMediaModeChangeEvent_FrameSourceSwitchingToDay:
        case kLTMediaModeChangeEvent_FrameSourceSwitchingToNight:
            /* Indicate that motion detection should be paused. Note that the check in
            * LTMediaMotionDetection_MediaSourceMotionEventProc() above ensures that an
            * existing motion event won't be terminated early. The paused flag status
            * only applies to newly occurring motion events (and then only to events that
            * both start and end with the pause period).
            */
            s_bMotionPaused = true;
            LTLOG("daynight.switching", "Pausing motion detection for day/night switch");
            break;

        case kLTMediaModeChangeEvent_FrameSourceSwitchComplete:
            s_bMotionPaused = false;
            LTLOG("daynight.switch.done", "Resuming motion detection");
            break;

        case kLTMediaModeChangeEvent_IRLEDSwitchingOff:
            LTLOG("ir.led.off.ev", "IR LED was switched off. Disabling motion de-sense.");
            SwitchMotionSensitivityMode(kLTMotionSensitivityMode_Normal);
            break;

        case kLTMediaModeChangeEvent_IRLEDSwitchingOn:
            LTLOG("ir.led.on.ev", "IR LED was switched on. Enabling motion de-sense.");
            SwitchMotionSensitivityMode(kLTMotionSensitivityMode_NightIRLEDs);
            break;

        default:
            LTLOG_YELLOWALERT("mode.chg.ev.err", "Unknown mode change event!");
            break;
    }
}

// Receive a snapshot
static void LTMediaMotionDetection_MediaSourceSnapshotProc(LTMediaEvent event, void *eventData, void *clientData) {
    /* Notify motion event consumers that a snapshot has been produced. Typically this
     * will be uploaded to the cloud, but it is application-specific what happens to the image.
     */
    LT_UNUSED(clientData);

    /* Guard against events arriving at an unexpected snapshot state */
    if (s_snapshotState != kLTMotionSnapshotState_Requested) {
        LTLOG_YELLOWALERT("snapshot.state.invalid", NULL);
        return;
    }
    // From this point onwards, s_snapshotState == kLTMotionSnapshotState_Requested

    /* Snapshot data failed to arrive */
    if (event == kLTMediaEvent_Error) {
        LTLOG_YELLOWALERT("snapshot.fail", NULL);
        s_snapshotState = kLTMotionSnapshotState_Assessing;
        return;
    }

    /* Snapshot arrives at an unexpected time */
    if (s_motionState != kLTMediaMotionState_Started) {
        LTLOG_YELLOWALERT("snapshot.no.motion", NULL);
        s_snapshotState = kLTMotionSnapshotState_Assessing;
        return;
    }

    /* Copy LTMediaData JPEG information to locally-owned buffer (as the JPEG data will expire
     * after this event has been handled). If the snapshot type is raw then we can't copy it,
     * as it's too large for available system memory. So just send a blank event in that case.
     */
    LTMediaData *Snapshot = eventData;
    LTLOG_DEBUG("snapshot.received", "Snapshot received, addr: %p size: %lu", Snapshot->pData, LT_Pu32(Snapshot->nDataLen));

    LT_SIZE allocSize = sizeof(LTMediaMotionDetection_Snapshot);
    if (s_formatSnapshotFrames.nEncoding != kLTMediaEncoding_Raw) {
        allocSize += Snapshot->nDataLen;
    }
    s_pMotionSnapshot = lt_malloc(allocSize);
    if (!s_pMotionSnapshot) {
        LTLOG_YELLOWALERT("snapshot.alloc.err", "Error: Failed to allocate buffer (%lu) for snapshot upload.", LT_Pu32(Snapshot->nDataLen));
        s_snapshotState = kLTMotionSnapshotState_Assessing; // s_pMotionSnapshot == NULL
        return;
    }
    s_pMotionSnapshot->nKind = s_formatSnapshotFrames.nKind;
    s_pMotionSnapshot->nEncoding = s_formatSnapshotFrames.nEncoding;
    s_pMotionSnapshot->bNightVisionActive = Snapshot->meta.video.bNightVisionActive;
    s_pMotionSnapshot->curSnapshotIndex = s_snapshotsUploaded;
    s_pMotionSnapshot->maxSnapshotCount = s_maxSnapshotsPerMotionEvent;
    s_pMotionSnapshot->nTimestamp = Snapshot->nTimestamp;

    if (s_formatSnapshotFrames.nEncoding != kLTMediaEncoding_Raw) {
        u8 *base = (u8 *)s_pMotionSnapshot + sizeof(LTMediaMotionDetection_Snapshot);
        lt_memcpy(base, Snapshot->pData, Snapshot->nDataLen);
        s_pMotionSnapshot->pImageData = base;
        s_pMotionSnapshot->nImageLen = Snapshot->nDataLen;
    } else {
        s_pMotionSnapshot->pImageData = NULL;
        s_pMotionSnapshot->nImageLen = 0;
    }

    // Resolution data
    if (s_formatSnapshotFrames.nKind == kLTMediaKind_Image_SD) {
        s_pMotionSnapshot->resolution = SnapshotResolution;
    } else {
        LTLOG_YELLOWALERT("snapshot.format.error", "Unsupported format for snapshot upload: %lu", LT_Pu32(s_formatSnapshotFrames.nKind));
        lt_free(s_pMotionSnapshot);
        s_pMotionSnapshot = NULL;
        s_snapshotState = kLTMotionSnapshotState_Assessing; // s_pMotionSnapshot == NULL
        return;
    }

    // Point to the ROI data that is associated with this snapshot
    // (malloced/cloned before snapshot Trigger() in LTMediaMotionDetection_MediaSourceMotionEventProc)
    // Freed in the completion proc after snapshot event
    s_pMotionSnapshot->pMotionROIs = s_snapshotMotionROIs;

    P("snapshot.taken", "Motion snapshot, size: %lu, timestamp: %lu\n",
        LT_Pu32(Snapshot->nDataLen),
        LT_Pu32(Snapshot->nTimestamp));

    // Finally, advance the state to indicate snapshot data exists
    s_snapshotState = kLTMotionSnapshotState_Available;
}

static void MotionDelayedStartup(void *pClientData) {
    LT_UNUSED(pClientData);

    ILTThread *iThread = lt_gethandleinterface(ILTThread, s_hMotionDetectionThread);
    iThread->KillTimer(s_hMotionDetectionThread, MotionDelayedStartup, NULL);

    ILTMediaSource *iMediaSourceMotion = lt_gethandleinterface(ILTMediaSource, s_mediaSourceMotion);

    P("startup.done", "Enabling motion detection");
    s_bMotionPaused = false;
    iMediaSourceMotion->Start(s_mediaSourceMotion);

    if (s_mediaSourceSnapshot) {
        ILTMediaSource *iMediaSourceSnapshot = lt_gethandleinterface(ILTMediaSource, s_mediaSourceSnapshot);
        iMediaSourceSnapshot->OnMediaEvent(s_mediaSourceSnapshot, &LTMediaMotionDetection_MediaSourceSnapshotProc, NULL);
    }

    iMediaSourceMotion->OnMediaEvent(s_mediaSourceMotion, &LTMediaMotionDetection_MediaSourceMotionEventProc, NULL);
    iMediaSourceMotion->OnModeChangeEvent(s_mediaSourceMotion, &LTMediaMotionDetection_MediaSourceModeChangeEventProc, NULL);
}

static bool LTMediaMotionDetection_ThreadInit(void) {
    // Run GenUUID() upfront as it takes 20+ ms the first time it runs and avoid delays when processing motion frames
    s_pUtilityByteOps->GenUUID(s_MotionUuid);

    /* Pause motion detection processing for a couple of seconds after startup, to allow the sensor to settle */
    s_bMotionPaused = true;
    ILTThread *iThread = lt_gethandleinterface(ILTThread, s_hMotionDetectionThread);
    if (s_pauseOnStartupTimeMilliseconds == 0){
        iThread->QueueTaskProc(s_hMotionDetectionThread, MotionDelayedStartup, NULL, NULL);
    } else {
        iThread->SetTimer(s_hMotionDetectionThread, LTTime_Milliseconds(s_pauseOnStartupTimeMilliseconds), MotionDelayedStartup, NULL, NULL);
    }

    return true;
}

static void LTMediaMotionDetection_ThreadExit(void) {
    ILTThread *iThread = lt_gethandleinterface(ILTThread, s_hMotionDetectionThread);
    iThread->KillTimer(s_hMotionDetectionThread, MotionDelayedStartup, NULL);

    ILTMediaSource *iMediaSourceMotion = lt_gethandleinterface(ILTMediaSource, s_mediaSourceMotion);
    iMediaSourceMotion->Stop(s_mediaSourceMotion);
    iMediaSourceMotion->NoModeChangeEvent(s_mediaSourceMotion, &LTMediaMotionDetection_MediaSourceModeChangeEventProc);
    iMediaSourceMotion->NoMediaEvent(s_mediaSourceMotion, &LTMediaMotionDetection_MediaSourceMotionEventProc);

    if (s_mediaSourceSnapshot) {
        ILTMediaSource *iMediaSourceSnapshot = lt_gethandleinterface(ILTMediaSource, s_mediaSourceSnapshot);
        iMediaSourceSnapshot->Stop(s_mediaSourceSnapshot);
        iMediaSourceSnapshot->NoMediaEvent(s_mediaSourceSnapshot, &LTMediaMotionDetection_MediaSourceSnapshotProc);
    }

    CleanupSnapshot();
}

/*_____________________________________
  Library Initialization and Cleanup */

static bool DecodeSensitivityTableString(const char *stringSetting, u8 *pDecodeBuffer, u32 nDecodeBufferSize) {
    /* Attempts to decode the given string into a sensitivity table and puts it in the given buffer if successful.
     * It is the caller's responsibility to allocate memory for pDecodeBuffer! */
    if (!stringSetting || !pDecodeBuffer || nDecodeBufferSize < sizeof(MotionSensitivityTable)) { return false; }

    /* Attempt to decode the table string into the provided buffer. The decoded table must fit into a
     * MotionSensitivityTable array exactly or the input table string is invalid. */
    u32 decodedBytes = s_pUtilityByteOps->Base64Decode(stringSetting, lt_strlen(stringSetting), pDecodeBuffer, nDecodeBufferSize);
    if (decodedBytes != sizeof(MotionSensitivityTable)) {
        LTLOG_YELLOWALERT("sensitivity.table.string.invalid", "Decoded sensitivity table has unexpected size %lu (should be %lu) - invalid input!", LT_Pu32(decodedBytes), LT_Pu32(sizeof(MotionSensitivityTable)));
        return false;
    }

    return true;
}

static LTString SensitivityTableToLogString(u32 tableIndex) {
    /* Convert the sensitivity table to a log string for debug purposes. */
    LTString sensitivityTableLogString = ltstring_create("");
    for (u32 bucketCount = 0; bucketCount < MOTION_SENSITIVITY_BUCKETS; bucketCount++) {
        ltstring_appendformat(&sensitivityTableLogString, "%lu: %lu %lu. ", LT_Pu32(bucketCount), LT_Pu32(s_motionSensitivityTables[tableIndex][bucketCount].sensitivityThreshold), LT_Pu32(s_motionSensitivityTables[tableIndex][bucketCount].minimumIslandSize));
    }
    return sensitivityTableLogString;
}

static bool ReadSensitivityConfig(LTProductConfig *productConfig) {
    if (!productConfig) { return false; }

    u32 libSection = productConfig->GetLibraryConfigSection("LTMediaMotionDetection");
    if (!libSection) { return false; }

    const char *sensitivityTableSettingsKeys[kLTMotionSensitivityMode_Count] = { "MotionSensitivityTables/Normal", "MotionSensitivityTables/NightIR" };

    bool bSuccess = true;
    for (u32 sensitivityModeIndex = 0; sensitivityModeIndex < kLTMotionSensitivityMode_Count; sensitivityModeIndex++) {
        /* 1. Read the base 64 encoded string sensitivity table for this sensitivity mode from LTProductConfig. */
        const char *encodedTableBuffer = productConfig->ReadString(libSection, sensitivityTableSettingsKeys[sensitivityModeIndex]);

        /* 2. Decode the string and set the motion sensitivity table upon successful decode. */
        if (!DecodeSensitivityTableString(encodedTableBuffer, (u8 *)s_motionSensitivityTables[sensitivityModeIndex], sizeof(MotionSensitivityTable))) {
            LTLOG_REDALERT("sense.cfg.fail", "Failed to read sensitivity config for %s", sensitivityTableSettingsKeys[sensitivityModeIndex]);
            bSuccess = false; // Allow attempts to decode other config settings to see if they are invalid too
        }
    }

    return bSuccess;
}

bool CheckHiddenIntegerSetting(LTSystemSettings *systemSettings, const char *settingName, s64 *value) {
    if (!systemSettings || !settingName || !value) { return false; }
    bool ret = systemSettings->GetIntegerValue(settingName, value);
    if (ret) {
        LTLOG("hidden.setting.found", "%s setting found with value: %lld.", settingName, LT_Ps64(*value));
    }
    return ret;
}

bool CheckHiddenStringSetting(LTSystemSettings *systemSettings, const char *settingName, char **value) {
    if (!systemSettings || !settingName || !value) { return false; }
    bool ret = systemSettings->GetStringValue(settingName, value);
    if (ret) {
        LTLOG("hidden.setting.found", "%s setting found with value: %s.", settingName, *value);
    }
    return ret;
}

static bool LTMediaMotionDetectionImpl_LibInit(void) {
    S = (struct Globals) {0};

    /* Create our mutex */
    if (!(s_mutex = lt_createobject(LTMutex))) { return false; } /* abort load if the mutex can't be created. */
    /* NOTE: s_mutex is used only to ensure thread safety of multiple clients calling into the Start and StopDetectionEngine functions simultaneously. The motion detection thread doesn't need it or use it. */

    if (!(s_pUtilityByteOps = lt_openlibrary(LTUtilityByteOps))) { return false; }

    /* Create the event we'll use to notify motion event receivers that motion has been detected */
    if (!(s_hMotionDetectionEvent =
            LT_GetCore()->CreateEvent(
                &s_MotionDetectionEventArgs,
                LTMediaMotionDetection_MotionDetectionEventDispatchProc,
                LTMediaMotionDetection_MotionDetectionEventDispatchCompleteProc,
                NULL, NULL))
        || !(s_hMotionMetadataEvent =
            LT_GetCore()->CreateEvent(
                &s_MotionMetadataEventArgs,
                LTMediaMotionDetection_MotionMetadataEventDispatchProc,
                LTMediaMotionDetection_MotionMetadataEventDispatchCompleteProc,
                NULL, NULL))) {
        /* couldn't create event; clean up what we made so far and bail */
        if (s_hMotionDetectionEvent) LT_GetCore()->DestroyHandle(s_hMotionDetectionEvent);
        if (s_hMotionMetadataEvent) LT_GetCore()->DestroyHandle(s_hMotionMetadataEvent);
        lt_destroyobject(s_mutex);
        s_mutex = NULL;
        s_hMotionDetectionEvent = 0;
        s_hMotionMetadataEvent = 0;
        return false;
    }

    if (!(s_hZoneStateEvent =  LT_GetCore()->CreateEvent(&s_ZoneStateEventArgs,
                                                         (void *)&LTMediaMotionDetection_ZoneStateEventDispatchProc,
                                                         NULL, NULL, NULL))) {
        LTLOG_REDALERT("lib.init.create.zonestateevent.fail", "Could not create ZoneStateEvent.");
        return false;
    }

    /* Create motion algorithm object. */
    s_algorithm = lt_createobject(LTMediaMotionDetectionAlgorithm);
    if (!s_algorithm) {
        LTLOG_REDALERT("lib.init.no.algo", "Failed to create motion detection algorithm object!");
        return false;
    }

    /* Fetch constants from video driver to set key library values. */
    LTDeviceMedia *deviceMedia = NULL;
    if (!(deviceMedia = lt_openlibrary(LTDeviceMedia))) {
        LTLOG_REDALERT("lib.init.open.devicemedia.fail", "Could not open LTDeviceMedia while initialising LTMediaMotionDetection.");
        return false;
    }
    if (!deviceMedia->GetProperty("resolution.sd", &SnapshotResolution)) {
        LTLOG_REDALERT("lib.init.open.get.snapshotresolution.fail", "Could not fetch snapshot resolution values from video driver.");
        return false;
    }
    LTMediaResolution baseMotionResolution;
    if (!deviceMedia->GetProperty("resolution.motion", &baseMotionResolution)) {
        LTLOG_REDALERT("lib.init.open.get.motionresolution.fail", "Could not fetch base motion resolution values from video driver.");
        return false;
    }
    lt_closelibrary(deviceMedia);

    /* Fetch constants from product config to set key library values. */
    LTProductConfig *productConfig = NULL;
    if (!(productConfig = lt_openlibrary(LTProductConfig))) {
        LTLOG_REDALERT("lib.init.open.productconfig.fail", "Could not open LTProductConfig while initialising LTMediaMotionDetection.");
        return false;
    }
    u32 libSection = productConfig->GetLibraryConfigSection("LTMediaMotionDetection");
    BitGridWidth = libSection ? productConfig->ReadInteger(libSection, "BitGridWidth") : 0;
    if (!BitGridWidth) {
        LTLOG_REDALERT("lib.init.open.get.bitgridwidth.fail", "Could not fetch motion zone BitGridWidth from LTProductConfig.");
        return false;
    }
    BitGridHeight = libSection ? productConfig->ReadInteger(libSection, "BitGridHeight") : 0;
    if (!BitGridHeight) {
        LTLOG_REDALERT("lib.init.open.get.bitgridheight.fail", "Could not fetch motion zone BitGridHeight from LTProductConfig.");
        return false;
    }
    /* The default product downsampling scale factor is read here, but can be overridden by the hidden setting
     * "motion/downsamplingScaleFactor". */

    u32 downsamplingScaleFactor = libSection ? productConfig->ReadInteger(libSection, "DownsamplingScaleFactor") : 0;
    if (!downsamplingScaleFactor) {
        LTLOG_REDALERT("lib.init.open.get.downsamplingscalefactor.fail", "Could not fetch downsampling scale factor from LTProductConfig.");
        return false;
    }
    /* Read the default motion sensitivity table from product config, which contains the product's specific sensitivity
     * threshold mapping values and corresponding minimum island sizes. These can be overridden by the hidden setting
     * "motion/sensitivityTable". */
    if (!ReadSensitivityConfig(productConfig)) {
        LTLOG_REDALERT("lib.init.open.get.sensitivitytableconfig.fail", "Could not fetch motion sensitivity table values from LTProductConfig.");
        return false;
    }

    /* Check the default downsampling type for this product, which can be later overridden by the hidden setting
     * "motion/downsamplingStrategy". If the product setting can't be read, the returned value will be 0 and we will
     * use the default of max pooling.  */
    s64 value = libSection ? productConfig->ReadInteger(libSection, "DefaultDownsamplingStrategy") : 0;
    LTMediaMotionDownsamplingStrategy downsamplingStrategy = (LTMediaMotionDownsamplingStrategy)value;

    /* All products use multi ROI calculation by default so don't even bother with reading product config. This can be
     * overridden by the hidden setting "motion/ROICalculationStrategy" if needed. */
    LTMediaMotionROICalculationStrategy roiCalculationStrategy = kLTMediaMotionROICalculationStrategy_MultipleROIs;

    bool autoStartEngine = libSection ? productConfig->ReadInteger(libSection, "AutoStartEngine") : 0;

    u32 cfgStartupTime = libSection ? productConfig->ReadInteger(libSection, "pauseOnStartupTime") : 2000;
    if (libSection  && (cfgStartupTime != 0 || productConfig->IsIntegerZero(libSection, "pauseOnStartupTime"))) {
        s_pauseOnStartupTimeMilliseconds = cfgStartupTime;
    }
    lt_closelibrary(productConfig);

    /* Calculate constants for motion zone computation. */
    S.verticalMoxels = baseMotionResolution.height;
    S.horizontalMoxels = baseMotionResolution.width;
    BitmapWords = (((S.verticalMoxels * S.horizontalMoxels) + BITMAP_WORD_LEN - 1) / BITMAP_WORD_LEN);

    /* Validate values read from LTProductConfig. */
    if ((S.horizontalMoxels % BitGridWidth != 0) || (S.verticalMoxels % BitGridHeight != 0)) {
        LTLOG_REDALERT("lib.init.invalidzonedim", "BitGrid size %lu x %lu is not compatible with motion data size %lu x %lu.", LT_Pu32(BitGridWidth), LT_Pu32(BitGridHeight), LT_Pu32(S.horizontalMoxels), LT_Pu32(S.verticalMoxels));
        return false;
    }

    if (downsamplingScaleFactor != 1 && downsamplingScaleFactor != 2) {
        LTLOG_REDALERT("lib.init.invaliddownsamplingscalefactor", "Unexpected value %lu for downsampling scale factor.", LT_Pu32(downsamplingScaleFactor));
        return false;
    }

    P("lib.init.consts", "VerticalMoxels: %lu, HorizontalMoxels: %lu, BitmapWordLen: %lu, BitmapWords: %lu, BytesPerWord: %lu, downsamplingScaleFactor: %lu\n",
        LT_Pu32(S.verticalMoxels),
        LT_Pu32(S.horizontalMoxels),
        LT_Pu32(BITMAP_WORD_LEN),
        LT_Pu32(BitmapWords),
        LT_Pu32(BYTES_PER_WORD),
        LT_Pu32(downsamplingScaleFactor)
    );

    /* Set default motion detection zone with all moxels enabled.
     * If there is a motion detection zone setting stored in flash then this default will be overridden. */
    u32 nBitmapBytes = (BitGridWidth * BitGridHeight + 7) / 8;
    LT_SIZE bitGridSize = sizeof(LTMediaMotionDetection_MotionZoneBitGrid) + sizeof(u8) * nBitmapBytes;

    P("lib.init.bitgridvalues", "BitGridWidth: %lu, BitGridHeight: %lu, nBitmapBytes: %lu, bitGridSize: %lu\n",
        LT_Pu32(BitGridWidth),
        LT_Pu32(BitGridHeight),
        LT_Pu32(nBitmapBytes),
        LT_Pu32(bitGridSize)
    );

    LTMediaMotionDetection_MotionZoneBitGrid *pMotionZoneBitGrid = lt_malloc(bitGridSize);
    if (!pMotionZoneBitGrid) {
        LTLOG_REDALERT("lib.init.bitgrid.malloc.fail", "Could not allocate memory for motion zone bitgrid.");
        return false;
    }
    pMotionZoneBitGrid->width = BitGridWidth;
    pMotionZoneBitGrid->height = BitGridHeight;
    pMotionZoneBitGrid->bitmapSize = nBitmapBytes;
    /* Setting to -1 should set all bits to 1 in each word, after a conversion from signed to unsigned. */
    lt_memset(pMotionZoneBitGrid->bitmap, -1, pMotionZoneBitGrid->bitmapSize);

    LTMediaMotionDetection_SetMotionZones(pMotionZoneBitGrid);
    P("lib.init.defaultzone", "Defaulting to entire frame as a motion zone on library initialisation.");
    lt_free(pMotionZoneBitGrid);

    /* Encode the filled-in table to a set of base 64 encoded strings for each mode and log as a final check. */
    for (u32 sensitivityModeIndex = 0; sensitivityModeIndex < kLTMotionSensitivityMode_Count; sensitivityModeIndex++) {
        u32 base64TableSize = s_pUtilityByteOps->GetBase64EncodeBufferRequirement(sizeof(MotionSensitivityTable));
        char *encodedTableString = lt_malloc(base64TableSize);
        lt_memset(encodedTableString, 0, base64TableSize);
        if (!encodedTableString) {
            LTLOG_REDALERT("lib.init.sensitivity.table.str.oom", "Could not allocate memory for sensitivity table base 64 encoded string.\n");
            return false;
        }
        s_pUtilityByteOps->Base64Encode((u8 *)s_motionSensitivityTables[sensitivityModeIndex], sizeof(MotionSensitivityTable), encodedTableString, base64TableSize);
        P("lib.init.sensitivity.table.str", "Sensitivity table encoded string for sensitivity mode %lu: %s", LT_Pu32(sensitivityModeIndex), encodedTableString);
        lt_free(encodedTableString);
    }

    /* Log the actual table values if desired */
    for (u32 sensitivityModeIndex = 0; sensitivityModeIndex < kLTMotionSensitivityMode_Count; sensitivityModeIndex++) {
        LTString sensitivityTableLogString = SensitivityTableToLogString(sensitivityModeIndex);
        P("lib.init.sensitivity.table", "Table %lu: %s", LT_Pu32(sensitivityModeIndex), sensitivityTableLogString);
        lt_free(sensitivityTableLogString);
    }

    /* Set default motion sensitivity value and threshold before user settings are applied, so that motion detection
     * does not collapse in debug scenarios where user settings cannot be applied for some reason. */
    s_motionDetectionSensitivity = 50;
    LTMediaMotionDetection_SetSensitivity(s_motionDetectionSensitivity);

    /* Now that the necessary settings have been gathered, initialise the algorithm with the relevant hyperparameters. */
    u32 sensitivityBin = s_motionDetectionSensitivity / 10;
    u32 minimumIslandSize = s_motionSensitivityTables[s_motionSensitivityMode][sensitivityBin].minimumIslandSize;
    u8 differenceThreshold = s_motionSensitivityTables[s_motionSensitivityMode][sensitivityBin].sensitivityThreshold;
    LTMediaMotionDetectionAlgorithm_Parameters params = {
        .downsamplingStrategy = downsamplingStrategy,
        .roiCalculationStrategy = roiCalculationStrategy,
        .differenceThreshold = differenceThreshold,
        .downsamplingScaleFactor = downsamplingScaleFactor,
        .minimumIslandSize = minimumIslandSize
    };

    if (!s_algorithm->API->Init(s_algorithm, &params)) {
        LTLOG_REDALERT("start.eng.algo.init.fail", "Failed to initialise motion detection algorithm object.");
        return false;
    }

    /* Check for hidden settings to override default parameters used by the motion algorithm and state machine.
     * be done after initialising the algorithm object to capture the new values. */
    LTMediaMotionDetection_ApplyConfiguration();

    /* Create object detection object if enabled -- this need to be done after algorithm init and applyConfiguration */
    if (s_algorithm->API->IsObjectDetectionEnabled(s_algorithm) && s_detectionFlags != kLTMediaMotionFlags_All) {
         s_pObjectDetection = lt_createobject(LTMediaObjectDetection);
        LTLOG("lib.init.objdet", "Object detection object created");
    } else {
        LTLOG("lib.init.no.objdet", "Object detection not enabled");
    }

    // Final step - automatically start the detection engine if the product requires it
    if (autoStartEngine) {
        // TBD - even though PIR triggers can be disabled on Silabs using the user's motion detection
        // enabling, we still need to observe that setting on this side for cases where Anyka was powered
        // up anyway (e.g. live stream) and a video  motion trigger happens.
        LTMediaMotionDetection_StartDetectionEngine(true);
        LTLOG("lib.init.autostart", "Auto-starting motion detection engine");
    }

    return true; /* LibInit successful; lib will proceed with loading on return true */
}

static void LTMediaMotionDetectionImpl_LibFini(void) {
    /* call StopDetectionEngine() in case the client did not, then destroy event and mutex */
    LTMediaMotionDetection_StopDetectionEngine();

    LT_GetCore()->DestroyHandle(s_hMotionDetectionEvent);
    LT_GetCore()->DestroyHandle(s_hMotionMetadataEvent);
    LT_GetCore()->DestroyHandle(s_hZoneStateEvent);
    lt_destroyobject(s_mutex);
    s_hMotionDetectionEvent = 0;
    s_hMotionMetadataEvent = 0;
    s_mutex = NULL;

    if (s_motionZoneBitmap) {
        lt_free(s_motionZoneBitmap->pData);
        lt_free(s_motionZoneBitmap);
        s_motionZoneBitmap = NULL;
    }

    lt_closelibrary(s_pUtilityByteOps);
    s_pUtilityByteOps = NULL;

    lt_destroyobject(s_algorithm);
    s_algorithm = NULL;

    lt_destroyobject(s_pObjectDetection);
    s_pObjectDetection = NULL;
}

/*________________________________________________________
  LTMediaMotionDetection library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTMediaMotionDetection)
    .StartDetectionEngine       = LTMediaMotionDetection_StartDetectionEngine,
    .PauseMotionDetection       = LTMediaMotionDetection_PauseMotionDetection,
    .ResumeMotionDetection      = LTMediaMotionDetection_ResumeMotionDetection,
    .StopDetectionEngine        = LTMediaMotionDetection_StopDetectionEngine,
    .IsDetectionEngineRunning   = LTMediaMotionDetection_IsDetectionEngineRunning,
    .ExternalTrigger            = LTMediaMotionDetection_ExternalTrigger,
    .SetMotionZones             = LTMediaMotionDetection_SetMotionZones,
    .GetMotionZones             = LTMediaMotionDetection_GetMotionZones,
    .IsMotionZoneEnabled        = LTMediaMotionDetection_IsMotionZoneEnabled,
    .EnableMotionZones          = LTMediaMotionDetection_EnableMotionZones,
    .SetMotionTimingMode        = LTMediaMotionDetection_SetMotionTimingMode,
    .OnMotionEvent              = LTMediaMotionDetection_OnMotionEvent,
    .NoMotionEvent              = LTMediaMotionDetection_NoMotionEvent,
    .OnMetadataEvent            = LTMediaMotionDetection_OnMetadataEvent,
    .NoMetadataEvent            = LTMediaMotionDetection_NoMetadataEvent,
    .SetSensitivity             = LTMediaMotionDetection_SetSensitivity,
    .GetSensitivity             = LTMediaMotionDetection_GetSensitivity,
    .SetDetectionFlags          = LTMediaMotionDetection_SetDetectionFlags,
    .GetDetectionFlags          = LTMediaMotionDetection_GetDetectionFlags,
    .OnZoneStateEvent           = LTMediaMotionDetection_OnZoneStateEvent,
    .NoZoneStateEvent           = LTMediaMotionDetection_NoZoneStateEvent,
    .ApplyConfiguration         = LTMediaMotionDetection_ApplyConfiguration,

    /* The following are exposed for the purpose of unit testing and debugging */
    .GetMotionStateParameters   = GetMotionStateParameters,
    .GetMotionState             = GetMotionState,
    .GetMotionZoneBitmap        = GetMotionZoneBitmap,
    .GetMotionEventMaxSnapshots = LTMediaMotionDetection_GetMotionEventMaxSnapshots,
    .SetMotionEventMaxSnapshots = LTMediaMotionDetection_SetMotionEventMaxSnapshots,
    .GetActiveAlgorithm         = LTMediaMotionDetection_GetActiveAlgorithm,
    .ResetMotionState           = ResetMotionState,
    .SetMotionEventStatsLogging = SetMotionEventStatsLogging,
    .SetMinFramesMotionEventDuration = SetMinFramesMotionEventDuration,
    .GetMotionFrameCount        = GetMotionFrameCount,
    .ResetMotionFrameCount      = ResetMotionFrameCount,
LTLIBRARY_DEFINITION;
