/*******************************************************************************
 * platforms/esp32/source/esp32/driver/gpio/Esp32DriverGpio.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/
/** @file Esp32DriverGpio.c LTDeviceGpio driver for the esp32, esp32s3, esp32c3 and esp32p4 */

/*
 * One source file serves every chip.  Everything chip specific - the pad count,
 * the IO_MUX function that hands a pad to the GPIO block, and which pad numbers
 * a package actually bonds out - comes from the per chip Esp32_GPIO.h that
 * $(SOC_PLATFORM_NAME) puts on the include path.
 *
 * LTDeviceGpio has a passthru api, so this driver is the device object:
 * lt_createobject(LTDeviceGpio, Esp32DriverGpio).
 *
 * Named pins come from this library's own config/pins section of LTDeviceConfig.json:
 *
 * "device": [ { "class": "LTDeviceGpio",
 *                 "api": "passthru",
 *                "unit": [ {   "name": "GPIO0", "driver": "Esp32DriverGpio",
 *                            "config": { "pins": [
 *                                                  { "name": "flashlight", "pin":  4 },
 *                                                  { "name": "relay",      "pin": 16 },
 *                                                  { "name": "status",     "pin": 34 }
 *                                               ]
 *                                      }
 *                          }
 *                        ]
 *              }
 *            ],
 *
 * The config/pins section is optional; without it the driver reports no named pins.
 */

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/config/LTDeviceKonfig.h>
#include <lt/device/gpio/LTDeviceGpio.h>

#include "Esp32_GPIO.h"

DEFINE_LTLOG_SECTION("esp32.drv.gpio");

/*_______________________________
  Esp32DriverGpio LTObject impl */
typedef_LTObjectImpl(LTDeviceGpio, Esp32DriverGpio) {
} LTOBJECT_API;

/*_______________
  konfig #defines */
#define DEVICEKONFIG_PINSARRAY_KEY          "pins"
#define DEVICEKONFIG_PINSOBJECT_NAMEKEY     "name"
#define DEVICEKONFIG_PINSOBJECT_NUMBERKEY   "pin"

/*
 * A GPIO index is the pad number, so the pads a package does not bond out keep
 * their name and their slot; every accessor rejects them, and
 * Esp32GPIO_IsValidPin() is what decides.
 */
static const char * const GpioNames[] = {
    "gpio_00", "gpio_01", "gpio_02", "gpio_03", "gpio_04", "gpio_05", "gpio_06",
    "gpio_07", "gpio_08", "gpio_09", "gpio_10", "gpio_11", "gpio_12", "gpio_13",
    "gpio_14", "gpio_15", "gpio_16", "gpio_17", "gpio_18", "gpio_19", "gpio_20",
    "gpio_21", "gpio_22", "gpio_23", "gpio_24", "gpio_25", "gpio_26", "gpio_27",
    "gpio_28", "gpio_29", "gpio_30", "gpio_31", "gpio_32", "gpio_33", "gpio_34",
    "gpio_35", "gpio_36", "gpio_37", "gpio_38", "gpio_39", "gpio_40", "gpio_41",
    "gpio_42", "gpio_43", "gpio_44", "gpio_45", "gpio_46", "gpio_47", "gpio_48",
    "gpio_49", "gpio_50", "gpio_51", "gpio_52", "gpio_53", "gpio_54",
};
#define TotalGpioCount          ((u16)kEsp32GPIO_NumPins)
LT_STATIC_ASSERT(sizeof(GpioNames) / sizeof(GpioNames[0]) >= kEsp32GPIO_NumPins,
                 "GpioNames does not name every pad");

/*
 * An IO_MUX function is selected by number, and which peripheral that number is
 * depends on the pad - so the alternate function index here is the IO_MUX
 * function select value and the names are the numbers.  The one function that
 * means the same thing on every pad is the GPIO block, which is named for it.
 */
static const char * const AltFuncNames[] = {
    "FUNC0", "FUNC1", "FUNC2", "FUNC3", "FUNC4", "FUNC5",
};
#define TotalAltFunctionCount   ((u16)kEsp32GPIO_NumFunctions)
LT_STATIC_ASSERT(sizeof(AltFuncNames) / sizeof(AltFuncNames[0]) >= kEsp32GPIO_NumFunctions,
                 "AltFuncNames does not name every IO_MUX function");

/*_______________
  library state */
static LTDeviceKonfig    *s_pDeviceKonfig = NULL;

