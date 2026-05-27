/*******************************************************************************
 *
 * LTShellMotion: Shell handler for LTMediaMotionDetection library
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/
#include <lt/LT.h>
#include <lt/media/motiondetection/LTMediaMotionDetection.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

DEFINE_LTLOG_SECTION("ltshell.motion");


/*******************************************************************************
 * Static Variables & Types
 ******************************************************************************/

/* Static references to LT libraries and interfaces, opened for long periods of time */
static struct Statics {
    LTCore *core;
    LTSystemShell *shell;
    LTMediaMotionDetection *motion;
    LTUtilityByteOps *byteOps;
    LTOThread *shellMotionThread;
    LTMediaMotionDetectionAlgorithm *algorithm; /* Reference to LTMediaMotionDetection's algorithm object. */

    ILTShell *iShell;
} S;

static bool LocalEventsRegistered = false;


/*******************************************************************************
 * Shell thread to handle motion events
 ******************************************************************************/

/* Sample client callback for motion detection event, used by the shell interface.
 * Called whenever the motion engine notifies that a motion event has been detected.
 */
static void OnMotionEvent(LTMediaMotionState motionState,
                          u8 *motionUuid,
                          LTMediaMotionDetection_Snapshot *motionSnapshot,
                          void *clientData) {
    LT_UNUSED(clientData);
    LT_UNUSED(motionSnapshot);

    char uuid[40];
    if (!S.byteOps->UUIDToString(motionUuid, uuid, sizeof(uuid))) {
        uuid[0] = '\0';
    }

    switch (motionState) {
        case kLTMediaMotionState_Started:
            LTLOG("event", "Motion event started: %s\n", uuid);
            break;
        case kLTMediaMotionState_Snapshot:
            LTLOG("event", "Motion snapshot generated: %s\n", uuid);
            break;
        case kLTMediaMotionState_Stopped:
            LTLOG("event", "Motion event ended: %s\n", uuid);
            break;
        default:
            LTLOG_REDALERT("event.err", "Error: State of motion event unknown!");
            break;
    }
}

// Sample client callback for metadata, called whenever there is metadata to share with clients
static void OnMetadataEvent(LTMediaMotionDetection_Metadata *metadata, u8 *motionUuid, void *clientData) {
    LT_UNUSED(motionUuid);
    LT_UNUSED(clientData);

    LTLOG_DEBUG("meta", "Metadata event!\n");
    if (!metadata) {
        LTLOG_REDALERT("metadata.proc.err:", "No metadata was received!");
        return;
    }
}

static void StartEngineProc(void *pClientData) {
    LT_UNUSED(pClientData);

    bool bStarted = false;
    if (S.motion) {
        S.motion->OnMotionEvent(OnMotionEvent, NULL, NULL);
        S.motion->OnMetadataEvent(OnMetadataEvent, NULL, NULL);
        LocalEventsRegistered = true;
        S.motion->StartDetectionEngine(true);

        bStarted = S.motion->IsDetectionEngineRunning();
    }

    if (!bStarted) {
        LTLOG_YELLOWALERT("start.failed", "motiondetection: failed to start detection engine\n");
    } else {
        LTLOG("start.ok", "motiondetection: detection engine has started\n");
    }
}

static void StopEngineProc(void *pClientData) {
    LT_UNUSED(pClientData);

    S.motion->NoMetadataEvent(OnMetadataEvent);
    S.motion->NoMotionEvent(OnMotionEvent);
    LocalEventsRegistered = false;
    S.motion->StopDetectionEngine();

    bool bStarted = S.motion->IsDetectionEngineRunning();
    if (bStarted) {
        LTLOG_YELLOWALERT("stop.failed", "motiondetection: failed to stop detection engine\n");
    } else {
        LTLOG("stop.ok", "motiondetection: detection engine has stopped\n");
    }
}

static bool LTShellMotion_ThreadInit(void) {
    return true;
}

