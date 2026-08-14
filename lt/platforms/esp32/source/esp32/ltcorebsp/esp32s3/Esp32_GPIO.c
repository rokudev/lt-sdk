/******************************************************************************
 * esp32s3/Esp32_GPIO.c                                            ESP32-S3 BSP
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

DEFINE_LTLOG_SECTION("esp32s3.gpio");

/*
 * This is a smaller file than the esp32 arm, and the reason is that the esp32s3
 * dropped the two irregularities that arm has to carry tables for.
 *
 * The esp32's IO_MUX pad registers are ordered by pad *name* rather than pad
 * number, so it needs kIOMuxPinRegOffsets to find a pad's register; here the pads
 * are one word apart in pad order and ESP32_IO_MUX_PAD_REG() indexes them
 * directly.  And on the esp32 nine of the RTC capable pads ignore the IO_MUX pull
 * bits and have to be pulled through whichever RTCIO register happens to own
 * them, which is what kPUPDRegMap is for; on this part FUN_WPU and FUN_WPD work
 * for every pad, so the pull is one write no matter which pad it is.
 *
 * What the esp32s3 adds is a hole: GPIO22 through GPIO25 are not bonded out on
 * any package, so a pad number below kEsp32GPIO_NumPins is not by itself a valid
 * pad.  Esp32GPIO_IsValidPin() is the single place that knows this.
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
    /* GPIO0..GPIO21 are the RTC capable pads; GPIO22..GPIO48 are digital only */
    kEsp32GPIO_MaxRtcPin                = 21,
    /* The pads that no esp32s3 package bonds out */
    kEsp32GPIO_FirstAbsentPin           = 22,
    kEsp32GPIO_LastAbsentPin            = 25,
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
    return nPin < kEsp32GPIO_NumPins &&
           (nPin < kEsp32GPIO_FirstAbsentPin || nPin > kEsp32GPIO_LastAbsentPin);
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
        nInterrupts &= ~(1u << nInterrupt);
    }
    while (nInterrupts1) {
        u32 nInterrupt = __builtin_ctz(nInterrupts1) + 32;
        bool bHigh = Esp32GPIO_ReadPin(nInterrupt);
        Esp32GPIO_Interrupt *pInterrupt = s_Interrupts + nInterrupt;
        if (ESP32_GPIO_SHOULD_INTERRUPT(pInterrupt->trigger, bHigh)) {
            pInterrupt->pISR(nInterrupt, bHigh, pInterrupt->pClientData);
        }
        nInterrupts1 &= ~(1u << (nInterrupt - 32));
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
 * Configures the given pin to hold the current value or clears that
 * functionality.  Both hold registers are cleared at reboot in
 * Esp32_LTChipStart.
 *
 * The esp32 held every pad from one RTCIO register through a bit map; here the
 * RTC capable pads are held from RTC_CNTL_PAD_HOLD at bit == pad, and the
 * digital pads from RTC_CNTL_DIG_PAD_HOLD at bit == pad - 21.
 ****************************************************************************/
void Esp32GPIO_ConfigPinHold(u8 nPin, bool bPinHold) {
    if (!Esp32GPIO_IsValidPin(nPin) || !ESP32_GPIO_IS_OUTPUT(nPin)) return;

    volatile u32 *pReg = (nPin <= kEsp32GPIO_MaxRtcPin) ? ESP32_REG_ADDR(RTC_CNTL_PAD_HOLD)
                                                       : ESP32_REG_ADDR(RTC_CNTL_DIG_PAD_HOLD);
    u32 nMask = (nPin <= kEsp32GPIO_MaxRtcPin) ? (1u << nPin)
                                               : (1u << (nPin - kEsp32GPIO_MaxRtcPin));
    if (bPinHold) {
        *pReg |= nMask;
    }
    else {
        *pReg &= ~nMask;
    }
}

/******************************************************************************
 * Releases every held pad.  Not all resets clear these two registers, so
 * Esp32_LTChipStart calls this on the way up to keep a pad configuration from a
 * previous boot from being held into this one.
 ****************************************************************************/
void Esp32GPIO_ClearAllPinHolds(void) {
    ESP32_REG(RTC_CNTL_PAD_HOLD)     = 0;
    ESP32_REG(RTC_CNTL_DIG_PAD_HOLD) = 0;
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

    // set LTCore ISR
    // This will get called with each installed ISR, but it's harmless and saves the need
    // for an init function. This function should be rarely called
    Esp32MapExternalToCPUIrq(kEsp32_CPU0, kEsp32_ExternalIrq_GPIO, kEsp32_IrqNumber_GPIO);
    LT_GetCore()->SetInterruptVector(kEsp32_IrqNumber_GPIO, Esp32GPIO_Isr, kEsp32_IrqPriority_GPIO);

    // set the ISR and trigger in the interrupts tables
    s_Interrupts[nPin] = (Esp32GPIO_Interrupt) { trigger, pISR, pClientData };

    // keep the open drain config as-is
    u32 nRegVal = ESP32_REG_ARRAY_VALUE(GPIO_PIN0, nPin) & ESP32_REG_MASK(GPIO_PIN, PAD_DRIVER);

    // enable the interrupt on whichever core the source is routed to
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

    u32 nVal = 0;
    if (nPin < 32) {
        nVal = ESP32_GPIO_IS_OUTPUT(nPin) ? ESP32_REG(GPIO_OUT) :
                                            ESP32_REG(GPIO_IN);
    } else {
        nVal = ESP32_GPIO_IS_OUTPUT(nPin) ? ESP32_REG(GPIO_OUT1) :
                                            ESP32_REG(GPIO_IN1);
        nPin -= 32;
    }
    return ((nVal & (1u << nPin)) != 0);
}

/******************************************************************************
 * Write the given value to the pin (pin direction must be output)
 *****************************************************************************/
void Esp32GPIO_WritePin(u8 nPin, bool bVal) {
    if (!Esp32GPIO_IsValidPin(nPin)) {
        return;
    }

    if (ESP32_GPIO_IS_OUTPUT(nPin)) {
        if (nPin < 32) {
            bVal ? (ESP32_REG(GPIO_OUT_W1TS) = (1u << nPin)) :
                   (ESP32_REG(GPIO_OUT_W1TC) = (1u << nPin));
        } else {
            bVal ? (ESP32_REG(GPIO_OUT1_W1TS) = (1u << (nPin - 32))) :
                   (ESP32_REG(GPIO_OUT1_W1TC) = (1u << (nPin - 32)));
        }
    }
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  29-Jul-26   claudius    created
 */