/* The LTDeviceGpio ISR per pad.  The client data is carried by the BSP, which
   hands it back to the trampoline. */
static LTDeviceGPIO_ISR  *s_pIsrs[kEsp32GPIO_NumPins];

/* An LTDeviceGpio index has to be inside the pad table before it can be narrowed
   to the u8 the BSP takes, or a wild index would alias onto a real pad. */
static bool Esp32DriverGpio_IsValidIndex(u16 gpioIndex) {
    return gpioIndex < TotalGpioCount && Esp32GPIO_IsValidPin((u8)gpioIndex);
}

/*______________
  translation */
static Esp32GPIO_PullType Esp32DriverGpio_PullToEsp32(LTDeviceGpio_PullType pull) {
    switch (pull) {
        case kLTDeviceGpio_PullType_PullUp:   return kEsp32GPIO_PullUp;
        case kLTDeviceGpio_PullType_PullDown: return kEsp32GPIO_PullDown;
        default:                              return kEsp32GPIO_PullNone;
    }
}

static LTDeviceGpio_PullType Esp32DriverGpio_PullFromEsp32(Esp32GPIO_PullType pull) {
    switch (pull) {
        case kEsp32GPIO_PullUp:   return kLTDeviceGpio_PullType_PullUp;
        case kEsp32GPIO_PullDown: return kLTDeviceGpio_PullType_PullDown;
        default:                  return kLTDeviceGpio_PullType_NoPull;
    }
}

/* Returns kEsp32GPIO_Trigger_Disabled for a trigger this chip cannot arm */
static Esp32GPIO_Trigger Esp32DriverGpio_TriggerToEsp32(LTDeviceGPIO_TriggerType trigger) {
    switch (trigger) {
        case kLTDeviceGPIO_TriggerType_FallingEdge: return kEsp32GPIO_Trigger_Falling;
        case kLTDeviceGPIO_TriggerType_RisingEdge:  return kEsp32GPIO_Trigger_Rising;
        case kLTDeviceGPIO_TriggerType_BothEdges:   return kEsp32GPIO_Trigger_Both;
        case kLTDeviceGPIO_TriggerType_LowLevel:    return kEsp32GPIO_Trigger_LowLevel;
        case kLTDeviceGPIO_TriggerType_HighLevel:   return kEsp32GPIO_Trigger_HighLevel;
        default:                                    return kEsp32GPIO_Trigger_Disabled;
    }
}

/* The BSP callback carries the pad and its level; an LTDeviceGpio ISR takes
   neither, so they are dropped here. */
static void Esp32DriverGpio_IsrTrampoline(u8 nPin, bool bPinHigh, void *pClientData) LT_ISR_SAFE {
    LT_UNUSED(bPinHigh);
    if (nPin < TotalGpioCount && s_pIsrs[nPin]) {
        s_pIsrs[nPin](pClientData);
    }
}

/*_____________________
  LTDeviceGpio api */
static u16 Esp32DriverGpio_GetNumberOfGpios(Esp32DriverGpio *gpio) {
    LT_UNUSED(gpio);
    return TotalGpioCount;
}

static char const * Esp32DriverGpio_GetGpioNameFromIndex(Esp32DriverGpio *gpio, u16 gpioIndex) {
    LT_UNUSED(gpio);
    if (gpioIndex < TotalGpioCount) {
        return GpioNames[gpioIndex];
    }

    return NULL;
}

static LTDeviceGpio_ModeType Esp32DriverGpio_GetGpioModeFromIndex(Esp32DriverGpio *gpio, u16 gpioIndex) {
    LT_UNUSED(gpio);
    Esp32GPIO_PinConfig config;
    if (!Esp32DriverGpio_IsValidIndex(gpioIndex) || !Esp32GPIO_GetPinConfig((u8)gpioIndex, &config)) {
        return kLTDeviceGpio_ModeType_Error;
    }

    if (config.func != kEsp32GPIO_Function_GPIO) {
        return kLTDeviceGpio_ModeType_AlternateFunction;
    }
    if (config.direction == kEsp32GPIO_Direction_Output) {
        return config.bInputEnabled ? kLTDeviceGpio_ModeType_Both : kLTDeviceGpio_ModeType_Output;
    }
    return config.bInputEnabled ? kLTDeviceGpio_ModeType_Input : kLTDeviceGpio_ModeType_HighZ;
}

