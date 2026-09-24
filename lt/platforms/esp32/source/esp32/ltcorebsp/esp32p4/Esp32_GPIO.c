/******************************************************************************
 * esp32p4/Esp32_GPIO.c                                            ESP32-P4 BSP
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

DEFINE_LTLOG_SECTION("esp32p4.gpio");

/*
 * This sits between the esp32c3 and esp32s3 arms of this file.  Like the
 * esp32s3 it has 55 pads and so carries a second bank of the OUT, ENABLE, IN
 * and STATUS registers, and like the esp32c3 it has no hole in the pad
 * numbering - every pad below kEsp32GPIO_NumPins is real.
 *
 * It also shares the esp32c3's two simplifications: the IO_MUX pad registers
 * run in pad order, so ESP32_IO_MUX_PAD_REG() indexes them directly, and
 * FUN_WPU/FUN_WPD work for every pad, there being no RTCIO block here to route
 * some of the pulls through.
 *
 * Pad hold is the one place the part is more awkward than either.  There is no
 * RTC_CNTL_PAD_HOLD; the bits are split across three registers in two power
 * domains, which is what Esp32GPIO_ConfigPinHold() below untangles.
 */

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
// using a macro here because this is used in an ISR
#define ESP32_GPIO_IS_OUTPUT(n)                                                 \
            ((n) < 32 ? (ESP32_REG(GPIO_ENABLE)  & (1u << (n))) :               \
                        (ESP32_REG(GPIO_ENABLE1) & (1u << ((n) - 32))))

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
    /* Reset value of IO_MUX FUN_DRV, roughly 20mA at 3.3V */
    kEsp32GPIO_DefaultDriveStrength     = 2,
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
 * True for pad numbers this chip actually has
 *****************************************************************************/
bool Esp32GPIO_IsValidPin(u8 nPin) {
    return nPin < kEsp32GPIO_NumPins;
}

/******************************************************************************
 * enables or disables the pad's output driver
 *****************************************************************************/
static void Esp32GPIO_SetOutputEnable(u8 nPin, bool bEnable) {
    if (nPin < 32) {
        if (bEnable) ESP32_REG(GPIO_ENABLE_W1TS)  = (1u << nPin);
        else         ESP32_REG(GPIO_ENABLE_W1TC)  = (1u << nPin);
    } else {
        if (bEnable) ESP32_REG(GPIO_ENABLE1_W1TS) = (1u << (nPin - 32));
        else         ESP32_REG(GPIO_ENABLE1_W1TC) = (1u << (nPin - 32));
    }
}

/******************************************************************************
 * ISR for GPIO interrupts.  Returns a tick advance to the BSP dispatcher, which
 * is always zero here - only the system tick line has anything to report.
 *****************************************************************************/
static u32 LT_ISR_SAFE Esp32GPIO_Isr(void) {
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
        nInterrupts &= ~(1u << nInterrupt);
    }
    while (nInterrupts1) {
        u32 nInterrupt = __builtin_ctz(nInterrupts1) + 32;
        if (nInterrupt >= kEsp32GPIO_NumPins) break;
        bool bHigh = Esp32GPIO_ReadPin(nInterrupt);
        Esp32GPIO_Interrupt *pInterrupt = s_Interrupts + nInterrupt;
        if (ESP32_GPIO_SHOULD_INTERRUPT(pInterrupt->trigger, bHigh)) {
            pInterrupt->pISR(nInterrupt, bHigh, pInterrupt->pClientData);
        }
        nInterrupts1 &= ~(1u << (nInterrupt - 32));
    }

    return 0;
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

    if (!Esp32GPIO_IsValidPin(nPin)) {
        LTLOG_YELLOWALERT("invalid.config.pin", "Invalid GPIO pin %d", nPin);
        return false;
    }

    if (direction == kEsp32GPIO_Direction_Input) {
        // disable output
        Esp32GPIO_SetOutputEnable(nPin, false);

        // set the pull type
        nIOMuxVal |= (pull == kEsp32GPIO_PullUp   ? ESP32_REG_MASK(IO_MUX, FUN_WPU) : 0);
        nIOMuxVal |= (pull == kEsp32GPIO_PullDown ? ESP32_REG_MASK(IO_MUX, FUN_WPD) : 0);

        // enable input
        nIOMuxVal |= ESP32_REG_MASK(IO_MUX, FUN_IE);
    } else if (direction == kEsp32GPIO_Direction_Output) {
        // default output type to push-pull
        ESP32_REG_ARRAY_VALUE(GPIO_PIN0, nPin) &= ~ESP32_REG_MASK(GPIO_PIN, PAD_DRIVER);

        // enable output
        Esp32GPIO_SetOutputEnable(nPin, true);
    }

    // set the IO_MUX function
    nIOMuxVal |= (func << ESP32_REG_SHIFT(IO_MUX, MCU_SEL));

    // set the driver strength to a default of 2 (from the manual)
    nIOMuxVal |= (kEsp32GPIO_DefaultDriveStrength << ESP32_REG_SHIFT(IO_MUX, FUN_DRV));

    // set the register value
    ESP32_IO_MUX_PAD_REG(nPin) = nIOMuxVal;

    return true;
}

