/*******************************************************************************
 * platforms/esp32/source/esp32/driver/pins/Esp32DriverPinsImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTThread.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/pins/LTDevicePins.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>

#include "Esp32_GPIO.h"
#include "Esp32_Clock.h"
#include "Esp32_Registers.h"

DEFINE_LTLOG_SECTION("esp32.drv.pins");

/* =========================================================================
 * Optional XCLK generation via LEDC high-speed timer 0 / channel 0.
 *
 * Enabled by adding "xclk-gpio" and optionally "xclk-freq-hz" to the
 * Esp32DriverPins driver section in LTDeviceConfig.json, e.g.:
 *
 *   { "lib": "LTDevicePins", "driver": [ { "lib": "Esp32DriverPins",
 *       "xclk-gpio": 0, "xclk-freq-hz": 16000000,
 *       "units": [ ... ] } ] }
 *
 * LEDC HS timer 0 / channel 0 is dedicated to XCLK.  The output is routed
 * through the GPIO matrix (signal LEDC_HS_SIG_OUT0_IDX = 71) to the
 * configured GPIO pin.
 *
 * LEDC frequency formula:
 *   f_out = f_apb / (clock_divider/256) / (2^duty_resolution)
 * With duty_resolution=2 (4-count period) and 50% duty (count=2):
 *   clock_divider = f_apb * 256 / (f_out * 4)
 * ========================================================================= */

#define XCLK_LEDC_SIG_OUT0      71u   /* LEDC_HS_SIG_OUT0_IDX in gpio_sig_map.h */
#define XCLK_DUTY_RES            2u   /* 2-bit → 4-count period */
#define XCLK_APB_HZ     80000000UL
#define XCLK_GPIO_NONE        0xFFu

static u8  s_xclkGpio  = XCLK_GPIO_NONE;
static u32 s_xclkFreqHz = 0u;

static void XclkStart(void) {
    if (s_xclkGpio == XCLK_GPIO_NONE) return;

    u32 freq = s_xclkFreqHz ? s_xclkFreqHz : 16000000u;
    u32 divider = (u32)(((u64)XCLK_APB_HZ * 256UL) / ((u64)freq * (1u << XCLK_DUTY_RES)));
    if (!divider) divider = 1u;

    Esp32_ClockEnablePeripheralClock(kEsp32_Clock_LEDC);

    ESP32_LEDC_TIMER_REG(CONF, 0) = kEsp32_RegisterLEDC_HSTIMER_CONF_RST_M;
    ESP32_LEDC_TIMER_REG(CONF, 0) =
        (XCLK_DUTY_RES << kEsp32_RegisterLEDC_HSTIMER_CONF_DUTY_RES_S)
      | (divider       << kEsp32_RegisterLEDC_HSTIMER_CONF_CLK_DIV_NUM_S)
      | kEsp32_RegisterLEDC_HSTIMER_CONF_TICK_SEL_APB_M;

    ESP32_LEDC_CHANNEL_REG(HPOINT, 0) = 0u;
    ESP32_LEDC_CHANNEL_REG(DUTY,   0) = (1u << XCLK_DUTY_RES) / 2u << 4u;  /* 50%, 4 frac bits */
    ESP32_LEDC_CHANNEL_REG(CONF0,  0) = kEsp32_RegisterLEDC_HSCH_CONF0_OUT_EN_M;
    ESP32_LEDC_CHANNEL_REG(CONF1,  0) = kEsp32_RegisterLEDC_HSCH_CONF1_DUTY_START_M;

    Esp32GPIO_ConfigPin(s_xclkGpio, kEsp32GPIO_Direction_Input,
                        kEsp32GPIO_PullNone, kEsp32GPIO_Function_GPIO);
    Esp32GPIO_ConfigMatrixPin(s_xclkGpio, XCLK_LEDC_SIG_OUT0,
                              kEsp32GPIO_Direction_Output, false);

    LTLOG_DEBUG("xclk", "XCLK %luHz on GPIO%lu (div=%lu)",
        LT_Pu32(freq), LT_Pu32((u32)s_xclkGpio), LT_Pu32(divider));
}

