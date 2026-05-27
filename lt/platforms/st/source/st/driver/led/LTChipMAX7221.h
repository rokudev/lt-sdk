/*******************************************************************************
 * <platforms/st/source/st/driver/led/LTChipMAX7221.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Control of the Maxim MAX7221 seven-segment display driver IC.
 *
 * Support for communication with a Maxim MAX7221 seven-segment display driver
 * IC.  It includes data values, initialization and communication, and depends
 * on the calling LT Library to provide the actual means of communication.
 * Communication with the MAX7221 is via SPI; this can be an actual SPI port or
 * a set of three GPIO pins (SPI is normally four, but this interface does not
 * use MISO) which implement SPI behavior by bit-banging.  The configuration and
 * operation of the SPI port are typically platform-specific, and therefore the
 * calling library is typically <platform>DriverLED. In the LTDeviceLED
 * parlance, a display, made up of one or more digits (the MAX7221 will drive a
 * maximum of eight digits), and is operated in the Driver Library as a
 * Seven-Segment LED Group.
 *
 * The MAX7221 can drive a display consisting of as few as one and as many as
 * eight seven-segment digits in common-cathode configuration.  It supports
 * variable display intensity through a command sent over SPI, as well as a
 * current-determining resistor.  A full-on, all-segments test mode is
 * available, as is a low-power shutdown mode.  The IC accepts commands to form
 * numerals and some letters on the digits in Code B decode mode (see "Digit
 * Rendering" below), or to illuminate the segments in any arbitrary pattern if
 * Code B decode mode is disabled.
 *
 * The MAX7221 receives commands via SPI, using data (MOSI), clock (SCLK)
 * and chip select (CS).  Commands are sixteen-bit packets, MSB first.  CS must
 * be low to clock data into the IC.  Data on MOSI is clocked into the IC on
 * the rising edge of SCLK.  CS must go high concurrently with or following the
 * sixteenth rising edge of SCLK, and before the next rising edge of SCLK.
 * SCLK idles low.
 *
 * For more information, please see the MAX7219/MAX7221 datasheet.
 ******************************************************************************/

#ifndef PLATFORMS_ST_SOURCE_ST_DRIVER_LED_LTCHIPMAX7221_H
#define PLATFORMS_ST_SOURCE_ST_DRIVER_LED_LTCHIPMAX7221_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

/***************************************************************************************************
   Data structure for digit state.
   This data structure is not meant to be manipulated outside LTChipMAX7221Impl, but appears
   here so that the calling LT Library can allocate memory for the digit array however it wants.  */

/* Digit Rendering
   When Code B decode mode is enabled (bRenderAsDigit == true), nValue is used to render the digit
   as a numeral, 0 - 9 or characters '-', 'H', 'E', 'L'or 'P', with bit 7 set to illuminatethe
   decimal point.  A value of 0xFF blanks the digit.  Bits 6 through 4 are ignored:

              | bits 3-0 | Display |
              |----------|---------|
              |    0     |  '0'    |
              |    1     |  '1'    |
              |    2     |  '2'    |
              |    3     |  '3'    |
              |    4     |  '4'    |
              |    5     |  '5'    |
              |    6     |  '6'    |
              |    7     |  '7'    |
              |    8     |  '8'    |
              |    9     |  '9'    |
              |    A     |  '-'    |
              |    B     |  'E'    |
              |    C     |  'L'    |
              |    D     |  'H'    |
              |    F     |  (blank)|

   When Code B decode mode is disabled (bRenderAsDigit == false), each bit of nValue indicates
   the state of an individual segment of the digit (1: segment on, 0: segment off):

              | bit | segment       |
              |-----|---------------|
              |  7  | decimal point |
              |  6  | segment A     |
              |  5  | segment B     |
              |  4  | segment C     |
              |  3  | segment D     |
              |  2  | segment E     |
              |  1  | segment F     |
              |  0  | segment G     |

               Segment map:
                             A
                           F   B
                             G
                           E   C
                             D  .                                                                 */

