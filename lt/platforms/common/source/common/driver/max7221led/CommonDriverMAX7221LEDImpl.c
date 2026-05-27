/***********************************************************************************************************************
 * <common/source/common/driver/max7221led/CommonDriverMAX7221LEDImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * LT Driver Library for MAX7221 LED control
 **********************************************************************************************************************/
/** @file CommonDriverMAX7221LEDImpl.c Implementation of LED control Driver for MAX7221 */

#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/led/LTDeviceLED.h>
#include <lt/device/spi/LTDeviceSPI.h>
#include "LTChipMAX7221.h"

/* clang-format off */

DEFINE_LTLOG_SECTION("max7221driver");

static const ILTDriverLED_GroupType_SevenSegmentDigit s_ILTDriverLED_GroupType_SevenSegmentDigit;
static ILTThread              * s_ILTThread  = NULL;
/***********************************************************************************************************************
 * Driver configuration.                                                                                              */

/***********************************************************************************************************************
 * Data structure for an LED Group instance:                                                                          */

typedef struct {
    LTChipMAX7221DisplayState * pDisplayState;            /**< Control structure for MAX7221 display driver IC  */
    LTMutex                   * mutex;                    /**< Mutex protection, mainly for reference count     */
    u32                         nRefCount;                /**< How many clients have a LTHandle to this Group   */
    LTDeviceUnit                hSPI;
    u32                         nSPIUnitNum;
} LEDGroupInstance;

/***********************************************************************************************************************
 * LED Group instance and LED instance data.                                                                          */

static void InitializeSevenSegmentSPI( LTDeviceUnit);
static void FinalizeSevenSegmentSPI( LTDeviceUnit);
static void SPIDisplayTransmit(u16 data, LTDeviceUnit hSPI);

/* Platform defined variables: Display name, Number of digits, SPI port num */
#define SPI7SEGDISPLAYDECLARE(Name, Numdigits) \
    static LTChipMAX7221Digit s_Digits##Name[Numdigits]; \
    static LTDeviceLED_GroupDescriptor s_LEDGroupDescriptor##Name = { \
        .m_pGroupName        = #Name, \
        .m_groupType         = kLTDeviceLED_GroupType_SevenSegmentDigit, \
        .m_nNumElements      = Numdigits, \
    }; \
    static LTChipMAX7221DisplayState s_DisplayState##Name = { \
        .pDigits =       s_Digits##Name, \
        .pTransmitProc = SPIDisplayTransmit, \
        .nNumDigits =    Numdigits, \
    };
/* Configuration limitation: macros are provided for up to 4 display configurations */
#define SPICONNECT1DISPLAY(Name1, SPIport1) \
    static LTDeviceLED_GroupDescriptor * s_LEDGroupDescriptors[] = { \
        &s_LEDGroupDescriptor##Name1, \
    }; \
    static LEDGroupInstance s_LEDGroupInstance[] = { \
        { \
            .pDisplayState   = &s_DisplayState##Name1, \
            .nSPIUnitNum     = SPIport1, \
            .hSPI            = 0, \
        }, \
    };
#define SPICONNECT2DISPLAYS(Name1, SPIport1, Name2, SPIport2) \
    static LTDeviceLED_GroupDescriptor * s_LEDGroupDescriptors[] = { \
        &s_LEDGroupDescriptor##Name1, \
        &s_LEDGroupDescriptor##Name2, \
    }; \
    static LEDGroupInstance s_LEDGroupInstance[] = { \
        { \
            .pDisplayState   = &s_DisplayState##Name1, \
            .nSPIUnitNum     = SPIport1, \
            .hSPI            = 0, \
        }, \
        { \
            .pDisplayState   = &s_DisplayState##Name2, \
            .nSPIUnitNum     = SPIport2, \
            .hSPI            = 0, \
        } \
    };
