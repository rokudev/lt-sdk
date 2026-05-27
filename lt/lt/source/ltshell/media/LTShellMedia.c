/*******************************************************************************
 *
 * LTShellMedia: Media Shell
 * -----------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/qrreader/LTDeviceQrreader.h>
#include <lt/media/audioassets/LTMediaAudioAssets.h>
#include <lt/media/microphone/LTMediaMicrophoneAGC.h>
#include <lt/system/fs/LTFile.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/media/LTShellMedia.h>
#include "LTShellMediaDrivers.h"
#include "LTShellMediaOpus.h"
#include "LTShellMediaPCM.h"
#include "LTShellMediaPlayTone.h"


DEFINE_LTLOG_SECTION("ltshell.media");
LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTShellMedia, (LTSystemShell));

enum {
    kCapturePatternSize = 4
};


/** Standard LT Interfaces ****************************************************/
static ILTShell           *SHL_iShell;
static LTDeviceQrreader   *s_qrreader = NULL;
static LTDeviceMedia      *s_deviceMedia = NULL;
static LTMediaAudioAssets *s_audioAssets = NULL;

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/
static void SHL_PlaybackComplete(u32 assetID, void *pClientData) {
    LTShell hShell = VOIDPTR_TO_LTHANDLE(pClientData);
    char *name;
    LT_SIZE assetLength;
    if (s_audioAssets->GetAudioAssetInfo(assetID, &name, &assetLength)) {
        SHL_iShell->Print(hShell, "Playback complete for %s\n", name);
    }
}

static void SHL_StopPlayback(void) {
    StopTone();
    StopOpusPlayback();
    StopPCMPlayback();
}

static int SHL_PlayFile(LTShell hShell, LTFile *pFile, bool loop) {
    LT_UNUSED(loop); // Not implemented yet

    if (!pFile->API->Open(pFile, false)) {
        SHL_iShell->Print(hShell, "Failed to open file\n");
        return -1;
    }

    do {
        u8 fourccBuf[kCapturePatternSize];
        // Read FourCC
        if (pFile->API->Read(pFile, kCapturePatternSize, fourccBuf) != kCapturePatternSize) {
            SHL_iShell->Print(hShell, "Failed to read file fourcc\n");
            break;
        }

        if (lt_memcmp(fourccBuf, "OggS", kCapturePatternSize) == 0) {
            if (!PlayOpusFile(hShell, pFile)) {
                SHL_iShell->Print(hShell, "Failed to play\n");
                break;
            }
            SHL_iShell->Print(hShell, "Playing OGG file\n");
            return 0;
        } else {
            // Assume PCM 16-bit, 16kHz, mono
            LTMediaFormat fmt;
            fmt.nKind = kLTMediaKind_Audio;
            fmt.nEncoding = kLTMediaEncoding_PCM;
            fmt.params.pcm.nSampleRate = 16000;
            fmt.params.pcm.nBitsPerSample = 16;
            fmt.params.pcm.nChannels = 1;
            if (!PlayPCMFile(hShell, pFile, fmt)) {
                SHL_iShell->Print(hShell, "Failed to play\n");
                break;
            }
            SHL_iShell->Print(hShell, "Playing PCM file\n");
            return 0;
        }

        // Add support for other formats here
    } while (0);
    pFile->API->Close(pFile);
    return -1;
}

static int SHL_PlayAsset(LTShell hShell, u32 assetID, bool loop) {
    LT_SIZE assetLength;
    char *assetName;
    s_audioAssets->GetAudioAssetInfo(assetID, &assetName, &assetLength);
    SHL_iShell->Print(hShell, "Playing asset   %s: %lu bytes.\n", assetName, LT_PLT_SIZE(assetLength));
    s_audioAssets->PlayByName(assetName,
                                           kLTAudioAssets_PlayOptions_Async | (loop ? (kLTAudioAssets_PlayOptions_Looped  | 30): 0),
                                           SHL_PlaybackComplete,
                                           LTHANDLE_TO_VOIDPTR(hShell));
    return 0;
}