static void LTShellMotion_ThreadExit(void) {
    /* Ensure the registered events are disabled before the thread is destroyed */
    if (LocalEventsRegistered) {
        if (S.motion) {
            S.motion->NoMetadataEvent(OnMetadataEvent);
            S.motion->NoMotionEvent(OnMotionEvent);
        LocalEventsRegistered = false;
    }
}


/*******************************************************************************
 * Shell Interface
 ******************************************************************************/

static int LTShellMotion_Status(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    bool bStarted = S.motion->IsDetectionEngineRunning();
    S.iShell->Print(hShell, "motiondetection: detection engine is %s\n", bStarted ? "started" : "stopped");
    return 0;
}

static int LTShellMotion_Start(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    bool bStarted = S.motion->IsDetectionEngineRunning();

    if (bStarted) {
        S.iShell->Print(hShell, "motiondetection: detection engine already started\n");
        return -1;
    }

    S.iShell->Print(hShell, "Starting motion detection engine\n");
    S.shellMotionThread->API->QueueTaskProcIfRequired(S.shellMotionThread, StartEngineProc, NULL, NULL);
    return 0;
}

static int LTShellMotion_Pause(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    bool bStarted = S.motion->IsDetectionEngineRunning();

    if (!bStarted) {
        S.iShell->Print(hShell, "motiondetection: detection engine not running\n");
        return -1;
    }

    if (!S.motion->PauseMotionDetection()) return -1;
    return 0;
}

static int LTShellMotion_Resume(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    bool bStarted = S.motion->IsDetectionEngineRunning();

    if (!bStarted) {
        S.iShell->Print(hShell, "motiondetection: detection engine not running\n");
        return -1;
    }

    if (!S.motion->ResumeMotionDetection()) return -1;
    return 0;
}

static int LTShellMotion_Stop(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    bool bStarted = S.motion->IsDetectionEngineRunning();

    if (!bStarted) {
        S.iShell->Print(hShell, "motiondetection: detection engine already stopped\n");
        return -1;
    }

    S.shellMotionThread->API->QueueTaskProcIfRequired(S.shellMotionThread, StopEngineProc, NULL, NULL);
    return 0;
}

static int LTShellMotion_Zones(LTShell hShell, int argc, const char *argv[]) {
    u32 width, height, bitmapBytes;
    LTMediaMotionDetection_MotionZoneBitGrid *pMotionZoneBitGrid = NULL;

    if (argc < 3) {
        S.iShell->Print(hShell, "motion zones W H [grid_bytes]\n");
        S.iShell->Print(hShell, "  - define a motion zone bit grid width 'W' by height 'H'\n");
        S.iShell->Print(hShell, "  - if grid_bytes is not provided, a WxH empty grid will be used\n");
        S.iShell->Print(hShell, "  - if grid_bytes is the character '*', a WxH filled grid will be used\n");
        S.iShell->Print(hShell, "  - otherwise grid_bytes should be 'N' 8-bit bytes defining the grid (e.g. 0x01 0xFF 0xFF 0x00 etc.)\n");

    } else {
        width = lt_strtou32(argv[1], NULL, 0);
        height = lt_strtou32(argv[2], NULL, 0);

        if (!width || !height) {
            S.iShell->Print(hShell, "ERROR: Width and height must be non-zero!\n");
            return -1;
        }

        if (argc == 3) {
            bitmapBytes = (width * height  + 7) / 8;
        } else if (argc == 4 && !lt_strcmp(argv[3], "*")) {
            bitmapBytes = (width * height  + 7) / 8;
        } else {
            /* Pass exactly the bytes provided - this may result in a malformed bit grid, but that's allowed for testing */
            bitmapBytes = argc - 3;
        }

        LT_SIZE bitGridSize = sizeof(LTMediaMotionDetection_MotionZoneBitGrid) + (sizeof(u8) * bitmapBytes);

        pMotionZoneBitGrid = lt_malloc(bitGridSize);
        if (!pMotionZoneBitGrid) {
            S.iShell->Print(hShell, "ERROR: Could not allocate memory for motion zone bitgrid!\n");
            return -1;
        }

        pMotionZoneBitGrid->width = width;
        pMotionZoneBitGrid->height = height;
        pMotionZoneBitGrid->bitmapSize = bitmapBytes;
        S.iShell->Print(hShell, "Created bit grid %lux%lu, %lu bytes\n", LT_Pu32(width), LT_Pu32(height), LT_Pu32(bitmapBytes));

        if (argc == 3) {
            /* Zero fill the grid, which will effectively disable motion detection */
            lt_memset(pMotionZoneBitGrid->bitmap, 0, bitmapBytes);
            S.iShell->Print(hShell, "Setting empty bit grid\n");
        } else if (argc == 4 && !lt_strcmp(argv[3], "*")) {
            lt_memset(pMotionZoneBitGrid->bitmap, -1, bitmapBytes);
            S.iShell->Print(hShell, "Setting full bit grid\n");
        } else {
            for (int curArg=3; curArg < argc; curArg++) {
                u32 nextByte = lt_strtou32(argv[curArg], NULL, 0);
                pMotionZoneBitGrid->bitmap[curArg-3] = (u8)(nextByte & 0xFF);
            }
        }

        S.motion->SetMotionZones(pMotionZoneBitGrid);
        lt_free(pMotionZoneBitGrid);
    }

    return 0;
}

static int LTShellMotion_ShowZones(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    LTMediaMotionDetection_MotionZoneBitGrid *motionZoneBitGrid;
    u32 motionZoneBitGridSize;
    S.motion->GetMotionZones((void **)&motionZoneBitGrid, &motionZoneBitGridSize);
    if (motionZoneBitGrid == NULL) {
        LTLOG_YELLOWALERT("show.zones.fail", "Failed to get motion zones setting. Could not show zones.\n");
        return -1;
    }

    u8 *currentRow = lt_malloc(motionZoneBitGrid->width);
    if (!currentRow) {
        LTLOG_YELLOWALERT("show.zones.oom", "Not enough memory to allocate to display zone row. Could not show zones.\n");
        return -1;
    }
    lt_memset(currentRow, 0, motionZoneBitGrid->width);

    u32 byteIndex = 0;
    u32 bitIndex = 0;
    for (u32 i = 0; i < motionZoneBitGrid->height; i++) {
        for (u32 j = 0; j < motionZoneBitGrid->width; j++) {

            if (byteIndex >= motionZoneBitGrid->bitmapSize) {
                LTLOG_YELLOWALERT("show.zones.incomplete", "Not enough bytes to display in bitmap, ending early.\n");
                return 0;
            }
            u8 currentByte = motionZoneBitGrid->bitmap[byteIndex];

            /* Check the next bit and set the current row element accordingly */
            currentRow[j] = ((currentByte << (bitIndex % 8)) & 0x80) ? 1 : 0;
            S.iShell->Print(hShell, "%u ", currentRow[j]);
            bitIndex++;

            /* Check if we reached the end of the current byte */
            if (bitIndex % 8 == 0) byteIndex++;
        }
        S.iShell->Print(hShell, "\n");
    }

    return 0;
}

static int LTShellMotion_SetSensitivity(LTShell hShell, int argc, const char *argv[]) {
    u32 sensitivity;

    if (argc < 2) {
        S.iShell->Print(hShell, "motion sense <value>\n");
    } else {
        sensitivity = lt_strtou32(argv[1], NULL, 0);
        // Ensure non-lossy conversion, so that a high value will set sensitivity to 100
        sensitivity = LT_MIN((u32)LT_U8_MAX, sensitivity);
        u32 oldSensitivity = S.motion->GetSensitivity();
        S.iShell->Print(hShell, "Old motion sensitivity value: %lu\n", LT_Pu32(oldSensitivity));
        S.motion->SetSensitivity((u8)sensitivity);
    }

    sensitivity = S.motion->GetSensitivity();
    S.iShell->Print(hShell, "Current motion sensitivity value: %lu\n", LT_Pu32(sensitivity));
    u8 differenceThreshold = S.algorithm->API->GetDifferenceThreshold(S.algorithm);
    S.iShell->Print(hShell, "Difference threshold: %lu\n", LT_Pu32(differenceThreshold));

    if (S.algorithm->API->IsHeatmapEnabled(S.algorithm)) {
        MotionHeatmapParameters heatmapParams;
        S.algorithm->API->GetHeatmapParameters(S.algorithm, &heatmapParams);
        S.iShell->Print(hShell, "Heatmap Gain: %.2f\n; Heatmap Drop: %.2f\n", heatmapParams.gain, heatmapParams.drop);
    }
    return 0;
}

static int LTShellMotion_SetTimingMode(LTShell hShell, int argc, const char *argv[]) {
    u32 mode;

    if (argc < 2) {
        S.iShell->Print(hShell, "motion time <0|1|2>\n");
        S.iShell->Print(hShell, "  - 0 disables motion timing\n");
        S.iShell->Print(hShell, "  - 1 enables motion timing for the call to CheckFrameFor(Multiple)Motion() only\n");
        S.iShell->Print(hShell, "  - 2 enables motion timing for the entire call to MediaSourceMotionEventProc()\n");
    } else {
        mode = lt_strtou32(argv[1], NULL, 0);
        if (mode > 2) {
            S.iShell->Print(hShell, "ERROR: argument must be 0 for disabling motion timing, 1 for timing only the frame checking logic, or 2 for timing the whole motion proc call!\n");
            return -1;
        }
        S.motion->SetMotionTimingMode((LTMediaMotionTimingMode)mode);
        S.iShell->Print(hShell, "Motion timing mode set to %lu\n", LT_Pu32(mode));
    }

    return 0;
}

static int LTShellMotion_SetROIDetection(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        S.iShell->Print(hShell, "motion algo <single|multi>\n");
        S.iShell->Print(hShell, "  - single: use single ROI detection algorithm\n");
        S.iShell->Print(hShell, "  - multi: use multi-ROI detection algorithm\n");
    } else {
        if (!lt_strcasecmp(argv[1], "single")) {
            S.algorithm->API->SetROICalculationStrategy(S.algorithm, kLTMediaMotionROICalculationStrategy_SingleROI);
            S.iShell->Print(hShell, "ROI detection algorithm set to use single ROI calculation\n");
        } else if (!lt_strcasecmp(argv[1], "multi")) {
            S.algorithm->API->SetROICalculationStrategy(S.algorithm, kLTMediaMotionROICalculationStrategy_MultipleROIs);
            S.iShell->Print(hShell, "ROI detection algorithm set to use multi ROI calculation\n");
        } else {
            S.iShell->Print(hShell, "ERROR: argument must be 'single' or 'multi'!\n");
            return -1;
        }
    }

    return 0;
}

