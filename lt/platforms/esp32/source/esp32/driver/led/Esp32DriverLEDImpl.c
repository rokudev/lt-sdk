/*******************************************************************************
 * platforms/esp32/source/esp32/driver/led/Esp32DriverLEDImpl.c>
 *
 * ************************************************************************
 * * NOTES: This Driver has limited capabilities:                         *
 * *        - It supports only one element per LED group                  *
 * *        - It supports only mono-color, mono-intensity LEDs            *
 * *        - It operates the LEDs directly through LTDevicePins          *
 * *        - It unconditionally sets the Pins output as push-pull        *
 * *        - It serves a a minimalist, lightweight LED driver for        *
 * *          platforms that will never support the more sophisticated    *
 * *          LED functions.                                              *
 * *        - While this is initially part of the Esp32 platform          *
 * *          Drivers, it could easily (and perhaps should be) a          *
 * *          common driver, possibly named                               *
 * *          CommonDriverLED_GPIOIndicatorLamp.                          *
 * ************************************************************************
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Esp32 LT Driver Library for LED access
 ******************************************************************************/
/** @file Esp32DriverLEDImpl.c Implementation of LED driver */

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/led/LTDeviceLED.h>
#include <lt/device/pins/LTDevicePins.h>

DEFINE_LTLOG_SECTION("esp32.drv.led");

static ILTDriverPins_OutputBank        *s_pIOutputBank        = NULL;
static ILTDriverPins_BidirectionalBank *s_pIBidirectionalBank = NULL;
static LTDevicePins                    *s_pDevicePins         = NULL;

static const ILTDriverLED_GroupType_IndicatorLamp s_ILTDriverLED_GroupType_IndicatorLamp;

/*******************************************************************************
 * LED Group descriptions                                                     */

typedef struct {
    LTDeviceLED_GroupDescriptor           groupDescriptor;
    const char                           *pName;                /* C-string name of the LED group, stored in Device Config.
                                                                   NOTE: LTDeviceConfig must remain open for the data
                                                                         dereferenced by this pointer to remain valid.    */
    LTDeviceUnit                          hPinBank;             /* GPIO pin bank handle                                   */
    LTDevicePin_PinType                   bankType;             /* Bidirectional or Output - used for interface selection */
    bool                                  bActiveHigh;          /* true: active-high                                      */
} LEDGroupInstance;

LT_STATIC_ASSERT_SIZE_32_64(LEDGroupInstance, 32, 44);

/* Container for all the LED group instances */
typedef struct DeviceUnits {
    LEDGroupInstance *pDeviceUnits;     /* Pointer into the heap where the Device Units are stored (as an array) */
    u32               nNumDeviceUnits;  /* How many Device Units this Driver supplies                            */
} DeviceUnits;

LT_STATIC_ASSERT_SIZE_32_64(DeviceUnits, 8, 12);

static DeviceUnits s_DeviceUnits;

/********************************************************************************************************************************
 * Access to Device Unit numbers and names:                                                                                    */

static bool Esp32DriverLED_GetGroupDescriptorFromUnitNumber(u32 nDeviceUnitIndex, LTDeviceLED_GroupDescriptor *pDescriptor) {
    if (nDeviceUnitIndex >= s_DeviceUnits.nNumDeviceUnits) return false;
    *pDescriptor = (s_DeviceUnits.pDeviceUnits + nDeviceUnitIndex)->groupDescriptor;
    return true;
}

static bool Esp32DriverLED_GetUnitNumberFromGroupName(char const * pGroupName, u32 * pDeviceUnitIndexToSet) {
    for (u32 i = 0; i < s_DeviceUnits.nNumDeviceUnits; ++i) {
        if (!lt_strcmp(pGroupName, (s_DeviceUnits.pDeviceUnits + i)->groupDescriptor.m_pGroupName)) {
            *pDeviceUnitIndexToSet = i;
            return true;
        }
    }
    return false;
}

