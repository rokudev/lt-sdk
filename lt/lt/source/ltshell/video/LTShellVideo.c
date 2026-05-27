/*******************************************************************************
 * lt/source/ltshell/video/LTShellVideo.c
 *
 *   Video device shell commands
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/LTTypes.h>
#include <lt/LTObject.h>
#include <lt/core/LTCore.h>
#include <lt/device/video/LTDeviceVideo.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/device/media/LTDeviceMedia.h>

DEFINE_LTLOG_SECTION("shell.video");
#define P(...)
#define PLOG(...)  LTLOG(__VA_ARGS__)

static LTDeviceVideo *s_video;
static LTSystemShell *s_shell;

/** LTShellVideo commands *****************************************************/

static int Command_Wdr(LTShell hShell, int argc, const char *argv[]) {
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    bool bEnable = false;
    if (argc == 1) {
        iShell->Print(hShell, "Command: wdr [on|off]\n");
        s_video->GetParam(kLTDeviceVideo_Param_Wdr, &bEnable);
        iShell->Print(hShell, "WDR status: %s\n", bEnable ? "On" : "Off");
    } else {
        if (lt_strncmp(argv[1], "on", 2) == 0) bEnable = true;
        s_video->SetParam(kLTDeviceVideo_Param_Wdr, &bEnable);
    }
    return 0;
}

static int Command_Ae(LTShell hShell, int argc, const char *argv[]) {
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    bool bEnable = false;
    if (argc == 1) {
        iShell->Print(hShell, "Command: ae [on|off]\n");
        s_video->GetParam(kLTDeviceVideo_Param_AutoExposure, &bEnable);
        iShell->Print(hShell, "AE status: %s\n", bEnable ? "On" : "Off");
    } else {
        if (lt_strncmp(argv[1], "on", 2) == 0) bEnable = true;
        s_video->SetParam(kLTDeviceVideo_Param_AutoExposure, &bEnable);
    }
    return 0;
}

static int Command_Bitrate(LTShell hShell, int argc, const char *argv[]) {
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    if (argc == 1) {
        iShell->Print(hShell, "Command: bitrate channel [rate]\n");
        iShell->Print(hShell, "Param channel: 0 or 2. 0 is HD, 2 is SD\n");
        iShell->Print(hShell, "Param rate: kbps\n");
        return 0;
    }
    LTDeviceVideo_Channel ch = lt_strtou32(argv[1], NULL, 10);
    LTDeviceVideo_Bitrate br = (LTDeviceVideo_Bitrate){ch, 0, 0};
    if (argc == 2) {
        s_video->GetParam(kLTDeviceVideo_Param_Bitrate, &br);
        iShell->Print(hShell, "bitrate %u stat %u (kbps)\n", (u16)br.bitrate, (u16)br.stat);
    } else {
        br.bitrate = lt_strtou32(argv[2], NULL, 10);
        s_video->SetParam(kLTDeviceVideo_Param_Bitrate, &br);
    }
    return 0;
}

static int Command_GopLength(LTShell hShell, int argc, const char *argv[]) {
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    if (argc == 1) {
        iShell->Print(hShell, "Command: gop channel [length]\n");
        iShell->Print(hShell, "Param channel: 0 or 2. 0 is HD, 2 is SD\n");
        return 0;
    }
    LTDeviceVideo_Channel ch = lt_strtou32(argv[1], NULL, 10);
    LTDeviceVideo_Gop gop = (LTDeviceVideo_Gop){ch, 0};
    if (argc == 2) {
        s_video->GetParam(kLTDeviceVideo_Param_GopLength, &gop);
        iShell->Print(hShell, "gop length %u\n", (u16)gop.length);
    } else {
        gop.length = lt_strtou32(argv[2], NULL, 10);
        s_video->SetParam(kLTDeviceVideo_Param_GopLength, &gop);
    }
    return 0;
}

static int Command_IFrame(LTShell hShell, int argc, const char *argv[]) {
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    if (argc == 1) {
        iShell->Print(hShell, "Command: iframe channel\n");
        iShell->Print(hShell, "Param channel: 0 HD, 2 SD\n");
    } else {
        LTDeviceVideo_Channel ch = lt_strtou32(argv[1], NULL, 10);
        s_video->RequestIdrFrame(ch);
    }
    return 0;
}

static int Command_ISPTuning(LTShell hShell, int argc, const char *argv[]) {
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    LTDeviceVideo_ISPTuningMode m = kLTDeviceVideo_ISPTuningMode0;
    if (argc == 1) {
        iShell->Print(hShell, "Command: isptuning [day|night]\n");
        iShell->Print(hShell, "Command: isptuning [mode_number]\n");
        iShell->Print(hShell, "Default day mode number is 0 and default night mode number is 1.\n");
        iShell->Print(hShell, "The mode numbers are defined by video driver.\n");
        iShell->Print(hShell, "For example, in Alta video driver, S19X/S1CS has day (0) and night (1), S1EM/S1EL has day (2) night (3).\n");
        iShell->Print(hShell, "\n");
        s_video->GetParam(kLTDeviceVideo_Param_ISPTuningMode, &m);
        iShell->Print(hShell, "Current mode %d\n", m);
    } else {
        const char *mode = argv[1];
        if (lt_strncmp(mode, "day", 3) == 0) m = kLTDeviceVideo_ISPTuningModeNaturalDay;
        else if (lt_strncmp(mode, "arti", 4) == 0) m = kLTDeviceVideo_ISPTuningModeArtificialDay;
        else if (lt_strncmp(mode, "night", 5) == 0) m = kLTDeviceVideo_ISPTuningModeNight;
        else m = lt_strtou32(mode, NULL, 10);
        s_video->SetParam(kLTDeviceVideo_Param_ISPTuningMode, &m);
        iShell->Print(hShell, "ISP tuning mode %d\n", m);
    }
    return 0;
}

