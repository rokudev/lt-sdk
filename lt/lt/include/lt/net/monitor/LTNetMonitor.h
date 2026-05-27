/*******************************************************************************
 *
 * LTNetMonitor.h - Network Monitor Library
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#ifndef LTNETMONITOR_H_1
#define LTNETMONITOR_H_1

#include <lt/LTTypes.h>
#include <lt/core/LTThread.h>

LT_EXTERN_C_BEGIN

/**
 * Network states indicated by the LTNetMonitor_StatusProc callback.
 */
typedef enum LTNetMonitor_Status {
    // Note: numerically spaced in case we want to add more states in-between
    kLTNetMonitor_Status_Unknown     = 0,
    kLTNetMonitor_Status_NetworkDown = 0x10,
    kLTNetMonitor_Status_NetworkUp   = 0x20,
    kLTNetMonitor_Status_NoIp        = 0x30,
    kLTNetMonitor_Status_DNSTimeout  = 0x40,
    kLTNetMonitor_Status_ResetMetrics = 0x50,
    kLTNetMonitor_Status_ShowMetrics  = 0x51,
} LTNetMonitor_Status;

/**
 * The callback function used for network status changes.
 *
 * Call OnStatusChange to register this callback.
 * Call NoStatusChange to unregister this callback.
 *
 * @param[in] status: The network system status.
 * @param[in] clientData: The client data supplied to OnStatusChange.
 */
typedef void (LTNetMonitor_StatusProc)(LTNetMonitor_Status status, void *clientData);

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTNetMonitor, 1);
struct LTNetMonitorApi {
    INHERIT_LIBRARY_BASE
    bool (*IsUp)(void);
        /**<
         * @brief Return true if network is up.
         *
         * This function can be used to check the connection state of the network. Both the link layer
         * and IP layer must be operational for it to return true.
         *
         * Note that you can also use OnStatusChange to be notified via a callback whenever the connection
         * state of the network changes.
         *
         * @return true if network is up, otherwise false
         */

    void (*OnStatusChange)(LTNetMonitor_StatusProc statusProc, LTThread_ClientDataReleaseProc *releaseProc, void *clientData);
        /**<
         * @brief Specify a callback for network status change notifications.
         *
         * See LTNetMonitor_StatusProc for the format of the callback.
         *
         * @param[in] statusProc will be called each time the network status changes
         * @param[in] releaseProc is called when the client data is released (when NoStatusChange is called)
         * @param[in] clientData is a data pointer passed to the callback
         */

    void (*NoStatusChange)(LTNetMonitor_StatusProc statusProc);
        /**<
         * @brief Disable a prior OnStatusChange callback.
         *
         * @param[in] statusProc that should no longer be called.
         * This must be the same as the function passed to OnStatusChange.
         */
};

LT_EXTERN_C_END

#endif // LTNETMONITOR_H