static int SHL_PlayTone(LTShell hShell) {
    StopTone();
    if(PlayTone(hShell)) {
        SHL_iShell->Print(hShell, "Playing tone\n");
        return 0;
    }
    SHL_iShell->Print(hShell, "Failed to play tone\n");
    return -1;
}

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/
static int SHL_List(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);  LT_UNUSED(argv);
    u32 assetCount = s_audioAssets->GetAudioAssetCount();
    char *name;
    LT_SIZE assetLength;

    SHL_iShell->Print(hShell, "Listing %lu audio assets\n", LT_Pu32(assetCount));
    for (u32 i = 0; i < assetCount; i++) {
        if (s_audioAssets->GetAudioAssetInfo(i, &name, &assetLength)) {
            SHL_iShell->Print(hShell, "    %s: %lu bytes.\n", name, LT_PLT_SIZE(assetLength));
        }
    }
    return 0;
}

static int SHL_Play(LTShell hShell, int argc, const char *argv[]) {
    int ret;
    bool loop = false;
    bool isAsset = false;
    LTFile *pFile;
    u32 assetID;
    if (argc < 2) {
        SHL_iShell->Print(hShell, "play <asset name> [loop]\n");
        return -2;
    }

    if (argc > 2 && 0 == lt_strcasecmp(argv[2], "loop")) {
        loop = true;
    }

    if (lt_strcasecmp(argv[1], "tone") == 0) {
        return SHL_PlayTone(hShell);
    }

    isAsset = s_audioAssets->GetAudioAssetIDByName(argv[1], &assetID);
    if (isAsset) {
        if (SHL_PlayAsset(hShell, assetID, loop) == 0) {
            return 0;
        }
    }

    pFile = lt_createobject(LTFile);
    if (!pFile) {
        SHL_iShell->Print(hShell, "Failed to create file object\n");
        return -1;
    }

    pFile->API->SetName(pFile, argv[1]);
    ret = SHL_PlayFile(hShell, pFile, loop);
    if (ret != 0) {
        SHL_iShell->Print(hShell, "Failed to play file\n");
        lt_destroyobject(pFile);
    }
    return ret;
}

static int SHL_PlayAll(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);  LT_UNUSED(argv);
    u32 i = 0;
    char *name;
    LT_SIZE assetLength;

    while (s_audioAssets->GetAudioAssetInfo(i, &name, &assetLength)) {
        SHL_iShell->Print(hShell, "Playing    %s: %lu bytes.\n", name, LT_PLT_SIZE(assetLength));
        s_audioAssets->Play(i, kLTAudioAssets_PlayOptions_Blocking, NULL, NULL);
        i++;
    }
    return 0;
}

static int SHL_StopAll(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);  LT_UNUSED(argv);
    s_audioAssets->Stop();
    SHL_StopPlayback();
    SHL_iShell->Print(hShell, "Done.\n");
    return 0;
}

static int SHL_Volume(LTShell hShell, int argc, const char *argv[]) {
    u32 volume;
    if (argc > 2) {
        SHL_iShell->Print(hShell, "volume <volume>\n");
        return -1;
    }
    if (argc == 1) {
        if (s_deviceMedia->GetProperty("outputVolume", &volume)) {
            SHL_iShell->Print(hShell, "Volume: %lu\n", LT_Pu32(volume));
            return 0;
        } else {
            SHL_iShell->Print(hShell, "Failed to get volume\n");
            return -1;
        }
    } else {
        volume = lt_strtou32(argv[1], NULL, 10);
        if(s_deviceMedia->SetProperty("outputVolume", &volume)) {
            SHL_iShell->Print(hShell, "Volume set to %lu\n", LT_Pu32(volume));
            return 0;
        } else {
            SHL_iShell->Print(hShell, "Failed to set volume\n");
            return -1;
        }
    }
}

static int SHL_AOAmpEnable(LTShell hShell, int argc, const char *argv[]) {
    bool on;
    if (argc != 2) {
        SHL_iShell->Print(hShell, "aoamp [on|off]\n");
        return -1;
    }

    if (0 == lt_strcasecmp(argv[1], "on")) {
        on = true;
    } else if (0 == lt_strcasecmp(argv[1], "off")) {
        on = false;
    } else {
        SHL_iShell->Print(hShell, "Invalid argument\n");
        return -1;
    }
    if(s_deviceMedia->SetProperty("aoamp", &on)) {
        SHL_iShell->Print(hShell, "Audio output amp %s\n", on ? "enabled" : "disabled");
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to set audio output amp\n");
        return -1;
    }
}

static int SHL_StopCapture(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);  LT_UNUSED(argv);
    StopOpusCapture();
    StopPCMCapture();
    SHL_iShell->Print(hShell, "Capture stopped\n");
    return 0;
}