static int LTShellMotion_GetSensitivityTableString(LTShell hShell, int argc, const char *argv[]) {
    if (argc != (MOTION_SENSITIVITY_BUCKETS * 2 + 1)) {

        S.iShell->Print(hShell, "motion sensetable <table_entries>\n");
        S.iShell->Print(hShell, "  This command is used to print the base 64 encoded string corresponding to the provided sensitivity table entries.\n");
        S.iShell->Print(hShell, "  This string can be stored in LTProductConfig to update the default table, or issued in a ConfigService deployment to override the 'motion/sensitivityTable' setting.\n");
        S.iShell->Print(hShell, "  - specify exactly %lu pairs of values\n", LT_Pu32(MOTION_SENSITIVITY_BUCKETS));
        S.iShell->Print(hShell, "  - each pair is an entry for the corresponding sensitivity bucket (0 - %lu)\n", LT_Pu32(MOTION_SENSITIVITY_BUCKETS - 1));
        S.iShell->Print(hShell, "  - the first value in the pair is the sensitivity threshold for that bucket\n");
        S.iShell->Print(hShell, "  - the second value in the pair is the minimum number of moxels in a motion island for that bucket\n");
        S.iShell->Print(hShell, "  - e.g. `motion sensetable 171 2 160 2 149 2 138 2 127 2 116 2 105 2 94 2 83 2 72 2 61 2`\n");
    } else {
        /* Iterate over provided arguments and fill in table accordingly. */
        MotionSensitivityParameters motionSensitivityTable[MOTION_SENSITIVITY_BUCKETS] = {0};
        const char **currentArg = &argv[1];
        for (u32 currentBucket = 0; currentBucket < MOTION_SENSITIVITY_BUCKETS; currentBucket++) {
            motionSensitivityTable[currentBucket].sensitivityThreshold = lt_strtou32(*currentArg, NULL, 10);
            currentArg++;
            motionSensitivityTable[currentBucket].minimumIslandSize = lt_strtou32(*currentArg, NULL, 10);
            currentArg++;
        }

        /* Encode the filled-in table to a base 64 encoded string and print to LT shell. */
        u32 base64TableSize = S.byteOps->GetBase64EncodeBufferRequirement(sizeof(motionSensitivityTable));
        char *tableString = lt_malloc(base64TableSize);
        lt_memset(tableString, 0, base64TableSize);
        if (!tableString) {
            S.iShell->Print(hShell, "ERROR: Could not allocate memory for sensitivity table base 64 encoded string!\n");
            return -1;
        }
        S.byteOps->Base64Encode((u8 *)motionSensitivityTable, sizeof(motionSensitivityTable), tableString, base64TableSize);
        S.iShell->Print(hShell, "Base 64 sensitivity table string: %s\n", tableString);
        lt_free(tableString);
    }

    return 0;
}

