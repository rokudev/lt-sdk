/*******************************************************************************
 *
 * LTShellSleep: console control for LTDeviceSleepControl
 * ------------------------------------------------------
 * Bring-up and bench tool.  Sleep is normally armed by the product application
 * (RCUApp calls AllowSleep()); images built for testing have no application, so
 * the sleep path they ship can never be reached from the console.  This library
 * supplies that missing caller and nothing else.
 *
 * It deliberately drives only the public LTDeviceSleepControl interface -- the
 * same calls the application makes -- so what gets exercised is the real sleep
 * path: LTCore idle -> the registered EnterSleepModeProc -> the platform sleep
 * owner.  It does not reach past that interface into any platform backend, so a
 * result observed here is a result about the shipping path, not about a private
 * test route around it.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/sleepcontrol/LTDeviceSleepControl.h>
#include <lt/system/schell/LTSystemSchell.h>

DEFINE_LTLOG_SECTION("ltshell.sleep");

/** Standard LT Interfaces ****************************************************/

static LTCore                *pCore;
static LTDeviceSleepControl  *iSleep;
static LTLibrary             *SelfLib;

/** Shell state ***************************************************************/

static LTOThread *SHL_NotifyThread;
static bool       SHL_Initialized;
static bool       SHL_Armed;
/* Serialises the shell commands with the window timer, which runs on
 * SHL_NotifyThread. */
static LTMutex   *SHL_Lock;

/*******************************************************************************
 * Sleep control callbacks
 ******************************************************************************/

/* Runs on a high-priority LTCore thread just before the sleep decision.  Keep
 * it trivial: anything slow here delays every idle pass. */
static bool SHL_BeforeSleep(void *pClientData) {
    LT_UNUSED(pClientData);
    return true;
}

/* Counted in the after-sleep task, which runs on every ATTEMPT: this is what
 * distinguishes "LTCore is asking and the platform keeps saying no" from
 * "LTCore has stopped asking".  Both look identical from the gate's logs. */
static u32    SHL_Attempts;
static LTTime SHL_LastAttemptTime;

/* Non-zero while a "sleep delay" window is counting down. */
static LTTime SHL_StopAt;

static void SHL_Disarm(void);

static void SHL_AfterSleep(void *pClientData) {
    static bool s_refusalReported;

    LTDeviceSleepControl_WakeReason reason = iSleep->GetWakeReason();
    u64 sleptMs = LTTime_GetMilliseconds(iSleep->GetLastActualSleepDuration());

    LT_UNUSED(pClientData);

    SHL_Attempts++;
    SHL_LastAttemptTime = pCore->GetKernelTime();

    if (reason == kLTDeviceSleepControl_WakeReason_Insomnia) {
        if (!s_refusalReported) {
            s_refusalReported = true;
            LTLOG("refused", "sleep refused; see the platform gate for the reason");
        }
        return;
    }

    s_refusalReported = false;
    LTLOG("woke", "reason=%ld slept=%llums", LT_Ps32((s32)reason), LT_Pu64(sleptMs));
}

static const char *SHL_WakeReasonName(LTDeviceSleepControl_WakeReason reason) {
    switch (reason) {
        case kLTDeviceSleepControl_WakeReason_RTC:      return "RTC";
        case kLTDeviceSleepControl_WakeReason_GPIO:     return "GPIO";
        case kLTDeviceSleepControl_WakeReason_Key:      return "Key";
        case kLTDeviceSleepControl_WakeReason_Insomnia: return "Insomnia (refused to sleep)";
        default:                                        return "Unknown";
    }
}

/* Bench default: no UART-RX wake pad (a sleeping console returns on the RTC deadline);
 * "sleep uartwake 1" re-arms it before the next initialisation. */
static bool s_uartWake = false;

/* One-shot device init. */
static bool SHL_EnsureInitialized(LTSystemSchell *shell) {
    if (SHL_Initialized) {
        return true;
    }

    LTDeviceSleepControl_WakeupSources sources =
        (LTDeviceSleepControl_WakeupSources)(kLTDeviceSleepControl_WakeupSource_RTC |
                                             (s_uartWake ? kLTDeviceSleepControl_WakeupSource_UARTRX : 0) |
                                             kLTDeviceSleepControl_WakeupSource_Key);

    if (!iSleep->Init(sources, SHL_NotifyThread, SHL_BeforeSleep, SHL_AfterSleep, NULL)) {
        shell->API->Print(shell,
                          "sleep: driver Init() refused -- this platform/product has sleep disabled\n");
        return false;
    }
    SHL_Initialized = true;
    return true;
}

/*******************************************************************************
 * Shell commands
 ******************************************************************************/

