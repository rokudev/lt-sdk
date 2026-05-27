/*******************************************************************************
 *
 * LTDeviceWiFi: WiFi Device
 * -------------------------
 *
 * Provides the primary STA-AP functions for link-layer networking. All
 * platform-dependent features and functions are hidden below this layer and
 * shall not be called or otherwise referenced directly from higher levels.
 * Application code must pass through the this layer to properly sequence
 * states, minimize locking, and avoid conflicts within the driver.
 *
 * See LTDeviceWiFi.h for interface documentation.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTArray.h>
#include <lt/device/wifi/LTDeviceWiFi.h>
#include <lt/device/wifi/LTDriverWiFi.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/product/config/LTProductConfig.h>
#include <lt/net/backoff/LTNetBackoff.h>

#define USE_LOGGER_RECORDS  0

#if USE_LOGGER_RECORDS
    #include <lt/system/logger/LTSystemLogger_Records.h>
#endif

DEFINE_LTLOG_SECTION("lt.dev.wifi");

// Temporary debug helpers:
//#define PR LT_GetCore()->ConsolePrint //(s); for(int n=0; n<1000000; n++);
//#define PV(var) LT_GetCore()->ConsolePrint("======= Variable %s: %d\n", #var, var)
//#define TR LT_GetCore()->ConsolePrint("----: %s\n", __FUNCTION__)
#define TR

#define LTLOG_LEVEL(level, pTag, pFormat, ...)                          \
    if (level & LTAtomic_Load(&WSM_DebugMode)) LTLOG(pTag, pFormat, ##__VA_ARGS__)
#define LTLOG_LEVEL_DEBUG(level, pTag, pFormat, ...) \
    if (level & LTAtomic_Load(&WSM_DebugMode)) LTLOG_DEBUG(pTag, pFormat, ##__VA_ARGS__)
enum {
    // Bit 0-2: Common for all modules
    kLogDebug   = 0x01,
    kLogVerbose = 0x02,
    kLogEvent   = 0x04,
    // Bit 3+: Local for current module
    kLogState   = 0x08,  // Log WiFi StateMachine changes
    kLogRX      = 0x10,  // Log WiFi Tx
    kLogTX      = 0x20,  // Log WiFi Rx
    kLogMGMT    = 0x40,  // Log WiFi Mgmt frames
    kLogBLE     = 0x80,  // Log Bluetooth
    kLogWPA     = 0x100, // Log wpa_supplicant (LT on linux)
};

enum LTDeviceWiFiErrorCode {
    LTDeviceWiFi_OK             = 0,
    LTDeviceWIFI_DRIVER_DOWN    = -1,
    LTDeviceWIFI_NOT_SUPPORT    = -2,
};

enum {
    kLTDeviceWiFiIdleState_MinDelay         =   100,     // 100s
    kLTDeviceWiFiIdleState_InitialBackoff   =   200,     // 200s
    kLTDeviceWiFiIdleState_MaxBackoff       =   3600,    // 3600s
};

enum {
    kLTLogRecord_NetworkStatus_Type = 1,
    kLTLogRecord_NetworkStatus_Channel,
    kLTLogRecord_NetworkStatus_RSSI,
    kLTLogRecord_NetworkStatus_SNR,
    kLTLogRecord_NetworkStatus_MCS,
    kLTLogRecord_NetworkStatus_Samples,
    kLTLogRecord_NetworkStatus_JoinSuccess,
    kLTLogRecord_NetworkStatus_JoinFail,
    kLTLogRecord_NetworkStatus_Disconnects,
    kLTLogRecord_NetworkStatus_TXTotal,
    kLTLogRecord_NetworkStatus_TXRetry,
    kLTLogRecord_NetworkStatus_TXDrop,
    kLTLogRecord_NetworkStatus_RXTotal,

    kLTLogRecord_NetworkStatus_FieldCount,
};

typedef enum {
    kLTLogRecord_NetworkStatus_Type_WiFi = 1,
    kLTLogRecord_NetworkStatus_Type_Ethernet
} LTLogRecordNetworkStatusType;

typedef enum {
    kLTLogRecord_MCSIndex_Count
} LTLogRecordMCSIndex;

typedef struct {
    s8 min;
    s8 max;
    s8 average;
    s8 latest;
} LTLogRecordStats;

typedef struct {
    LTLogRecordNetworkStatusType type;    ///< Connection type
    u8 channel;                           ///< Current channel
    LTLogRecordStats rssiStats;           ///< Historical statistics of RSSI
    LTLogRecordStats snrStats;            ///< Historical statistics of SNR
    u8 mcs[kLTLogRecord_MCSIndex_Count];  ///< MCS histogram
    u32 sampleCount;                      ///< Total number of samples taken
    u32 joinSuccess;                      ///< Count of successful associations
    u32 joinFail;                         ///< Count of failed association attempts
    u32 disconnects;                      ///< Count of disconnects after association
    u64 txTotal;                          ///< Total TX frames sent
    u64 txRetry;                          ///< Total TX frames retried
    u64 txDrop;                           ///< Total TX frames dropped
    u64 rxTotal;                          ///< Total RX frames received
} LTLogRecordNetworkStatus;

static LTLogRecordNetworkStatus Metrics;

/*******************************************************************************
 * OS Helper Functions
 ******************************************************************************/

#define ALLOC(v) (v = OSH_Malloc(#v, sizeof(*v)))
#define FREE(v)  { lt_free(v); (v) = NULL; }
#define CLEAR(v) lt_memset(v, 0, sizeof(*v))


/** Standard LT Interfaces ****************************************************/

static LTCore               *pCore = NULL;
static ILTThread            *iThread = NULL;
static ILTEvent             *iEvent = NULL;
static u32                   WSM_DisallowanceGrant = 0;
static LTNetBackoff         *WSM_Backoff = NULL;

#if USE_LOGGER_RECORDS
static LTSystemLogger       *pLogger = NULL;
#endif

/** Misc **********************************************************************/

static void *OSH_Malloc(char const *id, LT_SIZE size) {
    void *mem = lt_malloc(size);
    if (!mem) LTLOG("mem.none", "no mem: %s", id);
    else lt_memset(mem, 0, size);
    return mem;
}

static const char *DEV_ApSecurityStrings[kLTWiFi_ApSecurity_Max] = { LTWiFi_SecurityStrings };

#define SETTINGS_PREFIX       "wifi/"
#define SETTINGS_KEY_SSID     SETTINGS_PREFIX "ssid"
#define SETTINGS_KEY_PASS     SETTINGS_PREFIX "pass"
#define SETTINGS_KEY_BSSID    SETTINGS_PREFIX "bssid"
#define SETTINGS_KEY_SECURITY SETTINGS_PREFIX "security"
#define SETTINGS_KEY_AUTOJOIN SETTINGS_PREFIX "autojoin"
#define SETTINGS_KEY_REJOIN   SETTINGS_PREFIX "rejoin"

/// @todo put in system config for dynamic changes
#define METRICS_SAMPLING_PERIOD 10
/// @todo put in system config for dynamic changes
#define METRICS_SAMPLES_PER_LOG  6

/*******************************************************************************
 * WiFi Driver Interface
 ******************************************************************************/

static LTUtilityMacAddress *MAC_Library;
static LTSystemSettings    *SET_Library;
static LTDriverLibrary     *DRV_Library;
static LTDriverWiFi        *DRV_WiFi;
static LTDeviceUnit         DRV_Unit;
static LTWiFi_DriverInfo    DRV_Info;

static bool DEV_VarsValid = false; // means quit can execute

static void DEV_InitVars(void) {
    MAC_Library = NULL;
    SET_Library = NULL;
    DRV_Library = NULL;
    DRV_WiFi    = NULL;
    DRV_Unit    = 0;
    CLEAR(&DRV_Info);
    DEV_VarsValid = true;
}