static int SHL_Capture(LTShell hShell, int argc, const char *argv[]) {
    LTFile *pFile;
    LTMediaFormat fmt;

    if (argc < 3) {
        SHL_iShell->Print(hShell, "capture [pcm16khz|pcm8khz|opus] <filename>\n");
        return -1;
    }

    fmt.nKind = kLTMediaKind_Audio;
    fmt.nEncoding = kLTMediaEncoding_Unknown;
    if (0 == lt_strcasecmp(argv[1], "pcm16khz")) {
        fmt.nEncoding = kLTMediaEncoding_PCM;
        fmt.params.pcm.nSampleRate = 16000;
        fmt.params.pcm.nBitsPerSample = 16;
        fmt.params.pcm.nChannels = 1;
    } else if (0 == lt_strcasecmp(argv[1], "pcm8khz")) {
        fmt.nEncoding = kLTMediaEncoding_PCM;
        fmt.params.pcm.nSampleRate = 8000;
        fmt.params.pcm.nBitsPerSample = 16;
        fmt.params.pcm.nChannels = 1;
    } else if (0 == lt_strcasecmp(argv[1], "opus")) {
        fmt.nEncoding = kLTMediaEncoding_Opus;
        fmt.params.opus.nChannels = 1;
        fmt.params.opus.nSampleRate = 16000;
    } else {
        SHL_iShell->Print(hShell, "Unsupported encoding\n");
        return -1;
    }

    if (!s_deviceMedia->SupportsFormat(&fmt)) {
        SHL_iShell->Print(hShell, "Format not supported\n");
        return -1;
    }

    pFile = lt_createobject(LTFile);
    if (!pFile) {
        SHL_iShell->Print(hShell, "Failed to create file object\n");
        return -1;
    }

    pFile->API->SetName(pFile, argv[2]);
    if (!pFile->API->Open(pFile, true)) {
        SHL_iShell->Print(hShell, "Failed to open file\n");
        lt_destroyobject(pFile);
        return -1;
    }

    switch (fmt.nEncoding) {
        case kLTMediaEncoding_PCM:
            if (StartPCMCapture(hShell, pFile, fmt)) {
                SHL_iShell->Print(hShell, "Capturing PCM\n");
                return 0;
            }
            break;
        case kLTMediaEncoding_Opus:
            if (StartOpusCapture(hShell, pFile)) {
                SHL_iShell->Print(hShell, "Capturing OPUS\n");
                return 0;
            }
            break;
        default:
            break;
    }

    SHL_iShell->Print(hShell, "Failed to start capture\n");
    lt_destroyobject(pFile);
    return -1;
}

static int SHL_AgcMode(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        SHL_iShell->Print(hShell, "setagcmode <mode> { 0: Unchanged, 1: AdaptiveAnalog,  2: AdaptiveDigital, 3: FixedDigital}\n");
        return -1;
    }
    u32 agcmode = lt_strtou32(argv[1], NULL, 10);
    if(s_deviceMedia->SetProperty("agcmode", &agcmode)) {
        SHL_iShell->Print(hShell, "agcmode set to %lu\n", LT_Pu32(agcmode));
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to set agcmode\n");
        return -1;
    }
    return 0;
}

static int SHL_AgcConfig(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 4) {
        SHL_iShell->Print(hShell, "setagcmode <targetLevelDbfs> <compressionGaindB> <limiterEnable>\n");
        return -1;
    }
    LTMediaMicrophoneAGC_Config config;
    config.targetLevelDbfs = lt_strtos32(argv[1], NULL, 10);
    config.compressionGaindB = lt_strtos32(argv[2], NULL, 10);
    config.limiterEnable = lt_strtou32(argv[3], NULL, 10);
    if(s_deviceMedia->SetProperty("agcconfig", &config)) {
        SHL_iShell->Print(hShell, "agcconfig set to targetleveldbfs %d compressionGaindB %d limiterEnable %u\n",
                config.targetLevelDbfs, config.compressionGaindB, config.limiterEnable);
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to set agcconfig\n");
        return -1;
    }
    return 0;
}

static int SHL_AgcLevel(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 3) {
        SHL_iShell->Print(hShell, "setagclevel <minLevel> <maxLevel>\n");
        return -1;
    }
    LTMediaMicrophoneAGC_Level level;
    level.minLevel = lt_strtos32(argv[1], NULL, 10);
    level.maxLevel = lt_strtos32(argv[2], NULL, 10);
    if(s_deviceMedia->SetProperty("agclevel", &level)) {
        SHL_iShell->Print(hShell, "agclevel set to %ld %ld\n", LT_Ps32(level.minLevel), LT_Ps32(level.maxLevel));
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to set agclevel\n");
        return -1;
    }
    return 0;
}

