/*******************************************************************************
 * <lt/device/watchdog/LTDeviceWatchdog.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltdevice_watchdog LTDeviceWatchdog
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for watchdog functions.
 *
 * Provides general control for enabling, disabling, resetting, and
 * setting the timeout interval of watchdogs.
 *
 * Also provides general system reset.
 *
 * LT Libraries use LTDeviceWatchdog to interact with the hardware
 * watchdog.  Only LTDeviceWatchdog interacts directly with LTDriverWatchdog.
 * The simple interface assumes that the SoC has no more than one watchdog
 * per running core, obviating the need for Device Units.
 */

#ifndef LT_INCLUDE_LT_DEVICE_WATCHDOG_LTDEVICEWATCHDOG_H
#define LT_INCLUDE_LT_DEVICE_WATCHDOG_LTDEVICEWATCHDOG_H

#include <lt/core/LTTime.h>
#include <lt/core/LTThread.h>
LT_EXTERN_C_BEGIN

typedef void (LTDeviceWatchdog_NotifyRebootEventProc)(void *clientData);
  /**< Event proc type to register for notification that an api requested reboot is imminent.
   *   @param clientData the clientData passed in when registering for the reboot notification event
   */

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceWatchdog, 1);
/** LTDeviceWatchdog Library ROOT INTERFACE */
struct LTDeviceWatchdog {

    INHERIT_DEVICE_LIBRARY_BASE

    bool (* ResetTimer)(void);
        /**< Resets a watchdog to its timeout interval.
         *   Restarts the timeout interval; the watchdog will not time out
         *   until the timeout interval passes again.
         *   @return true if the timer could be reset, false otherwise. */
    bool (* EnableTimer)(void);
        /**< Enables a watchdog.
         *   The watchdog will reset (see ResetTimer()) and begin counting down.
         *   @return true if the watchdog could be enabled, false otherwise. */
    bool (* DisableTimer)(void);
        /**< Disables a watchdog.
         *   The watchdog will cease counting down and will not time out.
         *   @return true if the watchdog could be disabled, false otherwise. */
    bool (* IsEnabled)(void);
        /**< Returns the current state of the watchdog.
         *   @return true if the watchdog is enabled, false otherwise. */
    bool (* SetTimeout)(LTTime timeout);
        /**< Sets a new interval for a watchdog.
         *   Once enabled, the watchdog will use the new timeout interval.
         *   @param[in] timeout The duration of the watchdog timeout
         *   @return true if the timeout was changed, false otherwise. */
    LTTime (* GetTimeout)(void);
        /**< Gets the current interval for the watchdog.
         *   @return the current watchdog timeout interval. */

    void (* WatchThread)(LTTime responseFidelity, bool bTerminationAllowed);
        /**< instructs the watchdog to watch the current thread
         *
         *   Call %WatchThread() from your thread to automatically have the watchdog ensure your
         *   thread is responsive within the responseFidelity duration.  For example if time t = 0
         *   and the response fidelity is 100ms, LTDeviceWatchdog will queue a task proc to your thread
         *   immediately and if execution doesn't take place within 100ms, the watchdog will trigger a reboot.
         *   If the execution occurs at t=25ms, the next task proc will not be queued until t=100ms, and it
         *   will be given until t=200ms to execute before a reboot is triggered.  If bTerminationAllowed is true
         *   the watchdog will not trigger a reboot if the thread terminates.  If it is false, it will trigger
         *   a reboot if the thread terminates.
         *
         *   In this manner clients are relieved from having to coordinate amongst themselves
         *   to properly tickle the shared watchdog resource manually at the appropriate coordinated
         *   interval(s).  Further, the creation of an intricate mechanism for handing out private-watchdogs or watchdog slots
         *   is avoided entirely, for while such mechanisms alleviate inter-client coordination, they do not
         *   provide relief from the need for manual private-watchdog tickling, nor relief from the ambiguity
         *   and uncertainty of when and how to do so in any particular thread circumstance, which typically leads
         *   to incomplete watchdog coverage as the knowledge of how and when tickling should occur is complex and
         *   oft ignored or overlooked.
         *
         *   In contrast to the uncertainty and ambiguity inherent in traditional watchdog tickling approaches, it is trivial
         *   for an author of a subsystem to determine and specify the response fidelity that their thread must exhibit.
         *   That, combined with the fact that clients need have no responsibility to manage and administer shared watchdog
         *   operation will lead to more and proper use of the watchdog and efficacy of its ability to properly execute its
         *   ultimate task of rebooting when it is accurate and appropriate rather than accidental or circumstantial.
         *
         *   @param responseFidelity the interval response fidelity time the thread must continuously meet to prevent watchdog reboot
         *   @param bTerminationAllowed whether or not the watchdog should consider termination of the thread to be a rebootable event
         *
         *   @see UnwatchThread()
         *
         *   @note Once %WatchThread is called, the watch parameters cannot be modified.  In order to modify its watch parameters,
         *         a thread must first call %UnwatchThread and then %WatchTread to set new parameters.
         *   @note When a thread terminates, it will automatically be unwatched.
         */

