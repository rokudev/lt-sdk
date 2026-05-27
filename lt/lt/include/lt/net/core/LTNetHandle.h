/************************************************************************
 * LTNetHandle implementation
 * 
 * This file implements the LTNet Handle APIs
 * 
 * platforms/si91x/source/si91x/driver/wireless/include/LTNetHandle.h
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTNETHANDLE_H
#define ROKU_LT_INCLUDE_LT_CORE_LTNETHANDLE_H
#include <lt/LTObject.h>
LT_EXTERN_C_BEGIN
/***********************
 * LTNetHandle Object
***********************/

/**
 * @brief Callback function invoked when `Notify` is called on the handle.
 *        Note that although `Notify` is triggered from the ISR context, this poll callback is not. 
 *        The `LTNetHandlePollCb` is executed from an internal thread within `LTNetHandle`. The callback
 *        runs in a loop until the number of packets returned is less than the budget. If the amount of work 
 *        done is smaller than the budget, the callback should re-enable interrupts before returning.
 *
 * @param data - Data passed to the poll callback.
 * @param budget - The maximum number of packets that the poll callback is allowed to process in one invocation, 
 *                 as set by `LTNetHandle`.
 * @return workdone - The number of packets processed by the poll callback.
 */
typedef int (*LTNetHandlePollCb)(void *data, int budget);

typedef_LTObject(LTNetHandle, 1) {
    /**
     * @brief Add the poll callback to the handle
     * @param handle - handle to which poll callback to be added
     * @param pollCb - poll callback function
     * @param data - data to be passed to the poll callback
     * @return true if successful, false otherwise
     */
    bool   (* Add) (LTNetHandle *handle, u32 priority, LTNetHandlePollCb pollCb, void *data);

    /**
     * @brief Remove the poll callback from the handle
     * @param handle - handle from which poll callback to be removed
     * @param pollCb - poll callback function
     * @return true if successful, false otherwise
     */
    bool   (* Remove) (LTNetHandle *handle, LTNetHandlePollCb pollCb);

    /**
     * @brief Enable the handle
     * @param handle - handle to be enabled
     * @return void
     */
    void   (* Enable) (LTNetHandle *handle);

    /**
     * @brief Disable the handle
     * @param handle - handle to be disabled
     * @return void
     */
    void   (* Disable) (LTNetHandle *handle);

    /**
     * @brief Notify the handle. 
     *        Important:
     *        ***************************************************************
     *        * This function should be called from ISR
     *        * The interrupts houls be disabled before calling the notify.
     *        ***************************************************************
     * @param handle - handle to be notified
     * @return void
     */
    void   (* Notify) (LTNetHandle *handle) LT_ISR_SAFE;
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_LTNETHANDLE_H */

/************************************************************************
 *  LOG
 ************************************************************************
 *  28-Aug-24   galba       created
 ************************************************************************/