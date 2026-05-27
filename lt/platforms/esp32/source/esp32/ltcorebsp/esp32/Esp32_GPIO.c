/******************************************************************************
 * esp32/Esp32_GPIO.c                                                 ESP32 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *****************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>

#include "Esp32_Registers.h"
#include "Esp32_Irq.h"
#include "Esp32_SoC.h"
#include "Esp32_GPIO.h"

DEFINE_LTLOG_SECTION("esp32.gpio");

/******************************************************************************
 * macros
 *****************************************************************************/
// the check below is needed because GPIO's are shared interrupts and always
// trigger on both edges.
// Using a macro here to avoid a function call in the ISR
#define ESP32_GPIO_SHOULD_INTERRUPT(trig, state)                                \
            (trig == kEsp32GPIO_Trigger_Both) ||                                \
            (state  &&  (trig & 0x01))        || /* high interrupts are odd */  \
            (!state && !(trig & 0x01))           /* low interrupts are even */

// checks if the pin is configured for output
// using an macro here because this is used in an ISR
#define ESP32_GPIO_IS_OUTPUT(n)                                                 \
            (n <= 31 ? (ESP32_REG(GPIO_OUT_ENABLE)  & (1 << n)) :            \
                       (ESP32_REG(GPIO_OUT_ENABLE1) & (1 << n)))

/******************************************************************************
 * typedefs
 *****************************************************************************/
typedef struct {
    Esp32GPIO_Trigger  trigger;
    Esp32_IRQCallback *pISR;
    void              *pClientData;
} Esp32GPIO_Interrupt;

/******************************************************************************
 * constants
 *****************************************************************************/
enum {
    kEsp32GPIO_InvalidRegister          = 0x00,
    kEsp32GPIO_MaxHoldPin               = 0x17  /* pin 23 is the highest to hold */
};
#define _NOP                            LT_U8_MAX // no pin

// the offsets below are needed because the registers' offsets don't align with
// the pin numbers
static const u8 kIOMuxPinRegOffsets[]   = {
    0x44, 0x88, 0x40, 0x84,
    0x48, 0x6c, 0x60, 0x64,
    0x68, 0x54, 0x58, 0x5c,
    0x34, 0x38, 0x30, 0x3c,
    0x4c, 0x50, 0x70, 0x74,
    0x78, 0x7c, 0x80, 0x8c,
    0x90, 0x24, 0x28, 0x2c,
    _NOP, _NOP, _NOP, _NOP,
    0x1c, 0x20, 0x14, 0x18,
    0x04, 0x08, 0x0c, 0x10,
};

// hold register doesn't use the bit N to control the pin N, so an explicit
// map is required. An index in the map is the pin number, and the value is
// the mask applied to RTCIO_DIG_PAD_HOLD to control that pin.
// The pins that can't be held have 0 for their mapped value. Some of them are
// GPIO pins used for RTC I/O and they are handled differently. These are
// 0, 2, 4, 12, 13, 14, 15, 25, 26, 27, and they match the pins in kPUPDRegMap
// that don't have InvalidRegister as a value.
// Pin 20 is only available on ESP32-PICO-V3 package.
// Pins 24, 28, 29, 30, 31 don't exist, see soc/esp32/gpio_periph.c.
static const u32 kPinHoldBitMask[]       = {
           0, 1U <<  1,        0, 1U <<  0,
           0, 1U <<  8, 1U <<  2, 1U <<  3,
    1U <<  4, 1U <<  5, 1U <<  6, 1U <<  7,
           0,        0,        0,        0,
    1U <<  9, 1U << 10, 1U << 11, 1U << 12,
           0, 1U << 14, 1U << 15, 1U << 16,
           0,        0,        0,        0,
           0,        0,        0,        0
};