static int Command_PrintTuningDataHash(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    LTString hash = NULL;
    s_video->GetParam(kLTDeviceVideo_Param_TuningDataHash, &hash);

    if (hash) {
        iShell->Print(hShell, "Tuning data hash: %s\n", hash);
        ltstring_destroy(hash);
    } else {
        iShell->Print(hShell, "No tuning data hash available.\n");
    }
    return 0;
}

static int Command_Rotate(LTShell hShell, int argc, const char *argv[]) {
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    u32 degree = 0;
    if (argc == 1) {
        iShell->Print(hShell, "Command: rotation [0|180]\n");
        s_video->GetParam(kLTDeviceVideo_Param_Rotation, &degree);
        iShell->Print(hShell, "Rotation status: %d\n", degree);
    } else {
        degree = lt_strtou32(argv[1], NULL, 10);
        s_video->SetParam(kLTDeviceVideo_Param_Rotation, (void *)&degree);
    }
    return 0;
}

static int Command_Flip(LTShell hShell, int argc, const char *argv[]) {
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    if (argc == 1) {
        iShell->Print(hShell, "Command: flip h|v\n");
        iShell->Print(hShell, "Flip horizontally (h) or vertically (v)\n");
    } else {
        const char *flip = argv[1];
        if (flip[0] == 'h') s_video->SetParam(kLTDeviceVideo_Param_FlipHorizontal, NULL);
        else s_video->SetParam(kLTDeviceVideo_Param_FlipVertical, NULL);
    }
    return 0;
}

static int Command_Osd(LTShell hShell, int argc, const char *argv[]) {
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    if (argc < 3) {
        iShell->Print(hShell, "Command: osd channel on|off\n");
        iShell->Print(hShell, "Param channel: 0 HD stream, 1 HD JPEG, 2 SD stream, 3 SD JPEG\n");
        iShell->Print(hShell, "\n");
    } else {
        LTDeviceVideo_Osd osd = {};
        osd.channel = lt_strtou32(argv[1], NULL, 10);
        osd.data = "0123-45-67 89:01:23";
        osd.bEnable = (lt_strncmp(argv[2], "on", 2) == 0);
        s_video->SetParam(kLTDeviceVideo_Param_OsdLogo, &osd);
        s_video->SetParam(kLTDeviceVideo_Param_OsdTimestamp, &osd);
    }
    return 0;
}

static int Command_Status(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(hShell); LT_UNUSED(argc); LT_UNUSED(argv);
    s_video->GetParam(kLTDeviceVideo_Param_Status, "");
    return 0;
}

static const LTSystemShell_CommandDesc s_VideoCommands[] = {
    { "wdr",         Command_Wdr,                   "Turn on and off wide dynamic range",           NULL },
    { "ae",          Command_Ae,                    "Turn on and off auto exposure",                NULL },
    { "bitrate",     Command_Bitrate,               "Get and set bitrate",                          NULL },
    { "gop",         Command_GopLength,             "Get and set gop length",                       NULL },
    { "iframe",      Command_IFrame,                "Request an IDR frame",                         NULL },
    { "isptuning",   Command_ISPTuning,             "Get and set current ISP tuning",               NULL },
    { "tuninghash",  Command_PrintTuningDataHash,   "Print the SHA-256 of current tuning data",     NULL },
    { "rotate",      Command_Rotate,                "rotate 180(on) or 0(off) degree",              NULL },
    { "flip",        Command_Flip,                  "Flip vertically or horizontally",              NULL },
    { "osd",         Command_Osd,                   "Turn on and off OSD",                          NULL },
    { "videostatus", Command_Status,                "ISP and encoder status",                       NULL }
};

/** LTShellVideo library ******************************************************/

static void LTShellVideoImpl_LibFini(void) {
    if (s_shell) {
        s_shell->UnregisterCommands(s_VideoCommands);
        lt_closelibrary(s_shell);
        s_shell = NULL;
    }
    if (s_video) {
        lt_closelibrary(s_video);
        s_video = NULL;
    }
}

static bool LTShellVideoImpl_LibInit(void) {
    do {
        if (!(s_shell = lt_openlibrary(LTSystemShell))) { LTLOG("fail.shell", NULL); break; }
        if (!(s_video = lt_openlibrary(LTDeviceVideo))) { LTLOG("fail.video", NULL); break; }
        s_shell->RegisterCommands(s_VideoCommands, sizeof(s_VideoCommands)/sizeof(s_VideoCommands[0]));
        return true;
    } while (0);
    LTLOG_YELLOWALERT("init", "fail");
    LTShellVideoImpl_LibFini();
    return false;
}

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellVideo, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellVideo)     LTLIBRARY_DEFINITION;