#define SPICONNECT3DISPLAYS(Name1, SPIport1, Name2, SPIport2, Name3, SPIport3) \
    static LTDeviceLED_GroupDescriptor * s_LEDGroupDescriptors[] = { \
        &s_LEDGroupDescriptor##Name1, \
        &s_LEDGroupDescriptor##Name2, \
        &s_LEDGroupDescriptor##Name3, \
    }; \
    static LEDGroupInstance s_LEDGroupInstance[] = { \
        { \
            .pDisplayState   = &s_DisplayState##Name1, \
            .nSPIUnitNum     = SPIport1, \
            .hSPI            = 0, \
        }, \
        { \
            .pDisplayState   = &s_DisplayState##Name2, \
            .nSPIUnitNum     = SPIport2, \
            .hSPI            = 0, \
        } \
        { \
            .pDisplayState   = &s_DisplayState##Name3, \
            .nSPIUnitNum     = SPIport3, \
            .hSPI            = 0, \
        } \
    };
#define SPICONNECT4DISPLAYS(Name1, SPIport1, Name2, SPIport2, Name3, SPIport3, Name4, SPIport4) \
    static LTDeviceLED_GroupDescriptor * s_LEDGroupDescriptors[] = { \
        &s_LEDGroupDescriptor##Name1, \
        &s_LEDGroupDescriptor##Name2, \
        &s_LEDGroupDescriptor##Name3, \
        &s_LEDGroupDescriptor##Name4, \
    }; \
    static LEDGroupInstance s_LEDGroupInstance[] = { \
        { \
            .pDisplayState   = &s_DisplayState##Name1, \
            .nSPIUnitNum     = SPIport1, \
            .hSPI            = 0, \
        }, \
        { \
            .pDisplayState   = &s_DisplayState##Name2, \
            .nSPIUnitNum     = SPIport2, \
            .hSPI            = 0, \
        } \
        { \
            .pDisplayState   = &s_DisplayState##Name3, \
            .nSPIUnitNum     = SPIport3, \
            .hSPI            = 0, \
        }, \
        { \
            .pDisplayState   = &s_DisplayState##Name4, \
            .nSPIUnitNum     = SPIport4, \
            .hSPI            = 0, \
        } \
    };

/* List of available SPI ports */
#include LT_STRINGIFY(CONFIGURATION_FILE)

#undef SPI7SEGDISPLAYDECLARE
#undef SPICONNECT1DISPLAY
#undef SPICONNECT2DISPLAYS
#undef SPICONNECT3DISPLAYS
#undef SPICONNECT4DISPLAYS

enum { kNumLEDDeviceUnits = sizeof(s_LEDGroupInstance) / sizeof (LEDGroupInstance) };
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
static bool ProbeLEDInstance(LEDGroupInstance *pInstance, u32 idPort ,LTDeviceUnit hSPI) {
    /* Cannot reliably probe this device, so IDs of the SPI ports are hadcoded */
    return (hSPI && idPort == pInstance->nSPIUnitNum);
}
/***********************************************************************************************************************
 * Generic Driver interface - Initialization, finalization, and Device Unit access                                    */

static void CommonDriverMAX7221LEDImpl_LibFini(void);