static LTDeviceGpio_PullType Esp32DriverGpio_GetGpioPullFromIndex(Esp32DriverGpio *gpio, u16 gpioIndex) {
    LT_UNUSED(gpio);
    Esp32GPIO_PinConfig config;
    if (!Esp32DriverGpio_IsValidIndex(gpioIndex) || !Esp32GPIO_GetPinConfig((u8)gpioIndex, &config)) {
        return kLTDeviceGpio_PullType_Error;
    }

    return Esp32DriverGpio_PullFromEsp32(config.pull);
}

static bool Esp32DriverGpio_SetGpioModeFromIndex(Esp32DriverGpio *gpio, u16 gpioIndex, LTDeviceGpio_ModeType mode) {
    LT_UNUSED(gpio);
    Esp32GPIO_PinConfig config;
    if (!Esp32DriverGpio_IsValidIndex(gpioIndex) || !Esp32GPIO_GetPinConfig((u8)gpioIndex, &config)) {
        return false;
    }

    switch (mode) {
        case kLTDeviceGpio_ModeType_Input:
            return Esp32GPIO_ConfigPin((u8)gpioIndex, kEsp32GPIO_Direction_Input,
                                       config.pull, kEsp32GPIO_Function_GPIO);

        case kLTDeviceGpio_ModeType_Output:
        case kLTDeviceGpio_ModeType_Both:
            /* ConfigPin() only writes the pull for an input, so restore it here.
               Both is an output whose pad also feeds the input register. */
            if (!Esp32GPIO_ConfigPin((u8)gpioIndex, kEsp32GPIO_Direction_Output,
                                     config.pull, kEsp32GPIO_Function_GPIO)) {
                return false;
            }
            Esp32GPIO_ConfigPinPull((u8)gpioIndex, config.pull);
            Esp32GPIO_ConfigPinInputEnable((u8)gpioIndex, mode == kLTDeviceGpio_ModeType_Both);
            return true;

        case kLTDeviceGpio_ModeType_HighZ:
            /* neither driver nor receiver connected, and no pull.  The pad is
               left on the GPIO function so the mode reads back as HighZ. */
            if (!Esp32GPIO_ConfigPin((u8)gpioIndex, kEsp32GPIO_Direction_Input,
                                     kEsp32GPIO_PullNone, kEsp32GPIO_Function_GPIO)) {
                return false;
            }
            Esp32GPIO_ConfigPinInputEnable((u8)gpioIndex, false);
            return true;

        case kLTDeviceGpio_ModeType_AlternateFunction:
            /* the mode does not say which function - that is what
               SetGpioAlternateFunctionFromIndex() is for */
            LTLOG("sgmfi.needs.af", "gpio %d: use SetGpioAlternateFunctionFromIndex", (int)gpioIndex);
            return false;

        default:
            return false;
    }
}

static bool Esp32DriverGpio_SetGpioPullFromIndex(Esp32DriverGpio *gpio, u16 gpioIndex, LTDeviceGpio_PullType pull) {
    LT_UNUSED(gpio);
    if (!Esp32DriverGpio_IsValidIndex(gpioIndex) || pull == kLTDeviceGpio_PullType_Error) {
        return false;
    }

    Esp32GPIO_ConfigPinPull((u8)gpioIndex, Esp32DriverGpio_PullToEsp32(pull));
    return true;
}

static u16 Esp32DriverGpio_GetNumberOfAlternateFunctions(Esp32DriverGpio *gpio) {
    LT_UNUSED(gpio);
    return TotalAltFunctionCount;
}

static char const * Esp32DriverGpio_GetAlternateFunctionNameFromIndex(Esp32DriverGpio *gpio, u16 afIndex) {
    LT_UNUSED(gpio);
    if (afIndex >= TotalAltFunctionCount) {
        return NULL;
    }

    return (afIndex == kEsp32GPIO_Function_GPIO) ? "GPIO" : AltFuncNames[afIndex];
}

static int Esp32DriverGpio_GetGpioAlternateFunctionFromIndex(Esp32DriverGpio *gpio, u16 gpioIndex) {
    LT_UNUSED(gpio);
    Esp32GPIO_PinConfig config;
    if (!Esp32DriverGpio_IsValidIndex(gpioIndex) || !Esp32GPIO_GetPinConfig((u8)gpioIndex, &config)) {
        return -1;
    }

    return (int)config.func;
}