    void (* UnwatchThread)(void);
        /**< instructs the watchdog to stop watching the current thread
         *
         *   Call %UnwatchThread() to cause the watchdog to stop ensuring a previously specified
         *   response fidelity must be met.  Calling %UnwatchThread() on a thread that is not currently
         *   being watched has no effect.
         *
         *   @see WatchThread()
         */

    LTBootReason (* GetBootReason)(const char ** pReasonString);
        /**< Returns the boot reason.
         *   @return the LTBootReason (enum value). Returns
         *   kLTBootReason_Undefined if the boot reason could not be
         *   determined.
         *   @param[out] pReasonString Pointer to the chip vendor-specific
         *   string which will contain the vendor-determined boot reason
         *   string. If NULL, pReasonString is ignored and only the
         *   LTBootReason is returned. */

    void (* GetBootData)(const u8 ** pBootData, u32 * nBootDataLength);
        /**< Get a reference to application-specific data stored by the bootloader
         *   @param[out] pBootData Pointer to constant data stored by the bootloader
         *   @param[out] nBootDataLength Length of constant data stored by the bootloader */

    void (* Reboot)(void);
        /**< Reboots the system after notifying registered clients that the reboot is imminent.
         *
         *  Initiates an asynchronous Reboot operation which first notifies registered clients
         *  that a reboot will take place and then reboots the system.  This function may
         *  return control to the caller before the reboot takes place; the caller should
         *  cease further processing at that point and remain idle for reboot.
         *
         * @note This is a software initiated reboot that is distinct from the error timeout
         *       watchdog reboot.  Error timeout watchdog reboots do not give notification of
         *       immininent reboot.  Note also that reboots due to exceptions, including LT_ASSERTs
         *       also do not give notice of immininent reboot.  Only reboots initiated by this
         *       function provide reboot notification.
         *
         *   @see OnRebootNotify(), NoRebootNotify()
         */

    void (* OnRebootNotify)(LTDeviceWatchdog_NotifyRebootEventProc *notifyRebootEventProc, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void *clientData);
        /**< Registers a callback procedure for notification back to the calling thread when an api requested reboot is about to occur.
         *   @param pEventProc the event procedure to register for reboot notification
         *   @param pClientDataReleaseProc the client data release proc that will be called when the event is unregistered or NULL if no release prpc
         *   @param pClientData the client data that will be passed back to pEventProc
         */

    void (* NoRebootNotify)(LTDeviceWatchdog_NotifyRebootEventProc *notifyRebootEventProc);
        /**< Unregisters the current thread's previously registered notify reboot event callback procedure
         *   @param pEventProc the event procedure previously registered by the thread calling this procedure
         */
};

#ifndef DOXY_SKIP // [

/* This interface is to be used only by LTDeviceWatchdog. */
typedef_LTLIBRARY_INTERFACE(ILTDriverWatchdog, 1) {
    void (* Reboot)(void);
        /**< reboots the system. */
    bool (* ResetTimer)(void);
        /**< resets a watchdog to its timeout interval.
         *   Restarts the timeout interval; the watchdog will not time out
         *   until the timeout interval passes again.
         *   @return true if the timer could be reset, false otherwise. */
    bool (* EnableTimer)(void);
        /**< enables a watchdog.
         *   The watchdog will reset (see %Reset()) and begin counting down.
         *   @return true if the watchdog could be enabled, false otherwise. */
    bool (* DisableTimer)(void);
        /**< disables a watchdog
         *   The watchdog will cease counting down and will not time out.
         *   @return true if the watchdog could be disabled, false otherwise. */
    bool (* IsEnabled)(void);
        /**< gives the current state of the watchdog
         *   @return true if the watchdog is enabled, false otherwise.*/
    bool (* SetTimeout)(LTTime timeout);
        /**< sets a new interval for a watchdog.
         *   Once enabled, the watchdog will use the new timeout interval.
         *   @param[in] timeout the duration of the watchdog timeout or LTTime_Zero() to immediately
         *              reboot once the timer is enabled with EnableTimer()
         */
    LTBootReason (* GetBootReason)(const char ** pReasonString);
        /**< Returns the boot reason.
         *   @return the LTBootReason (enum value). Returns
         *   kLTBootReason_Undefined if the boot reason could not be
         *   determined.
         *   @param[out] pReasonString Pointer to the chip vendor-specific
         *   string which will contain the vendor-determined boot reason
         *   string. If NULL, pReasonString is ignored and only the
         *   LTBootReason is returned. */
    void (* GetBootData)(const u8 ** pBootData, u32 * nBootDataLength);
        /**< Get a reference to application-specific data stored by the bootloader
         *   @param[out] pBootData Pointer to constant data stored by the bootloader
         *   @param[out] nBootDataLength Length of constant data stored by the bootloader */
} LTLIBRARY_INTERFACE;

#endif // DOXY_SKIP  ]

/** @} */

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_WATCHDOG_LTDEVICEWATCHDOG_H