static void XclkStop(void) {
    if (s_xclkGpio == XCLK_GPIO_NONE) return;
    Esp32GPIO_ClearPinConfig(s_xclkGpio);
    ESP32_LEDC_CHANNEL_REG(CONF0, 0) = 0u;
    ESP32_LEDC_TIMER_REG(CONF,   0) = kEsp32_RegisterLEDC_HSTIMER_CONF_RST_M;
    Esp32_ClockDisablePeripheralClock(kEsp32_Clock_LEDC);
}

#define LTDEVICEPINS_DO_DLOG     0
#if     LTDEVICEPINS_DO_DLOG
#define DLOG                    LTLOG
#else
#define DLOG                    LTLOG_LOGNULL
#endif

#define REBOOT_THREAD_STACKSIZE 768

/* clang-format off */

enum { kInputOnlyHereAndUp = 33 };  /* All GPIO this and later are input-only. */

enum { kReservedPins =   (1 << 1)  /* UART             */
                       | (1 << 3)  /* UART             */
                       | (1 << 6)  /* SPI flash        */
                       | (1 << 7)  /* SPI flash        */
                       | (1 << 8)  /* SPI flash        */
                       | (1 << 9)  /* SPI flash        */
                       | (1 << 10) /* SPI flash        */
                       | (1 << 11) /* SPI flash        */
                       | (1 << 28) /* not configurable */
                       | (1 << 29) /* not configurable */
                       | (1 << 30) /* not configurable */
                       | (1 << 31) /* not configurable */ };

/* Return nonzero if pin nPin is in the reserved list (above): */
static bool IsPinReserved(u32 nPin) { return nPin > 31 ? false : 1 << nPin & kReservedPins; }

static LTDeviceWatchdog *s_pWatchdog = NULL;

/* A thread is required to wait for a reboot, and then to force such configured pins to hold
 * the value through a reboot. */
static ILTThread *s_iThread       = NULL;
static LTThread   s_hRebootThread = 0;

/* ISR Callback support */
typedef struct {
    LTDevicePin_IRQCallback *pCallback;
    void                    *pClientData;
} IRQ;

LT_STATIC_ASSERT_SIZE_32_64(IRQ, 8, 16);

/* A GPIO Pin instance (Device Unit), generated in LibInit, one for each pin called out in the DeviceConfig: */
typedef struct {
    const char  *pName;         /* Name, given in the "name" element in the DeviceConfig
                                   NOTE: LTDeviceConfig must remain open for the data
                                         dereferenced by this pointer to remain valid.      */
    IRQ          irq;           /* Callback and client data passed thereto                  */
    LTInterface *pInterface;    /* Which interface to supply (through gethandleinterface()) */
    LTMutex     *mutex;         /* mutual exclusion for reference counting and IRQ          */
    u32          nRefCount;     /* how many clients have a handle to this instance          */
    u8           nPin;          /* the GPIO pin in question                                 */
    bool         bRebootHold;   /* if the pin level is kept through a reboot                */
} PinInstance;

LT_STATIC_ASSERT_SIZE_32_64(PinInstance, 28, 48);

/* Container for all the pin instances */
typedef struct DeviceUnits {
    PinInstance *pDeviceUnits;      /* Pointer into the heap where the Device Units are stored (as an array) */
    u32          nNumDeviceUnits;   /* How many Device Units this Driver supplies                            */
} DeviceUnits;

LT_STATIC_ASSERT_SIZE_32_64(DeviceUnits, 8, 12);

static DeviceUnits s_DeviceUnits;

/* Return a pointer to the Device Unit instance, given the Device Unit handle: */
static PinInstance *InstanceFromHandle(LTDeviceUnit hPin) {
    if (!hPin) return NULL;
    PinInstance **ppInstance = (PinInstance **)LT_GetCore()->ReserveHandlePrivateData(hPin);
    PinInstance *pInstance = NULL;
    if (ppInstance) {
        pInstance = *ppInstance;
        LT_GetCore()->ReleaseHandlePrivateData(hPin, ppInstance);
    }
    return pInstance;
}

