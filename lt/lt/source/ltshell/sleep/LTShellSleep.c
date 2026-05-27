/*******************************************************************************
 *
 * LTShellSleep: Sleep test Shell
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
#include <lt/system/shell/LTSystemShell.h>
#include <lt/device/pins/LTDevicePins.h>
#include <lt/device/rtc/LTDeviceRTC.h>
#include <lt/device/wifi/LTDeviceWiFi.h>
#include <lt/device/power/LTDevicePower.h>
#include <lt/device/powersubswitch/LTDevicePowerSubswitch.h>
#include <../../platforms/si91x/include/siwg917m111/si91x_DeepSleep.h>
#include <../../platforms/si91x/include/siwg917m111/si91x_RS1.h>
#include <../../platforms/si91x/include/siwg917m111/si91x_IPC.h>
#include <lt/system/timezone/LTSystemTimeZone.h>


 DEFINE_LTLOG_SECTION("ltshell.sleep");

/** Standard LT Interfaces ****************************************************/
static LTDevicePins  *s_pDevPins    = NULL;
static LTDeviceWiFi  *s_pDevWiFi    = NULL;
static LTDeviceRTC   *s_DevRTC      = NULL;
static LTDevicePower *s_DevicePower = NULL;
static LTDevicePowerSubswitch *s_pMediaPowerSubswitch = NULL;
static ILTShell * SHL_iShell;
static u32 s_disallowanceGrants = 0;
static ILTThread *iThread = NULL;
static LTCore *s_pCore = NULL;
static LTNetCore *s_netCore = NULL;
static LTThread s_netthread = LTHANDLE_INVALID;
static LTTransport s_htransport = LTHANDLE_INVALID;

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/
static LTSystemShell *SHL_Library;

/** Shell Variables ***********************************************************/

/** Forward references ********************************************************/

static int SHL_Sleep(LTShell hShell, int argc, const char *argv[]);

/** Utility Functions *********************************************************/

/** Callback Functions ********************************************************/

/** Command Functions *********************************************************/

static inline void RSI_PS_SetRamRetention(u32 ramRetention) {
    MCU_FSM->MCU_FSM_SLEEP_CTRLS_AND_WAKEUP_MODE |= ramRetention;
}

static LTTime MySleepActionEventProc(LTCore_SleepAction action, LTTime wakeupTimeOrSleepDuration, void *pClientData) {
    LTTime time = LTTime_Zero();
    LTShell hShell = VOIDPTR_TO_LTHANDLE(pClientData);
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    char timeBuff[24];
    switch (action) {
        case kLTCore_SleepAction_GoingToSleep:
            time = LTTime_Milliseconds(10000);
            LT_GetCore()->FormatCanonicalTimeString(time, timeBuff, sizeof(timeBuff), false);
            iShell->Print(hShell, "Going to sleep for %ss\n", timeBuff);
            break;
        case kLTCore_SleepAction_SleepAborted:
            s_DevicePower->API->DisableSleepMode();
            LT_GetCore()->NoSleepAction(&MySleepActionEventProc);
            iShell->Print(hShell, "Sleep aborted\n");
            break;
        case kLTCore_SleepAction_AwakenedFromSleep:
            s_DevicePower->API->DisableSleepMode();
            LT_GetCore()->NoSleepAction(&MySleepActionEventProc);
            LT_GetCore()->FormatCanonicalTimeString(wakeupTimeOrSleepDuration, timeBuff, sizeof(timeBuff), false);
            iShell->Print(hShell, "Slept for %ss, not sleeping anymore.\n", timeBuff);
            break;
        default:
            break;
    }
    LT_GetCore()->FlushConsoleOutput();
    return time;
}

static void trigger_sleep_now(ILTShell *iShell, LTShell hShell) {
    iShell->Print(hShell, "Triggering sleep now\n");

    iShell->Print(hShell, "RSI_PS_EnterDeepSleep = 0x%08lX\n", LT_Pu32((u32)RSI_PS_EnterDeepSleep));
    iShell->Print(hShell, "Enabling Sleep Mode\n");

    /* set a sleep action event proc and enable sleep mode using a minimum sleep duration that is half what we well return as our requirement from MySleepActionEventProc */
    LT_GetCore()->OnSleepAction(&MySleepActionEventProc, LTHANDLE_TO_VOIDPTR(hShell));
    s_DevicePower->API->EnableSleepMode(LTTime_Milliseconds(1), LTTime_Milliseconds(5000));
}