static int LTShellMotion_ToggleHeatmap(LTShell hShell, int argc, const char * argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    bool newSetting = !S.algorithm->API->IsHeatmapEnabled(S.algorithm);
    S.algorithm->API->EnableHeatmap(S.algorithm, newSetting);
    S.iShell->Print(hShell, "Heatmap has been: %s.\n", newSetting ? "enabled" : "disabled");
    return 0;
}

static int LTShellMotion_ToggleBFS(LTShell hShell, int argc, const char * argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    bool newSetting = !S.algorithm->API->IsBFSEnabled(S.algorithm);
    S.algorithm->API->EnableBFS(S.algorithm, newSetting);
    S.iShell->Print(hShell, "BFS has been: %s.\n", newSetting ? "enabled" : "disabled");
    return 0;
}

static int LTShellMotion_CheckHeatmap(LTShell hShell, int argc, const char * argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    bool enableHeatmap = S.algorithm->API->IsHeatmapEnabled(S.algorithm);
    S.iShell->Print(hShell, "Heatmap is %s\n", enableHeatmap ? "enabled" : "disabled");

    float max_v = 0.0, avg_v = 0.0;
    max_v = S.algorithm->API->GetHeatmapMax(S.algorithm);
    avg_v = S.algorithm->API->GetHeatmapAvg(S.algorithm);
    S.iShell->Print(hShell, "Heatmap Max: %.2f\n", max_v);
    S.iShell->Print(hShell, "Heatmap Avg: %.2f\n", avg_v);
    return 0;
}