/******************************************************************************
 * configures the output type
 ****************************************************************************/
void Esp32GPIO_ConfigOutputType(u8 nPin, Esp32GPIO_OutputType outputType) {
    if (!Esp32GPIO_IsValidPin(nPin)) {
        return;
    }

    if (outputType == kEsp32GPIO_OutputType_OpenDrain) {
        ESP32_REG_ARRAY_VALUE(GPIO_PIN0, nPin) |= ESP32_REG_MASK(GPIO_PIN, PAD_DRIVER);
    } else {
        ESP32_REG_ARRAY_VALUE(GPIO_PIN0, nPin) &= ~ESP32_REG_MASK(GPIO_PIN, PAD_DRIVER);
    }
}

/******************************************************************************
 * sets just the IO_MUX function select for a pin, leaving the pull, input
 * enable and drive strength fields as they are
 ****************************************************************************/
void Esp32GPIO_ConfigPinFunction(u8 nPin, Esp32GPIO_Function func) {
    if (!Esp32GPIO_IsValidPin(nPin)) {
        return;
    }

    u32 nIOMuxVal = ESP32_IO_MUX_PAD_REG(nPin);
    nIOMuxVal &= ~ESP32_REG_MASK(IO_MUX, MCU_SEL);
    nIOMuxVal |= (func << ESP32_REG_SHIFT(IO_MUX, MCU_SEL)) & ESP32_REG_MASK(IO_MUX, MCU_SEL);
    ESP32_IO_MUX_PAD_REG(nPin) = nIOMuxVal;
}

/******************************************************************************
 * sets just the IO_MUX drive strength for a pin.  0..3 select roughly 5, 10,
 * 20 and 40mA at 3.3V; the reset default is 2.
 ****************************************************************************/
void Esp32GPIO_ConfigPinDriveStrength(u8 nPin, u8 nDriveStrength) {
    if (!Esp32GPIO_IsValidPin(nPin)) {
        return;
    }

    u32 nIOMuxVal = ESP32_IO_MUX_PAD_REG(nPin);
    nIOMuxVal &= ~ESP32_REG_MASK(IO_MUX, FUN_DRV);
    nIOMuxVal |= (nDriveStrength << ESP32_REG_SHIFT(IO_MUX, FUN_DRV)) &
                 ESP32_REG_MASK(IO_MUX, FUN_DRV);
    ESP32_IO_MUX_PAD_REG(nPin) = nIOMuxVal;
}

/******************************************************************************
 * sets just the pull for a pin, leaving the function, input enable and drive
 * strength fields as they are
 ****************************************************************************/
void Esp32GPIO_ConfigPinPull(u8 nPin, Esp32GPIO_PullType pull) {
    if (!Esp32GPIO_IsValidPin(nPin)) {
        return;
    }

    u32 nIOMuxVal = ESP32_IO_MUX_PAD_REG(nPin);
    nIOMuxVal &= ~(ESP32_REG_MASK(IO_MUX, FUN_WPU) | ESP32_REG_MASK(IO_MUX, FUN_WPD));
    nIOMuxVal |= (pull == kEsp32GPIO_PullUp   ? ESP32_REG_MASK(IO_MUX, FUN_WPU) : 0);
    nIOMuxVal |= (pull == kEsp32GPIO_PullDown ? ESP32_REG_MASK(IO_MUX, FUN_WPD) : 0);
    ESP32_IO_MUX_PAD_REG(nPin) = nIOMuxVal;
}

/******************************************************************************
 * sets just the IO_MUX input enable for a pin.  An output pad with the input
 * enabled also feeds the GPIO input register and the GPIO matrix.
 ****************************************************************************/
