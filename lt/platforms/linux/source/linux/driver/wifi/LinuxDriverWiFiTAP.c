/*******************************************************************************
 *
 * LinuxDriverWiFiLwIP: WiFi Driver that makes use of TAP device to
 * send / receive ethernet frames.
 * ---------------------------------------------------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/wifi/LTDriverWiFi.h>
/* Do not include LTDeviceWiFi.h here */

#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static LTCore *pCore;
static LTUtilityMacAddress *MAC_Library;

#define PR LT_GetCore()->ConsolePrint
#define PV(var) LT_GetCore()->ConsolePrint("======= Variable %s: %d\n", #var, var)

/*******************************************************************************
 *
 * WiFi Driver Abstract Interface
 * ------------------------------
 *
 * This interface provides isolation between the WiFi state machine and vendor
 * specific WiFi driver and WPA supplicant implementations.
 *
 * IMPORTANT RULE: This is no blocking or timing at this driver interface.
 * That makes it possible for the LTDeviceWifi layer to handle all time
 * related conditions, including timeouts. Please heed this rule.
 *
 * This interface is called only by the WiFi state machine which coordinates
 * and synchronizes access to these features to minimize locking and reduce
 * complex state management within the vendor device implementation. Only
 * short-period blocking is allowed. The WiFi state machine will also prevent
 * overlapped calls that would cause driver conflicts or malfunctions. For
 * example, a scan will not be called while a join is being processed.
 *
 ******************************************************************************/
static int tap_fd = -1;

typedef struct WiFiUnit {
    // Temporary unit variables (for this stub module)
    bool up;
    s8 up_count;
    s8 scan_count;
    s8 join_count;
    bool connected;
    LTWiFi_ApInfo ApJoinSpec;
    LTMutex *mutex;
    LTWiFi_FrameRxCallback *rx_frame;
    void *rx_client_data;
    union {
        LTWiFi_JoinStatus_CB *join_callback;
        LTWiFi_ScanResults_CB *scan_callback;
    };
} WiFiUnit;

static WiFiUnit s_unit;

#define CLEAR(v) lt_memset(v, 0, sizeof(*v))

#define GET_UNIT(wu, hu) \
    WiFiUnit *wu = *(WiFiUnit **)pCore->GetHandlePrivateData(hu); \
    if (!wu) return;

#define GET_UNIT_RETURN(wu, hu, code) \
    WiFiUnit *wu = *(WiFiUnit **)pCore->GetHandlePrivateData(hu); \
    if (!wu) return code;

/** Stub Test Data ************************************************************/

static LTWiFi_DriverInfo DriverInfo = {
    .vendor   = "ABC",
    .product  = "IoT-for-All",
    .version  = "0.0.1",
    .updated  = "2012-1-5",
    .mac_address.octet = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 },
};

static LTWiFi_ApInfo ApInfo1 = {
    .ssid      = "netflix",
    .security  = kLTWiFi_ApSecurity_Wpa2,
    .channel   = 6,
    .rssi      = -52,
    .bandwidth = 20,
};

static LTWiFi_ApInfo ApInfo2 = {
    .ssid      = "FBI-Van",
    .security  = kLTWiFi_ApSecurity_Open,
    .channel   = 11,
    .rssi      = -32,
    .bandwidth = 40,
};

static LTWiFi_ApInfo *ApInfos[2] = {
    &ApInfo1,
    &ApInfo2
};

static LTMacAddress ApFakeBssid = {{0x20, 0x10, 0x00, 0x60, 0x50, 0x40}};

/** Driver Library Root Interface ********************************************/

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDriverWiFi, LinuxDriverWiFi);
/*
*   This special macro is defined in LTTypes.h.  It provides a declaration
*   of a  hidden library root interface of type LTDriverLibrary which carries the
*   prototype functions immediately below.  The first argument "LTDriverWiFi tells
*   LTCore that this driver library contains implementation(s) of LTDriverWiFi and
*   it also slaves the library root interface version to the LTDriverWiFi version
*   it was compiled against.
*
*   In total, this macro does 5 things:
*    1. Creates a root library interface for the driver library
*    2. Types it as an LTDriverLibrary, giving it the driver type and the prototype functions thereof
*    3. Slaves the root interface version number to that of the driver interface number this library is
*       compiled against so the Device library opening this driver library can find out the version of
*       the driver interface(s) the library will supply before calling into the library to create unit handles
*    4. Allows for more than one driver library to exist at runtime for the same device class.
*    5. Communicates the driver mapping to LTCore so that at runtime When LTDeviceWiFi wants to know
*        what driver libraries supply WiFi drivers, LTCore can tell it.
*
*/
#define MTU_SIZE 1500

