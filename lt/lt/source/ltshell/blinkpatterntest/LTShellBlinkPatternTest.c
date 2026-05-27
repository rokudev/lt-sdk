/*******************************************************************************
 *
 * LTShellBlinkPatternTest - Shell commands for testing LED blink patterns
 *                           Originally written for UX prototyping of the
 *                           indicator LED on Alta (first Roku Indoor Camera).
 * -----------------------------------------------------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/led/LTDeviceLED.h>
#include <lt/system/shell/LTSystemShell.h>

DEFINE_LTLOG_SECTION("ltshell.blinkpattern");

static struct {
    LTOThread                            *thread;
    LTDeviceLEDFader                     *fader;
    LTDeviceLED                          *pLED;
    LTSystemShell                        *pShell;
    ILTShell                             *iShell;
    LTDeviceLEDFaderTimingStep           *pTimingSteps;
} S;

static bool EndsWithPeriod(const char *s) { return lt_strlen(s) ? *(s + lt_strlen(s) - 1) == '.' : false; }

static void BlinkHelp(LTShell hShell, int argc, const char *argv[]) { LT_UNUSED(argc), LT_UNUSED(argv);
    S.iShell->PutString(hShell, "usage: blink <device> <ramp time> <initial state> <dwell> [<ramp time> <next state> <dwell> [...]]\n"
                                "           Express color states in 32-bit hex, IRGB.\n"
                                "           Express dwell and ramp time in milliseconds.\n"
                                "           A dwell of 0 stops the sequence and disables repeat.\n"
                                "           A ramp time of zero causes a step transition.\n"
                                "           Examples:\n"
                                "                blink indicator 0 FF00FF00 500 0 FF0000FF 500 0 0 0\n"
                                "                (green for 500ms, blue for 500ms, stop)\n");
    S.iShell->PutString(hShell, "                blink indicator 0 FFFFFFFF 200 0 0 800\n"
                                "                (blink white, 1Hz, 20% duty cycle)\n"
                                "                blink indicator 1000 FFFFFFFF 1 1000 0 1\n"
                                "                (ramp up to full white for 1s, them ramp down to off, repeat)\n"
                                "       blink stop (interrupt the sequence)\n"
                                "       blink. <device> <ramp time> <initial state> <dwell> [<ramp time> <next state> <dwell> [...]]\n"
                                "           Enable step- and sequence-complete callbacks.\n");
}

enum {
    kNumInitialArguments = 2,      /* command and Device Unit name */
    kNumParametersPerStep = 3      /* transition, color, and dwell */
};

static void ParseStep(const char *argv[], LTDeviceLEDFaderTimingStep *pStep) {
    /* The current step being parsed is pointed to by pStep; its position in the step list (beginning at
       pTimingSteps) determines the argv array element indices for this step's parameters. */
    /* Determine the starting argv element index for this step's parameters: */
    u32 i = (pStep - S.pTimingSteps) * kNumParametersPerStep + kNumInitialArguments;
    /* Gather the parameters at that index (number of parameters below shall agree with kNumParametersPerStep above): */
    pStep->transition = lt_strtou32(argv[i],     NULL, 10);
    pStep->color      = lt_strtou32(argv[i + 1], NULL, 16);
    pStep->dwell      = lt_strtou32(argv[i + 2], NULL, 10);
}

static void StepComplete(const LTDeviceLEDFaderTimingStep *pStep, bool bReached, void *pClientData) { LT_UNUSED(pClientData);
    LTLOG("complete.step", "%s %08lX", bReached ? "reached" : "interrupted", LT_Pu32((u32)pStep));
}

static void SequenceComplete(const LTDeviceLEDFaderTimingStep *pStep, bool bReached, void *pClientData) { LT_UNUSED(pClientData);
    LTLOG("complete.sequence", "%s %08lX", bReached ? "reached" : "interrupted", LT_Pu32((u32)pStep));
}

/* Parse the command line arguments to assemble the blink-pattern sequence.  If everything looks good,
   begin the sequence with the first step: */