// the register map below is needed as some pins use different registers for pullup and pulldown.
// The PU and PD bits are not documented is some of the registers
static volatile u32 * const kPUPDRegMap[kEsp32GPIO_NumPins] = {
    &ESP32_REG_ARRAY_VALUE(RTCIO_TOUCH_PAD0, 1), kEsp32GPIO_InvalidRegister,                  &ESP32_REG_ARRAY_VALUE(RTCIO_TOUCH_PAD0, 2),
    kEsp32GPIO_InvalidRegister,                  &ESP32_REG_ARRAY_VALUE(RTCIO_TOUCH_PAD0, 0), kEsp32GPIO_InvalidRegister,
    kEsp32GPIO_InvalidRegister,                  kEsp32GPIO_InvalidRegister,                  kEsp32GPIO_InvalidRegister,
    kEsp32GPIO_InvalidRegister,                  kEsp32GPIO_InvalidRegister,                  kEsp32GPIO_InvalidRegister,
    &ESP32_REG_ARRAY_VALUE(RTCIO_TOUCH_PAD0, 5), &ESP32_REG_ARRAY_VALUE(RTCIO_TOUCH_PAD0, 4), &ESP32_REG_ARRAY_VALUE(RTCIO_TOUCH_PAD0, 6),
    &ESP32_REG_ARRAY_VALUE(RTCIO_TOUCH_PAD0, 3), kEsp32GPIO_InvalidRegister,                  kEsp32GPIO_InvalidRegister,
    kEsp32GPIO_InvalidRegister,                  kEsp32GPIO_InvalidRegister,                  kEsp32GPIO_InvalidRegister,
    kEsp32GPIO_InvalidRegister,                  kEsp32GPIO_InvalidRegister,                  kEsp32GPIO_InvalidRegister,
    kEsp32GPIO_InvalidRegister,                  &ESP32_REG(RTCIO_PAD_DAC1),                  &ESP32_REG(RTCIO_PAD_DAC2),
    &ESP32_REG_ARRAY_VALUE(RTCIO_TOUCH_PAD0, 7), kEsp32GPIO_InvalidRegister,                  kEsp32GPIO_InvalidRegister,
    kEsp32GPIO_InvalidRegister,                  kEsp32GPIO_InvalidRegister,                  &ESP32_REG(RTCIO_XTAL_32K_PAD),
    &ESP32_REG(RTCIO_XTAL_32K_PAD),              &ESP32_REG(RTCIO_ADC_PAD),                   &ESP32_REG(RTCIO_ADC_PAD),
    &ESP32_REG(RTCIO_SENSOR_PADS),               &ESP32_REG(RTCIO_SENSOR_PADS),               &ESP32_REG(RTCIO_SENSOR_PADS),
    &ESP32_REG(RTCIO_SENSOR_PADS)
};

/******************************************************************************
 * noop ISR for avoiding a check for NULL in the ISR dispatcher loop
 *****************************************************************************/
static void _gpio_isr_noop(u8 nPin, bool noop, void *pClientData) {
    LT_UNUSED(nPin);
    LT_UNUSED(noop);
    LT_UNUSED(pClientData);
};
static const Esp32GPIO_Interrupt _noop_interrupt                = {
    kEsp32GPIO_Trigger_Disabled,
    _gpio_isr_noop,
    NULL
};

/******************************************************************************
 * static variables
 *****************************************************************************/
static Esp32GPIO_Interrupt s_Interrupts[kEsp32GPIO_NumPins]     = { _noop_interrupt             };

/******************************************************************************
 * ISR for GPIO interrupts
 *****************************************************************************/
static void Esp32GPIO_Isr(void) {
    // save the current interrupt status
    u32 nInterrupts  = ESP32_REG(GPIO_STATUS);
    u32 nInterrupts1 = ESP32_REG(GPIO_STATUS1);

    // clear the interrupts
    ESP32_REG(GPIO_STATUS_W1TC)  = LT_U32_MAX;
    ESP32_REG(GPIO_STATUS1_W1TC) = LT_U32_MAX;

    // dispatch the interrupts
    while (nInterrupts) {
        u32 nInterrupt = __builtin_ctz(nInterrupts);
        bool bHigh = Esp32GPIO_ReadPin(nInterrupt);
        Esp32GPIO_Interrupt *pInterrupt = s_Interrupts + nInterrupt;
        if (ESP32_GPIO_SHOULD_INTERRUPT(pInterrupt->trigger, bHigh)) {
            pInterrupt->pISR(nInterrupt, bHigh, pInterrupt->pClientData);
        }
        nInterrupts &= ~(1 << nInterrupt);
    }
    while (nInterrupts1) {
        u32 nInterrupt = __builtin_ctz(nInterrupts1) + 32;
        bool bHigh = Esp32GPIO_ReadPin(nInterrupt);
        Esp32GPIO_Interrupt *pInterrupt = s_Interrupts + nInterrupt;
        if (ESP32_GPIO_SHOULD_INTERRUPT(pInterrupt->trigger, bHigh)) {
            pInterrupt->pISR(nInterrupt, bHigh, pInterrupt->pClientData);
        }
        nInterrupts1 &= ~(1 << (nInterrupt - 32));
    }
}