static int LTShellMotion_ResetHeatmap(LTShell hShell, int argc, const char * argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    float max_b = 0.0, max_a = 0.0, avg_b = 0.0, avg_a = 0.0;
    max_b = S.algorithm->API->GetHeatmapMax(S.algorithm);
    avg_b = S.algorithm->API->GetHeatmapAvg(S.algorithm);
    S.algorithm->API->ResetHeatmap(S.algorithm);
    max_a = S.algorithm->API->GetHeatmapMax(S.algorithm);
    avg_a = S.algorithm->API->GetHeatmapAvg(S.algorithm);
    S.iShell->Print(hShell, "Reset Heatmap\n");
    S.iShell->Print(hShell, "Heatmap Max: before - %.2f, after - %.2f\n", max_b, max_a);
    S.iShell->Print(hShell, "Heatmap Avg: before - %.2f, after - %.2f\n", avg_b, avg_a);
    return 0;
}

static int LTShellMotion_ResetMotionFrameCount(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    u32 totalMotionFrameCount = S.motion->GetMotionFrameCount();
    S.iShell->Print(hShell, "Total motion frame count: %lu\n", LT_Pu32(totalMotionFrameCount));
    S.motion->ResetMotionFrameCount();
    return 0;
}

static int LTShellMotion_SetBFSDistance(LTShell hShell, int argc, const char *argv[]){
    if (argc < 2) {
        S.iShell->Print(hShell, "motion setbfs <bfs_distance>\n");
    } else {
        u8 oldBFSDistance = S.algorithm->API->GetBFSDistance(S.algorithm);
        u8 newBFSDistance = (u8)lt_strtou32(argv[1], NULL, 0);
        S.algorithm->API->SetBFSDistance(S.algorithm, newBFSDistance);
        S.iShell->Print(hShell, "BFS distance set to %lu from %lu\n", LT_Pu32(newBFSDistance), LT_Pu32(oldBFSDistance));
    }
    return 0;
}

