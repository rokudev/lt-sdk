/***********************************************************************************************************************
 * <st/source/st/driver/led/STDriverLEDImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * ST LT Driver Library for LED control
 **********************************************************************************************************************/
/** @file STDriverLED.c Implementation of LED control Driver for ST */

#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/led/LTDeviceLED.h>
#include "LTChipMAX7221.h"

#include "vendor-sdk/stmicro/stm32-L4S5-discovery-iot-node/Drivers/STM32L4xx_HAL_Driver/Inc/stm32l4xx_hal.h"

static const ILTDriverLED_GroupType_IndicatorLamp s_ILTDriverLED_GroupType_IndicatorLamp;
static const ILTDriverLED_GroupType_SevenSegmentDigit s_ILTDriverLED_GroupType_SevenSegmentDigit;

/***********************************************************************************************************************
 * Driver configuration.                                                                                              */

enum { kNumLEDDeviceUnits = 3 };

enum { kNumDigits = 8 };    /* Dan's breadboard has 8 digits, which is the maximum the MAX7221 can handle.
                               Change this to accommodate your setup. */

/***********************************************************************************************************************
 * LED Group Descriptor for the LED Group, to answer queries from the Device.                                         */

static LTDeviceLED_GroupDescriptor s_LEDGroupDescriptors[kNumLEDDeviceUnits] = {
    /* Device unit number is filled in during initialization. */
    #if 0   /* The GPIO output for LED1 is SPI1 clock, which is used for the display Group. */
    {
        .m_pGroupName        = "LD1",
        .m_groupType         = kLTDeviceLED_GroupType_IndicatorLamp,
        .m_nNumElements      = 1
    },
    #endif
    {
        .m_pGroupName        = "LD2",
        .m_groupType         = kLTDeviceLED_GroupType_IndicatorLamp,
        .m_nNumElements      = 1
    },
    {
        .m_pGroupName        = "LD34",
        .m_groupType         = kLTDeviceLED_GroupType_IndicatorLamp,
        .m_nNumElements      = 1
    },
    {
        .m_pGroupName        = "Display",
        .m_groupType         = kLTDeviceLED_GroupType_SevenSegmentDigit,
        .m_nNumElements      = kNumDigits
    }
};

/***********************************************************************************************************************
 * Data structures for an instance of an LED in an LED Group, and for the LED Group.
 * A LEDInstance specifies a single LED.  A LEDGroupInstance specifies an LED Group, and contains a list of
 * LEDInstances.                                                                                                      */

struct LEDInstance;

/**< Initialization and control of an LED
 *   @param pInstance pointer to the control block for the LED */
typedef void (LEDControlProc)(struct LEDInstance * pInstance);

/**< LED instance control block */
typedef struct LEDInstance {
    LEDControlProc * pInstanceControlProc;  /**< How to control this LED instance                                     */
    GPIO_TypeDef   * pPort;                 /**< Designation of the I/O port that controls the instance               */
    u32              nColor;                /**< The currently set color of the LED                                   */
    u16              pin;                   /**< Designation of the I/O pin that controls the instance                */
} LEDInstance;

typedef struct LEDGroupState {
    LEDInstance * pLEDs;        /**< Pointer to the LED array */
    u8            nNumLEDs;     /**< Number of LEDs in this group */
} LEDGroupState;

/***********************************************************************************************************************
 * Data structure for an LED Group instance:                                                                          */

struct LEDGroupInstance;

typedef void (InitializeInterfaceProc)(struct LEDGroupInstance * pInstance);
typedef void (FinalizeInterfaceProc)(struct LEDGroupInstance * pInstance);

typedef struct LEDGroupInstance {
    void        * pGroupState;          /**< Control structure for the group (Indicator lamp or display)              */
    LTMutex       hMutex;               /**< Mutex protection, mainly for reference count                             */
    u32           nRefCount;            /**< How many clients have an LTHandle to this instance                       */
    InitializeInterfaceProc * pInitializeInterfaceProc; /**< How to initialize the display driver interface           */
    FinalizeInterfaceProc   * pFinalizeInterfaceProc;   /**< How to tear down the display driver interface            */
} LEDGroupInstance;

