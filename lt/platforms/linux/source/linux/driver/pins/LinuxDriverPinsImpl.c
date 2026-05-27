/*******************************************************************************
 * platforms/linux/source/Linux/driver/pins/LinuxDriverPinsImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/stat.h>

#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/pins/LTDevicePins.h>

DEFINE_LTLOG_SECTION("linux.drv.pins");

#define LTDEVICEPINS_DO_DLOG     0
#if     LTDEVICEPINS_DO_DLOG
#define DLOG                    LTLOG
#else
#define DLOG                    LTLOG_LOGNULL
#endif

/* Interrupt trigger type */
typedef u8 LinuxGPIO_Trigger;
enum LinuxGPIO_Trigger {
    kLinuxGPIO_Trigger_Disabled         = 0,
    kLinuxGPIO_Trigger_Rising           = 1,
    kLinuxGPIO_Trigger_Falling          = 2,
    kLinuxGPIO_Trigger_Both             = 3,
};

typedef u8 LinuxGPIO_Direction;
enum LinuxGPIO_Direction {
    kLinuxGPIO_Direction_Input          = 0,
    kLinuxGPIO_Direction_Output         = 1,
    kLinuxGPIO_Direction_Unknown        = LT_U8_MAX,
};

enum { kMaxFilePathLength = 64 };

/* (no input-only pins on this platform) */
/* (no output-only pins on this platform) */

/* IRQ structure. Maps a trigger and callback to the pin handle it is requested with */
typedef struct {
    LTList_Node node;
    LTDeviceUnit hPin;
    LTDevicePin_PinConfiguration_Trigger trigger;
    LTDevicePin_IRQCallback *pCallback;
    void *pClientData;
} IRQ;

/* A GPIO Pin instance (Device Unit), generated in LibInit, one for each pin called out in the DeviceConfig: */
typedef struct {
    const char          *pName;         /* Name, given in the "name" element in the DeviceConfig
                                           NOTE: LTDeviceConfig must remain open for the data
                                           dereferenced by this pointer to remain valid.      */
    LTDevicePin_PinType  eType;         /* Type of the device pin: input or bidirectional           */
    LTInterface         *pInterface;    /* Which interface to supply (through gethandleinterface()) */
    LTMutex             *mutex;         /* mutual exclusion for reference counting and IRQ          */
    int                  pollFd;        /* File descriptor for poll() */
    int                  rwFd;          /* File descriptor for reading and setting */
    LTList               irqs;          /* Callback for interrupt, one entry per pin handle */
    bool                 bActiveHigh;   /* If GPIO is active high */
    bool                 bPrevValue;    /* prev value to allow detection of rising or falling edge */
    u8                   nPin;          /* the GPIO pin in question                                 */
} PinInstance;

/* Container for all the pin instances */
typedef struct DeviceUnits {
    PinInstance *pDeviceUnits;      /* Pointer into the heap where the Device Units are stored (as an array) */
    u32          nNumDeviceUnits;   /* How many Device Units this Driver supplies                            */
} DeviceUnits;

/** static Variables ***********************************************************/
static DeviceUnits s_DeviceUnits;
static LTCore*     s_pCore;
static ILTThread*  s_iThread;
static LTThread    s_PollingThread;
static int         s_efd;
static const char const s_gpioDevfsPath[] = "/sys/class/gpio";
static const       ILTDriverPins_InputBank s_ILTDriverPins_InputBank;
static const       ILTDriverPins_BidirectionalBank s_ILTDriverPins_BidirectionalBank;

