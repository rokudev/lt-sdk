/*******************************************************************************
 *
 * DriverWiFi: Prototypical WiFi Driver Library
 * --------------------------------------------
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
/* Do not include LTDeviceWiFi.h to provide isolation from upper WiFi layers */

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <netpacket/packet.h>
#include <linux/if_ether.h>
#include <linux/if.h>
#include <linux/wireless.h>
#include <netinet/in.h>
#include <linux/if_tun.h>
#include <stdlib.h> // getenv()
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <net/if_arp.h>

DEFINE_LTLOG_SECTION("linux.drv.wifi");

/*******************************************************************************
 * LT Related Declarations
 ******************************************************************************/

static LTCore                       * pCore         = NULL;
static LTUtilityMacAddress          * MAC_Library   = NULL;
static ILTThread                    * iThread       = NULL;

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

typedef struct WiFiUnit {
    // Temporary unit variables (for this stub module)
    bool up;
    s8 up_count;
    s8 scan_count;
    s8 join_count;
    bool connected;
    LTWiFi_ApInfo ApJoinSpec;
    LTMutex *mutex;
    union {
        LTWiFi_JoinStatus_CB *join_callback;
        LTWiFi_ScanResults_CB *scan_callback;
    };
    int rawsock;
    char iface[IFNAMSIZ];
    int arptype_out;
    unsigned char mac[6];
    int rate;
    bool sniffer_running;
    LTThread sniff_LTThread;
    bool mesh_supported;
} WiFiUnit;

static WiFiUnit s_unit;

#define CLEAR(v) lt_memset(v, 0, sizeof(*v))

// Get rid of unit handles in this driver - we don't use them and they just present more failure surface
// In fact, the LTDriverWiFi should be modified to take a data pointer, not a handle.
#define GET_UNIT(wu, hu) WiFiUnit *wu = &s_unit; LT_UNUSED(wu); LT_UNUSED(hu);
#define GET_UNIT_RETURN(wu, hu, code) WiFiUnit *wu = &s_unit; LT_UNUSED(wu); LT_UNUSED(hu); LT_UNUSED(code);

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
static bool InitRawWiFi(WiFiUnit *unit);
static void ShutdownRawWiFi(WiFiUnit *unit);
static void SnifferMain(WiFiUnit *unit);

static void Sniff_TimerFired(void * pClientData) {
    WiFiUnit *unit = (WiFiUnit *)pClientData;
    /* kill the timer to make it one-shot */
    iThread->KillTimer(unit->sniff_LTThread, &Sniff_TimerFired, pClientData);
    SnifferMain(unit);
    if (unit->sniffer_running) {
        iThread->SetTimer(unit->sniff_LTThread, LTTime_Milliseconds(1), Sniff_TimerFired, NULL, pClientData);
    }
}

static bool Sniff_InitThread(void) {
    iThread->SetTimer(iThread->GetCurrentThread(), LTTime_Milliseconds(1), Sniff_TimerFired, NULL, (void *)&s_unit);
    return true;
}

static void Sniff_ExitThread(void) {
    iThread->KillTimer(iThread->GetCurrentThread(), Sniff_TimerFired, (void *)&s_unit);
}