/*
 * Outstanding sleep disallowance grants.
 *
 * A grant suppresses sleep inside LTCore, before the platform is consulted, so
 * the platform gate stays silent and the symptom is indistinguishable from a
 * gate that is refusing for its own reasons.  LTCore records the address that
 * requested each grant precisely so it can be attributed; resolve it with
 *   riscv64-zephyr-elf-addr2line -fe LTFirmwareImage.elf <address>
 *
 * Typing at this console is itself one of these: LTSystemSchell takes a grant on
 * the first keystroke and holds it briefly after the last, so a grant reported
 * immediately after running this command is expected.
 */
typedef struct {
    LTSystemSchell *shell;
    u32             count;
} SHL_DisallowanceScan;

static bool SHL_DisallowanceEnumProc(u32 grantID, void *pCallerAddress, void *pClientData) {
    SHL_DisallowanceScan *scan = (SHL_DisallowanceScan *)pClientData;

    scan->count++;
    scan->shell->API->Print(scan->shell, "    grant %lu held by %p\n",
                            LT_Pu32(grantID), pCallerAddress);
    return true;  /* keep enumerating */
}

static void SHL_PrintDisallowances(LTSystemSchell *shell) {
    SHL_DisallowanceScan scan = { .shell = shell, .count = 0 };

    shell->API->Print(shell, "  sleep disallowance grants:\n");
    /* The return value reports whether enumeration ran to completion, not
     * whether anything was found, so count in the callback. */
    (void)pCore->EnumerateSleepModeDisallowanceGrants(&SHL_DisallowanceEnumProc, &scan);
    if (scan.count == 0) {
        shell->API->Print(shell, "    none outstanding\n");
    }
}

static void SHL_Sleep_Help(LTSystemSchell *shell, int argc, const char *argv[]) {
    LT_UNUSED(argc), LT_UNUSED(argv);
    shell->API->Print(shell, "usage: sleep <command>\n");
    shell->API->Print(shell, "  on          - allow sleep until \"sleep off\"\n");
    shell->API->Print(shell, "  delay <ms>  - allow sleep, then block it again after <ms>\n");
    shell->API->Print(shell, "  off         - prevent sleep\n");
    shell->API->Print(shell, "  status      - armed state, last wake reason and duration\n");
    shell->API->Print(shell, "  uartwake <0|1> - arm the UART-RX wake pad at the next initialisation (default 0)\n");
    shell->API->Print(shell, "  cap <s>     - initialise the backend and limit a single sleep to <s> seconds (0 = no limit)\n");
    shell->API->Print(shell, "\n");
    shell->API->Print(shell, "How long any single sleep lasts is not set here: LTCore derives it\n");
    shell->API->Print(shell, "from the soonest thread timer in the system, so the device wakes\n");
    shell->API->Print(shell, "when something actually has work to do.\n");
}

/* Fires when the "sleep delay" window is over.  Registered as a WAKEUP timer,
 * so it also bounds the sleep that precedes it. */
static void SHL_WindowTimer(void *pClientData) {
    LT_UNUSED(pClientData);
    /* TryLock: the command holding the lock may be waiting in KillTimer() for
     * this callback, and it sets the window state itself. */
    if (!SHL_Lock->API->TryLock(SHL_Lock)) {
        return;
    }
    /* A window cancelled by "sleep on" or "sleep off" leaves nothing to do. */
    if (SHL_Armed && !LTTime_IsZero(SHL_StopAt)) {
        SHL_Disarm();
        LTLOG("window.done", "sleep window elapsed; sleep blocked again");
    }
    SHL_Lock->API->Unlock(SHL_Lock);
}

/* Shared by "sleep on" and "sleep delay <ms>". */
static bool SHL_Arm(LTSystemSchell *shell) {
    if (!SHL_EnsureInitialized(shell)) {
        return false;
    }
    if (!iSleep->AllowSleep(kLTDeviceSleepControl_SleepMode_Sleep)) {
        shell->API->Print(shell, "sleep: AllowSleep refused\n");
        return false;
    }
    SHL_Armed = true;
    return true;
}

static void SHL_Disarm(void) {
    iSleep->PreventSleep();
    SHL_Armed = false;
    SHL_StopAt = LTTime_Zero();
    /* Harmless if no window was running. */
    if (SHL_NotifyThread) {
        SHL_NotifyThread->API->KillTimer(SHL_NotifyThread, &SHL_WindowTimer, NULL);
    }
}

