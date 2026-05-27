/*******************************************************************************
 * platforms/linux/source/linux/driver/pushbutton/LinuxDriverPushButtonImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Linux LT Driver Library for pushbutton access
 *
 * This Driver LT Library runs a thread that simulates the press and release
 * of four buttons individually and in a combination.  It exercise (and passes)
 * the LTDevicePushButton Unit Test.
 *
 * It is also intended as a loose model (without the simulation) for
 * LTDriverPushButton implementations on other platforms.
 *
 ******************************************************************************/
/** @file LinuxDriverPushButtonImpl.c Implementation of pushbutton driver for Linux
 */

#include <lt/core/LTCore.h>
#include <lt/device/pushbutton/LTDevicePushButton.h>
#include <lt/system/shell/LTSystemShell.h>

/*******************************************************************************
 * Library and interface pointers.                                            */

static LTSystemShell    * s_pSystemShell = NULL;
static ILTShell         * s_pIShell      = NULL;
static ILTThread        * s_pIThread     = NULL;

/*******************************************************************************
 * Connection to notification thread.                                         */

static LTThread_TaskProc * s_pButtonPressEventProc = NULL;
static LTThread_TaskProc * s_pButtonReleaseEventProc = NULL;
static LTThread            s_hNotificationThread;
static u32                 s_nDeviceUnitIndexBase;

/*******************************************************************************
 * Push Button descriptions.                                                  */

typedef struct PushButtonDescription {
    char const * name;
} PushButtonDescription;

static PushButtonDescription s_PushButtons[] = {
    { "SYSTEM RESET" },
    { "OPTION"       },
    { "SELECT"       },
    { "START"        }
};

enum { kNumPushButtons = sizeof s_PushButtons / sizeof *s_PushButtons };

/*******************************************************************************
 * Button press simulation.
 * Allow press, release, or press-release.
 * Note: No attempt is made to keep and check the current state of the button.
 *       multiple, out-of-order press and release events are permitted by
 *       this very simplistic code, so behave yourself.                       */

static int Button(LTShell hShell, int argc, const char ** argv) {
    if (argc < 2) {
        s_pIShell->Print(hShell, "usage: button i [p]\n"
                                 "       i: index of button\n"
                                 "       p: 1: pressed\n"
                                 "          0: released\n"
                                 "          nothing: press-release\n");
        return 1;
    }
    if (!s_hNotificationThread) {
        s_pIShell->Print(hShell, "warning: no thread has registered for notifications\n");
        return 2;
    }
    u32 nButtonIndex = lt_strtou32(argv[1], NULL, 0);
    if (nButtonIndex >= kNumPushButtons) {
        s_pIShell->Print(hShell, "error: invalid button index %lu\n", LT_Pu32(nButtonIndex));
        return 3;
    }
    u8 * pClientData = 0;
    pClientData += nButtonIndex + s_nDeviceUnitIndexBase;
    if (argc < 3) {
        s_pIThread->QueueTaskProc(s_hNotificationThread, s_pButtonPressEventProc,   NULL, pClientData);
        s_pIThread->QueueTaskProc(s_hNotificationThread, s_pButtonReleaseEventProc, NULL, pClientData);
    } else {
        LTThread_TaskProc *pTaskProc = *argv[2] == '1' ? s_pButtonPressEventProc
                                                       : s_pButtonReleaseEventProc;
        if (pTaskProc)
            s_pIThread->QueueTaskProc(s_hNotificationThread, pTaskProc, NULL, (void *)pClientData);
    }
    return 0;
}

/*******************************************************************************
 * Shell command to simulate button press.                                    */

static const LTSystemShell_CommandDesc s_PushButtonCommands[] = {
    { "button",   Button,   "simulate a button press",      NULL, NULL },
    { NULL,       NULL,     NULL,                           NULL, NULL }
};

/********************************************************************************************************************************
 * Access to Device Unit numbers and names.                                                                                    */

static bool LinuxDriverPushButton_GetPushButtonNameFromIndex(u32 nIndex, char * pPushButtonNameToSet, LT_SIZE nStringSizeBytes) {
    if (nIndex < kNumPushButtons) {
        lt_strncpyTerm(pPushButtonNameToSet, s_PushButtons[nIndex].name, nStringSizeBytes);
        return true;
    }
    return false;
}