static bool LinuxDriverWiFiImpl_LibInit(void) {
    pCore = LT_GetCore();
    iThread = lt_getlibraryinterface(ILTThread, pCore);

    if (!(MAC_Library = lt_openlibrary(LTUtilityMacAddress))) return false;
    s_unit.mutex = lt_createobject(LTMutex);
    /* Mesh specific parameters */
    s_unit.rawsock = -1;
    s_unit.rate = 2; /* default to 1Mbps if nothing is set */
    s_unit.mesh_supported = true;
    /* In order to use mesh on Linux:
     * 1) Get USB WiFi with active monitor support (MediaTek chipsets mt76x0u mt76x2u are suggested). For example: Alfa AWUS036ACM, AWUS036ACHM
     * 2) sudo ifconfig <wifi_iface_name> down && sudo iw <wifi_iface_name> set monitor active && sudo ifconfig <wifi_iface_name> up
     * 3) sudo LD_LIBRARY_PATH=. LTMINIMESHIF=<wifi_iface_name> ./ltrun LTSystemShell
     * 4) > ltopen LTShellWiFi
     * 5) > wifi mesh
     * */
    const char *intf = getenv("LTMINIMESHIF");
    if (!intf || lt_strlen(intf) >= IFNAMSIZ) {
        LTLOG("mesh.info", "LTMINIMESHIF environment variable is not set, assuming no mesh support\n");
        s_unit.mesh_supported = false;
    }
    if (s_unit.mesh_supported) {
        lt_memcpy(s_unit.iface, intf, lt_strlen(intf)+1);
        InitRawWiFi(&s_unit);
    }
    return true;
}

static void LinuxDriverWiFiImpl_LibFini(void) {
    if(s_unit.rawsock >= 0) {
        ShutdownRawWiFi(&s_unit);
    }
    lt_destroyobject(s_unit.mutex);
    s_unit.mutex = NULL;
    if (MAC_Library) {
        lt_closelibrary(MAC_Library);
        MAC_Library = NULL;
    }
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
    //See note above the GET_UNIT define
    return hUnit;
}

/* no explicit DestroyDeviceUnitHandle function to match CreateDeviceUnitHandle because handle
   interfaces have a built in Destroy() and LT_GetCore()->DestroyHandle always works. */

/*******************************************************************************
 * LTDriverWiFi API Functions
 ******************************************************************************/
static bool DriverWiFi_GetOption(LTDeviceUnit h_unit, char const *option, void *value);

static bool DriverWiFi_GetDriverInfo(LTDeviceUnit h_unit, LTWiFi_DriverInfo *info) {
    GET_UNIT_RETURN(unit, h_unit, false);
    LTMacAddress mac;
    DriverWiFi_GetOption(h_unit,"mac_address", &mac);
    if (!MAC_Library->IsZero(&mac)) lt_memcpy(&DriverInfo.mac_address, &mac, sizeof(LTMacAddress));
    *info = DriverInfo;
    return true;
}

static bool DriverWiFi_SetDriverState(LTDeviceUnit h_unit, LTWiFi_DriverState state) {
    GET_UNIT_RETURN(unit, h_unit, false);
    switch (state) {
    case kLTWiFi_DriverState_Up:
        unit->up_count = 1;
        pCore->ConsolePrint("--- LTDriverWiFi driver up ---\n");
        break;
    case kLTWiFi_DriverState_Down:
        unit->up_count = 0;
        unit->connected = false;
        pCore->ConsolePrint("--- LTDriverWiFi driver down ---\n");
        break;
    case kLTWiFi_DriverState_ResetChip:
    case kLTWiFi_DriverState_ResetDriver:
        unit->up = false;
        unit->up_count = 0;
        unit->connected = false;
        pCore->ConsolePrint("--- LTDriverWiFi reset ---\n");
        break;
    default:
        return false;
    }
    return true;
}

static LTWiFi_DriverState DriverWiFi_GetDriverState(LTDeviceUnit h_unit) {
    GET_UNIT_RETURN(unit, h_unit, kLTWiFi_DriverState_Unknown);
    if (unit->up_count > 0) {
        return unit->connected ? kLTWiFi_DriverState_Connected : kLTWiFi_DriverState_Up;
    }
    return kLTWiFi_DriverState_Down; // be more specific, e.g. indicate reset state
}

static bool DriverWiFi_SetOption(LTDeviceUnit h_unit, char const *option, void *value) {
    GET_UNIT_RETURN(unit, h_unit, false);
    LTLOG("mac.set", "run");
    if (lt_strcmp(option, "mac_address") == 0) {
        DriverInfo.mac_address = *(LTMacAddress *)value;
        return true;
    }
    return false;
}