typedef struct LTChipMAX7221Digit {
    /**< The state of one digit.  The caller is responsible for allocating (or otherwise bringing
         into existence an array of these, one per digit on the display.   */

    bool bRenderAsDigit;     /**< Manner of rendering of the digit in the display
                                  (see "Digit Rendering" above):
                                  @val true  render as digit Code B decode mode
                                  @val false render as bit pattern                                */
    bool bValueChanged;      /**< True if the digit has been changed with a Set* method since the
                                  the last update of the display                                  */
    bool bRenderModeChanged; /**< True if the render mode has been changed with a Set* method
                                  since the last update of the display                            */
    u8   nValue;             /**< The value of the digit                                          */
} LTChipMAX7221Digit;

/***************************************************************************************************
 * Data structure for display state                                                               */

/**< Platform-specific transmit function.
     @note Transmit the value MSB-first as one SPI transaction, in the normal SPI manner (CS low,
           clock the data, CS high).  The MAX7221 clocks data in on the rising edge of normally-low
           SCLK.
     @param word the value to transmit to the display over SPI (or GPIO bit-banging as SPI).      */
typedef void (MAX7221TransmitProc)(u16 word);

typedef struct LTChipMAX7221DisplayState {
    /**< The state of one display driven by a MAX7221.

       @note STORAGE FOR THIS STRUCTURE IS MANAGED BY THE CALLING LT LIBRARY.
       @note Every function of the LTChipMAX7221 interface takes a pointer to this struct
             as its first argument.
       @note The calling Library must (in this order) allocate the digit array, assign values to
             .pDigits, .pTransmitProc, and .nNumDigits, and call Initialize() (which initializes
             the driver IC and this structure) before performing any other operations on the
             display. */

    LTChipMAX7221Digit  * pDigits;         /**< Pointer to the digit array     */
    MAX7221TransmitProc * pTransmitProc;   /**< Function to call to transmit to the MAX7221 */
    u32                   nColor;          /**< IIRRGGBB data for the display (only intensity
                                                is used). */
    u8                    nNumDigits;      /**< How many digits in the display. */
    u8                    nDecodeMode;     /**< MAX7221 decode mode - one bit for each digit,
                                                bit 0 as rightmost digit.
                                                0 = No decode - eight-bit value corresponds
                                                    one-for-one with seven segments plus decimal
                                                    point (see LTDeviceLED.h).
                                                1 = Code B decode mode - value is numeric,
                                                    translated to a numeral on the display. */
} LTChipMAX7221DisplayState;

/***************************************************************************************************
 * Support functions                                                                              */

bool LTChipMAX7221_Initialize(LTChipMAX7221DisplayState * pDisplayState, bool bInitializeDisplay);
    /**< Initializes the display state and the MAX7221 driving the display.
         @param pDisplayState pointer to the display state struct.
         @param bInitializeDisplay true to put the display driver IC into a known state,
                                   false to skip initialization of the IC (useful if the
                                   display was "left on" previously).
         @note %LTChipMAX7221_Initialize() must be called before any other function in the
               LTChipMAX7221 interface.
         @note %pDisplayState->pDigits, %pDisplayState->pTransmiptProc, and
               %pDisplayState->nNumDigits must be initialized before calling this
               %LTChipMAX7221_Initialize()
         @return true if the display and display state structure were successfully initialized,
                 and are ready for use, false otherwise.  Failure to initialize is typically
                 caused by bad arguments; zeroes for pointer values in the display state struct,
                 or out-of-range values for nNumDigits ensures failure. */

void LTChipMAX7221_Clear(LTChipMAX7221DisplayState * pDisplayState);
    /**< Clears the display.  All segments off.
         @param pDisplayState pointer to the display state struct. */

void LTChipMAX7221_SetDigit(LTChipMAX7221DisplayState * pDisplayState,
                            u32 nDigitIndex, u8 nDigitValue, bool bSetDecimalPoint);
    /**< Sets one digit in the display with a numeric value.  Optionally set the decimal point.
         @param pDisplayState pointer to the display state struct.
         @param nDigitIndex - the index of the digit to set (0 is leftmost).
         @param nDigitValue - the value for the digit, as per "Digit Rendering" above.
         @param bSetDecimalPoint - will illuminate decimal point if true, clear illimunation of decimal point if false.
         @note  passing in 0xFF for nDigitValue will clear the digit */

