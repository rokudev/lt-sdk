/***************************************************************************************************
 * platforms/tipton/source/tipton/driver/led/LTChipMAX7221Impl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Control of the Maxim MAX7221 seven-segment display driver IC.
 * Typically driven by <platform>DriverLED to control numeric LED displays.
 **************************************************************************************************/

#include <lt/core/LTCore.h>
#include "LTChipMAX7221.h"

/***************************************************************************************************
   MAX7221 Display Driver register addresses.  Other valid register address values are 0x01
   through 0x08, for digits 0 through 7, respectively.                                            */
typedef u8 MAX7221_register_address;

typedef enum {
    kMAX7221_No_Op                 = 0x00,
    kMAX7221_Register_Decode_Mode  = 0x09,
    kMAX7221_Register_Intensity    = 0x0A,
    kMAX7221_Register_Scan_Limit   = 0x0B,
    kMAX7221_Register_Shutdown     = 0x0C,
    kMAX7221_Register_Display_Test = 0x0F
} MAX7221_register_address_value;

/***************************************************************************************************
   Display manipulation functions.

   NOTE: All these functions have the precondition that pDisplayState and pDisplayState->pDigits
   are valid.                                                                                     */

/* Assemble the 16-bit command for the IC.  Transmit it through the Driver: */
static void Transmit(LTChipMAX7221DisplayState * pDisplayState,
                     MAX7221_register_address address, u8 operand) {
    u16 data = address;
    data <<= 8;
    data |= operand;
    (*pDisplayState->pTransmitProc)(data, pDisplayState->hSPIUnit);
}

static void UpdateDisplayIntensity(LTChipMAX7221DisplayState * pDisplayState) {
    /* The MAX7221 only uses the four most significant intensity bits (which reside
       in the uppermost 4 bits of nColor). */
    Transmit(pDisplayState, kMAX7221_Register_Intensity, ((pDisplayState->nColor >> 28) & 0x000F));
}

/* When one of the Set...() interface functions is called, it changes the display state accordingly.
   Gather up the display state and transmit commands to the driver IC to update the display: */
static void UpdateDisplayValues(LTChipMAX7221DisplayState * pDisplayState) {
    LTChipMAX7221Digit * pDigit = (LTChipMAX7221Digit *)pDisplayState->pDigits;
    for (u32 n = 0; n < pDisplayState->nNumDigits; ++n, ++pDigit) {
        /* In the MAX7221, digits are numbered from 0, right to left, which is opposite
           the digit instances in the Group. */
        u16 nDigitNumber = pDisplayState->nNumDigits - 1 - n;
        if (pDigit->bRenderModeChanged) {
            /* The render mode has changed.
               Render mode is expressed to the display driver as a byte, with one bit
               indicating the render mode for each digit.  Update the byte according
               to the render mode for this digit, and transmit it to the display driver: */
            u8 renderBit = 1 << nDigitNumber;
            if (pDigit->bRenderAsDigit)
                pDisplayState->nDecodeMode |= renderBit;
            else
                pDisplayState->nDecodeMode &= ~renderBit;
            Transmit(pDisplayState, kMAX7221_Register_Decode_Mode, pDisplayState->nDecodeMode);
            /* If the render mode has changed, the value needs to be updated in the
               display driver as well: */
            pDigit->bValueChanged = true;
        }
        if (pDigit->bValueChanged) {
            /* The digit value has changed - transmit it to the display driver: */
            Transmit(pDisplayState, nDigitNumber + 1, pDigit->nValue);
        }
        pDigit->bRenderModeChanged = pDigit->bValueChanged = false;
    }
}

/* Set a new value and render mode, according to a Set...() interface function call: */
static void SetDigit(LTChipMAX7221Digit * pDigit, u8 nValue, bool bRenderAsDigit) {
    if ((pDigit->bValueChanged = pDigit->nValue != nValue))
        pDigit->nValue = nValue;
    if ((pDigit->bRenderModeChanged = pDigit->bRenderAsDigit != bRenderAsDigit))
        pDigit->bRenderAsDigit = bRenderAsDigit;
}

static void ClearDisplay(LTChipMAX7221DisplayState * pDisplayState) {
    LTChipMAX7221Digit * pDigit = pDisplayState->pDigits;
    for (int n = pDisplayState->nNumDigits; n--; ++pDigit) {
        pDigit->bRenderAsDigit = pDigit->bValueChanged = pDigit->bRenderModeChanged = true;
        pDigit->nValue = 0x0F;
    }
}

/* Ensure that the count does not run a loop past the end of the digit instance array: */
static void ClipCount(LTChipMAX7221DisplayState * pDisplayState,
                      u32 nDigitIndex, u32 * nCount) {
    u32 nMax = nDigitIndex + *nCount;
    if (nMax > pDisplayState->nNumDigits)
        *nCount -= nMax - pDisplayState->nNumDigits;
}

/* Return a pointer to the digit instance corresponding to nDigitIndex.
   Return NULL if nDigitIndex is out of range: */
static LTChipMAX7221Digit * ThisDigit(LTChipMAX7221DisplayState * pDisplayState,
                                            u32 nDigitIndex) {
    return nDigitIndex < pDisplayState->nNumDigits ? pDisplayState->pDigits + nDigitIndex : NULL;
}

/***************************************************************************************************
   Interface functions.                                                                           */