static LTDeviceSPI * s_pDeviceSPI = NULL;
/* Library initialization upon opening - initialize reference-count objects for all Device Units:                     */
static bool CommonDriverMAX7221LEDImpl_LibInit(void) {
    s_pDeviceSPI = (LTDeviceSPI *)LT_GetCore()->OpenLibrary("LTDeviceSPI");
    if (!s_pDeviceSPI) {
        LTLOG_YELLOWALERT("fail.spi.interface", "LTDeviceSPI is not available");
        return false;
    }
    if (!(s_ILTThread = lt_getlibraryinterface(ILTThread, LT_GetCore()))) {
         return false;
    }
    u32 nNumDeviceUnits = s_pDeviceSPI->GetNumDeviceUnits();
    if (nNumDeviceUnits == 0) {
        LTLOG_YELLOWALERT("fail.deviceunits", "LTDeviceSPI provides no Device Units");
        return false;
    } else {
        for (u32 n = 0; n < nNumDeviceUnits; n++) {
            LTDeviceUnit hSPI = s_pDeviceSPI->CreateDeviceUnitHandle(n);
            LEDGroupInstance * pInstance = s_LEDGroupInstance;
            LTDeviceLED_GroupDescriptor ** pDescriptor = s_LEDGroupDescriptors;
            for (int nIndex = 0; nIndex < kNumLEDDeviceUnits; ++nIndex, ++pInstance, ++pDescriptor) {
                /* Initialize the LEDGroupInstance: */
                if ( !pInstance->hSPI && ProbeLEDInstance(pInstance, n, hSPI)) {
                    pInstance->hSPI = hSPI;
                    (*pDescriptor)->m_nDeviceUnitNumber = nIndex;
                    pInstance->nRefCount = 0;
                    if (!(pInstance->mutex = lt_createobject(LTMutex))) {
                        CommonDriverMAX7221LEDImpl_LibFini();
                        return false;
                    }
                    break;
                }
            }

        }
    }

    return true;
}

/* Library finalization - tear down instance data upon library close or upon failure of Library open:                 */
static void CommonDriverMAX7221LEDImpl_LibFini(void) {
    LEDGroupInstance * pInstance = s_LEDGroupInstance;
    for (int i = kNumLEDDeviceUnits; i--; ++pInstance) {
        if (pInstance->mutex) {
            lt_destroyobject(pInstance->mutex);
            pInstance->mutex = NULL;
        }
    }
    if (s_pDeviceSPI) {
		lt_closelibrary(s_pDeviceSPI);
		s_pDeviceSPI = NULL;
	}
    s_ILTThread = NULL;
}

static u32 CommonDriverMAX7221LEDImpl_GetNumDeviceUnits(void) {
    return kNumLEDDeviceUnits;
}

/* Provide a Device Unit handle.  Furnish the handle with a pointer to the respective instance data.
 * Initialize the SPI control blocks for the LED Group if this is the first handle for the Group. */