/* Interrupt service routine for all GPIO pins associated with this Driver */
static void Esp32DriverPinsImpl_ISR(u8 nPin, bool bPinHigh, void *pClientData) { LT_UNUSED(nPin);
    IRQ *pIRQ = &((PinInstance *)pClientData)->irq;
    if (pIRQ->pCallback) pIRQ->pCallback(bPinHigh, pIRQ->pClientData);
}

/* Return ESP32 pull type given generic pull type: */
static Esp32GPIO_PullType LTPull_To_Esp32Pull(LTDevicePin_PinConfiguration_PullType pullType) {
    Esp32GPIO_PullType esp32PullType = kEsp32GPIO_PullNone;
    switch (pullType) {
    case kLTDevicePin_PinConfiguration_PullType_PullUp:   esp32PullType = kEsp32GPIO_PullUp;      break;
    case kLTDevicePin_PinConfiguration_PullType_PullDown: esp32PullType = kEsp32GPIO_PullDown;    break;
    case kLTDevicePin_PinConfiguration_PullType_NoPull:   esp32PullType = kEsp32GPIO_PullNone;    break;
    }
    return esp32PullType;
}

/* ESP32 does not support the concept of pin banks; one pin per bank: */
static u32 Esp32DriverPinsImpl_GetNumPins(LTDeviceUnit hPin) { LT_UNUSED(hPin); return 1; }

/* Read a GPIO pin.  Return 1 for high, 0 for low, LT_U32_MAX for an invalid handle: */
static u32 Esp32DriverPinsImpl_Read(LTDeviceUnit hPin) {
    PinInstance *pInstance = InstanceFromHandle(hPin);
    return pInstance ? Esp32GPIO_ReadPin(pInstance->nPin) : LT_U32_MAX;
}

/* Set and clear the ISR callback and client data: */
static void SetIRQ(PinInstance *pInstance, LTDevicePin_IRQCallback *pCallback, void *pClientData) {
    pInstance->irq.pCallback   = pCallback;
    pInstance->irq.pClientData = pClientData;
}

static void ClearIRQ(PinInstance *pInstance) { SetIRQ(pInstance, NULL, NULL); }

/* Enable an interrupt for a given Device Unit: */
static void Esp32DriverPinsImpl_EnableIRQ(LTDeviceUnit hPin, LTDevicePin_PinConfiguration_Trigger trigger,
                                            LTTime tmDebounce, LTDevicePin_IRQCallback *pISRCallback, void *pISRClientData) {
    LT_UNUSED(tmDebounce); // ESP32 does not support debounce
    PinInstance *pInstance = InstanceFromHandle(hPin);
    if (pInstance) {
        Esp32GPIO_Trigger triggerEdge = kEsp32GPIO_Trigger_Disabled;
        switch (trigger) {
        case kLTDevicePin_PinConfiguration_Trigger_RisingEdge:  triggerEdge = kEsp32GPIO_Trigger_Rising;    break;
        case kLTDevicePin_PinConfiguration_Trigger_FallingEdge: triggerEdge = kEsp32GPIO_Trigger_Falling;   break;
        case kLTDevicePin_PinConfiguration_Trigger_BothEdges:   triggerEdge = kEsp32GPIO_Trigger_Both;      break;
        case kLTDevicePin_PinConfiguration_Trigger_LowLevel:    triggerEdge = kEsp32GPIO_Trigger_LowLevel;  break;
        case kLTDevicePin_PinConfiguration_Trigger_HighLevel:   triggerEdge = kEsp32GPIO_Trigger_HighLevel; break;
        }
        pInstance->mutex->API->Lock(pInstance->mutex);
        SetIRQ(pInstance, pISRCallback, pISRClientData);
        if (!Esp32GPIO_AttachISR(pInstance->nPin, triggerEdge, Esp32DriverPinsImpl_ISR, pInstance)) ClearIRQ(pInstance);
        pInstance->mutex->API->Unlock(pInstance->mutex);
    }
}

