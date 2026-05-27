/*******************************************************************************
 * <lt/device/led/LTDeviceLED.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltdevice_led LTDeviceLED
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for controlling collections of LEDs.
 *
 * This library organizes collections of one or more LEDs of various types into "groups"
 * which are represented, and operated on, by device unit handles which are
 * supplied by this library.  Each LED in a group shares the same basic method
 * of operation and the unit handles representing groups supply attached interfaces
 * that are purpose-built for user-friendly operation of the LEDs in the group.
 *
 * There are two types of LED's:
 * 1. Indicator Lamp
 *    This type of LED is a light for indicating various information such as I/O
 * 2. Seven-segment digital LED
 *    This type of LED is a display that shows information using numbers and letters
 *
 */

#ifndef LT_INCLUDE_LT_DEVICE_LED_LTDEVICELED_H
#define LT_INCLUDE_LT_DEVICE_LED_LTDEVICELED_H

#include <lt/LTTypes.h>
#include <lt/LTObject.h>
#include <lt/core/LTTime.h>
LT_EXTERN_C_BEGIN

/*******************************
*******************************
* LTDeviceLED_GroupType enum */
#ifndef DOXY_SKIP
typedef u32 LTDeviceLED_GroupType;                      /**< Numeric type of LTDEviceLED_GroupType  The type of the enum values is u32 */
#endif

/** LED Group Type enumeration */
typedef enum LTDeviceLED_GroupType {
    kLTDeviceLED_GroupType_IndicatorLamp = 0,           /**< A grouping of 1 or more traditional LED lamps. */
    kLTDeviceLED_GroupType_SevenSegmentDigit            /**< A grouping of one or more traditional LED seven-segment digits. */
} LEDGroupType;

/**
* A struct describing a group.
* Group descriptors may be retrieved and examined prior to creating unit handles.
*/
typedef struct LTDeviceLED_GroupDescriptor {
    const char                *m_pGroupName;        /**< The name of the Group. */
    LEDGroupType               m_groupType;         /**< The type of the Group. */
    u32                        m_nNumElements;      /**< The number of elements in the Group. */
    u32                        m_nDeviceUnitNumber; /**< The Device Unit number for use with CreateDeviceUnitHandle */
} LTDeviceLED_GroupDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceLED_GroupDescriptor, 16, 24);

typedef struct LTDeviceLEDFaderTimingStep {
    /**< one step in a fade sequence
     *   A fade sequence consists of an array of one or more of this structure, each comprising one step in the sequence.
     *   The Fader starts at the current color (typically the final color state expressed by %color, of the previous step),
     *   and ramps all four components of the color (I,R,G,B) to the values given in %color of the current step over the
     *   time given in %transition, then remains at %color for %dwell time before moving on to the next step.
     *   %dwell and %transition are expressed in milliseconds.
     *   A value of %dwell of 0 indicates that the sequence stops at this step and the LED remains at this color indefinitely.
     *
     *   Examples:
     *   - Set LED to green immediately, no change: { { 0xFF00FF00, 0, 0 } }
     *   - Alternate between red and blue, 500ms each, indefinitely: { { 0xFFFF0000, 500, 500 }, { 0xFF0000FF, 500, 500 } }
     *   - Force off immediately to start, fade up to red over 250ms, hold for 500ms, fade to white over 500ms, hold for 1000ms, fade to off over 250ms, repeat:
     *     { { 0x00000000, 0, 0 }, { 0xC0FF0000, 250, 500 }, { 0x80FFFFFF, 500, 1000 }, { 0x00000000, 250, 0 } }
     */
    u32 color;                  /***<  the color (IIRRGGBB) of the LED at the end of the step                                     */
    u16 transition;             /***<  how long (ms) it will take for the Fader to ramp from the previous color to this one.
                                       If zero, transition from the previous color to this color is instantanenous.               */
    u16 dwell;                  /***<  how long (ms) the Fader will stay at this color before this step ends and the next begins.
                                       If zero, the sequence stops and the LED maintains this color indefinitely.
                                       If nonzero, the sequence repeats from the beginning at the end of the dwell time of the
                                       last step.                                                                                 */
} LTDeviceLEDFaderTimingStep;

#define LTDeviceLEDFaderTimingStep_Count(stepListName) (sizeof(stepListName) / sizeof(LTDeviceLEDFaderTimingStep))