static bool DEV_Init(void) {
    DEV_InitVars();
    const char *msg;
    do {
        msg = "no mac lib";
        if (!(MAC_Library = lt_openlibrary(LTUtilityMacAddress))) break;
        msg = "no driver";
        if (!(DRV_Library = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceWiFi", 0))) break;
        msg = "no unit";
        if (!(DRV_Unit = DRV_Library->CreateDeviceUnitHandle(0))) break; // WiFi STA by definition
        if (!(DRV_WiFi = (LTDriverWiFi *)pCore->GetHandleInterface(DRV_Unit))) break;
        msg = NULL;
    } while (0);
    if (msg) LTLOG_REDALERT("init.fail", "%s", msg);
    return !msg;
}

static void DEV_Quit(void) {
    if (!DEV_VarsValid) return;
    pCore->DestroyHandle(DRV_Unit); // zero okay
    pCore->CloseLibrary((LTLibrary*)DRV_Library); // null okay
    pCore->CloseLibrary((LTLibrary*)SET_Library); // null okay
    pCore->CloseLibrary((LTLibrary*)MAC_Library); // null okay
    DEV_VarsValid = false;
}

static bool DEV_OpenSettings(void) {
    if (SET_Library) return true;
    SET_Library = lt_openlibrary(LTSystemSettings);
    return !!SET_Library;
}

/*******************************************************************************
 *
 * WSM - The WiFi State Machine
 *
 * This state machine runs as a continuous thread where STATES are integers
 * that transition through code ACTIONS.
 *
 * The current STATE is determined by:
 *
 *    1. Higher level API REQUESTS for a specific function (e.g. scan).
 *    2. Internal sequences between ACTIONS
 *    3. Lower level DRIVER RESPONSES that require processing
 *
 * ACTIONS cannot block. If an ACTION calls any blocking function, it must
 * be must be handled asynchronously. This includes the driver interface.
 *
 * API REQUESTS are usually queued and handled in a FIFO manner. However,
 * some API REQUESTS are urgent and are allowed to interrupt normal state
 * processing. (WiFi reset for example.)
 *
 ******************************************************************************/

/**
 * WSM_STATES - States used by the state machine
 */
#define WSM_STATES(DEF) \
    DEF(Done)           \
    DEF(Idle)           \
    DEF(Monitor)        \
    DEF(Start)          \
    DEF(Stop)           \
    DEF(Enable)         \
    DEF(Disable)        \
    DEF(WaitUp)         \
    DEF(WiFiUp)         \
    DEF(Reset)          \
    DEF(DriverCheck)    \
    DEF(DriverReset)    \
    DEF(ScanStart)      \
    DEF(ScanCheck)      \
    DEF(ScanFound)      \
    DEF(ScanDone)       \
    DEF(Join)           \
    DEF(Rejoin)         \
    DEF(JoinStart)      \
    DEF(JoinCheck)      \
    DEF(JoinDone)       \
    DEF(LinkCheck)      \
    DEF(Disconnect)     \
    DEF(DriverStates)   \
    DEF(HwFail)         \
    DEF(MaxState)

typedef u8 WSM_State;

/** Generate WiFi state enums  **/
#define WSENUM(val) WS_##val,
enum WSM_State {
    WSM_STATES(WSENUM)
};

/** Generate WiFi state name strings (for debug) **/
#define WSNAME(val) #val,
static const char *WSM_StateNames_[] = {
    WSM_STATES(WSNAME)
};

/**
 * WSM_Input - An input can be any API-created REQUEST or driver-originated
 * RESPONSE. Both are passed as input to the DoNext() function of the state
 * machine. These carry both state, context, data, and callbacks.
 */
typedef struct WSM_Input WSM_Input;
struct WSM_Input {
    WSM_Input *next;
    u8  state;                  // WSM state
    s32 value;                  // arbitrary value
    LTThread caller_thread;     // only dealloc WSM_Input when NULL
    LTEvent event;              // used to trigger callback
    union {
        LTWiFi_ScanSpec scan_spec;
        LTWiFi_ApInfo   join_spec;
        u8*             mesh_advert; // mesh advertisement (experimental)
    };
};

/**
 * WSM_Queue - Queue to hold WSM_Inputs for API requests and DRIVER responses.
 */
typedef struct WSM_Queue {
    WSM_Input *head;
    WSM_Input *tail;
    LTMutex   *mutex;
} WSM_Queue;

/*******************************************************************************
 * WSM Primary Variables and Functions
 ******************************************************************************/
enum {
    WiFiConfig_StateMachineStackSize = 1536,
    WiFiConfig_StartTimeoutSeconds = 2,
    WiFiConfig_CheckDriverSeconds = 2,
    WiFiConfig_ScanTimeoutSeconds = 6,
    WiFiConfig_JoinTimeoutSeconds = 9,
    WiFiConfig_LinkCheckSeconds = 4,
};

// Maximum number of cached APs from the driver's scan. Some drivers burst them.
#define MAX_SCANNED_APS 20

/** WSM Variables *************************************************************/

// Note: Could be put into a single struct
static LTThread   WSM_Thread;
static LTEvent    WSM_StatEvent;
static WSM_Queue  WSM_PendingQueue;
static WSM_Input  WSM_ThisRequest;
static LTMutex   *WSM_InfoMutex;
static LTMutex   *WSM_ScanMutex;
static LTAtomic   WSM_DriverUp;
static LTAtomic   WSM_DebugMode;
static LTAtomic   WSM_SniffMode;
static LTAtomic   WSM_Connected;      // 0->1 in WS_JoinDone, =1 during ReJoin
static LTAtomic   WSM_LinkConnected;  // Lower level disconnect. Triggers ReJoin
static LTAtomic   WSM_JoinStatus;
static LTAtomic   WSM_LastJoinStatus;
static bool       WSM_AutoJoin;
static bool       WSM_RejoinEnabled;  // enabled by settings or SetOption
static u8         WSM_RejoinCount;    // also indicates auto-rejoin (when > 0)
static bool       WSM_ScanActive;
static LTEvent    WSM_ScanEvent;
static LTEvent    WSM_JoinEvent;
static void      *WSM_CallbackData;  // from the current request (like scan)
static LTArray   *WSM_ScanCache;     // cache for AP list from LTDriverWiFi
static LTWiFi_ApInfo WSM_JoinSpec;   // join specification cached to support rejoin
static LTWiFi_ApInfo WSM_ApInfo;     // cached to allow API queries
static char          WSM_JoinPass[kLTWiFi_Max_Pass+2];

static bool WSM_VarsValid = false; // means quit can execute

static void WSM_InitVars(void) {
    WSM_Thread    = 0;
    WSM_StatEvent = 0;
    CLEAR(&WSM_PendingQueue);
    CLEAR(&WSM_ThisRequest);
    WSM_InfoMutex = NULL;
    LTAtomic_Store(&WSM_DriverUp,       0);
    LTAtomic_Store(&WSM_DebugMode,      0);
    LTAtomic_Store(&WSM_SniffMode,      0);
    LTAtomic_Store(&WSM_Connected,      0);
    LTAtomic_Store(&WSM_LinkConnected,  0);
    LTAtomic_Store(&WSM_JoinStatus,     0);
    LTAtomic_Store(&WSM_LastJoinStatus, 0);
    CLEAR(&WSM_ApInfo);
    WSM_VarsValid = true;
    WSM_ScanActive = false;
    WSM_CallbackData = NULL;
    WSM_RejoinCount = 0;
    WSM_AutoJoin = false;
    CLEAR(&WSM_JoinPass);
    WSM_RejoinEnabled = false;
}

/** Forward references ********************************************************/

static WSM_Input *WSM_CreateInput(u8 state);
static void WSM_PostRequest(WSM_Input *request);
static void WSM_GotTimer(void * pClientData);
static void WSM_FreeInput(LTThread_ReleaseReason releaseReason, void *pClientData);
static void WSM_ProcessInput(void *data);
static bool API_LoadApSettings(LTWiFi_ApInfo *ap);

/** Misc Utilities ************************************************************/

static LTWiFi_ApSecurity DEV_StringToApSecurity(const char *sec) {  // add to API? !!!
    if (!sec || !sec[0]) return kLTWiFi_ApSecurity_Unknown;
    for (int n = 0; n < kLTWiFi_ApSecurity_Max; n++) {
        if (lt_strcmp(sec, DEV_ApSecurityStrings[n]) == 0) return n;
    }
    return kLTWiFi_ApSecurity_Unknown;
}

static bool DEV_IsSameAp(LTWiFi_ApInfo *ap1, LTWiFi_ApInfo *ap2) {
    if (!ap1 || !ap2 || !ap1->pass || !ap2->pass) return false;
    return (lt_strcmp(ap1->ssid, ap2->ssid) == 0 &&
        lt_strcmp(ap1->pass, ap2->pass) == 0 &&
        ap1->security == ap2->security);
}

// Mac address to string for debug - Warning: static string return, not thread safe
static char *MacToStr(LTMacAddress *mac) {
    static char addr[20];
    MAC_Library->MacAddressToString(mac, addr, ':');
    return addr;
}

// Mac address to Oui+Hash for debug - Warning: static string return, not thread safe
static char *MacToOui(LTMacAddress *mac) {
    static char addr[30];
    MAC_Library->MacAddressToOui(mac, addr, ':');
    lt_snprintf(&addr[17], sizeof(addr)-17, ", %lx", LT_Pu32(MAC_Library->MacAddressToHash(mac)));
    return addr;
}

/** Event Handling ************************************************************/

static const LTArgsDescriptor WSM_StatEventArgs = {2, { kLTArgType_u32, kLTArgType_u32 }}; // status, unit

static void WSM_StatEventProc(LTEvent event, void *proc, LTArgs *args, void *data) {
    // Runs in API caller thread context
    LT_UNUSED(event);
    (*(LTDeviceWiFi_StatusCallback *)proc)((LTDeviceWiFi_Status)LTArgs_u32At(0, args),(LTDeviceUnit)LTArgs_u32At(1, args), data);
}

static void WSM_StatEventNotifyImmediateProc(LTEvent event, void *notifyImmediateClientData, void *proc, void *procClientData) {
    LT_UNUSED(event);
    LT_UNUSED(notifyImmediateClientData);
    // Newly registered event handlers are primarily interested in the Up and Connected statuses
    // to start with. Signal the new event handler if either or both of these are already true.
    if (LTAtomic_Load(&WSM_DriverUp)) {
        (*(LTDeviceWiFi_StatusCallback *)proc)(kLTDeviceWiFi_Status_Up, DRV_Unit, procClientData);
        if (LTAtomic_Load(&WSM_Connected)) (*(LTDeviceWiFi_StatusCallback *)proc)(kLTDeviceWiFi_Status_Connected, DRV_Unit, procClientData);
    }
}

static const LTArgsDescriptor WSM_ScanEventArgs = {1, { kLTArgType_pointer }}; // ap

static void WSM_ScanEventProc(LTEvent event, void *proc, LTArgs *args, void *data) {
    // Runs in API caller thread context
    LT_UNUSED(event);
    LTArray *scan_cache = LTArgs_pointerAt(0, args); // NULL on final callback
    if (scan_cache == NULL) {
        (*(LTDeviceWiFi_ScanCallback *)proc)(NULL, data);
    } else {
        for (u32 n = 0; n < scan_cache->API->GetCount(scan_cache); n++) {
            LTWiFi_ApInfo *ap = scan_cache->API->Get(scan_cache, n, NULL);
            (*(LTDeviceWiFi_ScanCallback *)proc)(ap, data);
        }
    }
}

static void WSM_ScanEventCompleteProc(LTEvent event, LTArgs *args) {
    // Runs in API caller thread context unless the API caller thread went away
    //
    // Any data allocated for an event Notify *must* be deallocated in the EventCompleteProc
    // not in the event dispatch proc for 2 reasons:
    // a. In the event (no pun intended) you have multiple event receivers, this procedure invocation
    //    is guaranteed to occur after all of the event receiver dispatch function invocations in the various
    //    receiver threads have all completed.
    // b. In the event the API caller thread has gone away
    LTArray *scan_cache = LTArgs_pointerAt(0, args);
    // LTLOG("event.done", "============ EVENT COMPLETE %lx ==========", LT_PLT_HANDLE(scan_cache));
    if (scan_cache) {
        lt_destroyobject(scan_cache);
    } else { // Final callback, delete the event
        iEvent->UnregisterFromEvent(event, WSM_ScanEventProc);
        iEvent->Destroy(event);
    }
}

static const LTArgsDescriptor WSM_JoinEventArgs = {1, { kLTArgType_u32 }}; // status

static void WSM_JoinEventProc(LTEvent event, void *proc, LTArgs *args, void *data) {
    // Runs in API caller thread context
    LTWiFi_JoinStatus status = (LTWiFi_JoinStatus)LTArgs_u32At(0, args);
    (*(LTDeviceWiFi_JoinCallback *)proc)(status, data);
    if (status >= kLTWiFi_JoinStatus_Failed) { // includes abort and success also
        iEvent->UnregisterFromEvent(event, WSM_JoinEventProc);
        iEvent->Destroy(event);
    }
}

/** WSM Thread ****************************************************************/
/* this is how WiFi should control the power level of the driver for sleep.
   although I would say SetOption("anything", anyVal) is  NOT the way we should
   be controlling driver behavior.  The driver should have an explicit api to
   set power states, no api that compares strings to set variables. */
static void API_SetOption(char const *option, s32 value);
static void WSM_PostState(WSM_State state);
static LTTime WSM_SleepActionEventProc(LTCore_SleepAction action, LTTime wakeupTimeOrSleepDuration, void *pClientData) {
    LT_UNUSED(wakeupTimeOrSleepDuration);
    LT_UNUSED(pClientData);
    switch (action) {
        case kLTCore_SleepAction_GoingToSleep:
            API_SetOption("performance_profile", kLTDeviceWiFiPowerState_Sleep);
            break;
        case kLTCore_SleepAction_AwakenedFromSleep:
            API_SetOption("performance_profile", kLTDeviceWiFiPowerState_Awake);
            WSM_PostState(WS_Monitor);
            break;
        default:
            API_SetOption("performance_profile", kLTDeviceWiFiPowerState_Awake);
            break;
    }
    return LTTime_Zero();
}

static bool WSM_InitThread(void) {
    WSM_PendingQueue.mutex = lt_createobject(LTMutex);
    WSM_InfoMutex = lt_createobject(LTMutex);
    WSM_ScanMutex = lt_createobject(LTMutex);
    WSM_PostRequest(WSM_CreateInput(WS_Start));
    LTThread hThread = iThread->GetCurrentThread();
    iThread->SetTimer(hThread, LTTime_Seconds(10), WSM_GotTimer, NULL, LTHANDLE_TO_VOIDPTR(hThread));
    LT_GetCore()->OnSleepAction(&WSM_SleepActionEventProc, NULL);
    return true;
}

static void WSM_ExitThread(void) {
    LT_GetCore()->NoSleepAction(&WSM_SleepActionEventProc);
    LTThread hThread = iThread->GetCurrentThread();
    iThread->KillTimer(hThread, WSM_GotTimer, LTHANDLE_TO_VOIDPTR(hThread));
    lt_destroyobject(WSM_ScanMutex);
    lt_destroyobject(WSM_InfoMutex);
    lt_destroyobject(WSM_PendingQueue.mutex);
    WSM_ScanMutex = NULL;
    WSM_InfoMutex = NULL;
    WSM_PendingQueue.mutex = NULL;
}

#if USE_LOGGER_RECORDS
LTLOG_DEFINE_RECORD_NETWORKSTATUS(Metrics);
#endif

enum {
    kCheckMetricsReset = 0,
    kCheckMetricsSample,
};

static void CheckMetrics(u8 mode) {
    if (mode == kCheckMetricsSample) {
        Metrics.sampleCount++;
        LTWiFi_ApInfo ap;
        if (!DRV_WiFi->GetApInfo(DRV_Unit, (&ap))) return;  // not connected, do not report
        Metrics.channel = ap.channel;
        Metrics.rssiStats.latest = ap.rssi;
        Metrics.snrStats.latest = ap.snr;
        if (ap.rssi < 0) {  // don't let a bogus RSSI mess up high/low water marks
            if (ap.rssi > Metrics.rssiStats.max) Metrics.rssiStats.max = ap.rssi;
            if (ap.rssi < Metrics.rssiStats.min) Metrics.rssiStats.min = ap.rssi;
            if (Metrics.sampleCount != 0) {
                Metrics.rssiStats.average += ((s32)ap.rssi - (s32)Metrics.rssiStats.average) /
                                             (s32)Metrics.sampleCount;
            }
            if (ap.snr > Metrics.snrStats.max) Metrics.snrStats.max = ap.snr;
            if (ap.snr < Metrics.snrStats.min) Metrics.snrStats.min = ap.snr;
            if (Metrics.sampleCount != 0) {
                Metrics.snrStats.average += ((s32)ap.snr - (s32)Metrics.snrStats.average) /
                                             (s32)Metrics.sampleCount;
            }
        }
        LTWiFi_Metrics metrics = {};
        DRV_WiFi->GetMetrics(DRV_Unit, &metrics, sizeof(metrics));
        Metrics.txTotal = metrics.tx_frame_count;
        Metrics.rxTotal = metrics.rx_frame_count;
        Metrics.joinSuccess += metrics.joinSuccess;  ///< New Auto-rejoin successes (optional)
        Metrics.joinFail += metrics.joinFail;        ///< New Auto-rejoin fails (optional)
        Metrics.disconnects += metrics.joinDisc;     ///< New Auto-rejoin disconnects (optional)
    }
    if (mode == kCheckMetricsReset || (Metrics.sampleCount % METRICS_SAMPLES_PER_LOG) == (METRICS_SAMPLES_PER_LOG - 1)) {
        if(Metrics.sampleCount > 0) {
            #if USE_LOGGER_RECORDS
                pLogger->EncodeRecord(&Metrics_Record);
            #endif
            LTLOG_LEVEL(kLogDebug, "metrics",
                        "sc:%lu js:%lu jf:%lu dc:%lu ch:%lu rs:%ld rh:%ld rl:%ld sn:%ld tx:%lu rx:%lu",
                        LT_Pu32(Metrics.sampleCount),
                        LT_Pu32(Metrics.joinSuccess),
                        LT_Pu32(Metrics.joinFail),
                        LT_Pu32(Metrics.disconnects),
                        LT_Pu32(Metrics.channel),
                        LT_Ps32(Metrics.rssiStats.latest),
                        LT_Ps32(Metrics.rssiStats.max),
                        LT_Ps32(Metrics.rssiStats.min),
                        LT_Ps32(Metrics.snrStats.latest),
                        LT_Pu32(Metrics.txTotal),
                        LT_Pu32(Metrics.rxTotal)
            );
        }
        if (mode == kCheckMetricsReset) {
            Metrics               = (LTLogRecordNetworkStatus){};
            Metrics.type          = kLTLogRecord_NetworkStatus_Type_WiFi;
            Metrics.rssiStats.max = -127;
            Metrics.snrStats.min = 127;
        }
    }
}

/**
 * WSM_Init -- Create the state machine thread and input/request queue.
 */
static bool WSM_Init(void) {
    WSM_InitVars();
    API_LoadApSettings(NULL); // Load "wifi/*" settings from nvram

    WSM_ScanCache = lt_createobject_typed(LTArray, List);
    if (!WSM_ScanCache) return false;
    WSM_ScanCache->API->InitAsStructArray(WSM_ScanCache, sizeof(LTWiFi_ApInfo));

    WSM_Thread = pCore->CreateThread("WiFiState");
    if (!WSM_Thread) return false;

    WSM_StatEvent = pCore->CreateEvent(&WSM_StatEventArgs, WSM_StatEventProc, NULL, WSM_StatEventNotifyImmediateProc, NULL);

    // Launch the WSM thread:
    iThread->SetStackSize(WSM_Thread, WiFiConfig_StateMachineStackSize);
    iThread->Start(WSM_Thread, WSM_InitThread, WSM_ExitThread);
    return true;
}

/**
 * WSM_Quit -- Stop the state machine and destroy its thread.
 */
static void WSM_Quit(void) {
    if (!WSM_VarsValid) return;
    iThread->Destroy(WSM_StatEvent); // Null okay for all below...
    iThread->Destroy(WSM_Thread);
    lt_destroyobject(WSM_ScanCache);
    WSM_VarsValid = false;
}

/**
 * InfoMutex is used when copying data from within the state machine thread
 * back to API function caches. This copy minimizes blocking for simple
 * check/info API functions.
 */

#define INFO_GUARD(expr) {       \
    WSM_InfoMutex->API->Lock(WSM_InfoMutex);   \
    expr;                          \
    WSM_InfoMutex->API->Unlock(WSM_InfoMutex); \
}

/**
 * WSM_Input - The state machine reacts to state inputs. An input can be
 * an API-created REQUEST or a driver-originated RESPONSE. Both types get
 * passed in the same WSM_Input structure with the State field determining
 * how they are treated.
 */
static WSM_Input *WSM_CreateInput(u8 state) {
    TR;
    WSM_Input *input = 0;
    if (!ALLOC(input)) return NULL;
    CLEAR(input);
    input->state = state;
    return input;
}

static void WSM_DestroyInput(WSM_Input *input) {
    if (input && input->caller_thread) {
        CLEAR(input);  // cause trouble if accessed
        FREE(input);
    }
}

static void WSM_QueueInput(WSM_Queue *queue, WSM_Input *input) {
    queue->mutex->API->Lock(queue->mutex);
    // If it's a DRIVER event, make it highest priority (even if there are
    // prior DRIVER events pending.
    if (input->state > WS_DriverStates) {
        // Queue it to head:
        if (queue->head) input->next = queue->head->next;
        queue->head = input;
        if (!queue->tail) queue->tail = input;
    } else {
        // Queue it to tail:
        if (queue->tail) queue->tail->next = input;
        if (!queue->head) queue->head = input;
        input->next = NULL;
    }
    queue->mutex->API->Unlock(queue->mutex);
}

static WSM_Input *WSM_PopInput(WSM_Queue *queue) {
    queue->mutex->API->Lock(queue->mutex);
    WSM_Input *next = queue->head;
    if (next) {
        queue->head = next->next;
        if (queue->tail == next) queue->tail = NULL;
        next->next = NULL;
    }
    queue->mutex->API->Unlock(queue->mutex);
    if (next) LTLOG_LEVEL(kLogDebug, "wsm.pop", "WSM PopInput(%s)", WSM_StateNames_[next->state]);
    return next;
}

/**
 * WSM_PostRequest -- Submit an API-created state request to the state machine.
 * If the request includes a callback, it will be called per the details
 * of the API. For example, callback on scan results and scan done.
 */
static void WSM_PostRequest(WSM_Input *request) {
    if (!WSM_Thread || request->state >= WS_MaxState) {
        FREE(request);
        return;
    }
    LTLOG_LEVEL(kLogDebug, "wsm.pst", "WSM PostRequest(%s)", WSM_StateNames_[request->state]);
    request->caller_thread = iThread->GetCurrentThread();
    iThread->QueueTaskProc(WSM_Thread, WSM_ProcessInput, WSM_FreeInput, request);
}

/**
 * WSM_PostState -- A helper for common case of state PostRequest.
 */
static void WSM_PostState(WSM_State state) {
    TR;
    WSM_PostRequest(WSM_CreateInput(state));
}

static void WSM_SleepTimerCb(void * pClientData) {
    LTThread hThread = VOIDPTR_TO_LTHANDLE(pClientData);
    iThread->KillTimer(hThread, WSM_SleepTimerCb, pClientData);
}

static void WSM_SetTimer(u32 t) {
    LTThread hThread = iThread->GetCurrentThread();
    iThread->SetTimer(hThread, LTTime_Milliseconds(t), WSM_GotTimer, NULL, LTHANDLE_TO_VOIDPTR(hThread));
}
static void WSM_SleepTimer(LTTime t) {
    LTThread hThread = iThread->GetCurrentThread();
    iThread->SetTimer(hThread, t, WSM_SleepTimerCb, NULL, LTHANDLE_TO_VOIDPTR(hThread));
    iThread->SetAsWakeupTimer(hThread, WSM_SleepTimerCb, LTHANDLE_TO_VOIDPTR(hThread));
}

#define NEXT(s) state = (s)
#define NEXT_TIME(s, t) {state = (s); WSM_SetTimer(t);}
#define NEXT_TIME_OUT(s, t, o) {state = (s); WSM_SetTimer(t); SET_TIMEOUT(o);}
#define SET_SLEEP(t) {WSM_SleepTimer(t);}

#define SET_TIMEOUT(s) expired_time = LTTime_Add(pCore->GetKernelTime(), LTTime_Seconds(s))
#define IS_TIMEOUT (LTTime_IsGreaterThan(pCore->GetKernelTime(), expired_time))

static void /*LTWiFi_ScanResults_CB*/ WSM_Scan_DriverCB(LTDeviceUnit h_unit, LTWiFi_ApInfo *ap) {
    // This is only called when driver has scan results or has finished the scan.
    // When scan has finished, the AP pointer will be null.
    // Special filtering can be done here (for filters not provided by scan_spec.)
    // WARNING: Context may not be LT! It may be called from lower level driver.
    // It needs to copy the AP info into a safe place and either notify the
    // state machine or bump a counter so the state machine can see the change.
    LT_UNUSED(h_unit);
    WSM_ScanMutex->API->Lock(WSM_ScanMutex);
    if (!ap) WSM_ScanActive = false;
    else if (WSM_ScanCache->API->GetCount(WSM_ScanCache) < MAX_SCANNED_APS) {
        LTLOG_LEVEL_DEBUG(kLogDebug, "scan.cache", "%d Scan %s %d rssi:%d", (int)WSM_ScanCache->API->GetCount(WSM_ScanCache), ap->ssid, ap->channel, (int)ap->rssi);
        WSM_ScanCache->API->Append(WSM_ScanCache, ap); // A copy of *ap is stored
        // If cache is half full, nag the state machine (but just once):
        // FIXME: the optimization below isn't essential, removing it improves scan stability
        // if (WSM_ScanCache->API->GetCount(WSM_ScanCache) == MAX_SCANNED_APS/2) WSM_PostState(WS_ScanCheck);
    } else {
        LTLOG("scan.over", "scan overflow: %s ch: %d", ap->ssid, ap->channel);
    }
    WSM_ScanMutex->API->Unlock(WSM_ScanMutex);
    // This is were we could trigger the state machine rather than use a timer
}

static void /*LTWiFi_JoinStatus_CB*/ WSM_Join_DriverCB(LTDeviceUnit h_unit, LTWiFi_JoinStatus status) {
    // This is called when as the driver progresses through a join operation.
    // WARNING: Context may not be LT! It may be called from lower level driver.
    LT_UNUSED(h_unit);
    LTAtomic_Store(&WSM_JoinStatus, status);
    // This is were we could trigger the state machine rather than use a timer
    if (status == kLTWiFi_JoinStatus_Failed && LTAtomic_Load(&WSM_Connected) && LTAtomic_Load(&WSM_LinkConnected)) {
        WSM_PostState(WS_LinkCheck); // We lost connection just now
    }
    if (status == kLTWiFi_JoinStatus_Success && LTAtomic_Load(&WSM_Connected) && !LTAtomic_Load(&WSM_LinkConnected)) {
        WSM_PostState(WS_LinkCheck); // We regained connection just now
    }
}

/**
 * WSM_DoNext - The body of the state machine. For simplicity it's written
 * as a switch-case statement. The WSM_input passes in both API requests,
 * driver responses, and timer events.
 *
 * WiFi state is ONLY changed within this single thread. As a result, locking
 * is greatly reduced. This thread also coordinates WiFi actions to avoid
 * collisions between various states (e.g. doing a scan during a join.)
 *
 * IMPORTANT: All case actions are asynchronous or very-short duration.
 * The are not allowed to block for any significant period of time to avoid
 * stalling the state machine (which monitors all aspects of WiFi and gathers
 * statistics.)
 */

static void WSM_DoNext(WSM_Input *input) {
    static u8 state = WS_Done;  // current state
    static LTTime expired_time;
    static LTTime rejoin_time;
    static LTTime link_check_time;
    static LTTime metrics_check_time = LTTimeInitializer_Seconds(METRICS_SAMPLING_PERIOD);
    LTTime now;

    // If woke for an API request or DRIVER response, queue accordingly.
    // Otherwise, we woke from a timer. Continue with current state.
    if (input) WSM_QueueInput(&WSM_PendingQueue, input);
    else {
        // LTLOG_DEBUG compiles to nothing in release mode, make sure nothing doesn't change the target of the if
        LTLOG_LEVEL_DEBUG(kLogState, "wsm.tim", "WSM Timer(%s)", WSM_StateNames_[state]);
    }

    // State machine loop:
    while (state < WS_MaxState) {
        // If nothing in-progress, get what's next:
        if (state <= WS_Idle) {
            WSM_Input *next = WSM_PopInput(&WSM_PendingQueue);
            if (next) {
                // We've got something new to do...
                WSM_ThisRequest = *next;
                state = next->state;
                WSM_DestroyInput(next); // dealloc the request
            } else {
                state = WS_Monitor;
                CLEAR(&WSM_ThisRequest);
            }
        }

        if (LTAtomic_Load(&WSM_DebugMode) & kLogState || (LTAtomic_Load(&WSM_DebugMode) & kLogVerbose && state != WS_Monitor)) {
            LTLOG("wsm.sta", "WSM State(%s)", WSM_StateNames_[state]);
        }

        // Process next state:
        switch (state) {

        case WS_Done:
            return;

        case WS_Start:
            SET_TIMEOUT(WiFiConfig_StartTimeoutSeconds);
            CheckMetrics(kCheckMetricsReset);
            iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_Start);
            NEXT(WS_Enable);
            break;

        case WS_Stop:
            NEXT(WS_Done);
            break;

        /*** Driver ***********************************************************/

        case WS_Enable:
            if (LTAtomic_Load(&WSM_DriverUp)) {
                // ignore enable
                NEXT(WS_Idle);
                break;
            }
            // Start bringing up the driver. If it fails, try again. Keep trying.
            // This can happen for catestrophic problems like a hardware failure.
            // Potentially, some kind of reset or power-cycle state could be added.
            // The line below requests driver to be up, but it make take some time.
            if (!DRV_WiFi->SetDriverState(DRV_Unit, kLTWiFi_DriverState_Up)) {
                NEXT_TIME(WS_Enable, 300);
                break;
            }
            // wait for it to come up
            NEXT_TIME_OUT(WS_WaitUp, 100, 1);
            break;

        case WS_Disable:
            // The line below requests driver to be down, but it could take time.
            DRV_WiFi->SetDriverState(DRV_Unit, kLTWiFi_DriverState_Down);
            // !!!??? will we get notified when it actually goes down?
            LTAtomic_Store(&WSM_DriverUp, 0);
            iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_Down, DRV_Unit);
            NEXT(WS_Idle);
            break;

        case WS_Reset:
            DRV_WiFi->SetDriverState(DRV_Unit, kLTWiFi_DriverState_ResetDriver);
            LTAtomic_Store(&WSM_DriverUp, 0);
            LTAtomic_Store(&WSM_Connected, 0);
            LTAtomic_Store(&WSM_LinkConnected, 0);
            CheckMetrics(kCheckMetricsReset);
            iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_Reset, DRV_Unit);
            NEXT(WS_WaitUp);
            break;

        case WS_WaitUp:
            if (DRV_WiFi->GetDriverState(DRV_Unit) >= kLTWiFi_DriverState_Up) {
                // If it's the initial UP, fetch the driver info. But, avoid doing
                // this again because it can make DRV_Info fields transient.
                LTAtomic_Store(&WSM_DriverUp, 1);
                NEXT(WS_WiFiUp);
                break;
            }
            if (IS_TIMEOUT) {
                if (LTAtomic_Load(&WSM_DriverUp)) iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_Down, DRV_Unit);
                LTAtomic_Store(&WSM_DriverUp, 0);
                NEXT(WS_Idle);
                break;
            }
            // Stay in WS_WaitUp state
            break;

        case WS_WiFiUp:
            // The WiFi driver is now officially up. We can start talking to it.
            INFO_GUARD(DRV_WiFi->GetDriverInfo(DRV_Unit, &DRV_Info));
            // if MAC_Library->IsZero(&DRV_Info.mac_address) indicate problem !!!
            LTLOG("wsm.sta.mac", "STA MAC: %s", MacToStr(&DRV_Info.mac_address));
            iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_Up, DRV_Unit);
            WSM_JoinSpec.pass = WSM_JoinPass;
            if (API_LoadApSettings(&WSM_JoinSpec)) {
                LTLOG("ap.set", "AP settings: ssid: \"%s\" sec: %s chan: %u", WSM_JoinSpec.ssid,
                    DEV_ApSecurityStrings[WSM_JoinSpec.security], WSM_JoinSpec.channel);
                WSM_RejoinCount = 0;
                LTAtomic_Store(&WSM_Connected, 0);
                LTAtomic_Store(&WSM_LinkConnected, 0);
                if (WSM_AutoJoin) {
                    // It's a validated AP. Rejoin and if that fails, keep trying.
                    WSM_RejoinCount = 1; // cause auto rejoin to happen
                    NEXT(WS_JoinStart);
                    break;
                }
            }
            NEXT(WS_Idle);
            break;

        case WS_DriverCheck:
            if (DRV_WiFi->GetDriverState(DRV_Unit) < kLTWiFi_DriverState_Up) {
                LTLOG("wsm.drv.bug", "driver problem"); // !!! need to handle driver problem and recover
            }
            NEXT(WS_Idle);
            break;

        case WS_Monitor:
            // !!! Add: Periodically gather WiFi metrics
            now = pCore->GetKernelTime();
            // If connected, periodically check link:
            if (LTAtomic_Load(&WSM_Connected) && LTTime_IsGreaterThan(now, link_check_time)) {
                NEXT(WS_LinkCheck);
                break;
            }
            // Periodically process metrics:
            if (LTAtomic_Load(&WSM_Connected) && LTTime_IsGreaterThan(now, metrics_check_time)) {
                CheckMetrics(kCheckMetricsSample);
                metrics_check_time = LTTime_Add(now, LTTime_Seconds(METRICS_SAMPLING_PERIOD));
                break;
            }
            // Not connected. Should we try to auto rejoin?
            if (!LTAtomic_Load(&WSM_LinkConnected) && WSM_RejoinCount) {
                // If kLTWiFi_DriverState_Connected then the driver is handling ReJoin
                if (DRV_WiFi->GetDriverState(DRV_Unit) != kLTWiFi_DriverState_Connected) {
                    if (WSM_RejoinCount == 1 || LTTime_IsGreaterThan(pCore->GetKernelTime(), rejoin_time)) {
                        WSM_RejoinCount++;  // Throttle rejoin attempts
                        if (WSM_RejoinCount > 20) WSM_RejoinCount = 20;
                        rejoin_time = LTTime_Add(pCore->GetKernelTime(), LTTime_Milliseconds(WSM_RejoinCount*500));
                        NEXT(WS_Rejoin);
                        break;
                    }
                }
            }
            if (IS_TIMEOUT) {
                SET_TIMEOUT(WiFiConfig_CheckDriverSeconds);
                NEXT(WS_DriverCheck);
                break;
            }
            if (WSM_Backoff && !LTAtomic_Load(&WSM_LinkConnected) && WSM_RejoinCount >= 20) {
                WSM_RejoinCount = 15;
                // If we're backing off, wake up when the backoff timer expires
                LTTime delay = WSM_Backoff->API->GetNextBackoff(WSM_Backoff);
                if (LTTime_IsEqual(delay, LTTime_Infinite())) {
                    //Unreachable
                    WSM_Backoff->API->Reset(WSM_Backoff);
                    delay = WSM_Backoff->API->GetNextBackoff(WSM_Backoff);
                }
                LTLOG("wsm.boff", "backoff delay: %llds", LT_Pu64(LTTime_GetSeconds(delay)));
                SET_SLEEP(delay);
                // Release sleep disallowance grant after setting backoff timer
                if (WSM_DisallowanceGrant) {
                    lt_reallowsleepmode(WSM_DisallowanceGrant);
                    WSM_DisallowanceGrant = 0;
                }
                NEXT_TIME(WS_Idle, LTTime_GetMilliseconds(delay));
            } else {
                NEXT_TIME(WS_Idle, 300);
            }
            return; // typical exit point

        /*** Scan *************************************************************/

        case WS_ScanStart:
            if(!WSM_DisallowanceGrant) {
                WSM_DisallowanceGrant = lt_disallowsleepmode();
            }
            SET_TIMEOUT(WiFiConfig_ScanTimeoutSeconds);
            WSM_ScanCache->API->SetCount(WSM_ScanCache, 0); // gets updated in the scan callback
            WSM_ScanActive = true;
            WSM_ScanEvent = WSM_ThisRequest.event;
            //LTLOG("event", "------scan event set %lx------", WSM_ScanEvent);
            DRV_WiFi->ScanStart(DRV_Unit, &WSM_ThisRequest.scan_spec, WSM_Scan_DriverCB);
            iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_ScanStart, DRV_Unit);
            NEXT_TIME(WS_ScanCheck, 200);
            break;

        case WS_ScanCheck: {
            // The state machine enters this state when either:
            // 1. The scan callback got a result and woke the state machine (best)
            // 2. A timer woke the state machine to check for scan results (polled)
            // 3. A timetout occurred, and the scan needs to be terminated (failed)
            if (!WSM_ScanEvent) { // handle false alarm from delayed driver queued request
                NEXT(WS_Monitor);
                break;
            }
            LTLOG_LEVEL_DEBUG(kLogDebug, "scan.check", "scanning... %d\n", (int)WSM_ScanCache->API->GetCount(WSM_ScanCache));
            bool done = false;
            WSM_ScanMutex->API->Lock(WSM_ScanMutex);
            u32 scan_count = WSM_ScanCache->API->GetCount(WSM_ScanCache);
            if (scan_count > 0) {
#ifdef LT_DEBUG
                for (u32 n = 0; n < scan_count; n++) {
                    LTWiFi_ApInfo *ap = WSM_ScanCache->API->Get(WSM_ScanCache, n, NULL);
                    LTLOG_LEVEL_DEBUG(kLogDebug, "scan.rslt", "%d: %s chan: %d", (int)n, ap->ssid, ap->channel);
                }
#endif
                LTArray *scan_cache = WSM_ScanCache;
                WSM_ScanCache = lt_createobject_typed(LTArray, List);
                if (WSM_ScanCache) {
                    WSM_ScanCache->API->InitAsStructArray(WSM_ScanCache, sizeof(LTWiFi_ApInfo));
                }
                iEvent->NotifyEvent(WSM_ScanEvent, scan_cache); // Event handler will free it
            }
            if (!WSM_ScanActive) done = true; // negative when scan done
            WSM_ScanMutex->API->Unlock(WSM_ScanMutex);
            if (!done && IS_TIMEOUT) {
                DRV_WiFi->ScanAbort(DRV_Unit); // make sure driver stopped the scan
                done = true;
            }
            if (done && WSM_ScanEvent) {
                if (LTAtomic_Load(&WSM_DebugMode) > 0) LTLOG_DEBUG("scan.done", "scan done");
                if (WSM_DisallowanceGrant) {
                    lt_reallowsleepmode(WSM_DisallowanceGrant);
                    WSM_DisallowanceGrant = 0;
                }
                iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_ScanDone, DRV_Unit);
                iEvent->NotifyEvent(WSM_ScanEvent, NULL); // indicates done
                WSM_ScanEvent = 0; // no more
                NEXT(WS_Monitor);
                break;
            }
            // If driver needs a nudge (e.g. if it's a blocking scan), nudge it:
            DRV_WiFi->ScanCheck(DRV_Unit); // a no-op if driver does not need it
            // return to the same state after timer or scan result wake-up
            return; // exit state machine, let something wake us up
        }

        /*** Join *************************************************************/

        case WS_Join:
            if (WSM_ThisRequest.join_spec.ssid[0]) {
                WSM_JoinSpec = WSM_ThisRequest.join_spec;
                if (WSM_JoinSpec.security <= kLTWiFi_ApSecurity_Open) WSM_JoinSpec.pass = NULL;
                else {
                    lt_strncpyTerm(WSM_JoinPass, WSM_ThisRequest.join_spec.pass, kLTWiFi_Max_Pass+1);
                    WSM_JoinSpec.pass = WSM_JoinPass;
                }
            }
            if (!WSM_JoinSpec.ssid[0]) { // not previously set?
                LTLOG("wsm.join.no", "join failed, AP not specified");
                iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_JoinFailed, DRV_Unit);
                NEXT(WS_Idle);
                break;
            }
            //WSM_CallbackData = WSM_ThisRequest.callback_data;
            WSM_JoinEvent = WSM_ThisRequest.event;
            WSM_JoinSpec.options = WSM_ThisRequest.join_spec.options;
            WSM_RejoinCount = WSM_JoinSpec.options & kLTWiFi_JoinOption_Retry ? 1 : 0;
            LTAtomic_Store(&WSM_Connected, 0);
            NEXT(WS_JoinStart);
            break;

        case WS_Rejoin:
            LTLOG_SERVER("join.auto", "auto rejoin \"%s\" for disconnect, count: %lu %ld", WSM_JoinSpec.ssid,
                         LT_Pu32(Metrics.joinFail + Metrics.joinSuccess), LT_Pu32(WSM_RejoinCount));
            iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_Rejoining, DRV_Unit);
            LTAtomic_Store(&WSM_LinkConnected, 0);
            NEXT(WS_JoinStart);
            break;

        case WS_JoinStart:
            LTLOG_SERVER("join.try", "ssid: \"%s\" sec: %s chan: %u bssid: %s", WSM_JoinSpec.ssid,
                         DEV_ApSecurityStrings[WSM_JoinSpec.security], WSM_JoinSpec.channel,
                         MacToOui(&WSM_JoinSpec.bssid));
            if(!WSM_DisallowanceGrant) {
                WSM_DisallowanceGrant = lt_disallowsleepmode();
            }
            SET_TIMEOUT(WiFiConfig_JoinTimeoutSeconds);
            if (WSM_RejoinEnabled) WSM_JoinSpec.options |= kLTWiFi_JoinOption_Retry;
            LTAtomic_Store(&WSM_JoinStatus, kLTWiFi_JoinStatus_Starting);
            LTAtomic_Store(&WSM_LastJoinStatus, kLTWiFi_JoinStatus_Starting);
            DRV_WiFi->JoinStart(DRV_Unit, &WSM_JoinSpec, WSM_Join_DriverCB);
            iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_JoinStart, DRV_Unit);
            NEXT_TIME(WS_JoinCheck, 200);
            break;

        case WS_JoinCheck: {
            if (IS_TIMEOUT) {
                LTAtomic_Store(&WSM_JoinStatus, kLTWiFi_JoinStatus_Failed);
            }
            // Sequence through the connection states:
            //LTLOG("join", "prior %ld new %ld", LT_Pu32(WSM_LastJoinStatus), LT_Pu32(WSM_JoinStatus));
            switch (LTAtomic_Load(&WSM_JoinStatus)) {
                case kLTWiFi_JoinStatus_Starting:
                    LTAtomic_Store(&WSM_JoinStatus, kLTWiFi_JoinStatus_Scanning);
                    break;
                case kLTWiFi_JoinStatus_Associated:
                    if (LTAtomic_Load(&WSM_LastJoinStatus) < kLTWiFi_JoinStatus_Associated) {
                        iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_JoinAssociated, DRV_Unit);
                    }
                    break;
                case kLTWiFi_JoinStatus_Authenticated:
                    if (LTAtomic_Load(&WSM_LastJoinStatus) < kLTWiFi_JoinStatus_Authenticated) {
                        iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_JoinAuthenticated, DRV_Unit);
                    }
                    break;
                case kLTWiFi_JoinStatus_Success:
                    {
                        u32 wasConnected = LTAtomic_Load(&WSM_Connected);
                        LTWiFi_ApInfo info;
                        LTAtomic_Store(&WSM_Connected, 1);
                        LTAtomic_Store(&WSM_LinkConnected, 1);
                        DRV_WiFi->GetApInfo(DRV_Unit, &info);
                        INFO_GUARD(WSM_ApInfo = info); // initial info
                        if (!wasConnected) {
                            iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_Connected, DRV_Unit);
                        } else {
                            iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_LinkConnected, DRV_Unit);
                        }
                    }
                    if(WSM_Backoff) WSM_Backoff->API->Reset(WSM_Backoff);
                    NEXT(WS_JoinDone);
                    break;
                case kLTWiFi_JoinStatus_Failed:
                    NEXT(WS_JoinDone);
                    break;
                default:
                    // don't care
                    break;
            }
            if (LTAtomic_Load(&WSM_JoinStatus) > LTAtomic_Load(&WSM_LastJoinStatus)) {
                LTAtomic_Store(&WSM_LastJoinStatus, LTAtomic_Load(&WSM_JoinStatus));
                iEvent->NotifyEvent(WSM_JoinEvent, WSM_JoinStatus, WSM_CallbackData);
            }
            DRV_WiFi->JoinCheck(DRV_Unit); // nudge driver if necessary
            return; // wait for state timer (even for JoinDone case it's more stable)
        }

        case WS_JoinDone:
            if (LTAtomic_Load(&WSM_LastJoinStatus) == kLTWiFi_JoinStatus_Failed) {
                LTLOG_SERVER("join.fail", "join failed: %lu", LT_Pu32(DRV_WiFi->GetDiscReasonCode(DRV_Unit)));
                Metrics.joinFail++;
                DRV_WiFi->JoinAbort(DRV_Unit);
                // WSM_RejoinCount = 0; // for debugging purposes - stops auto rejoin
                iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_JoinFailed, DRV_Unit);
                // Don't release sleep disallowance grant here if retrying
                // Let WS_Monitor handle backoff and then release it
                if (!WSM_RejoinCount) {
                    if (WSM_DisallowanceGrant) {
                        lt_reallowsleepmode(WSM_DisallowanceGrant);
                        WSM_DisallowanceGrant = 0;
                    }
                }
                NEXT(WS_Idle); // can initiate a rejoin
            } else {
                Metrics.joinSuccess++;
                LTLOG_SERVER("join.done", "%u, %u, %d, %u, %s, \"%s\", %lu, %lu, %lu, %lu",
                             WSM_ApInfo.channel, WSM_ApInfo.bandwidth, WSM_ApInfo.rssi, WSM_ApInfo.snr,
                             MacToOui(&WSM_ApInfo.bssid), WSM_ApInfo.ssid,
                             LT_Pu32(Metrics.joinSuccess), LT_Pu32(Metrics.joinFail),
                             LT_Pu32(Metrics.disconnects), LT_Pu32(WSM_RejoinCount));
                if (WSM_RejoinEnabled) WSM_RejoinCount = 1;
                iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_JoinDone, DRV_Unit);
                link_check_time = LTTime_Add(pCore->GetKernelTime(), LTTime_Milliseconds(2000));
                NEXT(WS_LinkCheck);
                // wifi join done, allow device to sleep
                if (WSM_DisallowanceGrant) {
                    lt_reallowsleepmode(WSM_DisallowanceGrant);
                    WSM_DisallowanceGrant = 0;
                }
            }
            break;

        case WS_LinkCheck:
            /* Logic for linkstate and where rejoin is handled:
             *  - Link connected:             GetDriverState() == kLTWiFi_DriverState_Connected && GetApInfo() == True
             *  - Link down - driver rejoins: GetDriverState() == kLTWiFi_DriverState_Connected && GetApInfo() == False
             *  - Link down - WSM rejoins:    GetDriverState() != kLTWiFi_DriverState_Connected
             */
            link_check_time = LTTime_Add(pCore->GetKernelTime(),
                                         LTTime_Seconds(WiFiConfig_LinkCheckSeconds));
            if (DRV_WiFi->GetDriverState(DRV_Unit) == kLTWiFi_DriverState_Connected) {
                LTWiFi_ApInfo info = {};
                if (DRV_WiFi->GetApInfo(DRV_Unit, &info)) {
                    INFO_GUARD(WSM_ApInfo = info);
                    if (!LTAtomic_Load(&WSM_LinkConnected)) {
                        // Link is Rejoin was handled by lower level driver
                        LTAtomic_Store(&WSM_LinkConnected, 1);
                        iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_LinkConnected, DRV_Unit);
                    }
                } else {
                    // Link is disconnected, but Rejoin is handled by lower level driver
                    if (LTAtomic_Load(&WSM_LinkConnected)) {
                        LTAtomic_Store(&WSM_LinkConnected, 0);
                        iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_LinkDisconnected, DRV_Unit);
                    }
                }
            } else {
                LTAtomic_Store(&WSM_LinkConnected, 0);
                Metrics.disconnects++;
                if (!WSM_RejoinCount) {
                    LTAtomic_Store(&WSM_Connected, 0);
                    iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_Disconnected, DRV_Unit);
                } else {
                    iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_LinkDisconnected, DRV_Unit);
                }
                // Do not rejoin from here, let it happen on next Monitor state.
            }
            NEXT(WS_Idle);
            break;

        case WS_Disconnect:
            if (LTAtomic_Load(&WSM_Connected)) {
                LTLOG_SERVER("disconnect", "%u, %u, %d, %u, %s, \"%s\"",
                             WSM_ApInfo.channel, WSM_ApInfo.bandwidth, WSM_ApInfo.rssi, WSM_ApInfo.snr,
                             MacToOui(&WSM_ApInfo.bssid), WSM_ApInfo.ssid);
            }
            LTAtomic_Store(&WSM_Connected, 0);
            LTAtomic_Store(&WSM_LinkConnected, 0);
            // Metrics.disconnects++;  // intentional disconnects should not be counted
            WSM_RejoinCount = 0;
            DRV_WiFi->Disconnect(DRV_Unit);
            // Always notify, even if not connected (allows user-side state changes)
            iEvent->NotifyEvent(WSM_StatEvent, kLTDeviceWiFi_Status_Disconnected, DRV_Unit);
            NEXT(WS_Idle);
            break;

        default:
            LTLOG_DEBUG("wsm.bad", "WSM BadState(%s)", WSM_StateNames_[state]);
            NEXT(WS_Idle);
            break;
        }
    }
}