static int SHL_Sleep(LTSystemSchell *shell, int argc, const char *argv[]) {
    if (argc >= 2 && lt_strcmp(argv[1], "on") == 0) {
        bool armed;

        SHL_Lock->API->Lock(SHL_Lock);
        /* Cancel a window left by "sleep delay" before arming, so it cannot
         * disarm the indefinite sleep. */
        SHL_StopAt = LTTime_Zero();
        SHL_NotifyThread->API->KillTimer(SHL_NotifyThread, &SHL_WindowTimer, NULL);
        armed = SHL_Arm(shell);
        SHL_Lock->API->Unlock(SHL_Lock);
        if (!armed) {
            return -1;
        }
        shell->API->Print(shell, "sleep: armed; the system sleeps whenever it goes idle\n");
        return 0;
    }

    if (argc >= 2 && lt_strcmp(argv[1], "off") == 0) {
        if (!SHL_Initialized) {
            shell->API->Print(shell, "sleep: not armed\n");
            return 0;
        }
        SHL_Lock->API->Lock(SHL_Lock);
        SHL_Disarm();
        SHL_Lock->API->Unlock(SHL_Lock);
        shell->API->Print(shell, "sleep: blocked\n");
        return 0;
    }

    /*
     * The window is a real LTCore WAKEUP timer rather than a deadline this
     * library polls.  That is the whole point: LTCore folds wakeup timers into
     * the softwareSleepWakeupTime it hands the platform, which becomes the
     * LPFW's rtc_timeout_us, so arming one both bounds the sleep and guarantees
     * something is scheduled to end it.  A plain deadline cannot -- with no
     * other timer in the system the device goes down with nothing to wake it.
     */
    if (argc >= 2 && lt_strcmp(argv[1], "delay") == 0) {
        u32 windowMs;

        if (argc < 3) {
            shell->API->Print(shell, "sleep: \"delay\" needs a duration in ms\n");
            return -1;
        }
        windowMs = lt_strtou32(argv[2], NULL, 10);
        if (windowMs == 0) {
            shell->API->Print(shell, "sleep: duration must be non-zero; use \"sleep off\"\n");
            return -1;
        }
        SHL_Lock->API->Lock(SHL_Lock);
        if (!SHL_Arm(shell)) {
            SHL_Lock->API->Unlock(SHL_Lock);
            return -1;
        }
        SHL_StopAt = LTTime_Add(pCore->GetKernelTime(), LTTime_Milliseconds(windowMs));
        SHL_Lock->API->Unlock(SHL_Lock);

        /* Set outside the lock: a short window can expire at once, and its
         * callback skips the work when it finds the lock held. */
        SHL_NotifyThread->API->SetTimer(SHL_NotifyThread,
                                        LTTime_Milliseconds(windowMs),
                                        &SHL_WindowTimer, NULL, NULL);
        SHL_NotifyThread->API->SetAsWakeupTimer(SHL_NotifyThread,
                                                &SHL_WindowTimer, NULL);

        /* An open window with no timer would never end: SetTimer() cannot
         * report a failed allocation, and a window that expired while this
         * thread held the lock was skipped by its callback.  End it here. */
        bool timerMissing;
        bool expired = false;
        SHL_Lock->API->Lock(SHL_Lock);
        timerMissing = SHL_Armed && !LTTime_IsZero(SHL_StopAt) &&
                       LTTime_IsZero(SHL_NotifyThread->API->GetTimerExpirationKernelTime(SHL_NotifyThread,
                                                                                         &SHL_WindowTimer, NULL));
        if (timerMissing) {
            expired = LTTime_IsGreaterThanOrEqual(pCore->GetKernelTime(), SHL_StopAt);
            SHL_Disarm();
        }
        SHL_Lock->API->Unlock(SHL_Lock);
        if (timerMissing && !expired) {
            shell->API->Print(shell, "sleep: could not set the window timer; sleep blocked\n");
            return -1;
        }
        shell->API->Print(shell, "sleep: armed for %lu ms, then blocked\n", LT_Pu32(windowMs));
        return 0;
    }

    if (argc >= 3 && lt_strcmp(argv[1], "uartwake") == 0) {   /* BENCH: arm the UART-RX wake pad (0/1), before init */
        s_uartWake = lt_strtou32(argv[2], NULL, 10) != 0;
        shell->API->Print(shell, "sleep: uartwake=%d (applies at the next initialisation)\n", s_uartWake ? 1 : 0);
        return 0;
    }
    if (argc >= 3 && lt_strcmp(argv[1], "cap") == 0) {   /* BENCH: max single sleep, seconds */
        u32 capS = lt_strtou32(argv[2], NULL, 10);
        if (!SHL_EnsureInitialized(shell)) return -1;
        iSleep->SetSleepDurationLimit(LTTime_Seconds(capS));
        shell->API->Print(shell, "sleep: cap_s=%llu\n", LT_Pu64(LTTime_GetSeconds(iSleep->GetSleepDurationLimit())));
        return 0;
    }
    if (argc >= 2 && lt_strcmp(argv[1], "status") == 0) {
        shell->API->Print(shell, "sleep: init=%s armed=%s\n",
                          SHL_Initialized ? "yes" : "no",
                          SHL_Armed ? "yes" : "no");
        if (SHL_Initialized) {
            shell->API->Print(shell, "  last wake reason: %s\n",
                              SHL_WakeReasonName(iSleep->GetWakeReason()));
            shell->API->Print(shell, "  last sleep:       %llu ms\n",
                              LTTime_GetMilliseconds(iSleep->GetLastActualSleepDuration()));
            if (!LTTime_IsZero(SHL_StopAt)) {
                LTTime now       = pCore->GetKernelTime();
                u64    remaining = 0;

                if (LTTime_IsGreaterThan(SHL_StopAt, now)) {
                    remaining = (u64)LTTime_GetMilliseconds(
                        LTTime_Subtract(SHL_StopAt, now));
                }
                shell->API->Print(shell, "  window:           %llu ms remaining\n",
                                  remaining);
            }
            /* Run "sleep status" twice a known number of seconds apart and
             * compare this: LTCore's kernel time is LTK tick time on platforms
             * whose BSP supplies no high-frequency counter, so if it advances
             * far slower than the wall clock the tick interrupt has stopped --
             * which also silently stops sleep, because the idle countdown that
             * triggers it is driven from that same tick. */
            shell->API->Print(shell, "  kernel time:      %llu ms\n",
                              LTTime_GetMilliseconds(pCore->GetKernelTime()));
            if (SHL_Attempts == 0) {
                shell->API->Print(shell, "  attempts:         0 (LTCore has never asked)\n");
            } else {
                shell->API->Print(shell, "  attempts:         %lu, last %llu ms ago\n",
                                  LT_Pu32(SHL_Attempts),
                                  LTTime_GetMilliseconds(
                                      LTTime_Subtract(pCore->GetKernelTime(),
                                                      SHL_LastAttemptTime)));
            }
        }
        SHL_PrintDisallowances(shell);
        return 0;
    }

    SHL_Sleep_Help(shell, argc, argv);
    return 0;
}