define_LTLIBRARY_INTERFACE(ILTDriverLED) {
    .GetGroupDescriptorFromUnitNumber = Esp32DriverLED_GetGroupDescriptorFromUnitNumber,
    .GetUnitNumberFromGroupName       = Esp32DriverLED_GetUnitNumberFromGroupName
} LTLIBRARY_DEFINITION;

/***********************************************************************************************************************
 * LED Group instance and LED instance access helpers                                                                 */

/* Retrieve a pointer to the Group Instance data from the Device Unit handle.
 * Return 0 if the handle or the private data pointer are invalid. */
static LEDGroupInstance *InstanceFromHandle(LTDeviceUnit hDevice) {
    if (!hDevice) return NULL;
    LEDGroupInstance **ppInstance = (LEDGroupInstance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    LEDGroupInstance *pInstance = NULL;
    if (ppInstance) {
        pInstance = *ppInstance;
        LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
    }
    return pInstance;
}

/********************************************************************************************************************************
 * Differentiation between Output Banks and Bidirectional Banks.
 * Allow ease of access of either type of Bank by moving all the output-or-bidirectional logic to here.
 * These functions have the PRECONDITION that the type is either output or bidirectional;  The validity of the Bank (as either
 * output or bidirectional - associating an output Bank with a LED wouldn't make any sense) is established in
 * InitializeDeviceUnit().                                                                                                     */

static void Set(LEDGroupInstance *pInstance, u32 bOn) {
    if (!pInstance) return;
    if (!pInstance->bActiveHigh) bOn = bOn ? 0 : 1;
    if (pInstance->bankType == kLTDevicePin_PinType_Output) s_pIOutputBank->Set(pInstance->hPinBank, bOn); else s_pIBidirectionalBank->Set(pInstance->hPinBank, bOn);
}

static u32 Read(LEDGroupInstance *pInstance) {
    if (!pInstance) return 0;
    u32 pin = pInstance->bankType == kLTDevicePin_PinType_Output ? s_pIOutputBank->Read(pInstance->hPinBank) : s_pIBidirectionalBank->Read(pInstance->hPinBank);
    return pInstance->bActiveHigh ? pin ? LT_U32_MAX : 0 : pin ? 0 : LT_U32_MAX;
}

/********************************************************************************************************************************
 * Device-unit creation interface.                                                                                             */
static u32 Esp32DriverLEDImpl_GetNumDeviceUnits(void) { return s_DeviceUnits.nNumDeviceUnits; }

static LTDeviceUnit Esp32DriverLEDImpl_CreateDeviceUnitHandle(u32 nDeviceUnitIndex) {
    LTDeviceUnit hDevice = 0;
    if (nDeviceUnitIndex < s_DeviceUnits.nNumDeviceUnits)
        hDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverLED_GroupType_IndicatorLamp, sizeof(LEDGroupInstance *));
    if (hDevice) {
        LEDGroupInstance **ppInstance = (LEDGroupInstance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
        if (ppInstance) {
            *ppInstance = s_DeviceUnits.pDeviceUnits + nDeviceUnitIndex;
            LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
        } else {
            LT_GetCore()->DestroyHandle(hDevice);
            hDevice = 0;
        }
    }
    return hDevice;
}

/********************************************************************************************************************************
 * GPIO initialization                                                                                                         */

static bool InitializeDeviceUnit(LEDGroupInstance *pInstance, u32 nBankNumber) {
    LTDevicePin_PinType PinType = kLTDevicePin_PinType_Invalid;
    if (!s_pDevicePins->GetBankTypeFromUnitNumber(nBankNumber, &PinType)) {
        LTLOG_REDALERT("init.bank.type.no", NULL);
        return false;
    }
    if (PinType != kLTDevicePin_PinType_Output && PinType != kLTDevicePin_PinType_Bidirectional) {
        LTLOG_REDALERT("init.bank.type", NULL);
        return false;
    }
    pInstance->bankType = PinType;
    if (!(pInstance->hPinBank = s_pDevicePins->CreateDeviceUnitHandle(nBankNumber))) {
        LTLOG_REDALERT("init.bank.handle", NULL);
        return false;
    }
    if (PinType == kLTDevicePin_PinType_Output) {
        if (!s_pIOutputBank) s_pIOutputBank = lt_gethandleinterface(ILTDriverPins_OutputBank, pInstance->hPinBank);
        if (!s_pIOutputBank) {
            LTLOG_REDALERT("init.cfg.out", NULL);
            return false;
        }
    } else {
        if (!s_pIBidirectionalBank) s_pIBidirectionalBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, pInstance->hPinBank);
        if (!s_pIBidirectionalBank) {
            LTLOG_REDALERT("init.cfg.bi", NULL);
            return false;
        }
        s_pIBidirectionalBank->ConfigureAsOutput(pInstance->hPinBank, kLTDevicePin_PinConfiguration_OutputType_PushPull);
    }
    return true;
}