/**
 * WSM_GotTimer - Captures timer events and passes them into the state machine.
 */
static void WSM_GotTimer(void * pClientData) {
    LT_UNUSED(pClientData); // currently there's only one timer.  Right now pClientData is the thread handle but it could be anything.
    /* LTThread hThread = VOIDPTR_TO_LTHANDLE(pClientData); */ /* when you want to cast pClientData to a handle, do this */
    WSM_DoNext(NULL);
}

/**
 * WSM_FreeInput - This ClientDataReleaseProc is used to free the input in the event that the
 *                 task proc was abandoned because the thread was terminated.
 */
static void WSM_FreeInput(LTThread_ReleaseReason releaseReason, void *pClientData) {
    if (kLTThread_ReleaseReason_TaskProcComplete != releaseReason) WSM_DestroyInput(pClientData);
}

/**
 * WSM_ProcessInput - This is currently just a shim to relay inputs provided
 * by requests into the state machine thread. It may be possible to eliminate
 * this in the future and go directly to WSM_DoNext. !!!???
 */
static void WSM_ProcessInput(void *data) {
    TR;
    WSM_DoNext(data);
}

/*******************************************************************************
 * API Functions
 ******************************************************************************/

static bool API_Init(void) {
    LTAtomic_Store(&WSM_Connected, 0);
    CLEAR(&WSM_ApInfo);
    WSM_Backoff = lt_createobject(LTNetBackoff);
    if (WSM_Backoff) {
        WSM_Backoff->API->Set(WSM_Backoff, LTTime_Seconds(kLTDeviceWiFiIdleState_MinDelay),
                                LTTime_Seconds(kLTDeviceWiFiIdleState_InitialBackoff),
                                LTTime_Seconds(kLTDeviceWiFiIdleState_MaxBackoff),
                                kLTNetBackoff_RetryForever);
    } else {
        LTLOG_STOMP("wsm.boff", "failed to create backoff object");
    }
    return true;
}