/***********************************************************************************************************************
 * LED Group instance and LED instance data.
 * ST has a two single-color, single-intensity LEDs, and one pair of LEDs appearing to the firmware as one bicolor LED.
 * The Board of Things has a multi-digit seven-segment display.  */

static void InitializeSevenSegmentSPI(LEDGroupInstance * pInstance);
static void FinalizeSevenSegmentSPI(LEDGroupInstance * pInstance);
static void DisplayTransmit(u16 data);
static void InitializeLEDGroupGPIO(LEDGroupInstance * pInstance);
static void FinalizeLEDGroupGPIO(LEDGroupInstance * pInstance);
static void ControlSingleColorLED(LEDInstance * pLED);
static void ControlDualColorLED  (LEDInstance * pLED);

#if 0   /* The GPIO output for LED1 is SPI1 clock, which is used for the display Group, making
           it unavailable as an LED element. */
static LEDInstance s_LD1 = {
    .pPort                = GPIOA,
    .pin                  = GPIO_PIN_5,
    .pInstanceControlProc = ControlSingleColorLED,
    .nColor                = 0
};

static LEDGroupState s_LD1_group = {
    .pLEDs    = &s_LD1,
    .nNumLEDs = 1
};
#endif

static LEDInstance s_LD2 = {
    .pPort                = GPIOB,
    .pin                  = GPIO_PIN_14,
    .pInstanceControlProc = ControlSingleColorLED,
    .nColor = 0
};

static LEDGroupState s_LD2_group = {
    .pLEDs    = &s_LD2,
    .nNumLEDs = 1
};

static LEDInstance s_LD34 = {
    .pPort                = GPIOC,
    .pin                  = GPIO_PIN_9,
    .pInstanceControlProc = ControlDualColorLED,
    .nColor = 0
};

static LEDGroupState s_LD34_group = {
    .pLEDs    = &s_LD34,
    .nNumLEDs = 1
};

static LTChipMAX7221Digit s_Digits[kNumDigits];

static LTChipMAX7221DisplayState s_DisplayState = {
    .pDigits       = s_Digits,
    .pTransmitProc = DisplayTransmit,
    .nNumDigits    = kNumDigits
};

static LEDGroupInstance s_LEDGroupInstance[kNumLEDDeviceUnits] = {
    #if 0   /* The GPIO output for LED1 is SPI1 clock, which is used for the display Group. */
    {
        .pGroupState              = &s_LD1_group
        .nRefCount                = 0,
        .pInitializeInterfaceProc = InitializeLEDGroupGPIO,
        .pFinalizeInterfaceProc   = FinalizeLEDGroupGPIO
    },
    #endif
    {
        .pGroupState              = &s_LD2_group,
        .nRefCount                = 0,
        .pInitializeInterfaceProc = InitializeLEDGroupGPIO,
        .pFinalizeInterfaceProc   = FinalizeLEDGroupGPIO
    },
    {
        .pGroupState              = &s_LD34_group,
        .nRefCount                = 0,
        .pInitializeInterfaceProc = InitializeLEDGroupGPIO,
        .pFinalizeInterfaceProc   = FinalizeLEDGroupGPIO
    },
    {
        .pGroupState              = &s_DisplayState,
        .nRefCount                = 0,
        .pInitializeInterfaceProc = InitializeSevenSegmentSPI,
        .pFinalizeInterfaceProc   = FinalizeSevenSegmentSPI
    }
};

/***********************************************************************************************************************
 * LED Group instance and LED instance access helpers                                                                 */

/* Retrieve a pointer to the Group Instance data from the Device Unit handle.
 * Return 0 if the handle or the private data pointer are invalid. */
static LEDGroupInstance * InstanceFromHandle(LTDeviceUnit hDevice) {
    if (!hDevice) return NULL;
    LEDGroupInstance ** ppInstance = (LEDGroupInstance **)LT_GetCore()->GetHandlePrivateData(hDevice);
    return ppInstance ? *ppInstance : NULL;
}

/* Retrieve a pointer to the LED instance from the Device Unit handle and the LED index
 * into the LED Group.
 * Return 0 if the handle, the private data pointer, or the Group instance pointer are 0,
 * or if the index and count run off the end of the LED list in the LED Group. */