typedef void (LTDeviceLEDFader_FadeCompleteProc)(u32 nColor, bool bReached, void * pClientData);
    /**< Called when a single fade (through %Fade()) has completed
      *  param[in] nColor the final color at the end of the fade
      *  param[in] bReached true if the intended final color was reached, false if something prevented the fade from completing
      *  param[in] pClientData client data pointer
      */

typedef void (LTDeviceLEDFader_FadeSequenceStepCompleteProc)(const LTDeviceLEDFaderTimingStep * pStep, bool bReached, void * pClientData);
    /**< Called when a fade sequence step (through %StartFadeSequence()) completes
      *  param[in] pStep pointer to the sequence step which just completed
      *  param[in] bReached true if the intended final color was reached, false if something prevented the fade from completing
      *  param[in] pClientData client data pointer
      */

typedef void (LTDeviceLEDFader_FadeSequenceCompleteProc)(const LTDeviceLEDFaderTimingStep * pSequence, bool bComplete, void * pClientData);
    /**< Called when a fade sequence (through %StartFadeSequence()) completes
      *  For repeating sequences, this proc runs immediately before the sequence restarts, every time it restarts.
      *  For non-repeating sequences, this proc runs once at the end of the sequence.
      *  param[in] pSequence pointer to the beginning of the sequence which just completed
      *  param[in] bReached true if the sequence reached the end successfully, false something prevented the sequence from completing
      *  param[in] pClientData client data pointer
      */

typedef_LTObject(LTDeviceLEDFader, 1) {
    /**< Control smooth (or instantaneous) transitions in color of an IndicatorLamp LED
      *
      * @note This may eventually be extended to other types of LEDs (e.g., Seven-Segment LED), once
      *       the Driver interface is updated to LTObjects.
      *
      * A Fader allows smooth transitions in color or brightness, affording the specification of
      * behavior patterns, such as pulsation or color rotation.  The Fader will gradually change
      * the color of the LED from one state to another and stop, or will iterate through a list
      * of transitions in a sequence to produce complex color-change patterns or simple pulsating
      * or blinking patterns.  Patterns are expressed as an array of structs carrying color,
      * transition time (the time required to ramp from one color state to another), and dwell
      * time (the time the Fader remains at the final color before beginning the next transition).
      *
      * @see LTDeviceLEDFaderTimingStep
      */

    bool (* Initialize)(LTDeviceLEDFader * pThis, char const * pGroupName, u32 nLEDIndex, LTOThread * pOThread, LTTime interval);
        /**< Initialize a Fader
          *  Attempts to obtain a Device Unit Handle (later an LTObject) for the LED Group, and verifies that an LED
          *  by the given index exists.  If so, initializes the LTDeviceLEDFader LTObject through which the client
          *  accesses and instructs the Fader.  The client then accesses the LED through the Fader.  Direct access
          *  through a handle to the LED Group is still permissible, if the client obtains its own handle.
          *
          *  The Fader will run along the %LTThread referred to by %pOThread, waking up every intervalTime
          *  to update the color of the LED it is controlling.  Color increments are calculated as the
          *  total time to fade from one color to the next, divided by the interval time.
          *
          *  @note if %pOThread is NULL, the Fader will create its own thread along which to run.
          *
          *  @note Upon destruction, all fading activity stops instantly, and the LED remains at the its current color.
          *
          *  @param[in]  pThis pointer to the Fader object
          *  @param[in]  pGroupName The name of the LED Group to retrieve the unit number for.
          *  @param[in]  nLEDIndex the index of the Indicator Lamp in the Group (0 if only one exists in the Group)
          *  @param[in]  pOThread pointer to the thread along which the Fader will run (NULL if %Initialize() should
          *              create a new thread for the Fader)
          *  @param[in]  interval how often the Fader will wake to update the color of the LED
          *  @return     true upon successful initialization, false otherwise.
          */

    void (* Fade)(LTDeviceLEDFader * pThis, u32 nColor, LTTime transition, LTDeviceLEDFader_FadeCompleteProc * pFadeCompleteProc, void * pClientData);
        /**< Fade the LED from its current color to another color (execute a "single fade")
          *
          *  Color is expressed as IIRRGGBB.  All four components are ramped individidually from
          *  their initial state to the state given in %nColor.  The ramp will take %transition milliseconds
          *  to complete, with each component incrementing appropriately to smoothly ramp between its initial
          *  value and that of %nColor every %intervalTime (see %InitializeFader).
          *
          *  @note If the Fader is already fading or running a fade sequence, the Fader immediately restarts
          *        with this fade, fading from the current color to %nColor.
          *
          *  param[in]  pThis pointer to the Fader object
          *  param[in]  nColor the intended color state at the end of the fade
          *  param[in]  transition the total duration of the fade
          *  param[in]  pFadeCompleteProc function to call upon completion of the fade
          *  param[in]  pClientData client data pointer for pFadeCompleteProc
          */

    void (* StartFadeSequence)(LTDeviceLEDFader * pThis, const LTDeviceLEDFaderTimingStep * pStepList, u16 nTimingStepCount, LTDeviceLEDFader_FadeSequenceStepCompleteProc * pStepCompleteProc, LTDeviceLEDFader_FadeSequenceCompleteProc * pSequenceCompleteProc, void * pClientData);
        /**< Begin execution of a sequence of Fader timing steps
          *  The Fader will execute a list of fade steps beginning at %pStepList.  A step in the step list
          *  expresses the ending color of the fade step, how long the fade takes (the "transition"), and
          *  how long the Fader dwells at the ending color before the step ends and the next step begins.
          *
          *  @note If the Fader is already fading or running a fade sequence, the Fader immediately restarts
          *        with this step list.  If an initial starting color is crucial, the step list should specify
          *        that initial color with a transition time of 0 to immediately force the initial color state.
          *
          *  @note LTDeviceLEDFader does NOT copy the step list; the step list referenced by pStepList MUST persist
          *        in memory for the entire time that the sequence runs.
          *
          *  param[in]  pThis pointer to the Fader object
          *  param[in]  pStepList pointer to the list of steps (must persist in memory while the sequence is running)
          *  param[in]  nTimingStepCount the number of steps in the list
          *  param[in]  pStepCompleteProc function to call upon completion of each step in the fade sequence
          *  param[in]  pSequenceCompleteProc function to call upon completion of one iteration of the sequence
          *  param[in]  pClientData client data pointer to pass to the completion procs
          */
} LTOBJECT_API;

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceLED, 1);