/********************************************************************************************************************************
 * Device Unit configuration                                                                                                   */

/* Data structure containing the context for initialization: */
typedef struct Esp32DriverLEDConfigContext {
    LTDeviceConfig       *pDeviceConfig;
    LEDGroupInstance     *pNextInstanceToInitialize;       /* pointer to next place in s_DeviceUnits.pDeviceUnits to place a Device Unit instance */
    u32                   driverConfigOffset;              /* The Device Config offset for this Driver */
    u32                   nInitialNumDeviceUnitInstances;  /* number of Device units in the Device Config for this Driver Library */
} Esp32DriverLEDConfigContext;

LT_STATIC_ASSERT_SIZE_32_64(Esp32DriverLEDConfigContext, 16, 24);

/* Reclaim all resources used by the context: */
static Esp32DriverLEDConfigContext *DestroyConfigurationContext(Esp32DriverLEDConfigContext *pContext) {
    if (pContext) {
        lt_closelibrary(pContext->pDeviceConfig);
        lt_free(pContext);
    }
    return NULL;
}

/* Allocate memory for the context, and fill in as much as possible: */
static Esp32DriverLEDConfigContext *CreateConfigurationContext(void) {
    Esp32DriverLEDConfigContext *pContext = lt_malloc(sizeof(Esp32DriverLEDConfigContext));
    if (   !pContext
        || !(pContext->pDeviceConfig                  = lt_openlibrary(LTDeviceConfig))
        || !(pContext->driverConfigOffset             = pContext->pDeviceConfig->GetDriverSection("LTDeviceLED", "Esp32DriverLED"))
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
static bool ConfigureDeviceUnit(Esp32DriverLEDConfigContext *pContext, u32 nDeviceUnitIndex) {
    if (s_DeviceUnits.nNumDeviceUnits >= pContext->nInitialNumDeviceUnitInstances) {
        /* All the allocated Device Unit instance storage has already been written.  Ignore any additional configurations: */
        LTLOG_REDALERT("cdu.config.excess", NULL);
        return false;
    }
    LEDGroupInstance *pInstance = pContext->pNextInstanceToInitialize;
    u32 deviceUnitSection = pContext->pDeviceConfig->GetDeviceUnitSectionAt(pContext->driverConfigOffset, nDeviceUnitIndex);
    if (!deviceUnitSection) {
        LTLOG_YELLOWALERT("cdu.no", NULL);
        return false;
    }
    LTResourceValue duPinNameValue;
    if (!LT_GetCore()->ReadResourceValue(pContext->pDeviceConfig->GetResourceTree(), deviceUnitSection, "pin", &duPinNameValue)) {
        LTLOG_YELLOWALERT("cdu.pin.no", NULL);
        return false;
    }
    if (duPinNameValue.type != kLTResourceValueType_String) {
        LTLOG_YELLOWALERT("cdu.pin.type", NULL);
        return false;
    }
    u32 nPinDeviceUnitIndex = LT_U32_MAX;
    if (!s_pDevicePins->GetUnitNumberFromBankName(duPinNameValue.string, &nPinDeviceUnitIndex)) {
        LTLOG_YELLOWALERT("cdu.pin.nf", NULL);
        return false;
    }
    if (!(pInstance->pName = pContext->pDeviceConfig->ReadString(deviceUnitSection, "name"))) {
        LTLOG_YELLOWALERT("cdu.name", NULL);
        return false;
    }
    const char *pActive = pContext->pDeviceConfig->ReadString(deviceUnitSection, "active");
    pInstance->bActiveHigh = true;      /* default to active-high if "active" attribute is bad or missing */
    if (pActive) {
             if (!lt_strcmp(pActive, "low" )) pInstance->bActiveHigh = false;
        else if ( lt_strcmp(pActive, "high")) LTLOG_YELLOWALERT("cdu.pin.active", "\"%s\" invalid", pActive);
    }
    if (!InitializeDeviceUnit(pInstance, nPinDeviceUnitIndex)) {
        LTLOG_YELLOWALERT("cdu.init", "init of LED \"%s\" failed", pInstance->pName);
        return false;
    } else {
        /* Successfully configured this Device Unit instance.
           Count it and advance to the next place in the Device Unit instance array: */
        pInstance->groupDescriptor.m_pGroupName        = pInstance->pName;
        pInstance->groupDescriptor.m_groupType         = kLTDeviceLED_GroupType_IndicatorLamp;
        pInstance->groupDescriptor.m_nNumElements      = 1;
        pInstance->groupDescriptor.m_nDeviceUnitNumber = s_DeviceUnits.nNumDeviceUnits;
        ++pContext->pNextInstanceToInitialize;
        ++s_DeviceUnits.nNumDeviceUnits;
        LTLOG_DEBUG("cdu", "pin %lu: \"%s\", active-%s", LT_Pu32(nPinDeviceUnitIndex), pInstance->pName, pInstance->bActiveHigh ? "high" : "low");
    }
    return true;
}

static bool ConfigureDeviceUnits(void) {
    if (s_DeviceUnits.nNumDeviceUnits || s_DeviceUnits.pDeviceUnits) return false;   /* already configured - do not allocate and configure again */
    Esp32DriverLEDConfigContext *pContext = CreateConfigurationContext();
    if (!pContext) {
        LTLOG_REDALERT("cdus.context", NULL);
        return false;
    }
    LT_SIZE nInstanceStorageBytes = pContext->nInitialNumDeviceUnitInstances * sizeof(LEDGroupInstance);
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
        lt_realloc(s_DeviceUnits.pDeviceUnits, s_DeviceUnits.nNumDeviceUnits * sizeof(LEDGroupInstance));
    } else {
        LTLOG_DEBUG("du.n", "%lu", LT_Pu32(s_DeviceUnits.nNumDeviceUnits));
    }
    DestroyConfigurationContext(pContext);
    return true;
}

/*******************************************************************************
 * Cleanup or bailure
 * Tear down all Device Units and reclaim resources.                          */

static bool Shutdown(Esp32DriverLEDConfigContext *pContext) {
    DestroyConfigurationContext(pContext);
    if (s_DeviceUnits.pDeviceUnits) {
        LEDGroupInstance * pInstance = s_DeviceUnits.pDeviceUnits;
        for (u32 i = s_DeviceUnits.nNumDeviceUnits; i; --i, ++pInstance) if (pInstance->hPinBank) LT_GetCore()->DestroyHandle(pInstance->hPinBank);
        lt_free(s_DeviceUnits.pDeviceUnits);
        s_DeviceUnits.pDeviceUnits = NULL;
    }
    s_DeviceUnits.nNumDeviceUnits = 0;
    lt_closelibrary(s_pDevicePins); s_pDevicePins = NULL;
    return false;
}

/********************************************************************************************************************************
 * Library initialization and finalization                                                                                     */

static bool Esp32DriverLEDImpl_LibInit(void) {
    LTLOG_DEBUG("init", NULL);
    return (   (s_pDevicePins = (LTDevicePins *)LT_GetCore()->OpenLibrary("LTDevicePins"))
            && ConfigureDeviceUnits()) ? true : Shutdown(NULL);
}

static void Esp32DriverLEDImpl_LibFini(void) { LTLOG_DEBUG("fini", NULL); Shutdown(NULL); }

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceLED, Esp32DriverLED);