static LEDInstance * ThisLEDInstance(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nCount) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (!pInstance) return NULL;
    LEDGroupState * pGroupState = pInstance->pGroupState;
    if (nLEDIndex + nCount > pGroupState->nNumLEDs) return NULL;
    return pGroupState->pLEDs + nLEDIndex;
}

/***********************************************************************************************************************
 * Generic Driver interface - Initialization, finalization, and Device Unit access                                    */

static void STDriverLEDImpl_LibFini(void);

static ILTMutex  * s_iMutex  = NULL;

/* Library initialization upon opening - initialize reference-count objects for all Device Units:                     */
static bool STDriverLEDImpl_LibInit(void) {
    LEDGroupInstance * pInstance = s_LEDGroupInstance;
    LTDeviceLED_GroupDescriptor * pDescriptor = s_LEDGroupDescriptors;
    for (int i = 0; i < kNumLEDDeviceUnits; ++i, ++pInstance, ++pDescriptor) {
        pDescriptor->m_nDeviceUnitNumber = i;
        pInstance->nRefCount = 0;
        if (!(pInstance->hMutex = LT_GetCore()->CreateMutex())) {
            STDriverLEDImpl_LibFini();
            return false;
        }
        s_iMutex = lt_gethandleinterface(ILTMutex, pInstance->hMutex);
    }
    return true;
}

/* Library finalization - tear down instance data upon library close or upon failure of Library open:                 */
static void STDriverLEDImpl_LibFini(void) {
    LEDGroupInstance * pInstance = s_LEDGroupInstance;
    for (int i = kNumLEDDeviceUnits; i--; ++pInstance)
        if (pInstance->hMutex) {
            s_iMutex->Destroy(pInstance->hMutex);
            pInstance->hMutex = 0;
        }
}

static u32 STDriverLEDImpl_GetNumDeviceUnits(void) { return kNumLEDDeviceUnits; }

/* Provide a Device Unit handle.  Furnish the handle with a pointer to the respective instance data.
 * Initialize the GPIO control blocks for the LED Group if this is the first handle for the Group. */

static LTDeviceUnit STDriverLEDImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LTDeviceUnit hDevice = 0;
    bool bSevenSegment = false;
    if (nDeviceUnitNumber < kNumLEDDeviceUnits) {
        switch (s_LEDGroupDescriptors[nDeviceUnitNumber].m_groupType) {
        case kLTDeviceLED_GroupType_SevenSegmentDigit:
            bSevenSegment = true;
            hDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverLED_GroupType_SevenSegmentDigit, sizeof(LEDGroupInstance *));
            break;
        case kLTDeviceLED_GroupType_IndicatorLamp:
            hDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverLED_GroupType_IndicatorLamp, sizeof(LEDGroupInstance *));
            break;
        default:
            break;
        }
    }
    if (hDevice) {
        bool bInterfaceOK = false;  /* A handle has been created.  Do not leak it if something goes
                                       wrong with preparing the handle or initializing the interface. */
        LEDGroupInstance ** ppInstance = (LEDGroupInstance **)LT_GetCore()->GetHandlePrivateData(hDevice);
        if (ppInstance) {
            LEDGroupInstance * pInstance = *ppInstance = &s_LEDGroupInstance[nDeviceUnitNumber];
            s_iMutex->Lock(pInstance->hMutex);
            if (++pInstance->nRefCount == 1) {   /* Just starting up this instance. */
                (*pInstance->pInitializeInterfaceProc)(pInstance);
                bInterfaceOK = true;
                if (bSevenSegment) {
                    LTChipMAX7221DisplayState * pDisplayState = (LTChipMAX7221DisplayState *)pInstance->pGroupState;
                    bInterfaceOK = bSevenSegment ? LTChipMAX7221_Initialize(pDisplayState, true) : true;
                }
            }
            s_iMutex->Unlock(pInstance->hMutex);
        }
        if (!bInterfaceOK) {
            LT_GetCore()->DestroyHandle(hDevice);
            hDevice = 0;
        }
    }
    return hDevice;
}

/* Reclaim a Device Unit handle.  Retrieve the refcount, and shut down
 * the GPIO instances if there are no more refs. */