/* Disable an interrupt for a given PinInstance: */
static void DisableInstanceIRQ(PinInstance *pInstance) {
    if (pInstance) {
        pInstance->mutex->API->Lock(pInstance->mutex);
        Esp32GPIO_DetachISR(pInstance->nPin);
        ClearIRQ(pInstance);
        pInstance->mutex->API->Unlock(pInstance->mutex);
    }
}

/* Disable an interrupt for a given Device Unit: */
static void Esp32DriverPinsImpl_DisableIRQ(LTDeviceUnit hPin) { DisableInstanceIRQ(InstanceFromHandle(hPin)); }

/* Handle destruction (used by all ILTDriverPins interfaces): */
static void OnDestroyHandle(LTHandle hDevice) {
    PinInstance *pInstance = InstanceFromHandle(hDevice);
    if (pInstance) {
        u32 nRefCount = LT_U32_MAX;
        pInstance->mutex->API->Lock(pInstance->mutex);
        if (pInstance->nRefCount > 0) nRefCount = --pInstance->nRefCount;
        pInstance->mutex->API->Unlock(pInstance->mutex);
        if (!nRefCount) DisableInstanceIRQ(pInstance);
    }
}

/*******************************************************************************
 * ILTDriverPins_OutputBank                                                   */

static void Esp32DriverOutputPins_ConfigureOutputType(LTDeviceUnit hPin, LTDevicePin_PinConfiguration_OutputType outputType) {
    PinInstance *pInstance = InstanceFromHandle(hPin);
    if (pInstance) Esp32GPIO_ConfigOutputType(pInstance->nPin,   outputType == kLTDevicePin_PinConfiguration_OutputType_OpenDrain
                                                               ? kEsp32GPIO_OutputType_OpenDrain
                                                               : kEsp32GPIO_OutputType_PushPull);
}

static void Esp32DriverOutputPins_ConfigureRebootHold(LTDeviceUnit hPin, bool bRebootHold) {
    PinInstance *pInstance = InstanceFromHandle(hPin);
    if (pInstance) pInstance->bRebootHold = bRebootHold;
}

static void Esp32DriverOutputPins_Set(LTDeviceUnit hPin, u32 pinBits) {
    PinInstance *pInstance = InstanceFromHandle(hPin);
    if (pInstance) Esp32GPIO_WritePin(pInstance->nPin, (pinBits > 0));
}

define_LTLIBRARY_INTERFACE(ILTDriverPins_OutputBank)
    .GetNumPins          = Esp32DriverPinsImpl_GetNumPins,
    .ConfigureOutputType = Esp32DriverOutputPins_ConfigureOutputType,
    .ConfigureRebootHold = Esp32DriverOutputPins_ConfigureRebootHold,
    .Set                 = Esp32DriverOutputPins_Set,
    .Read                = Esp32DriverPinsImpl_Read
LTLIBRARY_DEFINITION;

/*******************************************************************************
 * ILTDriverPins_BidirectionalBank                                            */

static void Esp32DriverBidirectionalPins_ConfigureAsInput(LTDeviceUnit hPin, LTDevicePin_PinConfiguration_PullType pullType) {
    PinInstance *pInstance = InstanceFromHandle(hPin);
    if (pInstance) Esp32GPIO_ConfigPin(pInstance->nPin, kEsp32GPIO_Direction_Input, LTPull_To_Esp32Pull(pullType), kEsp32GPIO_Function_GPIO);
}

static void Esp32DriverBidirectionalPins_ConfigureAsOutput(LTDeviceUnit hPin, LTDevicePin_PinConfiguration_OutputType outputType) {
    PinInstance *pInstance = InstanceFromHandle(hPin);
    if (pInstance) {
        Esp32GPIO_ConfigPin(pInstance->nPin, kEsp32GPIO_Direction_Output, kEsp32GPIO_PullNone, kEsp32GPIO_Function_GPIO);
        Esp32DriverOutputPins_ConfigureOutputType(hPin, outputType);
    }
}