static int Blink(LTShell hShell, int argc, const char *argv[]) {
    if (argc == 2 && !lt_strcmp(argv[1], "stop")) {
        if (S.fader) {
            lt_destroyobject(S.fader);
            S.fader = NULL;
            return 0;
        } else {
            S.iShell->PutString(hShell, "No LED Fader is running\n");
            return 4;
        }
        return 0;
    }
    if (argc < kNumInitialArguments + kNumParametersPerStep || (argc - kNumInitialArguments) % kNumParametersPerStep) {
        BlinkHelp(hShell, argc, argv);
        return 1;
    }
    if (S.fader) lt_destroyobject(S.fader);
    lt_free(S.pTimingSteps);
    u16 nNumStepsRemaining;
    u16 nNumSteps = nNumStepsRemaining = (argc - kNumInitialArguments) / kNumParametersPerStep;
    if (!(S.pTimingSteps = lt_malloc(nNumSteps * sizeof(LTDeviceLEDFaderTimingStep)))) {
        S.iShell->PutString(hShell, "Out of memory\n");
        return 2;
    }
    if (   !(S.fader = lt_createobject(LTDeviceLEDFader))
        || !S.fader->API->Initialize(S.fader, argv[1], 0, S.thread, LTTime_Milliseconds(20))) {
        S.iShell->Print(hShell, "Unable to make a LED Fader for \"%s\"\n", argv[1]);
        lt_free(S.pTimingSteps); S.pTimingSteps = NULL;
        if (S.fader) { lt_destroyobject(S.fader); S.fader = NULL; }
        return 3;
    }
    for (LTDeviceLEDFaderTimingStep *pStep = S.pTimingSteps; nNumStepsRemaining;
         --nNumStepsRemaining, ++pStep) ParseStep(argv, pStep);
    S.fader->API->StartFadeSequence(S.fader, S.pTimingSteps, nNumSteps,
                                    EndsWithPeriod(argv[0]) ? StepComplete : NULL,
                                    EndsWithPeriod(argv[0]) ? SequenceComplete : NULL,
                                    NULL);
    return 0;
}

static void FadeHelp(LTShell hShell, int argc, const char *argv[]) { LT_UNUSED(argc), LT_UNUSED(argv);
    S.iShell->PutString(hShell, "usage: fade <device> <ramp time> <color>\n"
                                "           Express color states in 32-bit hex, IRGB.\n"
                                "           Express ramp time in milliseconds.\n"
                                "           A ramp time of zero causes a step transition.\n"
                                "           Examples:\n"
                                "                fade indicator 0 FF00FF00\n"
                                "                (green immediately)\n"
                                "                blink indicator 1000 FFFFFFFF\n"
                                "                (ramp up to full white for 1s)\n"
                                "       fade. <device> <ramp time> <color>\n"
                                "           Enable fade-complete callback.\n");
}

static void FadeComplete(u32 nColor, bool bReached, void *pClientData) { LT_UNUSED(pClientData);
    LTLOG("complete.fade", "%s %08lX", bReached ? "reached" : "interrupted", LT_Pu32(nColor));
}

/* Parse a single transition and color: */
static int Fade(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 4) {
        S.iShell->PutString(hShell, "usage: fade <device> <ramp time> <color>\n");
        return 1;
    }
    if (S.fader) lt_destroyobject(S.fader);
    lt_free(S.pTimingSteps); S.pTimingSteps = NULL;
    if (   !(S.fader = lt_createobject(LTDeviceLEDFader))
        || !S.fader->API->Initialize(S.fader, argv[1], 0, S.thread, LTTime_Milliseconds(20))) {
        S.iShell->Print(hShell, "Unable to make a LED Fader for \"%s\"\n", argv[1]);
        if (S.fader) { lt_destroyobject(S.fader); S.fader = NULL; }
        return 2;
    }
    u32 transition = lt_strtou32(argv[2], NULL, 10);
    u32 color      = lt_strtou32(argv[3], NULL, 16);
    S.fader->API->Fade(S.fader, color, LTTime_Milliseconds(transition),
                       EndsWithPeriod(argv[0]) ? FadeComplete : NULL, NULL);
    return 0;
}

static const LTSystemShell_CommandDesc s_blinkCommands[] = {
    { "blink",  Blink, "Blink an LED",                                         BlinkHelp },
    { "blink.", Blink, "Blink an LED and report step and sequence completion", BlinkHelp },
    { "fade",   Fade,  "Fade an LED",                                          FadeHelp  },
    { "fade.",  Fade,  "Fade an LED and report fade completion",               FadeHelp  }
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTShellBlinkPatternTestImpl_LibFini(void) {
    if (S.pShell) {
        S.pShell->UnregisterCommands(s_blinkCommands);
        lt_closelibrary(S.pShell);
    }
    lt_destroyobject(S.fader);
    lt_destroyobject(S.thread);
    lt_free(S.pTimingSteps);
    lt_closelibrary(S.pLED);
    lt_memset(&S, 0, sizeof(S));
}

static bool LTShellBlinkPatternTestImpl_LibInit(void) {
    if (   !(S.pLED    = lt_openlibrary(LTDeviceLED))
        || !(S.pShell  = lt_openlibrary(LTSystemShell))
        || !(S.iShell  = lt_getlibraryinterface(ILTShell, S.pShell))
        || !(S.thread  = lt_createobject(LTOThread))) {
        LTLOG_YELLOWALERT("init.fail", NULL);
        LTShellBlinkPatternTestImpl_LibFini();
        return false;
    }
    S.thread->API->SetStackSize(S.thread, 1024);
    S.thread->API->Start(S.thread, "dasblinkenlights", NULL, NULL);
    S.pShell->RegisterCommands(s_blinkCommands, sizeof(s_blinkCommands) / sizeof(s_blinkCommands[0]));
    return true;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellBlinkPatternTest, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellBlinkPatternTest) LTLIBRARY_DEFINITION;
