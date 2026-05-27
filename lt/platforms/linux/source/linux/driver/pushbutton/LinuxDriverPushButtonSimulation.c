/*****************************************************************************************
 * platforms/linux/source/linux/driver/pushbutton/LinuxDriverPushButtonSimulation.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Button-press and -release simulation for Unit Test
 *
 ****************************************************************************************/
/** @file LinuxDriverPushButtonSimulation.c Implementation of pushbutton driver for Linux
 */

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include "LinuxDriverPushButtonSimulation.h"

/********************************************************************************************************************************
 * Button press simulation.
 * The test operates in two phases:
 * - The "single-button" test simulates a press then a release of a single button.
 * - The "multiple-button" test simulates the press and release of all buttons, releasing all buttons after all are held.      */

/* Function to call to simulate a pushbutton ISR. */
static PushButtonISR * s_pPushButtonISR = NULL;

/* Current button under test.
 * Single-button test - nSimulatedButton counts up from 0:
 * - the index of the button under test is in bits 1 and up.
 * - bit 0 is 0 for press and 1 for release.
 * Multi-button test - nSimulatedButton counts down from kNumPushButtons * 2 - 1:
 * - the index of the button is the value of nSimulatedButton modulo the number of available buttons.
 * - if the value of nSimulatedButton is greater than or equal to the number of buttons, the event is press, otherwise,
 *   the event is release. */
static u32 nSimulatedButton;

/* The multiple test: press and hold all buttons, then release all buttons in turn: */
static void MultiButtonPressAndRelease(LTThread hThread, u32 nTimerID) {
    LT_UNUSED(hThread);
    bool bPress = nSimulatedButton >= kNumPushButtons;
    u32 nIndex = nSimulatedButton % kNumPushButtons;
    if (!bPress) nIndex = kNumPushButtons - nIndex - 1; /* reverse the order of releases */
    (*s_pPushButtonISR)(nIndex, bPress);
    if (nSimulatedButton-- == 0) {  /* that was the last ISR to trigger */
        s_iThread->KillTimer(nTimerID);
        s_iThread->Terminate(s_iThread->GetCurrentThread());    /* the button test is complete. */
    }
}

/* The single test: press and release each button in turn: */
static void SingleButtonPressAndRelease(LTThread hThread, u32 nTimerID) {
    LT_UNUSED(hThread);
    bool bPress = !(nSimulatedButton % 2);  /* bit 0: 0 = press, 1 = release */
    u32 nIndex = nSimulatedButton >> 1;     /* bits 1 and up: the button index */
    (*s_pPushButtonISR)(nIndex, bPress);
    if (++nSimulatedButton >= kNumPushButtons * 2) {    /* that was the last ISR to trigger */
        nSimulatedButton = kNumPushButtons * 2 - 1;     /* reset for the multi test (above) */
        s_iThread->KillTimer(nTimerID);
        s_iThread->SetTimer(LTTime_Milliseconds(100), MultiButtonPressAndRelease);
    }
}

static void ButtonSimulationThread(LTThread hThread) {
    LT_UNUSED(hThread);
    nSimulatedButton = 0;
    s_iThread->SetTimer(LTTime_Milliseconds(100), SingleButtonPressAndRelease);
}

bool BeginButtonSimulation(PushButtonISR * pPushButtonISR) {
    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    if ((s_pPushButtonISR = pPushButtonISR)) {
        if (!s_hSimulationThread) {
            s_hSimulationThread = LT_GetCore()->CreateThread("LinuxPushButton");
            if (s_hSimulationThread)
                s_iThread->Start(s_hSimulationThread, ButtonSimulationThread, NULL);
            else
                return false;
        }
    }
    return true;
}

bool EndButtonSimulation(void) {
    s_pPushButtonISR = 0;
    if (s_iThread && s_hSimulationThread) {
        s_iThread->Terminate(s_hSimulationThread);
        s_iThread->WaitUntilFinished(s_hSimulationThread, LTTime_Infinite());
    }
    s_hSimulationThread = 0;
}


/******************************************************************************
 *  LOG
 ******************************************************************************
 *  17-Feb-21   constantine created
 */