/******************************************************************************
 * Configures the given pin according to the params
 * If pin is configured as output, the output types defaults to push-pull. Use
 * the Esp32GPIO_ConfigOutputType() function to change it
 *****************************************************************************/
bool Esp32GPIO_ConfigPin(u8 nPin,
                         Esp32GPIO_Direction direction,
                         Esp32GPIO_PullType pull,
                         Esp32GPIO_Function func) {
    u32 nIOMuxVal = 0;

    if (nPin >= kEsp32GPIO_NumPins || kIOMuxPinRegOffsets[nPin] == _NOP) {
        LTLOG_YELLOWALERT("invalid.config.pin", "Invalid GPIO pin %d", nPin);
        return false;
    }

    if (direction == kEsp32GPIO_Direction_Input) {
        // disable output
        if (nPin <= 31) {
            ESP32_REG(GPIO_OUTPUT_ENABLE_W1TC) = (1 << nPin);
        } else {
            ESP32_REG(GPIO_OUTPUT_ENABLE1_W1TC) = (1 << (nPin - 32));
        }

        // set the pull type
        // not all pins use the IO_MUX registers, so need to use a map for the correct ones
        if (kPUPDRegMap[nPin] != kEsp32GPIO_InvalidRegister) {
            u32 puMask = ESP32_REG_MASK(RTCIO, PU);
            u32 pdMask = ESP32_REG_MASK(RTCIO, PD);
            // need to special case pin 32 as it uses different bits
            if (nPin == 32) {
                puMask = ESP32_REG_MASK(RTCIO, PIN32_PU);
                pdMask = ESP32_REG_MASK(RTCIO, PIN32_PD);
            }
            u32 regVal = *kPUPDRegMap[nPin];
            switch (pull) {
                case kEsp32GPIO_PullUp:
                    regVal |= puMask;
                    regVal &= ~pdMask;
                    break;
                case kEsp32GPIO_PullDown:
                    regVal &= ~puMask;
                    regVal |= pdMask;
                    break;
                default:
                    regVal &= ~puMask;
                    regVal &= ~pdMask;
                    break;
            }
            *kPUPDRegMap[nPin] = regVal;
        }
        nIOMuxVal |= (pull == kEsp32GPIO_PullUp   ? ESP32_REG_MASK(GPIO_IO_MUX, FUN_WPU) : 0);
        nIOMuxVal |= (pull == kEsp32GPIO_PullDown ? ESP32_REG_MASK(GPIO_IO_MUX, FUN_WPD) : 0);

        // enable input
        nIOMuxVal |= ESP32_REG_MASK(GPIO_IO_MUX, FUN_IE);
    } else if (direction == kEsp32GPIO_Direction_Output) {
        // disable input
        nIOMuxVal &= ~ESP32_REG_MASK(GPIO_IO_MUX, FUN_IE);

        // default output type to push-pull
        ESP32_REG_ARRAY_VALUE(GPIO_PIN0, nPin) &= ~ESP32_REG_MASK(GPIO_PIN, OPENDRAIN);

        // enable output
        if (nPin <= 31) {
            ESP32_REG(GPIO_OUTPUT_ENABLE_W1TS) = (1 << nPin);
        } else {
            ESP32_REG(GPIO_OUTPUT_ENABLE1_W1TS) = (1 << (nPin - 32));
        }
    }

    // set the IO_MUX function
    nIOMuxVal |= (func << ESP32_REG_SHIFT(GPIO_IO_MUX, MCU_SEL));

    // set the driver strength to a default of 2 (from the manual)
    nIOMuxVal |= (2 << ESP32_REG_SHIFT(GPIO_IO_MUX, FUN_DRV));

    // set the register value
    ESP32_GPIO_IO_MUX_REG(kIOMuxPinRegOffsets[nPin]) = nIOMuxVal;

    return true;
}