void Esp32GPIO_ConfigPinInputEnable(u8 nPin, bool bEnable) {
    if (!Esp32GPIO_IsValidPin(nPin)) {
        return;
    }

    u32 nIOMuxVal = ESP32_IO_MUX_PAD_REG(nPin);
    if (bEnable) nIOMuxVal |=  ESP32_REG_MASK(IO_MUX, FUN_IE);
    else         nIOMuxVal &= ~ESP32_REG_MASK(IO_MUX, FUN_IE);
    ESP32_IO_MUX_PAD_REG(nPin) = nIOMuxVal;
}

/******************************************************************************
 * Configures the given pin to hold the current value or clears that
 * functionality.  The hold registers are cleared at reboot in Esp32_LTChipStart.
 *
 * The bits are split three ways on this part, by power domain and then by
 * register width: pads 0..15 are LP pads and hold from LP_IO_MUX at bit == pad,
 * and the digital pads hold from HP_SYSTEM, the first 32 of them in CTRL0 at
 * bit == pad - 16 and the remainder in CTRL1 at bit == pad - 48.
 ****************************************************************************/
void Esp32GPIO_ConfigPinHold(u8 nPin, bool bPinHold) {
    if (!Esp32GPIO_IsValidPin(nPin) || !ESP32_GPIO_IS_OUTPUT(nPin)) return;

    volatile u32 *pRegister;
    u32           nBit;

    if (nPin <= kEsp32_RegisterPAD_HOLD_LP_MAX_PAD) {
        pRegister = ESP32_REG_ADDR(LP_IO_MUX_PAD_HOLD);
        nBit      = nPin;
    } else if (nPin < 48) {
        pRegister = ESP32_REG_ADDR(HP_SYS_GPIO_HOLD_CTRL0);
        nBit      = nPin - 16;
    } else {
        pRegister = ESP32_REG_ADDR(HP_SYS_GPIO_HOLD_CTRL1);
        nBit      = nPin - 48;
    }

    if (bPinHold) *pRegister |=  (1u << nBit);
    else          *pRegister &= ~(1u << nBit);
}

/******************************************************************************
 * Releases every held pad.  Not all resets clear these registers, so
 * Esp32_LTChipStart calls this on the way up to keep a pad configuration from a
 * previous boot from being held into this one.
 ****************************************************************************/
void Esp32GPIO_ClearAllPinHolds(void) {
    ESP32_REG(LP_IO_MUX_PAD_HOLD)      = 0;
    ESP32_REG(HP_SYS_GPIO_HOLD_CTRL0)  = 0;
    ESP32_REG(HP_SYS_GPIO_HOLD_CTRL1)  = 0;
}

/******************************************************************************
 * configures the given pin to use the GPIO matrix
 * nSignal values are defined in the GPIO Matrix chapter of the reference manual
 ****************************************************************************/
void Esp32GPIO_ConfigMatrixPin(u8 nPin, u8 nSignal, Esp32GPIO_Direction direction, bool bInv) {
    if (!Esp32GPIO_IsValidPin(nPin)) {
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
        Esp32GPIO_SetOutputEnable(nPin, true);
    }
}

/******************************************************************************
 * Clears pins configuration
 *****************************************************************************/
void Esp32GPIO_ClearPinConfig(u8 nPin) {
    if (!Esp32GPIO_IsValidPin(nPin)) {
        return;
    }

    // disable output
    Esp32GPIO_SetOutputEnable(nPin, false);

    // set the input and function registers to their reset value
    ESP32_IO_MUX_PAD_REG(nPin)                          = 0;
    ESP32_REG_ARRAY_VALUE(GPIO_FUNC0_OUT_SEL_CFG, nPin) = ESP32_REG_VAL(GPIO_FUNC_OUT_SEL_CFG, GPIO_OUT);
}

/******************************************************************************
 * Configures the pin interrupt, enables it, and installs the ISR
 *****************************************************************************/