static void STLEDDeviceUnit_OnDestroyHandle(LTHandle hDevice) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hDevice);
    if (pInstance) {
        s_iMutex->Lock(pInstance->hMutex);
        if (pInstance->nRefCount > 0)
            if (--pInstance->nRefCount == 0)
                (*pInstance->pFinalizeInterfaceProc)(pInstance);
        s_iMutex->Unlock(pInstance->hMutex);
    }
}

/***********************************************************************************************************************
 * ILTDriverLED interface                                                                                             */

static bool STDriverLEDImpl_GetGroupDescriptorFromUnitNumber(u32 nDeviceUnitNumber, LTDeviceLED_GroupDescriptor * pDescriptor) {
    if (nDeviceUnitNumber < kNumLEDDeviceUnits) {
        *pDescriptor = s_LEDGroupDescriptors[nDeviceUnitNumber];
        return true;
    }
    return false;
}

static bool STDriverLEDImpl_GetUnitNumberFromGroupName(char const * pGroupName, u32 * pDeviceUnitNumberToSet) {
    LTDeviceLED_GroupDescriptor * pThisDescriptor = s_LEDGroupDescriptors;
    for (u32 i = 0; i < kNumLEDDeviceUnits; ++i, ++pThisDescriptor)
        if (lt_strcmp(pGroupName, pThisDescriptor->m_pGroupName) == 0) {
            *pDeviceUnitNumberToSet = i;
            return true;
        }
    return false;
}

define_LTLIBRARY_INTERFACE(ILTDriverLED) {
    .GetGroupDescriptorFromUnitNumber = STDriverLEDImpl_GetGroupDescriptorFromUnitNumber,
    .GetUnitNumberFromGroupName       = STDriverLEDImpl_GetUnitNumberFromGroupName
} LTLIBRARY_DEFINITION;

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceLED, STDriverLED);

LTLIBRARY_EXPORT_INTERFACES(STDriverLED, (ILTDriverLED))

/***********************************************************************************************************************
 * IndicatorLamp Group GPIO                                                                                           */

static void InitializeLEDGPIO(LEDInstance * pLED) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    HAL_GPIO_WritePin(pLED->pPort, pLED->pin, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = pLED->pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(pLED->pPort, &GPIO_InitStruct);
}

static void FinalizeLEDGPIO(LEDInstance * pLED) {
    HAL_GPIO_DeInit(pLED->pPort, pLED->pin);
}

typedef void (InitFiniLEDProc)(LEDInstance * pLED);

static void InitFiniLEDGPIO(LEDGroupInstance * pInstance, InitFiniLEDProc * pInitFiniProc) {
    if (pInstance && pInitFiniProc) {
        LEDGroupState * pGroupState = pInstance->pGroupState;
        if (pGroupState) {
            LEDInstance * pLED = pGroupState->pLEDs;
            if (pLED) for (u32 i = pGroupState->nNumLEDs; i; --i) (*pInitFiniProc)(pLED++);
        }
    }
}

static void InitializeLEDGroupGPIO(LEDGroupInstance * pInstance) {
    InitFiniLEDGPIO(pInstance, InitializeLEDGPIO);
}

static void FinalizeLEDGroupGPIO(LEDGroupInstance * pInstance) {
    InitFiniLEDGPIO(pInstance, FinalizeLEDGPIO);
}