/**
 * @brief The API for controlling LED devices.
 */
struct LTDeviceLED {

    INHERIT_DEVICE_LIBRARY_BASE

    bool (* GetGroupDescriptorFromUnitNumber)(u32 nDeviceUnitNumber, LTDeviceLED_GroupDescriptor * pDescriptor);
        /**< Obtains a descriptor for an LED Group.
         *   @param[in] nDeviceUnitNumber The Device Unit index.  Invalid values (>= the return value
         *          of GetNumDeviceUnits()) cause GetDeviceUnitDescriptor() to leave *pDescriptor
         *          untouched and return @c false.
         *   @param[out] pDescriptor The address of the descriptor to fill.
         *   @return true if *pDescriptor was written, false if not. */

    bool (* GetUnitNumberFromGroupName)(char const * pGroupName, u32 * pDeviceUnitNumberToSet);
        /**< Obtains the Device Unit number of a named LED Group.
         *   @param[in]  pGroupName The name of the LED Group to retrieve the unit number for.
         *   @param[out] pDeviceUnitNumberToSet Pointer to receive the Device Unit number.
         *   @return true and set *pDeviceUnitNumberToSet if an LED Group by the given name exists, false otherwise. */
};

#ifndef DOXY_SKIP // [
/* LTDriverLED Interface - this interface is to be used only by LTDeviceLED. */
typedef_LTLIBRARY_INTERFACE(ILTDriverLED, 1) {
    bool (* GetGroupDescriptorFromUnitNumber)(u32 nDeviceUnitNumber, LTDeviceLED_GroupDescriptor * pDescriptor);
        /**< Obtains a descriptor for an LED Group.
         *   @param[in] nDeviceUnitNumber The Device Unit index.  Invalid values (>= the return value
         *          of GetNumDeviceUnits()) cause GetDeviceUnitDescriptor() to leave *pDescriptor
         *          untouched and return false.
         *   @param[out] pDescriptor The address of the descriptor to fill.
         *   @return true if *pDescriptor was written, false if not. */

    bool (* GetUnitNumberFromGroupName)(char const * pGroupName, u32 * pDeviceUnitNumberToSet);
        /**< Obtains the Device Unit number of a named LED Group.
         *   @param[in] pGroupName The name of the LED Group to retrieve the unit number for.
         *   @param[out] pDeviceUnitNumberToSet Pointer to u32 to hold the Device Unit number.
         *   @return true and set *pDeviceUnitNumberToSet if an LED Group by the given name exists, false otherwise. */
} LTLIBRARY_INTERFACE;
#endif  // DOXY_SKIP ]