static int SHL_Setpowermode(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        SHL_iShell->Print(hShell, "Usage: tamode <power_mode>\n");
        return -1;
    }

    if (!s_pDevWiFi) {
        SHL_iShell->Print(hShell, "Initializing NWP\n");
        s_pDevWiFi = lt_openlibrary(LTDeviceWiFi);
    }

    u32 power_mode = lt_strtou32(argv[1], NULL, 10);

    RSI_PS_SetRamRetention(TA_RAM_RETENTION_MODE_EN);
    s_pDevWiFi->SetOption("performance_profile", power_mode);
    P2P_STATUS_REG &= ~M4_WAKEUP_TA;

    return 0;
}

static int SHL_CheckTAIsWake(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    SHL_iShell->Print(hShell, "P2P Status Reg: 0x%02X\n", (u8)(P2P_STATUS_REG));

    if (!s_pDevWiFi) {
        SHL_iShell->Print(hShell, "Initializing NWP\n");
        s_pDevWiFi = lt_openlibrary(LTDeviceWiFi);
    }

    if (P2P_STATUS_REG & TA_IS_ACTIVE) {
        SHL_iShell->Print(hShell, "TA is awake\n");
    } else {
        P2P_STATUS_REG &= ~M4_WAKEUP_TA;
        P2P_STATUS_REG;
        P2P_STATUS_REG |= M4_WAKEUP_TA;
        P2P_STATUS_REG;
        u32 retry = 30;
        while (retry--) {
            if (P2P_STATUS_REG & TA_IS_ACTIVE) {
                SHL_iShell->Print(hShell, "TA is wake\n");
                break;
            } else {
                SHL_iShell->Print(hShell, "TA is not wake, retrying\n");
            }
            lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Milliseconds(100));
        }

        if (P2P_STATUS_REG & TA_IS_ACTIVE) {
            SHL_iShell->Print(hShell, "TA is wake\n");
            P2P_STATUS_REG &= ~M4_WAKEUP_TA;
            P2P_STATUS_REG;
            retry = 30;
            while (retry--) {
                if (!(P2P_STATUS_REG & TA_IS_ACTIVE)) {
                    SHL_iShell->Print(hShell, "TA is not wake\n");
                    break;
                } else {
                    SHL_iShell->Print(hShell, "TA is wake, retrying\n");
                }
                lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Milliseconds(100));
            }
        } else {
            SHL_iShell->Print(hShell, "TA is not wake!!!\n");
        }
    }
    return 0;
}

static int SHL_Sleep(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argv);
    LT_UNUSED(argc);
    SHL_iShell->Print(hShell, "SI917 Sleep\n");

    if (!s_pDevWiFi) {
        SHL_iShell->Print(hShell, "Initializing NWP\n");
        s_pDevWiFi = lt_openlibrary(LTDeviceWiFi);
    }

    SHL_iShell->Print(hShell, "Trigger M4 to sleep\n");
    LTTime sleepStart = s_DevRTC->API->GetTimeUTC(s_DevRTC);
    trigger_sleep_now(SHL_iShell, hShell);
    LTTime sleepDuration = s_DevRTC->API->GetTimeUTC(s_DevRTC);
    LTTime_SubtractFrom(sleepDuration, sleepStart);
    SHL_iShell->Print(hShell, "Sleep duration: %ld ms\n", LT_Pu32(LTTime_GetMilliseconds(sleepDuration)));

    return 0;
}
static bool write_once = false;
static void OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    LTSystemTimeZone *s_pLTSystemTimeZone = lt_openlibrary(LTSystemTimeZone);
    char buff[100];
    LTShell hShell = VOIDPTR_TO_LTHANDLE(clientData);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, hSocket);
    if (event == kLTSocket_Event_Connected) {
        SHL_iShell->Print(hShell, "Socket connected hSocket = 0x%lx\n", LT_Pu32(hSocket));
    } else if (event == kLTSocket_Event_WriteReady) {
        if (!write_once) {
            SHL_iShell->Print(hShell, "Socket write ready hSocket = 0x%lx\n", LT_Pu32(hSocket));
            s_pLTSystemTimeZone->ClockTimeToHumanReadableString(s_pCore->GetClockTimeUTC(), false, "UTC", buff, sizeof(buff));
            pSocket->WriteSocket(hSocket, buff, lt_strlen(buff)+1);
            lt_reallowsleepmode(s_disallowanceGrants);
            write_once = true;
        }
        
    } else if (event == kLTSocket_Event_ReadReady) {
        u8 read_buffer[100];
        lt_memset(read_buffer, 0x00, sizeof(read_buffer));
        SHL_iShell->Print(hShell, "Socket read ready hSocket = 0x%lx\n", LT_Pu32(hSocket));
        s32 read_size = pSocket->ReadSocket(hSocket, read_buffer, sizeof(read_buffer));
        LT_UNUSED(read_size);
        s_pLTSystemTimeZone->ClockTimeToHumanReadableString(s_pCore->GetClockTimeUTC(), false, "UTC", buff, sizeof(buff));
        SHL_iShell->Print(hShell, "Message received at %s: %s\n", buff, read_buffer);
        lt_memset(buff, 0x00, sizeof(buff));
        s_pLTSystemTimeZone->ClockTimeToHumanReadableString(s_pCore->GetClockTimeUTC(), false, "UTC", buff, sizeof(buff));
        pSocket->WriteSocket(hSocket, buff, lt_strlen(buff)+1);
    } else if (event == kLTSocket_Event_Disconnected) {
        SHL_iShell->Print(hShell, "Socket disconnected hSocket = 0x%lx\n", LT_Pu32(hSocket));
        lt_destroyhandle(s_netthread);
    }
    lt_closelibrary(s_pLTSystemTimeZone);
}