LTLIBRARY_EXPORT_INTERFACES(Esp32DriverLED, (ILTDriverLED))

/***********************************************************************************************************************
 * IndicatorLamp Group interface                                                                                      */

/* This Driver supports only one indicator per group.  Return an instance pointer only if the group handle is valid
 * and nIndex is 0: */
static LEDGroupInstance *ValidInstance(LTDeviceUnit hGroup, u32 nLEDIndex) {
    LEDGroupInstance *pInstance = InstanceFromHandle(hGroup);
    return nLEDIndex ? NULL : pInstance;
}

static void SetLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nColor) {
    Set(ValidInstance(hGroup, nLEDIndex), nColor & 0xFF000000 ? 1 : 0); /* any nz intensity means on */
}

static void MaskLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nMask, u32 nColor) {
    SetLEDColor(hGroup, nLEDIndex, nColor &= nMask);
}

static u32 CommonDriverLED_IndicatorLamp_GetLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex) {
    return Read(ValidInstance(hGroup, nLEDIndex)) ? 0xFFFFFFFF : 0;
}

static void CommonDriverLED_IndicatorLamp_GetLEDColors(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nCount, u32 * pBufferToFill) {
    for (; nCount; --nCount, ++pBufferToFill) *pBufferToFill = Read(ValidInstance(hGroup, nLEDIndex++));
}

