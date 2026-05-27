/*******************************************************************************
 * platforms/linux/source/linux/driver/led/DriverLEDImpl.c>
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Linux LT Driver Library for LED access
 ******************************************************************************/
/** @file LinuxDriverLEDImpl.c Implementation of LED driver */

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/led/LTDeviceLED.h>
#include <lt/device/pins/LTDevicePins.h>

DEFINE_LTLOG_SECTION("linux.drv.led");

static const ILTDriverLED_GroupType_IndicatorLamp s_ILTDriverLED_GroupType_IndicatorLamp;

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LinuxDriverLED, (LTDeviceConfig) (LTDevicePins) );

typedef ILTDriverPins_OutputBank        IPinOut;

typedef struct {
    LTDeviceUnit hPin;
    void (* fnSet)(LTDeviceUnit hPinBank, u32 pinBits);
} LEDPin;

/*******************************************************************************
 * LED Group descriptions                                                     */

typedef struct {
    LTDeviceLED_GroupDescriptor           groupDescriptor;
    const char                           *pName;                /* C-string name of the LED group, stored in Device Config.
                                                                   NOTE: LTDeviceConfig must remain open for the data
                                                                         dereferenced by this pointer to remain valid.    */
    u32                                   color;                /* Current color */
    LEDPin                                red;
    LEDPin                                green;
    LEDPin                                blue;
} LEDGroupInstance;

static struct {
    LEDGroupInstance     *pNextInstanceToInitialize;       /* pointer to next place in DeviceUnits.pDeviceUnits to place a Device Unit instance */
    u32                   driverConfigOffset;              /* The Device Config offset for this Driver */
    u32                   nInitialNumDeviceUnitInstances;  /* number of Device units in the Device Config for this Driver Library */
    /* Container for all the LED group instances */
    struct {
        LEDGroupInstance *pDeviceUnits;     /* Pointer into the heap where the Device Units are stored (as an array) */
        u32               nNumDeviceUnits;  /* How many Device Units this Driver supplies                            */
    } DeviceUnits;
} S;

/********************************************************************************************************************************
 * Access to Device Unit numbers and names:                                                                                    */
static bool LinuxDriverLED_GetGroupDescriptorFromUnitNumber(u32 nDeviceUnitIndex, LTDeviceLED_GroupDescriptor *pDescriptor) {
    if (nDeviceUnitIndex >= S.DeviceUnits.nNumDeviceUnits) return false;
    *pDescriptor = (S.DeviceUnits.pDeviceUnits + nDeviceUnitIndex)->groupDescriptor;
    return true;
}

static bool LinuxDriverLED_GetUnitNumberFromGroupName(char const * pGroupName, u32 * pDeviceUnitIndexToSet) {
    for (u32 i = 0; i < S.DeviceUnits.nNumDeviceUnits; i++) {
        if (!lt_strcmp(pGroupName, (S.DeviceUnits.pDeviceUnits + i)->groupDescriptor.m_pGroupName)) {
            *pDeviceUnitIndexToSet = i;
            return true;
        }
    }
    return false;
}