static void API_Quit(void) {
    LTAtomic_Store(&WSM_Connected, 0);
}

static bool API_GetDeviceInfo(LTWiFi_DriverInfo *info) {
    if (!LTAtomic_Load(&WSM_DriverUp)) return false;
    INFO_GUARD(*info = DRV_Info);
    return true;
}

static bool API_GetMacAddress(LTMacAddress *mac) {
    TR;
    if (!LTAtomic_Load(&WSM_DriverUp)) return false;
    INFO_GUARD(*mac = DRV_Info.mac_address); // fetched earlier from DriverInfo
    return true;
}

static bool API_SetMacAddress(LTMacAddress *mac) {
    if (!LTAtomic_Load(&WSM_DriverUp)) return false;
    bool result = false;
    INFO_GUARD(result = DRV_WiFi->SetOption(DRV_Unit, "mac_address", mac));
    if (result) INFO_GUARD(DRV_WiFi->GetDriverInfo(DRV_Unit, &DRV_Info));
    return result;
}

static void API_SetOption(char const *option, s32 value) {
    // ToDo: !!! convert to using debug flags
    LTLOG_DEBUG("set.opt", "Set option %s: %lu", option, LT_Pu32(value));
    if (lt_strcmp(option, "rejoin") == 0) {
        WSM_RejoinEnabled = value;
        WSM_RejoinCount = 0;
        if (!DEV_OpenSettings()) return;
        SET_Library->SetIntegerValue(SETTINGS_KEY_REJOIN, value);
        return;
    }
    if (lt_strcmp(option, "autojoin") == 0) {
        WSM_AutoJoin = value;
        if (!DEV_OpenSettings()) return;
        SET_Library->SetIntegerValue(SETTINGS_KEY_AUTOJOIN, value);
        return;
    }
    // This is a special option for testing purposes only. It forces a disconnect
    // external to the state machine to simulate AP link loss.
    if (lt_strcmp(option, "disconnect") == 0) {
        DRV_WiFi->Disconnect(DRV_Unit);
        return;
    }
    if (lt_strcmp(option, "setting_clear") == 0) {
        WSM_RejoinEnabled = 0;
        WSM_AutoJoin = 0;
        if (value) {
            if (!DEV_OpenSettings()) return;
            SET_Library->SetIntegerValue(SETTINGS_KEY_AUTOJOIN, WSM_AutoJoin);
            SET_Library->SetIntegerValue(SETTINGS_KEY_REJOIN, WSM_RejoinEnabled);
        }
        return;
    }
    // These options fallthru into the driver:
    if (lt_strcmp(option, "debug_mode") == 0) LTAtomic_Store(&WSM_DebugMode, ((u32)value & 0x3));
    if (lt_strcmp(option, "sniff") == 0) LTAtomic_Store(&WSM_SniffMode, (value >= 0));
    if (DRV_WiFi) {
        DRV_WiFi->SetOption(DRV_Unit, option, &value);
    }
    // pCore->ConsolePrint("WiFi: SetOption %s 0x%08x %u\n", option, value, value);
}

