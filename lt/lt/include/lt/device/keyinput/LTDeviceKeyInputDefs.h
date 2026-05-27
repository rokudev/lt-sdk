/*******************************************************************************
 * <lt/device/keyinput/LTDeviceKeyInputDefs.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 * @file LTDeviceKeyInputDefs.h header for logical keypress values
 * @brief provide logical values for remote-control keypress and other
 *        key-press events
 *
 * LTDeviceKeyInputValue defines the set of logical key values available
 * to LT code.  When a keypress is detected (e.g., by an implementation of
 * LTDriverKeyInput), the LTDeviceKeyInput implementation is notified via
 * a message.  The 32-bit m_pMessageData contains the logical value
 * of the keypress, with bit 31 (the MSB) set to indicate a press, and
 * cleared to indicate a release.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_KEYINPUT_LTDEVICEKEYINPUTDEFS_H
#define LT_INCLUDE_LT_DEVICE_KEYINPUT_LTDEVICEKEYINPUTDEFS_H

/*******************************************************************************
 * #defines for LT keystrokes */

enum LTDeviceKeyInputValueBasis {
    kFirstASCII     = 0x00,
    kLastASCII      = 0xFF,
    kFirstUnicode   = 0x0100,
    kLastUnicode    = 0xFFFF,
    kLTKeyBase      = 0x10000,
    kLTKeyPressed   = 0x80000000
};

#ifndef DOXY_SKIP
/* Generate LTDeviceKeyInputValue values: */
#define LTDEVICEKEY(n) (kLTKeyBase + n)
/* Standard naming scheme for values: */
#define LTKEYINPUTVALUENAME(name) kLTKey ## name
/* Extract names and values from LTDeviceKeyInputDefs.txt: */
#define LTKEYDEF(a, b) LTKEYINPUTVALUENAME(a) = LTDEVICEKEY(b),
#endif

typedef enum {
    #include <lt/device/keyinput/LTDeviceKeyInputDefs.txt>
    kLTKeyInvalid                   = 0xFFFFFFFF
    /* kLTKeyInvalid must be last - place no additional key values */
} LTDeviceKeyInputValue;
/**< Key values */

#undef LTKEYDEF
#undef LTDEVICEKEY

typedef struct {
    bool                    press;  /* = false */
    /**< Indicates that the key was pressed                                   */
    bool                    repeat; /* = false */
    /**< Indicates that the key press was repeated (not currently supported)  */
    LTDeviceKeyInputValue   key;    /* = kLTKeyUnknown */
    /**< The key that was pressed                                           */
} LTDeviceKeyInputEvent;
/**< The key input struct that is passed to clients */

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_KEYINPUT_LTDEVICEKEYINPUTDEFS_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  20-Nov-19   constantine created
 *  05-Jan-20   constantine modified for Doxymentation
 *  24-Jan-20   constantine move value definitions to LTDeviceKeyInputDefs.txt
 *  14-Sep-21   constantine Convert KeyInput to redux LT C interface
 *  04-Nov-21   vitellius   Added typedefs so enum and struct are not required when declaring a variable
 */
