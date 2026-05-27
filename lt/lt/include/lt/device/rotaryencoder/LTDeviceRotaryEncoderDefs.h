/*******************************************************************************
 * <lt/device/rotaryencoder/LTDeviceRotaryEncoderDefs.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 * @file LTDeviceRotaryEncoderDefs.h header for RotaryEncoder types
 * @brief provide types and constants for RotaryEncoder
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_ROTARYENCODER_LTDEVICEROTARYENCODERDEFS_H
#define LT_INCLUDE_LT_DEVICE_ROTARYENCODER_LTDEVICEROTARYENCODERDEFS_H

/*******************************************************************************
 * #defines for LT RotaryEncoder */

// Direction of the rotation. Don't change the values as they're used for updating
typedef enum {
    kRotationDirectionNone                      = 0,
        /**< No rotation */
    kRotationDirectionCW                        = 1,
        /**< Clockwise rotation */
    kRotationDirectionCCW                       = -1
        /**< Counter-clockwise rotation */
} RotaryEncoderRotationDirection;

// Position info (needs to be 32-bits so it can be used as a volatile atomic operation))
typedef struct {
    s16 nPosition;
        /**< The current position value */
    RotaryEncoderRotationDirection direction    : 2;
        /**< The direction of the rotation */
    u16 nRotationSpeed                          : 14;   // (positions / sec)
        /**< The speed of the rotation in positions per milliseconds */
} RotaryEncoderPosition;

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_ROTARYENCODER_LTDEVICEROTARYENCODERDEFS_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  10-Nov-21   vitellius   created
 */