static void ControlSingleColorLED(LEDInstance * pLED) {
    HAL_GPIO_WritePin(pLED->pPort, pLED->pin, pLED->nColor ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void ControlDualColorLED(LEDInstance * pLED) {
    /* The dual-color LED on this platform is actually two separate LEDs, at different places on the circuit board.
     * One is blue, and the other is yellow.  If there is only blue in the color, set the blue, otherwise, if there
     * are any red or green components, set the yellow instead.  Intensity (uppermost eight bits) is ignored: */
    HAL_GPIO_WritePin(pLED->pPort, pLED->pin, pLED->nColor & 0x00FFFF00 ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/***********************************************************************************************************************
 * IndicatorLamp Group interface                                                                                      */

u32 STDriverLED_IndicatorLamp_GetLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex) {
    LEDInstance * pLED = ThisLEDInstance(hGroup, nLEDIndex, 1);
    return pLED ? pLED->nColor : 0;
}

void STDriverLED_IndicatorLamp_GetLEDColors(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nCount, u32 * pBufferToFill) {
    LEDInstance * pLED = ThisLEDInstance(hGroup, nLEDIndex, nCount);
    if (pLED) for (; nCount; --nCount, ++pLED, ++pBufferToFill) *pBufferToFill = pLED->nColor;
}

static void SetLEDColor(LEDInstance * pLED, u32 nColor) {
    pLED->nColor = nColor;
    (*pLED->pInstanceControlProc)(pLED);
}

static void MaskLEDColor(LEDInstance * pLED, u32 nMask, u32 nColor) { SetLEDColor(pLED, nColor &= nMask); }

void STDriverLED_IndicatorLamp_SetLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nColor) {
    LEDInstance * pLED = ThisLEDInstance(hGroup, nLEDIndex, 1);
    if (pLED) SetLEDColor(pLED, nColor);
}

void STDriverLED_IndicatorLamp_SetLEDColors(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nCount, u32 * pColors) {
    LEDInstance * pLED = ThisLEDInstance(hGroup, nLEDIndex, nCount);
    if (pLED) for (; nCount; --nCount, ++pLED, ++pColors) SetLEDColor(pLED, *pColors);
}

void STDriverLED_IndicatorLamp_MaskLEDColor(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nMask, u32 nColor) {
    LEDInstance * pLED = ThisLEDInstance(hGroup, nLEDIndex, 1);
    if (pLED) MaskLEDColor(pLED, nMask, nColor);
}

void STDriverLED_IndicatorLamp_MaskLEDColors(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nCount, u32 nMask, u32 * pColors) {
    LEDInstance * pLED = ThisLEDInstance(hGroup, nLEDIndex, nCount);
    if (pLED) for (; nCount; --nCount, ++pLED, ++pColors) MaskLEDColor(pLED, nMask, *pColors);
}

u32 STDriverLED_IndicatorLamp_GetNumberOfElements(LTDeviceUnit hGroup) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    LEDGroupState * pGroup = pInstance ? pInstance->pGroupState : NULL;
    return pGroup ? pGroup->nNumLEDs : 0;
}

define_LTLIBRARY_INTERFACE(ILTDriverLED_GroupType_IndicatorLamp, STLEDDeviceUnit_OnDestroyHandle)
    .GetLEDColor         = STDriverLED_IndicatorLamp_GetLEDColor,
    .GetLEDColors        = STDriverLED_IndicatorLamp_GetLEDColors,
    .SetLEDColor         = STDriverLED_IndicatorLamp_SetLEDColor,
    .SetLEDColors        = STDriverLED_IndicatorLamp_SetLEDColors,
    .MaskLEDColor        = STDriverLED_IndicatorLamp_MaskLEDColor,
    .MaskLEDColors       = STDriverLED_IndicatorLamp_MaskLEDColors,
    .GetNumberOfElements = STDriverLED_IndicatorLamp_GetNumberOfElements
LTLIBRARY_DEFINITION;

/***********************************************************************************************************************
 * Seven-Segment Display I/O particulars                                                                              */

static SPI_HandleTypeDef s_hSPI1;

static void InitializeSevenSegmentSPI(LEDGroupInstance * pInstance) {
    LT_UNUSED(pInstance);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    s_hSPI1.Instance = SPI1;
    s_hSPI1.Init.Mode = SPI_MODE_MASTER;
    s_hSPI1.Init.Direction = SPI_DIRECTION_2LINES;
    s_hSPI1.Init.DataSize = SPI_DATASIZE_16BIT;
    s_hSPI1.Init.CLKPolarity = SPI_POLARITY_LOW;
    s_hSPI1.Init.CLKPhase = SPI_PHASE_1EDGE;
    s_hSPI1.Init.NSS = SPI_NSS_SOFT;
    s_hSPI1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
    s_hSPI1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_hSPI1.Init.TIMode = SPI_TIMODE_DISABLE;
    s_hSPI1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s_hSPI1.Init.CRCPolynomial = 7;
    s_hSPI1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
    s_hSPI1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    HAL_StatusTypeDef result = HAL_SPI_Init(&s_hSPI1);
    LT_ASSERT(result == HAL_OK); LT_UNUSED(result);
}

static void FinalizeSevenSegmentSPI(LEDGroupInstance * pInstance) {
    LT_UNUSED(pInstance);
    HAL_SPI_DeInit(&s_hSPI1);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2);
}