/* Forward-decl for MotionCommands array due to circular references */
static int LTShellMotion_Help(LTShell hShell, int argc, const char *argv[]);

static const LTSystemShell_CommandDesc MotionCommands[] = {
    { "help",   LTShellMotion_Help,            "- list supported commands", NULL },
    { "status", LTShellMotion_Status,          "- display motion detection engine status", NULL },
    { "start",  LTShellMotion_Start,           "- start motion detection engine", NULL },
    { "pause",  LTShellMotion_Pause,           "- pause motion detection without stopping engine", NULL },
    { "resume", LTShellMotion_Resume,          "- resume motion detection", NULL },
    { "stop",   LTShellMotion_Stop,            "- stop motion detection engine", NULL },
    { "zones",  LTShellMotion_Zones,           "- set motion zone grid (? for detailed help)", NULL },
    { "sense",  LTShellMotion_SetSensitivity,  "- set motion detection sensitivity", NULL },
    { "time",   LTShellMotion_SetTimingMode,   "- set timing mode for motion detection (0|1|2)", NULL },
    { "algo",   LTShellMotion_SetROIDetection, "- set ROI detection algorithm (single|multi)", NULL },
    { "showzones", LTShellMotion_ShowZones,    "- display the current motion zone grid", NULL },
    { "togglebfs", LTShellMotion_ToggleBFS,    "- toggle useBFS mode", NULL },
    { "sensetable", LTShellMotion_GetSensitivityTableString, "- get the base 64 encoded string for a motion detection sensitivity table (? for detailed help)", NULL },
    { "toggleheatmap", LTShellMotion_ToggleHeatmap, "- toggle enableHeatmap mode", NULL },
    { "resetheatmap", LTShellMotion_ResetHeatmap, "- reset heatmap to 0", NULL },
    { "checkheatmap", LTShellMotion_CheckHeatmap, "- to check if heatmap is updated", NULL },
    { "resetcounters", LTShellMotion_ResetMotionFrameCount, "- reset s_motionEventCount and s_totalMotionFrameCount to 0", NULL },
    { "bfsdistance", LTShellMotion_SetBFSDistance, "- set BFS distance (supported values are 1 to 5)", NULL },
    { }
};

