/*******************************************************************************
 * <lt/device/captouch/LTDeviceCapTouchDefs.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 *  @file LTDeviceCapTouchDefs.h header for LTDeviceTouch Defs
 */

/**
 * @defgroup ltdevice_captouch LTDeviceCapTouchDefs
 * @ingroup ltdevice
 * @{
 *
 * @brief LTDeficeCapTouch definitions.
 *
 */

#ifndef LT_INCLUDE_LT_DEVICE_CAPTOUCH_LTDEVICECAPTOUCHDEFS_H
#define LT_INCLUDE_LT_DEVICE_CAPTOUCH_LTDEVICECAPTOUCHDEFS_H

LT_EXTERN_C_BEGIN

/* ________________________
   LTDeviceCapTouch_Mode */
typedef enum LTDeviceCapTouch_Mode {
    kLTDeviceCapTouch_Mode_Unset,       /**< The cap touch device is unset.  This is the initial mode the cap touch device is created in. */
    kLTDeviceCapTouch_Mode_LowPower,    /**< Low power mode - reduced sensitivity for sleep current */
    kLTDeviceCapTouch_Mode_Normal,      /**< Normal operating mode - full sensitivity */
} LTDeviceCapTouch_Mode;
    /**< LTDeviceCapTouch operating modes */

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_CAPTOUCH_LTDEVICECAPTOUCHDEFS_H */

/** @} */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  26-Mar-26   augustus    created
 */