static void OnSocketEvent_2(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    LTShell hShell = VOIDPTR_TO_LTHANDLE(clientData);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, hSocket);
    if (event == kLTSocket_Event_Connected) {
        SHL_iShell->Print(hShell, "Socket connected hSocket = 0x%lx\n", LT_Pu32(hSocket));
    } else if (event == kLTSocket_Event_WriteReady) {
        SHL_iShell->Print(hShell, "Socket write ready hSocket = 0x%lx write_once %d\n", LT_Pu32(hSocket), write_once);
        if (!write_once) {
            pSocket->WriteSocket(hSocket, "hi!", 3);
            write_once = true;
        }
    } else if (event == kLTSocket_Event_ReadReady) {
        u8 read_buffer[100];
        lt_memset(read_buffer, 0x00, sizeof(read_buffer));
        SHL_iShell->Print(hShell, "Socket read ready hSocket = 0x%lx\n", LT_Pu32(hSocket));
        s32 read_size = pSocket->ReadSocket(hSocket, read_buffer, sizeof(read_buffer));
        LT_UNUSED(read_size);
        SHL_iShell->Print(hShell, "Message received: %s\n", read_buffer);
        if (lt_strncmp((const char*)read_buffer, "close", 5) == 0) {
            pSocket->DisconnectSocket(hSocket);
            lt_destroyhandle(hSocket);
            SHL_iShell->Print(hShell, "Socket closed\n");
            write_once = false;
        }
    } else if (event == kLTSocket_Event_Disconnected) {
        SHL_iShell->Print(hShell, "Socket disconnected hSocket = 0x%lx\n", LT_Pu32(hSocket));
        lt_destroyhandle(s_netthread);
    }
}

static void OnSocketEvent_tid2(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    LTShell hShell = VOIDPTR_TO_LTHANDLE(clientData);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, hSocket);
    if (event == kLTSocket_Event_Connected) {
        SHL_iShell->Print(hShell, "Socket connected hSocket = 0x%lx\n", LT_Pu32(hSocket));
    } else if (event == kLTSocket_Event_WriteReady) {
        SHL_iShell->Print(hShell, "Socket write ready hSocket = 0x%lx\n", LT_Pu32(hSocket));
    } else if (event == kLTSocket_Event_ReadReady) {
        u8 read_buffer[100];
        lt_memset(read_buffer, 0x00, sizeof(read_buffer));
        SHL_iShell->Print(hShell, "Socket read ready hSocket = 0x%lx\n", LT_Pu32(hSocket));
        s32 read_size = pSocket->ReadSocket(hSocket, read_buffer, sizeof(read_buffer));
        LT_UNUSED(read_size);
        SHL_iShell->Print(hShell, "Message received: %s\n", read_buffer);
        if (lt_strstr((const char *)read_buffer, "port:")) {
            //Start a thread to open the socket on this port
            u32 port = lt_strtou32((const char *)read_buffer + 5, NULL, 10);
            SHL_iShell->Print(hShell, "Starting socket on port %lu\n", port);
            char specstr[100];
            lt_snprintf(specstr, sizeof(specstr), "tcp host: test.com port: %lu", port);
            SHL_iShell->Print(hShell, "Socket spec: %s\n", specstr);
            LTSocket sockFd = s_netCore->OpenSocket(s_htransport, specstr, OnSocketEvent_2, LTHANDLE_TO_VOIDPTR(hShell));
            if (sockFd == LTHANDLE_INVALID) {
                LTLOG_YELLOWALERT("net", "Failed to open socket");
            } else {
                SHL_iShell->Print(hShell, "Socket opened on port %lu\n", port);
            }
        }
    } else if (event == kLTSocket_Event_Disconnected) {
        SHL_iShell->Print(hShell, "Socket disconnected hSocket = 0x%lx\n", LT_Pu32(hSocket));
        lt_destroyhandle(s_netthread);
    }
}