static int SHL_AecEnable(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        SHL_iShell->Print(hShell, "aecenable {0: off, 1: on}\n");
        return -1;
    }
    bool on = !!(lt_strtou32(argv[1], NULL, 10));
    if(s_deviceMedia->SetProperty("aecenable", &on)) {
        SHL_iShell->Print(hShell, "AEC %s\n", on ? "enabled" : "disabled");
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to set echo cancellation\n");
        return -1;
    }
}

static int SHL_WebrtcNsEnable(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        SHL_iShell->Print(hShell, "nsenable {0: off, 1: on}\n");
        return -1;
    }
    bool on = !!(lt_strtou32(argv[1], NULL, 10));
    if(s_deviceMedia->SetProperty("webrtcnsenable", &on)) {
        SHL_iShell->Print(hShell, "NS enable %s\n", on ? "enabled" : "disabled");
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to set webrtc noise suppression\n");
        return -1;
    }
}

static int SHL_AecTail(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        SHL_iShell->Print(hShell, "setaectail <tail>\n");
        return -1;
    }
    u32 aectail = lt_strtou32(argv[1], NULL, 10);
    if(s_deviceMedia->SetProperty("aectail", &aectail)) {
        SHL_iShell->Print(hShell, "aectail set to %lu\n", LT_Pu32(aectail));
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to set aectail\n");
        return -1;
    }
    return 0;
}

static int SHL_SpeexNsEnable(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        SHL_iShell->Print(hShell, "nsenable {0: disable, 1: enable}\n");
        return -1;
    }
    bool enable = !!(lt_strtou32(argv[1], NULL, 10));
    if(s_deviceMedia->SetProperty("speexnsenable", &enable)) {
        SHL_iShell->Print(hShell, "NS %s\n", enable ? "enabled" : "disabled");
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to set speex noise suppression\n");
        return -1;
    }
}

static int SHL_NsMode(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        SHL_iShell->Print(hShell, "webrtcnsmode <mode> {0: Mild (6 dB), 1: Medium (10 dB), 2: Aggressive (15 dB)}\n");
        return -1;
    }
    u32 nsmode = lt_strtou32(argv[1], NULL, 10);
    if (nsmode > 2) {
        SHL_iShell->Print(hShell, "Invalid argument\n");
        return -1;
    }
    if(s_deviceMedia->SetProperty("webrtcnsmode", &nsmode)) {
        SHL_iShell->Print(hShell, "webrtcnsmode set to %lu\n", LT_Pu32(nsmode));
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to set webrtcnsmode\n");
        return -1;
    }
    return 0;
}

static int SHL_ProcDebug0(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        SHL_iShell->Print(hShell, "setprocdebug0 { 0: RawCapture 1: RawPlayback \n\
                2: PostHpf0 3: PreAec 4: PostAec 5: PreAgc \n\
                6: PostAgc 7: PreHpf1 8: PostHpf1 }\n");
        return -1;
    }
    u32 proc = lt_strtou32(argv[1], NULL, 10);
    if(s_deviceMedia->SetProperty("procdebug0", &proc)) {
        SHL_iShell->Print(hShell, "procdebug0 set to %lu\n", LT_Pu32(proc));
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to set procdebug0\n");
        return -1;
    }
    return 0;
}

static int SHL_ProcDebug1(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        SHL_iShell->Print(hShell, "setprocdebug1 { 0: RawCapture 1: RawPlayback \n\
                2: PostHpf0 3: PreAec 4: PostAec 5: PreAgc \n\
                6: PostAgc 7: PreHpf1 8: PostHpf1 }\n");
        return -1;
    }
    u32 proc = lt_strtou32(argv[1], NULL, 10);
    if(s_deviceMedia->SetProperty("procdebug1", &proc)) {
        SHL_iShell->Print(hShell, "procdebug1 set to %lu\n", LT_Pu32(proc));
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to set procdebug1\n");
        return -1;
    }
    return 0;
}