TYPEDEF_LTLIBRARY_INTERFACE(ILTDriverLED_GroupType_IndicatorLamp, 1);

/** LT Device Unit Handle interface for the IndicatorLamp Group Type. */
struct ILTDriverLED_GroupType_IndicatorLampApi {

    INHERIT_INTERFACE_BASE

    u32 (* GetLEDColor)(LTDeviceUnit hGroup, u32 nLEDIndex);
        /**< Reports the currently displaying color of the LED element at index nLEDIndex in IIRRGGBB, where I,R,G,B is Intensity,Red,Green,Blue.
         *   @note This works on all forms of traditional LED indicator lamps, even BLUE, WHITE and bi-state/tri-state LEDs.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @param[in] nLEDIndex The Index of the LED element.
         *   @note For on/off LED's, any non-zero intensity value will be full-blast on.
         *   @return the currently displaying color of the LED element at index nLEDIndex in IIRRGGBB, where I,R,G,B is Intensity,Red,Green,Blue. */

    void (* GetLEDColors)(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nCount, u32 * pBufferToFill);
        /**< Fills pBufferToFill with the currently displaying buffer values of nCount LED'S starting at index nLEDIndex.
         *   @note This works on all forms of traditional LED indicator lamps, even BLUE, WHITE and bi-state/tri-state LEDs.
         *   If nLEDIndex and nLEDIndex + nCount are out of range or if pBufferToFill is null, this function fails silently.
         *   @note pBufferToFill must be u32-aligned and be large enough to hold nCount u32 values or crashing will result.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @param[in] nLEDIndex The starting index of the LED elements.
         *   @param[in] nCount The number of LED elements for which to retrieve colors.
         *   @param[out] pBufferToFill Pointer to destination array for colors. */

    void (* SetLEDColor)(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nColor);
        /**< Sets the color of the LED element at index nLEDIndex in IIRRGGBB, where I,R,G,B is Intensity,Red,Green,Blue.
         *   This works on all forms of traditional LED indicator lamps, even BLUE, WHITE and bi-state/tri-state LEDs.
         *   @note For on/off LED's any non zero intensity value will be full blast on.
         *   @note If nLEDIndex is out of range, this function fails silently.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @param[in] nLEDIndex The index of the LED element.
         *   @param[in] nColor The color for the LED element. */

    void (* SetLEDColors)(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nCount, u32 * pColors);
        /**< Sets the color of nCount LEDs starting at index nLEDIndex.  If the parameters are invalid or out of bounds this function fails silently.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @param[in] nLEDIndex The starting index of the LED elements.
         *   @param[in] nCount The number of LED elements for which to set colors.
         *   @param[in] pColors Pointer to array of colors for the LED elements. */

    void (* MaskLEDColor)(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nMask, u32 nColor);
        /**< Like SetLEDColor but only sets the bits in the LED's color that are set in nMask.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @param[in] nLEDIndex The index of the LED element.
         *   @param[in] nMask The color mask.
         *   @param[in] nColor The color for the LED element. */

    void (* MaskLEDColors)(LTDeviceUnit hGroup, u32 nLEDIndex, u32 nCount, u32 nMask, u32 * pColors);
        /**< Like SetLEDColors but only sets the bits in the LEDs' colors that are set in nMask.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @param[in] nLEDIndex The starting index of the LED elements.
         *   @param[in] nCount The number of LED elements for which to set colors.
         *   @param[in] nMask The color mask.
         *   @param[in] pColors Pointer to array of colors for the LED elements. */

    u32 (* GetNumberOfElements)(LTDeviceUnit hGroup);
        /**< Reports the number of elements available in the Group.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @return the number of elements available in the Group. */
};

TYPEDEF_LTLIBRARY_INTERFACE(ILTDriverLED_GroupType_SevenSegmentDigit, 1);

/** LT Device Unit Handle interface for the Seven-Segment Digit Group Type.
 *  Where a digit index appears, this refers to the position of the digit in the display,
 *  with 0 being the leftmost digit, increasing by one for every digit to the right. */
struct ILTDriverLED_GroupType_SevenSegmentDigitApi {

    INHERIT_INTERFACE_BASE

    void (* Clear)(LTDeviceUnit hGroup);
        /**< Clears the display - all segments off.
         *   @param[in] hGroup The Device Unit Handle of LED Group. */