static void *tap_read_loop(void *arg) {
    LT_UNUSED(arg);

    u8 buffer[MTU_SIZE];
    for (;;) {
        ssize_t read_size = read(tap_fd, &buffer, sizeof(buffer));
        if (read_size <= 0)
            continue;

        LT_ASSERT(read_size != -1);

        LTBufferChain bufferChain = { .next = NULL, .buffer = lt_malloc(read_size), .size = read_size, .bytesUsed = read_size };
        if (bufferChain.buffer) {
            lt_memcpy(bufferChain.buffer, &buffer, read_size);
            s_unit.mutex->API->Lock(s_unit.mutex);
            if (s_unit.rx_frame == NULL || !s_unit.rx_frame(&bufferChain, s_unit.rx_client_data)) {
                lt_free(bufferChain.buffer);
            }
            s_unit.mutex->API->Unlock(s_unit.mutex);
        }
    }

    return NULL;
}

static pthread_t tap_read_thread;

static bool LinuxDriverWiFiImpl_LibInit(void) {
    pCore = LT_GetCore();
    if (!(MAC_Library = lt_openlibrary(LTUtilityMacAddress))) return false;
    s_unit.mutex = lt_createobject(LTMutex);

    const char *intf = getenv("LTNETWORKIF");
    if (!intf || strlen(intf) >= IFNAMSIZ) {
        PR("Please set LTNETWORKIF environment variable\n");
        return false;
    }

    char *tunfd = "/dev/net/tun";
    tap_fd = open(tunfd, O_RDWR);

    LT_ASSERT(tap_fd >= 0);
    if (tap_fd < 0) {
        return false;
    }

    struct ifreq ifr;
    lt_memset(&ifr, 0, sizeof(ifr));

    // DO NOT REMOVE NO_PI, it really does strip a header that we don't need
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strcpy(ifr.ifr_name, intf);

    int ret = ioctl(tap_fd, TUNSETIFF, (void *) &ifr);
    LT_ASSERT(ret >= 0);
    if (ret == -1) {
        close(tap_fd);
        tap_fd = -1;
        return false;
    }

    // Get/set the MAC address
    ret = ioctl(tap_fd, SIOCGIFHWADDR, &ifr);
    LT_ASSERT(ret >= 0);
    if (ret == -1) {
        close(tap_fd);
        tap_fd = -1;
        return false;
    }

    lt_memcpy(DriverInfo.mac_address.octet, ifr.ifr_hwaddr.sa_data, 6);
    DriverInfo.mac_address.octet[5]++;

    if (pthread_create(&tap_read_thread, NULL, &tap_read_loop, NULL)) {
        close(tap_fd);
        tap_fd = -1;
        return false;
    }

    return true;
}

static void LinuxDriverWiFiImpl_LibFini(void) {
    lt_destroyobject(s_unit.mutex);
    s_unit.mutex = NULL;
    if (MAC_Library) pCore->CloseLibrary((LTLibrary*)MAC_Library);
}

static u32 LinuxDriverWiFiImpl_GetNumDeviceUnits(void) {
    return 1; /* this driver library only knows how to control one physical WiFi hardware block */
}

static LTDriverWiFi s_LTDriverWiFi;
/* This is a forward declaration of the LTDriverWiFi interface instance that gets defined at the end of this file
   by the macro define_LTLIBRARY_INTERFACE(LTDriverWiFi).  The variable name has to be s_LTDriverWiFi because
   that is what the macro defines. */

static LTDeviceUnit LinuxDriverWiFiImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    /* we only manage one physical WiFi hardware block (unit) so nDeviceUnitNumber must be 0 */
    if (nDeviceUnitNumber > 0) return (LTDeviceUnit)0;

    /* units are not the same as unit handles.  Multiple handles can exist for the same unit.
       In practice, LTDeviceWiFi is our only client and probably will only call us once per
       unit number, so likely a 1:1 mapping, but not necessarily. Since the client who calls create
       is the owner of the resource, and up to them to destroy the handle, we won't manage lists
       of handles or anything like that here. */

    LTDeviceUnit hUnit = LT_GetCore()->CreateHandle((LTInterface *)&s_LTDriverWiFi, sizeof(WiFiUnit *));
    if (hUnit) {
        /* got a handle back with enough room to store a pointer to our data */
        WiFiUnit ** ppUnit = (WiFiUnit **)LT_GetCore()->GetHandlePrivateData(hUnit);
        *ppUnit = &s_unit;
    }
    return hUnit;
}