/******************************************************************************
 * configures the output type
 ****************************************************************************/
void Esp32GPIO_ConfigOutputType(u8 nPin, Esp32GPIO_OutputType outputType) {
    if (nPin >= kEsp32GPIO_NumPins) {
        return;
    }

    if (outputType == kEsp32GPIO_OutputType_OpenDrain) {
        ESP32_REG_ARRAY_VALUE(GPIO_PIN0, nPin) |= ESP32_REG_MASK(GPIO_PIN, OPENDRAIN);
    } else {
        ESP32_REG_ARRAY_VALUE(GPIO_PIN0, nPin) &= ~ESP32_REG_MASK(GPIO_PIN, OPENDRAIN);
    }
}

/******************************************************************************
 * Configures the given pin to hold the current value  or clears that functionality.
 * At reboot, the register RTCIO_DIG_PAD_HOLD ris cleared in Esp32_LTChipStart.
 ****************************************************************************/

void Esp32GPIO_ConfigPinHold(u8 nPin, bool bPinHold) {
    if (nPin > kEsp32GPIO_MaxHoldPin || !ESP32_GPIO_IS_OUTPUT(nPin)) return;
    if (bPinHold) {
        ESP32_REG(RTCIO_DIG_PAD_HOLD) |= kPinHoldBitMask[nPin];
    }
    else {
        ESP32_REG(RTCIO_DIG_PAD_HOLD) &= ~kPinHoldBitMask[nPin];
    }
}

/******************************************************************************
 * configures the given pin to use the GPIO matrix
 * nSignal values are defined in table 17 (GPIO Matrix Peripheral Signals) in the
 * reference manual
 ****************************************************************************/
void Esp32GPIO_ConfigMatrixPin(u8 nPin, u8 nSignal, Esp32GPIO_Direction direction, bool bInv) {
    if (nPin >= kEsp32GPIO_NumPins) {
        return;
    }

    if (direction == kEsp32GPIO_Direction_Input) {
        u32 nRegVal = 0;
        // enable the GPIO matrix
        nRegVal = ESP32_REG_MASK(GPIO_FUNC_IN_SEL_CFG, USE_MATRIX) | nPin;
        nRegVal |= (bInv ? ESP32_REG_MASK(GPIO_FUNC_IN_SEL_CFG, IN_INVERT) : 0);
        ESP32_REG_ARRAY_VALUE(GPIO_FUNC0_IN_SEL_CFG, nSignal) = nRegVal;
    } else if (direction == kEsp32GPIO_Direction_Output) {
        u32 nRegVal = nSignal;
        nRegVal |= (bInv ? ESP32_REG_MASK(GPIO_FUNC_OUT_SEL_CFG, OUT_INV) : 0);
        ESP32_REG_ARRAY_VALUE(GPIO_FUNC0_OUT_SEL_CFG, nPin) = nRegVal;

        // enable output
        if (nPin <= 31) {
            ESP32_REG(GPIO_OUTPUT_ENABLE_W1TS) = (1 << nPin);
        } else {
            ESP32_REG(GPIO_OUTPUT_ENABLE1_W1TS) = (1 << (nPin - 32));
        }
    }
}

/******************************************************************************
 * Clears pins configuration
 *****************************************************************************/
void Esp32GPIO_ClearPinConfig(u8 nPin) {
    if (nPin >= kEsp32GPIO_NumPins) {
        return;
    }

    // disable output
    if (nPin <= 31) {
        ESP32_REG(GPIO_OUTPUT_ENABLE_W1TC) = (1 << nPin);
    } else {
        ESP32_REG(GPIO_OUTPUT_ENABLE1_W1TC) = (1 << (nPin - 32));
    }
    // set the input and function registers to their reset value
    if (kIOMuxPinRegOffsets[nPin] != _NOP) {
        ESP32_GPIO_IO_MUX_REG(kIOMuxPinRegOffsets[nPin])    = 0;
        ESP32_REG_ARRAY_VALUE(GPIO_FUNC0_OUT_SEL_CFG, nPin) = 0x800;
    }
}