static bool DriverWiFi_GetOption(LTDeviceUnit h_unit, char const *option, void *value) {
    GET_UNIT_RETURN(unit, h_unit, false);
    LTMacAddress mac;
    if (lt_strcmp(option, "mac_address") == 0) {
        lt_memcpy(&mac.octet,unit->mac,6);
        *(LTMacAddress *)value = mac;
        return true;
    }
    if (lt_strcmp(option, "channels_1-14") == 0) {
        *((u32 *)value) = (1<<11)-1; // Simulate channel 1-11 available
        return true;
    }
    return false;
}

static void DriverWiFi_GetMetrics(LTDeviceUnit h_unit, LTWiFi_Metrics *metrics, LT_SIZE sizeOfMetrics) {
    GET_UNIT(unit, h_unit);
    lt_memset(metrics, 0, sizeOfMetrics);
}

/** Scan **********************************************************************/

static LTWiFi_ScanSpec *ScanSpec;

static void DriverWiFi_ScanStart(LTDeviceUnit h_unit, LTWiFi_ScanSpec *spec, LTWiFi_ScanResults_CB callback) {
    GET_UNIT(unit, h_unit);
    ScanSpec = spec;
    unit->scan_count = 0;
    unit->scan_callback = callback;
}

static void DriverWiFi_ScanCheck(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);
    unit->scan_count++;
    //LTLOG_DEBUG("scan", "scan %d\n", unit->scan_count);
    // Fake a delay in getting results;
    if (unit->scan_count < 4) return;
    // See if it's done with scan:
    if (unit->scan_count > 5) {
        unit->scan_callback(h_unit, NULL);
        return;
    }
    // Otherwise, get an AP and return it:
    LTWiFi_ApInfo *tmp_ap = ApInfos[unit->scan_count-4];
    bool ssid_ok = !ScanSpec->ssid || lt_strcmp(ScanSpec->ssid, tmp_ap->ssid) == 0;
    u8 chan = 0;
    if (ScanSpec->channel) chan = ScanSpec->channel[0]; // just use first one
    bool chan_ok = !chan || chan == tmp_ap->channel;
    if (ssid_ok && chan_ok) {
        unit->scan_callback(h_unit, tmp_ap);
    }
}

static void DriverWiFi_ScanAbort(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);
}

/** Join **********************************************************************/

static void DriverWiFi_JoinStart(LTDeviceUnit h_unit, LTWiFi_ApInfo *ap_spec, LTWiFi_JoinStatus_CB callback) {
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
    if (!spec || !spec->ssid[0]) return kLTWiFi_JoinStatus_Failed; // sanity check, should never happen
    char *ssid = spec->ssid;
    char *pass = spec->pass ? spec->pass : NULL;

    if (unit->join_count <= 2) return kLTWiFi_JoinStatus_Starting;
    if (lt_strcmp(ssid, "netflix") != 0 && lt_strcmp(ssid, "open") != 0) {
        return kLTWiFi_JoinStatus_Failed;
    }

    if (unit->join_count <= 3) return kLTWiFi_JoinStatus_Associating;
    if (unit->join_count <= 4) return kLTWiFi_JoinStatus_Associated;
    if (lt_strcmp(ssid, "open") != 0 && (!pass || pass[0] == 0)) {
        unit->connected = true;
        return kLTWiFi_JoinStatus_Success; // open AP
    }
    if (unit->join_count <= 5) return kLTWiFi_JoinStatus_Authenticating;
    if (lt_strcmp(pass, "12345678") != 0) return kLTWiFi_JoinStatus_Failed;
    if (unit->join_count <= 6) return kLTWiFi_JoinStatus_Authenticated;
    unit->connected = true;
    return kLTWiFi_JoinStatus_Success;
}

