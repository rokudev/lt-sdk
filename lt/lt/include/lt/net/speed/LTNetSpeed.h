/*******************************************************************************
 *
 * LTNetSpeed: Network Speed Test
 * ------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#ifndef LTNETSPEED_H_1
#define LTNETSPEED_H_1

typedef void (LTNetSpeed_Proc)(u32 bps, void *eventData);
    /**<
     * @brief The function prototype for speed reply events
     *
     * Passed as an argument to LinkSpeed(), this function gets called when an speed
     * command is done.
     *
     * @param[in] bps: speed in bits/second
     * @param[in] eventData: user data
     */

typedef_LTLIBRARY_ROOT_INTERFACE(LTNetSpeed, 1) {

    u32 (*LinkSpeed)(u16             msDuration,
                     LTTransport     hTransport,
                     bool            synchronous,
                     LTNetSpeed_Proc eventFunc,
                     void           *eventData);
    /**<
     * @brief Measure (WiFi)link speed
     *
     * Send UDP packets on link at maximum speed to measure the speed of the first link
     * The packets does not require a reply and will quickly be dropped in the network
     *
     * Example:
     *   LinkSpeed(0, 0, NULL, NULL, 0) : A default speed test - result is logged to LTLOG_SERVER
     *
     * @param[in] msDuration: optional time duration in msec (default 1000 msec)
     * @param[in] hTransport: handle for an open transport or zero for default transport
     * @param[in] synchronous: make this function call synchronous
     * @param[in] eventFunc: optional speed reply event function
     * @param[in] eventData: optional speed reply event function data
     * @returns kbps speed if synchronous == true, otherwise 0
     */

} LTLIBRARY_INTERFACE;

#endif  // LTNETSPEED_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  17-Jun-25   maximian    created
 */