bool Esp32GPIO_AttachISR(u8 nPin, Esp32GPIO_Trigger trigger, Esp32_IRQCallback *pISR, void *pClientData) {
    if (!Esp32GPIO_IsValidPin(nPin)) {
        LTLOG_YELLOWALERT("invalid.isr.pin", "Invalid GPIO pin %d", nPin);
        return false;
    }

    // route the shared GPIO source and install the line's vector
    // This will get called with each installed ISR, but it's harmless and saves the need
    // for an init function. This function should be rarely called
    Esp32_AttachInterrupt(kEsp32_ExternalIrq_GPIO, kEsp32_IrqNumber_GPIO,
                          kEsp32_IrqType_Level, kEsp32_IrqPriority_GPIO, Esp32GPIO_Isr);

    // set the ISR and trigger in the interrupts tables
    s_Interrupts[nPin] = (Esp32GPIO_Interrupt) { trigger, pISR, pClientData };

    // keep the open drain config as-is
    u32 nRegVal = ESP32_REG_ARRAY_VALUE(GPIO_PIN0, nPin) & ESP32_REG_MASK(GPIO_PIN, PAD_DRIVER);

    // enable the interrupt
    nRegVal |= ESP32_REG_VAL(GPIO_PIN, INT_ENA) << ESP32_REG_SHIFT(GPIO_PIN, INT_ENA);

    // set trigger type
    nRegVal |= (trigger << ESP32_REG_SHIFT(GPIO_PIN, INT_TYPE));
    ESP32_REG_ARRAY_VALUE(GPIO_PIN0, nPin) = nRegVal;

    return true;
}

/******************************************************************************
 * Disables the pin interrupts and removes the handler
 *****************************************************************************/
void Esp32GPIO_DetachISR(u8 nPin) {
    if (!Esp32GPIO_IsValidPin(nPin)) {
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
    if (!Esp32GPIO_IsValidPin(nPin)) {
        return false;
    }

    u32 nVal;
    if (nPin < 32) {
        nVal = ESP32_GPIO_IS_OUTPUT(nPin) ? ESP32_REG(GPIO_OUT)  : ESP32_REG(GPIO_IN);
        return ((nVal & (1u << nPin)) != 0);
    }
    nVal = ESP32_GPIO_IS_OUTPUT(nPin) ? ESP32_REG(GPIO_OUT1) : ESP32_REG(GPIO_IN1);
    return ((nVal & (1u << (nPin - 32))) != 0);
}

/******************************************************************************
 * Write the given value to the pin (pin direction must be output)
 *****************************************************************************/
void Esp32GPIO_WritePin(u8 nPin, bool bVal) {
    if (!Esp32GPIO_IsValidPin(nPin) || !ESP32_GPIO_IS_OUTPUT(nPin)) {
        return;
    }

    if (nPin < 32) {
        bVal ? (ESP32_REG(GPIO_OUT_W1TS)  = (1u << nPin)) :
               (ESP32_REG(GPIO_OUT_W1TC)  = (1u << nPin));
    } else {
        bVal ? (ESP32_REG(GPIO_OUT1_W1TS) = (1u << (nPin - 32))) :
               (ESP32_REG(GPIO_OUT1_W1TC) = (1u << (nPin - 32)));
    }
}

/******************************************************************************
 * Reads back a pad's configuration
 *****************************************************************************/
bool Esp32GPIO_GetPinConfig(u8 nPin, Esp32GPIO_PinConfig *pConfig) {
    if (!Esp32GPIO_IsValidPin(nPin) || pConfig == NULL) {
        return false;
    }

    u32 nIOMuxVal = ESP32_IO_MUX_PAD_REG(nPin);

    pConfig->direction     = ESP32_GPIO_IS_OUTPUT(nPin) ? kEsp32GPIO_Direction_Output :
                                                          kEsp32GPIO_Direction_Input;
    pConfig->bInputEnabled = (nIOMuxVal & ESP32_REG_MASK(IO_MUX, FUN_IE)) != 0;
    pConfig->func          = (nIOMuxVal & ESP32_REG_MASK(IO_MUX, MCU_SEL)) >>
                             ESP32_REG_SHIFT(IO_MUX, MCU_SEL);
    pConfig->pull          = (nIOMuxVal & ESP32_REG_MASK(IO_MUX, FUN_WPU)) ? kEsp32GPIO_PullUp   :
                             (nIOMuxVal & ESP32_REG_MASK(IO_MUX, FUN_WPD)) ? kEsp32GPIO_PullDown :
                                                                             kEsp32GPIO_PullNone;
    return true;
}

/******************************************************************************
 * Clears a pending interrupt for the pin.  Esp32GPIO_Isr() clears the whole
 * status register as it dispatches, so this is only needed for a pin whose
 * interrupt is enabled with no ISR attached.
 *****************************************************************************/
void Esp32GPIO_ClearPendingIRQ(u8 nPin) {
    if (!Esp32GPIO_IsValidPin(nPin)) {
        return;
    }

    if (nPin < 32) ESP32_REG(GPIO_STATUS_W1TC)  = (1u << nPin);
    else           ESP32_REG(GPIO_STATUS1_W1TC) = (1u << (nPin - 32));
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  23-Sep-26   claudius    created, from esp32c3/Esp32_GPIO.c
 */