define_LTLIBRARY_INTERFACE(ILTDriverPins_BidirectionalBank, OnDestroyHandle)
    .GetNumPins          = Esp32DriverPinsImpl_GetNumPins,
    .ConfigureAsOutput   = Esp32DriverBidirectionalPins_ConfigureAsOutput,
    .ConfigureAsInput    = Esp32DriverBidirectionalPins_ConfigureAsInput,
    .ConfigureRebootHold = Esp32DriverOutputPins_ConfigureRebootHold,
    .Set                 = Esp32DriverOutputPins_Set,
    .Read                = Esp32DriverPinsImpl_Read,
    .EnableIRQ           = Esp32DriverPinsImpl_EnableIRQ,
    .DisableIRQ          = Esp32DriverPinsImpl_DisableIRQ,
LTLIBRARY_DEFINITION;

/*******************************************************************************
 * ILTDriverPins_InputBank                                                    */

static void Esp32DriverInputPins_ConfigurePullType(LTDeviceUnit hPin, LTDevicePin_PinConfiguration_PullType pullType) {
    PinInstance *pInstance = InstanceFromHandle(hPin);
    if (pInstance) Esp32GPIO_ConfigPin(pInstance->nPin, kEsp32GPIO_Direction_Input, LTPull_To_Esp32Pull(pullType), kEsp32GPIO_Function_GPIO);
}

define_LTLIBRARY_INTERFACE(ILTDriverPins_InputBank, OnDestroyHandle)
    .GetNumPins          = Esp32DriverPinsImpl_GetNumPins,
    .ConfigurePullType   = Esp32DriverInputPins_ConfigurePullType,
    .Read                = Esp32DriverPinsImpl_Read,
    .EnableIRQ           = Esp32DriverPinsImpl_EnableIRQ,
    .DisableIRQ          = Esp32DriverPinsImpl_DisableIRQ,
LTLIBRARY_DEFINITION;


/*******************************************************************************
 * ILTDriverPin                                                               */

static u32 Esp32DriverPinsImpl_GetNumDeviceUnits(void) { return s_DeviceUnits.nNumDeviceUnits; }