static u32 API_GetOption(char const *option) {
    if (lt_strcmp(option, "debug_mode") == 0) return LTAtomic_Load(&WSM_DebugMode);
    if (lt_strcmp(option, "rejoin"    ) == 0) return WSM_RejoinEnabled;
    if (lt_strcmp(option, "autojoin"  ) == 0) return WSM_AutoJoin;
    if (lt_strcmp(option, "DisconnectReason") == 0 && DRV_WiFi && DRV_WiFi->GetDiscReasonCode) {
        return DRV_WiFi->GetDiscReasonCode(DRV_Unit);
    }
    u32 value = 0;
    if (DRV_WiFi && DRV_WiFi->GetOption(DRV_Unit, option, &value)) return value;
    return 0;
}

static void API_OnStatusChange(LTDeviceWiFi_StatusCallback func, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void * clientData) {
    iEvent->RegisterForEvent(WSM_StatEvent, func, clientDataReleaseProc, clientData, true);
}

static void API_NoStatusChange(LTDeviceWiFi_StatusCallback func) {
    iEvent->UnregisterFromEvent(WSM_StatEvent, func);
}

static void API_Reset(void){
    TR;
    WSM_PostState(WS_Disconnect); // may be a no-op
    WSM_PostState(WS_Reset);
}