static bool Esp32DriverGpio_SetGpioAlternateFunctionFromIndex(Esp32DriverGpio *gpio, u16 gpioIndex, u16 afIndex) {
    LT_UNUSED(gpio);
    if (!Esp32DriverGpio_IsValidIndex(gpioIndex) || afIndex >= TotalAltFunctionCount) {
        return false;
    }

    Esp32GPIO_ConfigPinFunction((u8)gpioIndex, (Esp32GPIO_Function)afIndex);
    return true;
}

static bool Esp32DriverGpio_SetOutputValue(Esp32DriverGpio *gpio, u16 gpioIndex, bool value) {
    LT_UNUSED(gpio);
    if (!Esp32DriverGpio_IsValidIndex(gpioIndex)) {
        return false;
    }

    Esp32GPIO_WritePin((u8)gpioIndex, value);
    return true;
}

static bool Esp32DriverGpio_GetInputValue(Esp32DriverGpio *gpio, u16 gpioIndex) {
    LT_UNUSED(gpio);
    return Esp32DriverGpio_IsValidIndex(gpioIndex) && Esp32GPIO_ReadPin((u8)gpioIndex);
}

static u16 Esp32DriverGpio_GetNumberOfNamedPins(Esp32DriverGpio *gpio) {
    u32 configSection = s_pDeviceKonfig->API->GetDeviceUnitConfigSectionForDeviceOrDriverObject(s_pDeviceKonfig, (LTObject *)gpio);
    return (u16)s_pDeviceKonfig->API->GetNumArrayElements(s_pDeviceKonfig, configSection, DEVICEKONFIG_PINSARRAY_KEY);
}

static char const * Esp32DriverGpio_GetNamedPinFromIndex(Esp32DriverGpio *gpio, u16 npIndex) {
    // { "config": { "pins": [ { "name": "pinName", "pin": number} ] } }
    u32 section = s_pDeviceKonfig->API->GetDeviceUnitConfigSectionForDeviceOrDriverObject(s_pDeviceKonfig, (LTObject *)gpio);
    section = section ? s_pDeviceKonfig->API->GetArraySubObjectSectionByIndex(s_pDeviceKonfig, section, DEVICEKONFIG_PINSARRAY_KEY, npIndex) : 0;
    return section ? s_pDeviceKonfig->API->ReadString(s_pDeviceKonfig, section, DEVICEKONFIG_PINSOBJECT_NAMEKEY) : NULL;
}

static int Esp32DriverGpio_GetNamedPinValueFromIndex(Esp32DriverGpio *gpio, u16 npIndex) {
    // { "config": { "pins": [ { "name": "pinName", "pin": number} ] } }
    u32 section = s_pDeviceKonfig->API->GetDeviceUnitConfigSectionForDeviceOrDriverObject(s_pDeviceKonfig, (LTObject *)gpio);
    section = section ? s_pDeviceKonfig->API->GetArraySubObjectSectionByIndex(s_pDeviceKonfig, section, DEVICEKONFIG_PINSARRAY_KEY, npIndex) : 0;
    if (section && (kLTResourceValueType_Integer == s_pDeviceKonfig->API->ReadValueType(s_pDeviceKonfig, section, DEVICEKONFIG_PINSOBJECT_NUMBERKEY))) {
        return s_pDeviceKonfig->API->ReadInteger(s_pDeviceKonfig, section, DEVICEKONFIG_PINSOBJECT_NUMBERKEY);
    }
    return -1;
}

static int Esp32DriverGpio_GetNamedPinValueFromName(Esp32DriverGpio *gpio, char const *npName) {
    // { "config": { "pins": [ { "name": "pinName", "pin": number} ] } }
    if (npName) {
        u32 section = s_pDeviceKonfig->API->GetDeviceUnitConfigSectionForDeviceOrDriverObject(s_pDeviceKonfig, (LTObject *)gpio);
        section = section ? s_pDeviceKonfig->API->GetArraySubObjectSectionWithName(s_pDeviceKonfig, section, DEVICEKONFIG_PINSARRAY_KEY, npName) : 0;
        if (section && (kLTResourceValueType_Integer == s_pDeviceKonfig->API->ReadValueType(s_pDeviceKonfig, section, DEVICEKONFIG_PINSOBJECT_NUMBERKEY))) {
            return s_pDeviceKonfig->API->ReadInteger(s_pDeviceKonfig, section, DEVICEKONFIG_PINSOBJECT_NUMBERKEY);
        }
    }
    return -1;
}