/* no explicit DestroyDeviceUnitHandle function to match CreateDeviceUnitHandle because handle
   interfaces have a built in Destroy() and LT_GetCore()->DestroyHandle always works. */

/*******************************************************************************
 * API Functions
 ******************************************************************************/

static bool SetOption(LTDeviceUnit h_unit, char const *option, void *value) {
    GET_UNIT_RETURN(unit, h_unit, false);
    if (lt_strcmp(option, "mac_address") == 0) {
        DriverInfo.mac_address = *(LTMacAddress *)value;
        return true;
    }
    return false;
}

static bool GetOption(LTDeviceUnit h_unit, char const *option, void *value) {
    GET_UNIT_RETURN(unit, h_unit, false);
    if (lt_strcmp(option, "mac_address") == 0) {
        *(LTMacAddress *)value = DriverInfo.mac_address;
        return true;
    }
    return false;
}

/** Driver ********************************************************************/

static bool GetDriverInfo(LTDeviceUnit h_unit, LTWiFi_DriverInfo *info) {
    GET_UNIT_RETURN(unit, h_unit, false);
    *info = DriverInfo;
    return true;
}

static bool SetDriverState(LTDeviceUnit h_unit, LTWiFi_DriverState state) {
    GET_UNIT_RETURN(unit, h_unit, false);
    switch (state) {
    case kLTWiFi_DriverState_Up:
        unit->up_count = 1;
        break;
    case kLTWiFi_DriverState_Down:
        unit->up_count = 0;
        unit->connected = false;
        break;
    case kLTWiFi_DriverState_ResetChip:
    case kLTWiFi_DriverState_ResetDriver:
        unit->up = false;
        unit->up_count = 0;
        unit->connected = false;
        break;
    default:
        return false;
    }
    return true;
}

static LTWiFi_DriverState GetDriverState(LTDeviceUnit h_unit) {
    GET_UNIT_RETURN(unit, h_unit, kLTWiFi_DriverState_Unknown);
    if (unit->up_count > 0) {
        return unit->connected ? kLTWiFi_DriverState_Connected : kLTWiFi_DriverState_Up;
    }
    return kLTWiFi_DriverState_Down; // be more specific, e.g. indicate reset state
}

/** Scan **********************************************************************/

LTWiFi_ScanSpec *ScanSpec;

static void ScanStart(LTDeviceUnit h_unit, LTWiFi_ScanSpec *spec, LTWiFi_ScanResults_CB callback) {
    GET_UNIT(unit, h_unit);
    ScanSpec = spec;
    unit->scan_count = 0;
    unit->scan_callback = callback;
}

static void ScanCheck(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);
    unit->scan_count++;
    if (unit->scan_count < 4) return;
    if (unit->scan_count > 5) {
        unit->scan_callback(h_unit, NULL);
        return;
    }
    LTWiFi_ApInfo *tmp_ap = ApInfos[unit->scan_count-4];
    bool ssid_ok = !ScanSpec->ssid || lt_strcmp(ScanSpec->ssid, tmp_ap->ssid) == 0;
    bool chan_ok = !ScanSpec->channel || ScanSpec->channel[0] == tmp_ap->channel;
    if (ssid_ok && chan_ok) {
        unit->scan_callback(h_unit, tmp_ap);
    }
}

///!!!    if (unit->scan_count > 5) return -1; // done

static void ScanAbort(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);
}

/** Join **********************************************************************/

static void JoinStart(LTDeviceUnit h_unit, LTWiFi_ApInfo *ap_spec, LTWiFi_JoinStatus_CB callback) {
    GET_UNIT(unit, h_unit);
    unit->ApJoinSpec = *ap_spec;
    unit->join_count = 0;
    unit->connected = false;
    unit->join_callback = callback;
}