static void API_EnableRadio(bool state){
    TR;
    WSM_Input *req = WSM_CreateInput(state ? WS_Enable : WS_Disable);
    WSM_PostRequest(req);
}

static void API_GetMetrics(LTWiFi_Metrics *metrics, LT_SIZE sizeOfMetrics) {
    TR;
    DRV_WiFi->GetMetrics(DRV_Unit, metrics, sizeOfMetrics);
    if (metrics != NULL && sizeOfMetrics == sizeof(LTWiFi_Metrics)) {
        // Update our local Metrics for values that are zero'ed when read
        // There is a potential race condition with CheckMetrics() here but the consequences are negligible
        Metrics.joinSuccess += metrics->joinSuccess;  ///< New Auto-rejoin successes (optional)
        Metrics.joinFail += metrics->joinFail;        ///< New Auto-rejoin fails (optional)
        Metrics.disconnects += metrics->joinDisc;     ///< New Auto-rejoin disconnects (optional)
    }
}

static bool API_LoadApSettings(LTWiFi_ApInfo *ap) {
    if (!DEV_OpenSettings()) return false;
    LTString ssid  = ltstring_create("");
    LTString bssid = ltstring_create("");
    LTString sec   = ltstring_create("");
    u32 pass_len = kLTWiFi_Max_Pass + 1;
    u8 pass[pass_len];
    bool result = false;

    if (ap) {
        // Clear all fields, but save pass pointer
        char *pw = ap->pass;
        *ap = (LTWiFi_ApInfo){};
        ap->pass = pw;
    }
    if (SET_Library->GetStringValue(SETTINGS_KEY_SSID, &ssid)
        && SET_Library->GetBinaryValue(SETTINGS_KEY_PASS, pass, &pass_len) // modifies pass_len
        && SET_Library->GetStringValue(SETTINGS_KEY_SECURITY, &sec)
    ) {
        if (pass_len == 0) {
            LTLOG("pass.bad", "System settings AP password encoding is not valid");
        } else {
            result = true;
            if (ap) {
                lt_strncpyTerm(ap->ssid, ssid, kLTWiFi_Max_Ssid + 1);
                lt_memcpy(ap->pass, pass, pass_len);
                ap->security = DEV_StringToApSecurity(sec);
                if (SET_Library->GetStringValue(SETTINGS_KEY_BSSID, &bssid)) {
                    LTLOG("bssid", "%s", bssid);
                    MAC_Library->StringToMacAddress(bssid, &ap->bssid);
                }
            }
        }
    }

    // Autojoin if setting is 1 or if not set at all:
    s64 value = 0;
    WSM_AutoJoin = (!SET_Library->GetIntegerValue(SETTINGS_KEY_AUTOJOIN, &value) || value != 0);
    // Auto rejoin if setting is 1 or if not set at all:
    WSM_RejoinEnabled = (!SET_Library->GetIntegerValue(SETTINGS_KEY_REJOIN, &value) || value != 0);
    ltstring_destroy(sec);
    ltstring_destroy(ssid);
    ltstring_destroy(bssid);
    if (ap && !ap->ssid[0]) return false;
    return result;
}