static bool Esp32DriverGpio_SetISR(Esp32DriverGpio *gpio, u16 gpioIndex,
                                                          LTDeviceGPIO_ISR *pISR,
                                                          LTDeviceGPIO_TriggerType triggerType,
                                                          void *pClientData) {
    LT_UNUSED(gpio);
    if (!Esp32DriverGpio_IsValidIndex(gpioIndex)) {
        return false;
    }

    if (pISR == NULL) {
        Esp32GPIO_DetachISR((u8)gpioIndex);
        s_pIsrs[gpioIndex] = NULL;
        return true;
    }

    Esp32GPIO_Trigger trigger = Esp32DriverGpio_TriggerToEsp32(triggerType);
    if (trigger == kEsp32GPIO_Trigger_Disabled) {
        LTLOG_YELLOWALERT("isr.bad.trigger", "gpio %d: unsupported trigger %d",
                          (int)gpioIndex, (int)triggerType);
        return false;
    }

    LTLOG("set.isr", "gpioIndex=%d, pISR=%p, triggerType=%d", (int)gpioIndex, pISR, (int)triggerType);

    /* the trampoline reads s_pIsrs, so fill it in before the interrupt is armed */
    LT_SIZE nMask = LT_GetCore()->Disable();
    s_pIsrs[gpioIndex] = pISR;
    if (!Esp32GPIO_AttachISR((u8)gpioIndex, trigger, Esp32DriverGpio_IsrTrampoline, pClientData)) {
        s_pIsrs[gpioIndex] = NULL;
        LT_GetCore()->Enable(nMask);
        return false;
    }
    LT_GetCore()->Enable(nMask);

    return true;
}

static void Esp32DriverGpio_ClearGPIOPendingIRQ(Esp32DriverGpio *gpio, s16 gpioIndex) {
    LT_UNUSED(gpio);
    if (gpioIndex >= 0 && Esp32DriverGpio_IsValidIndex((u16)gpioIndex)) {
        Esp32GPIO_ClearPendingIRQ((u8)gpioIndex);
    }
}

static u64 Esp32DriverGpio_GetWakeupGPIO(Esp32DriverGpio *gpio) {
    LT_UNUSED(gpio);
    /* The esp32 has no wakeup source latch to read: GPIO interrupts are one
       shared interrupt whose status register the BSP ISR clears as it
       dispatches, so nothing survives for a caller to read afterwards. */
    return 0;
}

/*_______________________________________
  Esp32DriverGpio library lifecycle */
static bool Esp32DriverGpio_LibInit(void) {
    s_pDeviceKonfig = lt_createobject(LTDeviceKonfig);

    return s_pDeviceKonfig ? true : false;
}

static void Esp32DriverGpio_LibFini(void) {
    for (u16 i = 0; i < TotalGpioCount; ++i) {
        if (s_pIsrs[i]) {
            Esp32GPIO_DetachISR((u8)i);
            s_pIsrs[i] = NULL;
        }
    }

    lt_destroyobject(s_pDeviceKonfig);
    s_pDeviceKonfig = NULL;
}

/*_______________________________
  Esp32DriverGpio constructors */
static bool Esp32DriverGpio_ConstructObject(Esp32DriverGpio *driver) {
    LT_UNUSED(driver);
    return true;
}

static void Esp32DriverGpio_DestructObject(Esp32DriverGpio *driver) {
    LT_UNUSED(driver);
}

/*____________________
  LTLibrary binding */
define_LTObjectLibrary(1, Esp32DriverGpio_LibInit, Esp32DriverGpio_LibFini);

/*__________________________
  LTDeviceGpio api binding */

/* Note: LTDeviceGpio has a passthru api so we bind Esp32DriverGpio to LTDeviceGpio */
define_LTObjectImplPublic(LTDeviceGpio, Esp32DriverGpio,
    GetNumberOfGpios,
    GetGpioNameFromIndex,
    GetGpioModeFromIndex,
    GetGpioPullFromIndex,
    SetGpioModeFromIndex,
    SetGpioPullFromIndex,
    GetNumberOfAlternateFunctions,
    GetAlternateFunctionNameFromIndex,
    GetGpioAlternateFunctionFromIndex,
    SetGpioAlternateFunctionFromIndex,
    SetOutputValue,
    GetInputValue,
    GetNumberOfNamedPins,
    GetNamedPinFromIndex,
    GetNamedPinValueFromIndex,
    GetNamedPinValueFromName,
    SetISR,
    ClearGPIOPendingIRQ,
    GetWakeupGPIO
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  08-Sep-26   claudius    created
 *  23-Sep-26   claudius    esp32p4: named the pads up to 54
 */