static LTDeviceUnit CommonDriverMAX7221LEDImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LTDeviceUnit hDevice = 0;
    if (nDeviceUnitNumber < kNumLEDDeviceUnits)
        hDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverLED_GroupType_SevenSegmentDigit, sizeof(LEDGroupInstance *));
    if (hDevice) {
        bool bInterfaceOK = false;  /* A handle has been created.  Do not leak it if something goes
                                       wrong with preparing the handle or initializing the interface. */
        LEDGroupInstance ** ppInstance = (LEDGroupInstance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
        if (ppInstance) {
            LEDGroupInstance * pInstance = *ppInstance = &s_LEDGroupInstance[nDeviceUnitNumber];
            bInterfaceOK = true;    /* all okay, unless first-reference initialization (below) fails */
            pInstance->mutex->API->Lock(pInstance->mutex);
            if (++pInstance->nRefCount == 1) {   /* Just starting up this instance. */
                InitializeSevenSegmentSPI(pInstance->hSPI);
                pInstance->pDisplayState->hSPIUnit = pInstance->hSPI;
                bInterfaceOK = LTChipMAX7221_Initialize(pInstance->pDisplayState, true);
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
 * the GPIO instances if there are no more refs. */
static void CommonDriverMAX7221LEDImplDeviceUnit_OnDestroyHandle(LTHandle hDevice) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hDevice);
    if (pInstance) {
        pInstance->mutex->API->Lock(pInstance->mutex);
        if (pInstance->nRefCount > 0)
            if (--pInstance->nRefCount == 0) {
                FinalizeSevenSegmentSPI(pInstance->hSPI);
                pInstance->hSPI = 0;
			}
        pInstance->mutex->API->Unlock(pInstance->mutex);
    }
}

/***********************************************************************************************************************
 * ILTDriverLED interface                                                                                             */

static bool CommonDriverMAX7221LEDImpl_GetGroupDescriptorFromUnitNumber(u32 nDeviceUnitNumber, LTDeviceLED_GroupDescriptor * pDescriptor) {
    if (nDeviceUnitNumber < kNumLEDDeviceUnits) {
        *pDescriptor = *s_LEDGroupDescriptors[nDeviceUnitNumber];
        return true;
    }
    return false;
}

static bool CommonDriverMAX7221LEDImpl_GetUnitNumberFromGroupName(char const * pGroupName, u32 * pDeviceUnitNumberToSet) {
    LTDeviceLED_GroupDescriptor ** pThisDescriptor = s_LEDGroupDescriptors;
    for (u32 i = 0; i < kNumLEDDeviceUnits; ++i, ++pThisDescriptor)
        if (lt_strcmp(pGroupName, (*pThisDescriptor)->m_pGroupName) == 0) {
            *pDeviceUnitNumberToSet = i;
            return true;
        }
    return false;
}

define_LTLIBRARY_INTERFACE(ILTDriverLED) {
    .GetGroupDescriptorFromUnitNumber = CommonDriverMAX7221LEDImpl_GetGroupDescriptorFromUnitNumber,
    .GetUnitNumberFromGroupName       = CommonDriverMAX7221LEDImpl_GetUnitNumberFromGroupName
} LTLIBRARY_DEFINITION;

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceLED, CommonDriverMAX7221LED);

LTLIBRARY_EXPORT_INTERFACES(CommonDriverMAX7221LED, (ILTDriverLED))

/***********************************************************************************************************************
 * Seven-Segment Display I/O particulars                                                                              */

static void InitializeSevenSegmentSPI(LTDeviceUnit hSPI) {
    LTDeviceSPI_Capabilities caps;
    LTDeviceSPI_Configuration cfg;
    if (s_pDeviceSPI->GetDeviceCapabilities(hSPI, &caps)) {
        LTLOG_DEBUG("spi.init.caps", "freq_min %lu freq_max %lu bits_min %lu bits_max %lu caps %lu", LT_Pu32(caps.Freq_min), LT_Pu32(caps.Freq_max), LT_Pu32(caps.Bits_min), LT_Pu32(caps.Bits_max),LT_Pu32(caps.Caps_mask));
    } else {
        LTLOG("spi.init.err.caps", "error");
        return;
    }
    cfg.Frequency = caps.Freq_max;
    cfg.Bits = caps.Bits_max;
    cfg.Mode = kLTDeviceSPI_Mode_0;
    cfg.Master = true;
    cfg.Async = false;
    cfg.Dma = false;
    bool bDeviceConfigured = s_pDeviceSPI->SetDeviceConfiguration(hSPI, &cfg);
    if (!bDeviceConfigured) {
        LTLOG("spi.init.fail", "configuration");
    }
}

static void FinalizeSevenSegmentSPI(LTDeviceUnit hSPI) {  LT_GetCore()->DestroyHandle(hSPI); }

static void SPIDisplayTransmit(u16 data, LTDeviceUnit hSPI) {
    u32 nTry = 0;
    bool bResult = false;
    if (!s_pDeviceSPI) {
        LTLOG("spi.xfer.invalid", "handle %lx",LT_PLT_HANDLE(hSPI));
        return;
    }
#define MAX_TRY 10
    do {
        bResult = s_pDeviceSPI->SPIMasterTransfer(hSPI,NULL, (u8*)&data, sizeof(u16), NULL, NULL);
        if (!bResult) {
            nTry++;
            s_ILTThread->Sleep(LTTime_Milliseconds(10));
        }
    } while (!bResult && nTry < MAX_TRY);
    if (!bResult) {
        LTLOG("spi.xfer.fail", "handle %lx",LT_PLT_HANDLE(hSPI));
    }
}

/***********************************************************************************************************************
 * Seven-Segment Display Group Interface                                                                              */

void CommonDriverMAX7221LED_SevenSegmentDigit_Clear(LTDeviceUnit hGroup) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_Clear(pInstance->pDisplayState);
}