static bool API_SaveApSettings(LTWiFi_ApInfo *ap) {
    if (!DEV_OpenSettings()) return false;
    if (ap && ap->security >= kLTWiFi_ApSecurity_Max) return false;
    if (ap && ap->pass == NULL) return false;

    bool bResult = false;
    if (!ap) {
        // ap == NULL: Delete all wifi/* settings
        bResult = SET_Library->DeleteSettingsWithPrefix(SETTINGS_PREFIX);
    } else if (ap->ssid[0] == '\0') {
        // ap->ssid == "": Delete wifi/{ssid,pass,security}
        bResult = (SET_Library->DeleteSetting(SETTINGS_KEY_SSID)
                   && SET_Library->DeleteSetting(SETTINGS_KEY_PASS)
                   && SET_Library->DeleteSetting(SETTINGS_KEY_SECURITY));
    } else {
        u32 pass_len = lt_strlen(ap->pass) + 1;
        bResult = (SET_Library->SetStringValue(SETTINGS_KEY_SSID, ap->ssid)
                   && SET_Library->SetBinaryValue(SETTINGS_KEY_PASS, (const u8*)ap->pass, pass_len)
                   && SET_Library->SetStringValue(SETTINGS_KEY_SECURITY, DEV_ApSecurityStrings[ap->security]));
    }
    API_LoadApSettings(&WSM_JoinSpec); // Reload in-memory cached variables
    return bResult;
}