    void (* SetDigit)(LTDeviceUnit hGroup, u32 nDigitIndex, u8 nDigitValue, bool bSetDecimalPoint);
        /**< Sets the digit at nDigitIndex with nDigitValue, optionally setting the decimal point.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @param[in] nDigitIndex The index of the 7 segment numeric display to set the digit on.
         *   @param[in] nDigitValue A value from 0-15 which will set one of 0,1,2,3,4,5,6,7,8,9,AbCdEF respectively.
         *   @param[in] bSetDecimalPoint Illuminates the decimal point if true, clear illumination of decimal point if false.
         *   @note Passing in 0xFF for nDigitValue will clear the digit. */
    void (* SetDigits)(LTDeviceUnit hGroup, u32 nDigitIndex, u32 nCount, const char * pDigitString);
        /**< Sets nCount digits starting at nDigitIndex with the encoding in pDigitString.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @param[in] pDigitString A string with the exactly length of nCount * 2 where the string
         *              contains the pattern "N." for nCountDigits where N is
         *              one of ascii characters '0','1','2','3','4','5','6','7','8','9','A','a','B','b','C','c','D','d','E','e','F','f'
         *              which will render 0,1,2,3,4,5,6,7,8,9,AbCdEF on the digit and following each digit's numeric specifier is either
         *              '.' or ' ' (space) to indicate illumination or absence of illumination of the decimal point. */

    void (* SetSegmentBitPattern)(LTDeviceUnit hGroup, u32 nDigitIndex, u8 bitPattern);
        /**< Sets individual segment patterns of a digit.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @param[in] nDigitIndex The index of the element to change.
         *   @param[in] bitPattern The bit pattern for the digit.
         *                  Bit pattern (1: segment on, 0: segment off)
         *                    bit 7: decimal point
         *                    bit 6: segment A          Segment map:
         *                    bit 5: segment B                        A
         *                    bit 4: segment C                      F   B
         *                    bit 3: segment D                        G
         *                    bit 2: segment E                      E   C
         *                    bit 1: segment F                        D  .
         *                    bit 0: segment G */

    void (* SetSegmentBitPatterns)(LTDeviceUnit hGroup, u32 nDigitIndex, u32 nCount, u8 * pBitPatterns);
        /**< Sets individual segment patterns of multiple digits.
         *   @param[in] nDigitIndex The index of the first digit to change.
         *   @param[in] nCount The number of elements to change.
         *   @param[in] pBitPatterns Pointer to nCount bit patterns, one u8 for each element.
         *                  Bit pattern (1: segment on, 0: segment off)
         *                    bit 7: decimal point
         *                    bit 6: segment A          Segment map:
         *                    bit 5: segment B                        A
         *                    bit 4: segment C                      F   B
         *                    bit 3: segment D                        G
         *                    bit 2: segment E                      E   C
         *                    bit 1: segment F                        D  .
         *                    bit 0: segment G */

    void (* SetLowPowerMode)(LTDeviceUnit hGroup, bool bLowPowerMode);
        /**< Puts the display in low-power mode (if supported) or normal mode.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @param[in] bLowPowerMode True to put the device into low-power mode, false for normal mode. */

    void (* SetDisplayTestMode)(LTDeviceUnit hGroup, bool bDisplayTestMode);
        /**< Puts the display in display-test mode (if supported) or normal mode.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @param[in] bLowPowerMode True to put the device into display-test mode, false for normal mode. */

    void (* SetColor)(LTDeviceUnit hGroup, u32 nColor);
        /**< Sets the color of the Group.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @param[in] nColor The color to set for the Group in IIRRGGBB, where I,R,G,B is
         *              Intensity,Red,Green,Blue. */

    u32 (* GetColor)(LTDeviceUnit hGroup);
        /**< Reports the currently displaying color of the LED Group in IIRRGGBB, where I,R,G,B is Intensity,Red,Green,Blue.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @return the currently displaying color of the LED Group in IIRRGGBB, where I,R,G,B is Intensity,Red,Green,Blue. */

    u32 (* GetNumberOfDigits)(LTDeviceUnit hGroup);
        /**< Reports the number of digits available in the Group.
         *   @param[in] hGroup The Device Unit Handle of LED Group.
         *   @return the number of digits available in the Group. */
};

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_LED_LTDEVICELED_H

/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  02-Jan-21   constantine created
 */