static LTDeviceUnit Esp32DriverPinsImpl_CreateDeviceUnitHandle(u32 nDeviceUnitIndex) {
    if (nDeviceUnitIndex >= s_DeviceUnits.nNumDeviceUnits) return 0;        /* invalid device unit index */
    PinInstance *pInstance = s_DeviceUnits.pDeviceUnits + nDeviceUnitIndex; /* the PinInstance for the index */
    LTDeviceUnit hDevice = LT_GetCore()->CreateHandle(pInstance->pInterface, sizeof(PinInstance *));
    if (hDevice) {
        PinInstance **ppInstance = (PinInstance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
        if (ppInstance) {
            *ppInstance = pInstance;    /* assign the PinInstance to the private data */
            pInstance->mutex->API->Lock(pInstance->mutex);
            ++pInstance->nRefCount;
            pInstance->mutex->API->Unlock(pInstance->mutex);
            LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
        } else {                        /* invalid private data pointer - throw out the handle */
            LTLOG_YELLOWALERT("cduh.hdl.inv", "Created handle is DOA");
            LT_GetCore()->DestroyHandle(hDevice);
            hDevice = 0;
        }
    } else {
        LTLOG_YELLOWALERT("cduh.create.no", "Unable to create handle");
    }
    return hDevice;
}

/* Pin name lookup support: */
static bool Esp32DriverPinsImpl_GetBankNameFromUnitNumber(u32 nDeviceUnitIndex, char const **ppPinBankNameToSet) {
    if (nDeviceUnitIndex >= s_DeviceUnits.nNumDeviceUnits) return false;
    PinInstance *pInstance = s_DeviceUnits.pDeviceUnits + nDeviceUnitIndex;
    *ppPinBankNameToSet = pInstance->pName;
    return true;
}

static bool Esp32DriverPinsImpl_GetUnitNumberFromBankName(char const *pPinBankName, u32 *pIndexToSet) {
    PinInstance *pInstance = s_DeviceUnits.pDeviceUnits;
    for (u32 n = 0; n < s_DeviceUnits.nNumDeviceUnits; ++n, ++pInstance)
        if (!lt_strcmp(pPinBankName, pInstance->pName)) {
            *pIndexToSet = n;
            return true;
        }
    return false;
}

static bool Esp32DriverPinsImpl_GetBankTypeFromUnitNumber(u32 nDeviceUnitIndex, LTDevicePin_PinType *pPinType) {
    if (nDeviceUnitIndex >= s_DeviceUnits.nNumDeviceUnits) return false;
    PinInstance *pInstance = s_DeviceUnits.pDeviceUnits + nDeviceUnitIndex;
    *pPinType = pInstance->nPin >= kInputOnlyHereAndUp ? kLTDevicePin_PinType_Input : kLTDevicePin_PinType_Bidirectional;
    return true;
}

/*******************************************************************************
 * Device Unit initialization and configuration                               */

/* Data structure containing the entire context for initialization: */
typedef struct Esp32DriverPinsConfigContext {
    LTDeviceConfig       *pDeviceConfig;
    PinInstance          *pNextInstanceToInitialize;       /* pointer to next place in s_DeviceUnits.pDeviceUnits to place a Device Unit instance */
    u32                   driverConfigOffset;              /* The Device Config offset for this Driver */
    u32                   nInitialNumDeviceUnitInstances;  /* number of Device units in the Device Config for this Driver Library */
} Esp32DriverPinsConfigContext;

LT_STATIC_ASSERT_SIZE_32_64(Esp32DriverPinsConfigContext, 16, 24);

/* Reclaim all resources used by the context: */
static Esp32DriverPinsConfigContext *DestroyConfigurationContext(Esp32DriverPinsConfigContext *pContext) {
    if (pContext) {
        lt_closelibrary(pContext->pDeviceConfig);
        lt_free(pContext);
    }
    return NULL;
}

/* Allocate memory for the context, and fill in as much as possible: */
static Esp32DriverPinsConfigContext *CreateConfigurationContext(void) {
    Esp32DriverPinsConfigContext *pContext = lt_malloc(sizeof(Esp32DriverPinsConfigContext));
    if (   !pContext
        || !(pContext->pDeviceConfig                  = lt_openlibrary(LTDeviceConfig))
        || !(pContext->driverConfigOffset             = pContext->pDeviceConfig->GetDriverSection("LTDevicePins", "Esp32DriverPins"))
        || !(pContext->nInitialNumDeviceUnitInstances = pContext->pDeviceConfig->GetNumDeviceUnits(pContext->driverConfigOffset))) return DestroyConfigurationContext(pContext);
    return pContext;
}

/* Configure one Device Unit instance from the configuration information in the Device Config.
   If the Device Unit instance is successfully configured, advance the instance pointer to the next instance,
   and increment the number of available Device Units.  Otherwise, leave the pointer and count alone, and the
   next iteration of the callback will rewrite the current instance (if that happens, ConfigureDeviceUnits()
   reallocs the Device Unit instance array to trim off the unused portion).
   Return true upon success, false if any part of obtaining the configuration information from the Device
   Config fails (this causes the loop iterating on Device Units to terminate early): */
static bool ConfigureDeviceUnit(Esp32DriverPinsConfigContext *pContext, u32 nDeviceUnitIndex) {
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
    if (IsPinReserved((u32)gpioValue.integer)) {
        LTLOG_YELLOWALERT("cdu.pin.reserved", "Pin for \"%s\" (%lu) reserved", pInstance->pName, LT_Pu32((u32)gpioValue.integer));
        return false;
    }
    if (!(pInstance->mutex = lt_createobject(LTMutex))) {
        LTLOG_REDALERT("cdu.mutex", "Unable to allocate mutex for \"%s\"", pInstance->pName);
        return false;
    }
    pInstance->mutex->API->Lock(pInstance->mutex);
    pInstance->pInterface = (pInstance->nPin = (u32)gpioValue.integer) >= kInputOnlyHereAndUp ? (LTInterface *)&s_ILTDriverPins_InputBank
                                                                                              : (LTInterface *)&s_ILTDriverPins_BidirectionalBank;
    ClearIRQ(pInstance);
    pInstance->nRefCount  = 0;
    /* Successfully configured this Device Unit instance.
       Count it and advance to the next place in the Device Unit instance array: */
    ++pContext->pNextInstanceToInitialize;
    ++s_DeviceUnits.nNumDeviceUnits;
    pInstance->mutex->API->Unlock(pInstance->mutex);
    LTLOG_DEBUG("cdu", "pin %u: \"%s\"", pInstance->nPin, pInstance->pName);
    return true;
}

static bool ConfigureDeviceUnits(void) {
    if (s_DeviceUnits.nNumDeviceUnits || s_DeviceUnits.pDeviceUnits) return false;   /* already configured - do not allocate and configure again */
    Esp32DriverPinsConfigContext *pContext = CreateConfigurationContext();
    if (!pContext) {
        LTLOG_REDALERT("cdus.context", NULL);
        return false;
    }

    /* Optional XCLK config at the driver level (not per-unit) */
    {
        u32 xclkGpio = pContext->pDeviceConfig->ReadInteger(pContext->driverConfigOffset, "xclk-gpio");
        u32 xclkFreq = pContext->pDeviceConfig->ReadInteger(pContext->driverConfigOffset, "xclk-freq-hz");
        /* ReadInteger returns 0 for absent keys; treat 0 as absent since GPIO0 is
         * a valid pin — caller must always provide xclk-gpio explicitly. */
        s_xclkGpio   = (xclkGpio < 40u) ? (u8)xclkGpio : XCLK_GPIO_NONE;
        s_xclkFreqHz = xclkFreq;
    }
    LT_SIZE nInstanceStorageBytes = pContext->nInitialNumDeviceUnitInstances * sizeof(PinInstance);
    if (!(s_DeviceUnits.pDeviceUnits = pContext->pNextInstanceToInitialize = lt_malloc(nInstanceStorageBytes))) {
        LTLOG_REDALERT("cdus.oom", NULL);
        return DestroyConfigurationContext(pContext);
    }
    lt_memset(s_DeviceUnits.pDeviceUnits, 0, nInstanceStorageBytes);        /* cleanliness is next to godliness */
    for (u32 i = 0; i < pContext->nInitialNumDeviceUnitInstances && ConfigureDeviceUnit(pContext, i); ++i);
    if (!s_DeviceUnits.nNumDeviceUnits || s_DeviceUnits.nNumDeviceUnits > pContext->nInitialNumDeviceUnitInstances) {
        LTLOG_REDALERT("du.n.fault", "expected %lu; actual %lu", LT_Pu32(pContext->nInitialNumDeviceUnitInstances), LT_Pu32(s_DeviceUnits.nNumDeviceUnits));
        return DestroyConfigurationContext(pContext);
    } else if (s_DeviceUnits.nNumDeviceUnits < pContext->nInitialNumDeviceUnitInstances) {
        /* Ended up with fewer Device Units than expected.  Give back some memory and attempt to carry on: */
        LTLOG_YELLOWALERT("du.n.fewer", "expected %lu; actual %lu", LT_Pu32(pContext->nInitialNumDeviceUnitInstances), LT_Pu32(s_DeviceUnits.nNumDeviceUnits));
        lt_realloc(s_DeviceUnits.pDeviceUnits, s_DeviceUnits.nNumDeviceUnits * sizeof(PinInstance));
    } else {
        LTLOG_DEBUG("du.n", "%lu", LT_Pu32(s_DeviceUnits.nNumDeviceUnits));
    }
    DestroyConfigurationContext(pContext);
    return true;
}

/*******************************************************************************
 * Cleanup or bailure
 * Tear down all Device Units and reclaim resources.                          */

static bool Shutdown(Esp32DriverPinsConfigContext *pContext) {
    XclkStop();
    s_xclkGpio   = XCLK_GPIO_NONE;
    s_xclkFreqHz = 0u;
    DestroyConfigurationContext(pContext);
    if (s_DeviceUnits.pDeviceUnits) {
        PinInstance *pInstance = s_DeviceUnits.pDeviceUnits;
        for (u32 i = s_DeviceUnits.nNumDeviceUnits; i; --i, ++pInstance) {
            pInstance->mutex->API->Lock(pInstance->mutex);
            if (pInstance->nRefCount)
                LTLOG_YELLOWALERT("du.nz.ref", "Device Unit still used (nz ref count)");
            pInstance->mutex->API->Unlock(pInstance->mutex);
            DisableInstanceIRQ(pInstance);
            lt_destroyobject(pInstance->mutex);
            pInstance->mutex = NULL;
        }
        lt_free(s_DeviceUnits.pDeviceUnits);
        s_DeviceUnits.pDeviceUnits = NULL;
    }
    s_DeviceUnits.nNumDeviceUnits = 0;

    s_iThread->Destroy(s_hRebootThread);
    s_hRebootThread = 0;
    if (s_pWatchdog) lt_closelibrary(s_pWatchdog);

    return false;
}

/* Set the pin value hold register for the pins that keep their value through reboots. */
void RebootHandler(void *pData) {
    LT_UNUSED(pData);
    if (s_DeviceUnits.pDeviceUnits) {
        PinInstance *pInstance = s_DeviceUnits.pDeviceUnits;
        for (u32 i = s_DeviceUnits.nNumDeviceUnits; i; --i, ++pInstance) {
            pInstance->mutex->API->Lock(pInstance->mutex);
            if (pInstance->bRebootHold
                && (pInstance->pInterface == (LTInterface *)(&s_ILTDriverPins_OutputBank) ||
                pInstance->pInterface == (LTInterface *)(&s_ILTDriverPins_BidirectionalBank))) {
                    Esp32GPIO_ConfigPinHold(pInstance->nPin, pInstance->bRebootHold);
            }
            pInstance->mutex->API->Unlock(pInstance->mutex);
        }
    }
}

bool RebootHandlerStart(void) {
    if (s_pWatchdog) {
        s_pWatchdog->OnRebootNotify(RebootHandler, NULL, NULL);
        return true;
    }
    else return false;
}

/********************************************************************************************************************************
 * Library initialization and finalization                                                                                     */

static bool Esp32DriverPinsImpl_LibInit(void) {
    DLOG("init", NULL);

    if (!(s_pWatchdog = lt_openlibrary(LTDeviceWatchdog))) return false;

    /* A new thread is needed to configure a pin at the reboot, but we should check if there
     * is a way to repurpose some other thread at the reboot time because we are shutting
     * down anyway. */
    if (!(s_hRebootThread = LT_GetCore()->CreateThread("esp32.pins.onreboot"))) {
        LTLOG_REDALERT("boot.thrd.err", "Failed to create reboot handler thread");
        lt_closelibrary(s_pWatchdog);
        return false;
    }
    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    s_iThread->SetStackSize(s_hRebootThread, REBOOT_THREAD_STACKSIZE);
    s_iThread->Start(s_hRebootThread, RebootHandlerStart, NULL);
    if (!ConfigureDeviceUnits()) return Shutdown(NULL);
    XclkStart();
    return true;
}

/* Library finalization or bailure: */
static void Esp32DriverPinsImpl_LibFini(void) { LTLOG_DEBUG("fini", NULL); Shutdown(NULL); }

define_LTLIBRARY_INTERFACE(ILTDriverPins)
    .GetBankNameFromUnitNumber = Esp32DriverPinsImpl_GetBankNameFromUnitNumber,
    .GetUnitNumberFromBankName = Esp32DriverPinsImpl_GetUnitNumberFromBankName,
    .GetBankTypeFromUnitNumber = Esp32DriverPinsImpl_GetBankTypeFromUnitNumber
LTLIBRARY_DEFINITION;

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDevicePins, Esp32DriverPins);

LTLIBRARY_EXPORT_INTERFACES(Esp32DriverPins, (ILTDriverPins) (ILTDriverPins_InputBank) (ILTDriverPins_OutputBank) (ILTDriverPins_BidirectionalBank))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  14-Nov-22   constantine created from Esp32DriverPins
 *  20-Apr-23   constantine converted to the new Device Config Arbolation hotness
 *  17-Aug-23   commodus    added reboot handler thread
 *  04-Dec-23   commodus    increased reboot handler thread stack to 768
 */
