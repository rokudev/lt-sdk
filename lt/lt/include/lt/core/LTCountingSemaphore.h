/******************************************************************************
 * <lt/core/LTCountingSemaphore.h>     LT ISR-Safe Counting Semaphore
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltcore_bsp_counting_semaphore LTCountingSemaphore
 * @ingroup ltcore_bsp
 * @{
 *
 * @brief  LTCountingSemaphore is a counting semaphore built on top of LTMonitor.
 *         It provides ISR-safe Signal and TryWait operations, and a blocking
 *         Wait for thread contexts.
 *
 *         This consolidates the counting semaphore pattern that was previously
 *         duplicated across multiple vendor OS adaptation layers (WiFi platform,
 *         BLE OSIF, ESP32 OS adapter), each independently emulating counting
 *         semaphores with LTMonitor + atomic counter.
 *
 *         Usage: <pre>
 *
 *         LTCountingSemaphore *sem = lt_createobject(LTCountingSemaphore);
 *         sem->API->Init(sem, 10, 0);  // max_count=10, init_count=0
 *
 *         // Thread context: blocking wait
 *         if (sem->API->Wait(sem, LTTime_Milliseconds(1000))) {
 *             // acquired (count was decremented)
 *         }
 *
 *         // Thread or ISR context: signal
 *         sem->API->Signal(sem);  // increments count, wakes waiter
 *
 *         // ISR context: non-blocking try
 *         if (sem->API->TryWait(sem)) {
 *             // acquired without blocking
 *         }
 *
 *         lt_destroyobject(sem); </pre>
 *
 * @note   Only one thread may Wait() at a time (inherited from LTMonitor).
 *         Signal() and TryWait() may be called from any context including ISRs.
 */

#ifndef ROKU_LT_INCLUDE_LT_CORE_BSP_LTCOUNTINGSEMAPHORE_H
#define ROKU_LT_INCLUDE_LT_CORE_BSP_LTCOUNTINGSEMAPHORE_H

#include <lt/LTObject.h>
#include <lt/core/LTTime.h>

LT_EXTERN_C_BEGIN

/*________________________
 / LTCountingSemaphore */
typedef_LTObject(LTCountingSemaphore, 1) {

    void (* Init)(LTCountingSemaphore *sem, u32 maxCount, u32 initCount);
        /**< @brief Initializes the counting semaphore after construction.
         *
         * Must be called exactly once after lt_createobject() and before any
         * call to Wait, TryWait, Signal, or GetCount.
         *
         * @param[in] sem       The semaphore to initialize.
         * @param[in] maxCount  Upper bound on the count.  Signal() is a no-op
         *                      when the count has reached maxCount.
         * @param[in] initCount Initial count value.  Must be <= maxCount.
         */

    bool (* Wait)(LTCountingSemaphore *sem, LTTime timeout);
        /**< @brief Decrements the count, blocking if the count is zero.
         *
         * If the count is greater than zero, atomically decrements it and
         * returns true immediately (fast path, no blocking).
         *
         * If the count is zero, blocks the calling thread until either:
         *   - another thread or ISR calls Signal(), making the count positive, or
         *   - the timeout expires.
         *
         * @param[in] sem     The semaphore.
         * @param[in] timeout Maximum time to wait.  Use LTTime_Infinite() to
         *                    wait indefinitely, LTTime_Zero() for a non-blocking
         *                    attempt from thread context.
         * @return true if the count was decremented, false if the timeout expired.
         *
         * @note Thread-only.  It is illegal to call Wait() from an ISR.
         * @note Only one thread may Wait() at a time (inherited from LTMonitor).
         */

    bool (* TryWait)(LTCountingSemaphore *sem) LT_ISR_SAFE;
        /**< @brief Non-blocking decrement of the count.
         *
         * If the count is greater than zero, atomically decrements it and
         * returns true.  Otherwise returns false immediately.
         *
         * @param[in] sem The semaphore.
         * @return true if the count was decremented, false if it was zero.
         *
         * @note ISR-safe.  Uses a CAS loop with no monitor interaction.
         */

    void (* Signal)(LTCountingSemaphore *sem) LT_ISR_SAFE;
        /**< @brief Increments the count and wakes a blocked waiter, if any.
         *
         * Atomically increments the count (up to maxCount) and performs a
         * naked Notify on the internal monitor to wake a waiting thread.
         *
         * If the count is already at maxCount, the call is a no-op.
         *
         * @param[in] sem The semaphore.
         *
         * @note ISR-safe.  Uses atomic increment and naked monitor Notify
         *       (no Enter/Exit) per the LTMonitor ISR contract.
         */

    void (* SignalFromThread)(LTCountingSemaphore *sem);
        /**< @brief Thread-context signal with deterministic wake-up.
         *
         * Like Signal(), increments the count (up to maxCount) and wakes
         * a blocked waiter.  Unlike Signal(), uses Enter/Notify/Exit on
         * the internal monitor, which guarantees that the Notify cannot be
         * lost between the waiter's predicate check and its Wait() call.
         *
         * Use SignalFromThread() when the caller is always in thread context
         * and deterministic (non-delayed) wake-up is required.  Use Signal()
         * when the caller may be in ISR context.
         *
         * @param[in] sem The semaphore.
         *
         * @note Thread-only.  It is illegal to call SignalFromThread() from
         *       an ISR — use Signal() instead.
         */

    u32 (* GetCount)(LTCountingSemaphore *sem) LT_ISR_SAFE;
        /**< @brief Returns the current count.
         *
         * @param[in] sem The semaphore.
         * @return Current count value.
         *
         * @note ISR-safe (atomic load).
         */

} LTOBJECT_API;

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_BSP_LTCOUNTINGSEMAPHORE_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  26-Mar-26   nerva       created
 */
