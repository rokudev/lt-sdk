/******************************************************************************
 * <lt/core/LTMonitor.h>               LT ISR-Notify-Safe Device Driver Monitor
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltcore_bsp_monitor LTMonitor
 * @ingroup ltcore_bsp
 * @{
 *
 * @brief  LTMonitor is a lightweight monitor used for low level ISR->Thread
 *         wakeup or low-level thread->thread wakeup, primarily intended for
 *         use in porting vendor hardware device drivers.  Use of LTMonitor
 *         precludes the use of normal LT thread QueueTaskProc operation,
 *         and the use of any asynchronous operations such as the notification
 *         capabilities of LTEvent and the timer capabilities of LTThread.
 *
 *         Because the use of LTMonitor precludes asynchronous operation,
 *         LTMonitor is typically <b>only<b> used within a thread's
 *         ThreadInitProc as follows: <pre>
 *
 *         static LTMonitor *s_pMonitor;
 *         static bool       s_bRunDriver;
 *
 *         bool DriverTopHalf_ThreadInit(void) {
 *             s_pMonitor = lt_createobject(LTMonitor);
 *             s_bRunDriver = true;
 *             EnableDriverISR();
 *             while (s_bRunDriver) {
 *                 s_pMonitor->API->Enter(s_pMonitor);                      / * equivalent to pthread_lock_mutex(&condMutex);    * /
 *                 s_pMonitor->API->Wait(s_pMonitor, LTTime_Infinite());    / * equivalent to pthread_cond_wait(&condCondition); * /
 *                 ProcessDeviceData();
 *                 s_pMonitor->API->Exit(s_pMonitor);                       / * equivalent to pthread_lock_mutex(&condMutex);    * /
 *             }
 *             DisableDriverISR();
 *             lt_destroyobject(s_pMonitor);
 *             s_pMonitor = false;
 *             return false;                                                / * returning false from ThreadInit exits thread     * /
 *        }
 *
 *        void DriverISR(void) {
 *             ReadDeviceData();
 *             s_pMonitor->API->Notify(s_pMonitor);                         / * must do naked notify from ISR, no Enter/Exit      * /
 *        } </pre>
 *
 * ________________________________________________________________________
 * NOTE: In all cases it is preferable, when possible, to rewrite the above
 *       to use QueueTaskProcIfRequired. This may involve minor refactoring
 *       of vendor driver code (see below).  LTMonitor is provided as a
 *       quick backstop to get vendor drivers with legacy os abstractions
 *       to shippable quality when vendor source code is not available for
 *       refactoring or product schedules preclude refactoring from being
 *       prioritized.
 *
 *       ______________________________________________________________
 *       EXAMPLE: rewriting above example using QueueTaskProcIfRequired
 *
 *         LTOThread *s_driverThread = NULL; / * created elsewhere * /
 *
 *         void Driver_TaskProc(void *clientData) { LT_UNUSED(clientData);
 *             ProcessDeviceData();
 *         }
 *
 *         void DriverISR(void) {
 *             ReadDeviceData();
 *             s_thread->API->QueueTaskProcIfRequired(s_thread, Driver_TaskProc, NULL, NULL);
 *         }
 */

#ifndef ROKU_LT_INCLUDE_LT_CORE_BSP_LTMONITOR_H
#define ROKU_LT_INCLUDE_LT_CORE_BSP_LTMONITOR_H

#include <lt/LTObject.h>
#include <lt/core/LTTime.h>

LT_EXTERN_C_BEGIN

/*_____________
 / LTMonitor */
typedef_LTObject(LTMonitor, 1) {

    void (* Enter)(LTMonitor *monitor);
        /**< @brief Enters the monitor (obtains monitor lock).
         *
         * Call Enter() to enter the monitor.  Enter() does not nest.
         * It is illegal to call Enter() from a thread that has already
         * entered the monitor.  Enter() will block the thread until no other threads
         * have entered the monitor.
         *
         * @param[in] monitor The monitor to enter.
         */

    void (* Exit)(LTMonitor *monitor);
        /**< @brief Exits the monitor (releases monitor lock).
         *
         * Call Exit() to exit the monitor.
         *
         * @param[in] monitor The monitor to exit.
         */

    bool (* Wait)(LTMonitor *monitor, LTTime timeout);
        /**< @brief Waits for monitor notification.
         *
         * Wait blocks the thread until the monitor is notified by another thread or ISR,
         * or the timeout expires, whichever comes first.
         *
         * @param[in] monitor the monitor to wait on
         * @return true if the monitor was notified, false if the timeout expired
         *
         * @note It is illegal to call Wait() without first having
         *       entered the monitor.  When the caller
         *       properly holds the monitor lock, Wait() atomically
         *       releases the lock & puts the thread into a wait state.  When the
         *       thread is awoken, the lock is atomically obtained and a return
         *       value of true indicates that the thread was woken up as a result
         *       of a Notify() and a return value of false means the
         *       wait timeout expired without a Notify().
         *       Monitors are typically associated with a boolean predicate.
         *       When a waiting thread ultimately resumes, it should immediately
         *       check the boolean predicate because even when Wait()
         *       returns true, the waiting thread may have been subject to aspurious wakeup.
         *       See: https://en.wikipedia.org/wiki/Spurious_wakeup
         *
         * @note Wait() can only be called from threads that have entered the monitor.
         *       Wait() can not be called from ISRs.
         * @note Only one thread can Wait() at a time.  It is illegal for a thread to call Wait()
         *       while another thread is waiting.
         */

    void (* Notify)(LTMonitor *monitor) LT_ISR_SAFE;
        /**< @brief Notifies the monitor.
         *
         * Wakes up the thread that is waiting on the monitor, if any.
         *
         * @param monitor the monitor to Notify
         *
         * @note In order for a notifying thread to guarantee synchronization of monitored data with the waiting
         *       thread, the notifying thread should be inside the monitor when calling Notify():<pre>
         *           monitor->Enter(monitor);
         *           if (NotificationIsDesired()) monitor->Notify(monitor);
         *           monitor->Exit(monitor);
         *       </pre>
         *
         * @note In order for an ISR to notify, it must perform a naked-notify (no enter/exit), since it can't block:<pre>
         *           monitor->Notify(monitor);
         *       </pre>
         *
         * @note When interrupts are disabled, Notify() may be called from thread contexts with the following caveats:
         *         (1) LTCore's interrupt Disable() and Enable() methods shall encapsulate the critical section(s) where
         *               Notify() is called.
         *         (2) As with any function lacking the LT_ISR_SAFE marker, monitor->Enter(), monitor->Exit() and monitor->Wait()
         *               shall NOT be called when interrupts are disabled.
         */

} LTOBJECT_API;

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_BSP_LTMONITOR_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Apr-24   augustus    created
 */