static void CommonDriverLED_IndicatorLamp_SetLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nColor) {
    SetLEDColor(hGroup, nLEDIndex, nColor);
}

static void CommonDriverLED_IndicatorLamp_SetLEDColors(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nCount, u32 * pColors) {
    for (; nCount; --nCount, ++nLEDIndex, ++pColors) SetLEDColor(hGroup, nLEDIndex, *pColors);
}

static void CommonDriverLED_IndicatorLamp_MaskLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nMask, u32 nColor) {
    MaskLEDColor(hGroup, nLEDIndex, nMask, nColor);
}

static void CommonDriverLED_IndicatorLamp_MaskLEDColors(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nCount, u32 nMask, u32 * pColors) {
    for (; nCount; --nCount, ++nLEDIndex, ++pColors) MaskLEDColor(hGroup, nLEDIndex, nMask, *pColors);
}

static u32 CommonDriverLED_IndicatorLamp_GetNumberOfElements(LTDeviceUnit hGroup) { LT_UNUSED(hGroup); return 1; }

define_LTLIBRARY_INTERFACE(ILTDriverLED_GroupType_IndicatorLamp)
    .GetLEDColor         = CommonDriverLED_IndicatorLamp_GetLEDColor,
    .GetLEDColors        = CommonDriverLED_IndicatorLamp_GetLEDColors,
    .SetLEDColor         = CommonDriverLED_IndicatorLamp_SetLEDColor,
    .SetLEDColors        = CommonDriverLED_IndicatorLamp_SetLEDColors,
    .MaskLEDColor        = CommonDriverLED_IndicatorLamp_MaskLEDColor,
    .MaskLEDColors       = CommonDriverLED_IndicatorLamp_MaskLEDColors,
    .GetNumberOfElements = CommonDriverLED_IndicatorLamp_GetNumberOfElements
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Feb-23   constantine created
 *  20-Apr-23   constantine converted to the new Device Config Arbolation hotness
 */