/* Transmit a command to the display driver via SPI: */
static void DisplayTransmit(u16 data) {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&s_hSPI1, (u8 *)&data, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
}

/***********************************************************************************************************************
 * Seven-Segment Display Group Interface                                                                              */

void STDriverLED_SevenSegmentDigit_Clear(LTDeviceUnit hGroup) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_Clear(pInstance->pGroupState);
}

void STDriverLED_SevenSegmentDigit_SetDigit(LTDeviceUnit hGroup,
                                                u32 nDigitIndex, u8 nDigitValue, bool bSetDecimalPoint) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetDigit(pInstance->pGroupState, nDigitIndex, nDigitValue, bSetDecimalPoint);
}

void STDriverLED_SevenSegmentDigit_SetDigits(LTDeviceUnit hGroup,
                                                 u32 nDigitIndex, u32 nCount, const char * pDigitString) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetDigits(pInstance->pGroupState, nDigitIndex, nCount, pDigitString);
}

void STDriverLED_SevenSegmentDigit_SetSegmentBitPattern(LTDeviceUnit hGroup,
                                                            u32 nDigitIndex, u8 bitPattern) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetBitPattern(pInstance->pGroupState, nDigitIndex, bitPattern);
}

void STDriverLED_SevenSegmentDigit_SetSegmentBitPatterns(LTDeviceUnit hGroup,
                                                             u32 nDigitIndex, u32 nCount, u8 * pBitPatterns) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetBitPatterns(pInstance->pGroupState, nDigitIndex, nCount, pBitPatterns);
}

void STDriverLED_SevenSegmentDigit_SetLowPowerMode(LTDeviceUnit hGroup, bool bLowPowerMode) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetLowPowerMode(pInstance->pGroupState, bLowPowerMode);
}

void STDriverLED_SevenSegmentDigit_SetDisplayTestMode(LTDeviceUnit hGroup, bool bDisplayTestMode) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetDisplayTestMode(pInstance->pGroupState, bDisplayTestMode);
}

void STDriverLED_SevenSegmentDigit_SetColor(LTDeviceUnit hGroup, u32 nColor) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetColor(pInstance->pGroupState, nColor);
}

static LTChipMAX7221DisplayState * DisplayStateFromHandle(LTDeviceUnit hGroup) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    return pInstance ? (LTChipMAX7221DisplayState *)pInstance->pGroupState : NULL;
}

u32 STDriverLED_SevenSegmentDigit_GetColor(LTDeviceUnit hGroup) {
    LTChipMAX7221DisplayState * pDisplayState = DisplayStateFromHandle(hGroup);
    return pDisplayState ? pDisplayState->nColor : 0;
}

u32 STDriverLED_SevenSegmentDigit_GetNumberOfDigits(LTDeviceUnit hGroup) {
    LTChipMAX7221DisplayState * pDisplayState = DisplayStateFromHandle(hGroup);
    return pDisplayState ? pDisplayState->nNumDigits : 0;
}

define_LTLIBRARY_INTERFACE(ILTDriverLED_GroupType_SevenSegmentDigit, STLEDDeviceUnit_OnDestroyHandle)
    .Clear                 = STDriverLED_SevenSegmentDigit_Clear,
    .SetDigit              = STDriverLED_SevenSegmentDigit_SetDigit,
    .SetDigits             = STDriverLED_SevenSegmentDigit_SetDigits,
    .SetSegmentBitPattern  = STDriverLED_SevenSegmentDigit_SetSegmentBitPattern,
    .SetSegmentBitPatterns = STDriverLED_SevenSegmentDigit_SetSegmentBitPatterns,
    .SetLowPowerMode       = STDriverLED_SevenSegmentDigit_SetLowPowerMode,
    .SetDisplayTestMode    = STDriverLED_SevenSegmentDigit_SetDisplayTestMode,
    .SetColor              = STDriverLED_SevenSegmentDigit_SetColor,
    .GetColor              = STDriverLED_SevenSegmentDigit_GetColor,
    .GetNumberOfDigits     = STDriverLED_SevenSegmentDigit_GetNumberOfDigits
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  18-Feb-21   constantine created
 *  26-Mar-21   constantine moved MAX7221-specific code into LTChipMAX7221
 */
