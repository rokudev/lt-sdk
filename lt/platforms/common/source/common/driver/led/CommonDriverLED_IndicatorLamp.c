/***********************************************************************************************************************
 * <platforms/common/source/common/driver/led/CommonDriverLEDImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * LT Driver Library for <CHIP_NAME> LED controller
 **********************************************************************************************************************/
/** @file CommonDriverLEDImpl.c Implementation of LED control Driver for <CHIP_NAME> */

#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/led/LTDeviceLED.h>

DEFINE_LTLOG_SECTION(LT_STRINGIFY(CHIP_NAME) "driver");

#define MAKE_API(chip, fn) LTChip##chip##_##fn
#define CHIP_API(chip, fn) MAKE_API(chip, fn)

#define MAKE_DRIVER_API2(drivername, fn) drivername##Impl_##fn
#define MAKE_DRIVER_API1(drivername, fn) MAKE_DRIVER_API2(drivername, fn)
#define DRIVER_API(fn) MAKE_DRIVER_API1(DRIVER_NAME, fn)

#define MAKE_DRIVER_NAME2(chip) CommonDriverLED##chip
#define MAKE_DRIVER_NAME1(chip) MAKE_DRIVER_NAME2(chip)
#define DRIVER_NAME             MAKE_DRIVER_NAME1(CHIP_NAME)

#define GET_IMPORTED_LIBRARIES_INSTANCE(libName) LT_Get##libName()
#define GET_IMPORTED_LIBRARIES_LIST_EXPAND(deviceSequence) LTTYPES_JOIN_ARGUMENTS(GET_IMPORTED_LIBRARIES_LIST_1 deviceSequence, _END)
#define GET_IMPORTED_LIBRARIES_LIST_1(libName, ...) GET_IMPORTED_LIBRARIES_INSTANCE(libName) LTTYPES_DEFERRED_COMMA_EXPANSION GET_IMPORTED_LIBRARIES_LIST_2
#define GET_IMPORTED_LIBRARIES_LIST_2(libName, ...) GET_IMPORTED_LIBRARIES_INSTANCE(libName) LTTYPES_DEFERRED_COMMA_EXPANSION GET_IMPORTED_LIBRARIES_LIST_1
#define GET_IMPORTED_LIBRARIES_LIST_1_END
#define GET_IMPORTED_LIBRARIES_LIST_2_END

#define GET_IMPORTED_LIBRARIES_LIST GET_IMPORTED_LIBRARIES_LIST_EXPAND(IMPORTED_LIBRARIES)

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(DRIVER_NAME, IMPORTED_LIBRARIES);

static const ILTDriverLED_GroupType_IndicatorLamp s_ILTDriverLED_GroupType_IndicatorLamp;
/***********************************************************************************************************************
 * Driver configuration.                                                                                              */

typedef struct {
    LTDeviceLED_GroupDescriptor groupDescriptor;
    void                      * pChip;         /**< Control structure for <CHIP_NAME> driver IC  */
    void                      * pChipConfig;
    LTMutex                   * mutex;         /**< Mutex protection, mainly for reference count     */
    u32                         nRefCount;     /**< How many clients have a LTHandle to this Group   */
} LEDGroupInstance;

#include LT_STRINGIFY(CONFIGURATION_FILE)

enum { kNumLEDDeviceUnits = sizeof(s_LEDGroupInstance) / sizeof(s_LEDGroupInstance[0]) };

/***********************************************************************************************************************
 * LED Group instance and LED instance access helpers                                                                 */

/* Retrieve a pointer to the Group Instance data from the Device Unit handle.
 * Return 0 if the handle or the private data pointer are invalid. */