static void API_ScanAps(LTWiFi_ScanSpec *spec, LTDeviceWiFi_ScanCallback *callback, void *callback_data){
    WSM_Input *req = WSM_CreateInput(WS_ScanStart); // req is cleared
    if (spec) req->scan_spec = *spec; // copy spec
    if (callback) {
        // Create an event used for the callback. It is created and registered
        // here, but because ScanAps is async, the unregister and destroy must
        // be part of the callback mechanism. See ScanEventProc().
        req->event = pCore->CreateEvent(&WSM_ScanEventArgs, WSM_ScanEventProc, WSM_ScanEventCompleteProc, NULL, NULL);
        iEvent->RegisterForEvent(req->event, callback, NULL, callback_data, false);
    }
    WSM_PostRequest(req);
}

static void API_JoinAp(LTWiFi_ApInfo *ap, LTDeviceWiFi_JoinCallback *callback, void *callback_data) {
    TR;
    // Avoid trying to join the same AP if already connected or in the process of connecting.
    WSM_PendingQueue.mutex->API->Lock(WSM_PendingQueue.mutex); // Use the same mutex as the queue for convenience
    bool dup = DEV_IsSameAp(ap, &WSM_JoinSpec) && (LTAtomic_Load(&WSM_Connected) || (LTAtomic_Load(&WSM_LastJoinStatus) >= kLTWiFi_JoinStatus_Starting && LTAtomic_Load(&WSM_LastJoinStatus) <= kLTWiFi_JoinStatus_Authenticated));
    WSM_PendingQueue.mutex->API->Unlock(WSM_PendingQueue.mutex);
    WSM_State state = WS_Join;
    if (dup) {
        LTWiFi_JoinStatus status = LTAtomic_Load(&WSM_LastJoinStatus);
        if (status < kLTWiFi_JoinStatus_Failed) {
            // if the join is in progress, move to WS_JoinCheck state
            state = WS_JoinCheck;
        }
        if (callback) {
            callback(status, callback_data);
            if (status >= kLTWiFi_JoinStatus_Failed) {
                return;
            }
        }
    }
    WSM_Input *req = WSM_CreateInput(state);
    req->join_spec = *ap;
    if (callback) {
        // Create an event used for the callback. It is created and registered
        // here, but because JoinAp is async, the unregister and destroy must
        // be part of the callback mechanism. See JoinEventProc().
        req->event = pCore->CreateEvent(&WSM_JoinEventArgs, WSM_JoinEventProc, NULL, NULL, NULL);
        iEvent->RegisterForEvent(req->event, callback, NULL, callback_data, false);
    }
    WSM_PostRequest(req);
}

static bool API_IsConnected(void) {
    TR;
    return (bool)(LTAtomic_Load(&WSM_Connected)); // atomic
}

static bool API_GetApInfo(LTWiFi_ApInfo *apInfo) {
    TR;
    bool linked = LTAtomic_Load(&WSM_LinkConnected);
    if (linked) {
        INFO_GUARD(*apInfo = WSM_ApInfo);
    }
    return linked;
}

static void API_Disconnect(void){
    TR;
    WSM_PostRequest(WSM_CreateInput(WS_Disconnect));
}

static bool API_StartAp(LTWiFi_ApInfo *ap) {
    // Experimental API !!! Needs to use WSM, not a direct driver call.
    if (!LTAtomic_Load(&WSM_DriverUp)) return false;
    return DRV_WiFi->ApStart(DRV_Unit, ap); // note: uses driver naming convention
}

static bool API_ReceiveFrame(LTDeviceUnit unit, LTWiFi_FrameRxCallback * pCallback, void * pClientData){
    if (!LTAtomic_Load(&WSM_DriverUp)) return false;
    return DRV_WiFi->ReceiveFrame(unit, pCallback, pClientData);
}

static bool API_TransmitFrames(LTDeviceUnit unit, const LTBufferChain *bufferChain) {
    if (!LTAtomic_Load(&WSM_DriverUp)) return false;
    return DRV_WiFi->TransmitFrames(unit,bufferChain);
}

static int API_IwPriv(int argc, const char **argv, void *priv) {
    if (!LTAtomic_Load(&WSM_DriverUp)) return LTDeviceWIFI_DRIVER_DOWN;
    if (DRV_WiFi->IwPriv == NULL) {
        return LTDeviceWIFI_NOT_SUPPORT;
    }
    return DRV_WiFi->IwPriv(argc, argv, priv);
}

static LTWiFi_DisconnectReason API_GetDiscReason(void) {
    if (DRV_Unit == 0 ||DRV_WiFi->GetDiscReasonCode == NULL) {
        return kLTWiFi_DisconnectReason_Unknown;
    }
    return DRV_WiFi->GetDiscReasonCode(DRV_Unit);
}

static LTWiFi_DisconnectReason API_GetLastJoinStatus(void) {
    if (DRV_Unit == 0 ||DRV_WiFi->GetLastJoinStatus == NULL) {
        return kLTWiFi_JoinStatus_Unknown;
    }
    return DRV_WiFi->GetLastJoinStatus(DRV_Unit);
}

static void API_GetWiFiDrvTxQStats(u8 ac, LTWiFi_DrvTxQStats *stats) {
    if (DRV_Unit == 0 || DRV_WiFi->GetLastJoinStatus == NULL) {
        return;
    }
    return DRV_WiFi->GetWiFiDrvTxQStats(DRV_Unit, ac, stats);
}
/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTDeviceWiFiImpl_LibFini(void) {
    //LTLOG_DEBUG("lib.quit", "wifi quit");
    WSM_Quit(); // destroys WSMThread, but might wait
    API_Quit();
    DEV_Quit();
#if USE_LOGGER_RECORDS
    if (pLogger) {
        pLogger->RemoveRecord(&Metrics_Record);
        pCore->CloseLibrary((LTLibrary*)pLogger));
    }
#endif
}

static bool LTDeviceWiFiImpl_LibInit(void) {
    pCore = LT_GetCore();
    iThread = lt_getlibraryinterface(ILTThread, pCore);
    iEvent  = lt_getlibraryinterface(ILTEvent,  pCore);
    #if USE_LOGGER_RECORDS
        pLogger = lt_openlibrary(LTSystemLogger);
        if (pLogger) {
    #endif
            CheckMetrics(kCheckMetricsReset); // Uses pLogger if USE_LOGGER_RECORDS enabled

            //LTLOG_DEBUG("lib.init", "wifi init");
            if (DEV_Init() && API_Init() && WSM_Init()) return true;
    #if USE_LOGGER_RECORDS
        }
    #endif

    //LTLOG("lib.init.fail", "wifi init failed");
    LTDeviceWiFiImpl_LibFini(); // not automatic
    return false;
}

static int LTDeviceWiFiImpl_Run(int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    //LTLOG_DEBUG("lib.run", "wifi lib argc: %d arg[0]: %s", argc, argv[0]);
    return 0;
}

static u32 LTDeviceWiFiImpl_GetNumDeviceUnits(void) {
    if (!DRV_Library) return 0;
    return DRV_Library->GetNumDeviceUnits();
}

static LTDeviceUnit LTDeviceWiFiImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNum) { LT_UNUSED(nDeviceUnitNum);
    /* because this is a device library, this function is required, but you can return 0 */
    /* FIXME: real stuff */
    return 0;
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceWiFi, LTDeviceWiFiImpl_Run, 1024)
    .GetDeviceInfo              = API_GetDeviceInfo,
    .GetMacAddress              = API_GetMacAddress,
    .SetMacAddress              = API_SetMacAddress,
    .SetOption                  = API_SetOption,
    .GetOption                  = API_GetOption,
    .OnStatusChange             = API_OnStatusChange,
    .NoStatusChange             = API_NoStatusChange,
    .Reset                      = API_Reset,
    .EnableRadio                = API_EnableRadio,
    .GetMetrics                 = API_GetMetrics,
    .LoadApSettings             = API_LoadApSettings,
    .SaveApSettings             = API_SaveApSettings,
    .ScanAps                    = API_ScanAps,
    .JoinAp                     = API_JoinAp,
    .IsConnected                = API_IsConnected,
    .GetApInfo                  = API_GetApInfo,
    .Disconnect                 = API_Disconnect,
    .StartAp                    = API_StartAp,
    .ReceiveFrame               = API_ReceiveFrame,
    .TransmitFrames             = API_TransmitFrames,
    .IwPriv                     = API_IwPriv,
    .GetDiscReason              = API_GetDiscReason,
    .GetLastJoinStatus          = API_GetLastJoinStatus,
    .GetWiFiDrvTxQStats         = API_GetWiFiDrvTxQStats,
LTLIBRARY_DEFINITION;