define_LTLIBRARY_INTERFACE(ILTDriverLED) {
    .GetGroupDescriptorFromUnitNumber = LinuxDriverLED_GetGroupDescriptorFromUnitNumber,
    .GetUnitNumberFromGroupName       = LinuxDriverLED_GetUnitNumberFromGroupName
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

static void Set(LEDGroupInstance *pInstance, u32 color) {
    if (!pInstance) return;

    pInstance->color = color;
    if (pInstance->red.fnSet)   pInstance->red.fnSet(pInstance->red.hPin,     ((color & 0x00FF0000) >> 16) ? 1 : 0);
    if (pInstance->green.fnSet) pInstance->green.fnSet(pInstance->green.hPin, ((color & 0x0000FF00) >>  8) ? 1 : 0);
    if (pInstance->blue.fnSet)  pInstance->blue.fnSet(pInstance->blue.hPin,   ((color & 0x000000FF)      ) ? 1 : 0);
}

static u32 Read(LEDGroupInstance *pInstance) {
    if (!pInstance) return 0;
    return pInstance->color;
}

static u32 LinuxDriverLEDImpl_GetNumDeviceUnits(void) { return S.DeviceUnits.nNumDeviceUnits; }

/********************************************************************************************************************************
 * Device-unit creation interface.                                                                                             */
static LTDeviceUnit LinuxDriverLEDImpl_CreateDeviceUnitHandle(u32 nDeviceUnitIndex) {
    LTDeviceUnit hDevice = 0;
    if (nDeviceUnitIndex < S.DeviceUnits.nNumDeviceUnits)
        hDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverLED_GroupType_IndicatorLamp, sizeof(LEDGroupInstance *));
    if (hDevice) {
        LEDGroupInstance **ppInstance = (LEDGroupInstance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
        if (ppInstance) {
            *ppInstance = S.DeviceUnits.pDeviceUnits + nDeviceUnitIndex;
            LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
        } else {
            LT_GetCore()->DestroyHandle(hDevice);
            hDevice = 0;
        }
    }
    return hDevice;
}

/********************************************************************************************************************************
 * Device Unit configuration                                                                                                   */

static LEDPin GetPinFromName(const char *pinName) {
    LEDPin p;
    u32 nDevIdx = 0;
    LTDevicePin_PinType pinType;

    do {
        if (!LT_GetLTDevicePins()->GetUnitNumberFromBankName(pinName, &nDevIdx)) break;
        if (!LT_GetLTDevicePins()->GetBankTypeFromUnitNumber(nDevIdx, &pinType)) break;
        p.hPin = LT_GetLTDevicePins()->CreateDeviceUnitHandle(nDevIdx);
        if (!p.hPin) break;

        // Extract the Set function from the correct interface. This saves checking for pin type after init.
        if (pinType == kLTDevicePin_PinType_Output) {
            ILTDriverPins_OutputBank *iface = (ILTDriverPins_OutputBank *)lt_gethandleinterface(ILTDriverPins_OutputBank, p.hPin);
            if (iface) {
                p.fnSet = iface->Set;
                return p;
            }
        } else if (pinType == kLTDevicePin_PinType_Bidirectional) {
            ILTDriverPins_BidirectionalBank *iface = (ILTDriverPins_BidirectionalBank *)lt_gethandleinterface(ILTDriverPins_BidirectionalBank, p.hPin);
            if (iface) {
                iface->ConfigureAsOutput(p.hPin, kLTDevicePin_PinConfiguration_OutputType_PushPull);
                p.fnSet = iface->Set;
                return p;
            }
        }
    } while (0);

    LT_GetCore()->DestroyHandle(p.hPin);
    lt_memset(&p, 0, sizeof(LEDPin));
    return p;
}


/* Data structure containing the context for initialization: */


/* Configure one Device Unit instance from the configuration information in the Device Config.
   If the Device Unit instance is successfully configured, advance the instance pointer to the next instance,
   and increment the number of available Device Units.  Otherwise, leave the pointer and count alone, and the
   next iteration of the callback will rewrite the current instance (if that happens, ConfigureDeviceUnits()
   reallocs the Device Unit instance array to trim off the unused portion).
   Return true upon success, false if any part of obtaining the configuration information from the Device
   Config fails (this causes the loop iterating on Device Units to terminate early): */
static bool ConfigureDeviceUnit(u32 nDeviceUnitIndex) {
    if (S.DeviceUnits.nNumDeviceUnits >= S.nInitialNumDeviceUnitInstances) {
        /* All the allocated Device Unit instance storage has already been written.  Ignore any additional configurations: */
        LTLOG_REDALERT("cdu.config.excess", NULL);
        return false;
    }
    LEDGroupInstance *pInstance = S.pNextInstanceToInitialize;
    LTDeviceConfig *pDeviceConfig = LT_GetLTDeviceConfig();
    u32 deviceUnitSection = pDeviceConfig->GetDeviceUnitSectionAt(S.driverConfigOffset, nDeviceUnitIndex);
    if (!deviceUnitSection) {
        LTLOG_YELLOWALERT("cdu.no", NULL);
        return false;
    }

    if (!(pInstance->pName = pDeviceConfig->ReadString(deviceUnitSection, "name"))) {
        LTLOG_YELLOWALERT("cdu.name", NULL);
        return false;
    }

    LTResourceValue duPinNameValue;

    if (LT_GetCore()->ReadResourceValue(pDeviceConfig->GetResourceTree(), deviceUnitSection, "pin", &duPinNameValue)) {
        // Generic pin treat as red
        if (duPinNameValue.type == kLTResourceValueType_String) {
            pInstance->red = GetPinFromName(duPinNameValue.string);
        }
    }
    if (LT_GetCore()->ReadResourceValue(pDeviceConfig->GetResourceTree(), deviceUnitSection, "rpin", &duPinNameValue)) {
        if (duPinNameValue.type == kLTResourceValueType_String) {
            pInstance->red = GetPinFromName(duPinNameValue.string);
        }
    }
    if (LT_GetCore()->ReadResourceValue(pDeviceConfig->GetResourceTree(), deviceUnitSection, "gpin", &duPinNameValue)) {
        if (duPinNameValue.type == kLTResourceValueType_String) {
            pInstance->green = GetPinFromName(duPinNameValue.string);
        }
    }
    if (LT_GetCore()->ReadResourceValue(pDeviceConfig->GetResourceTree(), deviceUnitSection, "bpin", &duPinNameValue)) {
        if (duPinNameValue.type == kLTResourceValueType_String) {
            pInstance->blue = GetPinFromName(duPinNameValue.string);
        }
    }

    /* Successfully configured this Device Unit instance.
        Count it and advance to the next place in the Device Unit instance array: */
    pInstance->groupDescriptor.m_pGroupName        = pInstance->pName;
    pInstance->groupDescriptor.m_groupType         = kLTDeviceLED_GroupType_IndicatorLamp;
    pInstance->groupDescriptor.m_nNumElements      = 1;
    pInstance->groupDescriptor.m_nDeviceUnitNumber = S.DeviceUnits.nNumDeviceUnits;
    ++S.pNextInstanceToInitialize;
    ++S.DeviceUnits.nNumDeviceUnits;
    LTLOG("cdu.success", "Initialized LED \"%s\"", pInstance->pName);

    return true;
}

static bool ConfigureDeviceUnits(void) {
    if (S.DeviceUnits.nNumDeviceUnits || S.DeviceUnits.pDeviceUnits) return false;   /* already configured - do not allocate and configure again */

    S.driverConfigOffset = LT_GetLTDeviceConfig()->GetDriverSection("LTDeviceLED", "LinuxDriverLED");
    if (!S.driverConfigOffset) {
        LTLOG_REDALERT("cdus.no.drvsec", NULL);
        return false;
    }

    S.nInitialNumDeviceUnitInstances = LT_GetLTDeviceConfig()->GetNumDeviceUnits(S.driverConfigOffset);
    if (!S.nInitialNumDeviceUnitInstances) {
        LTLOG_REDALERT("cdus.no.numdev", NULL);
        return false;
    }

    LT_SIZE nInstanceStorageBytes = S.nInitialNumDeviceUnitInstances * sizeof(LEDGroupInstance);
    if (!(S.DeviceUnits.pDeviceUnits = S.pNextInstanceToInitialize = lt_malloc(nInstanceStorageBytes))) {
        LTLOG_REDALERT("cdus.oom", NULL);
        return false;
    }

    lt_memset(S.DeviceUnits.pDeviceUnits, 0, nInstanceStorageBytes);        /* cleanliness is next to godliness */

    for (u32 i = 0; i < S.nInitialNumDeviceUnitInstances; i++) {
        ConfigureDeviceUnit(i);
    }

    if (!S.DeviceUnits.nNumDeviceUnits || S.DeviceUnits.nNumDeviceUnits > S.nInitialNumDeviceUnitInstances) {
        LTLOG_REDALERT("du.n.fault", "expected %lu; actual %lu", LT_Pu32(S.nInitialNumDeviceUnitInstances), LT_Pu32(S.DeviceUnits.nNumDeviceUnits));
        return false;
    } else if (S.DeviceUnits.nNumDeviceUnits < S.nInitialNumDeviceUnitInstances) {
        /* Ended up with fewer Device Units than expected.  Give back some memory and attempt to carry on: */
        LTLOG_YELLOWALERT("du.n.fewer", "expected %lu; actual %lu", LT_Pu32(S.nInitialNumDeviceUnitInstances), LT_Pu32(S.DeviceUnits.nNumDeviceUnits));
        lt_realloc(S.DeviceUnits.pDeviceUnits, S.DeviceUnits.nNumDeviceUnits * sizeof(LEDGroupInstance));
    } else {
        LTLOG_DEBUG("du.n", "%lu", LT_Pu32(S.DeviceUnits.nNumDeviceUnits));
    }
    return true;
}

/*******************************************************************************
 * Cleanup or bailure
 * Tear down all Device Units and reclaim resources.                          */
static bool Shutdown(void) {
    // Delete all device units
    if (S.DeviceUnits.pDeviceUnits) {
        LEDGroupInstance * pInstance = S.DeviceUnits.pDeviceUnits;
        for (u32 i = S.DeviceUnits.nNumDeviceUnits; i; --i, ++pInstance) {
            LT_GetCore()->DestroyHandle(pInstance->red.hPin);
            LT_GetCore()->DestroyHandle(pInstance->green.hPin);
            LT_GetCore()->DestroyHandle(pInstance->blue.hPin);
        }
        lt_free(S.DeviceUnits.pDeviceUnits);
        S.DeviceUnits.pDeviceUnits = NULL;
    }
    S.DeviceUnits.nNumDeviceUnits = 0;
    return false;
}

/********************************************************************************************************************************
 * Library initialization and finalization                                                                                     */

static bool LinuxDriverLEDImpl_LibInit(void) {
    return ConfigureDeviceUnits();
}

static void LinuxDriverLEDImpl_LibFini(void) { Shutdown(); }

/***********************************************************************************************************************
 * IndicatorLamp Group interface                                                                                      */

/* This Driver supports only one indicator per group.  Return an instance pointer only if the group handle is valid
 * and nIndex is 0: */
static LEDGroupInstance *ValidInstance(LTDeviceUnit hGroup, u32 nLEDIndex) {
    LEDGroupInstance *pInstance = InstanceFromHandle(hGroup);
    return nLEDIndex ? NULL : pInstance;
}

static void SetLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nColor) {
    Set(ValidInstance(hGroup, nLEDIndex), nColor);
}

static void MaskLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nMask, u32 nColor) {
    SetLEDColor(hGroup, nLEDIndex, nColor &= nMask);
}

static u32 CommonDriverLED_IndicatorLamp_GetLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex) {
    return Read(ValidInstance(hGroup, nLEDIndex));
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

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceLED, LinuxDriverLED);

LTLIBRARY_EXPORT_INTERFACES (LinuxDriverLED, (ILTDriverLED) (ILTDriverLED_GroupType_IndicatorLamp));