/******************************************************************************
 * Configures the pin interrupt, enables it, and installs the ISR
 *****************************************************************************/
bool Esp32GPIO_AttachISR(u8 nPin, Esp32GPIO_Trigger trigger, Esp32_IRQCallback *pISR, void *pClientData) {
    if (nPin >= kEsp32GPIO_NumPins) {
        LTLOG_YELLOWALERT("invalid.isr.pin", "Invalid GPIO pin %d", nPin);
        return false;
    }

    // set LTCore ISR
    // This will get called with each installed ISR, but it's harmless and saves the need
    // for an init function. This function should be rarely called
    Esp32MapExternalToCPUIrq(kEsp32_CPU0, kEsp32_ExternalIrq_GPIO, kEsp32_IrqNumber_GPIO);
    LT_GetCore()->SetInterruptVector(kEsp32_IrqNumber_GPIO, Esp32GPIO_Isr, kEsp32_IrqPriority_GPIO);

    // set the ISR and trigger in the interrupts tables
    s_Interrupts[nPin] = (Esp32GPIO_Interrupt) { trigger, pISR, pClientData };

    // keep the open drain config as-is
    u32 nRegVal = ESP32_REG_ARRAY_VALUE(GPIO_PIN0, nPin) & ESP32_REG_MASK(GPIO_PIN, OPENDRAIN);

    // enable cpu0 interrupt
    nRegVal |= ESP32_REG_VAL(GPIO_PIN, PRO_INT_ENA) << ESP32_REG_SHIFT(GPIO_PIN, PRO_INT_ENA);

    // set trigger type
    nRegVal |= (trigger << ESP32_REG_SHIFT(GPIO_PIN, INT_TYPE));
    ESP32_REG_ARRAY_VALUE(GPIO_PIN0, nPin) = nRegVal;

    return true;
}

/******************************************************************************
 * Disables the pin interrupts and removes the handler
 *****************************************************************************/
void Esp32GPIO_DetachISR(u8 nPin) {
    if (nPin >= kEsp32GPIO_NumPins) {
        return;
    }
    // disable the interrupt
    ESP32_REG_ARRAY_VALUE(GPIO_PIN0, nPin) = 0;

    // clear ISR
    s_Interrupts[nPin] = _noop_interrupt;
}

/******************************************************************************
 * Read the value of the given pin
 *****************************************************************************/
bool Esp32GPIO_ReadPin(u8 nPin) {
    if (nPin >= kEsp32GPIO_NumPins) {
        return false;
    }

    u32 nVal = 0;
    if (nPin < 32) {
        nVal = ESP32_GPIO_IS_OUTPUT(nPin) ? ESP32_REG(GPIO_OUT) :
                                            ESP32_REG(GPIO_IN);
    } else {
        nVal = ESP32_GPIO_IS_OUTPUT(nPin) ? ESP32_REG(GPIO_OUT1) :
                                            ESP32_REG(GPIO_IN1);
        nPin -= 32;
    }
    return ((nVal & (1 << nPin)) != 0);
}

/******************************************************************************
 * Write the given value to the pin (pin direction must be output)
 *****************************************************************************/
void Esp32GPIO_WritePin(u8 nPin, bool bVal) {
    if (nPin >= kEsp32GPIO_NumPins) {
        return;
    }

    if (ESP32_GPIO_IS_OUTPUT(nPin)) {
        if (nPin < 32) {
            bVal ? (ESP32_REG(GPIO_OUT_W1TS) = (1 << nPin)) :
                   (ESP32_REG(GPIO_OUT_W1TC) = (1 << nPin));
        } else {
            bVal ? (ESP32_REG(GPIO_OUT1_W1TS) = (1 << (nPin - 32))) :
                   (ESP32_REG(GPIO_OUT1_W1TC) = (1 << (nPin - 32)));
        }
    }
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  08-Jul-22   vitellius   created
 *  14-Aug-23   commodus    added ConfigurePinHold
 */