/** Utility Functions *********************************************************/
/* Return a pointer to the Device Unit instance, given the Device Unit handle: */
#define RESERVE_INSTANCE_OR_RETURN(function, retVal) PinInstance **ppInstance = LT_GetCore()->ReserveHandlePrivateData(hPin); \
                                                     if (!ppInstance) {                                                       \
                                                        LTLOG_YELLOWALERT(#function ".bad.hpin", "Invalid pin handle");       \
                                                        return retVal;                                                        \
                                                     }                                                                        \
                                                     PinInstance *pInstance = *ppInstance;                                    \
                                                     if (!pInstance) {                                                        \
                                                        LTLOG_YELLOWALERT(#function ".bad.dpin", "Invalid pin data");         \
                                                        LT_GetCore()->ReleaseHandlePrivateData(hPin, ppInstance);             \
                                                        return retVal;                                                        \
                                                     }
#define RELEASE_INSTANCE()                           LT_GetCore()->ReleaseHandlePrivateData(hPin, ppInstance)

static void NotifyIRQPollThread(void) {
    // eventfd needs at least 8 bytes (max 0xFFFFFFFFFFFFFFFE) which it will add as an integer to an internalcounter.
    // Further writes will block until a read if the internal counter reaches 0xFFFFFFFFFFFFFFFE
    // Read will block if the internal counter is zero,
    // so writing a 1 here to ensure read after write and further writes before read do not block.
    // A read will reset the counter (non-semaphore mode)
    long long val = 1;
    int res = write(s_efd, &val, sizeof(val));
    if (res < 0) {
        LTLOG_REDALERT("notify.fail", "Failed to notify IRQ poll thread, res = %d, Err: %s", res, strerror(errno));
    }
}

static IRQ* LinuxDriverPinsImpl_GetHandleIRQ(LTDeviceUnit hPin) {
    RESERVE_INSTANCE_OR_RETURN(gethandleirq, NULL);
    IRQ *irq;
    LTList_ForEach(node, &pInstance->irqs) {
        irq = (IRQ*)node;
        if (irq->hPin == hPin) {
            RELEASE_INSTANCE();
            return irq;
        }
    } LTList_EndForEach;
    RELEASE_INSTANCE();
    return NULL;
}

/** Driver implementation *********************************************************/
/* Using pins via sysfs. Pins are individually accessible */
static u32 LinuxDriverPinsImpl_GetNumPins(LTDeviceUnit hPin) { LT_UNUSED(hPin); return 1; }

/* Get file descriptor for a given GPIO */
static int LinuxDriverPinsImpl_GetFd(u32 nPin, bool rw) {
    char gpio_value_path[kMaxFilePathLength];
    int nCount;

    nCount = lt_snprintf(gpio_value_path, kMaxFilePathLength, "%s/gpio%u/value", s_gpioDevfsPath, nPin);
    if (nCount <= 0 || nCount >= kMaxFilePathLength) return -1;

    int openFlags = (rw) ? O_RDWR : O_RDONLY;

    return open(gpio_value_path, openFlags);
}

/* Read a GPIO pin.  Return 1 for high, 0 for low, LT_U32_MAX for an invalid handle: */
static u32 LinuxDriverPinsImpl_Read(LTDeviceUnit hPin) {
    RESERVE_INSTANCE_OR_RETURN(read, LT_U32_MAX);
    int nCount;
    char value;

    if (pInstance->rwFd < 0) {
        LTLOG_REDALERT("rd.fd.inv", "Invalid FD");
        RELEASE_INSTANCE();
        return LT_U32_MAX;
    }

    nCount = pread(pInstance->rwFd, &value, 1, 0);
    if (nCount != 1) {
        LTLOG_REDALERT("rd.fail", "Read failed for gpio \"%s\". %s", pInstance->pName, strerror(errno));
        RELEASE_INSTANCE();
        return LT_U32_MAX;
    }
    RELEASE_INSTANCE();
    return (value == '1') ? 1 : 0;
}

/* Set/Clear a GPIO pin. */
static void LinuxDriverPinsImpl_Set(LTDeviceUnit hPin, u32 pinBits) {
    RESERVE_INSTANCE_OR_RETURN(set, );
    int nCount;
    char value = (pinBits & 1) ? '1':'0';

    if (pInstance->rwFd < 0) {
        LTLOG_REDALERT("wr.fd.inv", "Invalid FD");
        RELEASE_INSTANCE();
        return;
    }

    nCount = pwrite(pInstance->rwFd, &value, 1, 0);
    if (nCount != 1) {
        LTLOG_REDALERT("set.wrerr", "Error writing to gpio  \"%s\". %s", pInstance->pName, strerror(errno));
    }
    RELEASE_INSTANCE();
}

/* Set trigger edge.
 * This should be set only if pin is input and interrupt is wanted on it.
 * Linux's GPIO driver will not allow output on the pin unless edge is set to "none" */
static bool LinuxDriverPinsImpl_SetEdge(u32 nPin, LinuxGPIO_Trigger edge) {
    char strFilePath[kMaxFilePathLength];
    int fd;
    int nCount;
    const char strEdgeNone[]      = "none";
    const char strEdgeRising[]    = "rising";
    const char strEdgeFalling[]   = "falling";
    const char strEdgeBoth[]      = "both";
    const char* strEdge;
    u32 strEdgeLen;

    switch(edge) {
        case kLinuxGPIO_Trigger_Disabled:
            strEdge = strEdgeNone;
            break;
        case kLinuxGPIO_Trigger_Rising:
            strEdge = strEdgeRising;
            break;
        case kLinuxGPIO_Trigger_Falling:
            strEdge = strEdgeFalling;
            break;
        case kLinuxGPIO_Trigger_Both:
            strEdge = strEdgeBoth;
            break;
        default:
            LTLOG_REDALERT("bug.edge", "Unimplemented trigger type");
            return false;
    }

    //If the edge file is present, set the irq edge trigger to "both"
    nCount = lt_snprintf(strFilePath, kMaxFilePathLength, "%s/gpio%u/edge", s_gpioDevfsPath, nPin);
    if (access(strFilePath, F_OK) == 0) {
        DLOG("export.edge", "gpio %u has edge file, setting to \"%s\"", nPin, strEdge);

        fd = open(strFilePath, O_WRONLY | O_TRUNC);
        if (fd < 0) {
            LTLOG_YELLOWALERT("export.f.err", "Failed to open gpio %u, %s", nPin, strerror(errno));
            return false;
        }

        strEdgeLen = lt_strlen(strEdge);
        nCount = write(fd, strEdge, strEdgeLen);
        close(fd);
        if ((u32)nCount != strEdgeLen) {
            LTLOG_YELLOWALERT("export.edge.wrerr", "Error setting edge trigger for gpio  %u (to %s). %s", nPin, strEdge, strerror(errno));
            return false;
        }
    }
    return true;
}

/* Export a pin by writing pin number to the export file
 * Skip if pin already exported */
static bool LinuxDriverPinsImpl_Export(u32 nPin) {
    char strFilePath[kMaxFilePathLength];
    int fd;
    int nCount;
    char strGpio[16];
    int gpioStrLen;

    nCount = lt_snprintf(strFilePath, kMaxFilePathLength, "%s/gpio%u", s_gpioDevfsPath, nPin);
    if (access(strFilePath, F_OK) == 0) {
        LTLOG_DEBUG("export.exists", "gpio %u already exported.", nPin);
        return true;
    }

    // Export the GPIO interface.
    nCount = lt_snprintf(strFilePath, kMaxFilePathLength, "%s/export", s_gpioDevfsPath);
    if (nCount <= 0 || nCount >= kMaxFilePathLength) {
        LTLOG_REDALERT("export.filepath.err", "Failed to set export file path for gpio %u", nPin);
        return false;
    }

    fd = open(strFilePath, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        LTLOG_REDALERT("export.f.err", "Failed to open export, %s", strerror(errno));
        return false;
    }

    gpioStrLen = lt_snprintf(strGpio, 16, "%u", nPin);
    nCount = write(fd, strGpio, gpioStrLen);
    close(fd);

    if (nCount != gpioStrLen) {
        LTLOG_REDALERT("export.wrerr", "Error exporting gpio  %u. %s", nPin, strerror(errno));
        return false;
    }
    return true;
}

/* Set Active level on pin */
static void LinuxDriverPinsImpl_SetActiveLow(u32 nPin, bool isActiveLow) {
    char gpio_al_path[kMaxFilePathLength];
    int fd;
    int nCount;
    char value;

    value = isActiveLow ? '1' : '0';
    nCount = lt_snprintf(gpio_al_path, kMaxFilePathLength, "%s/gpio%u/active_low", s_gpioDevfsPath, nPin);
    if (nCount <= 0 || nCount >= kMaxFilePathLength) {
        LTLOG_REDALERT("al.filepath.err", "Failed to set file path for gpio %d", nPin);
        return;
    }

    fd = open(gpio_al_path, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        LTLOG_REDALERT("al.f.err", "Failed to open gpio %u, %s", nPin, strerror(errno));
        return;
    }

    nCount = write(fd, &value, 1);
    if (nCount != 1) {
        LTLOG_REDALERT("al.wrerr", "Error writing to gpio %u. %s", nPin, strerror(errno));
    }
    close(fd);
}

/* Enable an interrupt for a given Device Unit: */
static void LinuxDriverPinsImpl_EnableIRQ(LTDeviceUnit hPin, LTDevicePin_PinConfiguration_Trigger trigger,
                                            LTTime tmDebounce, LTDevicePin_IRQCallback *pISRCallback, void *pISRClientData) {
    LT_UNUSED(tmDebounce); // debounce not implemented.
    RESERVE_INSTANCE_OR_RETURN(enableirq, );

    pInstance->mutex->API->Lock(pInstance->mutex);
    IRQ *irq = LinuxDriverPinsImpl_GetHandleIRQ(hPin);
    if (irq) {
        if (irq->pCallback) {
            LTLOG_YELLOWALERT("irq.overwrite", "Replacing existing IRQ handler");
        }
        irq->trigger = trigger;
        irq->pCallback = pISRCallback;
        irq->pClientData = pISRClientData;
    }

    // Always set edge to both to allow both rising and falling edges to propagate through to other IRQ handles.
    LinuxDriverPinsImpl_SetEdge(pInstance->nPin, kLinuxGPIO_Trigger_Both);
    pInstance->mutex->API->Unlock(pInstance->mutex);
    NotifyIRQPollThread();
    RELEASE_INSTANCE();
}

/* Disable an interrupt for a given Device Unit: */
static void LinuxDriverPinsImpl_DisableIRQ(LTDeviceUnit hPin) {
    RESERVE_INSTANCE_OR_RETURN(disableirq, );
    pInstance->mutex->API->Lock(pInstance->mutex);
    IRQ *irq = LinuxDriverPinsImpl_GetHandleIRQ(hPin);
    if (irq) {
        irq->pCallback = NULL;
        irq->pClientData = NULL;
    }
    pInstance->mutex->API->Unlock(pInstance->mutex);
    NotifyIRQPollThread();
    RELEASE_INSTANCE();
}

/* Handle destruction (used by all ILTDriverPins interfaces): */
static void OnDestroyHandle(LTHandle hPin) {
    RESERVE_INSTANCE_OR_RETURN(destroy, );
    pInstance->mutex->API->Lock(pInstance->mutex);
    IRQ *irq = LinuxDriverPinsImpl_GetHandleIRQ(hPin);
    if (irq) {
        LTList_Remove(&irq->node);
        lt_free(irq);
    }
    pInstance->mutex->API->Unlock(pInstance->mutex);
    NotifyIRQPollThread();
    RELEASE_INSTANCE();
}

static bool LinuxGPIOSetDirectionLogFlags(int nPin, LinuxGPIO_Direction dir, u32 nLogFlags) {
    const char instr[] = "in";
    const char outstr[] = "out";
    const char *pDirStr = NULL;
    char gpioDirPath[kMaxFilePathLength];
    int fd;
    int nCount;
    u32 nDirStrLen;

    switch (dir) {
        case kLinuxGPIO_Direction_Input:
            pDirStr = instr;
            nDirStrLen = sizeof(instr) - 1;
            break;
        case kLinuxGPIO_Direction_Output:
            LinuxDriverPinsImpl_SetEdge(nPin, kLinuxGPIO_Trigger_Disabled);
            pDirStr = outstr;
            nDirStrLen = sizeof(outstr) - 1;
            break;

        default:
            LT_GetCore()->Log(s_pLTLOG_Section, "setdir.unk", nLogFlags, "Unknown GPIO direction %u", dir);
            return false;
    };

    nCount = lt_snprintf(gpioDirPath, kMaxFilePathLength, "%s/gpio%u/direction", s_gpioDevfsPath, nPin);
    DLOG("setdir", "GPIO set direction %s", gpioDirPath);
    if (nCount <= 0 || nCount >= kMaxFilePathLength) {
        LT_GetCore()->Log(s_pLTLOG_Section, "setdir.err", nLogFlags, "Failed to create GPIO %d direction file path", nPin);
        return false;
    }

    // Read value from file.
    fd = open(gpioDirPath, O_WRONLY | O_TRUNC);

    if (fd < 0) {
        LT_GetCore()->Log(s_pLTLOG_Section, "setdir.open.failed", nLogFlags, "Failed to open %s", gpioDirPath);
        return false;
    }
    // Write the direction
    nCount = write(fd, pDirStr, nDirStrLen);

    close(fd);

    return (u32)nCount == nDirStrLen;
}

static bool LinuxGPIOSetDirection(int nPin, LinuxGPIO_Direction dir) {
    return LinuxGPIOSetDirectionLogFlags(nPin, dir,
                                         kLTCore_LogFlags_LogTypeRedAlert |
                                         kLTCore_LogFlags_LogToConsole |
                                         kLTCore_LogFlags_LogToServer);
}

/*******************************************************************************
 * ILTDriverPins_InputBank                                                    */

static void LinuxDriverInputPins_ConfigurePullType(LTDeviceUnit hPin,
                                                   LTDevicePin_PinConfiguration_PullType pullType) {
    LT_UNUSED(pullType);
    RESERVE_INSTANCE_OR_RETURN(configurepulltype, );
    LT_UNUSED(pInstance);
    RELEASE_INSTANCE();
}

define_LTLIBRARY_INTERFACE(ILTDriverPins_InputBank, OnDestroyHandle)
    .GetNumPins          = LinuxDriverPinsImpl_GetNumPins,
    .ConfigurePullType   = LinuxDriverInputPins_ConfigurePullType,
    .Read                = LinuxDriverPinsImpl_Read,
    .EnableIRQ           = LinuxDriverPinsImpl_EnableIRQ,
    .DisableIRQ          = LinuxDriverPinsImpl_DisableIRQ,
LTLIBRARY_DEFINITION;


/*******************************************************************************
 * ILTDriverPins_OutputBank                                                   */

static void LinuxDriverOutputPins_ConfigureOutputType(LTDeviceUnit hPin,
                                                      LTDevicePin_PinConfiguration_OutputType outputType) {
    LT_UNUSED(outputType);

    RESERVE_INSTANCE_OR_RETURN(configureoutputtype, );
    LT_UNUSED(pInstance);
    RELEASE_INSTANCE();
}

define_LTLIBRARY_INTERFACE(ILTDriverPins_OutputBank, OnDestroyHandle)
    .GetNumPins          = LinuxDriverPinsImpl_GetNumPins,
    .ConfigureOutputType = LinuxDriverOutputPins_ConfigureOutputType,
    .Set                 = LinuxDriverPinsImpl_Set,
    .Read                = LinuxDriverPinsImpl_Read,
LTLIBRARY_DEFINITION;


/*******************************************************************************
 * ILTDriverPins_BidirectionalBank                                            */

static void LinuxDriverBidirectionalPins_ConfigureAsInput(LTDeviceUnit hPin, LTDevicePin_PinConfiguration_PullType pullType) {
    LT_UNUSED(pullType);
    RESERVE_INSTANCE_OR_RETURN(configureasinput, );
    DLOG("pin.dir.in", "Set %s as input", pInstance->pName);
    LinuxGPIOSetDirection(pInstance->nPin, kLinuxGPIO_Direction_Input);
    RELEASE_INSTANCE();
}

static void LinuxDriverBidirectionalPins_ConfigureAsOutput(LTDeviceUnit hPin, LTDevicePin_PinConfiguration_OutputType outputType) {
    LT_UNUSED(outputType);
    RESERVE_INSTANCE_OR_RETURN(configureasoutput, );
    DLOG("pin.dir.out", "Set %s as output", pInstance->pName);
    LinuxGPIOSetDirection(pInstance->nPin, kLinuxGPIO_Direction_Output);
    RELEASE_INSTANCE();
}

define_LTLIBRARY_INTERFACE(ILTDriverPins_BidirectionalBank, OnDestroyHandle)
    .GetNumPins          = LinuxDriverPinsImpl_GetNumPins,
    .ConfigureAsOutput   = LinuxDriverBidirectionalPins_ConfigureAsOutput,
    .ConfigureAsInput    = LinuxDriverBidirectionalPins_ConfigureAsInput,
    .Set                 = LinuxDriverPinsImpl_Set,
    .Read                = LinuxDriverPinsImpl_Read,
    .EnableIRQ           = LinuxDriverPinsImpl_EnableIRQ,
    .DisableIRQ          = LinuxDriverPinsImpl_DisableIRQ,
LTLIBRARY_DEFINITION;


/*******************************************************************************
 * ILTDriverPin                                                               */

static u32 LinuxDriverPinsImpl_GetNumDeviceUnits(void) { return s_DeviceUnits.nNumDeviceUnits; }

static LTDeviceUnit LinuxDriverPinsImpl_CreateDeviceUnitHandle(u32 nDeviceUnitIndex) {
    PinInstance **ppInstance = NULL;
    if (nDeviceUnitIndex >= s_DeviceUnits.nNumDeviceUnits) return 0;        /* invalid device unit index */
    PinInstance *pInstance = s_DeviceUnits.pDeviceUnits + nDeviceUnitIndex; /* the PinInstance for the index */
    LTDeviceUnit hDevice = LT_GetCore()->CreateHandle(pInstance->pInterface, sizeof(PinInstance *));
    do {
        if (!hDevice) {
            LTLOG_REDALERT("cduh.create.no", "Unable to create handle");
            break;
        }

        ppInstance = (PinInstance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
        if (!ppInstance) {
            LTLOG_REDALERT("cduh.hdl.inv", "Created handle is DOA");
            break;
        }

        *ppInstance = pInstance;    /* assign the PinInstance to the private data */
        pInstance->mutex->API->Lock(pInstance->mutex);
        IRQ *irq = lt_malloc(sizeof(IRQ));
        if (!irq) {
            LTLOG_REDALERT("cduh.hdl.inv", "Failed to create IRQ. OOM");
            break;
        }
        irq->hPin = hDevice;
        irq->trigger = kLTDevicePin_PinConfiguration_Trigger_BothEdges;
        irq->pCallback = NULL;
        irq->pClientData = NULL;
        LTList_AddTail(&pInstance->irqs, &irq->node);
        pInstance->mutex->API->Unlock(pInstance->mutex);
        LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
        return hDevice;
    } while (0);

    // Failed
    if (ppInstance) LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
    LT_GetCore()->DestroyHandle(hDevice);
    return 0;
}

/* Pin name lookup support: */
static bool LinuxDriverPinsImpl_GetBankNameFromUnitNumber(u32 nDeviceUnitIndex, char const **ppPinBankNameToSet) {
    if (nDeviceUnitIndex >= s_DeviceUnits.nNumDeviceUnits) return false;
    PinInstance *pInstance = s_DeviceUnits.pDeviceUnits + nDeviceUnitIndex;
    *ppPinBankNameToSet = pInstance->pName;
    return true;
}

static bool LinuxDriverPinsImpl_GetUnitNumberFromBankName(char const *pPinBankName, u32 *pIndexToSet) {
    PinInstance *pInstance = s_DeviceUnits.pDeviceUnits;
    for (u32 n = 0; n < s_DeviceUnits.nNumDeviceUnits; ++n, ++pInstance)
        if (!lt_strcmp(pPinBankName, pInstance->pName)) {
            *pIndexToSet = n;
            return true;
        }
    return false;
}

static bool LinuxDriverPinsImpl_GetBankTypeFromUnitNumber(u32 nDeviceUnitIndex, LTDevicePin_PinType *pPinType) {
    if (nDeviceUnitIndex >= s_DeviceUnits.nNumDeviceUnits) return false;
    PinInstance *pInstance = s_DeviceUnits.pDeviceUnits + nDeviceUnitIndex;
    if (pPinType) *pPinType = pInstance->eType;
    return true;
}

/*******************************************************************************
 * Device Unit initialization and configuration                               */

/* Data structure containing the entire context for initialization: */
typedef struct LinuxDriverPinsConfigContext {
    LTDeviceConfig       *pDeviceConfig;
    PinInstance          *pNextInstanceToInitialize;       /* pointer to next place in s_DeviceUnits.pDeviceUnits to place a Device Unit instance */
    u32                   driverConfigOffset;              /* The Device Config offset for this Driver */
    u32                   nInitialNumDeviceUnitInstances;  /* number of Device units in the Device Config for this Driver Library */
} LinuxDriverPinsConfigContext;

LT_STATIC_ASSERT_SIZE_32_64(LinuxDriverPinsConfigContext, 16, 24);

/* Reclaim all resources used by the context: */
static void DestroyConfigurationContext(LinuxDriverPinsConfigContext *pContext) {
    if (pContext) {
        lt_closelibrary(pContext->pDeviceConfig);
        lt_free(pContext);
    }
}

/* Allocate memory for the context, and fill in as much as possible: */
static LinuxDriverPinsConfigContext *CreateConfigurationContext(void) {
    LinuxDriverPinsConfigContext *pContext = lt_malloc(sizeof(LinuxDriverPinsConfigContext));
    if (   !pContext
        || !(pContext->pDeviceConfig                  = lt_openlibrary(LTDeviceConfig))
        || !(pContext->driverConfigOffset             = pContext->pDeviceConfig->GetDriverSection("LTDevicePins", "LinuxDriverPins"))
        || !(pContext->nInitialNumDeviceUnitInstances = pContext->pDeviceConfig->GetNumDeviceUnits(pContext->driverConfigOffset))) { DestroyConfigurationContext(pContext); pContext = NULL; }
    return pContext;
}

/* Configure one Device Unit instance from the configuration information in the Device Config.
   If the Device Unit instance is successfully configured, advance the instance pointer to the next instance,
   and increment the number of available Device Units.  Otherwise, leave the pointer and count alone, and the
   next iteration of the callback will rewrite the current instance (if that happens, ConfigureDeviceUnits()
   reallocs the Device Unit instance array to trim off the unused portion).
   Return true upon success, false if any part of obtaining the configuration information from the Device
   Config fails (this causes the loop iterating on Device Units to terminate early): */
static bool ConfigureDeviceUnit(LinuxDriverPinsConfigContext *pContext, u32 nDeviceUnitIndex) {
    if (s_DeviceUnits.nNumDeviceUnits >= pContext->nInitialNumDeviceUnitInstances) {
        /* All the allocated Device Unit instance storage has already been written.  Ignore any additional configurations: */
        LTLOG_REDALERT("cdu.config.excess", NULL);
        return false;
    }
    PinInstance *pInstance = pContext->pNextInstanceToInitialize;
    u32 deviceUnitSection = pContext->pDeviceConfig->GetDeviceUnitSectionAt(pContext->driverConfigOffset, nDeviceUnitIndex);
    if (!deviceUnitSection) {
        LTLOG_YELLOWALERT("cdu.no", NULL);
        return false;
    }
    if (!(pInstance->pName = pContext->pDeviceConfig->ReadString(deviceUnitSection, "name")) || *pInstance->pName == '\0') {
        LTLOG_YELLOWALERT("cdu.name", NULL);
        return false;
    }
    LTResourceValue gpioValue;
    if (!LT_GetCore()->ReadResourceValue(pContext->pDeviceConfig->GetResourceTree(), deviceUnitSection, "gpio", &gpioValue)) {
        LTLOG_YELLOWALERT("cdu.pin", NULL);
        return false;
    }
    if (gpioValue.type != kLTResourceValueType_Integer) {
        LTLOG_YELLOWALERT("cdu.pin.type", NULL);
        return false;
    }

    const char *sType = pContext->pDeviceConfig->ReadString(deviceUnitSection, "type");
    LinuxGPIO_Direction eDirection = kLinuxGPIO_Direction_Unknown;
    if (sType == NULL || lt_strcmp(sType, "bidirectional") == 0) {
        pInstance->eType = kLTDevicePin_PinType_Bidirectional;
        pInstance->pInterface = (LTInterface *)&s_ILTDriverPins_BidirectionalBank;
    } else if (lt_strcmp(sType, "input") == 0) {
        pInstance->eType = kLTDevicePin_PinType_Input;
        pInstance->pInterface = (LTInterface *)&s_ILTDriverPins_InputBank;
        eDirection = kLinuxGPIO_Direction_Input;
    } else if (lt_strcmp(sType, "output") == 0) {
        pInstance->eType = kLTDevicePin_PinType_Output;
        pInstance->pInterface = (LTInterface *)&s_ILTDriverPins_OutputBank;
        eDirection = kLinuxGPIO_Direction_Output;
    } else {
        LTLOG_YELLOWALERT("cdu.type", NULL);
        return false;
    }

    if (!(pInstance->mutex = lt_createobject(LTMutex))) {
        LTLOG_REDALERT("cdu.mutex", "Unable to allocate mutex for \"%s\"", pInstance->pName);
        return false;
    }

    const char *pActive = pContext->pDeviceConfig->ReadString(deviceUnitSection, "active");
    pInstance->bActiveHigh = true;      /* default to active-high if "active" attribute is bad or missing */
    if (pActive) {
             if (!lt_strcmp(pActive, "low" )) pInstance->bActiveHigh = false;
        else if ( lt_strcmp(pActive, "high")) LTLOG_YELLOWALERT("cdu.pin.active", "\"%s\" invalid", pActive);
    }

    if (!LinuxDriverPinsImpl_Export((u32)gpioValue.integer)) {
        LTLOG_YELLOWALERT("export.err", "Failed to export GPIO: \"%s\"", pInstance->pName);
        return false;
    }

    // Set active_low file
    LinuxDriverPinsImpl_SetActiveLow((u32)gpioValue.integer, !pInstance->bActiveHigh);

    pInstance->mutex->API->Lock(pInstance->mutex);
    pInstance->nPin = (u32)gpioValue.integer;
    pInstance->pollFd = LinuxDriverPinsImpl_GetFd(pInstance->nPin, false);
    if (pInstance->pollFd < 0) {
        LTLOG_YELLOWALERT("pollfd.err", "Failed to open fd for polling: \"%s\"", pInstance->pName);
    }

    pInstance->rwFd = LinuxDriverPinsImpl_GetFd(pInstance->nPin, true);
    if (pInstance->rwFd < 0) {
        LTLOG_YELLOWALERT("rwfd.err", "Failed to open fd for read/write: \"%s\"", pInstance->pName);
    }

    /* Try to set the direction for input and output banks.
       Don't log errors to the servers as the Linux kernel
       might refuse to do this if direction is fixed
       in the device tree or similar configuration. */
    if (eDirection != kLinuxGPIO_Direction_Unknown) {
        LinuxGPIOSetDirectionLogFlags(pInstance->nPin, eDirection,
                                      kLTCore_LogFlags_LogTypeDebugLog |
                                      kLTCore_LogFlags_LogToConsole);
    }

    LTList_Init(&(pInstance->irqs)); // Initialize interrupt callback list
    /* Successfully configured this Device Unit instance.
       Count it and advance to the next place in the Device Unit instance array: */
    ++pContext->pNextInstanceToInitialize;
    ++s_DeviceUnits.nNumDeviceUnits;
    pInstance->mutex->API->Unlock(pInstance->mutex);
    DLOG("cdu", "pin %u: \"%s\"", pInstance->nPin, pInstance->pName);
    return true;
}

static bool ConfigureDeviceUnits(void) {
    if (s_DeviceUnits.nNumDeviceUnits || s_DeviceUnits.pDeviceUnits) return false;   /* already configured - do not allocate and configure again */
    LinuxDriverPinsConfigContext *pContext = CreateConfigurationContext();
    if (!pContext) {
        LTLOG_REDALERT("cdus.context", NULL);
        return false;
    }
    bool bSuccess = false;
    do {
        LT_SIZE nInstanceStorageBytes = pContext->nInitialNumDeviceUnitInstances * sizeof(PinInstance);
        if (!(s_DeviceUnits.pDeviceUnits = pContext->pNextInstanceToInitialize = lt_malloc(nInstanceStorageBytes))) {
            LTLOG_REDALERT("cdus.oom", NULL);
            break;
        }

        lt_memset(s_DeviceUnits.pDeviceUnits, 0, nInstanceStorageBytes);        /* cleanliness is next to godliness */
        for (u32 i = 0; i < pContext->nInitialNumDeviceUnitInstances && ConfigureDeviceUnit(pContext, i); ++i);
        if (!s_DeviceUnits.nNumDeviceUnits || s_DeviceUnits.nNumDeviceUnits > pContext->nInitialNumDeviceUnitInstances) {
            LTLOG_REDALERT("du.n.fault", "expected %lu; actual %lu", LT_Pu32(pContext->nInitialNumDeviceUnitInstances), LT_Pu32(s_DeviceUnits.nNumDeviceUnits));
            break;
        } else if (s_DeviceUnits.nNumDeviceUnits < pContext->nInitialNumDeviceUnitInstances) {
            /* Ended up with fewer Device Units than expected.  Give back some memory and attempt to carry on: */
            LTLOG_YELLOWALERT("du.n.fewer", "expected %lu; actual %lu", LT_Pu32(pContext->nInitialNumDeviceUnitInstances), LT_Pu32(s_DeviceUnits.nNumDeviceUnits));
            lt_realloc(s_DeviceUnits.pDeviceUnits, s_DeviceUnits.nNumDeviceUnits * sizeof(PinInstance));
        } else {
            DLOG("du.n", "%lu", LT_Pu32(s_DeviceUnits.nNumDeviceUnits));
        }
        bSuccess = true;
    } while (0);
    DestroyConfigurationContext(pContext);
    return bSuccess;
}

/*******************************************************************************
 * Cleanup or bailure
 * Tear down all Device Units and reclaim resources.                          */
static bool Shutdown(void) {
    // Stop the IRQ polling thread
    DLOG("shutdown.pollthread", NULL);
    s_iThread->Terminate(s_PollingThread);
    NotifyIRQPollThread();
    s_iThread->WaitUntilFinished(s_PollingThread, LTTime_Milliseconds(500));
    s_iThread->Destroy(s_PollingThread); // zero ok
    close(s_efd);
    DLOG("shutdown.pollthread.done", NULL);

    if (s_DeviceUnits.pDeviceUnits) {
        PinInstance *pInstance = s_DeviceUnits.pDeviceUnits;
        for (u32 i = s_DeviceUnits.nNumDeviceUnits; i; --i, ++pInstance) {
            pInstance->mutex->API->Lock(pInstance->mutex);

            if (!LTList_IsEmpty(&pInstance->irqs)) {
                LTLOG_YELLOWALERT("du.nz.irq", "Device Unit still used (nz irq count)");
                // Attempt to free the IRQ record and destroy the soon-to-be orphaned handles
                LTList_ForEach(node, &pInstance->irqs) {
                    IRQ *irq = (IRQ *)node;
                    PinInstance **ppInstance = (PinInstance **)LT_GetCore()->ReserveHandlePrivateData(irq->hPin);
                    if (ppInstance) *ppInstance = NULL; // Stop OnDestroyHandle from interrupting us.
                    LT_GetCore()->ReleaseHandlePrivateData(irq->hPin, ppInstance);
                    LT_GetCore()->DestroyHandle(irq->hPin); //Destroy the handle with OS
                    lt_free(irq);
                } LTList_EndForEach;
                LTList_Init(&pInstance->irqs);
            }

            pInstance->mutex->API->Unlock(pInstance->mutex);
            close(pInstance->pollFd);
            close(pInstance->rwFd);
            lt_destroyobject(pInstance->mutex);
            pInstance->mutex = NULL;
        }
        lt_free(s_DeviceUnits.pDeviceUnits);
        s_DeviceUnits.pDeviceUnits = NULL;
    }
    s_DeviceUnits.nNumDeviceUnits = 0;
    return false;
}

static void Polling_Task(void *data) {
    LT_UNUSED(data);
    struct pollfd *pfds;
    int nCount;
    char val;

    pfds = lt_malloc(sizeof(struct pollfd) * (s_DeviceUnits.nNumDeviceUnits + 1)); // +1 for the eventfd

    if (!pfds) {
        LTLOG_REDALERT("poll.oom", NULL);
        return;
    }

    while(!s_iThread->IsTerminatePending(s_iThread->GetCurrentThread())) {
        memset((void*)pfds, 0, sizeof(struct pollfd) * (s_DeviceUnits.nNumDeviceUnits + 1));

        // Add the eventfd to pollfd collection
        pfds[0].fd = s_efd;
        pfds[0].events = POLLIN;

        // Add the IRQ file descriptors
        u32 fdidx = 1;
        {
            PinInstance *pInstance = s_DeviceUnits.pDeviceUnits;
            for (u32 n = 0; n < s_DeviceUnits.nNumDeviceUnits; n++, pInstance++) {
                IRQ *irq;
                bool irqEnabled = false;
                LTList_ForEach(node, &pInstance->irqs) {
                    irq = (IRQ*)node;
                    if (irq->pCallback) irqEnabled = true;
                } LTList_EndForEach;

                if (irqEnabled) {
                    nCount = pread(pInstance->pollFd, &val, 1, 0);
                    if (nCount == 1) {
                        pInstance->bPrevValue = (val == '1') ? 1 : 0;
                    }
                    pfds[fdidx].fd = pInstance->pollFd;
                    pfds[fdidx].events = POLLPRI;
                    fdidx++;
                }

            }
        }

        // Enter poll
        int result;
        if ((result = poll(pfds, fdidx, -1)) < 0) {
            LTLOG_REDALERT("poll.err", "Poll error");
        }

        // Check if we are woken by eventfd
        if (pfds[0].revents & POLLIN) {
            // Waked by eventfd, clear the eventfd
            // We have no use for val at this point.
            long long val;
            read(pfds[0].fd, &val, sizeof(val)); // A read resets the counter in eventfd
        }

        // Process any Pins events
        for(u32 i = 1; i < fdidx; i++) {
            if (pfds[i].revents & POLLPRI) {
                PinInstance *pInstance = s_DeviceUnits.pDeviceUnits;
                for(u32 j = 0; j < s_DeviceUnits.nNumDeviceUnits; j++, pInstance++) {
                    if (pInstance->pollFd == pfds[i].fd) {
                        bool bNewValue = false;
                        bool bNewValueValid = false;
                        // Read the new value
                        nCount = pread(pInstance->pollFd, &val, 1, 0);
                        if (nCount == 1) {
                            if (val == '0') {
                                bNewValue = false;
                                bNewValueValid = true;
                            } else if (val == '1') {
                                bNewValue = true;
                                bNewValueValid = true;
                            } else {
                                LTLOG_YELLOWALERT("val.inv", "Invalid value (%c) from GPIO file", val);
                            }
                        }

                        if (bNewValueValid && (pInstance->bPrevValue != bNewValue)) {
                            IRQ *irq;
                            LTList_ForEach(node, &pInstance->irqs) {
                                irq = (IRQ*)node;
                                if (irq->pCallback) {
                                    bool triggered = false;
                                    switch(irq->trigger) {
                                        case kLTDevicePin_PinConfiguration_Trigger_BothEdges:
                                            triggered = true;
                                            break;
                                        case kLTDevicePin_PinConfiguration_Trigger_RisingEdge:
                                            triggered = (!pInstance->bPrevValue && bNewValue);
                                            break;
                                        case kLTDevicePin_PinConfiguration_Trigger_FallingEdge:
                                            triggered = (pInstance->bPrevValue && !bNewValue);
                                            break;
                                        default:
                                            LTLOG_YELLOWALERT("trigger.unimpl", "Trigger type %u not implemented", irq->trigger);
                                    }
                                    if (triggered) {
                                        DLOG("irq.fire", "IRQ fire on %d", pInstance->nPin);
                                        irq->pCallback(bNewValue, irq->pClientData);
                                    }
                                }
                            } LTList_EndForEach;
                        }
                    }
                }
            }
        }
    }
    lt_free(pfds);
}

/********************************************************************************************************************************
 * Library initialization and finalization                                                                                     */
static bool LinuxDriverPinsImpl_LibInit(void) {
    DLOG("init", NULL);
    s_pCore = LT_GetCore();
    s_iThread = lt_getlibraryinterface(ILTThread, s_pCore);

    if (!ConfigureDeviceUnits()) return false;

    // Create the eventfd
    s_efd = eventfd(0, 0);
    if (s_efd < 0) return false;

    // Create the IRQ polling thread and queue the polling task
    s_PollingThread = s_pCore->CreateThread("LinuxPinsPolling");
    if (!s_PollingThread) return false;
    s_iThread->SetStackSize(s_PollingThread, 1024);
    s_iThread->Start(s_PollingThread, NULL, NULL);
    s_iThread->QueueTaskProc(s_PollingThread, Polling_Task, NULL, (void*)NULL);

    return true;
}

/* Library finalization or bailure: */
static void LinuxDriverPinsImpl_LibFini(void) { LTLOG_DEBUG("fini", NULL); Shutdown(); }

define_LTLIBRARY_INTERFACE(ILTDriverPins)
    .GetBankNameFromUnitNumber = LinuxDriverPinsImpl_GetBankNameFromUnitNumber,
    .GetUnitNumberFromBankName = LinuxDriverPinsImpl_GetUnitNumberFromBankName,
    .GetBankTypeFromUnitNumber = LinuxDriverPinsImpl_GetBankTypeFromUnitNumber
LTLIBRARY_DEFINITION;

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDevicePins, LinuxDriverPins);

LTLIBRARY_EXPORT_INTERFACES(LinuxDriverPins, (ILTDriverPins) (ILTDriverPins_InputBank) (ILTDriverPins_OutputBank) (ILTDriverPins_BidirectionalBank))