static const LTSystemShell_CommandDesc SHL_Commands[] = {
    { "sleep", SHL_Sleep, "sleep control, type \"sleep\" for a list", SHL_Sleep_Help },
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void SHL_Quit(void) {
    LTSystemSchell *shell = LTSystemSchellConsole_GetConsoleShell();
    if (shell) {
        LTSystemShell_CommandTable table = {
            .commands    = SHL_Commands,
            .numCommands = sizeof(SHL_Commands) / sizeof(SHL_Commands[0])
        };
        shell->API->UnregisterCommands(shell, &table);
    }
    if (SHL_NotifyThread) {
        SHL_NotifyThread->API->Terminate(SHL_NotifyThread);
        SHL_NotifyThread = NULL;
    }
    lt_destroyobject(SHL_Lock);
    SHL_Lock = NULL;
}

static bool SHL_Init(void) {
    LTSystemSchell *shell = LTSystemSchellConsole_GetConsoleShell();
    if (!shell) return false;

    SHL_Lock = lt_createobject(LTMutex);
    if (!SHL_Lock) return false;

    /* The sleep driver runs the after-sleep task on this thread, so it must
     * exist before Init() and outlive every sleep cycle. */
    SHL_NotifyThread = lt_createobject(LTOThread);
    if (!SHL_NotifyThread) return false;
    SHL_NotifyThread->API->SetStackSize(SHL_NotifyThread, 1024);
    SHL_NotifyThread->API->Start(SHL_NotifyThread, "sleep.notify", NULL, NULL);

    LTSystemShell_CommandTable table = {
        .commands    = SHL_Commands,
        .numCommands = sizeof(SHL_Commands) / sizeof(SHL_Commands[0])
    };
    shell->API->RegisterCommands(shell, &table);
    return true;
}

static void LTShellSleepImpl_LibFini(void) {
    SHL_Quit();
    lt_closelibrary(iSleep); // null ok
}

static bool LTShellSleepImpl_LibInit(void) {
    pCore   = LT_GetCore();
    iSleep  = lt_openlibrary(LTDeviceSleepControl);
    if (iSleep && SHL_Init()) return true;
    LTShellSleepImpl_LibFini();
    return false;
}

static int LTShellSleep_Run(int argc, const char **argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    /* Open a second time so the library stays resident after main exits. */
    if (!SelfLib) SelfLib = LT_GetCore()->OpenLibrary("LTShellSleep");
    return 0;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellSleep, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellSleep, LTShellSleep_Run) LTLIBRARY_DEFINITION;
