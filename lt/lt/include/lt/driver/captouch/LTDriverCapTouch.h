/*******************************************************************************
 * <lt/driver/captouch/LTDriverCapTouch.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef LT_INCLUDE_LT_DRIVER_CAPTOUCH_LTDRIVERCAPTOUCH_H
#define LT_INCLUDE_LT_DRIVER_CAPTOUCH_LTDRIVERCAPTOUCH_H

#include <lt/device/captouch/LTDeviceCapTouchDefs.h>

LT_EXTERN_C_BEGIN;

/* _________________________
   LTDriverCapTouch types */
typedef void (LTDriverCapTouch_MotionProc)(void *pClientData) LT_ISR_SAFE;
 /**< called in ISR context whenever cap touch motion is detected
  *
  * @param capTouchDriver - the driver instance notifying the motion
  * @param pClientData - the clientData passed in to SetMode
  */

typedef LTCapTouch_ResultProc LTDriverCapTouch_ResultProc;
 /**< called when captouch initialization completes (success or failure)
  *
  * @param success - true if initialization succeeded, false if failed (doNotResuscitate set)
  * @param attemptCount - number of attempts required to initialize (or final count on failure)
  * @param pClientData - the clientData passed in to Initialize
  */

/* _______________________
   LTDriverCapTouch API */
typedef_LTObject(LTDriverCapTouch, 1) {

    bool (*Initialize)(LTDriverCapTouch *capTouchDriver, LTDeviceCapTouch_Mode mode, LTThread hNotificationThread, LTDriverCapTouch_MotionProc *pMotionProc, void *pMotionClientData, LTDriverCapTouch_ResultProc *pResultProc, void *pResultProcData);
        /**< sets the current operating mode.
         *
         *  @param[in]  capTouchDriver the cap touch driver on which to set the mode.
         *  @param[in]  mode the mode to set
         *  @param[in]  hNotificationThread the thread to queue motion notifications on
         *  @param[in]  pMotionProc the motion proc to be called when motion is detected
         *  @param[in]  pMotionClientData the client data passed back to the motion callback
         *  @param[in]  pResultProc the result proc to be called when initialization completes (success or failure)
         *  @param[in]  pResultProcData the client data passed back to the result callback
         *
         *  @note When the driver is created it must be created in the kLTDeviceCapTouch_Mode_Disabled mode
         */

    void (*Enable)(LTDriverCapTouch *capTouchDriver, bool bEnable);

    bool (*IsCapTouchTriggerActive)(LTDriverCapTouch *capTouchDriver);

    LTDeviceCapTouch_Mode (*GetMode)(LTDriverCapTouch *capTouchDriver);

    bool (*ResetTest)(LTDriverCapTouch *capTouchDriver);

    bool (*IsDoNotResuscitate)(LTDriverCapTouch *capTouchDriver);

} LTOBJECT_API;

LT_EXTERN_C_END;

#endif /* #ifndef LT_INCLUDE_LT_DRIVER_CAPTOUCH_LTDRIVERCAPTOUCH_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  24-Jan-25   augustus    created
 */