static bool LinuxDriverPushButton_GetPushButtonIndexFromName(char const * pPushButtonName, u32 * pIndexToSet) {
    PushButtonDescription * pButton = s_PushButtons;
    for (u32 n = 0; n < kNumPushButtons; ++n, ++pButton)
        if (!lt_strcmp(pPushButtonName, pButton->name)) {
            *pIndexToSet = n;
            return true;
        }
    return false;
}

/********************************************************************************************************************************
 * Connection to the Device layer.
 * As the Driver will likely collect button press and release events through an interrupt service routine, its notification
 * of the Device layer must be lightweight; connection with the Device (through the (*Connect)() method) specifies the thread
 * and press/release procs resident in the driver that receive these light-weight notifications through QueueTaskProc().  That
 * thread, running outside the interrupt context, is free to notify any and all threads which are currently subscribed to
 * button-press and -release events:                                                                                           */

static void LinuxDriverPushButton_Connect(LTThread hThread, LTThread_TaskProc *pPressDispatchProc,
                                                            LTThread_TaskProc *pReleaseDispatchProc,
                                                            u32 nDeviceUnitIndexBase) {
    s_hNotificationThread = hThread;
    s_pButtonPressEventProc = pPressDispatchProc;
    s_pButtonReleaseEventProc = pReleaseDispatchProc;
    s_nDeviceUnitIndexBase = nDeviceUnitIndexBase;
}

static void LinuxDriverPushButton_Disconnect(void) {
    s_hNotificationThread = 0;
    s_pButtonPressEventProc = s_pButtonReleaseEventProc = NULL;
}

/********************************************************************************************************************************
 * Library initialization and deinitialization.                                                                                */

static void LinuxDriverPushButtonImpl_LibFini(void);

static bool Shutdown(void) { LinuxDriverPushButtonImpl_LibFini(); return false; }


static bool LinuxDriverPushButtonImpl_LibInit(void) {
    if (!(s_pSystemShell = (LTSystemShell *)LT_GetCore()->OpenLibrary("LTSystemShell"))) return Shutdown();
    if (!(s_pIShell = lt_getlibraryinterface(ILTShell, s_pSystemShell)))                 return Shutdown();
    if (!(s_pIThread = lt_getlibraryinterface(ILTThread, LT_GetCore())))                 return Shutdown();
    s_pSystemShell->RegisterCommands(s_PushButtonCommands, sizeof s_PushButtonCommands / sizeof *s_PushButtonCommands);
    return true;
}

static void LinuxDriverPushButtonImpl_LibFini(void) {
    if (s_pSystemShell) {
        s_pSystemShell->UnregisterCommands(s_PushButtonCommands);
        LT_GetCore()->CloseLibrary((LTLibrary *)s_pSystemShell);
        s_pIShell      = NULL;
        s_pSystemShell = NULL;
    }
    if (s_pIThread)
        s_pIThread     = NULL;
}

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDevicePushButton, LinuxDriverPushButton);

/********************************************************************************************************************************
 * Device-unit creation interface.
 * Currently, LTDevicePushButton does not use the Device Unit interface, but once Event notifications are able to occur in an
 * interrupt context, that may change.                                                                                         */
static u32 LinuxDriverPushButtonImpl_GetNumDeviceUnits(void) { return kNumPushButtons; }

LTDeviceUnit LinuxDriverPushButtonImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) { LT_UNUSED(nDeviceUnitNumber); return 0; }

define_LTLIBRARY_INTERFACE(ILTDriverPushButton) {
    .GetPushButtonNameFromIndex = LinuxDriverPushButton_GetPushButtonNameFromIndex,
    .GetPushButtonIndexFromName = LinuxDriverPushButton_GetPushButtonIndexFromName,
    .Connect                    = LinuxDriverPushButton_Connect,
    .Disconnect                 = LinuxDriverPushButton_Disconnect
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LinuxDriverPushButton, (ILTDriverPushButton))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  09-Feb-21   constantine created
 *  13-Oct-21   constantine change from a thread-and-delay model to one using LTShell
 */