void CommonDriverMAX7221LED_SevenSegmentDigit_SetDigit(LTDeviceUnit hGroup,
                                                u32 nDigitIndex, u8 nDigitValue, bool bSetDecimalPoint) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetDigit(pInstance->pDisplayState, nDigitIndex, nDigitValue, bSetDecimalPoint);
}

void CommonDriverMAX7221LED_SevenSegmentDigit_SetDigits(LTDeviceUnit hGroup,
                                                 u32 nDigitIndex, u32 nCount, const char * pDigitString) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetDigits(pInstance->pDisplayState, nDigitIndex, nCount, pDigitString);
}

void CommonDriverMAX7221LED_SevenSegmentDigit_SetSegmentBitPattern(LTDeviceUnit hGroup,
                                                            u32 nDigitIndex, u8 bitPattern) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetBitPattern(pInstance->pDisplayState, nDigitIndex, bitPattern);
}

void CommonDriverMAX7221LED_SevenSegmentDigit_SetSegmentBitPatterns(LTDeviceUnit hGroup,
                                                             u32 nDigitIndex, u32 nCount, u8 * pBitPatterns) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetBitPatterns(pInstance->pDisplayState, nDigitIndex, nCount, pBitPatterns);
}

void CommonDriverMAX7221LED_SevenSegmentDigit_SetLowPowerMode(LTDeviceUnit hGroup, bool bLowPowerMode) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetLowPowerMode(pInstance->pDisplayState, bLowPowerMode);
}

void CommonDriverMAX7221LED_SevenSegmentDigit_SetDisplayTestMode(LTDeviceUnit hGroup, bool bDisplayTestMode) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetDisplayTestMode(pInstance->pDisplayState, bDisplayTestMode);
}

void CommonDriverMAX7221LED_SevenSegmentDigit_SetColor(LTDeviceUnit hGroup, u32 nColor) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    if (pInstance) LTChipMAX7221_SetColor(pInstance->pDisplayState, nColor);
}

u32 CommonDriverMAX7221LED_SevenSegmentDigit_GetColor(LTDeviceUnit hGroup) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    return pInstance ? pInstance->pDisplayState->nColor : 0;
}

u32 CommonDriverMAX7221LED_SevenSegmentDigit_GetNumberOfDigits(LTDeviceUnit hGroup) {
    LEDGroupInstance * pInstance = InstanceFromHandle(hGroup);
    return pInstance ? pInstance->pDisplayState->nNumDigits : 0;
}

define_LTLIBRARY_INTERFACE(ILTDriverLED_GroupType_SevenSegmentDigit, CommonDriverMAX7221LEDImplDeviceUnit_OnDestroyHandle)
    .Clear                 = CommonDriverMAX7221LED_SevenSegmentDigit_Clear,
    .SetDigit              = CommonDriverMAX7221LED_SevenSegmentDigit_SetDigit,
    .SetDigits             = CommonDriverMAX7221LED_SevenSegmentDigit_SetDigits,
    .SetSegmentBitPattern  = CommonDriverMAX7221LED_SevenSegmentDigit_SetSegmentBitPattern,
    .SetSegmentBitPatterns = CommonDriverMAX7221LED_SevenSegmentDigit_SetSegmentBitPatterns,
    .SetLowPowerMode       = CommonDriverMAX7221LED_SevenSegmentDigit_SetLowPowerMode,
    .SetDisplayTestMode    = CommonDriverMAX7221LED_SevenSegmentDigit_SetDisplayTestMode,
    .SetColor              = CommonDriverMAX7221LED_SevenSegmentDigit_SetColor,
    .GetColor              = CommonDriverMAX7221LED_SevenSegmentDigit_GetColor,
    .GetNumberOfDigits     = CommonDriverMAX7221LED_SevenSegmentDigit_GetNumberOfDigits
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  09-Mar-21   constantine created
 *  25-Mar-21   constantine moved MAX7221-specific code into LTChipMAX7221
 */