static void DriverWiFi_JoinCheck(LTDeviceUnit h_unit) {
    LTWiFi_JoinStatus status = join_status_check(h_unit);
    if (status >= kLTWiFi_JoinStatus_Associating) {
        GET_UNIT(unit, h_unit);
        if (unit->join_callback) unit->join_callback(h_unit, status);
        if (status >= kLTWiFi_JoinStatus_Failed) unit->join_callback = NULL;
    }
}

static void DriverWiFi_JoinAbort(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);
    unit->connected = false;
    unit->join_count = 8;
    if (unit->join_callback) {
        unit->join_callback(h_unit, kLTWiFi_JoinStatus_Aborted);
        unit->join_callback = NULL;
    }
}

static bool DriverWiFi_GetApInfo(LTDeviceUnit h_unit, LTWiFi_ApInfo *ap) {
    GET_UNIT_RETURN(unit, h_unit, false);
    *ap = unit->ApJoinSpec;
    ap->rssi = -42;
    ap->bssid = ApFakeBssid;
    ap->channel = 1;
    return unit->connected;
}

static bool DriverWiFi_GetLinkState(LTHandle h_unit) {
    // Return true if STA is connected to the AP.
    return (DriverWiFi_GetDriverState(h_unit) == kLTWiFi_DriverState_Connected);
}

static void DriverWiFi_Disconnect(LTDeviceUnit h_unit) {
    GET_UNIT(unit, h_unit);
    unit->connected = false;
}

static bool DriverWiFi_ApStart(LTDeviceUnit h_unit, LTWiFi_ApInfo *ap) {
    GET_UNIT_RETURN(unit, h_unit, false);
    LT_UNUSED(ap);
    return false; // Not supported
}

/** Transfer ******************************************************************/

static bool DriverWiFi_ReceiveFrame(LTDeviceUnit h_unit, LTWiFi_FrameRxCallback * pCallback, void * pClientData)
{
    LT_UNUSED(h_unit);
    LT_UNUSED(pCallback);
    LT_UNUSED(pClientData);
    LTLOG_DEBUG("rx.noimpl", "not implemented: ReceiveFrame");
    return true;
}

static bool DriverWiFi_TransmitFrames(LTDeviceUnit h_unit, const LTBufferChain *bufferChain)
{
    LT_UNUSED(h_unit);
    LT_UNUSED(bufferChain);
    LTLOG_DEBUG("noimpl", "not implemented: TransmitFrames");
    return true;
}
/** Sniff used for MiniMesh and Promiscuous Monitor ***************************/

static LTWiFi_SniffCallback *SniffCallback;

static void DriverWiFi_SniffCallback(char *data, int data_len, int flags, void *unused)
{
    LT_UNUSED(unused);
    LTWiFi_FrameInfo info;
    info.frame = (LTWiFi_Header*)data;
    info.size  = data_len;
    info.rssi  = flags;
    if (SniffCallback) SniffCallback(&info); // arg valid only during callback
}
// Very basic Radiotap stuff
struct LTWiFiRadiotapHeader {
    u8   Version;
    u8   Padding;
    u16  Length;
    u32  FieldsPresentMask;
};

enum {
    FIELD_RADIOTAP_TSFT = 0,
    FIELD_RADIOTAP_FLAGS = 1,
    FIELD_RADIOTAP_RATE = 2,
    FIELD_RADIOTAP_CHANNEL = 3,
    FIELD_RADIOTAP_FHSS = 4,
    FIELD_RADIOTAP_DBM_ANTSIGNAL = 5,
};

static const u8 FieldRadioTapSizes[] = {
    8,
    1,
    1,
    4,
    2,
    1
};