static int LTShellMotion_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argv);

    for (int n = 0; MotionCommands[n].pCommand; n++) {
        S.iShell->Print(hShell, "  %8s %s\n", MotionCommands[n].pCommand, MotionCommands[n].pDescription);
    }
    if (argc < 2) {
        return 0;
    } else {
        return -1;
    }
}

static int LTShellMotion_Handler(LTShell hShell, int argc, const char *argv[]) {
    /* Most commands have no arguments, so display help if any are given, with the exceptions below */
    if (argc == 1) {
        return LTShellMotion_Help(hShell, 0, NULL);
    } else if (argc == 2 ||
        !lt_strcasecmp(argv[1], "zones") ||
        !lt_strcasecmp(argv[1], "sense") ||
        !lt_strcasecmp(argv[1], "algo") ||
        !lt_strcasecmp(argv[1], "sensetable") ||
        !lt_strcasecmp(argv[1], "time") ||
        !lt_strcasecmp(argv[1], "bfsdistance")){
        for (int n = 0; MotionCommands[n].pCommand; n++) {
            if (0 == lt_strcasecmp(MotionCommands[n].pCommand, argv[1])) {
                return MotionCommands[n].pCommandProc(hShell, argc-1, argv+1);
            }
        }
    }
    return LTShellMotion_Help(hShell, 0, NULL);
}

static void LTShell_Help(LTShell hShell, int argc, const char ** argv) {
    /* This LTShell Help proc is so "help motion" works in addition to "motion help".
    * It prints usage and calls the regular LTShellMotion_Help
    */
    S.iShell->Print(hShell, "usage: motion <command> [args]\nCommands:\n");
    (void)LTShellMotion_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc LTShellMotion_Commands[] = {
    { "motion",          LTShellMotion_Handler, "manage and obtain status of motion detection engine", LTShell_Help },
    { "motiondetection", LTShellMotion_Handler, "(alias for 'motion' command)",                        LTShell_Help },
};


/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellMotionImpl_LibFini(void) {
    lt_destroyobject(S.shellMotionThread);
    lt_closelibrary(S.byteOps);
    lt_closelibrary(S.motion);
    if (S.shell) {
        S.shell->UnregisterCommands(LTShellMotion_Commands);
        lt_closelibrary(S.shell);
    }
    lt_destroyobject(S.algorithm);
    S = (struct Statics) {};
}

static bool LTShellMotionImpl_LibInit(void) {
    S = (struct Statics) {
        .core = LT_GetCore(),
    };
    const char *reason;
    do {
        reason = "no UtilityByteOps";
        if (!(S.byteOps = lt_openlibrary(LTUtilityByteOps))) break;

        reason = "thread failed";
        if (!(S.shellMotionThread = lt_createobject(LTOThread))) break;
        S.shellMotionThread->API->SetStackSize(S.shellMotionThread, 1024);
        S.shellMotionThread->API->Start(S.shellMotionThread, "ShellMotion", LTShellMotion_ThreadInit, LTShellMotion_ThreadExit);

        S.motion = lt_openlibrary(LTMediaMotionDetection);
        if (!S.motion) {
            reason = "no motion detection";
            break;
        } else {
            reason = "no shell";
            if (!(S.shell = lt_openlibrary(LTSystemShell))) break;
            S.shell->RegisterCommands(LTShellMotion_Commands, sizeof(LTShellMotion_Commands) / sizeof(LTShellMotion_Commands[0]));
            S.iShell = lt_getlibraryinterface(ILTShell, S.shell);
        }
        reason = "no motion algorithm";
        S.algorithm = S.motion->GetActiveAlgorithm();
        if (!S.algorithm) break;

        return true;
    } while (false);

    LTLOG_YELLOWALERT("init.fail", "init failed: %s", reason);
    LTShellMotionImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellMotion, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellMotion) LTLIBRARY_DEFINITION;