static int SHL_QrTest(LTShell hShell, int argc, const char *argv[]) {
    if (argc == 2) {
        if (0 == lt_strcasecmp(argv[1], "start")) {
            if(s_qrreader) {
                SHL_iShell->Print(hShell, "qrtest already running!\n");
            } else {
                s_qrreader = lt_openlibrary(LTDeviceQrreader);
                if (!s_qrreader) {
                    SHL_iShell->Print(hShell, "failed to start qrreader\n");
                    return -1;
                }
                s_qrreader->Start(false);
            }
            return 0;
        } else if (0 == lt_strcasecmp(argv[1], "stop")) {
            if(!s_qrreader) {
                SHL_iShell->Print(hShell, "qrtest already stopped!\n");
            } else {
                s_qrreader->Stop();
                lt_closelibrary(s_qrreader);
                s_qrreader = NULL;
            }
            return 0;
        }
    }

    /* An error if we get here */
    SHL_iShell->Print(hShell, "qrtest [start|stop]\n");
    return -2;
}

static int SHL_MediaDrivers(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    if (deviceConfig) {
        u32 nNumDrivers = deviceConfig->GetNumDrivers("LTDeviceMedia");
        if (nNumDrivers == 0) {
            SHL_iShell->Print(hShell, "No media drivers were found\n");
        } else {
            for (u32 i = 0; i < nNumDrivers; i++) {
                const char *pLibraryName = deviceConfig->GetDriverAt("LTDeviceMedia", i);
                SHL_iShell->Print(hShell, "  - %s\n", pLibraryName);
            }
        }
        lt_closelibrary(deviceConfig);
    }
    return 0;
}

static const LTSystemShell_CommandDesc SHL_Commands[] = {
    { "listmedia",      SHL_List,          "List media",                       NULL},
    { "play",           SHL_Play,          "Play media",                       NULL },
    { "playall",        SHL_PlayAll,       "Play all media",                   NULL },
    { "stopall",        SHL_StopAll,       "Stop all media",                   NULL },
    { "volume",         SHL_Volume,        "Get/Set output volume",            NULL },
    { "aoamp",          SHL_AOAmpEnable,   "Enable/Disable audio output amp",  NULL },
    { "capture",        SHL_Capture,       "Capture media",                    NULL },
    { "stopcapture",    SHL_StopCapture,   "Stop media capture",               NULL },
    { "agcmode",        SHL_AgcMode,       "Set AGC Mode",                     NULL },
    { "agcconfig",      SHL_AgcConfig,     "Set AGC Config",                   NULL },
    { "agclevel",       SHL_AgcLevel,      "Set AGC Level",                    NULL },
    { "aecenable",      SHL_AecEnable,     "Enable AEC",                       NULL },
    { "aectail",        SHL_AecTail,       "Set AEC Tail",                     NULL },
    { "webrtcnsenable", SHL_WebrtcNsEnable,"Enable webrtc NS",                 NULL },
    { "speexnsenable",  SHL_SpeexNsEnable, "Enable speex NS",                  NULL },
    { "webrtcnsmode",   SHL_NsMode,        "NS Mode",                          NULL },
    { "procdebug0",     SHL_ProcDebug0,    "Set ProcDebug Chn 0",              NULL },
    { "procdebug1",     SHL_ProcDebug1,    "Set ProcDebug Chn 1",              NULL },
    { "qrtest",         SHL_QrTest,        "QR Test Mode start/stop",          NULL },
    { "mediadrivers",   SHL_MediaDrivers,  "Display available media drivers",  NULL }
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellMediaImpl_LibFini(void) {
    LTShellVideoDriverImpl_LibFini();
    LT_GetLTSystemShell()->UnregisterCommands(SHL_Commands);
}

static bool LTShellMediaImpl_LibInit(void) {
    s_deviceMedia = lt_openlibrary(LTDeviceMedia);
    s_audioAssets = lt_openlibrary(LTMediaAudioAssets);
    if (s_deviceMedia && s_audioAssets) {
        SHL_iShell = lt_getlibraryinterface(ILTShell, LT_GetLTSystemShell());
        LT_GetLTSystemShell()->RegisterCommands(SHL_Commands, sizeof(SHL_Commands) / sizeof(SHL_Commands[0]));
    } else {
        LTLOG_YELLOWALERT("degraded", "No local LTDeviceMedia or LTMediaAudioAssets, main media shell commands not registered");
    }
    return LTShellVideoDriverImpl_LibInit();
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/
define_LTLIBRARY_ROOT_INTERFACE(LTShellMedia)
    .SetMotionSnapshotURL = LTShellVideoDriverImpl_SetMotionSnapshotURL,

LTLIBRARY_DEFINITION;