static bool StartSocket(void) {
    LTShell hShell = VOIDPTR_TO_LTHANDLE(iThread->GetThreadSpecificClientData(s_netthread, "hshell"));
    s_disallowanceGrants = lt_disallowsleepmode();
    SHL_iShell->Print(hShell, "Disallowance grant: %lu\n", LT_Pu32(s_disallowanceGrants));
    LTSocket sockFd = s_netCore->OpenSocket(s_htransport, "tcp host: test.com port: 8024", OnSocketEvent, LTHANDLE_TO_VOIDPTR(hShell));
    if (sockFd == LTHANDLE_INVALID) {
        LTLOG_YELLOWALERT("net", "Failed to open socket");
        return false;
    }
    return true;
}

static bool StartSocket_tid2(void) {
    LTShell hShell = VOIDPTR_TO_LTHANDLE(iThread->GetThreadSpecificClientData(s_netthread, "hshell"));
    //s_disallowanceGrants = lt_disallowsleepmode();
    //SHL_iShell->Print(hShell, "Disallowance grant: %lu\n", LT_Pu32(s_disallowanceGrants));
    LTSocket sockFd = s_netCore->OpenSocket(s_htransport, "tcp host: test.com port: 8025", OnSocketEvent_tid2, LTHANDLE_TO_VOIDPTR(hShell));
    if (sockFd == LTHANDLE_INVALID) {
        LTLOG_YELLOWALERT("net", "Failed to open socket");
        return false;
    }
    return true;
}

static void StopSocket(void) {
    if (s_htransport) {
        lt_destroyhandle(s_htransport);
    }
}

static int SHL_SleepSocketTest(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (argc < 2) {
        SHL_iShell->Print(hShell, "Usage: sleepsockettest <test_Case_id>\n");
        return -1;
    }
    u32 test_id = lt_strtou32(argv[1], NULL, 10);
    SHL_iShell->Print(hShell, "Sleep socket test id: %lu\n", test_id);
    if (test_id == 0) {
        // Test case 0:
        // Take a disallowance grant
        // Create a socket.
        // Wait for the socket to be ready.
        // Release the disallowance grant.
        // We should wakeup when a packet arrives
        //Start the transport thread
        s_netthread = s_pCore->CreateThread("netthread");
        if (!s_netthread) {
            SHL_iShell->Print(hShell, "Failed to create net thread\n");
            return -1;
        }
        s_htransport = s_netCore->OpenTransport(NULL, NULL, NULL);
        if (!s_htransport) {
            LTLOG_YELLOWALERT("net", "Failed to open transport");
            return false;
        }
        iThread->SetStackSize(s_netthread, 2048);
        iThread->SetThreadSpecificClientData(s_netthread, "hshell", NULL, LTHANDLE_TO_VOIDPTR(hShell));
        iThread->Start(s_netthread, StartSocket, StopSocket);
        
        s_disallowanceGrants = 0;
    } else if (test_id == 1) {
        //s_disallowanceGrants = lt_disallowsleepmode();
        s_netthread = s_pCore->CreateThread("netthread");
        if (!s_netthread) {
            SHL_iShell->Print(hShell, "Failed to create net thread\n");
            return -1;
        }
        s_htransport = s_netCore->OpenTransport(NULL, NULL, NULL);
        if (!s_htransport) {
            LTLOG_YELLOWALERT("net", "Failed to open transport");
            return false;
        }
        iThread->SetStackSize(s_netthread, 2048);
        iThread->SetThreadSpecificClientData(s_netthread, "hshell", NULL, LTHANDLE_TO_VOIDPTR(hShell));
        iThread->Start(s_netthread, StartSocket_tid2, StopSocket);
    }
    return 0;
}