bool LTChipMAX7221_Initialize(LTChipMAX7221DisplayState * pDisplayState, bool bInitializeDisplay) {
    if (!pDisplayState)
        return false;
    if (   !pDisplayState->pDigits         || !pDisplayState->pTransmitProc
        ||  pDisplayState->nNumDigits == 0 ||  pDisplayState->nNumDigits > 8)
        return false;
    if (bInitializeDisplay) {
        /* Normal mode; limit scan to number of digits; Code B enabled for all bits;
           low-power (shutdown) mode enabled; half intensity: */
        Transmit(pDisplayState, kMAX7221_Register_Display_Test, 0);
        Transmit(pDisplayState, kMAX7221_Register_Shutdown,     0);
        Transmit(pDisplayState, kMAX7221_Register_Scan_Limit,   pDisplayState->nNumDigits - 1);
        Transmit(pDisplayState, kMAX7221_Register_Decode_Mode,  pDisplayState->nDecodeMode);
        pDisplayState->nColor = 0x80000000;
        UpdateDisplayIntensity(pDisplayState);
        pDisplayState->nDecodeMode = 0xFF;
        ClearDisplay(pDisplayState);
        UpdateDisplayValues(pDisplayState);
    }
    return true;
}

void LTChipMAX7221_Clear(LTChipMAX7221DisplayState * pDisplayState) {
    if (!pDisplayState) return;
    ClearDisplay(pDisplayState);
    UpdateDisplayValues(pDisplayState);
}

void LTChipMAX7221_SetDigit(LTChipMAX7221DisplayState * pDisplayState,
                            u32 nDigitIndex,
                            u8 nDigitValue,
                            bool bSetDecimalPoint) {
    LTChipMAX7221Digit * pDigit = ThisDigit(pDisplayState, nDigitIndex);
    if (!pDigit) return;
    if (nDigitValue == 0xFF) nDigitValue = 0x7F; /* allow bSetDecimalPoint to set bit 7 */
    SetDigit(pDigit, nDigitValue | (bSetDecimalPoint ? 0x80 : 0), true);
    UpdateDisplayValues(pDisplayState);
}

void LTChipMAX7221_SetDigits(LTChipMAX7221DisplayState * pDisplayState,
                             u32 nDigitIndex,
                             u32 nCount,
                             char const * pDigitString) {
    LTChipMAX7221Digit * pDigit = ThisDigit(pDisplayState, nDigitIndex);
    if (!pDigit) return;
    ClipCount(pDisplayState, nDigitIndex, &nCount);
    for (; nCount; --nCount, ++pDigit) {
        char digit            = *pDigitString++;
        u8   value            = 0x0F;    /* blank */
        bool bRenderAsDigit   = true;
        switch (digit) {
        case '0': case '1': case '2':  case '3': case '4':
        case '5': case '6': case '7':  case '8': case '9': value = digit - '0'; break;
        case '-':                                          value = 0x0A;        break;
        case 'H': case 'h':                                value = 0x0C;        break;
        case 'L': case 'l':                                value = 0x0D;        break;
        case 'P': case 'p':                                value = 0x0E;        break;
        case 'A': case 'a':        bRenderAsDigit = false; value = 0x77;        break;
        case 'B': case 'b':        bRenderAsDigit = false; value = 0x1F;        break;
        case 'C': case 'c':        bRenderAsDigit = false; value = 0x4E;        break;
        case 'D': case 'd':        bRenderAsDigit = false; value = 0x3D;        break;
        case 'E': case 'e':                                value = 0x0B;        break;
        case 'F': case 'f':        bRenderAsDigit = false; value = 0x47;        break;
        default:                   /* leave it blank */                         break;
        };
        if (*pDigitString++ == '.') value |= 0x80;
        SetDigit(pDigit, value, bRenderAsDigit);
    }
    UpdateDisplayValues(pDisplayState);
}

void LTChipMAX7221_SetBitPattern(LTChipMAX7221DisplayState * pDisplayState,
                                 u32 nDigitIndex,
                                 u8 bitPattern) {
    LTChipMAX7221Digit * pDigit = ThisDigit(pDisplayState, nDigitIndex);
    if (!pDigit) return;
    SetDigit(pDigit, bitPattern, false);
    UpdateDisplayValues(pDisplayState);
}

void LTChipMAX7221_SetBitPatterns(LTChipMAX7221DisplayState * pDisplayState,
                                  u32 nDigitIndex,
                                  u32 nCount,
                                  u8 * pBitPatterns) {
    LTChipMAX7221Digit * pDigit = ThisDigit(pDisplayState, nDigitIndex);
    if (!pDigit) return;
    ClipCount(pDisplayState, nDigitIndex, &nCount);
    for (; nCount; --nCount, ++pDigit, ++pBitPatterns)
        SetDigit(pDigit, *pBitPatterns, false);
    UpdateDisplayValues(pDisplayState);
}

void LTChipMAX7221_SetLowPowerMode(LTChipMAX7221DisplayState * pDisplayState,
                                   bool bLowPowerMode) {
    Transmit(pDisplayState, kMAX7221_Register_Shutdown, bLowPowerMode ? 0 : 1);
}

void LTChipMAX7221_SetDisplayTestMode(LTChipMAX7221DisplayState * pDisplayState,
                                      bool bDisplayTestMode) {
    Transmit(pDisplayState, kMAX7221_Register_Display_Test, bDisplayTestMode ? 1 : 0);
}

void LTChipMAX7221_SetColor(LTChipMAX7221DisplayState * pDisplayState,
                            u32 nColor) {
    pDisplayState->nColor = nColor;
    UpdateDisplayIntensity(pDisplayState);
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  23-Mar-21   constantine created
 *  03-May-21   augustus    allow decimal point to be cleared when blanking digit
 */