void LTChipMAX7221_SetDigits(LTChipMAX7221DisplayState * pDisplayState,
                             u32 nDigitIndex, u32 nCount, char const * pDigitString);
    /**< Sets nCount digits starting at nDigitIndex with encoding in pDigitString
         @param pDisplayState pointer to the display state struct.
         @param nDigitIndex the index of the first digit to set from the string.
         @param nCount how many digits to set.  Will be clipped so as to not run past the last digit.
         @param pDigitString a string of length exactly nCount * 2 where the string
                contains the pattern "N." for nCountDigits where N is
                one of ascii characters '0','1','2','3','4','5','6','7','8','9','A','a','B','b','C','c','D','d','E','e','F','f'
                which will render 0,1,2,3,4,5,6,7,8,9,AbCdEF on the digit and following each digit's numeric specifier is either
                '.' or ' ' (space) to indicate illimunation or abscence of illimination of the decimal point. */

void LTChipMAX7221_SetBitPattern(LTChipMAX7221DisplayState * pDisplayState,
                                 u32 nDigitIndex, u8 bitPattern);
    /**< Sets an arbitrary segment pattern in a digit
         @param pDisplayState pointer to the display state struct.
         @param nDigitIndex the index of the digit to set.
         @param bitPattern the bit pattern for the digit.
                        Bit pattern (1: segment on, 0: segment off)
                          bit 7: decimal point
                          bit 6: segment A          Segment map:
                          bit 5: segment B                        A
                          bit 4: segment C                      F   B
                          bit 3: segment D                        G
                          bit 2: segment E                      E   C
                          bit 1: segment F                        D  .
                          bit 0: segment G */

void LTChipMAX7221_SetBitPatterns(LTChipMAX7221DisplayState * pDisplayState,
                                  u32 nDigitIndex, u32 nCount, u8 * pBitPatterns);
    /**< Sets individual segment patterns of multiple digits.
         @param pDisplayState pointer to the display state struct.
         @param nDigitIndex the index of the first digit to change.
         @param nCount the number of elements to change.
         @param pBitPatterns pointer to nCount bit patterns, one u8 for each element.
                        Bit pattern (1: segment on, 0: segment off)
                          bit 7: decimal point
                          bit 6: segment A          Segment map:
                          bit 5: segment B                        A
                          bit 4: segment C                      F   B
                          bit 3: segment D                        G
                          bit 2: segment E                      E   C
                          bit 1: segment F                        D  .
                          bit 0: segment G */

void LTChipMAX7221_SetLowPowerMode(LTChipMAX7221DisplayState * pDisplayState,
                                   bool bLowPowerMode);
    /**< Puts the display in low-power mode (if supported) or normal mode.
         @param pDisplayState pointer to the display state struct.
         @bLowPowerMode true to put the device into low-power mode, false for normal mode. */

void LTChipMAX7221_SetDisplayTestMode(LTChipMAX7221DisplayState * pDisplayState,
                                      bool bDisplayTestMode);
    /**< Puts the display in display-test mode (if supported) or normal mode.
         @param pDisplayState pointer to the display state struct.
         @bLowPowerMode true to put the device into display-test mode, false for normal mode. */

void LTChipMAX7221_SetColor(LTChipMAX7221DisplayState * pDisplayState,
                            u32 nColor);
    /**< Sets the color of the display.
         @param pDisplayState pointer to the display state struct.
         @param nColor IIRRGGBB data for the display.  Only the upper four bits of intensity are used. */

LT_EXTERN_C_END
#endif // #ifndef PLATFORMS_ST_SOURCE_ST_DRIVER_LED_LTCHIPMAX7221_H
/******************************************************************************
 *  LOG
 ******************************************************************************
 *  23-Mar-21   constantine created
 */
