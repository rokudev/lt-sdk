/*******************************************************************************
 *
 * LTShellAnalogInput - Shell commands for A/D input
 * -----------------------------------------------------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/adc/LTDeviceADC.h>
#if 0
#include <lt/device/analoginput/LTDeviceAnalogInput.h>
#endif
#include <lt/system/shell/LTSystemShell.h>

DEFINE_LTLOG_SECTION("ltshell.analoginput");

static LTSystemShell *s_pShell;
static ILTShell      *s_pIShell;

static void Help(LTShell hShell, int argc, const char *argv[]) { LT_UNUSED(argc), LT_UNUSED(argv);
    s_pIShell->PutString(hShell, "Usage: sample <channel>\n");
}

static int Sample(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        Help(hShell, argc, argv);
        return 1;
    }
    LTDeviceADC *pADC = lt_openlibrary(LTDeviceADC);
    if (!pADC) {
        s_pIShell->Print(hShell, "Unable to open LTDeviceADC\n");
        return 1;
    }

    LTADCChannel *pChannel = pADC->GetChannelByName(argv[1]);
    if (!pChannel) {
        s_pIShell->Print(hShell, "Unknown channel: \"%s\"\n", argv[1]);
        return 1;
    }
    u32 sample;
    if (!pChannel->API->GetSample(pChannel, &sample)) {
        s_pIShell->Print(hShell, "Unable to sample channel \"%s\"\n", argv[1]);
        return 1;
    }
    s_pIShell->Print(hShell, "channel \"%s\": %lu %08lX\n", argv[1], LT_Pu32(sample), LT_Pu32(sample));
    lt_destroyobject(pChannel);
    lt_closelibrary(pADC);
    return 0;
}

static const LTSystemShell_CommandDesc s_ADCCommands[] = {
    { "sample", Sample, "Sample an analog channel", Help }
};

static void ShellFini(void) {
    if (s_pShell) {
        s_pShell->UnregisterCommands(s_ADCCommands);
        lt_closelibrary(s_pShell);
        s_pShell  = NULL;
        s_pIShell = NULL;
    }
}

static bool ShellInit(void) {
    if (!(s_pShell = lt_openlibrary(LTSystemShell))) return false;
    s_pIShell = lt_getlibraryinterface(ILTShell, s_pShell);
    s_pShell->RegisterCommands(s_ADCCommands, sizeof(s_ADCCommands) / sizeof(s_ADCCommands[0]));
    return true;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTShellAnalogInputImpl_LibFini(void) {
    ShellFini();
}

static bool LTShellAnalogInputImpl_LibInit(void) {
    if (!ShellInit()) {
        LTLOG_YELLOWALERT("init.fail", NULL);
        LTShellAnalogInputImpl_LibFini();
        return false;
    }
    return true;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellAnalogInput, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellAnalogInput) LTLIBRARY_DEFINITION;