static LEDGroupInstance * InstanceFromHandle(LTDeviceUnit hDevice) {
    LEDGroupInstance ** ppInstance = (LEDGroupInstance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    LEDGroupInstance * pInstance = NULL;
    if (ppInstance) {
        pInstance = *ppInstance;
        LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
    }
    return pInstance;
}

/***********************************************************************************************************************
 * Generic Driver interface - Initialization, finalization, and Device Unit access                                    */

static void DRIVER_API(LibFini)(void);

/* Library initialization upon opening - initialize reference-count objects for all Device Units:                     */
static bool DRIVER_API(LibInit)(void) {
    for (int nIndex = 0; nIndex < kNumLEDDeviceUnits; ++nIndex) {
        LEDGroupInstance * pInstance = s_LEDGroupInstance[nIndex];
        pInstance->nRefCount = 0;
        pInstance->groupDescriptor.m_nDeviceUnitNumber = nIndex;
        if (!(pInstance->mutex = lt_createobject(LTMutex))) {
            LTLOG_YELLOWALERT("fail.mutex.create", "Create mutex failed");
            DRIVER_API(LibFini)();
            return false;
        }
    }
    return true;
}

/* Library finalization - tear down instance data upon library close or upon failure of Library open:                 */
static void DRIVER_API(LibFini)(void) {
    for (int i = kNumLEDDeviceUnits - 1; i >= 0; --i) {
        LEDGroupInstance * pInstance = s_LEDGroupInstance[i];
        if (pInstance->mutex) {
            lt_destroyobject(pInstance->mutex);
            pInstance->mutex = NULL;
        }
    }
}

static u32 DRIVER_API(GetNumDeviceUnits)(void) {
    return kNumLEDDeviceUnits;
}

/* Provide a Device Unit handle.  Furnish the handle with a pointer to the respective instance data.
 * Initialize the SPI control blocks for the LED Group if this is the first handle for the Group. */

static LTDeviceUnit DRIVER_API(CreateDeviceUnitHandle)(u32 nDeviceUnitNumber) {
    LTDeviceUnit hDevice = 0;
    if (nDeviceUnitNumber < kNumLEDDeviceUnits)
        hDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverLED_GroupType_IndicatorLamp, sizeof(LEDGroupInstance *));
    if (hDevice) {
        bool bInterfaceOK = false;  /* A handle has been created.  Do not leak it if something goes
                                       wrong with preparing the handle or initializing the interface. */
        LEDGroupInstance ** ppInstance = (LEDGroupInstance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
        if (ppInstance) {
            LEDGroupInstance * pInstance = *ppInstance = s_LEDGroupInstance[nDeviceUnitNumber];
            bInterfaceOK = true;    /* all okay, unless first-reference initialization (below) fails */
            pInstance->mutex->API->Lock(pInstance->mutex);
            if (++pInstance->nRefCount == 1) {   /* Just starting up this instance. */
                bInterfaceOK = (pInstance->pChip = CHIP_API(CHIP_NAME, Initialize)(GET_IMPORTED_LIBRARIES_LIST pInstance->pChipConfig));
            }
            pInstance->mutex->API->Unlock(pInstance->mutex);
            LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
        }
        if (!bInterfaceOK) {
            LT_GetCore()->DestroyHandle(hDevice);
            hDevice = 0;
        }
    }
    return hDevice;
}

/* Reclaim a Device Unit handle.  Retrieve the refcount, and shut down
 * if there are no more refs. */
static void CommonDriverLEDImplDeviceUnit_OnDestroyHandle(LTHandle hDevice) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hDevice);
    if (pInstance) {
        pInstance->mutex->API->Lock(pInstance->mutex);
        if (pInstance->nRefCount > 0) {
            if (--pInstance->nRefCount == 0) {
                CHIP_API(CHIP_NAME, Finalize)(pInstance->pChip);
            }
        }
        pInstance->mutex->API->Unlock(pInstance->mutex);
    }
}

/***********************************************************************************************************************
 * ILTDriverLED interface                                                                                             */