static void SnifferMain(WiFiUnit *unit)
{
    uint8_t tmpbuf[4096];
    uint8_t outbuf[4096];
    fd_set fds;
    s32 rssi=-255;
    struct timeval tv;
    int total_capture_length;

    if (!unit->sniffer_running) return;
    for (int ii=0; ii<10; ii++) {
        FD_ZERO(&fds);
        FD_SET(unit->rawsock, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 1000;
        if (select(unit->rawsock + 1, &fds, NULL, NULL, &tv) < 0) {
            if (errno == EINTR) {
                LTLOG("error.select","select failed");
                return;
            }

            if (!FD_ISSET(unit->rawsock, &fds)) return;
        }
        total_capture_length = read(unit->rawsock, tmpbuf, sizeof(tmpbuf));
        struct LTWiFiRadiotapHeader * HeaderRadioTap = (struct LTWiFiRadiotapHeader *) tmpbuf;
        //LTLOG_DEBUG("got.data", "len %d radiotap present 0x%x", total_capture_length, HeaderRadioTap->FieldsPresentMask);
        /* Extract RSSI from the Radiotap header */
        LT_ASSERT((HeaderRadioTap->FieldsPresentMask & (1 << FIELD_RADIOTAP_DBM_ANTSIGNAL)));
        if (HeaderRadioTap->FieldsPresentMask & (1 << FIELD_RADIOTAP_DBM_ANTSIGNAL)) {
            s8 *antsignal = (s8*)HeaderRadioTap + sizeof(struct LTWiFiRadiotapHeader);
            if (HeaderRadioTap->FieldsPresentMask & (1 << 31)) {
                antsignal += 4;
            }
            for (u8 i=0; i<FIELD_RADIOTAP_DBM_ANTSIGNAL; i++) {
                if (HeaderRadioTap->FieldsPresentMask & (1 << i)) {
                    antsignal += FieldRadioTapSizes[i];
                }
            }
            rssi = *antsignal;
        } else {
            LTLOG("rssi.error", "No RSSI info, in Radiotap header!");
            return;
        }
        if (HeaderRadioTap->Length <= 0 || HeaderRadioTap->Length >= total_capture_length) return;
        lt_memcpy(outbuf,tmpbuf + HeaderRadioTap->Length, total_capture_length - HeaderRadioTap->Length);
        DriverWiFi_SniffCallback((char *)outbuf, total_capture_length - HeaderRadioTap->Length, rssi,0);
    }
}

static bool InitRawWiFi(WiFiUnit *unit)
{
    LT_ASSERT(unit->iface != NULL);
    struct ifreq if_rq;
    struct iwreq iw_rq;
    struct packet_mreq p_mr;
    struct sockaddr_ll sa_ll;

    if (lt_strlen(unit->iface) >= sizeof(if_rq.ifr_name)) {
        LTLOG("if.name", "Interface name too long: %s\n", unit->iface);
        return false;
    }
    if ((unit->rawsock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
        LTLOG("linux.pfpacket.err", "socket(PF_PACKET) failed");
        unit->rawsock = -1;
        return false;
    }
    lt_memset(&if_rq, 0, sizeof(if_rq));
    lt_memcpy(if_rq.ifr_name, unit->iface, lt_strlen(unit->iface)+1);
    if (ioctl(unit->rawsock, SIOCGIFINDEX, &if_rq) < 0) {
        LTLOG("if.index.error", "Interface %s: SIOCGIFINDEX failed",unit->iface);
        goto error_iface;
    }
    lt_memset(&sa_ll, 0, sizeof(sa_ll));
    sa_ll.sll_family = AF_PACKET;
    sa_ll.sll_ifindex = if_rq.ifr_ifindex;
    sa_ll.sll_protocol = htons(ETH_P_ALL);
    if (ioctl(unit->rawsock, SIOCGIFHWADDR, &if_rq) < 0) {
        LTLOG("if.hwaddr.error", "Interface %s: SIOCGIFHWADDR failed",unit->iface);
        goto error_iface;
    }
    lt_memset(&iw_rq, 0, sizeof(struct iwreq));
    lt_memcpy(iw_rq.ifr_name, unit->iface, lt_strlen(unit->iface)+1);
    ioctl(unit->rawsock, SIOCGIWMODE, &iw_rq);
    if (iw_rq.u.mode != IW_MODE_MONITOR) {
        LTLOG("if.mon.mode", "Interface should be put in active monitor mode, do before running LT: sudo iw <wifi_interface> set monitor active");
        goto error_iface;
    }
    if ((if_rq.ifr_flags | IFF_UP | IFF_BROADCAST | IFF_RUNNING) != if_rq.ifr_flags) {
        if_rq.ifr_flags |= IFF_UP | IFF_BROADCAST | IFF_RUNNING;
        if (ioctl(unit->rawsock, SIOCSIFFLAGS, &if_rq) < 0) {
            LTLOG("if.iflags.error", "ioctl(SIOCSIFFLAGS) failed");
            goto error_iface;
        }
    }
    if (bind(unit->rawsock, (struct sockaddr *) &sa_ll, sizeof(sa_ll)) < 0) { //-V641
        LTLOG("if.bind.error", "Interface %s: bind(ETH_P_ALL) failed",unit->iface);
        goto error_iface;
    }
    if (ioctl(unit->rawsock, SIOCGIFHWADDR, &if_rq) < 0) {
        LTLOG("if.hwaddr.error", "Interface %s: ioctl(SIOCGIFHWADDR) failed",unit->iface);
        goto error_iface;
    }
    /* Initialize unit's MAC and address family */
    lt_memcpy(unit->mac, (unsigned char *) if_rq.ifr_hwaddr.sa_data, 6);
    unit->arptype_out = if_rq.ifr_hwaddr.sa_family;
    if (if_rq.ifr_hwaddr.sa_family != 803) {
        LTLOG("if.link.error", "Unsupported hardware link type %4d ", if_rq.ifr_hwaddr.sa_family);
        goto error_iface;
    }
    /* enable promiscuous mode */
    lt_memset(&p_mr, 0, sizeof(p_mr));
    p_mr.mr_ifindex = sa_ll.sll_ifindex;
    p_mr.mr_type = PACKET_MR_PROMISC;
    if (setsockopt(unit->rawsock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &p_mr, sizeof(p_mr)) < 0) {
        LTLOG("if.promisc.error", "setsockopt(PACKET_MR_PROMISC) failed");
        goto error_iface;
    }
    return true;
error_iface:
    close(unit->rawsock);
    unit->rawsock = -1;
    return false;
}

static void ShutdownRawWiFi(WiFiUnit *unit)
{
    close(unit->rawsock);
    unit->rawsock = -1;
}

static void DriverWiFi_SniffFrames(LTDeviceUnit h_unit, LTWiFi_SniffCallback callback)
{
    GET_UNIT(unit, h_unit);
    if (unit->mesh_supported) {
        if (callback) {
            if(unit->rawsock < 0) {
                InitRawWiFi(unit);
            }
            SniffCallback = callback;
            unit->sniffer_running = true;
            unit->sniff_LTThread = pCore->CreateThread("WiFi sniffer ");
            iThread->SetStackSize(unit->sniff_LTThread, 16 * 1024);
            iThread->Start(unit->sniff_LTThread, Sniff_InitThread, Sniff_ExitThread);

        } else {
            if(unit->rawsock >= 0) {
                unit->sniffer_running = false;
                iThread->Destroy(unit->sniff_LTThread);
                ShutdownRawWiFi(unit);
            }
            // SniffCallback stays assigned in case it gets called before unreg happens
        }
    }
}
static int SendRawFrame(WiFiUnit *unit, u8 *data, int count)
{
    if (!unit->mesh_supported) return -1;
    int ret;
    unsigned char tmpbuf[4096];
    unsigned char rate;
    struct LTWiFiRadiotapHeader *rt_h;
    u8 radiotap_header_bytes[12];
    lt_memset(&radiotap_header_bytes, 0, sizeof(radiotap_header_bytes));
    rt_h = (struct LTWiFiRadiotapHeader *)radiotap_header_bytes;
    rt_h->Version = 0;
    rt_h->Padding = 0;
    rt_h->Length = sizeof(radiotap_header_bytes);
    rt_h->FieldsPresentMask = (1 << 2 | 1 << 15); /* rate, TX flags */
    radiotap_header_bytes[10] = 0x8; /* 0x8 no ACK required */ /* 0x10 pre configured sequence number */

    if(unit->rawsock < 0) {
        InitRawWiFi(unit);
    }
    if (unit->rawsock < 0) {
        LTLOG("error.tx.raw", "Raw injection is not open!");
        return 0;
    }
    if ((unsigned) count > sizeof(tmpbuf) - 22) return -1;
    rate = unit->rate;
    radiotap_header_bytes[8] = rate;
    lt_memcpy(tmpbuf, radiotap_header_bytes, sizeof(radiotap_header_bytes));
    lt_memcpy(tmpbuf + sizeof(radiotap_header_bytes), (u8 *)data, count);
    count += sizeof(radiotap_header_bytes);
    int try_again = 0;
    do {
        unit->mutex->API->Lock(unit->mutex);
        ret = write(unit->rawsock, tmpbuf, count);
        unit->mutex->API->Unlock(unit->mutex);
        if (ret < 0) {
            if ((errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS
            || errno == ENOMEM) && (try_again < 10)) {
                usleep(10000);
                try_again++;
            } else {
                LTLOG("inject", "write failed");
                return -1;
            }
        } else {
            try_again = 0;
        }
    } while(try_again);
    return 0;
}

static int DriverWiFi_SendFrame(LTDeviceUnit h_unit, LTWiFi_Frame *frame)
{
    LT_UNUSED(h_unit);
    LT_UNUSED(frame);
    GET_UNIT_RETURN(unit, h_unit, 0);
    int count = frame->size;
    return SendRawFrame(unit, (u8*)&frame->header, count);
}

static LTWiFi_DisconnectReason DriverWiFi_GetDiscReasonCode(LTDeviceUnit h_unit) {
    LT_UNUSED(h_unit);
    return kLTWiFi_DisconnectReason_Unknown;
}

/*******************************************************************************
 * LTDriverWiFi Interface
 ******************************************************************************/

static void LinuxDriverWiFi_OnDestroyHandle(LTHandle h_unit) {
    GET_UNIT(unit, h_unit);
    /* this is where you could do something if you wanted to do something
       when the client destroys the handle.  You don't have to free anythign here because
       you just stored a pointer in the handle, you didn't store a pointer to something you allocated in the handle.
    */
}

define_LTLIBRARY_INTERFACE(LTDriverWiFi, LinuxDriverWiFi_OnDestroyHandle)
    .GetDriverInfo  = DriverWiFi_GetDriverInfo,
    .GetDriverState = DriverWiFi_GetDriverState,
    .SetDriverState = DriverWiFi_SetDriverState,
    .SetOption      = DriverWiFi_SetOption,
    .GetOption      = DriverWiFi_GetOption,
    .GetMetrics     = DriverWiFi_GetMetrics,
    .ScanStart      = DriverWiFi_ScanStart,
    .ScanCheck      = DriverWiFi_ScanCheck,
    .ScanAbort      = DriverWiFi_ScanAbort,
    .JoinStart      = DriverWiFi_JoinStart,
    .JoinCheck      = DriverWiFi_JoinCheck,
    .JoinAbort      = DriverWiFi_JoinAbort,
    .GetApInfo      = DriverWiFi_GetApInfo,
    .GetLinkState   = DriverWiFi_GetLinkState,
    .Disconnect     = DriverWiFi_Disconnect,
    .ApStart        = DriverWiFi_ApStart,
    .TransmitFrames = DriverWiFi_TransmitFrames,
    .ReceiveFrame   = DriverWiFi_ReceiveFrame,
    .SniffFrames    = DriverWiFi_SniffFrames,
    .SendFrame      = DriverWiFi_SendFrame,
    .GetDiscReasonCode = DriverWiFi_GetDiscReasonCode,
LTLIBRARY_DEFINITION;