static int SHL_MediaPower(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        SHL_iShell->Print(hShell, "Usage: mediapower <on(1)/off(0)>\n");
        return -1;
    }

    u32 mediaPower = lt_strtou32(argv[1], NULL, 10);
    SHL_iShell->Print(hShell, "Media Power %s\n", mediaPower ? "on":"off");

    if (!s_pMediaPowerSubswitch) {
        SHL_iShell->Print(hShell, "LTDevicePowerSubswitch not initialized\n");
        return -1;
    }
    mediaPower ? s_pMediaPowerSubswitch->API->SwitchOn(s_pMediaPowerSubswitch):
                  s_pMediaPowerSubswitch->API->SwitchOff(s_pMediaPowerSubswitch);
    return 0;
}

static const LTSystemShell_CommandDesc SHL_Commands[] = {
    { "tapowermode",        SHL_Setpowermode,    "Set TA power mode 0(HIGH_PERFORMANCE), 1(ASSOCIATED_POWER_SAVE), 2(ASSOCIATED_POWER_SAVE_LOW_LATENCY), 3(STANDBY_POWER_SAVE), 4(STANDBY_POWER_SAVE_WITH_RAM_RETENTION)",  NULL },
    { "checkta",            SHL_CheckTAIsWake,   "Check if TA died or not",  NULL },
    { "sleeptest",          SHL_Sleep,           "Trigger sleep 0(Sleep), 1(Awake)",  NULL },
    { "mediapower",         SHL_MediaPower,      "Set media power, 0(OFF), 1(ON)",  NULL },
    { "sleepsockettest",    SHL_SleepSocketTest, "Trigger sleep socket test",  NULL },
    { }
};

static void SHL_Quit(void) {

    lt_destroyobject(s_DevRTC);
    lt_destroyobject(s_DevicePower);
    lt_destroyobject(s_pMediaPowerSubswitch);
    s_DevRTC = NULL;
    if (SHL_Library) {
        SHL_Library->UnregisterCommands(SHL_Commands);
        lt_closelibrary(SHL_Library);
    }
}

static bool SHL_Init(void) {
    s_pDevPins = lt_openlibrary(LTDevicePins);
    if (!s_pDevPins) {
        LTLOG_YELLOWALERT("init.pins", "Failed to open LTDevicePins");
        return false;
    }

    s_DevRTC = lt_createobject(LTDeviceRTC);
    if (!s_DevRTC) {
        LTLOG_YELLOWALERT("init.RTC", "Failed to create LTDeviceRTC object");
        return false;
    }

    s_DevicePower = lt_createobject(LTDevicePower);
    if (!s_DevicePower) {
        LTLOG_YELLOWALERT("init.power", "Failed to create LTDevicePower object");
        return false;
    }

    s_pMediaPowerSubswitch = lt_createobject(LTDevicePowerSubswitch);
    if (!s_pMediaPowerSubswitch) {
        LTLOG_YELLOWALERT("init.powersubswitch", "Failed to create LTDevicePowerSubswitch object");
        return false;
    }
    s_pMediaPowerSubswitch->API->SetUnitName(s_pMediaPowerSubswitch, "media.soc");

    SHL_Library = lt_openlibrary(LTSystemShell);
    if (!SHL_Library) return false;

    SHL_iShell = lt_getlibraryinterface(ILTShell, SHL_Library);
    SHL_Library->RegisterCommands(SHL_Commands, sizeof(SHL_Commands) / sizeof(SHL_Commands[0]));

    RSI_PS_SetRamRetention(TA_RAM_RETENTION_MODE_EN);

    iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    if (!iThread) {
        LTLOG_YELLOWALERT("init.thread", "Failed to get ILTThread interface");
        return false;
    }
    s_pCore = LT_GetCore();
    if (!s_pCore) {
        LTLOG_YELLOWALERT("init.core", "Failed to get LTCore interface");
        return false;
    }
    s_netCore = lt_openlibrary(LTNetCore);
    if (!s_netCore) {
        LTLOG_YELLOWALERT("init.netcore", "Failed to open LTNetCore");
        return false;
    }
    return true;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellSleepImpl_LibFini(void) {
    SHL_Quit();
}

static bool LTShellSleepImpl_LibInit(void) {
    if (SHL_Init()) return true;
    LTShellSleepImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellSleep, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellSleep) LTLIBRARY_DEFINITION;