static bool DRIVER_API(GetGroupDescriptorFromUnitNumber)(u32 nDeviceUnitNumber, LTDeviceLED_GroupDescriptor * pDescriptor) {
    if (nDeviceUnitNumber < kNumLEDDeviceUnits) {
        *pDescriptor = s_LEDGroupInstance[nDeviceUnitNumber]->groupDescriptor;
        return true;
    }
    return false;
}

static bool DRIVER_API(GetUnitNumberFromGroupName)(char const * pGroupName, u32 * pDeviceUnitNumberToSet) {
    for (u32 i = 0; i < kNumLEDDeviceUnits; ++i) {
        LTDeviceLED_GroupDescriptor * pThisDescriptor = &s_LEDGroupInstance[i]->groupDescriptor;
        if (lt_strcmp(pGroupName, pThisDescriptor->m_pGroupName) == 0) {
            *pDeviceUnitNumberToSet = i;
            return true;
        }
    }
    return false;
}

define_LTLIBRARY_INTERFACE(ILTDriverLED) {
    .GetGroupDescriptorFromUnitNumber = DRIVER_API(GetGroupDescriptorFromUnitNumber),
    .GetUnitNumberFromGroupName       = DRIVER_API(GetUnitNumberFromGroupName)
} LTLIBRARY_DEFINITION;

#define DEFINE_DRIVER(drivername) define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceLED, drivername);
#define EXPORT_INTERFACES(drivername) LTLIBRARY_EXPORT_INTERFACES(drivername, (ILTDriverLED))

DEFINE_DRIVER(DRIVER_NAME)
EXPORT_INTERFACES(DRIVER_NAME)

/***********************************************************************************************************************
 * IndicatorLamp Group interface                                                                                      */

static void SetLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nColor) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    CHIP_API(CHIP_NAME, SetColor)(pInstance->pChip, nLEDIndex, nColor);
}

static void MaskLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nMask, u32 nColor) {
    SetLEDColor(hGroup, nLEDIndex, nColor &= nMask);
}

static void Refresh(LTDeviceUnit hGroup) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    CHIP_API(CHIP_NAME, Refresh)(pInstance->pChip);
}

static u32 CommonDriverLED_IndicatorLamp_GetLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    return CHIP_API(CHIP_NAME, GetColor)(pInstance->pChip, nLEDIndex);
}

static void CommonDriverLED_IndicatorLamp_GetLEDColors(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nCount, u32 * pBufferToFill) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    for (; nCount; --nCount, ++pBufferToFill) *pBufferToFill = CHIP_API(CHIP_NAME, GetColor)(pInstance->pChip, nLEDIndex++);
}

static void CommonDriverLED_IndicatorLamp_SetLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nColor) {
    SetLEDColor(hGroup, nLEDIndex, nColor);
    Refresh(hGroup);
}

static void CommonDriverLED_IndicatorLamp_SetLEDColors(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nCount, u32 * pColors) {
    for (; nCount; --nCount, ++nLEDIndex, ++pColors) SetLEDColor(hGroup, nLEDIndex, *pColors);
    Refresh(hGroup);
}

static void CommonDriverLED_IndicatorLamp_MaskLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nMask, u32 nColor) {
    MaskLEDColor(hGroup, nLEDIndex, nMask, nColor);
    Refresh(hGroup);
}

static void CommonDriverLED_IndicatorLamp_MaskLEDColors(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nCount, u32 nMask, u32 * pColors) {
    for (; nCount; --nCount, ++nLEDIndex, ++pColors) MaskLEDColor(hGroup, nLEDIndex, nMask, *pColors);
    Refresh(hGroup);
}

static u32 CommonDriverLED_IndicatorLamp_GetNumberOfElements(LTDeviceUnit hGroup) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    return pInstance ? pInstance->groupDescriptor.m_nNumElements : 0;
}

define_LTLIBRARY_INTERFACE(ILTDriverLED_GroupType_IndicatorLamp, CommonDriverLEDImplDeviceUnit_OnDestroyHandle)
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
 *  05-Nov-21   trajan      created
 */