static LTWiFi_JoinStatus join_status_check(LTDeviceUnit h_unit) {
    GET_UNIT_RETURN(unit, h_unit, kLTWiFi_JoinStatus_Unknown);
    unit->join_count++;

    LTWiFi_ApInfo *spec = &unit->ApJoinSpec;
    if (!spec || !spec->ssid) return kLTWiFi_JoinStatus_Failed; // sanity check, should never happen
    char *ssid = spec->ssid;
    char *pass = spec->pass ? spec->pass : NULL;

    if (unit->join_count <= 2) return kLTWiFi_JoinStatus_Starting;
    if (lt_strcmp(ssid, "netflix") != 0 && lt_strcmp(ssid, "open") != 0) {
        return kLTWiFi_JoinStatus_Failed;
    }

    if (unit->join_count <= 3) return kLTWiFi_JoinStatus_Associating;
    if (unit->join_count <= 4) return kLTWiFi_JoinStatus_Associated;
    if (!pass || pass[0] == 0) {
        unit->connected = true;
        return kLTWiFi_JoinStatus_Success; // open AP
    }
    if (unit->join_count <= 5) return kLTWiFi_JoinStatus_Authenticating;
    if (lt_strcmp(pass, "12345678") != 0) return kLTWiFi_JoinStatus_Failed;
    if (unit->join_count <= 6) return kLTWiFi_JoinStatus_Authenticated;
    unit->connected = true;
    return kLTWiFi_JoinStatus_Success;
}

static void JoinCheck(LTDeviceUnit h_unit) {
    LTWiFi_JoinStatus status = join_status_check(h_unit);
    if (status >= kLTWiFi_JoinStatus_Associating) {
        GET_UNIT(unit, h_unit);
        if (unit->join_callback) unit->join_callback(h_unit, status);
        if (status >= kLTWiFi_JoinStatus_Failed) unit->join_callback = NULL;
    }
}

static void JoinAbort(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);
    unit->connected = false;
}

static bool GetApInfo(LTDeviceUnit h_unit, LTWiFi_ApInfo *ap) {
    GET_UNIT_RETURN(unit, h_unit, false);
    *ap = unit->ApJoinSpec;
    ap->rssi = -42;
    ap->bssid = ApFakeBssid;
    return unit->connected;
}

static void Disconnect(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);
    unit->connected = false;
}

/** Transfer ******************************************************************/

static bool DRV_ReceiveFrame(LTDeviceUnit h_unit, LTWiFi_FrameRxCallback * pCallback, void * pClientData)
{
    GET_UNIT_RETURN(unit, h_unit, false);

    unit->mutex->API->Lock(unit->mutex);
    unit->rx_frame = pCallback;
    unit->rx_client_data = pClientData;
    unit->mutex->API->Unlock(unit->mutex);
    return true;
}

static bool DRV_TransmitFrames(LTDeviceUnit h_unit, const LTBufferChain *bufferChain)
{
    GET_UNIT_RETURN(unit, h_unit, false);

    if (tap_fd < 0) {
        return false;
    }
    while (bufferChain) {
        int err = write(tap_fd, bufferChain->buffer, bufferChain->bytesUsed);
        LT_ASSERT(err = bufferChain->bytesUsed); LT_UNUSED(err);
        bufferChain = bufferChain->next;
    }
    return true;
}

/*******************************************************************************
 * LTDriverWiFi Interface
 ******************************************************************************/

static void BlankDriverWiFi_OnDestroyHandle(LTHandle h_unit) {
    GET_UNIT(unit, h_unit);
    /* this is where you could do something if you wanted to do something
       when the client destroys the handle.  You don't have to free anythign here because
       you just stored a pointer in the handle, you didn't store a pointer to something you allocated in the handle.
    */
}

define_LTLIBRARY_INTERFACE(LTDriverWiFi, BlankDriverWiFi_OnDestroyHandle)
    .GetDriverInfo  = GetDriverInfo,
    .GetDriverState = GetDriverState,
    .SetDriverState = SetDriverState,
    .SetOption      = SetOption,
    .GetOption      = GetOption,
    .ScanStart      = ScanStart,
    .ScanCheck      = ScanCheck,
    .ScanAbort      = ScanAbort,
    .JoinStart      = JoinStart,
    .JoinCheck      = JoinCheck,
    .JoinAbort      = JoinAbort,
    .GetApInfo      = GetApInfo,
    .Disconnect     = Disconnect,
    .TransmitFrames = DRV_TransmitFrames,
    .ReceiveFrame   = DRV_ReceiveFrame
LTLIBRARY_DEFINITION;
